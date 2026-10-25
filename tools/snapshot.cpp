// Renders the editor to a PNG without a host, so a change to the front can be
// looked at straight from the build. A development tool, not part of the product.
//
//   massif_snapshot out.png [scale] [browser|menu]   with the bar's preset browser or its menu open
//   massif_snapshot --selftest       the plugin as a host drives it: blocks, twice the rate, bypass
//   massif_snapshot --switches       the window toggles under the pointer: clicks, drags, the wheel
#include "../Source/PluginEditor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
/** A tone through the whole plugin, in blocks larger than it was prepared for;
    what comes out at that frequency on each side, in dB against what went in. */
std::pair<double, double> through (juce::AudioProcessor& processor, double hz, bool& finite)
{
    constexpr double sr = 48000.0;
    constexpr int block = 700;
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    double re[2] = {}, im[2] = {};
    int n = 0;

    for (int b = 0, t = 0; b < 60; ++b)
    {
        for (int i = 0; i < block; ++i)
            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, i, 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (t + i) / sr));

        processor.processBlock (buffer, midi);

        for (int i = 0; i < block; ++i)
        {
            const auto phase = 2.0 * juce::MathConstants<double>::pi * hz * (t + i) / sr;

            for (int c = 0; c < 2; ++c)
            {
                const auto v = buffer.getSample (c, i);
                finite = finite && std::isfinite (v);

                if (b >= 30)
                {
                    re[c] += v * std::cos (phase);
                    im[c] += v * std::sin (phase);
                }
            }

            n += b >= 30 ? 1 : 0;
        }

        t += block;
    }

    auto level = [&] (int c) { return 20.0 * std::log10 (2.0 * std::sqrt (re[c] * re[c] + im[c] * im[c]) / n / 0.1); };
    return { level (0), level (1) };
}

void set (juce::AudioProcessorValueTreeState& state, const char* id, float value)
{
    auto* parameter = state.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

int selfTest (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state)
{
    int failures = 0;
    auto check = [&] (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
        failures += ok ? 0 : 1;
    };

    bool finite = true;
    std::printf ("  latency: %d samples\n", processor.getLatencySamples());
    check (processor.getLatencySamples() > 0, "the delay of the doubled rate is told to the host");

    const auto flat = through (processor, 1000.0, finite);
    std::printf ("  flat, 1 kHz: %+.2f dB, %+.2f dB\n", flat.first, flat.second);
    check (std::abs (flat.first) < 0.1 && std::abs (flat.second) < 0.1, "flat, it comes out as it went in");

    // The left side's first band: +10 dB at 100 Hz.
    set (state, "l1freq", 4.0f);
    set (state, "l1mode", (float) msf::boost);
    set (state, "l1gain", 10.0f);
    const auto boosted = through (processor, 100.0, finite);
    std::printf ("  L1 +10 dB at 100 Hz: %+.2f dB left, %+.2f dB right\n", boosted.first, boosted.second);
    check (std::abs (boosted.first - 10.0) < 0.3, "the band is heard through the plugin");
    check (std::abs (boosted.second) < 0.1, "on its own side only (the link is the editor's)");

    set (state, "r4freq", 9.0f);
    set (state, "r4mode", (float) msf::cut);
    set (state, "r4gain", 6.0f);
    const auto top = through (processor, 16000.0, finite);
    std::printf ("  R4 -6 dB at 16 kHz: %+.2f dB left, %+.2f dB right\n", top.first, top.second);
    check (std::abs (top.first) < 0.5, "the top end comes back down from the doubled rate intact");
    check (std::abs (top.second + 6.0) < 0.5, "and the high band cuts where it says");

    set (state, "bypass", 1.0f);
    const auto bypassed = through (processor, 100.0, finite);
    std::printf ("  power off, 100 Hz: %+.2f dB\n", bypassed.first);
    check (std::abs (bypassed.first) < 0.1, "power off: as it came in");
    check (finite, "nothing blows up");
    set (state, "bypass", 0.0f);

    // The presets: every stock one names only what exists, and loads; A/B and the saved state keep both sides.
    auto& presets = static_cast<onirique::MassifProcessor&> (processor).presets;
    const auto problems = presets.checkStock();

    for (const auto& problem : problems)
        std::printf ("  %s\n", problem.toRawUTF8());

    check (problems.isEmpty(), "the stock presets name real parameters, within their ranges");

    presets.load ("Mastering Air");
    check (presets.getCurrentName() == "Mastering Air" && std::abs (state.getRawParameterValue ("r4gain")->load() - 2.5f) < 0.01f
               && std::abs (state.getRawParameterValue ("l1gain")->load() - 2.0f) < 0.01f,
           "a stock preset loads, on both sides");
    presets.switchToSlot (1);
    presets.load ("Telephone");
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    presets.switchToSlot (0);
    check (presets.getCurrentName() == "Mastering Air" && state.getRawParameterValue ("lhighpass")->load() < 0.5f, "A comes back as it was left");
    processor.setStateInformation (saved.getData(), (int) saved.getSize());
    check (presets.getSlot() == 1 && presets.getCurrentName() == "Telephone", "the saved state keeps the side being heard");
    presets.switchToSlot (0);
    check (presets.getCurrentName() == "Mastering Air", "and the other side");

    return failures == 0 ? 0 : 1;
}

/** The window toggles as a hand works them, straight on the editor's components. */
int switchTest (onirique::MassifProcessor& processor, juce::AudioProcessorValueTreeState& state)
{
    using namespace juce;
    int failures = 0;
    auto check = [&] (bool ok, const char* what)
    {
        std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
        failures += ok ? 0 : 1;
    };

    std::unique_ptr<AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (onirique::MassifEditor::kWidth, onirique::MassifEditor::kTotalHeight);

    // The component that takes the pointer for a control: the one lying on its box.
    auto find = [&] (const std::string& id) -> Component*
    {
        for (const auto& c : onirique::layout::controls())
            if (c.id == id)
                for (auto* front : editor->getChildren())
                    for (auto* child : front->getChildren())
                        if (child->getBounds() == Rectangle<float> (c.hit.width, c.hit.height).withCentre ({ c.hit.x, c.hit.y }).toNearestInt())
                            return child;

        return nullptr;
    };

    auto index = [&] (const char* id) { auto* p = state.getParameter (id); return roundToInt (p->convertFrom0to1 (p->getValue())); };
    auto source = Desktop::getInstance().getMainMouseSource();

    auto event = [&] (Component* c, Point<float> at, Point<float> from, bool dragged)
    {
        return MouseEvent (source, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, Time::getCurrentTime(), from, Time::getCurrentTime(), 1, dragged);
    };

    auto click = [&] (Component* c, bool top)
    {
        const Point<float> at ((float) c->getWidth() * 0.5f, (float) c->getHeight() * (top ? 0.25f : 0.75f));
        c->mouseDown (event (c, at, at, false));
        c->mouseUp (event (c, at, at, false));
    };

    auto drag = [&] (Component* c, float dy)
    {
        const Point<float> from ((float) c->getWidth() * 0.5f, (float) c->getHeight() * 0.5f);
        c->mouseDown (event (c, from, from, false));

        for (int i = 1; i <= 10; ++i)
            c->mouseDrag (event (c, from + Point<float> (0.0f, dy * (float) i / 10.0f), from, true));

        c->mouseUp (event (c, from + Point<float> (0.0f, dy), from, true));
    };

    auto* mode = find ("l1mode");
    auto* shelf = find ("l2shelf");
    check (mode != nullptr && shelf != nullptr, "each toggle has its half of the window");

    if (mode == nullptr || shelf == nullptr)
        return 1;

    std::printf ("  L1 BOOST / CUT takes %d x %d points\n", mode->getWidth(), mode->getHeight());
    check (mode->getWidth() >= 50 && mode->getHeight() >= 70, "a target as big as its words");

    click (mode, true);
    check (index ("l1mode") == msf::boost, "a click on BOOST: boost");
    check (index ("r1mode") == msf::boost, "and, linked, the right side with it");
    click (mode, true);
    check (index ("l1mode") == msf::out, "BOOST clicked again: out");
    click (mode, false);
    check (index ("l1mode") == msf::cut, "a click on CUT: cut");
    click (mode, true);
    check (index ("l1mode") == msf::boost, "from cut, a click on BOOST goes straight there");

    drag (mode, 40.0f);
    check (index ("l1mode") == msf::cut, "dragged down from boost: cut");
    drag (mode, -14.0f);
    check (index ("l1mode") == msf::out, "dragged up a step: out");

    mode->mouseWheelMove (event (mode, {}, {}, false), MouseWheelDetails { 0.0f, 0.5f, false, false, false });
    check (index ("l1mode") == msf::boost, "the wheel up a notch: boost");

    click (shelf, true);
    check (index ("l2shelf") == 1, "a click on SHELF: shelf");
    click (shelf, false);
    check (index ("l2shelf") == 0, "a click on BELL: bell");
    drag (shelf, -20.0f);
    check (index ("l2shelf") == 1, "dragged up: shelf");

    return failures == 0 ? 0 : 1;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshot.png");
    const auto scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    processor->setPlayConfigDetails (2, 2, 48000.0, 512);
    processor->prepareToPlay (48000.0, 512);

    auto& massif = static_cast<onirique::MassifProcessor&> (*processor);
    auto& state = massif.apvts;

    if (juce::String (argc > 1 ? argv[1] : "") == "--selftest")
        return selfTest (massif, state);

    if (juce::String (argc > 1 ? argv[1] : "") == "--switches")
        return switchTest (massif, state);

    // Something worth looking at: a few bands in, some out, a shelf, the filters.
    for (const auto* side : { "l", "r" })
    {
        auto at = [&] (const char* what, float value) { set (state, (juce::String (side) + what).toRawUTF8(), value); };

        at ("1gain", 10.0f);  at ("1bw", 0.5f);  at ("1freq", 3.0f);  at ("1mode", (float) msf::boost);
        at ("2gain", 6.0f);   at ("2bw", 0.6f);  at ("2freq", 4.0f);  at ("2mode", (float) msf::cut);
        at ("3gain", 7.0f);   at ("3bw", 0.35f); at ("3freq", 6.0f);  at ("3mode", (float) msf::cut);
        at ("4gain", 13.0f);  at ("4bw", 0.2f);  at ("4freq", 8.0f);  at ("4mode", (float) msf::boost); at ("4shelf", 1.0f);
        at ("gain", 0.0f);    at ("lowpass", 0.0f); at ("highpass", 0.0f);
    }

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (massif.createEditor());
        editor->setSize (onirique::MassifEditor::kWidth, onirique::MassifEditor::kTotalHeight);

        // This machine may hold no licence for the plugin: the activation page
        // is not what is being looked at.
        const juce::String panel (argc > 3 ? argv[3] : "");

        for (auto* child : editor->getChildren())
        {
            if (dynamic_cast<onirique::license::Overlay*> (child) != nullptr)
                child->setVisible (false);

            if (auto* bar = dynamic_cast<onirique::nuit::Bar*> (child))
            {
                if (panel == "browser")
                    bar->openBrowser();
                else if (panel == "menu")
                    bar->openMenu();
            }
        }

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

        file.deleteFile();
        juce::FileOutputStream stream (file);

        if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
            return 1;
    }

    std::printf ("%s\n", file.getFullPathName().toRawUTF8());
    return 0;
}

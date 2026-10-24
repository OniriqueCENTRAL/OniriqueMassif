#include "PluginEditor.h"
#include "ui/OniriqueWindowSize.h"

#include <BinaryData.h>

namespace onirique
{

using namespace theme;

namespace
{
    Image photo (const String& file)
    {
        int size = 0;
        const auto* data = BinaryData::getNamedResource (file.removeCharacters ("-").replaceCharacter ('.', '_').toRawUTF8(), size);
        return data != nullptr ? ImageCache::getFromMemory (data, size) : Image();
    }

    bool endsWith (const layout::Control& c, const char* what) { return String (c.id).endsWith (what); }
    bool isIn (const layout::Control& c) { return c.id == "lin" || c.id == "rin"; }

    /** What each control does, for a pointer that rests on it. In French, accents and all. */
    String describe (const layout::Control& c)
    {
        const auto band = c.id.size() > 1 && c.id[1] >= '1' && c.id[1] <= '4' ? c.id[1] - '1' : -1;
        std::string text;

        if (c.id == "link")
            text = "LINK : levier en haut, les deux côtés vont ensemble : tourner un réglage tourne son jumeau de l'autre côté. En l'enclenchant, le côté droit prend les réglages du gauche.";
        else if (c.id == "bypass")
            text = "Power : éteint, le signal ressort tel qu'il est entré, et les voyants s'éteignent.";
        else if (band >= 0 && endsWith (c, "gain"))
            text = band < 2 ? "Gain : de 0 à 20 dB, vers le haut ou vers le bas selon le levier BOOST / CUT de la fenêtre. Les deux bandes de gauche sont le réseau passif de l'original, tel quel : larges, rondes, et qui se resserrent quand on pousse."
                            : "Gain : de 0 à 20 dB, vers le haut ou vers le bas selon le levier BOOST / CUT de la fenêtre. Les deux bandes de droite sont pour moitié le réseau passif de l'original, pour moitié une bande en classe A discrète : des aigus ouverts qui ne durcissent pas.";
        else if (endsWith (c, "bw"))
            text = "Bandwidth : la largeur de la cloche, de très large à gauche à étroite à droite. En SHELF, la résonance de l'étagère : douce à gauche ; à droite, une bosse avant sa fréquence et un creux juste après.";
        else if (endsWith (c, "freq"))
            text = std::string ("Frequency : la fréquence de la bande, en onze positions, de ") + msf::kBandRanges[band] + " Hz.";
        else if (endsWith (c, "mode"))
            text = "BOOST / OUT / CUT : levier en haut, la bande relève (BOOST s'allume en vert) ; au milieu, elle est hors circuit ; en bas, elle creuse (CUT s'allume en rouge).";
        else if (endsWith (c, "shelf"))
            text = band < 2 ? "SHELF / BELL : en haut, une étagère qui relève ou creuse tout ce qui est sous la fréquence ; en bas, une cloche."
                            : "SHELF / BELL : en haut, une étagère qui relève ou creuse tout ce qui est au-dessus de la fréquence ; en bas, une cloche.";
        else if (isIn (c))
            text = "IN : allumé, l'égaliseur de ce côté est en service ; éteint, les bandes et les filtres sont à plat, mais le son passe toujours par les lampes.";
        else if (endsWith (c, "lowpass"))
            text = "Low Pass : coupe le haut, 24 dB par octave, de 18 à 6 kHz.";
        else if (endsWith (c, "highpass"))
            text = "High Pass : coupe le bas, 18 dB par octave, de 22 à 220 Hz.";
        else if (endsWith (c, "gain"))
            text = "Gain : le niveau de sortie de ce côté, de -6 à +4 dB ; plus il monte, plus les lampes et le fer colorent.";

        return String::fromUTF8 (text.c_str());
    }
}

MassifEditor::MassifEditor (MassifProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      frontOff (photo ("front-off.jpg")),
      frontLit (photo ("front-lit.jpg"))
{
    setLookAndFeel (&lookAndFeel);

    front.onPaint = [this] (Graphics& g) { paintFront (g); };
    front.onPaintOver = [this] (Graphics& g) { paintReadout (g); };
    front.setInterceptsMouseClicks (false, true);
    addAndMakeVisible (front);

    for (const auto* run : layout::kRuns)
        photos[run->photo] = photo (String (run->photo) + ".png");

    auto& state = processor.apvts;
    const auto controls = layout::controls();
    parts.reserve (controls.size());

    for (const auto& control : controls)
    {
        auto* parameter = state.getParameter (control.id);
        jassert (parameter != nullptr);

        const String twinId = String (control.id).startsWith ("l") ? "r" + String (control.id).substring (1)
                            : String (control.id).startsWith ("r") ? "l" + String (control.id).substring (1) : String();

        parts.push_back ({ control, nullptr, parameter, twinId.isEmpty() ? nullptr : state.getParameter (twinId),
                           control.run != nullptr ? &photos[control.run->photo] : nullptr });
    }

    for (auto& part : parts)
    {
        const auto& control = part.control;

        if (control.clicked)
        {
            auto sw = std::make_unique<Switch>();
            sw->onClick = [this, &part] (const MouseEvent& e) { click (part, e); };
            sw->setTooltip (describe (control));
            sw->setMouseCursor (MouseCursor::PointingHandCursor);
            part.component = sw.get();
            components.push_back (std::move (sw));
        }
        else
        {
            auto slider = std::make_unique<Slider>();
            slider->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            slider->setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
            slider->setRotaryParameters (MathConstants<float>::twoPi + degreesToRadians (control.fromDegrees),
                                         MathConstants<float>::twoPi + degreesToRadians (control.toDegrees), true);
            attachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, control.id, *slider));
            slider->setDoubleClickReturnValue (true, part.parameter->convertFrom0to1 (part.parameter->getDefaultValue()));
            slider->setTooltip (describe (control));

            // Only what the hand does is followed: not a project being loaded.
            auto* s = slider.get();
            slider->onValueChange = [this, &part, s] { if (s->isMouseOverOrDragging()) follow (part); };
            slider->onDragStart = [this, &part] { if (linked() && part.twin != nullptr) part.twin->beginChangeGesture(); };
            slider->onDragEnd = [this, &part] { if (linked() && part.twin != nullptr) part.twin->endChangeGesture(); };

            part.component = slider.get();
            components.push_back (std::move (slider));
        }

        part.component->setBounds (Rectangle<float> (control.size, control.size).withCentre ({ control.x, control.y }).toNearestInt());
        front.addAndMakeVisible (part.component);
    }

    // Each lamp lit with its glow, faded out towards the edge so that it lies
    // over the unlit front without a seam.
    for (int side = 0; side < 2; ++side)
    {
        const auto reach = layout::kLampGlowReach, scale = (float) layout::kPhotoScale;
        const auto source = Rectangle<float> (2.0f * reach, 2.0f * reach).withCentre ({ layout::kLampX[side], layout::kLampY }) * scale;
        auto& glow = lampGlow[side];
        glow = Image (Image::ARGB, roundToInt (source.getWidth()), roundToInt (source.getHeight()), true);
        const Image::BitmapData from (frontLit, Image::BitmapData::readOnly);
        Image::BitmapData to (glow, Image::BitmapData::writeOnly);

        for (int y = 0; y < glow.getHeight(); ++y)
            for (int x = 0; x < glow.getWidth(); ++x)
            {
                const auto sx = roundToInt (source.getX()) + x, sy = roundToInt (source.getY()) + y;

                if (sx < 0 || sy < 0 || sx >= frontLit.getWidth() || sy >= frontLit.getHeight())
                    continue;

                const auto d = Point<float> ((float) x + 0.5f, (float) y + 0.5f).getDistanceFrom ({ reach * scale, reach * scale }) / scale;
                const auto alpha = jlimit (0.0f, 1.0f, (reach - d) / 12.0f);
                to.setPixelColour (x, y, from.getPixelColour (sx, sy).withAlpha (alpha));
            }
    }

    licenseOverlay = std::make_unique<onirique::license::Overlay> ("MASSIF", processor.license);
    addAndMakeVisible (*licenseOverlay);

    // The front keeps its proportions; the window grows by zooming it.
    setResizable (true, true);
    setResizeLimits (1000, 1000 * kHeight / kWidth, 2400, 2400 * kHeight / kWidth);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    // taille d'ouverture : la règle commune des plugins ONIRIQUE (ui/OniriqueWindowSize.h)
    const auto opening = onirique::window::openingSize ({}, "ONIRIQUE MASSIF", { kWidth, kHeight },
                                                        { 1000, 1000 * kHeight / kWidth }, { 2400, 2400 * kHeight / kWidth });
    setSize (opening.x, opening.x * kHeight / kWidth);

    timerCallback();
    startTimerHz (30);
}

MassifEditor::~MassifEditor()
{
    onirique::window::rememberOnThisComputer ("ONIRIQUE MASSIF", getWidth(), getHeight());
    setLookAndFeel (nullptr);
}

void MassifEditor::resized()
{
    const auto scale = (float) getWidth() / (float) kWidth;

    front.setBounds (0, 0, kWidth, kHeight);
    front.setTransform (AffineTransform::scale (scale));

    // The licence badge sits at the foot of the middle, under the name.
    if (licenseOverlay != nullptr)
    {
        const auto& b = layout::kBadge;
        licenseOverlay->fit (getLocalBounds(), scale, { b.x, b.y, b.width, b.height }, Justification::centred);
    }
}

//==============================================================================
bool MassifEditor::linked() const
{
    return processor.apvts.getRawParameterValue ("link")->load() > 0.5f;
}

void MassifEditor::set (RangedAudioParameter& parameter, float normalised)
{
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost (normalised);
    parameter.endChangeGesture();
}

void MassifEditor::follow (const Part& part)
{
    if (following || part.twin == nullptr || ! linked())
        return;

    const ScopedValueSetter<bool> busy (following, true);
    part.twin->setValueNotifyingHost (part.parameter->getValue());
}

void MassifEditor::click (const Part& part, const MouseEvent& e)
{
    auto& parameter = *part.parameter;
    auto index = roundToInt (parameter.convertFrom0to1 (parameter.getValue()));

    // A three-way toggle goes up a step when clicked above its middle, down below it.
    if (part.control.positions == 3)
        index = jlimit (0, 2, index + (e.position.y < (float) part.component->getHeight() * 0.5f ? -1 : 1));
    else
        index = 1 - index;

    set (parameter, parameter.convertTo0to1 ((float) index));

    if (part.twin != nullptr && linked())
        set (*part.twin, parameter.getValue());

    // Linking: the right side takes the left side's settings.
    if (part.control.id == "link" && index == 1)
        for (const auto& p : parts)
            if (p.twin != nullptr && p.control.id[0] == 'l')
                set (*p.twin, p.parameter->getValue());
}

void MassifEditor::timerCallback()
{
    // The part in hand first, else the one under the pointer.
    const Part* now = nullptr;

    for (const auto& part : parts)
        if (part.component->isMouseButtonDown())
            now = &part;

    if (now == nullptr)
        for (const auto& part : parts)
            if (part.component->isMouseOver())
                now = &part;

    String text;

    if (now != nullptr)
    {
        const auto& id = now->control.id;
        const auto on = now->parameter->getValue() > 0.5f;
        text << now->control.name << "   ";

        if (id == "bypass")
            text << (on ? "OFF" : "ON");
        else if (id == "link" || isIn (now->control))
            text << (on ? "ON" : "OFF");
        else
            text << now->parameter->getCurrentValueAsText();
    }

    std::vector<float> values;

    for (const auto& part : parts)
        values.push_back (part.parameter->getValue());

    if (now != shown || text != readout || values != shownValues)
    {
        shown = now;
        readout = text;
        shownValues = values;
        front.repaint();
    }
}

//==============================================================================
void MassifEditor::paintFront (Graphics& g)
{
    using namespace layout;
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (frontOff, Rectangle<float> ((float) kWidth, (float) kHeight));

    const auto scale = (float) kPhotoScale;
    const auto powered = processor.apvts.getRawParameterValue ("bypass")->load() < 0.5f;

    // What is alight, laid over the front from its lit photograph.
    if (powered && frontLit.isValid())
    {
        for (int side = 0; side < 2; ++side)
        {
            for (int band = 0; band < 4; ++band)
            {
                const auto mode = roundToInt (processor.apvts.getRawParameterValue (bandId (side, band, "mode"))->load());

                if (mode == msf::out)
                    continue;

                const auto& light = mode == msf::boost ? kBoostLight : kCutLight;
                const auto area = Rectangle<float> (light.width, light.height).withCentre ({ kBandX[side][band] + light.dx, light.y });
                g.drawImage (frontLit.getClippedImage ((area * scale).toNearestInt()), area);
            }

            if (processor.apvts.getRawParameterValue (sideId (side, "in"))->load() > 0.5f)
                g.drawImage (lampGlow[side], Rectangle<float> (2.0f * kLampGlowReach, 2.0f * kLampGlowReach).withCentre ({ kLampX[side], kLampY }));
        }
    }

    for (const auto& part : parts)
    {
        if (part.photos == nullptr || ! part.photos->isValid())
            continue;

        const auto& c = part.control;
        const auto& run = *c.run;

        // Which photograph: the nearest position. Between two of a knob's,
        // the nearest is turned the rest of the way, so it moves smoothly.
        const auto degrees = degreesAt (c, part.parameter->getValue());
        const auto step = (run.toDegrees - run.fromDegrees) / (float) (run.frames - 1);
        const auto k = jlimit (0, run.frames - 1, roundToInt ((degrees - run.fromDegrees) / step));
        const auto rest = run.kind == Kind::toggle ? 0.0f : degrees - (run.fromDegrees + step * (float) k);

        const auto pixels = roundToInt (run.frame.size() * scale);
        const auto columns = jmin (run.frames, 8);
        const auto shot = part.photos->getClippedImage ({ (k % columns) * pixels, (k / columns) * pixels, pixels, pixels });

        g.drawImageTransformed (shot, AffineTransform::scale (run.frame.size() / (float) pixels)
                                          .translated (c.x - run.frame.before, c.y - run.frame.before)
                                          .rotated (degreesToRadians (rest), c.x, c.y));
    }
}

void MassifEditor::paintReadout (Graphics& g)
{
    if (shown == nullptr || readout.isEmpty())
        return;

    // Beside the control: under it, or over it when there is no room below.
    const auto font = fonts->monoBoldFont (12.0f, 0.04f);
    const auto width = GlyphArrangement::getStringWidth (font, readout) + 26.0f;
    const auto bounds = shown->component->getBounds().toFloat();
    auto pill = Rectangle<float> (width, 24.0f).withCentre ({ bounds.getCentreX(), bounds.getBottom() + 14.0f });

    if (pill.getBottom() > (float) kHeight - 2.0f)
        pill.setY (bounds.getY() - 26.0f);

    pill = pill.constrainedWithin (Rectangle<float> ((float) kWidth, (float) kHeight).reduced (4.0f));

    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (pill.translated (0.0f, 2.0f), pill.getHeight() * 0.5f);
    g.setColour (Colour (0xe6140c0d));
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, 1.0f);

    g.setColour (mist);
    g.setFont (font);
    g.drawText (readout, pill, Justification::centred, false);
}

} // namespace onirique

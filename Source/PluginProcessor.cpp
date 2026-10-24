#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Layout.h"

namespace onirique
{

namespace
{
    using Attributes = AudioParameterFloatAttributes;

    float parseNumber (const String& text)
    {
        return text.retainCharacters ("0123456789.,-").replaceCharacter (',', '.').getFloatValue();
    }

    String hertz (double hz)
    {
        return hz < 1000.0 ? String (roundToInt (hz)) + " Hz" : String (hz / 1000.0, hz < 10000.0 && std::fmod (hz, 1000.0) != 0.0 ? 1 : 0) + " kHz";
    }

    String id (const std::string& s) { return String (s); }

    AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace onirique::layout;
        AudioProcessorValueTreeState::ParameterLayout layout;
        const msf::Band band;
        const msf::Channel channel;

        auto dB = Attributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })
                              .withValueFromStringFunction ([] (const String& t) { return parseNumber (t); });

        // -6 dB at the left, 0 at twelve o'clock, +4 at the right, as engraved.
        NormalisableRange<float> gainRange (
            (float) msf::kMinGainDb, (float) msf::kMaxGainDb,
            [] (float, float, float v) { return v < 0.5f ? -6.0f + 12.0f * v : 8.0f * (v - 0.5f); },
            [] (float, float, float x) { return x < 0.0f ? (x + 6.0f) / 12.0f : 0.5f + x / 8.0f; },
            [] (float s, float e, float x) { return jlimit (s, e, std::round (x * 100.0f) / 100.0f); });

        for (int side = 0; side < msf::kMaxChannels; ++side)
        {
            const String s = side == 0 ? "L" : "R";

            for (int b = 0; b < msf::kNumBands; ++b)
            {
                const auto n = s + String (b + 1) + " ";
                StringArray freqs;

                for (auto hz : msf::kBandFreqs[b])
                    freqs.add (hertz (hz));

                layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id (bandId (side, b, "gain")), 1 }, n + "Gain",
                                                                   NormalisableRange<float> (0.0f, (float) msf::kMaxBandDb, 0.01f), band.gain, dB));
                layout.add (std::make_unique<AudioParameterFloat> (
                    ParameterID { id (bandId (side, b, "bw")), 1 }, n + "Bandwidth", NormalisableRange<float> (0.0f, 1.0f, 0.001f), band.bandwidth,
                    Attributes().withStringFromValueFunction ([] (float v, int)
                                                              {
                                                                  return String (v * 10.0f, 1) + (v < 0.3f ? "  ·  wide" : v > 0.7f ? "  ·  narrow" : "");
                                                              })
                                .withValueFromStringFunction ([] (const String& t) { return parseNumber (t) * 0.1f; })));
                layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id (bandId (side, b, "freq")), 1 }, n + "Frequency", freqs, band.freq));
                layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id (bandId (side, b, "mode")), 1 }, n + "Boost/Cut",
                                                                    StringArray { "BOOST", "OUT", "CUT" }, band.mode));
                layout.add (std::make_unique<AudioParameterBool> (ParameterID { id (bandId (side, b, "shelf")), 1 }, n + "Shelf", band.shelf,
                                                                  AudioParameterBoolAttributes().withStringFromValueFunction (
                                                                      [] (bool v, int) { return String (v ? "SHELF" : "BELL"); })));
            }

            StringArray lowPass, highPass;

            for (int i = 0; i < msf::kNumFilters; ++i)
            {
                lowPass.add (i == 0 ? "OFF" : hertz (msf::kLowPassFreqs[i]));
                highPass.add (i == 0 ? "OFF" : hertz (msf::kHighPassFreqs[i]));
            }

            layout.add (std::make_unique<AudioParameterBool> (ParameterID { id (sideId (side, "in")), 1 }, s + " In", channel.in));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id (sideId (side, "gain")), 1 }, s + " Gain", gainRange, channel.gain, dB));
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id (sideId (side, "lowpass")), 1 }, s + " Low Pass", lowPass, channel.lowPass));
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id (sideId (side, "highpass")), 1 }, s + " High Pass", highPass, channel.highPass));
        }

        // Linked, a knob turned on one side turns its twin on the other (the editor does it).
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "link", 1 }, "Link", true));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

        return layout;
    }
}

//==============================================================================
MassifProcessor::MassifProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Massif", createLayout())
{
    using namespace onirique::layout;
    auto get = [this] (const std::string& s) { return apvts.getRawParameterValue (String (s)); };

    for (int side = 0; side < msf::kMaxChannels; ++side)
    {
        auto& v = sides[side];

        for (int b = 0; b < msf::kNumBands; ++b)
            v.bands[b] = { get (bandId (side, b, "gain")), get (bandId (side, b, "bw")), get (bandId (side, b, "freq")),
                           get (bandId (side, b, "mode")), get (bandId (side, b, "shelf")) };

        v.in = get (sideId (side, "in"));
        v.gain = get (sideId (side, "gain"));
        v.lowPass = get (sideId (side, "lowpass"));
        v.highPass = get (sideId (side, "highpass"));
    }

    bypass = get ("bypass");
    bypassParameter = apvts.getParameter ("bypass");
}

bool MassifProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == AudioChannelSet::mono() || out == AudioChannelSet::stereo()) && layouts.getMainInputChannelSet() == out;
}

msf::Settings MassifProcessor::readSettings() const noexcept
{
    msf::Settings s;

    for (int side = 0; side < msf::kMaxChannels; ++side)
    {
        const auto& v = sides[side];
        auto& c = s.channels[side];

        for (int b = 0; b < msf::kNumBands; ++b)
        {
            const auto& from = v.bands[b];
            c.bands[b] = { from.gain->load(), from.bandwidth->load(), roundToInt (from.freq->load()), roundToInt (from.mode->load()),
                           from.shelf->load() > 0.5f };
        }

        c.in = v.in->load() > 0.5f;
        c.gain = v.gain->load();
        c.lowPass = roundToInt (v.lowPass->load());
        c.highPass = roundToInt (v.highPass->load());
    }

    s.bypass = bypass->load() > 0.5f;
    return s;
}

void MassifProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    license.refresh();

    // Twice the rate, through half-band filters whose delay is a whole number
    // of samples, so the host can make up for it exactly.
    maxBlock = jmax (1, samplesPerBlock);
    oversampling = std::make_unique<dsp::Oversampling<float>> ((size_t) jmax (1, getMainBusNumOutputChannels()), 1,
                                                               dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampling->initProcessing ((size_t) maxBlock);
    setLatencySamples (roundToInt (oversampling->getLatencyInSamples()));

    massif.setSettings (readSettings());
    massif.prepare (sampleRate * 2.0);
}

void MassifProcessor::reset()
{
    massif.reset();

    if (oversampling != nullptr)
        oversampling->reset();
}

void MassifProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const auto numChannels = jmin (getMainBusNumOutputChannels(), msf::kMaxChannels, buffer.getNumChannels());
    const auto numSamples = buffer.getNumSamples();

    if (oversampling == nullptr || numChannels < 1)
        return;

    massif.setSettings (readSettings());

    // Even bypassed, the sound goes up and down again: the delay the host
    // makes up for stays the same, and switching makes no jump.
    dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numChannels, (size_t) numSamples);

    // A host may send more than it announced: in pieces the filters were made for.
    for (int at = 0; at < numSamples; at += maxBlock)
    {
        auto part = block.getSubBlock ((size_t) at, (size_t) jmin (maxBlock, numSamples - at));
        auto up = oversampling->processSamplesUp (part);

        float* channels[msf::kMaxChannels] = {};

        for (int c = 0; c < numChannels; ++c)
            channels[c] = up.getChannelPointer ((size_t) c);

        massif.process (channels, numChannels, (int) up.getNumSamples());
        oversampling->processSamplesDown (part);
    }

    // Last, and over every output: what an unlicensed plugin lets through.
    license.process (buffer, getSampleRate(), isNonRealtime());
}

AudioProcessorEditor* MassifProcessor::createEditor()
{
    return new MassifEditor (*this);
}

//==============================================================================
void MassifProcessor::getStateInformation (MemoryBlock& destination)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destination);
}

void MassifProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (ValueTree::fromXml (*xml));
}

} // namespace onirique

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new onirique::MassifProcessor();
}

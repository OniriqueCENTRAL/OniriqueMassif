// Onirique Massif — editor
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>

#include "License/LicenseOverlay.h"
#include "PluginProcessor.h"
#include "ui/Layout.h"
#include "ui/Theme.h"

namespace onirique
{

/** The front of a two-channel passive equalizer, painted red, shown as
    photographs: the front itself, its lamps and the words of its windows
    lit or not, and each part in the position its parameter is at
    (tools/photo.cpp renders them). The controls lie unseen over their parts.
    The whole front is drawn at one size and scaled as one with the window. */
class MassifEditor final : public AudioProcessorEditor,
                           private Timer
{
public:
    explicit MassifEditor (MassifProcessor&);
    ~MassifEditor() override;

    void resized() override;

    static constexpr int kWidth = layout::kWidth, kHeight = layout::kHeight;

private:
    struct Front final : Component
    {
        std::function<void (Graphics&)> onPaint, onPaintOver;
        void paint (Graphics& g) override { onPaint (g); }
        void paintOverChildren (Graphics& g) override { onPaintOver (g); }
    };

    /** What takes the pointer for a toggle, a lamp or the power switch: a click. */
    struct Switch final : Component, SettableTooltipClient
    {
        std::function<void (const MouseEvent&)> onClick;
        void mouseDown (const MouseEvent& e) override { onClick (e); }
    };

    /** A control of the front, the component that takes the pointer for it,
        and the photographs of its part. */
    struct Part
    {
        layout::Control control;
        Component* component;
        RangedAudioParameter* parameter;
        RangedAudioParameter* twin;     // the same control on the other side, or nothing
        const Image* photos;
    };

    void timerCallback() override;
    void paintFront (Graphics&);
    void paintReadout (Graphics&);

    void click (const Part&, const MouseEvent&);
    void set (RangedAudioParameter&, float normalised);

    /** Linked, the twin of a control goes where it is. */
    void follow (const Part&);
    bool linked() const;

    MassifProcessor& processor;
    theme::MassifLookAndFeel lookAndFeel;
    license::theme::Fonts fonts;

    Front front;
    std::vector<std::unique_ptr<Component>> components;
    std::vector<Part> parts;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>> attachments;

    /** The front, everything out, and everything alight; the photographs of each part. */
    Image frontOff, frontLit;
    std::map<String, Image> photos;

    /** Each lamp lit, and its glow on the front, fading out at the edge. */
    Image lampGlow[2];

    /** What the parts were last drawn at: a change is drawn again. */
    std::vector<float> shownValues;

    /** What the control in hand, or under the pointer, is set to; shown beside it. */
    const Part* shown = nullptr;
    String readout;

    bool following = false;

    TooltipWindow tooltips { this, 2000 };

    /** The licence badge and its activation page. Declared last and added
        last: it lies over everything else. */
    std::unique_ptr<onirique::license::Overlay> licenseOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MassifEditor)
};

} // namespace onirique

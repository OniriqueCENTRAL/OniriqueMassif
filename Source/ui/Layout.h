// Onirique Massif — where everything sits on the front, shared by the editor
// and by the renderer that photographs the parts (tools/photo.cpp)
#pragma once

#include <string>
#include <vector>

namespace onirique::layout
{

// The front, in points (a 3U rack front); its photographs have this many pixels to the point.
constexpr int kWidth = 1400, kHeight = 386;
constexpr int kPhotoScale = 2;

enum class Kind { knob, small, toggle };

/** What a part stands on: the red front, a band's black plate, or the black face of its window. */
enum class Floor { panel, plate, window };

/** Where a part's photograph lies around its centre: the part, and room for
    the shadow it casts down and to the right, away from the light. */
struct Frame
{
    float before, after;     // from the centre to the left and top edges, and to the right and bottom ones

    constexpr float size() const { return before + after; }
};

/** A run of photographs of one part over its travel: a knob turned from one
    angle to the other, clockwise from twelve o'clock, or a toggle's lever
    tilted from up to down. Several controls take their look from one run. */
struct Run
{
    const char* photo;
    Kind kind;
    int frames;
    float fromDegrees, toDegrees;
    Frame frame;
    Floor floor;             // photographed on it, so its sides mirror what is round it
};

inline constexpr Run kKnob   { "knob",   Kind::knob,   64, -160.0f, 160.0f, { 34.0f, 53.0f }, Floor::plate };
inline constexpr Run kSmall  { "small",  Kind::small,  64, -160.0f, 160.0f, { 26.0f, 41.0f }, Floor::plate };
inline constexpr Run kCentre { "centre", Kind::small,  64, -160.0f, 160.0f, { 26.0f, 41.0f }, Floor::panel };
inline constexpr Run kToggle { "toggle", Kind::toggle, 3,  35.0f,   -35.0f, { 20.0f, 34.0f }, Floor::window };
inline constexpr Run kLever  { "lever",  Kind::toggle, 3,  35.0f,   -35.0f, { 20.0f, 34.0f }, Floor::panel };
inline constexpr const Run* kRuns[] = { &kKnob, &kSmall, &kCentre, &kToggle, &kLever };

constexpr float kKnobRadius = 27.5f, kSmallRadius = 21.0f, kToggleUnit = 17.0f;

/** A control: its parameter, where it is, how it is shown and handled. A
    knob is dragged; a toggle, a lamp or the power switch is clicked. */
struct Control
{
    std::string id, name;
    const Run* run;          // nullptr: nothing of its own to show (a lamp, lit or not on the front)
    bool clicked;
    float x, y, size;        // its centre, and the square that takes the pointer
    int positions;           // 0: it turns freely; else that many steps
    float fromDegrees, toDegrees;   // at the parameter's lowest and highest
};

// The bands: their columns, and the rows of their parts.
constexpr float kBandX[2][4] = { { 176.0f, 295.5f, 415.0f, 534.5f }, { 865.5f, 985.0f, 1104.5f, 1224.0f } };
constexpr float kGainY = 146.0f, kBandwidthY = 239.0f, kFreqY = 326.5f, kToggleY = 54.0f;
constexpr float kModeDx = -32.0f, kShelfDx = 28.0f;

// The middle: one column a side, the shared parts between them.
constexpr float kMiddleX[2] = { 638.0f, 762.0f };
constexpr float kLampX[2] = { 631.0f, 769.0f };
constexpr float kLampY = 97.0f, kLampRadius = 13.3f;
constexpr float kCentreX = 700.0f, kPowerY = 94.0f, kOutGainY = 154.0f, kLinkY = 175.0f, kLowPassY = 238.5f, kHighPassY = 325.0f;

inline std::string sideId (int side, const char* what) { return std::string (side == 0 ? "l" : "r") + what; }
inline std::string bandId (int side, int band, const char* what) { return sideId (side, std::to_string (band + 1).c_str()) + what; }

inline std::vector<Control> controls()
{
    std::vector<Control> c;

    for (int side = 0; side < 2; ++side)
    {
        const std::string s = side == 0 ? "L" : "R";

        for (int band = 0; band < 4; ++band)
        {
            const auto x = kBandX[side][band];
            const auto n = s + std::to_string (band + 1) + " ";
            c.push_back ({ bandId (side, band, "gain"), n + "GAIN", &kKnob, false, x, kGainY, 58.0f, 0, -140.0f, 140.0f });
            c.push_back ({ bandId (side, band, "bw"), n + "BANDWIDTH", &kSmall, false, x, kBandwidthY, 46.0f, 0, -135.0f, 135.0f });
            c.push_back ({ bandId (side, band, "freq"), n + "FREQUENCY", &kKnob, false, x, kFreqY, 58.0f, 11, -155.0f, 155.0f });
            // Boost up, out in the middle, cut down; the shelf up, the bell down.
            c.push_back ({ bandId (side, band, "mode"), n + "BOOST / CUT", &kToggle, true, x + kModeDx, kToggleY, 30.0f, 3, 35.0f, -35.0f });
            c.push_back ({ bandId (side, band, "shelf"), n + "SHELF / BELL", &kToggle, true, x + kShelfDx, kToggleY, 30.0f, 2, -35.0f, 35.0f });
        }

        c.push_back ({ sideId (side, "in"), s + " IN", nullptr, true, kLampX[side], kLampY, 30.0f, 2, 0.0f, 0.0f });
        c.push_back ({ sideId (side, "gain"), s + " GAIN", &kCentre, false, kMiddleX[side], kOutGainY, 46.0f, 0, -145.0f, 145.0f });
        c.push_back ({ sideId (side, "lowpass"), s + " LOW PASS", &kCentre, false, kMiddleX[side], kLowPassY, 46.0f, 6, -77.0f, 77.0f });
        c.push_back ({ sideId (side, "highpass"), s + " HIGH PASS", &kCentre, false, kMiddleX[side], kHighPassY, 46.0f, 6, -77.0f, 77.0f });
    }

    c.push_back ({ "link", "LINK", &kLever, true, kCentreX, kLinkY, 30.0f, 2, -35.0f, 35.0f });
    // The power knob's first position is ON (the bypass off), pointing up.
    c.push_back ({ "bypass", "POWER", &kCentre, true, kCentreX, kPowerY, 44.0f, 2, 0.0f, -50.0f });

    return c;
}

/** Where a control's part is turned to, for a parameter at `value` (0 to 1). */
inline float degreesAt (const Control& c, float value)
{
    const auto v = c.positions > 1 ? (float) (int) (value * (float) (c.positions - 1) + 0.5f) / (float) (c.positions - 1) : value;
    return c.fromDegrees + (c.toDegrees - c.fromDegrees) * v;
}

// What is part of the front itself, photographed with it.
struct Plate { float x, top, bottom, halfWidth; };           // the black ovals the bands sit on
struct Window { float x, y, halfWidth, halfHeight; };       // the black windows of the band switches
struct Screw { float x, y, radius, slot; };

constexpr float kPlateTop = 96.0f, kPlateBottom = 380.0f, kPlateHalfWidth = 56.0f, kPlateHeight = 2.2f;
constexpr float kWindowY = 46.5f, kWindowHalfWidth = 53.0f, kWindowHalfHeight = 36.0f, kWindowRadius = 8.0f, kWindowHeight = 3.0f;

inline std::vector<Plate> plates()
{
    std::vector<Plate> p;

    for (const auto& side : kBandX)
        for (auto x : side)
            p.push_back ({ x, kPlateTop, kPlateBottom, kPlateHalfWidth });

    return p;
}

inline std::vector<Window> windows()
{
    std::vector<Window> w;

    for (const auto& side : kBandX)
        for (auto x : side)
            w.push_back ({ x, kWindowY, kWindowHalfWidth, kWindowHalfHeight });

    return w;
}

inline std::vector<Screw> screws()
{
    std::vector<Screw> s;
    auto slot = 0.3f;

    // Two on each plate, at its ends.
    for (const auto& side : kBandX)
        for (auto x : side)
        {
            s.push_back ({ x, 103.8f, 6.0f, slot });
            s.push_back ({ x, 367.7f, 6.0f, slot + 0.9f });
            slot += 0.7f;
        }

    // Six down each side, holding the front to the case.
    for (auto x : { 100.8f, 1299.2f })
        for (auto y : { 37.3f, 89.2f, 167.7f, 219.6f, 300.8f, 351.3f })
        {
            s.push_back ({ x, y, 8.0f, slot });
            slot += 1.3f;
        }

    return s;
}

// The rack ears' slots: rounded, cut through the front in the four corners.
struct Slot { float x, y; };
inline constexpr Slot kSlots[] = { { 20.0f, 15.0f }, { 1380.0f, 15.0f }, { 20.0f, 371.0f }, { 1380.0f, 371.0f } };
constexpr float kSlotLength = 30.0f, kSlotHeight = 15.0f;

/** The words that light in a band's window: BOOST in green when it boosts,
    CUT in red when it cuts; their place, around the band's centre. */
struct Light { float dx, y, width, height; };
constexpr Light kBoostLight { -28.0f, 35.0f, 40.0f, 14.0f };
constexpr Light kCutLight { -29.0f, 73.6f, 30.0f, 14.0f };

/** Where the licence badge sits: at the foot of the middle, under the name. */
struct Area { int x, y, width, height; };
constexpr Area kBadge { 590, 360, 220, 22 };

/** How far the glow of a lit lamp reaches on the front around it. */
constexpr float kLampGlowReach = 40.0f;

} // namespace onirique::layout

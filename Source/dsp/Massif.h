// Onirique Massif — the sound: four passive bands a channel, the tubes that make
// up for them, and highs voiced half like the passive original, half like a
// discrete class-A equalizer
#pragma once

namespace msf
{

constexpr int kMaxChannels = 2, kNumBands = 4, kNumSteps = 11, kNumFilters = 6;

// The frequency switches, as they are engraved round each band's knob.
constexpr double kBandFreqs[kNumBands][kNumSteps] = {
    { 22.0, 33.0, 47.0, 68.0, 100.0, 150.0, 220.0, 330.0, 470.0, 680.0, 1000.0 },
    { 82.0, 120.0, 180.0, 270.0, 390.0, 560.0, 820.0, 1200.0, 1800.0, 2700.0, 3900.0 },
    { 220.0, 330.0, 470.0, 680.0, 1000.0, 1500.0, 2200.0, 3300.0, 4700.0, 6800.0, 10000.0 },
    { 560.0, 820.0, 1200.0, 1800.0, 2700.0, 3900.0, 5600.0, 8200.0, 12000.0, 16000.0, 27000.0 },
};

constexpr const char* kBandFreqNames[kNumBands][kNumSteps] = {
    { "22", "33", "47", "68", "100", "150", "220", "330", "470", "680", "1K" },
    { "82", "120", "180", "270", "390", "560", "820", "1K2", "1K8", "2K7", "3K9" },
    { "220", "330", "470", "680", "1K", "1K5", "2K2", "3K3", "4K7", "6K8", "10K" },
    { "560", "820", "1K2", "1K8", "2K7", "3K9", "5K6", "8K2", "12K", "16K", "27K" },
};

constexpr const char* kBandRanges[kNumBands] = { "22 - 1K", "82 - 3K9", "220 - 10K", "560 - 27K" };

// The filters: off, then their corners.
constexpr double kLowPassFreqs[kNumFilters] = { 0.0, 18000.0, 12000.0, 9000.0, 7500.0, 6000.0 };
constexpr double kHighPassFreqs[kNumFilters] = { 0.0, 22.0, 39.0, 68.0, 120.0, 220.0 };
constexpr const char* kLowPassNames[kNumFilters] = { "OFF", "18K", "12K", "9K", "7K5", "6K" };
constexpr const char* kHighPassNames[kNumFilters] = { "OFF", "22", "39", "68", "120", "220" };

constexpr double kMaxBandDb = 20.0, kMinGainDb = -6.0, kMaxGainDb = 4.0;

enum Mode { boost, out, cut };

struct Band
{
    float gain = 0.0f;          // 0 to 20 dB, up or down as the mode says
    float bandwidth = 0.5f;     // 0 wide to 1 narrow; in a shelf, how hard it resonates
    int freq = 5;
    int mode = out;
    bool shelf = false;         // the two low bands shelve low, the two high bands high
};

struct Channel
{
    Band bands[kNumBands];
    bool in = true;             // out: the bands and filters go flat, the tubes stay
    float gain = 0.0f;          // -6 to +4 dB, the make-up after the network
    int lowPass = 0, highPass = 0;
};

struct Settings
{
    Channel channels[kMaxChannels];
    bool bypass = false;        // the power switch off: the signal as it came in
};

/** A second-order section, coefficients normalised. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double s1 = 0.0, s2 = 0.0;

    double process (double x) noexcept
    {
        const auto y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }

    double magnitude (double w) const noexcept;
    void clear() noexcept { s1 = s2 = 0.0; }
};

/** One side of the equalizer. The two low bands are the passive network of
    the original alone; each high band is that network at half its gain,
    followed by a class-A band at the other half, so the peak lands where the
    knob says and the shape lies between the two. */
class Side
{
public:
    void prepare (double sampleRate, const Channel& settings);
    void reset() noexcept;
    /** `wet` from 0 (the signal as it came in) to 1 glides towards `wetTarget`, `wetStep` a sample. */
    void process (float* x, int numSamples, const Channel& settings, double& wet, double wetTarget, double wetStep) noexcept;
    double responseDb (double hz) const noexcept;

private:
    // Glides: each band's gain (signed) and bandwidth, then the make-up gain.
    enum { numDials = kNumBands * 2 + 1, numSwitches = kNumBands * 2 + 2 };

    void wanted (const Channel&, double* dials, int* switches) const noexcept;
    void design() noexcept;

    static constexpr int kChunk = 32;
    static constexpr int kNumSections = 2 + 2 * 2 + 1 + 2 + 2;   // the low bands, the high bands, high pass, low pass

    double fs = 88200.0, glide = 0.0;
    double dials[numDials] = {};
    int switches[numSwitches] = {};   // each band's frequency and shelf, the two filters
    bool designed = false;

    Biquad sections[kNumSections];
    double hpPole = 0.0, hpState = 0.0, hpOn = 0.0;   // the first-order part of the high pass
    double makeUp = 1.0;

    // The make-up stage: the tubes take what lies under the highs, the iron
    // the bass, the discrete stage the highs; only what each adds is kept.
    double lowSplit = 0.0, bassSplit = 0.0, dcPole = 0.0;
    double lowIn = 0.0, bassIn = 0.0, dcX = 0.0, dcY = 0.0;
};

class Massif
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void setSettings (const Settings& s) noexcept { target = s; }

    /** Each channel through its own side; one channel uses the left side. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    double responseDb (int channel, double hz) const noexcept { return sides[channel].responseDb (hz); }

private:
    Settings target;
    Side sides[kMaxChannels];
    double wet = 1.0, wetStep = 0.0;
};

} // namespace msf

#include "Massif.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace msf
{

namespace
{
    // The passive network, set by reasoning from the original's published
    // behaviour rather than measured on one. Its bells go from very wide to
    // fairly narrow, and narrow as they are pushed, as resonant passive
    // sections do; its cuts are a little narrower than its boosts.
    constexpr double kWideQ = 0.4, kNarrowQ = 2.8, kCutNarrower = 1.2;

    // Its shelves: at the wide end a gentle slope; towards narrow they
    // resonate, overshooting before the corner and dipping just past it.
    constexpr double kShelfWideQ = 0.5, kShelfNarrowQ = 1.7;

    // The bands share one passive network and load each other: two bands
    // pushed close together give less than the sum of the two.
    constexpr double kLoad = 0.3, kLoadSpread = 1.2;

    // The high bands' class-A half: bells of a steadier width, whatever the
    // gain, and cuts narrower than boosts; a shelf that never resonates.
    constexpr double kClassAWideQ = 0.55, kClassANarrowQ = 2.2, kClassACutNarrower = 1.35;
    constexpr double kClassAShelfQ = 0.55, kClassAShelfNarrow = 0.2;

    // The make-up stage. The tubes take what lies under kLowSplit, the iron
    // the bass under kBassSplit, harder the louder it is; the discrete stage
    // the highs, only just.
    constexpr double kLowSplit = 1500.0, kBassSplit = 120.0;
    constexpr double kTubeDrive = 0.28, kTubeBias = 0.25;
    constexpr double kIronDrive = 0.7;
    constexpr double kClassADrive = 0.15, kClassABias = 0.2;

    constexpr double kPi = 3.14159265358979323846;

    void set (Biquad& f, double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        f.b0 = b0 / a0;
        f.b1 = b1 / a0;
        f.b2 = b2 / a0;
        f.a1 = a1 / a0;
        f.a2 = a2 / a0;
    }

    void flat (Biquad& f) noexcept { set (f, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0); }

    // The cookbook's second-order shapes (R. Bristow-Johnson).
    void lowShelf (Biquad& f, double fs, double hz, double q, double db) noexcept
    {
        const auto A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * hz / fs, c = std::cos (w);
        const auto k = 2.0 * std::sqrt (A) * std::sin (w) / (2.0 * q);

        set (f, A * ((A + 1.0) - (A - 1.0) * c + k), 2.0 * A * ((A - 1.0) - (A + 1.0) * c), A * ((A + 1.0) - (A - 1.0) * c - k),
             (A + 1.0) + (A - 1.0) * c + k, -2.0 * ((A - 1.0) + (A + 1.0) * c), (A + 1.0) + (A - 1.0) * c - k);
    }

    void highShelf (Biquad& f, double fs, double hz, double q, double db) noexcept
    {
        const auto A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * hz / fs, c = std::cos (w);
        const auto k = 2.0 * std::sqrt (A) * std::sin (w) / (2.0 * q);

        set (f, A * ((A + 1.0) + (A - 1.0) * c + k), -2.0 * A * ((A - 1.0) + (A + 1.0) * c), A * ((A + 1.0) + (A - 1.0) * c - k),
             (A + 1.0) - (A - 1.0) * c + k, 2.0 * ((A - 1.0) - (A + 1.0) * c), (A + 1.0) - (A - 1.0) * c - k);
    }

    void peak (Biquad& f, double fs, double hz, double q, double db) noexcept
    {
        const auto A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * hz / fs;
        const auto alpha = std::sin (w) / (2.0 * q);

        set (f, 1.0 + alpha * A, -2.0 * std::cos (w), 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * std::cos (w), 1.0 - alpha / A);
    }

    void lowPass (Biquad& f, double fs, double hz, double q) noexcept
    {
        const auto w = 2.0 * kPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set (f, (1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    void highPass (Biquad& f, double fs, double hz, double q) noexcept
    {
        const auto w = 2.0 * kPi * hz / fs, c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        set (f, (1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
    }

    double onePole (double hz, double fs) noexcept
    {
        return 1.0 - std::exp (-2.0 * kPi * hz / fs);
    }

    /** A stage's curve, biased so it bends one way before the other (even
        harmonics), and scaled so a small signal comes out as it went in. */
    double saturate (double u, double drive, double bias) noexcept
    {
        const auto t = std::tanh (drive * bias);
        return (std::tanh (drive * (u + bias)) - t) / (drive * (1.0 - t * t));
    }

    /** The iron: it bends both ways alike (odd harmonics). */
    double iron (double u) noexcept
    {
        return std::tanh (kIronDrive * u) / kIronDrive;
    }
}

//==============================================================================
double Biquad::magnitude (double w) const noexcept
{
    const auto z1 = std::polar (1.0, -w), z2 = z1 * z1;
    return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
}

//==============================================================================
void Side::wanted (const Channel& c, double* d, int* s) const noexcept
{
    for (int k = 0; k < kNumBands; ++k)
    {
        const auto& b = c.bands[k];
        const auto sign = ! c.in || b.mode == out ? 0.0 : b.mode == boost ? 1.0 : -1.0;
        d[2 * k] = sign * std::clamp ((double) b.gain, 0.0, kMaxBandDb);
        d[2 * k + 1] = std::clamp ((double) b.bandwidth, 0.0, 1.0);
        s[2 * k] = std::clamp (b.freq, 0, kNumSteps - 1);
        s[2 * k + 1] = b.shelf ? 1 : 0;
    }

    d[2 * kNumBands] = std::clamp ((double) c.gain, kMinGainDb, kMaxGainDb);
    s[2 * kNumBands] = c.in ? std::clamp (c.lowPass, 0, kNumFilters - 1) : 0;
    s[2 * kNumBands + 1] = c.in ? std::clamp (c.highPass, 0, kNumFilters - 1) : 0;
}

void Side::prepare (double sampleRate, const Channel& settings)
{
    fs = sampleRate;
    glide = 1.0 - std::exp (-(double) kChunk / (0.025 * fs));
    lowSplit = onePole (kLowSplit, fs);
    bassSplit = onePole (kBassSplit, fs);
    dcPole = 1.0 - 2.0 * kPi * 10.0 / fs;

    wanted (settings, dials, switches);
    design();
    reset();
}

void Side::reset() noexcept
{
    for (auto& s : sections)
        s.clear();

    hpState = lowIn = bassIn = dcX = dcY = 0.0;
}

void Side::design() noexcept
{
    const auto top = 0.45 * fs;   // no corner closer to the edge than this
    double freq[kNumBands], loaded[kNumBands];

    for (int k = 0; k < kNumBands; ++k)
        freq[k] = std::min (kBandFreqs[k][switches[2 * k]], top);

    // How much of each band the others leave it, as they load the network.
    for (int k = 0; k < kNumBands; ++k)
    {
        auto load = 0.0;

        for (int j = 0; j < kNumBands; ++j)
        {
            if (j == k)
                continue;

            const auto octaves = std::log2 (freq[k] / freq[j]) / kLoadSpread;
            load += std::exp (-octaves * octaves) * std::abs (dials[2 * j]) / kMaxBandDb;
        }

        loaded[k] = dials[2 * k] / (1.0 + kLoad * load);
    }

    for (int k = 0; k < kNumBands; ++k)
    {
        const auto db = loaded[k], bw = dials[2 * k + 1], push = std::abs (db) / kMaxBandDb;
        const auto shelf = switches[2 * k + 1] != 0, high = k >= 2;
        auto& passive = sections[k < 2 ? k : 2 + 2 * (k - 2)];

        // The passive half, or the whole band below.
        const auto share = high ? 0.5 : 1.0;

        if (shelf)
        {
            const auto q = kShelfWideQ + (kShelfNarrowQ - kShelfWideQ) * bw;
            (high ? highShelf : lowShelf) (passive, fs, freq[k], q, db * share);
        }
        else
        {
            const auto q = kWideQ * std::pow (kNarrowQ / kWideQ, bw) * (0.7 + 0.3 * push) * (db < 0.0 ? kCutNarrower : 1.0);
            peak (passive, fs, freq[k], q, db * share);
        }

        if (! high)
            continue;

        // The class-A half.
        auto& discrete = sections[3 + 2 * (k - 2)];

        if (shelf)
            highShelf (discrete, fs, freq[k], kClassAShelfQ + kClassAShelfNarrow * bw, db * 0.5);
        else
            peak (discrete, fs, freq[k], kClassAWideQ * std::pow (kClassANarrowQ / kClassAWideQ, bw) * (db < 0.0 ? kClassACutNarrower : 1.0),
                  db * 0.5);
    }

    // High pass: 18 dB an octave, a first-order section and a second-order one.
    const auto hp = kHighPassFreqs[switches[2 * kNumBands + 1]];
    hpOn = hp > 0.0 ? 1.0 : 0.0;
    hpPole = hp > 0.0 ? onePole (hp, fs) : 0.0;

    if (hp > 0.0)
        highPass (sections[6], fs, hp, 1.0);
    else
        flat (sections[6]);

    // Low pass: 24 dB an octave, two second-order sections.
    const auto lp = std::min (kLowPassFreqs[switches[2 * kNumBands]], top);

    if (lp > 0.0)
    {
        lowPass (sections[7], fs, lp, 0.5412);
        lowPass (sections[8], fs, lp, 1.3066);
    }
    else
    {
        flat (sections[7]);
        flat (sections[8]);
    }

    makeUp = std::pow (10.0, dials[2 * kNumBands] / 20.0);
    designed = true;
}

void Side::process (float* x, int numSamples, const Channel& settings, double& wet, double wetTarget, double wetStep) noexcept
{
    double wantedDials[numDials];
    int wantedSwitches[numSwitches];
    wanted (settings, wantedDials, wantedSwitches);

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const auto n = std::min (kChunk, numSamples - start);

        // The knobs glide; the switches jump, as they do on the front.
        auto changed = ! designed || ! std::equal (std::begin (switches), std::end (switches), std::begin (wantedSwitches));
        std::copy (std::begin (wantedSwitches), std::end (wantedSwitches), std::begin (switches));

        for (int k = 0; k < numDials; ++k)
        {
            if (dials[k] == wantedDials[k])
                continue;

            dials[k] += (wantedDials[k] - dials[k]) * glide;

            if (std::abs (wantedDials[k] - dials[k]) < 1.0e-4)
                dials[k] = wantedDials[k];

            changed = true;
        }

        if (changed)
            design();

        for (int i = start; i < start + n; ++i)
        {
            const double in = x[i];
            auto y = in;

            hpState += hpPole * (y - hpState);
            y -= hpOn * hpState;

            for (auto& s : sections)
                y = s.process (y);

            y *= makeUp;

            lowIn += lowSplit * (y - lowIn);
            bassIn += bassSplit * (y - bassIn);
            const auto highs = y - lowIn;

            // Only what each stage adds: its harmonics, freed of the DC a bias leaves.
            const auto colour = saturate (lowIn, kTubeDrive, kTubeBias) - lowIn + iron (bassIn) - bassIn
                                + saturate (highs, kClassADrive, kClassABias) - highs;
            dcY = colour - dcX + dcPole * dcY;
            dcX = colour;
            y += dcY;

            wet = wet < wetTarget ? std::min (wetTarget, wet + wetStep) : std::max (wetTarget, wet - wetStep);
            x[i] = (float) (in + wet * (y - in));
        }
    }
}

double Side::responseDb (double hz) const noexcept
{
    const auto w = 2.0 * kPi * hz / fs;
    auto gain = makeUp;

    for (const auto& s : sections)
        gain *= s.magnitude (w);

    // The first-order high pass: what its one-pole low pass leaves.
    if (hpOn > 0.0)
    {
        const auto z1 = std::polar (1.0, -w);
        gain *= std::abs (1.0 - hpPole / (1.0 - (1.0 - hpPole) * z1));
    }

    return 20.0 * std::log10 (gain);
}

//==============================================================================
void Massif::prepare (double sampleRate)
{
    for (int c = 0; c < kMaxChannels; ++c)
        sides[c].prepare (sampleRate, target.channels[c]);

    wetStep = 1.0 / (0.02 * sampleRate);
    wet = target.bypass ? 0.0 : 1.0;
}

void Massif::reset() noexcept
{
    for (auto& s : sides)
        s.reset();
}

void Massif::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, kMaxChannels);
    const auto wetTarget = target.bypass ? 0.0 : 1.0;
    auto after = wet;

    for (int c = 0; c < numChannels; ++c)
    {
        auto w = wet;
        sides[c].process (channels[c], numSamples, target.channels[c], w, wetTarget, wetStep);
        after = w;
    }

    wet = after;
}

} // namespace msf

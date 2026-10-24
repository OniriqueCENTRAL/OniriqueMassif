// The whole sound of Massif without a host: the bands, the shelves, the way
// the network loads itself, the high bands' two halves, the filters, the
// colour, and the switches.
#include "dsp/Massif.h"
#include "TestSignal.h"

#include <cstdio>
#include <cstdlib>

namespace
{
constexpr double kSr = 96000.0;   // the plugin runs the engine at twice the host's rate

int failures = 0;

void check (bool ok, const char* what)
{
    std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    failures += ok ? 0 : 1;
}

msf::Massif make (const msf::Settings& s)
{
    msf::Massif m;
    m.setSettings (s);
    m.prepare (kSr);
    return m;
}

std::vector<float> play (const msf::Settings& s, std::vector<float> x)
{
    auto m = make (s);
    float* channels[] = { x.data() };
    m.process (channels, 1, (int) x.size());
    return x;
}

/** The amplitude of one frequency over the second half of a signal. */
double amplitude (const std::vector<float>& x, double hz)
{
    const auto from = x.size() / 2, n = x.size() - from;
    double re = 0.0, im = 0.0;

    for (size_t i = 0; i < n; ++i)
    {
        const auto phase = 2.0 * M_PI * hz * (double) (from + i) / kSr;
        re += x[from + i] * std::cos (phase);
        im += x[from + i] * std::sin (phase);
    }

    return 2.0 * std::sqrt (re * re + im * im) / (double) n;
}

double db (double v) { return 20.0 * std::log10 (v + 1.0e-12); }

void printCurve (const char* name, const msf::Settings& s)
{
    const auto m = make (s);
    std::printf ("    %-30s", name);

    for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 15000.0, 20000.0 })
        std::printf (" %6.1f", m.responseDb (0, hz));

    std::printf ("\n");
}

double extreme (const msf::Massif& m, double from, double to, bool highest)
{
    auto v = highest ? -1.0e9 : 1.0e9;

    for (auto hz = from; hz <= to; hz *= 1.01)
        v = highest ? std::max (v, m.responseDb (0, hz)) : std::min (v, m.responseDb (0, hz));

    return v;
}

/** Where a bell is down by half its gain, above and below: its width in octaves. */
double widthOctaves (const msf::Massif& m, double centre)
{
    const auto half = m.responseDb (0, centre) * 0.5;
    auto lo = centre, hi = centre;

    while (lo > 10.0 && std::abs (m.responseDb (0, lo)) > std::abs (half))
        lo /= 1.005;

    while (hi < 40000.0 && std::abs (m.responseDb (0, hi)) > std::abs (half))
        hi *= 1.005;

    return std::log2 (hi / lo);
}

msf::Settings one (int band, int mode, float gain, float bandwidth, int freq, bool shelf = false)
{
    msf::Settings s;
    auto& b = s.channels[0].bands[band];
    b.mode = mode;
    b.gain = gain;
    b.bandwidth = bandwidth;
    b.freq = freq;
    b.shelf = shelf;
    return s;
}
}

int main()
{
    using namespace msf;
    std::printf ("Massif, offline\n");
    std::printf ("    %-30s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n", "dB at", "20", "50", "100", "200", "500",
                 "1k", "2k", "5k", "10k", "15k", "20k");

    const Settings flat;
    const auto lowBell = one (0, boost, 10.0f, 0.5f, 4);           // band 1, 100 Hz
    const auto lowWide = one (0, boost, 10.0f, 0.0f, 4);
    const auto lowNarrow = one (0, boost, 10.0f, 1.0f, 4);
    const auto lowCut = one (0, cut, 10.0f, 0.5f, 4);
    const auto lowShelfGentle = one (0, boost, 10.0f, 0.0f, 4, true);
    const auto lowShelfResonant = one (0, boost, 10.0f, 1.0f, 4, true);
    const auto highBell = one (3, boost, 10.0f, 0.5f, 7);          // band 4, 8.2 kHz
    const auto highShelf = one (3, boost, 8.0f, 0.5f, 7, true);
    auto twoLow = lowBell;
    twoLow.channels[0].bands[1] = { 10.0f, 0.5f, 1, boost, false };   // band 2 at 120 Hz

    printCurve ("flat", flat);
    printCurve ("band 1 +10 @100 bw .5", lowBell);
    printCurve ("band 1 +10 @100 wide", lowWide);
    printCurve ("band 1 +10 @100 narrow", lowNarrow);
    printCurve ("band 1 -10 @100", lowCut);
    printCurve ("band 1 shelf +10 @100 gentle", lowShelfGentle);
    printCurve ("band 1 shelf +10 @100 resonant", lowShelfResonant);
    printCurve ("bands 1+2 +10 @100/120", twoLow);
    printCurve ("band 4 +10 @8k2", highBell);
    printCurve ("band 4 shelf +8 @8k2", highShelf);

    // The curves.
    {
        const auto m = make (flat);
        check (std::abs (extreme (m, 10.0, 30000.0, true)) < 0.01 && std::abs (extreme (m, 10.0, 30000.0, false)) < 0.01,
               "every band out, filters off: flat");
    }
    {
        const auto m = make (lowBell);
        check (std::abs (m.responseDb (0, 100.0) - 10.0) < 0.05, "a bell peaks where the knob says");
        check (std::abs (m.responseDb (0, 5000.0)) < 0.3, "and leaves the rest");
    }
    {
        const auto wide = widthOctaves (make (lowWide), 100.0), narrow = widthOctaves (make (lowNarrow), 100.0);
        std::printf ("    bell width at half gain: %.2f octaves wide, %.2f narrow\n", wide, narrow);
        check (wide > 2.5 && narrow < 1.0, "the bandwidth knob runs from very wide to narrow");
    }
    {
        const auto soft = widthOctaves (make (one (0, boost, 3.0f, 0.5f, 4)), 100.0);
        const auto hard = widthOctaves (make (one (0, boost, 18.0f, 0.5f, 4)), 100.0);
        std::printf ("    passive bell: %.2f octaves at +3, %.2f at +18\n", soft, hard);
        check (hard < soft, "a passive bell narrows as it is pushed");
    }
    {
        const auto m = make (lowCut);
        check (std::abs (m.responseDb (0, 100.0) + 10.0) < 0.05, "a cut goes down where the knob says");
        check (widthOctaves (m, 100.0) < widthOctaves (make (lowBell), 100.0), "and is a little narrower than the boost");
    }
    {
        const auto gentle = make (lowShelfGentle), resonant = make (lowShelfResonant);
        check (gentle.responseDb (0, 20.0) > 9.0 && std::abs (gentle.responseDb (0, 2000.0)) < 0.5, "a low shelf lifts the bottom");
        const auto dipGentle = extreme (gentle, 100.0, 1000.0, false), dipResonant = extreme (resonant, 100.0, 1000.0, false);
        std::printf ("    low shelf +10 @100: dip above it %.2f dB gentle, %.2f dB narrow; bump %.2f dB\n", dipGentle, dipResonant,
                     extreme (resonant, 10.0, 200.0, true));
        check (dipGentle > -0.2 && dipResonant < -1.0, "narrow, the shelf resonates: a dip just past its corner");
    }
    {
        const auto m = make (twoLow);
        const auto top = extreme (m, 50.0, 300.0, true);
        std::printf ("    two bands +10 at 100 and 120 Hz: %.2f dB at most\n", top);
        check (top < 19.0 && top > 13.0, "neighbouring bands load each other: less than the sum");
    }
    {
        const auto m = make (highBell);
        check (std::abs (m.responseDb (0, 8200.0) - 10.0) < 0.1, "a high bell, its two halves together, peaks where the knob says");
        const auto both = widthOctaves (m, 8200.0);
        auto low = one (1, boost, 10.0f, 0.5f, 6);   // the same knob on a low band, 820 Hz
        const auto passive = widthOctaves (make (low), 820.0);
        std::printf ("    +10, bw .5: %.2f octaves passive, %.2f hybrid\n", passive, both);
        check (std::abs (both - passive) > 0.05, "and its shape is its own, between the two");
    }
    {
        const auto m = make (highShelf);
        check (m.responseDb (0, 20000.0) > 7.0 && std::abs (m.responseDb (0, 500.0)) < 0.3, "a high shelf lifts the top");
    }
    {
        auto s = flat;
        s.channels[0].highPass = 3;   // 68 Hz
        s.channels[0].lowPass = 2;    // 12 kHz
        const auto m = make (s);
        const auto octave = m.responseDb (0, 34.0) - m.responseDb (0, 17.0);
        std::printf ("    high pass 68: %.1f dB at 68 Hz, %.1f dB an octave far below; low pass 12k: %.1f dB at 24k\n",
                     m.responseDb (0, 68.0), octave, m.responseDb (0, 24000.0));
        check (octave > 16.0 && octave < 20.0, "high pass: 18 dB an octave");
        check (m.responseDb (0, 24000.0) < -20.0 && m.responseDb (0, 1000.0) > -0.1, "low pass: steep, and the mids untouched");
    }
    {
        auto s = flat;
        s.channels[0].gain = 4.0f;
        check (std::abs (make (s).responseDb (0, 1000.0) - 4.0) < 0.01, "the gain knob: +4 dB at its top");
    }

    // The sound itself follows the curve.
    {
        const auto x = mdxtest::makeTone (1.0, kSr, 100.0, 0.05f);
        const auto out = play (lowBell, x);
        const auto heard = db (amplitude (out, 100.0) / amplitude (x, 100.0)), drawn = make (lowBell).responseDb (0, 100.0);
        std::printf ("    100 Hz with band 1 at +10: %+.2f dB heard, %+.2f dB on the curve\n", heard, drawn);
        check (std::abs (heard - drawn) < 0.3, "what is heard is what the curve says");
    }

    // The colour.
    auto harmonics = [] (const Settings& s, double hz, float level)
    {
        const auto x = mdxtest::makeTone (1.0, kSr, hz, level);
        const auto out = play (s, x);
        const auto fundamental = amplitude (out, hz);
        return std::make_pair (db (amplitude (out, 2.0 * hz) / fundamental), db (amplitude (out, 3.0 * hz) / fundamental));
    };

    const auto minus18 = 0.126f, minus6 = 0.5f;
    {
        const auto [h2, h3] = harmonics (flat, 1000.0, minus18);
        const auto [h2hot, h3hot] = harmonics (flat, 1000.0, minus6);
        std::printf ("    1 kHz: H2 %.1f dB, H3 %.1f dB at -18 dBFS; H2 %.1f, H3 %.1f at -6\n", h2, h3, h2hot, h3hot);
        check (h2 < -50.0 && h2 > -80.0, "the tubes are there, softly");
        check (h2hot > h2 + 8.0, "and more so the harder they are driven");
    }
    {
        const auto [h2, h3] = harmonics (flat, 40.0, minus18);
        const auto [h2hot, h3hot] = harmonics (flat, 40.0, minus6);
        std::printf ("    40 Hz: H2 %.1f dB, H3 %.1f dB at -18 dBFS; H2 %.1f, H3 %.1f at -6\n", h2, h3, h2hot, h3hot);
        check (h3 > -75.0 && h3hot > h3 + 15.0, "the iron thickens the bass, and more so when it is loud");
    }
    {
        const auto [h2, h3] = harmonics (flat, 8000.0, minus18);
        std::printf ("    8 kHz at -18 dBFS: H2 %.1f dB, H3 %.1f dB\n", h2, h3);
        check (h2 < -65.0 && h3 < -70.0, "the highs stay clean");
    }

    // The switches.
    {
        auto s = twoLow;
        s.channels[0].in = false;
        s.channels[0].highPass = 3;
        const auto m = make (s);
        check (std::abs (extreme (m, 10.0, 30000.0, true)) < 0.01 && std::abs (extreme (m, 10.0, 30000.0, false)) < 0.01,
               "IN off: the bands and filters go flat");
    }
    {
        auto s = twoLow;
        s.bypass = true;
        const auto x = mdxtest::makeNoise (0.2, kSr, 0.5f);
        const auto out = play (s, x);
        auto worst = 0.0f;

        for (size_t i = 0; i < x.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - x[i]));

        check (worst == 0.0f, "power off: the signal as it came in, to the bit");
    }
    {
        // Two channels, each its own: the right one's settings touch only the right.
        auto s = flat;
        s.channels[1].bands[0] = { 12.0f, 0.5f, 4, boost, false };
        auto x = mdxtest::makeTone (0.5, kSr, 100.0, 0.05f), y = x;
        const auto in = x;
        auto m = make (s);
        float* channels[] = { x.data(), y.data() };
        m.process (channels, 2, (int) x.size());
        const auto left = db (amplitude (x, 100.0) / amplitude (in, 100.0)), right = db (amplitude (y, 100.0) / amplitude (in, 100.0));
        std::printf ("    100 Hz, band 1 +12 on the right only: left %+.2f dB, right %+.2f dB\n", left, right);
        check (std::abs (left) < 0.1 && std::abs (right - 12.0) < 0.3, "the two sides are independent");
    }
    {
        // Turning a band all the way during a note: no step in the sound.
        auto x = mdxtest::makeTone (0.5, kSr, 80.0, 0.25f);
        auto m = make (flat);
        float* channels[] = { x.data() };
        const int half = (int) x.size() / 2;
        m.process (channels, 1, half);
        m.setSettings (one (0, boost, 20.0f, 1.0f, 3));
        channels[0] = x.data() + half;
        m.process (channels, 1, (int) x.size() - half);

        auto jump = 0.0f;

        for (size_t i = 1; i < x.size(); ++i)
            jump = std::max (jump, std::abs (x[i] - x[i - 1]));

        // An 80 Hz sine at 0.25, boosted by up to 20 dB, moves at most ~0.013 a sample.
        check (jump < 0.03f, "knobs glide: no click");
        check (std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); }), "and nothing blows up");
    }

    std::printf (failures == 0 ? "all good\n" : "%d failed\n", failures);
    return failures == 0 ? 0 : 1;
}

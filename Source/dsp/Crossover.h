#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace shaper
{

/** Topology-preserving-transform state variable filter (Cytomic / A. Simper).
    Stays stable while its cutoff is modulated, which matters for automated crossovers. */
struct SvfCoefficients
{
    float g = 0.0f, k = 1.41421356f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

    void set (double frequency, double sampleRate, double damping = 1.4142135623730951)
    {
        const double nyquistSafe = std::min (frequency, sampleRate * 0.49);
        const double gd = std::tan (3.14159265358979323846 * nyquistSafe / sampleRate);
        const double a1d = 1.0 / (1.0 + gd * (gd + damping));
        g = (float) gd;
        k = (float) damping;
        a1 = (float) a1d;
        a2 = (float) (gd * a1d);
        a3 = (float) (gd * gd * a1d);
    }
};

struct SvfState
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    inline void tick (const SvfCoefficients& c, float v0, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = v0 - c.k * v1 - v2;
    }
};

/**
    3-band Linkwitz-Riley (24 dB/oct) crossover.

    low  = LR4 lowpass(f1), then a 2nd-order allpass at f2 so its phase matches the other bands
    mid  = LR4 highpass(f1) -> LR4 lowpass(f2)
    high = LR4 highpass(f1) -> LR4 highpass(f2)

    low + mid + high = allpass(f1) * allpass(f2): flat magnitude response.
*/
class Crossover3
{
public:
    void prepare (double newSampleRate, int numChannels)
    {
        sampleRate = newSampleRate;
        channels.assign ((size_t) std::max (1, numChannels), {});
        setFrequencies (lowFreq, highFreq);
    }

    void reset() noexcept
    {
        for (auto& ch : channels)
            for (auto* s : { &ch.lo1, &ch.lo2, &ch.lo3, &ch.loAllpass, &ch.hi1, &ch.hi2, &ch.hi3 })
                s->reset();
    }

    void setFrequencies (double low, double high)
    {
        lowFreq = std::clamp (low, 10.0, sampleRate * 0.45);
        highFreq = std::clamp (std::max (high, lowFreq * 1.25), 10.0, sampleRate * 0.45);
        lowCoeffs.set (lowFreq, sampleRate);
        highCoeffs.set (highFreq, sampleRate);
    }

    double getLowFrequency() const noexcept { return lowFreq; }
    double getHighFrequency() const noexcept { return highFreq; }

    inline void process (int channel, float x, float& low, float& mid, float& high) noexcept
    {
        auto& s = channels[(size_t) channel];
        float lp, bp, hp;

        s.lo1.tick (lowCoeffs, x, lp, bp, hp);
        const float lpA = lp, hpA = hp;
        s.lo2.tick (lowCoeffs, lpA, lp, bp, hp);
        const float lowRaw = lp;
        s.lo3.tick (lowCoeffs, hpA, lp, bp, hp);
        const float rest = hp;

        s.loAllpass.tick (highCoeffs, lowRaw, lp, bp, hp);
        low = lp - highCoeffs.k * bp + hp;

        s.hi1.tick (highCoeffs, rest, lp, bp, hp);
        const float lpB = lp, hpB = hp;
        s.hi2.tick (highCoeffs, lpB, lp, bp, hp);
        mid = lp;
        s.hi3.tick (highCoeffs, hpB, lp, bp, hp);
        high = hp;
    }

private:
    struct ChannelState
    {
        SvfState lo1, lo2, lo3, loAllpass, hi1, hi2, hi3;
    };

    double sampleRate = 44100.0;
    double lowFreq = 200.0, highFreq = 2500.0;
    SvfCoefficients lowCoeffs, highCoeffs;
    std::vector<ChannelState> channels { 2 };
};

} // namespace shaper

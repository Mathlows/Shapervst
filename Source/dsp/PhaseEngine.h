#pragma once

#include <cmath>

namespace shaper
{

enum class TriggerMode
{
    Sync = 0,      // locked to the host song position (free-running when the transport is stopped)
    Retrigger = 1, // loops, a MIDI note-on restarts the cycle
    Envelope = 2   // a MIDI note-on plays the curve once, then holds the last value
};

/** Tempo-synced divisions, expressed in quarter notes (beats) per cycle. */
inline constexpr double syncDivisionBeats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 0.0625 };
inline constexpr const char* syncDivisionNames[] = { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
inline constexpr int numSyncDivisions = 10;
inline constexpr double syncTypeFactors[] = { 1.0, 1.5, 2.0 / 3.0 }; // straight, dotted, triplet
inline constexpr const char* syncTypeNames[] = { "Straight", "Dotted", "Triplet" };

/** Drives the read position in the curve, one sample at a time. */
class PhaseEngine
{
public:
    void reset() noexcept
    {
        phase = (mode == TriggerMode::Envelope) ? 1.0 : 0.0;
        envelopeRunning = false;
    }

    void setMode (TriggerMode newMode) noexcept
    {
        if (newMode == mode)
            return;

        mode = newMode;
        reset();
    }

    TriggerMode getMode() const noexcept { return mode; }

    /** Cycles per sample. */
    void setIncrement (double cyclesPerSample) noexcept { increment = cyclesPerSample; }

    /** Locks the phase to the host (Sync mode only). `hostPhase` is the cycle position at the block start. */
    void syncToHost (double hostPhase) noexcept
    {
        if (mode == TriggerMode::Sync)
            phase = hostPhase - std::floor (hostPhase);
    }

    void noteOn() noexcept
    {
        if (mode == TriggerMode::Sync)
            return;

        phase = 0.0;
        envelopeRunning = (mode == TriggerMode::Envelope);
    }

    /** Position in the curve, [0, 1]. `offset` is the user phase offset, [0, 1]. */
    float readPosition (float offset) const noexcept
    {
        if (mode == TriggerMode::Envelope)
            return (float) phase;

        const double p = phase + offset;
        return (float) (p - std::floor (p));
    }

    void advance() noexcept
    {
        phase += increment;

        if (mode == TriggerMode::Envelope)
        {
            if (phase >= 1.0)
            {
                phase = 1.0;
                envelopeRunning = false;
            }
        }
        else if (phase >= 1.0)
        {
            phase -= std::floor (phase);
        }
    }

    bool isEnvelopeRunning() const noexcept { return envelopeRunning; }

private:
    TriggerMode mode = TriggerMode::Sync;
    double phase = 0.0;
    double increment = 0.0;
    bool envelopeRunning = false;
};

} // namespace shaper

#pragma once

#include <string>
#include <vector>

namespace shaper
{

/** One breakpoint of the curve. `tension` shapes the segment that STARTS at this point. */
struct CurvePoint
{
    float x = 0.0f;       // position in the cycle, [0, 1]
    float y = 0.0f;       // gain level, [0, 1] (1 = full volume)
    float tension = 0.0f; // segment bend, [-1, 1] (0 = straight line)
};

enum class CurvePreset { Flat, Pump, Sine, Triangle, SawUp, SawDown, Square, Gate };

/**
    Breakpoint curve edited like a Serum LFO:
     - the first and last points are pinned to x = 0 and x = 1 (vertical moves only),
     - every segment has its own tension (exponential bend),
     - several points can share the same x to build vertical jumps (steps).

    The class is JUCE-free so it can be unit-tested and reused by the DSP core.
*/
class ShapeCurve
{
public:
    static constexpr int maxPoints = 256;
    static constexpr float tensionStrength = 9.0f;

    ShapeCurve();
    static ShapeCurve fromPreset (CurvePreset preset);

    const std::vector<CurvePoint>& points() const noexcept { return pts; }
    int numPoints() const noexcept { return (int) pts.size(); }
    bool isEndpoint (int index) const noexcept { return index == 0 || index == numPoints() - 1; }

    /** Inserts a point and returns its index (or -1 if the curve is full). */
    int addPoint (float x, float y);
    /** Removes an interior point. Endpoints cannot be removed. */
    bool removePoint (int index);
    /** Moves a point, keeping x between its neighbours (endpoints only move vertically). */
    void movePoint (int index, float x, float y);
    /** Sets the tension of the segment starting at `segment`. */
    void setTension (int segment, float tension);
    /** Replaces the region [x0, x1] by a flat step at level y (Serum "paint" behaviour). */
    void paintStep (float x0, float x1, float y);

    void setPoints (std::vector<CurvePoint> newPoints);
    void invert();
    void flipHorizontal();

    /** Value of the curve at x (right-continuous at vertical jumps). */
    float evaluate (float x) const noexcept;
    /** Value at the middle of a segment: where the tension handle is drawn. */
    float segmentMidValue (int segment) const noexcept;
    /** Writes size + 1 values (x = 0 ... 1 inclusive) into dest. */
    void renderTable (float* dest, int size) const;

    /** Normalised segment shape: maps t in [0, 1] to [0, 1]. tension > 0 = fast start. */
    static float shape (float t, float tension) noexcept;

    std::string toString() const;
    static bool fromString (const std::string& text, ShapeCurve& result);

    bool operator== (const ShapeCurve& other) const noexcept;
    bool operator!= (const ShapeCurve& other) const noexcept { return ! operator== (other); }

private:
    void sanitise();
    std::vector<CurvePoint> pts;
};

} // namespace shaper

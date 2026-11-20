#include "ShapeCurve.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <locale>
#include <sstream>

namespace shaper
{

namespace
{
    float clamp01 (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }
    constexpr float eps = 1.0e-6f;
}

ShapeCurve::ShapeCurve()
{
    pts = { { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
}

float ShapeCurve::shape (float t, float tension) noexcept
{
    t = clamp01 (t);

    if (std::abs (tension) < 1.0e-4f)
        return t;

    const float a = tension * tensionStrength;
    return (1.0f - std::exp (-a * t)) / (1.0f - std::exp (-a));
}

float ShapeCurve::evaluate (float x) const noexcept
{
    x = clamp01 (x);

    // Last point whose x <= x: makes the curve right-continuous on vertical jumps.
    auto it = std::upper_bound (pts.begin(), pts.end(), x,
                                [] (float value, const CurvePoint& p) { return value < p.x; });
    const int i = std::max (0, (int) (it - pts.begin()) - 1);

    if (i >= numPoints() - 1)
        return pts.back().y;

    const auto& p0 = pts[(size_t) i];
    const auto& p1 = pts[(size_t) i + 1];
    const float dx = p1.x - p0.x;

    if (dx <= eps)
        return p1.y;

    return p0.y + (p1.y - p0.y) * shape ((x - p0.x) / dx, p0.tension);
}

float ShapeCurve::segmentMidValue (int segment) const noexcept
{
    if (segment < 0 || segment >= numPoints() - 1)
        return 0.0f;

    const auto& p0 = pts[(size_t) segment];
    const auto& p1 = pts[(size_t) segment + 1];
    return p0.y + (p1.y - p0.y) * shape (0.5f, p0.tension);
}

void ShapeCurve::renderTable (float* dest, int size) const
{
    for (int k = 0; k <= size; ++k)
        dest[k] = evaluate ((float) k / (float) size);
}

int ShapeCurve::addPoint (float x, float y)
{
    if (numPoints() >= maxPoints)
        return -1;

    x = clamp01 (x);
    y = clamp01 (y);

    auto it = std::upper_bound (pts.begin(), pts.end(), x,
                                [] (float value, const CurvePoint& p) { return value < p.x; });
    const int index = std::clamp ((int) (it - pts.begin()), 1, numPoints() - 1);

    // The split segment keeps its bend on both halves.
    const float tension = pts[(size_t) index - 1].tension;
    pts.insert (pts.begin() + index, CurvePoint { x, y, tension });
    return index;
}

bool ShapeCurve::removePoint (int index)
{
    if (index <= 0 || index >= numPoints() - 1)
        return false;

    pts.erase (pts.begin() + index);
    return true;
}

void ShapeCurve::movePoint (int index, float x, float y)
{
    if (index < 0 || index >= numPoints())
        return;

    auto& p = pts[(size_t) index];
    p.y = clamp01 (y);

    if (index == 0)
        p.x = 0.0f;
    else if (index == numPoints() - 1)
        p.x = 1.0f;
    else
        p.x = std::clamp (x, pts[(size_t) index - 1].x, pts[(size_t) index + 1].x);
}

void ShapeCurve::setTension (int segment, float tension)
{
    if (segment >= 0 && segment < numPoints() - 1)
        pts[(size_t) segment].tension = std::clamp (tension, -1.0f, 1.0f);
}

void ShapeCurve::paintStep (float x0, float x1, float y)
{
    x0 = clamp01 (x0);
    x1 = clamp01 (x1);
    y = clamp01 (y);

    if (x1 < x0)
        std::swap (x0, x1);

    if (x1 - x0 <= eps)
        return;

    const float leftValue = evaluate (x0 - eps);
    const float rightValue = evaluate (x1);

    auto it = std::upper_bound (pts.begin(), pts.end(), x1,
                                [] (float value, const CurvePoint& p) { return value < p.x; });
    const int segAtRight = std::max (0, (int) (it - pts.begin()) - 1);
    const float rightTension = pts[(size_t) segAtRight].tension;

    std::vector<CurvePoint> out;
    out.reserve (pts.size() + 4);

    for (const auto& p : pts)
        if (p.x < x0 - eps)
            out.push_back (p);

    if (x0 <= eps)
    {
        out.push_back ({ 0.0f, y, 0.0f });
    }
    else
    {
        out.push_back ({ x0, leftValue, 0.0f });
        out.push_back ({ x0, y, 0.0f });
    }

    if (x1 >= 1.0f - eps)
    {
        out.push_back ({ 1.0f, y, 0.0f });
    }
    else
    {
        out.push_back ({ x1, y, 0.0f });
        out.push_back ({ x1, rightValue, rightTension });

        for (const auto& p : pts)
            if (p.x > x1 + eps)
                out.push_back (p);
    }

    if ((int) out.size() > maxPoints)
        return;

    pts = std::move (out);
    sanitise();
}

void ShapeCurve::setPoints (std::vector<CurvePoint> newPoints)
{
    pts = std::move (newPoints);
    sanitise();
}

void ShapeCurve::invert()
{
    // A vertical flip keeps the tension: y' = 1 - (y0 + (y1 - y0) f) = y0' + (y1' - y0') f
    for (auto& p : pts)
        p.y = 1.0f - p.y;
}

void ShapeCurve::flipHorizontal()
{
    // A reversed segment uses the opposite tension: 1 - f_c(1 - t) == f_-c(t)
    const int n = numPoints();
    std::vector<CurvePoint> out ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        const auto& src = pts[(size_t) (n - 1 - i)];
        out[(size_t) i].x = 1.0f - src.x;
        out[(size_t) i].y = src.y;
        out[(size_t) i].tension = (i < n - 1) ? -pts[(size_t) (n - 2 - i)].tension : 0.0f;
    }

    pts = std::move (out);
    sanitise();
}

void ShapeCurve::sanitise()
{
    if (pts.size() < 2)
    {
        *this = ShapeCurve();
        return;
    }

    std::stable_sort (pts.begin(), pts.end(), [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });

    if ((int) pts.size() > maxPoints)
    {
        const auto last = pts.back();
        pts.resize ((size_t) maxPoints - 1);
        pts.push_back (last);
    }

    for (auto& p : pts)
    {
        p.x = clamp01 (p.x);
        p.y = clamp01 (p.y);
        p.tension = std::isfinite (p.tension) ? std::clamp (p.tension, -1.0f, 1.0f) : 0.0f;
    }

    pts.front().x = 0.0f;
    pts.back().x = 1.0f;
    pts.back().tension = 0.0f;
}

ShapeCurve ShapeCurve::fromPreset (CurvePreset preset)
{
    ShapeCurve c;
    constexpr float sineBend = 0.2f; // matches sin(pi/4) at the middle of a quarter wave

    switch (preset)
    {
        case CurvePreset::Flat:     break;
        case CurvePreset::Pump:     c.setPoints ({ { 0.0f, 0.0f, 0.6f }, { 0.45f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }); break;
        case CurvePreset::Sine:     c.setPoints ({ { 0.0f, 1.0f, -sineBend }, { 0.25f, 0.5f, sineBend }, { 0.5f, 0.0f, -sineBend },
                                                   { 0.75f, 0.5f, sineBend }, { 1.0f, 1.0f, 0.0f } }); break;
        case CurvePreset::Triangle: c.setPoints ({ { 0.0f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }); break;
        case CurvePreset::SawUp:    c.setPoints ({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } }); break;
        case CurvePreset::SawDown:  c.setPoints ({ { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }); break;
        case CurvePreset::Square:   c.setPoints ({ { 0.0f, 1.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } }); break;
        case CurvePreset::Gate:
            c.setPoints ({ { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } });
            for (int k = 0; k < 4; ++k)
                c.paintStep ((float) k / 4.0f, (float) k / 4.0f + 0.1875f, 1.0f);
            break;
    }

    return c;
}

std::string ShapeCurve::toString() const
{
    std::ostringstream os;
    os.imbue (std::locale::classic());
    os.precision (9); // max_digits10 for float: exact round-trip

    for (const auto& p : pts)
        os << p.x << ',' << p.y << ',' << p.tension << ';';

    return os.str();
}

bool ShapeCurve::fromString (const std::string& text, ShapeCurve& result)
{
    std::istringstream is (text);
    is.imbue (std::locale::classic());

    std::vector<CurvePoint> parsed;
    CurvePoint p;
    char sep1 = 0, sep2 = 0, sep3 = 0;

    while (is >> p.x >> sep1 >> p.y >> sep2 >> p.tension >> sep3)
    {
        if (sep1 != ',' || sep2 != ',' || sep3 != ';')
            return false;

        parsed.push_back (p);
    }

    if (parsed.size() < 2)
        return false;

    result.setPoints (std::move (parsed));
    return true;
}

bool ShapeCurve::operator== (const ShapeCurve& other) const noexcept
{
    // Exact comparison on purpose: used to detect identical curves (round-trips, undo).
    auto same = [] (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; };

    if (pts.size() != other.pts.size())
        return false;

    for (size_t i = 0; i < pts.size(); ++i)
        if (! same (pts[i].x, other.pts[i].x) || ! same (pts[i].y, other.pts[i].y) || ! same (pts[i].tension, other.pts[i].tension))
            return false;

    return true;
}

} // namespace shaper

// Minimal, dependency-free tests for the DSP core.
#include "dsp/Crossover.h"
#include "dsp/PhaseEngine.h"
#include "dsp/ShapeCurve.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace shaper;

static int failures = 0;

#define CHECK(cond) do { if (! (cond)) { std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
#define CHECK_NEAR(a, b, tol) do { const double va = (a), vb = (b); if (std::abs (va - vb) > (tol)) { \
    std::printf ("  FAIL %s:%d  %s = %g, expected %g\n", __FILE__, __LINE__, #a, va, vb); ++failures; } } while (0)

static void test (const char* name, const std::function<void()>& body)
{
    const int before = failures;
    body();
    std::printf ("%s %s\n", failures == before ? "[ ok ]" : "[FAIL]", name);
}

int main()
{
    test ("segment shape is monotonic, bounded and symmetric", []
    {
        for (float c : { -1.0f, -0.5f, 0.0f, 0.3f, 1.0f })
        {
            CHECK_NEAR (ShapeCurve::shape (0.0f, c), 0.0, 1e-6);
            CHECK_NEAR (ShapeCurve::shape (1.0f, c), 1.0, 1e-6);
            float prev = -1.0f;
            for (int i = 0; i <= 100; ++i)
            {
                const float v = ShapeCurve::shape ((float) i / 100.0f, c);
                CHECK (v >= prev - 1e-6f);
                prev = v;
            }
            // Reversing a segment is the same as negating its tension.
            CHECK_NEAR (1.0f - ShapeCurve::shape (1.0f - 0.3f, c), ShapeCurve::shape (0.3f, -c), 1e-5);
        }
        CHECK (ShapeCurve::shape (0.5f, 0.5f) > 0.5f);
        CHECK (ShapeCurve::shape (0.5f, -0.5f) < 0.5f);
    });

    test ("evaluate interpolates and endpoints are pinned", []
    {
        ShapeCurve c;
        c.setPoints ({ { 0.2f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 0.7f, 0.0f, 0.0f } });
        CHECK_NEAR (c.points().front().x, 0.0, 0.0);
        CHECK_NEAR (c.points().back().x, 1.0, 0.0);
        CHECK_NEAR (c.evaluate (0.25f), 0.5, 1e-6);
        CHECK_NEAR (c.evaluate (0.5f), 1.0, 1e-6);
        CHECK_NEAR (c.evaluate (0.75f), 0.5, 1e-6);
    });

    test ("add / move / remove points", []
    {
        ShapeCurve c = ShapeCurve::fromPreset (CurvePreset::SawDown);
        const int i = c.addPoint (0.5f, 0.9f);
        CHECK (i == 1);
        CHECK_NEAR (c.evaluate (0.5f), 0.9, 1e-6);

        c.movePoint (i, 2.0f, -1.0f); // clamped between neighbours and to [0, 1]
        CHECK_NEAR (c.points()[1].x, 1.0, 1e-6);
        CHECK_NEAR (c.points()[1].y, 0.0, 1e-6);

        c.movePoint (0, 0.4f, 0.3f); // endpoint: vertical only
        CHECK_NEAR (c.points()[0].x, 0.0, 0.0);
        CHECK_NEAR (c.points()[0].y, 0.3, 1e-6);

        CHECK (! c.removePoint (0));
        CHECK (c.removePoint (1));
        CHECK (c.numPoints() == 2);
    });

    test ("vertical jumps are right-continuous", []
    {
        ShapeCurve c = ShapeCurve::fromPreset (CurvePreset::Square);
        CHECK_NEAR (c.evaluate (0.49f), 1.0, 1e-6);
        CHECK_NEAR (c.evaluate (0.5f), 0.0, 1e-6);
        CHECK_NEAR (c.evaluate (0.9f), 0.0, 1e-6);
    });

    test ("paint steps", []
    {
        ShapeCurve c = ShapeCurve::fromPreset (CurvePreset::Flat);
        c.paintStep (0.25f, 0.5f, 0.2f);
        CHECK_NEAR (c.evaluate (0.1f), 1.0, 1e-6);
        CHECK_NEAR (c.evaluate (0.3f), 0.2, 1e-6);
        CHECK_NEAR (c.evaluate (0.6f), 1.0, 1e-6);

        c.paintStep (0.5f, 0.75f, 0.6f); // adjacent cell keeps the previous step intact
        CHECK_NEAR (c.evaluate (0.3f), 0.2, 1e-6);
        CHECK_NEAR (c.evaluate (0.6f), 0.6, 1e-6);
        CHECK_NEAR (c.evaluate (0.9f), 1.0, 1e-6);

        c.paintStep (0.0f, 0.25f, 0.0f);
        c.paintStep (0.75f, 1.0f, 0.4f);
        CHECK_NEAR (c.evaluate (0.0f), 0.0, 1e-6);
        CHECK_NEAR (c.evaluate (1.0f), 0.4, 1e-6);

        auto gate = ShapeCurve::fromPreset (CurvePreset::Gate);
        CHECK_NEAR (gate.evaluate (0.1f), 1.0, 1e-6);
        CHECK_NEAR (gate.evaluate (0.2f), 0.0, 1e-6);
        CHECK_NEAR (gate.evaluate (0.8f), 1.0, 1e-6);
    });

    test ("invert and flip", []
    {
        ShapeCurve c = ShapeCurve::fromPreset (CurvePreset::Pump);
        ShapeCurve flipped = c;
        flipped.flipHorizontal();
        for (float x : { 0.05f, 0.2f, 0.33f, 0.6f, 0.9f })
            CHECK_NEAR (flipped.evaluate (1.0f - x), c.evaluate (x), 1e-4);

        ShapeCurve inv = c;
        inv.invert();
        for (float x : { 0.05f, 0.2f, 0.33f, 0.6f })
            CHECK_NEAR (inv.evaluate (x), 1.0f - c.evaluate (x), 1e-5);
    });

    test ("sine preset approximates a cosine", []
    {
        ShapeCurve c = ShapeCurve::fromPreset (CurvePreset::Sine);
        double maxErr = 0.0;
        for (int i = 0; i <= 200; ++i)
        {
            const double x = i / 200.0;
            maxErr = std::max (maxErr, std::abs (c.evaluate ((float) x) - (0.5 + 0.5 * std::cos (2.0 * 3.14159265358979 * x))));
        }
        CHECK (maxErr < 0.05);
    });

    test ("serialisation round-trip", []
    {
        ShapeCurve c = ShapeCurve::fromPreset (CurvePreset::Sine);
        c.paintStep (0.5f, 0.625f, 0.33f);
        ShapeCurve back;
        CHECK (ShapeCurve::fromString (c.toString(), back));
        CHECK (back == c);
        CHECK (! ShapeCurve::fromString ("garbage", back));
        CHECK (! ShapeCurve::fromString ("0,1,0;", back));
    });

    test ("crossover bands sum to a flat magnitude", []
    {
        const double sr = 48000.0;
        for (double freq : { 30.0, 150.0, 200.0, 800.0, 2500.0, 6000.0, 15000.0 })
        {
            Crossover3 x;
            x.prepare (sr, 1);
            x.setFrequencies (200.0, 2500.0);

            const int n = (int) sr;
            double inEnergy = 0.0, outEnergy = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const float s = (float) std::sin (2.0 * 3.14159265358979 * freq * i / sr);
                float l, m, h;
                x.process (0, s, l, m, h);
                if (i > n / 2)
                {
                    inEnergy += s * s;
                    outEnergy += (double) (l + m + h) * (l + m + h);
                }
            }
            CHECK_NEAR (10.0 * std::log10 (outEnergy / inEnergy), 0.0, 0.01);
        }
    });

    test ("crossover separates bands", []
    {
        const double sr = 48000.0;
        auto bandLevels = [sr] (double freq)
        {
            Crossover3 x;
            x.prepare (sr, 1);
            x.setFrequencies (200.0, 2500.0);
            std::vector<double> e (3, 0.0);
            for (int i = 0; i < 48000; ++i)
            {
                float b[3];
                x.process (0, (float) std::sin (2.0 * 3.14159265358979 * freq * i / sr), b[0], b[1], b[2]);
                if (i > 24000)
                    for (int k = 0; k < 3; ++k)
                        e[(size_t) k] += (double) b[k] * b[k];
            }
            return e;
        };

        auto low = bandLevels (40.0);
        CHECK (low[0] > 100.0 * (low[1] + low[2]));
        auto mid = bandLevels (800.0);
        CHECK (mid[1] > 20.0 * (mid[0] + mid[2]));
        auto high = bandLevels (12000.0);
        CHECK (high[2] > 100.0 * (high[0] + high[1]));
    });

    test ("phase engine modes", []
    {
        PhaseEngine p;
        p.setMode (TriggerMode::Retrigger);
        p.setIncrement (0.1);
        for (int i = 0; i < 15; ++i)
            p.advance();
        CHECK_NEAR (p.readPosition (0.0f), 0.5, 1e-6);
        CHECK_NEAR (p.readPosition (0.75f), 0.25, 1e-6);
        p.noteOn();
        CHECK_NEAR (p.readPosition (0.0f), 0.0, 1e-9);

        p.setMode (TriggerMode::Envelope);
        CHECK_NEAR (p.readPosition (0.0f), 1.0, 1e-9); // idle: holds the end of the curve
        p.noteOn();
        CHECK (p.isEnvelopeRunning());
        for (int i = 0; i < 25; ++i)
            p.advance();
        CHECK (! p.isEnvelopeRunning());
        CHECK_NEAR (p.readPosition (0.0f), 1.0, 1e-9);

        p.setMode (TriggerMode::Sync);
        p.syncToHost (5.25);
        CHECK_NEAR (p.readPosition (0.0f), 0.25, 1e-9);
        p.noteOn(); // ignored in Sync mode
        CHECK_NEAR (p.readPosition (0.0f), 0.25, 1e-9);
    });

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

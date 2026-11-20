// Offline host test: drives the real processor (parameters, MIDI, transport, state) without a DAW.
#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

namespace
{
    struct FakePlayHead final : juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        bool playing = true;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            info.setIsPlaying (playing);
            return info;
        }
    };

    void setParam (ShaperProcessor& p, const juce::String& id, float plainValue)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    /** Renders a constant DC input of 1 and returns the output of channel 0 (= the applied gain). */
    std::vector<float> renderGain (ShaperProcessor& p, FakePlayHead& head, int numSamples, int blockSize,
                                   std::function<void (int, juce::MidiBuffer&)> midiAt = {})
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int start = 0; start < numSamples; start += blockSize)
        {
            buffer.clear();
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, blockSize);

            midi.clear();
            if (midiAt)
                midiAt (start, midi);

            p.processBlock (buffer, midi);
            head.ppq += blockSize * (head.bpm / 60.0) / 48000.0;

            for (int i = 0; i < blockSize; ++i)
                out.push_back (buffer.getSample (0, i));
        }

        return out;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    constexpr double sr = 48000.0;
    constexpr int block = 480;

    {
        std::printf ("[test] sync mode follows the host tempo\n");
        ShaperProcessor p;
        FakePlayHead head;
        p.setPlayHead (&head);
        setParam (p, ParamIDs::smooth, 0.0f);
        p.setCurve (0, shaper::ShapeCurve::fromPreset (shaper::CurvePreset::SawUp));
        p.prepareToPlay (sr, block);

        // 1/4 at 120 bpm = 0.5 s = 24000 samples per cycle.
        auto g = renderGain (p, head, 48000, block);
        CHECK (std::abs (g[0] - 0.0f) < 1e-3f);
        CHECK (std::abs (g[12000] - 0.5f) < 1e-3f);
        CHECK (std::abs (g[24000] - 0.0f) < 1e-3f);
        CHECK (std::abs (g[36000] - 0.5f) < 1e-3f);
    }

    {
        std::printf ("[test] depth and mix scale the effect\n");
        ShaperProcessor p;
        FakePlayHead head;
        p.setPlayHead (&head);
        setParam (p, ParamIDs::smooth, 0.0f);
        setParam (p, ParamIDs::depth (0), 0.5f);
        p.setCurve (0, shaper::ShapeCurve::fromPreset (shaper::CurvePreset::SawUp));
        p.prepareToPlay (sr, block);
        auto g = renderGain (p, head, 24000, block);
        CHECK (std::abs (g[0] - 0.5f) < 1e-3f);    // 1 - 0.5 * (1 - 0)
        CHECK (std::abs (g[12000] - 0.75f) < 1e-3f);
    }

    {
        std::printf ("[test] smoothing removes the click of a square wave\n");
        ShaperProcessor p;
        FakePlayHead head;
        p.setPlayHead (&head);
        setParam (p, ParamIDs::smooth, 5.0f);
        p.setCurve (0, shaper::ShapeCurve::fromPreset (shaper::CurvePreset::Square));
        p.prepareToPlay (sr, block);
        auto g = renderGain (p, head, 48000, block);
        float maxStep = 0.0f;
        for (size_t i = 1; i < g.size(); ++i)
            maxStep = std::max (maxStep, std::abs (g[i] - g[i - 1]));
        std::printf ("       max per-sample gain step with 5 ms smoothing: %.4f\n", maxStep);
        CHECK (maxStep < 0.01f);
    }

    {
        std::printf ("[test] MIDI note retriggers the curve sample-accurately\n");
        ShaperProcessor p;
        FakePlayHead head;
        head.playing = false;
        p.setPlayHead (&head);
        setParam (p, ParamIDs::smooth, 0.0f);
        setParam (p, ParamIDs::mode, 1.0f); // Retrigger
        p.setCurve (0, shaper::ShapeCurve::fromPreset (shaper::CurvePreset::SawUp));
        p.prepareToPlay (sr, block);

        const int noteSample = 10000 + 123;
        auto g = renderGain (p, head, 20000, block, [noteSample] (int start, juce::MidiBuffer& m)
        {
            if (noteSample >= start && noteSample < start + block)
                m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), noteSample - start);
        });
        CHECK (g[(size_t) noteSample - 1] > 0.3f);
        CHECK (std::abs (g[(size_t) noteSample]) < 1e-3f);
    }

    {
        std::printf ("[test] envelope mode plays once then holds\n");
        ShaperProcessor p;
        FakePlayHead head;
        p.setPlayHead (&head);
        setParam (p, ParamIDs::smooth, 0.0f);
        setParam (p, ParamIDs::mode, 2.0f); // Envelope
        p.setCurve (0, shaper::ShapeCurve::fromPreset (shaper::CurvePreset::Pump));
        p.prepareToPlay (sr, block);
        auto g = renderGain (p, head, 72000, block, [] (int start, juce::MidiBuffer& m)
        {
            if (start == 4800)
                m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        });
        CHECK (std::abs (g[100] - 1.0f) < 1e-3f);   // idle before the note: end of the pump = full volume
        CHECK (std::abs (g[4800]) < 1e-3f);         // note: drops to the start of the curve
        CHECK (std::abs (g[60000] - 1.0f) < 1e-3f); // finished and holding
    }

    {
        std::printf ("[test] multiband with flat curves is transparent, and bands are independent\n");
        ShaperProcessor p;
        FakePlayHead head;
        p.setPlayHead (&head);
        setParam (p, ParamIDs::smooth, 0.0f);
        setParam (p, ParamIDs::multiband, 1.0f);
        for (int b = 0; b < 3; ++b)
            p.setCurve (b, shaper::ShapeCurve::fromPreset (shaper::CurvePreset::Flat));
        p.setCurve (2, shaper::ShapeCurve::fromPreset (shaper::CurvePreset::SawUp));
        setParam (p, ParamIDs::depth (2), 0.0f); // high band curve present but depth 0
        p.prepareToPlay (sr, block);

        auto rmsOfSine = [&] (double freq)
        {
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            double inE = 0.0, outE = 0.0;
            for (int n = 0; n < 100; ++n)
            {
                for (int i = 0; i < block; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * (n * block + i) / sr));
                const auto in = buffer.getRMSLevel (0, 0, block);
                p.processBlock (buffer, midi);
                if (n > 50)
                {
                    inE += in * in;
                    outE += buffer.getRMSLevel (0, 0, block) * buffer.getRMSLevel (0, 0, block);
                }
            }
            return 10.0 * std::log10 (outE / inE);
        };

        for (double f : { 60.0, 1000.0, 8000.0 })
        {
            const double db = rmsOfSine (f);
            std::printf ("       %5.0f Hz: %+.3f dB\n", f, db);
            CHECK (std::abs (db) < 0.05);
        }

        setParam (p, ParamIDs::depth (2), 1.0f); // now only the high band is shaped
        const double lowDb = rmsOfSine (60.0);
        const double highDb = rmsOfSine (8000.0);
        std::printf ("       high band shaped: 60 Hz %+.2f dB, 8 kHz %+.2f dB\n", lowDb, highDb);
        CHECK (std::abs (lowDb) < 0.1);
        CHECK (highDb < -3.0);
    }

    {
        std::printf ("[test] state save / restore keeps curves and parameters\n");
        ShaperProcessor a;
        auto curve = shaper::ShapeCurve::fromPreset (shaper::CurvePreset::Sine);
        curve.paintStep (0.5f, 0.625f, 0.2f);
        a.setCurve (1, curve);
        setParam (a, ParamIDs::rate, 7.0f);
        setParam (a, ParamIDs::xoverLow, 321.0f);

        juce::MemoryBlock state;
        a.getStateInformation (state);

        ShaperProcessor b;
        b.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (b.getCurve (1) == curve);
        CHECK (b.getCurve (0) == shaper::ShapeCurve::fromPreset (shaper::CurvePreset::Pump));
        CHECK (juce::roundToInt (b.apvts.getRawParameterValue (ParamIDs::rate)->load()) == 7);
        CHECK (std::abs (b.apvts.getRawParameterValue (ParamIDs::xoverLow)->load() - 321.0f) < 0.5f);

        // A second save of the restored instance must be identical (no state drift).
        juce::MemoryBlock state2;
        b.getStateInformation (state2);
        CHECK (state == state2);
    }

    if (argc > 1)
    {
        std::printf ("[snapshot] rendering the editor to %s\n", argv[1]);
        ShaperProcessor p;
        FakePlayHead head;
        p.setPlayHead (&head);
        setParam (p, ParamIDs::multiband, 1.0f);
        auto c = shaper::ShapeCurve::fromPreset (shaper::CurvePreset::Pump);
        c.addPoint (0.7f, 0.85f);
        c.setTension (2, -0.5f);
        p.setCurve (0, c);
        p.prepareToPlay (sr, block);

        // Feed some audio so the scope has content.
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        juce::Random rng (1);
        for (int n = 0; n < 60; ++n)
        {
            for (int i = 0; i < block; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    buffer.setSample (ch, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.6f * std::exp (-(float) ((n * block + i) % 12000) / 3000.0f));
            p.processBlock (buffer, midi);
            head.ppq += block * 2.0 / sr;
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setSize (920, 580);

        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        editor.reset();
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL HOST TESTS PASSED" : "HOST TESTS FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

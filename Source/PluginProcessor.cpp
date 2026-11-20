#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace shaper;

//==============================================================================
ShaperProcessor::ShaperProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ShaperState", createLayout())
{
    pMode      = apvts.getRawParameterValue (ParamIDs::mode);
    pSyncRate  = apvts.getRawParameterValue (ParamIDs::syncRate);
    pRate      = apvts.getRawParameterValue (ParamIDs::rate);
    pRateType  = apvts.getRawParameterValue (ParamIDs::rateType);
    pRateHz    = apvts.getRawParameterValue (ParamIDs::rateHz);
    pPhase     = apvts.getRawParameterValue (ParamIDs::phase);
    pSmooth    = apvts.getRawParameterValue (ParamIDs::smooth);
    pMultiband = apvts.getRawParameterValue (ParamIDs::multiband);
    pXoverLow  = apvts.getRawParameterValue (ParamIDs::xoverLow);
    pXoverHigh = apvts.getRawParameterValue (ParamIDs::xoverHigh);
    pMix       = apvts.getRawParameterValue (ParamIDs::mix);
    pOutput    = apvts.getRawParameterValue (ParamIDs::output);

    for (int b = 0; b < numBands; ++b)
    {
        pDepth[(size_t) b]  = apvts.getRawParameterValue (ParamIDs::depth (b));
        pBandOn[(size_t) b] = apvts.getRawParameterValue (ParamIDs::bandOn (b));
    }

    for (auto& bin : inputScope)
        bin.store (0.0f);

    for (int b = 0; b < numBands; ++b)
        setCurve (b, ShapeCurve::fromPreset (CurvePreset::Pump));

    pullTablesFromMessageThread();
}

juce::AudioProcessorValueTreeState::ParameterLayout ShaperProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray divisions;
    for (auto* name : syncDivisionNames)
        divisions.add (name);

    StringArray types;
    for (auto* name : syncTypeNames)
        types.add (name);

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; });

    auto hz = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (roundToInt (v)) + " Hz"; });

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::mode, 1 }, "Mode",
                                                        StringArray { "Host sync", "MIDI retrigger", "MIDI envelope" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::syncRate, 1 }, "Tempo Sync", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::rate, 1 }, "Rate", divisions, 5));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::rateType, 1 }, "Rate Type", types, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::rateHz, 1 }, "Rate Hz",
                                                       NormalisableRange<float> (0.01f, 40.0f, 0.0f, 0.3f), 2.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (
                                                           [] (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::phase, 1 }, "Phase",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (
                                                           [] (float v, int) { return String (roundToInt (v * 360.0f)) + String::fromUTF8 ("\xc2\xb0"); })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::smooth, 1 }, "Smooth",
                                                       NormalisableRange<float> (0.0f, 50.0f, 0.0f, 0.5f), 2.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (
                                                           [] (float v, int) { return String (v, 1) + " ms"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::multiband, 1 }, "Multiband", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::xoverLow, 1 }, "Crossover Low",
                                                       NormalisableRange<float> (20.0f, 2000.0f, 0.0f, 0.3f), 200.0f, hz));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::xoverHigh, 1 }, "Crossover High",
                                                       NormalisableRange<float> (300.0f, 16000.0f, 0.0f, 0.3f), 2500.0f, hz));

    const char* bandNames[] = { "Low", "Mid", "High" };
    for (int b = 0; b < numBands; ++b)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::depth (b), 1 }, String (bandNames[b]) + " Depth",
                                                           NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::bandOn (b), 1 }, String (bandNames[b]) + " On", true));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::mix, 1 }, "Mix",
                                                       NormalisableRange<float> (0.0f, 1.0f), 1.0f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::output, 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 12.0f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (
                                                           [] (float v, int) { return String (v, 1) + " dB"; })));
    return layout;
}

//==============================================================================
bool ShaperProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void ShaperProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    crossover.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    crossover.reset();

    xoverLowSmoothed.reset (sampleRate, 0.05);
    xoverHighSmoothed.reset (sampleRate, 0.05);
    xoverLowSmoothed.setCurrentAndTargetValue (pXoverLow->load());
    xoverHighSmoothed.setCurrentAndTargetValue (pXoverHigh->load());
    crossover.setFrequencies (pXoverLow->load(), pXoverHigh->load());

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    mixAmount.reset (sampleRate, 0.02);
    mixAmount.setCurrentAndTargetValue (pMix->load());

    phaseEngine.setMode ((TriggerMode) juce::roundToInt (pMode->load()));
    phaseEngine.reset();

    pullTablesFromMessageThread();

    for (int b = 0; b < numBands; ++b)
        smoothedGain[(size_t) b] = lookup (b, phaseEngine.readPosition (pPhase->load()));
}

//==============================================================================
shaper::ShapeCurve ShaperProcessor::getCurve (int band) const
{
    std::lock_guard<std::mutex> lock (curveMutex);
    return curves[(size_t) juce::jlimit (0, numBands - 1, band)];
}

void ShaperProcessor::setCurve (int band, const shaper::ShapeCurve& curve)
{
    band = juce::jlimit (0, numBands - 1, band);

    std::array<float, tableSize + 1> rendered;
    curve.renderTable (rendered.data(), tableSize);

    {
        std::lock_guard<std::mutex> lock (curveMutex);
        curves[(size_t) band] = curve;
    }

    const juce::SpinLock::ScopedLockType lock (tableLock);
    sharedTables[(size_t) band] = rendered;
    tablesDirty = true;
}

void ShaperProcessor::pullTablesFromMessageThread() noexcept
{
    // Never blocks: if the editor is writing right now, the new tables are picked up next block.
    const juce::SpinLock::ScopedTryLockType lock (tableLock);

    if (lock.isLocked() && tablesDirty)
    {
        audioTables = sharedTables;
        tablesDirty = false;
    }
}

float ShaperProcessor::lookup (int band, float position) const noexcept
{
    const auto& table = audioTables[(size_t) band];
    const float pos = juce::jlimit (0.0f, 1.0f, position) * (float) tableSize;
    const int i0 = juce::jmin ((int) pos, tableSize - 1);
    const float frac = pos - (float) i0;
    return table[(size_t) i0] + frac * (table[(size_t) i0 + 1] - table[(size_t) i0]);
}

//==============================================================================
void ShaperProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (getTotalNumInputChannels(), getTotalNumOutputChannels(), 2);

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    pullTablesFromMessageThread();

    // --- Parameters -----------------------------------------------------------
    phaseEngine.setMode ((TriggerMode) juce::roundToInt (pMode->load()));

    const bool tempoSync = pSyncRate->load() > 0.5f;
    const int rateIndex = juce::jlimit (0, numSyncDivisions - 1, juce::roundToInt (pRate->load()));
    const int typeIndex = juce::jlimit (0, 2, juce::roundToInt (pRateType->load()));
    const double beatsPerCycle = syncDivisionBeats[rateIndex] * syncTypeFactors[typeIndex];
    const float phaseOffset = pPhase->load();
    const bool multiband = pMultiband->load() > 0.5f;
    const int activeBands = multiband ? numBands : 1;

    std::array<float, numBands> depth {};
    for (int b = 0; b < numBands; ++b)
        depth[(size_t) b] = pBandOn[(size_t) b]->load() > 0.5f ? pDepth[(size_t) b]->load() : 0.0f;

    const float smoothMs = pSmooth->load();
    const float smoothCoeff = smoothMs < 0.01f ? 1.0f
                                               : 1.0f - std::exp (-1.0f / (smoothMs * 0.001f * (float) currentSampleRate));

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    mixAmount.setTargetValue (pMix->load());

    // --- Host transport -------------------------------------------------------
    double bpm = 120.0;
    bool playing = false;
    juce::Optional<double> ppq;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto hostBpm = position->getBpm())
                bpm = juce::jlimit (20.0, 999.0, *hostBpm);

            playing = position->getIsPlaying();
            ppq = position->getPpqPosition();
        }
    }

    const double cyclesPerSecond = tempoSync ? (bpm / 60.0) / beatsPerCycle : (double) pRateHz->load();
    phaseEngine.setIncrement (cyclesPerSecond / currentSampleRate);

    if (tempoSync && playing && ppq.hasValue())
        phaseEngine.syncToHost (*ppq / beatsPerCycle);

    // --- Crossover ------------------------------------------------------------
    if (multiband && ! wasMultiband)
        crossover.reset(); // avoid stale filter state when the split is switched on

    wasMultiband = multiband;

    if (multiband)
    {
        xoverLowSmoothed.setTargetValue (pXoverLow->load());
        xoverHighSmoothed.setTargetValue (pXoverHigh->load());
        crossover.setFrequencies (xoverLowSmoothed.skip (numSamples), xoverHighSmoothed.skip (numSamples));
    }

    // --- Render ---------------------------------------------------------------
    auto midiIterator = midi.cbegin();
    std::array<float*, 2> channelData { buffer.getWritePointer (0), numChannels > 1 ? buffer.getWritePointer (1) : nullptr };
    float lastPosition = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Sample-accurate MIDI triggers.
        while (midiIterator != midi.cend() && (*midiIterator).samplePosition <= i)
        {
            if ((*midiIterator).getMessage().isNoteOn())
                phaseEngine.noteOn();

            ++midiIterator;
        }

        const float position = phaseEngine.readPosition (phaseOffset);
        lastPosition = position;

        // Gains: depth scales the reduction, then a one-pole smoother removes clicks on jumps.
        std::array<float, numBands> gain { 1.0f, 1.0f, 1.0f };
        for (int b = 0; b < activeBands; ++b)
        {
            const float target = 1.0f - depth[(size_t) b] * (1.0f - lookup (b, position));
            auto& g = smoothedGain[(size_t) b];
            g += smoothCoeff * (target - g);
            gain[(size_t) b] = g;
        }

        const float mixNow = mixAmount.getNextValue();
        const float outNow = outputGain.getNextValue();
        float inputPeak = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = channelData[(size_t) ch];
            const float x = data[i];
            inputPeak = juce::jmax (inputPeak, std::abs (x));

            float dry, wet;

            if (multiband)
            {
                float low, mid, high;
                crossover.process (ch, x, low, mid, high);
                dry = low + mid + high; // phase-matched dry signal for the mix
                wet = low * gain[0] + mid * gain[1] + high * gain[2];
            }
            else
            {
                dry = x;
                wet = x * gain[0];
            }

            data[i] = (dry + mixNow * (wet - dry)) * outNow;
        }

        // Input scope, binned by curve position.
        const int bin = juce::jlimit (0, scopeBins - 1, (int) (position * (float) scopeBins));
        if (bin != scopeCurrentBin)
        {
            inputScope[(size_t) scopeCurrentBin].store (scopeCurrentPeak, std::memory_order_relaxed);
            scopeCurrentBin = bin;
            scopeCurrentPeak = 0.0f;
        }
        scopeCurrentPeak = juce::jmax (scopeCurrentPeak, inputPeak);

        phaseEngine.advance();
    }

    displayPhase.store (lastPosition, std::memory_order_relaxed);
}

//==============================================================================
void ShaperProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    juce::ValueTree curvesTree ("CURVES");
    for (int b = 0; b < numBands; ++b)
        curvesTree.setProperty ("band" + juce::String (b + 1), juce::String (getCurve (b).toString()), nullptr);

    state.removeChild (state.getChildWithName ("CURVES"), nullptr);
    state.appendChild (curvesTree, nullptr);
    state.setProperty ("uiBand", uiSelectedBand.load(), nullptr);
    state.setProperty ("uiGrid", uiGridIndex.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ShaperProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    auto curvesTree = state.getChildWithName ("CURVES");

    for (int b = 0; b < numBands; ++b)
    {
        ShapeCurve curve;
        if (curvesTree.isValid() && ShapeCurve::fromString (curvesTree.getProperty ("band" + juce::String (b + 1)).toString().toStdString(), curve))
            setCurve (b, curve);
    }

    uiSelectedBand = juce::jlimit (0, numBands - 1, (int) state.getProperty ("uiBand", 0));
    uiGridIndex = (int) state.getProperty ("uiGrid", uiGridIndex.load());

    state.removeChild (curvesTree, nullptr);
    apvts.replaceState (state);
}

//==============================================================================
juce::AudioProcessorEditor* ShaperProcessor::createEditor()
{
    return new ShaperEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ShaperProcessor();
}

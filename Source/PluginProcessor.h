#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <mutex>

#include "dsp/Crossover.h"
#include "dsp/PhaseEngine.h"
#include "dsp/ShapeCurve.h"

namespace ParamIDs
{
    inline constexpr auto mode       = "mode";
    inline constexpr auto syncRate   = "syncRate";
    inline constexpr auto rate       = "rate";
    inline constexpr auto rateType   = "rateType";
    inline constexpr auto rateHz     = "rateHz";
    inline constexpr auto phase      = "phase";
    inline constexpr auto smooth     = "smooth";
    inline constexpr auto bands      = "bands";
    inline constexpr auto xoverLow   = "xoverLow";
    inline constexpr auto xoverHigh  = "xoverHigh";
    inline constexpr auto mix        = "mix";
    inline constexpr auto output     = "output";

    inline juce::String depth (int band)  { return "depth" + juce::String (band + 1); }
    inline juce::String bandOn (int band) { return "bandOn" + juce::String (band + 1); }
}

class ShaperProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int numBands = 3;
    static constexpr int tableSize = 2048;
    static constexpr int scopeBins = 256;

    ShaperProcessor();

    /** Number of bands currently in use (1 = no crossover, no phase shift). */
    int getActiveBandCount() const noexcept;
    /** Adds a band at the top of the spectrum, copying the current top band so the sound does not change. */
    void addBand();
    /** Removes the top band. */
    void removeBand();
    static juce::String bandName (int band, int bandCount);
    ~ShaperProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Curve access: message thread / state thread only (never the audio thread).
    shaper::ShapeCurve getCurve (int band) const;
    void setCurve (int band, const shaper::ShapeCurve& curve);

    juce::AudioProcessorValueTreeState apvts;

    // Values read by the editor.
    std::atomic<float> displayPhase { 0.0f };
    std::array<std::atomic<float>, scopeBins> inputScope {};
    std::atomic<int> uiSelectedBand { 0 };
    std::atomic<int> uiGridIndex { 5 };
    std::atomic<bool> uiShowOtherBands { true };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullTablesFromMessageThread() noexcept;
    float lookup (int band, float position) const noexcept;

    // Curve models (message thread), protected for hosts that save state from other threads.
    mutable std::mutex curveMutex;
    std::array<shaper::ShapeCurve, numBands> curves;

    // Rendered tables handed to the audio thread without blocking it.
    juce::SpinLock tableLock;
    std::array<std::array<float, tableSize + 1>, numBands> sharedTables {};
    bool tablesDirty = true; // guarded by tableLock
    std::array<std::array<float, tableSize + 1>, numBands> audioTables {};

    // DSP state (audio thread).
    shaper::PhaseEngine phaseEngine;
    shaper::Crossover3 crossover;
    std::array<float, numBands> smoothedGain { 1.0f, 1.0f, 1.0f };
    juce::SmoothedValue<float> outputGain { 1.0f }, mixAmount { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> xoverLowSmoothed { 200.0f }, xoverHighSmoothed { 2500.0f };
    double currentSampleRate = 44100.0;
    int lastActiveBands = 1;
    int scopeCurrentBin = 0;
    float scopeCurrentPeak = 0.0f;

    // Cached parameter pointers.
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pSyncRate = nullptr;
    std::atomic<float>* pRate = nullptr;
    std::atomic<float>* pRateType = nullptr;
    std::atomic<float>* pRateHz = nullptr;
    std::atomic<float>* pPhase = nullptr;
    std::atomic<float>* pSmooth = nullptr;
    std::atomic<float>* pBands = nullptr;
    std::atomic<float>* pXoverLow = nullptr;
    std::atomic<float>* pXoverHigh = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::array<std::atomic<float>*, numBands> pDepth {}, pBandOn {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperProcessor)
};

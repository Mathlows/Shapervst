#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"
#include "ui/CurveEditor.h"
#include "ui/Theme.h"

class ShaperEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit ShaperEditor (ShaperProcessor&);
    ~ShaperEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const juce::String& name, juce::Colour accent);
    void attachKnob (Knob&, const juce::String& paramId);
    void selectBand (int band);
    void updateEnablement();

    ShaperProcessor& processor;
    Theme::LookAndFeel lookAndFeel;

    juce::Label title;
    std::array<juce::TextButton, ShaperProcessor::numBands> bandButtons;
    juce::Label gridLabel;
    juce::ComboBox gridBox;
    CurveEditor curveEditor;

    juce::ComboBox modeBox, rateBox, rateTypeBox;
    juce::ToggleButton syncToggle { "BPM" }, multibandToggle { "Multiband" }, bandOnToggle { "Band on" };
    Knob rateHz, phase, smooth, xoverLow, xoverHigh, depth, mix, output;

    std::unique_ptr<ComboAttachment> modeAttachment, rateAttachment, rateTypeAttachment;
    std::unique_ptr<ButtonAttachment> syncAttachment, multibandAttachment, bandOnAttachment;

    bool lastMultiband = false;
    bool lastSync = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShaperEditor)
};

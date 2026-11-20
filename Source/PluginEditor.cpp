#include "PluginEditor.h"

namespace
{
    constexpr int gridOptions[] = { 0, 2, 3, 4, 6, 8, 12, 16, 24, 32 };
}

ShaperEditor::ShaperEditor (ShaperProcessor& p)
    : AudioProcessorEditor (&p), processor (p), curveEditor (p)
{
    setLookAndFeel (&lookAndFeel);

    title.setText ("SHAPER", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, Theme::text);
    addAndMakeVisible (title);

    for (int b = 0; b < ShaperProcessor::numBands; ++b)
    {
        auto& button = bandButtons[(size_t) b];
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1001);
        button.onClick = [this, b] { if (bandButtons[(size_t) b].getToggleState()) selectBand (b); };
        addAndMakeVisible (button);
    }

    addBandButton.setTooltip ("Add a band (copies the top band, the sound does not change)");
    removeBandButton.setTooltip ("Remove the top band");
    addBandButton.onClick = [this]
    {
        processor.addBand();
        timerCallback();
        selectBand (processor.getActiveBandCount() - 1);
    };
    removeBandButton.onClick = [this] { processor.removeBand(); timerCallback(); };
    addAndMakeVisible (addBandButton);
    addAndMakeVisible (removeBandButton);

    showOthersToggle.setTooltip ("Show the other bands' curves behind the one being edited");
    showOthersToggle.setToggleState (processor.uiShowOtherBands.load(), juce::dontSendNotification);
    curveEditor.setShowOtherBands (processor.uiShowOtherBands.load());
    showOthersToggle.onClick = [this]
    {
        processor.uiShowOtherBands = showOthersToggle.getToggleState();
        curveEditor.setShowOtherBands (showOthersToggle.getToggleState());
    };
    addAndMakeVisible (showOthersToggle);

    gridLabel.setText ("Grid", juce::dontSendNotification);
    gridLabel.setJustificationType (juce::Justification::centredRight);
    gridLabel.setColour (juce::Label::textColourId, Theme::textDim);
    addAndMakeVisible (gridLabel);

    for (int i = 0; i < (int) std::size (gridOptions); ++i)
        gridBox.addItem (gridOptions[i] == 0 ? juce::String ("Off") : juce::String (gridOptions[i]), i + 1);

    gridBox.onChange = [this]
    {
        const int index = gridBox.getSelectedItemIndex();
        processor.uiGridIndex = index;
        curveEditor.setGridDivisions (gridOptions[juce::jlimit (0, (int) std::size (gridOptions) - 1, index)]);
    };
    gridBox.setSelectedItemIndex (juce::jlimit (0, (int) std::size (gridOptions) - 1, processor.uiGridIndex.load()), juce::sendNotificationSync);
    addAndMakeVisible (gridBox);

    addAndMakeVisible (curveEditor);

    // Timing section.
    auto& params = processor.apvts;
    modeBox.addItemList (params.getParameter (ParamIDs::mode)->getAllValueStrings(), 1);
    rateBox.addItemList (params.getParameter (ParamIDs::rate)->getAllValueStrings(), 1);
    rateTypeBox.addItemList (params.getParameter (ParamIDs::rateType)->getAllValueStrings(), 1);
    modeAttachment = std::make_unique<ComboAttachment> (params, ParamIDs::mode, modeBox);
    rateAttachment = std::make_unique<ComboAttachment> (params, ParamIDs::rate, rateBox);
    rateTypeAttachment = std::make_unique<ComboAttachment> (params, ParamIDs::rateType, rateTypeBox);
    syncAttachment = std::make_unique<ButtonAttachment> (params, ParamIDs::syncRate, syncToggle);

    for (auto* c : { &modeBox, &rateBox, &rateTypeBox })
        addAndMakeVisible (*c);

    for (auto* t : { &syncToggle, &bandOnToggle })
        addAndMakeVisible (*t);

    const auto neutral = Theme::band (1);
    setupKnob (rateHz, "Rate Hz", neutral);
    setupKnob (phase, "Phase", neutral);
    setupKnob (smooth, "Smooth", neutral);
    setupKnob (xoverLow, "Low / Mid", neutral);
    setupKnob (xoverHigh, "Mid / High", neutral);
    setupKnob (depth, "Depth", Theme::band (0));
    setupKnob (mix, "Mix", neutral);
    setupKnob (output, "Output", neutral);

    attachKnob (rateHz, ParamIDs::rateHz);
    attachKnob (phase, ParamIDs::phase);
    attachKnob (smooth, ParamIDs::smooth);
    attachKnob (xoverLow, ParamIDs::xoverLow);
    attachKnob (xoverHigh, ParamIDs::xoverHigh);
    attachKnob (mix, ParamIDs::mix);
    attachKnob (output, ParamIDs::output);

    lastBandCount = processor.getActiveBandCount();
    lastSync = params.getRawParameterValue (ParamIDs::syncRate)->load() > 0.5f;
    updateEnablement();
    selectBand (processor.uiSelectedBand.load());

    setResizable (true, true);
    setResizeLimits (760, 480, 1800, 1100);
    setSize (920, 580);
    startTimerHz (15);
}

ShaperEditor::~ShaperEditor()
{
    setLookAndFeel (nullptr);
}

void ShaperEditor::setupKnob (Knob& knob, const juce::String& name, juce::Colour accent)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
    knob.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setColour (juce::Label::textColourId, Theme::textDim);
    knob.label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (knob.slider);
    addAndMakeVisible (knob.label);
}

void ShaperEditor::attachKnob (Knob& knob, const juce::String& paramId)
{
    knob.attachment.reset();
    knob.attachment = std::make_unique<SliderAttachment> (processor.apvts, paramId, knob.slider);
}

juce::Colour ShaperEditor::colourFor (int band) const
{
    // With 2 bands the top one is "High": use the high colour.
    return Theme::band (lastBandCount == 2 && band == 1 ? 2 : band);
}

void ShaperEditor::selectBand (int band)
{
    band = juce::jlimit (0, lastBandCount - 1, band);
    processor.uiSelectedBand = band;
    bandButtons[(size_t) band].setToggleState (true, juce::dontSendNotification);
    curveEditor.setBand (band, lastBandCount, colourFor (band));

    attachKnob (depth, ParamIDs::depth (band));
    depth.slider.setColour (juce::Slider::rotarySliderFillColourId, colourFor (band));
    bandOnAttachment.reset();
    bandOnAttachment = std::make_unique<ButtonAttachment> (processor.apvts, ParamIDs::bandOn (band), bandOnToggle);
    repaint();
}

void ShaperEditor::updateEnablement()
{
    std::array<juce::Colour, ShaperProcessor::numBands> colours;

    for (int b = 0; b < ShaperProcessor::numBands; ++b)
    {
        auto& button = bandButtons[(size_t) b];
        button.setVisible (b < lastBandCount);
        button.setButtonText (ShaperProcessor::bandName (b, lastBandCount));
        button.setColour (juce::TextButton::buttonOnColourId, colourFor (b));
        colours[(size_t) b] = colourFor (b);
    }

    curveEditor.setBandColours (colours);
    addBandButton.setEnabled (lastBandCount < ShaperProcessor::numBands);
    removeBandButton.setVisible (lastBandCount > 1);
    showOthersToggle.setVisible (lastBandCount > 1);

    xoverLow.label.setText (lastBandCount == 2 ? "Low / High" : "Low / Mid", juce::dontSendNotification);
    xoverLow.slider.setEnabled (lastBandCount >= 2);
    xoverHigh.slider.setEnabled (lastBandCount >= 3);

    rateBox.setEnabled (lastSync);
    rateTypeBox.setEnabled (lastSync);
    rateHz.slider.setEnabled (! lastSync);
}

void ShaperEditor::timerCallback()
{
    const int bandCount = processor.getActiveBandCount();
    const bool sync = processor.apvts.getRawParameterValue (ParamIDs::syncRate)->load() > 0.5f;

    if (bandCount != lastBandCount || sync != lastSync)
    {
        const bool bandsChanged = bandCount != lastBandCount;
        lastBandCount = bandCount;
        lastSync = sync;
        updateEnablement();

        if (bandsChanged)
        {
            selectBand (juce::jmin (curveEditor.getBand(), bandCount - 1));
            resized();
        }
    }
}

//==============================================================================
void ShaperEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    auto footer = getLocalBounds().removeFromBottom (150).reduced (12, 8).toFloat();
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (footer, 6.0f);
}

void ShaperEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header.
    auto header = bounds.removeFromTop (48).reduced (12, 10);
    title.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (8);
    for (auto& button : bandButtons)
    {
        if (! button.isVisible())
            continue;

        button.setBounds (header.removeFromLeft (64));
        header.removeFromLeft (4);
    }

    header.removeFromLeft (4);
    addBandButton.setBounds (header.removeFromLeft (28));
    header.removeFromLeft (4);
    removeBandButton.setBounds (header.removeFromLeft (28));
    gridBox.setBounds (header.removeFromRight (72));
    gridLabel.setBounds (header.removeFromRight (44));
    header.removeFromRight (12);
    showOthersToggle.setBounds (header.removeFromRight (120));

    // Footer.
    auto footer = bounds.removeFromBottom (150).reduced (20, 14);

    // Timing column: combo boxes.
    auto timing = footer.removeFromLeft (150);
    const int rowH = 26;
    modeBox.setBounds (timing.removeFromTop (rowH));
    timing.removeFromTop (6);
    auto rateRow = timing.removeFromTop (rowH);
    syncToggle.setBounds (rateRow.removeFromRight (60));
    rateBox.setBounds (rateRow);
    timing.removeFromTop (6);
    rateTypeBox.setBounds (timing.removeFromTop (rowH));

    footer.removeFromLeft (12);

    // Knobs.
    Knob* knobs[] = { &rateHz, &phase, &smooth, &xoverLow, &xoverHigh, &depth, &mix, &output };
    const int knobCount = (int) std::size (knobs);
    auto bandOnArea = footer.removeFromRight (0);
    const int knobW = footer.getWidth() / knobCount;

    for (auto* knob : knobs)
    {
        auto cell = footer.removeFromLeft (knobW);

        if (knob == &depth)
        {
            bandOnArea = cell.removeFromBottom (20);
        }

        knob->label.setBounds (cell.removeFromTop (16));
        knob->slider.setBounds (cell);
    }

    bandOnToggle.setBounds (bandOnArea.withSizeKeepingCentre (juce::jmin (90, bandOnArea.getWidth()), 20));

    // Curve editor fills the rest.
    curveEditor.setBounds (bounds.reduced (12, 0).withTrimmedBottom (4));
}

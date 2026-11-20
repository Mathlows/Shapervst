#pragma once

#include <JuceHeader.h>

namespace Theme
{
    inline const juce::Colour background { 0xff121419 };
    inline const juce::Colour panel      { 0xff1b1e25 };
    inline const juce::Colour panelLight { 0xff262a33 };
    inline const juce::Colour grid       { 0xff2c313b };
    inline const juce::Colour gridStrong { 0xff3a404c };
    inline const juce::Colour text       { 0xffd8dce4 };
    inline const juce::Colour textDim    { 0xff8a919e };

    inline juce::Colour band (int index)
    {
        switch (index)
        {
            case 1:  return juce::Colour (0xff5ad1c8); // mid: teal
            case 2:  return juce::Colour (0xffa98bff); // high: violet
            default: return juce::Colour (0xffffa24a); // low / full: orange
        }
    }

    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setColour (juce::ResizableWindow::backgroundColourId, background);
            setColour (juce::Label::textColourId, text);
            setColour (juce::ComboBox::backgroundColourId, panelLight);
            setColour (juce::ComboBox::outlineColourId, grid);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::arrowColourId, textDim);
            setColour (juce::PopupMenu::backgroundColourId, panel);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, panelLight);
            setColour (juce::TextButton::buttonColourId, panelLight);
            setColour (juce::TextButton::textColourOffId, textDim);
            setColour (juce::TextButton::textColourOnId, background);
            setColour (juce::ToggleButton::textColourId, text);
            setColour (juce::ToggleButton::tickColourId, band (1));
            setColour (juce::Slider::textBoxTextColourId, text);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::rotarySliderOutlineColourId, panelLight);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const float lineW = juce::jmax (2.5f, radius * 0.16f);
            const float arcRadius = radius - lineW * 0.5f;
            const float angle = startAngle + pos * (endAngle - startAngle);
            const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
            g.setColour (panelLight);
            g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
            g.setColour (slider.isEnabled() ? accent : accent.withAlpha (0.3f));
            g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const auto tip = centre.getPointOnCircumference (arcRadius - lineW * 1.6f, angle);
            g.setColour (text);
            g.drawLine ({ centre.getPointOnCircumference (arcRadius * 0.25f, angle), tip }, 2.0f);
        }
    };
}

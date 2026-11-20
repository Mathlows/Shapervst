#pragma once

#include <JuceHeader.h>

#include "../PluginProcessor.h"

/**
    Breakpoint editor modelled on Serum's LFO editor.

      Double-click empty space       add a point
      Double-click a point           remove it
      Drag a point                   move it (x snaps to the grid)
      Drag a curve handle (o)        bend the segment, double-click resets it
      Alt + drag                     paint steps on the grid
      Shift + drag                   fine adjustment
      Ctrl/Cmd + drag                bypass grid snapping
      Right-click                    presets, invert, flip, copy/paste
*/
class CurveEditor final : public juce::Component,
                          private juce::Timer
{
public:
    explicit CurveEditor (ShaperProcessor& processor);

    void setBand (int newBand);
    int getBand() const noexcept { return band; }
    void setGridDivisions (int divisions);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Target { None, Point, Handle };
    struct Hit { Target target = Target::None; int index = -1; };

    void timerCallback() override;

    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen (float x, float y) const;
    juce::Point<float> toCurve (juce::Point<float> screen) const;
    Hit hitTest (juce::Point<float> screen, const shaper::ShapeCurve& curve) const;
    float snapX (float x, const juce::ModifierKeys& mods) const;
    void paintAt (juce::Point<float> screen);
    void showContextMenu();
    void commit (const shaper::ShapeCurve& curve);

    ShaperProcessor& processor;
    int band = 0;
    int gridDivisions = 8;

    Hit hover, drag;
    bool painting = false;
    juce::Point<float> dragStartMouse;
    shaper::CurvePoint dragStartPoint;
    float dragStartTension = 0.0f;
    float dragDirection = 1.0f;

    std::array<float, ShaperProcessor::scopeBins> scopeDisplay {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveEditor)
};

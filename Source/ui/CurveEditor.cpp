#include "CurveEditor.h"
#include "Theme.h"

using namespace shaper;

namespace
{
    constexpr float pointRadius = 5.0f;
    constexpr float handleRadius = 4.0f;
    constexpr float hitRadius = 9.0f;
    constexpr float fineFactor = 0.15f;
    const juce::String clipboardPrefix = "shapervst-curve:";
}

CurveEditor::CurveEditor (ShaperProcessor& p) : processor (p)
{
    setRepaintsOnMouseActivity (false);
    startTimerHz (60);
}

void CurveEditor::setBand (int newBand, int bandCount, juce::Colour colour)
{
    activeBandCount = juce::jlimit (1, ShaperProcessor::numBands, bandCount);
    band = juce::jlimit (0, activeBandCount - 1, newBand);
    bandColour = colour;
    hover = drag = {};
    repaint();
}

void CurveEditor::setGridDivisions (int divisions)
{
    gridDivisions = juce::jmax (0, divisions);
    repaint();
}

void CurveEditor::timerCallback()
{
    for (size_t i = 0; i < scopeDisplay.size(); ++i)
        scopeDisplay[i] = juce::jmax (processor.inputScope[i].load (std::memory_order_relaxed), scopeDisplay[i] * 0.9f);

    repaint();
}

//==============================================================================
juce::Rectangle<float> CurveEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (12.0f, 10.0f);
}

juce::Point<float> CurveEditor::toScreen (float x, float y) const
{
    const auto area = plotArea();
    return { area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight() };
}

juce::Point<float> CurveEditor::toCurve (juce::Point<float> screen) const
{
    const auto area = plotArea();
    return { (screen.x - area.getX()) / area.getWidth(), (area.getBottom() - screen.y) / area.getHeight() };
}

float CurveEditor::snapX (float x, const juce::ModifierKeys& mods) const
{
    x = juce::jlimit (0.0f, 1.0f, x);

    if (gridDivisions <= 0 || mods.isCommandDown() || mods.isCtrlDown())
        return x;

    return std::round (x * (float) gridDivisions) / (float) gridDivisions;
}

CurveEditor::Hit CurveEditor::hitTest (juce::Point<float> screen, const ShapeCurve& curve) const
{
    const auto& pts = curve.points();
    Hit best;
    float bestDistance = hitRadius;

    for (int i = 0; i < (int) pts.size(); ++i)
    {
        const float d = screen.getDistanceFrom (toScreen (pts[(size_t) i].x, pts[(size_t) i].y));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = { Target::Point, i };
        }
    }

    if (best.target == Target::Point)
        return best;

    for (int i = 0; i + 1 < (int) pts.size(); ++i)
    {
        const auto& a = pts[(size_t) i];
        const auto& b = pts[(size_t) i + 1];
        const float widthPx = (b.x - a.x) * plotArea().getWidth();

        if (widthPx < 14.0f || std::abs (b.y - a.y) < 1.0e-3f)
            continue;

        const float d = screen.getDistanceFrom (toScreen ((a.x + b.x) * 0.5f, curve.segmentMidValue (i)));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = { Target::Handle, i };
        }
    }

    return best;
}

void CurveEditor::commit (const ShapeCurve& curve)
{
    processor.setCurve (band, curve);
    repaint();
}

//==============================================================================
void CurveEditor::paint (juce::Graphics& g)
{
    const auto area = plotArea();
    const auto colour = bandColour;
    const auto curve = processor.getCurve (band);
    const auto& pts = curve.points();

    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // Input level, binned by curve position (like ShaperBox / LFO Tool backgrounds).
    {
        const float binWidth = area.getWidth() / (float) scopeDisplay.size();
        g.setColour (Theme::textDim.withAlpha (0.16f));

        for (size_t i = 0; i < scopeDisplay.size(); ++i)
        {
            const float h = juce::jmin (1.0f, scopeDisplay[i]) * area.getHeight();
            g.fillRect (area.getX() + (float) i * binWidth, area.getBottom() - h, binWidth + 0.5f, h);
        }
    }

    // Grid.
    for (int k = 1; k < 4; ++k)
    {
        const float y = area.getY() + area.getHeight() * (float) k / 4.0f;
        g.setColour (k == 2 ? Theme::gridStrong : Theme::grid);
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
    }

    if (gridDivisions > 0)
    {
        for (int k = 1; k < gridDivisions; ++k)
        {
            const float x = area.getX() + area.getWidth() * (float) k / (float) gridDivisions;
            g.setColour (gridDivisions % 2 == 0 && k % (gridDivisions / 2) == 0 ? Theme::gridStrong : Theme::grid);
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        }
    }

    g.setColour (Theme::gridStrong);
    g.drawRect (area, 1.0f);

    // The other bands' curves, faintly, so the bands can be compared while editing one of them.
    for (int b = 0; showOtherBands && b < activeBandCount; ++b)
    {
        if (b == band)
            continue;

        g.setColour (otherColours[(size_t) b].withAlpha (0.35f));
        g.strokePath (curvePath (processor.getCurve (b)), juce::PathStrokeType (1.5f));
    }

    const auto line = curvePath (curve);

    juce::Path fill (line);
    fill.lineTo (area.getBottomRight());
    fill.lineTo (area.getBottomLeft());
    fill.closeSubPath();
    g.setGradientFill ({ colour.withAlpha (0.35f), area.getX(), area.getY(),
                         colour.withAlpha (0.03f), area.getX(), area.getBottom(), false });
    g.fillPath (fill);

    g.setColour (colour);
    g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Tension handles.
    for (int i = 0; i + 1 < (int) pts.size(); ++i)
    {
        const auto& a = pts[(size_t) i];
        const auto& b = pts[(size_t) i + 1];

        if ((b.x - a.x) * area.getWidth() < 14.0f || std::abs (b.y - a.y) < 1.0e-3f)
            continue;

        const bool active = (hover.target == Target::Handle && hover.index == i) || (drag.target == Target::Handle && drag.index == i);
        const auto c = toScreen ((a.x + b.x) * 0.5f, curve.segmentMidValue (i));
        const float r = active ? handleRadius + 1.5f : handleRadius;
        g.setColour (Theme::panel);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (colour.withAlpha (active ? 1.0f : 0.7f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.5f);
    }

    // Points.
    for (int i = 0; i < (int) pts.size(); ++i)
    {
        const bool active = (hover.target == Target::Point && hover.index == i) || (drag.target == Target::Point && drag.index == i);
        const auto c = toScreen (pts[(size_t) i].x, pts[(size_t) i].y);
        const float r = active ? pointRadius + 2.0f : pointRadius;
        g.setColour (active ? Theme::text : colour);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (Theme::panel);
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.5f);
    }

    // Playhead.
    const float position = processor.displayPhase.load (std::memory_order_relaxed);
    const auto head = toScreen (position, curve.evaluate (position));
    g.setColour (Theme::text.withAlpha (0.35f));
    g.drawVerticalLine ((int) head.x, area.getY(), area.getBottom());
    g.setColour (Theme::text);
    g.fillEllipse (head.x - 3.5f, head.y - 3.5f, 7.0f, 7.0f);

    // Tooltip-style readout while dragging a point.
    if (drag.target == Target::Point && drag.index < (int) pts.size())
    {
        const auto& p = pts[(size_t) drag.index];
        const auto text = juce::String (p.x * 100.0f, 1) + " %  |  " + juce::String (p.y * 100.0f, 1) + " %";
        g.setColour (Theme::text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (text, area.reduced (6.0f).removeFromTop (16.0f), juce::Justification::topRight);
    }
}

juce::Path CurveEditor::curvePath (const ShapeCurve& curve) const
{
    // Sampled per pixel so tensions and jumps are exact.
    const auto area = plotArea();
    const auto& pts = curve.points();
    juce::Path line;
    const int columns = juce::jmax (2, (int) area.getWidth());

    for (int c = 0; c <= columns; ++c)
    {
        const float x = (float) c / (float) columns;
        const auto p = toScreen (x, curve.evaluate (x));
        c == 0 ? line.startNewSubPath (p) : line.lineTo (p);
    }

    // Vertical jumps: draw the exact edges.
    for (int i = 0; i + 1 < (int) pts.size(); ++i)
        if (pts[(size_t) i + 1].x - pts[(size_t) i].x < 1.0e-6f)
            line.addLineSegment ({ toScreen (pts[(size_t) i].x, pts[(size_t) i].y), toScreen (pts[(size_t) i + 1].x, pts[(size_t) i + 1].y) }, 0.0f);

    return line;
}

//==============================================================================
void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto newHover = hitTest (e.position, processor.getCurve (band));

    if (newHover.target != hover.target || newHover.index != hover.index)
    {
        hover = newHover;
        setMouseCursor (hover.target == Target::Handle ? juce::MouseCursor::UpDownResizeCursor
                        : hover.target == Target::Point ? juce::MouseCursor::DraggingHandCursor
                                                        : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CurveEditor::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }

    const auto curve = processor.getCurve (band);
    drag = hitTest (e.position, curve);
    dragStartMouse = e.position;
    painting = false;

    if (drag.target == Target::Point)
    {
        dragStartPoint = curve.points()[(size_t) drag.index];
    }
    else if (drag.target == Target::Handle)
    {
        const auto& a = curve.points()[(size_t) drag.index];
        const auto& b = curve.points()[(size_t) drag.index + 1];
        dragStartTension = a.tension;
        dragDirection = b.y >= a.y ? 1.0f : -1.0f; // dragging up always bends the curve up
    }
    else if (e.mods.isAltDown())
    {
        painting = true;
        paintAt (e.position);
    }
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (painting)
    {
        paintAt (e.position);
        return;
    }

    if (drag.target == Target::None)
        return;

    auto curve = processor.getCurve (band);
    if (drag.index >= curve.numPoints())
        return;

    const float scale = e.mods.isShiftDown() ? fineFactor : 1.0f;
    const auto area = plotArea();
    const auto delta = (e.position - dragStartMouse) * scale;

    if (drag.target == Target::Point)
    {
        float x = dragStartPoint.x + delta.x / area.getWidth();
        float y = dragStartPoint.y - delta.y / area.getHeight();

        if (! e.mods.isShiftDown())
            x = snapX (x, e.mods);

        curve.movePoint (drag.index, x, y);
    }
    else
    {
        const float tension = dragStartTension - dragDirection * 2.5f * delta.y / area.getHeight();
        curve.setTension (drag.index, tension);
    }

    commit (curve);
}

void CurveEditor::mouseUp (const juce::MouseEvent& e)
{
    drag = {};
    painting = false;
    mouseMove (e);
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.mods.isAltDown())
        return;

    auto curve = processor.getCurve (band);
    const auto hit = hitTest (e.position, curve);

    if (hit.target == Target::Point)
    {
        curve.removePoint (hit.index);
    }
    else if (hit.target == Target::Handle)
    {
        curve.setTension (hit.index, 0.0f);
    }
    else
    {
        const auto p = toCurve (e.position);
        curve.addPoint (snapX (p.x, e.mods), juce::jlimit (0.0f, 1.0f, p.y));
    }

    drag = {};
    commit (curve);
    mouseMove (e);
}

void CurveEditor::paintAt (juce::Point<float> screen)
{
    const auto p = toCurve (screen);
    const int divisions = gridDivisions > 0 ? gridDivisions : 16;
    const int cell = juce::jlimit (0, divisions - 1, (int) std::floor (p.x * (float) divisions));

    auto curve = processor.getCurve (band);
    curve.paintStep ((float) cell / (float) divisions, (float) (cell + 1) / (float) divisions, juce::jlimit (0.0f, 1.0f, p.y));
    commit (curve);
}

//==============================================================================
void CurveEditor::showContextMenu()
{
    juce::PopupMenu presets;
    const std::pair<const char*, CurvePreset> presetList[] = {
        { "Pump (sidechain)", CurvePreset::Pump }, { "Sine", CurvePreset::Sine },       { "Triangle", CurvePreset::Triangle },
        { "Saw up", CurvePreset::SawUp },          { "Saw down", CurvePreset::SawDown }, { "Square", CurvePreset::Square },
        { "Gate", CurvePreset::Gate },             { "Flat", CurvePreset::Flat },
    };

    for (int i = 0; i < (int) std::size (presetList); ++i)
        presets.addItem (100 + i, presetList[i].first);

    const bool canPaste = juce::SystemClipboard::getTextFromClipboard().startsWith (clipboardPrefix);

    juce::PopupMenu menu;
    menu.addSubMenu ("Load shape", presets);
    menu.addSeparator();
    menu.addItem (1, "Invert (vertical flip)");
    menu.addItem (2, "Reverse (horizontal flip)");
    menu.addSeparator();
    menu.addItem (3, "Copy curve");
    menu.addItem (4, "Paste curve", canPaste);
    menu.addItem (5, "Copy to all bands");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [safe = juce::Component::SafePointer<CurveEditor> (this), presetList] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;

                            auto& self = *safe;
                            auto curve = self.processor.getCurve (self.band);

                            if (result >= 100)
                            {
                                curve = ShapeCurve::fromPreset (presetList[result - 100].second);
                            }
                            else if (result == 1)
                            {
                                curve.invert();
                            }
                            else if (result == 2)
                            {
                                curve.flipHorizontal();
                            }
                            else if (result == 3)
                            {
                                juce::SystemClipboard::copyTextToClipboard (clipboardPrefix + juce::String (curve.toString()));
                                return;
                            }
                            else if (result == 4)
                            {
                                const auto text = juce::SystemClipboard::getTextFromClipboard().fromFirstOccurrenceOf (clipboardPrefix, false, false);
                                if (! ShapeCurve::fromString (text.toStdString(), curve))
                                    return;
                            }
                            else if (result == 5)
                            {
                                for (int b = 0; b < self.activeBandCount; ++b)
                                    self.processor.setCurve (b, curve);
                                self.repaint();
                                return;
                            }

                            self.commit (curve);
                        });
}

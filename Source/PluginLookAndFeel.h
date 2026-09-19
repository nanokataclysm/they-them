#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theythem
{
namespace palette
{
inline const juce::Colour background { 0xff101419 }, panel { 0xff1a2028 }, border { 0xff2b3541 };
inline const juce::Colour text { 0xffedf1f5 }, muted { 0xff98a6b8 }, mint { 0xff99e6c5 };
inline const juce::Colour lilac { 0xffc1adf5 }, amber { 0xffefc27a }, red { 0xffff858e };
}

class VocalLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    VocalLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, palette::text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, palette::lilac.withAlpha (0.3f));
        setColour (juce::Slider::thumbColourId, palette::mint);
        setColour (juce::ComboBox::backgroundColourId, palette::panel);
        setColour (juce::ComboBox::textColourId, palette::text);
        setColour (juce::ComboBox::outlineColourId, palette::border);
        setColour (juce::ComboBox::arrowColourId, palette::mint);
        setColour (juce::PopupMenu::backgroundColourId, palette::panel);
        setColour (juce::PopupMenu::textColourId, palette::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::border);
        setColour (juce::PopupMenu::highlightedTextColourId, palette::mint);
        setColour (juce::TextButton::buttonColourId, palette::panel);
        setColour (juce::TextButton::textColourOffId, palette::text);
        setColour (juce::TooltipWindow::backgroundColourId, palette::border);
        setColour (juce::TooltipWindow::textColourId, palette::text);
        setColour (juce::TooltipWindow::outlineColourId, palette::muted);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (14.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (15.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool over, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? palette::border.brighter (0.15f) : over ? palette::border : palette::panel);
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (button.hasKeyboardFocus (true) ? palette::mint : palette::border);
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool over, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto accent = button.findColour (juce::ToggleButton::tickColourId);
        const bool on = button.getToggleState();
        g.setColour (on ? accent.withAlpha (down ? 0.25f : 0.12f) : over ? palette::border : palette::background);
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (on || button.hasKeyboardFocus (true) ? accent.withAlpha (0.65f) : palette::border);
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        g.setColour (on ? accent : palette::muted);
        g.fillEllipse (bounds.getX() + 10.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (24), juce::Justification::centredLeft);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                          float position, float start, float end, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                              static_cast<float> (width), static_cast<float> (height)).reduced (13.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = start + position * (end - start);
        const auto accent = slider.findColour (juce::Slider::thumbColourId);
        const auto arc = [&] (float a, float b, juce::Colour colour, float thickness)
        {
            juce::Path path;
            path.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, a, b, true);
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        };
        arc (start, end, palette::border, 5.0f);
        // Zero semitones is at twelve o'clock, independent of factory defaults.
        arc (juce::jmin ((start + end) * 0.5f, angle), juce::jmax ((start + end) * 0.5f, angle), accent, 5.0f);
        const float faceRadius = radius - 11.0f;
        const auto face = juce::Rectangle<float> (faceRadius * 2.0f, faceRadius * 2.0f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (palette::border.brighter (0.06f), face.getTopLeft(),
                                                 palette::background, face.getBottomRight(), false));
        g.fillEllipse (face);
        g.setColour (slider.hasKeyboardFocus (true) ? accent : palette::border.brighter (0.2f));
        g.drawEllipse (face, 1.0f);
        const auto tip = centre.getPointOnCircumference (faceRadius - 8.0f, angle);
        const auto tail = centre.getPointOnCircumference (faceRadius * 0.56f, angle);
        g.setColour (accent);
        g.drawLine ({ tail, tip }, 3.0f);
        g.setColour (palette::muted);
        g.fillEllipse (centre.x - 1.5f, centre.y - radius - 10.0f, 3.0f, 3.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float, float, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        const float cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
        const float left = static_cast<float> (x), right = static_cast<float> (x + width);
        g.setColour (palette::border);
        g.drawLine (left, cy, right, cy, 4.0f);
        g.setColour (slider.findColour (juce::Slider::thumbColourId));
        g.drawLine (left, cy, position, cy, 4.0f);
        g.fillEllipse (position - 5.0f, cy - 5.0f, 10.0f, 10.0f);
        if (slider.hasKeyboardFocus (true))
            g.drawEllipse (position - 8.0f, cy - 8.0f, 16.0f, 16.0f, 1.0f);
    }
};
} // namespace theythem

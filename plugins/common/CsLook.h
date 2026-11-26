// Shared look: dark, phosphor green, Tufte-minimal. No boxes, no knobs, no
// decoration: a control is a label, a hairline and a number.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cs::look {

inline const juce::Colour background{0xff0a0d0b};
// Phosphor green: luminance x (0.35, 1.0, 0.45), as in the scope renderer.
inline const juce::Colour phosphor = juce::Colour::fromFloatRGBA(0.35f, 1.0f, 0.45f, 1.0f);
inline const juce::Colour text = phosphor.withAlpha(0.85f);
inline const juce::Colour dim = phosphor.withAlpha(0.45f);
inline const juce::Colour faint = phosphor.withAlpha(0.16f);

inline juce::Font font(float height = 13.0f) { return juce::Font(juce::FontOptions(height)); }

class LookAndFeel final : public juce::LookAndFeel_V4 {
public:
  LookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Label::textColourId, text);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, faint);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::backgroundColourId, background);
    setColour(juce::TextEditor::highlightColourId, faint);
    setColour(juce::TextEditor::highlightedTextColourId, phosphor);
    setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::CaretComponent::caretColourId, phosphor);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::arrowColourId, dim);
    setColour(juce::PopupMenu::backgroundColourId, background.brighter(0.06f));
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, faint);
    setColour(juce::PopupMenu::highlightedTextColourId, phosphor);
  }

  // A slider is a hairline with a brighter stretch up to its value.
  void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                        juce::Slider::SliderStyle, juce::Slider&) override {
    const float base = static_cast<float>(y + height) - 1.5f;
    g.setColour(faint);
    g.fillRect(static_cast<float>(x), base, static_cast<float>(width), 1.0f);
    g.setColour(phosphor.withAlpha(0.8f));
    g.fillRect(static_cast<float>(x), base, juce::jmax(1.0f, sliderPos - static_cast<float>(x)), 1.0f);
  }

  juce::Label* createSliderTextBox(juce::Slider& slider) override {
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->setJustificationType(juce::Justification::centredRight);
    label->setFont(font());
    label->setColour(juce::Label::textColourId, text);
    label->setColour(juce::Label::textWhenEditingColourId, phosphor);
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::outlineWhenEditingColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::backgroundWhenEditingColourId, background);
    return label;
  }

  void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&) override {
    g.setColour(faint);
    g.fillRect(0.0f, static_cast<float>(height) - 1.5f, static_cast<float>(width), 1.0f);
  }
  juce::Font getComboBoxFont(juce::ComboBox&) override { return font(); }
  void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
    label.setBounds(0, 0, box.getWidth(), box.getHeight());
    label.setFont(font());
    label.setJustificationType(juce::Justification::centredRight);
  }
  juce::Font getPopupMenuFont() override { return font(); }
  juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(); }
  // A button is its text over a hairline, like every other control.
  void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) override {
    const auto area = button.getLocalBounds().toFloat();
    if (down || over) {
      g.setColour(faint.withMultipliedAlpha(down ? 1.0f : 0.5f));
      g.fillRect(area);
    }
    g.setColour(faint);
    g.fillRect(area.getX(), area.getBottom() - 1.5f, area.getWidth(), 1.0f);
  }
  juce::Font getLabelFont(juce::Label&) override { return font(); }
};

} // namespace cs::look

// A column of parameter rows: label on the left, control on the right,
// grouped under small headings. Sliders for numbers, menus for choices.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "CsLook.h"

namespace cs {

class ParamPanel final : public juce::Component {
public:
  static constexpr int kRowHeight = 23, kHeadingHeight = 26, kLabelWidth = 108;

  explicit ParamPanel(juce::AudioProcessorValueTreeState& state) : state_(state) {}

  void heading(const juce::String& title) { rows_.push_back({title.toUpperCase(), nullptr, nullptr, true}); }

  void add(const juce::String& paramId) {
    auto* param = state_.getParameter(paramId);
    jassert(param != nullptr);
    Row row;
    row.title = param->getName(64);
    row.label = std::make_unique<juce::Label>(juce::String(), row.title);
    row.label->setColour(juce::Label::textColourId, look::dim);
    row.label->setFont(look::font());
    addAndMakeVisible(*row.label);
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param)) {
      auto box = std::make_unique<juce::ComboBox>();
      box->addItemList(choice->choices, 1);
      box->setJustificationType(juce::Justification::centredRight);
      comboAttachments_.push_back(
          std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state_, paramId, *box));
      row.control = std::move(box);
    } else {
      auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::TextBoxRight);
      slider->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
      slider->setColour(juce::Slider::textBoxTextColourId, look::text);
      slider->setSliderSnapsToMousePosition(false);
      slider->setMouseDragSensitivity(220);
      sliderAttachments_.push_back(
          std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state_, paramId, *slider));
      row.control = std::move(slider);
    }
    addAndMakeVisible(*row.control);
    rows_.push_back(std::move(row));
    alignValues();
  }

  // Numbers sit flush right, like the menu values above and below them.
  void lookAndFeelChanged() override { alignValues(); }
  void parentHierarchyChanged() override { alignValues(); }

  int preferredHeight() const {
    int h = 0;
    for (const Row& r : rows_)
      h += r.isHeading ? kHeadingHeight : kRowHeight;
    return h;
  }

  void resized() override {
    int y = 0;
    for (Row& r : rows_) {
      if (r.isHeading) {
        y += kHeadingHeight;
        continue;
      }
      r.label->setBounds(0, y, kLabelWidth, kRowHeight - 3);
      r.control->setBounds(kLabelWidth, y, getWidth() - kLabelWidth, kRowHeight - 3);
      y += kRowHeight;
    }
  }

  void paint(juce::Graphics& g) override {
    g.setFont(look::font(10.5f));
    g.setColour(look::dim);
    int y = 0;
    for (const Row& r : rows_) {
      if (r.isHeading)
        g.drawText(r.title, 0, y + 8, getWidth(), kHeadingHeight - 10, juce::Justification::bottomLeft);
      y += r.isHeading ? kHeadingHeight : kRowHeight;
    }
  }

private:
  void alignValues() {
    for (Row& r : rows_)
      if (auto* slider = dynamic_cast<juce::Slider*>(r.control.get()))
        for (auto* child : slider->getChildren())
          if (auto* label = dynamic_cast<juce::Label*>(child))
            label->setJustificationType(juce::Justification::centredRight);
  }

  struct Row {
    juce::String title;
    std::unique_ptr<juce::Label> label;
    std::unique_ptr<juce::Component> control;
    bool isHeading = false;
  };
  juce::AudioProcessorValueTreeState& state_;
  std::vector<Row> rows_;
  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments_;
  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments_;
};

} // namespace cs

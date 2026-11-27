// A column of parameter rows: label on the left, control on the right,
// grouped under small headings. Sliders for numbers, menus for choices.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

#include "CsLook.h"

namespace cs {

class ParamPanel final : public juce::Component {
public:
  static constexpr int kRowHeight = 23, kHeadingHeight = 26, kLabelWidth = 108;

  explicit ParamPanel(juce::AudioProcessorValueTreeState& state) : state_(state) {}

  // Start again with an empty panel.
  void clear() {
    sliderAttachments_.clear();
    comboAttachments_.clear();
    rows_.clear(); // the rows own the controls
    removeAllChildren();
  }

  // A button across the full width, under its own small heading.
  void addButton(const juce::String& title, const juce::String& text, std::function<void()> onClick) {
    heading(title);
    Row row;
    row.height = kRowHeight + 2;
    row.fullWidth = true;
    auto button = std::make_unique<juce::TextButton>(text);
    button->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    button->setColour(juce::TextButton::buttonOnColourId, look::faint);
    button->setColour(juce::TextButton::textColourOffId, look::text);
    button->setColour(juce::TextButton::textColourOnId, look::phosphor);
    button->setComponentID("button");
    button->onClick = std::move(onClick);
    row.control = std::move(button);
    addAndMakeVisible(*row.control);
    rows_.push_back(std::move(row));
  }

  // A menu that picks one of `options` through a parameter, labelled `title`.
  void addChoice(const juce::String& paramId, const juce::String& title) { add(paramId, title); }

  // A menu at the top that applies a preset; it holds no state of its own.
  void addPresets(const juce::StringArray& names, std::function<void(int)> onChoose) {
    Row row;
    row.title = "Preset";
    row.label = std::make_unique<juce::Label>(juce::String(), row.title);
    row.label->setColour(juce::Label::textColourId, look::dim);
    row.label->setFont(look::font());
    addAndMakeVisible(*row.label);
    auto box = std::make_unique<juce::ComboBox>();
    box->addItemList(names, 1);
    box->setTextWhenNothingSelected("choose");
    box->setJustificationType(juce::Justification::centredRight);
    auto* raw = box.get();
    box->onChange = [raw, choose = std::move(onChoose)] {
      if (raw->getSelectedItemIndex() >= 0)
        choose(raw->getSelectedItemIndex());
    };
    row.control = std::move(box);
    addAndMakeVisible(*row.control);
    rows_.push_back(std::move(row));
  }

  // A line of text to type into, across the full width, under its own small heading.
  void addText(const juce::String& title, const juce::String& initial, std::function<void(const juce::String&)> onChange) {
    heading(title);
    Row row;
    row.height = kRowHeight + 6;
    row.fullWidth = true;
    auto editor = std::make_unique<juce::TextEditor>();
    editor->setFont(look::font(14.0f));
    editor->setColour(juce::TextEditor::textColourId, look::text);
    editor->setText(initial, false);
    editor->applyColourToAllText(look::text);
    editor->setIndents(2, 4);
    auto* raw = editor.get();
    editor->onTextChange = [raw, changed = std::move(onChange)] { changed(raw->getText()); };
    row.control = std::move(editor);
    addAndMakeVisible(*row.control);
    rows_.push_back(std::move(row));
  }

  void heading(const juce::String& title) {
    Row row;
    row.title = title.toUpperCase();
    row.isHeading = true;
    row.height = kHeadingHeight;
    rows_.push_back(std::move(row));
  }

  // `title`: the label; by default the parameter's name, less any "Group: " in front of it.
  void add(const juce::String& paramId, const juce::String& title = {}) {
    auto* param = state_.getParameter(paramId);
    jassert(param != nullptr);
    Row row;
    const juce::String name = param->getName(64);
    row.title = title.isNotEmpty() ? title : name.contains(": ") ? name.fromFirstOccurrenceOf(": ", false, false) : name;
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
      h += r.height;
    return h;
  }

  void resized() override {
    int y = 0;
    for (Row& r : rows_) {
      if (r.fullWidth)
        r.control->setBounds(0, y, getWidth(), r.height - 3);
      else if (!r.isHeading) {
        r.label->setBounds(0, y, kLabelWidth, r.height - 3);
        r.control->setBounds(kLabelWidth, y, getWidth() - kLabelWidth, r.height - 3);
      }
      y += r.height;
    }
  }

  void paint(juce::Graphics& g) override {
    g.setFont(look::font(10.5f));
    g.setColour(look::dim);
    int y = 0;
    for (const Row& r : rows_) {
      if (r.isHeading)
        g.drawText(r.title, 0, y + 8, getWidth(), kHeadingHeight - 10, juce::Justification::bottomLeft);
      else if (r.fullWidth) { // a hairline under the text, as under every other control
        g.setColour(look::faint);
        g.fillRect(0.0f, static_cast<float>(y + r.height) - 4.5f, static_cast<float>(getWidth()), 1.0f);
        g.setColour(look::dim);
      }
      y += r.height;
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
    bool isHeading = false, fullWidth = false;
    int height = kRowHeight;
  };
  juce::AudioProcessorValueTreeState& state_;
  std::vector<Row> rows_;
  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments_;
  std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments_;
};

} // namespace cs

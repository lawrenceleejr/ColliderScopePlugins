// The editor both plugins share: the scope on the left, the parameters in
// one column on the right.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

#include "CsLook.h"
#include "ParamPanel.h"
#include "Presets.h"
#include "ScopeDisplay.h"

namespace cs {

struct ParamGroup {
  const char* title;
  std::vector<const char*> ids;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
  // `fill` lays the panel out. When `layoutVersion` is given and its value
  // changes, the panel is emptied and filled again: for a plugin whose
  // controls depend on a choice.
  Editor(juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state, ScopeFeed& feed,
         const char* decayParamId, std::function<void(ParamPanel&)> fill, std::function<int()> layoutVersion = nullptr)
      : AudioProcessorEditor(processor),
        scope_(feed),
        panel_(state),
        decay_(state.getRawParameterValue(decayParamId)),
        fill_(std::move(fill)),
        layoutVersion_(std::move(layoutVersion)) {
    setLookAndFeel(&look_);
    addAndMakeVisible(scope_);
    addAndMakeVisible(panel_); // parented first, so its controls are born with this look
    refill();
    startTimerHz(10);
  }

  // The fixed layout: an optional preset menu and own controls, then groups of parameters.
  Editor(juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state, ScopeFeed& feed,
         const char* decayParamId, const std::vector<ParamGroup>& groups, const std::vector<Preset>* presets = nullptr,
         std::function<void(ParamPanel&)> extra = nullptr)
      : Editor(processor, state, feed, decayParamId, [&state, groups, presets, extra](ParamPanel& panel) {
          addPresetMenu(panel, state, presets);
          if (extra)
            extra(panel); // an instrument's own controls, above its parameters
          addGroups(panel, groups);
        }) {}

  static void addPresetMenu(ParamPanel& panel, juce::AudioProcessorValueTreeState& state, const std::vector<Preset>* presets) {
    if (presets == nullptr || presets->empty())
      return;
    juce::StringArray names;
    for (const Preset& preset : *presets)
      names.add(preset.name);
    panel.addPresets(names, [&state, presets](int index) { applyPreset(state, (*presets)[static_cast<std::size_t>(index)]); });
  }

  static void addGroups(ParamPanel& panel, const std::vector<ParamGroup>& groups) {
    for (const ParamGroup& group : groups) {
      panel.heading(group.title);
      for (const char* id : group.ids)
        panel.add(id);
    }
  }

  ~Editor() override {
    stopTimer();
    setLookAndFeel(nullptr);
  }

  void paint(juce::Graphics& g) override { g.fillAll(look::background); }

  void resized() override {
    scope_.setBounds(kMargin, kMargin, ScopeDisplay::kSize, ScopeDisplay::kSize);
    panel_.setBounds(kMargin + ScopeDisplay::kSize + kMargin, kMargin - 8, kPanelWidth, panel_.preferredHeight());
  }

private:
  static constexpr int kMargin = 16, kPanelWidth = 248;

  void refill() {
    version_ = layoutVersion_ ? layoutVersion_() : 0;
    panel_.clear();
    fill_(panel_);
    setSize(kMargin + ScopeDisplay::kSize + kMargin + kPanelWidth + kMargin,
            kMargin + juce::jmax(ScopeDisplay::kSize, panel_.preferredHeight() - 8) + kMargin);
    resized();
    panel_.repaint();
  }

  void timerCallback() override {
    scope_.setDecay(decay_->load());
    if (layoutVersion_ && layoutVersion_() != version_)
      refill();
  }

  look::LookAndFeel look_;
  ScopeDisplay scope_;
  ParamPanel panel_;
  std::atomic<float>* decay_;
  std::function<void(ParamPanel&)> fill_;
  std::function<int()> layoutVersion_;
  int version_ = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

} // namespace cs

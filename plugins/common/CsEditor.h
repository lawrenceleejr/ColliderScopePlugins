// The editor both plugins share: the scope on the left, the parameters in
// one column on the right.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <initializer_list>
#include <utility>
#include <vector>

#include "CsLook.h"
#include "ParamPanel.h"
#include "ScopeDisplay.h"

namespace cs {

struct ParamGroup {
  const char* title;
  std::vector<const char*> ids;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
  Editor(juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state, ScopeFeed& feed,
         const char* decayParamId, const std::vector<ParamGroup>& groups)
      : AudioProcessorEditor(processor), scope_(feed), panel_(state), decay_(state.getRawParameterValue(decayParamId)) {
    setLookAndFeel(&look_);
    addAndMakeVisible(scope_);
    addAndMakeVisible(panel_); // parented first, so its controls are born with this look
    for (const ParamGroup& group : groups) {
      panel_.heading(group.title);
      for (const char* id : group.ids)
        panel_.add(id);
    }
    setSize(kMargin + ScopeDisplay::kSize + kMargin + kPanelWidth + kMargin,
            kMargin + juce::jmax(ScopeDisplay::kSize, panel_.preferredHeight() - 8) + kMargin);
    startTimerHz(10);
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

  void timerCallback() override { scope_.setDecay(decay_->load()); }

  look::LookAndFeel look_;
  ScopeDisplay scope_;
  ParamPanel panel_;
  std::atomic<float>* decay_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};

} // namespace cs

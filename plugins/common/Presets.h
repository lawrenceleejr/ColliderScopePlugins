// Presets: a name and the parameter values that make it, in the units the
// UI shows (a choice parameter takes the index of its option).
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>
#include <vector>

namespace cs {

struct Preset {
  const char* name;
  std::vector<std::pair<const char*, float>> values;
};

inline void applyPreset(juce::AudioProcessorValueTreeState& state, const Preset& preset) {
  for (const auto& [id, value] : preset.values) {
    if (auto* param = state.getParameter(id)) {
      param->beginChangeGesture();
      param->setValueNotifyingHost(param->convertTo0to1(value));
      param->endChangeGesture();
    }
  }
}

} // namespace cs

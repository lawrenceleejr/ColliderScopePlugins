#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class MuxProcessor;

class MuxEditor final : public juce::AudioProcessorEditor {
public:
  explicit MuxEditor(MuxProcessor&);
  void paint(juce::Graphics&) override;

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MuxEditor)
};

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ChamberProcessor;

class ChamberEditor final : public juce::AudioProcessorEditor {
public:
  explicit ChamberEditor(ChamberProcessor&);
  void paint(juce::Graphics&) override;

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChamberEditor)
};

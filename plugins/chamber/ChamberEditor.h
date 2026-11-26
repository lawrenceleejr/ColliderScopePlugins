#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "CsLook.h"
#include "ParamPanel.h"
#include "ScopeDisplay.h"

class ChamberProcessor;

// The scope on the left, the parameters in one column on the right.
class ChamberEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit ChamberEditor(ChamberProcessor&);
  ~ChamberEditor() override;
  void paint(juce::Graphics&) override;
  void resized() override;

private:
  void timerCallback() override;

  ChamberProcessor& processor_;
  cs::look::LookAndFeel look_;
  cs::ScopeDisplay scope_;
  cs::ParamPanel panel_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChamberEditor)
};

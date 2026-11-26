#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

#include "ScopeDisplay.h"
#include "cscore/mux_engine.h"

// CS Mux: serializes two stereo XY sources, A (main input) and B (sidechain),
// into one stereo XY output, so both pictures show without distorting each
// other. A second output pair carries a Z blanking pulse during jumps.
// MIDI (Slot mode): 36 kick, 38 snare, 49 crash; any other note opens a slot for B.
class MuxProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
  MuxProcessor();
  ~MuxProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using juce::AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }

  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }

  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock&) override;
  void setStateInformation(const void*, int) override;

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
  cs::MuxParams readParams() const noexcept;
  void handleMidi(const juce::MidiMessage&) noexcept;
  void timerCallback() override; // reports Cycle-lock latency to the host, off the audio thread

  juce::AudioProcessorValueTreeState state_;
  cs::MuxEngine engine_;
  cs::ScopeFeed scopeFeed_;
  std::vector<float> scratchBx_, scratchBy_, scratchZ_;
  std::atomic<int> engineLatency_{0};
  int pendingLatency_ = 0;

  std::atomic<float>*mode_, *slice_, *balance_, *switchPoint_, *jumpWeight_, *slotMs_, *sensitivity_, *ax_, *ay_,
      *aScale_, *aRotation_, *bx_, *by_, *bScale_, *bRotation_, *zLevel_, *inputs_, *outScaleX_, *outOffsetX_, *outScaleY_, *outOffsetY_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MuxProcessor)
};

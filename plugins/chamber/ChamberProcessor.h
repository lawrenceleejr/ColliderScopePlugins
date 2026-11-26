#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

#include "Presets.h"
#include "ScopeDisplay.h"
#include "cscore/chamber_engine.h"

// CS Chamber: monophonic MIDI instrument, stereo XY out (left = X, right = Y).
// Each note births a cloud-chamber track, drawn once per period at the
// note's pitch.
class ChamberProcessor final : public juce::AudioProcessor, private juce::Thread, private juce::Timer {
public:
  ChamberProcessor();
  ~ChamberProcessor() override;

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
  double getTailLengthSeconds() const override { return 2.0; } // the longest Release

  // The presets are also the host's programs.
  static const std::vector<cs::Preset>& presets();
  int getNumPrograms() override { return static_cast<int>(presets().size()); }
  int getCurrentProgram() override { return program_; }
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock&) override;
  void setStateInformation(const void*, int) override;

  juce::AudioProcessorValueTreeState& state() noexcept { return state_; }
  cs::ScopeFeed& scopeFeed() noexcept { return scopeFeed_; }

private:
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
  cs::ChamberParams readParams() const noexcept;
  void handleMidi(const juce::MidiMessage&) noexcept;
  void run() override; // the builder thread: makes picture tables off the audio thread
  void timerCallback() override; // reports Clean mode's latency to the host, off the audio thread

  juce::AudioProcessorValueTreeState state_;
  cs::ChamberEngine engine_;
  cs::ScopeFeed scopeFeed_;
  int program_ = 0;

  std::atomic<float>*type_, *seedMode_, *diffusion_, *drift_, *condense_, *wallMode_, *wallRoot_, *wallShare_, *voices_, *size_,
      *attack_, *release_, *glide_, *bendRange_, *quality_, *gain_, *scopeDecay_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChamberProcessor)
};

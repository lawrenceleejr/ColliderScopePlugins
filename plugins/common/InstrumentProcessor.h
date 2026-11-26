// The processor shared by the instruments built on cs::MonoEngine: MIDI in,
// stereo XY out, a builder thread that makes pictures off the audio thread,
// the scope feed, presets as host programs, state.
//
// An instrument derives from it and supplies its parameters (its own, plus
// the voice parameters every instrument has), how to read them, its editor
// groups and its presets.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

#include "CsEditor.h"
#include "Presets.h"
#include "ScopeDisplay.h"
#include "cscore/mono_engine.h"

namespace cs {

namespace param {

inline juce::ParameterID id(const char* name) { return {name, 1}; }

inline std::unique_ptr<juce::AudioParameterFloat> number(const char* paramId, const char* name,
                                                         juce::NormalisableRange<float> range, float fallback,
                                                         const char* unit, int decimals) {
  return std::make_unique<juce::AudioParameterFloat>(
      id(paramId), name, range, fallback,
      juce::AudioParameterFloatAttributes()
          .withStringFromValueFunction([unit, decimals](float v, int) {
            const juce::String u(unit);
            return juce::String(std::abs(v) < 1e-6f ? 0.0f : v, decimals) + (u.isEmpty() ? u : " " + u);
          })
          .withValueFromStringFunction([](const juce::String& s) { return s.getFloatValue(); }));
}

inline std::unique_ptr<juce::AudioParameterChoice> choice(const char* paramId, const char* name,
                                                          const juce::StringArray& options, int fallback) {
  return std::make_unique<juce::AudioParameterChoice>(id(paramId), name, options, fallback);
}

inline std::unique_ptr<juce::AudioParameterInt> whole(const char* paramId, const char* name, int lo, int hi, int fallback) {
  return std::make_unique<juce::AudioParameterInt>(id(paramId), name, lo, hi, fallback);
}

// The parameters every instrument's voice has.
inline void addVoice(juce::AudioProcessorValueTreeState::ParameterLayout& layout) {
  using Range = juce::NormalisableRange<float>;
  layout.add(number("size", "Size", Range(0.0f, 1.0f, 0.01f), 0.9f, "", 2));
  layout.add(number("attack", "Attack", Range(0.0f, 50.0f, 0.1f), 0.0f, "ms", 1));
  Range release(0.0f, 2000.0f, 1.0f);
  release.setSkewForCentre(300.0f);
  layout.add(number("release", "Release", release, 0.0f, "ms", 0));
  layout.add(number("glide", "Glide", Range(0.0f, 500.0f, 1.0f), 0.0f, "ms", 0));
  layout.add(number("bendRange", "Bend range", Range(0.0f, 12.0f, 1.0f), 2.0f, "st", 0));
  layout.add(choice("quality", "Quality", {"Scope-exact", "Clean"}, 0));
  layout.add(number("gain", "Output gain", Range(-24.0f, 0.0f, 0.1f), 0.0f, "dB", 1));
  layout.add(number("scopeDecay", "Persistence", Range(0.3f, 0.9f, 0.01f), 0.55f, "", 2));
}

inline const std::vector<const char*> kVoiceGroup = {"size", "attack", "release", "glide", "bendRange"};
inline const std::vector<const char*> kOutputGroup = {"quality", "gain", "scopeDecay"};

} // namespace param

template <typename EngineT>
class InstrumentProcessor : public juce::AudioProcessor, private juce::Thread, private juce::Timer {
public:
  using Params = typename EngineT::Params;

  InstrumentProcessor(const juce::String& stateName, juce::AudioProcessorValueTreeState::ParameterLayout layout)
      : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        juce::Thread(stateName + " builder"),
        state_(*this, nullptr, stateName, std::move(layout)) {
    size_ = raw("size");
    attack_ = raw("attack");
    release_ = raw("release");
    glide_ = raw("glide");
    bendRange_ = raw("bendRange");
    quality_ = raw("quality");
    gain_ = raw("gain");
    startThread();
    startTimerHz(10);
  }

  ~InstrumentProcessor() override {
    stopTimer();
    stopThread(2000);
  }

  void prepareToPlay(double sampleRate, int) override {
    engine_.setSynchronous(true); // keeps the builder thread out while the engine resets
    engine_.prepare(sampleRate);
    engine_.setParams(readParams());
  }
  void releaseResources() override {}

  bool isBusesLayoutSupported(const BusesLayout& layouts) const override {
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); // XY needs exactly two channels
  }

  void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) {
      buffer.clear();
      return;
    }
    // Offline bounces build pictures inline, sample-exact; live, the builder thread does.
    engine_.setSynchronous(isNonRealtime());
    engine_.setParams(readParams());
    float* x = buffer.getWritePointer(0);
    float* y = buffer.getWritePointer(1);
    int pos = 0;
    for (const auto meta : midi) {
      const int at = juce::jlimit(pos, n, meta.samplePosition);
      engine_.process(x + pos, y + pos, at - pos);
      pos = at;
      const auto m = meta.getMessage();
      if (m.isNoteOn())
        engine_.noteOn(m.getNoteNumber(), m.getVelocity());
      else if (m.isNoteOff())
        engine_.noteOff(m.getNoteNumber());
      else if (m.isPitchWheel())
        engine_.setPitchBend((m.getPitchWheelValue() - 8192) / 8192.0);
      else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine_.allNotesOff();
    }
    engine_.process(x + pos, y + pos, n - pos);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
      buffer.clear(ch, 0, n);
    scopeFeed_.push(x, y, n);
  }
  using juce::AudioProcessor::processBlock;

  bool hasEditor() const override { return true; }
  juce::AudioProcessorEditor* createEditor() override {
    return new Editor(*this, state_, scopeFeed_, "scopeDecay", groups(), &presets());
  }

  const juce::String getName() const override { return JucePlugin_Name; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 2.0; } // the longest Release

  // The presets are also the host's programs.
  int getNumPrograms() override { return juce::jmax(1, static_cast<int>(presets().size())); }
  int getCurrentProgram() override { return program_; }
  void setCurrentProgram(int index) override {
    if (index < 0 || index >= static_cast<int>(presets().size()))
      return;
    program_ = index;
    applyPreset(state_, presets()[static_cast<std::size_t>(index)]);
  }
  const juce::String getProgramName(int index) override {
    return index >= 0 && index < static_cast<int>(presets().size()) ? juce::String(presets()[static_cast<std::size_t>(index)].name)
                                                                    : juce::String();
  }
  void changeProgramName(int, const juce::String&) override {}

  void getStateInformation(juce::MemoryBlock& destination) override {
    if (const auto xml = state_.copyState().createXml())
      copyXmlToBinary(*xml, destination);
  }
  void setStateInformation(const void* data, int size) override {
    if (const auto xml = getXmlFromBinary(data, size))
      if (xml->hasTagName(state_.state.getType()))
        state_.replaceState(juce::ValueTree::fromXml(*xml));
  }

protected:
  virtual Params readParams() const noexcept = 0;
  virtual std::vector<ParamGroup> groups() const = 0;
  virtual const std::vector<Preset>& presets() const = 0;

  std::atomic<float>* raw(const char* paramId) { return state_.getRawParameterValue(paramId); }

  VoiceParams readVoice() const noexcept {
    VoiceParams v;
    v.size = size_->load();
    v.attackMs = attack_->load();
    v.releaseMs = release_->load();
    v.glideMs = glide_->load();
    v.bendRange = bendRange_->load();
    v.quality = static_cast<Quality>(juce::roundToInt(quality_->load()));
    v.gainDb = gain_->load();
    return v;
  }

  juce::AudioProcessorValueTreeState state_;

private:
  void run() override { // the builder thread
    while (!threadShouldExit())
      if (!engine_.service())
        wait(1);
  }
  void timerCallback() override { // Clean quality delays the output by 4 samples
    const int latency = juce::roundToInt(quality_->load()) == 1 ? Decimator4::kLatency : 0;
    if (latency != getLatencySamples())
      setLatencySamples(latency);
  }

  EngineT engine_;
  ScopeFeed scopeFeed_;
  int program_ = 0;
  std::atomic<float>*size_, *attack_, *release_, *glide_, *bendRange_, *quality_, *gain_;

  JUCE_DECLARE_NON_COPYABLE(InstrumentProcessor)
};

} // namespace cs

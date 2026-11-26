#include "MuxProcessor.h"

#include "CsEditor.h"

namespace {

using Float = juce::AudioParameterFloat;
using Choice = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

juce::ParameterID id(const char* name) { return {name, 1}; }

std::unique_ptr<Float> number(const char* paramId, const char* name, Range range, float fallback, const char* unit,
                              int decimals) {
  return std::make_unique<Float>(
      id(paramId), name, range, fallback,
      juce::AudioParameterFloatAttributes()
          .withStringFromValueFunction([unit, decimals](float v, int) {
            const juce::String u(unit);
            return juce::String(std::abs(v) < 1e-6f ? 0.0f : v, decimals) + (u.isEmpty() ? u : " " + u);
          })
          .withValueFromStringFunction([](const juce::String& s) { return s.getFloatValue(); }));
}

constexpr float kSliceAuto = 7.0f; // the bottom of the Slice range means Auto
constexpr int kMaxChunk = 4096;

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout MuxProcessor::createLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  layout.add(std::make_unique<Choice>(id("mode"), "Mode", juce::StringArray{"Interleave", "Cycle-lock", "Slot"}, 0));
  Range slice(kSliceAuto, 4096.0f, 1.0f);
  slice.setSkewForCentre(256.0f);
  layout.add(std::make_unique<Float>(
      id("slice"), "Slice", slice, kSliceAuto,
      juce::AudioParameterFloatAttributes()
          .withStringFromValueFunction(
              [](float v, int) { return v < 7.5f ? juce::String("Auto") : juce::String(juce::roundToInt(v)) + " smp"; })
          .withValueFromStringFunction(
              [](const juce::String& s) { return s.containsIgnoreCase("a") ? kSliceAuto : s.getFloatValue(); })));
  layout.add(number("balance", "Balance", Range(-0.2f, 0.2f, 0.01f), 0.0f, "", 2));
  layout.add(std::make_unique<Choice>(id("switchPoint"), "Switch point", juce::StringArray{"Fixed", "Closest"}, 0));
  layout.add(number("jumpWeight", "Jump weight", Range(0.0f, 0.2f, 0.005f), 0.04f, "", 3));

  layout.add(number("slotMs", "Slot length", Range(20.0f, 300.0f, 1.0f), 110.0f, "ms", 0));
  layout.add(number("sensitivity", "B trigger", Range(0.0f, 1.0f, 0.01f), 0.5f, "", 2));

  for (const char* source : {"a", "b"}) {
    const juce::String s(source), name = s.toUpperCase();
    auto add = [&](const char* suffix, const char* label, Range range, float fallback, const char* unit, int decimals) {
      const juce::String paramId = s + suffix, title = name + " " + label;
      layout.add(std::make_unique<Float>(
          juce::ParameterID{paramId, 1}, title, range, fallback,
          juce::AudioParameterFloatAttributes()
              .withStringFromValueFunction([unit, decimals](float v, int) {
                const juce::String u(unit);
                return juce::String(std::abs(v) < 1e-6f ? 0.0f : v, decimals) + (u.isEmpty() ? u : " " + u);
              })
              .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); })));
    };
    add("X", "offset X", Range(-1.0f, 1.0f, 0.01f), 0.0f, "", 2);
    add("Y", "offset Y", Range(-1.0f, 1.0f, 0.01f), 0.0f, "", 2);
    add("Scale", "scale", Range(0.0f, 2.0f, 0.01f), 1.0f, "", 2);
    add("Rotation", "rotation", Range(-180.0f, 180.0f, 1.0f), 0.0f, "deg", 0);
  }

  layout.add(number("zLevel", "Z-out level", Range(-1.0f, 1.0f, 0.01f), 1.0f, "", 2));
  layout.add(number("scopeDecay", "Persistence", Range(0.3f, 0.9f, 0.01f), 0.55f, "", 2));
  return layout;
}

MuxProcessor::MuxProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("A", juce::AudioChannelSet::stereo(), true)
                         .withInput("B (sidechain)", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Z (blanking)", juce::AudioChannelSet::stereo(), false)),
      state_(*this, nullptr, "CSMux", createLayout()) {
  auto raw = [this](const char* paramId) { return state_.getRawParameterValue(paramId); };
  mode_ = raw("mode");
  slice_ = raw("slice");
  balance_ = raw("balance");
  switchPoint_ = raw("switchPoint");
  jumpWeight_ = raw("jumpWeight");
  slotMs_ = raw("slotMs");
  sensitivity_ = raw("sensitivity");
  ax_ = raw("aX");
  ay_ = raw("aY");
  aScale_ = raw("aScale");
  aRotation_ = raw("aRotation");
  bx_ = raw("bX");
  by_ = raw("bY");
  bScale_ = raw("bScale");
  bRotation_ = raw("bRotation");
  zLevel_ = raw("zLevel");
  startTimerHz(10);
}

MuxProcessor::~MuxProcessor() { stopTimer(); }

void MuxProcessor::prepareToPlay(double sampleRate, int) {
  scratchBx_.assign(kMaxChunk, 0.0f);
  scratchBy_.assign(kMaxChunk, 0.0f);
  scratchZ_.assign(kMaxChunk, 0.0f);
  engine_.prepare(sampleRate);
  engine_.setParams(readParams());
}

void MuxProcessor::releaseResources() {}

bool MuxProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  // XY needs exactly two channels on A and on the output. B and Z are stereo or off.
  const auto stereo = juce::AudioChannelSet::stereo();
  if (layouts.getMainInputChannelSet() != stereo || layouts.getMainOutputChannelSet() != stereo)
    return false;
  const auto sidechain = layouts.getChannelSet(true, 1), z = layouts.getChannelSet(false, 1);
  return (sidechain.isDisabled() || sidechain == stereo) && (z.isDisabled() || z == stereo);
}

cs::MuxParams MuxProcessor::readParams() const noexcept {
  cs::MuxParams p;
  p.mode = static_cast<cs::MuxMode>(juce::roundToInt(mode_->load()));
  const float slice = slice_->load();
  p.slice = slice < 7.5f ? 0 : juce::roundToInt(slice);
  p.balance = balance_->load();
  p.switchPoint = static_cast<cs::SwitchPoint>(juce::roundToInt(switchPoint_->load()));
  p.jumpWeight = jumpWeight_->load();
  p.slotMs = slotMs_->load();
  p.sensitivity = sensitivity_->load();
  p.a = {ax_->load(), ay_->load(), aScale_->load(), aRotation_->load()};
  p.b = {bx_->load(), by_->load(), bScale_->load(), bRotation_->load()};
  p.zLevel = zLevel_->load();
  return p;
}

void MuxProcessor::handleMidi(const juce::MidiMessage& m) noexcept {
  if (!m.isNoteOn())
    return;
  switch (m.getNoteNumber()) {
  case 36:
    engine_.triggerHit(cs::HitType::Kick);
    break;
  case 38:
    engine_.triggerHit(cs::HitType::Snare);
    break;
  case 49:
    engine_.triggerHit(cs::HitType::Crash);
    break;
  default:
    engine_.triggerB();
    break;
  }
}

void MuxProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  const int n = buffer.getNumSamples();
  auto main = getBusBuffer(buffer, false, 0);
  if (main.getNumChannels() < 2 || getBusBuffer(buffer, true, 0).getNumChannels() < 2) {
    buffer.clear();
    return;
  }
  engine_.setParams(readParams());

  // A and the output share channels 0-1; B and Z share the next pair when
  // both buses are on, so B is copied out before Z is written.
  auto sidechain = getBusBuffer(buffer, true, 1);
  auto zBus = getBusBuffer(buffer, false, 1);
  const bool hasB = sidechain.getNumChannels() >= 2, hasZ = zBus.getNumChannels() >= 1;
  float* x = main.getWritePointer(0);
  float* y = main.getWritePointer(1);

  auto midiEvent = midi.begin();
  for (int pos = 0; pos < n;) {
    // Up to the next MIDI event, in chunks the scratch buffers can hold.
    int end = juce::jmin(n, pos + kMaxChunk);
    while (midiEvent != midi.end() && (*midiEvent).samplePosition <= pos) {
      handleMidi((*midiEvent).getMessage());
      ++midiEvent;
    }
    if (midiEvent != midi.end())
      end = juce::jmin(end, juce::jmax(pos + 1, (*midiEvent).samplePosition));
    const int count = end - pos;
    if (hasB) {
      std::copy_n(sidechain.getReadPointer(0) + pos, count, scratchBx_.begin());
      std::copy_n(sidechain.getReadPointer(1) + pos, count, scratchBy_.begin());
    }
    engine_.process(x + pos, y + pos, hasB ? scratchBx_.data() : nullptr, hasB ? scratchBy_.data() : nullptr, x + pos,
                    y + pos, hasZ ? scratchZ_.data() : nullptr, nullptr, count);
    if (hasZ)
      for (int ch = 0; ch < zBus.getNumChannels(); ++ch)
        std::copy_n(scratchZ_.begin(), count, zBus.getWritePointer(ch) + pos);
    pos = end;
  }
  engineLatency_.store(engine_.latencySamples(), std::memory_order_relaxed);
  scopeFeed_.push(x, y, n);
}

void MuxProcessor::timerCallback() {
  // Cycle-lock delays the signal by one period of A. Tell the host once the
  // value has held for two polls, so a passing note does not make it re-plan.
  const int latency = engineLatency_.load(std::memory_order_relaxed);
  if (latency == pendingLatency_ && std::abs(latency - getLatencySamples()) > 1)
    setLatencySamples(latency);
  pendingLatency_ = latency;
}

juce::AudioProcessorEditor* MuxProcessor::createEditor() {
  return new cs::Editor(*this, state_, scopeFeed_, "scopeDecay",
                        {{"Mux", {"mode", "slice", "balance", "switchPoint", "jumpWeight"}},
                         {"Slot", {"slotMs", "sensitivity"}},
                         {"Place A", {"aX", "aY", "aScale", "aRotation"}},
                         {"Place B", {"bX", "bY", "bScale", "bRotation"}},
                         {"Output", {"zLevel", "scopeDecay"}}});
}

void MuxProcessor::getStateInformation(juce::MemoryBlock& destination) {
  if (const auto xml = state_.copyState().createXml())
    copyXmlToBinary(*xml, destination);
}

void MuxProcessor::setStateInformation(const void* data, int size) {
  if (const auto xml = getXmlFromBinary(data, size))
    if (xml->hasTagName(state_.state.getType()))
      state_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MuxProcessor(); }

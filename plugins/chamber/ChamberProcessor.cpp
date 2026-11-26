#include "ChamberProcessor.h"

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

const char* const kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// "A1 55 Hz": octave numbering with A4 = MIDI 69.
juce::String noteName(int note) {
  const double hz = 440.0 * std::pow(2.0, (note - 69) / 12.0);
  return juce::String(kNoteNames[note % 12]) + juce::String(note / 12 - 1) + "  " + juce::String(hz, hz < 100 ? 1 : 0) +
         " Hz";
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ChamberProcessor::createLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  layout.add(std::make_unique<Choice>(id("type"), "Type", juce::StringArray{"Auto", "Alpha", "Muon", "Electron", "Random"}, 0));
  layout.add(std::make_unique<Choice>(id("seedMode"), "Seed mode", juce::StringArray{"Random", "Per key", "Global"}, 0));
  layout.add(number("diffusion", "Diffusion rate", Range(0.0f, 0.06f, 0.001f), 0.03f, "/s", 3));
  layout.add(number("drift", "Drift", Range(0.0f, 0.2f, 0.001f), 0.06f, "/s", 3));
  layout.add(number("condense", "Condense time", Range(0.0f, 200.0f, 1.0f), 60.0f, "ms", 0));

  layout.add(std::make_unique<Choice>(id("wallMode"), "Wall mode", juce::StringArray{"Off", "Shared", "Drone"}, 1));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      id("wallRoot"), "Wall root", 21, 96, 33,
      juce::AudioParameterIntAttributes()
          .withStringFromValueFunction([](int v, int) { return noteName(v); })
          .withValueFromStringFunction([](const juce::String& s) { return s.getIntValue(); })));
  // The bottom of the range is Auto: p nearest 0.45.
  layout.add(std::make_unique<Float>(
      id("wallShare"), "Wall share p", Range(0.29f, 0.7f, 0.01f), 0.29f,
      juce::AudioParameterFloatAttributes()
          .withStringFromValueFunction([](float v, int) { return v < 0.295f ? juce::String("Auto") : juce::String(v, 2); })
          .withValueFromStringFunction(
              [](const juce::String& s) { return s.containsIgnoreCase("a") ? 0.29f : s.getFloatValue(); })));

  layout.add(number("size", "Size", Range(0.0f, 1.0f, 0.01f), 0.9f, "", 2));
  layout.add(number("attack", "Attack", Range(0.0f, 50.0f, 0.1f), 0.0f, "ms", 1));
  Range release(0.0f, 2000.0f, 1.0f);
  release.setSkewForCentre(300.0f);
  layout.add(number("release", "Release", release, 0.0f, "ms", 0));
  layout.add(number("glide", "Glide", Range(0.0f, 500.0f, 1.0f), 0.0f, "ms", 0));
  layout.add(number("bendRange", "Bend range", Range(0.0f, 12.0f, 1.0f), 2.0f, "st", 0));
  layout.add(number("gain", "Output gain", Range(-24.0f, 0.0f, 0.1f), 0.0f, "dB", 1));
  layout.add(number("scopeDecay", "Persistence", Range(0.3f, 0.9f, 0.01f), 0.55f, "", 2));
  return layout;
}

ChamberProcessor::ChamberProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("CS Chamber builder"),
      state_(*this, nullptr, "CSChamber", createLayout()) {
  auto raw = [this](const char* paramId) { return state_.getRawParameterValue(paramId); };
  type_ = raw("type");
  seedMode_ = raw("seedMode");
  diffusion_ = raw("diffusion");
  drift_ = raw("drift");
  condense_ = raw("condense");
  wallMode_ = raw("wallMode");
  wallRoot_ = raw("wallRoot");
  wallShare_ = raw("wallShare");
  size_ = raw("size");
  attack_ = raw("attack");
  release_ = raw("release");
  glide_ = raw("glide");
  bendRange_ = raw("bendRange");
  gain_ = raw("gain");
  scopeDecay_ = raw("scopeDecay");
  startThread();
}

ChamberProcessor::~ChamberProcessor() { stopThread(2000); }

void ChamberProcessor::run() {
  while (!threadShouldExit()) {
    if (!engine_.service())
      wait(1);
  }
}

void ChamberProcessor::prepareToPlay(double sampleRate, int) {
  engine_.setSynchronous(true); // keeps the builder thread out while the engine resets
  engine_.prepare(sampleRate);
  engine_.setParams(readParams());
}

void ChamberProcessor::releaseResources() {}

bool ChamberProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
  // XY needs exactly two channels.
  return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

cs::ChamberParams ChamberProcessor::readParams() const noexcept {
  cs::ChamberParams p;
  p.type = static_cast<cs::TypeMode>(juce::roundToInt(type_->load()));
  p.seedMode = static_cast<cs::SeedMode>(juce::roundToInt(seedMode_->load()));
  p.diffusionRate = diffusion_->load();
  p.drift = drift_->load();
  p.condenseMs = condense_->load();
  p.wall = static_cast<cs::WallMode>(juce::roundToInt(wallMode_->load()));
  p.wallRoot = juce::roundToInt(wallRoot_->load());
  const float share = wallShare_->load();
  p.wallShare = share < 0.295f ? 0.0 : share;
  p.size = size_->load();
  p.attackMs = attack_->load();
  p.releaseMs = release_->load();
  p.glideMs = glide_->load();
  p.bendRange = bendRange_->load();
  p.gainDb = gain_->load();
  return p;
}

void ChamberProcessor::handleMidi(const juce::MidiMessage& m) noexcept {
  if (m.isNoteOn())
    engine_.noteOn(m.getNoteNumber(), m.getVelocity());
  else if (m.isNoteOff())
    engine_.noteOff(m.getNoteNumber());
  else if (m.isPitchWheel())
    engine_.setPitchBend((m.getPitchWheelValue() - 8192) / 8192.0);
  else if (m.isAllNotesOff() || m.isAllSoundOff())
    engine_.allNotesOff();
}

void ChamberProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals noDenormals;
  const int n = buffer.getNumSamples();
  if (buffer.getNumChannels() < 2) {
    buffer.clear();
    return;
  }
  // Offline bounces build tables inline, sample-exact; live, the builder thread does.
  engine_.setSynchronous(isNonRealtime());
  engine_.setParams(readParams());

  float* x = buffer.getWritePointer(0);
  float* y = buffer.getWritePointer(1);
  int pos = 0;
  for (const auto meta : midi) {
    const int at = juce::jlimit(pos, n, meta.samplePosition);
    engine_.process(x + pos, y + pos, at - pos);
    pos = at;
    handleMidi(meta.getMessage());
  }
  engine_.process(x + pos, y + pos, n - pos);
  for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
    buffer.clear(ch, 0, n);
  scopeFeed_.push(x, y, n);
}

juce::AudioProcessorEditor* ChamberProcessor::createEditor() {
  return new cs::Editor(*this, state_, scopeFeed_, "scopeDecay",
                        {{"Track", {"type", "seedMode", "diffusion", "drift", "condense"}},
                         {"Wall", {"wallMode", "wallRoot", "wallShare"}},
                         {"Voice", {"size", "attack", "release", "glide", "bendRange"}},
                         {"Output", {"gain", "scopeDecay"}}});
}

void ChamberProcessor::getStateInformation(juce::MemoryBlock& destination) {
  if (const auto xml = state_.copyState().createXml())
    copyXmlToBinary(*xml, destination);
}

void ChamberProcessor::setStateInformation(const void* data, int size) {
  if (const auto xml = getXmlFromBinary(data, size))
    if (xml->hasTagName(state_.state.getType()))
      state_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChamberProcessor(); }

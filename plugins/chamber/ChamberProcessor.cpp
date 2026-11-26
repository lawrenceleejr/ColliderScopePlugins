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

  layout.add(std::make_unique<juce::AudioParameterInt>(
      id("voices"), "Voices", 1, 6, 1,
      juce::AudioParameterIntAttributes().withStringFromValueFunction(
          [](int v, int) { return v == 1 ? juce::String("Mono") : juce::String(v); })));
  layout.add(number("size", "Size", Range(0.0f, 1.0f, 0.01f), 0.9f, "", 2));
  layout.add(number("attack", "Attack", Range(0.0f, 50.0f, 0.1f), 0.0f, "ms", 1));
  Range release(0.0f, 2000.0f, 1.0f);
  release.setSkewForCentre(300.0f);
  layout.add(number("release", "Release", release, 0.0f, "ms", 0));
  layout.add(number("glide", "Glide", Range(0.0f, 500.0f, 1.0f), 0.0f, "ms", 0));
  layout.add(number("bendRange", "Bend range", Range(0.0f, 12.0f, 1.0f), 2.0f, "st", 0));
  layout.add(std::make_unique<Choice>(id("timing"), "Timing", juce::StringArray{"Even", "Harmonic"}, 1));
  layout.add(std::make_unique<Choice>(id("quality"), "Quality", juce::StringArray{"Scope-exact", "Clean"}, 0));
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
  voices_ = raw("voices");
  size_ = raw("size");
  attack_ = raw("attack");
  release_ = raw("release");
  glide_ = raw("glide");
  bendRange_ = raw("bendRange");
  quality_ = raw("quality");
  timing_ = raw("timing");
  gain_ = raw("gain");
  scopeDecay_ = raw("scopeDecay");
  startThread();
  startTimerHz(10);
}

ChamberProcessor::~ChamberProcessor() {
  stopTimer();
  stopThread(2000);
}

// Type: 0 Auto, 1 Alpha, 2 Muon, 3 Electron, 4 Random. Wall mode: 0 Off, 1 Shared, 2 Drone.
const std::vector<cs::Preset>& ChamberProcessor::presets() {
  static const std::vector<cs::Preset> list = {
      {"Default",
       {{"type", 0}, {"seedMode", 0}, {"diffusion", 0.03f}, {"drift", 0.06f}, {"condense", 60}, {"wallMode", 1},
        {"wallRoot", 33}, {"wallShare", 0.29f}, {"voices", 1}, {"size", 0.9f}, {"attack", 0}, {"release", 0},
        {"glide", 0}, {"bendRange", 2}, {"quality", 0}, {"gain", 0}}},
      // A short bright track that speaks at once and slides between notes.
      {"Alpha lead",
       {{"type", 1}, {"seedMode", 0}, {"diffusion", 0.015f}, {"drift", 0.04f}, {"condense", 25}, {"wallMode", 0},
        {"size", 0.9f}, {"attack", 0}, {"release", 60}, {"glide", 45}}},
      // A long steady chord across the chamber, with the wall: full and plain.
      {"Muon bass",
       {{"type", 2}, {"seedMode", 1}, {"diffusion", 0.006f}, {"drift", 0.02f}, {"condense", 40}, {"wallMode", 1},
        {"size", 0.9f}, {"attack", 0}, {"release", 0}, {"glide", 0}}},
      // A curl that dissolves quickly: mostly noise by the end of the note.
      {"Electron hiss",
       {{"type", 3}, {"seedMode", 0}, {"diffusion", 0.06f}, {"drift", 0.1f}, {"condense", 120}, {"wallMode", 0},
        {"size", 0.9f}, {"attack", 5}, {"release", 400}, {"glide", 0}}},
      // The wall hums at A1; notes ring as its overtones.
      {"Drone chamber",
       {{"type", 0}, {"seedMode", 0}, {"diffusion", 0.03f}, {"drift", 0.06f}, {"condense", 60}, {"wallMode", 2},
        {"wallRoot", 33}, {"wallShare", 0.29f}, {"size", 0.9f}, {"attack", 0}, {"release", 150}, {"glide", 0}}},
      // Four voices over the A1 wall: chords as overtones of the drone.
      {"Chamber choir",
       {{"type", 0}, {"seedMode", 0}, {"diffusion", 0.02f}, {"drift", 0.04f}, {"condense", 40}, {"wallMode", 2},
        {"wallRoot", 33}, {"wallShare", 0.29f}, {"voices", 4}, {"size", 0.9f}, {"attack", 0}, {"release", 120},
        {"glide", 0}}},
  };
  return list;
}

void ChamberProcessor::setCurrentProgram(int index) {
  if (index < 0 || index >= getNumPrograms())
    return;
  program_ = index;
  cs::applyPreset(state_, presets()[static_cast<std::size_t>(index)]);
}

const juce::String ChamberProcessor::getProgramName(int index) {
  return index >= 0 && index < getNumPrograms() ? juce::String(presets()[static_cast<std::size_t>(index)].name) : juce::String();
}

void ChamberProcessor::timerCallback() {
  // Clean quality delays the output by 4 samples (its low-pass is linear phase).
  const int latency = juce::roundToInt(quality_->load()) == 1 ? cs::Decimator4::kLatency : 0;
  if (latency != getLatencySamples())
    setLatencySamples(latency);
}

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
  p.voices = juce::roundToInt(voices_->load());
  p.size = size_->load();
  p.attackMs = attack_->load();
  p.releaseMs = release_->load();
  p.glideMs = glide_->load();
  p.bendRange = bendRange_->load();
  p.quality = static_cast<cs::Quality>(juce::roundToInt(quality_->load()));
  p.timing = static_cast<cs::Timing>(juce::roundToInt(timing_->load()));
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
                         {"Voice", {"voices", "size", "attack", "release", "glide", "bendRange"}},
                         {"Output", {"timing", "quality", "gain", "scopeDecay"}}},
                        &presets());
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

// CS Bubble: a bubble chamber you can play. Each note is an event: a
// particle curls through the magnetic field, leaving a row of bubbles that
// grow while the note is held.
#include "InstrumentProcessor.h"
#include "cscore/bubble.h"

namespace {

using Range = juce::NormalisableRange<float>;
using namespace cs::param;

juce::AudioProcessorValueTreeState::ParameterLayout layout() {
  juce::AudioProcessorValueTreeState::ParameterLayout l;
  l.add(choice("type", "Event", {"Auto", "Electron", "Pair", "Decay", "Beam", "Random"}, 0));
  l.add(choice("seedMode", "Seed mode", {"Random", "Per key", "Global"}, 0));
  l.add(number("field", "Field", Range(0.0f, 1.0f, 0.01f), 0.5f, "", 2));
  l.add(whole("bubbles", "Bubbles", 20, 200, 90));
  l.add(number("growth", "Bubble growth", Range(0.0f, 0.1f, 0.001f), 0.02f, "/s", 3));
  l.add(number("maxRadius", "Bubble size", Range(0.005f, 0.06f, 0.001f), 0.03f, "", 3));
  l.add(number("boil", "Boil", Range(0.0f, 1.0f, 0.01f), 0.15f, "", 2));
  l.add(number("form", "Form time", Range(0.0f, 200.0f, 1.0f), 40.0f, "ms", 0));
  addVoice(l);
  return l;
}

class BubbleProcessor final : public cs::InstrumentProcessor<cs::BubbleEngine> {
public:
  BubbleProcessor() : InstrumentProcessor("CSBubble", layout()) {
    type_ = raw("type");
    seedMode_ = raw("seedMode");
    field_ = raw("field");
    bubbles_ = raw("bubbles");
    growth_ = raw("growth");
    maxRadius_ = raw("maxRadius");
    boil_ = raw("boil");
    form_ = raw("form");
  }

private:
  cs::BubbleParams readParams() const noexcept override {
    cs::BubbleParams p;
    p.voice = readVoice();
    p.type = static_cast<cs::BubbleType>(juce::roundToInt(type_->load()));
    p.seedMode = static_cast<cs::BubbleSeed>(juce::roundToInt(seedMode_->load()));
    p.field = field_->load();
    p.bubbles = juce::roundToInt(bubbles_->load());
    p.growth = growth_->load();
    p.maxRadius = maxRadius_->load();
    p.boil = boil_->load();
    p.formMs = form_->load();
    return p;
  }

  std::vector<cs::ParamGroup> groups() const override {
    return {{"Event", {"type", "seedMode", "field", "form"}},
            {"Bubbles", {"bubbles", "growth", "maxRadius", "boil"}},
            {"Voice", kVoiceGroup},
            {"Output", kOutputGroup}};
  }

  // Event: 0 Auto, 1 Electron, 2 Pair, 3 Decay, 4 Beam, 5 Random.
  const std::vector<cs::Preset>& presets() const override {
    static const std::vector<cs::Preset> list = {
        {"Default",
         {{"type", 0}, {"seedMode", 0}, {"field", 0.5f}, {"form", 40}, {"bubbles", 90}, {"growth", 0.02f},
          {"maxRadius", 0.03f}, {"boil", 0.15f}, {"size", 0.9f}, {"attack", 0}, {"release", 0}, {"glide", 0},
          {"bendRange", 2}, {"quality", 0}, {"gain", 0}}},
        // One spiral per key, the same every time: a steady, buzzy bass.
        {"Spiral bass",
         {{"type", 1}, {"seedMode", 1}, {"field", 0.55f}, {"form", 15}, {"bubbles", 60}, {"growth", 0.004f},
          {"maxRadius", 0.012f}, {"boil", 0.0f}, {"release", 0}, {"glide", 0}}},
        // Pairs that appear at once and fade quickly.
        {"Pair pluck",
         {{"type", 2}, {"seedMode", 0}, {"field", 0.6f}, {"form", 10}, {"bubbles", 80}, {"growth", 0.03f},
          {"maxRadius", 0.02f}, {"boil", 0.1f}, {"release", 90}, {"glide", 0}}},
        // Bubbles that swell and shiver: the tone turns to fizz as the note is held.
        {"Fizz",
         {{"type", 5}, {"seedMode", 0}, {"field", 0.5f}, {"form", 60}, {"bubbles", 140}, {"growth", 0.08f},
          {"maxRadius", 0.05f}, {"boil", 0.7f}, {"release", 250}, {"glide", 0}}},
    };
    return list;
  }

  std::atomic<float>*type_, *seedMode_, *field_, *bubbles_, *growth_, *maxRadius_, *boil_, *form_;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BubbleProcessor(); }

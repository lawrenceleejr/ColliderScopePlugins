// CS Script: text and equations you can play. Type a line, in a small part
// of LaTeX's syntax, and each note draws it with single-stroke letters.
#include "InstrumentProcessor.h"
#include "cscore/lockfree.h"
#include "cscore/script.h"

namespace {

using Range = juce::NormalisableRange<float>;
using namespace cs::param;

const char* const kDefaultText = "E = mc^2";

juce::AudioProcessorValueTreeState::ParameterLayout layout() {
  juce::AudioProcessorValueTreeState::ParameterLayout l;
  Range write(0.0f, 2000.0f, 1.0f);
  write.setSkewForCentre(300.0f);
  l.add(number("write", "Write time", write, 0.0f, "ms", 0));
  l.add(number("slant", "Slant", Range(-0.5f, 0.5f, 0.01f), 0.0f, "", 2));
  addVoice(l);
  return l;
}

class ScriptProcessor final : public cs::InstrumentProcessor<cs::ScriptEngine> {
public:
  ScriptProcessor() : InstrumentProcessor("CSScript", layout()) {
    write_ = raw("write");
    slant_ = raw("slant");
    setText(kDefaultText);
  }

private:
  using Text = std::array<char, 200>;

  // The text is not a parameter (hosts automate numbers, not strings): it
  // lives in the saved state and reaches the audio thread lock-free.
  void setText(const juce::String& text) {
    state_.state.setProperty("text", text, nullptr);
    cs::ScriptParams p;
    p.setText(text.toStdString());
    text_.write(p.text);
  }

  void stateRestored() override {
    const juce::String text = state_.state.getProperty("text", kDefaultText).toString();
    cs::ScriptParams p;
    p.setText(text.toStdString());
    text_.write(p.text);
  }

  void addOwnControls(cs::ParamPanel& panel) override {
    panel.addText("Text", state_.state.getProperty("text", kDefaultText).toString(),
                  [this](const juce::String& text) { setText(text); });
  }

  cs::ScriptParams readParams() const noexcept override {
    cs::ScriptParams p;
    p.voice = readVoice();
    p.text = text_.read();
    p.writeMs = write_->load();
    p.slant = slant_->load();
    return p;
  }

  std::vector<cs::ParamGroup> groups() const override {
    return {{"Writing", {"write", "slant"}}, {"Voice", kVoiceGroup}, {"Output", kOutputGroup}};
  }

  const std::vector<cs::Preset>& presets() const override {
    static const std::vector<cs::Preset> list = {
        {"Default",
         {{"write", 0}, {"slant", 0}, {"size", 0.9f}, {"attack", 0}, {"release", 0}, {"glide", 0}, {"bendRange", 2},
          {"timing", 1}, {"quality", 0}, {"gain", 0}}},
        // Each note writes the line out, like a hand.
        {"Handwriting", {{"write", 400}, {"slant", 0.2f}, {"release", 150}}},
    };
    return list;
  }

  cs::LatestValue<Text> text_;
  std::atomic<float>*write_, *slant_;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ScriptProcessor(); }

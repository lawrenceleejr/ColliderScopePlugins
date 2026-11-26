// CS Orbital: surfaces you can play. Each note draws a hydrogen orbital as a
// wireframe of its isosurface, spinning about the orbital's axis.
#include "InstrumentProcessor.h"
#include "cscore/orbital.h"

namespace {

using Range = juce::NormalisableRange<float>;
using namespace cs::param;

juce::AudioProcessorValueTreeState::ParameterLayout layout() {
  juce::AudioProcessorValueTreeState::ParameterLayout l;
  juce::StringArray orbitals{"Auto"};
  for (const cs::OrbitalId& orbital : cs::kOrbitals)
    orbitals.add(orbital.name);
  l.add(choice("orbital", "Orbital", orbitals, 0));
  l.add(number("level", "Iso level", Range(0.05f, 0.8f, 0.01f), 0.25f, "", 2));
  l.add(whole("lines", "Wires", 4, 16, 8));
  l.add(number("bloom", "Bloom time", Range(0.0f, 500.0f, 1.0f), 80.0f, "ms", 0));
  l.add(number("spin", "Spin", Range(-180.0f, 180.0f, 1.0f), 30.0f, "deg/s", 0));
  l.add(number("tilt", "Tilt", Range(0.0f, 90.0f, 1.0f), 25.0f, "deg", 0));
  addVoice(l);
  return l;
}

class OrbitalProcessor final : public cs::InstrumentProcessor<cs::OrbitalEngine> {
public:
  OrbitalProcessor() : InstrumentProcessor("CSOrbital", layout()) {
    orbital_ = raw("orbital");
    level_ = raw("level");
    lines_ = raw("lines");
    bloom_ = raw("bloom");
    spin_ = raw("spin");
    tilt_ = raw("tilt");
  }

private:
  cs::OrbitalParams readParams() const noexcept override {
    cs::OrbitalParams p;
    p.voice = readVoice();
    p.orbital = juce::roundToInt(orbital_->load());
    p.level = level_->load();
    p.lines = juce::roundToInt(lines_->load());
    p.bloomMs = bloom_->load();
    p.spin = spin_->load();
    p.tilt = tilt_->load();
    return p;
  }

  std::vector<cs::ParamGroup> groups() const override {
    return {{"Surface", {"orbital", "level", "lines", "bloom"}},
            {"View", {"spin", "tilt"}},
            {"Voice", kVoiceGroup},
            {"Output", kOutputGroup}};
  }

  // Orbital: 0 Auto, then 1s, 2s, 2pz, 2px, 3s, 3pz, 3px, 3dz2 (8), 3dxz, 3dx2-y2, 4s ... 4fz3 (17) ...
  const std::vector<cs::Preset>& presets() const override {
    static const std::vector<cs::Preset> list = {
        {"Default",
         {{"orbital", 0}, {"level", 0.25f}, {"lines", 8}, {"bloom", 80}, {"spin", 30}, {"tilt", 25}, {"size", 0.9f},
          {"attack", 0}, {"release", 0}, {"glide", 0}, {"bendRange", 2}, {"quality", 0}, {"gain", 0}}},
        // 1s: a plain sphere, few wires. The purest tone here.
        {"s sine", {{"orbital", 1}, {"level", 0.3f}, {"lines", 4}, {"bloom", 0}, {"spin", 20}, {"tilt", 30}, {"release", 0}}},
        // 2px: two lobes swinging round, with glide between notes.
        {"p lead", {{"orbital", 4}, {"level", 0.25f}, {"lines", 8}, {"bloom", 40}, {"spin", 120}, {"tilt", 20}, {"glide", 50}, {"release", 60}}},
        // 3dz2: lobes and a ring, blooming slowly and fading slowly.
        {"d pad", {{"orbital", 8}, {"level", 0.2f}, {"lines", 10}, {"bloom", 350}, {"spin", 25}, {"tilt", 35}, {"attack", 30}, {"release", 500}}},
        // 4f: many lobes, many wires: bright and dense.
        {"f bell", {{"orbital", 18}, {"level", 0.2f}, {"lines", 12}, {"bloom", 15}, {"spin", 60}, {"tilt", 30}, {"release", 300}}},
    };
    return list;
  }

  std::atomic<float>*orbital_, *level_, *lines_, *bloom_, *spin_, *tilt_;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OrbitalProcessor(); }

// CS Chamber acceptance tests that the core can answer offline:
// pitch, picture and hard rest.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

#include "analysis.h"
#include "cscore/cscore.h"

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kSampleRate = 48000.0;

struct Render {
  std::vector<float> x, y;
};

Render play(const cs::ChamberParams& params, int note, int velocity, double held, double tail) {
  cs::ChamberVoice voice;
  voice.setParams(params);
  voice.prepare(kSampleRate);
  const auto on = static_cast<std::size_t>(held * kSampleRate), off = static_cast<std::size_t>(tail * kSampleRate);
  Render r;
  r.x.resize(on + off);
  r.y.resize(on + off);
  voice.noteOn(note, velocity);
  voice.render(r.x.data(), r.y.data(), static_cast<int>(on));
  voice.noteOff();
  voice.render(r.x.data() + on, r.y.data() + on, static_cast<int>(off));
  return r;
}

} // namespace

TEST_CASE("Pitch: period of X within 1 cent of the note", "[chamber][acceptance]") {
  const int note = GENERATE(33, 45, 57, 69, 81);
  const auto wall = GENERATE(cs::WallMode::Shared, cs::WallMode::Off);
  // A steady picture: ageing changes the waveform from period to period,
  // which is the instrument's sound but not its pitch.
  const bool steady = GENERATE(true, false);
  cs::ChamberParams p;
  p.wall = wall;
  if (steady) {
    p.diffusionRate = 0.0;
    p.drift = 0.0;
  }
  CAPTURE(note, static_cast<int>(wall), steady);
  const Render r = play(p, note, 80, 0.6, 0.0);
  const double period = analysis::periodByAutocorrelation(r.x, kSampleRate, static_cast<std::size_t>(0.1 * kSampleRate),
                                                           kSampleRate / cs::midiToHz(note));
  const double error = cs::cents(kSampleRate / period, cs::midiToHz(note));
  CAPTURE(period, error);
  CHECK(std::abs(error) < 1.0);
}

TEST_CASE("Pitch: Drone holds the root and snaps the note to an overtone", "[chamber]") {
  cs::ChamberParams p;
  p.wall = cs::WallMode::Drone;
  p.wallRoot = 33;
  cs::ChamberVoice voice;
  voice.setParams(p);
  voice.prepare(kSampleRate);
  voice.noteOn(57, 80); // A3 = 4 x A1
  CHECK_THAT(voice.periodHz(), WithinAbs(55.0, 1e-9));
  CHECK(voice.harmonic() == 4);
  voice.noteOn(58, 80); // snaps to the nearest harmonic
  CHECK(voice.harmonic() == 4);
  voice.noteOn(64, 80); // E4 = 329.6 Hz = 6 x 55
  CHECK(voice.harmonic() == 6);

  const Render r = play(p, 57, 80, 0.6, 0.0);
  const double period = analysis::periodByAutocorrelation(r.x, kSampleRate, static_cast<std::size_t>(0.1 * kSampleRate),
                                                           kSampleRate / 55.0);
  CHECK(std::abs(cs::cents(kSampleRate / period, 55.0)) < 1.0);
}

TEST_CASE("Picture: every track type stays within r <= 0.95", "[chamber][acceptance]") {
  const auto type = GENERATE(cs::TypeMode::Alpha, cs::TypeMode::Muon, cs::TypeMode::Electron);
  const auto wall = GENERATE(cs::WallMode::Off, cs::WallMode::Shared, cs::WallMode::Drone);
  const double size = GENERATE(0.9, 1.0);
  cs::ChamberParams p;
  p.type = type;
  p.wall = wall;
  p.size = size;
  for (std::uint64_t seed = 1; seed <= 8; ++seed) {
    p.seed = seed;
    const Render r = play(p, 57, 80, 2.6, 0.0); // past the longest life
    double peak = 0.0, peakChannel = 0.0;
    for (std::size_t i = 0; i < r.x.size(); ++i) {
      peak = std::max(peak, analysis::radius(r.x[i], r.y[i]));
      peakChannel = std::max({peakChannel, static_cast<double>(std::abs(r.x[i])), static_cast<double>(std::abs(r.y[i]))});
    }
    CAPTURE(static_cast<int>(type), static_cast<int>(wall), size, seed);
    CHECK(peak <= 0.95);
    CHECK(peakChannel <= cs::kPeak);
    CHECK(peak > 0.05); // and something was drawn
  }
}

TEST_CASE("Picture: with the wall on, the beam spends its time on the wall and the track", "[chamber]") {
  cs::ChamberParams p;
  p.type = cs::TypeMode::Muon;
  p.diffusionRate = 0.0;
  p.drift = 0.0;
  const Render r = play(p, 45, 120, 0.5, 0.0);
  // Size 0.9: wall at radius 0.81. A muon is a chord, so radius < 0.81 inside.
  std::size_t onWall = 0, inside = 0, outside = 0;
  for (std::size_t i = 4800; i < r.x.size(); ++i) {
    const double radius = analysis::radius(r.x[i], r.y[i]);
    if (std::abs(radius - 0.81) < 0.002)
      ++onWall;
    else if (radius < 0.81)
      ++inside;
    else
      ++outside;
  }
  const double total = static_cast<double>(r.x.size() - 4800);
  CHECK(outside == 0);
  CHECK(onWall / total > 0.6);  // 2 pi 0.9 = 5.65 of about 7.2 length units
  CHECK(inside / total > 0.15); // the muon chord
}

TEST_CASE("Hard rest: below -80 dBFS within 0.5 ms of note-off", "[chamber][acceptance]") {
  const double sampleRate = GENERATE(44100.0, 48000.0, 96000.0);
  const auto wall = GENERATE(cs::WallMode::Off, cs::WallMode::Shared, cs::WallMode::Drone);
  cs::ChamberParams p;
  p.wall = wall;
  p.releaseMs = 0.0;
  cs::ChamberVoice voice;
  voice.setParams(p);
  voice.prepare(sampleRate);
  const int held = static_cast<int>(0.3 * sampleRate), tail = static_cast<int>(0.1 * sampleRate);
  std::vector<float> x(static_cast<std::size_t>(held + tail)), y(x.size());
  voice.noteOn(57, 80);
  voice.render(x.data(), y.data(), held);
  CHECK(std::abs(x[static_cast<std::size_t>(held) - 1]) + std::abs(y[static_cast<std::size_t>(held) - 1]) > 0.01);
  voice.noteOff();
  voice.render(x.data() + held, y.data() + held, tail);

  const float limit = 1e-4f; // -80 dBFS
  const auto deadline = static_cast<std::size_t>(held) + static_cast<std::size_t>(std::floor(0.0005 * sampleRate));
  CAPTURE(sampleRate, static_cast<int>(wall));
  for (std::size_t i = deadline; i < x.size(); ++i) {
    REQUIRE(std::abs(x[i]) < limit);
    REQUIRE(std::abs(y[i]) < limit);
  }
  CHECK(x.back() == 0.0f); // and then exactly zero
  CHECK(y.back() == 0.0f);
  CHECK_FALSE(voice.active());
}

TEST_CASE("Release: the track keeps sounding, then fades to exact zero", "[chamber]") {
  cs::ChamberParams p;
  p.releaseMs = 200.0;
  const Render r = play(p, 57, 80, 0.3, 0.4);
  const auto off = static_cast<std::size_t>(0.3 * kSampleRate);
  CHECK(analysis::radius(r.x[off + 2400], r.y[off + 2400]) > 0.05); // 50 ms in: still drawing
  CHECK(r.x[off + 9700] == 0.0f);                                   // 202 ms in: silent
  CHECK(r.y[off + 9700] == 0.0f);
}

TEST_CASE("Seed modes", "[chamber]") {
  auto trackOf = [](cs::SeedMode mode, std::vector<int> notes) {
    cs::ChamberParams p;
    p.seedMode = mode;
    p.type = cs::TypeMode::Electron;
    cs::ChamberVoice voice;
    voice.setParams(p);
    voice.prepare(kSampleRate);
    for (int n : notes)
      voice.noteOn(n, 80);
    return voice.track().points;
  };
  auto same = [](const std::vector<cs::Vec2>& a, const std::vector<cs::Vec2>& b) {
    return a.size() == b.size() && a[5].x == b[5].x && a[40].y == b[40].y;
  };
  CHECK_FALSE(same(trackOf(cs::SeedMode::Random, {60}), trackOf(cs::SeedMode::Random, {60, 60})));
  CHECK(same(trackOf(cs::SeedMode::PerKey, {60}), trackOf(cs::SeedMode::PerKey, {62, 60})));
  CHECK_FALSE(same(trackOf(cs::SeedMode::PerKey, {60}), trackOf(cs::SeedMode::PerKey, {62})));
  CHECK(same(trackOf(cs::SeedMode::Global, {60}), trackOf(cs::SeedMode::Global, {62, 71})));
}

TEST_CASE("Gate envelope: attack and release times", "[envelope]") {
  cs::GateEnvelope env;
  env.setSampleRate(48000.0);
  env.setAttack(0.010);
  env.setRelease(0.0);
  env.gate(true);
  float g = 0.0f;
  for (int i = 0; i < 240; ++i)
    g = env.next();
  CHECK_THAT(g, WithinAbs(0.5, 1e-3)); // halfway up a 10 ms raised cosine
  for (int i = 0; i < 240; ++i)
    g = env.next();
  CHECK_THAT(g, WithinAbs(1.0, 1e-6));
  env.gate(false);
  int n = 0;
  while (!env.idle() && n < 1000) {
    g = env.next();
    ++n;
  }
  CHECK(g == 0.0f);
  CHECK(n <= 24); // 0.5 ms at 48 kHz
}

TEST_CASE("Scope view: light lands where the beam dwells", "[scope]") {
  std::vector<float> x(48000), y(48000);
  for (std::size_t i = 0; i < x.size(); ++i) {
    const double a = 2.0 * 3.14159265358979323846 * 220.0 * static_cast<double>(i) / 48000.0;
    x[i] = static_cast<float>(0.8 * std::cos(a));
    y[i] = static_cast<float>(0.8 * std::sin(a));
  }
  cs::ScopeView view(256);
  view.setGraticule(false);
  view.addSamples(x.data(), y.data(), 4800);
  std::vector<std::uint8_t> rgb;
  view.render(rgb);
  REQUIRE(rgb.size() == 256u * 256u * 3u);
  auto green = [&](double px, double py) {
    const int col = static_cast<int>((px + 1.02) / 2.04 * 256), row = static_cast<int>((1.02 - py) / 2.04 * 256);
    return rgb[(static_cast<std::size_t>(row) * 256 + static_cast<std::size_t>(col)) * 3 + 1];
  };
  CHECK(green(0.8, 0.0) > 200);
  CHECK(green(0.0, 0.8) > 200);
  CHECK(green(-0.566, -0.566) > 200);
  CHECK(green(0.0, 0.0) < 40);   // dark centre
  CHECK(green(0.98, 0.98) < 10); // dark corner

  // Persistence: decay^(dt * 30) per frame.
  cs::ScopeView fading(64);
  fading.setGraticule(false);
  fading.addSamples(x.data(), y.data(), 4800);
  std::vector<std::uint8_t> before, after;
  fading.render(before);
  for (int i = 0; i < 120; ++i)
    fading.beginFrame(1.0 / 60.0);
  fading.render(after);
  std::uint32_t lit = 0, left = 0;
  for (std::size_t i = 1; i < before.size(); i += 3) {
    lit += before[i];
    left += after[i];
  }
  CHECK(lit > 10000);
  CHECK(left < lit / 20);
}

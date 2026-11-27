// Polyphony: voices share each period and snap to overtones of a common
// fundamental.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "analysis.h"
#include "cscore/cscore.h"

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kSampleRate = 48000.0;

cs::ChordPlan plan(std::vector<int> notes, double wallMin = 0.0, double wallTarget = 0.0, int root = -1) {
  std::vector<double> hz;
  for (int n : notes)
    hz.push_back(cs::midiToHz(n));
  return cs::planChord(hz, root >= 0 ? cs::midiToHz(root) : hz[0], wallMin, wallTarget);
}

double total(const cs::ChordPlan& p) {
  double sum = p.wall + p.filler + p.elasticShare;
  for (int i = 0; i < p.count; ++i)
    if (i != p.elastic)
      sum += static_cast<double>(p.m[static_cast<std::size_t>(i)]) / p.h[static_cast<std::size_t>(i)];
  return sum;
}

struct Render {
  std::vector<float> x, y;
};

Render chord(const cs::ChamberParams& params, const std::vector<int>& notes, double seconds, cs::ChamberEngine* keep = nullptr) {
  cs::ChamberEngine local;
  cs::ChamberEngine& voice = keep ? *keep : local;
  voice.prepare(kSampleRate);
  voice.setParams(params);
  for (int n : notes)
    voice.noteOn(n, 80);
  Render r;
  r.x.resize(static_cast<std::size_t>(seconds * kSampleRate));
  r.y.resize(r.x.size());
  voice.process(r.x.data(), r.y.data(), static_cast<int>(r.x.size()));
  return r;
}

cs::ChamberParams polyParams(cs::WallMode wall) {
  cs::ChamberParams p;
  p.voices = 4;
  p.wall = wall;
  p.diffusionRate = 0.0; // a steady picture, for pitch measurements
  p.drift = 0.0;
  return p;
}

} // namespace

TEST_CASE("Chord plans: the period is fully shared out, and notes snap to overtones", "[poly]") {
  SECTION("a chord on the overtones of its bass repeats at the bass note") {
    const cs::ChordPlan p = plan({45, 57, 64}); // A2, A3, E4 = overtones 1, 2, 3
    CHECK(p.k == 1);
    CHECK_THAT(p.f0, WithinAbs(110.0, 1e-9));
    CHECK(p.h[0] == 1);
    CHECK(p.h[1] == 2);
    CHECK(p.h[2] == 3);
    CHECK(p.elastic == 0);
    CHECK(p.elasticShare >= 0.15);
    CHECK(p.maxErrorCents < 3.0);
    CHECK_THAT(total(p), WithinAbs(1.0, 1e-9));
  }
  SECTION("an octave: the lower note owns the period, the upper is its 2nd overtone") {
    const cs::ChordPlan p = plan({57, 69});
    CHECK(p.k == 1);
    CHECK(p.h[1] == 2);
    CHECK(p.maxErrorCents < 1e-6);
    CHECK_THAT(total(p), WithinAbs(1.0, 1e-9));
  }
  SECTION("a close major triad is 4 : 5 : 6 of a fundamental two octaves down") {
    const cs::ChordPlan p = plan({57, 61, 64});
    CHECK(p.k == 4);
    CHECK_THAT(p.f0, WithinAbs(55.0, 1e-9));
    CHECK(p.h[0] == 4);
    CHECK(p.h[1] == 5);
    CHECK(p.h[2] == 6);
    CHECK(p.elastic == -1);
    CHECK(p.maxErrorCents < 15.0); // the equal-tempered third is 14 cents wide of 5 : 4
    CHECK_THAT(total(p), WithinAbs(1.0, 1e-9));
  }
  SECTION("with a wall, the wall gets a share too") {
    const cs::ChordPlan shared = plan({57, 61, 64}, 0.2, 0.3);
    CHECK(shared.wall >= 0.2 - 1e-9);
    CHECK_THAT(total(shared), WithinAbs(1.0, 1e-9));
    const cs::ChordPlan drone = plan({57, 64, 69}, 0.2, 0.45, 33); // over an A1 wall
    CHECK_THAT(drone.f0 * drone.k, WithinAbs(55.0, 1e-9));
    CHECK(drone.wall >= 0.2 - 1e-9);
    CHECK_THAT(total(drone), WithinAbs(1.0, 1e-9));
  }
  SECTION("every chord of 2 to 6 notes gets a valid plan") {
    cs::Rng rng(12);
    double worst = 0.0;
    for (int trial = 0; trial < 500; ++trial) {
      const int count = 2 + static_cast<int>(rng.next() % 5);
      std::vector<int> notes;
      for (int i = 0; i < count; ++i)
        notes.push_back(36 + static_cast<int>(rng.next() % 49));
      std::sort(notes.begin(), notes.end());
      const bool wall = trial % 2;
      const cs::ChordPlan p = plan(notes, wall ? 0.2 : 0.0, wall ? 0.3 : 0.0);
      CAPTURE(trial, count, p.k);
      REQUIRE(p.count == count);
      REQUIRE_THAT(total(p), WithinAbs(1.0, 1e-6));
      REQUIRE(p.wall >= (wall ? 0.2 - 1e-9 : 0.0));
      REQUIRE(p.filler >= -1e-12);
      for (int i = 0; i < count; ++i) {
        REQUIRE(p.h[static_cast<std::size_t>(i)] >= 1);
        REQUIRE(p.m[static_cast<std::size_t>(i)] >= 1);
      }
      worst = std::max(worst, p.maxErrorCents);
    }
  }
}

TEST_CASE("Polyphony: the picture repeats at the chord's fundamental", "[poly][chamber]") {
  const auto wall = GENERATE(cs::WallMode::Off, cs::WallMode::Shared);
  struct Case {
    std::vector<int> notes;
    double f0;
  };
  const auto c = GENERATE(Case{{57, 69}, 220.0}, Case{{45, 57, 64}, 110.0}, Case{{57, 61, 64}, 55.0},
                          Case{{48, 55, 64, 72}, 0.0});
  cs::ChamberEngine voice;
  const Render r = chord(polyParams(wall), c.notes, 0.6, &voice);
  // With no wall the fundamentals are known; a wall needs its own share and
  // can push the fundamental lower. Either way it is the bass note over k.
  const double f0 = c.f0 > 0.0 && wall == cs::WallMode::Off ? c.f0 : voice.periodHz();
  CAPTURE(static_cast<int>(wall), c.notes.size(), f0);
  CHECK_THAT(voice.periodHz(), WithinAbs(f0, 1e-6));
  CHECK_THAT(voice.chordPlan().f0 * voice.chordPlan().k, WithinAbs(cs::midiToHz(c.notes[0]), 1e-6));
  const double period = analysis::periodByAutocorrelation(r.x, kSampleRate, static_cast<std::size_t>(0.1 * kSampleRate), kSampleRate / f0);
  CHECK(std::abs(cs::cents(kSampleRate / period, f0)) < 1.0);
  for (std::size_t i = 0; i < r.x.size(); ++i)
    REQUIRE(analysis::radius(r.x[i], r.y[i]) <= 0.95);
}

TEST_CASE("Polyphony: each voice gets its slot, sized to its overtone", "[poly][chamber]") {
  // A2, A3, E4 with no wall: overtones 1, 2, 3 of A2. A3's pass lasts 1/2
  // of the period and E4's 1/3; A2, on the fundamental, takes the rest.
  cs::ChamberParams p = polyParams(cs::WallMode::Off);
  p.type = cs::TypeMode::Muon;
  cs::ChamberEngine voice;
  const Render r = chord(p, {45, 57, 64}, 0.3, &voice);
  const cs::ChordPlan& plan = voice.chordPlan();
  REQUIRE(plan.count == 3);
  CHECK(plan.h[0] == 1);
  CHECK(plan.h[1] == 2);
  CHECK(plan.h[2] == 3);
  CHECK(plan.elastic == 0);
  CHECK_THAT(plan.elasticShare, WithinAbs(1.0 - 0.5 - 1.0 / 3.0, 1e-9));

  // Three muons are three straight lines: over one period the beam's
  // direction is steady within a slot and turns sharply between slots.
  const double period = kSampleRate / 110.0;
  const auto start = static_cast<std::size_t>(0.2 * kSampleRate);
  int turns = 0;
  for (std::size_t i = start + 2; i < start + static_cast<std::size_t>(period); ++i) {
    const double ax = r.x[i - 1] - r.x[i - 2], ay = r.y[i - 1] - r.y[i - 2];
    const double bx = r.x[i] - r.x[i - 1], by = r.y[i] - r.y[i - 1];
    const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
    if (la > 1e-4 && lb > 1e-4 && (ax * bx + ay * by) / (la * lb) < 0.5)
      ++turns;
  }
  CHECK(turns >= 3);  // into each of the three lines
  CHECK(turns <= 12); // and little else: a jump is two turns
}

TEST_CASE("Polyphony: keys come and go, and all keys up is a hard rest", "[poly][chamber]") {
  cs::ChamberEngine voice;
  voice.prepare(kSampleRate);
  voice.setParams(polyParams(cs::WallMode::Off));
  std::vector<float> x(4800), y(4800);
  voice.noteOn(45, 80);
  voice.process(x.data(), y.data(), 4800);
  CHECK_THAT(voice.periodHz(), WithinAbs(110.0, 1e-9)); // one key: as mono
  voice.noteOn(57, 80);
  voice.noteOn(64, 80);
  voice.process(x.data(), y.data(), 4800);
  CHECK_THAT(voice.periodHz(), WithinAbs(110.0, 1e-9));
  voice.noteOff(45); // A3 and E4 remain: 2 : 3 of A2
  voice.process(x.data(), y.data(), 4800);
  CHECK(voice.active());
  CHECK_THAT(voice.periodHz(), WithinAbs(110.0, 1e-6));
  voice.noteOff(64); // A3 alone
  voice.process(x.data(), y.data(), 4800);
  CHECK_THAT(voice.periodHz(), WithinAbs(220.0, 1e-9));
  CHECK(std::abs(x.back()) + std::abs(y.back()) > 0.01);
  voice.noteOff(57);
  voice.process(x.data(), y.data(), 4800);
  for (std::size_t i = 24; i < x.size(); ++i) { // silent within 0.5 ms
    REQUIRE(std::abs(x[i]) < 1e-4f);
    REQUIRE(std::abs(y[i]) < 1e-4f);
  }
  CHECK_FALSE(voice.active());
}

TEST_CASE("Polyphony: the voice limit keeps the newest keys", "[poly][chamber]") {
  cs::ChamberParams p = polyParams(cs::WallMode::Off);
  p.voices = 2;
  cs::ChamberEngine voice;
  voice.prepare(kSampleRate);
  voice.setParams(p);
  std::vector<float> x(4800), y(4800);
  voice.noteOn(45, 80);
  voice.noteOn(57, 80);
  voice.noteOn(69, 80); // A2 drops out: A3 and A4 remain
  voice.process(x.data(), y.data(), 4800);
  CHECK_THAT(voice.periodHz(), WithinAbs(220.0, 1e-9));
  voice.noteOff(69); // the held A2 comes back
  voice.process(x.data(), y.data(), 4800);
  CHECK_THAT(voice.periodHz(), WithinAbs(110.0, 1e-9));
}

TEST_CASE("Polyphony: default ageing stays in bounds in every wall mode", "[poly][chamber]") {
  const auto wall = GENERATE(cs::WallMode::Off, cs::WallMode::Shared, cs::WallMode::Drone);
  cs::ChamberParams p;
  p.voices = 6;
  p.wall = wall;
  const Render r = chord(p, {40, 47, 52, 56, 59, 64}, 2.6); // past every track's life
  double peak = 0.0;
  for (std::size_t i = 0; i < r.x.size(); ++i)
    peak = std::max(peak, analysis::radius(r.x[i], r.y[i]));
  CAPTURE(static_cast<int>(wall));
  CHECK(peak <= 0.95);
  CHECK(peak > 0.05);
}

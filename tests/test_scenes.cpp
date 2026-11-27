// The Scenes instrument and the CMS tracker sequence.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "analysis.h"
#include "cscore/cscore.h"

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kSampleRate = 48000.0;

struct Render {
  std::vector<float> x, y;
};

template <typename Engine>
Render play(Engine& voice, const typename Engine::Params& params, int note, int velocity, double held, double tail) {
  voice.prepare(kSampleRate);
  voice.setParams(params);
  const auto on = static_cast<std::size_t>(held * kSampleRate), off = static_cast<std::size_t>(tail * kSampleRate);
  Render r{std::vector<float>(on + off), std::vector<float>(on + off)};
  voice.noteOn(note, velocity);
  voice.process(r.x.data(), r.y.data(), static_cast<int>(on));
  voice.noteOff(note);
  voice.process(r.x.data() + on, r.y.data() + on, static_cast<int>(off));
  return r;
}

double reach(const std::vector<cs::Stroke>& strokes) {
  double r = 0.0;
  for (const cs::Stroke& s : strokes)
    for (const cs::Vec2& p : s)
      r = std::max(r, std::hypot(p.x, p.y));
  return r;
}

double height(const std::vector<cs::Stroke>& strokes) {
  double lo = 1e9, hi = -1e9;
  for (const cs::Stroke& s : strokes)
    for (const cs::Vec2& p : s) {
      lo = std::min(lo, p.y);
      hi = std::max(hi, p.y);
    }
  return hi - lo;
}

template <typename Engine>
void checkPitchAndRest(const typename Engine::Params& params, int velocity = 80) {
  for (int note : {33, 57, 81}) {
    Engine voice;
    const Render r = play(voice, params, note, velocity, 0.5, 0.1);
    const double period = analysis::periodByAutocorrelation(r.x, kSampleRate, static_cast<std::size_t>(0.2 * kSampleRate),
                                                           kSampleRate / cs::midiToHz(note));
    CAPTURE(note);
    CHECK(std::abs(cs::cents(kSampleRate / period, cs::midiToHz(note))) < 1.0);
    const auto off = static_cast<std::size_t>(0.5 * kSampleRate);
    CHECK(std::abs(r.x[off - 1]) + std::abs(r.y[off - 1]) > 0.001);
    for (std::size_t i = off + 24; i < r.x.size(); ++i) {
      REQUIRE(std::abs(r.x[i]) < 1e-4f);
      REQUIRE(std::abs(r.y[i]) < 1e-4f);
    }
  }
}

} // namespace

TEST_CASE("Scenes: every scene draws, stays on the screen, at any age and energy", "[scenes]") {
  cs::GalleryParams p;
  for (int i = 0; i < cs::kSceneCount; ++i) {
    p.scene = static_cast<cs::Scene>(i);
    for (double age : {0.0, 0.05, 0.3, 1.0, 4.7})
      for (double energy : {0.0, 0.5, 1.0})
        for (double detail : {0.0, 1.0}) {
          p.detail = detail;
          p.camera = {age * 50.0, 10.0 + 70.0 * energy, 0.0, energy};
          const auto strokes = cs::sceneStrokes(p, age, energy, 7 + static_cast<std::uint64_t>(i));
          CAPTURE(cs::sceneName(p.scene), age, energy, detail);
          REQUIRE_FALSE(strokes.empty());
          REQUIRE(reach(strokes) <= 0.9 + 1e-9);
          if (age >= 1.0)
            CHECK(reach(strokes) > 0.3); // grown to a fair size once formed
        }
  }
  CHECK(std::string(cs::sceneName(cs::Scene::EightfoldWay)) == "Eightfold Way");
}

TEST_CASE("Scenes: the same seed draws the same scene; another seed another", "[scenes]") {
  cs::GalleryParams p;
  p.speed = 0.0;
  for (cs::Scene scene : {cs::Scene::TrackingPlanes, cs::Scene::WireChamber, cs::Scene::Shower, cs::Scene::Cherenkov}) {
    p.scene = scene;
    const auto a = cs::sceneStrokes(p, 1.0, 0.7, 11), b = cs::sceneStrokes(p, 1.0, 0.7, 11), c = cs::sceneStrokes(p, 1.0, 0.7, 12);
    CAPTURE(cs::sceneName(scene));
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
      REQUIRE(a[i].size() == b[i].size());
    bool differs = a.size() != c.size();
    for (std::size_t i = 0; !differs && i < a.size(); ++i)
      differs = a[i].size() != c[i].size() || std::abs(a[i][0].x - c[i][0].x) > 1e-9 || std::abs(a[i].back().y - c[i].back().y) > 1e-9;
    CHECK(differs);
  }
}

TEST_CASE("Scenes: things build up over the form time", "[scenes]") {
  cs::GalleryParams p;
  p.formMs = 500.0;
  p.speed = 0.0;
  for (cs::Scene scene : {cs::Scene::Shower, cs::Scene::SparkChamber, cs::Scene::WireChamber, cs::Scene::EightfoldWay}) {
    p.scene = scene;
    auto points = [&](double age) {
      std::size_t n = 0;
      for (const cs::Stroke& s : cs::sceneStrokes(p, age, 0.8, 3))
        n += s.size();
      return n;
    };
    CAPTURE(cs::sceneName(scene));
    CHECK(points(0.1) < points(0.3));
    CHECK(points(0.3) <= points(0.6));
    CHECK(points(0.6) == points(2.0));
  }
}

TEST_CASE("Scenes: neutrino oscillation probabilities", "[scenes]") {
  for (int from = 0; from < 3; ++from) {
    for (double le : {0.0, 100.0, 500.0, 2000.0, 15000.0, 33000.0}) {
      double sum = 0.0;
      for (int to = 0; to < 3; ++to) {
        const double p = cs::neutrinoProbability(from, to, le);
        CHECK(p >= 0.0);
        CHECK(p <= 1.0);
        sum += p;
      }
      CAPTURE(from, le);
      CHECK_THAT(sum, WithinAbs(1.0, 1e-9)); // unitarity: it is always some flavour
    }
    CHECK_THAT(cs::neutrinoProbability(from, from, 0.0), WithinAbs(1.0, 1e-12)); // at the source it is what it was born as
  }
  // A muon neutrino at the first atmospheric maximum, L/E ~ 500 km/GeV: mostly gone, to tau.
  CHECK(cs::neutrinoProbability(1, 1, 495.0) < 0.05);
  CHECK(cs::neutrinoProbability(1, 2, 495.0) > 0.85);
  // Electron neutrinos barely notice the atmospheric scale (small theta13) but do the solar one.
  CHECK(cs::neutrinoProbability(0, 0, 495.0) > 0.85);
  CHECK(cs::neutrinoProbability(0, 0, 16500.0) < 0.35);
}

TEST_CASE("Scenes: pitch and hard rest, still and moving", "[scenes][acceptance]") {
  cs::GalleryParams p;
  p.formMs = 0.0;
  p.speed = 0.0; // still: one repeating picture
  p.spin = 0.0;
  p.scene = GENERATE(cs::Scene::TrackingPlanes, cs::Scene::Cherenkov, cs::Scene::Betatron, cs::Scene::FluxTube,
                     cs::Scene::HiggsPotential, cs::Scene::EightfoldWay);
  CAPTURE(cs::sceneName(p.scene));
  checkPitchAndRest<cs::GalleryEngine>(p);
}

TEST_CASE("Scenes: the moving ones move, and stay on the screen while they do", "[scenes]") {
  cs::GalleryParams p;
  p.voice.size = 1.0;
  p.speed = 2.0;
  for (cs::Scene scene : {cs::Scene::Betatron, cs::Scene::Cyclotron, cs::Scene::BunchCrossing, cs::Scene::FluxTube,
                          cs::Scene::SparkChamber, cs::Scene::Neutrino}) {
    p.scene = scene;
    const auto a = cs::sceneStrokes(p, 1.0, 0.7, 3), b = cs::sceneStrokes(p, 1.37, 0.7, 3);
    bool moved = a.size() != b.size();
    for (std::size_t i = 0; !moved && i < a.size(); ++i)
      moved = a[i].size() != b[i].size() || std::abs(a[i][a[i].size() / 2].x - b[i][b[i].size() / 2].x) > 1e-6 ||
              std::abs(a[i][a[i].size() / 2].y - b[i][b[i].size() / 2].y) > 1e-6;
    CAPTURE(cs::sceneName(scene));
    CHECK(moved);
    cs::GalleryEngine voice;
    const Render r = play(voice, p, 57, 90, 1.2, 0.0);
    double peak = 0.0;
    for (std::size_t i = 0; i < r.x.size(); ++i)
      peak = std::max(peak, analysis::radius(r.x[i], r.y[i]));
    CHECK(peak <= 0.95);
  }
}

// ---------------------------------------------------------------- CMS tracker

TEST_CASE("Tracker: the stages play in order, and each can be held", "[tracker]") {
  cs::TrackerParams p;
  p.spin = 0.0;
  // Auto, at speed 1: 2.5 s a stage.
  CHECK(cs::trackerFrame(p, 1.0, 0.5, 1).stage == cs::TrackerStage::Module);
  CHECK(cs::trackerFrame(p, 3.5, 0.5, 1).stage == cs::TrackerStage::Exploded);
  CHECK(cs::trackerFrame(p, 6.0, 0.5, 1).stage == cs::TrackerStage::Tracker);
  CHECK(cs::trackerFrame(p, 9.0, 0.5, 1).stage == cs::TrackerStage::Tracks);
  CHECK(cs::trackerFrame(p, 40.0, 0.5, 1).stage == cs::TrackerStage::Tracks); // and tracks keep coming
  p.speed = 2.0;
  CHECK(cs::trackerFrame(p, 1.5, 0.5, 1).stage == cs::TrackerStage::Exploded);

  p.speed = 1.0;
  // The explosion opens through stage 2; stage 3 puts it back and flies it home.
  CHECK(cs::trackerFrame(p, 2.6, 0.5, 1).explode < cs::trackerFrame(p, 3.5, 0.5, 1).explode);
  CHECK_THAT(cs::trackerFrame(p, 4.9, 0.5, 1).explode, WithinAbs(1.0, 1e-9));
  CHECK(cs::trackerFrame(p, 5.3, 0.5, 1).explode < 1.0);
  CHECK(cs::trackerFrame(p, 6.0, 0.5, 1).zoom < cs::trackerFrame(p, 7.0, 0.5, 1).zoom);
  CHECK_THAT(cs::trackerFrame(p, 7.45, 0.5, 1).zoom, WithinAbs(1.0, 1e-9));

  p.stage = cs::TrackerStage::Exploded;
  CHECK_THAT(cs::trackerFrame(p, 0.1, 0.5, 1).explode, WithinAbs(1.0, 1e-9));
  p.stage = cs::TrackerStage::Tracker;
  CHECK_THAT(cs::trackerFrame(p, 0.1, 0.5, 1).zoom, WithinAbs(1.0, 1e-9));
}

TEST_CASE("Tracker: exploded, the module is taller; both module types draw", "[tracker]") {
  cs::TrackerParams p;
  p.spin = 0.0;
  for (cs::ModuleType type : {cs::ModuleType::TwoS, cs::ModuleType::PS}) {
    p.module = type;
    p.stage = cs::TrackerStage::Module;
    const auto together = cs::trackerFrame(p, 1.0, 0.5, 1);
    p.stage = cs::TrackerStage::Exploded;
    const auto apart = cs::trackerFrame(p, 1.0, 0.5, 1);
    CAPTURE(static_cast<int>(type));
    CHECK(height(apart.strokes) > 1.1); // the layers stand apart across most of the screen
    CHECK(height(together.strokes) < height(apart.strokes));
    CHECK(together.strokes.size() == apart.strokes.size()); // the same parts, moved
    CHECK(reach(apart.strokes) <= 0.9 + 1e-9);
  }
}

TEST_CASE("Tracker: tracks leave stubs where they cross the layers", "[tracker]") {
  cs::TrackerParams p;
  p.stage = cs::TrackerStage::Tracks;
  p.speed = 0.0; // the whole event at once
  int total = 0;
  for (std::uint64_t seed = 1; seed <= 10; ++seed) {
    const auto quiet = cs::trackerFrame(p, 1.0, 0.0, seed), busy = cs::trackerFrame(p, 1.0, 1.0, seed);
    CHECK(busy.stubs >= quiet.stubs);
    CHECK(quiet.stubs >= 3); // three tracks or more, each crossing some layers
    CHECK(reach(busy.strokes) <= 0.9 + 1e-9);
    total += busy.stubs;
  }
  // Twelve tracks of 1-10 GeV cross most of the six barrel layers or the discs: dozens of stubs.
  CHECK(total > 10 * 30);
  // A new event each stage-length.
  p.speed = 1.0;
  const auto first = cs::trackerFrame(p, 1.0, 0.7, 4), second = cs::trackerFrame(p, 3.6, 0.7, 4);
  const cs::Vec2 a = first.strokes.back().back(), b = second.strokes.back().back(); // where the last track ends
  CHECK(std::hypot(a.x - b.x, a.y - b.y) > 1e-3);
}

TEST_CASE("Tracker: pitch, picture and hard rest", "[tracker][acceptance]") {
  cs::TrackerParams p;
  p.spin = 0.0;
  SECTION("pitch and rest, module held") {
    p.stage = cs::TrackerStage::Module;
    checkPitchAndRest<cs::TrackerEngine>(p);
  }
  SECTION("the whole sequence stays within r <= 0.95") {
    p.voice.size = 1.0;
    p.speed = 3.0;
    p.spin = 40.0;
    cs::TrackerEngine voice;
    const Render r = play(voice, p, 57, 110, 5.0, 0.0);
    double peak = 0.0;
    for (std::size_t i = 0; i < r.x.size(); ++i)
      peak = std::max(peak, analysis::radius(r.x[i], r.y[i]));
    CHECK(peak <= 0.95);
    CHECK(peak > 0.5);
  }
}

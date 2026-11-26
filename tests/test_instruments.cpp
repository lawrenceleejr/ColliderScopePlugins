// CS Bubble and CS Orbital: the physics of what they draw, and the same
// acceptance tests as CS Chamber (pitch, picture, hard rest).
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "analysis.h"
#include "cscore/cscore.h"

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

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

template <typename Engine>
void checkPitch(const typename Engine::Params& params, int velocity) {
  for (int note : {33, 45, 57, 69, 81}) {
    Engine voice;
    const Render r = play(voice, params, note, velocity, 0.6, 0.0);
    const double period = analysis::periodByAutocorrelation(r.x, kSampleRate, static_cast<std::size_t>(0.2 * kSampleRate),
                                                           kSampleRate / cs::midiToHz(note));
    CAPTURE(note);
    CHECK(std::abs(cs::cents(kSampleRate / period, cs::midiToHz(note))) < 1.0);
  }
}

template <typename Engine>
void checkHardRest(const typename Engine::Params& params) {
  Engine voice;
  const Render r = play(voice, params, 57, 80, 0.3, 0.1);
  const auto off = static_cast<std::size_t>(0.3 * kSampleRate);
  CHECK(std::abs(r.x[off - 1]) + std::abs(r.y[off - 1]) > 0.005);
  for (std::size_t i = off + 24; i < r.x.size(); ++i) { // 0.5 ms
    REQUIRE(std::abs(r.x[i]) < 1e-4f);
    REQUIRE(std::abs(r.y[i]) < 1e-4f);
  }
  CHECK(r.x.back() == 0.0f);
  CHECK_FALSE(voice.active());
}

// Signed turning of a polyline, in radians: positive is anticlockwise.
double turning(const std::vector<cs::Vec2>& p, std::size_t from, std::size_t to) {
  double sum = 0.0;
  for (std::size_t i = from + 2; i < to; ++i) {
    const double a = std::atan2(p[i - 1].y - p[i - 2].y, p[i - 1].x - p[i - 2].x);
    const double b = std::atan2(p[i].y - p[i - 1].y, p[i].x - p[i - 1].x);
    sum += std::remainder(b - a, 2.0 * kPi);
  }
  return sum;
}

} // namespace

// ------------------------------------------------------------------ CS Bubble

TEST_CASE("Bubble: a charged track curls in the field and tightens as it slows", "[bubble]") {
  const cs::BubbleTrack plus = cs::curlTrack({0.0, -0.3}, 0.0, 0.8, +1, 0.5, 0.55);
  const cs::BubbleTrack minus = cs::curlTrack({0.0, 0.3}, 0.0, 0.8, -1, 0.5, 0.55);
  REQUIRE(plus.points.size() > 200);
  REQUIRE(minus.points.size() > 200);
  CHECK(turning(plus.points, 0, plus.points.size()) > 2.0 * kPi);   // more than a full turn, anticlockwise
  CHECK(turning(minus.points, 0, minus.points.size()) < -2.0 * kPi); // the other charge, the other way
  // Turning per step grows: the radius of curvature shrinks with the momentum.
  const std::size_t n = plus.points.size();
  CHECK(turning(plus.points, n - 60, n) > 2.0 * turning(plus.points, 0, 60));
  for (const cs::Vec2& p : plus.points)
    CHECK(std::hypot(p.x, p.y) <= 0.9);
  // Evenly spaced bubbles need evenly spaced points.
  for (std::size_t i = 1; i < n; ++i)
    CHECK_THAT(std::hypot(plus.points[i].x - plus.points[i - 1].x, plus.points[i].y - plus.points[i - 1].y), WithinAbs(0.01, 1e-9));
  // No field to speak of, lots of momentum: nearly straight.
  const cs::BubbleTrack beam = cs::curlTrack({-0.8, 0.0}, 0.0, 8.0, +1, 0.0, 0.05);
  CHECK(std::abs(turning(beam.points, 0, beam.points.size())) < 0.1);
}

TEST_CASE("Bubble: velocity picks the event, and each event has its shape", "[bubble]") {
  cs::BubbleParams p;
  auto born = [&](int velocity, cs::BubbleType type = cs::BubbleType::Auto) {
    p.type = type;
    cs::BubbleSource source;
    source.birth(p, 60, velocity, 1);
    return source;
  };
  CHECK(born(30).type() == cs::BubbleType::Electron);
  CHECK(born(60).type() == cs::BubbleType::Pair);
  CHECK(born(100).type() == cs::BubbleType::Decay);
  CHECK(born(120).type() == cs::BubbleType::Beam);

  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    p.seed = seed;
    const cs::BubbleSource pair = born(60, cs::BubbleType::Pair);
    REQUIRE(pair.tracks().size() == 2);
    // One vertex, opposite charges, opposite curls.
    CHECK(pair.tracks()[0].points.front().x == pair.tracks()[1].points.front().x);
    CHECK(pair.tracks()[0].charge == -pair.tracks()[1].charge);
    CHECK(turning(pair.tracks()[0].points, 0, pair.tracks()[0].points.size()) > 0.5);
    CHECK(turning(pair.tracks()[1].points, 0, pair.tracks()[1].points.size()) < -0.5);

    const cs::BubbleSource decay = born(100, cs::BubbleType::Decay);
    REQUIRE(decay.tracks().size() == 2);
    // The daughter starts where the parent stops, and curls tighter.
    CHECK(decay.tracks()[0].points.back().x == decay.tracks()[1].points.front().x);
    const auto& parent = decay.tracks()[0].points;
    const auto& daughter = decay.tracks()[1].points;
    if (daughter.size() > 40)
      CHECK(std::abs(turning(daughter, 0, 40)) > 2.0 * std::abs(turning(parent, 0, 40)));

    const cs::BubbleSource electron = born(30, cs::BubbleType::Electron);
    REQUIRE(electron.tracks().size() == 1);
    CHECK(std::abs(turning(electron.tracks()[0].points, 0, electron.tracks()[0].points.size())) > 1.5 * kPi);
  }
}

TEST_CASE("Bubble: a young track is a row of dots on the particle's path", "[bubble]") {
  cs::BubbleParams p;
  p.type = cs::BubbleType::Electron;
  p.growth = 0.0;
  p.boil = 0.0;
  p.formMs = 0.0;
  cs::BubbleEngine voice;
  const Render r = play(voice, p, 45, 30, 0.2, 0.0);
  const auto& track = voice.source().tracks()[0].points;
  std::size_t onTrack = 0;
  for (std::size_t i = 4800; i < r.x.size(); ++i) {
    double nearest = 1e9;
    for (const cs::Vec2& q : track)
      nearest = std::min(nearest, std::hypot(r.x[i] / 0.9 - q.x, r.y[i] / 0.9 - q.y)); // Size 0.9
    onTrack += nearest < 0.02;
  }
  // All but the jump back from the track's end to its start.
  CHECK(static_cast<double>(onTrack) / static_cast<double>(r.x.size() - 4800) > 0.9);
}

TEST_CASE("Bubble: bubbles grow while the note is held", "[bubble]") {
  cs::BubbleParams p;
  p.type = cs::BubbleType::Beam; // a line: any spread across it is bubble size
  p.boil = 0.0;
  p.growth = 0.05;
  p.maxRadius = 0.04;
  cs::BubbleEngine voice;
  const Render r = play(voice, p, 45, 120, 1.0, 0.0);
  const auto& track = voice.source().tracks()[0].points;
  auto spread = [&](std::size_t from, std::size_t to) { // how far the beam strays from the path: 90th percentile
    std::vector<double> off;
    for (std::size_t i = from; i < to; ++i) {
      double nearest = 1e9;
      for (const cs::Vec2& q : track)
        nearest = std::min(nearest, std::hypot(r.x[i] / 0.9 - q.x, r.y[i] / 0.9 - q.y));
      off.push_back(nearest);
    }
    std::sort(off.begin(), off.end());
    return off[off.size() * 9 / 10];
  };
  const double young = spread(4800, 7200), old = spread(43200, 48000);
  CAPTURE(young, old);
  CHECK(young < 0.012); // 0.1 s old: radius about 0.007
  CHECK(old > 0.025);   // 1 s old: grown to the 0.04 limit
  CHECK(old < 0.04 * 1.4 + 0.012); // the largest bubble, plus half a step along the track
}

TEST_CASE("Bubble: pitch, picture and hard rest", "[bubble][acceptance]") {
  const auto type = GENERATE(cs::BubbleType::Electron, cs::BubbleType::Pair, cs::BubbleType::Decay, cs::BubbleType::Beam);
  cs::BubbleParams p;
  p.type = type;
  CAPTURE(static_cast<int>(type));
  SECTION("pitch within 1 cent on a steady picture") {
    p.boil = 0.0;
    p.growth = 0.0;
    checkPitch<cs::BubbleEngine>(p, 80);
  }
  SECTION("within r <= 0.95, for any seed, field and age") {
    p.voice.size = 1.0;
    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
      p.seed = seed;
      p.field = 0.15 * static_cast<double>(seed);
      cs::BubbleEngine voice;
      const Render r = play(voice, p, 57, 80, 2.0, 0.0);
      double peak = 0.0;
      for (std::size_t i = 0; i < r.x.size(); ++i)
        peak = std::max(peak, analysis::radius(r.x[i], r.y[i]));
      CHECK(peak <= 0.95);
      CHECK(peak > 0.05);
    }
  }
  SECTION("hard rest") { checkHardRest<cs::BubbleEngine>(p); }
}

// ----------------------------------------------------------------- CS Orbital

TEST_CASE("Orbital: hydrogen radial functions have their nodes in the right places", "[orbital]") {
  auto nodes = [](int n, int l) {
    std::vector<double> found;
    double previous = cs::hydrogenRadial(n, l, 0.05);
    for (double r = 0.06; r < 60.0; r += 0.001) {
      const double value = cs::hydrogenRadial(n, l, r);
      if (value * previous < 0.0)
        found.push_back(r);
      previous = value;
    }
    return found;
  };
  CHECK(nodes(1, 0).empty());
  REQUIRE(nodes(2, 0).size() == 1);
  CHECK_THAT(nodes(2, 0)[0], WithinAbs(2.0, 0.002)); // R_20 ~ (2 - r) e^(-r/2)
  REQUIRE(nodes(3, 0).size() == 2);
  CHECK_THAT(nodes(3, 0)[0], WithinAbs(1.5 * (3.0 - std::sqrt(3.0)), 0.002)); // 1.902
  CHECK_THAT(nodes(3, 0)[1], WithinAbs(1.5 * (3.0 + std::sqrt(3.0)), 0.002)); // 7.098
  REQUIRE(nodes(3, 1).size() == 1);
  CHECK_THAT(nodes(3, 1)[0], WithinAbs(6.0, 0.002)); // R_31 ~ r (6 - r) e^(-r/3)
  CHECK(nodes(3, 2).empty());
  CHECK(nodes(4, 0).size() == 3);
  CHECK(nodes(4, 3).empty());
  // 1s decays as e^(-r).
  CHECK_THAT(cs::hydrogenRadial(1, 0, 2.0) / cs::hydrogenRadial(1, 0, 1.0), WithinAbs(std::exp(-1.0), 1e-12));
}

TEST_CASE("Orbital: associated Legendre functions", "[orbital]") {
  for (double x : {-0.9, -0.3, 0.0, 0.4, 0.8}) {
    const double s = std::sqrt(1.0 - x * x);
    CHECK_THAT(cs::legendre(0, 0, x), WithinAbs(1.0, 1e-12));
    CHECK_THAT(cs::legendre(1, 0, x), WithinAbs(x, 1e-12));
    CHECK_THAT(cs::legendre(1, 1, x), WithinAbs(-s, 1e-12));
    CHECK_THAT(cs::legendre(2, 0, x), WithinAbs(0.5 * (3 * x * x - 1), 1e-12));
    CHECK_THAT(cs::legendre(2, 1, x), WithinAbs(-3 * x * s, 1e-12));
    CHECK_THAT(cs::legendre(2, 2, x), WithinAbs(3 * (1 - x * x), 1e-12));
    CHECK_THAT(cs::legendre(3, 0, x), WithinAbs(0.5 * (5 * x * x * x - 3 * x), 1e-12));
    CHECK_THAT(cs::legendre(3, 2, x), WithinAbs(15 * x * (1 - x * x), 1e-12));
    CHECK_THAT(cs::legendre(3, 3, x), WithinAbs(-15 * s * s * s, 1e-12));
  }
}

TEST_CASE("Orbital: isosurfaces have the shapes of the orbitals", "[orbital]") {
  cs::OrbitalShape shape;
  for (const cs::OrbitalId& id : cs::kOrbitals) {
    shape.set(id.n, id.l, id.m);
    CAPTURE(id.name);
    CHECK(shape.shells() == id.n - id.l); // one shell per radial lobe
    double largest = 0.0;
    for (int i = 0; i <= 60; ++i)
      for (int j = 0; j < 60; ++j)
        for (int shell = 0; shell < shape.shells(); ++shell) {
          const double r = shape.radius(shell, 0.25, kPi * i / 60.0, 2.0 * kPi * j / 60.0);
          REQUIRE(r >= 0.0);
          REQUIRE(r <= 1.0 + 1e-9);
          if (shell + 1 < shape.shells() && r > 0.0) // shells nest
            REQUIRE(r < shape.radius(shape.shells() - 1, 0.25, kPi * i / 60.0, 2.0 * kPi * j / 60.0));
          largest = std::max(largest, r);
        }
    CHECK_THAT(largest, WithinAbs(1.0, 0.01)); // the outer shell just reaches 1
  }

  SECTION("1s is a sphere") {
    shape.set(1, 0, 0);
    for (double theta : {0.0, 0.7, 1.5, 3.0})
      CHECK_THAT(shape.radius(0, 0.3, theta, 1.1), WithinAbs(1.0, 1e-9));
  }
  SECTION("2s is a sphere within a sphere") {
    shape.set(2, 0, 0);
    CHECK_THAT(shape.radius(1, 0.3, 1.0, 0.0), WithinAbs(1.0, 1e-9));
    CHECK(shape.radius(0, 0.3, 1.0, 0.0) > 0.01);
    CHECK(shape.radius(0, 0.3, 1.0, 0.0) < 0.3);
  }
  SECTION("2pz: two lobes along z, nothing in the xy plane") {
    shape.set(2, 1, 0);
    CHECK_THAT(shape.radius(0, 0.25, 0.0, 0.0), WithinAbs(1.0, 1e-9));
    CHECK_THAT(shape.radius(0, 0.25, kPi, 0.0), WithinAbs(1.0, 1e-9));
    CHECK(shape.radius(0, 0.25, kPi / 2, 0.3) == 0.0);
    CHECK(shape.radius(0, 0.25, 1.0, 0.0) < 1.0);
    CHECK(shape.radius(0, 0.25, 1.0, 0.0) > 0.3);
  }
  SECTION("2px: lobes along x, a node in the yz plane") {
    shape.set(2, 1, 1);
    CHECK_THAT(shape.radius(0, 0.25, kPi / 2, 0.0), WithinAbs(1.0, 1e-9));
    CHECK_THAT(shape.radius(0, 0.25, kPi / 2, kPi), WithinAbs(1.0, 1e-9));
    CHECK(shape.radius(0, 0.25, kPi / 2, kPi / 2) == 0.0);
    CHECK(shape.radius(0, 0.25, 0.0, 0.0) == 0.0);
  }
  SECTION("3dz2: lobes on the axis, a ring at the equator, cones of nodes between") {
    shape.set(3, 2, 0);
    CHECK_THAT(shape.radius(0, 0.2, 0.0, 0.0), WithinAbs(1.0, 1e-9));
    CHECK(shape.radius(0, 0.2, kPi / 2, 0.0) > 0.3); // the ring: half the amplitude of the lobes
    CHECK(shape.radius(0, 0.2, kPi / 2, 0.0) < 1.0);
    CHECK(shape.radius(0, 0.2, std::acos(1.0 / std::sqrt(3.0)), 0.0) == 0.0); // 54.7 degrees
  }
  SECTION("a lower level draws a fatter surface") {
    shape.set(2, 1, 0);
    CHECK(shape.radius(0, 0.1, 1.2, 0.0) > 0.0);
    CHECK(shape.radius(0, 0.5, 1.2, 0.0) == 0.0);
  }
}

TEST_CASE("Orbital: Auto picks s, p, d, f by velocity", "[orbital]") {
  auto l = [](int note, int velocity) { return cs::kOrbitals[static_cast<std::size_t>(cs::OrbitalSource::autoOrbital(note, velocity) - 1)].l; };
  CHECK(l(60, 20) == 0);
  CHECK(l(60, 60) == 1);
  CHECK(l(60, 100) == 2);
  CHECK(l(60, 127) == 3);
  // The key turns the orbital: every m of that l comes up.
  std::vector<int> seen;
  for (int note = 60; note < 64; ++note)
    seen.push_back(cs::kOrbitals[static_cast<std::size_t>(cs::OrbitalSource::autoOrbital(note, 127) - 1)].m);
  std::sort(seen.begin(), seen.end());
  CHECK(seen == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("Orbital: the wireframe of 1s is circles on a sphere", "[orbital]") {
  cs::OrbitalParams p;
  p.orbital = 1;
  p.spin = 0.0;
  p.tilt = 0.0;
  p.bloomMs = 0.0;
  p.voice.size = 1.0;
  cs::OrbitalEngine voice;
  const Render r = play(voice, p, 57, 80, 0.2, 0.0);
  double peak = 0.0;
  std::size_t onRim = 0;
  for (std::size_t i = 0; i < r.x.size(); ++i) {
    const double radius = analysis::radius(r.x[i], r.y[i]);
    peak = std::max(peak, radius);
    onRim += std::abs(radius - 0.9) < 0.005;
  }
  CHECK_THAT(peak, WithinAbs(0.9, 0.002));
  // Seen along no tilt, the meridians are ellipses and lines inside the rim;
  // one of them is the rim itself.
  CHECK(static_cast<double>(onRim) / static_cast<double>(r.x.size()) > 0.08);
}

TEST_CASE("Orbital: pitch, picture and hard rest", "[orbital][acceptance]") {
  cs::OrbitalParams p;
  SECTION("pitch within 1 cent on a steady picture") {
    p.spin = 0.0;
    p.bloomMs = 0.0;
    p.orbital = GENERATE(1, 4, 8, 17);
    checkPitch<cs::OrbitalEngine>(p, 80);
  }
  SECTION("every orbital stays within r <= 0.95 while it spins and blooms") {
    p.voice.size = 1.0;
    p.spin = 120.0;
    for (int orbital = 1; orbital <= 20; ++orbital) {
      p.orbital = orbital;
      p.tilt = 4.5 * orbital;
      cs::OrbitalEngine voice;
      const Render r = play(voice, p, 57, 80, 0.5, 0.0);
      double peak = 0.0;
      for (std::size_t i = 0; i < r.x.size(); ++i)
        peak = std::max(peak, analysis::radius(r.x[i], r.y[i]));
      CAPTURE(orbital);
      CHECK(peak <= 0.95);
      CHECK(peak > 0.3);
    }
  }
  SECTION("hard rest") { checkHardRest<cs::OrbitalEngine>(p); }
}

TEST_CASE("Orbital: it spins, and it blooms from the lobe's core", "[orbital]") {
  cs::OrbitalParams p;
  p.orbital = 4; // 2px: lobes along x that swing round as it spins
  p.tilt = 0.0;
  p.spin = 90.0;
  p.bloomMs = 0.0;
  p.voice.size = 1.0;
  cs::OrbitalEngine voice;
  const Render r = play(voice, p, 57, 80, 1.05, 0.0);
  auto width = [&](std::size_t from) { // widest reach in x over 20 ms
    float w = 0.0f;
    for (std::size_t i = from; i < from + 960; ++i)
      w = std::max(w, std::abs(r.x[i]));
    return w;
  };
  CHECK(width(480) > 0.85);   // lobes along x, side on
  CHECK(width(47040) < 0.75); // a quarter turn later, end on: only the lobes' width shows

  p.spin = 0.0;
  p.bloomMs = 200.0;
  cs::OrbitalEngine blooming;
  const Render b = play(blooming, p, 57, 80, 0.4, 0.0);
  auto reach = [&](std::size_t from) {
    double w = 0.0;
    for (std::size_t i = from; i < from + 480; ++i)
      w = std::max(w, analysis::radius(b.x[i], b.y[i]));
    return w;
  };
  CHECK(reach(480) < reach(4800));
  CHECK(reach(4800) < reach(14400));
  CHECK_THAT(reach(14400), WithinAbs(0.9, 0.01));
}

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "cscore/cscore.h"

using Catch::Matchers::WithinAbs;

namespace {
double dist(cs::Vec2 a, cs::Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
} // namespace

TEST_CASE("Alpha: straight, 0.30-0.45 long, from the source, 3 strands", "[tracks]") {
  for (std::uint64_t seed = 1; seed <= 50; ++seed) {
    cs::Rng rng(seed);
    const cs::Track t = cs::makeTrack(cs::TrackType::Alpha, rng);
    REQUIRE(t.points.size() == 16);
    CHECK(t.strands == 3);
    CHECK_THAT(t.spread, WithinAbs(0.009, 1e-12));
    CHECK_THAT(t.life, WithinAbs(2.0, 1e-12));
    CHECK_THAT(dist(t.points.front(), {-0.42, -0.30}), WithinAbs(0.04, 1e-9));
    const double reach = dist(t.points.back(), {-0.42, -0.30});
    CHECK(reach >= 0.30 - 1e-9);
    CHECK(reach <= std::hypot(0.45, 0.03) + 1e-9);
    // Straight until the recoil kink on the last 4 points, bent up to 0.03.
    const cs::Vec2 a = t.points[0], b = t.points[11];
    const double ux = (b.x - a.x) / dist(a, b), uy = (b.y - a.y) / dist(a, b);
    for (std::size_t i = 0; i < 16; ++i) {
      const double off = std::abs((t.points[i].x - a.x) * uy - (t.points[i].y - a.y) * ux);
      if (i <= 12)
        CHECK_THAT(off, WithinAbs(0.0, 1e-9));
      else
        CHECK(off <= 0.03 + 1e-9);
    }
    CHECK_THAT(std::abs((t.points[15].x - a.x) * uy - (t.points[15].y - a.y) * ux), WithinAbs(0.03, 1e-9));
  }
}

TEST_CASE("Muon: straight across the chamber, 2.6 long, 44 points", "[tracks]") {
  for (std::uint64_t seed = 1; seed <= 50; ++seed) {
    cs::Rng rng(seed);
    const cs::Track t = cs::makeTrack(cs::TrackType::Muon, rng);
    REQUIRE(t.points.size() == 44);
    CHECK(t.strands == 1);
    CHECK_THAT(t.life, WithinAbs(2.4, 1e-12));
    CHECK_THAT(dist(t.points.front(), t.points.back()), WithinAbs(2.6, 1e-9));
    const cs::Vec2 mid{(t.points.front().x + t.points.back().x) / 2, (t.points.front().y + t.points.back().y) / 2};
    CHECK(std::abs(mid.x) <= 0.4 + 1e-9);
    CHECK(std::abs(mid.y) <= 0.4 + 1e-9);
    for (std::size_t i = 1; i < 44; ++i)
      CHECK_THAT(dist(t.points[i - 1], t.points[i]), WithinAbs(2.6 / 43, 1e-9));
  }
}

TEST_CASE("Electron: 60 steps of 0.018, curling one way", "[tracks]") {
  for (std::uint64_t seed = 1; seed <= 50; ++seed) {
    cs::Rng rng(seed);
    const cs::Track t = cs::makeTrack(cs::TrackType::Electron, rng);
    REQUIRE(t.points.size() == 61);
    CHECK(t.strands == 1);
    CHECK_THAT(t.life, WithinAbs(1.8, 1e-12));
    double turned = 0.0;
    for (std::size_t i = 1; i < 61; ++i) {
      CHECK_THAT(dist(t.points[i - 1], t.points[i]), WithinAbs(0.018, 1e-9));
      if (i >= 2) {
        const double h0 = std::atan2(t.points[i - 1].y - t.points[i - 2].y, t.points[i - 1].x - t.points[i - 2].x);
        const double h1 = std::atan2(t.points[i].y - t.points[i - 1].y, t.points[i].x - t.points[i - 1].x);
        turned += std::remainder(h1 - h0, 2.0 * 3.14159265358979323846);
      }
    }
    // Curl sums to 0.006 * (1 + ... + 59) = 10.6 rad against heading noise of sigma 1.7 rad.
    CHECK(std::abs(turned) > 4.0);
  }
}

TEST_CASE("Type follows velocity", "[tracks]") {
  CHECK(cs::typeForVelocity(1) == cs::TrackType::Electron);
  CHECK(cs::typeForVelocity(50) == cs::TrackType::Electron);
  CHECK(cs::typeForVelocity(51) == cs::TrackType::Alpha);
  CHECK(cs::typeForVelocity(100) == cs::TrackType::Alpha);
  CHECK(cs::typeForVelocity(101) == cs::TrackType::Muon);
  CHECK(cs::typeForVelocity(127) == cs::TrackType::Muon);
}

TEST_CASE("Ageing: condense, drift, diffuse, clip", "[tracks]") {
  cs::Rng rng(3), noise(4);
  const cs::Track alpha = cs::makeTrack(cs::TrackType::Alpha, rng);
  const cs::AgeingParams still{0.0, 0.0, 0.06};
  std::vector<cs::Stroke> out;

  SECTION("condense shows max(2, N * a / 0.06) points") {
    cs::ageTrack(alpha, 0.0, still, noise, out);
    REQUIRE(out.size() == 3);
    CHECK(out[0].size() == 2);
    out.clear();
    cs::ageTrack(alpha, 0.03, still, noise, out);
    CHECK(out[0].size() == 8);
    out.clear();
    cs::ageTrack(alpha, 0.5, still, noise, out);
    CHECK(out[0].size() == 16);
  }
  SECTION("drift is (0.01, -0.06) per second") {
    cs::ageTrack(alpha, 1.0, {0.0, 0.06, 0.06}, noise, out);
    REQUIRE(out.size() == 3);
    double dx = 0.0, dy = 0.0;
    for (std::size_t i = 0; i < 16; ++i) { // the middle strand sits on the track
      dx += out[1][i].x - alpha.points[i].x;
      dy += out[1][i].y - alpha.points[i].y;
    }
    CHECK_THAT(dx / 16, WithinAbs(0.01, 0.002)); // noise sigma 0.002 over 16 points
    CHECK_THAT(dy / 16, WithinAbs(-0.06, 0.002));
  }
  SECTION("diffusion is sigma = 0.002 + 0.03 a") {
    cs::Rng r(9);
    const cs::Track muon = cs::makeTrack(cs::TrackType::Muon, r);
    double sum = 0.0;
    std::size_t count = 0;
    for (int pass = 0; pass < 200; ++pass) {
      out.clear();
      cs::ageTrack(muon, 1.0, {0.03, 0.0, 0.06}, noise, out);
      for (const cs::Vec2& p : out[0]) {
        // distance from the muon's line
        const cs::Vec2 a = muon.points.front(), b = muon.points.back();
        const double off = ((p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x)) / 2.6;
        sum += off * off;
        ++count;
      }
    }
    CHECK_THAT(std::sqrt(sum / static_cast<double>(count)), WithinAbs(0.032, 0.002));
  }
  SECTION("everything stays inside the wall, and the track dies at the end of its life") {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
      cs::Rng r(seed);
      const cs::Track muon = cs::makeTrack(cs::TrackType::Muon, r);
      out.clear();
      cs::ageTrack(muon, 2.0, {}, noise, out);
      for (const cs::Stroke& s : out)
        for (const cs::Vec2& p : s)
          CHECK(std::hypot(p.x, p.y) < 0.87);
      out.clear();
      cs::ageTrack(muon, 2.41, {}, noise, out);
      CHECK(out.empty());
    }
  }
}

TEST_CASE("Harmonic lock: h = m / (1 - p), p nearest 0.45 within 0.3-0.7", "[tracks]") {
  for (int h = 2; h <= 26; ++h) {
    const cs::HarmonicSplit s = cs::harmonicSplit(h);
    CHECK(s.m >= 1);
    CHECK(s.p >= 0.3 - 1e-9);
    CHECK(s.p <= 0.7 + 1e-9);
    CHECK_THAT(s.m / (1.0 - s.p), WithinAbs(h, 1e-9));
  }
  // Values the reference `split` gives.
  CHECK(cs::harmonicSplit(4).m == 2);
  CHECK_THAT(cs::harmonicSplit(4).p, WithinAbs(0.5, 1e-12));
  CHECK(cs::harmonicSplit(5).m == 3);
  CHECK_THAT(cs::harmonicSplit(5).p, WithinAbs(0.4, 1e-12));
  CHECK(cs::harmonicSplit(8).m == 4);
  CHECK(cs::harmonicSplit(12).m == 7);
  // A manual wall share picks the nearest achievable p.
  CHECK(cs::harmonicSplit(10, 0.7).m == 3);

  CHECK(cs::nearestHarmonic(220.0, 55.0) == 4);
  CHECK(cs::nearestHarmonic(233.1, 55.0) == 4);
  CHECK(cs::nearestHarmonic(55.0, 55.0) == 2);
}

TEST_CASE("The wall is a closed circle of radius 0.9", "[tracks]") {
  const cs::Stroke wall = cs::wallStroke();
  REQUIRE(wall.size() == 193);
  for (const cs::Vec2& p : wall)
    CHECK_THAT(std::hypot(p.x, p.y), WithinAbs(0.9, 1e-12));
  CHECK(wall.front().x == wall.back().x);
  CHECK(wall.front().y == wall.back().y);
}

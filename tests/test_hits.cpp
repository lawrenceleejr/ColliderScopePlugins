#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "analysis.h"
#include "cscore/cscore.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("Kick: a spiral collapsing from r0 over 110 ms", "[hits]") {
  cs::KickParams quiet;
  quiet.click = 0.0;
  const auto kick = cs::kickSpiral(48000.0, 0.8, 0.0, 3, quiet);
  REQUIRE(kick.size() == 5280);
  CHECK_THAT(analysis::radius(kick[0].x, kick[0].y), WithinAbs(0.8, 1e-6));
  // Radius decays with tau 50 ms.
  CHECK_THAT(analysis::radius(kick[2400].x, kick[2400].y), WithinAbs(0.8 * std::exp(-1.0), 1e-5));
  // Sweep 185 -> 48 Hz with tau 28 ms: total turns = integral of f.
  double turns = 0.0, last = std::atan2(kick[0].y, kick[0].x);
  for (std::size_t i = 1; i < kick.size(); ++i) {
    const double a = std::atan2(kick[i].y, kick[i].x);
    turns += std::remainder(a - last, 2.0 * 3.14159265358979323846);
    last = a;
  }
  const double expected = 48.0 * 0.11 + 137.0 * 0.028 * (1.0 - std::exp(-0.11 / 0.028));
  CHECK_THAT(turns / (2.0 * 3.14159265358979323846), WithinAbs(expected, 0.02));

  // The click is 4 % of r0 and gone within a few ms.
  const auto clicked = cs::kickSpiral(48000.0, 0.8);
  double early = 0.0;
  for (std::size_t i = 0; i < 48; ++i)
    early = std::max(early, std::abs(analysis::radius(clicked[i].x, clicked[i].y) - analysis::radius(kick[i].x, kick[i].y)));
  CHECK(early > 0.005);
  CHECK(early < 0.2);
  CHECK_THAT(analysis::radius(clicked[2400].x, clicked[2400].y), WithinAbs(analysis::radius(kick[2400].x, kick[2400].y), 1e-6));
}

TEST_CASE("Spark rings: random phase on a decaying ring", "[hits]") {
  const auto snare = cs::snareRing(48000.0);
  const auto crash = cs::crashRing(48000.0);
  CHECK(snare.size() == 6240);
  CHECK(crash.size() == 14400);
  auto at = [](const std::vector<cs::XY>& hit, double t, double r0, double tau) {
    const auto i = static_cast<std::size_t>(t * 48000.0);
    const double expected = r0 * std::exp(-t / tau) * (1.0 - std::exp(-t / 0.004));
    CHECK_THAT(analysis::radius(hit[i].x, hit[i].y), WithinAbs(expected, 1e-5));
  };
  at(snare, 0.02, 0.42, 0.035);
  at(crash, 0.05, 0.80, 0.090);
  // Random phase: successive angles are uncorrelated, so the mean is near the centre.
  double mx = 0.0, my = 0.0;
  for (std::size_t i = 500; i < 1500; ++i) {
    mx += snare[i].x / analysis::radius(snare[i].x, snare[i].y);
    my += snare[i].y / analysis::radius(snare[i].x, snare[i].y);
  }
  CHECK(std::hypot(mx, my) / 1000.0 < 0.1);
}

TEST_CASE("Radial soft limit: knee 0.72, ceiling 0.86, angle kept", "[hits]") {
  std::vector<cs::XY> xy = {{0.3f, 0.4f}, {0.72f, 0.0f}, {0.6f, 0.8f}, {3.0f, -4.0f}};
  cs::radialSoftLimit(xy);
  CHECK(xy[0].x == 0.3f); // below the knee: untouched
  CHECK(xy[0].y == 0.4f);
  CHECK(xy[1].x == 0.72f);
  CHECK_THAT(analysis::radius(xy[2].x, xy[2].y), WithinAbs(0.72 + 0.14 * std::tanh(0.28 / 0.14), 1e-6));
  CHECK_THAT(xy[2].y / xy[2].x, WithinAbs(0.8 / 0.6, 1e-5));
  CHECK(analysis::radius(xy[3].x, xy[3].y) <= 0.86 + 1e-6);
  CHECK_THAT(xy[3].y / xy[3].x, WithinAbs(-4.0 / 3.0, 1e-5));
}

TEST_CASE("A slot ends once the hit falls below 12 % of its peak", "[hits]") {
  const auto kick = cs::kickSpiral(48000.0, 0.8);
  const std::size_t n = cs::slotLength(kick);
  // 0.8 e^(-t / 50 ms) reaches 12 % at 106 ms.
  CHECK_THAT(static_cast<double>(n) / 48000.0, WithinAbs(0.050 * std::log(1.0 / 0.12), 0.002));
  CHECK(cs::slotLength(std::vector<cs::XY>(100)) == 0);
}

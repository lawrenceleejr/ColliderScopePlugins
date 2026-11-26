#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

#include "cscore/cscore.h"

using Catch::Matchers::WithinAbs;

namespace {
cs::Path closedSquare() { // side 1, pen down all the way round
  return {{0, 0, false}, {1, 0, true}, {1, 1, true}, {0, 1, true}, {0, 0, true}};
}
} // namespace

TEST_CASE("Arc-length resampling spaces points evenly", "[path]") {
  std::vector<cs::XY> out(400);
  cs::resamplePath(closedSquare(), out, {0, 0});
  for (std::size_t i = 0; i < out.size(); ++i) {
    const cs::XY a = out[i], b = out[(i + 1) % out.size()];
    CHECK_THAT(std::hypot(b.x - a.x, b.y - a.y), WithinAbs(0.01, 1e-5));
  }
  CHECK_THAT(out[0].x, WithinAbs(0.0, 1e-9));
  CHECK_THAT(out[100].x, WithinAbs(1.0, 1e-6)); // the corner lands on a sample
  CHECK_THAT(out[100].y, WithinAbs(0.0, 1e-6));
}

TEST_CASE("A pen-up jump gets 0.04 of its length's time", "[path]") {
  // Two unit strokes one unit apart: 2 drawn, 2 jumped (there and back).
  const cs::Path path = cs::strokesToPath({{{0, 0}, {1, 0}}, {{1, 1}, {0, 1}}});
  REQUIRE(path.size() == 4);
  CHECK_FALSE(path[0].pen);
  CHECK(path[1].pen);
  CHECK_FALSE(path[2].pen);

  const std::size_t n = 20800; // 2 + 2 * 0.04 = 2.08 units of time
  std::vector<cs::XY> out(n);
  cs::resamplePath(path, out, {0, 0});
  std::size_t onJump = 0;
  for (const cs::XY& p : out)
    if (p.y > 1e-6f && p.y < 1.0f - 1e-6f)
      ++onJump;
  // Each jump is 0.04 / 2.08 of the period: 400 samples, less its two ends.
  CHECK(onJump >= 2 * 398);
  CHECK(onJump <= 2 * 400);
}

TEST_CASE("Strokes are ordered nearest-neighbour and reversed when that is shorter", "[path]") {
  const std::vector<cs::Stroke> strokes = {
      {{5, 0}, {6, 0}},     // far
      {{2, 0}, {1, 0}},     // near, but its far end is nearer the start: reversed
      {{2.1, 0}, {3, 0}},   // next
  };
  const auto ordered = cs::orderStrokes(strokes, {0, 0});
  REQUIRE(ordered.size() == 3);
  CHECK(ordered[0].front().x == 1.0);
  CHECK(ordered[0].back().x == 2.0);
  CHECK(ordered[1].front().x == 2.1);
  CHECK(ordered[2].front().x == 5.0);
}

TEST_CASE("Table segments share the period by fraction", "[path]") {
  const cs::Path left = {{-1, 0, false}, {-1, 1, true}};
  const cs::Path right = {{1, 0, false}, {1, 1, true}};
  const std::vector<cs::Segment> segments = {{left, 0.25}, {right, 0.75}};
  std::vector<cs::XY> table(2048);
  cs::buildTable(segments, table);
  std::size_t onLeft = 0;
  for (std::size_t i = 0; i < 512; ++i)
    onLeft += table[i].x < 0.0f || i > 500; // the tail of the slot jumps toward the next segment
  CHECK(onLeft == 512);
  CHECK(table[512].x == 1.0f);
  CHECK(table[512].y == 0.0f);

  std::vector<cs::XY> empty(64, cs::XY{1.0f, 1.0f});
  cs::buildTable({}, empty);
  for (const cs::XY& p : empty)
    CHECK((p.x == 0.0f && p.y == 0.0f));
}

TEST_CASE("Oscillator phase is exact, never rounded to whole samples", "[oscillator]") {
  // A circle table read at 880 Hz, 48 kHz: 54.5454... samples per period.
  std::vector<cs::XY> table(2048);
  for (std::size_t i = 0; i < table.size(); ++i) {
    const double a = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(table.size());
    table[i] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
  }
  cs::TableOscillator osc;
  const double step = 880.0 / 48000.0;
  int periods = 0;
  for (int n = 0; n < 48000; ++n) {
    const cs::XY p = osc.read(table);
    const double a = 2.0 * std::numbers::pi * 880.0 * n / 48000.0;
    REQUIRE_THAT(p.x, WithinAbs(std::cos(a), 1e-4));
    REQUIRE_THAT(p.y, WithinAbs(std::sin(a), 1e-4));
    periods += osc.advance(step) ? 1 : 0;
  }
  CHECK(periods == 880);
}

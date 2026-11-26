// CS Mux acceptance tests: no distortion (Interleave, Slot), latency
// (Cycle-lock), plus the tracker and the hits it relies on.
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
constexpr double kTwoPi = 6.283185307179586;

struct Signal {
  std::vector<float> x, y;
};

Signal circle(double hz, double r, std::size_t n) {
  Signal s{std::vector<float>(n), std::vector<float>(n)};
  for (std::size_t i = 0; i < n; ++i) {
    const double a = kTwoPi * hz * static_cast<double>(i) / kSampleRate;
    s.x[i] = static_cast<float>(r * std::cos(a));
    s.y[i] = static_cast<float>(r * std::sin(a));
  }
  return s;
}

// A square of half-side `half`, traced once per period at constant speed.
Signal square(double hz, double half, std::size_t n) {
  Signal s{std::vector<float>(n), std::vector<float>(n)};
  for (std::size_t i = 0; i < n; ++i) {
    double u = hz * static_cast<double>(i) / kSampleRate;
    u = (u - std::floor(u)) * 4.0;
    const int side = static_cast<int>(u);
    const double t = (u - side) * 2.0 - 1.0;
    const double px[] = {t, 1.0, -t, -1.0}, py[] = {-1.0, t, 1.0, -t};
    s.x[i] = static_cast<float>(half * px[side]);
    s.y[i] = static_cast<float>(half * py[side]);
  }
  return s;
}

Signal silence(std::size_t n) { return {std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f)}; }

struct Output {
  std::vector<float> x, y, z;
  std::vector<cs::MuxTag> tag;
};

Output run(cs::MuxEngine& engine, const Signal& a, const Signal& b, std::size_t from = 0, std::size_t to = 0) {
  if (to == 0)
    to = a.x.size();
  const std::size_t n = to - from;
  Output o{std::vector<float>(n), std::vector<float>(n), std::vector<float>(n), std::vector<cs::MuxTag>(n)};
  for (std::size_t done = 0; done < n;) { // in host-sized blocks
    const std::size_t block = std::min<std::size_t>(128, n - done);
    engine.process(a.x.data() + from + done, a.y.data() + from + done, b.x.data() + from + done, b.y.data() + from + done,
                   o.x.data() + done, o.y.data() + done, o.z.data() + done, o.tag.data() + done, static_cast<int>(block));
    done += block;
  }
  return o;
}

} // namespace

TEST_CASE("YIN tracks the period of a pitched signal", "[yin]") {
  const double hz = GENERATE(30.87, 55.0, 110.0, 220.0, 440.0, 880.0, 1760.0);
  cs::PitchTracker tracker;
  tracker.prepare(kSampleRate);
  for (int i = 0; i < 24000; ++i) { // a sine with two overtones
    const double t = kTwoPi * hz * i / kSampleRate;
    tracker.push(static_cast<float>(0.5 * std::sin(t) + 0.25 * std::sin(2 * t + 1.0) + 0.1 * std::sin(3 * t + 2.0)));
  }
  CAPTURE(hz, tracker.period());
  REQUIRE(tracker.locked());
  CHECK(std::abs(cs::cents(kSampleRate / tracker.period(), hz)) < 3.0);
  CHECK(tracker.confidence() > 0.85);
}

TEST_CASE("YIN: no lock on noise, and the last good period is held for 100 ms", "[yin]") {
  cs::PitchTracker tracker;
  tracker.prepare(kSampleRate);
  cs::Rng rng(5);
  for (int i = 0; i < 24000; ++i)
    tracker.push(static_cast<float>(rng.normal(0.0, 0.3)));
  CHECK_FALSE(tracker.locked());

  for (int i = 0; i < 24000; ++i)
    tracker.push(static_cast<float>(0.5 * std::sin(kTwoPi * 220.0 * i / kSampleRate)));
  REQUIRE(tracker.locked());
  const double period = tracker.period();
  // The signal turns to noise: analysis windows go bad over ~55 ms, then the hold runs.
  int lostAfter = -1;
  for (int i = 0; i < 24000 && lostAfter < 0; ++i) {
    tracker.push(static_cast<float>(rng.normal(0.0, 0.3)));
    if (i == 2400)
      CHECK_THAT(tracker.period(), WithinAbs(period, 1.0)); // 50 ms in: still held
    if (!tracker.locked())
      lostAfter = i;
  }
  CHECK(lostAfter > static_cast<int>(0.1 * kSampleRate));
  CHECK(lostAfter < static_cast<int>(0.25 * kSampleRate));
}

TEST_CASE("No distortion (Interleave): every A sample on the circle, every B sample on the square",
          "[mux][acceptance]") {
  const auto switchPoint = GENERATE(cs::SwitchPoint::Fixed, cs::SwitchPoint::Closest);
  const int slice = GENERATE(0, 64, 512, 4096);
  const double weight = GENERATE(0.0, 0.04);
  const std::size_t n = 96000;
  const Signal a = circle(55.0, 0.8, n), b = square(220.0, 0.3, n);
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  cs::MuxParams p;
  p.slice = slice;
  p.switchPoint = switchPoint;
  p.jumpWeight = weight;
  engine.setParams(p);
  const Output o = run(engine, a, b);

  std::size_t countA = 0, countB = 0, countJump = 0;
  double worstA = 0.0, worstB = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    if (o.tag[i] == cs::MuxTag::A) {
      ++countA;
      worstA = std::max(worstA, std::abs(analysis::radius(o.x[i], o.y[i]) - 0.8) / 0.8);
    } else if (o.tag[i] == cs::MuxTag::B) {
      ++countB;
      worstB = std::max(worstB, std::abs(std::max(std::abs(o.x[i]), std::abs(o.y[i])) - 0.3f) / 0.3);
    } else {
      ++countJump;
    }
  }
  CAPTURE(static_cast<int>(switchPoint), slice, weight, countA, countB, countJump);
  CHECK(worstA < 0.005);
  CHECK(worstB < 0.005);
  // Each picture gets about half the beam; jumps take little.
  CHECK(countA > n * 2 / 5);
  CHECK(countB > n * 2 / 5);
  CHECK(countJump < n / 20);
  if (weight == 0.0)
    CHECK(countJump == 0);
  CHECK(engine.latencySamples() == 0);
}

TEST_CASE("Interleave: Auto slice is one period of A; Balance trades time between A and B", "[mux]") {
  const std::size_t n = 96000;
  const Signal a = circle(100.0, 0.8, n), b = square(400.0, 0.3, n);
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  cs::MuxParams p;
  p.jumpWeight = 0.0;
  engine.setParams(p);
  Output o = run(engine, a, b);
  // After the tracker locks, runs of A are 480 samples long.
  std::size_t runLength = 0, last = 0;
  for (std::size_t i = n / 2; i < n; ++i) {
    if (o.tag[i] == cs::MuxTag::A)
      ++runLength;
    else if (runLength) {
      last = runLength;
      runLength = 0;
    }
  }
  CHECK(last == 480);

  p.slice = 500;
  p.balance = 0.2;
  engine.reset();
  engine.setParams(p);
  o = run(engine, a, b);
  std::size_t countA = 0;
  for (cs::MuxTag t : o.tag)
    countA += t == cs::MuxTag::A;
  CHECK_THAT(static_cast<double>(countA) / n, WithinAbs(0.7, 0.01));
}

TEST_CASE("Closest switch point shortens the jumps", "[mux]") {
  const std::size_t n = 192000;
  // A small circle beside a big one: their gap swings between 0.1 and 1.5.
  Signal a = circle(55.0, 0.8, n), b = circle(131.0, 0.25, n);
  for (float& v : b.x)
    v += 0.45f;
  auto meanJump = [&](cs::SwitchPoint sp) {
    cs::MuxEngine engine;
    engine.prepare(kSampleRate);
    cs::MuxParams p;
    p.slice = 300;
    p.switchPoint = sp;
    p.jumpWeight = 0.0;
    engine.setParams(p);
    const Output o = run(engine, a, b);
    double sum = 0.0;
    int count = 0;
    for (std::size_t i = 1; i < n; ++i) {
      if (o.tag[i] != o.tag[i - 1]) {
        sum += std::hypot(o.x[i] - o.x[i - 1], o.y[i] - o.y[i - 1]);
        ++count;
      }
    }
    REQUIRE(count > 100);
    return sum / count;
  };
  const double fixed = meanJump(cs::SwitchPoint::Fixed), closest = meanJump(cs::SwitchPoint::Closest);
  CAPTURE(fixed, closest);
  CHECK(closest < 0.8 * fixed);
}

TEST_CASE("Jumps are drawn: travel lies on the line between the pictures, and Z blanks it", "[mux]") {
  const std::size_t n = 48000;
  const Signal a = circle(55.0, 0.8, n), b = square(220.0, 0.3, n);
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  cs::MuxParams p;
  p.slice = 512;
  p.jumpWeight = 0.2;
  p.zLevel = 1.0;
  engine.setParams(p);
  const Output o = run(engine, a, b);
  std::size_t jumps = 0;
  for (std::size_t i = 1; i < n; ++i) {
    if (o.tag[i] == cs::MuxTag::Jump) {
      ++jumps;
      const double r = analysis::radius(o.x[i], o.y[i]);
      CHECK(r < 0.8 + 1e-3); // between the square and the circle, never outside
      CHECK(o.z[i] == 1.0f);
    }
    if (o.tag[i] != o.tag[i - 1])
      CHECK(o.z[i] == 1.0f); // every switch is blanked
  }
  CHECK(jumps > 100);
  // Z rests at zero while a picture is being drawn.
  std::size_t blanked = 0;
  for (float v : o.z)
    blanked += v != 0.0f;
  CHECK(blanked < n / 10);
}

TEST_CASE("No distortion (Slot): outside slots the output equals input A bit-exactly", "[mux][acceptance]") {
  const std::size_t n = 96000;
  const Signal a = circle(55.0, 0.8, n);
  Signal b = silence(n);
  // B: two short bursts.
  cs::Rng rng(2);
  for (std::size_t start : {std::size_t{20000}, std::size_t{60000}})
    for (std::size_t i = 0; i < 1500; ++i) {
      const double env = 0.5 * std::exp(-static_cast<double>(i) / 400.0);
      b.x[start + i] = static_cast<float>(env * rng.normal());
      b.y[start + i] = static_cast<float>(env * rng.normal());
    }
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  cs::MuxParams p;
  p.mode = cs::MuxMode::Slot;
  p.slotMs = 80.0;
  engine.setParams(p);

  Output o = run(engine, a, b, 0, 40000);
  engine.triggerHit(cs::HitType::Kick); // a MIDI kick at sample 40000
  const Output second = run(engine, a, b, 40000, n);
  o.x.insert(o.x.end(), second.x.begin(), second.x.end());
  o.y.insert(o.y.end(), second.y.begin(), second.y.end());
  o.tag.insert(o.tag.end(), second.tag.begin(), second.tag.end());

  std::size_t slotSamples = 0, exact = 0, starts = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (o.tag[i] == cs::MuxTag::A) {
      REQUIRE(o.x[i] == a.x[i]);
      REQUIRE(o.y[i] == a.y[i]);
      ++exact;
    } else {
      ++slotSamples;
      if (i > 0 && o.tag[i - 1] == cs::MuxTag::A)
        ++starts;
    }
  }
  CHECK(starts == 3); // two transients on B and the kick
  CHECK(exact > n * 8 / 10);
  // Slots are short: the bursts die within about 25 ms, the kick within 110 ms.
  CHECK(slotSamples < static_cast<std::size_t>(0.2 * kSampleRate));
  CHECK(o.tag[20000 + 240] == cs::MuxTag::B);
  CHECK(o.tag[40000 + 2400] == cs::MuxTag::B);
  CHECK(o.tag[40000 + 6000] == cs::MuxTag::A);
  // The kick takes over from where A was: on A's radius.
  CHECK_THAT(analysis::radius(o.x[40000 + 8], o.y[40000 + 8]), WithinAbs(0.8, 0.06));
}

TEST_CASE("Slot: a MIDI note opens a slot for B; sensitivity 0 ignores transients", "[mux]") {
  const std::size_t n = 48000;
  const Signal a = circle(55.0, 0.8, n), b = square(220.0, 0.3, n);
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  cs::MuxParams p;
  p.mode = cs::MuxMode::Slot;
  p.sensitivity = 0.0;
  p.slotMs = 50.0;
  p.jumpWeight = 0.0;
  engine.setParams(p);
  Output o = run(engine, a, b, 0, 24000);
  for (cs::MuxTag t : o.tag)
    REQUIRE(t == cs::MuxTag::A);
  engine.triggerB();
  o = run(engine, a, b, 24000, n);
  std::size_t onB = 0;
  for (cs::MuxTag t : o.tag)
    onB += t == cs::MuxTag::B;
  CHECK(onB == 2400); // B holds its level, so the slot runs its full 50 ms
  CHECK(o.tag[2400] == cs::MuxTag::A);
}

TEST_CASE("Latency (Cycle-lock): reported latency equals measured latency within 1 sample", "[mux][acceptance]") {
  const double hzA = GENERATE(100.0, 55.0, 220.0);
  const std::size_t n = 96000;
  const Signal a = circle(hzA, 0.8, n), b = square(hzA * 4.0, 0.3, n);
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  cs::MuxParams p;
  p.mode = cs::MuxMode::CycleLock;
  p.jumpWeight = 0.0;
  engine.setParams(p);
  const Output o = run(engine, a, b);
  REQUIRE(engine.cycleLocked());
  const int reported = engine.latencySamples();
  CHECK_THAT(static_cast<double>(reported), WithinAbs(kSampleRate / hzA, 1.0));
  CHECK(engine.harmonic() == 4);

  // Measure: each A slot starts with the sample A put in one period earlier.
  // Find the delay that best explains the first samples of every A slot.
  int checked = 0;
  for (std::size_t i = n / 2; i < n - 8; ++i) {
    if (o.tag[i] != cs::MuxTag::A || o.tag[i - 1] == cs::MuxTag::A)
      continue;
    int best = 0;
    double bestError = 1e9;
    for (int lag = reported - 40; lag <= reported + 40; ++lag) {
      const std::size_t j = i - static_cast<std::size_t>(lag);
      const double error = std::hypot(o.x[i] - a.x[j], o.y[i] - a.y[j]);
      if (error < bestError) {
        bestError = error;
        best = lag;
      }
    }
    CAPTURE(i, best, reported, bestError);
    REQUIRE(std::abs(best - reported) <= 1);
    REQUIRE(bestError < 0.01);
    ++checked;
  }
  CHECK(checked > 20);
}

TEST_CASE("Cycle-lock: both pictures intact, B snapped to an overtone of A", "[mux]") {
  const std::size_t n = 96000;
  // B at 410 Hz is not a harmonic of 100 Hz; it is replayed as the 4th.
  const Signal a = circle(100.0, 0.8, n), b = square(410.0, 0.3, n);
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  cs::MuxParams p;
  p.mode = cs::MuxMode::CycleLock;
  p.jumpWeight = 0.0;
  engine.setParams(p);
  const Output o = run(engine, a, b);
  REQUIRE(engine.cycleLocked());
  CHECK(engine.harmonic() == 4);
  double worstA = 0.0, worstB = 0.0;
  std::size_t countA = 0, countB = 0;
  for (std::size_t i = n / 2; i < n; ++i) {
    if (o.tag[i] == cs::MuxTag::A) {
      ++countA;
      worstA = std::max(worstA, std::abs(analysis::radius(o.x[i], o.y[i]) - 0.8) / 0.8);
    } else {
      ++countB;
      worstB = std::max(worstB, std::abs(std::max(std::abs(o.x[i]), std::abs(o.y[i])) - 0.3f) / 0.3);
    }
  }
  CHECK(worstA < 0.005);
  // The square is read between samples, which rounds its corners a little.
  CHECK(worstB < 0.06);
  CHECK_THAT(static_cast<double>(countA) / (countA + countB), WithinAbs(0.5, 0.02));
  // The whole output repeats at A's pitch.
  const double period = analysis::periodByAutocorrelation(o.x, kSampleRate, n / 2, 480.0);
  CHECK(std::abs(cs::cents(kSampleRate / period, 100.0)) < 2.0);
}

TEST_CASE("Cycle-lock falls back to Interleave when an input has no pitch", "[mux]") {
  const std::size_t n = 48000;
  const Signal a = circle(100.0, 0.8, n);
  Signal b = silence(n);
  cs::Rng rng(8);
  for (std::size_t i = 0; i < n; ++i) {
    b.x[i] = static_cast<float>(rng.normal(0.0, 0.1));
    b.y[i] = static_cast<float>(rng.normal(0.0, 0.1));
  }
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  cs::MuxParams p;
  p.mode = cs::MuxMode::CycleLock;
  p.jumpWeight = 0.0;
  engine.setParams(p);
  const Output o = run(engine, a, b);
  CHECK_FALSE(engine.cycleLocked());
  CHECK(engine.latencySamples() == 0);
  for (std::size_t i = 0; i < n; ++i) { // Interleave: live samples, undelayed
    if (o.tag[i] == cs::MuxTag::A)
      REQUIRE(o.x[i] == a.x[i]);
    else
      REQUIRE(o.x[i] == b.x[i]);
  }
}

TEST_CASE("Placement: offset, scale and rotation put the pictures side by side", "[mux]") {
  const std::size_t n = 24000;
  const Signal a = circle(55.0, 0.8, n), b = circle(220.0, 0.8, n);
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  cs::MuxParams p;
  p.slice = 256;
  p.jumpWeight = 0.0;
  p.a = {-0.5, 0.0, 0.5, 0.0};
  p.b = {0.5, 0.1, 0.25, 90.0};
  engine.setParams(p);
  const Output o = run(engine, a, b);
  for (std::size_t i = 0; i < n; ++i) {
    if (o.tag[i] == cs::MuxTag::A)
      REQUIRE_THAT(std::hypot(o.x[i] + 0.5, o.y[i]), WithinAbs(0.4, 1e-4));
    else
      REQUIRE_THAT(std::hypot(o.x[i] - 0.5, o.y[i] - 0.1), WithinAbs(0.2, 1e-4));
  }
  // Rotation by 90 degrees: B's first sample (r, 0) lands at (0, r) before the offset.
  cs::MuxEngine one;
  one.prepare(kSampleRate);
  cs::MuxParams q;
  q.mode = cs::MuxMode::Slot;
  q.sensitivity = 0.0;
  q.jumpWeight = 0.0;
  q.b = {0.0, 0.0, 1.0, 90.0};
  one.setParams(q);
  one.triggerB();
  const Output r = run(one, a, b, 0, 64);
  CHECK_THAT(r.x[0], WithinAbs(0.0, 1e-6));
  CHECK_THAT(r.y[0], WithinAbs(0.8, 1e-6));
}

TEST_CASE("Streaming hits match the offline ones and end before the beam parks", "[hits]") {
  cs::HitVoice::warmUp();
  cs::HitVoice voice;
  voice.start(cs::HitType::Kick, kSampleRate, 0.6, 0.0);
  cs::KickParams quiet;
  quiet.click = 0.0;
  const auto reference = cs::kickSpiral(kSampleRate, 0.6, 0.0, 3, quiet);
  int n = 0;
  double last = 0.0;
  while (voice.active()) {
    const cs::XY s = voice.next();
    last = analysis::radius(s.x, s.y);
    if (n > 200) // past the click
      REQUIRE_THAT(last, WithinAbs(analysis::radius(reference[static_cast<std::size_t>(n)].x, reference[static_cast<std::size_t>(n)].y), 2e-3));
    ++n;
  }
  CHECK_THAT(n / kSampleRate, WithinAbs(0.050 * std::log(1.0 / 0.12), 0.001)); // 106 ms
  CHECK(last > 0.6 * 0.11);

  for (cs::HitType type : {cs::HitType::Snare, cs::HitType::Crash}) {
    voice.start(type, kSampleRate);
    const double ring = type == cs::HitType::Snare ? 0.42 : 0.80;
    double peak = 0.0;
    n = 0;
    while (voice.active()) {
      const cs::XY s = voice.next();
      peak = std::max(peak, analysis::radius(s.x, s.y));
      ++n;
    }
    CHECK(peak < ring);
    CHECK(peak > 0.5 * ring);
    CHECK(n < static_cast<int>(0.3 * kSampleRate));
  }
}

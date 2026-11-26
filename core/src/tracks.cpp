#include "cscore/tracks.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cs {

namespace {

constexpr double kPi = std::numbers::pi;
constexpr Vec2 kAlphaSource{-0.42, -0.30};

Track makeAlpha(Rng& rng) {
  Track t;
  t.type = TrackType::Alpha;
  t.strands = 3;
  t.spread = 0.009;
  t.life = 2.0;
  const double a = rng.uniform(0.0, 2.0 * kPi);
  const double length = rng.uniform(0.30, 0.45);
  const double c = std::cos(a), s = std::sin(a);
  constexpr int n = 16;
  t.points.resize(n);
  for (int i = 0; i < n; ++i) {
    const double d = 0.04 + (length - 0.04) * i / (n - 1);
    t.points[static_cast<std::size_t>(i)] = {kAlphaSource.x + d * c, kAlphaSource.y + d * s};
  }
  for (int j = 0; j < 4; ++j) { // nuclear-recoil kink
    const double u = j / 3.0;
    Vec2& p = t.points[static_cast<std::size_t>(n - 4 + j)];
    p.x += u * u * 0.03 * -s;
    p.y += u * u * 0.03 * c;
  }
  return t;
}

Track makeMuon(Rng& rng) {
  Track t;
  t.type = TrackType::Muon;
  t.life = 2.4;
  const double a = rng.uniform(0.0, kPi);
  const Vec2 centre{rng.uniform(-0.4, 0.4), rng.uniform(-0.4, 0.4)};
  const double c = std::cos(a), s = std::sin(a);
  constexpr int n = 44;
  t.points.resize(n);
  for (int i = 0; i < n; ++i) {
    const double d = -1.3 + 2.6 * i / (n - 1);
    t.points[static_cast<std::size_t>(i)] = {centre.x + d * c, centre.y + d * s};
  }
  return t;
}

Track makeElectron(Rng& rng) {
  Track t;
  t.type = TrackType::Electron;
  t.life = 1.8;
  Vec2 p{rng.uniform(-0.5, 0.5), rng.uniform(-0.5, 0.5)};
  double heading = rng.uniform(0.0, 2.0 * kPi);
  const double turn = rng.uniform() < 0.5 ? -1.0 : 1.0;
  t.points.reserve(61);
  t.points.push_back(p);
  for (int i = 0; i < 60; ++i) { // loses energy, curls up
    heading += rng.normal(0.0, 0.22) + turn * 0.006 * i;
    p.x += 0.018 * std::cos(heading);
    p.y += 0.018 * std::sin(heading);
    t.points.push_back(p);
  }
  return t;
}

} // namespace

Track makeTrack(TrackType type, Rng& rng) {
  switch (type) {
  case TrackType::Muon:
    return makeMuon(rng);
  case TrackType::Electron:
    return makeElectron(rng);
  case TrackType::Alpha:
    break;
  }
  return makeAlpha(rng);
}

TrackType typeForVelocity(int velocity) noexcept {
  if (velocity <= 50)
    return TrackType::Electron;
  if (velocity <= 100)
    return TrackType::Alpha;
  return TrackType::Muon;
}

void ageTrack(const Track& track, double age, const AgeingParams& params, Rng& noise, std::vector<Stroke>& out) {
  const std::size_t total = track.points.size();
  if (total < 2 || age < 0.0 || age > track.life)
    return;

  // 1. Condense: the track appears along its length.
  std::size_t m = total;
  if (params.condense > 0.0) {
    const double shown = std::clamp(age / params.condense, 0.0, 1.0);
    m = std::clamp(static_cast<std::size_t>(static_cast<double>(total) * shown), std::size_t{2}, total);
  }

  // 2. Drift: convection carries it, (0.01, -0.06) per second at the default rate.
  const double scale = params.drift / 0.06;
  const Vec2 offset{0.01 * scale * age, -0.06 * scale * age};
  std::vector<Vec2> base(m), normal(m);
  for (std::size_t i = 0; i < m; ++i)
    base[i] = {track.points[i].x + offset.x, track.points[i].y + offset.y};
  for (std::size_t i = 0; i < m; ++i) {
    const Vec2 a = base[i == 0 ? 0 : i - 1], b = base[i + 1 == m ? i : i + 1];
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::hypot(dx, dy) + 1e-9;
    normal[i] = {-dy / len, dx / len};
  }

  // 3. Diffuse: fresh noise every time. 4. Clip to inside the wall.
  const double sigma = 0.002 + params.diffusionRate * age;
  const int strands = std::max(1, track.strands);
  for (int k = 0; k < strands; ++k) {
    const double o = strands > 1 ? track.spread * (2.0 * k / (strands - 1) - 1.0) : 0.0;
    Stroke s;
    s.reserve(m);
    for (std::size_t i = 0; i < m; ++i) {
      const Vec2 p{base[i].x + o * normal[i].x + noise.normal(0.0, sigma),
                   base[i].y + o * normal[i].y + noise.normal(0.0, sigma)};
      if (std::hypot(p.x, p.y) < kTrackClipRadius)
        s.push_back(p);
    }
    if (s.size() >= 2)
      out.push_back(std::move(s));
  }
}

Stroke wallStroke(int points) {
  Stroke s(static_cast<std::size_t>(points));
  for (int i = 0; i < points; ++i) {
    const double a = 2.0 * kPi * i / (points - 1);
    s[static_cast<std::size_t>(i)] = {kWallRadius * std::cos(a), kWallRadius * std::sin(a)};
  }
  s.back() = s.front(); // closed exactly
  return s;
}

HarmonicSplit harmonicSplit(int h, double target) noexcept {
  h = std::clamp(h, kMinHarmonic, kMaxHarmonic);
  HarmonicSplit best{1, 1.0 - 1.0 / h};
  double bestCost = 1e9;
  for (int m = 1; m < h; ++m) {
    const double p = 1.0 - static_cast<double>(m) / h;
    if (p < 0.3 - 1e-12 || p > 0.7 + 1e-12)
      continue;
    const double cost = std::abs(p - target);
    if (cost < bestCost - 1e-12) {
      bestCost = cost;
      best = {m, p};
    }
  }
  return best;
}

int nearestHarmonic(double noteHz, double rootHz) noexcept {
  const double h = std::round(noteHz / rootHz);
  return static_cast<int>(std::clamp(h, static_cast<double>(kMinHarmonic), static_cast<double>(kMaxHarmonic)));
}

} // namespace cs

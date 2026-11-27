#include "cscore/hits.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "cscore/rng.h"

namespace cs {

std::vector<XY> kickSpiral(double sampleRate, double r0, double a0, std::uint64_t seed, const KickParams& k) {
  Rng rng(seed);
  const auto n = static_cast<std::size_t>(k.duration * sampleRate);
  std::vector<XY> out(n);
  double phase = a0;
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    phase += 2.0 * std::numbers::pi * (k.endHz + (k.startHz - k.endHz) * std::exp(-t / k.pitchTau)) / sampleRate;
    const double r = r0 * std::exp(-t / k.radiusTau);
    const double c = k.click * r0 * std::exp(-t / k.clickTau); // beater click
    out[i] = {static_cast<float>(r * std::cos(phase) + c * rng.normal()),
              static_cast<float>(r * std::sin(phase) + c * rng.normal())};
  }
  return out;
}

std::vector<XY> noiseRing(double sampleRate, double duration, double r0, double tau, std::uint64_t seed, double grow) {
  Rng rng(seed);
  const auto n = static_cast<std::size_t>(duration * sampleRate);
  std::vector<XY> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    const double r = r0 * std::exp(-t / tau) * (1.0 - std::exp(-t / grow));
    const double a = rng.uniform(0.0, 2.0 * std::numbers::pi);
    out[i] = {static_cast<float>(r * std::cos(a)), static_cast<float>(r * std::sin(a))};
  }
  return out;
}

std::vector<XY> snareRing(double sampleRate, std::uint64_t seed) { return noiseRing(sampleRate, 0.13, 0.42, 0.035, seed); }

std::vector<XY> crashRing(double sampleRate, std::uint64_t seed) { return noiseRing(sampleRate, 0.30, 0.80, 0.090, seed); }

void radialSoftLimit(std::span<XY> xy, double knee, double ceiling) noexcept {
  for (XY& s : xy) {
    const double r = std::hypot(static_cast<double>(s.x), static_cast<double>(s.y));
    if (r > knee) {
      const double g = (knee + (ceiling - knee) * std::tanh((r - knee) / (ceiling - knee))) / r;
      s.x = static_cast<float>(s.x * g);
      s.y = static_cast<float>(s.y * g);
    }
  }
}

std::size_t slotLength(std::span<const XY> hit, double threshold) noexcept {
  double peak = 0.0;
  for (const XY& s : hit)
    peak = std::max(peak, std::hypot(static_cast<double>(s.x), static_cast<double>(s.y)));
  if (peak <= 0.0)
    return 0;
  for (std::size_t i = hit.size(); i > 0; --i)
    if (std::hypot(static_cast<double>(hit[i - 1].x), static_cast<double>(hit[i - 1].y)) >= threshold * peak)
      return i;
  return 0;
}

// ---------------------------------------------------------------- HitVoice

namespace {
constexpr std::size_t kNoiseSize = 1u << 14;
struct NoiseTable {
  std::vector<XY> unit;  // random points on the unit circle
  std::vector<XY> gauss; // standard normal pairs
  NoiseTable() : unit(kNoiseSize), gauss(kNoiseSize) {
    Rng rng(0xC0111DE5ULL);
    for (std::size_t i = 0; i < kNoiseSize; ++i) {
      const double a = rng.uniform(0.0, 2.0 * std::numbers::pi);
      unit[i] = {static_cast<float>(std::cos(a)), static_cast<float>(std::sin(a))};
      gauss[i] = {static_cast<float>(rng.normal()), static_cast<float>(rng.normal())};
    }
  }
};
const NoiseTable& noiseTable() {
  static const NoiseTable table;
  return table;
}
} // namespace

void HitVoice::warmUp() { (void)noiseTable(); }

void HitVoice::start(HitType type, double sampleRate, double r0, double a0) noexcept {
  type_ = type;
  index_ = 0;
  noise_ = (++serial_ * 2654435761u) & (kNoiseSize - 1); // a different stretch of noise each hit
  const double keep = std::log(1.0 / 0.12);             // time constants until 12 % remains
  if (type == HitType::Kick) {
    const KickParams k;
    r0_ = r0;
    radius_ = r0;
    radiusDecay_ = std::exp(-1.0 / (k.radiusTau * sampleRate));
    phase_ = a0;
    sweep_ = 2.0 * std::numbers::pi * (k.startHz - k.endHz) / sampleRate;
    sweepDecay_ = std::exp(-1.0 / (k.pitchTau * sampleRate));
    endStep_ = 2.0 * std::numbers::pi * k.endHz / sampleRate;
    click_ = k.click * r0;
    clickDecay_ = std::exp(-1.0 / (k.clickTau * sampleRate));
    left_ = static_cast<int>(std::min(k.duration, k.radiusTau * keep) * sampleRate);
  } else {
    const double radius = type == HitType::Snare ? 0.42 : 0.80;
    const double tau = type == HitType::Snare ? 0.035 : 0.090;
    const double duration = type == HitType::Snare ? 0.13 : 0.30;
    r0_ = radius;
    radius_ = radius;
    radiusDecay_ = std::exp(-1.0 / (tau * sampleRate));
    grow_ = 1.0;
    growDecay_ = std::exp(-1.0 / (0.004 * sampleRate));
    // The ring peaks near t = 10 ms; it has fallen to 12 % of that peak by about tau * (keep + 0.3).
    left_ = static_cast<int>(std::min(duration, tau * (keep + 0.3)) * sampleRate);
  }
}

XY HitVoice::next() noexcept {
  if (left_ <= 0)
    return {};
  --left_;
  const NoiseTable& noise = noiseTable();
  const std::size_t n = (noise_ + static_cast<std::uint32_t>(index_++)) & (kNoiseSize - 1);
  XY out;
  if (type_ == HitType::Kick) {
    phase_ += endStep_ + sweep_;
    out = {static_cast<float>(radius_ * std::cos(phase_) + click_ * noise.gauss[n].x),
           static_cast<float>(radius_ * std::sin(phase_) + click_ * noise.gauss[n].y)};
    sweep_ *= sweepDecay_;
    radius_ *= radiusDecay_;
    click_ *= clickDecay_;
  } else {
    const double r = radius_ * (1.0 - grow_);
    out = {static_cast<float>(r * noise.unit[n].x), static_cast<float>(r * noise.unit[n].y)};
    radius_ *= radiusDecay_;
    grow_ *= growDecay_;
  }
  radialSoftLimit(std::span<XY>(&out, 1));
  return out;
}

} // namespace cs

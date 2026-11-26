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

} // namespace cs

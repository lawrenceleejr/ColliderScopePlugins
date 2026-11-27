// Measurement helpers for acceptance tests.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace analysis {

// Normalised autocorrelation of x at `lag`, over `window` samples from `start`.
inline double autocorr(const std::vector<float>& x, std::size_t start, std::size_t window, std::size_t lag) {
  double ab = 0.0, aa = 0.0, bb = 0.0;
  for (std::size_t i = start; i < start + window; ++i) {
    const double a = x[i], b = x[i + lag];
    ab += a * b;
    aa += a * a;
    bb += b * b;
  }
  return ab / (std::sqrt(aa * bb) + 1e-30);
}

// Period of x in samples, by autocorrelation. Takes the autocorrelation peak
// within half a semitone either side of `expectedPeriod`, then refines it on
// lags of 2, 4, 8, ... periods up to about 0.1 s, with parabolic interpolation
// at each step. The long lag resolves far below one cent.
inline double periodByAutocorrelation(const std::vector<float>& x, double sampleRate, std::size_t start,
                                      double expectedPeriod) {
  const auto window = static_cast<std::size_t>(0.1 * sampleRate);
  auto peakNear = [&](std::size_t lo, std::size_t hi) {
    std::size_t peak = lo;
    double peakValue = -2.0;
    for (std::size_t lag = lo; lag <= hi; ++lag) {
      const double v = autocorr(x, start, window, lag);
      if (v > peakValue) {
        peakValue = v;
        peak = lag;
      }
    }
    const double a = autocorr(x, start, window, peak - 1), c = autocorr(x, start, window, peak + 1);
    const double denom = a - 2.0 * peakValue + c;
    return static_cast<double>(peak) + (denom != 0.0 ? 0.5 * (a - c) / denom : 0.0);
  };
  const double halfSemitone = std::exp2(0.5 / 12.0);
  double period = peakNear(static_cast<std::size_t>(std::floor(expectedPeriod / halfSemitone)),
                           static_cast<std::size_t>(std::ceil(expectedPeriod * halfSemitone)));
  for (std::size_t k = 2; static_cast<double>(k) * period <= 0.1 * sampleRate; k *= 2) {
    const auto centre = static_cast<std::size_t>(std::llround(static_cast<double>(k) * period));
    period = peakNear(centre - 3, centre + 3) / static_cast<double>(k);
  }
  return period;
}

inline double radius(float x, float y) { return std::hypot(static_cast<double>(x), static_cast<double>(y)); }

} // namespace analysis

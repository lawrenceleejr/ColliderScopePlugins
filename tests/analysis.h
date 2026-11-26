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

// Period of x in samples, by autocorrelation, independent of the expected
// pitch. Finds the shortest strong period in 30-2000 Hz, then refines it on a
// lag of many periods (about 0.1 s) with parabolic interpolation, which
// resolves far below one cent.
inline double periodByAutocorrelation(const std::vector<float>& x, double sampleRate, std::size_t start) {
  const auto window = static_cast<std::size_t>(0.1 * sampleRate);
  const auto minLag = static_cast<std::size_t>(sampleRate / 2000.0);
  const auto maxLag = static_cast<std::size_t>(sampleRate / 30.0);
  std::vector<double> r(maxLag + 2, 0.0);
  double best = -1.0;
  for (std::size_t lag = minLag; lag <= maxLag + 1; ++lag)
    best = std::max(best, r[lag] = autocorr(x, start, window, lag));
  std::size_t coarse = minLag;
  for (std::size_t lag = minLag + 1; lag <= maxLag; ++lag) {
    if (r[lag] >= 0.9 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
      coarse = lag;
      break;
    }
  }
  const auto k = std::max<std::size_t>(1, static_cast<std::size_t>(0.1 * sampleRate / static_cast<double>(coarse)));
  const std::size_t centre = k * coarse, slack = k + 2;
  std::size_t peak = centre;
  double peakValue = -2.0;
  for (std::size_t lag = centre - slack; lag <= centre + slack; ++lag) {
    const double v = autocorr(x, start, window, lag);
    if (v > peakValue) {
      peakValue = v;
      peak = lag;
    }
  }
  const double a = autocorr(x, start, window, peak - 1), c = autocorr(x, start, window, peak + 1);
  const double denom = a - 2.0 * peakValue + c;
  const double offset = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
  return (static_cast<double>(peak) + offset) / static_cast<double>(k);
}

inline double radius(float x, float y) { return std::hypot(static_cast<double>(x), static_cast<double>(y)); }

} // namespace analysis

#include "cscore/yin.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {
constexpr int kDecimate = 4;

// Sum of squared differences between x[0..n) and y[0..n).
float difference(const float* x, const float* y, int n) noexcept {
  float sum = 0.0f;
  for (int j = 0; j < n; ++j) {
    const float d = x[j] - y[j];
    sum += d * d;
  }
  return sum;
}
} // namespace

void PitchTracker::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  maxTau_ = static_cast<int>(std::ceil(sampleRate / kMinHz)) + kDecimate;
  minTau_ = std::max(2 * kDecimate, static_cast<int>(std::floor(sampleRate / kMaxHz)));
  holdHops_ = static_cast<int>(std::ceil(kHoldSeconds * sampleRate / kHop));
  const auto length = static_cast<std::size_t>(kWindow + maxTau_ + 2 * kDecimate);
  ring_.assign(length, 0.0f);
  linear_.assign(length, 0.0f);
  coarse_.assign(length / kDecimate, 0.0f);
  diff_.assign(static_cast<std::size_t>(maxTau_ / kDecimate + 2), 0.0f);
  reset();
}

void PitchTracker::reset() noexcept {
  std::fill(ring_.begin(), ring_.end(), 0.0f);
  write_ = 0;
  sinceHop_ = 0;
  hopsSinceGood_ = 1 << 20;
  locked_ = false;
  period_ = confidence_ = 0.0;
}

void PitchTracker::analyse() noexcept {
  // Oldest to newest.
  const std::size_t length = ring_.size();
  std::copy(ring_.begin() + static_cast<std::ptrdiff_t>(write_), ring_.end(), linear_.begin());
  std::copy(ring_.begin(), ring_.begin() + static_cast<std::ptrdiff_t>(write_),
            linear_.begin() + static_cast<std::ptrdiff_t>(length - write_));

  // Coarse search on a 4x-decimated copy.
  const int coarseLength = static_cast<int>(length) / kDecimate;
  for (int i = 0; i < coarseLength; ++i) {
    const float* p = linear_.data() + i * kDecimate;
    coarse_[static_cast<std::size_t>(i)] = 0.25f * (p[0] + p[1] + p[2] + p[3]);
  }
  const int window = kWindow / kDecimate;
  const int maxTau = std::min(maxTau_ / kDecimate, coarseLength - window - 1);
  const int minTau = std::max(2, minTau_ / kDecimate);
  const float* base = coarse_.data() + (coarseLength - window - maxTau);
  float running = 0.0f;
  int found = -1;
  diff_[0] = 1.0f;
  for (int tau = 1; tau <= maxTau; ++tau) {
    const float d = difference(base, base + tau, window);
    running += d;
    diff_[static_cast<std::size_t>(tau)] = running > 0.0f ? d * static_cast<float>(tau) / running : 1.0f;
  }
  for (int tau = minTau; tau <= maxTau; ++tau) {
    if (diff_[static_cast<std::size_t>(tau)] < static_cast<float>(kThreshold)) {
      while (tau + 1 <= maxTau && diff_[static_cast<std::size_t>(tau + 1)] < diff_[static_cast<std::size_t>(tau)])
        ++tau;
      found = tau;
      break;
    }
  }

  if (found < 0) {
    if (++hopsSinceGood_ > holdHops_)
      locked_ = false; // held the last good period for 100 ms; now let go
    return;
  }

  // Refine at the full rate around the coarse period.
  const int centre = found * kDecimate;
  const int lo = std::max(2, centre - kDecimate - 2), hi = std::min(maxTau_ - 1, centre + kDecimate + 2);
  const float* full = linear_.data() + (static_cast<int>(length) - kWindow - maxTau_ - 1);
  float d[2 * kDecimate + 8] = {};
  const int count = hi - lo + 3; // lo - 1 .. hi + 1
  for (int i = 0; i < count; ++i)
    d[i] = difference(full, full + (lo - 1 + i), kWindow);
  int bestIndex = 1;
  for (int i = 2; i < count - 1; ++i)
    if (d[i] < d[bestIndex])
      bestIndex = i;
  const int best = lo - 1 + bestIndex;
  const double below = d[bestIndex - 1], bestValue = d[bestIndex], above = d[bestIndex + 1];
  const double denom = below - 2.0 * bestValue + above;
  const double offset = denom > 0.0 ? std::clamp(0.5 * (below - above) / denom, -1.0, 1.0) : 0.0;
  period_ = best + offset;
  confidence_ = 1.0 - diff_[static_cast<std::size_t>(found)];
  hopsSinceGood_ = 0;
  locked_ = true;
}

} // namespace cs

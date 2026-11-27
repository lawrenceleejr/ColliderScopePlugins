#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace cs {

// xoshiro256++ seeded by splitmix64. Same stream on every platform, unlike
// the std distributions, so a seed always draws the same track.
class Rng {
public:
  explicit Rng(std::uint64_t seed = 1) noexcept { reseed(seed); }

  void reseed(std::uint64_t seed) noexcept {
    for (auto& word : s_) {
      seed += 0x9e3779b97f4a7c15ULL;
      std::uint64_t z = seed;
      z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
      z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
      word = z ^ (z >> 31);
    }
    hasSpare_ = false;
  }

  std::uint64_t next() noexcept {
    const std::uint64_t result = rotl(s_[0] + s_[3], 23) + s_[0];
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  // Uniform in [0, 1).
  double uniform() noexcept { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
  double uniform(double lo, double hi) noexcept { return lo + (hi - lo) * uniform(); }

  // Standard normal, Box-Muller.
  double normal() noexcept {
    if (hasSpare_) {
      hasSpare_ = false;
      return spare_;
    }
    const double u = 1.0 - uniform(); // (0, 1]
    const double v = uniform();
    const double r = std::sqrt(-2.0 * std::log(u));
    const double a = 2.0 * std::numbers::pi * v;
    spare_ = r * std::sin(a);
    hasSpare_ = true;
    return r * std::cos(a);
  }
  double normal(double mean, double sigma) noexcept { return mean + sigma * normal(); }

private:
  static std::uint64_t rotl(std::uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }
  std::uint64_t s_[4]{};
  double spare_ = 0.0;
  bool hasSpare_ = false;
};

} // namespace cs

#pragma once

#include <cmath>

namespace cs {

// f = 440 * 2^((n - 69) / 12) Hz. Fractional notes carry pitch bend and glide.
inline double midiToHz(double note) noexcept {
  return 440.0 * std::exp2((note - 69.0) / 12.0);
}

// Interval from reference to f in cents.
inline double cents(double f, double reference) noexcept {
  return 1200.0 * std::log2(f / reference);
}

} // namespace cs

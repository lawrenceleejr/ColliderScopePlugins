#pragma once

#include <cmath>
#include <span>

#include "cscore/path.h"

namespace cs {

// Reads a one-period picture table with a fractional phase accumulator and
// linear interpolation. The period is never rounded to whole samples, so the
// pitch is exact: at 880 Hz and 48 kHz a period is 54.5 samples, and rounding
// it to 55 would be 14 cents flat.
class TableOscillator {
public:
  void reset(double phase = 0.0) noexcept { phase_ = phase - std::floor(phase); }
  double phase() const noexcept { return phase_; }

  // Read the table at the current phase.
  XY read(std::span<const XY> table) const noexcept {
    const double pos = phase_ * static_cast<double>(table.size());
    auto i = static_cast<std::size_t>(pos);
    if (i >= table.size())
      i = table.size() - 1;
    const auto u = static_cast<float>(pos - static_cast<double>(i));
    const XY a = table[i];
    const XY b = table[i + 1 == table.size() ? 0 : i + 1];
    return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u};
  }

  // Advance by one sample at `cyclesPerSample` = f / sampleRate.
  // Returns true when a new period starts.
  bool advance(double cyclesPerSample) noexcept {
    phase_ += cyclesPerSample;
    if (phase_ >= 1.0) {
      phase_ -= std::floor(phase_);
      return true;
    }
    return false;
  }

private:
  double phase_ = 0.0;
};

} // namespace cs

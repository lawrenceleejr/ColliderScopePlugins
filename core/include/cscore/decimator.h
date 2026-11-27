// 4x decimating low-pass for the Clean quality mode: the picture is read at
// four times the sample rate, gently low-passed and decimated, so sharp
// corners alias less. Linear phase: a delay of exactly 4 output samples.
#pragma once

#include <array>
#include <cmath>
#include <numbers>

namespace cs {

class Decimator4 {
public:
  static constexpr int kFactor = 4;
  static constexpr int kTaps = 33; // odd: group delay (kTaps - 1) / 2 = 16 inputs = 4 outputs
  static constexpr int kLatency = (kTaps - 1) / 2 / kFactor;

  Decimator4() noexcept {
    // Hann-windowed sinc, cutoff 0.42 of the output rate (20 kHz at 48 kHz).
    const double cutoff = 0.42 / kFactor; // cycles per input sample
    double sum = 0.0;
    for (int k = 0; k < kTaps; ++k) {
      const double t = k - (kTaps - 1) / 2.0;
      const double sinc = t == 0.0 ? 2.0 * cutoff : std::sin(2.0 * std::numbers::pi * cutoff * t) / (std::numbers::pi * t);
      const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * (k + 1) / (kTaps + 1));
      sum += taps_[static_cast<std::size_t>(k)] = static_cast<float>(sinc * window);
    }
    for (float& h : taps_)
      h = static_cast<float>(h / sum); // unity gain at DC
    reset();
  }

  void reset() noexcept {
    history_.fill(0.0f);
    write_ = 0;
  }

  // Four input samples in, one output sample out.
  float push(const float* in) noexcept {
    for (int i = 0; i < kFactor; ++i) {
      history_[write_] = in[i];
      write_ = (write_ + 1) & kMask;
    }
    // Centre the filter on the first input of a group, 4 groups back, so the
    // delay is a whole number of output samples.
    float out = 0.0f;
    for (std::size_t k = 0; k < kTaps; ++k)
      out += taps_[k] * history_[(write_ - kFactor - k) & kMask];
    return out;
  }

private:
  static constexpr std::size_t kMask = 63;
  std::array<float, kTaps> taps_{};
  std::array<float, kMask + 1> history_{};
  std::size_t write_ = 0;
};

} // namespace cs

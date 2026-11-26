#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cs {

// Gate and amplitude envelope. Volume is size: the gain scales the picture.
// Rests are hard: with Release 0 the gate reaches exact zero within 0.5 ms,
// over a short raised-cosine ramp so the cut does not click.
class GateEnvelope {
public:
  // Shortest ramp, in seconds. Under the 0.5 ms hard-rest limit at any rate >= 22.05 kHz.
  static constexpr double kMinRamp = 0.0004;

  void setSampleRate(double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    update();
  }
  void setAttack(double seconds) noexcept {
    attack_ = seconds;
    update();
  }
  void setRelease(double seconds) noexcept {
    release_ = seconds;
    update();
  }

  void gate(bool on) noexcept { on_ = on; }
  void reset() noexcept {
    on_ = false;
    pos_ = 0.0;
  }

  bool isOn() const noexcept { return on_; }
  // Closed and fully faded: the output is exactly zero.
  bool idle() const noexcept { return !on_ && pos_ <= 0.0; }

  // Gain for the next sample, 0..1.
  float next() noexcept {
    pos_ = on_ ? std::min(1.0, pos_ + up_) : std::max(0.0, pos_ - down_);
    return static_cast<float>(0.5 - 0.5 * std::cos(std::numbers::pi * pos_));
  }

private:
  void update() noexcept {
    up_ = 1.0 / (std::max(attack_, kMinRamp) * sampleRate_);
    down_ = 1.0 / (std::max(release_, kMinRamp) * sampleRate_);
  }

  double sampleRate_ = 48000.0;
  double attack_ = 0.0, release_ = 0.0;
  double up_ = 1.0 / (kMinRamp * 48000.0), down_ = 1.0 / (kMinRamp * 48000.0);
  double pos_ = 0.0;
  bool on_ = false;
};

} // namespace cs

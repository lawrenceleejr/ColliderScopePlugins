// YIN pitch tracker for CS Mux. Fed one sample at a time on the audio
// thread; allocates only in prepare().
//
// Window 1024, hop 256, threshold 0.15, range 30-2000 Hz. To keep the cost
// low the difference function is first searched on a 4x-decimated copy,
// then refined at the full rate over the 1024-sample window.
#pragma once

#include <cstddef>
#include <vector>

namespace cs {

class PitchTracker {
public:
  static constexpr int kWindow = 1024, kHop = 256;
  static constexpr double kThreshold = 0.15, kMinHz = 30.0, kMaxHz = 2000.0, kHoldSeconds = 0.1;

  void prepare(double sampleRate);
  void reset() noexcept;

  // Feed one sample; an estimate is made every kHop samples.
  void push(float sample) noexcept {
    ring_[write_] = sample;
    write_ = write_ + 1 == ring_.size() ? 0 : write_ + 1;
    if (++sinceHop_ >= kHop) {
      sinceHop_ = 0;
      analyse();
    }
  }

  // True while a period was found within the last 100 ms.
  bool locked() const noexcept { return locked_; }
  // Period in samples (fractional). Valid while locked.
  double period() const noexcept { return period_; }
  // 1 - the normalised difference at the chosen period: 1 is a perfect repeat.
  double confidence() const noexcept { return confidence_; }

private:
  void analyse() noexcept;

  double sampleRate_ = 48000.0;
  std::vector<float> ring_, linear_, coarse_, diff_;
  std::size_t write_ = 0;
  int sinceHop_ = 0;
  int maxTau_ = 1600, minTau_ = 24;
  int hopsSinceGood_ = 1 << 20, holdHops_ = 19;
  double period_ = 0.0, confidence_ = 0.0;
  bool locked_ = false;
};

} // namespace cs

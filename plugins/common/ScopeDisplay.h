// The built-in phosphor scope: a square XY display of the plugin's own
// output. The audio thread pushes samples into a lock-free ring and never
// waits; this component drains it at 60 fps.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cstring>
#include <vector>

#include "cscore/lockfree.h"
#include "cscore/path.h"
#include "cscore/scope_view.h"

namespace cs {

// Audio-thread end of the scope. Lives in the processor.
class ScopeFeed {
public:
  // Audio thread: no allocation, no locks. Samples are dropped when the
  // display is closed or cannot keep up.
  void push(const float* x, const float* y, int n) noexcept {
    if (!open_.load(std::memory_order_relaxed))
      return;
    for (int i = 0; i < n; ++i)
      ring_.push({x[i], y[i]});
  }
  void setOpen(bool open) noexcept { open_.store(open); }
  std::size_t pop(XY* out, std::size_t max) noexcept { return ring_.pop(out, max); }

private:
  SpscRing<XY> ring_{1u << 15};
  std::atomic<bool> open_{false};
};

class ScopeDisplay final : public juce::Component, private juce::Timer {
public:
  static constexpr int kSize = 512;

  explicit ScopeDisplay(ScopeFeed& feed) : feed_(feed), view_(kSize), image_(juce::Image::ARGB, kSize, kSize, true) {
    setOpaque(true);
    chunk_.resize(8192);
    x_.resize(chunk_.size());
    y_.resize(chunk_.size());
    feed_.setOpen(true);
    startTimerHz(60);
  }
  ~ScopeDisplay() override {
    stopTimer();
    feed_.setOpen(false);
  }

  void setDecay(double decay) { view_.setDecay(decay); }

  void paint(juce::Graphics& g) override {
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.drawImage(image_, getLocalBounds().toFloat());
  }

private:
  void timerCallback() override {
    const double now = juce::Time::getMillisecondCounterHiRes() * 1e-3;
    const double dt = last_ > 0.0 ? juce::jlimit(0.0, 0.25, now - last_) : 1.0 / 60.0;
    last_ = now;
    view_.beginFrame(dt);
    for (;;) {
      const std::size_t n = feed_.pop(chunk_.data(), chunk_.size());
      if (n == 0)
        break;
      for (std::size_t i = 0; i < n; ++i) {
        x_[i] = chunk_[i].x;
        y_[i] = chunk_[i].y;
      }
      view_.addSamples(x_.data(), y_.data(), static_cast<int>(n));
    }
    view_.renderARGB(pixels_);
    {
      juce::Image::BitmapData data(image_, juce::Image::BitmapData::writeOnly);
      for (int row = 0; row < kSize; ++row)
        std::memcpy(data.getLinePointer(row), pixels_.data() + static_cast<std::size_t>(row) * kSize,
                    static_cast<std::size_t>(kSize) * sizeof(std::uint32_t));
    }
    repaint();
  }

  ScopeFeed& feed_;
  ScopeView view_;
  juce::Image image_;
  std::vector<XY> chunk_;
  std::vector<float> x_, y_;
  std::vector<std::uint32_t> pixels_;
  double last_ = 0.0;
};

} // namespace cs

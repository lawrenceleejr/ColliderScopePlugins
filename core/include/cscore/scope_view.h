// The phosphor scope: draws XY samples the way the ColliderScope video
// renderer does (pipeline_scope.py, frame_rgb with look=False).
// Brightness is beam dwell time. Used by csrender for PNGs and, from
// Phase 2, by each plugin's display.
#pragma once

#include <cstdint>
#include <vector>

namespace cs {

class ScopeView {
public:
  explicit ScopeView(int size = 512);

  int size() const noexcept { return size_; }
  void setDecay(double decay) noexcept { decay_ = decay; } // 0.3-0.9, default 0.55
  void setGraticule(bool on) noexcept { graticule_ = on; }
  void clear();

  // Start a display frame `dt` seconds after the last: the phosphor fades
  // by decay^(dt * 30).
  void beginFrame(double dt);

  // Accumulate samples (left = X, right = Y, +Y up), 6 interpolated points
  // per sample so brightness follows dwell.
  void addSamples(const float* x, const float* y, int n);

  // Tone-map the accumulated light into size x size pixels, row 0 at the top:
  // 0xAARRGGBB words (opaque), or RGB8 bytes.
  void renderARGB(std::vector<std::uint32_t>& argb) const;
  void render(std::vector<std::uint8_t>& rgb) const;

private:
  int size_;
  double decay_ = 0.55;
  bool graticule_ = true;
  bool hasLast_ = false;
  float lastX_ = 0.0f, lastY_ = 0.0f;
  std::vector<float> acc_;
  // render() scratch, kept so a display frame allocates nothing after the first
  mutable std::vector<float> img_, wide_, narrow_, glowHalf_, glow_, tmp_, pad_;
  mutable std::vector<std::uint32_t> argbScratch_, gratPixels_;
};

} // namespace cs

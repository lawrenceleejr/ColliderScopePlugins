#include "cscore/scope_view.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {

constexpr int kUpsample = 6;
constexpr double kRange = 1.02; // the display spans +-1.02 of full scale

// The video renderer uses gain 0.2 for a 1000 px scope at 30 fps. Light per
// pixel scales with 1/size for a line, and with the frame rate through the
// persistence sum, so 512 px at 60 fps needs 0.2 * (1000 / 512) * 0.87.
double gainFor(int size) { return 0.2 * (1000.0 / size) * 0.87; }

void blur(std::vector<float>& img, int size, double sigma) {
  const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
  std::vector<float> kernel(static_cast<std::size_t>(2 * radius + 1));
  float sum = 0.0f;
  for (int i = -radius; i <= radius; ++i)
    sum += kernel[static_cast<std::size_t>(i + radius)] = static_cast<float>(std::exp(-0.5 * i * i / (sigma * sigma)));
  for (float& k : kernel)
    k /= sum;

  std::vector<float> tmp(img.size());
  auto pass = [&](const std::vector<float>& src, std::vector<float>& dst, int stride, int cross) {
    for (int a = 0; a < size; ++a) {
      for (int b = 0; b < size; ++b) {
        float v = 0.0f;
        for (int i = -radius; i <= radius; ++i) {
          const int c = std::clamp(b + i, 0, size - 1); // hold the edge value
          v += kernel[static_cast<std::size_t>(i + radius)] * src[static_cast<std::size_t>(a * cross + c * stride)];
        }
        dst[static_cast<std::size_t>(a * cross + b * stride)] = v;
      }
    }
  };
  pass(img, tmp, 1, size); // along rows
  pass(tmp, img, size, 1); // along columns
}

} // namespace

ScopeView::ScopeView(int size) : size_(size), acc_(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0.0f) {}

void ScopeView::clear() {
  std::fill(acc_.begin(), acc_.end(), 0.0f);
  hasLast_ = false;
}

void ScopeView::beginFrame(double dt) {
  const auto k = static_cast<float>(std::pow(decay_, dt * 30.0));
  for (float& v : acc_)
    v *= k;
}

void ScopeView::addSamples(const float* x, const float* y, int n) {
  const double scale = size_ / (2.0 * kRange);
  auto plot = [&](double px, double py) {
    const int col = static_cast<int>(std::floor((px + kRange) * scale));
    const int row = static_cast<int>(std::floor((kRange - py) * scale));
    if (col >= 0 && col < size_ && row >= 0 && row < size_)
      acc_[static_cast<std::size_t>(row) * static_cast<std::size_t>(size_) + static_cast<std::size_t>(col)] += 1.0f;
  };
  for (int i = 0; i < n; ++i) {
    if (hasLast_) {
      for (int k = 1; k <= kUpsample; ++k) {
        const double u = static_cast<double>(k) / kUpsample;
        plot(lastX_ + (x[i] - lastX_) * u, lastY_ + (y[i] - lastY_) * u);
      }
    } else {
      plot(x[i], y[i]);
    }
    lastX_ = x[i];
    lastY_ = y[i];
    hasLast_ = true;
  }
}

void ScopeView::render(std::vector<std::uint8_t>& rgb) const {
  const std::size_t count = acc_.size();
  // Core: slight blur, then a log tone map.
  std::vector<float> img = acc_;
  blur(img, size_, 1.1);
  const auto norm = static_cast<float>(1.0 / (kUpsample * gainFor(size_)));
  const float invLog = 1.0f / std::log1p(4.0f);
  for (float& v : img)
    v = std::min(1.5f, std::log1p(v * norm) * invLog);
  // Glow: wide and narrow halos.
  std::vector<float> wide = img, narrow = img;
  blur(wide, size_, 7.0);
  blur(narrow, size_, 2.5);

  rgb.assign(count * 3, 0);
  // Graticule: 8 x 8 divisions across +-0.95 of full scale, faint.
  std::vector<float> grat(count, 0.0f);
  if (graticule_) {
    const double scale = size_ / (2.0 * kRange);
    const int lo = static_cast<int>(std::lround((kRange - 0.95) * scale));
    const int hi = static_cast<int>(std::lround((kRange + 0.95) * scale));
    for (int k = 0; k <= 8; ++k) {
      const int line = std::min(size_ - 1, static_cast<int>(std::lround(lo + (hi - lo) * k / 8.0)));
      for (int j = lo; j <= hi && j < size_; ++j) {
        grat[static_cast<std::size_t>(line) * static_cast<std::size_t>(size_) + static_cast<std::size_t>(j)] = 1.0f;
        grat[static_cast<std::size_t>(j) * static_cast<std::size_t>(size_) + static_cast<std::size_t>(line)] = 1.0f;
      }
    }
  }
  for (std::size_t i = 0; i < count; ++i) {
    const float lum = std::clamp(img[i] * 0.9f + wide[i] * 1.2f + narrow[i] * 0.7f, 0.0f, 1.0f);
    const float hot = std::clamp(lum - 0.75f, 0.0f, 1.0f) * 1.2f; // hot core -> white
    const float g = 0.055f * grat[i];
    const float r = lum * 0.35f + hot + g * 0.75f;
    const float gr = lum * 1.0f + hot + g * 0.9f;
    const float b = lum * 0.45f + hot + g * 0.8f;
    rgb[3 * i + 0] = static_cast<std::uint8_t>(std::lround(std::clamp(r, 0.0f, 1.0f) * 255.0f));
    rgb[3 * i + 1] = static_cast<std::uint8_t>(std::lround(std::clamp(gr, 0.0f, 1.0f) * 255.0f));
    rgb[3 * i + 2] = static_cast<std::uint8_t>(std::lround(std::clamp(b, 0.0f, 1.0f) * 255.0f));
  }
}

} // namespace cs

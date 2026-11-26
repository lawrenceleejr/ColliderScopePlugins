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

// Separable Gaussian blur, edges held. Loops run tap-outermost over
// contiguous rows so the compiler can vectorise them.
void blur(std::vector<float>& img, int size, double sigma, std::vector<float>& tmp, std::vector<float>& pad) {
  const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
  float kernel[64];
  float sum = 0.0f;
  for (int i = -radius; i <= radius; ++i)
    sum += kernel[i + radius] = static_cast<float>(std::exp(-0.5 * i * i / (sigma * sigma)));
  for (int i = 0; i <= 2 * radius; ++i)
    kernel[i] /= sum;

  const auto n = static_cast<std::size_t>(size);
  tmp.assign(img.size(), 0.0f);
  pad.resize(n + 2 * static_cast<std::size_t>(radius));
  for (std::size_t row = 0; row < n; ++row) { // along rows
    const float* src = img.data() + row * n;
    float* dst = tmp.data() + row * n;
    std::fill(pad.begin(), pad.begin() + radius, src[0]);
    std::copy(src, src + n, pad.begin() + radius);
    std::fill(pad.begin() + radius + size, pad.end(), src[n - 1]);
    for (int k = 0; k <= 2 * radius; ++k) {
      const float w = kernel[k];
      const float* p = pad.data() + k;
      for (std::size_t c = 0; c < n; ++c)
        dst[c] += w * p[c];
    }
  }
  std::fill(img.begin(), img.end(), 0.0f);
  for (int row = 0; row < size; ++row) { // along columns
    float* dst = img.data() + static_cast<std::size_t>(row) * n;
    for (int k = -radius; k <= radius; ++k) {
      const float w = kernel[k + radius];
      const float* src = tmp.data() + static_cast<std::size_t>(std::clamp(row + k, 0, size - 1)) * n;
      for (std::size_t c = 0; c < n; ++c)
        dst[c] += w * src[c];
    }
  }
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
  const auto n = static_cast<std::size_t>(size_);
  // Core: slight blur, then a log tone map. Most of the screen is dark.
  img_ = acc_;
  blur(img_, size_, 1.1, tmp_, pad_);
  const auto norm = static_cast<float>(1.0 / (kUpsample * gainFor(size_)));
  const float invLog = 1.0f / std::log1p(4.0f);
  for (float& v : img_)
    v = v * norm < 1e-4f ? 0.0f : std::min(1.5f, std::log1p(v * norm) * invLog);

  // Glow at half resolution, as the video renderer does: a wide halo
  // (sigma 7 px) and a narrow one (sigma 2.5 px).
  const int half = size_ / 2;
  const auto h = static_cast<std::size_t>(half);
  wide_.resize(h * h);
  for (std::size_t r = 0; r < h; ++r)
    for (std::size_t c = 0; c < h; ++c)
      wide_[r * h + c] = 0.25f * (img_[2 * r * n + 2 * c] + img_[2 * r * n + 2 * c + 1] + img_[(2 * r + 1) * n + 2 * c] +
                                  img_[(2 * r + 1) * n + 2 * c + 1]);
  narrow_ = wide_;
  blur(wide_, half, 3.5, tmp_, pad_);
  blur(narrow_, half, 1.25, tmp_, pad_);
  for (std::size_t i = 0; i < h * h; ++i)
    wide_[i] = wide_[i] * 1.2f + narrow_[i] * 0.7f;

  if (graticule_ && grat_.empty()) {
    // 8 x 8 divisions across +-0.95 of full scale, faint.
    grat_.assign(count, 0);
    const double scale = size_ / (2.0 * kRange);
    const int lo = static_cast<int>(std::lround((kRange - 0.95) * scale));
    const int hi = std::min(size_ - 1, static_cast<int>(std::lround((kRange + 0.95) * scale)));
    for (int k = 0; k <= 8; ++k) {
      const auto line = static_cast<std::size_t>(std::min(size_ - 1, static_cast<int>(std::lround(lo + (hi - lo) * k / 8.0))));
      for (auto j = static_cast<std::size_t>(lo); j <= static_cast<std::size_t>(hi); ++j)
        grat_[line * n + j] = grat_[j * n + line] = 1;
    }
  }

  rgb.resize(count * 3);
  const float maxIndex = static_cast<float>(half - 1);
  for (std::size_t r = 0; r < n; ++r) {
    // Bilinear upsample of the glow.
    const float fy = std::clamp((static_cast<float>(r) - 0.5f) * 0.5f, 0.0f, maxIndex);
    const auto y0 = static_cast<std::size_t>(fy);
    const std::size_t y1 = std::min(y0 + 1, h - 1);
    const float wy = fy - static_cast<float>(y0);
    for (std::size_t c = 0; c < n; ++c) {
      const float fx = std::clamp((static_cast<float>(c) - 0.5f) * 0.5f, 0.0f, maxIndex);
      const auto x0 = static_cast<std::size_t>(fx);
      const std::size_t x1 = std::min(x0 + 1, h - 1);
      const float wx = fx - static_cast<float>(x0);
      const float glow = (wide_[y0 * h + x0] * (1 - wx) + wide_[y0 * h + x1] * wx) * (1 - wy) +
                         (wide_[y1 * h + x0] * (1 - wx) + wide_[y1 * h + x1] * wx) * wy;
      const std::size_t i = r * n + c;
      const float lum = std::clamp(img_[i] * 0.9f + glow, 0.0f, 1.0f);
      const float hot = std::max(lum - 0.75f, 0.0f) * 1.2f; // hot core -> white
      const float g = graticule_ && grat_[i] ? 0.055f : 0.0f;
      rgb[3 * i + 0] = static_cast<std::uint8_t>(std::min(lum * 0.35f + hot + g * 0.75f, 1.0f) * 255.0f + 0.5f);
      rgb[3 * i + 1] = static_cast<std::uint8_t>(std::min(lum + hot + g * 0.9f, 1.0f) * 255.0f + 0.5f);
      rgb[3 * i + 2] = static_cast<std::uint8_t>(std::min(lum * 0.45f + hot + g * 0.8f, 1.0f) * 255.0f + 0.5f);
    }
  }
}

} // namespace cs

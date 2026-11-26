#include "cscore/scope_view.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cs {

namespace {

constexpr int kUpsample = 6;
constexpr double kRange = 1.02; // the display spans +-1.02 of full scale
constexpr std::size_t kToneSize = 4096;
constexpr std::size_t kColours = 1023;
constexpr double kToneScale = kToneSize / 10.19; // table spans x = 0 .. 5^1.5 - 1

// The video renderer uses gain 0.2 for a 1000 px scope at 30 fps. Light per
// pixel scales with 1/size for a line, and with the frame rate through the
// persistence sum, so 512 px at 60 fps needs 0.2 * (1000 / 512) * 0.87.
double gainFor(int size) { return 0.2 * (1000.0 / size) * 0.87; }

// Separable Gaussian blur, edges held. Loops run tap-outermost over
// contiguous rows so the compiler can vectorise them.
void blur(const std::vector<float>& in, std::vector<float>& img, int size, double sigma, std::vector<float>& tmp,
          std::vector<float>& pad) {
  const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
  float kernel[64];
  float sum = 0.0f;
  for (int i = -radius; i <= radius; ++i)
    sum += kernel[i + radius] = static_cast<float>(std::exp(-0.5 * i * i / (sigma * sigma)));
  for (int i = 0; i <= 2 * radius; ++i)
    kernel[i] /= sum;

  const auto n = static_cast<std::size_t>(size);
  tmp.assign(in.size(), 0.0f);
  pad.resize(n + 2 * static_cast<std::size_t>(radius));
  for (std::size_t row = 0; row < n; ++row) { // along rows
    const float* src = in.data() + row * n;
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
  img.assign(in.size(), 0.0f);
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

// 2 x 2 box downsample of an n x n image.
void halve(const std::vector<float>& src, std::size_t n, std::vector<float>& dst) {
  const std::size_t h = n / 2;
  dst.resize(h * h);
  for (std::size_t r = 0; r < h; ++r) {
    const float* a = src.data() + 2 * r * n;
    const float* b = a + n;
    float* d = dst.data() + r * h;
    for (std::size_t c = 0; c < h; ++c)
      d[c] = 0.25f * (a[2 * c] + a[2 * c + 1] + b[2 * c] + b[2 * c + 1]);
  }
}

// Bilinear 2 x upsample of an h x h image, edges held: fixed 3/4, 1/4 weights.
void doubleSize(const std::vector<float>& src, std::size_t h, std::vector<float>& dst, std::vector<float>& row) {
  const std::size_t n = 2 * h;
  dst.resize(n * n);
  row.resize(h + 2);
  for (std::size_t r = 0; r < n; ++r) {
    const std::size_t j = r / 2;
    const std::size_t other = (r & 1) ? std::min(j + 1, h - 1) : (j == 0 ? 0 : j - 1);
    const float* a = src.data() + j * h;
    const float* b = src.data() + other * h;
    for (std::size_t c = 0; c < h; ++c)
      row[c + 1] = 0.75f * a[c] + 0.25f * b[c];
    row[0] = row[1];
    row[h + 1] = row[h];
    float* d = dst.data() + r * n;
    for (std::size_t c = 0; c < h; ++c) {
      d[2 * c] = 0.25f * row[c] + 0.75f * row[c + 1];
      d[2 * c + 1] = 0.75f * row[c + 1] + 0.25f * row[c + 2];
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
  for (float& v : acc_) {
    v *= k;
    v = v < 1e-4f ? 0.0f : v; // faded out; also keeps denormals out of the blurs
  }
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

void ScopeView::renderARGB(std::vector<std::uint32_t>& argb) const {
  const std::size_t count = acc_.size();
  const auto n = static_cast<std::size_t>(size_);
  // Core: slight blur, then a log tone map. Most of the screen is dark.
  blur(acc_, img_, size_, 1.1, tmp_, pad_);
  // log1p(x) / log1p(4), clipped at 1.5 (x = 5^1.5 - 1), from a table.
  static const auto toneTable = [] {
    std::array<float, kToneSize + 2> t{};
    for (std::size_t i = 0; i < t.size(); ++i)
      t[i] = std::min(1.5f, static_cast<float>(std::log1p(static_cast<double>(i) / kToneScale) / std::log1p(4.0)));
    return t;
  }();
  const auto norm = static_cast<float>(kToneScale / (kUpsample * gainFor(size_)));
  for (float& v : img_) {
    const float scaled = v * norm;
    const float t = scaled < 1e-3f ? 0.0f : std::min(scaled, static_cast<float>(kToneSize));
    const auto i = static_cast<std::size_t>(t);
    v = toneTable[i] + (toneTable[i + 1] - toneTable[i]) * (t - static_cast<float>(i));
  }

  // Glow: a narrow halo (sigma 2.5 px) blurred at half resolution, as the
  // video renderer does, and a wide one (sigma 7 px) at quarter resolution.
  const std::size_t h = n / 2, q = n / 4;
  halve(img_, n, narrow_);
  halve(narrow_, h, wide_);
  blur(narrow_, narrow_, static_cast<int>(h), 1.25, tmp_, pad_);
  blur(wide_, wide_, static_cast<int>(q), 1.75, tmp_, pad_);
  doubleSize(wide_, q, glowHalf_, pad_);
  for (std::size_t i = 0; i < h * h; ++i)
    glowHalf_[i] = glowHalf_[i] * 1.2f + narrow_[i] * 0.7f;
  doubleSize(glowHalf_, h, glow_, pad_);

  // Colour: luminance x (0.35, 1.0, 0.45), plus white above 0.75 for a hot
  // core. A table from luminance to pixel; the graticule goes on afterwards.
  auto colour = [](float lum, float g) {
    const float hot = std::max(lum - 0.75f, 0.0f) * 1.2f;
    const auto channel = [&](float gain, float gratGain) {
      return static_cast<std::uint32_t>(std::min(lum * gain + hot + g * gratGain, 1.0f) * 255.0f + 0.5f);
    };
    return 0xff000000u | (channel(0.35f, 0.75f) << 16) | (channel(1.0f, 0.9f) << 8) | channel(0.45f, 0.8f);
  };
  static const auto colourTable = [&colour] {
    std::array<std::uint32_t, kColours + 1> t{};
    for (std::size_t i = 0; i <= kColours; ++i)
      t[i] = colour(static_cast<float>(i) / kColours, 0.0f);
    return t;
  }();

  argb.resize(count);
  const float* core = img_.data();
  const float* glow = glow_.data();
  std::uint32_t* out = argb.data();
  for (std::size_t i = 0; i < count; ++i) {
    const float lum = std::min(core[i] * 0.9f + glow[i], 1.0f);
    out[i] = colourTable[static_cast<std::size_t>(static_cast<std::int32_t>(lum * kColours + 0.5f))];
  }

  if (graticule_) {
    // 8 x 8 divisions across +-0.95 of full scale, faint: only these pixels are recoloured.
    if (gratPixels_.empty()) {
      const double scale = size_ / (2.0 * kRange);
      const int lo = static_cast<int>(std::lround((kRange - 0.95) * scale));
      const int hi = std::min(size_ - 1, static_cast<int>(std::lround((kRange + 0.95) * scale)));
      for (int k = 0; k <= 8; ++k) {
        const auto line = static_cast<std::size_t>(std::min(size_ - 1, static_cast<int>(std::lround(lo + (hi - lo) * k / 8.0))));
        for (auto j = static_cast<std::size_t>(lo); j <= static_cast<std::size_t>(hi); ++j) {
          gratPixels_.push_back(static_cast<std::uint32_t>(line * n + j));
          gratPixels_.push_back(static_cast<std::uint32_t>(j * n + line));
        }
      }
    }
    for (const std::uint32_t i : gratPixels_)
      out[i] = colour(std::min(core[i] * 0.9f + glow[i], 1.0f), 0.055f);
  }
}

void ScopeView::render(std::vector<std::uint8_t>& rgb) const {
  renderARGB(argbScratch_);
  rgb.resize(argbScratch_.size() * 3);
  for (std::size_t i = 0; i < argbScratch_.size(); ++i) {
    rgb[3 * i + 0] = static_cast<std::uint8_t>(argbScratch_[i] >> 16);
    rgb[3 * i + 1] = static_cast<std::uint8_t>(argbScratch_[i] >> 8);
    rgb[3 * i + 2] = static_cast<std::uint8_t>(argbScratch_[i]);
  }
}

} // namespace cs

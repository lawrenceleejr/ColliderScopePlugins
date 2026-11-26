#include "cscore/mux_engine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "cscore/tracks.h"

namespace cs {

namespace {
double dist(XY p, XY q) noexcept {
  return std::hypot(static_cast<double>(p.x) - q.x, static_cast<double>(p.y) - q.y);
}
double radius(XY p) noexcept { return std::hypot(static_cast<double>(p.x), static_cast<double>(p.y)); }
} // namespace

void MuxEngine::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  HitVoice::warmUp();
  trackA_.prepare(sampleRate);
  trackB_.prepare(sampleRate);
  ringA_.assign(kRing, XY{});
  ringB_.assign(kRing, XY{});
  envFastDecay_ = std::exp(-1.0 / (0.005 * sampleRate));
  envSlowCoef_ = 1.0 - std::exp(-1.0 / (0.050 * sampleRate));
  slotEnvDecay_ = std::exp(-1.0 / (0.002 * sampleRate));
  peakDecay_ = std::exp(-1.0 / (0.5 * sampleRate));
  minSlot_ = static_cast<int>(0.005 * sampleRate);
  reset();
  setParams(params_);
}

void MuxEngine::reset() noexcept {
  trackA_.reset();
  trackB_.reset();
  std::fill(ringA_.begin(), ringA_.end(), XY{});
  std::fill(ringB_.begin(), ringB_.end(), XY{});
  writeIndex_ = 0;
  lastOut_ = {};
  lastSrc_ = MuxTag::A;
  travelLeft_ = zHold_ = 0;
  onB_ = false;
  sliceCount_ = 0;
  nearestSeen_ = nearestLast_ = 1e9;
  lockActive_ = false;
  phase_ = 1.0;
  latency_ = 0;
  slotLeft_ = slotAge_ = rearm_ = 0;
  fastEnv_ = slowEnv_ = peakA_ = 0.0;
}

void MuxEngine::configure(Source& s, const Placement& p) noexcept {
  const double r = p.rotation * std::numbers::pi / 180.0;
  s.cosR = std::cos(r);
  s.sinR = std::sin(r);
  s.scale = p.scale;
  s.ox = p.x;
  s.oy = p.y;
  s.identity = p.x == 0.0 && p.y == 0.0 && p.scale == 1.0 && p.rotation == 0.0;
}

void MuxEngine::setParams(const MuxParams& params) noexcept {
  params_ = params;
  configure(a_, params.a);
  configure(b_, params.b);
}

void MuxEngine::triggerHit(HitType type) noexcept {
  if (params_.mode != MuxMode::Slot)
    return;
  // The kick starts where A is: on A's outer radius, at A's angle.
  const double r0 = peakA_ > 0.05 ? std::min(peakA_, 0.9) : 0.8;
  const double a0 = radius(a_.last) > 0.3 ? std::atan2(static_cast<double>(a_.last.y), static_cast<double>(a_.last.x)) : 0.0;
  hit_.start(type, sampleRate_, r0, a0);
  slotIsHit_ = true;
  slotLeft_ = static_cast<int>(0.4 * sampleRate_); // the hit ends itself
  slotAge_ = 0;
}

void MuxEngine::triggerB() noexcept {
  if (params_.mode != MuxMode::Slot)
    return;
  slotIsHit_ = false;
  slotLeft_ = static_cast<int>(std::clamp(params_.slotMs, 20.0, 300.0) * 1e-3 * sampleRate_);
  slotAge_ = 0;
  slotPeak_ = slotEnv_ = 0.0;
}

XY MuxEngine::readRing(const std::vector<XY>& ring, double position) const noexcept {
  const double floor = std::floor(position);
  const auto u = static_cast<float>(position - floor);
  const auto i = static_cast<std::size_t>(static_cast<std::int64_t>(floor)) & (kRing - 1);
  const XY p = ring[i], q = ring[(i + 1) & (kRing - 1)];
  return {p.x + (q.x - p.x) * u, p.y + (q.y - p.y) * u};
}

// ---------------------------------------------------------------- Interleave

void MuxEngine::interleave(XY a, XY b, XY& target, MuxTag& src) noexcept {
  int n = params_.slice;
  if (n <= 0)
    n = trackA_.locked() ? static_cast<int>(std::lround(trackA_.period())) : kDefaultSlice;
  n = std::clamp(n, kMinSlice, kMaxSlice);
  const double pA = std::clamp(0.5 + params_.balance, 0.05, 0.95);
  const int sliceA = std::max(1, static_cast<int>(std::lround(2.0 * n * pA)));
  const int slice = onB_ ? std::max(1, 2 * n - sliceA) : sliceA;

  bool flip = false;
  if (params_.switchPoint == SwitchPoint::Closest && slice >= 20) {
    // Move the switch, within +-10 % of the slice, to where the two traces
    // are nearest, so the jump line is short. Causal: switch as soon as the
    // gap is as small as the smallest seen in the previous window.
    const int open = static_cast<int>(0.9 * slice), close = static_cast<int>(1.1 * slice);
    if (sliceCount_ >= open) {
      const double gap = dist(a, b);
      nearestSeen_ = std::min(nearestSeen_, gap);
      flip = gap <= nearestLast_ * 1.05 || sliceCount_ >= close;
    }
  } else {
    flip = sliceCount_ >= slice;
  }
  if (flip) {
    onB_ = !onB_;
    sliceCount_ = 0;
    nearestLast_ = nearestSeen_;
    nearestSeen_ = 1e9;
  }
  ++sliceCount_;
  target = onB_ ? b : a;
  src = onB_ ? MuxTag::B : MuxTag::A;
}

// ----------------------------------------------------------------- CycleLock

void MuxEngine::latchCycle() noexcept {
  periodA_ = trackA_.period();
  periodB_ = trackB_.period();
  // The latest whole cycle of each, ending at the sample just written.
  const double newest = static_cast<double>(writeIndex_) + static_cast<double>(kRing) - 1.0;
  endA_ = newest;
  harmonic_ = nearestHarmonic(1.0 / periodB_, 1.0 / periodA_);
  const HarmonicSplit split = harmonicSplit(harmonic_, 0.45 + params_.balance);
  share_ = split.p;
  repeats_ = split.m;
  startB_ = newest - periodB_;
  if (params_.switchPoint == SwitchPoint::Closest) {
    // Start B's cycle at the point nearest to where A's cycle ends.
    const XY end = readRing(ringA_, endA_);
    double best = 1e9;
    const int steps = static_cast<int>(periodB_);
    for (int k = 0; k < steps; ++k) {
      const double d = dist(readRing(ringB_, newest - periodB_ - k), end);
      if (d < best) {
        best = d;
        startB_ = newest - periodB_ - k;
      }
    }
  }
  latency_ = static_cast<int>(std::lround(periodA_));
}

bool MuxEngine::cycleLock(XY& target, MuxTag& src) noexcept {
  const bool pitched = trackA_.locked() && trackB_.locked();
  if (!lockActive_) {
    if (!pitched)
      return false;
    lockActive_ = true;
    phase_ = 1.0;
  }
  if (phase_ >= 1.0) {
    if (!pitched) { // lost the pitch: back to Interleave
      lockActive_ = false;
      latency_ = 0;
      return false;
    }
    phase_ -= std::floor(phase_);
    latchCycle();
  }
  if (phase_ < share_) {
    // A's cycle, played in fraction p of the period.
    target = readRing(ringA_, endA_ - periodA_ + (phase_ / share_) * periodA_);
    src = MuxTag::A;
  } else {
    // B's cycle m times in the rest.
    double u = (phase_ - share_) / (1.0 - share_) * repeats_;
    u -= std::floor(u);
    target = readRing(ringB_, startB_ + u * periodB_);
    src = MuxTag::B;
  }
  phase_ += 1.0 / periodA_;
  return true;
}

// ---------------------------------------------------------------------- Slot

void MuxEngine::slot(XY a, XY b, XY& target, MuxTag& src) noexcept {
  // Transient trigger on B: a fast envelope jumping clear of a slow one.
  const double rb = radius(b);
  fastEnv_ = std::max(rb, fastEnv_ * envFastDecay_);
  const double slowBefore = slowEnv_;
  slowEnv_ += (rb - slowEnv_) * envSlowCoef_;
  if (rearm_ > 0)
    --rearm_;
  if (slotLeft_ <= 0 && rearm_ <= 0 && params_.sensitivity > 0.0) {
    const double s = std::clamp(params_.sensitivity, 0.0, 1.0);
    const double floorLevel = 0.2 - 0.18 * s; // 0.2 at the least sensitive, 0.02 at the most
    const double ratio = 6.0 - 4.5 * s;       // 6x .. 1.5x the recent level
    if (fastEnv_ > floorLevel && fastEnv_ > ratio * slowBefore)
      triggerB();
  }

  if (slotLeft_ > 0) {
    --slotLeft_;
    ++slotAge_;
    if (slotIsHit_) {
      target = hit_.next();
      if (!hit_.active())
        slotLeft_ = 0;
    } else {
      target = b;
      // End early once B has fallen below 12 % of its peak in this slot.
      slotEnv_ = std::max(rb, slotEnv_ * slotEnvDecay_);
      slotPeak_ = std::max(slotPeak_, slotEnv_);
      if (slotAge_ > minSlot_ && slotEnv_ < kSlotEndLevel * slotPeak_)
        slotLeft_ = 0;
    }
    src = MuxTag::B;
    if (slotLeft_ == 0)
      rearm_ = static_cast<int>(0.010 * sampleRate_);
    return;
  }
  target = a;
  src = MuxTag::A;
}

// ------------------------------------------------------------------- process

void MuxEngine::process(const float* ax, const float* ay, const float* bx, const float* by, float* x, float* y,
                        float* z, MuxTag* tags, int n) noexcept {
  const bool needPitch =
      params_.mode == MuxMode::CycleLock || (params_.mode == MuxMode::Interleave && params_.slice <= 0);
  const auto zLevel = static_cast<float>(params_.zLevel);
  for (int i = 0; i < n; ++i) {
    const float rawBx = bx ? bx[i] : 0.0f, rawBy = by ? by[i] : 0.0f;
    const XY a = a_.place(ax[i], ay[i]);
    const XY b = b_.place(rawBx, rawBy);
    peakA_ = std::max(radius(a), peakA_ * peakDecay_);
    a_.last = a;
    b_.last = b;
    if (needPitch) {
      trackA_.push(0.5f * (ax[i] + ay[i]));
      if (params_.mode == MuxMode::CycleLock)
        trackB_.push(0.5f * (rawBx + rawBy));
    }
    if (params_.mode == MuxMode::CycleLock) {
      ringA_[writeIndex_] = a;
      ringB_[writeIndex_] = b;
      writeIndex_ = (writeIndex_ + 1) & (kRing - 1);
    }

    XY target = a;
    MuxTag src = MuxTag::A;
    switch (params_.mode) {
    case MuxMode::Interleave:
      interleave(a, b, target, src);
      break;
    case MuxMode::CycleLock:
      if (!cycleLock(target, src))
        interleave(a, b, target, src);
      break;
    case MuxMode::Slot:
      slot(a, b, target, src);
      break;
    }

    // Jumps are drawn too: the beam travels to the new picture in
    // jumpWeight of the time it would take to draw that distance.
    const double step = dist(target, lastOut_);
    MuxTag tag = src;
    if (src != lastSrc_) {
      lastSrc_ = src;
      const double travel = params_.jumpWeight * step / std::max(speedSmooth_, 1e-5);
      travelLeft_ = static_cast<int>(std::min(travel, 0.002 * sampleRate_));
      zHold_ = 2;
    } else if (travelLeft_ == 0) {
      speedSmooth_ += (step - speedSmooth_) * 0.01; // how fast the beam draws, per sample
    }
    XY out = target;
    if (travelLeft_ > 0) {
      const float f = 1.0f / static_cast<float>(travelLeft_ + 1);
      out = {lastOut_.x + (target.x - lastOut_.x) * f, lastOut_.y + (target.y - lastOut_.y) * f};
      --travelLeft_;
      tag = MuxTag::Jump;
      zHold_ = 2;
    }
    lastOut_ = out;
    x[i] = out.x;
    y[i] = out.y;
    if (z) {
      z[i] = zHold_ > 0 ? zLevel : 0.0f;
    }
    if (zHold_ > 0)
      --zHold_;
    if (tags)
      tags[i] = tag;
  }
}

} // namespace cs

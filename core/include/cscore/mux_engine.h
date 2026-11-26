// CS Mux's engine: serializes two XY sources, A and B, into one beam.
// One beam, one voice: the pictures alternate in time and are never added,
// so neither is sheared by the other.
//
//   Interleave  A for a slice, then B for a slice. No latency.
//   CycleLock   Tracks each input's period and holds the latest whole cycle
//               of each. Each output period plays A's cycle in fraction p,
//               then B's cycle m times, time-compressed, in 1 - p; B rings
//               as overtone h = m / (1 - p) of A. Latency: one period of A.
//               Falls back to Interleave while either input has no pitch.
//   Slot        A plays; a trigger gives B (or a built-in drum hit) the
//               whole beam for a slot, then A resumes. No latency.
//
// Everything here runs on the audio thread: no allocation after prepare(),
// no locks, no random-number generation.
#pragma once

#include <cstdint>
#include <vector>

#include "cscore/hits.h"
#include "cscore/path.h"
#include "cscore/yin.h"

namespace cs {

enum class MuxMode { Interleave, CycleLock, Slot };
enum class SwitchPoint { Fixed, Closest };

// Where a source's picture sits: p' = rotate(p * scale) + offset.
struct Placement {
  double x = 0.0, y = 0.0;
  double scale = 1.0;
  double rotation = 0.0; // degrees, anticlockwise
};

struct MuxParams {
  MuxMode mode = MuxMode::Interleave;
  int slice = 0;                // Interleave: samples per slice, 8-4096; 0 = Auto (A's period, else 512)
  double balance = 0.0;         // -0.2..0.2: shifts p, trading brightness between A and B
  SwitchPoint switchPoint = SwitchPoint::Fixed;
  double jumpWeight = 0.04;     // travel time per unit distance, relative to drawing
  double slotMs = 110.0;        // Slot: 20-300 ms
  double sensitivity = 0.5;     // Slot: transient trigger on B, 0 = off
  Placement a, b;
  double zLevel = 1.0;          // Z-out level during jumps, -1..1
};

// What each output sample is, for tests and the Z output.
enum class MuxTag : std::uint8_t { A, B, Jump };

class MuxEngine {
public:
  static constexpr int kDefaultSlice = 512, kMinSlice = 8, kMaxSlice = 4096;
  static constexpr double kSlotEndLevel = 0.12;

  void prepare(double sampleRate);
  void reset() noexcept;
  void setParams(const MuxParams& params) noexcept;

  // Slot mode triggers: a built-in hit, or the B input.
  void triggerHit(HitType type) noexcept;
  void triggerB() noexcept;

  // n samples: A in, B in (null = silent), XY out. z (blanking pulse during
  // jumps) and tags are optional. In-place is fine: out may alias A.
  void process(const float* ax, const float* ay, const float* bx, const float* by, float* x, float* y, float* z,
               MuxTag* tags, int n) noexcept;

  // Delay from input to output, in samples: one period of A while cycle-locked, else 0.
  int latencySamples() const noexcept { return latency_; }
  bool cycleLocked() const noexcept { return lockActive_; }
  int harmonic() const noexcept { return harmonic_; }
  bool inSlot() const noexcept { return slotLeft_ > 0; }
  double periodA() const noexcept { return trackA_.locked() ? trackA_.period() : 0.0; }
  double periodB() const noexcept { return trackB_.locked() ? trackB_.period() : 0.0; }

private:
  static constexpr std::size_t kRing = 8192; // > 2 periods at 30 Hz, 96 kHz
  struct Source {
    double cosR = 1.0, sinR = 0.0, scale = 1.0, ox = 0.0, oy = 0.0;
    bool identity = true;
    XY last{};
    double speed = 0.01; // smoothed distance per sample
    XY place(float x, float y) const noexcept {
      if (identity)
        return {x, y};
      const double sx = x * scale, sy = y * scale;
      return {static_cast<float>(sx * cosR - sy * sinR + ox), static_cast<float>(sx * sinR + sy * cosR + oy)};
    }
  };

  void configure(Source& s, const Placement& p) noexcept;
  XY readRing(const std::vector<XY>& ring, double position) const noexcept;
  void interleave(XY a, XY b, XY& target, MuxTag& src) noexcept;
  bool cycleLock(XY& target, MuxTag& src) noexcept;
  void slot(XY a, XY b, XY& target, MuxTag& src) noexcept;
  void latchCycle() noexcept;

  MuxParams params_;
  double sampleRate_ = 48000.0;
  Source a_, b_;

  // output stage: jumps and Z
  XY lastOut_{};
  MuxTag lastSrc_ = MuxTag::A;
  int travelLeft_ = 0, zHold_ = 0;
  double speedSmooth_ = 0.002;

  // Interleave
  bool onB_ = false;
  int sliceCount_ = 0;
  double nearestSeen_ = 1e9, nearestLast_ = 1e9;

  // CycleLock
  PitchTracker trackA_, trackB_;
  std::vector<XY> ringA_, ringB_;
  std::size_t writeIndex_ = 0;
  bool lockActive_ = false;
  double phase_ = 1.0, periodA_ = 0.0, periodB_ = 0.0, endA_ = 0.0, startB_ = 0.0, share_ = 0.5;
  int repeats_ = 1, harmonic_ = 2, latency_ = 0;

  // Slot
  HitVoice hit_;
  bool slotIsHit_ = false;
  int slotLeft_ = 0, slotAge_ = 0, rearm_ = 0, minSlot_ = 240;
  double slotPeak_ = 0.0, slotEnv_ = 0.0;
  double fastEnv_ = 0.0, slowEnv_ = 0.0, envFastDecay_ = 0.99, envSlowCoef_ = 0.001, slotEnvDecay_ = 0.99;
  double peakA_ = 0.0, peakDecay_ = 0.9999;
};

} // namespace cs

// Drum hits drawn as pictures, each played in its own time slot so it never
// shears whatever else is on the scope. From pipeline_scope.py (kick_spiral,
// put_slot) and pipeline_batch05_cloud_song.py (noise_ring, mix).
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cscore/path.h"

namespace cs {

struct KickParams {
  double duration = 0.110;   // s
  double startHz = 185.0;
  double endHz = 48.0;
  double pitchTau = 0.028;   // s
  double radiusTau = 0.050;  // s
  double click = 0.04;       // noise click, as a fraction of r0
  double clickTau = 0.002;   // s
};

// Kick: a collapsing spiral. Quadrature sine sweep starting on a circle of
// radius r0 at angle a0, so it can take over from a wall of that radius.
std::vector<XY> kickSpiral(double sampleRate, double r0, double a0 = 0.0, std::uint64_t seed = 3,
                           const KickParams& params = {});

// Sparks on a ring: random phase every sample, radius r0 decaying with tau.
std::vector<XY> noiseRing(double sampleRate, double duration, double r0, double tau, std::uint64_t seed,
                          double grow = 0.004);

// Snare: ring of radius 0.42, tau 35 ms. Crash: radius 0.80, tau 90 ms.
std::vector<XY> snareRing(double sampleRate, std::uint64_t seed = 1);
std::vector<XY> crashRing(double sampleRate, std::uint64_t seed = 9);

// Radial soft limit for drum hits only: shrinks radius smoothly above the
// knee toward the ceiling, keeping the angle, so a hit never outgrows the wall.
void radialSoftLimit(std::span<XY> xy, double knee = 0.72, double ceiling = 0.86) noexcept;

// Samples to keep from a hit: up to the last one whose radius is still at
// least `threshold` of the peak. Ending the slot there stops the beam
// parking and burning a dot.
std::size_t slotLength(std::span<const XY> hit, double threshold = 0.12) noexcept;

// A hit generated one sample at a time, for the audio thread: no allocation,
// no random-number generation (it reads a noise table built once).
enum class HitType { Kick, Snare, Crash };

class HitVoice {
public:
  // Build the shared noise table. Call once off the audio thread.
  static void warmUp();

  // r0, a0: for a kick, the radius and angle it starts from.
  void start(HitType type, double sampleRate, double r0 = 0.8, double a0 = 0.0) noexcept;
  bool active() const noexcept { return left_ > 0; }
  // The next sample, already soft-limited. Ends once the hit has fallen
  // below 12 % of its peak, so the beam does not park.
  XY next() noexcept;

private:
  HitType type_ = HitType::Kick;
  int left_ = 0, index_ = 0;
  std::uint32_t noise_ = 0;
  double r0_ = 0.8, radius_ = 0.0, radiusDecay_ = 1.0;
  double phase_ = 0.0, sweep_ = 0.0, sweepDecay_ = 1.0, endStep_ = 0.0;
  double click_ = 0.0, clickDecay_ = 1.0;
  double grow_ = 1.0, growDecay_ = 1.0;
  std::uint32_t serial_ = 0;
};

} // namespace cs

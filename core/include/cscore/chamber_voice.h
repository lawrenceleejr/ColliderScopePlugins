// CS Chamber's voice: one note, one cloud-chamber track, drawn once per
// period at the note's pitch.
//
// Phase 1 runs it offline: the picture table is rebuilt inline at period
// boundaries. In the plugin (Phase 2) the rebuild moves to a non-audio
// thread and tables are published lock-free; render() then only reads.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "cscore/envelope.h"
#include "cscore/oscillator.h"
#include "cscore/path.h"
#include "cscore/rng.h"
#include "cscore/tracks.h"

namespace cs {

enum class TypeMode { Auto, Alpha, Muon, Electron, Random };
enum class SeedMode { Random, PerKey, Global };
enum class WallMode { Off, Shared, Drone };

struct ChamberParams {
  TypeMode type = TypeMode::Auto;
  SeedMode seedMode = SeedMode::Random;
  double diffusionRate = 0.03; // 0-0.06 per s
  double drift = 0.06;         // 0-0.2 per s
  double condenseMs = 60.0;    // 0-200 ms
  WallMode wall = WallMode::Shared;
  int wallRoot = 33;           // Drone root, MIDI note. A1 = 55 Hz
  double wallShare = 0.0;      // Drone wall share p, 0.3-0.7; 0 = Auto (nearest 0.45)
  double size = 0.9;           // 0-1
  double attackMs = 0.0;       // 0-50 ms
  double releaseMs = 0.0;      // 0-2000 ms; 0 = hard cut
  double gainDb = 0.0;         // -24 to 0 dB
  std::uint64_t seed = 1;      // base seed for all three seed modes
};

class ChamberVoice {
public:
  static constexpr std::size_t kTableSize = 2048;
  static constexpr double kMinRebuildSeconds = 0.002;

  void prepare(double sampleRate);
  void setParams(const ChamberParams& params);
  const ChamberParams& params() const noexcept { return params_; }

  void noteOn(int note, int velocity);
  void noteOff();

  // Writes n samples of X (left) and Y (right). Silence is exact zero.
  void render(float* x, float* y, int n);

  bool active() const noexcept { return active_; }
  // Rate at which the whole picture repeats: the note, or the root in Drone mode.
  double periodHz() const noexcept { return periodHz_; }
  // Overtone of the root the track rings at in Drone mode; 1 otherwise.
  int harmonic() const noexcept { return harmonic_; }
  const Track& track() const noexcept { return track_; }

private:
  void rebuild();

  ChamberParams params_;
  double sampleRate_ = 48000.0;
  std::array<XY, kTableSize> table_{};
  TableOscillator osc_;
  GateEnvelope env_;
  Track track_;
  Rng noise_{1};
  Stroke wall_ = wallStroke();
  std::vector<Stroke> strokes_;
  std::vector<Segment> segments_;
  double periodHz_ = 440.0;
  int harmonic_ = 1;
  std::uint64_t noteCount_ = 0;
  std::int64_t ageSamples_ = 0;
  std::int64_t sinceRebuild_ = 0;
  float gain_ = 0.9f;
  bool active_ = false;
};

} // namespace cs

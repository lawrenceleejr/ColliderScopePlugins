// CS Chamber's engine: a monophonic voice that draws one cloud-chamber track
// per note, once per period, at the note's pitch.
//
// Two sides, two threads:
//   audio    noteOn / noteOff / setPitchBend / setParams / process.
//            Reads a picture table with a fractional phase accumulator.
//            No allocation, no locks, no random numbers.
//   builder  service(). Births tracks, ages them, orders the strokes,
//            resamples by arc length into a 2048-point table and publishes
//            it lock-free. Runs at control rate: each period or 2 ms,
//            whichever is longer.
//
// Synchronous mode (the default) runs the builder inline from the audio
// side, sample-exact and deterministic: for offline renders and tests.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "cscore/decimator.h"
#include "cscore/envelope.h"
#include "cscore/lockfree.h"
#include "cscore/oscillator.h"
#include "cscore/path.h"
#include "cscore/rng.h"
#include "cscore/tracks.h"

namespace cs {

enum class TypeMode { Auto, Alpha, Muon, Electron, Random };
enum class SeedMode { Random, PerKey, Global };
enum class WallMode { Off, Shared, Drone };
// Scope-exact: the table read as is. Clean: 4x oversampled with a gentle low-pass.
enum class Quality { ScopeExact, Clean };

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
  double glideMs = 0.0;        // 0-500 ms, legato only
  double bendRange = 2.0;      // 0-12 semitones
  double gainDb = 0.0;         // -24 to 0 dB
  Quality quality = Quality::ScopeExact;
  int voices = 1;              // 1 = mono; 2-6 = polyphonic, voices sharing each period
  std::uint64_t seed = 1;      // base seed for all three seed modes
};

class ChamberEngine {
public:
  static constexpr std::size_t kTableSize = 2048;
  static constexpr double kMinRebuildSeconds = 0.002;

  ChamberEngine();

  // Setup, not real-time safe.
  void prepare(double sampleRate);
  void setSynchronous(bool synchronous) noexcept { synchronous_.store(synchronous); }

  // Audio thread.
  void setParams(const ChamberParams& params) noexcept;
  void noteOn(int note, int velocity) noexcept;
  void noteOff(int note) noexcept;
  void allNotesOff() noexcept;
  void setPitchBend(double amount) noexcept; // -1..1 of the bend range
  // Writes n samples of X (left) and Y (right). Silence is exact zero.
  void process(float* x, float* y, int n) noexcept;
  bool active() const noexcept { return active_; }
  // Delay through the engine in samples: 4 in Clean quality, else 0.
  int latencySamples() const noexcept { return params_.quality == Quality::Clean ? Decimator4::kLatency : 0; }

  // Builder thread: do any pending work. Returns true if a table was built.
  bool service();

  // Rate at which the whole picture repeats: the note, or the root in Drone mode.
  double periodHz() const noexcept { return step_ * sampleRate_; }
  // Overtone of the root the track rings at in Drone mode; 1 otherwise.
  int harmonic() const noexcept;
  // The current track. Builder state: read it only in synchronous mode.
  const Track& track() const noexcept { return track_; }
  // The plan for the current chord (two or more keys). Builder state, as track().
  const ChordPlan& chordPlan() const noexcept { return chordPlan_; }

private:
  struct Frame {
    std::array<XY, kTableSize> table{};
    std::uint32_t serial = 0;
    double periodHz = 0.0; // polyphonic: the fundamental the builder chose
  };
  struct Held {
    std::int32_t note = 0, velocity = 0;
    std::uint32_t birth = 0;      // identifies this key press
    std::int64_t birthClock = 0;  // when it was pressed, in samples
  };
  struct BuilderInput {
    ChamberParams params;
    std::uint32_t serial = 0;
    std::int32_t note = 69;
    std::int32_t velocity = 80;
    std::int32_t voiceCount = 0; // polyphonic: the keys held, oldest first
    std::array<Held, kMaxVoices> voices{};
  };
  struct PolyVoice {
    std::uint32_t birth = 0;
    Track track;
  };

  // audio side
  void startNote(int note, int velocity) noexcept;
  void applyPending() noexcept;
  void takeFrame() noexcept;
  void updateStep() noexcept;
  void publishInput() noexcept;
  void serviceInline() noexcept;
  bool poly() const noexcept { return params_.voices > 1; }
  void startChord() noexcept;
  void newPeriod(bool sync) noexcept;
  // builder side
  bool serviceLocked();
  void build(const BuilderInput& in, double age);
  void buildChord(const BuilderInput& in);
  Track birthTrack(const ChamberParams& params, int note, int velocity, std::uint32_t serial, std::uint64_t& seed) const;

  // --- audio thread state ---
  ChamberParams params_;
  double sampleRate_ = 48000.0;
  TableOscillator osc_;
  GateEnvelope env_;
  Decimator4 cleanX_, cleanY_;
  std::array<Held, 16> held_{};
  int heldCount_ = 0;
  int note_ = 69, velocity_ = 80; // the note sounding or about to
  bool pending_ = false;
  bool pendingLegato_ = false;
  std::uint32_t pendingSerial_ = 0;
  std::uint32_t serial_ = 0;
  std::uint32_t serialCounter_ = 0;
  std::uint32_t birthCounter_ = 0;
  double polyHz_ = 0.0;
  BuilderInput lastInput_;
  double pitch_ = 69.0, pitchTarget_ = 69.0, glideStep_ = 0.0;
  std::int64_t glideLeft_ = 0;
  double bend_ = 0.0;
  double step_ = 440.0 / 48000.0;
  std::int64_t clock_ = 0;
  std::int64_t sinceAdopt_ = 0;
  std::int64_t minRebuild_ = 96;
  bool awaitingBuild_ = false;
  float gain_ = 0.9f;
  bool active_ = false;

  // --- shared ---
  LatestValue<BuilderInput> input_;
  TripleBuffer<Frame> frames_;
  std::atomic<std::int64_t> clockShared_{0}, startClock_{0};
  std::atomic<std::uint32_t> startSerial_{0};
  std::atomic<std::uint32_t> buildRequest_{0};
  std::atomic<bool> synchronous_{true};
  std::atomic_flag servicing_ = ATOMIC_FLAG_INIT;

  // --- builder thread state ---
  double builderSampleRate_ = 48000.0;
  std::uint32_t builtSerial_ = 0;
  std::uint32_t servedRequest_ = 0;
  Track track_;
  Rng noise_{1};
  Stroke wall_ = wallStroke();
  std::vector<Stroke> strokes_;
  std::vector<Segment> segments_;
  std::array<PolyVoice, kMaxVoices> polyVoices_{};
  ChordPlan chordPlan_;
};

} // namespace cs

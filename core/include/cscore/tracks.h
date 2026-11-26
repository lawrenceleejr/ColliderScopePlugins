// Cloud-chamber tracks: generators, ageing, the chamber wall and the
// harmonic lock. Units: chamber radius = 1. Numbers follow the Python
// reference (pipeline_batch05_cloud_song.py).
#pragma once

#include <array>
#include <span>
#include <vector>

#include "cscore/path.h"
#include "cscore/rng.h"

namespace cs {

inline constexpr double kWallRadius = 0.9;
inline constexpr double kTrackClipRadius = 0.87; // vapour only inside the wall

enum class TrackType { Alpha, Muon, Electron };

struct Track {
  TrackType type = TrackType::Alpha;
  std::vector<Vec2> points; // the track as born, before ageing
  int strands = 1;
  double spread = 0.0; // strands sit at offsets in [-spread, +spread] across the track
  double life = 2.0;   // seconds
};

// Alpha: straight, length 0.30-0.45, from the source at (-0.42, -0.30),
// 16 points, recoil kink on the last 4; 3 strands. Life 2.0 s.
// Muon: straight across the chamber, length 2.6, 44 points. Life 2.4 s.
// Electron: random walk, 60 steps of 0.018, curling one way. Life 1.8 s.
Track makeTrack(TrackType type, Rng& rng);

// Velocity <= 50 electron, 51-100 alpha, > 100 muon.
TrackType typeForVelocity(int velocity) noexcept;

struct AgeingParams {
  double diffusionRate = 0.03; // per s
  double drift = 0.06;         // per s
  double condense = 0.06;      // s
};

// The track at `age` seconds, appended to `out` as strokes:
// condense along its length, drift, diffuse with fresh noise (the hiss),
// clip to inside the wall. Nothing is appended once age exceeds the life.
void ageTrack(const Track& track, double age, const AgeingParams& params, Rng& noise, std::vector<Stroke>& out);

// The chamber wall: a closed circle of radius 0.9 starting at angle 0.
Stroke wallStroke(int points = 193);

// Drone mode: the wall takes fraction p of the period and the track is drawn
// m times in the rest, so it rings as overtone h = m / (1 - p).
struct HarmonicSplit {
  int m = 1;
  double p = 0.5;
};
inline constexpr int kMinHarmonic = 2;
inline constexpr int kMaxHarmonic = 26;

// m and p for overtone h with p nearest `target` within 0.3-0.7.
HarmonicSplit harmonicSplit(int h, double target = 0.45) noexcept;

// Nearest overtone of `rootHz` to `noteHz`, within the range the split allows.
int nearestHarmonic(double noteHz, double rootHz) noexcept;

// Polyphony on one beam. The picture still repeats once per period, at a
// shared fundamental f0 = base / k. Voice i is drawn m[i] times, each pass
// lasting 1 / h[i] of the period, so it rings as overtone h[i] of f0: its
// note snapped to the nearest one. What is left of the period goes to the
// wall (when there is one) or to one more, slower pass of the lowest voice.
inline constexpr int kMaxVoices = 6;

struct ChordPlan {
  double f0 = 0.0;   // Hz: the rate at which the whole picture repeats
  int k = 1;         // base / f0
  int count = 0;
  std::array<int, kMaxVoices> h{}; // overtone of f0 per voice
  std::array<int, kMaxVoices> m{}; // passes per period per voice
  double wall = 0.0;   // share of the period for the wall
  double filler = 0.0; // share left over when there is no wall
  double maxErrorCents = 0.0; // worst snap, in cents
};

// hz: the voices' note frequencies. baseHz: the lowest note, or the wall's
// root in Drone mode. wallMin: the least share the wall may get (0 = no
// wall); wallTarget: the share to aim for.
ChordPlan planChord(std::span<const double> hz, double baseHz, double wallMin, double wallTarget) noexcept;

} // namespace cs

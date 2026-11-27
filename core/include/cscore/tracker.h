// The CMS tracker instrument: a module of the CMS Phase-2 Outer Tracker, as
// a schematic in four stages.
//
//   1 Module    the module, assembled, turning slowly
//   2 Exploded  its layers lifted apart: sensors, spacers, hybrids
//   3 Tracker   put back together and flown into its place in the outer
//               tracker: six barrel layers and five endcap discs a side
//   4 Tracks    collisions: tracks curl out through the tracker in the
//               3.8 T field, leaving a stub (a pair of hits, one in each
//               sensor of a module) where they cross a layer
//
// Auto plays the stages in order over each note; or one stage can be held.
// The geometry is simplified: radii and lengths are close to the real
// detector's, module counts and the tilted barrel rings are not.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cscore/mono_engine.h"
#include "cscore/path.h"
#include "cscore/view3d.h"

namespace cs {

enum class TrackerStage { Auto, Module, Exploded, Tracker, Tracks };
enum class ModuleType { TwoS, PS }; // 2S: two strip sensors. PS: a macro-pixel sensor under a strip sensor.

struct TrackerParams {
  VoiceParams voice;
  TrackerStage stage = TrackerStage::Auto;
  ModuleType module = ModuleType::TwoS;
  double speed = 1.0;  // 0.1-4: how fast the stages play
  double detail = 0.6; // 0-1: strips, chips, staves and discs drawn
  Camera camera{30.0, 20.0, 0.0, 0.35};
  double spin = 12.0;  // -180..180 degrees per second
  int seedMode = 0;    // 0 random, 1 per key, 2 global
  std::uint64_t seed = 1;
};

// The real detector, in metres: barrel layer radii and half-length, endcap
// disc positions and radii.
inline constexpr double kBarrelRadii[6] = {0.23, 0.36, 0.51, 0.68, 0.88, 1.08};
inline constexpr double kOtBarrelHalfLength = 1.15;
inline constexpr double kDiscZ[5] = {1.3, 1.55, 1.85, 2.2, 2.6};
inline constexpr double kDiscInner = 0.23, kDiscOuter = 1.08;

struct TrackerFrame {
  std::vector<Stroke> strokes;
  int stubs = 0;           // stubs drawn (stage 4)
  double explode = 0.0;    // 0 assembled .. 1 fully apart
  double zoom = 0.0;       // 0 the module filling the screen .. 1 in place in the tracker
  TrackerStage stage = TrackerStage::Module; // the stage shown
};

// The picture at `age` seconds into a note, fitted within radius 0.9.
TrackerFrame trackerFrame(const TrackerParams& params, double age, double energy, std::uint64_t seed);

class TrackerSource {
public:
  using Params = TrackerParams;
  static double rebuildSeconds(const Params&) noexcept { return 1.0 / 50.0; }

  void prepare(double sampleRate) { sheet_.prepare(sampleRate); }
  void birth(const Params& params, int note, int velocity, std::uint32_t serial);
  void build(const Params& params, int note, double age, std::span<XY> table);
  const TrackerFrame& frame() const noexcept { return frame_; }

private:
  std::uint64_t seed_ = 1;
  double energy_ = 0.6;
  TrackerFrame frame_;
  SheetRenderer sheet_;
  std::vector<FillPolygon> fills_;
};

using TrackerEngine = MonoEngine<TrackerSource>;

} // namespace cs

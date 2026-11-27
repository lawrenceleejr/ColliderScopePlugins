// The Scenes instrument: pictures from around particle physics, one per
// scene. Each note draws the chosen scene; velocity is the "energy" (how
// busy the scene is), the seed decides the random parts.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cscore/expr.h"
#include "cscore/mono_engine.h"
#include "cscore/path.h"
#include "cscore/rng.h"
#include "cscore/view3d.h"

namespace cs {

enum class Scene {
  TrackingPlanes, // a track crossing six silicon planes, hits first, then the fitted track (3D)
  WireChamber,    // drift circles round the struck wires, tangent to the track
  Shower,         // an electromagnetic shower branching through a calorimeter block
  Cherenkov,      // rings of Cherenkov light opening on a plane of photosensors (3D)
  SparkChamber,   // sparks jumping between plates along the track
  Betatron,       // a beam wobbling round the ring, and its phase-space ellipse
  Cyclotron,      // a particle spiralling out between the dees, then extracted
  BunchCrossing,  // two trains of bunches passing through each other
  FluxTube,       // two quarks pulled apart until the string snaps into two pairs
  HiggsPotential, // the Mexican hat, with a ball rolling round its valley (3D)
  Neutrino,       // the three flavour probabilities along the baseline
  EightfoldWay,   // the baryon octet or decuplet
};
inline constexpr int kSceneCount = 12;
const char* sceneName(Scene scene) noexcept;

enum class SceneSeed { Random, PerKey, Global };

struct GalleryParams {
  VoiceParams voice;
  Scene scene = Scene::TrackingPlanes;
  SceneSeed seedMode = SceneSeed::Random;
  double formMs = 300.0; // 0-2000 ms: how long the scene takes to build up at the start of a note
  double speed = 1.0;    // 0-4: how fast it moves; 0 holds it still
  double detail = 0.5;   // 0-1: frames, grids, labels and the like
  Camera camera{30.0, 25.0, 0.0, 0.3}; // the 3D scenes only
  double spin = 15.0;    // -180..180 degrees per second, the 3D scenes only
  std::uint64_t seed = 1;
};

// Three-flavour oscillation in vacuum, no CP phase: the chance that a
// neutrino born as flavour `from` (0 e, 1 mu, 2 tau) is seen as `to` after
// travelling L km with energy E GeV, as a function of L/E.
double neutrinoProbability(int from, int to, double kmPerGeV) noexcept;

// The scene as strokes, fitted within radius 0.9. Exposed for tests and offline use.
std::vector<Stroke> sceneStrokes(const GalleryParams& params, double age, double energy, std::uint64_t seed);

// What a scene fills in when drawn as a sheet: its closed shapes, dimly, or
// for the Higgs potential the surface itself, shaded.
std::vector<FillPolygon> sceneFills(const GalleryParams& params, double age, const std::vector<Stroke>& strokes);

class GallerySource {
public:
  using Params = GalleryParams;
  // The Higgs potential is a mesh of wires, the most to build: redrawn less often.
  static double rebuildSeconds(const Params& p) noexcept { return p.scene == Scene::HiggsPotential ? 1.0 / 30.0 : 1.0 / 60.0; }

  void prepare(double sampleRate) { sheet_.prepare(sampleRate); }
  void birth(const Params& params, int note, int velocity, std::uint32_t serial);
  void build(const Params& params, int note, double age, std::span<XY> table);

private:
  std::uint64_t seed_ = 1;
  double energy_ = 0.6;
  SheetRenderer sheet_;
  std::vector<FillPolygon> fills_;
};

using GalleryEngine = MonoEngine<GallerySource>;

} // namespace cs

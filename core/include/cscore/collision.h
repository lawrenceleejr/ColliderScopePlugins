// The Collision instrument: LHC-style event displays, seen along the beam.
// Charged particles leave the collision point and curve in the solenoid's
// field, tighter the less momentum they carry; jets are sprays of them, with
// energy piled up in the calorimeter behind; muons run straight out through
// everything.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cscore/mono_engine.h"
#include "cscore/path.h"
#include "cscore/rng.h"

namespace cs {

enum class CollisionEvent { Auto, MinimumBias, Dijet, ZToMuons, Multijet, FourMuons, Random };
enum class CollisionSeed { Random, PerKey, Global };

struct CollisionParams {
  VoiceParams voice;
  CollisionEvent event = CollisionEvent::Auto;
  CollisionSeed seedMode = CollisionSeed::Random;
  double field = 0.5;    // 0-1: the solenoid's strength; higher curls the tracks more
  double pileup = 0.3;   // 0-1: extra soft tracks from other collisions in the same crossing
  bool detector = true;  // draw the calorimeter's outer ring
  double formMs = 60.0;  // 0-300 ms: time for the tracks to fly out from the collision point
  std::uint64_t seed = 1;
};

struct CollisionTrack {
  double pt = 1.0;  // transverse momentum, arbitrary units: about 0.1 (curls up) to 20 (straight)
  double phi = 0.0; // direction at the collision point, radians
  int charge = 1;
  bool muon = false; // reaches the muon chambers
};
struct CollisionTower {
  double phi = 0.0, energy = 0.1; // energy as a length, 0-1 of the calorimeter's depth
  bool hadronic = false;          // outer layer; else the inner, electromagnetic one
};

// Radii of the detector, in picture units.
inline constexpr double kTrackerRadius = 0.52, kEcalRadius = 0.56, kHcalRadius = 0.68, kCalorimeterEdge = 0.80,
                        kMuonRadius = 0.88;

// The path of one track from the collision point: an arc of a circle through
// the origin, out to the tracker's edge (or to the muon chambers). A track
// with too little momentum to get out curls round once and stops.
Stroke collisionTrackPath(const CollisionTrack& track, double field);

class CollisionSource {
public:
  using Params = CollisionParams;
  static double rebuildSeconds(const Params&) noexcept { return 1.0 / 120.0; }

  void prepare(double) {}
  void birth(const Params& params, int note, int velocity, std::uint32_t serial);
  void build(const Params& params, int note, double age, std::span<XY> table);

  const std::vector<CollisionTrack>& tracks() const noexcept { return tracks_; }
  const std::vector<CollisionTower>& towers() const noexcept { return towers_; }
  CollisionEvent event() const noexcept { return event_; }

private:
  void addJet(Rng& rng, double phi, double energy);
  void addSoft(Rng& rng, int count);

  std::vector<CollisionTrack> tracks_;
  std::vector<CollisionTower> towers_;
  CollisionEvent event_ = CollisionEvent::Dijet;
  std::vector<Stroke> strokes_;
  HarmonicGrid grid_;
};

using CollisionEngine = MonoEngine<CollisionSource>;

} // namespace cs

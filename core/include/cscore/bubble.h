// CS Bubble: a bubble chamber. Charged particles cross a magnetic field,
// losing energy as they go, so their tracks curl into spirals; each track
// is a row of bubbles that grow while the note is held.
//
// The beam dwells on a young bubble (a bright dot) and circles an older one,
// so a note starts as a dotted line with a stepped, buzzy tone and fizzes
// more as the bubbles swell.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cscore/mono_engine.h"
#include "cscore/path.h"
#include "cscore/rng.h"

namespace cs {

enum class BubbleType { Auto, Electron, Pair, Decay, Beam, Random };
enum class BubbleSeed { Random, PerKey, Global };

struct BubbleParams {
  VoiceParams voice;
  BubbleType type = BubbleType::Auto;
  BubbleSeed seedMode = BubbleSeed::Random;
  double field = 0.5;      // 0-1: how hard the magnetic field bends the tracks
  int bubbles = 90;        // 20-200: bubbles along the picture
  double growth = 0.02;    // 0-0.1 per s: how fast a bubble's radius grows
  double maxRadius = 0.03; // 0.005-0.06: where it stops
  double boil = 0.15;      // 0-1: how much the bubbles shiver (the hiss)
  double formMs = 40.0;    // 0-200 ms: time for the track to appear along its length
  std::uint64_t seed = 1;
};

// One particle's path, before it is broken into bubbles.
struct BubbleTrack {
  std::vector<Vec2> points; // evenly spaced along the path
  int charge = 1;
};

// A particle of momentum `momentum` (arbitrary units, about 0.2-3) and the
// given charge, started at `from` heading `heading` (radians), curling in a
// field of strength `field` and losing energy as it goes. Ends when it
// stops or leaves the chamber (radius 0.9).
BubbleTrack curlTrack(Vec2 from, double heading, double momentum, int charge, double field, double loss);

class BubbleSource {
public:
  using Params = BubbleParams;
  static constexpr double kChamberRadius = 0.9;
  static double rebuildSeconds(const Params&) noexcept { return 0.002; }

  void prepare(double sampleRate) { sheet_.prepare(sampleRate); }
  void birth(const Params& params, int note, int velocity, std::uint32_t serial);
  void build(const Params& params, int note, double age, std::span<XY> table);

  // The tracks of the current event (one, or two for a pair or a decay).
  const std::vector<BubbleTrack>& tracks() const noexcept { return tracks_; }
  BubbleType type() const noexcept { return type_; }

private:
  std::vector<BubbleTrack> tracks_;
  BubbleType type_ = BubbleType::Electron;
  double builtField_ = -1.0;
  std::uint64_t seed_ = 1;
  int velocity_ = 80;
  Rng noise_{1};
  std::vector<float> bubbleScale_; // each bubble's own size, fixed at birth
  std::vector<Stroke> strokes_;
  HarmonicGrid grid_;
  void makeTracks(const Params& params);
  SheetRenderer sheet_;
  std::vector<FillPolygon> fills_;
};

using BubbleEngine = MonoEngine<BubbleSource>;

} // namespace cs

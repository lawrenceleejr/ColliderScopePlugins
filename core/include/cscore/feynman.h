// The Feynman instrument: Feynman diagrams. Time runs left to right.
// Fermions are straight lines with an arrow, photons and W bosons are wavy,
// gluons are curly, the Higgs is dashed, and vertices are dots.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "cscore/mono_engine.h"
#include "cscore/path.h"

namespace cs {

enum class FeynmanLine { Fermion, Photon, Gluon, Scalar };

struct FeynmanEdge {
  int from = 0, to = 0; // vertices
  FeynmanLine kind = FeynmanLine::Fermion;
  double bulge = 0.0; // 0 = straight; else the arc's height as a fraction of its length, to the left
};

struct FeynmanDiagram {
  const char* name;
  std::vector<Vec2> vertices;
  std::vector<FeynmanEdge> edges;
  int external = 0; // the first `external` vertices are the loose ends, which get no dot
};

// The diagrams it knows, simplest first.
const std::vector<FeynmanDiagram>& feynmanDiagrams();

struct FeynmanParams {
  VoiceParams voice;
  int diagram = 0;        // 0 = Auto (velocity picks how complicated, the key which one); 1.. = feynmanDiagrams()
  double formMs = 80.0;   // 0-500 ms: the diagram is drawn from left to right, as time runs
  double wiggle = 1.0;    // 0.3-3: how tightly the photons wave and the gluons curl
  double motion = 1.0;    // -4..4 cycles per second: the waves run along their lines
  bool arrows = true;     // arrows on the fermion lines
};

// One line of a diagram as strokes. `phase` (cycles) shifts the wave along a
// photon or gluon; `upTo` (an x position) cuts the picture off for drawing it in.
void feynmanStrokes(Vec2 a, Vec2 b, const FeynmanEdge& edge, const FeynmanParams& params, double phase,
                    std::vector<Stroke>& out);

class FeynmanSource {
public:
  using Params = FeynmanParams;
  static double rebuildSeconds(const Params&) noexcept { return 1.0 / 60.0; }

  void prepare(double sampleRate) { sheet_.prepare(sampleRate); }
  void birth(const Params& params, int note, int velocity, std::uint32_t serial);
  void build(const Params& params, int note, double age, std::span<XY> table);

  // The diagram a note draws when Diagram is Auto.
  static int autoDiagram(int note, int velocity) noexcept;
  int chosen() const noexcept { return chosen_; }

private:
  int chosen_ = 1;
  std::vector<Stroke> strokes_;
  HarmonicGrid grid_;
  SheetRenderer sheet_;
  std::vector<FillPolygon> fills_;
};

using FeynmanEngine = MonoEngine<FeynmanSource>;

} // namespace cs

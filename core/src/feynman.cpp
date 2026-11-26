#include "cscore/feynman.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cs {

namespace {
constexpr double kPi = std::numbers::pi;
using L = FeynmanLine;
} // namespace

const std::vector<FeynmanDiagram>& feynmanDiagrams() {
  // Vertices in picture units; loose ends first.
  static const std::vector<FeynmanDiagram> list = {
      // e+ e- annihilate to a photon, which makes a muon pair.
      {"Annihilation",
       {{-0.8, 0.5}, {-0.8, -0.5}, {0.8, 0.5}, {0.8, -0.5}, {-0.3, 0.0}, {0.3, 0.0}},
       {{0, 4, L::Fermion}, {4, 1, L::Fermion}, {4, 5, L::Photon}, {3, 5, L::Fermion}, {5, 2, L::Fermion}},
       4},
      // Two electrons scatter by exchanging a photon.
      {"Exchange",
       {{-0.8, 0.5}, {0.8, 0.5}, {-0.8, -0.5}, {0.8, -0.5}, {0.0, 0.38}, {0.0, -0.38}},
       {{0, 4, L::Fermion}, {4, 1, L::Fermion}, {2, 5, L::Fermion}, {5, 3, L::Fermion}, {4, 5, L::Photon}},
       4},
      // A photon scatters off an electron.
      {"Compton",
       {{-0.8, -0.35}, {-0.8, 0.5}, {0.8, -0.35}, {0.8, 0.5}, {-0.3, -0.35}, {0.3, -0.35}},
       {{0, 4, L::Fermion}, {4, 5, L::Fermion}, {5, 2, L::Fermion}, {1, 4, L::Photon}, {5, 3, L::Photon}},
       4},
      // A quark pair annihilates to a gluon, which splits into two.
      {"Three gluons",
       {{-0.8, 0.5}, {-0.8, -0.5}, {0.8, 0.5}, {0.8, -0.5}, {-0.3, 0.0}, {0.25, 0.0}},
       {{0, 4, L::Fermion}, {4, 1, L::Fermion}, {4, 5, L::Gluon}, {5, 2, L::Gluon}, {5, 3, L::Gluon}},
       4},
      // An electron emits a photon and takes it back.
      {"Self-energy",
       {{-0.8, -0.1}, {0.8, -0.1}, {-0.4, -0.1}, {0.4, -0.1}},
       {{0, 2, L::Fermion}, {2, 3, L::Fermion}, {3, 1, L::Fermion}, {2, 3, L::Photon, -0.55}},
       2},
      // Beta decay: a down quark turns into an up quark and a W, which gives an electron and a neutrino.
      {"Beta decay",
       {{-0.8, -0.35}, {0.8, -0.55}, {0.8, 0.55}, {0.8, 0.05}, {-0.15, -0.3}, {0.3, 0.25}},
       {{0, 4, L::Fermion}, {4, 1, L::Fermion}, {4, 5, L::Photon}, {5, 2, L::Fermion}, {3, 5, L::Fermion}},
       4},
      // A photon turns briefly into an electron pair.
      {"Vacuum polarisation",
       {{-0.8, 0.0}, {0.8, 0.0}, {-0.3, 0.0}, {0.3, 0.0}},
       {{0, 2, L::Photon}, {2, 3, L::Fermion, -0.5}, {3, 2, L::Fermion, -0.5}, {3, 1, L::Photon}},
       2},
      // Two photons exchanged: the box.
      {"Box",
       {{-0.8, 0.45}, {0.8, 0.45}, {-0.8, -0.45}, {0.8, -0.45}, {-0.3, 0.4}, {0.3, 0.4}, {-0.3, -0.4}, {0.3, -0.4}},
       {{0, 4, L::Fermion}, {4, 5, L::Fermion}, {5, 1, L::Fermion}, {2, 6, L::Fermion}, {6, 7, L::Fermion},
        {7, 3, L::Fermion}, {4, 6, L::Photon}, {5, 7, L::Photon}},
       4},
      // Two gluons fuse through a loop of top quarks to make a Higgs.
      {"Gluon fusion",
       {{-0.8, 0.5}, {-0.8, -0.5}, {0.8, 0.0}, {-0.25, 0.32}, {-0.25, -0.32}, {0.2, 0.0}},
       {{0, 3, L::Gluon}, {1, 4, L::Gluon}, {3, 5, L::Fermion}, {5, 4, L::Fermion}, {4, 3, L::Fermion}, {5, 2, L::Scalar}},
       3},
      // A Higgs decays to two photons through a loop.
      {"Higgs to photons",
       {{-0.8, 0.0}, {0.8, 0.5}, {0.8, -0.5}, {-0.2, 0.0}, {0.25, 0.32}, {0.25, -0.32}},
       {{0, 3, L::Scalar}, {3, 4, L::Fermion}, {4, 5, L::Fermion}, {5, 3, L::Fermion}, {4, 1, L::Photon}, {5, 2, L::Photon}},
       3},
  };
  return list;
}

void feynmanStrokes(Vec2 a, Vec2 b, const FeynmanEdge& edge, const FeynmanParams& params, double phase,
                    std::vector<Stroke>& out) {
  const double dx = b.x - a.x, dy = b.y - a.y, chord = std::hypot(dx, dy);
  if (chord <= 0.0)
    return;
  // The line itself: straight, or an arc through a point `bulge` to the left of the middle.
  const Vec2 control{0.5 * (a.x + b.x) - 2.0 * edge.bulge * dy, 0.5 * (a.y + b.y) + 2.0 * edge.bulge * dx};
  auto at = [&](double u) {
    const double v = 1.0 - u;
    return Vec2{v * v * a.x + 2.0 * v * u * control.x + u * u * b.x, v * v * a.y + 2.0 * v * u * control.y + u * u * b.y};
  };
  auto tangent = [&](double u) {
    const double tx = 2.0 * (1.0 - u) * (control.x - a.x) + 2.0 * u * (b.x - control.x);
    const double ty = 2.0 * (1.0 - u) * (control.y - a.y) + 2.0 * u * (b.y - control.y);
    const double len = std::hypot(tx, ty) + 1e-12;
    return Vec2{tx / len, ty / len};
  };
  const double length = chord * (1.0 + 2.0 * edge.bulge * edge.bulge); // near enough for counting waves
  const double wiggle = std::clamp(params.wiggle, 0.2, 4.0);

  switch (edge.kind) {
  case L::Fermion: {
    Stroke line;
    const int steps = edge.bulge == 0.0 ? 1 : 24;
    for (int i = 0; i <= steps; ++i)
      line.push_back(at(static_cast<double>(i) / steps));
    out.push_back(std::move(line));
    if (params.arrows) { // a chevron at the middle, pointing the way the particle goes
      const Vec2 p = at(0.5), t = tangent(0.5);
      constexpr double size = 0.045;
      const Vec2 back{p.x - size * t.x, p.y - size * t.y};
      out.push_back({{back.x - 0.6 * size * t.y, back.y + 0.6 * size * t.x},
                     {p.x + 0.4 * size * t.x, p.y + 0.4 * size * t.y},
                     {back.x + 0.6 * size * t.y, back.y - 0.6 * size * t.x}});
    }
    break;
  }
  case L::Photon: {
    // A whole number of waves, dying away at the ends so it meets its vertices.
    const int waves = std::max(2, static_cast<int>(std::lround(length / 0.11 * wiggle)));
    const int steps = waves * 12;
    Stroke line;
    for (int i = 0; i <= steps; ++i) {
      const double u = static_cast<double>(i) / steps;
      const Vec2 p = at(u), t = tangent(u);
      const double edgeFade = std::min(1.0, 6.0 * std::min(u, 1.0 - u));
      const double off = 0.032 * edgeFade * std::sin(2.0 * kPi * (waves * u - phase));
      line.push_back({p.x - off * t.y, p.y + off * t.x});
    }
    out.push_back(std::move(line));
    break;
  }
  case L::Gluon: {
    // A coil: the beam circles as it advances.
    const int turns = std::max(3, static_cast<int>(std::lround(length / 0.075 * wiggle)));
    const int steps = turns * 14;
    constexpr double radius = 0.04;
    Stroke line;
    for (int i = 0; i <= steps; ++i) {
      const double u = static_cast<double>(i) / steps;
      const Vec2 p = at(u), t = tangent(u);
      const double angle = 2.0 * kPi * (turns * u - phase);
      const double edgeFade = std::min(1.0, 8.0 * std::min(u, 1.0 - u));
      const double along = -radius * edgeFade * std::sin(angle), across = radius * edgeFade * (1.0 - std::cos(angle)) - radius * edgeFade;
      line.push_back({p.x + along * t.x - across * t.y, p.y + along * t.y + across * t.x});
    }
    out.push_back(std::move(line));
    break;
  }
  case L::Scalar: {
    // Dashes.
    const int dashes = std::max(3, static_cast<int>(std::lround(length / 0.1)));
    for (int i = 0; i < dashes; ++i) {
      const double u0 = (i + 0.2) / dashes, u1 = (i + 0.8) / dashes;
      out.push_back({at(u0), at(u1)});
    }
    break;
  }
  }
}

int FeynmanSource::autoDiagram(int note, int velocity) noexcept {
  // Soft notes draw tree diagrams, harder ones loops, the hardest the Higgs.
  static const int tree[] = {1, 2, 3}, more[] = {4, 5, 6}, loops[] = {7, 8}, higgs[] = {9, 10};
  if (velocity <= 40)
    return tree[note % 3];
  if (velocity <= 80)
    return more[note % 3];
  if (velocity <= 110)
    return loops[note % 2];
  return higgs[note % 2];
}

void FeynmanSource::birth(const Params& params, int note, int velocity, std::uint32_t) {
  const int count = static_cast<int>(feynmanDiagrams().size());
  chosen_ = params.diagram >= 1 && params.diagram <= count ? params.diagram : autoDiagram(note, velocity);
  grid_.reset();
}

void FeynmanSource::build(const Params& params, int, double age, std::span<XY> table) {
  const int count = static_cast<int>(feynmanDiagrams().size());
  if (params.diagram >= 1 && params.diagram <= count && params.diagram != chosen_) {
    chosen_ = params.diagram; // a fixed choice follows the control
    grid_.reset();
  }
  const FeynmanDiagram& diagram = feynmanDiagrams()[static_cast<std::size_t>(chosen_ - 1)];
  const double phase = params.motion * age;
  strokes_.clear();
  for (const FeynmanEdge& edge : diagram.edges)
    feynmanStrokes(diagram.vertices[static_cast<std::size_t>(edge.from)], diagram.vertices[static_cast<std::size_t>(edge.to)], edge,
                   params, phase, strokes_);
  for (std::size_t i = static_cast<std::size_t>(diagram.external); i < diagram.vertices.size(); ++i)
    strokes_.push_back({diagram.vertices[i], diagram.vertices[i]}); // a vertex: the beam dwells, a dot

  // Time runs left to right: the diagram is drawn in over the form time.
  const bool formed = params.formMs <= 0.0 || age >= params.formMs * 1e-3;
  if (!formed) {
    const double limit = -0.85 + 1.7 * age / (params.formMs * 1e-3);
    std::vector<Stroke> shown;
    for (const Stroke& s : strokes_) {
      Stroke run;
      for (const Vec2& p : s) {
        if (p.x <= limit) {
          run.push_back(p);
        } else {
          if (run.size() >= 2)
            shown.push_back(run);
          run.clear();
        }
      }
      if (run.size() >= 2)
        shown.push_back(std::move(run));
    }
    strokes_ = std::move(shown);
  }
  if (strokes_.empty()) {
    std::fill(table.begin(), table.end(), XY{});
    return;
  }
  if (params.voice.timing == Timing::Harmonic) {
    grid_.setSolveOrder(formed); // solved once the whole diagram is there
    grid_.build(strokes_, table);
    return;
  }
  const Path path = strokesToPath(orderStrokes(strokes_, {}));
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs

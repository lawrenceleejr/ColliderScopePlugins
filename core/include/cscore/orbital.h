// CS Orbital: surfaces. Each note draws a hydrogen orbital as a wireframe
// of its isosurface, the surface where |psi| has one value, spinning slowly.
//
// psi_nlm(r, theta, phi) = R_nl(r) * Y_lm(theta, phi), in real form
// (cos m phi), with lengths in Bohr radii. Each radial lobe of R gives one
// shell: an s orbital is nested spheres, a p orbital two lobes, and so on.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "cscore/mono_engine.h"
#include "cscore/path.h"

namespace cs {

struct OrbitalParams {
  VoiceParams voice;
  int orbital = 0;       // 0 = Auto; 1-20 = 1s, 2s, 2pz, 2px, 3s ... 4f (see kOrbitals)
  double spin = 30.0;    // -180..180 degrees per second about the orbital's axis
  double tilt = 25.0;    // 0-90 degrees: the axis leans toward the viewer
  int lines = 8;         // 4-16: wires around the surface
  double level = 0.25;   // 0.05-0.8 of the lobe's peak: lower draws a fatter surface
  double bloomMs = 80.0; // 0-500 ms: the surface grows out of the lobe's core
};

struct OrbitalId {
  int n, l, m;
  const char* name;
};
inline constexpr std::array<OrbitalId, 20> kOrbitals = {{
    {1, 0, 0, "1s"},
    {2, 0, 0, "2s"},
    {2, 1, 0, "2pz"},
    {2, 1, 1, "2px"},
    {3, 0, 0, "3s"},
    {3, 1, 0, "3pz"},
    {3, 1, 1, "3px"},
    {3, 2, 0, "3dz2"},
    {3, 2, 1, "3dxz"},
    {3, 2, 2, "3dx2-y2"},
    {4, 0, 0, "4s"},
    {4, 1, 0, "4pz"},
    {4, 1, 1, "4px"},
    {4, 2, 0, "4dz2"},
    {4, 2, 1, "4dxz"},
    {4, 2, 2, "4dx2-y2"},
    {4, 3, 0, "4fz3"},
    {4, 3, 1, "4fxz2"},
    {4, 3, 2, "4fz(x2-y2)"},
    {4, 3, 3, "4fx(x2-3y2)"},
}};

// Unnormalised hydrogen radial function R_nl(r), r in Bohr radii.
double hydrogenRadial(int n, int l, double r) noexcept;
// Associated Legendre function P_l^m(x), m >= 0.
double legendre(int l, int m, double x) noexcept;

// One orbital's isosurfaces, ready to be asked for a radius in any direction.
class OrbitalShape {
public:
  void set(int n, int l, int m);
  int n() const noexcept { return n_; }
  int l() const noexcept { return l_; }
  int m() const noexcept { return m_; }
  // Radial lobes: n - l of them, innermost first.
  int shells() const noexcept { return static_cast<int>(lobes_.size()); }

  // Radius of shell `shell` in direction (theta, phi) at iso level `level`
  // (a fraction of that lobe's peak), scaled so the outermost shell just
  // reaches 1 where the angular part is largest. 0 where there is no
  // surface in that direction (near an angular node).
  // `scaleLevel`, if given, is the level whose outer shell is the unit of
  // length: a surface drawn at a higher level than that is smaller.
  double radius(int shell, double level, double theta, double phi, double scaleLevel = 0.0) const noexcept;

private:
  struct Lobe {
    std::size_t peak = 0, end = 0; // grid indices: |R| falls from peak to end
    double peakValue = 0.0;
  };
  double crossing(const Lobe& lobe, double value) const noexcept;

  int n_ = 0, l_ = 0, m_ = 0;
  double dr_ = 0.05, angularPeak_ = 1.0;
  std::vector<double> radial_; // |R| on a grid
  std::vector<Lobe> lobes_;
};

class OrbitalSource {
public:
  using Params = OrbitalParams;
  static constexpr double kSize = 0.9; // the outer shell's reach in the picture
  static double rebuildSeconds(const Params&) noexcept { return 1.0 / 60.0; } // a wireframe costs more to build than a track

  void prepare(double sampleRate) { sheet_.prepare(sampleRate); }
  void birth(const Params& params, int note, int velocity, std::uint32_t serial);
  void build(const Params& params, int note, double age, std::span<XY> table);

  const OrbitalShape& shape() const noexcept { return shape_; }
  // The orbital a note draws when Orbital is Auto: velocity picks s, p, d
  // or f, the key picks the orientation.
  static int autoOrbital(int note, int velocity) noexcept;

private:
  OrbitalShape shape_;
  int chosen_ = 1;
  std::vector<Stroke> strokes_, ordered_;
  HarmonicGrid grid_;
  SheetRenderer sheet_;
  std::vector<FillPolygon> fills_;
};

using OrbitalEngine = MonoEngine<OrbitalSource>;

} // namespace cs

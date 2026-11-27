// The Function instrument: z = f(x, y), drawn as a wire mesh over a square
// of the xy plane, seen in 3D. Lines behind the surface are hidden.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cscore/expr.h"
#include "cscore/mono_engine.h"
#include "cscore/path.h"
#include "cscore/pitch.h"
#include "cscore/sheet.h"
#include "cscore/view3d.h"

namespace cs {

struct SurfaceParams {
  VoiceParams voice;
  std::array<char, 160> function{}; // the expression, zero-terminated
  double range = 3.0;     // 0.5-12: x and y run from -range to +range
  double height = 0.6;    // 0-2: how tall one unit of f is drawn
  int lines = 14;         // 4-32: wires each way
  bool hidden = true;     // hide the lines behind the surface
  Camera camera{35.0, 30.0, 0.0, 0.2};
  double spin = 15.0;     // -180..180 degrees per second about the z axis

  // Style, scanlines, shading and interlace are in voice.sheet: a Sheet fills
  // the surface in, shaded scanlines with the far side hidden.

  void setFunction(std::string_view source) noexcept {
    function.fill(0);
    for (std::size_t i = 0; i < source.size() && i + 1 < function.size(); ++i)
      function[i] = source[i];
  }
};

// The wire mesh of f over [-1, 1]^2 (f is asked for at range * (x, y)),
// as screen strokes. Exposed for tests and offline use.
std::vector<Stroke> surfaceStrokes(const Expression& f, const SurfaceParams& params, double t, double spinDegrees);

// The filled sheet: scanlines `offset`, `offset + stride`, ... of `lines`, as
// one path with per-point dwell weights. Exposed for tests and offline use.
Path surfaceSheet(const Expression& f, const SurfaceParams& params, double t, double spinDegrees, int lines, int offset = 0,
                  int stride = 1);

// The surface as filled quads, n by n, each with its depth and shade: for
// instruments that draw a function as one part of a picture.
std::vector<FillPolygon> surfacePolygons(const Expression& f, const SurfaceParams& params, double t, double spinDegrees, int n);

// Where a point (x, y, z), in the function's own units, lands on the screen
// in the picture surfaceStrokes() draws with the same parameters and spin.
Vec2 surfaceScreenPoint(const SurfaceParams& params, double x, double y, double z, double spinDegrees);

class SurfaceSource {
public:
  using Params = SurfaceParams;
  // A mesh of wires is the most to build: redraw a dense one less often.
  static double rebuildSeconds(const Params& p) noexcept { return p.lines > 16 ? 1.0 / 24.0 : 1.0 / 40.0; }

  void prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    sheet_.prepare(sampleRate);
  }
  void birth(const Params& params, int note, int velocity, std::uint32_t serial);
  void build(const Params& params, int note, double age, std::span<XY> table);

  // Scanlines a sheet uses for a note: as many as asked for, but no more
  // than a period of the note has room to draw (about 14 samples a line).
  int sheetLines(const Params& params, int note) const noexcept;

  const Expression& expression() const noexcept { return expression_; }
  const std::string& error() const noexcept { return error_; }

private:
  void compile(const Params& params);
  std::string compiled_ = "\x01"; // nothing compiled yet
  Expression expression_;
  std::string error_;
  double sampleRate_ = 48000.0;
  std::uint32_t frame_ = 0;
  SheetRenderer sheet_;
};

using SurfaceEngine = MonoEngine<SurfaceSource>;

} // namespace cs

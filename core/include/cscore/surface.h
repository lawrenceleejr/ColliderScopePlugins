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

  void setFunction(std::string_view source) noexcept {
    function.fill(0);
    for (std::size_t i = 0; i < source.size() && i + 1 < function.size(); ++i)
      function[i] = source[i];
  }
};

// The wire mesh of f over [-1, 1]^2 (f is asked for at range * (x, y)),
// as screen strokes. Exposed for tests and offline use.
std::vector<Stroke> surfaceStrokes(const Expression& f, const SurfaceParams& params, double t, double spinDegrees);

class SurfaceSource {
public:
  using Params = SurfaceParams;
  static double rebuildSeconds(const Params&) noexcept { return 1.0 / 60.0; }

  void prepare(double) {}
  void birth(const Params& params, int note, int velocity, std::uint32_t serial);
  void build(const Params& params, int note, double age, std::span<XY> table);

  const Expression& expression() const noexcept { return expression_; }
  const std::string& error() const noexcept { return error_; }

private:
  void compile(const Params& params);
  std::string compiled_ = "\x01"; // nothing compiled yet
  Expression expression_;
  std::string error_;
};

using SurfaceEngine = MonoEngine<SurfaceSource>;

} // namespace cs

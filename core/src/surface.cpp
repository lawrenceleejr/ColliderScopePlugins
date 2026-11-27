#include "cscore/surface.h"

#include <algorithm>
#include <cmath>

namespace cs {

namespace {

constexpr int kSamples = 72;   // points along each wire
constexpr int kColumns = 384;  // screen columns for the hidden-line test
constexpr double kExtent = 0.54; // half-width of the base square: its far corners reach 0.935
constexpr double kScreen = 1.0; // columns span x in [-kScreen, kScreen]

struct Wire {
  std::vector<Vec3> points; // projected: x, y on screen, z = depth
  double depth = 0.0;       // mean depth, for front-to-back order
};

// The floating horizon: wires are drawn front to back; a point is visible
// if it rises above everything drawn so far in its column, or drops below it.
void drawWires(std::vector<Wire>& wires, bool hidden, std::vector<Stroke>& out) {
  std::sort(wires.begin(), wires.end(), [](const Wire& a, const Wire& b) { return a.depth > b.depth; });
  std::vector<double> upper(kColumns, -1e9), lower(kColumns, 1e9), newUpper, newLower;
  auto column = [](double x) { return std::clamp(static_cast<int>((x + kScreen) / (2.0 * kScreen) * kColumns), 0, kColumns - 1); };
  for (const Wire& wire : wires) {
    if (!hidden) {
      Stroke s;
      for (const Vec3& p : wire.points)
        s.push_back({p.x, p.y});
      out.push_back(std::move(s));
      continue;
    }
    newUpper = upper;
    newLower = lower;
    Stroke run;
    auto flush = [&] {
      if (run.size() >= 2)
        out.push_back(run);
      run.clear();
    };
    for (std::size_t i = 0; i + 1 < wire.points.size(); ++i) {
      const Vec3 a = wire.points[i], b = wire.points[i + 1];
      // Step no more than a column at a time.
      const int steps = std::max(1, std::abs(column(b.x) - column(a.x)) + 1);
      for (int k = (i == 0 ? 0 : 1); k <= steps; ++k) {
        const double u = static_cast<double>(k) / steps;
        const double x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u;
        const int c = column(x);
        if (y >= upper[static_cast<std::size_t>(c)] || y <= lower[static_cast<std::size_t>(c)])
          run.push_back({x, y});
        else
          flush();
        newUpper[static_cast<std::size_t>(c)] = std::max(newUpper[static_cast<std::size_t>(c)], y);
        newLower[static_cast<std::size_t>(c)] = std::min(newLower[static_cast<std::size_t>(c)], y);
      }
    }
    flush();
    upper.swap(newUpper); // a wire does not hide itself
    lower.swap(newLower);
  }
}

} // namespace

std::vector<Stroke> surfaceStrokes(const Expression& f, const SurfaceParams& params, double t, double spinDegrees) {
  Camera camera = params.camera;
  camera.azimuth += spinDegrees;
  const View view(camera, kExtent * std::sqrt(3.0));
  const int lines = std::clamp(params.lines, 2, 48);
  const double range = params.range, height = params.height;

  auto point = [&](double u, double v) { // u, v in [-1, 1]
    const double z = std::clamp(f.valid() ? f.eval(u * range, v * range, t) * height : 0.0, -1.0, 1.0);
    return view.project({u * kExtent, v * kExtent, z * kExtent});
  };
  std::vector<Stroke> out;
  for (int family = 0; family < 2; ++family) { // wires of constant y, then of constant x
    std::vector<Wire> wires(static_cast<std::size_t>(lines));
    for (int j = 0; j < lines; ++j) {
      const double fixed = lines > 1 ? -1.0 + 2.0 * j / (lines - 1) : 0.0;
      Wire& wire = wires[static_cast<std::size_t>(j)];
      wire.points.resize(kSamples + 1);
      for (int i = 0; i <= kSamples; ++i) {
        const double along = -1.0 + 2.0 * i / kSamples;
        wire.points[static_cast<std::size_t>(i)] = family == 0 ? point(along, fixed) : point(fixed, along);
      }
      // Order by where the wire sits on the base, not by its height.
      wire.depth = family == 0 ? view.project({0.0, fixed, 0.0}).z : view.project({fixed, 0.0, 0.0}).z;
    }
    drawWires(wires, params.hidden, out);
  }
  return out;
}

Vec2 surfaceScreenPoint(const SurfaceParams& params, double x, double y, double z, double spinDegrees) {
  Camera camera = params.camera;
  camera.azimuth += spinDegrees;
  const View view(camera, kExtent * std::sqrt(3.0));
  const double zz = std::clamp(z * params.height, -1.0, 1.0);
  const Vec3 q = view.project({x / params.range * kExtent, y / params.range * kExtent, zz * kExtent});
  return {q.x, q.y};
}

void SurfaceSource::compile(const Params& params) {
  const std::string source(params.function.data());
  if (source == compiled_)
    return;
  compiled_ = source;
  error_.clear();
  if (!expression_.compile(source, &error_))
    expression_.compile("0"); // a flat sheet, and the error is kept for the display
}

void SurfaceSource::birth(const Params& params, int, int, std::uint32_t) { compile(params); }

void SurfaceSource::build(const Params& params, int, double age, std::span<XY> table) {
  compile(params); // the function can be edited while a note sounds
  std::vector<Stroke> strokes = surfaceStrokes(expression_, params, age, params.spin * age);
  // The picture changes from frame to frame as it turns, so time is spread
  // evenly along it rather than solved for harmony.
  const Path path = strokesToPath(orderStrokes(std::move(strokes), {}));
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs

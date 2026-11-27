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

Path surfaceSheet(const Expression& f, const SurfaceParams& params, double t, double spinDegrees, int lines, int offset,
                  int stride) {
  Camera camera = params.camera;
  camera.azimuth += spinDegrees;
  const View view(camera, kExtent * std::sqrt(3.0));
  lines = std::clamp(lines, 2, 400);
  stride = std::max(1, stride);
  const double range = params.range, height = params.height;
  auto z = [&](double u, double v) { return std::clamp(f.valid() ? f.eval(u * range, v * range, t) * height : 0.0, -1.0, 1.0); };

  // Scan along whichever axis of the base runs more across the screen, so
  // the scanlines lie flat and the floating horizon can hide what is behind.
  const Vec3 o = view.project({0, 0, 0}), ux = view.project({1, 0, 0}), uy = view.project({0, 1, 0});
  const bool alongX = std::abs(ux.x - o.x) >= std::abs(uy.x - o.x);

  // How much light each point gets, from a lamp above and to one side of the
  // surface. The beam's dwell follows it, so that is how bright the sheet is there.
  const double shading = std::clamp(params.shading, 0.0, 1.0);
  auto weight = [&](double u, double v) {
    constexpr double h = 0.02;
    const double dzdu = (z(u + h, v) - z(u - h, v)) / (2.0 * h), dzdv = (z(u, v + h) - z(u, v - h)) / (2.0 * h);
    Vec3 n{-dzdu, -dzdv, 1.0};
    const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    n = {n.x / len, n.y / len, n.z / len};
    constexpr double lx = -0.62, ly = -0.42, lz = 0.66; // unit length
    const double lambert = std::max(0.0, n.x * lx + n.y * ly + n.z * lz);
    const double light = lambert * lambert; // squared: the eye reads the log of the light, so stretch it
    return static_cast<float>((1.0 - shading) + shading * (0.04 + 0.96 * light));
  };

  struct Line {
    std::vector<Vec3> points; // on screen, with depth
    std::vector<float> weights;
    double depth = 0.0;
  };
  std::vector<Line> scan;
  for (int j = offset % stride; j < lines; j += stride) {
    const double fixed = -1.0 + 2.0 * j / (lines - 1);
    Line line;
    line.points.resize(kSamples + 1);
    line.weights.resize(kSamples + 1);
    for (int i = 0; i <= kSamples; ++i) {
      const double along = -1.0 + 2.0 * i / kSamples;
      const double u = alongX ? along : fixed, v = alongX ? fixed : along;
      line.points[static_cast<std::size_t>(i)] = view.project({u * kExtent, v * kExtent, z(u, v) * kExtent});
      line.weights[static_cast<std::size_t>(i)] = weight(u, v);
    }
    line.depth = alongX ? view.project({0.0, fixed, 0.0}).z : view.project({fixed, 0.0, 0.0}).z;
    scan.push_back(std::move(line));
  }
  // Front to back, with the floating horizon hiding what has been passed in front of.
  std::sort(scan.begin(), scan.end(), [](const Line& a, const Line& b) { return a.depth > b.depth; });
  std::vector<double> upper(kColumns, -1e9), lower(kColumns, 1e9), newUpper, newLower;
  auto column = [](double x) { return std::clamp(static_cast<int>((x + kScreen) / (2.0 * kScreen) * kColumns), 0, kColumns - 1); };
  Path path;
  bool reverse = false;
  std::vector<Path> runs;
  for (const Line& line : scan) {
    newUpper = upper;
    newLower = lower;
    runs.clear();
    Path run;
    auto flush = [&] {
      if (run.size() >= 2)
        runs.push_back(run);
      run.clear();
    };
    for (std::size_t i = 0; i + 1 < line.points.size(); ++i) {
      const Vec3 a = line.points[i], b = line.points[i + 1];
      const float wa = line.weights[i], wb = line.weights[i + 1];
      const int steps = std::max(1, std::abs(column(b.x) - column(a.x)) + 1);
      for (int k = (i == 0 ? 0 : 1); k <= steps; ++k) {
        const double u = static_cast<double>(k) / steps;
        const double x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u;
        const auto c = static_cast<std::size_t>(column(x));
        if (!params.hidden || y >= upper[c] || y <= lower[c])
          run.push_back({x, y, true, wa + (wb - wa) * static_cast<float>(u)});
        else
          flush();
        newUpper[c] = std::max(newUpper[c], y);
        newLower[c] = std::min(newLower[c], y);
      }
    }
    flush();
    upper.swap(newUpper);
    lower.swap(newLower);
    // To and fro: every other line is drawn the other way, so the beam does
    // not fly back across the sheet between lines.
    if (reverse) {
      std::reverse(runs.begin(), runs.end());
      for (Path& r : runs)
        std::reverse(r.begin(), r.end());
    }
    for (Path& r : runs) {
      // The weight belongs to the segment arriving at a point: after a
      // reversal, move each one along by a point.
      if (reverse)
        for (std::size_t i = 0; i + 1 < r.size(); ++i)
          r[i].weight = r[i + 1].weight;
      r[0].pen = false; // the jump into a run
      path.insert(path.end(), r.begin(), r.end());
    }
    reverse = !reverse;
  }
  return path;
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

int SurfaceSource::sheetLines(const Params& params, int note) const noexcept {
  const double samplesPerPeriod = sampleRate_ / midiToHz(note);
  return std::clamp(std::min(params.scanlines, static_cast<int>(samplesPerPeriod / 14.0)), 6, 160);
}

void SurfaceSource::build(const Params& params, int note, double age, std::span<XY> table) {
  compile(params); // the function can be edited while a note sounds
  if (params.style == SurfaceParams::Style::Sheet) {
    const int stride = std::clamp(params.interlace, 1, 4);
    const int lines = sheetLines(params, note) * stride; // interlaced, each frame has the lines a period has room for
    const Path path = surfaceSheet(expression_, params, age, params.spin * age, lines, static_cast<int>(frame_++ % static_cast<std::uint32_t>(stride)), stride);
    if (path.empty()) {
      std::fill(table.begin(), table.end(), XY{});
      return;
    }
    const Segment segment{path, 1.0};
    buildTable(std::span<const Segment>(&segment, 1), table);
    return;
  }
  std::vector<Stroke> strokes = surfaceStrokes(expression_, params, age, params.spin * age);
  // The picture changes from frame to frame as it turns, so time is spread
  // evenly along it rather than solved for harmony.
  const Path path = strokesToPath(orderStrokes(std::move(strokes), {}));
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs

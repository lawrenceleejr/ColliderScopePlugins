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

std::vector<Path> surfaceRows(const Expression& f, const SurfaceParams& params, double t, double spinDegrees, int lines) {
  Camera camera = params.camera;
  camera.azimuth += spinDegrees;
  const View view(camera, kExtent * std::sqrt(3.0));
  lines = std::clamp(lines, 2, 640);
  const double range = params.range, height = params.height;
  auto z = [&](double u, double v) { return std::clamp(f.valid() ? f.eval(u * range, v * range, t) * height : 0.0, -1.0, 1.0); };

  // Scan along whichever axis of the base runs more across the screen, so
  // the scanlines lie flat and the floating horizon can hide what is behind.
  const Vec3 o = view.project({0, 0, 0}), ux = view.project({1, 0, 0}), uy = view.project({0, 1, 0});
  const bool alongX = std::abs(ux.x - o.x) >= std::abs(uy.x - o.x);

  // How much light each point gets, from a lamp above and to one side of the
  // surface. The beam's dwell follows it, so that is how bright the sheet is there.
  const double shading = std::clamp(params.voice.sheet.shading, 0.0, 1.0);
  // The surface on a grid, once: the lines, and one more beyond each end
  // each way, for the slopes. One evaluation a point.
  const int across = lines + 2, along = kSamples + 3;
  std::vector<double> grid(static_cast<std::size_t>(across * along));
  const double da = 2.0 / kSamples, df = 2.0 / (lines - 1);
  for (int j = 0; j < across; ++j)
    for (int i = 0; i < along; ++i) {
      const double fixed = -1.0 + (j - 1) * df, run = -1.0 + (i - 1) * da;
      grid[static_cast<std::size_t>(j * along + i)] = alongX ? z(run, fixed) : z(fixed, run);
    }
  auto at = [&](int j, int i) { return grid[static_cast<std::size_t>((j + 1) * along + (i + 1))]; };
  auto weight = [&](int j, int i) {
    const double dRun = (at(j, i + 1) - at(j, i - 1)) / (2.0 * da), dFixed = (at(j + 1, i) - at(j - 1, i)) / (2.0 * df);
    const double dzdu = alongX ? dRun : dFixed, dzdv = alongX ? dFixed : dRun;
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
  for (int j = 0; j < lines; ++j) {
    const double fixed = -1.0 + j * df;
    Line line;
    line.points.resize(kSamples + 1);
    line.weights.resize(kSamples + 1);
    for (int i = 0; i <= kSamples; ++i) {
      const double run = -1.0 + i * da;
      const double u = alongX ? run : fixed, v = alongX ? fixed : run;
      line.points[static_cast<std::size_t>(i)] = view.project({u * kExtent, v * kExtent, at(j, i) * kExtent});
      line.weights[static_cast<std::size_t>(i)] = weight(j, i);
    }
    line.depth = alongX ? view.project({0.0, fixed, 0.0}).z : view.project({fixed, 0.0, 0.0}).z;
    scan.push_back(std::move(line));
  }
  // Front to back, with the floating horizon hiding what has been passed in front of.
  std::sort(scan.begin(), scan.end(), [](const Line& a, const Line& b) { return a.depth > b.depth; });
  std::vector<double> upper(kColumns, -1e9), lower(kColumns, 1e9), newUpper, newLower;
  auto column = [](double x) { return std::clamp(static_cast<int>((x + kScreen) / (2.0 * kScreen) * kColumns), 0, kColumns - 1); };
  std::vector<Path> rows;
  rows.reserve(scan.size());
  Path run;
  for (const Line& line : scan) {
    newUpper = upper;
    newLower = lower;
    Path row;
    auto flush = [&] {
      if (run.size() >= 2) {
        run[0].pen = false; // the jump into a run
        row.insert(row.end(), run.begin(), run.end());
      }
      run.clear();
    };
    // The test runs column by column, but a point is kept only where a run
    // starts or ends and at the ends of the line's own pieces: the beam goes
    // straight between them anyway.
    bool pending = false;
    PathPoint last{};
    for (std::size_t i = 0; i + 1 < line.points.size(); ++i) {
      const Vec3 a = line.points[i], b = line.points[i + 1];
      const float wa = line.weights[i], wb = line.weights[i + 1];
      const int steps = std::max(1, std::abs(column(b.x) - column(a.x)) + 1);
      for (int k = (i == 0 ? 0 : 1); k <= steps; ++k) {
        const double u = static_cast<double>(k) / steps;
        const double x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u;
        const auto c = static_cast<std::size_t>(column(x));
        if (!params.hidden || y >= upper[c] || y <= lower[c]) {
          const PathPoint q{x, y, true, wa + (wb - wa) * static_cast<float>(u)};
          if (run.empty() || k == steps) {
            run.push_back(q);
            pending = false;
          } else {
            last = q;
            pending = true;
          }
        } else {
          if (pending)
            run.push_back(last);
          pending = false;
          flush();
        }
        newUpper[c] = std::max(newUpper[c], y);
        newLower[c] = std::min(newLower[c], y);
      }
    }
    if (pending)
      run.push_back(last);
    flush();
    upper.swap(newUpper);
    lower.swap(newLower);
    rows.push_back(std::move(row));
  }
  return rows;
}

Path surfaceSheet(const Expression& f, const SurfaceParams& params, double t, double spinDegrees, int lines, int offset,
                  int stride) {
  stride = std::max(1, stride);
  std::vector<Path> all = surfaceRows(f, params, t, spinDegrees, lines);
  // To and fro: every other line is drawn the other way, so the beam does
  // not fly back across the sheet between lines.
  Path path;
  bool reverse = false;
  for (std::size_t k = static_cast<std::size_t>(offset % stride); k < all.size(); k += static_cast<std::size_t>(stride)) {
    Path& row = all[k];
    if (reverse)
      reverseRow(row);
    path.insert(path.end(), row.begin(), row.end());
    reverse = !reverse;
  }
  return path;
}

std::vector<FillPolygon> surfacePolygons(const Expression& f, const SurfaceParams& params, double t, double spinDegrees, int n) {
  Camera camera = params.camera;
  camera.azimuth += spinDegrees;
  const View view(camera, kExtent * std::sqrt(3.0));
  n = std::clamp(n, 2, 96);
  const double range = params.range, height = params.height;
  auto model = [&](double u, double v) {
    const double z = std::clamp(f.valid() ? f.eval(u * range, v * range, t) * height : 0.0, -1.0, 1.0);
    return Vec3{u * kExtent, v * kExtent, z * kExtent};
  };
  std::vector<Vec3> grid(static_cast<std::size_t>((n + 1) * (n + 1)));
  for (int j = 0; j <= n; ++j)
    for (int i = 0; i <= n; ++i)
      grid[static_cast<std::size_t>(j * (n + 1) + i)] = view.project(model(-1.0 + 2.0 * i / n, -1.0 + 2.0 * j / n));
  std::vector<FillPolygon> out;
  for (int j = 0; j < n; ++j)
    for (int i = 0; i < n; ++i) {
      const Vec3 a = grid[static_cast<std::size_t>(j * (n + 1) + i)], b = grid[static_cast<std::size_t>(j * (n + 1) + i + 1)],
                 c = grid[static_cast<std::size_t>((j + 1) * (n + 1) + i + 1)], d = grid[static_cast<std::size_t>((j + 1) * (n + 1) + i)];
      // The normal, on the screen, from the quad's two diagonals.
      const Vec3 p{c.x - a.x, c.y - a.y, c.z - a.z}, q{d.x - b.x, d.y - b.y, d.z - b.z};
      FillPolygon poly;
      poly.contours.push_back({{a.x, a.y}, {b.x, b.y}, {c.x, c.y}, {d.x, d.y}});
      poly.depth = 0.25 * (a.z + b.z + c.z + d.z);
      poly.shade = lampShade({p.y * q.z - p.z * q.y, p.z * q.x - p.x * q.z, p.x * q.y - p.y * q.x});
      out.push_back(std::move(poly));
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

int SurfaceSource::sheetLines(const Params& params, int note) const noexcept {
  const double samplesPerPeriod = sampleRate_ / midiToHz(note);
  if (params.voice.timing == Timing::Harmonic)
    return planSheet(params.voice.sheet, samplesPerPeriod, false).rows;
  return std::clamp(std::min(params.voice.sheet.scanlines, static_cast<int>(samplesPerPeriod / 14.0)), 6, 160);
}

void SurfaceSource::build(const Params& params, int note, double age, std::span<XY> table) {
  compile(params); // the function can be edited while a note sounds
  if (params.voice.sheet.style == SheetParams::Style::Sheet) {
    const int stride = interlaceStride(params.voice.sheet.interlace);
    const int lines = sheetLines(params, note); // a frame's lines; interlaced, the sheet has stride times as many
    const int offset = static_cast<int>(frame_++ % static_cast<std::uint32_t>(stride));
    if (params.voice.timing == Timing::Harmonic) {
      // Equal time a line, on a consonant sweep, the lines spaced so the shading holds.
      const std::vector<Path> fine =
          surfaceRows(expression_, params, age, params.spin * age, std::min(320, 3 * lines * stride / 2));
      std::vector<Segment> segments = harmonicSegments(pickRows(fine, lines, offset, stride), 1.0, Path{});
      buildTable(segments, table);
      return;
    }
    const Path path = surfaceSheet(expression_, params, age, params.spin * age, lines * stride, offset, stride);
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

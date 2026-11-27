#include "cscore/sheet.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "cscore/pitch.h"

namespace cs {

namespace {
constexpr int kColumns = 192;
}

std::vector<Path> rasterRows(std::span<const FillPolygon> fills, int rows, double shading) {
  rows = std::clamp(rows, 1, 1024);
  std::vector<Path> out(static_cast<std::size_t>(rows));
  double xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9;
  for (const FillPolygon& f : fills)
    for (const auto& contour : f.contours)
      for (const Vec2& p : contour) {
        xmin = std::min(xmin, p.x);
        xmax = std::max(xmax, p.x);
        ymin = std::min(ymin, p.y);
        ymax = std::max(ymax, p.y);
      }
  if (xmax <= xmin || ymax <= ymin)
    return out;
  const double dx = (xmax - xmin) / kColumns, dy = (ymax - ymin) / rows;
  shading = std::clamp(shading, 0.0, 1.0);

  const auto n = static_cast<std::size_t>(rows);
  std::vector<float> depth(n * kColumns, -std::numeric_limits<float>::infinity());
  std::vector<float> weight(n * kColumns, -1.0f);
  std::vector<double> crossings;

  for (const FillPolygon& f : fills) {
    double fy0 = 1e9, fy1 = -1e9;
    for (const auto& contour : f.contours)
      for (const Vec2& p : contour) {
        fy0 = std::min(fy0, p.y);
        fy1 = std::max(fy1, p.y);
      }
    const auto w = static_cast<float>((1.0 - shading) + shading * (0.04 + 0.96 * std::clamp(static_cast<double>(f.shade), 0.0, 1.0)));
    const auto d = static_cast<float>(f.depth);
    // Only the lines that cross this polygon: line k is at y = ymax - (k + 0.5) dy.
    const int k0 = std::max(0, static_cast<int>(std::ceil((ymax - fy1) / dy - 0.5)));
    const int k1 = std::min(rows - 1, static_cast<int>(std::floor((ymax - fy0) / dy - 0.5)));
    for (int k = k0; k <= k1; ++k) {
      const double y = ymax - (k + 0.5) * dy;
      crossings.clear();
      for (const auto& contour : f.contours) {
        const std::size_t m = contour.size();
        for (std::size_t i = 0; i < m; ++i) {
          const Vec2 a = contour[i], b = contour[(i + 1) % m];
          if ((a.y <= y) != (b.y <= y)) // the half-open rule: a vertex on the line counts once
            crossings.push_back(a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y));
        }
      }
      std::sort(crossings.begin(), crossings.end());
      float* zrow = depth.data() + static_cast<std::size_t>(k) * kColumns;
      float* wrow = weight.data() + static_cast<std::size_t>(k) * kColumns;
      for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) { // even-odd within one polygon
        const int c0 = std::max(0, static_cast<int>(std::ceil((crossings[i] - xmin) / dx - 0.5)));
        const int c1 = std::min(kColumns - 1, static_cast<int>(std::floor((crossings[i + 1] - xmin) / dx - 0.5)));
        for (int c = c0; c <= c1; ++c)
          if (d >= zrow[c]) { // nearer (or as near) covers: polygons are a union
            zrow[c] = d;
            wrow[c] = w;
          }
      }
    }
  }

  // Each line left to right. A point is made where a run starts, ends or
  // changes shade; the weight belongs to the segment arriving at the point.
  for (std::size_t k = 0; k < n; ++k) {
    const double y = ymax - (static_cast<double>(k) + 0.5) * dy;
    const float* wrow = weight.data() + k * kColumns;
    Path& row = out[k];
    for (int c = 0; c < kColumns; ++c) {
      const float w = wrow[c];
      const bool inPrevious = c > 0 && wrow[c - 1] >= 0.0f;
      const double x = xmin + c * dx;
      if (w >= 0.0f) {
        if (!inPrevious)
          row.push_back({x, y, false, 1.0f}); // a run starts: jump to it
        else if (wrow[c - 1] != w)
          row.push_back({x, y, true, wrow[c - 1]}); // the shade changes
      } else if (inPrevious) {
        row.push_back({x, y, true, wrow[c - 1]}); // a run ends
      }
    }
    if (wrow[kColumns - 1] >= 0.0f)
      row.push_back({xmax, y, true, wrow[kColumns - 1]});
  }
  return out;
}

void reverseRow(Path& row) {
  if (row.empty())
    return;
  // Split into runs, each starting with a jump.
  std::vector<Path> runs;
  for (const PathPoint& q : row) {
    if (!q.pen || runs.empty())
      runs.emplace_back();
    runs.back().push_back(q);
  }
  row.clear();
  for (auto r = runs.rbegin(); r != runs.rend(); ++r) {
    std::reverse(r->begin(), r->end());
    // The weight belongs to the segment arriving at a point: move each one back a point.
    for (std::size_t i = r->size(); i-- > 1;)
      (*r)[i].weight = (*r)[i - 1].weight;
    for (std::size_t i = 1; i < r->size(); ++i)
      (*r)[i].pen = true;
    (*r)[0].pen = false;
    (*r)[0].weight = 1.0f;
    row.insert(row.end(), r->begin(), r->end());
  }
}

Path rasterSheet(std::span<const FillPolygon> fills, int rows, int offset, int stride, double shading) {
  stride = std::max(1, stride);
  std::vector<Path> all = rasterRows(fills, rows, shading);
  Path path;
  bool reverse = false;
  for (std::size_t k = static_cast<std::size_t>(offset % stride); k < all.size(); k += static_cast<std::size_t>(stride)) {
    Path& row = all[k];
    if (row.empty())
      continue;
    if (reverse)
      reverseRow(row);
    path.insert(path.end(), row.begin(), row.end());
    reverse = !reverse;
  }
  return path;
}

int interlaceStride(int interlace) noexcept { return interlace >= 4 ? 4 : interlace >= 2 ? 2 : 1; }

double rowLength(const Path& row) noexcept {
  double length = 0.0;
  for (std::size_t i = 1; i < row.size(); ++i)
    if (row[i].pen)
      length += std::hypot(row[i].x - row[i - 1].x, row[i].y - row[i - 1].y) * row[i].weight;
  return length;
}

SheetPlan planSheet(const SheetParams& params, double samplesPerPeriod, bool withLines) noexcept {
  const double asked = withLines ? std::clamp(params.fillShare, 0.2, 0.95) : 1.0;
  const int ceiling = std::clamp(std::min(params.scanlines, static_cast<int>(samplesPerPeriod * asked / 14.0)), 4, 160);
  // Rows for a sweep on harmonic h: 2 * round(h * share), even so the beam
  // ends the period on the side it began; one pair fewer if that would
  // leave the lines over the fill no time. The most rows under the ceiling.
  auto rowsFor = [&](int h) {
    int rows = 2 * static_cast<int>(std::lround(h * asked));
    if (withLines && rows / (2.0 * h) > 0.97)
      rows -= 2;
    return rows;
  };
  SheetPlan best{2, 2, 0.0};
  for (int base : {1, 3})
    for (int h = base * 2; h <= 1024; h *= 2) {
      const int rows = rowsFor(h);
      if (rows > ceiling)
        break;
      if (rows >= 2 && (rows > best.rows || (rows == best.rows && h < best.harmonic)))
        best = {rows, h, 0.0};
    }
  best.share = withLines ? best.rows / (2.0 * best.harmonic) : 1.0;
  return best;
}

std::vector<Path> pickRows(const std::vector<Path>& fine, int count, int offset, int stride) {
  std::vector<Path> out;
  stride = std::max(1, stride);
  if (count <= 0 || fine.empty())
    return out;
  // Each line's claim on the time: its weighted length, but no less than a
  // floor, so a narrow part still gets lines no more than ~3x the mean apart.
  std::vector<double> claim(fine.size());
  double sum = 0.0;
  int lit = 0;
  for (std::size_t i = 0; i < fine.size(); ++i) {
    claim[i] = rowLength(fine[i]);
    if (claim[i] > 0.0) {
      sum += claim[i];
      ++lit;
    }
  }
  if (lit == 0)
    return out;
  const double floor = 0.35 * sum / lit;
  double total = 0.0;
  for (double& c : claim)
    if (c > 0.0) {
      c = std::max(c, floor);
      total += c;
    }
  // Line j of the whole raster sits where the running claim passes (j + 1/2) / N of it.
  const int all = count * stride;
  out.reserve(static_cast<std::size_t>(count));
  double running = 0.0;
  std::size_t i = 0;
  for (int j = 0; j < all; ++j) {
    const double target = (j + 0.5) / all * total;
    while (i + 1 < fine.size() && (claim[i] <= 0.0 || running + claim[i] < target)) {
      running += claim[i];
      ++i;
    }
    std::size_t pick = i;
    while (pick > 0 && claim[pick] <= 0.0) // past the last lit line: step back to it
      --pick;
    if (j % stride == offset % stride)
      out.push_back(fine[pick]);
  }
  for (std::size_t k = 1; k < out.size(); k += 2)
    reverseRow(out[k]);
  return out;
}

std::vector<Segment> harmonicSegments(std::vector<Path>&& rows, double share, Path&& over) {
  std::vector<Segment> segments;
  segments.reserve(rows.size() + 1);
  const double each = (over.empty() ? 1.0 : share) / std::max<std::size_t>(rows.size(), 1);
  for (Path& row : rows)
    if (!row.empty())
      segments.push_back({std::move(row), each});
  if (!over.empty())
    segments.push_back({std::move(over), segments.empty() ? 1.0 : 1.0 - share});
  return segments;
}

bool isClosed(const Stroke& s) noexcept {
  if (s.size() < 4)
    return false;
  return std::hypot(s.front().x - s.back().x, s.front().y - s.back().y) < 1e-6;
}

void fillClosed(const std::vector<Stroke>& strokes, float shade, std::vector<FillPolygon>& out, double depth) {
  for (const Stroke& s : strokes)
    if (isClosed(s)) {
      FillPolygon f;
      f.contours.emplace_back(s.begin(), s.end() - 1);
      f.depth = depth;
      f.shade = shade;
      out.push_back(std::move(f));
    }
}

void ribbons(const std::vector<Stroke>& strokes, double width, float shade, std::vector<FillPolygon>& out, bool includeClosed,
             std::size_t maxPoints) {
  const double h = 0.5 * width;
  for (const Stroke& s : strokes) {
    if (s.empty() || (!includeClosed && isClosed(s)) || (maxPoints > 0 && s.size() > maxPoints))
      continue;
    bool dot = true;
    for (const Vec2& p : s)
      dot = dot && std::hypot(p.x - s[0].x, p.y - s[0].y) < 1e-9;
    if (dot) { // a disc
      FillPolygon f;
      f.contours.emplace_back();
      for (int i = 0; i < 12; ++i) {
        const double a = 2.0 * std::numbers::pi * i / 12.0;
        f.contours.back().push_back({s[0].x + h * std::cos(a), s[0].y + h * std::sin(a)});
      }
      f.shade = shade;
      out.push_back(std::move(f));
      continue;
    }
    for (std::size_t i = 1; i < s.size(); ++i) { // a quad per segment, a little longer so the joins close
      const Vec2 a = s[i - 1], b = s[i];
      const double len = std::hypot(b.x - a.x, b.y - a.y);
      if (len <= 1e-12)
        continue;
      const double ux = (b.x - a.x) / len, uy = (b.y - a.y) / len, nx = -uy * h, ny = ux * h;
      const double ex = ux * h * 0.5, ey = uy * h * 0.5;
      FillPolygon f;
      f.contours.push_back({{a.x - ex + nx, a.y - ey + ny}, {b.x + ex + nx, b.y + ey + ny}, {b.x + ex - nx, b.y + ey - ny},
                            {a.x - ex - nx, a.y - ey - ny}});
      f.shade = shade;
      out.push_back(std::move(f));
    }
  }
}

float lampShade(Vec3 n) noexcept {
  const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
  if (len <= 0.0)
    return 0.5f;
  if (n.z < 0.0) // turn it to face the viewer: we see whichever side is in front
    n = {-n.x, -n.y, -n.z};
  constexpr double lx = -0.45, ly = 0.55, lz = 0.70;
  const double lambert = std::max(0.0, (n.x * lx + n.y * ly + n.z * lz) / len);
  return static_cast<float>(lambert * lambert); // squared: the eye reads the log of the light
}

int SheetRenderer::lines(const SheetParams& params, int note, bool withLines) const noexcept {
  const double share = withLines ? std::clamp(params.fillShare, 0.2, 0.95) : 1.0;
  const double samples = sampleRate_ / midiToHz(note) * share;
  return std::clamp(std::min(params.scanlines, static_cast<int>(samples / 14.0)), 6, 160);
}

void SheetRenderer::build(const SheetParams& params, int note, std::span<const FillPolygon> fills,
                          const std::vector<Stroke>& lines, std::span<XY> table, bool harmonic) {
  const int stride = interlaceStride(params.interlace);
  const int offset = static_cast<int>(frame_++ % static_cast<std::uint32_t>(stride));
  const bool withLines = !lines.empty();
  Path over = withLines ? strokesToPath(orderStrokes(lines, {})) : Path{};
  std::vector<Path> rows;
  double share = withLines ? std::clamp(params.fillShare, 0.2, 0.95) : 1.0;
  if (fills.empty()) {
    rows.clear();
  } else if (harmonic) {
    const SheetPlan plan = planSheet(params, sampleRate_ / midiToHz(note), withLines);
    share = plan.share;
    // Fine lines to choose from: three for every line drawn.
    const std::vector<Path> fine = rasterRows(fills, std::min(1024, 3 * plan.rows * stride), params.shading);
    rows = pickRows(fine, plan.rows, offset, stride);
  } else {
    rows.push_back(rasterSheet(fills, this->lines(params, note, withLines) * stride, offset, stride, params.shading));
  }
  // Ribbons and discs reach a little past the lines they thicken: keep the
  // whole picture on the screen.
  double reach = 0.0;
  auto measure = [&](const Path& p) {
    for (const PathPoint& q : p)
      reach = std::max(reach, std::hypot(q.x, q.y));
  };
  for (const Path& r : rows)
    measure(r);
  measure(over);
  if (reach > 0.93) {
    auto shrink = [&](Path& p) {
      for (PathPoint& q : p) {
        q.x *= 0.93 / reach;
        q.y *= 0.93 / reach;
      }
    };
    for (Path& r : rows)
      shrink(r);
    shrink(over);
  }
  std::vector<Segment> segments;
  if (harmonic) {
    segments = harmonicSegments(std::move(rows), share, std::move(over));
  } else {
    if (!rows.empty() && !rows[0].empty())
      segments.push_back({std::move(rows[0]), over.empty() ? 1.0 : share});
    if (!over.empty())
      segments.push_back({std::move(over), segments.empty() ? 1.0 : 1.0 - share});
  }
  buildTable(segments, table);
}

} // namespace cs

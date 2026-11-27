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

Path rasterSheet(std::span<const FillPolygon> fills, int rows, int offset, int stride, double shading) {
  Path path;
  rows = std::clamp(rows, 1, 640);
  stride = std::max(1, stride);
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
    return path;
  const double dx = (xmax - xmin) / kColumns, dy = (ymax - ymin) / rows;
  shading = std::clamp(shading, 0.0, 1.0);

  // Only the rows this frame draws are rasterised.
  std::vector<int> chosen;
  for (int r = offset % stride; r < rows; r += stride)
    chosen.push_back(r);
  const std::size_t n = chosen.size();
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
    for (std::size_t k = 0; k < n; ++k) {
      const double y = ymax - (chosen[k] + 0.5) * dy;
      if (y < fy0 || y > fy1)
        continue;
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
      for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) { // even-odd within one polygon
        const int c0 = std::max(0, static_cast<int>(std::ceil((crossings[i] - xmin) / dx - 0.5)));
        const int c1 = std::min(kColumns - 1, static_cast<int>(std::floor((crossings[i + 1] - xmin) / dx - 0.5)));
        float* zrow = depth.data() + k * kColumns;
        float* wrow = weight.data() + k * kColumns;
        for (int c = c0; c <= c1; ++c)
          if (d >= zrow[c]) { // nearer (or as near) covers: polygons are a union
            zrow[c] = d;
            wrow[c] = w;
          }
      }
    }
  }

  // Scanlines, to and fro. A point is made where a run starts, ends or
  // changes shade; the weight belongs to the segment arriving at the point.
  bool reverse = false;
  for (std::size_t k = 0; k < n; ++k) {
    const double y = ymax - (chosen[k] + 0.5) * dy;
    const float* wrow = weight.data() + k * kColumns;
    auto xAt = [&](double c) { return xmin + c * dx; };
    bool any = false;
    for (int i = 0; i < kColumns; ++i) {
      const int c = reverse ? kColumns - 1 - i : i;
      const float w = wrow[c];
      const int previous = reverse ? c + 1 : c - 1;
      const bool inPrevious = previous >= 0 && previous < kColumns && wrow[previous] >= 0.0f;
      const double edge = reverse ? c + 1.0 : static_cast<double>(c); // where this cell begins, in the direction of travel
      if (w >= 0.0f) {
        if (!inPrevious) {
          path.push_back({xAt(edge), y, false, 1.0f}); // a run starts: jump to it
          any = true;
        } else if (wrow[previous] != w) {
          path.push_back({xAt(edge), y, true, wrow[previous]}); // the shade changes
        }
      } else if (inPrevious) {
        path.push_back({xAt(edge), y, true, wrow[previous]}); // a run ends
      }
    }
    const int last = reverse ? 0 : kColumns - 1;
    if (wrow[last] >= 0.0f)
      path.push_back({xAt(reverse ? 0.0 : static_cast<double>(kColumns)), y, true, wrow[last]});
    if (any)
      reverse = !reverse;
  }
  return path;
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
                          const std::vector<Stroke>& lines, std::span<XY> table) {
  const int stride = std::clamp(params.interlace, 1, 4);
  const bool withLines = !lines.empty();
  const Path fill =
      fills.empty() ? Path{}
                    : rasterSheet(fills, this->lines(params, note, withLines) * stride,
                                  static_cast<int>(frame_++ % static_cast<std::uint32_t>(stride)), stride, params.shading);
  Path over = withLines ? strokesToPath(orderStrokes(lines, {})) : Path{};
  Path filled = fill;
  // Ribbons and discs reach a little past the lines they thicken: keep the
  // whole picture on the screen.
  double reach = 0.0;
  for (const Path* p : {&filled, &over})
    for (const PathPoint& q : *p)
      reach = std::max(reach, std::hypot(q.x, q.y));
  if (reach > 0.93)
    for (Path* p : {&filled, &over})
      for (PathPoint& q : *p) {
        q.x *= 0.93 / reach;
        q.y *= 0.93 / reach;
      }
  std::vector<Segment> segments;
  if (!filled.empty())
    segments.push_back({filled, over.empty() ? 1.0 : std::clamp(params.fillShare, 0.2, 0.95)});
  if (!over.empty())
    segments.push_back({over, filled.empty() ? 1.0 : 1.0 - std::clamp(params.fillShare, 0.2, 0.95)});
  buildTable(segments, table);
}

} // namespace cs

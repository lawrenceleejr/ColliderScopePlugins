#include "cscore/path.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cs {

namespace {
double dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
} // namespace

std::vector<Stroke> orderStrokes(std::vector<Stroke> strokes, Vec2 start) {
  std::erase_if(strokes, [](const Stroke& s) { return s.empty(); });
  if (strokes.size() < 2)
    return strokes;

  std::vector<Stroke> out;
  out.reserve(strokes.size());
  std::vector<bool> left(strokes.size(), true);
  Vec2 p = start;
  for (std::size_t n = 0; n < strokes.size(); ++n) {
    double bestHead = std::numeric_limits<double>::infinity(), bestTail = bestHead;
    std::size_t iHead = 0, iTail = 0;
    for (std::size_t i = 0; i < strokes.size(); ++i) {
      if (!left[i])
        continue;
      const double dh = dist(strokes[i].front(), p);
      const double dt = dist(strokes[i].back(), p);
      if (dh < bestHead) {
        bestHead = dh;
        iHead = i;
      }
      if (dt < bestTail) {
        bestTail = dt;
        iTail = i;
      }
    }
    if (bestTail < bestHead) {
      Stroke& s = strokes[iTail];
      std::reverse(s.begin(), s.end());
      p = s.back();
      left[iTail] = false;
      out.push_back(std::move(s));
    } else {
      Stroke& s = strokes[iHead];
      p = s.back();
      left[iHead] = false;
      out.push_back(std::move(s));
    }
  }
  return out;
}

void appendStroke(Path& path, const Stroke& stroke) {
  bool first = true;
  for (const Vec2& v : stroke) {
    path.push_back({v.x, v.y, !first});
    first = false;
  }
}

Path strokesToPath(const std::vector<Stroke>& strokes) {
  Path path;
  std::size_t total = 0;
  for (const Stroke& s : strokes)
    total += s.size();
  path.reserve(total);
  for (const Stroke& s : strokes)
    appendStroke(path, s);
  return path;
}

void resamplePath(const Path& path, std::span<XY> out, Vec2 closeTo, double penUpWeight) {
  if (out.empty())
    return;
  if (path.empty()) {
    std::fill(out.begin(), out.end(), XY{});
    return;
  }
  const std::size_t k = path.size();
  // Point i for i < k is path[i]; point k is closeTo, reached by a jump.
  auto point = [&](std::size_t i) { return i < k ? Vec2{path[i].x, path[i].y} : closeTo; };
  auto weight = [&](std::size_t i) { return (i < k && path[i].pen) ? 1.0 : penUpWeight; };

  std::vector<double> s(k + 1);
  s[0] = 0.0;
  for (std::size_t i = 1; i <= k; ++i)
    s[i] = s[i - 1] + dist(point(i - 1), point(i)) * weight(i);
  const double total = s[k];
  if (!(total > 0.0)) {
    std::fill(out.begin(), out.end(), XY{static_cast<float>(path[0].x), static_cast<float>(path[0].y)});
    return;
  }

  std::size_t seg = 0; // segment from point(seg) to point(seg + 1)
  const double n = static_cast<double>(out.size());
  for (std::size_t j = 0; j < out.size(); ++j) {
    const double t = total * static_cast<double>(j) / n;
    while (seg + 1 < k && s[seg + 1] <= t)
      ++seg;
    const double len = s[seg + 1] - s[seg];
    const double u = len > 0.0 ? (t - s[seg]) / len : 0.0;
    const Vec2 a = point(seg), b = point(seg + 1);
    out[j] = {static_cast<float>(a.x + (b.x - a.x) * u), static_cast<float>(a.y + (b.y - a.y) * u)};
  }
}

void buildTable(std::span<const Segment> segments, std::span<XY> table, double penUpWeight) {
  std::vector<const Segment*> live;
  double sum = 0.0;
  for (const Segment& s : segments) {
    if (!s.path.empty() && s.fraction > 0.0) {
      live.push_back(&s);
      sum += s.fraction;
    }
  }
  if (live.empty()) {
    std::fill(table.begin(), table.end(), XY{});
    return;
  }
  const double n = static_cast<double>(table.size());
  double cum = 0.0;
  std::size_t begin = 0;
  for (std::size_t i = 0; i < live.size(); ++i) {
    cum += live[i]->fraction / sum;
    const std::size_t end =
        i + 1 == live.size() ? table.size() : std::min(table.size(), static_cast<std::size_t>(std::llround(cum * n)));
    const Path& nextPath = live[(i + 1) % live.size()]->path;
    if (end > begin)
      resamplePath(live[i]->path, table.subspan(begin, end - begin), {nextPath[0].x, nextPath[0].y}, penUpWeight);
    begin = std::max(begin, end);
  }
}

} // namespace cs

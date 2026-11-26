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

// ----------------------------------------------------------- harmonic timing

double harmonicWeight(int k) noexcept {
  if (k < 1)
    return 0.0;
  double weight = 1.0;
  while (k % 2 == 0)
    k /= 2;
  for (int prime = 3; k > 1; prime += 2) {
    while (k % prime == 0) {
      weight *= prime == 3 ? 0.9 : prime == 5 ? 0.7 : prime == 7 ? 0.35 : 0.15;
      k /= prime;
    }
  }
  return weight;
}

double consonance(std::span<const XY> table, int maxHarmonic) noexcept {
  const std::size_t n = table.size();
  if (n < 4)
    return 0.0;
  maxHarmonic = std::min(maxHarmonic, static_cast<int>(n / 2) - 1);
  double weighted = 0.0, total = 0.0;
  for (int k = 1; k <= maxHarmonic; ++k) {
    // One DFT bin of each channel, by a rotating phasor.
    const double step = 2.0 * 3.14159265358979323846 * k / static_cast<double>(n);
    const double cs = std::cos(step), sn = std::sin(step);
    double c = 1.0, s = 0.0, xr = 0.0, xi = 0.0, yr = 0.0, yi = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      xr += table[i].x * c;
      xi += table[i].x * s;
      yr += table[i].y * c;
      yi += table[i].y * s;
      const double next = c * cs - s * sn;
      s = s * cs + c * sn;
      c = next;
    }
    const double energy = xr * xr + xi * xi + yr * yr + yi * yi;
    weighted += harmonicWeight(k) * energy;
    total += energy;
  }
  return total > 0.0 ? weighted / total : 0.0;
}

int nextHarmonicGrid(int atLeast) noexcept {
  int best = 1;
  while (best < atLeast)
    best *= 2;
  for (int three = 3; three < best; three *= 2)
    if (three >= atLeast)
      best = std::min(best, three);
  return best;
}

void buildTableOnGrid(const std::vector<Stroke>& ordered, std::span<XY> table, int slots, double penUpWeight) {
  std::vector<const Stroke*> strokes;
  for (const Stroke& s : ordered)
    if (!s.empty())
      strokes.push_back(&s);
  const std::size_t count = strokes.size();
  if (count == 0) {
    std::fill(table.begin(), table.end(), XY{});
    return;
  }
  slots = std::max(slots, static_cast<int>(count));

  // Each stroke's share of the time: its length, plus its jump to the next.
  std::vector<double> weight(count);
  double total = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    const Stroke& s = *strokes[i];
    double length = 0.0;
    for (std::size_t j = 1; j < s.size(); ++j)
      length += dist(s[j - 1], s[j]);
    length += penUpWeight * dist(s.back(), strokes[(i + 1) % count]->front());
    weight[i] = std::max(length, 1e-9);
    total += weight[i];
  }
  // Whole slots: at least one each, the rest by largest remainder.
  std::vector<int> share(count);
  std::vector<double> remainder(count);
  int used = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const double exact = weight[i] / total * slots;
    share[i] = std::max(1, static_cast<int>(exact));
    remainder[i] = exact - share[i];
    used += share[i];
  }
  while (used != slots) {
    std::size_t pick = 0;
    for (std::size_t i = 1; i < count; ++i) {
      if (used < slots ? remainder[i] > remainder[pick]
                       : (share[i] > 1 && (share[pick] <= 1 || remainder[i] < remainder[pick])))
        pick = i;
    }
    const int change = used < slots ? 1 : -1;
    if (change < 0 && share[pick] <= 1)
      break;
    share[pick] += change;
    remainder[pick] -= change;
    used += change;
  }

  const double perSlot = static_cast<double>(table.size()) / used;
  int slot = 0;
  Path path;
  for (std::size_t i = 0; i < count; ++i) {
    const auto begin = static_cast<std::size_t>(std::llround(slot * perSlot));
    slot += share[i];
    const std::size_t end = i + 1 == count ? table.size() : static_cast<std::size_t>(std::llround(slot * perSlot));
    path.clear();
    appendStroke(path, *strokes[i]);
    path[0].pen = true; // the jump into this stroke belongs to the stroke before
    const Vec2 next = strokes[(i + 1) % count]->front();
    if (end > begin)
      resamplePath(path, table.subspan(begin, end - begin), next, penUpWeight);
  }
}

void HarmonicGrid::build(const std::vector<Stroke>& ordered, std::span<XY> table, double penUpWeight) {
  std::size_t count = 0;
  for (const Stroke& s : ordered)
    count += !s.empty();
  if (count == 0) {
    std::fill(table.begin(), table.end(), XY{});
    return;
  }
  if (count != count_) {
    // Try the next few grids that fit and keep the most consonant.
    count_ = count;
    int candidate = nextHarmonicGrid(static_cast<int>(count));
    double best = -1.0;
    for (int tries = 0; tries < 4; ++tries) {
      buildTableOnGrid(ordered, table, candidate, penUpWeight);
      const double score = consonance(table);
      if (score > best + 1e-6) {
        best = score;
        slots_ = candidate;
      }
      candidate = nextHarmonicGrid(candidate + 1);
    }
  }
  buildTableOnGrid(ordered, table, slots_, penUpWeight);
}

} // namespace cs

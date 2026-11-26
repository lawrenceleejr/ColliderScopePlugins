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

double HarmonicGrid::arrange(const std::vector<Stroke>& strokes, const Arrangement& a, int slots, std::span<XY> table,
                             double penUpWeight, bool score) {
  arranged_.resize(a.order.size());
  double drawn = 0.0, jumped = 0.0;
  for (std::size_t i = 0; i < a.order.size(); ++i) {
    Stroke& s = arranged_[i];
    s = strokes[a.order[i]];
    if (a.flip[i])
      std::reverse(s.begin(), s.end());
    for (std::size_t j = 1; j < s.size(); ++j)
      drawn += dist(s[j - 1], s[j]);
  }
  for (std::size_t i = 0; i < arranged_.size(); ++i)
    jumped += dist(arranged_[i].back(), arranged_[(i + 1) % arranged_.size()].front());
  if (!score) {
    buildTableOnGrid(arranged_, table, slots, penUpWeight);
    return 0.0;
  }
  // To score, a quarter-size table is enough: harmonics up to 32 need no more.
  trial_.resize(table.size() / 4);
  buildTableOnGrid(arranged_, trial_, slots, penUpWeight);
  // Consonance, less a charge for jumps: a jump is drawn too, as a faint line.
  return consonance(trial_) - 0.3 * jumped / (drawn + jumped + 1e-9);
}

void HarmonicGrid::build(const std::vector<Stroke>& strokes, std::span<XY> table, Vec2 start, double penUpWeight) {
  std::vector<std::uint32_t> live;
  for (std::size_t i = 0; i < strokes.size(); ++i)
    if (!strokes[i].empty())
      live.push_back(static_cast<std::uint32_t>(i));
  const std::size_t count = live.size();
  if (count == 0) {
    std::fill(table.begin(), table.end(), XY{});
    order_.clear();
    return;
  }
  auto next = [this]() { // xorshift: the solver's own dice
    random_ ^= random_ << 13;
    random_ ^= random_ >> 7;
    random_ ^= random_ << 17;
    return random_;
  };

  bool stale = order_.size() != count;
  for (std::size_t i = 0; !stale && i < count; ++i)
    stale = order_[i] >= strokes.size() || strokes[order_[i]].empty();
  if (stale) {
    // Start from the nearest-neighbour order, on the most consonant of the next few grids.
    order_.clear();
    flip_.clear();
    std::vector<bool> left(count, true);
    Vec2 p = start;
    for (std::size_t n = 0; n < count && !solveOrder_; ++n) { // as given
      order_.push_back(live[n]);
      flip_.push_back(false);
    }
    for (std::size_t n = 0; n < count && solveOrder_; ++n) {
      double best = 1e300;
      std::size_t pick = 0;
      bool reversed = false;
      for (std::size_t i = 0; i < count; ++i) {
        if (!left[i])
          continue;
        const Stroke& s = strokes[live[i]];
        const double head = dist(s.front(), p), tail = dist(s.back(), p);
        if (head < best) {
          best = head;
          pick = i;
          reversed = false;
        }
        if (tail < best) {
          best = tail;
          pick = i;
          reversed = true;
        }
      }
      left[pick] = false;
      order_.push_back(live[pick]);
      flip_.push_back(reversed);
      const Stroke& s = strokes[live[pick]];
      p = reversed ? s.front() : s.back();
    }
    int candidate = nextHarmonicGrid(static_cast<int>(count));
    score_ = -1e9;
    for (int tries = 0; tries < 4; ++tries) {
      const double score = arrange(strokes, {order_, flip_}, candidate, table, penUpWeight, true);
      if (score > score_ + 1e-6) {
        score_ = score;
        slots_ = candidate;
      }
      candidate = nextHarmonicGrid(candidate + 1);
    }
    // Then improve it: swap two strokes, reverse one, reverse a run, or change the grid.
    for (int trial = 0; trial < (solveOrder_ ? kTrials : 0) && count > 1; ++trial) {
      Arrangement a{order_, flip_};
      int slots = slots_;
      const std::uint64_t r = next();
      const std::size_t i = (r >> 8) % count, j = (r >> 24) % count;
      switch (r % 4) {
      case 0:
        std::swap(a.order[i], a.order[j]);
        std::swap(a.flip[i], a.flip[j]);
        break;
      case 1:
        a.flip[i] = !a.flip[i];
        break;
      case 2: { // reverse a run of strokes, as a whole
        const std::size_t lo = std::min(i, j), hi = std::max(i, j);
        std::reverse(a.order.begin() + static_cast<std::ptrdiff_t>(lo), a.order.begin() + static_cast<std::ptrdiff_t>(hi) + 1);
        std::reverse(a.flip.begin() + static_cast<std::ptrdiff_t>(lo), a.flip.begin() + static_cast<std::ptrdiff_t>(hi) + 1);
        for (std::size_t k = lo; k <= hi; ++k)
          a.flip[k] = !a.flip[k];
        break;
      }
      default:
        slots = (r >> 40) % 2 ? nextHarmonicGrid(slots_ + 1) : std::max(nextHarmonicGrid(static_cast<int>(count)), slots_ / 2);
        if (slots == slots_)
          continue;
        break;
      }
      const double score = arrange(strokes, a, slots, table, penUpWeight, true);
      if (score > score_ + 0.002) { // only for a real gain
        score_ = score;
        order_ = std::move(a.order);
        flip_ = std::move(a.flip);
        slots_ = slots;
      }
    }
  }
  // The arrangement then holds for as long as the picture has this many
  // strokes, so the tone is steady.
  arrange(strokes, {order_, flip_}, slots_, table, penUpWeight, false);
}

} // namespace cs

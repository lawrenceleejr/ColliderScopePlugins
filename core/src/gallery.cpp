#include "cscore/gallery.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>

#include "cscore/script.h"
#include "cscore/surface.h"

namespace cs {

namespace {

constexpr double kPi = std::numbers::pi;

double clamp01(double u) { return std::clamp(u, 0.0, 1.0); }
double ease(double u) {
  u = clamp01(u);
  return u * u * (3.0 - 2.0 * u);
}

Stroke circle(Vec2 c, double r, int n = 48) {
  Stroke s(static_cast<std::size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    const double a = 2.0 * kPi * i / n;
    s[static_cast<std::size_t>(i)] = {c.x + r * std::cos(a), c.y + r * std::sin(a)};
  }
  s.back() = s.front();
  return s;
}

Stroke dot(Vec2 p) { return {p, p}; }

// Everything a scene needs while it draws.
struct Ctx {
  const GalleryParams& p;
  double age, energy;
  std::uint64_t seed;
  double progress;   // 0..1 through the form time
  double t;          // age * speed: the clock that drives motion
  std::uint64_t frame; // changes 30 times a second of motion: for flicker
  std::vector<Stroke>& out;

  View view(double reach) const {
    Camera c = p.camera;
    c.azimuth += p.spin * age;
    return View(c, reach);
  }
};

Vec2 flat(const View& v, Vec3 q) {
  const Vec3 s = v.project(q);
  return {s.x, s.y};
}

// ------------------------------------------------------------ tracking planes

void trackingPlanes(Ctx& c) {
  Rng rng(c.seed);
  const View v = c.view(0.88);
  constexpr double scale = 1.05, half = 0.33; // drawn large; the final fit brings it inside the screen
  auto place = [&](Vec3 q) { return flat(v, {q.x * scale, q.y * scale, q.z * scale}); };
  std::array<double, 6> planes{};
  for (int k = 0; k < 6; ++k) {
    planes[static_cast<std::size_t>(k)] = -0.55 + 0.22 * k;
    const double x = planes[static_cast<std::size_t>(k)];
    c.out.push_back({place({x, -half, -half}), place({x, half, -half}), place({x, half, half}), place({x, -half, half}),
                     place({x, -half, -half})});
  }
  const int tracks = 1 + static_cast<int>(c.energy * 2.99);
  const double front = -0.75 + 1.5 * c.progress; // how far the particle has got
  for (int n = 0; n < tracks; ++n) {
    const double y0 = rng.uniform(-0.15, 0.15), z0 = rng.uniform(-0.15, 0.15);
    const double dy = rng.uniform(-0.15, 0.15), dz = rng.uniform(-0.15, 0.15), bend = rng.uniform(-0.3, 0.3);
    auto at = [&](double x) {
      const double u = x + 0.75;
      return Vec3{x, y0 + dy * u + 0.5 * bend * u * u, z0 + dz * u};
    };
    for (double x : planes) {
      const Vec3 h = at(x);
      const double ey = rng.normal(0.0, 0.006), ez = rng.normal(0.0, 0.006); // measurement scatter
      if (x > front)
        continue;
      constexpr double arm = 0.022;
      c.out.push_back({place({x, h.y + ey - arm, h.z + ez}), place({x, h.y + ey + arm, h.z + ez})});
      c.out.push_back({place({x, h.y + ey, h.z + ez - arm}), place({x, h.y + ey, h.z + ez + arm})});
    }
    if (c.progress >= 1.0) { // reconstructed: the fitted track through the hits
      Stroke s;
      for (int i = 0; i <= 24; ++i)
        s.push_back(place(at(-0.75 + 1.5 * i / 24.0)));
      c.out.push_back(std::move(s));
    }
  }
}

// --------------------------------------------------------------- wire chamber

void wireChamber(Ctx& c) {
  Rng rng(c.seed);
  constexpr std::array<double, 4> rows{0.39, 0.13, -0.13, -0.39};
  constexpr double pitch = 0.14;
  if (c.p.detail > 0.3)
    c.out.push_back({{-0.8, -0.55}, {0.8, -0.55}, {0.8, 0.55}, {-0.8, 0.55}, {-0.8, -0.55}});
  const int tracks = 1 + (c.energy > 0.6 ? 1 : 0);
  struct Line {
    double x0, m;
  };
  std::vector<Line> lines;
  for (int n = 0; n < tracks; ++n)
    lines.push_back({rng.uniform(-0.4, 0.4), rng.uniform(-0.8, 0.8)});
  for (std::size_t r = 0; r < rows.size(); ++r) {
    const double y = rows[r];
    const double rowShown = clamp01(c.progress * 4.0 - static_cast<double>(r)); // rows fire top to bottom
    for (int i = 0; i < 11; ++i) {
      const double x = -0.7 + pitch * i + (r % 2 ? pitch / 2 : 0.0);
      if (x > 0.75)
        continue;
      c.out.push_back(dot({x, y}));
      for (const Line& l : lines) {
        const double d = std::abs(x - (l.x0 + l.m * y)) / std::sqrt(1.0 + l.m * l.m);
        if (d < 0.5 * pitch * 1.05 && rowShown > 0.0)
          c.out.push_back(circle({x, y}, std::max(0.006, d * ease(rowShown)), 32)); // the drift circle grows
      }
    }
  }
  if (c.progress >= 1.0)
    for (const Line& l : lines)
      c.out.push_back({{l.x0 + l.m * 0.55, 0.55}, {l.x0 - l.m * 0.55, -0.55}});
}

// ------------------------------------------------------------- shower

void shower(Ctx& c) {
  Rng rng(c.seed);
  constexpr double left = -0.8, right = 0.8, top = 0.5;
  c.out.push_back({{left, -top}, {right, -top}, {right, top}, {left, top}, {left, -top}});
  if (c.p.detail > 0.3) // sampling layers
    for (int k = 1; k < 8; ++k)
      c.out.push_back({{left + 0.2 * k, -top}, {left + 0.2 * k, top}});
  const int generations = 3 + static_cast<int>(std::lround(4.0 * c.energy));
  const double grown = c.progress * generations; // generations shown, the last one part grown
  const double y0 = rng.uniform(-0.15, 0.15);
  // The whole cascade as one stroke that goes down each branch and back:
  // no jumps, and every line is drawn twice, so it is bright.
  Stroke tour{{-0.92, y0}, {left, y0}};
  auto grow = [&](auto&& self, Vec2 from, double angle, double length, int generation) -> void {
    const double delta = rng.uniform(0.08, 0.3) * (1.0 + 0.25 * generation), shrink = rng.uniform(0.75, 0.9);
    const double part = clamp01(grown - generation);
    if (part <= 0.0)
      return;
    Vec2 to{from.x + length * part * std::cos(angle), from.y + length * part * std::sin(angle)};
    to.x = std::min(to.x, right - 0.01);
    to.y = std::clamp(to.y, -top + 0.01, top - 0.01);
    tour.push_back(to);
    if (part >= 1.0 && generation + 1 < generations) {
      self(self, to, angle + delta, length * shrink, generation + 1);
      tour.push_back(to);
      self(self, to, angle - delta, length * shrink, generation + 1);
      tour.push_back(to);
    }
    tour.push_back(from);
  };
  grow(grow, {left, y0}, rng.normal(0.0, 0.05), 0.26, 0);
  c.out.push_back(std::move(tour));
}

// ------------------------------------------------------------- Cherenkov

void cherenkov(Ctx& c) {
  Rng rng(c.seed), flicker(c.seed ^ (c.frame * 0x9e3779b97f4a7c15ULL));
  const View v = c.view(0.86);
  auto place = [&](Vec2 q) { return flat(v, {q.x, 0.0, q.y}); }; // an upright plane, facing the camera
  constexpr double half = 0.6;
  c.out.push_back({place({-half, -half}), place({half, -half}), place({half, half}), place({-half, half}), place({-half, -half})});
  if (c.p.detail > 0.6) // the photosensors
    for (int i = 0; i < 7; ++i)
      for (int j = 0; j < 7; ++j)
        c.out.push_back(dot(place({-0.48 + 0.16 * i, -0.48 + 0.16 * j})));
  const int rings = 1 + static_cast<int>(std::lround(c.energy * 4.0));
  for (int n = 0; n < rings; ++n) {
    const Vec2 centre{rng.uniform(-0.3, 0.3), rng.uniform(-0.3, 0.3)};
    const double radius = rng.uniform(0.14, 0.3) * ease(c.progress); // the cone of light opens
    const bool electron = rng.uniform() < 0.5; // electrons scatter: a fuzzy ring; muons a sharp one
    Stroke ring;
    for (int i = 0; i <= 64; ++i) {
      const double a = 2.0 * kPi * i / 64.0;
      const double r = std::max(0.0, radius + (electron && i < 64 ? flicker.normal(0.0, 0.01) : 0.0));
      ring.push_back(place({centre.x + r * std::cos(a), centre.y + r * std::sin(a)}));
    }
    ring.back() = ring.front();
    c.out.push_back(std::move(ring));
  }
}

// ------------------------------------------------------------- spark chamber

void sparkChamber(Ctx& c) {
  Rng rng(c.seed), flicker(c.seed ^ (c.frame * 0x9e3779b97f4a7c15ULL));
  constexpr int plates = 8;
  auto plateY = [](int k) { return 0.6 - 1.2 * k / (plates - 1); };
  for (int k = 0; k < plates; ++k)
    c.out.push_back({{-0.55, plateY(k)}, {0.55, plateY(k)}});
  const int tracks = 1 + (c.energy > 0.5 ? 1 : 0) + (c.energy > 0.85 ? 1 : 0);
  for (int n = 0; n < tracks; ++n) {
    const double x0 = rng.uniform(-0.3, 0.3), m = rng.uniform(-0.4, 0.4);
    for (int k = 0; k + 1 < plates; ++k) {
      if (c.progress * (plates - 1) < k + 0.5)
        break; // sparks fire top to bottom
      Stroke spark;
      const double y1 = plateY(k), y2 = plateY(k + 1);
      for (int i = 0; i <= 5; ++i) {
        const double y = y1 + (y2 - y1) * i / 5.0;
        const double jag = (i == 0 || i == 5) ? 0.0 : flicker.normal(0.0, 0.018);
        spark.push_back({x0 + m * y + jag, y});
      }
      c.out.push_back(std::move(spark));
    }
  }
}

// ------------------------------------------------------------- betatron

void betatron(Ctx& c) {
  Rng rng(c.seed);
  constexpr double radius = 0.55;
  if (c.p.detail > 0.3)
    c.out.push_back(circle({0.0, 0.0}, radius, 96)); // the design orbit
  const double tune = 3.0 + static_cast<double>(c.seed % 4) + rng.uniform(0.12, 0.38);
  const double amplitude = 0.04 + 0.06 * c.energy;
  const double phase = 2.0 * kPi * 0.5 * c.t;
  Stroke orbit;
  constexpr int turns = 3, steps = 3 * 160;
  for (int i = 0; i <= steps; ++i) {
    const double theta = 2.0 * kPi * turns * i / steps;
    const double r = radius + amplitude * std::cos(tune * theta + phase);
    orbit.push_back({r * std::cos(theta), r * std::sin(theta)});
  }
  c.out.push_back(std::move(orbit));
  // Phase space, turn by turn: the beam's position and angle land on an ellipse.
  const double fraction = tune - std::floor(tune);
  const int points = 1 + static_cast<int>(39.0 * c.progress);
  for (int k = 0; k < points; ++k) {
    const double a = 2.0 * kPi * fraction * k + phase;
    c.out.push_back(dot({0.15 * std::cos(a), 0.075 * std::sin(a)}));
  }
}

// ------------------------------------------------------------- cyclotron

void cyclotron(Ctx& c) {
  constexpr double gap = 0.03, dee = 0.8;
  for (int side : {1, -1}) { // the two dees: a half-disc each, gap between
    Stroke d;
    for (int i = 0; i <= 40; ++i) {
      const double a = -0.5 * kPi + kPi * i / 40.0;
      d.push_back({side * (gap + dee * std::cos(a)), dee * std::sin(a)});
    }
    d.push_back(d.front());
    c.out.push_back(std::move(d));
  }
  const double first = 0.07 + 0.08 * (1.0 - c.p.detail); // more detail: more, tighter turns
  int last = 0;
  while (first * std::sqrt(last + 2.0) < 0.72)
    ++last;
  const int total = last + 8; // the turns, then a pause after extraction
  const int shown = c.p.speed > 0.0 ? static_cast<int>(std::fmod(c.age * c.p.speed * 12.0, total)) : last;
  Stroke path{{0.0, 0.0}};
  Vec2 end{0.0, 0.0};
  for (int n = 0; n <= std::min(shown, last); ++n) {
    const double r = first * std::sqrt(n + 1.0); // each kick at the gap raises the energy, and so the radius
    const double start = n % 2 ? 0.5 * kPi : -0.5 * kPi;
    for (int i = 0; i <= 24; ++i) {
      const double a = start + kPi * i / 24.0;
      end = {r * std::cos(a) + (n % 2 ? -gap : gap), r * std::sin(a)};
      path.push_back(end);
    }
  }
  if (shown > last) // extracted: off along the tangent
    path.push_back({end.x - 0.25, end.y > 0 ? 0.78 : -0.78});
  c.out.push_back(std::move(path));
}

// ------------------------------------------------------------- bunch crossing

void bunchCrossing(Ctx& c) {
  c.out.push_back({{-0.85, 0.12}, {0.85, 0.12}});
  c.out.push_back({{-0.85, -0.12}, {0.85, -0.12}});
  constexpr double spacing = 0.5, span = 2.0;
  const double travel = 0.4 * c.t;
  auto bunch = [&](double x) {
    Stroke s;
    for (int i = 0; i <= 24; ++i) {
      const double a = 2.0 * kPi * i / 24.0;
      s.push_back({x + 0.07 * std::cos(a), 0.035 * std::sin(a)});
    }
    s.back() = s.front();
    return s;
  };
  for (int k = 0; k < 4; ++k) {
    const double raw = std::fmod(k * spacing + travel, span) - 1.0; // -1 .. 1
    if (std::abs(raw) < 0.78) {
      c.out.push_back(bunch(raw));  // one train moving right
      c.out.push_back(bunch(-raw)); // the other moving left
    }
    if (std::abs(raw) < 0.06) { // they meet in the middle: a collision
      const auto crossing = static_cast<std::uint64_t>(std::floor((k * spacing + travel) / span));
      Rng rng(c.seed + crossing * 7919 + static_cast<std::uint64_t>(k));
      const double strength = 1.0 - std::abs(raw) / 0.06;
      const int spokes = 5 + static_cast<int>(c.energy * 7.0);
      for (int s = 0; s < spokes; ++s) {
        const double a = rng.uniform(0.0, 2.0 * kPi), l = rng.uniform(0.15, 0.45) * strength;
        c.out.push_back({{0.0, 0.0}, {l * std::cos(a), l * std::sin(a)}});
      }
    }
  }
}

// ------------------------------------------------------------- flux tube

void fluxTube(Ctx& c) {
  const double cycle = 2.4;
  const double u = c.p.speed > 0.0 ? std::fmod(c.t, cycle) / cycle : 0.5;
  const double width = 0.06;
  auto tube = [&](double a, double b) {
    c.out.push_back({{a, width}, {b, width}});
    c.out.push_back({{a, -width}, {b, -width}});
    if (c.p.detail > 0.5) // the colour field inside
      c.out.push_back({{a, 0.0}, {b, 0.0}});
  };
  if (u < 0.6) { // stretching: the energy stored grows with the length
    const double d = 0.1 + 0.55 * ease(u / 0.6);
    tube(-d, d);
    c.out.push_back(circle({-d, 0.0}, 0.05, 24));
    c.out.push_back(circle({d, 0.0}, 0.05, 24));
  } else { // snapped: a new quark and antiquark from the vacuum, two mesons fly apart
    const double s = ease((u - 0.6) / 0.4);
    const double outer = 0.65 + 0.2 * s, inner = 0.06 + 0.25 * s;
    tube(-outer, -inner);
    tube(inner, outer);
    for (double x : {-outer, outer})
      c.out.push_back(circle({x, 0.0}, 0.05, 24));
    for (double x : {-inner, inner})
      c.out.push_back(circle({x, 0.0}, 0.04, 24));
  }
}

// ------------------------------------------------------------- Higgs potential

void higgsPotential(Ctx& c) {
  static thread_local Expression hat;
  if (!hat.valid())
    hat.compile("1.2*((r/1.5)^2 - 1)^2 - 0.8");
  SurfaceParams s;
  s.range = 1.9; // the brim stays below the peak: a hat, not a bowl
  s.height = 1.0;
  s.lines = 10 + static_cast<int>(10.0 * c.p.detail);
  s.hidden = true;
  s.camera = c.p.camera;
  const double spin = c.p.spin * c.age;
  for (Stroke& stroke : surfaceStrokes(hat, s, 0.0, spin))
    c.out.push_back(std::move(stroke));
  // The ball, rolling round the bottom of the valley.
  const double theta = 2.0 * kPi * 0.35 * c.t;
  const Vec2 ball = surfaceScreenPoint(s, 1.5 * std::cos(theta), 1.5 * std::sin(theta), -0.8 + 0.07, spin);
  c.out.push_back(circle(ball, 0.035, 20));
}

// ------------------------------------------------------------- neutrino

void neutrino(Ctx& c) {
  constexpr double left = -0.8, right = 0.8, bottom = -0.45, top = 0.45;
  c.out.push_back({{left, top}, {left, bottom}, {right, bottom}}); // axes: probability up, L/E along
  if (c.p.detail > 0.5)
    for (int k = 1; k <= 4; ++k)
      c.out.push_back({{left, bottom + (top - bottom) * k / 4.0}, {left - 0.03, bottom + (top - bottom) * k / 4.0}});
  const int from = c.energy < 0.34 ? 0 : c.energy < 0.67 ? 1 : 2; // velocity picks the flavour it starts as
  const double reach = 1000.0 + 9000.0 * c.p.detail;             // km/GeV across the plot
  constexpr int steps = 480;
  const double drawn = c.progress; // the curves are drawn out along the baseline
  for (int to = 0; to < 3; ++to) {
    Stroke curve;
    for (int i = 0; i <= static_cast<int>(steps * drawn); ++i) {
      const double u = static_cast<double>(i) / steps;
      curve.push_back({left + (right - left) * u, bottom + (top - bottom) * neutrinoProbability(from, to, reach * u)});
    }
    if (curve.size() >= 2)
      c.out.push_back(std::move(curve));
  }
  // A marker sweeping along the baseline, with where each flavour stands.
  const double u = c.p.speed > 0.0 ? std::fmod(0.12 * c.t, 1.0) * drawn : 0.5 * drawn;
  const double x = left + (right - left) * u;
  c.out.push_back({{x, bottom}, {x, top}});
  for (int to = 0; to < 3; ++to)
    c.out.push_back(circle({x, bottom + (top - bottom) * neutrinoProbability(from, to, reach * u)}, 0.018, 16));
}

// ------------------------------------------------------------- Eightfold Way

void eightfoldWay(Ctx& c) {
  struct Particle {
    double i3, y;
    const char* name;
  };
  static const Particle octet[] = {{-0.5, 1, "n"},      {0.5, 1, "p"},     {-1, 0, "\\Sigma^-"}, {0, 0, "\\Sigma^0"},
                                   {0, 0, "\\Lambda"}, {1, 0, "\\Sigma^+"}, {-0.5, -1, "\\Xi^-"}, {0.5, -1, "\\Xi^0"}};
  static const Particle decuplet[] = {{-1.5, 1, "\\Delta^-"},    {-0.5, 1, "\\Delta^0"},   {0.5, 1, "\\Delta^+"},
                                      {1.5, 1, "\\Delta^{++}"},  {-1, 0, "\\Sigma^{*-}"},  {0, 0, "\\Sigma^{*0}"},
                                      {1, 0, "\\Sigma^{*+}"},    {-0.5, -1, "\\Xi^{*-}"}, {0.5, -1, "\\Xi^{*0}"},
                                      {0, -2, "\\Omega^-"}};
  const bool big = c.energy > 0.5; // a hard note: the decuplet
  const std::span<const Particle> set = big ? std::span<const Particle>(decuplet) : std::span<const Particle>(octet);
  const double unit = big ? 0.36 : 0.42, shift = big ? 0.22 : 0.0;
  auto at = [&](const Particle& q) { return Vec2{unit * q.i3, unit * q.y * std::sqrt(3.0) / 2.0 + shift}; };
  const auto shown = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(c.progress * static_cast<double>(set.size()))));
  for (std::size_t i = 0; i < std::min(shown, set.size()); ++i) {
    const Vec2 p = at(set[i]);
    const bool second = !big && i == 4; // the Lambda shares the centre with the Sigma0
    c.out.push_back(circle(p, second ? 0.055 : 0.03, 20));
    if (c.p.detail >= 0.5) {
      const ScriptLayout label = layoutScript(set[i].name);
      for (const Stroke& s : label.strokes) {
        Stroke placed;
        for (const Vec2& q : s)
          placed.push_back({p.x + 0.05 + 0.08 * (q.x - label.left), p.y + (second ? -0.11 : 0.04) + 0.08 * q.y});
        c.out.push_back(std::move(placed));
      }
    }
  }
  if (c.progress >= 1.0) { // the edges of the multiplet
    Stroke edge;
    const int corners[] = {0, 1, 5, 7, 6, 2, 0};
    const int triangle[] = {0, 3, 9, 0};
    for (int k : big ? std::span<const int>(triangle) : std::span<const int>(corners))
      edge.push_back(at(set[static_cast<std::size_t>(k)]));
    c.out.push_back(std::move(edge));
  }
}

} // namespace

const char* sceneName(Scene scene) noexcept {
  static const char* const names[kSceneCount] = {"Tracking planes", "Wire chamber",   "Calorimeter shower", "Cherenkov rings",
                                                  "Spark chamber",   "Betatron",       "Cyclotron",          "Bunch crossings",
                                                  "Flux tube",       "Higgs potential", "Neutrino oscillation", "Eightfold Way"};
  const int i = static_cast<int>(scene);
  return i >= 0 && i < kSceneCount ? names[i] : "";
}

double neutrinoProbability(int from, int to, double kmPerGeV) noexcept {
  // PMNS mixing, normal ordering, CP phase 0.
  const double t12 = 33.4 * kPi / 180.0, t23 = 49.0 * kPi / 180.0, t13 = 8.6 * kPi / 180.0;
  const double s12 = std::sin(t12), c12 = std::cos(t12), s23 = std::sin(t23), c23 = std::cos(t23), s13 = std::sin(t13),
               c13 = std::cos(t13);
  const double u[3][3] = {{c12 * c13, s12 * c13, s13},
                          {-s12 * c23 - c12 * s23 * s13, c12 * c23 - s12 * s23 * s13, s23 * c13},
                          {s12 * s23 - c12 * c23 * s13, -c12 * s23 - s12 * c23 * s13, c23 * c13}};
  const double dm2[3][3] = {{0.0, -7.4e-5, -2.5e-3}, {7.4e-5, 0.0, -2.5e-3 + 7.4e-5}, {2.5e-3, 2.5e-3 - 7.4e-5, 0.0}};
  from = std::clamp(from, 0, 2);
  to = std::clamp(to, 0, 2);
  double p = from == to ? 1.0 : 0.0;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < i; ++j) {
      const double phase = 1.267 * dm2[i][j] * kmPerGeV;
      p -= 4.0 * u[from][i] * u[to][i] * u[from][j] * u[to][j] * std::sin(phase) * std::sin(phase);
    }
  return std::clamp(p, 0.0, 1.0);
}

std::vector<Stroke> sceneStrokes(const GalleryParams& params, double age, double energy, std::uint64_t seed) {
  std::vector<Stroke> out;
  const double progress = params.formMs > 0.0 ? clamp01(age / (params.formMs * 1e-3)) : 1.0;
  const double t = age * std::max(params.speed, 0.0);
  Ctx c{params, age, std::clamp(energy, 0.0, 1.0), seed, progress, t, static_cast<std::uint64_t>(t * 30.0), out};
  switch (params.scene) {
  case Scene::TrackingPlanes: trackingPlanes(c); break;
  case Scene::WireChamber: wireChamber(c); break;
  case Scene::Shower: shower(c); break;
  case Scene::Cherenkov: cherenkov(c); break;
  case Scene::SparkChamber: sparkChamber(c); break;
  case Scene::Betatron: betatron(c); break;
  case Scene::Cyclotron: cyclotron(c); break;
  case Scene::BunchCrossing: bunchCrossing(c); break;
  case Scene::FluxTube: fluxTube(c); break;
  case Scene::HiggsPotential: higgsPotential(c); break;
  case Scene::Neutrino: neutrino(c); break;
  case Scene::EightfoldWay: eightfoldWay(c); break;
  }
  // Whatever a scene drew, it stays on the screen.
  double reach = 0.0;
  for (const Stroke& s : out)
    for (const Vec2& p : s)
      reach = std::max(reach, std::hypot(p.x, p.y));
  if (reach > 0.9)
    for (Stroke& s : out)
      for (Vec2& p : s)
        p = {p.x * 0.9 / reach, p.y * 0.9 / reach};
  return out;
}

void GallerySource::birth(const Params& params, int note, int velocity, std::uint32_t serial) {
  seed_ = params.seed * 0x9e3779b97f4a7c15ULL;
  switch (params.seedMode) {
  case SceneSeed::Random:
    seed_ += 0x4000 + serial;
    break;
  case SceneSeed::PerKey:
    seed_ += static_cast<std::uint64_t>(note & 0x7f);
    break;
  case SceneSeed::Global:
    break;
  }
  energy_ = velocity / 127.0;
}

void GallerySource::build(const Params& params, int, double age, std::span<XY> table) {
  std::vector<Stroke> strokes = sceneStrokes(params, age, energy_, seed_);
  if (strokes.empty()) {
    std::fill(table.begin(), table.end(), XY{});
    return;
  }
  // Scenes move from frame to frame, so time is spread evenly along the picture.
  const Path path = strokesToPath(orderStrokes(std::move(strokes), {}));
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs

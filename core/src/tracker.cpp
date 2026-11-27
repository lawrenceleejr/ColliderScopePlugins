#include "cscore/tracker.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "cscore/rng.h"

namespace cs {

namespace {

constexpr double kPi = std::numbers::pi;

double clamp01(double u) { return std::clamp(u, 0.0, 1.0); }
double ease(double u) {
  u = clamp01(u);
  return u * u * (3.0 - 2.0 * u);
}
Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 mul(Vec3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
Vec3 lerp(Vec3 a, Vec3 b, double u) { return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u}; }
Vec3 unit(Vec3 a) {
  const double l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z) + 1e-12;
  return mul(a, 1.0 / l);
}

// A layer of a module, in the module's own frame: x along it, y across, z up.
struct Layer {
  enum Kind { Sensor, Pixels, Spacers, Hybrids, Board } kind;
  double hx, hy;  // half-sizes of one piece
  double cx, cy;  // centre of one piece; Spacers and Hybrids come as a mirrored pair
  double z;       // height when assembled
  bool pair;      // a second piece mirrored across y = 0 (or x = 0 for Boards)
};

// Schematics of the two module types, bottom layer first. 0.3 ~ 5 cm.
std::vector<Layer> moduleLayers(ModuleType type) {
  if (type == ModuleType::TwoS)
    return {{Layer::Sensor, 0.30, 0.30, 0.0, 0.0, 0.00, false},   // bottom strip sensor, 10 x 10 cm
            {Layer::Spacers, 0.30, 0.015, 0.0, 0.27, 0.02, true}, // the spacers that set the gap
            {Layer::Sensor, 0.30, 0.30, 0.0, 0.0, 0.04, false},   // top strip sensor
            {Layer::Hybrids, 0.30, 0.05, 0.0, 0.36, 0.02, true},  // front-end hybrids, with their chips
            {Layer::Board, 0.05, 0.30, 0.38, 0.0, 0.02, false}};  // service hybrid
  return {{Layer::Board, 0.32, 0.17, 0.0, 0.0, -0.02, false},     // base plate
          {Layer::Pixels, 0.30, 0.15, 0.0, 0.0, 0.00, false},     // macro-pixel sensor and its chips
          {Layer::Spacers, 0.30, 0.012, 0.0, 0.135, 0.02, true},
          {Layer::Sensor, 0.30, 0.15, 0.0, 0.0, 0.04, false},     // strip sensor, 5 x 10 cm
          {Layer::Hybrids, 0.30, 0.04, 0.0, 0.21, 0.02, true},
          {Layer::Board, 0.05, 0.15, 0.38, 0.0, 0.02, true}};     // power and readout hybrids, one at each end
}

// Draws the module with its basis (ex, ey, ez), centred at `centre`, scaled by `scale`.
void drawModule(ModuleType type, double explode, double detail, Vec3 centre, Vec3 ex, Vec3 ey, Vec3 ez, double scale,
                const View& view, std::vector<Stroke>& out) {
  auto place = [&](double x, double y, double z) {
    const Vec3 p = add(centre, mul(add(add(mul(ex, x), mul(ey, y)), mul(ez, z)), scale));
    const Vec3 s = view.project(p);
    return Vec2{s.x, s.y};
  };
  const std::vector<Layer> layers = moduleLayers(type);
  const double count = static_cast<double>(layers.size());
  for (std::size_t i = 0; i < layers.size(); ++i) {
    const Layer& l = layers[i];
    // Exploded, the layers stack up evenly, centred on the module.
    const double z = l.z + explode * (0.2 * (static_cast<double>(i) - 0.5 * (count - 1)) - l.z);
    auto rect = [&](double cx, double cy, double hx, double hy) {
      out.push_back({place(cx - hx, cy - hy, z), place(cx + hx, cy - hy, z), place(cx + hx, cy + hy, z),
                     place(cx - hx, cy + hy, z), place(cx - hx, cy - hy, z)});
    };
    for (int copy = 0; copy < (l.pair ? 2 : 1); ++copy) {
      const double sign = copy ? -1.0 : 1.0;
      const bool board = l.kind == Layer::Board;
      const double cx = board ? sign * l.cx : l.cx, cy = board ? l.cy : sign * l.cy;
      rect(cx, cy, l.hx, l.hy);
      if (detail <= 0.25)
        continue;
      switch (l.kind) {
      case Layer::Sensor: { // strips, in two rows either side of the middle
        const int strips = 4 + static_cast<int>(8.0 * detail);
        out.push_back({place(cx - l.hx, cy, z), place(cx + l.hx, cy, z)});
        for (int s = 1; s < strips; ++s) {
          const double x = cx - l.hx + 2.0 * l.hx * s / strips;
          out.push_back({place(x, cy - l.hy, z), place(x, cy + l.hy, z)});
        }
        break;
      }
      case Layer::Pixels: { // a grid of macro-pixels
        const int across = 2 + static_cast<int>(4.0 * detail), along = 2 * across;
        for (int s = 1; s < along; ++s) {
          const double x = cx - l.hx + 2.0 * l.hx * s / along;
          out.push_back({place(x, cy - l.hy, z), place(x, cy + l.hy, z)});
        }
        for (int s = 1; s < across; ++s) {
          const double y = cy - l.hy + 2.0 * l.hy * s / across;
          out.push_back({place(cx - l.hx, y, z), place(cx + l.hx, y, z)});
        }
        break;
      }
      case Layer::Hybrids: { // the readout chips along each hybrid
        const int chips = 8;
        for (int s = 0; s < chips; ++s) {
          const double x = cx - l.hx + 2.0 * l.hx * (s + 0.5) / chips;
          rect(x, cy, 0.6 * l.hx / chips, 0.5 * l.hy);
        }
        break;
      }
      case Layer::Spacers:
      case Layer::Board:
        break;
      }
    }
  }
}

// The outer tracker, in metres, scaled into the picture by `s`.
void drawTracker(double detail, double shown, const View& view, double s, std::vector<Stroke>& out) {
  // The beam runs along the picture's x axis.
  auto place = [&](double z, double x, double y) {
    const Vec3 q = view.project({z * s, x * s, y * s});
    return Vec2{q.x, q.y};
  };
  std::vector<Stroke> all;
  auto ring = [&](double z, double r) {
    Stroke ring;
    for (int i = 0; i <= 40; ++i) {
      const double a = 2.0 * kPi * i / 40.0;
      ring.push_back(place(z, r * std::cos(a), r * std::sin(a)));
    }
    all.push_back(std::move(ring));
  };
  const int staves = 2 + static_cast<int>(std::lround(6.0 * detail));
  for (double r : kBarrelRadii) {
    ring(-kOtBarrelHalfLength, r);
    ring(kOtBarrelHalfLength, r);
    for (int k = 0; k < staves; ++k) {
      const double a = 2.0 * kPi * (k + 0.5) / staves;
      all.push_back({place(-kOtBarrelHalfLength, r * std::cos(a), r * std::sin(a)),
                     place(kOtBarrelHalfLength, r * std::cos(a), r * std::sin(a))});
    }
  }
  const int spokes = static_cast<int>(std::lround(4.0 * detail));
  for (double side : {-1.0, 1.0})
    for (double z : kDiscZ) {
      ring(side * z, kDiscOuter);
      if (detail > 0.4)
        ring(side * z, kDiscInner);
      for (int k = 0; k < spokes; ++k) {
        const double a = 2.0 * kPi * k / spokes;
        all.push_back({place(side * z, kDiscInner * std::cos(a), kDiscInner * std::sin(a)),
                       place(side * z, kDiscOuter * std::cos(a), kDiscOuter * std::sin(a))});
      }
    }
  // Drawn in, barrel first, as `shown` goes from 0 to 1.
  const auto count = static_cast<std::size_t>(std::ceil(clamp01(shown) * static_cast<double>(all.size())));
  for (std::size_t i = 0; i < count; ++i)
    out.push_back(std::move(all[i]));
}

// One event of tracks, with a stub wherever a track crosses a layer or a disc.
int drawTracks(double energy, double shown, std::uint64_t seed, const View& view, double s, std::vector<Stroke>& out) {
  auto place = [&](Vec3 p) { // p in metres: x, y across the beam, z along it
    const Vec3 q = view.project({p.z * s, p.x * s, p.y * s});
    return Vec2{q.x, q.y};
  };
  Rng rng(seed);
  int stubs = 0;
  const int tracks = 3 + static_cast<int>(9.0 * energy);
  for (int n = 0; n < tracks; ++n) {
    const double pt = 1.0 / rng.uniform(0.1, 1.0); // 1-10 GeV
    const int charge = rng.uniform() < 0.5 ? -1 : 1;
    const double eta = rng.uniform(-2.2, 2.2), phi = rng.uniform(0.0, 2.0 * kPi);
    const double radius = pt / (0.3 * 3.8); // metres, in 3.8 T
    constexpr double step = 0.01;
    Vec3 p{};
    double heading = phi;
    Stroke track{place(p)};
    const int maxSteps = static_cast<int>(350.0 * clamp01(shown)); // flying out from the collision point
    for (int i = 0; i < maxSteps; ++i) {
      const Vec3 before = p;
      heading += charge * step / radius;
      p = {p.x + step * std::cos(heading), p.y + step * std::sin(heading), p.z + step * std::sinh(eta)};
      const double r0 = std::hypot(before.x, before.y), r1 = std::hypot(p.x, p.y);
      if (r1 > 1.12 || std::abs(p.z) > 2.7)
        break;
      track.push_back(place(p));
      bool hit = false;
      for (double r : kBarrelRadii)
        hit = hit || (r0 < r && r1 >= r && std::abs(p.z) < kOtBarrelHalfLength);
      for (double z : kDiscZ)
        hit = hit || ((std::abs(before.z) < z && std::abs(p.z) >= z) && r1 > kDiscInner && r1 < kDiscOuter);
      if (hit) { // a stub: a short segment along the track, drawn twice so it is bright
        const Vec3 d = unit({p.x - before.x, p.y - before.y, p.z - before.z});
        const Vec2 a = place({p.x - 0.03 * d.x, p.y - 0.03 * d.y, p.z - 0.03 * d.z});
        const Vec2 b = place({p.x + 0.03 * d.x, p.y + 0.03 * d.y, p.z + 0.03 * d.z});
        out.push_back({a, b, a, b});
        ++stubs;
      }
    }
    if (track.size() >= 2)
      out.push_back(std::move(track));
  }
  return stubs;
}

} // namespace

TrackerFrame trackerFrame(const TrackerParams& params, double age, double energy, std::uint64_t seed) {
  TrackerFrame frame;
  const double speed = std::clamp(params.speed, 0.05, 8.0);
  const double stageLength = 2.5; // seconds per stage at speed 1
  const double t = age * speed / stageLength; // in stages
  TrackerStage stage = params.stage;
  double within = 0.0;
  if (stage == TrackerStage::Auto) {
    const int index = std::min(3, static_cast<int>(t));
    stage = static_cast<TrackerStage>(1 + index);
    within = t - index; // through the stage, and beyond it for the last
  } else {
    within = stage == TrackerStage::Tracks ? t : 1.0; // a held stage sits at its end; Tracks keep coming
  }
  frame.stage = stage;

  Camera camera = params.camera;
  camera.azimuth += params.spin * age;
  const View view(camera, 0.88);
  // The module alone is seen from higher up, so its face shows; the camera
  // comes down to the tracker's level as the module flies into place.
  Camera above = camera;
  above.elevation = std::min(89.0, camera.elevation + 35.0);
  const View moduleView(above, 0.88);
  // The tracker, scaled so its far corners reach 0.85.
  const double s = 0.85 / std::hypot(kDiscZ[4], kDiscOuter);
  // The module that is shown: in layer 5, a quarter of the way along, turned to face out.
  const double where = 0.6, radius = kBarrelRadii[4], along = 0.3;
  const Vec3 home{along * s, radius * std::cos(where) * s, radius * std::sin(where) * s};
  const Vec3 outward{0.0, std::cos(where), std::sin(where)}, round{0.0, -std::sin(where), std::cos(where)};
  const double size = 0.05 / 0.3 * s; // 0.3 in module units is about 5 cm

  switch (stage) {
  case TrackerStage::Module:
  case TrackerStage::Auto:
    drawModule(params.module, 0.0, params.detail, {}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 1.8, moduleView, frame.strokes);
    break;
  case TrackerStage::Exploded:
    frame.explode = ease(within / 0.7);
    drawModule(params.module, frame.explode, params.detail, {}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, 1.8, moduleView, frame.strokes);
    break;
  case TrackerStage::Tracker: {
    // Put back together, then flown to its place as the tracker draws in round it.
    frame.explode = params.stage == TrackerStage::Auto ? 1.0 - ease(within / 0.35) : 0.0;
    frame.zoom = params.stage == TrackerStage::Auto ? ease((within - 0.35) / 0.55) : 1.0;
    const double z = frame.zoom;
    const Vec3 centre = lerp({}, home, z);
    const Vec3 ex = unit(lerp({1, 0, 0}, {1, 0, 0}, z)), ey = unit(lerp({0, 1, 0}, round, z)), ez = unit(lerp({0, 0, 1}, outward, z));
    const double scale = 1.8 + (size - 1.8) * z;
    drawTracker(params.detail * 0.6, z, view, s, frame.strokes);
    Camera between = camera;
    between.elevation = above.elevation + (camera.elevation - above.elevation) * z;
    const View travel(between, 0.88);
    for (int copy = 0; copy < (z > 0.95 ? 3 : 1); ++copy) // in place, drawn over three times: it stands out
      drawModule(params.module, frame.explode, z > 0.5 ? 0.0 : params.detail, centre, ex, ey, ez, scale, travel, frame.strokes);
    break;
  }
  case TrackerStage::Tracks: {
    drawTracker(params.detail * 0.5, 1.0, view, s, frame.strokes);
    // A new collision every stage-length; each flies out over the first part of it.
    const double clock = params.stage == TrackerStage::Auto ? within : t;
    const auto event = static_cast<std::uint64_t>(std::floor(clock));
    const double flight = params.speed > 0.0 ? ease((clock - std::floor(clock)) / 0.4) : 1.0;
    frame.stubs = drawTracks(energy, flight, seed + 0x51ED * event, view, s, frame.strokes);
    break;
  }
  }
  double reach = 0.0;
  for (const Stroke& st : frame.strokes)
    for (const Vec2& p : st)
      reach = std::max(reach, std::hypot(p.x, p.y));
  if (reach > 0.9)
    for (Stroke& st : frame.strokes)
      for (Vec2& p : st)
        p = {p.x * 0.9 / reach, p.y * 0.9 / reach};
  return frame;
}

void TrackerSource::birth(const Params& params, int note, int velocity, std::uint32_t serial) {
  seed_ = params.seed * 0x9e3779b97f4a7c15ULL;
  if (params.seedMode == 0)
    seed_ += 0x5000 + serial;
  else if (params.seedMode == 1)
    seed_ += static_cast<std::uint64_t>(note & 0x7f);
  energy_ = velocity / 127.0;
}

void TrackerSource::build(const Params& params, int, double age, std::span<XY> table) {
  frame_ = trackerFrame(params, age, energy_, seed_);
  if (frame_.strokes.empty()) {
    std::fill(table.begin(), table.end(), XY{});
    return;
  }
  const Path path = strokesToPath(orderStrokes(frame_.strokes, {}));
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs

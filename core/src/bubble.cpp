#include "cscore/bubble.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cs {

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kStep = 0.01;
} // namespace

BubbleTrack curlTrack(Vec2 from, double heading, double momentum, int charge, double field, double loss) {
  BubbleTrack track;
  track.charge = charge;
  Vec2 p = from;
  double mom = momentum;
  track.points.push_back(p);
  // Radius of curvature = momentum / field; the particle slows as it goes,
  // so the curve tightens into a spiral.
  for (int i = 0; i < 1200 && mom > 0.03; ++i) {
    heading += charge * (0.35 + 6.0 * field) * kStep / mom;
    p.x += kStep * std::cos(heading);
    p.y += kStep * std::sin(heading);
    if (std::hypot(p.x, p.y) > BubbleSource::kChamberRadius - 0.04)
      break;
    track.points.push_back(p);
    mom *= 1.0 - loss * kStep;
  }
  return track;
}

void BubbleSource::birth(const Params& params, int note, int velocity, std::uint32_t serial) {
  seed_ = params.seed * 0x9e3779b97f4a7c15ULL;
  switch (params.seedMode) {
  case BubbleSeed::Random:
    seed_ += 0x2000 + serial;
    break;
  case BubbleSeed::PerKey:
    seed_ += static_cast<std::uint64_t>(note & 0x7f);
    break;
  case BubbleSeed::Global:
    break;
  }
  velocity_ = velocity;
  noise_.reseed(seed_ ^ 0x5bd1e995ULL);
  makeTracks(params);
}

void BubbleSource::makeTracks(const Params& params) {
  Rng rng(seed_);
  // Velocity picks the event: a soft note is a slow electron curling up, a
  // hard one a fast beam particle that barely bends.
  type_ = params.type;
  if (type_ == BubbleType::Auto)
    type_ = velocity_ <= 40 ? BubbleType::Electron
                            : velocity_ <= 80 ? BubbleType::Pair : velocity_ <= 110 ? BubbleType::Decay : BubbleType::Beam;
  else if (type_ == BubbleType::Random)
    type_ = static_cast<BubbleType>(1 + rng.next() % 4);

  const double field = params.field;
  const int sign = rng.uniform() < 0.5 ? -1 : 1;
  tracks_.clear();
  switch (type_) {
  case BubbleType::Pair: {
    // A photon converts: an electron and a positron leave one point
    // together and curl opposite ways.
    const Vec2 vertex{rng.uniform(-0.3, 0.3), rng.uniform(-0.45, 0.0)};
    const double heading = rng.uniform(0.25 * kPi, 0.75 * kPi);
    const double momentum = rng.uniform(0.5, 0.9);
    tracks_.push_back(curlTrack(vertex, heading + 0.04, momentum, +1, field, 1.1));
    tracks_.push_back(curlTrack(vertex, heading - 0.04, momentum * rng.uniform(0.6, 1.0), -1, field, 1.1));
    break;
  }
  case BubbleType::Decay: {
    // A heavier particle crosses, then decays: the track kinks and the
    // lighter daughter curls away tighter.
    const double a = rng.uniform(0.0, 2.0 * kPi);
    const Vec2 from{0.8 * std::cos(a), 0.8 * std::sin(a)};
    const double heading = a + kPi + rng.uniform(-0.35, 0.35);
    BubbleTrack parent = curlTrack(from, heading, rng.uniform(2.0, 3.0), sign, field, 0.25);
    const std::size_t cut = std::min(parent.points.size() - 1, static_cast<std::size_t>(rng.uniform(55.0, 90.0)));
    parent.points.resize(cut + 1);
    const Vec2 end = parent.points.back(), before = parent.points[cut > 0 ? cut - 1 : 0];
    const double arriving = std::atan2(end.y - before.y, end.x - before.x);
    const double kink = (rng.uniform() < 0.5 ? -1.0 : 1.0) * rng.uniform(0.5, 1.1);
    tracks_.push_back(std::move(parent));
    tracks_.push_back(curlTrack(end, arriving + kink, rng.uniform(0.35, 0.6), sign, field, 1.3));
    break;
  }
  case BubbleType::Beam: {
    const double a = rng.uniform(0.0, 2.0 * kPi);
    const Vec2 from{0.85 * std::cos(a), 0.85 * std::sin(a)};
    tracks_.push_back(curlTrack(from, a + kPi + rng.uniform(-0.3, 0.3), rng.uniform(5.0, 8.0), sign, field, 0.05));
    break;
  }
  case BubbleType::Electron:
  case BubbleType::Auto:
  case BubbleType::Random: {
    const Vec2 from{rng.uniform(-0.45, 0.45), rng.uniform(-0.45, 0.45)};
    tracks_.push_back(curlTrack(from, rng.uniform(0.0, 2.0 * kPi), rng.uniform(0.7, 1.1), sign, field, 0.9));
    break;
  }
  }
  builtField_ = field;
  bubbleScale_.resize(256);
  for (float& s : bubbleScale_)
    s = static_cast<float>(rng.uniform(0.6, 1.4));
}

void BubbleSource::build(const Params& params, int, double age, std::span<XY> table) {
  if (params.field != builtField_)
    makeTracks(params); // the field moved: the same particles, bent anew

  std::size_t total = 0;
  for (const BubbleTrack& t : tracks_)
    total += t.points.size();
  const int count = std::clamp(params.bubbles, 4, 240);
  // The track appears along its length, as the particle crosses.
  const double formed = params.formMs > 0.0 ? std::clamp(age / (params.formMs * 1e-3), 0.0, 1.0) : 1.0;
  const int shown = std::max(2, static_cast<int>(std::lround(count * formed)));
  const double radius = std::min(params.maxRadius, 0.002 + params.growth * age);
  const double shiver = params.boil * (0.0015 + 0.25 * radius);

  // One stroke per bubble: a small ring. The beam jumps between them, pen
  // up, so the track reads as a row of dots.
  strokes_.clear();
  constexpr int kSides = 8;
  for (int b = 0; b < shown; ++b) {
    // Bubble b sits at an even spacing along all the tracks, in order.
    const double at = (b + 0.5) / count * static_cast<double>(total);
    std::size_t index = static_cast<std::size_t>(at), trackIndex = 0;
    while (trackIndex + 1 < tracks_.size() && index >= tracks_[trackIndex].points.size()) {
      index -= tracks_[trackIndex].points.size();
      ++trackIndex;
    }
    const BubbleTrack& track = tracks_[trackIndex];
    if (track.points.empty())
      continue;
    const Vec2 centre = track.points[std::min(index, track.points.size() - 1)];
    const double r = radius * bubbleScale_[static_cast<std::size_t>(b) % bubbleScale_.size()];
    const Vec2 c{centre.x + noise_.normal(0.0, shiver), centre.y + noise_.normal(0.0, shiver)};
    Stroke ring(kSides + 1);
    for (int k = 0; k <= kSides; ++k) {
      const double a = 2.0 * kPi * k / kSides;
      ring[static_cast<std::size_t>(k)] = {c.x + r * std::cos(a), c.y + r * std::sin(a)};
    }
    strokes_.push_back(std::move(ring));
  }
  // Bubbles are already in order along the track: keep it, so the picture
  // is drawn the way the particle flew.
  const Path path = strokesToPath(strokes_);
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs

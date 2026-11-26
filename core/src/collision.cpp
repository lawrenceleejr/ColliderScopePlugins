#include "cscore/collision.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cs {

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kStep = 0.012;
} // namespace

Stroke collisionTrackPath(const CollisionTrack& track, double field) {
  Stroke path;
  Vec2 p{};
  double heading = track.phi;
  const double curve = track.charge * (0.15 + 2.2 * field) / std::max(track.pt, 0.02); // 1 / radius
  const double edge = track.muon ? kMuonRadius : kTrackerRadius;
  double turned = 0.0;
  path.push_back(p);
  for (int i = 0; i < 400; ++i) {
    // Beyond the solenoid a muon is no longer bent.
    const double bend = std::hypot(p.x, p.y) < kTrackerRadius ? curve * kStep : 0.0;
    heading += bend;
    turned += std::abs(bend);
    p.x += kStep * std::cos(heading);
    p.y += kStep * std::sin(heading);
    path.push_back(p);
    if (std::hypot(p.x, p.y) >= edge || turned > 1.9 * kPi)
      break; // out of the tracker, or curled right round
  }
  return path;
}

void CollisionSource::addJet(Rng& rng, double phi, double energy) {
  // A spray of charged particles round the jet's axis, the more energetic nearer it.
  const int count = 3 + static_cast<int>(rng.uniform(0.0, 4.0) + 3.0 * energy);
  for (int i = 0; i < count; ++i)
    tracks_.push_back({energy * rng.uniform(0.6, 5.0), phi + rng.normal(0.0, 0.11), rng.uniform() < 0.5 ? -1 : 1, false});
  // And its energy in the calorimeter: a few towers, tallest on the axis.
  for (int k = -2; k <= 2; ++k) {
    const double fall = std::exp(-0.7 * k * k);
    towers_.push_back({phi + 0.07 * k, std::min(1.0, energy * fall * rng.uniform(0.25, 0.5)), false});
    towers_.push_back({phi + 0.07 * k + 0.02, std::min(1.0, energy * fall * rng.uniform(0.5, 1.0)), true});
  }
}

void CollisionSource::addSoft(Rng& rng, int count) {
  for (int i = 0; i < count; ++i)
    tracks_.push_back({rng.uniform(0.12, 0.7), rng.uniform(0.0, 2.0 * kPi), rng.uniform() < 0.5 ? -1 : 1, false});
}

void CollisionSource::birth(const Params& params, int note, int velocity, std::uint32_t serial) {
  std::uint64_t seed = params.seed * 0x9e3779b97f4a7c15ULL;
  switch (params.seedMode) {
  case CollisionSeed::Random:
    seed += 0x3000 + serial;
    break;
  case CollisionSeed::PerKey:
    seed += static_cast<std::uint64_t>(note & 0x7f);
    break;
  case CollisionSeed::Global:
    break;
  }
  Rng rng(seed);
  // Velocity picks the collision: the harder the note, the rarer the event.
  event_ = params.event;
  if (event_ == CollisionEvent::Auto)
    event_ = velocity <= 35   ? CollisionEvent::MinimumBias
             : velocity <= 65 ? CollisionEvent::Dijet
             : velocity <= 90 ? CollisionEvent::ZToMuons
             : velocity <= 110 ? CollisionEvent::Multijet : CollisionEvent::FourMuons;
  else if (event_ == CollisionEvent::Random)
    event_ = static_cast<CollisionEvent>(1 + rng.next() % 5);

  tracks_.clear();
  towers_.clear();
  const double axis = rng.uniform(0.0, 2.0 * kPi);
  switch (event_) {
  case CollisionEvent::Dijet: // two jets, back to back
    addJet(rng, axis, rng.uniform(0.7, 1.0));
    addJet(rng, axis + kPi + rng.normal(0.0, 0.12), rng.uniform(0.6, 1.0));
    addSoft(rng, 4);
    break;
  case CollisionEvent::ZToMuons: // two muons, back to back, and little else
    tracks_.push_back({rng.uniform(12.0, 20.0), axis, +1, true});
    tracks_.push_back({rng.uniform(12.0, 20.0), axis + kPi + rng.normal(0.0, 0.1), -1, true});
    addSoft(rng, 5);
    break;
  case CollisionEvent::Multijet: { // a busy event: four to six jets, perhaps a muon
    const int jets = 4 + static_cast<int>(rng.next() % 3);
    for (int j = 0; j < jets; ++j)
      addJet(rng, axis + 2.0 * kPi * j / jets + rng.normal(0.0, 0.25), rng.uniform(0.4, 0.9));
    if (rng.uniform() < 0.6)
      tracks_.push_back({rng.uniform(8.0, 16.0), rng.uniform(0.0, 2.0 * kPi), rng.uniform() < 0.5 ? -1 : 1, true});
    addSoft(rng, 3);
    break;
  }
  case CollisionEvent::FourMuons: // the golden channel: four muons from one point
    for (int k = 0; k < 4; ++k)
      tracks_.push_back({rng.uniform(6.0, 18.0), axis + 0.5 * kPi * k + rng.normal(0.0, 0.35), k % 2 ? -1 : 1, true});
    addSoft(rng, 4);
    break;
  case CollisionEvent::MinimumBias:
  case CollisionEvent::Auto:
  case CollisionEvent::Random:
    addSoft(rng, 7 + static_cast<int>(rng.next() % 5));
    for (int k = 0; k < 4; ++k)
      towers_.push_back({rng.uniform(0.0, 2.0 * kPi), rng.uniform(0.05, 0.2), rng.uniform() < 0.5});
    break;
  }
  // Pile-up: soft tracks from the other collisions in the same crossing.
  addSoft(rng, static_cast<int>(std::lround(std::clamp(params.pileup, 0.0, 1.0) * 14.0)));
  grid_.reset();
}

void CollisionSource::build(const Params& params, int, double age, std::span<XY> table) {
  // Everything flies out from the collision point at one speed.
  const double reached = params.formMs > 0.0 ? std::min(1.0, age / (params.formMs * 1e-3)) * kMuonRadius : kMuonRadius;
  const bool formed = reached >= kMuonRadius;
  strokes_.clear();
  for (const CollisionTrack& track : tracks_) {
    Stroke path = collisionTrackPath(track, params.field);
    if (!formed) {
      const auto shown = static_cast<std::size_t>(reached / kStep) + 1;
      if (shown < path.size())
        path.resize(std::max<std::size_t>(shown, 2));
    }
    strokes_.push_back(std::move(path));
  }
  for (const CollisionTower& tower : towers_) {
    // A tower is a radial bar, as long as its energy, in its layer of the calorimeter.
    const double inner = tower.hadronic ? kHcalRadius : kEcalRadius;
    const double depth = (tower.hadronic ? kCalorimeterEdge - kHcalRadius : kHcalRadius - kEcalRadius) * 0.95;
    if (reached < inner)
      continue;
    const double outer = std::min(inner + depth * tower.energy, std::max(inner + 0.004, reached));
    const double c = std::cos(tower.phi), s = std::sin(tower.phi);
    strokes_.push_back({{inner * c, inner * s}, {outer * c, outer * s}});
  }
  if (params.detector) {
    Stroke ring(97);
    for (std::size_t i = 0; i < ring.size(); ++i) {
      const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(ring.size() - 1);
      ring[i] = {kCalorimeterEdge * std::cos(a), kCalorimeterEdge * std::sin(a)};
    }
    ring.back() = ring.front();
    strokes_.push_back(std::move(ring));
  }
  if (strokes_.empty()) {
    std::fill(table.begin(), table.end(), XY{});
    return;
  }
  if (params.voice.timing == Timing::Harmonic) {
    // While the event is still forming, strokes keep their order; once it
    // has formed the arrangement is solved, once, and then holds.
    grid_.setSolveOrder(formed);
    grid_.build(strokes_, table);
    return;
  }
  const Path path = strokesToPath(orderStrokes(strokes_, {}));
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs

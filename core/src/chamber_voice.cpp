#include "cscore/chamber_voice.h"

#include <algorithm>
#include <cmath>

#include "cscore/pitch.h"

namespace cs {

void ChamberVoice::prepare(double sampleRate) {
  sampleRate_ = sampleRate;
  env_.setSampleRate(sampleRate);
  env_.reset();
  osc_.reset();
  active_ = false;
  noteCount_ = 0;
  setParams(params_);
}

void ChamberVoice::setParams(const ChamberParams& params) {
  params_ = params;
  env_.setAttack(params.attackMs * 1e-3);
  env_.setRelease(params.releaseMs * 1e-3);
  gain_ = static_cast<float>(std::clamp(params.size, 0.0, 1.0) * std::pow(10.0, std::min(params.gainDb, 0.0) / 20.0));
}

void ChamberVoice::noteOn(int note, int velocity) {
  std::uint64_t seed = params_.seed * 0x9e3779b97f4a7c15ULL;
  switch (params_.seedMode) {
  case SeedMode::Random:
    seed += 0x1000 + noteCount_;
    break;
  case SeedMode::PerKey:
    seed += static_cast<std::uint64_t>(note & 0x7f);
    break;
  case SeedMode::Global:
    break;
  }
  ++noteCount_;
  Rng rng(seed);

  TrackType type = typeForVelocity(velocity);
  switch (params_.type) {
  case TypeMode::Auto:
    break;
  case TypeMode::Alpha:
    type = TrackType::Alpha;
    break;
  case TypeMode::Muon:
    type = TrackType::Muon;
    break;
  case TypeMode::Electron:
    type = TrackType::Electron;
    break;
  case TypeMode::Random:
    type = static_cast<TrackType>(rng.next() % 3);
    break;
  }
  track_ = makeTrack(type, rng);
  noise_.reseed(seed ^ 0xd1b54a32d192ed03ULL);

  const double noteHz = midiToHz(note);
  if (params_.wall == WallMode::Drone) {
    periodHz_ = midiToHz(params_.wallRoot);
    harmonic_ = nearestHarmonic(noteHz, periodHz_);
  } else {
    periodHz_ = noteHz;
    harmonic_ = 1;
  }

  if (!active_)
    osc_.reset(); // a note from silence starts its picture at the start
  ageSamples_ = 0;
  active_ = true;
  env_.gate(true);
  rebuild();
}

void ChamberVoice::noteOff() { env_.gate(false); }

void ChamberVoice::rebuild() {
  sinceRebuild_ = 0;
  const double age = static_cast<double>(ageSamples_) / sampleRate_;
  const AgeingParams ageing{params_.diffusionRate, params_.drift, params_.condenseMs * 1e-3};
  const Vec2 wallEnd = wall_.back();

  auto agedTrack = [&](Vec2 from) {
    strokes_.clear();
    ageTrack(track_, age, ageing, noise_, strokes_);
    return strokesToPath(orderStrokes(std::move(strokes_), from));
  };

  segments_.clear();
  switch (params_.wall) {
  case WallMode::Off:
    segments_.push_back({agedTrack({}), 1.0});
    break;
  case WallMode::Shared: {
    // Wall and track in one path: each gets time in proportion to its length.
    Path path;
    appendStroke(path, wall_);
    const Path track = agedTrack(wallEnd);
    path.insert(path.end(), track.begin(), track.end());
    segments_.push_back({std::move(path), 1.0});
    break;
  }
  case WallMode::Drone: {
    Path wall;
    appendStroke(wall, wall_);
    const HarmonicSplit split = harmonicSplit(harmonic_, params_.wallShare > 0.0 ? params_.wallShare : 0.45);
    Path first = agedTrack(wallEnd);
    if (first.empty()) {
      // No track left: the wall alone, forward then back.
      Path back(wall.rbegin(), wall.rend());
      segments_.push_back({wall, 0.5});
      segments_.push_back({std::move(back), 0.5});
    } else {
      segments_.push_back({std::move(wall), split.p});
      segments_.push_back({std::move(first), (1.0 - split.p) / split.m});
      for (int i = 1; i < split.m; ++i) // fresh diffusion noise on every pass
        segments_.push_back({agedTrack(wallEnd), (1.0 - split.p) / split.m});
    }
    break;
  }
  }
  buildTable(segments_, table_);
}

void ChamberVoice::render(float* x, float* y, int n) {
  const double step = periodHz_ / sampleRate_;
  const auto minRebuild = static_cast<std::int64_t>(kMinRebuildSeconds * sampleRate_);
  for (int i = 0; i < n; ++i) {
    if (!active_) {
      x[i] = y[i] = 0.0f;
      continue;
    }
    const float g = env_.next() * gain_;
    const XY p = osc_.read(table_);
    x[i] = p.x * g;
    y[i] = p.y * g;
    if (env_.idle()) {
      active_ = false;
      x[i] = y[i] = 0.0f;
      continue;
    }
    ++ageSamples_;
    ++sinceRebuild_;
    // Control rate: each period or 2 ms, whichever is longer.
    if (osc_.advance(step) && sinceRebuild_ >= minRebuild)
      rebuild();
  }
}

} // namespace cs

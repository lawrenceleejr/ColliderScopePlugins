#include "cscore/chamber_engine.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "cscore/pitch.h"

namespace cs {

ChamberEngine::ChamberEngine() { prepare(48000.0); }

void ChamberEngine::prepare(double sampleRate) {
  while (servicing_.test_and_set())
    std::this_thread::yield();
  sampleRate_ = builderSampleRate_ = sampleRate;
  minRebuild_ = static_cast<std::int64_t>(kMinRebuildSeconds * sampleRate);
  env_.setSampleRate(sampleRate);
  env_.reset();
  osc_.reset();
  heldCount_ = 0;
  pending_ = false;
  active_ = false;
  awaitingBuild_ = false;
  glideLeft_ = 0;
  bend_ = 0.0;
  clock_ = 0;
  clockShared_.store(0);
  // Serials keep counting across prepare(), so a stale frame can never match.
  servicing_.clear();
  setParams(params_);
}

// ---------------------------------------------------------------- audio side

void ChamberEngine::setParams(const ChamberParams& params) noexcept {
  if ((params.voices > 1) != (params_.voices > 1))
    allNotesOff(); // mono and polyphonic voices are built differently: start clean
  params_ = params;
  params_.voices = std::clamp(params.voices, 1, kMaxVoices);
  env_.setAttack(params.attackMs * 1e-3);
  env_.setRelease(params.releaseMs * 1e-3);
  gain_ = static_cast<float>(std::clamp(params.size, 0.0, 1.0) * std::pow(10.0, std::min(params.gainDb, 0.0) / 20.0));
  updateStep();
  publishInput();
}

void ChamberEngine::publishInput() noexcept {
  BuilderInput in;
  in.params = params_;
  in.serial = pending_ ? pendingSerial_ : serial_;
  in.note = note_;
  in.velocity = velocity_;
  if (heldCount_ > 0) {
    // The newest keys, up to the voice limit, oldest first.
    const int count = std::min(heldCount_, params_.voices);
    in.voiceCount = count;
    for (int i = 0; i < count; ++i)
      in.voices[static_cast<std::size_t>(i)] = held_[static_cast<std::size_t>(heldCount_ - count + i)];
  } else {
    // Nothing held: the release tail keeps drawing the last chord.
    in.voiceCount = lastInput_.voiceCount;
    in.voices = lastInput_.voices;
  }
  lastInput_ = in;
  input_.write(in);
}

void ChamberEngine::updateStep() noexcept {
  const double bend = bend_ * params_.bendRange;
  if (poly() && polyHz_ > 0.0) {
    step_ = polyHz_ * std::exp2(bend / 12.0) / sampleRate_; // the builder chose the fundamental
    return;
  }
  const double note = params_.wall == WallMode::Drone ? params_.wallRoot + bend : pitch_ + bend;
  step_ = midiToHz(note) / sampleRate_;
}

int ChamberEngine::harmonic() const noexcept {
  if (params_.wall != WallMode::Drone)
    return 1;
  return nearestHarmonic(midiToHz(note_), midiToHz(params_.wallRoot));
}

void ChamberEngine::setPitchBend(double amount) noexcept {
  bend_ = std::clamp(amount, -1.0, 1.0);
  updateStep();
}

void ChamberEngine::noteOn(int note, int velocity) noexcept {
  note = std::clamp(note, 0, 127);
  velocity = std::clamp(velocity, 1, 127);
  // Last-note priority: move the key to the top of the held stack.
  int n = 0;
  for (int i = 0; i < heldCount_; ++i)
    if (held_[static_cast<std::size_t>(i)].note != note)
      held_[static_cast<std::size_t>(n++)] = held_[static_cast<std::size_t>(i)];
  if (n == static_cast<int>(held_.size())) { // full: forget the oldest
    std::copy(held_.begin() + 1, held_.end(), held_.begin());
    --n;
  }
  held_[static_cast<std::size_t>(n)] = {note, velocity, ++birthCounter_, clock_};
  heldCount_ = n + 1;
  if (poly())
    startChord();
  else
    startNote(note, velocity);
}

void ChamberEngine::noteOff(int note) noexcept {
  int n = 0;
  bool found = false;
  for (int i = 0; i < heldCount_; ++i) {
    if (held_[static_cast<std::size_t>(i)].note == note)
      found = true;
    else
      held_[static_cast<std::size_t>(n++)] = held_[static_cast<std::size_t>(i)];
  }
  heldCount_ = n;
  if (poly()) {
    if (!found)
      return;
    if (heldCount_ == 0) {
      pending_ = false;
      env_.gate(false);
    } else {
      startChord(); // the remaining keys share the beam between them
    }
    return;
  }
  if (!found || note != note_)
    return; // not the note that is sounding
  if (heldCount_ == 0) {
    pending_ = false;
    env_.gate(false);
  } else {
    const Held back = held_[static_cast<std::size_t>(heldCount_ - 1)];
    startNote(back.note, back.velocity); // fall back to the key still held
  }
}

void ChamberEngine::allNotesOff() noexcept {
  heldCount_ = 0;
  pending_ = false;
  env_.gate(false);
}

void ChamberEngine::startNote(int note, int velocity) noexcept {
  // Each note births a track. The builder makes it; until its first table
  // arrives the voice carries on as it was (a millisecond or two, live).
  note_ = note;
  velocity_ = velocity;
  if (++serialCounter_ == 0)
    ++serialCounter_;
  pendingSerial_ = serialCounter_;
  pendingLegato_ = active_ && env_.isOn();
  pending_ = true;
  publishInput();
  if (synchronous_.load(std::memory_order_relaxed)) {
    serviceInline();
    takeFrame();
  }
}

void ChamberEngine::startChord() noexcept {
  // The set of keys changed: the builder plans a new picture for the chord.
  int lowest = 127;
  const int count = std::min(heldCount_, params_.voices);
  for (int i = heldCount_ - count; i < heldCount_; ++i)
    lowest = std::min(lowest, static_cast<int>(held_[static_cast<std::size_t>(i)].note));
  note_ = lowest;
  velocity_ = held_[static_cast<std::size_t>(heldCount_ - 1)].velocity;
  if (++serialCounter_ == 0)
    ++serialCounter_;
  pendingSerial_ = serialCounter_;
  pendingLegato_ = false;
  pending_ = true;
  publishInput();
  if (synchronous_.load(std::memory_order_relaxed)) {
    serviceInline();
    takeFrame();
  }
}

void ChamberEngine::applyPending() noexcept {
  pending_ = false;
  serial_ = pendingSerial_;
  if (poly()) {
    if (!active_) {
      osc_.reset();
      cleanX_.reset();
      cleanY_.reset();
    }
    glideLeft_ = 0;
    active_ = true;
    env_.gate(true);
    polyHz_ = frames_.front().periodHz;
    updateStep();
    return;
  }
  pitchTarget_ = note_;
  const auto glide = static_cast<std::int64_t>(params_.glideMs * 1e-3 * sampleRate_);
  if (pendingLegato_ && glide > 0) {
    glideLeft_ = glide;
    glideStep_ = (pitchTarget_ - pitch_) / static_cast<double>(glide);
  } else {
    glideLeft_ = 0;
    pitch_ = pitchTarget_;
  }
  if (!active_) {
    osc_.reset(); // a note from silence starts its picture at the start
    cleanX_.reset();
    cleanY_.reset();
  }
  active_ = true;
  env_.gate(true);
  startClock_.store(clock_);
  startSerial_.store(serial_);
  updateStep();
}

void ChamberEngine::takeFrame() noexcept {
  if (!frames_.take())
    return;
  sinceAdopt_ = 0;
  awaitingBuild_ = false;
  if (pending_ && frames_.front().serial == pendingSerial_)
    applyPending();
  else if (poly() && frames_.front().periodHz > 0.0 && frames_.front().periodHz != polyHz_) {
    polyHz_ = frames_.front().periodHz;
    updateStep();
  }
}

void ChamberEngine::serviceInline() noexcept {
  clockShared_.store(clock_);
  while (servicing_.test_and_set())
    std::this_thread::yield();
  serviceLocked();
  servicing_.clear();
}

void ChamberEngine::newPeriod(bool sync) noexcept {
  // Control rate: each period or 2 ms, whichever is longer.
  if (!awaitingBuild_ && sinceAdopt_ >= minRebuild_) {
    awaitingBuild_ = true;
    clockShared_.store(clock_, std::memory_order_relaxed);
    buildRequest_.fetch_add(1);
    if (sync)
      serviceInline();
  }
  takeFrame(); // pictures change only between periods
}

void ChamberEngine::process(float* x, float* y, int n) noexcept {
  const bool sync = synchronous_.load(std::memory_order_relaxed);
  const bool clean = params_.quality == Quality::Clean;
  for (int i = 0; i < n; ++i) {
    if (pending_ && frames_.hasFresh())
      takeFrame();
    if (!active_) {
      x[i] = y[i] = 0.0f;
      ++clock_;
      continue;
    }
    const float g = env_.next() * gain_;
    ++clock_;
    ++sinceAdopt_;
    XY p;
    if (clean) {
      // Read the picture at 4x the rate, low-pass, keep every fourth sample.
      float sx[Decimator4::kFactor], sy[Decimator4::kFactor];
      const double quarter = step_ * 0.25;
      for (int k = 0; k < Decimator4::kFactor; ++k) {
        const XY q = osc_.read(frames_.front().table);
        sx[k] = q.x;
        sy[k] = q.y;
        if (osc_.advance(quarter))
          newPeriod(sync);
      }
      p = {cleanX_.push(sx), cleanY_.push(sy)};
    } else {
      p = osc_.read(frames_.front().table);
      if (osc_.advance(step_))
        newPeriod(sync);
    }
    x[i] = p.x * g;
    y[i] = p.y * g;
    if (env_.idle()) {
      active_ = false;
      x[i] = y[i] = 0.0f;
      continue;
    }
    if (glideLeft_ > 0) {
      pitch_ = --glideLeft_ == 0 ? pitchTarget_ : pitch_ + glideStep_;
      updateStep();
    }
  }
  clockShared_.store(clock_, std::memory_order_relaxed);
}

// -------------------------------------------------------------- builder side

bool ChamberEngine::service() {
  if (synchronous_.load() || servicing_.test_and_set())
    return false;
  const bool built = serviceLocked();
  servicing_.clear();
  return built;
}

bool ChamberEngine::serviceLocked() {
  const BuilderInput in = input_.read();
  if (in.serial == 0)
    return false;

  if (in.params.voices > 1) {
    if (in.voiceCount <= 0)
      return false;
    const std::uint32_t request = buildRequest_.load();
    if (in.serial == builtSerial_ && (request == servedRequest_ || frames_.hasFresh()))
      return false;
    if (in.serial != builtSerial_) {
      // The chord changed: keep the tracks of keys still held, birth the new ones.
      std::array<PolyVoice, kMaxVoices> next{};
      for (int i = 0; i < in.voiceCount; ++i) {
        const Held& key = in.voices[static_cast<std::size_t>(i)];
        PolyVoice& voice = next[static_cast<std::size_t>(i)];
        voice.birth = key.birth;
        bool kept = false;
        for (PolyVoice& old : polyVoices_) {
          if (old.birth == key.birth && !old.track.points.empty()) {
            voice.track = std::move(old.track);
            kept = true;
            break;
          }
        }
        if (!kept) {
          std::uint64_t seed = 0;
          voice.track = birthTrack(in.params, key.note, key.velocity, key.birth, seed);
        }
      }
      polyVoices_ = std::move(next);
      builtSerial_ = in.serial;
    }
    servedRequest_ = request;
    buildChord(in);
    return true;
  }

  if (in.serial != builtSerial_) {
    // A new note: birth its track.
    std::uint64_t seed = 0;
    track_ = birthTrack(in.params, in.note, in.velocity, in.serial, seed);
    noise_.reseed(seed ^ 0xd1b54a32d192ed03ULL);
    builtSerial_ = in.serial;
    servedRequest_ = buildRequest_.load();
    build(in, 0.0);
    return true;
  }

  const std::uint32_t request = buildRequest_.load();
  if (request == servedRequest_ || frames_.hasFresh())
    return false;
  servedRequest_ = request;
  const double age = startSerial_.load() == builtSerial_
                         ? static_cast<double>(clockShared_.load() - startClock_.load()) / builderSampleRate_
                         : 0.0;
  build(in, std::max(0.0, age));
  return true;
}

Track ChamberEngine::birthTrack(const ChamberParams& params, int note, int velocity, std::uint32_t serial,
                                std::uint64_t& seed) const {
  seed = params.seed * 0x9e3779b97f4a7c15ULL;
  switch (params.seedMode) {
  case SeedMode::Random:
    seed += 0x1000 + serial;
    break;
  case SeedMode::PerKey:
    seed += static_cast<std::uint64_t>(note & 0x7f);
    break;
  case SeedMode::Global:
    break;
  }
  Rng rng(seed);
  TrackType type = typeForVelocity(velocity);
  switch (params.type) {
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
  return makeTrack(type, rng);
}

void ChamberEngine::buildChord(const BuilderInput& in) {
  const ChamberParams& p = in.params;
  const std::int64_t now = clockShared_.load();
  const int count = std::min(in.voiceCount, kMaxVoices);

  if (count == 1) {
    // One key: exactly the mono picture.
    const Held& key = in.voices[0];
    track_ = polyVoices_[0].track;
    BuilderInput one = in;
    one.note = key.note;
    one.velocity = key.velocity;
    build(one, std::max(0.0, static_cast<double>(now - key.birthClock) / builderSampleRate_));
    return;
  }

  // Voices in pitch order, lowest first.
  std::array<int, kMaxVoices> order{};
  for (int i = 0; i < count; ++i)
    order[static_cast<std::size_t>(i)] = i;
  std::sort(order.begin(), order.begin() + count, [&](int a, int b) {
    return in.voices[static_cast<std::size_t>(a)].note < in.voices[static_cast<std::size_t>(b)].note;
  });
  std::array<double, kMaxVoices> hz{};
  for (int i = 0; i < count; ++i)
    hz[static_cast<std::size_t>(i)] = midiToHz(in.voices[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])].note);

  const bool drone = p.wall == WallMode::Drone, hasWall = p.wall != WallMode::Off;
  const double wallTarget = drone ? (p.wallShare > 0.0 ? p.wallShare : 0.45) : 0.3;
  const ChordPlan plan = planChord(std::span<const double>(hz.data(), static_cast<std::size_t>(count)),
                                   drone ? midiToHz(p.wallRoot) : hz[0], hasWall ? std::min(0.2, wallTarget) : 0.0,
                                   hasWall ? wallTarget : 0.0);
  chordPlan_ = plan;

  const AgeingParams ageing{p.diffusionRate, p.drift, p.condenseMs * 1e-3};
  Vec2 from = hasWall ? wall_.back() : Vec2{};
  auto agedVoice = [&](int slot) {
    const int index = order[static_cast<std::size_t>(slot)];
    const Held& key = in.voices[static_cast<std::size_t>(index)];
    const double age = std::max(0.0, static_cast<double>(now - key.birthClock) / builderSampleRate_);
    strokes_.clear();
    ageTrack(polyVoices_[static_cast<std::size_t>(index)].track, age, ageing, noise_, strokes_);
    Path path = strokesToPath(orderStrokes(std::move(strokes_), from));
    if (!path.empty())
      from = {path.back().x, path.back().y};
    return path;
  };

  segments_.clear();
  double unused = plan.filler; // time no voice claims: tracks past their life give theirs up too
  if (hasWall) {
    Path wall;
    appendStroke(wall, wall_);
    segments_.push_back({std::move(wall), plan.wall});
  }
  for (int i = 0; i < count; ++i) {
    const auto u = static_cast<std::size_t>(i);
    if (i == plan.elastic) {
      // The voice on the fundamental: once per period, in the share left for it.
      Path path = agedVoice(i);
      if (path.empty())
        unused += plan.elasticShare;
      else
        segments_.push_back({std::move(path), plan.elasticShare});
      continue;
    }
    for (int pass = 0; pass < plan.m[u]; ++pass) { // fresh diffusion noise on every pass
      Path path = agedVoice(i);
      if (path.empty())
        unused += 1.0 / plan.h[u];
      else
        segments_.push_back({std::move(path), 1.0 / plan.h[u]});
    }
  }
  if (unused > 1e-9) {
    // Spend the rest on the wall, or on one more, slower pass of the lowest
    // voice still alive: never on a parked beam.
    if (hasWall) {
      segments_.front().fraction += unused;
    } else {
      for (int i = 0; i < count; ++i) {
        Path path = agedVoice(i);
        if (!path.empty()) {
          segments_.push_back({std::move(path), unused});
          break;
        }
      }
    }
  }
  Frame& frame = frames_.back();
  buildTable(segments_, frame.table);
  frame.serial = in.serial;
  frame.periodHz = plan.f0;
  frames_.publish();
}

void ChamberEngine::build(const BuilderInput& in, double age) {
  const ChamberParams& p = in.params;
  const AgeingParams ageing{p.diffusionRate, p.drift, p.condenseMs * 1e-3};
  const Vec2 wallEnd = wall_.back();

  auto agedTrack = [&](Vec2 from) {
    strokes_.clear();
    ageTrack(track_, age, ageing, noise_, strokes_);
    return strokesToPath(orderStrokes(std::move(strokes_), from));
  };

  segments_.clear();
  switch (p.wall) {
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
    const int h = nearestHarmonic(midiToHz(in.note), midiToHz(p.wallRoot));
    const HarmonicSplit split = harmonicSplit(h, p.wallShare > 0.0 ? p.wallShare : 0.45);
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
  Frame& frame = frames_.back();
  buildTable(segments_, frame.table);
  frame.serial = in.serial;
  frame.periodHz = midiToHz(p.wall == WallMode::Drone ? p.wallRoot : in.note);
  frames_.publish();
}

} // namespace cs

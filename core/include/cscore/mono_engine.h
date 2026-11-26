// A monophonic picture instrument, generic over what it draws.
//
// The audio side is the same for every instrument: last-note priority,
// glide, pitch bend, the gate envelope, a picture table read with a
// fractional phase accumulator, Scope-exact or Clean quality. No allocation,
// no locks, no random numbers there.
//
// The Source draws the pictures, on the builder side:
//
//   struct Source {
//     struct Params { VoiceParams voice; ... };   // trivially copyable
//     static constexpr double kRebuildSeconds;     // shortest time between pictures
//     void prepare(double sampleRate);
//     void birth(const Params&, int note, int velocity, std::uint32_t serial);
//     void build(const Params&, int note, double age, std::span<XY> table);
//   };
//
// service() runs it at control rate (each period or kRebuildSeconds,
// whichever is longer) and publishes tables lock-free. Synchronous mode (the default)
// runs it inline, sample-exact: for offline renders and tests.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>
#include <thread>

#include "cscore/chamber_engine.h" // Quality
#include "cscore/decimator.h"
#include "cscore/envelope.h"
#include "cscore/lockfree.h"
#include "cscore/oscillator.h"
#include "cscore/path.h"
#include "cscore/pitch.h"

namespace cs {

// What every instrument's voice has.
struct VoiceParams {
  double size = 0.9;      // 0-1
  double attackMs = 0.0;  // 0-50 ms
  double releaseMs = 0.0; // 0-2000 ms; 0 = hard cut
  double glideMs = 0.0;   // 0-500 ms, legato only
  double bendRange = 2.0; // 0-12 semitones
  double gainDb = 0.0;    // -24 to 0 dB
  Quality quality = Quality::ScopeExact;
};

template <typename Source>
class MonoEngine {
public:
  using Params = typename Source::Params;
  static constexpr std::size_t kTableSize = 2048;

  MonoEngine() { prepare(48000.0); }

  // Setup, not real-time safe.
  void prepare(double sampleRate) {
    while (servicing_.test_and_set())
      std::this_thread::yield();
    sampleRate_ = builderSampleRate_ = sampleRate;
    minRebuild_ = static_cast<std::int64_t>(Source::kRebuildSeconds * sampleRate);
    env_.setSampleRate(sampleRate);
    env_.reset();
    osc_.reset();
    heldCount_ = 0;
    pending_ = active_ = awaitingBuild_ = false;
    glideLeft_ = 0;
    bend_ = 0.0;
    clock_ = 0;
    clockShared_.store(0);
    source_.prepare(sampleRate);
    servicing_.clear();
    setParams(params_);
  }
  void setSynchronous(bool synchronous) noexcept { synchronous_.store(synchronous); }

  // ---- audio thread ----
  void setParams(const Params& params) noexcept {
    params_ = params;
    const VoiceParams& v = params.voice;
    env_.setAttack(v.attackMs * 1e-3);
    env_.setRelease(v.releaseMs * 1e-3);
    gain_ = static_cast<float>(std::clamp(v.size, 0.0, 1.0) * std::pow(10.0, std::min(v.gainDb, 0.0) / 20.0));
    updateStep();
    publishInput();
  }

  void noteOn(int note, int velocity) noexcept {
    note = std::clamp(note, 0, 127);
    velocity = std::clamp(velocity, 1, 127);
    int n = 0;
    for (int i = 0; i < heldCount_; ++i)
      if (held_[static_cast<std::size_t>(i)].note != note)
        held_[static_cast<std::size_t>(n++)] = held_[static_cast<std::size_t>(i)];
    if (n == static_cast<int>(held_.size())) {
      std::copy(held_.begin() + 1, held_.end(), held_.begin());
      --n;
    }
    held_[static_cast<std::size_t>(n)] = {note, velocity};
    heldCount_ = n + 1;
    startNote(note, velocity);
  }

  void noteOff(int note) noexcept {
    int n = 0;
    bool found = false;
    for (int i = 0; i < heldCount_; ++i) {
      if (held_[static_cast<std::size_t>(i)].note == note)
        found = true;
      else
        held_[static_cast<std::size_t>(n++)] = held_[static_cast<std::size_t>(i)];
    }
    heldCount_ = n;
    if (!found || note != note_)
      return;
    if (heldCount_ == 0) {
      pending_ = false;
      env_.gate(false);
    } else {
      const Held back = held_[static_cast<std::size_t>(heldCount_ - 1)];
      startNote(back.note, back.velocity);
    }
  }

  void allNotesOff() noexcept {
    heldCount_ = 0;
    pending_ = false;
    env_.gate(false);
  }

  void setPitchBend(double amount) noexcept { // -1..1 of the bend range
    bend_ = std::clamp(amount, -1.0, 1.0);
    updateStep();
  }

  // Writes n samples of X (left) and Y (right). Silence is exact zero.
  void process(float* x, float* y, int n) noexcept {
    const bool sync = synchronous_.load(std::memory_order_relaxed);
    const bool clean = params_.voice.quality == Quality::Clean;
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

  bool active() const noexcept { return active_; }
  double periodHz() const noexcept { return step_ * sampleRate_; }
  int latencySamples() const noexcept { return params_.voice.quality == Quality::Clean ? Decimator4::kLatency : 0; }

  // ---- builder thread ----
  // Do any pending work. Returns true if a table was built.
  bool service() {
    if (synchronous_.load() || servicing_.test_and_set())
      return false;
    const bool built = serviceLocked();
    servicing_.clear();
    return built;
  }

  // Builder state: read it only in synchronous mode.
  const Source& source() const noexcept { return source_; }

private:
  struct Frame {
    std::array<XY, kTableSize> table{};
    std::uint32_t serial = 0;
  };
  struct Input {
    Params params;
    std::uint32_t serial = 0;
    std::int32_t note = 69, velocity = 80;
  };
  struct Held {
    std::int32_t note = 0, velocity = 0;
  };

  void publishInput() noexcept {
    Input in;
    in.params = params_;
    in.serial = pending_ ? pendingSerial_ : serial_;
    in.note = note_;
    in.velocity = velocity_;
    input_.write(in);
  }

  void updateStep() noexcept { step_ = midiToHz(pitch_ + bend_ * params_.voice.bendRange) / sampleRate_; }

  void startNote(int note, int velocity) noexcept {
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

  void applyPending() noexcept {
    pending_ = false;
    serial_ = pendingSerial_;
    pitchTarget_ = note_;
    const auto glide = static_cast<std::int64_t>(params_.voice.glideMs * 1e-3 * sampleRate_);
    if (pendingLegato_ && glide > 0) {
      glideLeft_ = glide;
      glideStep_ = (pitchTarget_ - pitch_) / static_cast<double>(glide);
    } else {
      glideLeft_ = 0;
      pitch_ = pitchTarget_;
    }
    if (!active_) {
      osc_.reset();
      cleanX_.reset();
      cleanY_.reset();
    }
    active_ = true;
    env_.gate(true);
    startClock_.store(clock_);
    startSerial_.store(serial_);
    updateStep();
  }

  void takeFrame() noexcept {
    if (!frames_.take())
      return;
    sinceAdopt_ = 0;
    awaitingBuild_ = false;
    if (pending_ && frames_.front().serial == pendingSerial_)
      applyPending();
  }

  void serviceInline() noexcept {
    clockShared_.store(clock_);
    while (servicing_.test_and_set())
      std::this_thread::yield();
    serviceLocked();
    servicing_.clear();
  }

  void newPeriod(bool sync) noexcept {
    if (!awaitingBuild_ && sinceAdopt_ >= minRebuild_) {
      awaitingBuild_ = true;
      clockShared_.store(clock_, std::memory_order_relaxed);
      buildRequest_.fetch_add(1);
      if (sync)
        serviceInline();
    }
    takeFrame(); // pictures change only between periods
  }

  bool serviceLocked() {
    const Input in = input_.read();
    if (in.serial == 0)
      return false;
    double age = 0.0;
    if (in.serial != builtSerial_) {
      source_.birth(in.params, in.note, in.velocity, in.serial);
      builtSerial_ = in.serial;
      servedRequest_ = buildRequest_.load();
    } else {
      const std::uint32_t request = buildRequest_.load();
      if (request == servedRequest_ || frames_.hasFresh())
        return false;
      servedRequest_ = request;
      if (startSerial_.load() == builtSerial_)
        age = std::max(0.0, static_cast<double>(clockShared_.load() - startClock_.load()) / builderSampleRate_);
    }
    Frame& frame = frames_.back();
    source_.build(in.params, in.note, age, frame.table);
    frame.serial = in.serial;
    frames_.publish();
    return true;
  }

  // audio thread
  Params params_{};
  double sampleRate_ = 48000.0;
  TableOscillator osc_;
  GateEnvelope env_;
  Decimator4 cleanX_, cleanY_;
  std::array<Held, 16> held_{};
  int heldCount_ = 0;
  int note_ = 69, velocity_ = 80;
  bool pending_ = false, pendingLegato_ = false;
  std::uint32_t pendingSerial_ = 0, serial_ = 0, serialCounter_ = 0;
  double pitch_ = 69.0, pitchTarget_ = 69.0, glideStep_ = 0.0;
  std::int64_t glideLeft_ = 0;
  double bend_ = 0.0;
  double step_ = 440.0 / 48000.0;
  std::int64_t clock_ = 0, sinceAdopt_ = 0, minRebuild_ = 96;
  bool awaitingBuild_ = false;
  float gain_ = 0.9f;
  bool active_ = false;

  // shared
  LatestValue<Input> input_;
  TripleBuffer<Frame> frames_;
  std::atomic<std::int64_t> clockShared_{0}, startClock_{0};
  std::atomic<std::uint32_t> startSerial_{0}, buildRequest_{0};
  std::atomic<bool> synchronous_{true};
  std::atomic_flag servicing_ = ATOMIC_FLAG_INIT;

  // builder thread
  double builderSampleRate_ = 48000.0;
  std::uint32_t builtSerial_ = 0, servedRequest_ = 0;
  Source source_;
};

} // namespace cs

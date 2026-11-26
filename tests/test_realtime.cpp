// Real-time safety and CPU cost of the CS Chamber engine, run the way the
// plugin runs it: a builder thread making tables, an audio thread reading.
//
// The malloc hook: global operator new counts every allocation made while
// the calling thread has declared itself the audio thread.
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>
#include <vector>

#include "cscore/cscore.h"

namespace {
thread_local bool onAudioThread = false;
std::atomic<long> audioAllocations{0};
} // namespace

void* operator new(std::size_t size) {
  if (onAudioThread)
    audioAllocations.fetch_add(1);
  if (void* p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept {
  if (onAudioThread && p)
    audioAllocations.fetch_add(1);
  std::free(p);
}
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 128;

struct Builder {
  explicit Builder(cs::ChamberEngine& e) : thread([this, &e] {
    while (!stop.load()) {
      if (!e.service())
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }) {}
  ~Builder() {
    stop.store(true);
    thread.join();
  }
  std::atomic<bool> stop{false};
  std::thread thread;
};

} // namespace

TEST_CASE("Real-time safety: no allocation on the audio thread", "[realtime][acceptance]") {
  cs::ChamberEngine engine;
  engine.prepare(kSampleRate);
  engine.setSynchronous(false);
  Builder builder(engine);
  std::vector<float> x(kBlock), y(kBlock);
  float peak = 0.0f, tail = 1.0f;

  audioAllocations.store(0);
  onAudioThread = true;
  cs::ChamberParams p;
  const cs::WallMode walls[] = {cs::WallMode::Shared, cs::WallMode::Off, cs::WallMode::Drone};
  for (int phrase = 0; phrase < 12; ++phrase) {
    p.wall = walls[phrase % 3];
    p.releaseMs = phrase % 2 ? 30.0 : 0.0;
    p.glideMs = phrase % 4 ? 0.0 : 40.0;
    engine.setParams(p);
    p.voices = phrase % 3 == 2 ? 4 : 1; // every third phrase is polyphonic
    engine.setParams(p);
    engine.noteOn(45 + phrase * 3, 30 + phrase * 8);
    for (int block = 0; block < 60; ++block) { // 160 ms, paced like a live stream
      if (block == 20)
        engine.noteOn(52 + phrase, 100); // legato
      if (block == 25)
        engine.noteOn(57 + phrase, 60); // a third key: a chord when polyphonic
      if (block == 30)
        engine.setPitchBend(0.5);
      if (block == 40)
        engine.noteOff(52 + phrase);
      engine.process(x.data(), y.data(), kBlock);
      for (int i = 0; i < kBlock; ++i)
        peak = std::max(peak, std::abs(x[static_cast<std::size_t>(i)]));
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    engine.allNotesOff();
    for (int block = 0; block < 30; ++block) {
      engine.process(x.data(), y.data(), kBlock);
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    tail = std::abs(x.back()) + std::abs(y.back());
  }
  onAudioThread = false;

  CHECK(audioAllocations.load() == 0);
  CHECK(peak > 0.3f);   // it played
  CHECK(tail == 0.0f);  // and stopped
}

TEST_CASE("Real-time safety: CS Mux allocates nothing while processing", "[realtime][acceptance]") {
  cs::MuxEngine engine;
  engine.prepare(kSampleRate);
  std::vector<float> ax(kBlock), ay(kBlock), bx(kBlock), by(kBlock), x(kBlock), y(kBlock), z(kBlock);
  double phase = 0.0;

  audioAllocations.store(0);
  onAudioThread = true;
  cs::MuxParams p;
  for (int pass = 0; pass < 6; ++pass) {
    p.mode = static_cast<cs::MuxMode>(pass % 3);
    p.switchPoint = pass < 3 ? cs::SwitchPoint::Fixed : cs::SwitchPoint::Closest;
    p.a.rotation = pass * 10.0;
    engine.setParams(p);
    for (int block = 0; block < 400; ++block) {
      for (int i = 0; i < kBlock; ++i) {
        phase += 6.283185307179586 * 110.0 / kSampleRate;
        ax[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * std::cos(phase));
        ay[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * std::sin(phase));
        bx[static_cast<std::size_t>(i)] = static_cast<float>(0.3 * std::cos(4.0 * phase));
        by[static_cast<std::size_t>(i)] = static_cast<float>(0.3 * std::sin(8.0 * phase));
      }
      if (block % 50 == 10)
        engine.triggerHit(static_cast<cs::HitType>((block / 50) % 3));
      if (block % 50 == 30)
        engine.triggerB();
      engine.process(ax.data(), ay.data(), bx.data(), by.data(), x.data(), y.data(), z.data(), nullptr, kBlock);
    }
  }
  onAudioThread = false;
  CHECK(audioAllocations.load() == 0);
}

// The same check for the instruments built on MonoEngine.
template <typename Engine>
void checkNoAudioAllocations(typename Engine::Params p = {}) {
  Engine engine;
  engine.prepare(kSampleRate);
  engine.setSynchronous(false);
  std::atomic<bool> stop{false};
  std::thread builder([&] {
    while (!stop.load())
      if (!engine.service())
        std::this_thread::sleep_for(std::chrono::microseconds(200));
  });
  std::vector<float> x(kBlock), y(kBlock);
  float peak = 0.0f;
  audioAllocations.store(0);
  onAudioThread = true;
  for (int phrase = 0; phrase < 8; ++phrase) {
    p.voice.releaseMs = phrase % 2 ? 30.0 : 0.0;
    p.voice.glideMs = phrase % 4 ? 0.0 : 40.0;
    p.voice.quality = phrase % 3 ? cs::Quality::ScopeExact : cs::Quality::Clean;
    engine.setParams(p);
    engine.noteOn(45 + phrase * 4, 20 + phrase * 14);
    for (int block = 0; block < 60; ++block) {
      if (block == 20)
        engine.noteOn(52 + phrase, 100);
      if (block == 30)
        engine.setPitchBend(-0.5);
      if (block == 40)
        engine.noteOff(52 + phrase);
      engine.process(x.data(), y.data(), kBlock);
      for (int i = 0; i < kBlock; ++i)
        peak = std::max(peak, std::abs(x[static_cast<std::size_t>(i)]));
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    engine.allNotesOff();
    for (int block = 0; block < 30; ++block) {
      engine.process(x.data(), y.data(), kBlock);
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  }
  onAudioThread = false;
  stop.store(true);
  builder.join();
  CHECK(audioAllocations.load() == 0);
  CHECK(peak > 0.1f);
  CHECK(std::abs(x.back()) + std::abs(y.back()) == 0.0f);
}

TEST_CASE("Real-time safety: CS Bubble allocates nothing on the audio thread", "[realtime][acceptance]") {
  checkNoAudioAllocations<cs::BubbleEngine>();
}

TEST_CASE("Real-time safety: CS Orbital allocates nothing on the audio thread", "[realtime][acceptance]") {
  checkNoAudioAllocations<cs::OrbitalEngine>();
}

TEST_CASE("Real-time safety: CS Script allocates nothing on the audio thread", "[realtime][acceptance]") {
  cs::ScriptParams p;
  p.setText("E = mc^2");
  p.writeMs = 50.0;
  checkNoAudioAllocations<cs::ScriptEngine>(p);
}

template <typename Engine>
double cpuPercent(const typename Engine::Params& p) {
  using Clock = std::chrono::steady_clock;
  const double seconds = 10.0;
  const int blocks = static_cast<int>(seconds * kSampleRate / kBlock);
  std::vector<float> x(kBlock), y(kBlock);
  Engine engine;
  engine.prepare(kSampleRate);
  engine.setParams(p);
  const auto start = Clock::now();
  for (int b = 0; b < blocks; ++b) {
    if (b % 188 == 0)
      engine.noteOn(33 + (b / 188) % 49, 20 + (b / 188 * 37) % 100);
    engine.process(x.data(), y.data(), kBlock);
  }
  return std::chrono::duration<double>(Clock::now() - start).count() / seconds * 100.0;
}

TEST_CASE("CPU: CS Bubble and CS Orbital under 5 % of one core", "[realtime][acceptance]") {
  const double bubble = cpuPercent<cs::BubbleEngine>({});
  cs::OrbitalParams heavy;
  heavy.orbital = 20; // 4f, 16 wires: the most to draw
  heavy.lines = 16;
  const double orbital = cpuPercent<cs::OrbitalEngine>(heavy);
  cs::ScriptParams text;
  text.setText("\\frac{\\partial\\psi}{\\partial t} = \\hat{H}\\psi \\\\ The quick brown fox jumps over the lazy dog");
  text.writeMs = 300.0;
  const double script = cpuPercent<cs::ScriptEngine>(text);
  WARN("bubble: " << bubble << " % of a core; orbital: " << orbital << " %; script: " << script
                  << " %, audio plus picture building");
  CHECK(bubble < 5.0);
  CHECK(orbital < 5.0);
  CHECK(script < 5.0);
}

TEST_CASE("The hook does see allocations", "[realtime]") {
  audioAllocations.store(0);
  onAudioThread = true;
  auto* v = new std::vector<int>(100);
  delete v;
  onAudioThread = false;
  CHECK(audioAllocations.load() >= 2);
}

TEST_CASE("CPU: under 5 % of one core at 48 kHz, 128-sample buffer", "[realtime][acceptance]") {
  using Clock = std::chrono::steady_clock;
  const double seconds = 10.0;
  const int blocks = static_cast<int>(seconds * kSampleRate / kBlock);
  std::vector<float> x(kBlock), y(kBlock);

  // Synchronous mode: the audio work plus all table building on one thread,
  // an upper bound on what the plugin's audio thread spends.
  for (cs::WallMode wall : {cs::WallMode::Off, cs::WallMode::Shared, cs::WallMode::Drone}) {
    cs::ChamberEngine engine;
    engine.prepare(kSampleRate);
    cs::ChamberParams p;
    p.wall = wall;
    engine.setParams(p);
    float peak = 0.0f;
    const auto start = Clock::now();
    for (int b = 0; b < blocks; ++b) {
      if (b % 188 == 0) // a new note every half second, low to high
        engine.noteOn(33 + (b / 188) % 49, 40 + (b / 188 * 37) % 80);
      engine.process(x.data(), y.data(), kBlock);
      peak = std::max(peak, std::abs(x[0]));
    }
    const double percent = std::chrono::duration<double>(Clock::now() - start).count() / seconds * 100.0;
    WARN("wall mode " << static_cast<int>(wall) << ": " << percent << " % of a core, audio plus table building");
    CHECK(peak > 0.1f);
    CHECK(percent < 5.0);
  }
}

TEST_CASE("CPU: CS Mux under 5 % of one core at 48 kHz, 128-sample buffer", "[realtime][acceptance]") {
  using Clock = std::chrono::steady_clock;
  const double seconds = 10.0;
  const int blocks = static_cast<int>(seconds * kSampleRate / kBlock);
  std::vector<float> ax(kBlock), ay(kBlock), bx(kBlock), by(kBlock), x(kBlock), y(kBlock);
  for (cs::MuxMode mode : {cs::MuxMode::Interleave, cs::MuxMode::CycleLock, cs::MuxMode::Slot}) {
    cs::MuxEngine engine;
    engine.prepare(kSampleRate);
    cs::MuxParams p;
    p.mode = mode;
    p.switchPoint = cs::SwitchPoint::Closest;
    engine.setParams(p);
    double phase = 0.0, spent = 0.0;
    for (int b = 0; b < blocks; ++b) {
      for (int i = 0; i < kBlock; ++i) {
        phase += 6.283185307179586 * 55.0 / kSampleRate;
        ax[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * std::cos(phase));
        ay[static_cast<std::size_t>(i)] = static_cast<float>(0.8 * std::sin(phase));
        bx[static_cast<std::size_t>(i)] = static_cast<float>(0.3 * std::cos(4.0 * phase));
        by[static_cast<std::size_t>(i)] = static_cast<float>(0.3 * std::sin(4.0 * phase));
      }
      if (b % 94 == 0)
        engine.triggerHit(cs::HitType::Kick);
      const auto start = Clock::now();
      engine.process(ax.data(), ay.data(), bx.data(), by.data(), x.data(), y.data(), nullptr, nullptr, kBlock);
      spent += std::chrono::duration<double>(Clock::now() - start).count();
    }
    const double percent = spent / seconds * 100.0;
    WARN("mux mode " << static_cast<int>(mode) << ": " << percent << " % of a core");
    CHECK(percent < 5.0);
  }
}

TEST_CASE("Scope display cost", "[realtime]") {
  // What the plugin's display does each second: gather light 60 times,
  // tone-map and redraw 30 times. Best of five runs, as shared CI machines are noisy.
  using Clock = std::chrono::steady_clock;
  cs::ChamberEngine engine;
  engine.prepare(kSampleRate);
  engine.noteOn(57, 80);
  std::vector<float> x(800), y(800);
  cs::ScopeView view(512);
  std::vector<std::uint32_t> argb;
  view.renderARGB(argb); // first frame sizes the scratch buffers
  double best = 1e9;
  for (int run = 0; run < 5; ++run) {
    const auto start = Clock::now();
    for (int tick = 0; tick < 60; ++tick) {
      engine.process(x.data(), y.data(), 800);
      view.beginFrame(1.0 / 60.0);
      view.addSamples(x.data(), y.data(), 800);
      if (tick % 2 == 0)
        view.renderARGB(argb);
    }
    best = std::min(best, std::chrono::duration<double>(Clock::now() - start).count());
  }
  WARN("scope display: " << best * 100.0 << " % of a core (" << best * 1000.0 / 30.0 << " ms per redraw)");
  CHECK(best < 0.5);
}

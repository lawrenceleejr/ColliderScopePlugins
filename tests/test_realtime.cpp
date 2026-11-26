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
    engine.noteOn(45 + phrase * 3, 30 + phrase * 8);
    for (int block = 0; block < 60; ++block) { // 160 ms, paced like a live stream
      if (block == 20)
        engine.noteOn(52 + phrase, 100); // legato
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

TEST_CASE("Scope display cost per frame", "[realtime]") {
  using Clock = std::chrono::steady_clock;
  cs::ChamberEngine engine;
  engine.prepare(kSampleRate);
  engine.noteOn(57, 80);
  std::vector<float> x(800), y(800);
  cs::ScopeView view(512);
  std::vector<std::uint8_t> rgb;
  view.render(rgb); // first frame sizes the scratch buffers
  const int frames = 120;
  const auto start = Clock::now();
  for (int f = 0; f < frames; ++f) {
    engine.process(x.data(), y.data(), 800);
    view.beginFrame(1.0 / 60.0);
    view.addSamples(x.data(), y.data(), 800);
    view.render(rgb);
  }
  const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / frames;
  WARN("scope frame (accumulate + tone map, 512 x 512): " << ms << " ms = " << ms / (1000.0 / 60.0) * 100.0
                                                         << " % of a core at 60 fps");
  CHECK(ms < 16.0); // must at least keep up with 60 fps
}

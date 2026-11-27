// The ColliderScope instrument: one voice, several things it can draw.
// The Instrument control picks which source builds the pictures.
#pragma once

#include <cstdint>
#include <span>

#include "cscore/bubble.h"
#include "cscore/collision.h"
#include "cscore/feynman.h"
#include "cscore/gallery.h"
#include "cscore/mesh.h"
#include "cscore/mono_engine.h"
#include "cscore/orbital.h"
#include "cscore/script.h"
#include "cscore/surface.h"
#include "cscore/tracker.h"

namespace cs {

enum class Instrument { Bubble, Orbital, Script, Function, Collision, Mesh, Feynman, Scenes, Tracker };
inline constexpr int kInstrumentCount = 9;

struct MultiParams {
  VoiceParams voice; // shared by every instrument
  Instrument instrument = Instrument::Bubble;
  BubbleParams bubble;
  OrbitalParams orbital;
  ScriptParams script;
  SurfaceParams surface;
  CollisionParams collision;
  MeshParams mesh;
  FeynmanParams feynman;
  GalleryParams gallery;
  TrackerParams tracker;
};

class MultiSource {
public:
  using Params = MultiParams;
  static double rebuildSeconds(const Params& p) noexcept {
    switch (p.instrument) {
    case Instrument::Bubble:
      return BubbleSource::rebuildSeconds(p.bubble);
    case Instrument::Orbital:
      return OrbitalSource::rebuildSeconds(p.orbital);
    case Instrument::Script:
      return ScriptSource::rebuildSeconds(p.script);
    case Instrument::Function:
      return SurfaceSource::rebuildSeconds(p.surface);
    case Instrument::Collision:
      return CollisionSource::rebuildSeconds(p.collision);
    case Instrument::Feynman:
      return FeynmanSource::rebuildSeconds(p.feynman);
    case Instrument::Scenes:
      return GallerySource::rebuildSeconds(p.gallery);
    case Instrument::Tracker:
      return TrackerSource::rebuildSeconds(p.tracker);
    case Instrument::Mesh:
      break;
    }
    return MeshSource::rebuildSeconds(p.mesh);
  }

  void prepare(double sampleRate) {
    bubble_.prepare(sampleRate);
    orbital_.prepare(sampleRate);
    script_.prepare(sampleRate);
    surface_.prepare(sampleRate);
    collision_.prepare(sampleRate);
    mesh_.prepare(sampleRate);
    feynman_.prepare(sampleRate);
    gallery_.prepare(sampleRate);
    tracker_.prepare(sampleRate);
  }

  void birth(const Params& params, int note, int velocity, std::uint32_t serial) {
    note_ = note;
    velocity_ = velocity;
    serial_ = serial;
    born_ = params.instrument;
    switch (params.instrument) {
    case Instrument::Bubble:
      return bubble_.birth(with(params.bubble, params), note, velocity, serial);
    case Instrument::Orbital:
      return orbital_.birth(with(params.orbital, params), note, velocity, serial);
    case Instrument::Script:
      return script_.birth(with(params.script, params), note, velocity, serial);
    case Instrument::Function:
      return surface_.birth(with(params.surface, params), note, velocity, serial);
    case Instrument::Collision:
      return collision_.birth(with(params.collision, params), note, velocity, serial);
    case Instrument::Mesh:
      return mesh_.birth(with(params.mesh, params), note, velocity, serial);
    case Instrument::Feynman:
      return feynman_.birth(with(params.feynman, params), note, velocity, serial);
    case Instrument::Scenes:
      return gallery_.birth(with(params.gallery, params), note, velocity, serial);
    case Instrument::Tracker:
      return tracker_.birth(with(params.tracker, params), note, velocity, serial);
    }
  }

  void build(const Params& params, int note, double age, std::span<XY> table) {
    if (params.instrument != born_)
      birth(params, note_, velocity_, serial_); // the instrument was switched while a note sounds
    switch (params.instrument) {
    case Instrument::Bubble:
      return bubble_.build(with(params.bubble, params), note, age, table);
    case Instrument::Orbital:
      return orbital_.build(with(params.orbital, params), note, age, table);
    case Instrument::Script:
      return script_.build(with(params.script, params), note, age, table);
    case Instrument::Function:
      return surface_.build(with(params.surface, params), note, age, table);
    case Instrument::Collision:
      return collision_.build(with(params.collision, params), note, age, table);
    case Instrument::Mesh:
      return mesh_.build(with(params.mesh, params), note, age, table);
    case Instrument::Feynman:
      return feynman_.build(with(params.feynman, params), note, age, table);
    case Instrument::Scenes:
      return gallery_.build(with(params.gallery, params), note, age, table);
    case Instrument::Tracker:
      return tracker_.build(with(params.tracker, params), note, age, table);
    }
  }

  // The model the Mesh instrument draws. Safe from any thread but the audio thread.
  void setMesh(std::shared_ptr<const Mesh> mesh) { mesh_.setMesh(std::move(mesh)); }
  std::shared_ptr<const Mesh> mesh() const { return mesh_.mesh(); }
  // Builder state: read only in synchronous mode.
  const SurfaceSource& surface() const noexcept { return surface_; }
  const CollisionSource& collision() const noexcept { return collision_; }

private:
  // An instrument's own parameters, with the shared voice.
  template <typename P>
  static P with(P own, const Params& all) noexcept {
    own.voice = all.voice;
    return own;
  }

  BubbleSource bubble_;
  OrbitalSource orbital_;
  ScriptSource script_;
  SurfaceSource surface_;
  CollisionSource collision_;
  MeshSource mesh_;
  FeynmanSource feynman_;
  GallerySource gallery_;
  TrackerSource tracker_;
  Instrument born_ = Instrument::Bubble;
  int note_ = 69, velocity_ = 80;
  std::uint32_t serial_ = 1;
};

using MultiEngine = MonoEngine<MultiSource>;

} // namespace cs

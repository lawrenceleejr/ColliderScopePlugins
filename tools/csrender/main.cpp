// csrender: offline renders for the ColliderScope plugin suite.
// Every DSP feature gets a WAV and a PNG of its XY picture here before it
// goes into a plugin.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "cscore/cscore.h"
#include "io.h"

namespace {

const char* kUsage =
    "csrender %s\n"
    "\n"
    "  csrender notes --out PREFIX [--notes NOTE:START:DUR[:VEL],...] [options]\n"
    "      Play a note list through an instrument: --instrument chamber (default), bubble, orbital, script, function, collision or mesh.\n"
    "      function:  --function 'sin(3*r - 4*t) / (1 + r)'   --range R   --height H   --lines N   --hidden 0|1   --spin DEG/S\n"
    "                 --style wires|sheet   --scanlines N   --shading S   --interlace N\n"
    "      collision: --event auto|minbias|dijet|zmumu|multijet|fourmuon|random   --field F   --pileup P   --detector 0|1   --form MS   --3d 0|1\n"
    "      scenes:    --scene tracking-planes|wire-chamber|calorimeter-shower|cherenkov-rings|spark-chamber|betatron|\n"
    "                 cyclotron|bunch-crossings|flux-tube|higgs-potential|neutrino-oscillation|eightfold-way (or 1-12)\n"
    "                 --form MS   --speed S   --detail D   --spin DEG/S\n"
    "      tracker:   --stage auto|module|exploded|tracker|tracks   --module 2s|ps   --speed S   --detail D   --spin DEG/S\n"
    "      feynman:   --diagram N (0 = auto, 1-10)   --form MS   --wiggle W   --motion CYCLES/S   --arrows 0|1\n"
    "      mesh:      --stl FILE.stl   --zoom Z   --spin DEG/S   --axis x|y|z   --crease DEG   --cull 0|1   --edges N\n"
    "      function, mesh and collision in 3D also take a camera: --azimuth DEG   --elevation DEG   --roll DEG   --perspective 0..1\n"
    "      script:  --text 'E = mc^2' (LaTeX-like: ^ _ \\frac \\sqrt \\alpha ...)   --write MS   --slant S\n"
    "      bubble:  --type auto|electron|pair|decay|beam|random   --field F   --bubbles N   --growth R   --boil B   --form MS\n"
    "      orbital: --orbital auto|1s|2s|2pz|2px|3dz2|...|4fz3   --spin DEG/S   --tilt DEG   --lines N   --level L   --bloom MS\n"
    "      chamber:\n"
    "      --type auto|alpha|muon|electron|random   --seed-mode random|key|global   --seed N\n"
    "      --wall off|shared|drone   --root NOTE   --share P\n"
    "      --diffusion R   --drift R   --condense MS   --size S   --attack MS   --release MS   --glide MS   --gain DB   --quality exact|clean   --voices N   --timing even|harmonic\n"
    "  csrender hit kick|snare|crash --out PREFIX [--radius R] [--seed N]\n"
    "      Render one drum hit.\n"
    "  csrender mux A.wav B.wav --out PREFIX [--mode interleave|lock|slot] [options]\n"
    "      Serialize two XY sources through the CS Mux engine.\n"
    "      --slice N (0 = auto)   --balance B   --switch fixed|closest   --jump W\n"
    "      --slot MS   --sensitivity S   --hits kick:T,snare:T,crash:T,b:T (slot mode, seconds)\n"
    "  csrender shape circle|square --out PREFIX [--hz F] [--size S] [--dur SECONDS]\n"
    "      A plain test picture.\n"
    "  csrender view IN.wav --out PREFIX\n"
    "      Draw the XY picture of an existing stereo WAV.\n"
    "\n"
    "  Common: --sr RATE (48000)   --at SECONDS (PNG shows the scope at this time;\n"
    "          default: all light from the whole render)   --decay D (0.55)   --px N (512)\n"
    "          --video FILE (also write the scope as raw rgb24 frames, PX x PX at 60 fps, for ffmpeg)\n"
    "  Writes PREFIX.wav and PREFIX.png. In XY mode left = X, right = Y.\n"
;

struct Args {
  std::vector<std::string> positional;
  std::map<std::string, std::string> options;
  bool has(const std::string& k) const { return options.count(k) > 0; }
  std::string str(const std::string& k, const std::string& fallback) const {
    auto it = options.find(k);
    return it == options.end() ? fallback : it->second;
  }
  double num(const std::string& k, double fallback) const {
    auto it = options.find(k);
    return it == options.end() ? fallback : std::atof(it->second.c_str());
  }
};

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "csrender: %s\n", message.c_str());
  std::exit(2);
}

template <typename T>
T choose(const Args& args, const std::string& key, const std::map<std::string, T>& table, T fallback) {
  if (!args.has(key))
    return fallback;
  auto it = table.find(args.str(key, ""));
  if (it == table.end())
    fail("unknown value for --" + key + ": " + args.str(key, ""));
  return it->second;
}

struct Note {
  int note = 69;
  double start = 0.0, duration = 1.0;
  int velocity = 80;
};

std::vector<Note> parseNotes(const std::string& text) {
  std::vector<Note> notes;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const std::size_t end = std::min(text.find(',', pos), text.size());
    const std::string item = text.substr(pos, end - pos);
    Note n;
    if (std::sscanf(item.c_str(), "%d:%lf:%lf:%d", &n.note, &n.start, &n.duration, &n.velocity) < 3)
      fail("bad note '" + item + "', expected NOTE:START:DUR[:VEL]");
    notes.push_back(n);
    pos = end + 1;
  }
  std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.start < b.start; });
  return notes;
}

// Plays the note list through any engine with noteOn / noteOff / process.
template <typename Engine>
csio::Audio playNotes(Engine& voice, const std::vector<Note>& notes, double sampleRate, double tail) {
  double end = 0.0;
  for (const Note& n : notes)
    end = std::max(end, n.start + n.duration);
  const auto total = static_cast<std::size_t>((end + tail + 0.05) * sampleRate);
  struct Event {
    std::size_t at;
    bool on;
    std::size_t index;
  };
  std::vector<Event> events;
  for (std::size_t i = 0; i < notes.size(); ++i) {
    events.push_back({static_cast<std::size_t>(std::llround(notes[i].start * sampleRate)), true, i});
    events.push_back({static_cast<std::size_t>(std::llround((notes[i].start + notes[i].duration) * sampleRate)), false, i});
  }
  std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    return a.at != b.at ? a.at < b.at : (!a.on && b.on); // offs before ons at the same sample
  });
  csio::Audio audio;
  audio.sampleRate = sampleRate;
  audio.x.assign(total, 0.0f);
  audio.y.assign(total, 0.0f);
  std::size_t pos = 0;
  auto advance = [&](std::size_t to) {
    to = std::min(to, total);
    if (to > pos)
      voice.process(audio.x.data() + pos, audio.y.data() + pos, static_cast<int>(to - pos));
    pos = std::max(pos, to);
  };
  for (const Event& e : events) {
    advance(e.at);
    if (e.on)
      voice.noteOn(notes[e.index].note, notes[e.index].velocity);
    else
      voice.noteOff(notes[e.index].note);
  }
  advance(total);
  return audio;
}

void readVoice(const Args& args, cs::VoiceParams& v) {
  v.size = args.num("size", v.size);
  v.attackMs = args.num("attack", v.attackMs);
  v.releaseMs = args.num("release", v.releaseMs);
  v.glideMs = args.num("glide", v.glideMs);
  v.gainDb = args.num("gain", v.gainDb);
  v.quality = choose<cs::Quality>(args, "quality", {{"exact", cs::Quality::ScopeExact}, {"clean", cs::Quality::Clean}}, v.quality);
  v.timing = choose<cs::Timing>(args, "timing", {{"even", cs::Timing::Even}, {"harmonic", cs::Timing::Harmonic}}, v.timing);
}

csio::Audio renderBubble(const Args& args, double sampleRate) {
  cs::BubbleParams p;
  readVoice(args, p.voice);
  p.type = choose<cs::BubbleType>(args, "type",
                                  {{"auto", cs::BubbleType::Auto},
                                   {"electron", cs::BubbleType::Electron},
                                   {"pair", cs::BubbleType::Pair},
                                   {"decay", cs::BubbleType::Decay},
                                   {"beam", cs::BubbleType::Beam},
                                   {"random", cs::BubbleType::Random}},
                                  p.type);
  p.seed = static_cast<std::uint64_t>(args.num("seed", 1));
  p.field = args.num("field", p.field);
  p.bubbles = static_cast<int>(args.num("bubbles", p.bubbles));
  p.growth = args.num("growth", p.growth);
  p.boil = args.num("boil", p.boil);
  p.formMs = args.num("form", p.formMs);
  cs::BubbleEngine voice;
  voice.prepare(sampleRate);
  voice.setParams(p);
  return playNotes(voice, parseNotes(args.str("notes", "69:0:1:80")), sampleRate, p.voice.releaseMs * 1e-3);
}

csio::Audio renderOrbital(const Args& args, double sampleRate) {
  cs::OrbitalParams p;
  readVoice(args, p.voice);
  if (args.has("orbital")) {
    p.orbital = -1;
    for (std::size_t i = 0; i < cs::kOrbitals.size(); ++i)
      if (args.str("orbital", "") == cs::kOrbitals[i].name)
        p.orbital = static_cast<int>(i) + 1;
    if (args.str("orbital", "") == "auto")
      p.orbital = 0;
    if (p.orbital < 0)
      fail("unknown orbital '" + args.str("orbital", "") + "' (try 1s, 2pz, 3dz2, 4fz3, auto)");
  }
  p.spin = args.num("spin", p.spin);
  p.tilt = args.num("tilt", p.tilt);
  p.lines = static_cast<int>(args.num("lines", p.lines));
  p.level = args.num("level", p.level);
  p.bloomMs = args.num("bloom", p.bloomMs);
  cs::OrbitalEngine voice;
  voice.prepare(sampleRate);
  voice.setParams(p);
  return playNotes(voice, parseNotes(args.str("notes", "69:0:1:80")), sampleRate, p.voice.releaseMs * 1e-3);
}

csio::Audio renderScript(const Args& args, double sampleRate) {
  cs::ScriptParams p;
  readVoice(args, p.voice);
  p.setText(args.str("text", "E = mc^2"));
  p.writeMs = args.num("write", p.writeMs);
  p.slant = args.num("slant", p.slant);
  cs::ScriptEngine voice;
  voice.prepare(sampleRate);
  voice.setParams(p);
  return playNotes(voice, parseNotes(args.str("notes", "69:0:1:80")), sampleRate, p.voice.releaseMs * 1e-3);
}

void readCamera(const Args& args, cs::Camera& camera) {
  camera.azimuth = args.num("azimuth", camera.azimuth);
  camera.elevation = args.num("elevation", camera.elevation);
  camera.roll = args.num("roll", camera.roll);
  camera.perspective = args.num("perspective", camera.perspective);
}

csio::Audio renderFunction(const Args& args, double sampleRate) {
  cs::SurfaceParams p;
  readVoice(args, p.voice);
  p.setFunction(args.str("function", "sin(3*r - 4*t) / (1 + r)"));
  cs::Expression check;
  std::string error;
  if (!check.compile(p.function.data(), &error))
    fail("function: " + error);
  p.range = args.num("range", p.range);
  p.height = args.num("height", p.height);
  p.lines = static_cast<int>(args.num("lines", p.lines));
  p.hidden = args.num("hidden", 1) != 0;
  p.spin = args.num("spin", p.spin);
  p.style = args.str("style", "wires") == "sheet" ? cs::SurfaceParams::Style::Sheet : cs::SurfaceParams::Style::Wires;
  p.scanlines = static_cast<int>(args.num("scanlines", p.scanlines));
  p.shading = args.num("shading", p.shading);
  p.interlace = static_cast<int>(args.num("interlace", p.interlace));
  readCamera(args, p.camera);
  cs::SurfaceEngine voice;
  voice.prepare(sampleRate);
  voice.setParams(p);
  return playNotes(voice, parseNotes(args.str("notes", "69:0:1:80")), sampleRate, p.voice.releaseMs * 1e-3);
}

csio::Audio renderCollision(const Args& args, double sampleRate) {
  cs::CollisionParams p;
  readVoice(args, p.voice);
  p.event = choose<cs::CollisionEvent>(args, "event",
                                       {{"auto", cs::CollisionEvent::Auto},
                                        {"minbias", cs::CollisionEvent::MinimumBias},
                                        {"dijet", cs::CollisionEvent::Dijet},
                                        {"zmumu", cs::CollisionEvent::ZToMuons},
                                        {"multijet", cs::CollisionEvent::Multijet},
                                        {"fourmuon", cs::CollisionEvent::FourMuons},
                                        {"random", cs::CollisionEvent::Random}},
                                       p.event);
  p.seed = static_cast<std::uint64_t>(args.num("seed", 1));
  p.field = args.num("field", p.field);
  p.pileup = args.num("pileup", p.pileup);
  p.detector = args.num("detector", 1) != 0;
  p.formMs = args.num("form", p.formMs);
  p.threeD = args.num("3d", 0) != 0;
  p.spin = args.num("spin", p.spin);
  readCamera(args, p.camera);
  cs::CollisionEngine voice;
  voice.prepare(sampleRate);
  voice.setParams(p);
  return playNotes(voice, parseNotes(args.str("notes", "69:0:1:80")), sampleRate, p.voice.releaseMs * 1e-3);
}

csio::Audio renderFeynman(const Args& args, double sampleRate) {
  cs::FeynmanParams p;
  readVoice(args, p.voice);
  p.diagram = static_cast<int>(args.num("diagram", 0)); // 0 = auto, 1.. = by number
  p.formMs = args.num("form", p.formMs);
  p.wiggle = args.num("wiggle", p.wiggle);
  p.motion = args.num("motion", p.motion);
  p.arrows = args.num("arrows", 1) != 0;
  cs::FeynmanEngine voice;
  voice.prepare(sampleRate);
  voice.setParams(p);
  return playNotes(voice, parseNotes(args.str("notes", "69:0:1:80")), sampleRate, p.voice.releaseMs * 1e-3);
}

csio::Audio renderScenes(const Args& args, double sampleRate) {
  cs::GalleryParams p;
  readVoice(args, p.voice);
  readCamera(args, p.camera);
  if (args.has("scene")) {
    const std::string want = args.str("scene", "");
    int found = -1;
    for (int i = 0; i < cs::kSceneCount; ++i) {
      std::string name = cs::sceneName(static_cast<cs::Scene>(i));
      std::transform(name.begin(), name.end(), name.begin(), [](char ch) { return ch == ' ' ? '-' : static_cast<char>(std::tolower(ch)); });
      if (name == want || std::to_string(i + 1) == want)
        found = i;
    }
    if (found < 0)
      fail("unknown scene '" + want + "'");
    p.scene = static_cast<cs::Scene>(found);
  }
  p.seed = static_cast<std::uint64_t>(args.num("seed", 1));
  p.formMs = args.num("form", p.formMs);
  p.speed = args.num("speed", p.speed);
  p.detail = args.num("detail", p.detail);
  p.spin = args.num("spin", p.spin);
  cs::GalleryEngine voice;
  voice.prepare(sampleRate);
  voice.setParams(p);
  return playNotes(voice, parseNotes(args.str("notes", "69:0:1:80")), sampleRate, p.voice.releaseMs * 1e-3);
}

csio::Audio renderTracker(const Args& args, double sampleRate) {
  cs::TrackerParams p;
  readVoice(args, p.voice);
  readCamera(args, p.camera);
  p.stage = choose<cs::TrackerStage>(args, "stage",
                                     {{"auto", cs::TrackerStage::Auto},
                                      {"module", cs::TrackerStage::Module},
                                      {"exploded", cs::TrackerStage::Exploded},
                                      {"tracker", cs::TrackerStage::Tracker},
                                      {"tracks", cs::TrackerStage::Tracks}},
                                     p.stage);
  p.module = choose<cs::ModuleType>(args, "module", {{"2s", cs::ModuleType::TwoS}, {"ps", cs::ModuleType::PS}}, p.module);
  p.seed = static_cast<std::uint64_t>(args.num("seed", 1));
  p.speed = args.num("speed", p.speed);
  p.detail = args.num("detail", p.detail);
  p.spin = args.num("spin", p.spin);
  cs::TrackerEngine voice;
  voice.prepare(sampleRate);
  voice.setParams(p);
  return playNotes(voice, parseNotes(args.str("notes", "69:0:1:80")), sampleRate, p.voice.releaseMs * 1e-3);
}

csio::Audio renderMesh(const Args& args, double sampleRate) {
  cs::MeshParams p;
  readVoice(args, p.voice);
  readCamera(args, p.camera);
  p.zoom = args.num("zoom", p.zoom);
  p.spin = args.num("spin", p.spin);
  p.spinAxis = choose<int>(args, "axis", {{"x", 0}, {"y", 1}, {"z", 2}}, p.spinAxis);
  p.crease = args.num("crease", p.crease);
  p.cull = args.num("cull", 1) != 0;
  p.maxEdges = static_cast<int>(args.num("edges", p.maxEdges));
  cs::MeshEngine voice;
  if (args.has("stl")) {
    std::ifstream file(args.str("stl", ""), std::ios::binary);
    if (!file)
      fail("cannot open " + args.str("stl", ""));
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::string error;
    auto mesh = cs::parseStl(bytes, &error);
    if (!mesh)
      fail(args.str("stl", "") + ": " + error);
    std::printf("%s: %zu vertices, %zu faces, %zu edges\n", args.str("stl", "").c_str(), mesh->vertices.size(),
                mesh->faces.size(), mesh->edges.size());
    voice.sourceForSetup().setMesh(std::move(mesh));
  }
  voice.prepare(sampleRate);
  voice.setParams(p);
  return playNotes(voice, parseNotes(args.str("notes", "69:0:1:80")), sampleRate, p.voice.releaseMs * 1e-3);
}

csio::Audio renderNotes(const Args& args, double sampleRate) {
  const std::string instrument = args.str("instrument", "chamber");
  if (instrument == "function")
    return renderFunction(args, sampleRate);
  if (instrument == "collision")
    return renderCollision(args, sampleRate);
  if (instrument == "mesh")
    return renderMesh(args, sampleRate);
  if (instrument == "feynman")
    return renderFeynman(args, sampleRate);
  if (instrument == "scenes")
    return renderScenes(args, sampleRate);
  if (instrument == "tracker")
    return renderTracker(args, sampleRate);
  if (instrument == "script")
    return renderScript(args, sampleRate);
  if (instrument == "bubble")
    return renderBubble(args, sampleRate);
  if (instrument == "orbital")
    return renderOrbital(args, sampleRate);
  if (instrument != "chamber")
    fail("unknown instrument '" + instrument + "' (chamber, bubble, orbital, script, function, collision, mesh, feynman, scenes, tracker)");
  cs::ChamberParams p;
  p.type = choose<cs::TypeMode>(args, "type",
                                {{"auto", cs::TypeMode::Auto},
                                 {"alpha", cs::TypeMode::Alpha},
                                 {"muon", cs::TypeMode::Muon},
                                 {"electron", cs::TypeMode::Electron},
                                 {"random", cs::TypeMode::Random}},
                                p.type);
  p.seedMode = choose<cs::SeedMode>(
      args, "seed-mode",
      {{"random", cs::SeedMode::Random}, {"key", cs::SeedMode::PerKey}, {"global", cs::SeedMode::Global}}, p.seedMode);
  p.wall = choose<cs::WallMode>(
      args, "wall", {{"off", cs::WallMode::Off}, {"shared", cs::WallMode::Shared}, {"drone", cs::WallMode::Drone}},
      p.wall);
  p.quality = choose<cs::Quality>(args, "quality", {{"exact", cs::Quality::ScopeExact}, {"clean", cs::Quality::Clean}},
                                  p.quality);
  p.voices = static_cast<int>(args.num("voices", 1));
  p.timing = choose<cs::Timing>(args, "timing", {{"even", cs::Timing::Even}, {"harmonic", cs::Timing::Harmonic}}, p.timing);
  p.seed = static_cast<std::uint64_t>(args.num("seed", 1));
  p.wallRoot = static_cast<int>(args.num("root", p.wallRoot));
  p.wallShare = args.num("share", p.wallShare);
  p.diffusionRate = args.num("diffusion", p.diffusionRate);
  p.drift = args.num("drift", p.drift);
  p.condenseMs = args.num("condense", p.condenseMs);
  p.size = args.num("size", p.size);
  p.attackMs = args.num("attack", p.attackMs);
  p.glideMs = args.num("glide", p.glideMs);
  p.releaseMs = args.num("release", p.releaseMs);
  p.gainDb = args.num("gain", p.gainDb);

  const std::vector<Note> notes = parseNotes(args.str("notes", "69:0:1:80"));
  double end = 0.0;
  for (const Note& n : notes)
    end = std::max(end, n.start + n.duration);
  const auto total = static_cast<std::size_t>((end + p.releaseMs * 1e-3 + 0.05) * sampleRate);

  // Sample-accurate events. The engine keeps track of which keys are held.
  struct Event {
    std::size_t at;
    bool on;
    std::size_t index;
  };
  std::vector<Event> events;
  for (std::size_t i = 0; i < notes.size(); ++i) {
    events.push_back({static_cast<std::size_t>(std::llround(notes[i].start * sampleRate)), true, i});
    events.push_back({static_cast<std::size_t>(std::llround((notes[i].start + notes[i].duration) * sampleRate)), false, i});
  }
  std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    return a.at != b.at ? a.at < b.at : (!a.on && b.on); // offs before ons at the same sample
  });

  cs::ChamberEngine voice; // synchronous: sample-exact and repeatable
  voice.prepare(sampleRate);
  voice.setParams(p);
  csio::Audio audio;
  audio.sampleRate = sampleRate;
  audio.x.assign(total, 0.0f);
  audio.y.assign(total, 0.0f);
  std::size_t pos = 0;
  auto advance = [&](std::size_t to) {
    to = std::min(to, total);
    if (to > pos)
      voice.process(audio.x.data() + pos, audio.y.data() + pos, static_cast<int>(to - pos));
    pos = std::max(pos, to);
  };
  for (const Event& e : events) {
    advance(e.at);
    if (e.on)
      voice.noteOn(notes[e.index].note, notes[e.index].velocity);
    else
      voice.noteOff(notes[e.index].note);
  }
  advance(total);
  return audio;
}

csio::Audio renderHit(const Args& args, double sampleRate) {
  if (args.positional.size() < 2)
    fail("hit needs a name: kick, snare or crash");
  const std::string& name = args.positional[1];
  std::vector<cs::XY> hit;
  if (name == "kick")
    hit = cs::kickSpiral(sampleRate, args.num("radius", cs::kWallRadius * 0.9), 0.0,
                         static_cast<std::uint64_t>(args.num("seed", 3)));
  else if (name == "snare")
    hit = cs::snareRing(sampleRate, static_cast<std::uint64_t>(args.num("seed", 1)));
  else if (name == "crash")
    hit = cs::crashRing(sampleRate, static_cast<std::uint64_t>(args.num("seed", 9)));
  else
    fail("unknown hit '" + name + "'");
  cs::radialSoftLimit(hit);
  hit.resize(cs::slotLength(hit)); // the slot ends before the beam parks
  csio::Audio audio;
  audio.sampleRate = sampleRate;
  for (const cs::XY& s : hit) {
    audio.x.push_back(s.x);
    audio.y.push_back(s.y);
  }
  return audio;
}

// The scope over time: one frame per 1/60 s, with persistence, as raw rgb24.
void writeVideo(const Args& args, const csio::Audio& audio, const std::string& path) {
  cs::ScopeView view(static_cast<int>(args.num("px", 512)));
  view.setDecay(args.num("decay", 0.55));
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f)
    fail("cannot write " + path);
  const double frame = audio.sampleRate / 60.0;
  const auto total = static_cast<int>(audio.x.size());
  std::vector<std::uint8_t> rgb;
  for (int k = 0;; ++k) {
    const int a = static_cast<int>(k * frame), b = std::min(total, static_cast<int>((k + 1) * frame));
    if (a >= b)
      break;
    view.beginFrame(1.0 / 60.0);
    view.addSamples(audio.x.data() + a, audio.y.data() + a, b - a);
    view.render(rgb);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
  }
  std::fclose(f);
}

csio::Audio renderShape(const Args& args, double sampleRate) {
  if (args.positional.size() < 2)
    fail("shape needs a name: circle or square");
  const bool isSquare = args.positional[1] == "square";
  if (!isSquare && args.positional[1] != "circle")
    fail("unknown shape '" + args.positional[1] + "'");
  const double hz = args.num("hz", 55.0), size = args.num("size", 0.8);
  const auto n = static_cast<std::size_t>(args.num("dur", 2.0) * sampleRate);
  csio::Audio audio;
  audio.sampleRate = sampleRate;
  audio.x.resize(n);
  audio.y.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    double u = hz * static_cast<double>(i) / sampleRate;
    u -= std::floor(u);
    if (isSquare) {
      const int side = static_cast<int>(u * 4.0);
      const double t = (u * 4.0 - side) * 2.0 - 1.0;
      const double px[] = {t, 1.0, -t, -1.0}, py[] = {-1.0, t, 1.0, -t};
      audio.x[i] = static_cast<float>(size * px[side]);
      audio.y[i] = static_cast<float>(size * py[side]);
    } else {
      audio.x[i] = static_cast<float>(size * std::cos(6.283185307179586 * u));
      audio.y[i] = static_cast<float>(size * std::sin(6.283185307179586 * u));
    }
  }
  return audio;
}

csio::Audio renderMux(const Args& args) {
  if (args.positional.size() < 3)
    fail("mux needs two input WAVs: A and B");
  csio::Audio a, b;
  std::string error;
  if (!csio::readWav(args.positional[1], a, error) || !csio::readWav(args.positional[2], b, error))
    fail(error);
  if (a.sampleRate != b.sampleRate)
    fail("A and B have different sample rates");
  const std::size_t n = a.x.size();
  b.x.resize(n, 0.0f);
  b.y.resize(n, 0.0f);

  cs::MuxParams p;
  p.mode = choose<cs::MuxMode>(
      args, "mode", {{"interleave", cs::MuxMode::Interleave}, {"lock", cs::MuxMode::CycleLock}, {"slot", cs::MuxMode::Slot}},
      p.mode);
  p.switchPoint = choose<cs::SwitchPoint>(
      args, "switch", {{"fixed", cs::SwitchPoint::Fixed}, {"closest", cs::SwitchPoint::Closest}}, p.switchPoint);
  p.slice = static_cast<int>(args.num("slice", 0));
  p.balance = args.num("balance", p.balance);
  p.jumpWeight = args.num("jump", p.jumpWeight);
  p.slotMs = args.num("slot", p.slotMs);
  p.sensitivity = args.num("sensitivity", p.sensitivity);

  // Slot triggers, as MIDI notes would arrive: name:seconds.
  struct Trigger {
    std::size_t at;
    int kind; // 0 kick, 1 snare, 2 crash, 3 the B input
  };
  std::vector<Trigger> triggers;
  const std::string hits = args.str("hits", "");
  for (std::size_t pos = 0; pos < hits.size();) {
    const std::size_t end = std::min(hits.find(',', pos), hits.size());
    const std::string item = hits.substr(pos, end - pos);
    const std::size_t colon = item.find(':');
    const std::string name = item.substr(0, colon);
    const std::map<std::string, int> kinds = {{"kick", 0}, {"snare", 1}, {"crash", 2}, {"b", 3}};
    if (colon == std::string::npos || !kinds.count(name))
      fail("bad trigger '" + item + "', expected kick|snare|crash|b:SECONDS");
    triggers.push_back({static_cast<std::size_t>(std::atof(item.c_str() + colon + 1) * a.sampleRate), kinds.at(name)});
    pos = end + 1;
  }
  std::sort(triggers.begin(), triggers.end(), [](const Trigger& l, const Trigger& r) { return l.at < r.at; });

  cs::MuxEngine engine;
  engine.prepare(a.sampleRate);
  engine.setParams(p);
  csio::Audio out;
  out.sampleRate = a.sampleRate;
  out.x.resize(n);
  out.y.resize(n);
  std::size_t pos = 0;
  auto advance = [&](std::size_t to) {
    to = std::min(to, n);
    if (to > pos)
      engine.process(a.x.data() + pos, a.y.data() + pos, b.x.data() + pos, b.y.data() + pos, out.x.data() + pos,
                     out.y.data() + pos, nullptr, nullptr, static_cast<int>(to - pos));
    pos = std::max(pos, to);
  };
  for (const Trigger& t : triggers) {
    advance(t.at);
    if (t.kind == 3)
      engine.triggerB();
    else
      engine.triggerHit(static_cast<cs::HitType>(t.kind));
  }
  advance(n);
  return out;
}

void writePicture(const Args& args, const csio::Audio& audio, const std::string& path) {
  cs::ScopeView view(static_cast<int>(args.num("px", 512)));
  view.setDecay(args.num("decay", 0.55));
  const auto total = static_cast<int>(audio.x.size());
  if (args.has("at")) {
    // The scope as it looks at one moment: 60 fps frames with persistence.
    const double frame = audio.sampleRate / 60.0;
    const int last = std::min(total, static_cast<int>(args.num("at", 0.0) * audio.sampleRate));
    for (int k = 0;; ++k) {
      const int a = static_cast<int>(k * frame), b = std::min(last, static_cast<int>((k + 1) * frame));
      if (a >= b)
        break;
      view.beginFrame(1.0 / 60.0);
      view.addSamples(audio.x.data() + a, audio.y.data() + a, b - a);
    }
  } else {
    view.addSamples(audio.x.data(), audio.y.data(), total);
  }
  std::vector<std::uint8_t> rgb;
  view.render(rgb);
  if (!csio::writePng(path, view.size(), view.size(), rgb))
    fail("cannot write " + path);
}

} // namespace

int main(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("--", 0) == 0) {
      const std::string key = a.substr(2);
      if (key == "version" || key == "help")
        args.options[key] = "1";
      else if (i + 1 < argc)
        args.options[key] = argv[++i];
      else
        fail("--" + key + " needs a value");
    } else {
      args.positional.push_back(a);
    }
  }
  if (args.has("version")) {
    std::printf("csrender %s\n", cs::version());
    return 0;
  }
  if (args.positional.empty() || args.has("help")) {
    std::printf(kUsage, cs::version());
    return 0;
  }
  if (!args.has("out"))
    fail("--out PREFIX is required");

  const double sampleRate = args.num("sr", 48000.0);
  const std::string& command = args.positional[0];
  const std::string prefix = args.str("out", "");
  csio::Audio audio;
  bool writeAudio = true;
  if (command == "notes") {
    audio = renderNotes(args, sampleRate);
  } else if (command == "hit") {
    audio = renderHit(args, sampleRate);
  } else if (command == "shape") {
    audio = renderShape(args, sampleRate);
  } else if (command == "mux") {
    audio = renderMux(args);
  } else if (command == "view") {
    if (args.positional.size() < 2)
      fail("view needs an input WAV");
    std::string error;
    if (!csio::readWav(args.positional[1], audio, error))
      fail(error);
    writeAudio = false;
  } else {
    fail("unknown command '" + command + "'");
  }

  float peak = 0.0f;
  for (std::size_t i = 0; i < audio.x.size(); ++i)
    peak = std::max({peak, std::abs(audio.x[i]), std::abs(audio.y[i])});
  if (writeAudio && !csio::writeWav(prefix + ".wav", audio))
    fail("cannot write " + prefix + ".wav");
  writePicture(args, audio, prefix + ".png");
  if (args.has("video"))
    writeVideo(args, audio, args.str("video", ""));
  std::printf("%s: %.3f s, peak %.3f\n", prefix.c_str(), static_cast<double>(audio.x.size()) / audio.sampleRate,
              static_cast<double>(peak));
  if (peak > static_cast<float>(cs::kPeak)) {
    std::fprintf(stderr, "csrender: peak %.3f exceeds %.2f\n", static_cast<double>(peak), cs::kPeak);
    return 1;
  }
  return 0;
}

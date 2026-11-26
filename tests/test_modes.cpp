// The Function, Collision and Mesh instruments, and the multi-instrument
// source that ColliderScope plays them through.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "analysis.h"
#include "cscore/cscore.h"

using Catch::Matchers::WithinAbs;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

struct Render {
  std::vector<float> x, y;
};

template <typename Engine>
Render play(Engine& voice, const typename Engine::Params& params, int note, int velocity, double held, double tail) {
  voice.prepare(kSampleRate);
  voice.setParams(params);
  const auto on = static_cast<std::size_t>(held * kSampleRate), off = static_cast<std::size_t>(tail * kSampleRate);
  Render r{std::vector<float>(on + off), std::vector<float>(on + off)};
  voice.noteOn(note, velocity);
  voice.process(r.x.data(), r.y.data(), static_cast<int>(on));
  voice.noteOff(note);
  voice.process(r.x.data() + on, r.y.data() + on, static_cast<int>(off));
  return r;
}

double peakRadius(const Render& r) {
  double peak = 0.0;
  for (std::size_t i = 0; i < r.x.size(); ++i)
    peak = std::max(peak, analysis::radius(r.x[i], r.y[i]));
  return peak;
}

template <typename Engine>
void checkPitchAndRest(const typename Engine::Params& params) {
  for (int note : {33, 57, 81}) {
    Engine voice;
    const Render r = play(voice, params, note, 80, 0.6, 0.1);
    const double period = analysis::periodByAutocorrelation(r.x, kSampleRate, static_cast<std::size_t>(0.25 * kSampleRate),
                                                           kSampleRate / cs::midiToHz(note));
    CAPTURE(note);
    CHECK(std::abs(cs::cents(kSampleRate / period, cs::midiToHz(note))) < 1.0);
    const auto off = static_cast<std::size_t>(0.6 * kSampleRate);
    CHECK(std::abs(r.x[off - 1]) + std::abs(r.y[off - 1]) > 0.002);
    for (std::size_t i = off + 24; i < r.x.size(); ++i) { // silent within 0.5 ms
      REQUIRE(std::abs(r.x[i]) < 1e-4f);
      REQUIRE(std::abs(r.y[i]) < 1e-4f);
    }
  }
}

double value(const char* source, double x = 0.0, double y = 0.0, double t = 0.0) {
  cs::Expression e;
  std::string error;
  INFO(source << ": " << error);
  REQUIRE(e.compile(source, &error));
  return e.eval(x, y, t);
}

std::size_t segments(const std::vector<cs::Stroke>& strokes) {
  std::size_t n = 0;
  for (const cs::Stroke& s : strokes)
    n += s.size() - 1;
  return n;
}

// A cube as 12 triangles.
std::vector<std::array<cs::Vec3, 3>> cube() {
  const cs::Vec3 v[8] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
  const int quads[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {2, 3, 7, 6}, {1, 2, 6, 5}, {3, 0, 4, 7}};
  std::vector<std::array<cs::Vec3, 3>> t;
  for (const auto& q : quads) {
    t.push_back({v[q[0]], v[q[1]], v[q[2]]});
    t.push_back({v[q[0]], v[q[2]], v[q[3]]});
  }
  return t;
}

std::vector<std::uint8_t> binaryStl(const std::vector<std::array<cs::Vec3, 3>>& triangles) {
  std::vector<std::uint8_t> bytes(84 + triangles.size() * 50, 0);
  const auto count = static_cast<std::uint32_t>(triangles.size());
  std::memcpy(bytes.data() + 80, &count, 4);
  for (std::size_t i = 0; i < triangles.size(); ++i)
    for (std::size_t k = 0; k < 3; ++k) {
      const float v[3] = {static_cast<float>(triangles[i][k].x), static_cast<float>(triangles[i][k].y), static_cast<float>(triangles[i][k].z)};
      std::memcpy(bytes.data() + 84 + i * 50 + 12 + k * 12, v, 12);
    }
  return bytes;
}

} // namespace

// ----------------------------------------------------------------- expressions

TEST_CASE("Expressions: arithmetic, precedence, names and functions", "[expr]") {
  CHECK_THAT(value("1 + 2 * 3"), WithinAbs(7.0, 1e-12));
  CHECK_THAT(value("(1 + 2) * 3"), WithinAbs(9.0, 1e-12));
  CHECK_THAT(value("2 ^ 3 ^ 2"), WithinAbs(512.0, 1e-9)); // right to left
  CHECK_THAT(value("-x^2", 3.0), WithinAbs(-9.0, 1e-12)); // the power binds first
  CHECK_THAT(value("2 - -3"), WithinAbs(5.0, 1e-12));
  CHECK_THAT(value("10 / 4 / 5"), WithinAbs(0.5, 1e-12));
  CHECK_THAT(value("1.5e1 + .5"), WithinAbs(15.5, 1e-12));
  CHECK_THAT(value("x * y + t", 2.0, 3.0, 4.0), WithinAbs(10.0, 1e-12));
  CHECK_THAT(value("r", 3.0, 4.0), WithinAbs(5.0, 1e-12));
  CHECK_THAT(value("theta", 0.0, 2.0), WithinAbs(kPi / 2, 1e-12));
  CHECK_THAT(value("sin(pi / 2) + cos(0) + exp(0) + ln(e)"), WithinAbs(4.0, 1e-12));
  CHECK_THAT(value("sqrt(abs(-16)) + floor(2.7) + sign(-3)"), WithinAbs(5.0, 1e-12));
  CHECK_THAT(value("max(2, 5) + min(2, 5) + mod(7, 4) + pow(2, 5) + atan2(0, 1)"), WithinAbs(42.0, 1e-12));
  CHECK_THAT(value("sinc(0) + gauss(0)"), WithinAbs(2.0, 1e-12));
  CHECK_THAT(value("\\sin(x)", kPi / 2), WithinAbs(1.0, 1e-12)); // LaTeX habits are forgiven
  // Anything not finite comes back as 0, so a pole does not break the picture.
  CHECK(value("1 / x") == 0.0);
  CHECK(value("log(-1)") == 0.0);

  cs::Expression e;
  std::string error;
  for (const char* bad : {"", "1 +", "foo", "sin 3", "(1 + 2", "2 * * 3", "max(1)", "3 4", "x y"}) {
    error.clear();
    CAPTURE(bad);
    CHECK_FALSE(e.compile(bad, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(e.valid());
  }
  REQUIRE(e.compile("sin(x) * t"));
  CHECK(e.usesTime());
  REQUIRE(e.compile("sin(x) * y"));
  CHECK_FALSE(e.usesTime());
}

// -------------------------------------------------------------------- Function

TEST_CASE("Function: a wire mesh over the plane, with the far side hidden", "[function]") {
  cs::SurfaceParams p;
  p.lines = 10;
  cs::Expression flat, hill;
  REQUIRE(flat.compile("0"));
  REQUIRE(hill.compile("2 * gauss(r)"));

  // A flat sheet hides nothing: every wire is whole, 10 each way.
  for (bool hidden : {false, true}) {
    p.hidden = hidden;
    const auto strokes = cs::surfaceStrokes(flat, p, 0.0, 0.0);
    CHECK(strokes.size() == 20);
  }
  // A hill hides what is behind it: fewer points drawn than with hiding off.
  auto points = [&](bool hidden) {
    p.hidden = hidden;
    std::size_t n = 0;
    double length = 0.0;
    for (const cs::Stroke& s : cs::surfaceStrokes(hill, p, 0.0, 0.0))
      for (std::size_t i = 1; i < s.size(); ++i) {
        length += std::hypot(s[i].x - s[i - 1].x, s[i].y - s[i - 1].y);
        ++n;
      }
    return length;
  };
  CHECK(points(true) < 0.92 * points(false));

  // Whatever the function and the camera, the picture stays on the screen.
  cs::Expression wild;
  REQUIRE(wild.compile("100 * sin(5 * x) * cos(7 * y) + t"));
  for (double azimuth : {0.0, 45.0, 200.0})
    for (double perspective : {0.0, 1.0}) {
      p.camera.azimuth = azimuth;
      p.camera.perspective = perspective;
      p.camera.elevation = azimuth / 3.0;
      for (const cs::Stroke& s : cs::surfaceStrokes(wild, p, 1.0, 30.0))
        for (const cs::Vec2& q : s)
          REQUIRE(std::hypot(q.x, q.y) <= 0.95);
    }
}

TEST_CASE("Function: pitch and hard rest; a bad function is a flat sheet", "[function][acceptance]") {
  cs::SurfaceParams p;
  p.setFunction("cos(2 * x) * sin(2 * y)");
  p.spin = 0.0;
  checkPitchAndRest<cs::SurfaceEngine>(p);

  p.setFunction("this is not a function");
  cs::SurfaceEngine voice;
  const Render r = play(voice, p, 57, 80, 0.2, 0.0);
  CHECK_FALSE(voice.source().error().empty());
  CHECK(peakRadius(r) > 0.3); // still drawn: the plane itself
  CHECK(peakRadius(r) <= 0.95);
}

TEST_CASE("Function: t moves the surface", "[function]") {
  cs::SurfaceParams p;
  p.setFunction("sin(2 * x - 6 * t)");
  p.spin = 0.0;
  p.voice.size = 1.0;
  cs::SurfaceEngine voice;
  const Render r = play(voice, p, 57, 80, 0.6, 0.0);
  double change = 0.0;
  const std::size_t period = 4800; // 0.1 s apart, a whole number of note periods (22 at 220 Hz)
  for (std::size_t i = 9600; i < 14400; ++i)
    change = std::max(change, std::abs(static_cast<double>(r.x[i] - r.x[i + period])) + std::abs(static_cast<double>(r.y[i] - r.y[i + period])));
  CHECK(change > 0.05);
  CHECK(peakRadius(r) <= 0.95);
}

// ------------------------------------------------------------------- Collision

TEST_CASE("Collision: tracks curve by charge and momentum", "[collision]") {
  const cs::Stroke stiff = cs::collisionTrackPath({20.0, 0.3, +1, false}, 0.5);
  const cs::Stroke soft = cs::collisionTrackPath({0.3, 0.3, +1, false}, 0.5);
  const cs::Stroke softMinus = cs::collisionTrackPath({0.3, 0.3, -1, false}, 0.5);
  const cs::Stroke muon = cs::collisionTrackPath({20.0, 0.3, +1, true}, 0.5);
  auto reach = [](const cs::Stroke& s) {
    double r = 0.0;
    for (const cs::Vec2& p : s)
      r = std::max(r, std::hypot(p.x, p.y));
    return r;
  };
  auto side = [](const cs::Stroke& s) { // which side of its starting direction it ends up
    const cs::Vec2 end = s[s.size() / 2];
    return end.y * std::cos(0.3) - end.x * std::sin(0.3);
  };
  CHECK(stiff.front().x == 0.0); // from the collision point
  CHECK(stiff.front().y == 0.0);
  CHECK_THAT(reach(stiff), WithinAbs(cs::kTrackerRadius, 0.02)); // straight out to the tracker's edge
  CHECK_THAT(std::atan2(stiff.back().y, stiff.back().x), WithinAbs(0.3, 0.03));
  CHECK(reach(soft) < 0.6 * cs::kTrackerRadius); // too soft to get out: it curls up
  CHECK(side(soft) > 0.01);
  CHECK(side(softMinus) < -0.01); // the other charge, the other way
  CHECK_THAT(reach(muon), WithinAbs(cs::kMuonRadius, 0.02));
  // A stronger field curls the same track tighter.
  CHECK(reach(cs::collisionTrackPath({0.6, 0.0, 1, false}, 1.0)) < reach(cs::collisionTrackPath({0.6, 0.0, 1, false}, 0.1)));
}

TEST_CASE("Collision: each kind of event has what it should", "[collision]") {
  cs::CollisionParams p;
  p.pileup = 0.0;
  auto born = [&](cs::CollisionEvent event, int velocity = 80, std::uint64_t seed = 1) {
    p.event = event;
    p.seed = seed;
    cs::CollisionSource source;
    source.birth(p, 60, velocity, 1);
    return source;
  };
  auto muons = [](const cs::CollisionSource& s) {
    std::vector<double> phi;
    for (const cs::CollisionTrack& t : s.tracks())
      if (t.muon)
        phi.push_back(t.phi);
    return phi;
  };
  CHECK(born(cs::CollisionEvent::Auto, 20).event() == cs::CollisionEvent::MinimumBias);
  CHECK(born(cs::CollisionEvent::Auto, 50).event() == cs::CollisionEvent::Dijet);
  CHECK(born(cs::CollisionEvent::Auto, 80).event() == cs::CollisionEvent::ZToMuons);
  CHECK(born(cs::CollisionEvent::Auto, 100).event() == cs::CollisionEvent::Multijet);
  CHECK(born(cs::CollisionEvent::Auto, 127).event() == cs::CollisionEvent::FourMuons);

  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    const auto z = muons(born(cs::CollisionEvent::ZToMuons, 80, seed));
    REQUIRE(z.size() == 2);
    CHECK(std::abs(std::remainder(z[1] - z[0] - kPi, 2 * kPi)) < 0.5); // back to back
    CHECK(muons(born(cs::CollisionEvent::FourMuons, 80, seed)).size() == 4);
    CHECK(muons(born(cs::CollisionEvent::MinimumBias, 80, seed)).empty());

    const cs::CollisionSource dijet = born(cs::CollisionEvent::Dijet, 80, seed);
    CHECK(dijet.tracks().size() >= 10);
    CHECK(dijet.towers().size() == 20); // five towers in each layer, for each jet
    const cs::CollisionSource multijet = born(cs::CollisionEvent::Multijet, 80, seed);
    CHECK(multijet.towers().size() >= 40);
    CHECK(multijet.tracks().size() > dijet.tracks().size());
    // No pile-up asked for, so a quiet event stays quiet.
    CHECK(born(cs::CollisionEvent::MinimumBias, 80, seed).tracks().size() <= 12);
  }
  p.pileup = 1.0;
  CHECK(born(cs::CollisionEvent::MinimumBias).tracks().size() >= 21);
}

TEST_CASE("Collision: pitch, picture and hard rest", "[collision][acceptance]") {
  cs::CollisionParams p;
  p.event = GENERATE(cs::CollisionEvent::MinimumBias, cs::CollisionEvent::Dijet, cs::CollisionEvent::FourMuons);
  SECTION("pitch and rest") { checkPitchAndRest<cs::CollisionEngine>(p); }
  SECTION("within r <= 0.95") {
    p.voice.size = 1.0;
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
      p.seed = seed;
      p.field = 0.2 * static_cast<double>(seed);
      cs::CollisionEngine voice;
      const Render r = play(voice, p, 57, 80, 0.4, 0.0);
      CHECK(peakRadius(r) <= 0.95);
      CHECK(peakRadius(r) > 0.4);
    }
  }
}

TEST_CASE("Collision: the event flies out from the collision point", "[collision]") {
  cs::CollisionParams p;
  p.event = cs::CollisionEvent::FourMuons;
  p.detector = false;
  p.formMs = 200.0;
  p.voice.size = 1.0;
  cs::CollisionEngine voice;
  const Render r = play(voice, p, 57, 80, 0.4, 0.0);
  auto reach = [&](double at) {
    double w = 0.0;
    const auto from = static_cast<std::size_t>(at * kSampleRate);
    for (std::size_t i = from; i < from + 480; ++i)
      w = std::max(w, analysis::radius(r.x[i], r.y[i]));
    return w;
  };
  CHECK(reach(0.02) < 0.2);
  CHECK(reach(0.02) < reach(0.1));
  CHECK(reach(0.1) < reach(0.18));
  CHECK_THAT(reach(0.3), WithinAbs(cs::kMuonRadius, 0.02));
}

// ------------------------------------------------------------------------ Mesh

TEST_CASE("Mesh: corners are welded, edges found, the model centred and scaled", "[mesh]") {
  const auto mesh = cs::makeMesh(cube(), "cube");
  CHECK(mesh->vertices.size() == 8);
  CHECK(mesh->faces.size() == 12);
  CHECK(mesh->edges.size() == 18); // 12 edges of the cube and a diagonal across each face
  for (const cs::Vec3& v : mesh->vertices)
    CHECK_THAT(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), WithinAbs(1.0, 1e-9));
  for (const cs::Mesh::Edge& e : mesh->edges)
    CHECK(e.right >= 0); // closed: a face on both sides of every edge
  for (const cs::Vec3& n : mesh->normals)
    CHECK_THAT(std::abs(n.x) + std::abs(n.y) + std::abs(n.z), WithinAbs(1.0, 1e-9)); // axis-aligned faces

  // One triangle alone has three open edges.
  const auto open = cs::makeMesh({{cs::Vec3{0, 0, 0}, cs::Vec3{1, 0, 0}, cs::Vec3{0, 1, 0}}});
  REQUIRE(open->edges.size() == 3);
  for (const cs::Mesh::Edge& e : open->edges)
    CHECK(e.right < 0);
  CHECK(cs::makeMesh({})->edges.empty());
}

TEST_CASE("Mesh: STL files, binary and ASCII", "[mesh]") {
  std::string error;
  const auto binary = cs::parseStl(binaryStl(cube()), &error);
  REQUIRE(binary);
  CHECK(binary->vertices.size() == 8);
  CHECK(binary->faces.size() == 12);

  std::string ascii = "solid cube\n";
  for (const auto& t : cube()) {
    ascii += " facet normal 0 0 0\n  outer loop\n";
    for (const cs::Vec3& v : t)
      ascii += "   vertex " + std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z) + "\n";
    ascii += "  endloop\n endfacet\n";
  }
  ascii += "endsolid cube\n";
  const auto text = cs::parseStl(std::vector<std::uint8_t>(ascii.begin(), ascii.end()), &error);
  REQUIRE(text);
  CHECK(text->vertices.size() == 8);
  CHECK(text->edges.size() == 18);

  // Not STL, or nothing in it: refused, with a reason.
  for (const std::string& bad : {std::string("hello"), std::string(), std::string("solid empty\nendsolid empty\n"), std::string(200, 'x')}) {
    error.clear();
    CHECK(cs::parseStl(std::vector<std::uint8_t>(bad.begin(), bad.end()), &error) == nullptr);
    CHECK_FALSE(error.empty());
  }
  // A cap on size.
  CHECK(cs::parseStl(binaryStl(cube()), &error, 4)->faces.size() == 4);
}

TEST_CASE("Mesh: the outline and the creases are drawn, the flat diagonals are not", "[mesh]") {
  const auto mesh = cs::makeMesh(cube());
  cs::MeshParams p;
  p.camera = {30.0, 25.0, 0.0, 0.0};
  p.cull = false;
  CHECK(segments(cs::meshStrokes(*mesh, p, 0.0)) == 12); // every edge of the cube
  p.cull = true;
  CHECK(segments(cs::meshStrokes(*mesh, p, 0.0)) == 9); // three faces in view: nine edges
  p.crease = 95.0; // a cube's faces meet at 90 degrees: no creases now, only the outline
  CHECK(segments(cs::meshStrokes(*mesh, p, 0.0)) == 6);
  // Edges that meet are joined into long strokes.
  p.crease = 30.0;
  CHECK(cs::meshStrokes(*mesh, p, 0.0).size() <= 4);
  // The limit keeps the longest.
  p.maxEdges = 10;
  p.cull = false;
  CHECK(segments(cs::meshStrokes(*mesh, p, 0.0)) == 10);

  // A smooth shape is mostly outline; everything stays on the screen at any angle.
  const auto torus = cs::torusMesh();
  cs::MeshParams q;
  q.zoom = 1.0;
  for (double spin : {0.0, 37.0, 190.0})
    for (int axis : {0, 1, 2}) {
      q.spinAxis = axis;
      q.camera.perspective = spin / 190.0;
      const auto strokes = cs::meshStrokes(*torus, q, spin);
      CHECK(segments(strokes) > 20);
      CHECK(segments(strokes) < torus->edges.size() / 4);
      for (const cs::Stroke& s : strokes)
        for (const cs::Vec2& v : s)
          REQUIRE(std::hypot(v.x, v.y) <= 1.0 + 1e-9);
    }
}

TEST_CASE("Mesh: pitch, picture and hard rest; a model can be swapped in", "[mesh][acceptance]") {
  cs::MeshParams p;
  p.spin = 0.0;
  checkPitchAndRest<cs::MeshEngine>(p);

  p.voice.size = 1.0;
  p.zoom = 1.0;
  p.spin = 90.0;
  cs::MeshEngine voice;
  voice.sourceForSetup().setMesh(cs::makeMesh(cube(), "cube"));
  const Render r = play(voice, p, 57, 80, 0.4, 0.0);
  CHECK(peakRadius(r) <= 0.95 + 0.06); // zoom 1 and size 1 fill the screen; the default zoom of 0.9 does not
  CHECK(voice.source().mesh()->name == "cube");
  voice.sourceForSetup().setMesh(nullptr); // back to the built-in torus
  CHECK(voice.source().mesh()->name == "torus");
  p.zoom = 0.9;
  cs::MeshEngine fitted;
  CHECK(peakRadius(play(fitted, p, 57, 80, 0.4, 0.0)) <= 0.95);
}

// ----------------------------------------------------------------------- Multi

TEST_CASE("ColliderScope: every instrument plays, and they can be switched mid-note", "[multi]") {
  cs::MultiParams p;
  p.script.setText("E = mc^2");
  p.surface.setFunction("sin(x) * cos(y)");
  cs::MultiEngine voice;
  voice.prepare(kSampleRate);
  std::vector<float> x(9600), y(9600);
  for (int i = 0; i < cs::kInstrumentCount; ++i) {
    p.instrument = static_cast<cs::Instrument>(i);
    voice.setParams(p);
    voice.noteOn(57, 80);
    voice.process(x.data(), y.data(), 9600);
    double peak = 0.0;
    for (std::size_t k = 4800; k < x.size(); ++k)
      peak = std::max(peak, analysis::radius(x[k], y[k]));
    CAPTURE(i);
    CHECK(peak > 0.15);
    CHECK(peak <= 0.95);
    // Switch to the next instrument while this note is still held.
    p.instrument = static_cast<cs::Instrument>((i + 1) % cs::kInstrumentCount);
    voice.setParams(p);
    voice.process(x.data(), y.data(), 9600);
    CHECK(voice.active());
    voice.noteOff(57);
    voice.process(x.data(), y.data(), 480);
    CHECK(x[479] == 0.0f);
  }
}

TEST_CASE("ColliderScope: the voice controls apply to whichever instrument is chosen", "[multi]") {
  cs::MultiParams p;
  p.voice.size = 0.3;
  for (int i = 0; i < cs::kInstrumentCount; ++i) {
    p.instrument = static_cast<cs::Instrument>(i);
    p.script.setText("abc");
    cs::MultiEngine voice;
    const Render r = play(voice, p, 57, 80, 0.3, 0.0);
    CAPTURE(i);
    CHECK(peakRadius(r) <= 0.3 * 1.06);
    CHECK(peakRadius(r) > 0.03);
  }
}

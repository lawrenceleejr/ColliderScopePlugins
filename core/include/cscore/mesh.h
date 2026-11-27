// The Mesh instrument: a 3D model (an STL file) drawn as its outline and
// its sharp edges, from a camera you place, turning at a rate you set.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "cscore/mono_engine.h"
#include "cscore/path.h"
#include "cscore/view3d.h"

namespace cs {

struct Mesh {
  struct Edge {
    std::uint32_t a = 0, b = 0;     // vertices
    std::int32_t left = -1, right = -1; // faces on either side; -1 = none (an open edge)
  };
  std::vector<Vec3> vertices; // centred on the origin, farthest one at distance 1
  std::vector<std::array<std::uint32_t, 3>> faces;
  std::vector<Vec3> normals;  // one per face, unit length
  std::vector<Edge> edges;
  std::string name;
};

// Welds the triangles' corners into shared vertices, finds the edges and
// what lies either side of them, and centres and scales the model.
std::shared_ptr<const Mesh> makeMesh(const std::vector<std::array<Vec3, 3>>& triangles, std::string name = {});

// Reads an STL file's bytes, binary or ASCII. Null, with a message, if it
// is not one or holds no triangles. At most `maxTriangles` are kept.
std::shared_ptr<const Mesh> parseStl(std::span<const std::uint8_t> bytes, std::string* error = nullptr,
                                     std::size_t maxTriangles = 200000);

// A torus: what the instrument draws until a file is loaded.
std::shared_ptr<const Mesh> torusMesh();

struct MeshParams {
  VoiceParams voice;
  Camera camera{30.0, 25.0, 0.0, 0.3};
  double zoom = 0.9;        // 0.2-1.5: size of the model in the picture
  double spin = 30.0;       // -180..180 degrees per second
  int spinAxis = 2;         // 0 x, 1 y, 2 z: the model's own axis it turns about
  double crease = 30.0;     // 1-90 degrees: an edge is drawn where its two faces meet at more than this
  bool cull = true;         // leave out edges on the far side
  int maxEdges = 600;       // 50-2000: the longest edges are kept
};

// The edges to draw for one view: outline (where the surface turns away),
// creases and open edges, as screen strokes, joined end to end where they meet.
std::vector<Stroke> meshStrokes(const Mesh& mesh, const MeshParams& params, double spinDegrees);

// The faces to fill for one view, with depth and shade; those facing away
// are left out when params.cull is on.
std::vector<FillPolygon> meshFaces(const Mesh& mesh, const MeshParams& params, double spinDegrees);

class MeshSource {
public:
  using Params = MeshParams;
  static double rebuildSeconds(const Params&) noexcept { return 1.0 / 40.0; }

  MeshSource();
  void prepare(double sampleRate) { sheet_.prepare(sampleRate); }
  void birth(const Params&, int, int, std::uint32_t) {}
  void build(const Params& params, int note, double age, std::span<XY> table);

  // Safe from any thread except the audio thread: swaps the model in.
  void setMesh(std::shared_ptr<const Mesh> mesh);
  std::shared_ptr<const Mesh> mesh() const;

private:
  mutable std::mutex mutex_;
  std::shared_ptr<const Mesh> mesh_;
  SheetRenderer sheet_;
  std::vector<FillPolygon> fills_;
};

using MeshEngine = MonoEngine<MeshSource>;

} // namespace cs

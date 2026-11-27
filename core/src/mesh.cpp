#include "cscore/mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <unordered_map>

namespace cs {

namespace {
constexpr double kPi = std::numbers::pi;

Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(Vec3 a) { return std::sqrt(dot(a, a)); }
} // namespace

std::shared_ptr<const Mesh> makeMesh(const std::vector<std::array<Vec3, 3>>& triangles, std::string name) {
  auto mesh = std::make_shared<Mesh>();
  mesh->name = std::move(name);
  if (triangles.empty())
    return mesh;

  // Bounds, to weld corners that agree to within a hair of the model's size.
  Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
  for (const auto& t : triangles)
    for (const Vec3& v : t) {
      lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
      hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
  const double span = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z, 1e-12});
  const double cell = span * 1e-5;
  struct Key {
    std::int64_t x, y, z;
    bool operator==(const Key& o) const { return x == o.x && y == o.y && z == o.z; }
  };
  struct Hash {
    std::size_t operator()(const Key& k) const {
      return static_cast<std::size_t>(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL);
    }
  };
  std::unordered_map<Key, std::uint32_t, Hash> index;
  auto weld = [&](Vec3 v) {
    const Key key{std::llround((v.x - lo.x) / cell), std::llround((v.y - lo.y) / cell), std::llround((v.z - lo.z) / cell)};
    const auto [it, added] = index.try_emplace(key, static_cast<std::uint32_t>(mesh->vertices.size()));
    if (added)
      mesh->vertices.push_back(v);
    return it->second;
  };
  for (const auto& t : triangles) {
    const std::array<std::uint32_t, 3> f{weld(t[0]), weld(t[1]), weld(t[2])};
    if (f[0] == f[1] || f[1] == f[2] || f[0] == f[2])
      continue; // a sliver with no area
    const Vec3 n = cross(sub(mesh->vertices[f[1]], mesh->vertices[f[0]]), sub(mesh->vertices[f[2]], mesh->vertices[f[0]]));
    const double len = length(n);
    if (len <= 0.0)
      continue;
    mesh->faces.push_back(f);
    mesh->normals.push_back({n.x / len, n.y / len, n.z / len});
  }
  // Edges, and the faces either side of each.
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> edgeIndex;
  for (std::size_t face = 0; face < mesh->faces.size(); ++face) {
    for (int k = 0; k < 3; ++k) {
      std::uint32_t a = mesh->faces[face][static_cast<std::size_t>(k)], b = mesh->faces[face][static_cast<std::size_t>((k + 1) % 3)];
      if (a > b)
        std::swap(a, b);
      const auto [it, added] = edgeIndex.try_emplace({a, b}, mesh->edges.size());
      if (added)
        mesh->edges.push_back({a, b, static_cast<std::int32_t>(face), -1});
      else if (mesh->edges[it->second].right < 0)
        mesh->edges[it->second].right = static_cast<std::int32_t>(face);
    }
  }
  // Centre on the middle of the bounds; farthest vertex at distance 1.
  const Vec3 centre{0.5 * (lo.x + hi.x), 0.5 * (lo.y + hi.y), 0.5 * (lo.z + hi.z)};
  double reach = 1e-12;
  for (Vec3& v : mesh->vertices) {
    v = sub(v, centre);
    reach = std::max(reach, length(v));
  }
  for (Vec3& v : mesh->vertices)
    v = {v.x / reach, v.y / reach, v.z / reach};
  return mesh;
}

std::shared_ptr<const Mesh> parseStl(std::span<const std::uint8_t> bytes, std::string* error, std::size_t maxTriangles) {
  auto fail = [&](const char* message) -> std::shared_ptr<const Mesh> {
    if (error)
      *error = message;
    return nullptr;
  };
  std::vector<std::array<Vec3, 3>> triangles;
  // Binary: an 80-byte header, a count, then 50 bytes per triangle.
  bool binary = false;
  if (bytes.size() >= 84) {
    std::uint32_t count = 0;
    std::memcpy(&count, bytes.data() + 80, 4);
    binary = bytes.size() == 84 + static_cast<std::size_t>(count) * 50 && count > 0;
    if (binary) {
      triangles.reserve(std::min<std::size_t>(count, maxTriangles));
      for (std::size_t i = 0; i < count && triangles.size() < maxTriangles; ++i) {
        float v[9];
        std::memcpy(v, bytes.data() + 84 + i * 50 + 12, sizeof(v)); // after the stored normal
        std::array<Vec3, 3> t;
        bool finite = true;
        for (std::size_t k = 0; k < 3; ++k) {
          t[k] = {v[3 * k], v[3 * k + 1], v[3 * k + 2]};
          finite = finite && std::isfinite(v[3 * k]) && std::isfinite(v[3 * k + 1]) && std::isfinite(v[3 * k + 2]);
        }
        if (finite)
          triangles.push_back(t);
      }
    }
  }
  if (!binary) {
    // ASCII: every "vertex x y z"; three make a triangle.
    const std::string text(bytes.begin(), bytes.end());
    if (text.find("vertex") == std::string::npos)
      return fail("not an STL file");
    std::array<Vec3, 3> t;
    std::size_t corner = 0;
    for (std::size_t pos = text.find("vertex"); pos != std::string::npos && triangles.size() < maxTriangles;
         pos = text.find("vertex", pos + 6)) {
      const char* p = text.c_str() + pos + 6;
      char* end = nullptr;
      double v[3];
      bool good = true;
      for (double& value : v) {
        value = std::strtod(p, &end);
        good = good && end != p && std::isfinite(value);
        p = end;
      }
      if (!good)
        continue;
      t[corner++] = {v[0], v[1], v[2]};
      if (corner == 3) {
        triangles.push_back(t);
        corner = 0;
      }
    }
  }
  if (triangles.empty())
    return fail("the STL file has no triangles");
  auto mesh = makeMesh(triangles);
  if (mesh->faces.empty())
    return fail("the STL file has no triangles with any area");
  return mesh;
}

std::shared_ptr<const Mesh> torusMesh() {
  std::vector<std::array<Vec3, 3>> triangles;
  constexpr int around = 28, across = 12;
  constexpr double big = 1.0, small = 0.42;
  auto point = [&](int i, int j) {
    const double u = 2.0 * kPi * i / around, v = 2.0 * kPi * j / across;
    const double r = big + small * std::cos(v);
    return Vec3{r * std::cos(u), r * std::sin(u), small * std::sin(v)};
  };
  for (int i = 0; i < around; ++i)
    for (int j = 0; j < across; ++j) {
      const Vec3 a = point(i, j), b = point(i + 1, j), c = point(i + 1, j + 1), d = point(i, j + 1);
      triangles.push_back({a, b, c});
      triangles.push_back({a, c, d});
    }
  return makeMesh(triangles, "torus");
}

std::vector<Stroke> meshStrokes(const Mesh& mesh, const MeshParams& params, double spinDegrees) {
  std::vector<Stroke> out;
  if (mesh.edges.empty())
    return out;
  // Turn the model about its own axis, then look at it.
  const double a = spinDegrees * kPi / 180.0, c = std::cos(a), s = std::sin(a);
  auto turn = [&](Vec3 p) -> Vec3 {
    switch (params.spinAxis) {
    case 0:
      return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
    case 1:
      return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
    default:
      return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
    }
  };
  const View view(params.camera, 1.0);
  const double zoom = std::clamp(params.zoom, 0.05, 1.5);

  std::vector<Vec3> screen(mesh.vertices.size());
  for (std::size_t i = 0; i < screen.size(); ++i) {
    screen[i] = view.project(turn(mesh.vertices[i]));
    screen[i].x *= zoom;
    screen[i].y *= zoom;
  }
  std::vector<std::uint8_t> facing(mesh.faces.size());
  for (std::size_t f = 0; f < facing.size(); ++f)
    facing[f] = view.depthOf(turn(mesh.normals[f])) > 0.0;

  const double creaseCos = std::cos(std::clamp(params.crease, 0.5, 180.0) * kPi / 180.0);
  struct Chosen {
    std::uint32_t a, b;
    double length;
  };
  std::vector<Chosen> chosen;
  for (const Mesh::Edge& e : mesh.edges) {
    const bool open = e.right < 0;
    const bool leftFront = facing[static_cast<std::size_t>(e.left)];
    const bool rightFront = open ? leftFront : facing[static_cast<std::size_t>(e.right)] != 0;
    const bool outline = !open && leftFront != rightFront;
    const bool crease =
        !open && dot(mesh.normals[static_cast<std::size_t>(e.left)], mesh.normals[static_cast<std::size_t>(e.right)]) < creaseCos;
    if (!(open || outline || crease))
      continue;
    if (params.cull && !leftFront && !rightFront)
      continue; // wholly on the far side
    chosen.push_back({e.a, e.b, std::hypot(screen[e.a].x - screen[e.b].x, screen[e.a].y - screen[e.b].y)});
  }
  // Too many to draw well: keep the longest.
  const auto limit = static_cast<std::size_t>(std::clamp(params.maxEdges, 10, 4000));
  if (chosen.size() > limit) {
    std::nth_element(chosen.begin(), chosen.begin() + static_cast<std::ptrdiff_t>(limit), chosen.end(),
                     [](const Chosen& l, const Chosen& r) { return l.length > r.length; });
    chosen.resize(limit);
  }

  // Join edges end to end into long strokes: follow shared vertices.
  std::unordered_multimap<std::uint32_t, std::size_t> at;
  for (std::size_t i = 0; i < chosen.size(); ++i) {
    at.emplace(chosen[i].a, i);
    at.emplace(chosen[i].b, i);
  }
  std::vector<std::uint8_t> used(chosen.size(), 0);
  auto point = [&](std::uint32_t v) { return Vec2{screen[v].x, screen[v].y}; };
  for (std::size_t i = 0; i < chosen.size(); ++i) {
    if (used[i])
      continue;
    used[i] = 1;
    Stroke stroke{point(chosen[i].a), point(chosen[i].b)};
    for (int direction = 0; direction < 2; ++direction) { // grow from the end, then from the start
      std::uint32_t tip = direction == 0 ? chosen[i].b : chosen[i].a;
      for (;;) {
        std::size_t next = chosen.size();
        const auto range = at.equal_range(tip);
        for (auto it = range.first; it != range.second; ++it)
          if (!used[it->second]) {
            next = it->second;
            break;
          }
        if (next == chosen.size())
          break;
        used[next] = 1;
        tip = chosen[next].a == tip ? chosen[next].b : chosen[next].a;
        if (direction == 0)
          stroke.push_back(point(tip));
        else
          stroke.insert(stroke.begin(), point(tip));
      }
    }
    out.push_back(std::move(stroke));
  }
  return out;
}

std::vector<FillPolygon> meshFaces(const Mesh& mesh, const MeshParams& params, double spinDegrees) {
  std::vector<FillPolygon> out;
  const double a = spinDegrees * kPi / 180.0, c = std::cos(a), s = std::sin(a);
  auto turn = [&](Vec3 p) -> Vec3 {
    switch (params.spinAxis) {
    case 0:
      return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
    case 1:
      return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
    default:
      return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
    }
  };
  const View view(params.camera, 1.0);
  const double zoom = std::clamp(params.zoom, 0.05, 1.5);
  std::vector<Vec3> screen(mesh.vertices.size());
  for (std::size_t i = 0; i < screen.size(); ++i) {
    screen[i] = view.project(turn(mesh.vertices[i]));
    screen[i].x *= zoom;
    screen[i].y *= zoom;
  }
  out.reserve(mesh.faces.size());
  for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
    if (params.cull && view.depthOf(turn(mesh.normals[f])) <= 0.0)
      continue; // facing away: hidden behind the faces in front of it
    const Vec3 p = screen[mesh.faces[f][0]], q = screen[mesh.faces[f][1]], r = screen[mesh.faces[f][2]];
    const Vec3 u{q.x - p.x, q.y - p.y, q.z - p.z}, w{r.x - p.x, r.y - p.y, r.z - p.z};
    FillPolygon tri;
    tri.contours.push_back({{p.x, p.y}, {q.x, q.y}, {r.x, r.y}});
    tri.depth = (p.z + q.z + r.z) / 3.0;
    tri.shade = lampShade({u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x});
    out.push_back(std::move(tri));
  }
  return out;
}

MeshSource::MeshSource() : mesh_(torusMesh()) {}

void MeshSource::setMesh(std::shared_ptr<const Mesh> mesh) {
  if (!mesh)
    mesh = torusMesh();
  const std::lock_guard<std::mutex> lock(mutex_);
  mesh_ = std::move(mesh);
}

std::shared_ptr<const Mesh> MeshSource::mesh() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return mesh_;
}

void MeshSource::build(const Params& params, int note, double age, std::span<XY> table) {
  const std::shared_ptr<const Mesh> current = mesh(); // builder thread: a brief lock, never the audio thread
  std::vector<Stroke> strokes = meshStrokes(*current, params, params.spin * age);
  if (params.voice.sheet.style == SheetParams::Style::Sheet) {
    fills_ = meshFaces(*current, params, params.spin * age); // shaded faces, with the outline and creases over them
    sheet_.build(params.voice.sheet, note, fills_, strokes, table);
    return;
  }
  if (strokes.empty()) {
    std::fill(table.begin(), table.end(), XY{});
    return;
  }
  // The outline changes as the model turns, so time is spread evenly along
  // the picture rather than solved for harmony.
  const Path path = strokesToPath(orderStrokes(std::move(strokes), {}));
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs

// Path engine: strokes -> one beam path -> a lookup table evenly spaced by
// weighted arc length. Brightness is dwell time per unit length, so even
// spacing draws even lines; pen-up jumps get little time and stay faint.
#pragma once

#include <span>
#include <vector>

namespace cs {

struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

// One audio sample of picture: left = X, right = Y.
struct XY {
  float x = 0.0f;
  float y = 0.0f;
};

using Stroke = std::vector<Vec2>;

// pen == false: the segment arriving at this point is a jump.
struct PathPoint {
  double x = 0.0;
  double y = 0.0;
  bool pen = true;
};
using Path = std::vector<PathPoint>;

// A pen-up jump takes this fraction of the time its length would take pen-down.
inline constexpr double kPenUpWeight = 0.04;

// Greedy nearest-neighbour stroke order from `start`; a stroke is reversed
// when its far end is the nearer one. Keeps jumps short and faint.
std::vector<Stroke> orderStrokes(std::vector<Stroke> strokes, Vec2 start = {});

// Concatenate strokes into one path; the segment into each stroke is a jump.
Path strokesToPath(const std::vector<Stroke>& strokes);
void appendStroke(Path& path, const Stroke& stroke);

// Fill `out` with points evenly spaced by weighted arc length along `path`,
// then along a pen-up jump from its last point to `closeTo` (the start of
// whatever the beam draws next). out[0] is path[0]; the end is not repeated.
void resamplePath(const Path& path, std::span<XY> out, Vec2 closeTo, double penUpWeight = kPenUpWeight);

// A picture is drawn once per period: each segment gets a fraction of the
// period. One segment is a plain picture; several multiplex the beam.
struct Segment {
  Path path;
  double fraction = 1.0;
};

// Resample every segment into its share of `table`. Empty segments are
// skipped; with nothing to draw the table is all zeros (beam parked).
void buildTable(std::span<const Segment> segments, std::span<XY> table, double penUpWeight = kPenUpWeight);

} // namespace cs

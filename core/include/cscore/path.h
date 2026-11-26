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

// ---- Harmonic timing ----
//
// Arc-length timing gives even brightness but takes no account of the
// sound. Harmonic timing puts the strokes on a grid instead: the period is
// cut into N equal slots, each stroke (with its jump to the next) gets a
// whole number of them, and N is a product of 2s and at most one 3, so the
// rhythm of the strokes falls on octaves and fifths of the note. Among the
// grids that fit, the one whose table has the most consonant spectrum wins.
// The price is brightness: a stroke rounded up to a slot is drawn slower,
// and so brighter, than one rounded down.

// How consonant harmonic k is with the fundamental, 0..1: powers of 2 are
// octaves (1.0), a factor of 3 adds a fifth (0.9), 5 a third (0.7), 7 and
// larger primes are progressively out of tune with the scale (0.35, 0.15).
double harmonicWeight(int k) noexcept;

// The share of a table's energy, over harmonics 1..maxHarmonic of both
// channels, that sits in consonant harmonics (each weighted as above).
double consonance(std::span<const XY> table, int maxHarmonic = 32) noexcept;

// The smallest grid size 2^a or 3 * 2^a that is at least `atLeast`.
int nextHarmonicGrid(int atLeast) noexcept;

// Strokes, in drawing order, onto a grid of `slots` equal slots. Each
// stroke gets slots in proportion to its length, at least one.
void buildTableOnGrid(const std::vector<Stroke>& ordered, std::span<XY> table, int slots,
                      double penUpWeight = kPenUpWeight);

// Chooses the grid for a picture and remembers it while the number of
// strokes stays the same, so the timbre does not flicker between rebuilds.
class HarmonicGrid {
public:
  // Builds `table` from `ordered` on the best grid.
  void build(const std::vector<Stroke>& ordered, std::span<XY> table, double penUpWeight = kPenUpWeight);
  int slots() const noexcept { return slots_; }
  void reset() noexcept { count_ = 0; }

private:
  std::size_t count_ = 0;
  int slots_ = 0;
};

} // namespace cs

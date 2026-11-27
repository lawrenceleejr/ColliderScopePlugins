// Sheets: filled areas, drawn as a raster. A scope can only draw lines, so
// an area is filled by sweeping the beam to and fro across it in close
// scanlines, and shaded by how long the beam dwells: brightness on a scope
// is time spent per unit length.
//
// Any instrument can hand over filled polygons (with a depth, for which is
// in front, and a shade from 0 to 1) and lines to draw over them. The
// polygons are rasterised through a coarse depth buffer, the scanlines are
// turned into one path, and the period is shared between the fill and the
// lines.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "cscore/path.h"
#include "cscore/view3d.h"

namespace cs {

struct SheetParams {
  enum class Style { Lines, Sheet } style = Style::Lines;
  int scanlines = 64;      // 8-160: the most scanlines; a short period has room for fewer
  double shading = 0.7;    // 0-1: how strongly the light shades the fill; 0 is flat
  int interlace = 1;       // 1, 2 or 4: draw every n-th scanline each frame, moving on a line each frame
                           // (3 is taken as 2: the picture would repeat every third period, a twelfth below the note)
  double fillShare = 0.6;  // 0.2-0.95: the share of each period spent on the fill, the rest on the lines
  double width = 0.025;    // 0.005-0.08: how wide lines are when filled in as ribbons
};

// A filled area on the screen: one or more closed contours (even-odd inside
// one polygon, so a ring with a hole is two contours), all at one depth.
struct FillPolygon {
  std::vector<std::vector<Vec2>> contours;
  double depth = 0.0; // larger is nearer: a nearer polygon covers a farther one
  float shade = 0.6f; // 0 dark .. 1 fully lit
};

// The fill as scanlines `offset`, `offset + stride`, ... of `rows`, spread
// over the polygons' height, to and fro, with dwell weights from the shades.
Path rasterSheet(std::span<const FillPolygon> fills, int rows, int offset = 0, int stride = 1, double shading = 0.7);

// The same `rows` evenly spread scanlines, top to bottom, each its own path,
// all drawn left to right; a run starts with a jump (pen = false). A line
// that crosses nothing is an empty path, so the spacing is kept.
std::vector<Path> rasterRows(std::span<const FillPolygon> fills, int rows, double shading = 0.7);

// The interlace as a stride: 1, 2 or 4.
int interlaceStride(int interlace) noexcept;

// A row's drawn length, each piece times its dwell weight.
double rowLength(const Path& row) noexcept;
// A row drawn the other way: its runs in reverse order, each reversed.
void reverseRow(Path& row);

// Harmonic sheets. A scanline sweeps the beam across and the next sweeps it
// back, so the fill puts a buzz into the sound at half the rate it draws
// lines. Left to itself that rate wanders, since long lines take longer
// than short ones, and the buzz smears over a cluster of neighbouring
// harmonics. A harmonic sheet gives every scanline the same time, so the
// sweep is steady, and chooses the number of lines so it lands on a
// consonant harmonic of the note: 2^n or 3 * 2^n times it (octaves, and
// fifths above octaves), as the Harmonic timing grid does.
//
// Equal time for a short line would make it brighter than a long one. To
// keep the shading true, the lines are spaced to suit: each covers an equal
// share of the picture's weighted width, so they crowd where it is wide and
// spread where it is narrow (but no more than about three times the mean
// spacing, so thin strokes do not fall between them).
struct SheetPlan {
  int rows = 0;      // scanlines a frame draws (even, so a period ends where it began)
  int harmonic = 0;  // the sweep's harmonic of the note: rows / (2 * share)
  double share = 1;  // the fill's exact share of the period
};
// Plans a period: the most rows that fit the ceiling (Scanlines, and about
// 14 samples a line) with a consonant sweep, and the fill share, moved
// slightly from the one asked for so the harmonic is exact.
SheetPlan planSheet(const SheetParams& params, double samplesPerPeriod, bool withLines) noexcept;
// Picks `count * stride` rows from `fine` so each covers an equal share of
// the weighted width, keeps every stride-th from `offset`, and turns every
// other one round so the beam goes to and fro. Only rows with something on
// them are picked.
std::vector<Path> pickRows(const std::vector<Path>& fine, int count, int offset = 0, int stride = 1);
// The rows, each with an equal share of `share`, followed by `over` with the rest.
std::vector<Segment> harmonicSegments(std::vector<Path>&& rows, double share, Path&& over);

bool isClosed(const Stroke& stroke) noexcept;
// Closed strokes become filled polygons.
void fillClosed(const std::vector<Stroke>& strokes, float shade, std::vector<FillPolygon>& out, double depth = 0.0);
// Open strokes of more than one point become ribbons `width` wide; strokes
// that are a single point (a dot) become discs of that diameter.
void ribbons(const std::vector<Stroke>& strokes, double width, float shade, std::vector<FillPolygon>& out,
             bool includeClosed = false, std::size_t maxPoints = 0);

// How lit a surface is, 0..1, from its normal as seen on the screen (x right,
// y up, z toward the viewer): a lamp above, to the left and in front.
float lampShade(Vec3 normalOnScreen) noexcept;

class SheetRenderer {
public:
  void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
  // Scanlines a frame can use for a note: as many as asked for, but no more
  // than the fill's share of a period has room for, about 14 samples a line.
  int lines(const SheetParams& params, int note, bool withLines) const noexcept;
  // The table: the fill, then the lines over it. Harmonic (Timing:
  // Harmonic) gives every scanline equal time on a consonant sweep; Even
  // spreads time along the scanlines by length.
  void build(const SheetParams& params, int note, std::span<const FillPolygon> fills, const std::vector<Stroke>& lines,
             std::span<XY> table, bool harmonic = true);

private:
  double sampleRate_ = 48000.0;
  std::uint32_t frame_ = 0;
};

} // namespace cs

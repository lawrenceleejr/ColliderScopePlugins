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
  int interlace = 1;       // 1-4: draw every n-th scanline each frame, moving on a line each frame
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
  // The table: the fill, then the lines over it.
  void build(const SheetParams& params, int note, std::span<const FillPolygon> fills, const std::vector<Stroke>& lines,
             std::span<XY> table);

private:
  double sampleRate_ = 48000.0;
  std::uint32_t frame_ = 0;
};

} // namespace cs

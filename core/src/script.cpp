#include "cscore/script.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace cs {

namespace {

#include "hershey_data.inc"

constexpr double kUnit = 1.0 / 21.0; // Hershey capitals are 21 units tall
constexpr double kBaseline = 9.0;    // and sit on y = +9

// A laid-out piece: strokes with x from 0 to `width`, baseline at y = 0.
struct Box {
  std::vector<Stroke> strokes;
  double width = 0.0, ascent = 0.0, descent = 0.0;

  void shift(double dx, double dy) {
    for (Stroke& s : strokes)
      for (Vec2& p : s) {
        p.x += dx;
        p.y += dy;
      }
  }
  void scale(double k) {
    for (Stroke& s : strokes)
      for (Vec2& p : s) {
        p.x *= k;
        p.y *= k;
      }
    width *= k;
    ascent *= k;
    descent *= k;
  }
  // Put `other` after this one, raised by dy.
  void append(Box other, double dy = 0.0) {
    other.shift(width, dy);
    for (Stroke& s : other.strokes)
      strokes.push_back(std::move(s));
    width += other.width;
    ascent = std::max(ascent, other.ascent + dy);
    descent = std::max(descent, other.descent - dy);
  }
  void line(Vec2 a, Vec2 b) { strokes.push_back({a, b}); }
};

Box space(double width) {
  Box b;
  b.width = width;
  return b;
}

Box glyph(const char* const* font, int ch) {
  Box b;
  if (ch < 32 || ch > 127)
    return space(0.4);
  const char* data = font[ch - 32];
  const int left = data[0] - 'R', right = data[1] - 'R';
  b.width = (right - left) * kUnit;
  Stroke current;
  auto flush = [&] {
    if (current.size() == 1)
      current.push_back(current[0]); // a dot: the beam dwells
    if (!current.empty())
      b.strokes.push_back(std::move(current));
    current.clear();
  };
  for (const char* p = data + 2; p[0] && p[1]; p += 2) {
    if (p[0] == ' ' && p[1] == 'R') {
      flush();
      continue;
    }
    const Vec2 v{(p[0] - 'R' - left) * kUnit, (kBaseline - (p[1] - 'R')) * kUnit};
    b.ascent = std::max(b.ascent, v.y);
    b.descent = std::max(b.descent, -v.y);
    current.push_back(v);
  }
  flush();
  return b;
}

struct Symbol {
  const char* const* font;
  char ch;
};

const std::map<std::string_view, Symbol>& symbols() {
  static const std::map<std::string_view, Symbol> table = {
      {"alpha", {kGreek, 'a'}},      {"beta", {kGreek, 'b'}},     {"gamma", {kGreek, 'g'}},   {"delta", {kGreek, 'd'}},
      {"epsilon", {kGreek, 'e'}},    {"zeta", {kGreek, 'z'}},     {"eta", {kGreek, 'h'}},     {"theta", {kGreek, 'q'}},
      {"iota", {kGreek, 'i'}},       {"kappa", {kGreek, 'k'}},    {"lambda", {kGreek, 'l'}},  {"mu", {kGreek, 'm'}},
      {"nu", {kGreek, 'n'}},         {"xi", {kGreek, 'x'}},       {"pi", {kGreek, 'p'}},      {"rho", {kGreek, 'r'}},
      {"sigma", {kGreek, 's'}},      {"tau", {kGreek, 't'}},      {"upsilon", {kGreek, 'u'}}, {"phi", {kGreek, 'f'}},
      {"chi", {kGreek, 'c'}},        {"psi", {kGreek, 'y'}},      {"omega", {kGreek, 'w'}},   {"Gamma", {kGreek, 'G'}},
      {"Delta", {kGreek, 'D'}},      {"Theta", {kGreek, 'Q'}},    {"Lambda", {kGreek, 'L'}},  {"Xi", {kGreek, 'X'}},
      {"Pi", {kGreek, 'P'}},         {"Sigma", {kGreek, 'S'}},    {"Upsilon", {kGreek, 'U'}}, {"Phi", {kGreek, 'F'}},
      {"Psi", {kGreek, 'Y'}},        {"Omega", {kGreek, 'W'}},    {"pm", {kMath, '!'}},       {"mp", {kMath, '"'}},
      {"times", {kMath, '#'}},       {"cdot", {kMath, '$'}},      {"leq", {kMath, '&'}},      {"le", {kMath, '&'}},
      {"geq", {kMath, '\''}},        {"ge", {kMath, '\''}},       {"prod", {kMath, ':'}},     {"sum", {kMath, ';'}},
      {"neq", {kMath, '?'}},         {"ne", {kMath, '?'}},        {"equiv", {kMath, '@'}},    {"propto", {kMath, '^'}},
      {"infty", {kMath, '_'}},       {"circ", {kMath, '`'}},      {"degree", {kMath, '`'}},   {"subset", {kMath, 'd'}},
      {"cup", {kMath, 'e'}},         {"supset", {kMath, 'f'}},    {"cap", {kMath, 'g'}},      {"in", {kMath, 'h'}},
      {"to", {kMath, 'i'}},          {"rightarrow", {kMath, 'i'}}, {"uparrow", {kMath, 'j'}}, {"leftarrow", {kMath, 'k'}},
      {"downarrow", {kMath, 'l'}},   {"partial", {kMath, 'm'}},   {"nabla", {kMath, 'n'}},    {"int", {kMath, 'p'}},
      {"exists", {kMath, 'v'}},      {"aleph", {kMath, 'w'}},     {"div", {kMath, 'x'}},      {"parallel", {kMath, 'y'}},
      {"perp", {kMath, 'z'}},        {"angle", {kMath, '|'}},     {"therefore", {kMath, '~'}}, {"sim", {kMath, 127}},
      {"approx", {kMath, 127}},
  };
  return table;
}

class Parser {
public:
  explicit Parser(std::string_view source) : s_(source) {}

  // Lines, split at \\ and at newlines.
  std::vector<Box> lines() {
    std::vector<Box> out;
    while (true) {
      out.push_back(row());
      if (i_ >= s_.size())
        break;
      if (s_[i_] == '}') // a stray brace: skip it
        ++i_;
    }
    return out;
  }

private:
  bool atLineBreak() const {
    return i_ < s_.size() && (s_[i_] == '\n' || (s_[i_] == '\\' && i_ + 1 < s_.size() && s_[i_ + 1] == '\\'));
  }

  // Atoms up to a closing brace, a line break or the end.
  Box row() {
    Box out;
    while (i_ < s_.size() && s_[i_] != '}') {
      if (atLineBreak()) {
        if (depth_ > 0) { // a line break inside braces is only a space
          i_ += s_[i_] == '\n' ? 1 : 2;
          out.append(space(0.35));
          continue;
        }
        i_ += s_[i_] == '\n' ? 1 : 2;
        return out;
      }
      Box piece = atom();
      // Scripts hang off the atom before them.
      Box sup, sub;
      bool hasSup = false, hasSub = false;
      while (i_ < s_.size() && (s_[i_] == '^' || s_[i_] == '_')) {
        const bool up = s_[i_++] == '^';
        Box script = argument();
        script.scale(0.62);
        (up ? sup : sub) = std::move(script);
        (up ? hasSup : hasSub) = true;
      }
      if (hasSup || hasSub) {
        const double base = piece.width + 0.04;
        const double raise = std::max(0.48, piece.ascent - 0.3), lower = std::max(0.2, piece.descent + 0.05);
        Box both = std::move(piece);
        if (hasSup) {
          sup.shift(base, raise);
          for (Stroke& s : sup.strokes)
            both.strokes.push_back(std::move(s));
          both.ascent = std::max(both.ascent, raise + sup.ascent);
        }
        if (hasSub) {
          sub.shift(base, -lower);
          for (Stroke& s : sub.strokes)
            both.strokes.push_back(std::move(s));
          both.descent = std::max(both.descent, lower + sub.descent);
        }
        both.width = base + std::max(sup.width, sub.width) + 0.04;
        piece = std::move(both);
      }
      out.append(std::move(piece));
    }
    return out;
  }

  // One argument: a braced group, or a single atom.
  Box argument() {
    while (i_ < s_.size() && s_[i_] == ' ')
      ++i_;
    if (i_ < s_.size() && s_[i_] == '{') {
      ++i_;
      ++depth_;
      Box inner = row();
      --depth_;
      if (i_ < s_.size() && s_[i_] == '}')
        ++i_;
      return inner;
    }
    return i_ < s_.size() ? atom() : Box{};
  }

  Box atom() {
    const char c = s_[i_];
    if (c == '{')
      return argument();
    if (c == '$') {
      ++i_;
      return {};
    }
    if (c == ' ') {
      ++i_;
      return space(0.35);
    }
    if (c == '^' || c == '_') // a script with nothing to hang on: handled by row() after an empty atom
      return {};
    if (c != '\\') {
      ++i_;
      return glyph(kRoman, static_cast<unsigned char>(c));
    }
    // A command.
    ++i_;
    if (i_ >= s_.size())
      return {};
    if (!std::isalpha(static_cast<unsigned char>(s_[i_]))) {
      const char symbol = s_[i_++];
      switch (symbol) {
      case ',':
        return space(0.17);
      case ';':
      case ':':
        return space(0.28);
      case ' ':
        return space(0.35);
      case '!':
        return {};
      default:
        return glyph(kRoman, static_cast<unsigned char>(symbol)); // \{ \} \% \$ ...
      }
    }
    const std::size_t begin = i_;
    while (i_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[i_])))
      ++i_;
    const std::string_view name = s_.substr(begin, i_ - begin);
    if (i_ < s_.size() && s_[i_] == ' ') // the space that ends a command name is not drawn
      ++i_;

    if (name == "frac")
      return fraction();
    if (name == "sqrt")
      return root();
    if (name == "quad")
      return space(1.0);
    if (name == "qquad")
      return space(2.0);
    if (name == "left" || name == "right" || name == "displaystyle" || name == "limits")
      return {};
    if (name == "text" || name == "mathrm" || name == "mathbf" || name == "mathit")
      return argument();
    if (name == "vec" || name == "hat" || name == "bar" || name == "dot" || name == "tilde")
      return accent(name);
    if (name == "hbar") {
      Box h = glyph(kRoman, 'h');
      h.line({-0.02, 0.78}, {0.36, 0.9});
      return h;
    }
    if (const auto it = symbols().find(name); it != symbols().end())
      return glyph(it->second.font, static_cast<unsigned char>(it->second.ch));
    // Unknown: draw its name, so a typo is visible rather than silent.
    Box out;
    for (const char ch : name)
      out.append(glyph(kRoman, static_cast<unsigned char>(ch)));
    return out;
  }

  Box fraction() {
    Box top = argument(), bottom = argument();
    top.scale(0.8);
    bottom.scale(0.8);
    constexpr double axis = 0.38, gap = 0.13, pad = 0.1;
    const double width = std::max(top.width, bottom.width) + 2.0 * pad;
    Box out;
    top.shift(0.5 * (width - top.width), axis + gap + top.descent);
    bottom.shift(0.5 * (width - bottom.width), axis - gap - bottom.ascent);
    out.ascent = axis + gap + top.descent + top.ascent;
    out.descent = -(axis - gap - bottom.ascent - bottom.descent);
    for (Stroke& s : top.strokes)
      out.strokes.push_back(std::move(s));
    out.line({0.03, axis}, {width - 0.03, axis});
    for (Stroke& s : bottom.strokes)
      out.strokes.push_back(std::move(s));
    out.width = width;
    return out;
  }

  Box root() {
    Box inside = argument();
    const double top = std::max(inside.ascent, 0.6) + 0.14, foot = inside.descent;
    constexpr double sign = 0.5;
    Box out;
    out.strokes.push_back({{0.0, 0.3}, {0.1, 0.38}, {0.27, -foot}, {0.45, top}, {sign + inside.width + 0.08, top}});
    inside.shift(sign, 0.0);
    for (Stroke& s : inside.strokes)
      out.strokes.push_back(std::move(s));
    out.width = sign + inside.width + 0.14;
    out.ascent = top;
    out.descent = foot;
    return out;
  }

  Box accent(std::string_view name) {
    Box base = argument();
    const double y = base.ascent + 0.12, mid = 0.5 * base.width, half = std::max(0.16, 0.4 * base.width);
    if (name == "bar") {
      base.line({mid - half, y}, {mid + half, y});
    } else if (name == "vec") {
      base.strokes.push_back({{mid - half, y + 0.05}, {mid + half, y + 0.05}});
      base.strokes.push_back({{mid + half - 0.1, y + 0.12}, {mid + half, y + 0.05}, {mid + half - 0.1, y - 0.02}});
    } else if (name == "hat") {
      base.strokes.push_back({{mid - 0.14, y}, {mid, y + 0.14}, {mid + 0.14, y}});
    } else if (name == "tilde") {
      base.strokes.push_back({{mid - 0.16, y + 0.03}, {mid - 0.08, y + 0.1}, {mid + 0.08, y + 0.02}, {mid + 0.16, y + 0.09}});
    } else { // dot
      base.strokes.push_back({{mid, y + 0.04}, {mid, y + 0.04}});
    }
    base.ascent = y + 0.16;
    return base;
  }

  std::string_view s_;
  std::size_t i_ = 0;
  int depth_ = 0;
};

} // namespace

ScriptLayout layoutScript(std::string_view source) {
  ScriptLayout out;
  std::vector<Box> lines = Parser(source).lines();
  // Drop empty lines at the end; keep empty ones between as spacing.
  while (!lines.empty() && lines.back().strokes.empty())
    lines.pop_back();
  out.lines = static_cast<int>(lines.size());
  double y = 0.0;
  bool first = true;
  out.left = out.bottom = 1e9;
  out.right = out.top = -1e9;
  for (std::size_t n = 0; n < lines.size(); ++n) {
    Box& line = lines[n];
    if (!first) // far enough below the line before that nothing touches
      y -= std::max(1.5, lines[n - 1].descent + line.ascent + 0.35);
    first = false;
    line.shift(-0.5 * line.width, y);
    for (Stroke& s : line.strokes) {
      for (const Vec2& p : s) {
        out.left = std::min(out.left, p.x);
        out.right = std::max(out.right, p.x);
        out.bottom = std::min(out.bottom, p.y);
        out.top = std::max(out.top, p.y);
      }
      out.strokes.push_back(std::move(s));
    }
  }
  if (out.strokes.empty())
    out.left = out.right = out.bottom = out.top = 0.0;
  return out;
}

std::vector<Stroke> fitScript(std::string_view source, double fit) {
  ScriptLayout layout = layoutScript(source);
  if (layout.strokes.empty())
    return {};
  const double cx = 0.5 * (layout.left + layout.right), cy = 0.5 * (layout.bottom + layout.top);
  double reach = 1e-9;
  for (const Stroke& s : layout.strokes)
    for (const Vec2& p : s)
      reach = std::max(reach, std::hypot(p.x - cx, p.y - cy));
  // Fill the screen, but a single letter need not be a metre tall.
  const double scale = std::min(fit / reach, 0.6);
  for (Stroke& s : layout.strokes)
    for (Vec2& p : s)
      p = {(p.x - cx) * scale, (p.y - cy) * scale};
  return std::move(layout.strokes);
}

// -------------------------------------------------------------- ScriptSource

void ScriptSource::layout(const Params& params) {
  const std::string source(params.text.data());
  if (source == laidOut_ && params.slant == slant_)
    return;
  laidOut_ = source;
  slant_ = params.slant;
  // Lean the letters first, then fit, so slanted text still sits in the screen.
  ScriptLayout raw = layoutScript(source);
  double cx = 0.5 * (raw.left + raw.right), cy = 0.5 * (raw.bottom + raw.top), reach = 1e-9;
  for (Stroke& s : raw.strokes)
    for (Vec2& p : s) {
      p = {p.x - cx + params.slant * (p.y - cy), p.y - cy};
      reach = std::max(reach, std::hypot(p.x, p.y));
    }
  const double scale = std::min(0.85 / reach, 0.6);
  strokes_ = std::move(raw.strokes);
  lengths_.clear();
  double total = 0.0;
  for (Stroke& s : strokes_) {
    for (Vec2& p : s)
      p = {p.x * scale, p.y * scale};
    for (std::size_t i = 1; i < s.size(); ++i)
      total += std::hypot(s[i].x - s[i - 1].x, s[i].y - s[i - 1].y);
    total += 0.01; // a dot has no length but still takes its turn
    lengths_.push_back(total);
  }
  grid_.reset();
}

void ScriptSource::birth(const Params& params, int, int, std::uint32_t) { layout(params); }

void ScriptSource::build(const Params& params, int, double age, std::span<XY> table) {
  layout(params); // the text can be edited while a note sounds
  if (strokes_.empty()) {
    std::fill(table.begin(), table.end(), XY{});
    return;
  }
  // Written out along its length, in reading order.
  const std::vector<Stroke>* drawn = &strokes_;
  if (params.writeMs > 0.0 && age < params.writeMs * 1e-3) {
    const double upTo = lengths_.back() * age / (params.writeMs * 1e-3);
    shown_.clear();
    for (std::size_t i = 0; i < strokes_.size(); ++i) {
      const double before = i ? lengths_[i - 1] : 0.0;
      if (before >= upTo)
        break;
      shown_.push_back(strokes_[i]);
      if (lengths_[i] > upTo) { // the stroke being written: as far as the pen has got
        Stroke& s = shown_.back();
        double along = before;
        for (std::size_t j = 1; j < s.size(); ++j) {
          const double step = std::hypot(s[j].x - s[j - 1].x, s[j].y - s[j - 1].y);
          if (along + step > upTo) {
            const double u = step > 0.0 ? (upTo - along) / step : 0.0;
            s[j] = {s[j - 1].x + (s[j].x - s[j - 1].x) * u, s[j - 1].y + (s[j].y - s[j - 1].y) * u};
            s.resize(j + 1);
            break;
          }
          along += step;
        }
      }
    }
    if (shown_.empty())
      shown_.push_back({strokes_[0][0], strokes_[0][0]});
    drawn = &shown_;
  }
  if (params.voice.timing == Timing::Harmonic) {
    grid_.setSolveOrder(false); // text is drawn in reading order
    grid_.build(*drawn, table);
    return;
  }
  const Path path = strokesToPath(*drawn);
  const Segment segment{path, 1.0};
  buildTable(std::span<const Segment>(&segment, 1), table);
}

} // namespace cs

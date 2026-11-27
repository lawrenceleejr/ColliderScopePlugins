// CS Script: text and equations, drawn with single-stroke (Hershey) letters.
//
// The source uses a small part of LaTeX's syntax:
//
//   plain text            E = mc^2
//   scripts               x^2  a_i  e^{i\pi}  \int_0^\infty
//   fractions, roots      \frac{a}{b}   \sqrt{x^2 + y^2}
//   Greek                 \alpha ... \omega, \Gamma \Delta \Theta \Lambda \Xi \Pi \Sigma \Phi \Psi \Omega
//   symbols               \pm \mp \times \cdot \div \leq \geq \neq \equiv \approx \sim \propto \infty
//                         \partial \nabla \int \sum \prod \to \leftarrow \uparrow \downarrow
//                         \in \subset \supset \cup \cap \exists \perp \parallel \angle \therefore \hbar
//   accents               \vec{a}  \hat{H}  \bar{x}  \dot{q}
//   spacing, lines        \,  \;  \quad   and  \\  for a new line
//
// Braces group. $ signs are ignored. An unknown command is drawn as its name.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cscore/mono_engine.h"
#include "cscore/path.h"

namespace cs {

struct ScriptLayout {
  std::vector<Stroke> strokes; // in reading order; units of capital-letter height, y up
  double left = 0.0, right = 0.0, bottom = 0.0, top = 0.0; // bounding box
  int lines = 0;
};

// Lays the source out. Lines are centred on x = 0; the first line's baseline is y = 0.
ScriptLayout layoutScript(std::string_view source);

// The same, scaled and centred to sit within `fit` of the origin.
std::vector<Stroke> fitScript(std::string_view source, double fit = 0.85);

struct ScriptParams {
  VoiceParams voice;
  std::array<char, 200> text{}; // the source, zero-terminated
  double writeMs = 0.0;         // 0-2000 ms: time to write the text out along its length; 0 = all at once
  double slant = 0.0;           // -0.5..0.5: lean of the letters, as a shear (0.2 is italic)

  void setText(std::string_view source) noexcept {
    text.fill(0);
    for (std::size_t i = 0; i < source.size() && i + 1 < text.size(); ++i)
      text[i] = source[i];
  }
};

class ScriptSource {
public:
  using Params = ScriptParams;
  static double rebuildSeconds(const Params&) noexcept { return 0.01; }

  void prepare(double sampleRate) { sheet_.prepare(sampleRate); }
  void birth(const Params& params, int note, int velocity, std::uint32_t serial);
  void build(const Params& params, int note, double age, std::span<XY> table);

  const std::vector<Stroke>& strokes() const noexcept { return strokes_; }

private:
  void layout(const Params& params);

  std::string laidOut_;
  double slant_ = 1e9;
  std::vector<Stroke> strokes_, shown_;
  std::vector<double> lengths_; // cumulative length at the end of each stroke
  HarmonicGrid grid_;
  SheetRenderer sheet_;
  std::vector<FillPolygon> fills_;
};

using ScriptEngine = MonoEngine<ScriptSource>;

} // namespace cs

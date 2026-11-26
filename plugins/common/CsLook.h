// Shared look: dark, phosphor green, Tufte-minimal.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cs::look {

inline const juce::Colour background{0xff0a0d0b};
// Phosphor green, luminance x (0.35, 1.0, 0.45) as in the scope renderer.
inline const juce::Colour phosphor = juce::Colour::fromFloatRGBA(0.35f, 1.0f, 0.45f, 1.0f);
inline const juce::Colour dim = phosphor.withAlpha(0.45f);

// Phase 0 placeholder face: the plugin name, nothing else.
inline void paintPlaceholder(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& name) {
  g.fillAll(background);
  g.setColour(dim);
  g.setFont(juce::FontOptions(15.0f));
  g.drawText(name, bounds.reduced(16), juce::Justification::topLeft);
}

} // namespace cs::look

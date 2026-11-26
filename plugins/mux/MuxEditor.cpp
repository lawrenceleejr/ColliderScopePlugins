#include "MuxEditor.h"

#include "CsLook.h"
#include "MuxProcessor.h"

MuxEditor::MuxEditor(MuxProcessor& p) : AudioProcessorEditor(p) { setSize(420, 420); }

void MuxEditor::paint(juce::Graphics& g) {
  g.fillAll(cs::look::background);
  g.setColour(cs::look::dim);
  g.setFont(cs::look::font(15.0f));
  g.drawText("CS Mux", getLocalBounds().reduced(16), juce::Justification::topLeft);
}

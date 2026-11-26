#include "MuxEditor.h"

#include "CsLook.h"
#include "MuxProcessor.h"

MuxEditor::MuxEditor(MuxProcessor& p) : AudioProcessorEditor(p) { setSize(420, 420); }

void MuxEditor::paint(juce::Graphics& g) { cs::look::paintPlaceholder(g, getLocalBounds(), "CS Mux"); }

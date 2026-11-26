#include "ChamberEditor.h"

#include "ChamberProcessor.h"
#include "CsLook.h"

ChamberEditor::ChamberEditor(ChamberProcessor& p) : AudioProcessorEditor(p) { setSize(420, 420); }

void ChamberEditor::paint(juce::Graphics& g) { cs::look::paintPlaceholder(g, getLocalBounds(), "CS Chamber"); }

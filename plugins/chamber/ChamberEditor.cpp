#include "ChamberEditor.h"

#include "ChamberProcessor.h"

namespace {
constexpr int kMargin = 16, kPanelWidth = 248;
}

ChamberEditor::ChamberEditor(ChamberProcessor& p)
    : AudioProcessorEditor(p), processor_(p), scope_(p.scopeFeed()), panel_(p.state()) {
  setLookAndFeel(&look_);
  addAndMakeVisible(scope_);

  panel_.heading("Track");
  for (const char* id : {"type", "seedMode", "diffusion", "drift", "condense"})
    panel_.add(id);
  panel_.heading("Wall");
  for (const char* id : {"wallMode", "wallRoot", "wallShare"})
    panel_.add(id);
  panel_.heading("Voice");
  for (const char* id : {"size", "attack", "release", "glide", "bendRange"})
    panel_.add(id);
  panel_.heading("Output");
  for (const char* id : {"gain", "scopeDecay"})
    panel_.add(id);
  addAndMakeVisible(panel_);

  setSize(kMargin + cs::ScopeDisplay::kSize + kMargin + kPanelWidth + kMargin, kMargin + cs::ScopeDisplay::kSize + kMargin);
  startTimerHz(10);
}

ChamberEditor::~ChamberEditor() {
  stopTimer();
  setLookAndFeel(nullptr);
}

void ChamberEditor::paint(juce::Graphics& g) { g.fillAll(cs::look::background); }

void ChamberEditor::resized() {
  scope_.setBounds(kMargin, kMargin, cs::ScopeDisplay::kSize, cs::ScopeDisplay::kSize);
  panel_.setBounds(kMargin + cs::ScopeDisplay::kSize + kMargin, kMargin - 8, kPanelWidth, panel_.preferredHeight());
}

void ChamberEditor::timerCallback() { scope_.setDecay(processor_.scopeDecay()); }

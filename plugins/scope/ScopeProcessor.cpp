// ColliderScope: one instrument plugin, several things it can draw. The
// Instrument control picks one: a bubble chamber, hydrogen orbitals, text and
// equations, a function of two variables, LHC-style collisions, a 3D model, Feynman diagrams, scenes from
// around particle physics, or the CMS outer tracker.
#include <map>

#include "InstrumentProcessor.h"
#include "cscore/lockfree.h"
#include "cscore/multi.h"

namespace {

using Range = juce::NormalisableRange<float>;
using namespace cs::param;

const char* const kDefaultText = "E = mc^2";
const char* const kDefaultFunction = "sin(3*r - 4*t) / (1 + r)";

// A numbered parameter whose host-visible name carries its instrument.
std::unique_ptr<juce::AudioParameterFloat> num(const char* paramId, const char* name, Range range, float fallback,
                                               const char* unit, int decimals) {
  return number(paramId, name, range, fallback, unit, decimals);
}

juce::AudioProcessorValueTreeState::ParameterLayout layout() {
  juce::AudioProcessorValueTreeState::ParameterLayout l;
  l.add(choice("instrument", "Instrument", {"Bubble", "Orbital", "Script", "Function", "Collision", "Mesh", "Feynman", "Scenes", "CMS tracker"}, 0));

  l.add(choice("b_type", "Bubble: Event", {"Auto", "Electron", "Pair", "Decay", "Beam", "Random"}, 0));
  l.add(choice("b_seedMode", "Bubble: Seed mode", {"Random", "Per key", "Global"}, 0));
  l.add(num("b_field", "Bubble: Field", Range(0.0f, 1.0f, 0.01f), 0.5f, "", 2));
  l.add(num("b_form", "Bubble: Form time", Range(0.0f, 200.0f, 1.0f), 40.0f, "ms", 0));
  l.add(whole("b_bubbles", "Bubble: Bubbles", 20, 200, 90));
  l.add(num("b_growth", "Bubble: Bubble growth", Range(0.0f, 0.1f, 0.001f), 0.02f, "/s", 3));
  l.add(num("b_maxRadius", "Bubble: Bubble size", Range(0.005f, 0.06f, 0.001f), 0.03f, "", 3));
  l.add(num("b_boil", "Bubble: Boil", Range(0.0f, 1.0f, 0.01f), 0.15f, "", 2));

  juce::StringArray orbitals{"Auto"};
  for (const cs::OrbitalId& orbital : cs::kOrbitals)
    orbitals.add(orbital.name);
  l.add(choice("o_orbital", "Orbital: Orbital", orbitals, 0));
  l.add(num("o_level", "Orbital: Iso level", Range(0.05f, 0.8f, 0.01f), 0.25f, "", 2));
  l.add(whole("o_lines", "Orbital: Wires", 4, 16, 8));
  l.add(num("o_bloom", "Orbital: Bloom time", Range(0.0f, 500.0f, 1.0f), 80.0f, "ms", 0));
  l.add(num("o_spin", "Orbital: Spin", Range(-180.0f, 180.0f, 1.0f), 30.0f, "deg/s", 0));
  l.add(num("o_tilt", "Orbital: Tilt", Range(0.0f, 90.0f, 1.0f), 25.0f, "deg", 0));

  Range write(0.0f, 2000.0f, 1.0f);
  write.setSkewForCentre(300.0f);
  l.add(num("s_write", "Script: Write time", write, 0.0f, "ms", 0));
  l.add(num("s_slant", "Script: Slant", Range(-0.5f, 0.5f, 0.01f), 0.0f, "", 2));

  l.add(num("f_range", "Function: Range", Range(0.5f, 12.0f, 0.1f), 3.0f, "", 1));
  l.add(num("f_height", "Function: Height", Range(0.0f, 2.0f, 0.01f), 0.6f, "", 2));
  l.add(whole("f_lines", "Function: Wires", 4, 32, 14));
  l.add(choice("f_hidden", "Function: Hidden lines", {"Shown", "Removed"}, 1));
  l.add(num("f_azimuth", "Function: Azimuth", Range(-180.0f, 180.0f, 1.0f), 35.0f, "deg", 0));
  l.add(num("f_elevation", "Function: Elevation", Range(0.0f, 90.0f, 1.0f), 30.0f, "deg", 0));
  l.add(num("f_perspective", "Function: Perspective", Range(0.0f, 1.0f, 0.01f), 0.2f, "", 2));
  l.add(num("f_spin", "Function: Spin", Range(-180.0f, 180.0f, 1.0f), 15.0f, "deg/s", 0));
  // Sheets: shared by every instrument.
  l.add(choice("sheet", "Sheet: Style", {"Lines", "Sheet"}, 0));
  l.add(whole("scanlines", "Sheet: Scanlines", 8, 160, 64));
  l.add(num("shading", "Sheet: Shading", Range(0.0f, 1.0f, 0.01f), 0.7f, "", 2));
  l.add(choice("interlace", "Sheet: Interlace", {"Off", "2", "4"}, 0)); // 3 would repeat the picture every third period
  l.add(num("fillShare", "Sheet: Fill share", Range(0.2f, 0.95f, 0.01f), 0.6f, "", 2));
  l.add(num("lineWidth", "Sheet: Line width", Range(0.005f, 0.08f, 0.001f), 0.025f, "", 3));

  l.add(choice("c_event", "Collision: Event",
               {"Auto", "Minimum bias", "Dijet", "Z to muons", "Multijet", "Four muons", "Random"}, 0));
  l.add(choice("c_seedMode", "Collision: Seed mode", {"Random", "Per key", "Global"}, 0));
  l.add(num("c_field", "Collision: Field", Range(0.0f, 1.0f, 0.01f), 0.5f, "", 2));
  l.add(num("c_pileup", "Collision: Pile-up", Range(0.0f, 1.0f, 0.01f), 0.3f, "", 2));
  l.add(choice("c_detector", "Collision: Detector ring", {"Off", "On"}, 1));
  l.add(num("c_form", "Collision: Form time", Range(0.0f, 300.0f, 1.0f), 60.0f, "ms", 0));
  l.add(choice("c_view", "Collision: View", {"Along the beam", "3D"}, 0));
  l.add(num("c_azimuth", "Collision: Azimuth", Range(-180.0f, 180.0f, 1.0f), 35.0f, "deg", 0));
  l.add(num("c_elevation", "Collision: Elevation", Range(-90.0f, 90.0f, 1.0f), 20.0f, "deg", 0));
  l.add(num("c_perspective", "Collision: Perspective", Range(0.0f, 1.0f, 0.01f), 0.4f, "", 2));
  l.add(num("c_spin", "Collision: Spin", Range(-180.0f, 180.0f, 1.0f), 20.0f, "deg/s", 0));

  l.add(num("m_azimuth", "Mesh: Azimuth", Range(-180.0f, 180.0f, 1.0f), 30.0f, "deg", 0));
  l.add(num("m_elevation", "Mesh: Elevation", Range(-90.0f, 90.0f, 1.0f), 25.0f, "deg", 0));
  l.add(num("m_roll", "Mesh: Roll", Range(-180.0f, 180.0f, 1.0f), 0.0f, "deg", 0));
  l.add(num("m_perspective", "Mesh: Perspective", Range(0.0f, 1.0f, 0.01f), 0.3f, "", 2));
  l.add(num("m_zoom", "Mesh: Zoom", Range(0.2f, 1.5f, 0.01f), 0.9f, "", 2));
  l.add(num("m_spin", "Mesh: Spin", Range(-180.0f, 180.0f, 1.0f), 30.0f, "deg/s", 0));
  l.add(choice("m_axis", "Mesh: Spin axis", {"X", "Y", "Z"}, 2));
  l.add(num("m_crease", "Mesh: Crease angle", Range(1.0f, 90.0f, 1.0f), 30.0f, "deg", 0));
  l.add(choice("m_cull", "Mesh: Far side", {"Shown", "Hidden"}, 1));
  l.add(whole("m_edges", "Mesh: Most edges", 50, 2000, 600));

  juce::StringArray diagrams{"Auto"};
  for (const cs::FeynmanDiagram& diagram : cs::feynmanDiagrams())
    diagrams.add(diagram.name);
  l.add(choice("k_diagram", "Feynman: Diagram", diagrams, 0));
  l.add(num("k_form", "Feynman: Form time", Range(0.0f, 500.0f, 1.0f), 80.0f, "ms", 0));
  l.add(num("k_wiggle", "Feynman: Wiggle", Range(0.3f, 3.0f, 0.01f), 1.0f, "", 2));
  l.add(num("k_motion", "Feynman: Motion", Range(-4.0f, 4.0f, 0.01f), 1.0f, "/s", 2));
  l.add(choice("k_arrows", "Feynman: Arrows", {"Off", "On"}, 1));

  juce::StringArray scenes;
  for (int i = 0; i < cs::kSceneCount; ++i)
    scenes.add(cs::sceneName(static_cast<cs::Scene>(i)));
  l.add(choice("g_scene", "Scenes: Scene", scenes, 0));
  l.add(choice("g_seedMode", "Scenes: Seed mode", {"Random", "Per key", "Global"}, 0));
  Range form(0.0f, 2000.0f, 1.0f);
  form.setSkewForCentre(300.0f);
  l.add(num("g_form", "Scenes: Form time", form, 300.0f, "ms", 0));
  l.add(num("g_speed", "Scenes: Speed", Range(0.0f, 4.0f, 0.01f), 1.0f, "", 2));
  l.add(num("g_detail", "Scenes: Detail", Range(0.0f, 1.0f, 0.01f), 0.5f, "", 2));
  l.add(num("g_azimuth", "Scenes: Azimuth", Range(-180.0f, 180.0f, 1.0f), 30.0f, "deg", 0));
  l.add(num("g_elevation", "Scenes: Elevation", Range(-90.0f, 90.0f, 1.0f), 25.0f, "deg", 0));
  l.add(num("g_perspective", "Scenes: Perspective", Range(0.0f, 1.0f, 0.01f), 0.3f, "", 2));
  l.add(num("g_spin", "Scenes: Spin", Range(-180.0f, 180.0f, 1.0f), 15.0f, "deg/s", 0));

  l.add(choice("t_stage", "CMS tracker: Stage", {"Auto", "Module", "Exploded", "Tracker", "Tracks"}, 0));
  l.add(choice("t_module", "CMS tracker: Module", {"2S", "PS"}, 0));
  l.add(num("t_speed", "CMS tracker: Speed", Range(0.1f, 4.0f, 0.01f), 1.0f, "", 2));
  l.add(num("t_detail", "CMS tracker: Detail", Range(0.0f, 1.0f, 0.01f), 0.6f, "", 2));
  l.add(choice("t_seedMode", "CMS tracker: Seed mode", {"Random", "Per key", "Global"}, 0));
  l.add(num("t_azimuth", "CMS tracker: Azimuth", Range(-180.0f, 180.0f, 1.0f), 30.0f, "deg", 0));
  l.add(num("t_elevation", "CMS tracker: Elevation", Range(-90.0f, 90.0f, 1.0f), 20.0f, "deg", 0));
  l.add(num("t_perspective", "CMS tracker: Perspective", Range(0.0f, 1.0f, 0.01f), 0.35f, "", 2));
  l.add(num("t_spin", "CMS tracker: Spin", Range(-180.0f, 180.0f, 1.0f), 12.0f, "deg/s", 0));

  addVoice(l);
  return l;
}

class ScopeProcessor final : public cs::InstrumentProcessor<cs::MultiEngine> {
public:
  ScopeProcessor() : InstrumentProcessor("ColliderScope", layout()) {
    for (const char* id :
         {"instrument", "b_type",  "b_seedMode", "b_field",    "b_form",      "b_bubbles",    "b_growth",      "b_maxRadius",
          "b_boil",     "o_orbital", "o_level",  "o_lines",    "o_bloom",     "o_spin",       "o_tilt",        "s_write",
          "s_slant",    "f_range", "f_height",   "f_lines",    "f_hidden",    "f_azimuth",    "f_elevation",   "f_perspective",
          "f_spin",     "c_event", "c_seedMode", "c_field",    "c_pileup",    "c_detector",   "c_form",        "m_azimuth",
          "m_elevation", "m_roll", "m_perspective", "m_zoom",  "m_spin",      "m_axis",       "m_crease",      "m_cull",
          "m_edges",    "c_view",  "c_azimuth",  "c_elevation", "c_perspective", "c_spin",     "k_diagram",     "k_form",
          "k_wiggle",   "k_motion", "k_arrows",   "g_scene",    "g_seedMode",  "g_form",       "g_speed",       "g_detail",
          "g_azimuth",  "g_elevation", "g_perspective", "g_spin", "t_stage",     "t_module",     "t_speed",       "t_detail",
          "t_seedMode", "t_azimuth", "t_elevation", "t_perspective", "t_spin", "sheet", "scanlines", "shading",
          "interlace", "fillShare", "lineWidth"})
      values_[id] = raw(id);
    setText(kDefaultText);
    setFunction(kDefaultFunction);
  }

  juce::AudioProcessorEditor* createEditor() override {
    return new cs::Editor(
        *this, state_, scopeFeed_, "scopeDecay", [this](cs::ParamPanel& panel) { fill(panel); },
        [this] { return instrument(); });
  }

private:
  float v(const char* id) const noexcept { return values_.at(id)->load(); }
  int i(const char* id) const noexcept { return juce::roundToInt(v(id)); }
  int instrument() const noexcept { return juce::roundToInt(instrument_->load()); }

  // ---- things that are not parameters: two texts and a file ----
  void setText(const juce::String& text) {
    state_.state.setProperty("text", text, nullptr);
    cs::ScriptParams p;
    p.setText(text.toStdString());
    text_.write(p.text);
  }
  void setFunction(const juce::String& text) {
    state_.state.setProperty("function", text, nullptr);
    cs::SurfaceParams p;
    p.setFunction(text.toStdString());
    function_.write(p.function);
  }
  // Loads an STL for the Mesh instrument. Message thread. Returns what happened, in words.
  juce::String loadMesh(const juce::File& file) {
    juce::MemoryBlock data;
    if (!file.existsAsFile() || !file.loadFileAsData(data))
      return "cannot read " + file.getFileName();
    std::string error;
    auto mesh = cs::parseStl({static_cast<const std::uint8_t*>(data.getData()), data.getSize()}, &error);
    if (!mesh)
      return file.getFileName() + ": " + juce::String(error);
    engine_.sourceForSetup().setMesh(std::move(mesh));
    state_.state.setProperty("stl", file.getFullPathName(), nullptr);
    return file.getFileName();
  }
  juce::String meshLabel() const {
    const juce::String path = state_.state.getProperty("stl", "").toString();
    return path.isEmpty() ? juce::String("Load STL...  (showing a torus)") : juce::File(path).getFileName();
  }

  void stateRestored() override {
    setText(state_.state.getProperty("text", kDefaultText).toString());
    setFunction(state_.state.getProperty("function", kDefaultFunction).toString());
    const juce::String path = state_.state.getProperty("stl", "").toString();
    if (path.isNotEmpty())
      loadMesh(juce::File(path)); // if the file has gone, the torus stays
  }

  // ---- the panel: the instrument menu, then only that instrument's controls ----
  void fill(cs::ParamPanel& panel) {
    panel.add("instrument");
    cs::Editor::addPresetMenu(panel, state_, &presets());
    switch (static_cast<cs::Instrument>(instrument())) {
    case cs::Instrument::Bubble:
      cs::Editor::addGroups(panel, {{"Event", {"b_type", "b_seedMode", "b_field", "b_form"}},
                                    {"Bubbles", {"b_bubbles", "b_growth", "b_maxRadius", "b_boil"}}});
      break;
    case cs::Instrument::Orbital:
      cs::Editor::addGroups(panel, {{"Surface", {"o_orbital", "o_level", "o_lines", "o_bloom"}}, {"View", {"o_spin", "o_tilt"}}});
      break;
    case cs::Instrument::Script:
      panel.addText("Text", state_.state.getProperty("text", kDefaultText).toString(),
                    [this](const juce::String& text) { setText(text); });
      cs::Editor::addGroups(panel, {{"Writing", {"s_write", "s_slant"}}});
      break;
    case cs::Instrument::Function:
      panel.addText("z = f(x, y, t)", state_.state.getProperty("function", kDefaultFunction).toString(),
                    [this](const juce::String& text) { setFunction(text); });
      cs::Editor::addGroups(panel, {{"Surface", {"f_range", "f_height", "f_hidden"}},
                                    {"Drawing", {"f_lines"}},
                                    {"Camera", {"f_azimuth", "f_elevation", "f_perspective", "f_spin"}}});
      break;
    case cs::Instrument::Collision:
      cs::Editor::addGroups(panel, {{"Event", {"c_event", "c_seedMode", "c_pileup", "c_form"}},
                                    {"Detector", {"c_field", "c_detector"}},
                                    {"View", {"c_view", "c_azimuth", "c_elevation", "c_perspective", "c_spin"}}});
      break;
    case cs::Instrument::Feynman:
      cs::Editor::addGroups(panel, {{"Diagram", {"k_diagram", "k_form", "k_arrows"}}, {"Waves", {"k_wiggle", "k_motion"}}});
      break;
    case cs::Instrument::Scenes:
      cs::Editor::addGroups(panel, {{"Scene", {"g_scene", "g_seedMode", "g_form", "g_speed", "g_detail"}},
                                    {"Camera (3D scenes)", {"g_azimuth", "g_elevation", "g_perspective", "g_spin"}}});
      break;
    case cs::Instrument::Tracker:
      cs::Editor::addGroups(panel, {{"Sequence", {"t_stage", "t_module", "t_speed", "t_detail", "t_seedMode"}},
                                    {"Camera", {"t_azimuth", "t_elevation", "t_perspective", "t_spin"}}});
      break;
    case cs::Instrument::Mesh:
      panel.addButton("Model", meshLabel(), [this, &panel] { chooseMesh(panel); });
      cs::Editor::addGroups(panel, {{"Camera", {"m_azimuth", "m_elevation", "m_roll", "m_perspective", "m_zoom"}},
                                    {"Rotation", {"m_spin", "m_axis"}},
                                    {"Edges", {"m_crease", "m_cull", "m_edges"}}});
      break;
    }
    cs::Editor::addGroups(panel, {{"Sheet", {"sheet", "scanlines", "shading", "interlace", "fillShare", "lineWidth"}},
                                  {"Voice", kVoiceGroup},
                                  {"Output", kOutputGroup}});
  }

  void chooseMesh(cs::ParamPanel& panel) {
    chooser_ = std::make_unique<juce::FileChooser>("Choose an STL file", juce::File(state_.state.getProperty("stl", "").toString()),
                                                   "*.stl;*.STL");
    juce::Component::SafePointer<cs::ParamPanel> safe(&panel);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, safe](const juce::FileChooser& chooser) {
                            const juce::File file = chooser.getResult();
                            if (file == juce::File())
                              return;
                            const juce::String result = loadMesh(file);
                            if (safe != nullptr)
                              if (auto* button = dynamic_cast<juce::TextButton*>(safe->findChildWithID("button")))
                                button->setButtonText(result);
                          });
  }

  cs::MultiParams readParams() const noexcept override {
    cs::MultiParams p;
    p.voice = readVoice();
    p.voice.sheet.style = static_cast<cs::SheetParams::Style>(i("sheet"));
    p.voice.sheet.scanlines = i("scanlines");
    p.voice.sheet.shading = v("shading");
    p.voice.sheet.interlace = 1 << i("interlace"); // 1, 2, 4
    p.voice.sheet.fillShare = v("fillShare");
    p.voice.sheet.width = v("lineWidth");
    p.instrument = static_cast<cs::Instrument>(instrument());

    p.bubble.type = static_cast<cs::BubbleType>(i("b_type"));
    p.bubble.seedMode = static_cast<cs::BubbleSeed>(i("b_seedMode"));
    p.bubble.field = v("b_field");
    p.bubble.formMs = v("b_form");
    p.bubble.bubbles = i("b_bubbles");
    p.bubble.growth = v("b_growth");
    p.bubble.maxRadius = v("b_maxRadius");
    p.bubble.boil = v("b_boil");

    p.orbital.orbital = i("o_orbital");
    p.orbital.level = v("o_level");
    p.orbital.lines = i("o_lines");
    p.orbital.bloomMs = v("o_bloom");
    p.orbital.spin = v("o_spin");
    p.orbital.tilt = v("o_tilt");

    p.script.text = text_.read();
    p.script.writeMs = v("s_write");
    p.script.slant = v("s_slant");

    p.surface.function = function_.read();
    p.surface.range = v("f_range");
    p.surface.height = v("f_height");
    p.surface.lines = i("f_lines");
    p.surface.hidden = i("f_hidden") == 1;
    p.surface.camera = {v("f_azimuth"), v("f_elevation"), 0.0, v("f_perspective")};
    p.surface.spin = v("f_spin");

    p.collision.event = static_cast<cs::CollisionEvent>(i("c_event"));
    p.collision.seedMode = static_cast<cs::CollisionSeed>(i("c_seedMode"));
    p.collision.field = v("c_field");
    p.collision.pileup = v("c_pileup");
    p.collision.detector = i("c_detector") == 1;
    p.collision.formMs = v("c_form");
    p.collision.threeD = i("c_view") == 1;
    p.collision.camera = {v("c_azimuth"), v("c_elevation"), 0.0, v("c_perspective")};
    p.collision.spin = v("c_spin");

    p.feynman.diagram = i("k_diagram");
    p.feynman.formMs = v("k_form");
    p.feynman.wiggle = v("k_wiggle");
    p.feynman.motion = v("k_motion");
    p.feynman.arrows = i("k_arrows") == 1;

    p.gallery.scene = static_cast<cs::Scene>(i("g_scene"));
    p.gallery.seedMode = static_cast<cs::SceneSeed>(i("g_seedMode"));
    p.gallery.formMs = v("g_form");
    p.gallery.speed = v("g_speed");
    p.gallery.detail = v("g_detail");
    p.gallery.camera = {v("g_azimuth"), v("g_elevation"), 0.0, v("g_perspective")};
    p.gallery.spin = v("g_spin");

    p.tracker.stage = static_cast<cs::TrackerStage>(i("t_stage"));
    p.tracker.module = static_cast<cs::ModuleType>(i("t_module"));
    p.tracker.speed = v("t_speed");
    p.tracker.detail = v("t_detail");
    p.tracker.seedMode = i("t_seedMode");
    p.tracker.camera = {v("t_azimuth"), v("t_elevation"), 0.0, v("t_perspective")};
    p.tracker.spin = v("t_spin");

    p.mesh.camera = {v("m_azimuth"), v("m_elevation"), v("m_roll"), v("m_perspective")};
    p.mesh.zoom = v("m_zoom");
    p.mesh.spin = v("m_spin");
    p.mesh.spinAxis = i("m_axis");
    p.mesh.crease = v("m_crease");
    p.mesh.cull = i("m_cull") == 1;
    p.mesh.maxEdges = i("m_edges");
    return p;
  }

  // Not used: this plugin lays its own panel out.
  std::vector<cs::ParamGroup> groups() const override { return {}; }

  // Instrument: 0 Bubble, 1 Orbital, 2 Script, 3 Function, 4 Collision, 5 Mesh, 6 Feynman, 7 Scenes, 8 CMS tracker.
  // Scene: 0 Tracking planes ... 3 Cherenkov rings ... 6 Cyclotron ... 11 Eightfold Way.
  const std::vector<cs::Preset>& presets() const override {
    static const std::vector<cs::Preset> list = {
        {"Bubble: spiral bass",
         {{"instrument", 0}, {"b_type", 1}, {"b_seedMode", 1}, {"b_field", 0.55f}, {"b_form", 15}, {"b_bubbles", 60},
          {"b_growth", 0.004f}, {"b_maxRadius", 0.012f}, {"b_boil", 0.0f}, {"release", 0}, {"glide", 0}}},
        {"Bubble: fizz",
         {{"instrument", 0}, {"b_type", 5}, {"b_seedMode", 0}, {"b_field", 0.5f}, {"b_form", 60}, {"b_bubbles", 140},
          {"b_growth", 0.08f}, {"b_maxRadius", 0.05f}, {"b_boil", 0.7f}, {"release", 250}}},
        {"Orbital: p lead",
         {{"instrument", 1}, {"o_orbital", 4}, {"o_level", 0.25f}, {"o_lines", 8}, {"o_bloom", 40}, {"o_spin", 120},
          {"o_tilt", 20}, {"glide", 50}, {"release", 60}}},
        {"Orbital: d pad",
         {{"instrument", 1}, {"o_orbital", 8}, {"o_level", 0.2f}, {"o_lines", 10}, {"o_bloom", 350}, {"o_spin", 25},
          {"o_tilt", 35}, {"attack", 30}, {"release", 500}}},
        {"Script: handwriting", {{"instrument", 2}, {"s_write", 400}, {"s_slant", 0.2f}, {"release", 150}}},
        {"Function: ripples",
         {{"instrument", 3}, {"f_range", 3.0f}, {"f_height", 0.6f}, {"f_lines", 14}, {"f_hidden", 1}, {"f_azimuth", 35},
          {"f_elevation", 30}, {"f_perspective", 0.2f}, {"f_spin", 15}, {"release", 200}}},
        {"Function: lit sheet",
         {{"instrument", 3}, {"sheet", 1}, {"scanlines", 64}, {"shading", 0.8f}, {"interlace", 1}, {"f_range", 3.0f},
          {"f_height", 0.6f}, {"f_hidden", 1}, {"f_azimuth", 35}, {"f_elevation", 30}, {"f_perspective", 0.2f}, {"f_spin", 10},
          {"release", 200}}},
        {"Collision: by velocity",
         {{"instrument", 4}, {"c_event", 0}, {"c_seedMode", 0}, {"c_field", 0.5f}, {"c_pileup", 0.3f}, {"c_detector", 1},
          {"c_form", 60}, {"release", 0}}},
        {"Collision: four muons",
         {{"instrument", 4}, {"c_event", 5}, {"c_seedMode", 1}, {"c_field", 0.5f}, {"c_pileup", 0.1f}, {"c_detector", 1},
          {"c_form", 30}, {"release", 120}}},
        {"Collision: 3D, turning",
         {{"instrument", 4}, {"c_event", 0}, {"c_view", 1}, {"c_azimuth", 35}, {"c_elevation", 20}, {"c_perspective", 0.4f},
          {"c_spin", 20}, {"c_detector", 1}, {"c_pileup", 0.2f}, {"release", 150}}},
        {"Feynman: by velocity",
         {{"instrument", 6}, {"k_diagram", 0}, {"k_form", 80}, {"k_wiggle", 1.0f}, {"k_motion", 1.0f}, {"k_arrows", 1},
          {"release", 100}}},
        {"Scenes: Cherenkov rings",
         {{"instrument", 7}, {"g_scene", 3}, {"g_form", 250}, {"g_speed", 1}, {"g_detail", 0.7f}, {"g_spin", 10}, {"release", 200}}},
        {"Scenes: cyclotron",
         {{"instrument", 7}, {"g_scene", 6}, {"g_form", 0}, {"g_speed", 1}, {"g_detail", 0.5f}, {"release", 100}}},
        {"CMS tracker: the full story",
         {{"instrument", 8}, {"t_stage", 0}, {"t_module", 0}, {"t_speed", 1}, {"t_detail", 0.6f}, {"t_spin", 12}, {"release", 300}}},
        {"CMS tracker: collisions",
         {{"instrument", 8}, {"t_stage", 4}, {"t_speed", 1}, {"t_detail", 0.5f}, {"t_spin", 8}, {"release", 150}}},
        {"Mesh: turntable",
         {{"instrument", 5}, {"m_azimuth", 30}, {"m_elevation", 25}, {"m_roll", 0}, {"m_perspective", 0.3f}, {"m_zoom", 0.9f},
          {"m_spin", 30}, {"m_axis", 2}, {"m_crease", 30}, {"m_cull", 1}, {"m_edges", 600}, {"release", 200}}},
    };
    return list;
  }

  std::map<juce::String, std::atomic<float>*> values_;
  std::atomic<float>* instrument_ = raw("instrument");
  cs::LatestValue<std::array<char, 200>> text_;
  cs::LatestValue<std::array<char, 160>> function_;
  std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ScopeProcessor(); }

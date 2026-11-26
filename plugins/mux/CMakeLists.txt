include(${PROJECT_SOURCE_DIR}/plugins/common/CsPlugin.cmake)

# Plain effect for now. Slot mode's MIDI trigger (Phase 3) may need the AU to
# become a MusicEffect; that depends on the DAW, an open question in the spec.
juce_add_plugin(CSMux
  PRODUCT_NAME "CS Mux"
  COMPANY_NAME "ColliderScope"
  BUNDLE_ID com.colliderscope.csmux
  PLUGIN_MANUFACTURER_CODE Clsc
  PLUGIN_CODE CMux
  FORMATS VST3 AU Standalone
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Fx
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD ${CS_COPY_PLUGINS})

target_sources(CSMux PRIVATE
  MuxProcessor.cpp
  MuxEditor.cpp)

cs_configure_plugin(CSMux)

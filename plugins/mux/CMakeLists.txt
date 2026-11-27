include(${PROJECT_SOURCE_DIR}/plugins/common/CsPlugin.cmake)

# An effect that takes MIDI (Slot mode's triggers). In Ableton Live, use the
# VST3: Live routes MIDI to VST3 effects but not to AU effects. The AU is a
# MusicEffect so that hosts which do route MIDI to AU effects can.
juce_add_plugin(CSMux
  PRODUCT_NAME "CS Mux"
  COMPANY_NAME "ColliderScope"
  BUNDLE_ID com.colliderscope.csmux
  PLUGIN_MANUFACTURER_CODE Clsc
  PLUGIN_CODE CMux
  FORMATS ${CS_PLUGIN_FORMATS}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Fx
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  COPY_PLUGIN_AFTER_BUILD ${CS_COPY_PLUGINS})

target_sources(CSMux PRIVATE
  MuxProcessor.cpp)

cs_configure_plugin(CSMux)
cs_add_uisnap(csmux_uisnap CSMux)

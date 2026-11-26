include(${PROJECT_SOURCE_DIR}/plugins/common/CsPlugin.cmake)

juce_add_plugin(CSOrbital
  PRODUCT_NAME "CS Orbital"
  COMPANY_NAME "ColliderScope"
  BUNDLE_ID com.colliderscope.csorbital
  PLUGIN_MANUFACTURER_CODE Clsc
  PLUGIN_CODE Orbt
  FORMATS ${CS_PLUGIN_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD ${CS_COPY_PLUGINS})

target_sources(CSOrbital PRIVATE
  OrbitalProcessor.cpp)

cs_configure_plugin(CSOrbital)
cs_add_uisnap(csorbital_uisnap CSOrbital)

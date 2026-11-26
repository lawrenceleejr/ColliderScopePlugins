include(${PROJECT_SOURCE_DIR}/plugins/common/CsPlugin.cmake)

juce_add_plugin(CSScript
  PRODUCT_NAME "CS Script"
  COMPANY_NAME "ColliderScope"
  BUNDLE_ID com.colliderscope.csscript
  PLUGIN_MANUFACTURER_CODE Clsc
  PLUGIN_CODE Scrp
  FORMATS ${CS_PLUGIN_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD ${CS_COPY_PLUGINS})

target_sources(CSScript PRIVATE
  ScriptProcessor.cpp)

cs_configure_plugin(CSScript)
cs_add_uisnap(csscript_uisnap CSScript)

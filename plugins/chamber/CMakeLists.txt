include(${PROJECT_SOURCE_DIR}/plugins/common/CsPlugin.cmake)

juce_add_plugin(CSChamber
  PRODUCT_NAME "CS Chamber"
  COMPANY_NAME "ColliderScope"
  BUNDLE_ID com.colliderscope.cschamber
  PLUGIN_MANUFACTURER_CODE Clsc
  PLUGIN_CODE Chmb
  FORMATS VST3 AU Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD ${CS_COPY_PLUGINS})

target_sources(CSChamber PRIVATE
  ChamberProcessor.cpp
  ChamberEditor.cpp)

cs_configure_plugin(CSChamber)

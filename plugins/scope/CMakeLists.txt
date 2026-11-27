include(${PROJECT_SOURCE_DIR}/plugins/common/CsPlugin.cmake)

juce_add_plugin(CSScope
  PRODUCT_NAME "ColliderScope"
  COMPANY_NAME "ColliderScope"
  BUNDLE_ID com.colliderscope.scope
  PLUGIN_MANUFACTURER_CODE Clsc
  PLUGIN_CODE Scop
  FORMATS ${CS_PLUGIN_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD ${CS_COPY_PLUGINS})

target_sources(CSScope PRIVATE
  ScopeProcessor.cpp)

cs_configure_plugin(CSScope)
cs_add_uisnap(colliderscope_uisnap CSScope)

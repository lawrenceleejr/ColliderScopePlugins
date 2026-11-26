include(${PROJECT_SOURCE_DIR}/plugins/common/CsPlugin.cmake)

juce_add_plugin(CSBubble
  PRODUCT_NAME "CS Bubble"
  COMPANY_NAME "ColliderScope"
  BUNDLE_ID com.colliderscope.csbubble
  PLUGIN_MANUFACTURER_CODE Clsc
  PLUGIN_CODE Bubl
  FORMATS ${CS_PLUGIN_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Instrument Synth
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  COPY_PLUGIN_AFTER_BUILD ${CS_COPY_PLUGINS})

target_sources(CSBubble PRIVATE
  BubbleProcessor.cpp)

cs_configure_plugin(CSBubble)
cs_add_uisnap(csbubble_uisnap CSBubble)

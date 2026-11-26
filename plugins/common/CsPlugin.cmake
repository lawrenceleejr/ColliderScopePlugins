# Settings shared by both plugins.
function(cs_configure_plugin target)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/plugins/common)
  target_compile_definitions(${target} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
  target_link_libraries(${target}
    PRIVATE
      cscore
      juce::juce_audio_utils
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)
endfunction()

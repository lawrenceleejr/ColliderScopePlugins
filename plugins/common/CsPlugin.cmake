# Settings shared by both plugins.
if(APPLE)
  set(CS_PLUGIN_FORMATS VST3 AU Standalone)
else()
  set(CS_PLUGIN_FORMATS VST3 Standalone) # for compile checks off macOS
endif()

find_package(Threads REQUIRED)

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
      Threads::Threads
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)
endfunction()

# A console tool that saves a picture of the plugin's editor (tools/uisnap).
function(cs_add_uisnap target plugin)
  juce_add_console_app(${target})
  target_sources(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tools/uisnap/main.cpp)
  target_compile_definitions(${target} PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(${target} PRIVATE ${plugin} juce::juce_audio_utils)
endfunction()

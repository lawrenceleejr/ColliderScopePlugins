# cscore: the shared DSP core. No JUCE, no platform code.
add_library(cscore STATIC
  src/cscore.cpp)
target_include_directories(cscore PUBLIC include)
target_compile_features(cscore PUBLIC cxx_std_20)
target_compile_definitions(cscore PRIVATE CSCORE_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(cscore PRIVATE /W4)
else()
  target_compile_options(cscore PRIVATE -Wall -Wextra -Wpedantic)
endif()

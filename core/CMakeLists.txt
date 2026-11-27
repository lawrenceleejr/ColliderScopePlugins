# cscore: the shared DSP core. No JUCE, no platform code.
add_library(cscore STATIC
  src/bubble.cpp
  src/chamber_engine.cpp
  src/collision.cpp
  src/cscore.cpp
  src/expr.cpp
  src/feynman.cpp
  src/gallery.cpp
  src/hits.cpp
  src/mesh.cpp
  src/mux_engine.cpp
  src/orbital.cpp
  src/path.cpp
  src/scope_view.cpp
  src/script.cpp
  src/surface.cpp
  src/tracker.cpp
  src/tracks.cpp
  src/yin.cpp)
target_include_directories(cscore PUBLIC include)
target_compile_features(cscore PUBLIC cxx_std_20)
target_compile_definitions(cscore PRIVATE CSCORE_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(cscore PRIVATE /W4)
else()
  target_compile_options(cscore PRIVATE -Wall -Wextra -Wpedantic)
  # The scope's blur loops need the vectoriser, which GCC holds back at -O2.
  target_compile_options(cscore PRIVATE $<$<CONFIG:Release>:-O3>)
endif()

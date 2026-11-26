add_executable(cscore_tests
  test_chamber.cpp
  test_hits.cpp
  test_mux.cpp
  test_path.cpp
  test_pitch.cpp
  test_tracks.cpp)
target_link_libraries(cscore_tests PRIVATE cscore Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(cscore_tests)

# Real-time checks get their own binary: it replaces global operator new.
find_package(Threads REQUIRED)
add_executable(cscore_rt_tests test_realtime.cpp)
target_link_libraries(cscore_rt_tests PRIVATE cscore Catch2::Catch2WithMain Threads::Threads)
add_test(NAME realtime COMMAND cscore_rt_tests)

add_test(NAME csrender_version COMMAND csrender --version)
set_tests_properties(csrender_version PROPERTIES
  PASS_REGULAR_EXPRESSION "^csrender ${PROJECT_VERSION}")

# The command-line renders: a WAV and a PNG per feature, kept by CI.
add_test(NAME csrender_renders
  COMMAND ${PROJECT_SOURCE_DIR}/tools/render_tests.sh $<TARGET_FILE:csrender> ${PROJECT_BINARY_DIR}/renders)

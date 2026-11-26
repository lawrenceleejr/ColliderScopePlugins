add_executable(cscore_tests
  test_pitch.cpp)
target_link_libraries(cscore_tests PRIVATE cscore Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(cscore_tests)

add_test(NAME csrender_version COMMAND csrender --version)
set_tests_properties(csrender_version PROPERTIES
  PASS_REGULAR_EXPRESSION "^csrender ${PROJECT_VERSION}")

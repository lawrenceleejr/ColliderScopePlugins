cmake_minimum_required(VERSION 3.22)

# Universal binary, macOS 12+. Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")

project(ColliderScopePlugins VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# The core, the command-line tool and the tests build anywhere.
# The plugins need JUCE and are built on macOS only by default.
if(APPLE)
  set(CS_PLUGINS_DEFAULT ON)
else()
  set(CS_PLUGINS_DEFAULT OFF)
endif()
option(CS_BUILD_PLUGINS "Build the CS Chamber and CS Mux plugins (fetches JUCE)" ${CS_PLUGINS_DEFAULT})
option(CS_BUILD_TESTS "Build unit and acceptance tests (fetches Catch2)" ON)
option(CS_COPY_PLUGINS "Install plugins into the user plug-in folders after building" OFF)

include(FetchContent)

add_subdirectory(core)
add_subdirectory(tools/csrender)

if(CS_BUILD_PLUGINS)
  # JUCE is dual-licensed GPL/commercial. See README.
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.17-security
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
  add_subdirectory(plugins/chamber)
  add_subdirectory(plugins/mux)
  add_subdirectory(plugins/scope)
endif()

if(CS_BUILD_TESTS)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.9.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
  enable_testing()
  add_subdirectory(tests)
endif()

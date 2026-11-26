# ColliderScope Plugins

Two macOS audio plugins (VST3 + AU) for ColliderScope oscilloscope music, on one shared C++ core.
In XY mode left = X and right = Y, so the sound is the picture.

- **CS Chamber**, a MIDI instrument. Each note births a cloud-chamber track drawn as an XY loop pitched to the note.
- **CS Mux**, an effect. It serializes two XY sources (main input and sidechain) so both pictures show without distorting each other.

Status: **Phase 0, skeleton.** Both plugins build, load and validate; Chamber is silent and Mux passes input A through.

## Layout

```
CMakeLists.txt
core/               cscore: shared DSP core (no JUCE)
plugins/chamber/    CS Chamber
plugins/mux/        CS Mux
plugins/common/     shared look and CMake settings
tools/csrender/     command-line renderer
tests/              unit and acceptance tests (Catch2)
.github/workflows/  build.yml
```

## Build

Needs CMake 3.22+ and a C++20 compiler. JUCE 8 and Catch2 are fetched at configure time.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # macOS: universal arm64 + x86_64, macOS 12+
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `-DCS_BUILD_PLUGINS=OFF` builds only the core, `csrender` and tests (any platform);
`-DCS_COPY_PLUGINS=ON` installs the plugins into `~/Library/Audio/Plug-Ins` after building.

CI (GitHub Actions, macOS 14) builds on every push, runs the tests, validates with pluginval
(strictness 5) and auval, and uploads the zipped plugins as an artifact.

## Install

Builds are unsigned. After copying a plugin into `~/Library/Audio/Plug-Ins/VST3` or
`~/Library/Audio/Plug-Ins/Components`, clear the quarantine flag:

```sh
xattr -dr com.apple.quarantine <plugin path>
```

## Licence note

JUCE is dual-licensed GPL/commercial. GPL is fine for personal or open-source use;
a commercial release needs a JUCE licence.

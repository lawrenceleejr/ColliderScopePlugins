# ColliderScope Plugins

Two macOS audio plugins (VST3 + AU) for ColliderScope oscilloscope music, on one shared C++ core.
In XY mode left = X and right = Y, so the sound is the picture.

- **CS Chamber**, a MIDI instrument. Each note births a cloud-chamber track drawn as an XY loop pitched to the note.
- **CS Mux**, an effect. It serializes two XY sources (main input and sidechain) so both pictures show without distorting each other.

Status: **Phase 1, core and command-line tool.** The shared core draws and plays cloud-chamber tracks offline
through `csrender`; the plugins are still the Phase 0 skeletons (Chamber is silent, Mux passes input A through).

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

CI (GitHub Actions, macOS 15 runner; the macOS 14 image is deprecated) builds on every push, runs the tests, validates with pluginval
(strictness 5) and auval, and uploads the zipped plugins as an artifact.

## csrender

Every DSP feature gets an offline render, a WAV plus a PNG of its XY picture, before it goes into a plugin.

```sh
csrender notes --notes 57:0:1.2:80 --type alpha --at 0.6 --out alpha   # NOTE:START:DUR[:VEL], comma-separated
csrender notes --notes 57:0:1.2:40 --wall drone --root 33 --out drone  # wall hums at A1, track rings as overtone 4
csrender hit kick --out kick                                           # kick, snare or crash
csrender view some.wav --out picture                                   # draw any stereo WAV
```

`--at SECONDS` shows the scope at that moment, with phosphor persistence; without it the PNG collects all
the light of the render. `csrender --help` lists every option. `tools/render_tests.sh` makes the set CI keeps
as the `test-renders` artifact.

## Install

Builds are unsigned. After copying a plugin into `~/Library/Audio/Plug-Ins/VST3` or
`~/Library/Audio/Plug-Ins/Components`, clear the quarantine flag:

```sh
xattr -dr com.apple.quarantine <plugin path>
```

## Licence note

JUCE is dual-licensed GPL/commercial. GPL is fine for personal or open-source use;
a commercial release needs a JUCE licence.

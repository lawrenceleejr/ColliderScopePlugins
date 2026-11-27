# ColliderScope Plugins

Three macOS audio plugins (VST3 + AU) for [ColliderScope](https://cern.ch/colliderscope) oscilloscope music, on one shared C++ core.
By [Larry Lee](https://cern.ch/larry).
In XY mode left = X and right = Y, so the sound is the picture.

- **CS Chamber**, a MIDI instrument. Each note births a cloud-chamber track drawn as an XY loop pitched to the note. Plays chords.
- **ColliderScope**, a MIDI instrument with nine modes on one Instrument menu:
  Bubble (a bubble chamber), Orbital (hydrogen orbital surfaces), Script (text and equations in a LaTeX-like syntax),
  Function (a surface z = f(x, y) from a typed formula), Collision (LHC-style event displays, flat or 3D), Mesh (an STL model),
  Feynman (Feynman diagrams), Scenes (twelve pictures from around particle physics: tracking planes, wire and spark
  chambers, showers, Cherenkov rings, accelerators, the flux tube, the Higgs potential, neutrino oscillation,
  the Eightfold Way) and CMS tracker (a Phase-2 Outer Tracker module, exploded, put in place, then hit by tracks).
  Every mode can also draw its picture as a shaded sheet: a raster of scanlines, lit by dwell time.
- **CS Mux**, an effect. It serializes two XY sources (main input and sidechain) so both pictures show without distorting each other.

All three are built. How to install, wire and play them is in [docs/manual.md](docs/manual.md).

What is checked on every push, by CI on a macOS runner:

- Pitch within 1 cent, pictures within r <= 0.95, hard rests below -80 dBFS within 0.5 ms (CS Chamber and every mode of ColliderScope).
- No distortion in Interleave and Slot, reported latency equal to measured latency in Cycle-lock (CS Mux).
- No allocation on the audio thread (malloc hook), and under 5 % of one core for the audio work.
- pluginval at strictness 5 on the VST3s, auval on the AUs.

Not checked by CI: the plugins in a real DAW and on a real scope.

## Layout

```
CMakeLists.txt
core/               cscore: shared DSP core (no JUCE)
plugins/chamber/    CS Chamber
plugins/mux/        CS Mux
plugins/scope/      ColliderScope
plugins/common/     shared editor, scope display, look and CMake settings
tools/csrender/     command-line renderer
tools/uisnap/       saves a picture of a plugin's window
docs/manual.md      how to install, wire and play
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
csrender mux a.wav b.wav --mode lock --out both                        # two pictures through the CS Mux engine
csrender view some.wav --out picture                                   # draw any stereo WAV
```

`--at SECONDS` shows the scope at that moment, with phosphor persistence; without it the PNG collects all
the light of the render. `csrender --help` lists every option. `tools/render_tests.sh` makes the set CI keeps
as the `test-renders` artifact.

## Licence note

The letters Script draws are the Hershey fonts; their acknowledgement is in
`core/third_party/hershey/README.md`.

For personal use for now: there is no licence file. JUCE is dual-licensed GPL/commercial, so sharing
the plugins means either releasing this code under the GPL or buying a JUCE licence first.

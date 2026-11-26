#!/usr/bin/env bash
# The command-line renders CI keeps as artifacts: a WAV and a PNG per feature.
# usage: tools/render_tests.sh <path to csrender> <output directory>
set -euo pipefail
csrender=$1
out=$2
mkdir -p "$out"

# One note per track type, default Shared wall; the scope 0.6 s into the note.
"$csrender" notes --notes 57:0:1.2:80  --type alpha    --seed 4 --at 0.6 --out "$out/chamber_alpha"
"$csrender" notes --notes 45:0:1.2:120 --type muon     --seed 4 --at 0.6 --out "$out/chamber_muon"
"$csrender" notes --notes 69:0:1.2:40  --type electron --seed 4 --at 0.6 --out "$out/chamber_electron"
# The track alone, young and old: diffusion is the hiss.
"$csrender" notes --notes 57:0:1.8:80 --type alpha --wall off --seed 4 --at 0.15 --out "$out/chamber_alpha_off_young"
"$csrender" notes --notes 57:0:1.8:80 --type alpha --wall off --seed 4 --at 1.7  --out "$out/chamber_alpha_off_old"
# Drone: the wall hums at A1, the track rings as overtone 4 (A3).
"$csrender" notes --notes 57:0:1.2:40 --type electron --wall drone --root 33 --seed 4 --at 0.6 --out "$out/chamber_drone_h4"
# A short melody with hard rests, velocity picking the type.
"$csrender" notes --notes 57:0:0.4:80,60:0.5:0.4:40,64:1.0:0.4:120,69:1.5:0.8:80 --seed 7 --out "$out/chamber_melody"
# Drum hits, each in its own slot.
"$csrender" hit kick  --out "$out/hit_kick"
"$csrender" hit snare --out "$out/hit_snare"
"$csrender" hit crash --out "$out/hit_crash"
# Round trip: a picture drawn from a WAV on disk.
"$csrender" view "$out/chamber_alpha.wav" --at 0.6 --out "$out/view_roundtrip"
ls -la "$out"

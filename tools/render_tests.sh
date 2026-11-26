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
# Polyphony: A2, A3, E4 are overtones 1, 2, 3 of A2; then a close triad over the A1 drone.
"$csrender" notes --voices 4 --notes 45:0:1.2:120,57:0:1.2:80,64:0:1.2:40 --wall off --seed 4 --at 0.6 --out "$out/chamber_poly_overtones"
"$csrender" notes --voices 4 --notes 57:0:1.2:80,61:0:1.2:80,64:0:1.2:40 --wall drone --seed 4 --at 0.6 --out "$out/chamber_poly_drone_triad"
# CS Bubble: one event of each kind, 0.9 s into the note, and a pair when it is young (dots).
for event in electron pair decay beam; do
  "$csrender" notes --instrument bubble --type "$event" --notes 57:0:1.2:80 --seed 5 --at 0.9 --out "$out/bubble_$event"
done
"$csrender" notes --instrument bubble --type pair --notes 57:0:0.3:80 --seed 5 --at 0.12 --out "$out/bubble_pair_young"
# CS Orbital: one orbital of each kind.
for orbital in 1s 2s 2pz 2px 3dz2 3dxz 4fz3 4fxz2; do
  "$csrender" notes --instrument orbital --orbital "$orbital" --notes 57:0:1.2:80 --at 0.6 --out "$out/orbital_$orbital"
done
# Drum hits, each in its own slot.
"$csrender" hit kick  --out "$out/hit_kick"
"$csrender" hit snare --out "$out/hit_snare"
"$csrender" hit crash --out "$out/hit_crash"
# CS Mux: a circle at 55 Hz and a square at 220 Hz share the beam.
"$csrender" shape circle --hz 55  --size 0.8 --dur 2 --out "$out/in_circle"
"$csrender" shape square --hz 220 --size 0.3 --dur 2 --out "$out/in_square"
"$csrender" mux "$out/in_circle.wav" "$out/in_square.wav" --mode interleave --at 1.5 --out "$out/mux_interleave"
"$csrender" mux "$out/in_circle.wav" "$out/in_square.wav" --mode interleave --switch closest --at 1.5 --out "$out/mux_interleave_closest"
"$csrender" mux "$out/in_circle.wav" "$out/in_square.wav" --mode lock --at 1.5 --out "$out/mux_cycle_lock"
# Slot: the Chamber melody keeps the beam; kick, snare and crash each take a slot.
"$csrender" mux "$out/chamber_melody.wav" "$out/in_square.wav" --mode slot --sensitivity 0 \
  --hits kick:0.0,snare:0.25,kick:0.5,snare:0.75,kick:1.0,crash:1.5 --out "$out/mux_slot_drums"
"$csrender" mux "$out/chamber_melody.wav" "$out/in_square.wav" --mode slot --sensitivity 0 --hits kick:1.0 --at 1.05 --out "$out/mux_slot_kick"
# Round trip: a picture drawn from a WAV on disk.
"$csrender" view "$out/chamber_alpha.wav" --at 0.6 --out "$out/view_roundtrip"
ls -la "$out"

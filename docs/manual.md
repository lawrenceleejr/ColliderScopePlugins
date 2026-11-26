# ColliderScope Plugins: manual

Two plugins for oscilloscope music. In XY mode the left channel is X and the right channel is Y,
so the sound is the picture: every setting below changes both.

## Install

Download `ColliderScope-macOS-universal` from the latest green run under the repository's Actions tab
and unzip it.

| Plugin file | Goes in |
| --- | --- |
| `CS Chamber.vst3`, `CS Mux.vst3` | `~/Library/Audio/Plug-Ins/VST3` |
| `CS Chamber.component`, `CS Mux.component` | `~/Library/Audio/Plug-Ins/Components` |

The builds are unsigned, so clear the quarantine flag once after copying, then rescan in your DAW:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/CS\ Chamber.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/CS\ Mux.vst3
```

Universal binary (Apple Silicon and Intel), macOS 12 or later.

## Wiring the scope

- Left output to the scope's X input, right output to Y, scope in XY mode.
- Keep the track at unity gain with no limiter, compressor or stereo effects after the plugin:
  anything that reshapes the waveform reshapes the picture.
- **DC.** A picture that sits off-centre carries DC. A DC-coupled interface holds it in place; an
  AC-coupled one lets it drift back to the centre over a few seconds. CS Chamber's shapes are centred.
  CS Mux's offsets move pictures off-centre and rely on DC coupling to stay there.
- **Silence parks the beam** at the centre, where it shows as a dot. Turn the intensity down between
  pieces: a parked beam can mark the phosphor.

## CS Chamber (instrument)

A monophonic instrument. Each note births a cloud-chamber track with a random shape and draws it once
per period, so the picture repeats at the note's pitch. Last note wins; releasing it falls back to a
key that is still held.

| Group | Parameter | What it does |
| --- | --- | --- |
| Track | Type | Auto picks by velocity: up to 50 an electron (a curl), 51 to 100 an alpha (a short thick track), above 100 a muon (a line across the chamber). Or fix the type, or Random. |
| | Seed mode | Random: a new shape every note. Per key: the same key always draws the same track. Global: one track for everything. |
| | Diffusion rate | How fast the track blurs. This is the hiss that grows while a note is held. |
| | Drift | How fast the track sinks, as vapour does. |
| | Condense time | How long the track takes to appear along its length at the start of a note. |
| Wall | Wall mode | Off: the track alone. Shared: wall and track drawn together each period, both at the note's pitch. Drone: the wall owns the period at a fixed root note and the track rings as one of its overtones; notes snap to the nearest overtone. |
| | Wall root | The Drone's root note. A1 (55 Hz) by default. |
| | Wall share p | Drone: the fraction of each period spent on the wall. Auto keeps it nearest 0.45. |
| Voice | Size | Size of the picture, which is also the volume. |
| | Attack, Release | Grow and shrink times. Release 0 is a hard rest: silent within half a millisecond. |
| | Glide | Slide time between overlapping notes. |
| | Bend range | Pitch-wheel range in semitones. In Drone mode the wheel bends the root. |
| Output | Quality | Scope-exact: no filtering, the truest picture. Clean: 4x oversampled with a gentle low-pass, less aliasing in the sound, corners very slightly rounded, 4 samples of latency. |
| | Output gain | Trim. |
| | Persistence | How long the built-in display glows. Does not affect the sound. |

Presets: Alpha lead, Muon bass, Electron hiss, Drone chamber (also available as the host's programs).

A track lives about two seconds (alpha 2.0 s, muon 2.4 s, electron 1.8 s). With the wall on, the wall
carries on alone after that. With the wall off, a note held longer goes silent.

## CS Mux (effect)

Shows two pictures on one scope. Adding two XY signals shears both pictures; CS Mux gives each the
whole beam in turn. A is the track the plugin sits on; B arrives on the sidechain.

| Mode | How it works | Latency | Cost |
| --- | --- | --- | --- |
| Interleave | A for a slice, then B for a slice. | 0 | Each picture half as bright; a switching tone at sample rate / (2 x slice); faint jump lines. |
| Cycle-lock | Follows each input's pitch and holds its latest whole cycle. Each period plays A's cycle, then B's cycle several times, faster. | One period of A, reported to the host | B's pitch snaps to an overtone of A. Needs pitched inputs; uses Interleave until both have a pitch. |
| Slot | A plays. A trigger gives B, or a built-in drum hit, the whole beam for a moment. | 0 | A is cut during each slot, like a hard sidechain. Meant for drums. |

| Group | Parameter | What it does |
| --- | --- | --- |
| Mux | Slice | Interleave: samples per slice, 8 to 4096. Auto uses one period of A when A has a pitch, otherwise 512. |
| | Balance | Trades time, and so brightness, between A and B. |
| | Switch point | Fixed, or Closest: each switch moves, within 10 % of the slice, to where the two traces are near each other, so jump lines are shorter. |
| | Jump weight | How long the beam takes to travel between pictures, relative to drawing the same distance. Lower is fainter jump lines and a sharper switching sound. |
| Slot | Slot length | Longest slot for B, 20 to 300 ms. A slot ends early once B falls below 12 % of its peak. |
| | B trigger | How easily a transient on B opens a slot. 0 turns it off, leaving MIDI only. |
| Place A, Place B | Offset, scale, rotation | Put the pictures side by side or one inside the other. |
| Output | Z-out level | Level of the blanking pulse on the Z output during jumps. Negative flips its polarity. |
| | Persistence | How long the built-in display glows. |

**MIDI in Slot mode.** Note 36 (C1) plays the built-in kick, a spiral collapsing from A's outer edge.
38 (D1) is the snare and 49 (C#2) the crash, both rings of sparks. Any other note opens a slot for B.

**Z output.** The second output pair carries a pulse whenever the beam is jumping between pictures.
Send it from a DC-coupled interface output to the scope's Z (intensity) input to hide the jump lines.
Scopes differ in polarity and level: start with Z-out level at 0 and move it away from zero, in
whichever direction dims the jumps. Leave the pair unconnected if you do not use it.

### Routing in Ableton Live

Use the VST3 versions: Live sends MIDI to VST3 effects but not to Audio Unit effects.

1. Put picture A on an audio or instrument track and add CS Mux after it.
2. Open the Sidechain section of CS Mux's device title bar and choose the track that carries picture B.
   Mute B's own output, or set its Audio To to "Sends Only", so B reaches the scope only through CS Mux.
3. For drum slots: make a MIDI track, set its MIDI To to the track with CS Mux and, in the chooser
   below it, to CS Mux. Notes on that track now trigger slots.
4. For Z: on a new audio track set Audio From to CS Mux's track and pick its second output pair, then
   send that track to the interface output wired to the scope's Z input.

## Known limits

- Played live, a note starts when its first picture arrives from a background thread, up to a
  millisecond or two after the key. Offline bounces are sample-exact.
- Cycle-lock changes its reported latency when A changes pitch. Some hosts pause briefly to re-plan.
- The pitch tracker listens to (left + right) / 2. A picture whose X and Y cancel there, such as a
  line along x = -y, cannot be tracked.
- The builds are unsigned and for personal use. JUCE is dual-licensed GPL/commercial: sharing the
  plugins means choosing one of those first.

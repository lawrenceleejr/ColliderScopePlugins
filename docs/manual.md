# ColliderScope Plugins: manual

Five plugins for oscilloscope music: four instruments (CS Chamber, CS Bubble, CS Orbital, CS Script)
and one effect (CS Mux). In XY mode the left channel is X and the right channel is Y, so the sound is the
picture: every setting below changes both.

## Install

Download `ColliderScope-macOS-universal` from the latest green run under the repository's Actions tab
and unzip it.

| Plugin file | Goes in |
| --- | --- |
| the five `.vst3` bundles | `~/Library/Audio/Plug-Ins/VST3` |
| the five `.component` bundles | `~/Library/Audio/Plug-Ins/Components` |

The builds are unsigned, so clear the quarantine flag once after copying, then rescan in your DAW:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/CS\ *.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/CS\ *.component
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

Each note births a cloud-chamber track with a random shape and draws it once per period, so the picture
repeats at the note's pitch. With Voices at Mono the last note wins, and releasing it falls back to a
key that is still held. With more voices it plays chords: see Polyphony below.

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
| Voice | Voices | Mono, or up to 6 keys at once sharing the beam. |
| | Size | Size of the picture, which is also the volume. |
| | Attack, Release | Grow and shrink times. Release 0 is a hard rest: silent within half a millisecond. |
| | Glide | Slide time between overlapping notes. Mono only. |
| | Bend range | Pitch-wheel range in semitones. In Drone mode the wheel bends the root. |
| Output | Timing | How the period is shared out along the path: see Timing below. |
| | Quality | Scope-exact: no filtering, the truest picture. Clean: 4x oversampled with a gentle low-pass, less aliasing in the sound, 4 samples of latency. Corners round very slightly and the beam overshoots a little where it jumps. |
| | Output gain | Trim. |
| | Persistence | How long the built-in display glows. Does not affect the sound. |

Presets: Alpha lead, Muon bass, Electron hiss, Drone chamber, Chamber choir (also available as the
host's programs).

### Polyphony

There is one beam, so a chord cannot be several pictures at once. Instead each period is divided into
slots, one per key, and each key's track is drawn in its slot. The whole picture still repeats at one
rate, the chord's fundamental, and every note is snapped to the nearest overtone of it.

- **Chords voiced on the overtones of their bass note** (A2, A3, E4, A4, C#5 ...) repeat at the bass
  note and stay in tune with it.
- **Any other chord** gets a fundamental below the bass: a close major triad on A3 is the 4th, 5th and
  6th overtones of A1. You hear that low note, and the thirds are just rather than equal-tempered
  (a major third lands 14 cents flat of the keyboard's).
- **Dense or dissonant chords** need a very low fundamental, which flickers, or snap further: in the
  worst case by up to a quarter tone.
- Each added voice gets a smaller share of the period, so its track is dimmer and quieter.
- The envelope is shared: lifting one key of a chord removes its track at once; Release applies when
  the last key is lifted.
- With the wall on, the wall takes a share of every period too. In Drone mode the fundamental is the
  wall's root, or a whole fraction of it when the chord needs more room.

A track lives about two seconds (alpha 2.0 s, muon 2.4 s, electron 1.8 s). With the wall on, the wall
carries on alone after that. With the wall off, a note held longer goes silent.

### Timing

All four instruments have this control.

- **Even** spreads the period along the path by length, so every line is equally bright. It takes
  no account of the sound.
- **Harmonic** (the default) solves for the drawing with the sound in mind. The period is cut into
  equal slots, 2, 3, 4, 6, 8, 12, 16 ... of them, and each stroke gets a whole number, so the rhythm
  of the strokes falls on octaves and fifths of the note. Then the order and direction of the strokes
  are rearranged, as long as the jumps between them do not grow much, to move energy out of the
  harmonics that clash with the scale (the 7th, 11th, 13th ...) and into those that agree with it.
  It solves once at the start of a note and then holds, so the tone is steady.

What it buys depends on how much there is to rearrange. Measured as the share of the sound's energy
in consonant harmonics: CS Orbital, with many wires, goes from 0.85 to 0.89 and improves on four notes
in five; CS Bubble and CS Chamber barely change, because a row of bubbles or a single track leaves
little choice. CS Script keeps its strokes in reading order and only chooses the grid. The price is brightness: a stroke rounded up to a whole slot is drawn slower, and so
brighter, than one rounded down.

## CS Bubble (instrument)

A bubble chamber. Each note is an event: a charged particle crosses a magnetic field, losing energy as
it goes, so its track curls into a spiral. The track is a row of bubbles. The beam dwells on a young
bubble, a bright dot, and circles an older one, so a note starts as a dotted line with a stepped,
buzzy tone and fizzes more as the bubbles swell. Monophonic, with the same voice controls as CS Chamber.

| Group | Parameter | What it does |
| --- | --- | --- |
| Event | Event | Auto picks by velocity: up to 40 an electron (one spiral), 41 to 80 a pair (an electron and a positron leaving one point and curling opposite ways), 81 to 110 a decay (a track that kinks and curls away tighter), above 110 a beam particle (nearly straight). Or fix the event, or Random. |
| | Seed mode | Random: a new event every note. Per key: the same key always draws the same event. Global: one event for everything. |
| | Field | Strength of the magnetic field: higher curls the tracks tighter. It can be moved while a note sounds. |
| | Form time | How long the track takes to appear along its length. |
| Bubbles | Bubbles | How many bubbles the picture has. Fewer is a coarser, buzzier tone. |
| | Bubble growth | How fast the bubbles swell while the note is held. |
| | Bubble size | The size at which they stop. |
| | Boil | How much the bubbles shiver: the hiss. |

Presets: Spiral bass, Pair pluck, Fizz. A track lasts as long as the key is held.

## CS Orbital (instrument)

Surfaces. Each note draws a hydrogen orbital as a wireframe of its isosurface, the surface on which
the wavefunction has one size, spinning about the orbital's axis. Each radial lobe of the orbital is
one shell: 1s is a sphere, 2s a sphere within a sphere, 2p two lobes, 3d lobes and a ring or a
four-leaf clover, 4f more lobes still. Monophonic, with the same voice controls as CS Chamber.

| Group | Parameter | What it does |
| --- | --- | --- |
| Surface | Orbital | Auto picks by velocity: up to 40 an s orbital, 41 to 80 p, 81 to 110 d, above 110 f, and the key turns it (which m). Or choose one of the twenty from 1s to 4f. |
| | Iso level | Where the surface sits, as a fraction of the lobe's peak. Lower is a fatter surface that reaches closer to the nodes. |
| | Wires | How many wires are drawn around each shell. More is a denser picture and a brighter tone. |
| | Bloom time | The surface grows out of the lobe's core over this time at the start of a note. |
| View | Spin | Turning rate about the orbital's axis. Negative turns the other way. |
| | Tilt | How far the axis leans toward you. At 0 you look from the side, at 90 down the axis. |

Presets: s sine, p lead, d pad, f bell. The wireframe is see-through: wires on the far side show too.

## CS Script (instrument)

Text and equations. Type a line into the Text field and each note draws it with single-stroke
letters, fitted to the screen and drawn in reading order. Monophonic, with the same voice controls as
CS Chamber. The text is saved with the project; it is not a parameter, so it cannot be automated.

The Text field takes a small part of LaTeX's syntax:

| To get | Type |
| --- | --- |
| Superscripts and subscripts | `x^2`, `a_i`, `e^{i\pi}`, `\int_0^\infty` |
| Fractions and roots | `\frac{a}{b}`, `\sqrt{x^2 + y^2}` |
| Greek letters | `\alpha` ... `\omega`, and `\Gamma \Delta \Theta \Lambda \Xi \Pi \Sigma \Phi \Psi \Omega` |
| Symbols | `\pm \mp \times \cdot \div \leq \geq \neq \equiv \approx \propto \infty \partial \nabla \int \sum \prod \to \leftarrow \uparrow \downarrow \in \subset \supset \cup \cap \exists \perp \parallel \angle \therefore \hbar` |
| Accents | `\vec{a}`, `\hat{H}`, `\bar{x}`, `\dot{q}`, `\tilde{u}` |
| Spaces and a new line | `\,`, `\;`, `\quad`, and `\\` |

Braces group, `$` signs are ignored, and a command it does not know is drawn as its name, so a typo
shows. It is not a full LaTeX: there are no matrices, no `\left(` ... `\right)` sizing (the brackets
are drawn at normal size), no big operators with limits above and below, and one font.

| Group | Parameter | What it does |
| --- | --- | --- |
| Writing | Write time | At the start of each note the line is written out from left to right over this time. 0 shows it all at once. |
| | Slant | Leans the letters. 0.2 is about italic. |

A line of text is many short strokes with jumps between them, so the faint jump lines show more than
on the other instruments, and long lines are small. Short texts and single equations read best.
The letters are the Hershey fonts (see `core/third_party/hershey/README.md`).

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
| Mux | Inputs | A + B, or one input alone. Alone, the picture keeps the whole beam, and placement, the output controls and (in Slot mode) the built-in drum hits still apply. With nothing on the sidechain, CS Mux behaves as A only. |
| | Slice | Interleave: samples per slice, 8 to 4096. Auto uses one period of A when A has a pitch, otherwise 512. |
| | Balance | Trades time, and so brightness, between A and B. |
| | Switch point | Fixed, or Closest: each switch moves, within 10 % of the slice, to where the two traces are near each other, so jump lines are shorter. |
| | Jump weight | How long the beam takes to travel between pictures, relative to drawing the same distance. Lower is fainter jump lines and a sharper switching sound. |
| Slot | Slot length | Longest slot for B, 20 to 300 ms. A slot ends early once B falls below 12 % of its peak. |
| | B trigger | How easily a transient on B opens a slot. 0 turns it off, leaving MIDI only. |
| Place A, Place B | Offset, scale, rotation | Put the pictures side by side or one inside the other. |
| Output | X scale, X offset, Y scale, Y offset | Overall size and position of each output channel, applied last. A negative scale mirrors the picture. Offsets need DC-coupled outputs to hold. |
| | Z-out level | Level of the blanking pulse on the Z output during jumps. Negative flips its polarity. |
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

- The built-in display gathers light 60 times a second and redraws 30 times a second.

- CS Bubble, CS Orbital and CS Script are monophonic.
- Played live, a note starts when its first picture arrives from a background thread, up to a
  millisecond or two after the key. Offline bounces are sample-exact.
- Cycle-lock changes its reported latency when A changes pitch. Some hosts pause briefly to re-plan.
- The pitch tracker listens to (left + right) / 2. A picture whose X and Y cancel there, such as a
  line along x = -y, cannot be tracked.
- The builds are unsigned and for personal use. JUCE is dual-licensed GPL/commercial: sharing the
  plugins means choosing one of those first.

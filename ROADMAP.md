# Roadmap

EmissionControl2-Next is a fork of [EmissionControl2](https://github.com/EmissionControl2/EmissionControl2)
that adds DAW plugin builds. This file tracks what is done, what is next, and what is deliberately out of
scope. It is a working document, not a promise of dates.

## Shipped

- **Plugin targets**: VST3, CLAP and a standalone app, built from one CMake project on Windows.
- **Engine**: AlloLib's graphics layer compiled out with `AL_NO_GRAPHICS`, so the plugin links without a
  second OpenGL loader.
- **Parameters**: all 71 of EC2's controls, including 15 modulation depths, 15 LFO routings and the six
  LFOs' shape, polarity, frequency and duty.
- **Editor**: EC2 1.2's panel structure and palette, with editable value ranges, preset grid, scan display,
  active grain history, oscilloscope and VU. Three themes, Studio by default.
- **MIDI**: notes start grain clouds with per-note pitch; CC 20 to 34 drive the fifteen parameters.
  Free Run holds a cloud without MIDI.
- **Presets**: host state chunks, plus `.ec2p` files and a 36 slot bank.
- **Samples**: a bank with load, add and clear, selected by the Sound File parameter.
- **OSC**: parameter, modulation and LFO addresses on 127.0.0.1:16447.
- **Resizing**: free in both axes from the bottom-right handle, with a 1100x660 floor and busy state
  remembered between sessions.
- **Reproducibility**: VST3 SDK, CLAP and CLAP helpers as pinned submodules; local patches exported to
  `patches/` and applied by `tools/bootstrap.ps1`.

## Next

### 1. MIDI Learn, Unlearn and Logarithmic

Each range slider gets a right-click menu with MIDI Learn, Unlearn and Logarithmic. This needs a CC map in
the plugin, learn state per parameter, and a logarithmic transfer in the slider's normalized mapping. It is
the largest remaining gap you can feel in normal use.

### 2. Note gating and grain rate

Today the engine is either free running, where Free Run holds one note for you, or note gated, where each MIDI
note starts a cloud and the cloud emits grains at the global Grain Rate. With Free Run off and the default
rate of 1 Hz, a held note produces roughly one grain per second, so the instrument appears to do nothing, and
a short note can pass without producing a single grain. This is the behavior to settle before anything else
in the list, because it decides whether the plugin feels alive in a host.

Decisions to make:

- **Density while a note is held.** Either Grain Rate stays global and every voice shares it, which is what
  the desktop app does, or it scales per voice so polyphony has independent density. Global is faithful; per
  voice is more musical when two notes are held.
- **Note lifetime.** Note off currently ends the cloud. It needs a release envelope and a minimum cloud
  length, so a staccato note still produces grains instead of nothing.
- **Streams.** A voice spawning several streams multiplies density. Decide whether Streams is per note or a
  global multiplier before adding per-voice rate.
- **A sensible first note.** A rate floor, or a higher default for the gated path, so the first key a user
  presses produces sound rather than a wait.
- **Feedback.** Show the grain count and the active voices where the user can see them, so a quiet result is
  explainable instead of looking broken.

Acceptance: with Free Run off, pressing a key produces audible grains immediately; releasing it ends them
cleanly without clicks; Grain Rate changes density while the note is held; Free Run uses the same code path
with an internal note rather than a separate branch.

### 3. Preset Map

The control is drawn and inert today, deliberately, rather than pretending to work. Implementing it means
deciding what a map is: a stored set of 36 slot assignments, or a morph field across the grid.

### 4. Draw-time geometry

Controls capture their rectangle when the layout is built, so a resize has to rebuild roughly ninety
controls. Having each control compute its rectangle from the current canvas size at draw time removes the
rebuild, the churn while dragging, and the fragility that has cost several rounds of fixes on the resize
path. This is the correct design for a vector editor and should be done deliberately, not patched in.

### 5. The host crash

The plugin crashed a DAW after a few minutes of playback. One UI-thread read of the engine was removed as a
candidate, and the engine measures flat over 64 seconds of playback, so the cause is not confirmed. Needs a
reproduction with the time to failure and whether a sample had been loaded.

### 6. Per-panel proportions

The window resizes freely; the panel splits are fixed fractions. Dragging the splits and storing the ratios
would complete the layout work.

### 7. HiDPI

The canvas currently uses device pixels directly, so the interface is correct in layout but smaller than a
DPI-aware application on a scaled display. Restoring crisp text means passing the platform scale as the
canvas scale while also scaling the canvas, the exact combination that previously caused a resize feedback
loop. Needs a host test at 125% and 150%.

### 8. Platform coverage

AU and AUv3 need macOS. AAX needs Avid's SDK and PACE signing. The CMake already lists what iPlug2 supports;
neither can be tested on this machine.

### 9. Sample file handling

Drag and drop onto the editor, and whatever further actions the desktop app's Sound Files menu carries that
the plugin does not yet match.

## Out of scope

- **Recording**: the host records. EC2's recorder has no clean plugin equivalent.
- **Menu bar and help text**: host conventions replace these.
- **Preset morphing DSP**: EC2 interpolates between presets over time. The control is present and disabled
  until the engine supports it, rather than moving values instantly and claiming otherwise.

## Known limitations

- The oscilloscope and scan displays read snapshots published by the audio thread; they are diagnostic, not
  sample-accurate.
- `ecSource` is upstream code and is not modified. Behavior differences from the desktop app are handled in
  `src/engine` and `src/plugin`.
- Windows is the only platform built and tested so far.

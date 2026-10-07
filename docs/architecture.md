# Architecture

## Signal path

```text
physical MIDI input
  -> platform MIDI backend
  -> immutable per-note velocity lookup tables
  -> per-key manual adjustment
  -> global velocity curve
  -> existing MIDI output (Windows) or application-owned virtual output (macOS/Linux)
  -> DAW or instrument
```

Normal playing performs no statistical work. A Note On lookup is an indexed read
from `mapping[note][velocity]`; other MIDI messages pass through unchanged.

## Calibration path

```text
Note On stream
  -> repeated-note range inference
  -> press collector
  -> press quality checks
  -> robust per-press reference
  -> per-note observations (raw velocity, reference velocity)
  -> robust bins and interpolation
  -> deadband and confidence shrinkage
  -> monotonic lookup table
  -> before/after validation
```

White and black keys are separate sessions. Each session stores raw events as
well as acceptance decisions so later algorithm versions can regenerate curves.

### Short calibration bars

Each press carries a segment identifier, and each segment has its own expected
note range. Neighbouring segments overlap by at least two keys. For every shared
key, VelCal compares its response relative to each segment's local median in low,
medium, and high velocity regions. These ratios align the segment references
before per-key curves are fitted.

The overlap graph must be connected. A disconnected section has no physical-force
reference in common with the rest of the keyboard, so a whole-section sensitivity
difference is mathematically indistinguishable from a harder or softer press.

## Boundaries

- `velcal_core`: calibration types, analysis, lookup generation, validation.
- `MidiEngine`: JUCE input lifecycle, capture buffering, and real-time routing.
- `velcal_app`: JUCE desktop capture, profile, inspection, and routing workflow.
- `velcal_plugin`: JUCE VST3 MIDI-processing instrument using the same calibration/editing UI.
- `PluginState`: per-instance profile snapshots and self-contained DAW state.

The core must not expose JUCE types. This keeps tests fast and prevents UI or
device-lifecycle concerns from leaking into the calibration algorithm.

## VST3 Host Path

Installed releases also build a separate AU MIDI-effect target on macOS. It uses
the same processor/state/editor with no audio buses and runs in Logic's MIDI FX
slot before the instrument. VST3 retains its silent instrument presentation.
`DataPaths.hpp` selects writable per-user storage; project state remains
independent per plugin instance. Installers never bundle personal profiles.

The host supplies MIDI blocks directly to `MidiEngine::processHostMidi`.
The plugin declares an instrument category with no audio input and a default
stereo audio output (mono also supported). Every processed or bypassed audio
block is cleared to silence. Corrected MIDI goes to the host's MIDI output bus;
the user routes it to the target instrument. Bypass preserves original MIDI.
This follows VelPro's documented instrument/silent-audio approach to host
compatibility. It does not put the target instrument inside VelCal.
The plugin changes only nonzero Note On velocity bytes, preserving event
ordering, channels, and sample offsets. Other messages remain unchanged in
the JUCE MIDI buffer; delivery to instruments also depends on the VST3 wrapper
and host's supported event types. It opens no physical or virtual MIDI devices.

The audio callback tries to copy a published lookup bank without waiting for
UI edits. Raw capture notes enter a preallocated single-producer/single-consumer
FIFO, with sample-based timestamps; the UI drains and analyses them. Overflow
stops capture and reports that the section needs restarting. Calibration fitting,
JSON, file access, and dialogs run outside the audio callback.

The processor owns the MIDI engine and published profile for its full lifetime.
Editors can close and reopen without dropping maps, unsaved edits, or ongoing
capture. Host state includes the full profile, file association, dirty flag, and
key group and selected curve tab, but excludes unfinished captures and MIDI device preferences.
Revision checks prevent a stale editor publishing over a newly restored project.
Shared `effectiveMaps` composes automatic/manual per-key maps, trim, and global
curve for both standalone and plugin. Profile schema remains 4.

## Profile And Preset Storage

Profile Save/Save As use the existing atomic profile writer. A Save-and-continue
action proceeds only after a successful write; cancellation, failure or a newer
editor/host revision cancels that continuation. Reset operations modify only
the working profile until Save. Clear Calibration preserves the global curve;
Reset All preserves identity and saved presets while resetting settings.

`CurvePresetLibrary.hpp` stores named global curves separately in
`.velcal-curve-presets.json`, beneath the shared writable profile directory.
Core preset serialization reuses the profile curve parser and validation, with
a separate version-1 library envelope; profile schema 4 is unchanged.
Library edits acquire a bounded inter-process lock, reread the latest file,
validate names/curves and replace it through a JUCE temporary file. Invalid
existing data is reported rather than overwritten. Editors poll for library
changes alongside appearance preferences. Library changes never republish
profile or host state: applying a preset copies its settings into the profile.
Legacy profile-local presets remain embedded and can be copied into the library
through Save Preset. The shared library itself is not embedded in DAW state.

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
- `velcal_plugin`: optional later VST3/AU/LV2 targets.

The core must not expose JUCE types. This keeps tests fast and prevents UI or
device-lifecycle concerns from leaking into the calibration algorithm.

# VelCal

VelCal is an open-source, cross-platform MIDI velocity calibration tool. It is
intended to measure persistent response differences between the individual keys
of a MIDI keyboard and generate a monotonic correction mapping for each note.

The repository now contains the calibration engine, a JUCE desktop application,
live short-section capture, profile persistence, and real-time MIDI routing.

## Current scope

- Group near-simultaneous Note On events into calibration presses.
- Reject incomplete presses, duplicate notes, and events from the wrong key group.
- Use a robust per-press median as the keyboard reference velocity.
- Learn a separate nonlinear velocity mapping for each MIDI note.
- Suppress isolated outliers and corrections inside a configurable deadband.
- Produce a constant-time 128-entry lookup table for real-time processing.
- Measure dynamic-range coverage and before/after consistency.
- Join shorter-bar calibration sections through overlapping reference keys.
- Infer the keys beneath a short bar from repeated strikes while retaining stray
  notes as rejected measurements.
- Apply profile mappings to live MIDI and pass non-Note-On messages through.

## Short-bar calibration

The keyboard may be measured in sections when the calibration bar cannot span
the full keybed. Adjacent sections must overlap by at least two same-colour keys;
three or four is preferable. Each section still needs soft, medium, and hard
presses. Shared keys let VelCal estimate the relative response of neighbouring
sections without knowing the physical force used for either set of presses.

Sections with no overlap are reported as disconnected and cannot provide a
trustworthy keyboard-wide calibration. White-key and black-key section chains
remain separate calibration sessions.

## Build

All project dependencies and generated files must remain on the `D:` drive. Do
not install project tooling or dependencies system-wide on `C:`. The repository's
`AGENTS.md` records the storage policy for future development sessions.

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Build outputs, downloaded dependencies, temporary files, local profiles/app
preferences, captures, and third-party reference data are excluded from Git.
Profiles and captures are created locally during use; personal calibration data
is not bundled with the source repository.

## Physical capture prototype (Windows)

After building, run `build/Debug/velcal_capture.exe`. The console utility lists
MIDI inputs, learns the keys under a short bar, records repeated presses, and can
continue through overlapping sections. It writes accepted and rejected raw events
to the project's `captures/` directory as CSV, even when launched by double-click.
This is an instrumentation tool for tuning the provisional
algorithm, not the finished application or real-time MIDI processor.

Saved captures can be reprocessed with the latest algorithm without replaying the
keyboard:

```powershell
.\build\Debug\velcal_analyze.exe .\captures\your-capture.csv
```

The capture tool also writes a versioned `.velcal.json` profile to `profiles/`.
It contains the original measurements, quality decisions, algorithm settings,
section alignment, confidence statistics, and all 128 generated lookup tables.

## Desktop application

Run `build/velcal_app_artefacts/Debug/VelCal.exe` after building. To add a
calibration section:

1. Select the physical MIDI input and either white or black keys.
2. Select **Start section**, then press the same group of keys at least three
   times. The capture guide targets eight accepted soft, medium, and firm presses
   and reports which strength is still needed while recording.
3. Select **Finish section**. VelCal infers the short bar's note range, rejects
   incomplete or contaminated presses, and rebuilds the profile immediately.
4. Move the bar with at least two same-colour keys overlapping the previous
   section and repeat. Select **Save profile** when finished.

For live use, select an output and enable **Route MIDI**. macOS and Linux JUCE
backends can create an application-owned virtual endpoint. The current Windows
build can route to an already installed virtual MIDI output; its built-in
`VelCal Output` option requires Windows MIDI Services support that is not enabled
in this build.

Live routing uses a bounded output queue and stops automatically if it detects an
abnormal message rate or a backed-up MIDI output. Driver sends happen away from
the hardware input callback so a slow endpoint cannot hold up incoming MIDI.

The **Per-key calibration** tab contains section capture, the keyboard response
display, and a manual adjustment for the selected key. The **Global curve** tab
applies a final keyboard-wide response curve after per-key correction. It includes
linear, soft, firm, compressed, and wide-dynamics defaults and can save custom
presets inside the profile.

Both response graphs are directly editable. Select or add a control point with
the left mouse button, drag it to reshape the response, and remove an interior
point with the right mouse button. Endpoints remain anchored to MIDI inputs 1 and
127, and output values are constrained to remain monotonic. The **Smooth** toggle
switches between monotonic cubic interpolation and straight line segments. MIDI
output remains quantized to the 127 values defined by the protocol even though
the editor displays the continuous response envelope.

## Platform direction

The calibration and profile model remain independent of JUCE. The desktop layer
uses JUCE for its UI and MIDI backends:

- CoreMIDI virtual endpoints on macOS.
- ALSA sequencer ports on Linux.
- Windows MIDI Services on supported Windows 11 releases.
- Selection of an existing third-party virtual cable as a legacy Windows fallback.

Original measurements are retained in versioned VelCal profiles so correction
curves can be regenerated when the algorithm improves.

## Licence

AGPL-3.0-only. See [LICENSE](LICENSE).

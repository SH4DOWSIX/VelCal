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

Use a rigid object with a straight, smooth edge, such as a straight piece of wood
or a spirit level, as the calibration bar. Place its edge across a group of
same-colour keys and press them down together, then fully release them between
presses. Measure white and black keys separately.

Push down near the middle of the bar to distribute pressure as evenly as possible
across all the keys beneath it. Pressing harder on one side can make those keys
appear more responsive and produce an inaccurate calibration.

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

The tracked icon source is `resources/app-icon.png`; it is embedded into the
executable during the build.

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Build outputs, downloaded dependencies, temporary files, local profiles/app
preferences, captures, and third-party reference data are excluded from Git.
Profiles and captures are created locally during use; personal calibration data
is not bundled with the source repository.

### Local Windows portable Release

Double-click `build-portable.bat` in the repository root. Requires CMake and Git
on PATH, plus Visual Studio C++ build tools with a Windows SDK already installed.
The script builds x64 Release, runs the core tests, and creates a fresh portable
folder and ZIP under `build/portable/`, containing the EXE, licence, and an empty
profiles directory, plus dependency licence notices. The repository README is not bundled. For a terminal run
without the final pause, use `build-portable.bat --no-pause`.

Extract the ZIP into a writable folder and run `VelCal.exe`. Profiles and app
preferences are stored in `profiles/` beside the executable. Profiles saved there
remain discoverable when the folder is moved. Personal profiles, preferences,
captures, and build tools are not included. The MSVC runtime is linked statically;
a Windows virtual MIDI cable such as loopMIDI is still needed for DAW routing.

The separate build tree is `build/windows-portable`; dependencies stay in `.deps`
and the script directs temporary build files to `.tmp`. Nothing is published.

### GitHub portable builds

The **Portable builds** GitHub Actions workflow builds Windows x64 ZIP, Linux x64
AppImage (inside a tar.gz), and universal macOS ZIP packages. It runs on pushes,
pull requests, or manually from the Actions tab, and runs core tests on each OS.
Download packages from a successful run's artifacts or from a published release.
Builds do not automatically publish releases.

Only Windows has been tested with real MIDI hardware and a DAW. Linux and macOS
packages remain experimental; report problems through
[repository issues](https://github.com/SH4DOWSIX/VelCal/issues/new). See the
[0.0.1 release notes](docs/releases/0.0.1.md) for package requirements and
unsigned-app security guidance.

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

1. Select the physical MIDI input and either white or black keys. Place your
   straight-edge bar (for example, a piece of wood or a spirit level) across the
   group of keys you want to measure.
2. Select **Start section**, then press the same group of keys at least three
   times. The capture guide targets eight accepted soft, medium, and firm presses
   and reports which strength is still needed while recording.
3. Select **Finish section**. VelCal infers the short bar's note range, rejects
   incomplete or contaminated presses, and rebuilds the profile immediately.
4. Move the bar with at least two same-colour keys overlapping the previous
   section and repeat. Select **Save profile** when finished.

For live use, select an output and enable **Route MIDI**. macOS and Linux JUCE
backends support an application-owned `VelCal Output (virtual)` endpoint, but
these platforms have not yet been built or physically tested for VelCal.

### Windows MIDI setup

VelCal does not create a virtual MIDI cable on Windows. Create one with
[loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html), or use another
installed virtual MIDI cable. This carries MIDI messages, not audio.

1. Install and open loopMIDI, then create a port named `VelCal Output` with its
   **+** button. Keep loopMIDI running while using the port.
2. Start or restart VelCal after creating the port. Select your physical keyboard
   as **MIDI input** and the loopMIDI port as **MIDI output**.
3. In your DAW, select the same loopMIDI port as the instrument track's MIDI input.
4. Enable **Route MIDI** in VelCal. Use the loopMIDI input rather than also
   receiving the physical keyboard directly, to avoid duplicate notes. Do not
   route the DAW's MIDI output back to VelCal's input.

VelCal lists installed Windows MIDI outputs only. It remembers the selected port
when available; it does not automatically choose an output on first use.

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
- Selection of an existing virtual cable such as loopMIDI on Windows.

Original measurements are retained in versioned VelCal profiles so correction
curves can be regenerated when the algorithm improves.

## Licence

AGPL-3.0-only. See [LICENSE](LICENSE).

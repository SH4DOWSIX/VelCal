# VelCal Project Handoff and Plan

Last updated: 2026-10-04

This is the durable starting point for a new VelCal development conversation.
Read `AGENTS.md` first for workspace rules, then this file before proposing or
making changes. Keep this document synchronized with material project changes.

## Product Goal

VelCal is a free, open-source, cross-platform MIDI utility. It measures persistent
velocity-response differences between individual piano keys, generates a
correction curve per MIDI note, and routes
corrected MIDI into a DAW or instrument. Its distinguishing feature is guided,
automatic calibration using repeated simultaneous presses across a group of
keys.

Calibration uses native VelCal profiles. Importing profiles from other products
is outside the product scope at the user's request.

The user's physical test fixture cannot cover an entire keyboard. Calibration
therefore supports short, overlapping sections and treats white and black keys
as separate measurement sessions. The intended signal path is:

```text
physical keyboard
  -> automatic per-key calibration map OR that key's manual curve override
  -> optional per-key trim
  -> global velocity curve
  -> virtual/existing MIDI output
  -> DAW or software instrument
```

Starting a manual per-note curve seeds its points from the current calibrated
shape, then stores it as an override for that note. It does not destroy the raw
measurements or generated map, and Reset Key returns to automatic calibration.
The global curve never overwrites per-key data; it is the final keyboard-wide
transformation.

## Non-Negotiable Workspace Constraint

Do not install or download project-owned tools, SDKs, dependencies, caches, or
build outputs to `C:`. The drive is nearly full.

- Workspace: `D:\Coding\VelCal`
- Builds: `D:\Coding\VelCal\build`
- Dependencies: `D:\Coding\VelCal\.deps`
- Temporary project files: `D:\Coding\VelCal\.tmp`
- Existing compilers/Windows components on `C:` may be executed.
- Do not install JUCE system-wide.

See `AGENTS.md` for the authoritative rule.

## Current Status

VelCal is an early functional desktop application, not merely a prototype core.
It currently builds on Windows and has been used by the user to calibrate keys
and play through a DAW. The user reported being happy with the calibration and
playing behavior at that stage. The most recent smart capture guidance, UI, and
`8/8/8` readiness changes have not yet received a complete fresh physical
calibration pass.

Current versions:

- Application/CMake project version: `0.0.1`
- Profile schema: `4`
- Calibration algorithm: `0.4.0`
- JUCE: `9.0.3`, pinned under `.deps`
- nlohmann/json: `3.11.3`, pinned under `.deps`
- Language level: C++17
- Licence intent: `AGPL-3.0-only`

The project repository is https://github.com/SH4DOWSIX/VelCal, with `main` as the
default branch. Source, tests, build configuration, and documentation are tracked;
builds, dependencies, temporary files, profiles/app preferences, physical captures,
and third-party reference data remain local and are ignored by Git.

The first public release, [VelCal 0.0.1](https://github.com/SH4DOWSIX/VelCal/releases/tag/0.0.1),
was published on 2026-10-04 from commit
`30e469c6df8734c4f4d471dfe1d3e9070450d140`. Assets are Windows x64 and universal
macOS ZIPs, a Linux x64 tar.gz containing an AppImage, and `SHA256SUMS.txt`.
Only Windows has real keyboard/DAW test evidence; Linux/macOS remain experimental.

Development changes stay local unless the user explicitly requests a GitHub
update. The initial repository creation/push does not authorize future automatic
pushes. See `AGENTS.md` for the durable rule.

## Implemented Behavior

### Calibration Core

- Groups Note On messages into physical presses.
- Current grouping windows are 250 ms quiet time and 400 ms maximum press span.
- Infers the keys under a short bar after at least three repeated presses.
- A note must occur in at least 60% of preliminary presses to define the inferred
  section range.
- Requires 90% of expected notes for an accepted press.
- Rejects incomplete presses, duplicate notes, unexpected notes, and velocity 0.
- Uses the press median as the local force/reference velocity.
- Splits observations into soft `1-42`, medium `43-84`, and firm `85-127`.
- Uses robust outlier rejection, velocity bins, deadband suppression, confidence
  shrinkage, interpolation, and monotonic enforcement.
- Produces a 128-value constant-time lookup map for each of 128 MIDI notes.
- Preserves original accepted and rejected press data in profiles.

### Recommended Calibration Target

The normal pass criterion is now per note, not total-only:

- 8 accepted soft observations
- 8 accepted medium observations
- 8 accepted firm observations
- 24 accepted observations per covered key in total

Three presses only establish the bar position. Six total usable samples allow
the fitting algorithm to begin producing a map, but a key remains in the UI's
"Needs data" state until it reaches `8/8/8` after outlier rejection.

Potential future modes have been discussed but are not implemented:

- Quick: `5/5/5`
- Recommended: `8/8/8` (current fixed default)
- Thorough: `10/10/10`

### Short-Bar Alignment

- Each capture receives a segment ID.
- Adjacent segments should overlap by at least two same-colour keys; three or
  four is preferable.
- Shared keys align neighbouring segment references independently in soft,
  medium, and firm regions.
- White and black segment chains are independent.
- Disconnected chains are reported and should not be treated as a trustworthy
  whole-keyboard calibration.

### Guided Capture

- The desktop app has White keys / Black keys selection.
- Start Section opens a guide explaining bar placement and the regional
  calibration target.
- The start guide is a modal overlay inside the existing main window, avoiding
  native popup-window creation on click. MIDI setup begins only after Begin
  capture. This replaces the desktop AlertWindow after a reported two-second
  opening delay. A Debug-build UI Automation check passed three open/cancel
  cycles; temporary timing instrumentation measured the first overlay paint
  15.7-17.8 ms after entering the guide handler. User confirmation is still needed
  for the originally reported delay. Timing instrumentation is not in the app.
- During capture the header displays the inferred section range plus the weakest
  post-outlier soft, medium, and firm counts across keys covered by the bar.
- Live guidance says which bar-press strength is still needed and names the
  weakest key in that region.
- Selecting Finish Section before the inferred section reaches the selected
  target warns the user and offers to keep capturing.
- Finish Section infers the range, retains accepted/rejected measurements,
  recalculates maps, and updates the profile in memory.
- Save Profile is explicit; a completed capture is not automatically written.
- New Profile creates an unsaved identity profile.
- Clear Data confirms, clears measurements/manual note curves, and resets the
  global curve. A loaded file is unchanged until Save Profile is selected.

### Profiles and Editing

- Profiles are JSON files ending in `.velcal.json`.
- The header profile dropdown lists local profiles from `profiles/*.velcal.json`.
- The app remembers the last loaded profile and restores it on startup when the
  file still exists.
- A loaded profile can be deleted from the header after a confirmation prompt;
  deleting the active profile clears it from memory and resets runtime maps to
  identity.
- Schema migrations are backward compatible through schema 4.
- Older default `5/5/5` and 18-total profiles migrate to `8/8/8` and regenerate
  their calibration result from retained raw presses.
- Profiles contain raw presses, settings, generated maps/statistics, segment
  alignment, per-note trims, per-note curve overrides, global curve settings,
  and profile-local user presets.
- Per-note and global curves support draggable control points.
- Left-click selects a point or adds one between existing points.
- Right-click removes an interior point.
- Endpoint input coordinates remain anchored at 1 and 127.
- Point output and ordering are constrained so curves remain monotonic.
- The Smooth toggle switches between monotonic cubic interpolation and straight
  line segments.
- While dragging, a transient bubble displays integer input/output coordinates.
- Runtime maps remain quantized to MIDI's 1-127 Note On velocity values even
  though the graph displays a smooth continuous envelope.

### UI State

The app has two primary tabs:

1. Per-key calibration
   - guided section capture
   - piano keyboard visualization
   - selected-key trim
   - selected-key editable curve
   - valid press, coverage, and section metrics
2. Global curve
   - Linear, Soft touch, Firm touch, Compressed, and Wide dynamics presets
   - curvature, minimum output, and maximum output controls
   - editable control points and smoothing
   - profile-local custom preset saving

The piano rendering uses correct black-key placement over white-key joins. At
narrow widths it keeps a minimum white-key width and scrolls horizontally rather
than compressing all 88 keys.

Application icons use the user-supplied `resources/app-icon.png`
unchanged. The source was recovered from the previous build's embedded bytes
after the original workspace-root file was removed, so rebuilds retain the icon.
The user subsequently authorized tracking the renamed source PNG so GitHub
hosted runners and fresh source checkouts can build with the icon.
JUCE generates the Windows EXE icon and macOS bundle ICNS from this source. The
same image is embedded for the native window icon, including Linux. Linux CMake
installation also installs the executable, PNG icon, and
`resources/org.velcal.app.desktop` launcher. Platform-generated icons stay under
the build tree; the portable Windows EXE requires no separate image file.

Keyboard status display:

- No strip: no calibration observations
- Amber: some data, but at least one region is below 8 usable samples
- Cyan: fully sampled key is quieter and is being boosted
- Grey: fully sampled key is already near neutral
- Red: fully sampled key is louder and is being reduced
- Bright magenta outline: selected key

The legend is visible under the keyboard. Curve graphs show `ppp` through `fff`
on the output axis and `0, 32, 64, 96, 127` on the input axis. Internally Note On
velocity 0 is not used because it conventionally means Note Off.

App-wide state is stored in `profiles/.velcal-app-state.json` so this workspace
stays on `D:`. Startup restores the last selected MIDI input, installed MIDI
output, and last profile when those resources are available. The virtual-output
selection is supported only on macOS/Linux; an old Windows virtual-output
preference leaves the output unselected.

The optional `VELCAL_PORTABLE` build stores profiles and app preferences in
`profiles/` beside the Windows executable, Linux AppImage, or macOS `.app` bundle
instead of the development workspace. AppImages use the `APPIMAGE` environment
path so data is outside the read-only mount. macOS uses `currentApplicationFile`
so data is outside the signed bundle. Profile
chooser defaults use the same directory. Last-profile paths inside that directory
are saved relatively so they survive moving the portable folder. External profile
paths remain absolute. Normal development builds keep their existing storage.

### MIDI Routing

- Note On velocities pass through the composed lookup bank.
- Note Off, sustain, pitch bend, controllers, SysEx, and other MIDI messages pass
  through unchanged.
- Map banks are immutable snapshots swapped atomically.
- MIDI output sends run on a dedicated worker rather than the input callback.
- The output queue is bounded at 512 messages.
- Routing stops above 1,000 incoming messages/second or when the queue backs up.
- Activity reports raw and corrected velocity for the latest note.
- Capture and routing are mutually exclusive.

On Windows, selecting an already-installed virtual MIDI cable works. The
unavailable `VelCal Output (virtual)` option has been removed from the Windows
output list. Users create a port with loopMIDI (or another installed virtual MIDI
cable), keep it running, restart VelCal to discover it, and select the same port
as VelCal's output and the DAW's input. README contains setup instructions.
Windows requires an explicit output selection on first use; missing selections
show a generic selection warning when routing is requested. loopMIDI setup
guidance belongs in README only, not in the app. macOS/Linux retain the native
virtual-output option, but remain untested.

## Real Hardware and Test Evidence

Hardware/input used during development includes a Kawai piano exposed as
`KAWAI USB MIDI`. Existing virtual endpoints on the machine include names such as
`LM - Keysight Input` and `LM - PianoVFX Input`.

The user physically tested calibration and routed playing into a DAW, including
using `LM - PianoVFX Input`, and reported that it appeared to work correctly.
This predates the newest UI refinements and final `8/8/8` rule.

Real capture retained in the local workspace (not uploaded to GitHub):

- `captures/velcal-white-1790637398.csv`
- 62 physical presses across two overlapping white-key sections
- Segment 1: 32 accepted, 3 rejected
- Segment 2: 23 accepted, 4 rejected
- 7 overlapping keys
- Historical coverage: low 27, medium 21, high 7
- Segment chain connected
- Held-out validation: raw mean deviation 2.80, calibrated 1.07
- Measured improvement: 61.79%

Because the retained capture has only seven firm presses, it is one firm press
short of the new recommended `8/8/8` pass criterion. Reprocessing it under schema
4 should therefore show Needs Data rather than 100% completion.

Existing local profiles (ignored by Git) include:

- `profiles/kawai-white-partial.velcal.json`
- `profiles/LM - Keysight Input calibration.velcal.json`
- `profiles/LM - Keysight Input calibration v2.velcal.json`

Automated verification as of 2026-10-04:

```text
ctest --test-dir build -C Debug --output-on-failure
1/1 velcal_core_tests passed
```

Tests cover press grouping/rejection, short-section inference, nonlinear fitting,
outlier resistance, held-out consistency improvement, deadband behavior,
overlapping/disconnected sections, global curves, editable monotonic curves, and
profile round trips including control points and per-note overrides.

The Windows output-list change (removing built-in virtual output and adding
a generic missing-selection warning) builds successfully in Debug; the core
suite still passes 1/1. The changed selection and warning UI has not yet received
a physical DAW test. MSBuild reports an existing shared-intermediate-directory
warning involving a conflict-named generated resource project under `build`.

Local portable Release verification on 2026-10-04: `build-portable.bat --no-pause`
completed configuration, x64 Release compilation, CTest (1/1 passed), and folder/
ZIP packaging. Extracting the ZIP into a different `.tmp` folder with spaces,
launching from another working directory, and invoking New profile through UI
Automation wrote valid preferences beside the relocated EXE; the app closed
normally. DLL import inspection found only Windows components, with no dynamic
MSVC runtime dependency. The package contains no personal profiles/preferences.
MIDI hardware and a separate Windows machine have not been tested with this build.

App icon verification on 2026-10-04: the standard portable Release build and
CTest (1/1) passed with the supplied PNG. The packaged EXE icon and the running
native Windows window icon were extracted and visually confirmed to match the
source image. The test app closed normally. macOS bundle and Linux launcher/
window icon configuration remain untested on those platforms.

The local icon source has been renamed to `resources/app-icon.png`; CMake,
embedded-resource identifiers, and the Git exclusion use the new name. The
standard portable Release build and CTest (1/1) passed after the rename.

Sidebar padding verification on 2026-10-04: controls in both tabs now have 28px
left/right inset within the 273px sidebar. The portable Release build and CTest
(1/1) passed. UI Automation confirmed 217px sidebar dropdown widths in both tabs
and at minimum window size. Native screenshot capture returned blank images, so
visual confirmation of this change remains with the user.

Release CI verification on 2026-10-04: all three jobs in
[run 37217137034](https://github.com/SH4DOWSIX/VelCal/actions/runs/37217137034)
passed app compilation, core tests (1/1 per platform), and portable packaging.
Linux also passed an Xvfb AppImage startup check; macOS verified both arm64 and
x86_64 binary slices and its ad-hoc bundle signature. The initial CI run exposed
a missing explicit `<algorithm>` include in the tests and incorrect `lipo`
argument ordering; both were fixed before this successful run.
Downloaded archives were checked for required executables/licences and absence
of README files or personal profiles/preferences. GitHub's uploaded SHA-256
digests matched the local release assets before publication. These automated
checks do not establish Linux/macOS MIDI hardware, DAW, or visual correctness.

## Known Risks and Limitations

### Virtual MIDI Endpoint Stall

Before the routing worker and flood guard were added, the user observed the PC
freezing for several seconds around selecting a third-party virtual MIDI output
and again after closing VelCal. Windows Application/System logs showed no corresponding
MIDI, display, driver, or application-hang event. A feedback flood or blocking
virtual-driver call was suspected.

The affected endpoint was temporarily blocked, then re-enabled at the user's
request. The current safeguards remain, but the exact scenario has not yet been
deliberately retested and proven resolved. When testing it, record:

- exact physical input and virtual output names
- whether Route MIDI was enabled before changing the output
- whether the DAW or another MIDI application was open
- whether a feedback path existed
- whether VelCal's safety cutoff message appeared

Do not remove the bounded queue/flood cutoff without equivalent protection.

### Windows Virtual Endpoint

The current Windows product workflow uses user-created virtual MIDI cables.
Native Windows endpoint creation is optional future work, not a requirement for
the current release workflow.

JUCE 9.0.3 can use Windows MIDI Services when configured with
`NEEDS_WINDOWS_MIDI_SERVICES TRUE`, but this build does not enable it. Prior
research found the out-of-band Windows MIDI Services preview unsuitable for a
shipping dependency, and its default NuGet location would violate the `D:`-only
project storage rule.

When the production in-box `Windows.Devices.Midi2` API is appropriate:

- keep any SDK/build package under `.deps` on `D:`
- set `JUCE_WINDOWS_MIDI_SERVICES_PACKAGE_LOCATION` explicitly
- enable `NEEDS_WINDOWS_MIDI_SERVICES TRUE`
- move the app target to C++20 if JUCE requires it
- retain runtime fallback to legacy MIDI/existing virtual cables
- test endpoint creation, discovery, shutdown, and DAW reconnection

Do not install preview/runtime packages system-wide or onto `C:` without the
user's explicit permission.

### Product Gaps

- macOS and Linux have not been physically tested with MIDI hardware/DAWs.
- The newest smart capture guide, curve editor, axis labels, keyboard status
  colours, and `8/8/8` readiness rule need a fresh end-to-end user pass.
- There is no section manager for reviewing, deleting, or recapturing one bad
  section after it has been added.
- There is no undo/redo for curve editing.
- There is no dirty/unsaved indicator or close confirmation.
- Window state is not persisted.
- User-created global presets are profile-local, not yet shared across profiles.
- Quick/Recommended/Thorough capture modes are not implemented.
- There is no one-click calibration bypass/A-B validation view.
- The curve editor has no keyboard-accessible point editing yet.
- Portable packaging and GitHub build/test workflows exist; installers and
  trusted publisher signing/notarization do not. macOS uses ad-hoc signing only.
- The published `0.0.1` release has passing platform CI, but only Windows has
  physical test evidence. Fresh P0 validation and the virtual MIDI stall audit
  remain open.

## Prioritized Next Work

### P0: Validate the Current Milestone

1. Start a new profile with `KAWAI USB MIDI`.
2. Capture one short white-key section at `8/8/8` and confirm live guidance.
3. Confirm every covered key changes from amber to cyan/grey/red only after its
   own post-outlier regional counts reach `8/8/8`.
4. Capture an overlapping second white section and verify the chain connects.
5. Repeat with black keys.
6. Save, close, reopen, and confirm maps, regional counts, curve points, presets,
   trims, and status colours survive.
7. Route into the DAW through the known-working virtual endpoint and compare
   bypassed versus corrected playing.
8. Carefully retest the affected virtual MIDI endpoint while watching for the old
   stall and the routing safety message.
9. Exercise resizing at minimum and wide window sizes, keyboard scrolling, black
   key hit-testing, curve point add/drag/remove, smoothing, and coordinate bubble.

Treat failures found here as higher priority than new features.

### P1: Calibration Workflow Quality

1. Add a section/session manager showing group, note range, accepted/rejected
   presses, regional counts, overlap status, and delete/recapture actions.
2. Add Quick `5/5/5`, Recommended `8/8/8`, and Thorough `10/10/10` modes and
   store the selected target in the profile.
3. Expose why individual presses were rejected and allow a section retry.
4. Add an A/B bypass and before/after validation summary for physical confidence.
5. Add focused tests for schema 3-to-4 migration and per-key readiness with
   region-specific outlier removal.

### P2: Editing and Profile UX

1. Add undo/redo for curve points, smoothing, trims, and preset application.
2. Add dirty-state indication, close confirmation, Save As, and deliberate
   overwrite behavior.
3. Decide whether user presets should be global app data or profile-local. Any
   global preset store must remain under the project/user-selected `D:` location
   for this workspace.
4. Add precise keyboard controls/accessibility for selected control points.
5. Review whether per-key trim remains useful now that full per-key curves are
   editable, or whether it should become a simpler advanced control.

### P3: MIDI and Platform Work

1. Reproduce or close out the virtual MIDI endpoint stall investigation.
2. Add robust device hot-plug refresh and unavailable-device states.
3. Optionally evaluate a production Windows MIDI Services virtual endpoint when
   the supported in-box API/toolchain is available and distributable; the current
   Windows workflow uses user-created cables such as loopMIDI.
4. Build and test CoreMIDI virtual output on macOS.
5. Build and test ALSA sequencer virtual output on Linux.
6. Add automated tests around routing order, passthrough, queue overflow, and
   feedback cutoff where practical.

### P4: Release

1. Review generated and user-data exclusions before user-requested GitHub updates;
   do not automatically push routine development changes.
2. Keep the full AGPL-3.0 licence and dependency licence notices in packages.
3. Maintain the passing three-platform CI and inspect each future release's
   portable packages before publication.
4. Add trusted publisher signing/notarization when available.
5. Preserve explicit platform testing status, issue reporting, checksums, and
   unsigned-app security guidance in future release notes. These were included
   in the published `0.0.1` release; P0 validation and the virtual MIDI stall
   audit remain open.

## Source Map

- `include/velcal/calibration.hpp`: core types, capture analysis, maps, metrics
- `src/press_collector.cpp`: press grouping, range inference, quality decisions
- `src/calibration.cpp`: alignment, robust fitting, validation, curve interpolation
- `include/velcal/profile.hpp`: schema 4 profile model
- `src/profile.cpp`: JSON serialization, validation, migration
- `app/MidiEngine.*`: device lifecycle, capture buffering, routing worker/safety
- `app/MainComponent.*`: desktop workflow, painting, curve editor, controls
- `app/Main.cpp`: JUCE application/window setup
- `tools/windows_capture.cpp`: older Windows console capture/instrumentation tool
- `tools/analyze_capture.cpp`: offline CSV reprocessing and validation
- `tests/calibration_tests.cpp`: core regression suite
- `.github/workflows/portable-builds.yml`: three-platform build/test artifacts
- `tools/package_unix_portable.sh`: Linux AppImage and universal macOS packages
- `tools/package_windows_portable.ps1`: Windows portable folder/ZIP packaging
- `docs/releases/0.0.1.md`: published release notes and security guidance
- `docs/architecture.md`: concise architectural overview
- `README.md`: user-facing introduction, calibration, routing, and profile guide
- `docs/BUILDING.md`: build workflows, dependencies, and development tools

## Build and Run

The standard Windows build workflow is `build-portable.bat` from the workspace
root (or double-click it), producing a local Windows x64 portable Release.
Agents use `.\build-portable.bat --no-pause`. It configures `build/windows-portable`,
enables `VELCAL_PORTABLE`, statically links the MSVC runtime, builds the app/core
tests in Release, runs CTest, and calls `tools/package_windows_portable.ps1`.
Each successful run creates a new folder and ZIP under `build/portable`, containing
the EXE, licence, dependency licence notices, and an empty profiles directory. The repository README and
setup instructions remain on GitHub and are not bundled. Personal data is never
copied into the package. Temporary files are directed to `.tmp`; downloaded
dependencies remain in `.deps`. The script requires existing CMake, Git, Visual
Studio C++ tools and a Windows SDK; it does not install tools or publish anything.
Use `build-portable.bat --no-pause` for an unattended terminal run.

GitHub Actions uses `.github/workflows/portable-builds.yml` for Windows x64,
Linux x64 (Ubuntu 22.04 baseline), and universal macOS (11+ target). Windows uses
the existing batch workflow with deterministic release package names. Unix jobs
build/test in `build/ci` and use `tools/package_unix_portable.sh`. Linux packages
an AppImage with SHA-256-checked linuxdeploy; macOS verifies both CPU slices and
ad-hoc signs its bundle. Artifacts contain no README or personal data. Builds
upload artifacts but never automatically publish a release. Release notes live
in `docs/releases/0.0.1.md`; only Windows has real MIDI/DAW test evidence.

For specific debugging or offline-tool work, the separate Debug build remains
available. It is not the default build for local app delivery. From
`D:\Coding\VelCal`:

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Executables:

- Desktop app: `build\velcal_app_artefacts\Debug\VelCal.exe`
- Core tests: `build\Debug\velcal_core_tests.exe`
- Capture tool: `build\Debug\velcal_capture.exe`
- Offline analyzer: `build\Debug\velcal_analyze.exe`

Reprocess the retained physical capture:

```powershell
.\build\Debug\velcal_analyze.exe `
  .\captures\velcal-white-1790637398.csv `
  .\.tmp\reprocessed.velcal.json
```

## Handoff Prompt for a New Conversation

A concise way to resume is:

> Work on VelCal in `D:\Coding\VelCal`. Read `AGENTS.md` and
> `docs/PLAN.md` first, preserve the D-drive storage constraint, inspect the
> current code, and continue from the highest relevant priority in the plan.

Do not assume that a planned item is implemented merely because it was discussed.
Use the source and current test results as the final authority.

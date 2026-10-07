# VelCal Project Handoff and Plan

Last updated: 2026-10-07

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

0.0.4 published (2026-10-07), after the user requested publication:
https://github.com/SH4DOWSIX/VelCal/releases/tag/0.0.4
Verified run 37600534097 passed for Windows x64, Linux x64 and universal macOS,
including tests, packaging and installer checks (macOS also installed AU
validation). Public tag 0.0.4 points to tested revision
`ed89215e9ef9b85e448ca6c4bfba3814e9d083e1`; subsequent commits are documentation
only. Four installer assets plus SHA256SUMS.txt were uploaded, with each GitHub
asset size and SHA256 digest matching local files under build/releases/0.0.4.
Verified non-draft/non-prerelease publication and latest-release status. Notes
in docs/releases/0.0.4.md retain the Windows-only real hardware/DAW validation
caveat. No additional builds were run or requested.

All-platform build and conditional release authorized (2026-10-07): the user
requested fresh Windows x64, Linux x64 and universal macOS installer jobs for
0.0.4. Release notes prepared; the manual workflow was dispatched with
platform=all from revision `ed89215`:
https://github.com/SH4DOWSIX/VelCal/actions/runs/37600534097
Do not poll; wait for the user's results before checking the completed run.
Publish 0.0.4 only after all requested jobs pass and release artifacts are
verified. No local build requested. Linux/macOS real hardware/DAW validation
remains outstanding, and release notes must retain that limitation.

Source push authorized (2026-10-07): the user requested pushing this session's
DAW tab recall, profile/reset/save flow, shared preset library, compact header,
version indicator and version 0.0.4 changes. The proposed played-note-follow
feature was declined and is not implemented. Latest retained local Windows
CTest evidence is 3/3 passing (core 0.11s, app 8.03s, plugin 3.34s) from the
temporary 0.0.2 build, with user-reported working update display. Subsequent
0.0.4/hyphen edits are source/whitespace checked, not rebuilt. Remote main was
fetched and verified aligned before committing; its only installer workflow
is manual-only. Push includes reviewed source/tests/docs only, excluding build
outputs, dependencies, preferences, calibration profiles and captures. No local
or GitHub build, release tag or publication requested with this source push.

Version-display validation (2026-10-07): the user reports the temporary 0.0.2
build and update-available display work. At their request, PROJECT_VERSION is
now 0.0.4, superseding the temporary test version below. The installed/update
label uses an ASCII hyphen separator (`v0.0.4 - v0.0.5 is available`) rather
than a middle dot; the offline formatting assertion matches. This is a local
source change, not a published release. Whitespace checked; no new agent build,
test execution or GitHub update.

Temporary update-display version (2026-10-07): at the user's request, the CMake
project/build version is set to 0.0.2 so the installed checker can detect the
published 0.0.3 release as an update. Compiled labels, comparison and package
version follow PROJECT_VERSION. This is a local test version, not a release;
restore the intended release version before publication. No build or push run.

Version-label build follow-up (2026-10-07): the user's local build reached app
test compilation and failed with C2662 because the new tooltip test helper took
a const component while JUCE's `getTooltip()` is non-const. Corrected that helper
to accept a mutable component; all callers already supply one. Test-only fix,
source/API/whitespace checked; no agent rebuild or test execution performed.

Header version indicator (2026-10-07, source-only): moved the existing update
label from bottom-left to the space above Global curve, right-aligned to the
header edge, without moving/resizing navigation controls. It shows the compiled
installed version (`v0.0.3`), adding `v0.0.4 is available` after a middle dot when
a newer release is found. Checking, disabled and failed checks keep the installed
version visible; full check details stay in the tooltip. UpdateStatus now carries
the release tag separately from its descriptive text, preserving worker/cache
ownership and existing network behavior. Sidebar metrics use window bounds
instead of the relocated label for height calculations. Added offline status
formatting/tooltip and minimum/default/wide header-placement regressions.
Documentation updated. Source/API/whitespace checks only; no build or test run.

Single-row navigation (2026-10-07, source-only): per-key/global tabs now sit
immediately right of the profile selector, aligned with the menu and selector
at 38px height. Compact tab widths below 1100px leave at least 200px for the
selector at the supported 860px minimum. Removed the former 42px tab row and
14px gap; the working view starts 56px higher and its divider follows it.
Existing minimum/default/wide standalone/plugin layout coverage now checks
tab ordering, equal heights/alignment, selector width and non-overlap on both
views. Source/whitespace checks only; no build or runtime visual validation.

Profile menu revision (2026-10-07, source-only): the user reports the corrected
local installer build succeeded. Their UI feedback supersedes the visible-button
request below: New/Open/Save/Save As/Reset All/Delete now live in an editor-contained
menu opened by an icon button immediately left of the profile selector, on both
tabs. Accent colour is in the same menu and opens the existing swatch palette.
The extra action row and separate paintbrush button are removed, restoring the
52px header and earlier sidebar/tab positions. Menu enabled states and action
dispatch use the existing commands, including all save/reset confirmations;
callbacks check editor lifetime and current host state before dispatch. Closing
the editor exits its menu and detaches its owned theme. Existing layout coverage
now checks the compact header and menu entries/enabled states. Documentation
updated. Source/API/whitespace checks only; no new build, test execution, visual
validation or GitHub update. The user's successful build predates these UI edits.

Local build follow-up (2026-10-07): the user's Windows installer build compiled
the profile/preset changes. Core tests passed (0.14s) and plugin tests passed
(3.31s); app tests failed (17.95s) with `profile-flow dialog appears`, so packaging
stopped. The profile-flow failed-save case invokes a callback-less error dialog.
Pinned JUCE's `showUnmanaged` runs such dialogs synchronously when modal loops
are enabled, as they are in app tests, even through `showMessageBoxAsync`.
Profile/preset errors now supply explicit no-op callbacks to force asynchronous
delivery. The regression helper searches all active modal components by title,
reports the expected title on failure, waits for dialog destruction, and uses
the one-button OK result (0). Failed-save dirty-state/continuation assertions
remain intact. Source/API/whitespace review only after this correction; no agent
rebuild or runtime execution. The next user-authorized build must confirm the
app suite passes before installers can be delivered.

Profile flow and shared presets (2026-10-07, source-only): New/Open/Save/Save As/
Reset All/Delete are visible in a two-row top-right profile header on both tabs.
Save As switches to a separately saved copy and rejects the current filename.
First saves normalize `.velcal.json` and confirm existing-file replacement.
Unsaved transitions offer Save/Discard/Cancel; cancellation, save failure or
changed editor/host revision prevents the pending action. An ongoing capture
offers Finish and Save; direct saves are disabled during capture. New/Delete/
reset explicitly cancel capture. Clear Calibration stays in the per-key sidebar,
removing measurements, overrides and trims while preserving the global curve
and calibration settings. Reset All also restores default global/calibration
settings and white-key selection, preserving identity/file association and
legacy presets. Both are confirmed, dirty working edits, not immediate writes.

Named global presets now save immediately into `.velcal-curve-presets.json`
under the shared writable profile directory, independently of Save Profile.
Bounded inter-process locking and temporary-file replacement protect library
read-modify-write; malformed existing files are never silently replaced.
Duplicate names are case-insensitive with explicit replacement confirmation;
built-in names are reserved. Rename/Delete controls manage shared entries.
Applied curves are independent profile/DAW copies, so library changes cannot
alter saved songs. Legacy embedded presets remain available with `(profile)`
labels and can be copied via Save Preset; no automatic migration or profile
schema change. Other open editors refresh the library during appearance polling.
Core/app regression coverage added for library validation/round trips, immediate
save/recall, duplicates, copy independence, Save As original preservation,
cancelled/stale save callbacks, failed Save stopping New, Save-and-New, resets,
DAW publication and toolbar bounds on both tabs at minimum/default/wide sizes.
Source/API/whitespace checks only: no build, test execution, visual/host validation
or GitHub update. Next explicitly authorized installer build must run all suites
and validate native Save As/overwrite/cancel dialogs and both UI layouts.

Host-owned tab recall (2026-10-07, source-only): plugin tab clicks now publish
the selected per-key/global view in each instance's embedded DAW state and
notify the host of a non-parameter state change. Project/preset/default-state
recall restores that view before editor creation or into an already-open editor;
editor close/reopen retains the instance view. Plugins no longer read or write
the standalone tab preference in `.velcal-appearance.json`. Standalone still
remembers its last tab there; accent remains shared across standalone/plugins.
Older state without a tab, and invalid tab values, default to per-key. The new
optional field retains plugin state version 1 and profile schema 4. Existing
embedded profile state already recalls calibration, trims, custom per-key/global
curves, Smooth, global settings and user presets independently of profile files.
Added regressions for both tabs, live/headless host recall, instance isolation,
editor reopening, host notification, legacy/invalid fields, full profile settings
and standalone preference independence. Source/whitespace review only; no build,
runtime tests, push or host validation performed. Validate in the next explicitly
authorized build and exercise Studio One saved default/preset recall.

0.0.3 publication authorized (2026-10-06): run `37474725810` succeeded on
Windows x64 (job `112307051045`), Linux x64 (`112307050997`) and universal macOS
(`112307050479`), all from `85be388fcb8319ff97135c5006f42c5309631d4f`.
Build/test/package and platform installer checks passed, including installed AU
validation on macOS. This supersedes earlier source-only/unbuilt status and the
release hold below. The user requested publication after reporting completion;
release preparation uses only this run's installer artifacts, with SHA256 sums,
and tags the tested source revision. No additional builds are requested.
Windows-only real hardware/DAW evidence and remaining Linux/macOS risks remain
explicit in the release notes. Published `0.0.3` as the latest stable release:
https://github.com/SH4DOWSIX/VelCal/releases/tag/0.0.3 . Its four installer assets
and `SHA256SUMS.txt` are uploaded; GitHub-reported installer digests match local
SHA256 hashes. The release targets the tested source revision above. Subsequent
release-verification documentation changes do not alter build/package inputs.

Fresh verification authorized (2026-10-06): the user requested pushing all
changes since the last push and dispatching all three native installer jobs.
These builds must include the shutdown lifecycle fixes and remembered-tab
preference, so earlier passing installers are not substitutes. Await the user's
build-result notice without automatic polling. Do not publish 0.0.3: the user
will separately request release publication after successful builds.

Last-tab preference (2026-10-06): user tab clicks now save `curveTab` in the
existing per-user `.velcal-appearance.json`, preserving accent and other fields.
Standalone and plugin editors restore the last selected per-key/global tab on
opening; existing open editors do not switch tabs during accent polling.
Missing/invalid tab values default to per-key. Profile data, dirty state and DAW
serialization are unchanged. Added offline regressions for both tab directions,
accent preservation, standalone/plugin recall and invalid values. Source-only:
no agent build, test run or push authorized for this change.

Studio One follow-up (2026-10-06): the user rebuilt with `build-installers.bat`,
reinstalled, launched normally without disabling update checks, and can no longer
reproduce the lingering background process. This is positive Windows host
shutdown evidence for the combined lifecycle fixes, not isolation of one root
cause or Linux/macOS validation. No build/test logs were supplied to the agent.
Publication remains held pending fresh requested platform verification.

Release hold: Studio One shutdown regression (2026-10-06). The user reports
that installed 0.0.3 leaves Studio One running indefinitely after closing,
whereas 0.0.2 exits after a few seconds. The update check has already completed
when this happens, so a slow GitHub response is not a sufficient explanation.
Root cause is not confirmed. Local, unbuilt changes move the update worker from
an owning function-static singleton to shared live plugin/standalone ownership,
cancel its request and join before the final owner is destroyed, close response
handles on the worker, and retain only weak ownership/results in static storage.
Plugin state keeps the worker alive across editor close/reopen; headless plugin
instances do not start it. The embedded icon no longer keeps a DLL-static native
graphics image; its cropped app/window images use owner-scoped software storage.
Offline regressions cover completed/blocked requests, editor reopening, final
instance destruction and icon storage. Tests have been added but not run;
no build, push or release is authorized for this investigation. An A/B test using
`VELCAL_DISABLE_UPDATE_CHECK=1` in Studio One's launch environment is pending.
Hold publication even if the pending macOS run passes. After diagnosis, request
fresh build authorization and verify real Studio One exit/relaunch; older passing
installers cannot validate these changed production inputs.

Release 0.0.3 preparation (2026-10-06): the user authorized pushing all reviewed
development changes, dispatching all three native installer jobs, and publishing
only after every requested job passes. Application version is now 0.0.3;
schema 4 and algorithm 0.4.0 remain unchanged. Release notes are
`docs/releases/0.0.3.md`. Build/publication evidence will be recorded here once
available. No local build is requested; the existing workflow stays manual-only.

Release build dispatched: run `37464355700` builds all three platforms from
`0349abeb80c37bf995aa8f18d460bf9867cf98a1`. At the user's request, automatic
polling has stopped; await their completion/failure notice before inspecting
results. README edits requested during this run are documentation-only and do
not authorize a new build. When tagging a later documentation commit, verify
all build/package inputs are identical to this tested source revision.

Release build result (2026-10-06): Windows and Linux jobs in run `37464355700`
passed compilation, all three suites, packaging and installer smoke checks.
macOS universal compiled successfully; core/VST3/AU tests passed, but app tests
reported `FAIL: discard confirmation is shown`. Packaging and installer checks
were skipped, so 0.0.3 must not be published. The discard test assumed an async
dialog would exist after a fixed 30ms message-loop delay. A local test-only fix
now pumps the GUI loop until the specifically named AlertWindow appears and
until its discard callback completes, each bounded to two seconds. This is a
likely scheduling flake, not a proven diagnosis. The user subsequently authorized
pushing this test-only fix and dispatching a fresh macOS-only build. No local
build/test run is requested. App/plugin production and packaging inputs are
unchanged; a rerun of the old revision would not contain the test fix. Passing
Windows/Linux installers may be retained if production/package-input equivalence
is verified. Continue waiting for the user's build-result notice, without polling.

README refresh (2026-10-06): the user supplied standalone screenshots, retained
unchanged under `docs/images/per-key-calibration.png` and
`docs/images/global-velocity-curve.png`, with captions and descriptive alt text
in the README. Updated guidance covers whole-keyboard/default-on Smooth,
independent global Smooth, filename-based profile selection, embedded DAW
recall, persistent accent colours and the non-installing update indicator.
Documentation/images only; no new build or polling of the active run.

Post-release source fixes (2026-10-05): the user reported the installed profile
folder was absent until standalone saved, external profile names did not update
in the DAW, and new profiles inherited a virtual MIDI input name. Opening either
UI now creates the profile folder. The active external file is included in the
profile selector. The filename-display update below supersedes the earlier
use of internal profile names. Profile loading publishes its new path together with
its settings. New profiles use `New calibration`; plugin device metadata is
`DAW MIDI`, never a hidden physical endpoint. Existing saved names are preserved.
Regression tests were added for folder creation, external/library naming,
plugin recall, and neutral new-profile names. These changes are source-only:
no builds or test execution were authorized, and published 0.0.2 installers are
unchanged. Compile and run the updated suites in the next explicitly authorized
build before delivering updated installers.

Post-release source change (2026-10-06): standalone and plugin editors now have a
small, unboxed update status line anchored to the bottom of the left sidebar,
with 28px left and bottom padding. It follows window resizing independently
of the three metrics above it.
Standalone and plugin windows default to 1180 x 820, with a minimum height of
820, matching their opening height so the status stays clear of Sections when
resizing. The curve editor no longer reserves
bottom space for the update indicator. Installed builds perform a one-shot
background check of GitHub's latest VelCal release, compare it with the compiled
app version, and report whether an update is available, the app is current, or
the check is unavailable. A module-owned worker and cached result are shared
across plugin instances and editor close/reopen; neither networking nor worker
shutdown runs when closing an editor. The worker is joined at module unload.
The first editor starts the check; a host unloading/reloading the module starts
a fresh check. Separate VST3/AU modules or sandboxed host processes each have
their own cache. CTest sets `VELCAL_DISABLE_UPDATE_CHECK=1` to keep tests offline;
direct plugin test runs in installed builds must set that variable too.
Development builds leave checks off. This change is source-only; no build or
test execution was authorized. Compilation and real-host validation are pending.

UI modernization (2026-10-06, source-only): standalone/VST3/AU share the new
component-owned `app/Theme.hpp` look and feel, following the user's mockup.
Teal primary actions and selected tabs, outlined icon buttons, switch-style
Route MIDI/Smooth controls, larger selectors, the embedded app icon in the
header, and green/cyan/violet statistics replace the earlier flat styling.
The keyboard has shaded keys and a teal selection outline. Both velocity graphs
have a title and a translucent fill below the curve; plotting and hit testing
still use the same shared bounds. Statistics now follow the sidebar controls
and adapt their height to preserve clearance above the bottom update text.
Default/minimum window heights remain 820. The existing app PNG is embedded for
plugin-only builds and app/plugin tests as well as standalone. No profile schema,
calibration, routing, or host-state changes. Source/API/layout checks completed;
no build or runtime visual validation performed. Next authorized build should
verify both tabs, minimum/default/wide sizes, menus, capture guide and dialogs,
keyboard scrolling, curve editing, and plugin editor reopening.

Smooth first-click fix (2026-10-06, source-only): creating the first editable
per-key curve refreshed the toggle from its previous stored value before the
click handler read the user's requested value. The handler now captures that
value before curve creation and applies it to both the model and switch.
Added app regressions for the first off/next on click on initially automatic
per-key and global curves, including editable-point creation and dirty state.
No build or test execution performed; run the updated suite in the next
explicitly authorized build. The shared fix also applies to plugin editors.

Automatic Smooth follow-up (2026-10-06, source-only): the initial nine-point
manual override lost details on the first toggle. The subsequent approach of
interpolating all 127 integer values preserved calibration but did not visibly
smooth quantization kinks. The current automatic Smooth mode uses a shared,
adaptive monotonic fit: nine initial anchors plus additional points wherever
the interpolated output differs from the original map by more than one velocity
step. The graph and effective MIDI maps both use this fit when Smooth is on;
off restores the exact generated map. Original calibration, trim and automatic
mode are retained, so off/on returns to the same fit without a manual override.
Smoothing is also applied at load for profiles whose per-key Smooth is already
true (the existing default), with at most one velocity step difference before
trim and global processing. Existing manual curves still use their stored
points; global curves retain their existing behavior. Profile serialization
retains Smooth=false for empty per-key point lists using existing schema-4
fields. Added core regressions for visible kink removal, bounded error, sharp
calibration detail, monotonicity, endpoint preservation, MIDI/graph agreement,
off/on restoration and manual overrides; app regressions cover trim and setting
serialization. No build or test execution performed. Validate the updated
core/app/plugin suites and visual on/off behavior in the next authorized build.

Whole-keyboard Smooth (2026-10-06, source-only): the Smooth switch on the
per-key tab now sets smoothing for all 128 note curves, including manual
overrides, in one action. Selection and Reset key preserve the keyboard-wide
choice. New profiles default to on. The existing per-note schema-4 flags store
the uniform choice; older mixed profiles resolve to off for all keys during
loading so selection cannot produce conflicting switch states. Manual points
and calibration remain intact. Global-tab Smooth remains independent and
controls the final global curve. Added regressions for all-key toggles, key
selection, reset, default-on, serialization and legacy mixed-profile recall.
No build or test execution performed.

Profile selector update (2026-10-06, source-only): all saved-profile entries and
the active selection now display the filename without `.velcal.json`, never the
internal profile or MIDI-device name. Internal metadata is not rewritten.
Unsaved profiles have a selectable `New calibration (unsaved)` entry; an empty
library has a disabled `No saved profiles` entry. A recalled DAW profile keeps
its filename and selected entry even if the file is missing, using the embedded
state. Selecting that active entry does not attempt to reload the missing file.
The shared theme parents combo-box menus to MainComponent, prefers downward
placement, removes selected-row alignment that pushed the menu above the app,
and uses 36px rows. JUCE constrains large menus to the editor with scrolling.
Editor shutdown dismisses its menus before destroying the theme. Standalone
and shared VST3/AU regression coverage now checks filename/internal-name
mismatches, empty libraries, selectable unsaved profiles, missing-file project
recall and unchanged metadata after saving. No build or test execution; runtime
popup placement/scrolling verification remains for the next authorized build.

Curve coordinate bubble (2026-10-06, source-only): the drag readout now measures
its full Input/Output text using an explicit 12px font, reserves padding and
space for three-digit values, and disables ellipsis. Placement flips around
the dragged point and clamps to the plot at all four edges. This shared drawing
path covers per-key/global curves in standalone and plugins. Source/API and
edge-placement checks completed; no build or runtime visual verification.

Icon replacement (2026-10-06, source-only): `resources/app-icon.png` is now a
byte-for-byte copy of the user's `D:\Coding\Gradient Curve Editor Icon.png`
(1254 x 1254 RGBA). The original artwork is not regenerated or altered. The
shared `app/AppIcon.hpp` caches a transparent-margin crop for runtime display,
preserving every nontransparent pixel and a 2px safety margin. Header rendering
uses a 52 x 52 area, aspect-ratio preservation, high-quality resampling and
explicit full opacity; the earlier icon inherited 40% opacity from sidebar
border drawing. The standalone native window icon uses the same cropped image,
scaled into a square canvas without stretching. Existing CMake/platform icon
packaging uses the replaced PNG. Source/hash/API checks completed; no build or
runtime icon verification. Generated executable/bundle icons require rebuilding.

Accent palette (2026-10-06, source-only): a paintbrush icon below Save profile
opens an editor-contained callout with 16 labelled colour swatches. Selection
applies immediately to primary actions, tabs, switches, sliders, curve lines,
fills, handles, coordinate outlines, connected status and keyboard selection.
The dark surfaces and semantic data-status colours remain unchanged. Teal is
the default. `.velcal-appearance.json` in the shared writable profile directory
stores the named accent separately from MIDI preferences, calibration profiles
and DAW project state. Standalone/VST3/AU editors recall the choice and poll it
once per second to synchronize other open editors; malformed/unknown preferences
fall back to teal. Writes use JUCE's temporary-file replacement, and failed
writes leave the previous accent selected and report the error. The popup is
owned by its editor and destroyed before its theme, including during modal use.
Added app regressions for all colours, persistence, plugin/editor sharing,
unchanged profile/host state, minimum-size placement, popup shutdown and invalid
preferences. Source/API and whitespace checks only; no build or runtime tests
were authorized. Verify popup placement, keyboard navigation and visual contrast
in standalone and real DAW hosts during the next authorized build.

Current release `0.0.2` is installer-first: Windows Setup, Linux DEB/user-local RUN,
and universal macOS PKG. VST3 is an instrument-style MIDI processor on all three;
AU is a separate macOS MIDI effect for Logic. Full calibration/editing features
are shared; DAW plugins omit physical MIDI device/routing selectors. Installed
user data uses `app/DataPaths.hpp`, with `VELCAL_DATA_DIR` for isolated tests.
Historical portable milestones below describe earlier work, not the active
build workflow. Windows Studio One live correction and project recall are now
user-confirmed. Local installer Release passed all three suites on 2026-10-05.
Final Windows run `37378091541` passed all three test suites, installation,
standalone startup, custom-directory upgrade, and uninstall/data preservation.
Final Linux run `37375028280` passed all three suites plus DEB and user-local
RUN install/startup/uninstall checks. The RUN launcher uses extraction mode
automatically instead of depending on FUSE; app files live under
`~/.local/lib/velcal`, separate from profiles. Bundled dependency notices include
NSIS, ALSA, Brotli, libpng and makeself as applicable.
Earlier macOS runs passed all four core/app/VST3/AU suites, universal-slice
checks and ad-hoc signature verification, but failed packaging. The precise
`pkgbuild --analyze` variation was a plugin component listed without the optional
`BundleIsRelocatable` field. Packaging now sets or adds that boolean for actual
listed components. Final macOS run `37379958486` passed all four suites, universal
binary/ad-hoc signature checks, PKG installation, AU validation and standalone
startup for arm64 and x86_64, and uninstall with profiles preserved.
All three platforms have passing native release verification. Their compilation
inputs are identical; the platform-specific installer fixes were validated in
separate runs. Release notes link to the exact passing jobs. Release `0.0.2`
is published with all four installers and `SHA256SUMS.txt`; GitHub's uploaded
asset digests match the locally verified files. Real Linux/macOS/Logic testing
is still outstanding.
Native macOS CI preserves only tested bundles/version/licences as an internal
artifact before packaging. Optional packaging-only retries verify identical
CMake/C++/test/resource inputs before restoring those binaries. This avoids
recompiling unchanged code for further installer fixes; it is not portable
distribution. Workflows remain manual-only with all/default or platform-specific
selection; source pushes do not start builds.

VelCal is an early functional desktop application, not merely a prototype core.
It currently builds on Windows and has been used by the user to calibrate keys
and play through a DAW. The user reported being happy with the calibration and
playing behavior at that stage. The most recent smart capture guidance, UI, and
`8/8/8` readiness changes have not yet received a complete fresh physical
calibration pass.

Current versions:

- Application/CMake project version: `0.0.3`
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

[VelCal 0.0.2](https://github.com/SH4DOWSIX/VelCal/releases/tag/0.0.2) was published
on 2026-10-05 from commit `8da59b6cfd1870d2d2de110a6c1bf9206515595a`.
Assets are Windows x64 Setup, Linux x64 DEB and user-local RUN, universal macOS
PKG, and `SHA256SUMS.txt`. Standalone and VST3 are included on all platforms;
macOS also includes the AU MIDI effect. Native CI is passing; real Logic and
Linux/macOS hardware validation remain outstanding. No paid Apple signing or
notarization is used.

Development changes stay local unless the user explicitly requests a GitHub
update. The initial repository creation/push does not authorize future automatic
pushes. See `AGENTS.md` for the durable rule.

Local and GitHub builds also require an explicit build request. Changing,
testing, committing, or pushing source does not by itself authorize a build.
The installer workflow is manual-only (`workflow_dispatch`), with no push or
pull-request triggers. Source-only pushes do not start builds.

## Implemented Behavior

### VST3 Source Milestone (2026-10-05)

The user requested full standalone feature parity in VST3, excluding MIDI device
selection/routing handled by the DAW. Local source now includes a JUCE VST3
MIDI-processing instrument target for Windows/Linux/macOS and the shared calibration/editing UI.
Guided calibration receives raw host MIDI with sample-based timing. Correction
runs without an open editor; instances retain independent profiles and embed
complete profile data, including unsaved edits, in DAW project state. Profile
schema remains 4 and files are interchangeable with standalone. Unfinished
capture is retained across editor close/reopen but not saved in project state.

The host callback uses preallocated capture storage and a nonblocking map-copy
attempt, without OS MIDI ports, file operations, or statistical fitting. The
standalone routing worker and portable preferences remain separate. Development
plugin profile dialogs stay in the workspace's profiles directory; deployed
copies use user application data, never the DAW executable/plugin directory.

The Windows portable script now includes `velcal_plugin_VST3` and plugin tests,
and packages the complete `.vst3` bundle beside standalone. Manual-only platform
CI and Unix packaging have matching target/bundle additions. The subsequent
user-authorized Windows portable Release build passed all three CTest suites
(core, app, plugin; 3/3) and generated standalone plus the VST3 bundle in
`build/portable/VelCal-Windows-x64-20261005-201813-731` and its ZIP. JUCE's VST3
manifest helper loaded the built module and generated its manifest. Package
inspection verified required binaries/licences, an empty profiles directory,
no personal profiles/preferences or README in the ZIP, and matching hashes
for every bundled plugin file. No workflow dispatch, source push, or release
occurred. Automated tests do not establish compatibility with a real DAW.
Next: user Windows DAW validation of
calibration, passthrough, editor reopening, multiple instances and project recall.
Only after Windows VST3 works well should an installer install both standalone
and VST3. Linux/macOS plugin validation remains pending; host MIDI-effect support
and MIDI routing differ across DAWs.

### VST3 Instrument Compatibility Follow-Up (2026-10-05)

The first Windows plugin was categorized as an effect with no audio buses.
In Studio One, the user loaded it on the piano channel's audio inserts. It
received no keyboard MIDI until a separate instrument track explicitly fed it,
then showed MIDI activity but did not affect the piano. The user could not
choose that effect's output as the piano track's MIDI input.

At the user's request, VelCal now follows
[Springbeats' published VelPro VST3 design](https://springbeats.com/2026/02/19/velpro-vst3-format/):
an instrument category, MIDI input/output, and silent audio output. JUCE flags
are IS_SYNTH=TRUE and IS_MIDI_EFFECT=FALSE, VST3 categories Instrument/Tools,
with one default stereo output (mono also accepted) and no audio inputs.
Normal processing clears audio and corrects MIDI; bypass clears audio and
leaves MIDI unchanged. Plugin identifiers and profile/DAW state formats remain
unchanged. Full calibration/editing and standalone remain intact.

The intended Studio One workflow is a VelCal instrument track receiving the
keyboard, with piano instrument tracks receiving VelCal's MIDI output. It
requires replacing the old bundle, rescanning its changed category, and loading
it from Instruments rather than audio inserts. This is not a same-track Note FX
integration and does not host the piano plugin. The user authorized the Windows
Release build for this change. The standard portable build succeeded and all
three suites passed (3/3). The new package is
`build/portable/VelCal-Windows-x64-20261005-205407-326` with a matching ZIP.
JUCE's manifest helper loaded the module and generated Instrument/Tools
categories with the existing class IDs. Package checks passed for required
binaries/licences, matching plugin file hashes, an empty profiles directory,
and absence of personal profiles/preferences or README in the ZIP.
The user subsequently confirmed corrected live playing and song save/reopen in
Studio One on Windows. Linux/macOS hardware and real-host validation remain
pending. No remote update occurred at this historical milestone.
Plugin regressions now also cover instrument flags, stereo/mono bus layouts,
audio silence, corrected MIDI with an audio bus, and bypass passthrough.

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
- Every capture exit restores the input, key-group, routing, and capture-button
  controls, including output changes, New Profile, Clear Data, and deletion.
- New Profile creates an unsaved identity profile.
- Clear Data confirms, clears measurements/manual note curves, and resets the
  global curve. A loaded file is unchanged until Save Profile is selected.

### Profiles and Editing

- Profiles are JSON files ending in `.velcal.json`.
- Saves serialize completely and write/close a temporary file beside the
  destination before replacing it atomically. Failed serialization, writing,
  or replacement leaves the previous profile intact. This is not a guarantee
  against power loss or failing storage hardware.
- An asterisk marks unsaved profile changes. Closing, creating a new profile,
  or loading another profile asks before discarding edits or an active capture.
  Cancel keeps the current work; Save Profile remains explicit.
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
- Clicking at an existing input selects its point instead of inserting a
  duplicate. Crowded points are protected against reversed drag bounds;
  fractional points in existing profiles remain supported.
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

Application icons use the user-supplied `resources/app-icon.png`, replaced by
the gradient curve editor artwork on 2026-10-06 as described above. The earlier
source was recovered from the previous build's embedded bytes
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
- Teal outline: selected key

The sampled-key coverage percentage uses the weakest post-outlier count in
each velocity region across measured keys. It reaches 100% only when all
sampled keys meet their regional target; it does not mean all 88 keys have been
captured or that disconnected sections have been aligned. Loading existing
schema-4 profiles refreshes this percentage from stored per-note statistics
without changing their maps. The profile schema and fitting algorithm versions
remain unchanged.

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
- Output opening and destruction also run on the worker. Opening is reported
  in the UI; failed or unresponsive output operations stop routing and report
  an error. The UI polls for operations exceeding two seconds.
- On manual stop or safety cutoff, the output worker releases sustain,
  sostenuto, and hold-2, then sends All Notes Off and All Sound Off on channels
  used by routed notes/pedals. Cleanup follows any already in-flight send.
- Output shutdown waits at most 250 ms for its worker. A blocked driver retains
  its own session without references to MidiEngine or the UI; it completes
  cleanup if it recovers. A new output session cannot start while that old
  session is still busy. Cleanup cannot reach a permanently blocked driver.
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

Local audit-fix verification on 2026-10-05: the standard portable Release build
passed both CTest suites (2/2) and generated a new Windows folder/ZIP. Core
regressions cover crowded/fractional curve points, per-key regional coverage,
outlier-dependent readiness, stale cached coverage, and preservation of an
existing profile during failed serialization/replacement. App regressions use
fake outputs for message ordering and passthrough, mapped velocities, normal
and safety-triggered note cleanup, bounded queue overflow, blocked open/send/
close calls, delayed cleanup after engine destruction, watchdog errors, and
restarting without delivering stale messages to a new output. UI tests cover
capture-control restoration and Cancel/Discard behavior for closing or switching
unsaved profiles, curve-only edits, and clearing the dirty marker after saving.
The tests use their own build-tree profile paths and do not open real MIDI ports.
The dialog test initially needed a message-loop pump before inspecting JUCE's
asynchronously created confirmation; the corrected suite passes. No fresh
physical keyboard/DAW test or Linux/macOS build has been performed for these
local changes. Verification was completed locally before the subsequent
user-authorized source push; no release publication was requested.

The subsequent GitHub run
[37340016486](https://github.com/SH4DOWSIX/VelCal/actions/runs/37340016486)
compiled Linux successfully and passed the core tests, but the new app tests
terminated with X11 `BadAtom` on `X_ChangeProperty` (atom 0) under bare Xvfb.
JUCE's pinned Linux source looks up `WM_PROTOCOLS` without creating it and uses
it when creating desktop windows; this points to missing window-manager atoms
when the confirmation tests open a dialog. The ALSA missing-sequencer warning
is separate from the fatal X11 error. The local workflow fix installs Openbox
and x11-utils on the disposable runner, then uses `run_linux_gui_check.sh` to
wait for window-manager readiness for both tests and package startup. No tests
are disabled. This fix has not yet been validated by a Linux rerun; no build or
workflow dispatch was performed because builds require explicit authorization.

## Known Risks and Limitations

### Virtual MIDI Endpoint Stall

Before the routing worker and flood guard were added, the user observed the PC
freezing for several seconds around selecting a third-party virtual MIDI output
and again after closing VelCal. Windows Application/System logs showed no corresponding
MIDI, display, driver, or application-hang event. A feedback flood or blocking
virtual-driver call was suspected.

The affected endpoint was temporarily blocked, then re-enabled at the user's
request. Output lifecycle calls now run on an independently owned worker session,
shutdown has a bounded wait, and simulated blocked-driver regressions pass.
Input-device opening/stopping still uses JUCE synchronously. These changes cannot
repair a malfunctioning OS driver or prove the cause of the original PC freeze.
The exact physical scenario has not yet been
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
- New profile toolbar, Save As, Save-and-continue, reset operations and shared
  preset library require build/runtime and real-host validation (source-only).
- Window state is not persisted.
- Quick/Recommended/Thorough capture modes are not implemented.
- There is no one-click calibration bypass/A-B validation view.
- The curve editor has no keyboard-accessible point editing yet.
- Installer packaging and manual native GitHub build/test workflows exist.
  Trusted publisher signing/notarization is not provided; macOS uses ad-hoc
  signing only, and paid Apple signing is explicitly outside this release.
- The published `0.0.2` release has passing platform CI, but only Windows has
  physical test evidence. Fresh P0 validation and the virtual MIDI stall audit
  remain open.

## Prioritized Next Work

### Completed: Installer And AU Release

The user authorized implementation, GitHub push, all three native builds,
monitoring/fixing failures, and publication after passing checks on 2026-10-05.
Portable distribution is retired by request; no paid Apple signing/notarization.

1. Windows installer Release and core/app/plugin suites passed locally (3/3).
2. All native tests and installer checks passed, including both AU architectures.
3. Verified 0.0.2 installers/checksums published; no portable assets.
4. Obtain real Linux/macOS/Logic testing; CI is not hardware/host evidence.
5. Continue full Windows calibration/editor/pedal/multiple-instance host checks.

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
2. Validate Save As, Save-and-continue and reset dialogs in the next authorized build.
3. Validate the shared curve-preset library across standalone, VST3/AU and legacy
   profiles. The development store stays in the project/user-selected `D:` location.
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
6. Extend the fake-output routing regressions with real hardware/DAW evidence
   and device hot-plug cases.

### P4: Release

1. Review generated and user-data exclusions before user-requested GitHub updates;
   do not automatically push routine development changes.
2. Keep the full AGPL-3.0 licence and dependency licence notices in packages.
3. Maintain the passing three-platform CI and inspect each future release's
   installer packages before publication.
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
- `app/Theme.hpp`: shared JUCE control styling and action icons
- `app/AppIcon.hpp`: owner-scoped software icon loading, crop and window sizing
- `app/UpdateCheck.*`: shared live-owner worker, cancellation and weak result cache
- `app/Main.cpp`: JUCE application/window setup
- `app/PluginProcessor.*`: VST3 processor/editor and DAW lifecycle
- `app/PluginState.*`: per-instance profiles, map publication, DAW state
- `tools/windows_capture.cpp`: older Windows console capture/instrumentation tool
- `tools/analyze_capture.cpp`: offline CSV reprocessing and validation
- `tests/calibration_tests.cpp`: core regression suite
- `tests/app_tests.cpp`: fake MIDI output, capture controls, and unsaved-profile regressions
- `tests/plugin_tests.cpp`: host MIDI, raw capture/overflow, editor lifetime and DAW recall (Windows Release passed)
- `.github/workflows/installer-builds.yml`: manual native installer build/test jobs
- `tools/package_unix_installers.sh`: Linux DEB/RUN and universal macOS PKG
- `tools/run_linux_gui_check.sh`: window-manager setup for headless Linux checks
- `tools/package_windows_installer.ps1`: pinned NSIS Windows setup packaging
- `app/DataPaths.hpp`: installed per-user storage and test overrides
- `installers/`: platform install/uninstall definitions
- `docs/releases/0.0.3.md`: release-candidate notes, verification gate and security guidance
- `docs/releases/0.0.2.md`: previous installer release notes and native evidence
- `docs/releases/0.0.1.md`: historical portable release notes
- `docs/architecture.md`: concise architectural overview
- `README.md`: user-facing introduction, calibration, routing, and profile guide
- `docs/BUILDING.md`: build workflows, dependencies, and development tools

## Build and Run

Use `.\build-installers.bat --no-pause` for Windows x64 Release. It configures
`build/windows-installer` with `VELCAL_INSTALLED=ON`, statically links the MSVC
runtime, builds standalone/VST3 and core/app/plugin tests, runs CTest, then
packages with pinned, hash-checked NSIS under `.deps`. Setup EXEs go into
`build/installers`; temporary files stay in `.tmp`. No personal data is bundled.

`.github/workflows/installer-builds.yml` is manual-only, never triggered by
push/PR. It builds Windows x64, Linux x64 (Ubuntu 22.04 baseline), and universal
macOS (11+ target). Unix builds use `build/ci` and
`tools/package_unix_installers.sh`. Linux outputs DEB and user-local RUN;
macOS verifies universal slices, ad-hoc signatures, and AU validation, then
outputs an unsigned PKG. Native installer/uninstaller smoke checks preserve
user profiles. Artifacts do not automatically publish releases. Current release
notes are `docs/releases/0.0.3.md`. Only Windows has real MIDI/DAW evidence.

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

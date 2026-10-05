# Building VelCal

Builds and publication require explicit user authorization. Source pushes do not
trigger builds; the installer workflow is manual-only.
Its platform selector defaults to all; platform-specific packaging fixes can
be validated individually without rebuilding unchanged platform payloads.

## Windows Release

Use existing Visual Studio C++ tools, Windows SDK, Git, and CMake 3.22+:

```powershell
.\build-installers.bat --no-pause
```

This builds standalone and VST3 with a static MSVC runtime, runs core/app/plugin
tests, and creates `build/installers/VelCal-<version>-Windows-x64-Setup.exe`.
Build files live in `build/windows-installer`. Pinned NSIS tools are fetched and
hash-checked inside `.deps`; temporary files stay in `.tmp`.

## Linux And macOS

Native dependencies and commands are recorded in
[Installer builds](../.github/workflows/installer-builds.yml).
Configure `build/ci` with Release, `VELCAL_INSTALLED=ON`, and install prefix `/usr`.
Linux GUI tests use Xvfb/Openbox. Package with
`bash tools/package_unix_installers.sh build/ci`.
Outputs are a Debian/Ubuntu `.deb` and user-local `.run` installer. The latter
installs an AppImage internally rather than distributing a portable app.

macOS uses `-DCMAKE_OSX_ARCHITECTURES='arm64;x86_64'` and
`-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`. AU defaults on only on macOS. Packaging
checks both CPU slices, ad-hoc signs binaries, and produces an unsigned `.pkg`.
No paid Apple signing credentials or notarization are required by the build.

## Targets And Tests

`VELCAL_BUILD_APP`, `VELCAL_BUILD_VST3`, and `VELCAL_BUILD_AU` control formats.
`VELCAL_INSTALLED=ON` enables user storage; development builds otherwise use
workspace profiles. `VELCAL_DATA_DIR` overrides the data root for tests.
JUCE 9.0.3 includes the VST3 SDK. Dependencies are pinned under `.deps`.

VST3 uses an instrument presentation with silent audio output and MIDI in/out.
AU is a separate MIDI effect with no audio buses for Logic's MIDI FX slot.
Both use the complete calibration/editing UI without physical MIDI selectors.
Core, UI/routing, plugin-state, MIDI/bypass, capture, and presentation tests run
on native platforms. CI also installs/uninstalls packages, checks app startup,
preserves user data, and validates AU with `auval` for both architectures.
Automated checks do not replace real keyboard/DAW validation.

See [installation and routing](INSTALLING.md), [architecture](architecture.md),
and [project handoff](PLAN.md). Debug builds are reserved for specific debugging.
Capture/analyze tools remain development targets, not installer payloads.
Keep local build/dependency/temp storage on D: as specified in AGENTS.md.

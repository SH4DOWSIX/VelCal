# Building VelCal

For downloads and app instructions, start with the [README](../README.md).
This guide is for building from source and using the development tools.

Agents run local or GitHub builds only on the user's explicit build request;
permission to change, commit, or push source does not authorize a build.

## Windows Portable Release

Requirements: CMake 3.22 or newer and Git on PATH, plus existing Visual Studio
C++ build tools and a Windows SDK. The script does not install these tools.

Double-click `build-portable.bat` in the repository root, or run:

```powershell
.\build-portable.bat --no-pause
```

This configures an x64 Release build in `build/windows-portable`, builds the app
and core/app tests, runs CTest, and creates a fresh portable folder and ZIP under
`build/portable`. The MSVC runtime is linked statically.

Packages contain the executable, licence, dependency licence notices, and an
empty `profiles/` directory. README files, personal profiles, preferences,
captures, and build tools are not bundled. Nothing is published automatically.

## Dependencies And Storage

CMake fetches pinned JUCE and nlohmann/json sources into `.deps`. The tracked
`resources/app-icon.png` is embedded during the build; generated icons remain
under the build tree. Do not install JUCE system-wide.

For the development workstation at `D:\Coding\VelCal`, project dependencies,
tools, caches, and generated files must remain on `D:`. Use `build` for outputs,
`.deps` for dependencies, and `.tmp` for temporary files. Do not add project
tools or dependencies to `C:`. See [AGENTS.md](../AGENTS.md) for workspace rules.
The Windows batch script directs temporary files to `.tmp`.

Builds, dependencies, temporary files, profiles/preferences, physical captures,
and third-party reference data are excluded from Git.

## Debug Builds And Tools

For specific debugging work on Windows:

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The desktop executable is `build/velcal_app_artefacts/Debug/VelCal.exe`.
This is separate from the standard portable Release workflow.

`build/Debug/velcal_capture.exe` is a Windows console instrumentation tool. It
lists MIDI inputs, captures repeated presses across overlapping sections, and
writes raw accepted/rejected events to `captures/` and a profile to `profiles/`.
It is not the normal desktop app or a real-time MIDI processor.

Reprocess a saved capture without replaying the keyboard:

```powershell
.\build\Debug\velcal_analyze.exe .\captures\your-capture.csv
```

Profiles retain measurements and generated corrections. For algorithm details,
see [architecture.md](architecture.md) and [PLAN.md](PLAN.md).

## GitHub Builds

The workflow is manual-only (`workflow_dispatch`), not triggered by pushes or
pull requests. Agents dispatch it only when the user requests a GitHub build.

The [Portable builds workflow](../.github/workflows/portable-builds.yml) builds
Windows x64 ZIPs, Linux x64 AppImages inside tar.gz archives, and universal macOS
ZIPs. It runs core and app tests on each OS, checks Linux startup, and verifies both
macOS CPU slices and the ad-hoc bundle signature.

Windows uses the existing batch workflow. Linux/macOS use `build/ci` and
`tools/package_unix_portable.sh`. Exact runners, platform dependencies, and
configuration flags are recorded in the workflow.

The app regression suite uses fake MIDI outputs for routing, cleanup, failure,
and blocked-driver tests. Profile/UI tests use a separate directory in the build
tree. Linux runs the tests and package startup check under Xvfb with Openbox;
`tools/run_linux_gui_check.sh` waits for window-manager readiness before running
each command and preserves failures. These checks do not replace hardware tests.

Successful runs upload downloadable artifacts but never automatically publish
releases. Only Windows has real keyboard/DAW testing; Linux/macOS packages remain
experimental. Release notes are kept in `docs/releases/`.

# VelCal Workspace Rules

## Start here

- Before planning or changing VelCal, read [docs/PLAN.md](docs/PLAN.md). It is
  the project handoff: product intent, current implementation, test evidence,
  known risks, and prioritized next work.
- Keep that plan current when a change materially alters behavior, architecture,
  profile compatibility, testing status, or priorities.

## Default Windows build

- Use `build-portable.bat` in the workspace root as the standard Windows build
  workflow. For agent/terminal runs, use `.\build-portable.bat --no-pause`.
- This builds Windows x64 Release in `build/windows-portable`, runs the core
  tests, and creates a new portable folder and ZIP under `build/portable`.
- Keep portable behavior intact: profiles and app preferences belong in
  `profiles/` beside the executable, and the MSVC runtime is linked statically.
- Never bundle personal profiles, preferences, or captures in the package.
- Package the EXE, licence, and an empty profiles directory. Keep the repository
  README and setup instructions on GitHub rather than copying them into the ZIP.
- Use Debug builds only for specific debugging needs; the portable Release is
  the default build to deliver for local use and verification.
- Keep build instructions in `README.md` and `docs/PLAN.md` consistent with this
  workflow. Building locally does not authorize publication or a GitHub update.

## GitHub updates

- Keep development changes local by default. A request to implement, fix, test,
  or document something is not permission to update GitHub.
- Only push commits, publish releases, or otherwise update the GitHub repository
  when the user explicitly asks for that update. Permission for one update does
  not authorize automatic pushes in future turns or conversations.
- Local Git inspection and local commits are permitted as part of development;
  they must not trigger an automatic push or other remote publication.
- Before an authorized push, review the staged files and keep generated builds,
  downloaded dependencies, caches, app preferences, personal calibration profiles,
  captures, and third-party reference data out of the repository.

## Storage constraint

- Do not install or download VelCal SDKs, dependencies, caches, or build trees to
  the `C:` drive. Space on that drive is limited.
- Keep project-owned content beneath `D:\Coding\VelCal`.
- Use `D:\Coding\VelCal\build` for generated builds.
- Use `D:\Coding\VelCal\.deps` for downloaded source dependencies and caches.
- Use `D:\Coding\VelCal\.tmp` for project-specific temporary files when a tool
  permits selecting its temporary directory.
- Existing compilers and operating-system components already installed on `C:`
  may be executed, but do not add or update them as part of this project.
- Do not perform a system-wide JUCE installation. Fetch or clone a pinned JUCE
  source tree into `.deps` and build it from the workspace.

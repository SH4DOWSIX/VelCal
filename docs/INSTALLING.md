# Installing VelCal

Close DAWs and VelCal before installing or updating. Download only from
[VelCal releases](https://github.com/SH4DOWSIX/VelCal/releases).
Installers never include personal calibration data.

## Windows

Run the x64 Setup EXE. Select standalone and/or VST3. Standalone goes under
Program Files with a Start menu shortcut; VST3 goes into
`C:\Program Files\Common Files\VST3\VelCal.vst3`. Administrator access is needed.
The unsigned installer may show SmartScreen warnings; review the source and
download before proceeding. Uninstall through Windows Installed Apps.

## Linux

Debian/Ubuntu x64 (Ubuntu 22.04 or compatible newer runtime):

```sh
sudo apt install ./VelCal-0.0.2-Linux-x64.deb
```

Standalone goes to `/usr/bin/VelCal` and VST3 to `/usr/lib/vst3`.
Add that plugin directory to your DAW's scan paths if needed.
Remove using `sudo apt remove velcal`.

Alternatively, install per user without root:

```sh
chmod +x VelCal-0.0.2-Linux-x64-Install.run
./VelCal-0.0.2-Linux-x64-Install.run
```

The `.run` installs standalone in `~/.local/share/velcal` with a desktop entry
and `~/.local/bin/velcal`; VST3 goes into `~/.vst3`. Optional components:
`./VelCal-0.0.2-Linux-x64-Install.run -- --standalone-only` or `-- --vst3-only`.
Uninstall with `bash ~/.local/share/velcal/uninstall.sh --uninstall`.
Do not mix both installers. Linux x64 needs glibc 2.35+; the plugin also requires
your distribution's normal desktop/audio libraries.

## macOS

Run the universal `.pkg` (macOS 11+, Intel or Apple Silicon). Customize the
components if needed. It installs `/Applications/VelCal.app`, VST3 under
`/Library/Audio/Plug-Ins/VST3`, and AU under `/Library/Audio/Plug-Ins/Components`.

This release is not Developer ID signed or notarized. Binaries are ad-hoc signed
for integrity, not publisher authentication. If macOS blocks the installer/app,
review the download, then use System Settings > Privacy & Security > Open Anyway
where available. See [Apple's security guidance](https://support.apple.com/en-us/102445).
Managed Macs may prohibit this. Never disable Gatekeeper globally.
Restart your DAW and rescan after installation; unsigned-plugin approval can vary
between macOS versions and hosts. Report blocked scans rather than assuming
automated AU validation guarantees Logic acceptance.

Uninstall using:

```sh
sudo bash '/Library/Application Support/VelCal/uninstall.command'
```

## DAW Routing

VST3: load VelCal as an instrument on its own track. Set its input to your
keyboard; set the piano track's MIDI input to the VelCal instance. Enable
monitoring on both. Select only VelCal, not All Inputs or the keyboard, on the
piano track. VelCal produces MIDI, not sound, and is not an audio insert.
Host support for instrument MIDI output is required.

Studio One: select the piano track and use its Inspector MIDI input selector.
The `MIDI Input 1..16` menu beside the instrument name selects a destination bus,
not the source. The user confirmed corrected playing and song save/reopen on
Windows with the instrument presentation.

Logic: load AU into the piano track's **MIDI FX** slot above its instrument.
This processes MIDI on the same track before the piano. See
[Apple's plug-in slot instructions](https://support.apple.com/en-ie/guide/logicpro/lgcp7989b5cd/mac).
Real Logic/keyboard testing remains outstanding.

All calibration, profiles, per-key trims/curves, presets, and live feedback are
available in plugins. MIDI devices are selected by the DAW. Completed profiles
and unsaved edits are embedded in DAW projects. Finish an active capture before
saving the project; unfinished captures are not serialized.

## Profiles And Updates

User data is preserved on uninstall and stored in:

- Windows: `%APPDATA%\VelCal\profiles`
- macOS: `~/Library/Application Support/VelCal/profiles`
- Linux: `${XDG_DATA_HOME:-~/.local/share}/VelCal/profiles`

For an older portable copy, open its profile JSON files from their old folder,
then save in the new location. Back up important profiles; installers do not
migrate or delete old personal files. Standalone and plugins share profile
files, but each plugin instance has independent DAW project state.

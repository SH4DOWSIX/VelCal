#!/usr/bin/env bash
set -euo pipefail
if [[ "$EUID" == 0 ]]; then echo 'Run this installer as your normal desktop user, not root.' >&2; exit 1; fi
case "${1:-}" in
  '') standalone=1; plugin=1 ;;
  --standalone-only) standalone=1; plugin=0 ;;
  --vst3-only) standalone=0; plugin=1 ;;
  --uninstall)
    rm -f "$HOME/.local/bin/velcal" "$HOME/.local/share/applications/org.velcal.app.desktop" "$HOME/.local/share/icons/hicolor/256x256/apps/org.velcal.app.png"
    rm -rf "$HOME/.local/share/velcal" "$HOME/.vst3/VelCal.vst3"
    echo 'VelCal removed. Personal profiles have been preserved.'
    exit 0 ;;
  *) echo 'Options: --standalone-only, --vst3-only, --uninstall' >&2; exit 1 ;;
esac
payload=$(cd "$(dirname "$0")" && pwd)
app="$HOME/.local/share/velcal"
mkdir -p "$app" "$HOME/.local/bin" "$HOME/.vst3"
cp "$payload/LICENSE" "$app/LICENSE"
cp -R "$payload/LICENSES" "$app/"
cp "$payload/install.sh" "$app/uninstall.sh"
chmod +x "$app/uninstall.sh"
if [[ "$standalone" == 1 ]]; then
    cp "$payload/VelCal.AppImage" "$app/VelCal.AppImage"
    chmod +x "$app/VelCal.AppImage"
    ln -sfn "$app/VelCal.AppImage" "$HOME/.local/bin/velcal"
    mkdir -p "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/256x256/apps"
    cp "$payload/org.velcal.app.png" "$HOME/.local/share/icons/hicolor/256x256/apps/"
    printf '[Desktop Entry]\nType=Application\nName=VelCal\nExec="%s"\nIcon=org.velcal.app\nTerminal=false\nCategories=AudioVideo;Audio;Midi;\n' "$app/VelCal.AppImage" > "$HOME/.local/share/applications/org.velcal.app.desktop"
fi
if [[ "$plugin" == 1 ]]; then
    rm -rf "$HOME/.vst3/VelCal.vst3"
    cp -R "$payload/VelCal.vst3" "$HOME/.vst3/"
fi
echo 'VelCal installed. Restart/rescan your DAW. Profiles are stored separately from application files.'
echo "Uninstall: bash '$app/uninstall.sh' --uninstall"

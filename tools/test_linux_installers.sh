#!/usr/bin/env bash
set -euo pipefail
workspace=$(cd "$(dirname "$0")/.." && pwd)
cd "$workspace"
sudo apt-get install -y ./build/installers/VelCal-*-Linux-x64.deb
test -x /usr/bin/VelCal
test -f /usr/lib/vst3/VelCal.vst3/Contents/x86_64-linux/VelCal.so
test -z "$(ldd /usr/bin/VelCal | grep 'not found' || true)"
test -z "$(ldd /usr/lib/vst3/VelCal.vst3/Contents/x86_64-linux/VelCal.so | grep 'not found' || true)"
set +e
VELCAL_DATA_DIR="$workspace/.tmp/deb-user-data" xvfb-run -a bash tools/run_linux_gui_check.sh timeout 8s /usr/bin/VelCal
result=$?
set -e
test "$result" -eq 124
mkdir -p "$HOME/.local/share/VelCal/profiles"
touch "$HOME/.local/share/VelCal/profiles/preserve.txt"
sudo apt-get remove -y velcal
test ! -f /usr/bin/VelCal
test -f "$HOME/.local/share/VelCal/profiles/preserve.txt"
bash build/installers/VelCal-*-Linux-x64-Install.run -- --standalone-only
test -x "$HOME/.local/lib/velcal/VelCal.AppImage"
test ! -d "$HOME/.vst3/VelCal.vst3"
bash build/installers/VelCal-*-Linux-x64-Install.run -- --vst3-only
test -d "$HOME/.vst3/VelCal.vst3"
set +e
VELCAL_DATA_DIR="$workspace/.tmp/run-user-data" xvfb-run -a bash tools/run_linux_gui_check.sh timeout 8s "$HOME/.local/lib/velcal/VelCal.AppImage" --appimage-extract-and-run
result=$?
set -e
test "$result" -eq 124
bash "$HOME/.local/lib/velcal/uninstall.sh" --uninstall
test ! -d "$HOME/.vst3/VelCal.vst3"
test -f "$HOME/.local/share/VelCal/profiles/preserve.txt"

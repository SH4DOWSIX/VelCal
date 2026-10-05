#!/bin/bash
set -euo pipefail
if [[ "$EUID" != 0 ]]; then exec sudo /bin/bash "$0"; fi
rm -rf /Applications/VelCal.app /Library/Audio/Plug-Ins/VST3/VelCal.vst3 /Library/Audio/Plug-Ins/Components/VelCal.component
for id in org.velcal.app org.velcal.vst3 org.velcal.au org.velcal.shared; do
    pkgutil --forget "$id" >/dev/null 2>&1 || true
done
rm -rf '/Library/Application Support/VelCal'
echo 'VelCal removed. User profiles and DAW project data have been preserved.'

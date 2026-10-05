#!/usr/bin/env bash
set -euo pipefail

: "${DISPLAY:?Run this helper inside xvfb-run or an X11 display}"
[[ "$#" -gt 0 ]] || { echo 'Expected a GUI check command.' >&2; exit 1; }
workspace=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$workspace/.tmp"

# Xvfb alone lacks window-manager atoms required by JUCE's desktop dialogs.
openbox --sm-disable > "$workspace/.tmp/openbox.log" 2>&1 &
wm_pid=$!
cleanup() {
    kill "$wm_pid" 2>/dev/null || true
    wait "$wm_pid" 2>/dev/null || true
}
trap cleanup EXIT

for ((attempt = 0; attempt < 100; ++attempt)); do
    if ! kill -0 "$wm_pid" 2>/dev/null; then
        echo 'Openbox exited before the GUI check.' >&2
        cat "$workspace/.tmp/openbox.log" >&2
        exit 1
    fi
    if [[ "$(xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null)" == *"window id # 0x"* ]]; then
        "$@"
        exit 0
    fi
    sleep 0.1
done

echo 'Timed out waiting for the X11 window manager.' >&2
cat "$workspace/.tmp/openbox.log" >&2
exit 1

#!/usr/bin/env bash
set -euo pipefail
app_dir=$(dirname "$(readlink -f "$0")")
exec "$app_dir/VelCal.AppImage" --appimage-extract-and-run "$@"

#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$workspace/${1:-build/ci}"
version=$(tr -d '\r\n' < "$build_dir/velcal-version.txt")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid release version' >&2; exit 1; }
output="$workspace/build/portable"
mkdir -p "$output" "$workspace/.deps" "$workspace/.tmp"

case "$(uname -s)" in
  Darwin)
    name="VelCal-$version-macOS-universal-portable"
    package="$output/$name"
    mkdir "$package"
    ditto "$build_dir/velcal_app_artefacts/Release/VelCal.app" "$package/VelCal.app"
    lipo "$package/VelCal.app/Contents/MacOS/VelCal" -verify_arch arm64 x86_64
    # Ad-hoc signing supports Apple Silicon but provides no developer identity/notarization.
    codesign --force --deep --sign - "$package/VelCal.app"
    codesign --verify --deep --strict "$package/VelCal.app"
    ;;
  Linux)
    name="VelCal-$version-Linux-x64-portable"
    package="$output/$name"
    mkdir "$package"
    app_dir="$build_dir/AppDir"
    DESTDIR="$app_dir" cmake --install "$build_dir"
    mkdir -p "$app_dir/usr/share/icons/hicolor/256x256/apps"
    convert "$workspace/resources/app-icon.png" -resize 256x256 \
      "$app_dir/usr/share/icons/hicolor/256x256/apps/org.velcal.app.png"
    cp "$app_dir/usr/share/icons/hicolor/256x256/apps/org.velcal.app.png" \
      "$app_dir/usr/share/pixmaps/org.velcal.app.png"
    desktop-file-validate "$workspace/resources/org.velcal.app.desktop"
    deploy="$workspace/.deps/linuxdeploy-x86_64.AppImage"
    curl -fL --retry 3 https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -o "$deploy"
    echo "8aea8da0f7f7039d2a2cecb14657d752a222a5e1d3825caeef186c82f751cdd1  $deploy" | sha256sum --check
    chmod +x "$deploy"
    export APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64
    export LDAI_OUTPUT="$package/VelCal.AppImage" LDAI_NO_APPSTREAM=1
    "$deploy" --appdir "$app_dir" \
      --executable "$app_dir/usr/bin/VelCal" \
      --desktop-file "$workspace/resources/org.velcal.app.desktop" \
      --icon-file "$app_dir/usr/share/icons/hicolor/256x256/apps/org.velcal.app.png" \
      --library /usr/lib/x86_64-linux-gnu/libasound.so.2 --output appimage
    chmod +x "$package/VelCal.AppImage"
    ;;
  *) echo 'Only Linux and macOS packaging is supported.' >&2; exit 1 ;;
esac

mkdir "$package/profiles" "$package/LICENSES"
cp "$workspace/LICENSE" "$package/LICENSE"
cp "$workspace/.deps/juce-src/LICENSE.md" "$package/LICENSES/JUCE.md"
cp "$workspace/.deps/nlohmann_json-src/LICENSE.MIT" "$package/LICENSES/nlohmann-json.txt"

if [[ "$(uname -s)" == Darwin ]]; then
  ditto -c -k --sequesterRsrc --keepParent "$package" "$output/$name.zip"
else
  cp /usr/share/doc/libasound2/copyright "$package/LICENSES/ALSA.txt"
  tar -czf "$output/$name.tar.gz" -C "$output" "$name"
fi
echo "Portable package: $package"

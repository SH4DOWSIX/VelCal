#!/usr/bin/env bash
set -euo pipefail
workspace=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$workspace/${1:-build/ci}"
version=$(tr -d '\r\n' < "$build_dir/velcal-version.txt")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || exit 1
output="$workspace/build/installers"
mkdir -p "$output" "$workspace/.tmp" "$workspace/.deps"
stage=$(mktemp -d "$build_dir/installer-stage.XXXXXX")
mkdir -p "$stage/LICENSES"
cp "$workspace/LICENSE" "$stage/LICENSE"
cp "$workspace/.deps/juce-src/LICENSE.md" "$stage/LICENSES/JUCE.md"
cp "$workspace/.deps/nlohmann_json-src/LICENSE.MIT" "$stage/LICENSES/nlohmann-json.txt"
cp "$workspace/.deps/juce-src/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt" "$stage/LICENSES/VST3-SDK.txt"

case "$(uname -s)" in
  Darwin)
    mkdir -p "$stage/app/Applications" "$stage/vst3/Library/Audio/Plug-Ins/VST3" "$stage/au/Library/Audio/Plug-Ins/Components" "$stage/components" "$stage/resources"
    shared="$stage/shared/Library/Application Support/VelCal"
    mkdir -p "$shared"
    ditto "$build_dir/velcal_app_artefacts/Release/VelCal.app" "$stage/app/Applications/VelCal.app"
    ditto "$build_dir/velcal_plugin_artefacts/Release/VST3/VelCal.vst3" "$stage/vst3/Library/Audio/Plug-Ins/VST3/VelCal.vst3"
    ditto "$build_dir/velcal_au_artefacts/Release/AU/VelCal.component" "$stage/au/Library/Audio/Plug-Ins/Components/VelCal.component"
    for bundle in "$stage/app/Applications/VelCal.app" "$stage/vst3/Library/Audio/Plug-Ins/VST3/VelCal.vst3" "$stage/au/Library/Audio/Plug-Ins/Components/VelCal.component"; do
        lipo "$bundle/Contents/MacOS/VelCal" -verify_arch arm64 x86_64
        codesign --force --deep --sign - "$bundle"
        codesign --verify --deep --strict "$bundle"
    done
    cp "$stage/LICENSE" "$shared/LICENSE"
    cp -R "$stage/LICENSES" "$shared/"
    cp "$workspace/installers/macos/uninstall.command" "$shared/"
    chmod +x "$shared/uninstall.command"
    cp "$stage/LICENSE" "$stage/resources/LICENSE.txt"
    for component in app vst3 au; do
        pkgbuild --analyze --root "$stage/$component" "$stage/$component.plist"
        index=0
        while /usr/libexec/PlistBuddy -c "Print :$index:RootRelativeBundlePath" "$stage/$component.plist" >/dev/null 2>&1; do
            /usr/libexec/PlistBuddy -c "Set :$index:BundleIsRelocatable false" "$stage/$component.plist" 2>/dev/null \
                || /usr/libexec/PlistBuddy -c "Add :$index:BundleIsRelocatable bool false" "$stage/$component.plist"
            index=$((index + 1))
        done
        pkgbuild --root "$stage/$component" --component-plist "$stage/$component.plist" --identifier "org.velcal.$component" --version "$version" --install-location / "$stage/components/$component.pkg"
    done
    pkgbuild --root "$stage/shared" --identifier org.velcal.shared --version "$version" --install-location / "$stage/components/shared.pkg"
    productbuild --distribution "$workspace/installers/macos/distribution.xml" --resources "$stage/resources" --package-path "$stage/components" "$output/VelCal-$version-macOS-universal.pkg"
    ;;
  Linux)
    deb="$stage/deb"
    DESTDIR="$deb" cmake --install "$build_dir"
    mkdir -p "$deb/usr/lib/vst3" "$deb/usr/share/doc/velcal" "$deb/DEBIAN" "$stage/debian"
    cp -R "$build_dir/velcal_plugin_artefacts/Release/VST3/VelCal.vst3" "$deb/usr/lib/vst3/"
    cp "$stage/LICENSE" "$deb/usr/share/doc/velcal/copyright"
    cp -R "$stage/LICENSES" "$deb/usr/share/doc/velcal/"
    printf 'Source: velcal\nSection: sound\nPriority: optional\nMaintainer: SH4DOWSIX <SH4DOWSIX@users.noreply.github.com>\nStandards-Version: 4.6.0\n\nPackage: velcal\nArchitecture: amd64\nDescription: MIDI velocity calibration\n' > "$stage/debian/control"
    dependencies=$(cd "$stage" && dpkg-shlibdeps -O -e"$deb/usr/bin/VelCal" -e"$deb/usr/lib/vst3/VelCal.vst3/Contents/x86_64-linux/VelCal.so" | sed -n 's/^shlibs:Depends=//p')
    [[ -n "$dependencies" ]]
    printf 'Package: velcal\nVersion: %s\nSection: sound\nPriority: optional\nArchitecture: amd64\nMaintainer: SH4DOWSIX <SH4DOWSIX@users.noreply.github.com>\nDepends: %s\nDescription: Per-key MIDI velocity calibration and VST3 instrument\n' "$version" "$dependencies" > "$deb/DEBIAN/control"
    dpkg-deb --build --root-owner-group "$deb" "$output/VelCal-$version-Linux-x64.deb"

    # Bundle standalone dependencies inside an AppImage, then install it rather than ship it loose.
    appdir="$stage/AppDir"
    DESTDIR="$appdir" cmake --install "$build_dir"
    mkdir -p "$appdir/usr/share/icons/hicolor/256x256/apps"
    convert "$workspace/resources/app-icon.png" -resize 256x256 "$appdir/usr/share/icons/hicolor/256x256/apps/org.velcal.app.png"
    cp "$appdir/usr/share/icons/hicolor/256x256/apps/org.velcal.app.png" "$appdir/usr/share/pixmaps/org.velcal.app.png"
    desktop-file-validate "$workspace/resources/org.velcal.app.desktop"
    deploy="$workspace/.deps/linuxdeploy-x86_64.AppImage"
    curl -fL --retry 3 https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -o "$deploy"
    echo "8aea8da0f7f7039d2a2cecb14657d752a222a5e1d3825caeef186c82f751cdd1  $deploy" | sha256sum --check
    chmod +x "$deploy"
    payload="$stage/payload"
    mkdir -p "$payload"
    export APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 LDAI_NO_APPSTREAM=1
    export LDAI_OUTPUT="$payload/VelCal.AppImage"
    "$deploy" --appdir "$appdir" --executable "$appdir/usr/bin/VelCal" --desktop-file "$workspace/resources/org.velcal.app.desktop" --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/org.velcal.app.png" --library /usr/lib/x86_64-linux-gnu/libasound.so.2 --output appimage
    cp -R "$build_dir/velcal_plugin_artefacts/Release/VST3/VelCal.vst3" "$payload/"
    cp "$stage/LICENSE" "$payload/"
    cp -R "$stage/LICENSES" "$payload/"
    cp /usr/share/doc/libasound2/copyright "$payload/LICENSES/ALSA.txt"
    cp /usr/share/doc/libbrotli1/copyright "$payload/LICENSES/Brotli.txt"
    cp /usr/share/doc/libpng16-16/copyright "$payload/LICENSES/libpng.txt"
    cp /usr/share/doc/makeself/copyright "$payload/LICENSES/makeself.txt"
    cp "$appdir/usr/share/icons/hicolor/256x256/apps/org.velcal.app.png" "$payload/"
    cp "$workspace/installers/linux/install.sh" "$payload/install.sh"
    cp "$workspace/installers/linux/launch.sh" "$payload/launch.sh"
    chmod +x "$payload/install.sh" "$payload/VelCal.AppImage"
    makeself --nox11 "$payload" "$output/VelCal-$version-Linux-x64-Install.run" "VelCal $version" ./install.sh
    ;;
  *) echo 'Unsupported packaging OS' >&2; exit 1 ;;
esac
echo "Installers: $output"

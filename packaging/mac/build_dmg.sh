#!/bin/bash
# Builds dist/SANDWAVE-<version>-macOS.dmg containing a .pkg installer for the VST3 and AU.
#
# Signing is optional:
#   MAC_APP_SIGN_IDENTITY        "Developer ID Application: ..." (default: ad-hoc "-")
#   MAC_INSTALLER_SIGN_IDENTITY  "Developer ID Installer: ..."   (default: unsigned installer)
set -euo pipefail
cd "$(dirname "$0")/../.."

VERSION="${VERSION:-$(sed -n 's/^project(Sandwave VERSION \([0-9.]*\)).*/\1/p' CMakeLists.txt)}"
ART="build/Sandwave_artefacts/Release"
WORK="build/pkgwork"
OUT="dist"
APP_ID="${MAC_APP_SIGN_IDENTITY:--}"

for f in "$ART/VST3/SANDWAVE.vst3" "$ART/AU/SANDWAVE.component"; do
    [ -d "$f" ] || { echo "missing $f - build the plugin first (cmake --build build)"; exit 1; }
done

rm -rf "$WORK"
mkdir -p "$WORK/root/Library/Audio/Plug-Ins/VST3" "$WORK/root/Library/Audio/Plug-Ins/Components" "$WORK/dmg" "$OUT"
cp -R "$ART/VST3/SANDWAVE.vst3"      "$WORK/root/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/SANDWAVE.component"   "$WORK/root/Library/Audio/Plug-Ins/Components/"

# drop extended attributes so no "._" AppleDouble files end up in the payload
xattr -cr "$WORK/root"
export COPYFILE_DISABLE=1

# sign the plugin bundles (hardened runtime, so they can be notarized with a real identity)
for b in "$WORK/root/Library/Audio/Plug-Ins/VST3/SANDWAVE.vst3" "$WORK/root/Library/Audio/Plug-Ins/Components/SANDWAVE.component"; do
    if [ "$APP_ID" = "-" ]; then
        codesign --force --deep --sign - "$b"
    else
        codesign --force --deep --options runtime --timestamp --sign "$APP_ID" "$b"
    fi
done

# component package; bundles must not be "relocated" to wherever an older copy happens to be
pkgbuild --analyze --root "$WORK/root" "$WORK/components.plist"
i=0
while /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/components.plist" 2>/dev/null; do i=$((i + 1)); done

pkgbuild --root "$WORK/root" \
         --component-plist "$WORK/components.plist" \
         --identifier com.uraniummusic.sandwave \
         --version "$VERSION" \
         --install-location / \
         --scripts packaging/mac/scripts \
         "$WORK/Sandwave.pkg"

sed "s/@VERSION@/$VERSION/g" packaging/mac/distribution.xml > "$WORK/distribution.xml"
INSTALLER="$WORK/dmg/SANDWAVE Installer.pkg"
productbuild --distribution "$WORK/distribution.xml" \
             --resources packaging/mac/resources \
             --package-path "$WORK" \
             "$WORK/unsigned.pkg"

if [ -n "${MAC_INSTALLER_SIGN_IDENTITY:-}" ]; then
    productsign --timestamp --sign "$MAC_INSTALLER_SIGN_IDENTITY" "$WORK/unsigned.pkg" "$INSTALLER"
else
    mv "$WORK/unsigned.pkg" "$INSTALLER"
fi

cp packaging/mac/ReadMe.txt "$WORK/dmg/Read Me.txt"

DMG="$OUT/SANDWAVE-$VERSION-macOS.dmg"
rm -f "$DMG"
hdiutil create -volname "SANDWAVE $VERSION" -srcfolder "$WORK/dmg" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null
echo "$DMG"

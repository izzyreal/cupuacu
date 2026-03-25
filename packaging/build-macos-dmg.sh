#!/bin/sh
set -eu

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "usage: $0 <app_bundle> <version> <output_dmg> [background_png]" >&2
    exit 1
fi

APP_BUNDLE=$1
VERSION=$2
OUTPUT_DMG=$3
BACKGROUND_SRC=${4:-resources/app-icons/macos/Cupuacu.iconset/icon_512x512@2x.png}

if [ ! -d "$APP_BUNDLE" ]; then
    echo "app bundle not found: $APP_BUNDLE" >&2
    exit 1
fi

if [ ! -f "$BACKGROUND_SRC" ]; then
    echo "background image not found: $BACKGROUND_SRC" >&2
    exit 1
fi

VOLUME_NAME="Cupuacu"
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/cupuacu-dmg.XXXXXX")
STAGING_DIR="$WORK_DIR/staging"
RW_DMG="$WORK_DIR/Cupuacu-${VERSION}.dmg"
FINAL_DMG=$(cd "$(dirname "$OUTPUT_DMG")" && pwd)/$(basename "$OUTPUT_DMG")

cleanup() {
    if [ "${MOUNT_DIR:-}" ] && [ -d "${MOUNT_DIR:-}" ]; then
        hdiutil detach "$MOUNT_DIR" >/dev/null 2>&1 || true
    fi
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT INT TERM

mkdir -p "$STAGING_DIR/.background"
cp -R "$APP_BUNDLE" "$STAGING_DIR/Cupuacu.app"
ln -s /Applications "$STAGING_DIR/Applications"
cp "$BACKGROUND_SRC" "$STAGING_DIR/.background/background.png"

hdiutil create -quiet -srcfolder "$STAGING_DIR" -volname "$VOLUME_NAME" \
    -fs HFS+ -format UDRW "$RW_DMG"

ATTACH_OUTPUT=$(hdiutil attach -quiet -readwrite -noverify -noautoopen "$RW_DMG")
DEVICE=$(printf '%s\n' "$ATTACH_OUTPUT" | awk '/^\/dev\// {print $1; exit}')
MOUNT_DIR=$(printf '%s\n' "$ATTACH_OUTPUT" | awk '/\/Volumes\// {print $3; exit}')

if [ -z "$DEVICE" ] || [ -z "$MOUNT_DIR" ]; then
    echo "failed to attach dmg" >&2
    exit 1
fi

chflags hidden "$MOUNT_DIR/.background" || true

osascript <<EOF
tell application "Finder"
    tell disk "$VOLUME_NAME"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {120, 120, 900, 560}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 128
        set text size of viewOptions to 14
        set background picture of viewOptions to file ".background:background.png"
        set position of item "Cupuacu.app" of container window to {190, 250}
        set position of item "Applications" of container window to {560, 250}
        update without registering applications
        delay 2
        close
    end tell
end tell
EOF

sync
hdiutil detach "$DEVICE" -quiet
MOUNT_DIR=""

mkdir -p "$(dirname "$FINAL_DMG")"
rm -f "$FINAL_DMG"
hdiutil convert "$RW_DMG" -quiet -format UDZO -imagekey zlib-level=9 -o "$FINAL_DMG"


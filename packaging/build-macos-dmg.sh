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
BACKGROUND_SCALE_PERCENT=${DMG_BACKGROUND_SCALE_PERCENT:-50}
BACKGROUND_OPACITY=${DMG_BACKGROUND_OPACITY:-0.5}

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
MOUNT_DIR="$WORK_DIR/mount"
BACKGROUND_OUT="$WORK_DIR/background.png"
FINAL_DMG=$(cd "$(dirname "$OUTPUT_DMG")" && pwd)/$(basename "$OUTPUT_DMG")

create_background_image() {
    if ! command -v swift >/dev/null 2>&1; then
        cp "$BACKGROUND_SRC" "$BACKGROUND_OUT"
        return
    fi

    if ! swift - "$BACKGROUND_SRC" "$BACKGROUND_OUT" \
        "$BACKGROUND_SCALE_PERCENT" "$BACKGROUND_OPACITY" <<'EOF'
import AppKit
import Foundation

let args = CommandLine.arguments
guard args.count == 5 else {
    fputs("unexpected swift arguments\n", stderr)
    exit(1)
}

let sourceURL = URL(fileURLWithPath: args[1])
let outputURL = URL(fileURLWithPath: args[2])
guard let scalePercent = Double(args[3]),
      scalePercent > 0.0,
      let opacityValue = Double(args[4]) else {
    fputs("invalid background scale/opacity\n", stderr)
    exit(1)
}

let opacity = max(0.0, min(1.0, opacityValue))

guard let sourceData = try? Data(contentsOf: sourceURL),
      let sourceRep = NSBitmapImageRep(data: sourceData),
      let sourceImage = NSImage(contentsOf: sourceURL) else {
    fputs("failed to load background image\n", stderr)
    exit(1)
}

let canvasWidth = sourceRep.pixelsWide
let canvasHeight = sourceRep.pixelsHigh
let scaledWidth = max(1, Int(round(Double(canvasWidth) * scalePercent / 100.0)))
let scaledHeight = max(1, Int(round(Double(canvasHeight) * scalePercent / 100.0)))

guard let outputRep = NSBitmapImageRep(
    bitmapDataPlanes: nil,
    pixelsWide: canvasWidth,
    pixelsHigh: canvasHeight,
    bitsPerSample: 8,
    samplesPerPixel: 4,
    hasAlpha: true,
    isPlanar: false,
    colorSpaceName: .deviceRGB,
    bytesPerRow: 0,
    bitsPerPixel: 0
) else {
    fputs("failed to allocate output bitmap\n", stderr)
    exit(1)
}

guard let context = NSGraphicsContext(bitmapImageRep: outputRep) else {
    fputs("failed to create drawing context\n", stderr)
    exit(1)
}

let drawRect = NSRect(
    x: (canvasWidth - scaledWidth) / 2,
    y: (canvasHeight - scaledHeight) / 2,
    width: scaledWidth,
    height: scaledHeight
)

NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = context
NSColor.clear.setFill()
NSRect(x: 0, y: 0, width: canvasWidth, height: canvasHeight).fill()
sourceImage.draw(
    in: drawRect,
    from: NSRect(x: 0, y: 0, width: sourceImage.size.width, height: sourceImage.size.height),
    operation: .sourceOver,
    fraction: opacity
)
context.flushGraphics()
NSGraphicsContext.restoreGraphicsState()

guard let pngData = outputRep.representation(using: .png, properties: [:]) else {
    fputs("failed to encode background image\n", stderr)
    exit(1)
}

do {
    try pngData.write(to: outputURL)
} catch {
    fputs("failed to write background image\n", stderr)
    exit(1)
}
EOF
    then
        echo "warning: background preprocessing failed; using original image" >&2
        cp "$BACKGROUND_SRC" "$BACKGROUND_OUT"
    fi
}

cleanup() {
    if [ "${MOUNT_DIR:-}" ] && [ -d "${MOUNT_DIR:-}" ]; then
        hdiutil detach "$MOUNT_DIR" >/dev/null 2>&1 || true
    fi
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT INT TERM

create_background_image

mkdir -p "$STAGING_DIR/.background"
cp -R "$APP_BUNDLE" "$STAGING_DIR/Cupuacu.app"
ln -s /Applications "$STAGING_DIR/Applications"
cp "$BACKGROUND_OUT" "$STAGING_DIR/.background/background.png"
mkdir -p "$MOUNT_DIR"

hdiutil create -quiet -srcfolder "$STAGING_DIR" -volname "$VOLUME_NAME" \
    -fs HFS+ -format UDRW "$RW_DMG"

ATTACH_OUTPUT=$(hdiutil attach -readwrite -noverify -noautoopen \
    -mountpoint "$MOUNT_DIR" "$RW_DMG")
DEVICE=$(printf '%s\n' "$ATTACH_OUTPUT" | awk '/^\/dev\// {print $1; exit}')

if [ -z "$DEVICE" ] || [ ! -d "$MOUNT_DIR" ]; then
    echo "failed to attach dmg" >&2
    exit 1
fi

chflags hidden "$MOUNT_DIR/.background" || true

if ! osascript <<EOF
set mountDir to POSIX file "$MOUNT_DIR" as alias
tell application "Finder"
    with timeout of 120 seconds
        tell folder mountDir
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
    end timeout
end tell
EOF
then
    echo "warning: Finder customization failed; continuing with default DMG layout" >&2
fi

sync
hdiutil detach "$DEVICE" -quiet
MOUNT_DIR=""

mkdir -p "$(dirname "$FINAL_DMG")"
rm -f "$FINAL_DMG"
hdiutil convert "$RW_DMG" -quiet -format UDZO -imagekey zlib-level=9 -o "$FINAL_DMG"

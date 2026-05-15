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

if ! command -v python3 >/dev/null 2>&1; then
    echo "python3 is required to build macOS DMGs" >&2
    exit 1
fi

if ! python3 -c "import dmgbuild" >/dev/null 2>&1; then
    echo "dmgbuild is required; install it with python3 -m pip install --user dmgbuild==1.6.7" >&2
    exit 1
fi

VOLUME_NAME="Cupuacu"
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/cupuacu-dmg.XXXXXX")
BACKGROUND_OUT="$WORK_DIR/background.png"
SETTINGS_FILE="$WORK_DIR/dmgbuild-settings.py"
FINAL_DMG=$(cd "$(dirname "$OUTPUT_DMG")" && pwd)/$(basename "$OUTPUT_DMG")
APP_BUNDLE_ABS=$(cd "$(dirname "$APP_BUNDLE")" && pwd)/$(basename "$APP_BUNDLE")

cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT INT TERM

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
let horizontalMargin = max(0, canvasWidth - scaledWidth)
let verticalMargin = max(0, canvasHeight - scaledHeight)
let shiftLeft = Int(round(Double(horizontalMargin) * 0.2))
let shiftUp = Int(round(Double(verticalMargin) * -0.5))

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
    x: max(0, horizontalMargin / 2 - shiftLeft),
    y: max(0, verticalMargin / 2 - shiftUp),
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

create_settings_file() {
    cat >"$SETTINGS_FILE" <<EOF
format = "UDZO"
filesystem = "HFS+"
files = [("${APP_BUNDLE_ABS}", "Cupuacu.app")]
symlinks = {"Applications": "/Applications"}
background = "${BACKGROUND_OUT}"
window_rect = ((120, 120), (780, 440))
show_toolbar = False
show_status_bar = False
show_tab_view = False
show_pathbar = False
show_sidebar = False
default_view = "icon-view"
arrange_by = None
icon_size = 128
text_size = 14
icon_locations = {
    "Cupuacu.app": (190, 190),
    "Applications": (560, 190),
}
EOF
}

create_background_image
create_settings_file

mkdir -p "$(dirname "$FINAL_DMG")"
rm -f "$FINAL_DMG"
python3 -m dmgbuild -s "$SETTINGS_FILE" "$VOLUME_NAME" "$FINAL_DMG"

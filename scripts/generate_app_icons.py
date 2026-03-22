#!/usr/bin/env python3

import argparse
import pathlib
import struct
import zlib


def read_bmp_rgba(path: pathlib.Path):
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError("Expected a BMP file")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError("Unsupported BMP header")

    width = struct.unpack_from("<i", data, 18)[0]
    height_signed = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    bits_per_pixel = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]

    if planes != 1 or bits_per_pixel != 32 or compression not in (0, 3):
        raise ValueError("Only uncompressed 32-bit BMP icons are supported")

    top_down = height_signed < 0
    height = abs(height_signed)
    width = abs(width)
    stride = width * 4
    pixels = []

    for row_index in range(height):
        source_row = row_index if top_down else (height - 1 - row_index)
        row_offset = pixel_offset + source_row * stride
        row = []
        for x in range(width):
            blue, green, red, alpha = struct.unpack_from(
                "<BBBB", data, row_offset + x * 4
            )
            row.append((red, green, blue, alpha))
        pixels.append(row)

    return width, height, pixels


def square_canvas(width, height, pixels):
    side = max(width, height)
    canvas = [[(0, 0, 0, 0) for _ in range(side)] for _ in range(side)]
    offset_x = (side - width) // 2
    offset_y = (side - height) // 2
    for y in range(height):
        for x in range(width):
            canvas[offset_y + y][offset_x + x] = pixels[y][x]
    return side, canvas


def scale_nearest(pixels, size):
    source_size = len(pixels)
    scaled = []
    for y in range(size):
        source_y = min(source_size - 1, (y * source_size) // size)
        row = []
        for x in range(size):
            source_x = min(source_size - 1, (x * source_size) // size)
            row.append(pixels[source_y][source_x])
        scaled.append(row)
    return scaled


def png_chunk(chunk_type: bytes, payload: bytes):
    return (
        struct.pack(">I", len(payload))
        + chunk_type
        + payload
        + struct.pack(">I", zlib.crc32(chunk_type + payload) & 0xFFFFFFFF)
    )


def write_png(path: pathlib.Path, pixels):
    height = len(pixels)
    width = len(pixels[0])
    raw_rows = bytearray()
    for row in pixels:
        raw_rows.append(0)
        for red, green, blue, alpha in row:
            raw_rows.extend((red, green, blue, alpha))

    png = bytearray(b"\x89PNG\r\n\x1a\n")
    png += png_chunk(
        b"IHDR",
        struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0),
    )
    png += png_chunk(b"IDAT", zlib.compress(bytes(raw_rows), level=9))
    png += png_chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def write_ico(path: pathlib.Path, png_paths):
    entries = []
    payload = bytearray()
    offset = 6 + 16 * len(png_paths)

    for png_path in png_paths:
        image = png_path.read_bytes()
        size = int(png_path.stem.split("_")[-1].split("x")[0])
        width_byte = 0 if size >= 256 else size
        height_byte = 0 if size >= 256 else size
        entries.append(
            struct.pack(
                "<BBBBHHII",
                width_byte,
                height_byte,
                0,
                0,
                1,
                32,
                len(image),
                offset,
            )
        )
        payload.extend(image)
        offset += len(image)

    header = struct.pack("<HHH", 0, 1, len(entries))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + b"".join(entries) + payload)


def generate_mac_iconset(output_dir: pathlib.Path, pixels):
    iconset_dir = output_dir / "macos" / "Cupuacu.iconset"
    sizes = {
        "icon_16x16.png": 16,
        "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32,
        "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128,
        "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256,
        "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512,
        "icon_512x512@2x.png": 1024,
    }
    for filename, size in sizes.items():
        write_png(iconset_dir / filename, scale_nearest(pixels, size))


def generate_windows_ico(output_dir: pathlib.Path, pixels):
    windows_dir = output_dir / "windows"
    png_sizes = [16, 24, 32, 48, 64, 128, 256]
    png_paths = []
    for size in png_sizes:
        png_path = windows_dir / f"icon_{size}x{size}.png"
        write_png(png_path, scale_nearest(pixels, size))
        png_paths.append(png_path)
    write_ico(windows_dir / "cupuacu.ico", png_paths)


def generate_linux_icon(output_dir: pathlib.Path, pixels):
    write_png(output_dir / "linux" / "cupuacu.png", scale_nearest(pixels, 256))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    output_dir = pathlib.Path(args.output_dir)

    width, height, pixels = read_bmp_rgba(input_path)
    _, square_pixels = square_canvas(width, height, pixels)
    generate_mac_iconset(output_dir, square_pixels)
    generate_windows_ico(output_dir, square_pixels)
    generate_linux_icon(output_dir, square_pixels)


if __name__ == "__main__":
    main()

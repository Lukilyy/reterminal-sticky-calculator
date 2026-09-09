#!/usr/bin/env python3
"""Generate compact 1-bit Inter glyph strikes for the iPhone display."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageFont


GLYPHS = " 0123456789,.-+x/_=%ACEFHOVehiOrstovy"
RENDER_AS = {"x": "×", "/": "÷", "_": "/", "-": "−"}
STRIKES = (
    ("ResultLarge", "light", 108),
    ("ResultMedium", "light", 82),
    ("ResultSmall", "light", 55),
    ("ResultCompact", "light", 28),
    ("Expression", "regular", 28),
    ("KeyNumber", "light", 58),
    ("KeyEmphasis", "regular", 45),
)


def emit_strike(name: str, font_path: Path, pixel_size: int):
    font = ImageFont.truetype(str(font_path), pixel_size)
    glyph_rows = []
    bitmap = bytearray()
    for code in GLYPHS:
        rendered = RENDER_AS.get(code, code)
        left, top, right, bottom = font.getbbox(rendered, anchor="ls")
        width = max(0, right - left)
        height = max(0, bottom - top)
        offset = len(bitmap)
        if width and height:
            image = Image.new("L", (width, height), 0)
            draw = __import__("PIL.ImageDraw", fromlist=["Draw"]).Draw(image)
            draw.text((-left, -top), rendered, font=font, fill=255,
                      anchor="ls")
            pixels = image.load()
            for y in range(height):
                byte = 0
                bits = 0
                for x in range(width):
                    byte = (byte << 1) | (1 if pixels[x, y] >= 128 else 0)
                    bits += 1
                    if bits == 8:
                        bitmap.append(byte)
                        byte = 0
                        bits = 0
                if bits:
                    bitmap.append(byte << (8 - bits))
        advance = round(font.getlength(rendered))
        glyph_rows.append((code, offset, width, height, left, top, advance))
    return name, pixel_size, glyph_rows, bitmap


def cpp_char(code: str) -> str:
    if code == " ":
        return "' '"
    if code == "'":
        return "'\\\''"
    return repr(code)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--light", required=True, type=Path)
    parser.add_argument("--regular", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)

    generated = []
    for name, weight, size in STRIKES:
        generated.append(emit_strike(
            name, args.light if weight == "light" else args.regular, size))

    header = """#pragma once

#include <cstddef>
#include <cstdint>

struct IphoneFontGlyph {
    char code;
    uint32_t bitmap_offset;
    uint16_t width;
    uint16_t height;
    int16_t bearing_x;
    int16_t bearing_y;
    uint16_t advance;
};

struct IphoneFontStrike {
    uint16_t pixel_size;
    const IphoneFontGlyph *glyphs;
    size_t glyph_count;
    const uint8_t *bitmap;
};

enum class IphoneFontSize : uint8_t {
    ResultLarge, ResultMedium, ResultSmall, ResultCompact, Expression,
    KeyNumber, KeyEmphasis,
};

const IphoneFontStrike &iphone_inter_font(IphoneFontSize size);
const IphoneFontGlyph *iphone_inter_glyph(const IphoneFontStrike &strike,
                                          char code);
"""
    (args.out_dir / "iphone_inter_font.h").write_text(header, encoding="utf-8")

    lines = ['#include "iphone_inter_font.h"', "", "namespace {"]
    for name, size, glyphs, bitmap in generated:
        lines.append(f"constexpr IphoneFontGlyph k{name}Glyphs[] = {{")
        for code, offset, width, height, bx, by, advance in glyphs:
            lines.append(
                f"    {{{cpp_char(code)}, {offset}, {width}, {height}, "
                f"{bx}, {by}, {advance}}},")
        lines.append("};")
        lines.append(f"constexpr uint8_t k{name}Bitmap[] = {{")
        for index in range(0, len(bitmap), 16):
            chunk = bitmap[index:index + 16]
            lines.append("    " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")
        lines.append("};")
        lines.append(
            f"constexpr IphoneFontStrike k{name} = "
            f"{{{size}, k{name}Glyphs, sizeof(k{name}Glyphs) / "
            f"sizeof(k{name}Glyphs[0]), k{name}Bitmap}};")
    lines += ["}  // namespace", "",
              "const IphoneFontStrike &iphone_inter_font(IphoneFontSize size)", "{",
              "    switch (size) {"]
    for name, _, _, _ in generated:
        lines.append(f"    case IphoneFontSize::{name}: return k{name};")
    lines += ["    }", "    return kResultCompact;", "}", "",
              "const IphoneFontGlyph *iphone_inter_glyph(",
              "    const IphoneFontStrike &strike, char code)", "{",
              "    for (size_t i = 0; i < strike.glyph_count; ++i) {",
              "        if (strike.glyphs[i].code == code) return &strike.glyphs[i];",
              "    }", "    return nullptr;", "}", ""]
    (args.out_dir / "iphone_inter_font.cpp").write_text(
        "\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()

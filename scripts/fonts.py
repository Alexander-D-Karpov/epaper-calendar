import os
import sys
import urllib.request

import freetype

SOURCES = {
    "Regular": "https://raw.githubusercontent.com/google/fonts/main/ofl/ptsans/PT_Sans-Web-Regular.ttf",
    "Bold": "https://raw.githubusercontent.com/google/fonts/main/ofl/ptsans/PT_Sans-Web-Bold.ttf",
}

FONTS = [
    ("FontS", "Regular", 13, None),
    ("FontSB", "Bold", 13, None),
    ("FontM", "Regular", 15, None),
    ("FontMB", "Bold", 15, None),
    ("FontL", "Bold", 18, None),
    ("FontXL", "Bold", 25, None),
    ("FontBig", "Bold", 40, "0123456789:"),
    ("FontHuge", "Bold", 62, "0123456789:"),
]

EXTRA = (
    [chr(c) for c in range(0x410, 0x450)]
    + ["Ё", "ё", "°", "·", "—", "–", "−", "…", "«", "»", "№", "↑", "↓", "✓"]
)


def charset():
    ascii_chars = [chr(c) for c in range(0x20, 0x7F)]
    return ascii_chars + ["\x7f"] + EXTRA


def fetch(style, cache_dir):
    os.makedirs(cache_dir, exist_ok=True)
    path = os.path.join(cache_dir, f"PTSans-{style}.ttf")
    if not os.path.exists(path):
        urllib.request.urlretrieve(SOURCES[style], path)
    return path


def render_glyph(face, ch):
    if ch == "\x7f" or face.get_char_index(ord(ch)) == 0:
        return [], 0, 0, 0, 0, 0
    face.load_char(ch, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_MONO)
    g = face.glyph
    bm = g.bitmap
    bits = []
    for y in range(bm.rows):
        for x in range(bm.width):
            byte = bm.buffer[y * bm.pitch + (x >> 3)]
            bits.append((byte >> (7 - (x & 7))) & 1)
    return bits, bm.width, bm.rows, g.advance.x >> 6, g.bitmap_left, -g.bitmap_top


def pack(bits):
    out = []
    for i in range(0, len(bits), 8):
        chunk = bits[i:i + 8] + [0] * (8 - len(bits[i:i + 8]))
        v = 0
        for b in chunk:
            v = (v << 1) | b
        out.append(v)
    return out


def build_font(name, path, px, only):
    face = freetype.Face(path)
    face.set_pixel_sizes(0, px)
    chars = charset()
    if only:
        last = max(chars.index(c) for c in only)
        chars = chars[:last + 1]
    bitmap = []
    glyphs = []
    for ch in chars:
        if only and ch not in only and ch != " ":
            glyphs.append((len(bitmap), 0, 0, 0, 0, 0))
            continue
        bits, w, h, adv, xo, yo = render_glyph(face, ch)
        data = pack(bits)
        glyphs.append((len(bitmap), w, h, adv, xo, yo))
        bitmap.extend(data)
    y_adv = face.size.height >> 6
    lines = [f"const uint8_t {name}Bitmaps[] PROGMEM = {{"]
    for i in range(0, len(bitmap), 24):
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in bitmap[i:i + 24]) + ",")
    if not bitmap:
        lines.append("  0x00,")
    lines.append("};")
    lines.append(f"const GFXglyph {name}Glyphs[] PROGMEM = {{")
    for g in glyphs:
        lines.append(f"  {{{g[0]}, {g[1]}, {g[2]}, {g[3]}, {g[4]}, {g[5]}}},")
    lines.append("};")
    lines.append(
        f"const GFXfont {name} PROGMEM = {{(uint8_t *){name}Bitmaps, (GFXglyph *){name}Glyphs, 0x20, {0x20 + len(chars) - 1}, {y_adv}}};"
    )
    return "\n".join(lines)


def build_map():
    chars = charset()
    pairs = sorted((ord(c), 0x20 + i) for i, c in enumerate(chars) if ord(c) > 0x7F)
    uni = ", ".join(f"0x{u:04X}" for u, _ in pairs)
    codes = ", ".join(f"0x{c:02X}" for _, c in pairs)
    return (
        f"const uint16_t kGlyphUnicode[] PROGMEM = {{{uni}}};\n"
        f"const uint8_t kGlyphCode[] PROGMEM = {{{codes}}};\n"
        f"const uint8_t kGlyphCount = {len(pairs)};"
    )


def generate(out_path, cache_dir):
    parts = ["#pragma once", "#include <Adafruit_GFX.h>", build_map()]
    for name, style, px, only in FONTS:
        parts.append(build_font(name, fetch(style, cache_dir), px, only))
    tmp = out_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write("\n\n".join(parts) + "\n")
    os.replace(tmp, out_path)


if __name__ == "__main__":
    generate(sys.argv[1], sys.argv[2])
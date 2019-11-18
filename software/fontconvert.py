import freetype
import zlib
import sys
from collections import namedtuple

GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top", "compressed_size", "data_offset"])

face = freetype.Face("/usr/share/fonts/TTF/FiraSans-Regular.ttf")
font_name = "FiraSans"
first = 32
last = 255

size = 24
# shift by 6 bytes, because sizes are given as 6-bit fractions
# the display has about 150 dpi.
face.set_char_size(size << 6, size << 6, 150, 150)

def chunks(l, n):
    for i in range(0, len(l), n):
        yield l[i:i + n]

def twopack(buf, width):
    new_buf = []
    while buf:
        line = buf[:width]
        buf = buf[width:]
        if len(line) % 2:
            line.append(0)
        while line:
            new_buf.append(line.pop(0) << 4 | line.pop(0))
    return new_buf

total_size = 0
total_packed = 0
all_glyphs = []
for i in range(first, last + 1):
    if i in range(0x7F, 0xA0):
        continue
    face.load_char(chr(i))
    bitmap = face.glyph.bitmap

    packed = bytes([255 - b for b in bitmap.buffer]);
    total_packed += len(packed)
    compressed = zlib.compress(packed)
    glyph = GlyphProps(
        width = bitmap.width,
        height = bitmap.rows,
        advance_x = face.glyph.advance.x >> 6,
        left = face.glyph.bitmap_left,
        top = face.glyph.bitmap_top,
        compressed_size = len(compressed),
        data_offset = total_size,
    )
    total_size += len(compressed)
    all_glyphs.append((glyph, compressed))

glyph_data = []
glyph_props = []
for index, glyph in enumerate(all_glyphs):
    props, compressed = glyph
    glyph_data.extend([b for b in compressed])
    glyph_props.append(props)

print("total", total_packed, file=sys.stderr)
print("compressed", total_size, file=sys.stderr)

print(f"const uint8_t {font_name}Bitmaps[{len(glyph_data)}] = {{")
for c in chunks(glyph_data, 16):
    print ("    " + " ".join(f"0x{b:02X}," for b in c))
print ("};");

print(f"const GFXglyph {font_name}Glyphs[] = {{")
for i, g in enumerate(glyph_props):
    print ("    { " + ", ".join([f"{a}" for a in list(g)]), f"}}, // {first + i}")
print ("};");

print(f"const GFXfont {font_name} = {{")
print(f"    (uint8_t*){font_name}Bitmaps,")
print(f"    (GFXglyph*){font_name}Glyphs,")
print(f"    {first},")
print(f"    {last},")
print(f"    {face.size.height >> 6},")
print("};")

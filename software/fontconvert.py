import freetype
import zlib
import sys
import re
from collections import namedtuple

GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top", "compressed_size", "data_offset", "code_point"])

font_stack = [
    freetype.Face("/usr/share/fonts/TTF/FiraSans-Regular.ttf"),
    freetype.Face("/usr/share/fonts/TTF/Symbola.ttf")
]
font_name = "FiraSans"

# inclusive unicode code point intervals
# must not overlap and be in ascending order
intervals = [
    (32, 126),
    (160, 255),
    (0x1F600, 0x1F680),
]

size = 24

for face in font_stack:
    # shift by 6 bytes, because sizes are given as 6-bit fractions
    # the display has about 150 dpi.
    face.set_char_size(size << 6, size << 6, 150, 150)

def chunks(l, n):
    for i in range(0, len(l), n):
        yield l[i:i + n]

total_size = 0
total_packed = 0
all_glyphs = []
for i_start, i_end in intervals:
    for code_point in range(i_start, i_end + 1):
        face_index = 0
        face = None
        while face_index < len(font_stack):
            face = font_stack[face_index]
            if face.get_char_index(code_point) > 0:
                face.load_char(chr(code_point))
                break
            face_index += 1
            print (f"falling back to font {face_index} for {chr(code_point)}.", file=sys.stderr)

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
            code_point = code_point,
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


print("#include \"font.h\"")
print(f"const uint8_t {font_name}Bitmaps[{len(glyph_data)}] = {{")
for c in chunks(glyph_data, 16):
    print ("    " + " ".join(f"0x{b:02X}," for b in c))
print ("};");

print(f"const GFXglyph {font_name}Glyphs[] = {{")
for i, g in enumerate(glyph_props):
    print ("    { " + ", ".join([f"{a}" for a in list(g[:-1])]),"},", f"// {chr(g.code_point) if g.code_point != 92 else '<backslash>'}")
print ("};");

print(f"const UnicodeInterval {font_name}Intervals[] = {{")
offset = 0
for i_start, i_end in intervals:
    print (f"    {{ 0x{i_start:X}, 0x{i_end:X}, 0x{offset:X} }},")
    offset += i_end - i_start + 1
print ("};");

print(f"const GFXfont {font_name} = {{")
print(f"    (uint8_t*){font_name}Bitmaps,")
print(f"    (GFXglyph*){font_name}Glyphs,")
print(f"    (UnicodeInterval*){font_name}Intervals,")
print(f"    {len(intervals)},")
print(f"    {face.size.height >> 6},")
print("};")

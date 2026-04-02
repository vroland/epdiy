#!python3

from PIL import Image
from argparse import ArgumentParser
import sys
import math

parser = ArgumentParser()
parser.add_argument('-i', action="store", dest="inputfile", required=True)
parser.add_argument('-n', action="store", dest="name", required=True)
parser.add_argument('-o', action="store", dest="outputfile", required=True)
parser.add_argument('-maxw', action="store", dest="max_width", default=1200, type=int)
parser.add_argument('-maxh', action="store", dest="max_height", default=825, type=int)
parser.add_argument('-nd', action="store_false", dest="dither", required=False, default=True)
parser.add_argument('-l', action="store", dest="levels", default=16, type=int)

args = parser.parse_args()

if args.levels > 16:
    raise Exception("Maximum 16 levels of gray supported by this program")

if args.levels < 2:
    # less than 2 doesn't make sense, there is at least on and off
    arg.levels = 2

if args.max_height < 1:
    raise Exception("Max height cannot be lower than 1")

if args.max_width < 1:
    raise Exception("Max width cannot be lower than 1")


im = Image.open(args.inputfile)
if im.mode == 'I;16': # some pngs load like this
    # so we fix it
    im = im.point(lambda i:i*(1./256)).convert('L')
im.thumbnail((args.max_width, args.max_height), Image.LANCZOS)
if im.mode != 'L':
    if im.mode != 'RGB':
        im = im.convert(mode='RGB') # drop alpha 
    # now we can convert it to grayscale to get rid of colors
    im = im.convert(mode='L') 

# but the quant algorithm works much better on RGB, so go to RGB mode just for that (even though we're still grayscale)
im = im.convert(mode='RGB')

# prepare a palette for the levels of gray
paletteImage = Image.new(mode='P', size=[1,1])
# we make a palette that matches the indexed colors of the display, just repeating the same value for rgb
paletteColors = [round(i * 255.0 / (args.levels - 1)) for i in range(0,args.levels) for c in range(0,3)]
paletteImage.putpalette(paletteColors, rawmode='RGB')
im = im.quantize(colors=args.levels, palette=paletteImage, dither=Image.Dither.FLOYDSTEINBERG if args.dither else Image.Dither.NONE)
# now it's quantizied to the palette, 0 == dark, args.levels-1 == white


# Write out the output file.
with open(args.outputfile, 'w') as f:
    f.write("const uint32_t {}_width = {};\n".format(args.name, im.size[0]))
    f.write("const uint32_t {}_height = {};\n".format(args.name, im.size[1]))
    f.write(
        "const uint8_t {}_data[({}*{})/2] = {{\n".format(args.name, math.ceil(im.size[0] / 2) * 2, im.size[1])
    )
    for y in range(0, im.size[1]):
        byte = 0
        for x in range(0, im.size[0]):
            l = im.getpixel((x, y))
            if x % 2 == 0:
                byte = l
            else:
                byte |= l << 4
                f.write("0x{:02X}, ".format(byte))
        if im.size[0] % 2 == 1:
            f.write("0x{:02X}, ".format(byte))
        f.write("\n\t")
    f.write("};\n")

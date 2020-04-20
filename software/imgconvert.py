#!python3

from PIL import Image, ImageOps
from argparse import ArgumentParser
import sys
import math

SCREEN_WIDTH = 1200
SCREEN_HEIGHT = 825

if SCREEN_WIDTH % 2:
    print("image width must be even!", file=sys.stderr)
    sys.exit(1)

parser = ArgumentParser()
parser.add_argument('-i', action="store", dest="inputfile")
parser.add_argument('-n', action="store", dest="name")
parser.add_argument('-o', action="store", dest="outputfile")

args = parser.parse_args()

im = Image.open(args.inputfile)
# convert to grayscale
im = im.convert(mode='L')
im.thumbnail((SCREEN_WIDTH, SCREEN_HEIGHT), Image.ANTIALIAS)

# Write out the output file.
with open(args.outputfile, 'w') as f:
    f.write("const uint32_t {}_width = {};\n".format(args.name, im.size[0]))
    f.write("const uint32_t {}_height = {};\n".format(args.name, im.size[1]))
    f.write(
        "const uint8_t {}_data[({}*{})/2] = {{\n".format(args.name, math.ceil(im.size[0] / 2) * 2, im.size[1])
    )
    for y in range(0, im.size[1]):
        byte = 0
        done = True
        for x in range(0, im.size[0]):
            l = im.getpixel((x, y))
            if x % 2 == 0:
                byte = l >> 4
                done = False;
            else:
                byte |= l & 0xF0
                f.write("0x{:02X}, ".format(byte))
                done = True
        if not done:
            f.write("0x{:02X}, ".format(byte))
        f.write("\n\t");
    f.write("};\n")

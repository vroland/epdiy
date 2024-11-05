#!/usr/bin/env python3
import argparse
import requests
from typing import NamedTuple
import PIL
import PIL.Image


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("hostname")
    subparsers = parser.add_subparsers(dest="command")
    clear_parser = subparsers.add_parser("clear")
    draw_parser = subparsers.add_parser("draw")
    draw_parser.add_argument("-c", "--clear", action="store_true")
    draw_parser.add_argument("file")
    info_praser = subparsers.add_parser("info")

    return parser.parse_args()


def clear(hostname):
    requests.post(f"http://{hostname}/clear").raise_for_status()


class EpdInfo(NamedTuple):
    width: int
    height: int
    temperature: int

    @classmethod
    def from_response(cls, resp):
        return cls(
            width=int(resp.headers["width"]),
            height=int(resp.headers["height"]),
            temperature=int(resp.headers["temperature"]),
        )


class Dimensions(NamedTuple):
    width: int
    height: int


def info(hostname):
    resp = requests.get(f"http://{hostname}")
    resp.raise_for_status()
    return EpdInfo.from_response(resp)


def image_refit(image: PIL.Image, bounder: Dimensions) -> PIL.Image:
    bounder_ratio = bounder.width / bounder.height
    image_width, image_height = image.size

    image_width_by_height = int(image_height * bounder_ratio)
    image_height_by_width = int(image_width / bounder_ratio)
    if image_width > image_width_by_height:
        new_dimensions = Dimensions(image_width_by_height, image_height)
    else:
        new_dimensions = Dimensions(image_width, image_height_by_width)
    return PIL.ImageOps.fit(image, new_dimensions)


def convert_8bit_to_4bit(bytestring):
    fourbit = []
    for i in range(0, len(bytestring), 2):
        first_nibble = int(bytestring[i] / 17)
        second_nibble = int(bytestring[i + 1] / 17)
        fourbit += [first_nibble << 4 | second_nibble]
    fourbit = bytes(fourbit)
    return fourbit


def draw(hostname, filename, clear):
    inf = info(hostname)
    img = PIL.Image.open(filename)
    img = image_refit(img, Dimensions(width=inf.width, height=inf.height))
    img = img.resize((inf.width, inf.height))
    img = img.convert("L")
    img_bytes = convert_8bit_to_4bit(img.tobytes())
    requests.post(
        f"http://{hostname}/draw",
        headers={
            "width": str(inf.width),
            "height": str(inf.height),
            "x": "0",
            "y": "0",
            "clear": "1" if clear else "0",
        },
        data=img_bytes,
    )


def main():
    args = parse_args()
    if args.command == "clear":
        clear(args.hostname)
    elif args.command == "info":
        print(info(args.hostname))
    elif args.command == "draw":
        draw(args.hostname, args.file, args.clear)
    else:
        raise Exception(f"Unknown command {args.command}")


if __name__ == "__main__":
    main()

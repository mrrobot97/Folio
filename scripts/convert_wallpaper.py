#!/usr/bin/env python3
"""Convert an image locally to a 684×1216 16-gray Folio wallpaper; never upload it."""
import argparse
from pathlib import Path
import struct
import zlib
from PIL import Image, ImageOps

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("input", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
with Image.open(args.input) as source:
    image = ImageOps.fit(ImageOps.exif_transpose(source).convert("L"), (684,1216), Image.Resampling.LANCZOS)
    values = bytes(min(15,(v+8)//17) for v in image.tobytes())
    pixels = bytes(values[i] | values[i+1]<<4 for i in range(0,len(values),2))
header = struct.pack("<4sHHII",b"FWP1",684,1216,len(pixels),zlib.crc32(pixels))
args.output.write_bytes(header+pixels)
print(f"{args.output}: {len(header)+len(pixels)} bytes; copy to TF /wallpapers/")

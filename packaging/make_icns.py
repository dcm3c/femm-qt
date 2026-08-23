#!/usr/bin/env python3
"""Rebuild gui/icons/femm.icns from gui/icons/femm.png.

    python3 packaging/make_icns.py

Run this if the application artwork ever changes; the .icns is a build product
of the .png and nothing regenerates it automatically. Needs Pillow, and
nothing else -- in particular it does NOT need macOS, so the icon can be
regenerated on whichever machine the artwork was edited on.

An .icns is a simple container: the magic 'icns', a big-endian total length,
then a run of chunks, each a 4-character type, a big-endian length that
INCLUDES its own 8-byte header, and the payload. Since OS X 10.7 the payload
for these types may be a PNG, which is what this writes -- so iconutil and
sips, both macOS-only, are not involved.

Sizes stop at 256 because that is the largest artwork there is. Upscaling to
512 or 1024 would only hand Finder a blurry image to prefer over a sharp
smaller one; leaving those types out lets it downscale the 256 instead.

Two halves are needed for an icon to appear and neither reports the other
missing: this file, in Contents/Resources (gui/CMakeLists.txt puts it there
via MACOSX_PACKAGE_LOCATION), and CFBundleIconFile naming it in
gui/Info.plist.in. Without both, Finder draws the blank placeholder and says
nothing about why.
"""
import io
import os
import struct
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("make_icns.py needs Pillow:  pip install pillow")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "gui", "icons", "femm.png")
OUT = os.path.join(ROOT, "gui", "icons", "femm.icns")

# (icns type, pixel size) -- the iconutil names are in the comments.
ENTRIES = [
    (b"icp4", 16),    # icon_16x16
    (b"ic11", 32),    # icon_16x16@2x
    (b"icp5", 32),    # icon_32x32
    (b"ic12", 64),    # icon_32x32@2x
    (b"ic07", 128),   # icon_128x128
    (b"ic13", 256),   # icon_128x128@2x
    (b"ic08", 256),   # icon_256x256
]


def main():
    src = Image.open(SRC).convert("RGBA")
    print("source: %s %s" % (os.path.relpath(SRC, ROOT), src.size))

    chunks = []
    for kind, size in ENTRIES:
        im = src if src.size == (size, size) else src.resize((size, size),
                                                             Image.LANCZOS)
        buf = io.BytesIO()
        im.save(buf, format="PNG")
        data = buf.getvalue()
        chunks.append(kind + struct.pack(">I", len(data) + 8) + data)
        print("  %-6s %4dx%-4d %6d bytes" % (kind.decode(), size, size,
                                             len(data)))

    body = b"".join(chunks)
    with open(OUT, "wb") as f:
        f.write(b"icns" + struct.pack(">I", len(body) + 8) + body)
    print("wrote %s" % os.path.relpath(OUT, ROOT))

    # Read it back with an independent parser to prove it is well formed.
    check = Image.open(OUT)
    print("verified: reads back as %s, %d sizes"
          % (check.size, len(check.info.get("sizes", []))))


if __name__ == "__main__":
    main()

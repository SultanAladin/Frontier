#!/usr/bin/env python3
"""Recompress the Phase 3 proof images in place.

Phase3ProofImages.cpp writes valid PNGs whose IDAT is a run of zlib "stored" (uncompressed) blocks,
which keeps the renderer free of any dependency — but a 1280x800 image then costs three megabytes.
This pass decodes those images, re-filters every scanline with the filter that suits it best, and
re-emits the IDAT with zlib at level 9. The pixels are untouched, so the two forms decode to the
same image byte for byte.

    python3 Validation/CompressProofImages.py [directory ...]        (default VisualProof/Phase3)
"""

import struct
import sys
import zlib


def _chunks(data):
    position = 8
    while position < len(data):
        (length,) = struct.unpack(">I", data[position:position + 4])
        kind = data[position + 4:position + 8]
        yield kind, data[position + 8:position + 8 + length]
        position += 12 + length


def _chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def _paeth(left, up, upper_left):
    estimate = left + up - upper_left
    distance_left = abs(estimate - left)
    distance_up = abs(estimate - up)
    distance_upper_left = abs(estimate - upper_left)
    if distance_left <= distance_up and distance_left <= distance_upper_left:
        return left
    if distance_up <= distance_upper_left:
        return up
    return upper_left


def _filter_row(raw, previous, stride):
    """Returns the best-filtered copy of one scanline: the filter byte plus the row."""
    candidates = []
    plain = bytearray(raw)
    candidates.append((0, bytes(plain)))

    sub = bytearray(stride)
    for index in range(stride):
        left = raw[index - 3] if index >= 3 else 0
        sub[index] = (raw[index] - left) & 0xFF
    candidates.append((1, bytes(sub)))

    up = bytearray(stride)
    for index in range(stride):
        up[index] = (raw[index] - previous[index]) & 0xFF
    candidates.append((2, bytes(up)))

    average = bytearray(stride)
    for index in range(stride):
        left = raw[index - 3] if index >= 3 else 0
        average[index] = (raw[index] - ((left + previous[index]) >> 1)) & 0xFF
    candidates.append((3, bytes(average)))

    paeth = bytearray(stride)
    for index in range(stride):
        left = raw[index - 3] if index >= 3 else 0
        upper_left = previous[index - 3] if index >= 3 else 0
        paeth[index] = (raw[index] - _paeth(left, previous[index], upper_left)) & 0xFF
    candidates.append((4, bytes(paeth)))

    # 📝 The usual heuristic: the filter whose residual is smallest in absolute terms compresses best.
    def cost(candidate):
        values = candidate[1]
        return sum(value if value < 128 else 256 - value for value in values)

    kind, row = min(candidates, key=cost)
    return bytes([kind]) + row


def _compress(path):
    with open(path, "rb") as handle:
        source = handle.read()
    if source[:8] != b"\x89PNG\r\n\x1a\n":
        return None

    header = None
    pixels = b""
    for kind, payload in _chunks(source):
        if kind == b"IHDR":
            header = payload
        elif kind == b"IDAT":
            pixels += payload
    if header is None:
        return None

    width, height, depth, colour = struct.unpack(">IIBB", header[:10])
    if depth != 8 or colour != 2:
        return None

    raw = zlib.decompress(pixels)
    stride = width * 3
    rows = [raw[row * (stride + 1) + 1: row * (stride + 1) + 1 + stride] for row in range(height)]

    filtered = bytearray()
    previous = bytearray(stride)
    for row in rows:
        filtered += _filter_row(row, previous, stride)
        previous = bytearray(row)

    out = bytearray(b"\x89PNG\r\n\x1a\n")
    out += _chunk(b"IHDR", header)
    out += _chunk(b"IDAT", zlib.compress(bytes(filtered), 9))
    out += _chunk(b"IEND", b"")

    with open(path, "wb") as handle:
        handle.write(bytes(out))

    before = len(source)
    after = len(out)
    return width, height, before, after


def main(argv):
    directories = argv[1:] or ["VisualProof/Phase3"]
    import glob
    import os

    total_before = 0
    total_after = 0
    for directory in directories:
        for path in sorted(glob.glob(os.path.join(directory, "*.png"))):
            result = _compress(path)
            if result is None:
                print(f"  skipped {path} (not an 8-bit truecolour PNG)")
                continue
            width, height, before, after = result
            total_before += before
            total_after += after
            print(f"  {os.path.basename(path):32s} {width}x{height}  {before/1048576:.2f} MB -> {after/1024:.0f} kB")
    if total_before:
        print(f"total {total_before/1048576:.1f} MB -> {total_after/1024:.0f} kB ({100.0*total_after/total_before:.1f} %)")


if __name__ == "__main__":
    main(sys.argv)

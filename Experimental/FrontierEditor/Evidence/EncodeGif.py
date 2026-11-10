#!/usr/bin/env python3
"""Encode WorkspaceEvidence PPM frames into delivery GIFs and PNG stills.

Usage: python3 EncodeGif.py <frames-dir> <output-dir>
Frames were sampled every 3rd simulated 60 Hz cycle, so playback is 20 fps (50 ms).
"""
import glob
import os
import sys

from PIL import Image

SEGMENTS = ["outliner-inspector", "docking-input", "notch-simulation"]
GIF_WIDTH = 900  # [px] - Delivery width; full-resolution stills carry the detail.
FRAME_STRIDE = 3  # [-] - Every 3rd sampled frame: ~6.7 fps playback at 150 ms per frame.


def load(path):
    image = Image.open(path)
    image.load()
    return image.convert("RGB")


def main():
    frames_dir, output_dir = sys.argv[1], sys.argv[2]
    os.makedirs(output_dir, exist_ok=True)
    for segment in SEGMENTS:
        paths = sorted(glob.glob(os.path.join(frames_dir, segment + "-*.ppm")))
        if not paths:
            print(f"segment {segment}: no frames, skipped")
            continue
        frames = []
        for path in paths[::FRAME_STRIDE]:
            image = load(path)
            scale = GIF_WIDTH / image.width
            image = image.resize((GIF_WIDTH, round(image.height * scale)), Image.LANCZOS)
            frames.append(image.convert("P", palette=Image.Palette.ADAPTIVE, colors=256))
        target = os.path.join(output_dir, segment + ".gif")
        frames[0].save(target, save_all=True, append_images=frames[1:], duration=50 * FRAME_STRIDE, loop=0, optimize=True)
        print(f"segment {segment}: {len(frames)} frames -> {target} ({os.path.getsize(target) // 1024} KiB)")
    for path in sorted(glob.glob(os.path.join(frames_dir, "still-*.ppm"))):
        target = os.path.join(output_dir, os.path.basename(path)[:-4] + ".png")
        load(path).save(target, optimize=True)
        print(f"still -> {target} ({os.path.getsize(target) // 1024} KiB)")


if __name__ == "__main__":
    main()

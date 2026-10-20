#!/usr/bin/env python3
"""Procedural surface-detail textures for shipped material presets.

Generates tileable normal + ORM maps for the golf-ball dimple presets and
albedo + ORM + normal maps for the photovoltaic-module presets. The
MaterialSurface shader samples these through the per-stratum TextureMask
(albedo = 1, ORM = 2, normal = 4); normals follow the preview geometry
convention (u right along +x pixels, v down along +y pixels, tangent =
normalize(-dh/du, -dh/dv, 1)) so dimples dent inward instead of bulging.

Regenerate with:  python Automation/GenerateSurfaceDetailTextures.py
Requires: numpy, Pillow.
"""
from __future__ import annotations

import math
import random
import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parent.parent
TEXTURES = REPOSITORY / "EngineContent/MaterialArchives/TextureStudio/Textures"
SIZE = 512


def height_to_normal(height, strength: float):
    """Tangent-space normal map from a periodic height field (v = image rows)."""
    import numpy as np

    gy, gx = ((np.roll(height, -1, axis) - np.roll(height, 1, axis)) * 0.5 for axis in (0, 1))
    normal = np.dstack((-gx * strength, -gy * strength, np.ones_like(height)))
    normal /= np.linalg.norm(normal, axis=2, keepdims=True)
    return ((normal * 0.5 + 0.5) * 255.0).round().astype("uint8")


def save(path: Path, array) -> None:
    from PIL import Image

    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(array, "RGBA").save(path)
    print(f"wrote {path.relative_to(REPOSITORY)}")


def golf_pair(stem: str, radius: float, depth: float, roughness: float, seed: int) -> None:
    """Hex-packed dimple field shared by normal + ORM outputs."""
    import numpy as np

    across, size = 10, SIZE
    spacing = size / across
    dimple_radius = spacing * radius
    rows = np.arange(size).reshape(-1, 1).astype(float)
    cols = np.arange(size).reshape(1, -1).astype(float)
    height = np.zeros((size, size))
    for row in range(across):
        cy = (row + 0.5) * spacing
        for col in range(across):
            cx = (col + 0.5 + (0.5 if row % 2 else 0.0)) * spacing % size
            dx = np.abs(cols - cx)
            dx = np.minimum(dx, size - dx)
            dy = np.abs(rows - cy)
            dy = np.minimum(dy, size - dy)
            distance = np.hypot(dx, dy)
            inside = distance < dimple_radius
            height += np.where(inside, depth * 0.5 * (1 + np.cos(math.pi * distance / dimple_radius)), 0.0)
    fraction = np.clip(height / depth, 0.0, 1.0)
    normal = np.dstack((height_to_normal(height, 2.6), np.full_like(fraction, 255, dtype="uint8")))
    occlusion = (1.0 - 0.38 * fraction) * 255.0
    # 📝 ORM green multiplies the authored roughness, so 255 preserves the base and dimples polish slightly smoother.
    rough = (1.0 - 0.08 * fraction) * 255.0
    orm = np.dstack(
        (
            occlusion.round(),
            rough.round(),
            np.zeros_like(occlusion),
            np.full_like(occlusion, 255.0),
        )
    ).astype("uint8")
    save(TEXTURES / f"{stem}-normal.png", normal)
    save(TEXTURES / f"{stem}-orm.png", orm)


def solar_module(stem: str, cell: tuple[int, int, int], busbar: tuple[int, int, int], jitter: float, seed: int) -> None:
    """8x8 silicon cells with busbars, collection fingers and separator gaps."""
    import numpy as np

    rng = random.Random(seed)
    size, cells, pitch = SIZE, 8, SIZE // 8
    gap, bar, finger_every = 2, 3, 8
    albedo = np.zeros((size, size, 4), dtype="uint8")
    albedo[:, :] = (10, 12, 14, 255)  # Separator gaps.
    height = np.full((size, size), -0.3)
    cell_tint = {}
    for cy in range(cells):
        for cx in range(cells):
            variation = 1.0 + rng.uniform(-jitter, jitter)
            tint = tuple(min(255, round(c * variation)) for c in cell)
            cell_tint[(cx, cy)] = tint
            x0, y0 = cx * pitch + gap, cy * pitch + gap
            x1, y1 = x0 + pitch - 2 * gap, y0 + pitch - 2 * gap
            albedo[y0:y1, x0:x1] = (*tint, 255)
            height[y0:y1, x0:x1] = 0.0
            for third in (1, 2):
                bx = x0 + (x1 - x0) * third // 3 - bar // 2
                albedo[y0:y1, bx : bx + bar] = (*busbar, 255)
                height[y0:y1, bx : bx + bar] = 0.5
            for fy in range(y0 + 4, y1, finger_every):
                albedo[fy, x0:x1] = (*busbar, 255)
                height[fy, x0:x1] = 0.35
    normal = np.dstack((height_to_normal(height, 1.6), np.full((size, size), 255, dtype="uint8")))
    is_bus = np.all(albedo[:, :, :3] == busbar, axis=2)
    is_gap = np.all(albedo[:, :, :3] == (10, 12, 14), axis=2)
    occlusion = np.where(is_gap, 200, 255)
    rough = np.where(is_bus, 110, 255)
    metal = np.where(is_bus, 255, np.where(is_gap, 0, 140))
    orm = np.dstack(
        (
            occlusion.astype("uint8"),
            rough.astype("uint8"),
            metal.astype("uint8"),
            np.full((size, size), 255, dtype="uint8"),
        )
    )
    save(TEXTURES / f"{stem}-albedo.png", albedo)
    # 📝 Cell layout is identical for every finish; ORM/normal are shared, only the albedo tint varies.
    save(TEXTURES / "solar-cells-orm.png", orm)
    save(TEXTURES / "solar-cells-normal.png", normal)


def main() -> int:
    try:
        import numpy  # noqa: F401
        from PIL import Image  # noqa: F401
    except ImportError:
        print("numpy and Pillow are required (pip install numpy Pillow).", file=sys.stderr)
        return 1
    del numpy, Image
    golf_pair("tour-golf", radius=0.40, depth=1.0, roughness=0.25, seed=7)
    golf_pair("practice-golf", radius=0.45, depth=1.25, roughness=0.30, seed=21)
    solar_module("solar-mono", cell=(0x17, 0x27, 0x42), busbar=(0xBE, 0xC6, 0xC4), jitter=0.03, seed=17)
    solar_module("solar-poly", cell=(0x25, 0x3F, 0x83), busbar=(0xBE, 0xC6, 0xC4), jitter=0.10, seed=34)
    solar_module("solar-black", cell=(0x10, 0x19, 0x1D), busbar=(0x3A, 0x40, 0x43), jitter=0.02, seed=51)
    return 0


if __name__ == "__main__":
    sys.exit(main())

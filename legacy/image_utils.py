from __future__ import annotations

from pathlib import Path

import numpy as np
from PIL import Image


MAX_DIMENSION = 512


def load_image(path: Path, max_dim: int = MAX_DIMENSION) -> tuple[np.ndarray, np.ndarray | None]:
    """Load an image and return (rgb HxWx3 uint8, alpha HxW uint8 or None).

    The image is downscaled so that its longest side is at most `max_dim` pixels.
    This keeps generation tractable on a CPU.
    """
    img = Image.open(path)
    if img.mode in ("RGBA", "LA") or "transparency" in img.info:
        img = img.convert("RGBA")
        has_alpha = True
    else:
        img = img.convert("RGB")
        has_alpha = False

    w, h = img.size
    longest = max(w, h)
    if longest > max_dim:
        scale = max_dim / longest
        new_size = (max(1, int(w * scale)), max(1, int(h * scale)))
        img = img.resize(new_size, Image.LANCZOS)

    arr = np.asarray(img)
    if has_alpha:
        rgb = arr[:, :, :3].copy()
        alpha = arr[:, :, 3].copy()
        if (alpha == 255).all():
            return rgb, None
        return rgb, alpha
    return arr.copy(), None


def has_transparency(path: Path) -> bool:
    """Cheap check: does the file have a non-trivial alpha channel?"""
    try:
        img = Image.open(path)
    except Exception:
        return False
    if img.mode in ("RGBA", "LA"):
        alpha = np.asarray(img.convert("RGBA"))[:, :, 3]
        return bool((alpha < 255).any())
    if "transparency" in img.info:
        return True
    return False


def compose_on_background(rgb: np.ndarray, alpha: np.ndarray, bg_rgb: tuple[int, int, int]) -> np.ndarray:
    """Composite the RGBA image onto a solid colour, returning a pure RGB array."""
    a = (alpha.astype(np.float32) / 255.0)[:, :, None]
    bg = np.array(bg_rgb, dtype=np.float32)
    fg = rgb.astype(np.float32)
    out = a * fg + (1.0 - a) * bg
    return np.clip(out, 0, 255).astype(np.uint8)

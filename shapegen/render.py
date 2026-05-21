from __future__ import annotations

import numpy as np

from shapegen.shapes import Shape


def _checkerboard(width: int, height: int, tile: int = 12) -> np.ndarray:
    yy, xx = np.indices((height, width))
    mask = ((xx // tile) + (yy // tile)) & 1
    canvas = np.empty((height, width, 3), dtype=np.uint8)
    canvas[mask == 0] = (208, 208, 208)
    canvas[mask == 1] = (160, 160, 160)
    return canvas


def render_shapes(
    shapes: list[Shape],
    width: int,
    height: int,
    background=(255, 255, 255),
    alpha_mask=None,
    xp=np,
):
    """Composite shapes onto a canvas. background can be a 3-tuple, "auto", or "checker".

    `xp` controls whether rasterisation happens on CPU (numpy, default) or GPU (cupy).
    The returned canvas lives in whichever array module is used; call shapegen.xp.to_cpu()
    before passing it to PIL or Tk.
    """
    if background == "checker":
        canvas = xp.asarray(_checkerboard(width, height))
    elif background == "auto":
        if alpha_mask is not None:
            canvas = xp.asarray(_checkerboard(width, height))
        else:
            canvas = xp.full((height, width, 3), 255, dtype=xp.uint8)
    else:
        canvas = xp.full((height, width, 3), background, dtype=xp.uint8)
    for s in shapes:
        mask_local, bbox = s.rasterize_mask(width, height, xp=xp)
        x0, y0, x1, y1 = bbox
        if x1 <= x0 or y1 <= y0 or mask_local.size == 0:
            continue
        if alpha_mask is not None:
            region_alpha = alpha_mask[y0:y1, x0:x1]
            mask_local = xp.minimum(mask_local, region_alpha)
        color = s.color
        a = (color[3] / 255.0) if len(color) >= 4 else 1.0
        region_cur = canvas[y0:y1, x0:x1].astype(xp.float32)
        src = xp.asarray(color[:3], dtype=xp.float32)
        m = (mask_local.astype(xp.float32) / 255.0)[:, :, None]
        blended = m * (a * src + (1.0 - a) * region_cur) + (1.0 - m) * region_cur
        canvas[y0:y1, x0:x1] = xp.clip(blended, 0, 255).astype(xp.uint8)
    return canvas

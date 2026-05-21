from __future__ import annotations

import math

import numpy as np

from shapegen.shapes.base import Shape


def rms_error(a, b, alpha_mask=None, xp=np) -> float:
    """RMS pixel error between two (H, W, 3) uint8 images. Lower is better.

    If `alpha_mask` (H, W) uint8 is given, only pixels where alpha>0 contribute (sticker mode).
    """
    diff = a.astype(xp.int32) - b.astype(xp.int32)
    sq = diff * diff
    if alpha_mask is None:
        return math.sqrt(float(sq.mean()))
    weight = (alpha_mask > 0)[:, :, None].astype(xp.float32)
    total = float((sq * weight).sum())
    n = float(weight.sum() * 3)
    if n < 1:
        return 0.0
    return math.sqrt(total / n)


def compute_optimal_color(
    target,
    current,
    mask_local,
    bbox: tuple[int, int, int, int],
    alpha: int,
    xp=np,
) -> tuple[int, int, int, int]:
    """Closed-form optimal RGB for `over` compositing at fixed alpha."""
    x0, y0, x1, y1 = bbox
    if x1 <= x0 or y1 <= y0 or mask_local.size == 0:
        return (0, 0, 0, alpha)
    tgt = target[y0:y1, x0:x1].astype(xp.float32)
    cur = current[y0:y1, x0:x1].astype(xp.float32)
    m = mask_local.astype(xp.float32) / 255.0
    weight = m.sum()
    if float(weight) < 0.5:
        return (0, 0, 0, alpha)
    a = alpha / 255.0
    if a < 1e-6:
        return (0, 0, 0, alpha)
    src = (tgt - (1.0 - a) * cur) / a
    src_masked = src * m[:, :, None]
    avg = src_masked.reshape(-1, 3).sum(axis=0) / weight
    avg = xp.clip(avg, 0, 255).astype(xp.int32)
    return (int(avg[0]), int(avg[1]), int(avg[2]), alpha)


def composite(
    current,
    shape: Shape,
    target,
    alpha_mask=None,
    xp=np,
):
    """Composite shape over canvas with optimal color. Returns (new_canvas, new_rms)."""
    h, w = current.shape[:2]
    mask_local, bbox = shape.rasterize_mask(w, h, xp=xp)
    x0, y0, x1, y1 = bbox
    if x1 <= x0 or y1 <= y0 or mask_local.size == 0:
        return current, rms_error(current, target, alpha_mask, xp=xp)
    if alpha_mask is not None:
        region_alpha = alpha_mask[y0:y1, x0:x1]
        effective_mask = xp.minimum(mask_local, region_alpha)
    else:
        effective_mask = mask_local
    color = compute_optimal_color(target, current, effective_mask, bbox, shape.color[3], xp=xp)
    new = current.copy()
    a = color[3] / 255.0
    region_cur = new[y0:y1, x0:x1].astype(xp.float32)
    region_tgt_color = xp.asarray(color[:3], dtype=xp.float32)
    m = (effective_mask.astype(xp.float32) / 255.0)[:, :, None]
    blended = m * (a * region_tgt_color + (1.0 - a) * region_cur) + (1.0 - m) * region_cur
    new[y0:y1, x0:x1] = xp.clip(blended, 0, 255).astype(xp.uint8)
    shape.color = color
    return new, rms_error(new, target, alpha_mask, xp=xp)


STICKER_OVERLAP_MIN = 0.995


def precompute_baseline(current, target, alpha_mask=None, xp=np) -> tuple[float, float]:
    """Pre-compute (diff_out_squared_sum, n_norm) for the current canvas.

    score_shape() recomputes this O(H×W) quantity on every call ; when scoring
    N candidates against the same canvas (e.g. random search), we precompute
    once and pass it via the `baseline` argument to drop a factor N from the
    scoring cost. The pair (sum_sq, n_norm) is enough — score_shape derives
    its delta from the per-candidate region only.

    For sticker mode (alpha_mask given), the squared sum is weighted by the
    alpha mask so transparent pixels don't contribute.
    """
    if alpha_mask is None:
        diff = current.astype(xp.int32) - target.astype(xp.int32)
        diff_out_squared_sum = float((diff * diff).sum())
        n_norm = float(current.shape[0] * current.shape[1] * 3)
        return diff_out_squared_sum, n_norm
    weight_full = (alpha_mask > 0)[:, :, None].astype(xp.float32)
    diff = current.astype(xp.float32) - target.astype(xp.float32)
    diff_out_squared_sum = float(((diff * diff) * weight_full).sum())
    n_norm = float(weight_full.sum() * 3)
    return diff_out_squared_sum, n_norm


def score_shape(
    shape: Shape,
    current,
    target,
    alpha_mask=None,
    xp=np,
    baseline: tuple[float, float] | None = None,
) -> tuple[float, tuple[int, int, int, int]]:
    """Score a candidate. Returns (rms_if_committed, optimal_color).

    Sticker mode: a shape must sit essentially entirely inside the opaque region
    (≥ STICKER_OVERLAP_MIN of body pixels) or it's rejected with +inf.

    When scoring many candidates against the same canvas, pass `baseline`
    (from precompute_baseline()) to skip the O(H×W) global sum on each call.
    """
    h, w = current.shape[:2]
    mask_local, bbox = shape.rasterize_mask(w, h, xp=xp)
    x0, y0, x1, y1 = bbox
    if x1 <= x0 or y1 <= y0 or mask_local.size == 0:
        return float("inf"), shape.color
    effective_mask = mask_local
    if alpha_mask is not None:
        region_alpha = alpha_mask[y0:y1, x0:x1]
        shape_body = mask_local >= 128
        body_total = float(shape_body.sum())
        if body_total < 1.0:
            return float("inf"), shape.color
        opaque_body = region_alpha >= 128
        if not bool(opaque_body.any()):
            return float("inf"), shape.color
        inside = float((shape_body & opaque_body).sum())
        if inside / body_total < STICKER_OVERLAP_MIN:
            return float("inf"), shape.color
        effective_mask = xp.minimum(mask_local, region_alpha)
    color = compute_optimal_color(target, current, effective_mask, bbox, shape.color[3], xp=xp)
    a = color[3] / 255.0
    region_cur = current[y0:y1, x0:x1].astype(xp.float32)
    region_tgt = target[y0:y1, x0:x1].astype(xp.float32)
    src = xp.asarray(color[:3], dtype=xp.float32)
    m = (mask_local.astype(xp.float32) / 255.0)[:, :, None]
    blended = m * (a * src + (1.0 - a) * region_cur) + (1.0 - m) * region_cur
    diff_in = blended - region_tgt
    if alpha_mask is None:
        if baseline is None:
            diff_out_squared_sum = float(((current.astype(xp.int32) - target.astype(xp.int32)) ** 2).sum())
            n_norm = float(current.shape[0] * current.shape[1] * 3)
        else:
            diff_out_squared_sum, n_norm = baseline
        region_old_sq = float(((region_cur - region_tgt) ** 2).sum())
        region_new_sq = float((diff_in ** 2).sum())
        total_sq = diff_out_squared_sum - region_old_sq + region_new_sq
        return math.sqrt(max(0.0, total_sq) / n_norm), color
    weight_full = (alpha_mask > 0)[:, :, None].astype(xp.float32)
    weight_region = weight_full[y0:y1, x0:x1]
    if baseline is None:
        diff_out_sq = ((current.astype(xp.float32) - target.astype(xp.float32)) ** 2) * weight_full
        diff_out_squared_sum = float(diff_out_sq.sum())
        n = float(weight_full.sum() * 3)
    else:
        diff_out_squared_sum, n = baseline
    region_old_sq = float((((region_cur - region_tgt) ** 2) * weight_region).sum())
    region_new_sq = float(((diff_in ** 2) * weight_region).sum())
    total_sq = diff_out_squared_sum - region_old_sq + region_new_sq
    if n < 1:
        return 0.0, color
    return math.sqrt(max(0.0, total_sq) / n), color

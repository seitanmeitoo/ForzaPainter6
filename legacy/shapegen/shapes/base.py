from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import ClassVar
import random

import numpy as np

ShapeType = str

SHAPE_REGISTRY: dict[ShapeType, type["Shape"]] = {}


def _register(cls: type["Shape"]) -> type["Shape"]:
    SHAPE_REGISTRY[cls.type_name] = cls
    return cls


@dataclass
class Shape(ABC):
    """Abstract shape primitive. Color (RGBA 0-255) is tracked separately from geometry."""

    type_name: ClassVar[ShapeType] = "shape"

    color: tuple[int, int, int, int] = (0, 0, 0, 128)

    @abstractmethod
    def bbox(self, w: int, h: int) -> tuple[int, int, int, int]:
        ...

    @abstractmethod
    def rasterize_mask(self, w: int, h: int, xp=np) -> tuple["np.ndarray", tuple[int, int, int, int]]:
        ...

    @abstractmethod
    def mutate(self, rng: random.Random, w: int, h: int) -> "Shape":
        ...

    @abstractmethod
    def to_json(self) -> dict:
        ...

    @classmethod
    @abstractmethod
    def from_json(cls, data: dict) -> "Shape":
        ...

    @classmethod
    @abstractmethod
    def random(cls, rng: random.Random, w: int, h: int) -> "Shape":
        ...

    def with_color(self, color: tuple[int, int, int, int]) -> "Shape":
        from copy import copy as shallow_copy
        new = shallow_copy(self)
        new.color = color
        return new


def random_shape(
    rng: random.Random,
    w: int,
    h: int,
    allowed_types: list[ShapeType],
    size_scale: float = 1.0,
    alpha: int = 128,
) -> Shape:
    type_name = rng.choice(allowed_types)
    cls = SHAPE_REGISTRY[type_name]
    return cls.random(rng, w, h, size_scale=size_scale, alpha=alpha)


def random_shape_batch(
    rng: random.Random,
    w: int,
    h: int,
    allowed_types: list[ShapeType],
    n: int,
    size_scale: float,
    alpha: int,
    xp=np,
):
    """Répartit N candidats sur les types autorisés et renvoie:
      - batches: list[(cls, params_xp, colors_xp)] — params shape (n_i, K_dim), colors (n_i, 4)
      - type_offsets: list[int] de longueur len(batches)+1, où batches[i] couvre
                      les indices [type_offsets[i], type_offsets[i+1]) dans le batch concaténé.

    Reproduit la distribution rng.choice du chemin séquentiel : les comptes par
    type sont random.Random.choices() — pas une répartition strictement uniforme.
    """
    if n <= 0 or not allowed_types:
        return [], [0]
    type_counts: dict[str, int] = {t: 0 for t in allowed_types}
    for _ in range(n):
        type_counts[rng.choice(allowed_types)] += 1
    batches = []
    offsets = [0]
    for t in allowed_types:
        cnt = type_counts[t]
        if cnt == 0:
            continue
        cls = SHAPE_REGISTRY[t]
        params, colors = cls.random_batch(rng, w, h, cnt, size_scale, alpha, xp=xp)
        batches.append((cls, params, colors))
        offsets.append(offsets[-1] + cnt)
    return batches, offsets


def rasterize_batch_multi(batches, w: int, h: int, xp=np):
    """Rasterise chaque sous-batch (un par type) et concatène les masks
    en (N_total, H, W) uint8."""
    if not batches:
        return xp.zeros((0, h, w), dtype=xp.uint8)
    parts = []
    for cls, params, _ in batches:
        parts.append(cls.rasterize_batch(params, w, h, xp=xp))
    if len(parts) == 1:
        return parts[0]
    return xp.concatenate(parts, axis=0)


def concat_colors(batches, xp=np):
    """Concatène les couleurs des sous-batches en (N_total, 4) uint8."""
    if not batches:
        return xp.zeros((0, 4), dtype=xp.uint8)
    cs = [colors for _, _, colors in batches]
    if len(cs) == 1:
        return cs[0]
    return xp.concatenate(cs, axis=0)


def locate_batch_index(batches, type_offsets, idx: int):
    """À partir d'un index global dans le batch concaténé, renvoie
    (cls, params_row_xp, color_row_xp) — le row vit toujours sur xp."""
    for i, (cls, params, colors) in enumerate(batches):
        start = type_offsets[i]
        end = type_offsets[i + 1]
        if start <= idx < end:
            local = idx - start
            return cls, params[local], colors[local]
    raise IndexError(f"idx {idx} out of range for batches of total {type_offsets[-1]}")


def shape_from_json(data: dict) -> Shape:
    type_name = data.get("type")
    if type_name not in SHAPE_REGISTRY:
        raise ValueError(f"Unknown shape type: {type_name!r}")
    return SHAPE_REGISTRY[type_name].from_json(data)


def _clip_bbox(x0: float, y0: float, x1: float, y1: float, w: int, h: int) -> tuple[int, int, int, int]:
    cx0 = max(0, int(np.floor(x0)))
    cy0 = max(0, int(np.floor(y0)))
    cx1 = min(w, int(np.ceil(x1)))
    cy1 = min(h, int(np.ceil(y1)))
    if cx1 <= cx0 or cy1 <= cy0:
        return (0, 0, 0, 0)
    return cx0, cy0, cx1, cy1


def _clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))

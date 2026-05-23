from __future__ import annotations

from dataclasses import dataclass
import random

import numpy as np

from legacy.shapegen.shapes.base import Shape, _clip_bbox, _clamp, _register


@dataclass
class Circle(Shape):
    type_name = "circle"

    x: float = 0.0
    y: float = 0.0
    r: float = 1.0

    def bbox(self, w: int, h: int) -> tuple[int, int, int, int]:
        return _clip_bbox(self.x - self.r, self.y - self.r, self.x + self.r + 1, self.y + self.r + 1, w, h)

    def rasterize_mask(self, w: int, h: int, xp=np) -> tuple[np.ndarray, tuple[int, int, int, int]]:
        bbox = self.bbox(w, h)
        x0, y0, x1, y1 = bbox
        if x1 <= x0 or y1 <= y0:
            return xp.zeros((0, 0), dtype=xp.uint8), bbox
        ys = xp.arange(y0, y1, dtype=xp.float32) - self.y
        xs = xp.arange(x0, x1, dtype=xp.float32) - self.x
        r2 = max(self.r, 1e-6) ** 2
        mask = (xs[None, :] ** 2 + ys[:, None] ** 2) <= r2
        return (mask.astype(xp.uint8) * 255), bbox

    def mutate(self, rng: random.Random, w: int, h: int) -> "Circle":
        from copy import copy as shallow_copy
        new = shallow_copy(self)
        which = rng.randint(0, 1)
        if which == 0:
            new.x = _clamp(new.x + rng.gauss(0, 16), 0, w - 1)
            new.y = _clamp(new.y + rng.gauss(0, 16), 0, h - 1)
        else:
            new.r = _clamp(new.r + rng.gauss(0, 16), 1, max(w, h))
        return new

    def to_json(self) -> dict:
        return {
            "type": self.type_name,
            "x": round(self.x, 3), "y": round(self.y, 3),
            "r": round(self.r, 3),
            "color": list(self.color),
        }

    @classmethod
    def from_json(cls, data: dict) -> "Circle":
        return cls(
            color=tuple(data["color"]),
            x=float(data["x"]), y=float(data["y"]),
            r=float(data["r"]),
        )

    @classmethod
    def random(cls, rng: random.Random, w: int, h: int, size_scale: float = 1.0, alpha: int = 128) -> "Circle":
        return cls(
            color=(rng.randint(0, 255), rng.randint(0, 255), rng.randint(0, 255), alpha),
            x=rng.uniform(0, w - 1), y=rng.uniform(0, h - 1),
            r=rng.uniform(1, max(2, min(w, h) / 8) * size_scale),
        )

    PARAM_DIM = 3  # [x, y, r]

    @classmethod
    def random_batch(cls, rng: random.Random, w: int, h: int, n: int,
                     size_scale: float, alpha: int, xp=np):
        r_max = max(2.0, min(w, h) / 8.0) * size_scale
        p = np.empty((n, 3), dtype=np.float32)
        c = np.empty((n, 4), dtype=np.uint8)
        for i in range(n):
            p[i, 0] = rng.uniform(0, w - 1)
            p[i, 1] = rng.uniform(0, h - 1)
            p[i, 2] = rng.uniform(1, r_max)
            c[i, 0] = rng.randint(0, 255)
            c[i, 1] = rng.randint(0, 255)
            c[i, 2] = rng.randint(0, 255)
            c[i, 3] = alpha
        return xp.asarray(p), xp.asarray(c)

    @classmethod
    def rasterize_batch(cls, params, w: int, h: int, xp=np):
        x = params[:, 0:1, None]
        y = params[:, 1:2, None]
        r = params[:, 2:3, None]
        xs_grid = xp.arange(w, dtype=xp.float32)[None, None, :]
        ys_grid = xp.arange(h, dtype=xp.float32)[None, :, None]
        dx = xs_grid - x
        dy = ys_grid - y
        r_safe = xp.maximum(r, 1e-6)
        mask = (dx * dx + dy * dy) <= (r_safe * r_safe)
        return mask.astype(xp.uint8) * 255

    @classmethod
    def mutate_batch(cls, best_params, rng: random.Random, w: int, h: int, k: int, xp=np):
        bp = np.asarray(best_params, dtype=np.float32).reshape(-1)
        out = np.tile(bp, (k, 1))
        r_lim = float(max(w, h))
        for i in range(k):
            which = rng.randint(0, 1)
            if which == 0:
                out[i, 0] = _clamp(out[i, 0] + rng.gauss(0, 16), 0, w - 1)
                out[i, 1] = _clamp(out[i, 1] + rng.gauss(0, 16), 0, h - 1)
            else:
                out[i, 2] = _clamp(out[i, 2] + rng.gauss(0, 16), 1, r_lim)
        return xp.asarray(out)

    @classmethod
    def params_to_instance(cls, params_row, color) -> "Circle":
        return cls(
            color=tuple(int(c) for c in color),
            x=float(params_row[0]), y=float(params_row[1]),
            r=float(params_row[2]),
        )

    def to_params_row(self) -> np.ndarray:
        return np.asarray([self.x, self.y, self.r], dtype=np.float32)


_register(Circle)

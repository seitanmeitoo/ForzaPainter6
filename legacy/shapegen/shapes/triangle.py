from __future__ import annotations

from dataclasses import dataclass
import random

import numpy as np

from legacy.shapegen.shapes.base import Shape, _clip_bbox, _clamp, _register


@dataclass
class Triangle(Shape):
    type_name = "triangle"

    x1: float = 0.0
    y1: float = 0.0
    x2: float = 0.0
    y2: float = 0.0
    x3: float = 0.0
    y3: float = 0.0

    def bbox(self, w: int, h: int) -> tuple[int, int, int, int]:
        xs = (self.x1, self.x2, self.x3)
        ys = (self.y1, self.y2, self.y3)
        return _clip_bbox(min(xs), min(ys), max(xs) + 1, max(ys) + 1, w, h)

    def rasterize_mask(self, w: int, h: int, xp=np) -> tuple[np.ndarray, tuple[int, int, int, int]]:
        bbox = self.bbox(w, h)
        x0, y0, x1, y1 = bbox
        if x1 <= x0 or y1 <= y0:
            return xp.zeros((0, 0), dtype=xp.uint8), bbox
        ys = xp.arange(y0, y1, dtype=xp.float32)
        xs = xp.arange(x0, x1, dtype=xp.float32)
        xg, yg = xp.meshgrid(xs, ys)
        def edge(ax, ay, bx, by, px, py):
            return (bx - ax) * (py - ay) - (by - ay) * (px - ax)
        d1 = edge(self.x1, self.y1, self.x2, self.y2, xg, yg)
        d2 = edge(self.x2, self.y2, self.x3, self.y3, xg, yg)
        d3 = edge(self.x3, self.y3, self.x1, self.y1, xg, yg)
        has_neg = (d1 < 0) | (d2 < 0) | (d3 < 0)
        has_pos = (d1 > 0) | (d2 > 0) | (d3 > 0)
        mask = ~(has_neg & has_pos)
        return (mask.astype(xp.uint8) * 255), bbox

    def mutate(self, rng: random.Random, w: int, h: int) -> "Triangle":
        from copy import copy as shallow_copy
        new = shallow_copy(self)
        vertex = rng.randint(0, 2)
        dx, dy = rng.gauss(0, 16), rng.gauss(0, 16)
        if vertex == 0:
            new.x1 = _clamp(new.x1 + dx, 0, w - 1)
            new.y1 = _clamp(new.y1 + dy, 0, h - 1)
        elif vertex == 1:
            new.x2 = _clamp(new.x2 + dx, 0, w - 1)
            new.y2 = _clamp(new.y2 + dy, 0, h - 1)
        else:
            new.x3 = _clamp(new.x3 + dx, 0, w - 1)
            new.y3 = _clamp(new.y3 + dy, 0, h - 1)
        return new

    def to_json(self) -> dict:
        return {
            "type": self.type_name,
            "x1": round(self.x1, 3), "y1": round(self.y1, 3),
            "x2": round(self.x2, 3), "y2": round(self.y2, 3),
            "x3": round(self.x3, 3), "y3": round(self.y3, 3),
            "color": list(self.color),
        }

    @classmethod
    def from_json(cls, data: dict) -> "Triangle":
        return cls(
            color=tuple(data["color"]),
            x1=float(data["x1"]), y1=float(data["y1"]),
            x2=float(data["x2"]), y2=float(data["y2"]),
            x3=float(data["x3"]), y3=float(data["y3"]),
        )

    @classmethod
    def random(cls, rng: random.Random, w: int, h: int, size_scale: float = 1.0, alpha: int = 128) -> "Triangle":
        cx, cy = rng.uniform(0, w - 1), rng.uniform(0, h - 1)
        spread = max(4, min(w, h) / 8) * size_scale
        return cls(
            color=(rng.randint(0, 255), rng.randint(0, 255), rng.randint(0, 255), alpha),
            x1=_clamp(cx + rng.gauss(0, spread), 0, w - 1),
            y1=_clamp(cy + rng.gauss(0, spread), 0, h - 1),
            x2=_clamp(cx + rng.gauss(0, spread), 0, w - 1),
            y2=_clamp(cy + rng.gauss(0, spread), 0, h - 1),
            x3=_clamp(cx + rng.gauss(0, spread), 0, w - 1),
            y3=_clamp(cy + rng.gauss(0, spread), 0, h - 1),
        )

    PARAM_DIM = 6  # [x1, y1, x2, y2, x3, y3]

    @classmethod
    def random_batch(cls, rng: random.Random, w: int, h: int, n: int,
                     size_scale: float, alpha: int, xp=np):
        spread = max(4.0, min(w, h) / 8.0) * size_scale
        p = np.empty((n, 6), dtype=np.float32)
        c = np.empty((n, 4), dtype=np.uint8)
        for i in range(n):
            cx = rng.uniform(0, w - 1)
            cy = rng.uniform(0, h - 1)
            p[i, 0] = _clamp(cx + rng.gauss(0, spread), 0, w - 1)
            p[i, 1] = _clamp(cy + rng.gauss(0, spread), 0, h - 1)
            p[i, 2] = _clamp(cx + rng.gauss(0, spread), 0, w - 1)
            p[i, 3] = _clamp(cy + rng.gauss(0, spread), 0, h - 1)
            p[i, 4] = _clamp(cx + rng.gauss(0, spread), 0, w - 1)
            p[i, 5] = _clamp(cy + rng.gauss(0, spread), 0, h - 1)
            c[i, 0] = rng.randint(0, 255)
            c[i, 1] = rng.randint(0, 255)
            c[i, 2] = rng.randint(0, 255)
            c[i, 3] = alpha
        return xp.asarray(p), xp.asarray(c)

    @classmethod
    def rasterize_batch(cls, params, w: int, h: int, xp=np):
        x1 = params[:, 0:1, None]
        y1 = params[:, 1:2, None]
        x2 = params[:, 2:3, None]
        y2 = params[:, 3:4, None]
        x3 = params[:, 4:5, None]
        y3 = params[:, 5:6, None]
        xs_grid = xp.arange(w, dtype=xp.float32)[None, None, :]
        ys_grid = xp.arange(h, dtype=xp.float32)[None, :, None]
        # edge(a, b, p) = (bx-ax)*(py-ay) - (by-ay)*(px-ax)
        d1 = (x2 - x1) * (ys_grid - y1) - (y2 - y1) * (xs_grid - x1)
        d2 = (x3 - x2) * (ys_grid - y2) - (y3 - y2) * (xs_grid - x2)
        d3 = (x1 - x3) * (ys_grid - y3) - (y1 - y3) * (xs_grid - x3)
        has_neg = (d1 < 0) | (d2 < 0) | (d3 < 0)
        has_pos = (d1 > 0) | (d2 > 0) | (d3 > 0)
        mask = ~(has_neg & has_pos)
        return mask.astype(xp.uint8) * 255

    @classmethod
    def mutate_batch(cls, best_params, rng: random.Random, w: int, h: int, k: int, xp=np):
        bp = np.asarray(best_params, dtype=np.float32).reshape(-1)
        out = np.tile(bp, (k, 1))
        for i in range(k):
            vertex = rng.randint(0, 2)
            dx = rng.gauss(0, 16)
            dy = rng.gauss(0, 16)
            ix = vertex * 2
            iy = vertex * 2 + 1
            out[i, ix] = _clamp(out[i, ix] + dx, 0, w - 1)
            out[i, iy] = _clamp(out[i, iy] + dy, 0, h - 1)
        return xp.asarray(out)

    @classmethod
    def params_to_instance(cls, params_row, color) -> "Triangle":
        return cls(
            color=tuple(int(c) for c in color),
            x1=float(params_row[0]), y1=float(params_row[1]),
            x2=float(params_row[2]), y2=float(params_row[3]),
            x3=float(params_row[4]), y3=float(params_row[5]),
        )

    def to_params_row(self) -> np.ndarray:
        return np.asarray([self.x1, self.y1, self.x2, self.y2, self.x3, self.y3], dtype=np.float32)


_register(Triangle)

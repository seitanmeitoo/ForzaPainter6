from __future__ import annotations

from dataclasses import dataclass
import math
import random

import numpy as np

from legacy.shapegen.shapes.base import Shape, _clip_bbox, _clamp, _register


@dataclass
class Ellipse(Shape):
    type_name = "ellipse"

    x: float = 0.0
    y: float = 0.0
    rx: float = 1.0
    ry: float = 1.0

    def bbox(self, w: int, h: int) -> tuple[int, int, int, int]:
        return _clip_bbox(self.x - self.rx, self.y - self.ry, self.x + self.rx + 1, self.y + self.ry + 1, w, h)

    def rasterize_mask(self, w: int, h: int, xp=np) -> tuple[np.ndarray, tuple[int, int, int, int]]:
        bbox = self.bbox(w, h)
        x0, y0, x1, y1 = bbox
        if x1 <= x0 or y1 <= y0:
            return xp.zeros((0, 0), dtype=xp.uint8), bbox
        ys = xp.arange(y0, y1, dtype=xp.float32) - self.y
        xs = xp.arange(x0, x1, dtype=xp.float32) - self.x
        dx = xs / max(self.rx, 1e-6)
        dy = ys / max(self.ry, 1e-6)
        mask = (dx[None, :] ** 2 + dy[:, None] ** 2) <= 1.0
        return (mask.astype(xp.uint8) * 255), bbox

    def mutate(self, rng: random.Random, w: int, h: int) -> "Ellipse":
        from copy import copy as shallow_copy
        new = shallow_copy(self)
        which = rng.randint(0, 2)
        if which == 0:
            new.x = _clamp(new.x + rng.gauss(0, 16), 0, w - 1)
            new.y = _clamp(new.y + rng.gauss(0, 16), 0, h - 1)
        elif which == 1:
            new.rx = _clamp(new.rx + rng.gauss(0, 16), 1, w)
            new.ry = _clamp(new.ry + rng.gauss(0, 16), 1, h)
        else:
            new.x = _clamp(new.x + rng.gauss(0, 8), 0, w - 1)
            new.y = _clamp(new.y + rng.gauss(0, 8), 0, h - 1)
            new.rx = _clamp(new.rx + rng.gauss(0, 8), 1, w)
            new.ry = _clamp(new.ry + rng.gauss(0, 8), 1, h)
        return new

    def to_json(self) -> dict:
        return {
            "type": self.type_name,
            "x": round(self.x, 3), "y": round(self.y, 3),
            "rx": round(self.rx, 3), "ry": round(self.ry, 3),
            "color": list(self.color),
        }

    @classmethod
    def from_json(cls, data: dict) -> "Ellipse":
        return cls(
            color=tuple(data["color"]),
            x=float(data["x"]), y=float(data["y"]),
            rx=float(data["rx"]), ry=float(data["ry"]),
        )

    @classmethod
    def random(cls, rng: random.Random, w: int, h: int, size_scale: float = 1.0, alpha: int = 128) -> "Ellipse":
        return cls(
            color=(rng.randint(0, 255), rng.randint(0, 255), rng.randint(0, 255), alpha),
            x=rng.uniform(0, w - 1), y=rng.uniform(0, h - 1),
            rx=rng.uniform(1, max(2, w / 8) * size_scale),
            ry=rng.uniform(1, max(2, h / 8) * size_scale),
        )

    PARAM_DIM = 4  # [x, y, rx, ry]

    @classmethod
    def random_batch(cls, rng: random.Random, w: int, h: int, n: int,
                     size_scale: float, alpha: int, xp=np):
        rx_max = max(2.0, w / 8.0) * size_scale
        ry_max = max(2.0, h / 8.0) * size_scale
        p = np.empty((n, 4), dtype=np.float32)
        c = np.empty((n, 4), dtype=np.uint8)
        for i in range(n):
            p[i, 0] = rng.uniform(0, w - 1)
            p[i, 1] = rng.uniform(0, h - 1)
            p[i, 2] = rng.uniform(1, rx_max)
            p[i, 3] = rng.uniform(1, ry_max)
            c[i, 0] = rng.randint(0, 255)
            c[i, 1] = rng.randint(0, 255)
            c[i, 2] = rng.randint(0, 255)
            c[i, 3] = alpha
        return xp.asarray(p), xp.asarray(c)

    @classmethod
    def rasterize_batch(cls, params, w: int, h: int, xp=np):
        x = params[:, 0:1, None]
        y = params[:, 1:2, None]
        rx = params[:, 2:3, None]
        ry = params[:, 3:4, None]
        xs_grid = xp.arange(w, dtype=xp.float32)[None, None, :]
        ys_grid = xp.arange(h, dtype=xp.float32)[None, :, None]
        dx = (xs_grid - x) / xp.maximum(rx, 1e-6)
        dy = (ys_grid - y) / xp.maximum(ry, 1e-6)
        mask = (dx * dx + dy * dy) <= 1.0
        return mask.astype(xp.uint8) * 255

    @classmethod
    def mutate_batch(cls, best_params, rng: random.Random, w: int, h: int, k: int, xp=np):
        bp = np.asarray(best_params, dtype=np.float32).reshape(-1)
        out = np.tile(bp, (k, 1))
        for i in range(k):
            which = rng.randint(0, 2)
            if which == 0:
                out[i, 0] = _clamp(out[i, 0] + rng.gauss(0, 16), 0, w - 1)
                out[i, 1] = _clamp(out[i, 1] + rng.gauss(0, 16), 0, h - 1)
            elif which == 1:
                out[i, 2] = _clamp(out[i, 2] + rng.gauss(0, 16), 1, w)
                out[i, 3] = _clamp(out[i, 3] + rng.gauss(0, 16), 1, h)
            else:
                out[i, 0] = _clamp(out[i, 0] + rng.gauss(0, 8), 0, w - 1)
                out[i, 1] = _clamp(out[i, 1] + rng.gauss(0, 8), 0, h - 1)
                out[i, 2] = _clamp(out[i, 2] + rng.gauss(0, 8), 1, w)
                out[i, 3] = _clamp(out[i, 3] + rng.gauss(0, 8), 1, h)
        return xp.asarray(out)

    @classmethod
    def params_to_instance(cls, params_row, color) -> "Ellipse":
        return cls(
            color=tuple(int(c) for c in color),
            x=float(params_row[0]), y=float(params_row[1]),
            rx=float(params_row[2]), ry=float(params_row[3]),
        )

    def to_params_row(self) -> np.ndarray:
        return np.asarray([self.x, self.y, self.rx, self.ry], dtype=np.float32)


_register(Ellipse)


@dataclass
class RotatedEllipse(Shape):
    type_name = "rotated_ellipse"

    x: float = 0.0
    y: float = 0.0
    rx: float = 1.0
    ry: float = 1.0
    angle: float = 0.0

    def bbox(self, w: int, h: int) -> tuple[int, int, int, int]:
        r = max(self.rx, self.ry)
        return _clip_bbox(self.x - r, self.y - r, self.x + r + 1, self.y + r + 1, w, h)

    def rasterize_mask(self, w: int, h: int, xp=np) -> tuple[np.ndarray, tuple[int, int, int, int]]:
        bbox = self.bbox(w, h)
        x0, y0, x1, y1 = bbox
        if x1 <= x0 or y1 <= y0:
            return xp.zeros((0, 0), dtype=xp.uint8), bbox
        rad = math.radians(self.angle)
        cos_a, sin_a = math.cos(rad), math.sin(rad)
        ys = xp.arange(y0, y1, dtype=xp.float32) - self.y
        xs = xp.arange(x0, x1, dtype=xp.float32) - self.x
        xg, yg = xp.meshgrid(xs, ys)
        xr = cos_a * xg + sin_a * yg
        yr = -sin_a * xg + cos_a * yg
        dx = xr / max(self.rx, 1e-6)
        dy = yr / max(self.ry, 1e-6)
        mask = (dx ** 2 + dy ** 2) <= 1.0
        return (mask.astype(xp.uint8) * 255), bbox

    def mutate(self, rng: random.Random, w: int, h: int) -> "RotatedEllipse":
        from copy import copy as shallow_copy
        new = shallow_copy(self)
        which = rng.randint(0, 3)
        if which == 0:
            new.x = _clamp(new.x + rng.gauss(0, 16), 0, w - 1)
            new.y = _clamp(new.y + rng.gauss(0, 16), 0, h - 1)
        elif which == 1:
            new.rx = _clamp(new.rx + rng.gauss(0, 16), 1, w)
            new.ry = _clamp(new.ry + rng.gauss(0, 16), 1, h)
        elif which == 2:
            new.angle = (new.angle + rng.gauss(0, 25)) % 180.0
        else:
            new.x = _clamp(new.x + rng.gauss(0, 8), 0, w - 1)
            new.y = _clamp(new.y + rng.gauss(0, 8), 0, h - 1)
            new.angle = (new.angle + rng.gauss(0, 15)) % 180.0
        return new

    def to_json(self) -> dict:
        return {
            "type": self.type_name,
            "x": round(self.x, 3), "y": round(self.y, 3),
            "rx": round(self.rx, 3), "ry": round(self.ry, 3),
            "angle": round(self.angle, 3),
            "color": list(self.color),
        }

    @classmethod
    def from_json(cls, data: dict) -> "RotatedEllipse":
        return cls(
            color=tuple(data["color"]),
            x=float(data["x"]), y=float(data["y"]),
            rx=float(data["rx"]), ry=float(data["ry"]),
            angle=float(data["angle"]),
        )

    @classmethod
    def random(cls, rng: random.Random, w: int, h: int, size_scale: float = 1.0, alpha: int = 128) -> "RotatedEllipse":
        return cls(
            color=(rng.randint(0, 255), rng.randint(0, 255), rng.randint(0, 255), alpha),
            x=rng.uniform(0, w - 1), y=rng.uniform(0, h - 1),
            rx=rng.uniform(1, max(2, w / 8) * size_scale),
            ry=rng.uniform(1, max(2, h / 8) * size_scale),
            angle=rng.uniform(0, 180),
        )

    PARAM_DIM = 5  # [x, y, rx, ry, angle]

    @classmethod
    def random_batch(cls, rng: random.Random, w: int, h: int, n: int,
                     size_scale: float, alpha: int, xp=np):
        rx_max = max(2.0, w / 8.0) * size_scale
        ry_max = max(2.0, h / 8.0) * size_scale
        p = np.empty((n, 5), dtype=np.float32)
        c = np.empty((n, 4), dtype=np.uint8)
        for i in range(n):
            p[i, 0] = rng.uniform(0, w - 1)
            p[i, 1] = rng.uniform(0, h - 1)
            p[i, 2] = rng.uniform(1, rx_max)
            p[i, 3] = rng.uniform(1, ry_max)
            p[i, 4] = rng.uniform(0, 180)
            c[i, 0] = rng.randint(0, 255)
            c[i, 1] = rng.randint(0, 255)
            c[i, 2] = rng.randint(0, 255)
            c[i, 3] = alpha
        return xp.asarray(p), xp.asarray(c)

    @classmethod
    def rasterize_batch(cls, params, w: int, h: int, xp=np):
        x = params[:, 0:1, None]
        y = params[:, 1:2, None]
        rx = params[:, 2:3, None]
        ry = params[:, 3:4, None]
        angle = params[:, 4:5, None]
        rad = angle * (math.pi / 180.0)
        cos_a = xp.cos(rad)
        sin_a = xp.sin(rad)
        xs_grid = xp.arange(w, dtype=xp.float32)[None, None, :]
        ys_grid = xp.arange(h, dtype=xp.float32)[None, :, None]
        xg = xs_grid - x
        yg = ys_grid - y
        xr = cos_a * xg + sin_a * yg
        yr = -sin_a * xg + cos_a * yg
        dx = xr / xp.maximum(rx, 1e-6)
        dy = yr / xp.maximum(ry, 1e-6)
        mask = (dx * dx + dy * dy) <= 1.0
        return mask.astype(xp.uint8) * 255

    @classmethod
    def mutate_batch(cls, best_params, rng: random.Random, w: int, h: int, k: int, xp=np):
        bp = np.asarray(best_params, dtype=np.float32).reshape(-1)
        out = np.tile(bp, (k, 1))
        for i in range(k):
            which = rng.randint(0, 3)
            if which == 0:
                out[i, 0] = _clamp(out[i, 0] + rng.gauss(0, 16), 0, w - 1)
                out[i, 1] = _clamp(out[i, 1] + rng.gauss(0, 16), 0, h - 1)
            elif which == 1:
                out[i, 2] = _clamp(out[i, 2] + rng.gauss(0, 16), 1, w)
                out[i, 3] = _clamp(out[i, 3] + rng.gauss(0, 16), 1, h)
            elif which == 2:
                out[i, 4] = (out[i, 4] + rng.gauss(0, 25)) % 180.0
            else:
                out[i, 0] = _clamp(out[i, 0] + rng.gauss(0, 8), 0, w - 1)
                out[i, 1] = _clamp(out[i, 1] + rng.gauss(0, 8), 0, h - 1)
                out[i, 4] = (out[i, 4] + rng.gauss(0, 15)) % 180.0
        return xp.asarray(out)

    @classmethod
    def params_to_instance(cls, params_row, color) -> "RotatedEllipse":
        return cls(
            color=tuple(int(c) for c in color),
            x=float(params_row[0]), y=float(params_row[1]),
            rx=float(params_row[2]), ry=float(params_row[3]),
            angle=float(params_row[4]),
        )

    def to_params_row(self) -> np.ndarray:
        return np.asarray([self.x, self.y, self.rx, self.ry, self.angle], dtype=np.float32)


_register(RotatedEllipse)

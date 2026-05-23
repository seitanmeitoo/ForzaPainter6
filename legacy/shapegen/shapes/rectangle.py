from __future__ import annotations

from dataclasses import dataclass
import math
import random

import numpy as np

from legacy.shapegen.shapes.base import Shape, _clip_bbox, _clamp, _register


@dataclass
class Rectangle(Shape):
    type_name = "rectangle"

    x: float = 0.0
    y: float = 0.0
    hw: float = 1.0
    hh: float = 1.0

    def bbox(self, w: int, h: int) -> tuple[int, int, int, int]:
        return _clip_bbox(self.x - self.hw, self.y - self.hh, self.x + self.hw + 1, self.y + self.hh + 1, w, h)

    def rasterize_mask(self, w: int, h: int, xp=np) -> tuple[np.ndarray, tuple[int, int, int, int]]:
        bbox = self.bbox(w, h)
        x0, y0, x1, y1 = bbox
        if x1 <= x0 or y1 <= y0:
            return xp.zeros((0, 0), dtype=xp.uint8), bbox
        return xp.full((y1 - y0, x1 - x0), 255, dtype=xp.uint8), bbox

    def mutate(self, rng: random.Random, w: int, h: int) -> "Rectangle":  # noqa: E501
        from copy import copy as shallow_copy
        new = shallow_copy(self)
        which = rng.randint(0, 1)
        if which == 0:
            new.x = _clamp(new.x + rng.gauss(0, 16), 0, w - 1)
            new.y = _clamp(new.y + rng.gauss(0, 16), 0, h - 1)
        else:
            new.hw = _clamp(new.hw + rng.gauss(0, 16), 1, w)
            new.hh = _clamp(new.hh + rng.gauss(0, 16), 1, h)
        return new

    def to_json(self) -> dict:
        return {
            "type": self.type_name,
            "x": round(self.x, 3), "y": round(self.y, 3),
            "hw": round(self.hw, 3), "hh": round(self.hh, 3),
            "color": list(self.color),
        }

    @classmethod
    def from_json(cls, data: dict) -> "Rectangle":
        return cls(
            color=tuple(data["color"]),
            x=float(data["x"]), y=float(data["y"]),
            hw=float(data["hw"]), hh=float(data["hh"]),
        )

    @classmethod
    def random(cls, rng: random.Random, w: int, h: int, size_scale: float = 1.0, alpha: int = 128) -> "Rectangle":
        return cls(
            color=(rng.randint(0, 255), rng.randint(0, 255), rng.randint(0, 255), alpha),
            x=rng.uniform(0, w - 1), y=rng.uniform(0, h - 1),
            hw=rng.uniform(1, max(2, w / 8) * size_scale),
            hh=rng.uniform(1, max(2, h / 8) * size_scale),
        )

    PARAM_DIM = 4  # [x, y, hw, hh]

    @classmethod
    def random_batch(cls, rng: random.Random, w: int, h: int, n: int,
                     size_scale: float, alpha: int, xp=np):
        hw_max = max(2.0, w / 8.0) * size_scale
        hh_max = max(2.0, h / 8.0) * size_scale
        p = np.empty((n, 4), dtype=np.float32)
        c = np.empty((n, 4), dtype=np.uint8)
        for i in range(n):
            p[i, 0] = rng.uniform(0, w - 1)
            p[i, 1] = rng.uniform(0, h - 1)
            p[i, 2] = rng.uniform(1, hw_max)
            p[i, 3] = rng.uniform(1, hh_max)
            c[i, 0] = rng.randint(0, 255)
            c[i, 1] = rng.randint(0, 255)
            c[i, 2] = rng.randint(0, 255)
            c[i, 3] = alpha
        return xp.asarray(p), xp.asarray(c)

    @classmethod
    def rasterize_batch(cls, params, w: int, h: int, xp=np):
        x = params[:, 0:1, None]
        y = params[:, 1:2, None]
        hw = params[:, 2:3, None]
        hh = params[:, 3:4, None]
        xs_grid = xp.arange(w, dtype=xp.float32)[None, None, :]
        ys_grid = xp.arange(h, dtype=xp.float32)[None, :, None]
        mask = (xp.abs(xs_grid - x) <= hw) & (xp.abs(ys_grid - y) <= hh)
        return mask.astype(xp.uint8) * 255

    @classmethod
    def mutate_batch(cls, best_params, rng: random.Random, w: int, h: int, k: int, xp=np):
        bp = np.asarray(best_params, dtype=np.float32).reshape(-1)
        out = np.tile(bp, (k, 1))
        for i in range(k):
            which = rng.randint(0, 1)
            if which == 0:
                out[i, 0] = _clamp(out[i, 0] + rng.gauss(0, 16), 0, w - 1)
                out[i, 1] = _clamp(out[i, 1] + rng.gauss(0, 16), 0, h - 1)
            else:
                out[i, 2] = _clamp(out[i, 2] + rng.gauss(0, 16), 1, w)
                out[i, 3] = _clamp(out[i, 3] + rng.gauss(0, 16), 1, h)
        return xp.asarray(out)

    @classmethod
    def params_to_instance(cls, params_row, color) -> "Rectangle":
        return cls(
            color=tuple(int(c) for c in color),
            x=float(params_row[0]), y=float(params_row[1]),
            hw=float(params_row[2]), hh=float(params_row[3]),
        )

    def to_params_row(self) -> np.ndarray:
        return np.asarray([self.x, self.y, self.hw, self.hh], dtype=np.float32)


_register(Rectangle)


@dataclass
class RotatedRectangle(Shape):
    type_name = "rotated_rectangle"

    x: float = 0.0
    y: float = 0.0
    hw: float = 1.0
    hh: float = 1.0
    angle: float = 0.0

    def bbox(self, w: int, h: int) -> tuple[int, int, int, int]:
        r = math.hypot(self.hw, self.hh)
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
        mask = (xp.abs(xr) <= self.hw) & (xp.abs(yr) <= self.hh)
        return (mask.astype(xp.uint8) * 255), bbox

    def mutate(self, rng: random.Random, w: int, h: int) -> "RotatedRectangle":
        from copy import copy as shallow_copy
        new = shallow_copy(self)
        which = rng.randint(0, 2)
        if which == 0:
            new.x = _clamp(new.x + rng.gauss(0, 16), 0, w - 1)
            new.y = _clamp(new.y + rng.gauss(0, 16), 0, h - 1)
        elif which == 1:
            new.hw = _clamp(new.hw + rng.gauss(0, 16), 1, w)
            new.hh = _clamp(new.hh + rng.gauss(0, 16), 1, h)
        else:
            new.angle = (new.angle + rng.gauss(0, 25)) % 180.0
        return new

    def to_json(self) -> dict:
        return {
            "type": self.type_name,
            "x": round(self.x, 3), "y": round(self.y, 3),
            "hw": round(self.hw, 3), "hh": round(self.hh, 3),
            "angle": round(self.angle, 3),
            "color": list(self.color),
        }

    @classmethod
    def from_json(cls, data: dict) -> "RotatedRectangle":
        return cls(
            color=tuple(data["color"]),
            x=float(data["x"]), y=float(data["y"]),
            hw=float(data["hw"]), hh=float(data["hh"]),
            angle=float(data["angle"]),
        )

    @classmethod
    def random(cls, rng: random.Random, w: int, h: int, size_scale: float = 1.0, alpha: int = 128) -> "RotatedRectangle":
        return cls(
            color=(rng.randint(0, 255), rng.randint(0, 255), rng.randint(0, 255), alpha),
            x=rng.uniform(0, w - 1), y=rng.uniform(0, h - 1),
            hw=rng.uniform(1, max(2, w / 8) * size_scale),
            hh=rng.uniform(1, max(2, h / 8) * size_scale),
            angle=rng.uniform(0, 180),
        )

    PARAM_DIM = 5  # [x, y, hw, hh, angle]

    @classmethod
    def random_batch(cls, rng: random.Random, w: int, h: int, n: int,
                     size_scale: float, alpha: int, xp=np):
        hw_max = max(2.0, w / 8.0) * size_scale
        hh_max = max(2.0, h / 8.0) * size_scale
        p = np.empty((n, 5), dtype=np.float32)
        c = np.empty((n, 4), dtype=np.uint8)
        for i in range(n):
            p[i, 0] = rng.uniform(0, w - 1)
            p[i, 1] = rng.uniform(0, h - 1)
            p[i, 2] = rng.uniform(1, hw_max)
            p[i, 3] = rng.uniform(1, hh_max)
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
        hw = params[:, 2:3, None]
        hh = params[:, 3:4, None]
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
        mask = (xp.abs(xr) <= hw) & (xp.abs(yr) <= hh)
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
                out[i, 4] = (out[i, 4] + rng.gauss(0, 25)) % 180.0
        return xp.asarray(out)

    @classmethod
    def params_to_instance(cls, params_row, color) -> "RotatedRectangle":
        return cls(
            color=tuple(int(c) for c in color),
            x=float(params_row[0]), y=float(params_row[1]),
            hw=float(params_row[2]), hh=float(params_row[3]),
            angle=float(params_row[4]),
        )

    def to_params_row(self) -> np.ndarray:
        return np.asarray([self.x, self.y, self.hw, self.hh, self.angle], dtype=np.float32)


_register(RotatedRectangle)

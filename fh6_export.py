"""Convert the FD6 Shape list into the FH6 JSON schema expected by the
in-game vinyl editor (and by forza-painter-fh6 for import).

FH6 schema:
    {"shapes": [bg_rect, drawable1, drawable2, ...]}

Each shape is:
    {"type": int, "data": [x, y, w, h, (rot)], "color": [r, g, b, a]}

FH6 type codes:
    1  → axis-aligned rectangle  (data = [x, y, w, h], (x,y) at centre)
    16 → rotated ellipse         (data = [x, y, w, h, rot_deg])

Other FD6 shapes (Circle, Ellipse, RotatedRectangle, Triangle) are converted
to one of these two types. Circle and axis-aligned Ellipse map losslessly to
type=16 with angle=0. RotatedRectangle and Triangle are approximated by a
bounding rotated ellipse — visually lossy. The exporter counts these lossy
conversions so the GUI can warn the user.
"""
from __future__ import annotations

import json
import math
from pathlib import Path

from shapegen.shapes import Shape
from shapegen.shapes.rectangle import Rectangle, RotatedRectangle
from shapegen.shapes.ellipse import Ellipse, RotatedEllipse
from shapegen.shapes.circle import Circle
from shapegen.shapes.triangle import Triangle


RECTANGLE_TYPE = 1
ROTATED_ELLIPSE_TYPE = 16


def _convert_shape(shape: Shape) -> tuple[dict | None, bool]:
    """Return (fh6_shape_or_None, was_lossy)."""
    color = [int(c) for c in shape.color]

    if isinstance(shape, Rectangle) and not isinstance(shape, RotatedRectangle):
        return {
            "type": RECTANGLE_TYPE,
            "data": [
                round(shape.x), round(shape.y),
                max(1, round(2 * shape.hw)), max(1, round(2 * shape.hh)),
            ],
            "color": color,
        }, False

    if isinstance(shape, RotatedRectangle):
        # Approximation : ellipse tournée englobant le rectangle tourné
        return {
            "type": ROTATED_ELLIPSE_TYPE,
            "data": [
                round(shape.x), round(shape.y),
                max(1, round(2 * shape.hw)), max(1, round(2 * shape.hh)),
                round(shape.angle) % 360,
            ],
            "color": color,
        }, True

    if isinstance(shape, Circle):
        return {
            "type": ROTATED_ELLIPSE_TYPE,
            "data": [
                round(shape.x), round(shape.y),
                max(1, round(2 * shape.r)), max(1, round(2 * shape.r)),
                0,
            ],
            "color": color,
        }, False

    if isinstance(shape, RotatedEllipse):
        return {
            "type": ROTATED_ELLIPSE_TYPE,
            "data": [
                round(shape.x), round(shape.y),
                max(1, round(2 * shape.rx)), max(1, round(2 * shape.ry)),
                round(shape.angle) % 360,
            ],
            "color": color,
        }, False

    if isinstance(shape, Ellipse):
        return {
            "type": ROTATED_ELLIPSE_TYPE,
            "data": [
                round(shape.x), round(shape.y),
                max(1, round(2 * shape.rx)), max(1, round(2 * shape.ry)),
                0,
            ],
            "color": color,
        }, False

    if isinstance(shape, Triangle):
        # Approximation : ellipse circonscrite à la bbox du triangle
        min_x = min(shape.x1, shape.x2, shape.x3)
        max_x = max(shape.x1, shape.x2, shape.x3)
        min_y = min(shape.y1, shape.y2, shape.y3)
        max_y = max(shape.y1, shape.y2, shape.y3)
        cx, cy = (min_x + max_x) / 2.0, (min_y + max_y) / 2.0
        w, h = max(1.0, max_x - min_x), max(1.0, max_y - min_y)
        return {
            "type": ROTATED_ELLIPSE_TYPE,
            "data": [round(cx), round(cy), max(1, round(w)), max(1, round(h)), 0],
            "color": color,
        }, True

    return None, False


def to_fh6_payload(
    shapes: list[Shape],
    image_size: tuple[int, int],
    bg_color: tuple[int, int, int, int],
) -> tuple[dict, int]:
    """Convertit la liste de Shapes vers le format FH6.

    Renvoie (payload, lossy_count). lossy_count = nombre de formes converties
    avec perte visuelle (rotated_rectangle, triangle).
    """
    w, h = image_size
    background = {
        "type": RECTANGLE_TYPE,
        "data": [w // 2, h // 2, w, h],
        "color": [int(c) for c in bg_color],
    }
    drawables: list[dict] = []
    lossy_count = 0
    for s in shapes:
        out, was_lossy = _convert_shape(s)
        if out is None:
            continue
        drawables.append(out)
        if was_lossy:
            lossy_count += 1
    return {"shapes": [background] + drawables}, lossy_count


def save_fh6_json(payload: dict, path: Path) -> None:
    Path(path).write_text(json.dumps(payload, indent=2), encoding="utf-8")

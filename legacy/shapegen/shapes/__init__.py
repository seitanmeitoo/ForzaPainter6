from legacy.shapegen.shapes.base import (
    Shape, ShapeType, SHAPE_REGISTRY,
    random_shape, random_shape_batch,
    rasterize_batch_multi, concat_colors, locate_batch_index,
    shape_from_json,
)
from legacy.shapegen.shapes.rectangle import Rectangle, RotatedRectangle
from legacy.shapegen.shapes.ellipse import Ellipse, RotatedEllipse
from legacy.shapegen.shapes.circle import Circle
from legacy.shapegen.shapes.triangle import Triangle

__all__ = [
    "Shape", "ShapeType", "SHAPE_REGISTRY",
    "random_shape", "random_shape_batch",
    "rasterize_batch_multi", "concat_colors", "locate_batch_index",
    "shape_from_json",
    "Rectangle", "RotatedRectangle",
    "Ellipse", "RotatedEllipse",
    "Circle", "Triangle",
]

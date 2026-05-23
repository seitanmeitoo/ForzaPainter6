"""Genere native/icon.ico (multi-tailles) pour vinyl-painter.

Visuel : formes geometriques translucides qui se chevauchent sur un fond
arrondi sombre -- coherent avec ce que fait l'app (image -> formes).

Usage : py native/tools/make_icon.py
Dependance : Pillow.
"""
import os
from PIL import Image, ImageDraw, ImageChops

SIZE = 256
RADIUS = 48
BG = (34, 34, 40, 255)

OUT = os.path.join(os.path.dirname(__file__), "..", "icon.ico")


def _overlay():
    return Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))


def build():
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ImageDraw.Draw(img).rounded_rectangle(
        [0, 0, SIZE - 1, SIZE - 1], radius=RADIUS, fill=BG
    )

    # Triangle (teal)
    o = _overlay()
    ImageDraw.Draw(o).polygon(
        [(58, 198), (150, 40), (212, 176)], fill=(64, 196, 180, 205)
    )
    img.alpha_composite(o)

    # Cercle (rose/magenta)
    o = _overlay()
    ImageDraw.Draw(o).ellipse([40, 92, 162, 214], fill=(232, 72, 128, 190))
    img.alpha_composite(o)

    # Rectangle (ambre)
    o = _overlay()
    ImageDraw.Draw(o).rectangle([118, 118, 222, 212], fill=(245, 180, 60, 185))
    img.alpha_composite(o)

    # Clip final aux coins arrondis
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, SIZE - 1, SIZE - 1], radius=RADIUS, fill=255
    )
    r, g, b, a = img.split()
    img = Image.merge("RGBA", (r, g, b, ImageChops.multiply(a, mask)))

    img.save(
        OUT,
        format="ICO",
        sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    print("ecrit:", os.path.normpath(OUT))


if __name__ == "__main__":
    build()

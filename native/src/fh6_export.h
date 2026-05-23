#ifndef VINYL_FH6_EXPORT_H
#define VINYL_FH6_EXPORT_H

#include <stdint.h>
#include <wchar.h>
#include "shapes.h"

/* Schema FH6 attendu par forza-painter-fh6 et par l'editeur de vinyles
 * du jeu :
 *
 *   { "shapes": [bg_rect, drawable1, drawable2, ...] }
 *
 * Chaque shape :
 *   { "type": int, "data": [x, y, w, h, (rot)], "color": [r, g, b, a] }
 *
 * Types FH6 :
 *   1  -> rectangle axis-aligned, data = [cx, cy, w, h]
 *   16 -> ellipse tournee,        data = [cx, cy, w, h, rot_deg]
 *
 * Les autres types sont approximes :
 *  - Circle, Ellipse axis-aligned : -> type 16, angle 0 (sans perte)
 *  - RotatedEllipse               : -> type 16 natif (sans perte)
 *  - Rectangle axis               : -> type 1 (sans perte)
 *  - RotatedRectangle             : -> type 16 englobant (LOSSY)
 *  - Triangle                     : -> type 16 sur bbox (LOSSY) */

/* Compte combien de shapes seront converties avec perte visuelle. Pas d'IO. */
int fh6_count_lossy(const Shape *shapes, int n);

/* Ecrit le JSON sur disque. bg_color : RGBA du rectangle de fond (premier
 * "shapes[0]" du JSON). Renvoie 0 si OK, -1 sinon. */
int fh6_export_to_file(const Shape *shapes, int n,
                       int img_w, int img_h,
                       const uint8_t bg_color[4],
                       const wchar_t *path);

#endif

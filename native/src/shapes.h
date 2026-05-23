#ifndef VINYL_SHAPES_H
#define VINYL_SHAPES_H

#include <stdint.h>
#include "rng.h"

/* Bbox demi-ouverte [x0..x1) x [y0..y1). */
typedef struct Bbox {
    int x0, y0, x1, y1;
} Bbox;

static inline int bbox_w(const Bbox *b) { return b->x1 - b->x0; }
static inline int bbox_h(const Bbox *b) { return b->y1 - b->y0; }
static inline int bbox_empty(const Bbox *b) { return b->x1 <= b->x0 || b->y1 <= b->y0; }

typedef enum ShapeType {
    SHAPE_CIRCLE       = 0,
    SHAPE_RECT         = 1,
    SHAPE_RECT_ROT     = 2,
    SHAPE_ELLIPSE      = 3,
    SHAPE_ELLIPSE_ROT  = 4,
    SHAPE_TRIANGLE     = 5,
    SHAPE_TYPE_COUNT
} ShapeType;

typedef struct Shape Shape;

typedef struct ShapeOps {
    const char *name;
    int         param_dim;   /* nb de floats utiles dans Shape.params */

    /* Initialise une forme aleatoire dans [0..w) x [0..h).
     * size_scale (>= 1.0) multiplie la borne haute de la dimension principale. */
    void (*random_init)(Shape *s, Rng *rng, int w, int h, float size_scale);

    /* Perturbe s en place via rng. */
    void (*mutate)(Shape *s, Rng *rng, int w, int h);

    /* Renvoie la bbox cliquee sur le canvas (jamais hors [0..w] x [0..h]). */
    void (*compute_bbox)(const Shape *s, int w, int h, Bbox *out);

    /* Ecrit dans out_mask[w*h] (caller alloue) la couverture binaire (0 ou 255)
     * limitee a la bbox. Les pixels hors bbox ne sont PAS touches (caller doit
     * memset si besoin). out_bbox est rempli avec la meme bbox que compute_bbox. */
    void (*rasterize_mask)(const Shape *s, int w, int h, uint8_t *out_mask, Bbox *out_bbox);
} ShapeOps;

struct Shape {
    const ShapeOps *ops;
    uint8_t color[4];   /* RGBA, A = couverture quand la forme est composee */
    float   params[6];  /* max-arity (Triangle = 6) */
};

extern const ShapeOps SHAPE_OPS_CIRCLE;
extern const ShapeOps SHAPE_OPS_RECT;
extern const ShapeOps SHAPE_OPS_RECT_ROT;
extern const ShapeOps SHAPE_OPS_ELLIPSE;
extern const ShapeOps SHAPE_OPS_ELLIPSE_ROT;
extern const ShapeOps SHAPE_OPS_TRIANGLE;

/* Registre des types disponibles, indexable par ShapeType. */
extern const ShapeOps *const SHAPE_REGISTRY[SHAPE_TYPE_COUNT];

/* Helper : initialise une Shape d'un type donne avec params/color aleatoires. */
void shape_random(Shape *out, ShapeType type, Rng *rng, int w, int h, float size_scale,
                  uint8_t alpha);

#endif

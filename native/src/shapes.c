#include "shapes.h"
#include "util.h"

#include <math.h>
#include <string.h>

/* Helper : perturbe un parametre par un bruit gaussien et clamp. */
static inline float gauss_clamp(Rng *r, float x, float sigma, float lo, float hi)
{
    return fclampf(x + rng_gauss(r, sigma), lo, hi);
}

static inline float fmax2(float a, float b) { return a > b ? a : b; }

/* Bornes "demi-axe / demi-cote" pour rectangles / ellipses axis-aligned. */
static inline float bound_hw(int w, float size_scale)
{
    return fmax2(2.0f, (float)w / 8.0f) * size_scale;
}
static inline float bound_hh(int h, float size_scale)
{
    return fmax2(2.0f, (float)h / 8.0f) * size_scale;
}

/* ============================================================
 * Circle : params = [cx, cy, r]
 * ============================================================ */

static void circle_random(Shape *s, Rng *rng, int w, int h, float size_scale)
{
    float max_r = 0.5f * (float)imin(w, h) * size_scale;
    if (max_r < 2.0f) max_r = 2.0f;

    s->params[0] = rng_f_range(rng, 0.0f, (float)w);                    /* cx */
    s->params[1] = rng_f_range(rng, 0.0f, (float)h);                    /* cy */
    s->params[2] = rng_f_range(rng, 1.0f, max_r);                       /* r  */
}

static void circle_mutate(Shape *s, Rng *rng, int w, int h)
{
    /* Une mutation = on tire au sort un parametre et on lui applique un delta
     * uniforme de ±16 px (cx/cy) ou ±16 (r), clipe pour rester valide. */
    uint32_t k = rng_u32_range(rng, 3);
    float delta = rng_f_range(rng, -16.0f, 16.0f);
    switch (k) {
        case 0: s->params[0] = fclampf(s->params[0] + delta, 0.0f, (float)(w - 1)); break;
        case 1: s->params[1] = fclampf(s->params[1] + delta, 0.0f, (float)(h - 1)); break;
        default: {
            float max_r = 0.5f * (float)imin(w, h) * 2.0f;  /* tolere un peu plus large que init */
            s->params[2] = fclampf(s->params[2] + delta, 1.0f, max_r);
        } break;
    }
}

static void circle_compute_bbox(const Shape *s, int w, int h, Bbox *out)
{
    float cx = s->params[0], cy = s->params[1], r = s->params[2];
    int x0 = (int)floorf(cx - r);
    int y0 = (int)floorf(cy - r);
    int x1 = (int)ceilf (cx + r);
    int y1 = (int)ceilf (cy + r);
    out->x0 = iclamp(x0, 0, w);
    out->y0 = iclamp(y0, 0, h);
    out->x1 = iclamp(x1, 0, w);
    out->y1 = iclamp(y1, 0, h);
}

static void circle_rasterize(const Shape *s, int w, int h, uint8_t *out_mask, Bbox *out_bbox)
{
    circle_compute_bbox(s, w, h, out_bbox);
    if (bbox_empty(out_bbox)) return;

    float cx = s->params[0], cy = s->params[1], r = s->params[2];
    float r2 = r * r;

    for (int y = out_bbox->y0; y < out_bbox->y1; ++y) {
        float dy = (float)y + 0.5f - cy;
        float dy2 = dy * dy;
        uint8_t *row = out_mask + (size_t)y * (size_t)w;
        for (int x = out_bbox->x0; x < out_bbox->x1; ++x) {
            float dx = (float)x + 0.5f - cx;
            row[x] = (dx * dx + dy2 <= r2) ? 255u : 0u;
        }
    }
}

const ShapeOps SHAPE_OPS_CIRCLE = {
    .name           = "circle",
    .param_dim      = 3,
    .random_init    = circle_random,
    .mutate         = circle_mutate,
    .compute_bbox   = circle_compute_bbox,
    .rasterize_mask = circle_rasterize,
};

/* ============================================================
 * Rectangle axis-aligned : params = [x, y, hw, hh]
 * ============================================================ */

static void rect_random(Shape *s, Rng *rng, int w, int h, float size_scale)
{
    s->params[0] = rng_f_range(rng, 0.0f, (float)(w - 1));
    s->params[1] = rng_f_range(rng, 0.0f, (float)(h - 1));
    s->params[2] = rng_f_range(rng, 1.0f, bound_hw(w, size_scale));
    s->params[3] = rng_f_range(rng, 1.0f, bound_hh(h, size_scale));
}

static void rect_mutate(Shape *s, Rng *rng, int w, int h)
{
    if (rng_u32_range(rng, 2) == 0) {
        s->params[0] = gauss_clamp(rng, s->params[0], 16.0f, 0.0f, (float)(w - 1));
        s->params[1] = gauss_clamp(rng, s->params[1], 16.0f, 0.0f, (float)(h - 1));
    } else {
        s->params[2] = gauss_clamp(rng, s->params[2], 16.0f, 1.0f, (float)w);
        s->params[3] = gauss_clamp(rng, s->params[3], 16.0f, 1.0f, (float)h);
    }
}

static void rect_compute_bbox(const Shape *s, int w, int h, Bbox *out)
{
    float x = s->params[0], y = s->params[1], hw = s->params[2], hh = s->params[3];
    out->x0 = iclamp((int)floorf(x - hw),        0, w);
    out->y0 = iclamp((int)floorf(y - hh),        0, h);
    out->x1 = iclamp((int)ceilf (x + hw + 1.0f), 0, w);
    out->y1 = iclamp((int)ceilf (y + hh + 1.0f), 0, h);
}

static void rect_rasterize(const Shape *s, int w, int h, uint8_t *out_mask, Bbox *bb)
{
    rect_compute_bbox(s, w, h, bb);
    if (bbox_empty(bb)) return;
    int span = bb->x1 - bb->x0;
    for (int y = bb->y0; y < bb->y1; ++y) {
        memset(out_mask + (size_t)y * (size_t)w + bb->x0, 255, (size_t)span);
    }
}

const ShapeOps SHAPE_OPS_RECT = {
    .name           = "rectangle",
    .param_dim      = 4,
    .random_init    = rect_random,
    .mutate         = rect_mutate,
    .compute_bbox   = rect_compute_bbox,
    .rasterize_mask = rect_rasterize,
};

/* ============================================================
 * Rectangle tourne : params = [x, y, hw, hh, angle_deg]
 * ============================================================ */

static void rrect_random(Shape *s, Rng *rng, int w, int h, float size_scale)
{
    s->params[0] = rng_f_range(rng, 0.0f, (float)(w - 1));
    s->params[1] = rng_f_range(rng, 0.0f, (float)(h - 1));
    s->params[2] = rng_f_range(rng, 1.0f, bound_hw(w, size_scale));
    s->params[3] = rng_f_range(rng, 1.0f, bound_hh(h, size_scale));
    s->params[4] = rng_f_range(rng, 0.0f, 180.0f);
}

static void rrect_mutate(Shape *s, Rng *rng, int w, int h)
{
    uint32_t k = rng_u32_range(rng, 3);
    if (k == 0) {
        s->params[0] = gauss_clamp(rng, s->params[0], 16.0f, 0.0f, (float)(w - 1));
        s->params[1] = gauss_clamp(rng, s->params[1], 16.0f, 0.0f, (float)(h - 1));
    } else if (k == 1) {
        s->params[2] = gauss_clamp(rng, s->params[2], 16.0f, 1.0f, (float)w);
        s->params[3] = gauss_clamp(rng, s->params[3], 16.0f, 1.0f, (float)h);
    } else {
        float a = s->params[4] + rng_gauss(rng, 25.0f);
        a = fmodf(a, 180.0f);
        if (a < 0.0f) a += 180.0f;
        s->params[4] = a;
    }
}

static void rrect_compute_bbox(const Shape *s, int w, int h, Bbox *out)
{
    float x = s->params[0], y = s->params[1];
    float hw = s->params[2], hh = s->params[3];
    float r = hypotf(hw, hh); /* enveloppe circulaire conservatrice */
    out->x0 = iclamp((int)floorf(x - r),        0, w);
    out->y0 = iclamp((int)floorf(y - r),        0, h);
    out->x1 = iclamp((int)ceilf (x + r + 1.0f), 0, w);
    out->y1 = iclamp((int)ceilf (y + r + 1.0f), 0, h);
}

static void rrect_rasterize(const Shape *s, int w, int h, uint8_t *out_mask, Bbox *bb)
{
    rrect_compute_bbox(s, w, h, bb);
    if (bbox_empty(bb)) return;
    float x = s->params[0], y = s->params[1];
    float hw = s->params[2], hh = s->params[3];
    float rad = s->params[4] * 3.14159265358979f / 180.0f;
    float ca = cosf(rad), sa = sinf(rad);

    for (int yy = bb->y0; yy < bb->y1; ++yy) {
        float dy = (float)yy + 0.5f - y;
        uint8_t *row = out_mask + (size_t)yy * (size_t)w;
        for (int xx = bb->x0; xx < bb->x1; ++xx) {
            float dx = (float)xx + 0.5f - x;
            float xr =  ca * dx + sa * dy;
            float yr = -sa * dx + ca * dy;
            row[xx] = (fabsf(xr) <= hw && fabsf(yr) <= hh) ? 255u : 0u;
        }
    }
}

const ShapeOps SHAPE_OPS_RECT_ROT = {
    .name           = "rotated_rectangle",
    .param_dim      = 5,
    .random_init    = rrect_random,
    .mutate         = rrect_mutate,
    .compute_bbox   = rrect_compute_bbox,
    .rasterize_mask = rrect_rasterize,
};

/* ============================================================
 * Ellipse axis-aligned : params = [x, y, rx, ry]
 * ============================================================ */

static void ellipse_random(Shape *s, Rng *rng, int w, int h, float size_scale)
{
    s->params[0] = rng_f_range(rng, 0.0f, (float)(w - 1));
    s->params[1] = rng_f_range(rng, 0.0f, (float)(h - 1));
    s->params[2] = rng_f_range(rng, 1.0f, bound_hw(w, size_scale));
    s->params[3] = rng_f_range(rng, 1.0f, bound_hh(h, size_scale));
}

static void ellipse_mutate(Shape *s, Rng *rng, int w, int h)
{
    uint32_t k = rng_u32_range(rng, 3);
    if (k == 0) {
        s->params[0] = gauss_clamp(rng, s->params[0], 16.0f, 0.0f, (float)(w - 1));
        s->params[1] = gauss_clamp(rng, s->params[1], 16.0f, 0.0f, (float)(h - 1));
    } else if (k == 1) {
        s->params[2] = gauss_clamp(rng, s->params[2], 16.0f, 1.0f, (float)w);
        s->params[3] = gauss_clamp(rng, s->params[3], 16.0f, 1.0f, (float)h);
    } else {
        s->params[0] = gauss_clamp(rng, s->params[0], 8.0f, 0.0f, (float)(w - 1));
        s->params[1] = gauss_clamp(rng, s->params[1], 8.0f, 0.0f, (float)(h - 1));
        s->params[2] = gauss_clamp(rng, s->params[2], 8.0f, 1.0f, (float)w);
        s->params[3] = gauss_clamp(rng, s->params[3], 8.0f, 1.0f, (float)h);
    }
}

static void ellipse_compute_bbox(const Shape *s, int w, int h, Bbox *out)
{
    float x = s->params[0], y = s->params[1], rx = s->params[2], ry = s->params[3];
    out->x0 = iclamp((int)floorf(x - rx),        0, w);
    out->y0 = iclamp((int)floorf(y - ry),        0, h);
    out->x1 = iclamp((int)ceilf (x + rx + 1.0f), 0, w);
    out->y1 = iclamp((int)ceilf (y + ry + 1.0f), 0, h);
}

static void ellipse_rasterize(const Shape *s, int w, int h, uint8_t *out_mask, Bbox *bb)
{
    ellipse_compute_bbox(s, w, h, bb);
    if (bbox_empty(bb)) return;
    float x = s->params[0], y = s->params[1];
    float rx = s->params[2], ry = s->params[3];
    if (rx < 1e-6f) rx = 1e-6f;
    if (ry < 1e-6f) ry = 1e-6f;
    float inv_rx2 = 1.0f / (rx * rx);
    float inv_ry2 = 1.0f / (ry * ry);

    for (int yy = bb->y0; yy < bb->y1; ++yy) {
        float dy = (float)yy + 0.5f - y;
        float dy2 = dy * dy * inv_ry2;
        uint8_t *row = out_mask + (size_t)yy * (size_t)w;
        for (int xx = bb->x0; xx < bb->x1; ++xx) {
            float dx = (float)xx + 0.5f - x;
            row[xx] = (dx * dx * inv_rx2 + dy2 <= 1.0f) ? 255u : 0u;
        }
    }
}

const ShapeOps SHAPE_OPS_ELLIPSE = {
    .name           = "ellipse",
    .param_dim      = 4,
    .random_init    = ellipse_random,
    .mutate         = ellipse_mutate,
    .compute_bbox   = ellipse_compute_bbox,
    .rasterize_mask = ellipse_rasterize,
};

/* ============================================================
 * Ellipse tournee : params = [x, y, rx, ry, angle_deg]
 * ============================================================ */

static void rellipse_random(Shape *s, Rng *rng, int w, int h, float size_scale)
{
    s->params[0] = rng_f_range(rng, 0.0f, (float)(w - 1));
    s->params[1] = rng_f_range(rng, 0.0f, (float)(h - 1));
    s->params[2] = rng_f_range(rng, 1.0f, bound_hw(w, size_scale));
    s->params[3] = rng_f_range(rng, 1.0f, bound_hh(h, size_scale));
    s->params[4] = rng_f_range(rng, 0.0f, 180.0f);
}

static void rellipse_mutate(Shape *s, Rng *rng, int w, int h)
{
    uint32_t k = rng_u32_range(rng, 4);
    if (k == 0) {
        s->params[0] = gauss_clamp(rng, s->params[0], 16.0f, 0.0f, (float)(w - 1));
        s->params[1] = gauss_clamp(rng, s->params[1], 16.0f, 0.0f, (float)(h - 1));
    } else if (k == 1) {
        s->params[2] = gauss_clamp(rng, s->params[2], 16.0f, 1.0f, (float)w);
        s->params[3] = gauss_clamp(rng, s->params[3], 16.0f, 1.0f, (float)h);
    } else if (k == 2) {
        float a = s->params[4] + rng_gauss(rng, 25.0f);
        a = fmodf(a, 180.0f);
        if (a < 0.0f) a += 180.0f;
        s->params[4] = a;
    } else {
        s->params[0] = gauss_clamp(rng, s->params[0], 8.0f, 0.0f, (float)(w - 1));
        s->params[1] = gauss_clamp(rng, s->params[1], 8.0f, 0.0f, (float)(h - 1));
        float a = s->params[4] + rng_gauss(rng, 15.0f);
        a = fmodf(a, 180.0f);
        if (a < 0.0f) a += 180.0f;
        s->params[4] = a;
    }
}

static void rellipse_compute_bbox(const Shape *s, int w, int h, Bbox *out)
{
    float x = s->params[0], y = s->params[1];
    float rx = s->params[2], ry = s->params[3];
    float r = rx > ry ? rx : ry; /* enveloppe circulaire conservatrice */
    out->x0 = iclamp((int)floorf(x - r),        0, w);
    out->y0 = iclamp((int)floorf(y - r),        0, h);
    out->x1 = iclamp((int)ceilf (x + r + 1.0f), 0, w);
    out->y1 = iclamp((int)ceilf (y + r + 1.0f), 0, h);
}

static void rellipse_rasterize(const Shape *s, int w, int h, uint8_t *out_mask, Bbox *bb)
{
    rellipse_compute_bbox(s, w, h, bb);
    if (bbox_empty(bb)) return;
    float x = s->params[0], y = s->params[1];
    float rx = s->params[2], ry = s->params[3];
    if (rx < 1e-6f) rx = 1e-6f;
    if (ry < 1e-6f) ry = 1e-6f;
    float rad = s->params[4] * 3.14159265358979f / 180.0f;
    float ca = cosf(rad), sa = sinf(rad);
    float inv_rx2 = 1.0f / (rx * rx);
    float inv_ry2 = 1.0f / (ry * ry);

    for (int yy = bb->y0; yy < bb->y1; ++yy) {
        float dy = (float)yy + 0.5f - y;
        uint8_t *row = out_mask + (size_t)yy * (size_t)w;
        for (int xx = bb->x0; xx < bb->x1; ++xx) {
            float dx = (float)xx + 0.5f - x;
            float xr =  ca * dx + sa * dy;
            float yr = -sa * dx + ca * dy;
            row[xx] = (xr * xr * inv_rx2 + yr * yr * inv_ry2 <= 1.0f) ? 255u : 0u;
        }
    }
}

const ShapeOps SHAPE_OPS_ELLIPSE_ROT = {
    .name           = "rotated_ellipse",
    .param_dim      = 5,
    .random_init    = rellipse_random,
    .mutate         = rellipse_mutate,
    .compute_bbox   = rellipse_compute_bbox,
    .rasterize_mask = rellipse_rasterize,
};

/* ============================================================
 * Triangle : params = [x1, y1, x2, y2, x3, y3]
 * ============================================================ */

static void triangle_random(Shape *s, Rng *rng, int w, int h, float size_scale)
{
    float spread = fmax2(4.0f, (float)imin(w, h) / 8.0f) * size_scale;
    float cx = rng_f_range(rng, 0.0f, (float)(w - 1));
    float cy = rng_f_range(rng, 0.0f, (float)(h - 1));
    for (int v = 0; v < 3; ++v) {
        s->params[v*2+0] = fclampf(cx + rng_gauss(rng, spread), 0.0f, (float)(w - 1));
        s->params[v*2+1] = fclampf(cy + rng_gauss(rng, spread), 0.0f, (float)(h - 1));
    }
}

static void triangle_mutate(Shape *s, Rng *rng, int w, int h)
{
    uint32_t v = rng_u32_range(rng, 3);
    s->params[v*2+0] = gauss_clamp(rng, s->params[v*2+0], 16.0f, 0.0f, (float)(w - 1));
    s->params[v*2+1] = gauss_clamp(rng, s->params[v*2+1], 16.0f, 0.0f, (float)(h - 1));
}

static void triangle_compute_bbox(const Shape *s, int w, int h, Bbox *out)
{
    float x1 = s->params[0], y1 = s->params[1];
    float x2 = s->params[2], y2 = s->params[3];
    float x3 = s->params[4], y3 = s->params[5];
    float mnx = x1; if (x2 < mnx) mnx = x2; if (x3 < mnx) mnx = x3;
    float mny = y1; if (y2 < mny) mny = y2; if (y3 < mny) mny = y3;
    float mxx = x1; if (x2 > mxx) mxx = x2; if (x3 > mxx) mxx = x3;
    float mxy = y1; if (y2 > mxy) mxy = y2; if (y3 > mxy) mxy = y3;
    out->x0 = iclamp((int)floorf(mnx),        0, w);
    out->y0 = iclamp((int)floorf(mny),        0, h);
    out->x1 = iclamp((int)ceilf (mxx + 1.0f), 0, w);
    out->y1 = iclamp((int)ceilf (mxy + 1.0f), 0, h);
}

static void triangle_rasterize(const Shape *s, int w, int h, uint8_t *out_mask, Bbox *bb)
{
    triangle_compute_bbox(s, w, h, bb);
    if (bbox_empty(bb)) return;
    float x1 = s->params[0], y1 = s->params[1];
    float x2 = s->params[2], y2 = s->params[3];
    float x3 = s->params[4], y3 = s->params[5];
    float e1x = x2 - x1, e1y = y2 - y1;
    float e2x = x3 - x2, e2y = y3 - y2;
    float e3x = x1 - x3, e3y = y1 - y3;

    for (int yy = bb->y0; yy < bb->y1; ++yy) {
        float py = (float)yy + 0.5f;
        float py1 = py - y1, py2 = py - y2, py3 = py - y3;
        uint8_t *row = out_mask + (size_t)yy * (size_t)w;
        for (int xx = bb->x0; xx < bb->x1; ++xx) {
            float px = (float)xx + 0.5f;
            float d1 = e1x * py1 - e1y * (px - x1);
            float d2 = e2x * py2 - e2y * (px - x2);
            float d3 = e3x * py3 - e3y * (px - x3);
            int has_neg = (d1 < 0.0f) | (d2 < 0.0f) | (d3 < 0.0f);
            int has_pos = (d1 > 0.0f) | (d2 > 0.0f) | (d3 > 0.0f);
            row[xx] = !(has_neg && has_pos) ? 255u : 0u;
        }
    }
}

const ShapeOps SHAPE_OPS_TRIANGLE = {
    .name           = "triangle",
    .param_dim      = 6,
    .random_init    = triangle_random,
    .mutate         = triangle_mutate,
    .compute_bbox   = triangle_compute_bbox,
    .rasterize_mask = triangle_rasterize,
};

/* ============================================================
 * Registry
 * ============================================================ */

const ShapeOps *const SHAPE_REGISTRY[SHAPE_TYPE_COUNT] = {
    &SHAPE_OPS_CIRCLE,
    &SHAPE_OPS_RECT,
    &SHAPE_OPS_RECT_ROT,
    &SHAPE_OPS_ELLIPSE,
    &SHAPE_OPS_ELLIPSE_ROT,
    &SHAPE_OPS_TRIANGLE,
};

void shape_random(Shape *out, ShapeType type, Rng *rng, int w, int h, float size_scale,
                  uint8_t alpha)
{
    out->ops = SHAPE_REGISTRY[type];
    out->ops->random_init(out, rng, w, h, size_scale);
    out->color[0] = (uint8_t)rng_u32_range(rng, 256);
    out->color[1] = (uint8_t)rng_u32_range(rng, 256);
    out->color[2] = (uint8_t)rng_u32_range(rng, 256);
    out->color[3] = alpha;
}

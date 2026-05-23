#include "scoring.h"
#include "util.h"

#include <math.h>
#include <string.h>

double rms_error(const uint8_t *cur, const uint8_t *tgt,
                  const uint8_t *alpha_mask, int w, int h)
{
    const int n = w * h;
    double sq = 0.0;
    long   count = 0;
    for (int i = 0; i < n; ++i) {
        if (alpha_mask && alpha_mask[i] < 128) continue;
        int dr = (int)cur[i*4+0] - (int)tgt[i*4+0];
        int dg = (int)cur[i*4+1] - (int)tgt[i*4+1];
        int db = (int)cur[i*4+2] - (int)tgt[i*4+2];
        sq += (double)(dr*dr + dg*dg + db*db);
        ++count;
    }
    double n_norm = (double)count * 3.0;
    if (n_norm < 1.0) return 0.0;
    return sqrt(sq / n_norm);
}

void scoring_precompute_baseline(const uint8_t *cur, const uint8_t *tgt,
                                  const uint8_t *alpha_mask,
                                  int w, int h, ScoringBaseline *out)
{
    const int n = w * h;
    double sq = 0.0;
    long   count = 0;
    for (int i = 0; i < n; ++i) {
        if (alpha_mask && alpha_mask[i] < 128) continue;
        int dr = (int)cur[i*4+0] - (int)tgt[i*4+0];
        int dg = (int)cur[i*4+1] - (int)tgt[i*4+1];
        int db = (int)cur[i*4+2] - (int)tgt[i*4+2];
        sq += (double)(dr*dr + dg*dg + db*db);
        ++count;
    }
    out->diff_sq = sq;
    out->n_norm  = (double)count * 3.0;
    if (out->n_norm < 1.0) out->n_norm = 1.0;
}

int compute_optimal_color(const uint8_t *tgt, const uint8_t *cur,
                           const uint8_t *mask, const uint8_t *alpha_mask,
                           int w, int h, const Bbox *bbox,
                           uint8_t alpha_shape, uint8_t out_rgb[3])
{
    (void)h;
    if (bbox_empty(bbox) || alpha_shape == 0) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0;
        return 0;
    }
    const double a = alpha_shape / 255.0;
    if (a < 1e-6) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0;
        return 0;
    }
    const double inv_a   = 1.0 / a;
    const double one_m_a = 1.0 - a;

    double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0;
    long   count = 0;
    for (int y = bbox->y0; y < bbox->y1; ++y) {
        const uint8_t *mrow = mask + (size_t)y * w;
        const uint8_t *arow = alpha_mask ? (alpha_mask + (size_t)y * w) : NULL;
        const uint8_t *crow = cur + (size_t)y * w * 4u;
        const uint8_t *trow = tgt + (size_t)y * w * 4u;
        for (int x = bbox->x0; x < bbox->x1; ++x) {
            if (!mrow[x]) continue;
            if (arow && arow[x] < 128) continue;
            const uint8_t *cp = crow + (size_t)x * 4u;
            const uint8_t *tp = trow + (size_t)x * 4u;
            sum_r += ((double)tp[0] - one_m_a * (double)cp[0]) * inv_a;
            sum_g += ((double)tp[1] - one_m_a * (double)cp[1]) * inv_a;
            sum_b += ((double)tp[2] - one_m_a * (double)cp[2]) * inv_a;
            ++count;
        }
    }
    if (count == 0) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0;
        return 0;
    }
    double inv_c = 1.0 / (double)count;
    out_rgb[0] = (uint8_t)iclamp((int)lround(sum_r * inv_c), 0, 255);
    out_rgb[1] = (uint8_t)iclamp((int)lround(sum_g * inv_c), 0, 255);
    out_rgb[2] = (uint8_t)iclamp((int)lround(sum_b * inv_c), 0, 255);
    return 1;
}

double score_shape(const Shape *shape,
                    const uint8_t *cur, const uint8_t *tgt,
                    const uint8_t *alpha_mask,
                    int w, int h, uint8_t *mask,
                    const ScoringBaseline *baseline, uint8_t out_rgb[3])
{
    Bbox bb;
    shape->ops->rasterize_mask(shape, w, h, mask, &bb);
    if (bbox_empty(&bb)) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0;
        return INFINITY;
    }

    /* Filtre sticker : la shape doit etre essentiellement dans la zone opaque. */
    if (alpha_mask) {
        long body_total = 0, inside = 0;
        for (int y = bb.y0; y < bb.y1; ++y) {
            const uint8_t *mrow = mask + (size_t)y * w;
            const uint8_t *arow = alpha_mask + (size_t)y * w;
            for (int x = bb.x0; x < bb.x1; ++x) {
                if (mrow[x] >= 128) {
                    ++body_total;
                    if (arow[x] >= 128) ++inside;
                }
            }
        }
        if (body_total < 1) return INFINITY;
        if (inside == 0)    return INFINITY;
        if ((double)inside / (double)body_total < STICKER_OVERLAP_MIN) return INFINITY;
    }

    if (!compute_optimal_color(tgt, cur, mask, alpha_mask, w, h, &bb,
                                shape->color[3], out_rgb))
        return INFINITY;

    const double a       = shape->color[3] / 255.0;
    const double one_m_a = 1.0 - a;
    const double src_r   = out_rgb[0];
    const double src_g   = out_rgb[1];
    const double src_b   = out_rgb[2];

    double region_old_sq = 0.0;
    double region_new_sq = 0.0;

    for (int y = bb.y0; y < bb.y1; ++y) {
        const uint8_t *mrow = mask + (size_t)y * w;
        const uint8_t *arow = alpha_mask ? (alpha_mask + (size_t)y * w) : NULL;
        const uint8_t *crow = cur + (size_t)y * w * 4u;
        const uint8_t *trow = tgt + (size_t)y * w * 4u;
        for (int x = bb.x0; x < bb.x1; ++x) {
            if (arow && arow[x] < 128) continue; /* pondere par alpha_mask */

            double cr = crow[x*4+0], cg = crow[x*4+1], cb = crow[x*4+2];
            double tr = trow[x*4+0], tg = trow[x*4+1], tb = trow[x*4+2];

            double dor = cr - tr, dog = cg - tg, dob = cb - tb;
            region_old_sq += dor*dor + dog*dog + dob*dob;

            if (mrow[x]) {
                double br = a * src_r + one_m_a * cr;
                double bg = a * src_g + one_m_a * cg;
                double bb_ = a * src_b + one_m_a * cb;
                double dnr = br - tr, dng = bg - tg, dnb = bb_ - tb;
                region_new_sq += dnr*dnr + dng*dng + dnb*dnb;
            } else {
                region_new_sq += dor*dor + dog*dog + dob*dob;
            }
        }
    }

    double total_sq = baseline->diff_sq - region_old_sq + region_new_sq;
    if (total_sq < 0.0) total_sq = 0.0;
    return sqrt(total_sq / baseline->n_norm);
}

void apply_shape(uint8_t *canvas, int w, int h,
                  const uint8_t *mask, const Bbox *bbox,
                  const uint8_t color_rgb[3], uint8_t alpha_shape)
{
    (void)h;
    if (bbox_empty(bbox)) return;
    unsigned a  = alpha_shape;
    unsigned ia = 255u - a;
    for (int y = bbox->y0; y < bbox->y1; ++y) {
        const uint8_t *mrow = mask + (size_t)y * w;
        uint8_t *crow = canvas + (size_t)y * w * 4u;
        for (int x = bbox->x0; x < bbox->x1; ++x) {
            if (mrow[x]) {
                uint8_t *px = crow + (size_t)x * 4u;
                px[0] = (uint8_t)((color_rgb[0] * a + px[0] * ia + 127u) / 255u);
                px[1] = (uint8_t)((color_rgb[1] * a + px[1] * ia + 127u) / 255u);
                px[2] = (uint8_t)((color_rgb[2] * a + px[2] * ia + 127u) / 255u);
            }
        }
    }
}

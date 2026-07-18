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

double scoring_region_diff_sq(const uint8_t *cur, const uint8_t *tgt,
                               const uint8_t *alpha_mask, int w, const Bbox *bbox)
{
    if (bbox_empty(bbox)) return 0.0;
    double sq = 0.0;
    for (int y = bbox->y0; y < bbox->y1; ++y) {
        const uint8_t *arow = alpha_mask ? (alpha_mask + (size_t)y * w) : NULL;
        const uint8_t *crow = cur + (size_t)y * w * 4u;
        const uint8_t *trow = tgt + (size_t)y * w * 4u;
        for (int x = bbox->x0; x < bbox->x1; ++x) {
            if (arow && arow[x] < 128) continue;
            int dr = (int)crow[x*4+0] - (int)trow[x*4+0];
            int dg = (int)crow[x*4+1] - (int)trow[x*4+1];
            int db = (int)crow[x*4+2] - (int)trow[x*4+2];
            sq += (double)(dr*dr + dg*dg + db*db);
        }
    }
    return sq;
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

/* Fusion de 3 parcours de bbox (filtre sticker, compute_optimal_color, delta)
 * en 2 : compute_optimal_color reste exportee telle quelle (autres call sites
 * potentiels) mais n'est plus appelee ici - sa logique est inlinee pour
 * partager le parcours avec le filtre sticker et masked_old_sq.
 *
 * Passe 1 (fusionnee) : un seul parcours de la bbox accumule (i) les
 * compteurs sticker body_total/inside, (ii) les sommes couleur optimale
 * (identique a compute_optimal_color), (iii) masked_old_sq (diffs^2 des
 * pixels couverts par la shape, cf. note ci-dessous). Le rejet sticker est
 * applique apres coup : meme resultat, un candidat rejete ne fait toujours
 * qu'une passe (la fusion ne coute rien de plus qu'avant dans ce cas).
 *
 * masked_old_sq ne somme que les pixels ou mrow[x] est vrai (couverts par la
 * shape) : dans la formule originale, region_old_sq parcourait toute la
 * bbox et region_new_sq ajoutait exactement la meme valeur pour les pixels
 * non couverts (canvas inchange par apply_shape) - leur contribution
 * s'annule donc exactement dans total_sq, ce n'est pas une approximation.
 *
 * Piege : le filtre sticker teste mrow[x] >= 128 alors que couleur/delta
 * testent mrow[x] via !mrow[x] (verite/faux) - preserve ici tel quel, les
 * deux formes sont equivalentes en pratique (mask binaire 0/255) mais on ne
 * "nettoie" pas cette incoherence existante (cf. plan). */
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

    /* Reproduit les rejets precoces de compute_optimal_color. */
    const uint8_t alpha_shape = shape->color[3];
    if (alpha_shape == 0) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0;
        return INFINITY;
    }
    const double a = alpha_shape / 255.0;
    if (a < 1e-6) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0;
        return INFINITY;
    }
    const double inv_a   = 1.0 / a;
    const double one_m_a = 1.0 - a;

    long   body_total = 0, inside = 0;
    double sum_r = 0.0, sum_g = 0.0, sum_b = 0.0;
    long   color_count = 0;
    double masked_old_sq = 0.0;

    for (int y = bb.y0; y < bb.y1; ++y) {
        const uint8_t *mrow = mask + (size_t)y * w;
        const uint8_t *arow = alpha_mask ? (alpha_mask + (size_t)y * w) : NULL;
        const uint8_t *crow = cur + (size_t)y * w * 4u;
        const uint8_t *trow = tgt + (size_t)y * w * 4u;
        for (int x = bb.x0; x < bb.x1; ++x) {
            if (alpha_mask && mrow[x] >= 128) {
                ++body_total;
                if (arow[x] >= 128) ++inside;
            }

            if (!mrow[x]) continue;
            if (arow && arow[x] < 128) continue;

            const uint8_t *cp = crow + (size_t)x * 4u;
            const uint8_t *tp = trow + (size_t)x * 4u;
            sum_r += ((double)tp[0] - one_m_a * (double)cp[0]) * inv_a;
            sum_g += ((double)tp[1] - one_m_a * (double)cp[1]) * inv_a;
            sum_b += ((double)tp[2] - one_m_a * (double)cp[2]) * inv_a;
            ++color_count;

            double dor = (double)cp[0] - (double)tp[0];
            double dog = (double)cp[1] - (double)tp[1];
            double dob = (double)cp[2] - (double)tp[2];
            masked_old_sq += dor*dor + dog*dog + dob*dob;
        }
    }

    /* Filtre sticker : la shape doit etre essentiellement dans la zone opaque. */
    if (alpha_mask) {
        if (body_total < 1) return INFINITY;
        if (inside == 0)    return INFINITY;
        if ((double)inside / (double)body_total < STICKER_OVERLAP_MIN) return INFINITY;
    }

    if (color_count == 0) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0;
        return INFINITY;
    }
    double inv_c = 1.0 / (double)color_count;
    out_rgb[0] = (uint8_t)iclamp((int)lround(sum_r * inv_c), 0, 255);
    out_rgb[1] = (uint8_t)iclamp((int)lround(sum_g * inv_c), 0, 255);
    out_rgb[2] = (uint8_t)iclamp((int)lround(sum_b * inv_c), 0, 255);

    const double src_r = out_rgb[0];
    const double src_g = out_rgb[1];
    const double src_b = out_rgb[2];

    /* Passe 2 : masked_new_sq, memes pixels que masked_old_sq, couleur connue. */
    double masked_new_sq = 0.0;
    for (int y = bb.y0; y < bb.y1; ++y) {
        const uint8_t *mrow = mask + (size_t)y * w;
        const uint8_t *arow = alpha_mask ? (alpha_mask + (size_t)y * w) : NULL;
        const uint8_t *crow = cur + (size_t)y * w * 4u;
        const uint8_t *trow = tgt + (size_t)y * w * 4u;
        for (int x = bb.x0; x < bb.x1; ++x) {
            if (!mrow[x]) continue;
            if (arow && arow[x] < 128) continue;

            const uint8_t *cp = crow + (size_t)x * 4u;
            const uint8_t *tp = trow + (size_t)x * 4u;
            double br  = a * src_r + one_m_a * (double)cp[0];
            double bg  = a * src_g + one_m_a * (double)cp[1];
            double bb_ = a * src_b + one_m_a * (double)cp[2];
            double dnr = br - (double)tp[0], dng = bg - (double)tp[1], dnb = bb_ - (double)tp[2];
            masked_new_sq += dnr*dnr + dng*dng + dnb*dnb;
        }
    }

    double total_sq = baseline->diff_sq - masked_old_sq + masked_new_sq;
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

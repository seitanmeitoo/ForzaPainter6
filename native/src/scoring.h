#ifndef VINYL_SCORING_H
#define VINYL_SCORING_H

#include <stdint.h>
#include "shapes.h"

/* Tous les buffers sont RGBA 8-bit (stride implicite = w * 4). Le canal A
 * n'est jamais lu par le scoring - il vit pour la GUI uniquement.
 *
 * alpha_mask (uint8 w*h, 0 ou 255) est OPTIONNEL :
 *  - NULL  : mode fond opaque (tous les pixels comptent egalement)
 *  - != NULL : mode "sticker" - seuls les pixels ou alpha_mask >= 128
 *              contribuent au scoring + filtre body_inside / body_total >= 1.0
 *              sur chaque shape candidate (aucun pixel hors silhouette). */

/* 1.0 = zero pixel de la shape autorise hors de la silhouette opaque. Relacher
 * (ex 0.999) si les bords de la silhouette paraissent erodes. */
#define STICKER_OVERLAP_MIN 1.0

typedef struct ScoringBaseline {
    double diff_sq;
    double n_norm;
} ScoringBaseline;

double rms_error(const uint8_t *current_rgba, const uint8_t *target_rgba,
                 const uint8_t *alpha_mask,
                 int w, int h);

void scoring_precompute_baseline(const uint8_t *current_rgba,
                                  const uint8_t *target_rgba,
                                  const uint8_t *alpha_mask,
                                  int w, int h,
                                  ScoringBaseline *out);

/* Couleur RGB optimale en sommant src_pp = (target - (1-a)*current)/a sur
 * les pixels du masque effectif. En sticker, effective = mask AND alpha_mask. */
int compute_optimal_color(const uint8_t *target_rgba,
                           const uint8_t *current_rgba,
                           const uint8_t *mask,
                           const uint8_t *alpha_mask,
                           int w, int h,
                           const Bbox *bbox,
                           uint8_t alpha_shape,
                           uint8_t out_rgb[3]);

/* Score d'une shape candidate. En sticker (alpha_mask != NULL) : rejet
 * (renvoie INFINITY) si la shape sort de la silhouette opaque (inside /
 * body < STICKER_OVERLAP_MIN). */
double score_shape(const Shape *shape,
                    const uint8_t *current_rgba,
                    const uint8_t *target_rgba,
                    const uint8_t *alpha_mask,
                    int w, int h,
                    uint8_t *mask_scratch,
                    const ScoringBaseline *baseline,
                    uint8_t out_rgb[3]);

void apply_shape(uint8_t *canvas_rgba,
                  int w, int h,
                  const uint8_t *mask,
                  const Bbox *bbox,
                  const uint8_t color_rgb[3],
                  uint8_t alpha_shape);

#endif

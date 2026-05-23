#ifndef VINYL_IMAGE_IO_H
#define VINYL_IMAGE_IO_H

#include <stdint.h>
#include <wchar.h>

typedef struct Image {
    int      width;
    int      height;
    int      has_alpha;   /* 1 si le fichier d'origine avait un canal alpha (2 ou 4 channels) */
    uint8_t *rgba;        /* RGBA 8-bit, w*h*4 bytes, top-down, owned */
} Image;

/* Charge un PNG/JPG/BMP depuis un chemin Unicode. Renvoie 0 si OK, -1 sinon.
 * En sortie, *out est initialisé. Le caller doit appeler image_free. */
int  image_load(const wchar_t *path, Image *out);

/* Libère img->rgba et zero le struct. Safe sur Image{0}. */
void image_free(Image *img);

/* Compose le RGBA sur un fond uniforme et écrit du RGB 24-bit dans out_rgb.
 * out_rgb doit faire au moins w*h*3 bytes. */
void image_compose_on_bg(const Image *img,
                         uint8_t bg_r, uint8_t bg_g, uint8_t bg_b,
                         uint8_t *out_rgb);

/* Mediane par canal RGB via histogramme (O(N) lecture + O(256) recherche).
 * Utile comme couleur de fond plus robuste que la moyenne. Alpha ignore. */
void image_median_per_channel(const uint8_t *rgba, int n_pixels, uint8_t out[3]);

/* Extrait un mask binaire {0, 255} depuis le canal alpha (seuil >= 128).
 * out_mask doit faire au moins n_pixels octets. */
void image_extract_alpha_mask(const uint8_t *rgba, int n_pixels, uint8_t *out_mask);

#endif

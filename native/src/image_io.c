#include "image_io.h"

#include <stdio.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_GIF
#define STBI_FAILURE_USERMSG
#include "stb_image.h"
#pragma GCC diagnostic pop

int image_load(const wchar_t *path, Image *out)
{
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));

    FILE *fp = _wfopen(path, L"rb");
    if (!fp) return -1;

    int w = 0, h = 0, comp = 0;
    uint8_t *data = stbi_load_from_file(fp, &w, &h, &comp, 4);
    fclose(fp);

    if (!data) return -1;

    out->width  = w;
    out->height = h;
    out->rgba   = data;

    /* has_alpha = canal alpha present ET au moins un pixel non opaque.
     * Beaucoup de PNG ont un canal alpha entierement a 255 (genere par
     * export Photoshop par defaut) - inutile de payer le mode sticker. */
    out->has_alpha = 0;
    if (comp == 4 || comp == 2) {
        const uint8_t *p   = data + 3;
        const uint8_t *end = data + (size_t)w * (size_t)h * 4u;
        for (; p < end; p += 4) {
            if (*p != 255) { out->has_alpha = 1; break; }
        }
    }
    return 0;
}

void image_free(Image *img)
{
    if (!img) return;
    if (img->rgba) stbi_image_free(img->rgba);
    memset(img, 0, sizeof(*img));
}

void image_extract_alpha_mask(const uint8_t *rgba, int n, uint8_t *out_mask)
{
    for (int i = 0; i < n; ++i) {
        out_mask[i] = (rgba[i*4+3] >= 128) ? 255u : 0u;
    }
}

void image_median_per_channel(const uint8_t *rgba, int n, uint8_t out[3])
{
    int hist[3][256] = {{0}};
    for (int i = 0; i < n; ++i) {
        ++hist[0][rgba[i*4+0]];
        ++hist[1][rgba[i*4+1]];
        ++hist[2][rgba[i*4+2]];
    }
    int target = (n + 1) / 2;
    for (int c = 0; c < 3; ++c) {
        int acc = 0;
        for (int v = 0; v < 256; ++v) {
            acc += hist[c][v];
            if (acc >= target) { out[c] = (uint8_t)v; break; }
        }
    }
}

void image_compose_on_bg(const Image *img,
                         uint8_t bg_r, uint8_t bg_g, uint8_t bg_b,
                         uint8_t *out_rgb)
{
    if (!img || !img->rgba || !out_rgb) return;
    const int n = img->width * img->height;
    const uint8_t *src = img->rgba;
    uint8_t *dst = out_rgb;
    for (int i = 0; i < n; ++i) {
        unsigned a = src[3];
        unsigned ia = 255u - a;
        dst[0] = (uint8_t)((src[0] * a + bg_r * ia + 127u) / 255u);
        dst[1] = (uint8_t)((src[1] * a + bg_g * ia + 127u) / 255u);
        dst[2] = (uint8_t)((src[2] * a + bg_b * ia + 127u) / 255u);
        src += 4;
        dst += 3;
    }
}

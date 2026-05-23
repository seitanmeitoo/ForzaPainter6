#include "fh6_export.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FH6_TYPE_RECT             1
#define FH6_TYPE_ROTATED_ELLIPSE 16

/* Convertit une Shape interne vers les params FH6.
 * out_data : entiers (cx, cy, w, h, [angle]). out_has_angle : 1 si type=16.
 * Renvoie 1 si la conversion est lossy, 0 sinon. */
static int convert_shape(const Shape *s,
                          int *out_type,
                          int out_data[5],
                          int *out_has_angle)
{
    *out_has_angle = 0;
    const ShapeOps *ops = s->ops;
    int cx, cy;

    if (ops == &SHAPE_OPS_CIRCLE) {
        /* params = [cx, cy, r] -> type 16 (sans perte) */
        *out_type = FH6_TYPE_ROTATED_ELLIPSE;
        *out_has_angle = 1;
        cx = (int)lroundf(s->params[0]);
        cy = (int)lroundf(s->params[1]);
        int d = imax(1, (int)lroundf(2.0f * s->params[2]));
        out_data[0] = cx; out_data[1] = cy;
        out_data[2] = d;  out_data[3] = d;
        out_data[4] = 0;
        return 0;
    }
    if (ops == &SHAPE_OPS_RECT) {
        /* params = [cx, cy, hw, hh] -> type 1 (sans perte) */
        *out_type = FH6_TYPE_RECT;
        cx = (int)lroundf(s->params[0]);
        cy = (int)lroundf(s->params[1]);
        out_data[0] = cx; out_data[1] = cy;
        out_data[2] = imax(1, (int)lroundf(2.0f * s->params[2]));
        out_data[3] = imax(1, (int)lroundf(2.0f * s->params[3]));
        return 0;
    }
    if (ops == &SHAPE_OPS_RECT_ROT) {
        /* params = [cx, cy, hw, hh, angle] -> type 16 englobant (LOSSY) */
        *out_type = FH6_TYPE_ROTATED_ELLIPSE;
        *out_has_angle = 1;
        cx = (int)lroundf(s->params[0]);
        cy = (int)lroundf(s->params[1]);
        out_data[0] = cx; out_data[1] = cy;
        out_data[2] = imax(1, (int)lroundf(2.0f * s->params[2]));
        out_data[3] = imax(1, (int)lroundf(2.0f * s->params[3]));
        int ang = (int)lroundf(s->params[4]) % 360;
        if (ang < 0) ang += 360;
        out_data[4] = ang;
        return 1;
    }
    if (ops == &SHAPE_OPS_ELLIPSE) {
        /* params = [cx, cy, rx, ry] -> type 16, angle 0 (sans perte) */
        *out_type = FH6_TYPE_ROTATED_ELLIPSE;
        *out_has_angle = 1;
        cx = (int)lroundf(s->params[0]);
        cy = (int)lroundf(s->params[1]);
        out_data[0] = cx; out_data[1] = cy;
        out_data[2] = imax(1, (int)lroundf(2.0f * s->params[2]));
        out_data[3] = imax(1, (int)lroundf(2.0f * s->params[3]));
        out_data[4] = 0;
        return 0;
    }
    if (ops == &SHAPE_OPS_ELLIPSE_ROT) {
        /* params = [cx, cy, rx, ry, angle] -> type 16 natif (sans perte) */
        *out_type = FH6_TYPE_ROTATED_ELLIPSE;
        *out_has_angle = 1;
        cx = (int)lroundf(s->params[0]);
        cy = (int)lroundf(s->params[1]);
        out_data[0] = cx; out_data[1] = cy;
        out_data[2] = imax(1, (int)lroundf(2.0f * s->params[2]));
        out_data[3] = imax(1, (int)lroundf(2.0f * s->params[3]));
        int ang = (int)lroundf(s->params[4]) % 360;
        if (ang < 0) ang += 360;
        out_data[4] = ang;
        return 0;
    }
    if (ops == &SHAPE_OPS_TRIANGLE) {
        /* params = [x1, y1, x2, y2, x3, y3] -> ellipse englobant la bbox (LOSSY) */
        *out_type = FH6_TYPE_ROTATED_ELLIPSE;
        *out_has_angle = 1;
        float mnx = s->params[0], mxx = mnx;
        float mny = s->params[1], mxy = mny;
        for (int v = 1; v < 3; ++v) {
            float px = s->params[v*2+0];
            float py = s->params[v*2+1];
            if (px < mnx) mnx = px;
            if (px > mxx) mxx = px;
            if (py < mny) mny = py;
            if (py > mxy) mxy = py;
        }
        float wf = mxx - mnx, hf = mxy - mny;
        if (wf < 1.0f) wf = 1.0f;
        if (hf < 1.0f) hf = 1.0f;
        out_data[0] = (int)lroundf((mnx + mxx) * 0.5f);
        out_data[1] = (int)lroundf((mny + mxy) * 0.5f);
        out_data[2] = imax(1, (int)lroundf(wf));
        out_data[3] = imax(1, (int)lroundf(hf));
        out_data[4] = 0;
        return 1;
    }
    /* Shape non reconnue : on emet rien. */
    *out_type = 0;
    return 0;
}

int fh6_count_lossy(const Shape *shapes, int n)
{
    int count = 0;
    int type, data[5], has_angle;
    for (int i = 0; i < n; ++i) {
        if (convert_shape(&shapes[i], &type, data, &has_angle)) ++count;
    }
    return count;
}

int fh6_export_to_file(const Shape *shapes, int n,
                       int img_w, int img_h,
                       const uint8_t bg_color[4],
                       const wchar_t *path)
{
    FILE *fp = _wfopen(path, L"wb");
    if (!fp) return -1;

    fprintf(fp, "{\n  \"shapes\": [\n");

    /* shapes[0] = fond : rectangle pleine surface, type=1. */
    fprintf(fp,
            "    {\"type\": %d, \"data\": [%d, %d, %d, %d], "
            "\"color\": [%u, %u, %u, %u]}",
            FH6_TYPE_RECT, img_w / 2, img_h / 2, img_w, img_h,
            (unsigned)bg_color[0], (unsigned)bg_color[1],
            (unsigned)bg_color[2], (unsigned)bg_color[3]);

    for (int i = 0; i < n; ++i) {
        int type, data[5], has_angle;
        if (!convert_shape(&shapes[i], &type, data, &has_angle)) {
            if (type == 0) continue; /* shape inconnue, skip */
        }
        fprintf(fp, ",\n");
        if (has_angle) {
            fprintf(fp,
                    "    {\"type\": %d, \"data\": [%d, %d, %d, %d, %d], "
                    "\"color\": [%u, %u, %u, %u]}",
                    type, data[0], data[1], data[2], data[3], data[4],
                    (unsigned)shapes[i].color[0], (unsigned)shapes[i].color[1],
                    (unsigned)shapes[i].color[2], (unsigned)shapes[i].color[3]);
        } else {
            fprintf(fp,
                    "    {\"type\": %d, \"data\": [%d, %d, %d, %d], "
                    "\"color\": [%u, %u, %u, %u]}",
                    type, data[0], data[1], data[2], data[3],
                    (unsigned)shapes[i].color[0], (unsigned)shapes[i].color[1],
                    (unsigned)shapes[i].color[2], (unsigned)shapes[i].color[3]);
        }
    }
    fprintf(fp, "\n  ]\n}\n");
    fclose(fp);
    return 0;
}

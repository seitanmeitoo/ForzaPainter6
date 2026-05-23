/* vinyl-painter - native C refactor */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_GDI_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_gdi.h"

#include "image_io.h"
#include "rng.h"
#include "shapes.h"
#include "scoring.h"
#include "engine.h"
#include "threads.h"
#include "profile.h"
#include "fh6_export.h"
#include "util.h"

#include <math.h>

#define WINDOW_W 1280
#define WINDOW_H 800

#define PANEL_BG_R 45
#define PANEL_BG_G 45
#define PANEL_BG_B 48

static const wchar_t WINDOW_CLASS[] = L"VinylPainterMainWindow";
static const wchar_t WINDOW_TITLE[] = L"Vinyl Painter";

/* État image courante (mutated par load_image_into_preview). */
static Image           g_image          = {0};
static struct nk_image g_preview        = {0};
static char            g_path_utf8[MAX_PATH * 4] = {0};
static uint8_t        *g_alpha_mask     = NULL;  /* extrait au load si has_alpha */
static int             g_sticker_enabled = 0;    /* checkbox UI, auto a load */

/* Couleur de fond opaque. Active seulement en mode opaque
 * (image sans alpha, ou alpha mais sticker decoche). */
static int              g_use_custom_bg = 0;
static struct nk_colorf g_bg_colorf     = {1.0f, 1.0f, 1.0f, 1.0f};  /* blanc par defaut */
/* Snapshot du bg reellement utilise au lancement du run (pour l'export FH6). */
static int              g_run_custom_bg = 0;
static uint8_t          g_run_bg[3]     = {0, 0, 0};

/* Rapport multi-ligne du test scoring. */
#define SCORING_REPORT_LINES 8
#define SCORING_REPORT_LINE  96
static char g_scoring_report[SCORING_REPORT_LINES][SCORING_REPORT_LINE] = {{0}};
static int  g_scoring_report_count = 0;

/* Generation asynchrone. */
static int           g_engine_active     = 0;
static EngineThread  g_engine_thread     = {0};
static Engine        g_engine            = {0};
static uint8_t      *g_engine_target     = NULL;
static double        g_engine_start_rms  = 0.0;
static DWORD         g_engine_t0         = 0;
static int           g_engine_stop_at    = 0;

/* Resultat de la derniere generation (snapshot apres engine_free pour
 * permettre l'export FH6 ulterieur). */
static Shape  *g_result_shapes     = NULL;
static int     g_result_count      = 0;
static int     g_result_w          = 0;
static int     g_result_h          = 0;
static uint8_t g_result_bg[4]      = {0, 0, 0, 0};
static int     g_result_available  = 0;
static int     g_result_sticker    = 0;  /* run en mode sticker -> bg alpha=0 a l'export */

/* Etat des checkboxes "types autorises". Defaut : rectangle +
 * rotated_ellipse, comme le legacy Python. Indices alignes sur ShapeType. */
static int g_types_enabled[SHAPE_TYPE_COUNT] = {
    0,  /* SHAPE_CIRCLE */
    1,  /* SHAPE_RECT */
    0,  /* SHAPE_RECT_ROT */
    0,  /* SHAPE_ELLIPSE */
    1,  /* SHAPE_ELLIPSE_ROT */
    0,  /* SHAPE_TRIANGLE */
};
static const char *g_types_labels[SHAPE_TYPE_COUNT] = {
    "Cercle", "Rectangle", "Rect. tourne", "Ellipse", "Ellipse tournee", "Triangle"
};

/* Construit un nk_image (HBITMAP DIB BGR 24-bit top-down, lignes alignees 4 bytes)
 * en composant le RGBA sur le fond uniforme. Remplace le bug de nk_create_image
 * du vendor qui boucle sur le stride pade et fait du R/B swap. */
static struct nk_image build_preview_image(const Image *img,
                                           uint8_t bgR, uint8_t bgG, uint8_t bgB)
{
    struct nk_image out = {0};
    if (!img || !img->rgba || img->width <= 0 || img->height <= 0) return out;

    const int w = img->width;
    const int h = img->height;
    const int row = (w * 3 + 3) & ~3;

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;   /* negatif = top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    bi.bmiHeader.biSizeImage   = (DWORD)row * (DWORD)h;

    void *bits = NULL;
    HBITMAP hbm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm || !bits) return out;

    uint8_t *dst = (uint8_t*)bits;
    const uint8_t *src = img->rgba;
    for (int y = 0; y < h; ++y) {
        uint8_t *p = dst + (size_t)y * (size_t)row;
        for (int x = 0; x < w; ++x) {
            unsigned a  = src[3];
            unsigned ia = 255u - a;
            p[0] = (uint8_t)((src[2] * a + bgB * ia + 127u) / 255u); /* B */
            p[1] = (uint8_t)((src[1] * a + bgG * ia + 127u) / 255u); /* G */
            p[2] = (uint8_t)((src[0] * a + bgR * ia + 127u) / 255u); /* R */
            p   += 3;
            src += 4;
        }
        /* padding (row - w*3) bytes deja a zero dans le DIB */
    }

    out.handle.ptr = hbm;
    out.w = (nk_ushort)w;
    out.h = (nk_ushort)h;
    out.region[0] = 0;
    out.region[1] = 0;
    out.region[2] = (nk_ushort)w;
    out.region[3] = (nk_ushort)h;
    return out;
}

static void free_preview(void)
{
    if (g_preview.handle.ptr) {
        DeleteObject((HBITMAP)g_preview.handle.ptr);
        memset(&g_preview, 0, sizeof(g_preview));
    }
    image_free(&g_image);
    free(g_alpha_mask); g_alpha_mask = NULL;
    g_path_utf8[0] = 0;
}

static void load_image_into_preview(HWND owner, const wchar_t *path)
{
    free_preview();

    if (image_load(path, &g_image) != 0) {
        MessageBoxW(owner, L"Impossible de charger l'image.", WINDOW_TITLE, MB_ICONERROR);
        return;
    }

    g_preview = build_preview_image(&g_image, PANEL_BG_R, PANEL_BG_G, PANEL_BG_B);
    if (!g_preview.handle.ptr) {
        MessageBoxW(owner, L"Creation du HBITMAP de preview echouee.", WINDOW_TITLE, MB_ICONERROR);
        image_free(&g_image);
        return;
    }

    /* Extraire l'alpha_mask pour le mode sticker. Si l'image n'a pas
     * d'alpha effective (canal absent ou tout opaque), on ne stocke rien
     * et la checkbox sera desactivee. */
    if (g_image.has_alpha) {
        g_alpha_mask = (uint8_t*)malloc((size_t)g_image.width * g_image.height);
        if (g_alpha_mask) {
            image_extract_alpha_mask(g_image.rgba, g_image.width * g_image.height,
                                     g_alpha_mask);
            g_sticker_enabled = 1;
        }
    } else {
        g_sticker_enabled = 0;
    }

    WideCharToMultiByte(CP_UTF8, 0, path, -1,
                        g_path_utf8, sizeof(g_path_utf8), NULL, NULL);
}

static void open_file_dialog(HWND owner)
{
    wchar_t buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0Tous (*.*)\0*.*\0";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        load_image_into_preview(owner, buf);
    }
}

/* --- smoke test - genere 10 cercles aleatoires et les affiche --- */
static void run_circles_test(HWND owner, int n_circles)
{
    const int W = 512, H = 512;
    const int N = n_circles;

    uint8_t *canvas = (uint8_t*)malloc((size_t)W * H * 4u);
    uint8_t *mask   = (uint8_t*)malloc((size_t)W * H);
    if (!canvas || !mask) {
        MessageBoxW(owner, L"Allocation canvas/mask echouee.", WINDOW_TITLE, MB_ICONERROR);
        free(canvas); free(mask);
        return;
    }

    /* Fond gris clair pour bien voir les couleurs des cercles. */
    for (int i = 0; i < W * H; ++i) {
        canvas[i*4+0] = 220;
        canvas[i*4+1] = 220;
        canvas[i*4+2] = 220;
        canvas[i*4+3] = 255;
    }

    Rng rng;
    rng_seed(&rng, (uint64_t)GetTickCount64());

    for (int i = 0; i < N; ++i) {
        Shape s;
        shape_random(&s, SHAPE_CIRCLE, &rng, W, H, 1.0f, 180);

        Bbox bb;
        s.ops->rasterize_mask(&s, W, H, mask, &bb);
        if (bbox_empty(&bb)) continue;

        unsigned a  = s.color[3];
        unsigned ia = 255u - a;
        for (int y = bb.y0; y < bb.y1; ++y) {
            const uint8_t *mrow = mask + (size_t)y * W;
            uint8_t *crow = canvas + (size_t)y * W * 4u;
            for (int x = bb.x0; x < bb.x1; ++x) {
                if (mrow[x]) {
                    uint8_t *px = crow + (size_t)x * 4u;
                    px[0] = (uint8_t)((s.color[0] * a + px[0] * ia + 127u) / 255u);
                    px[1] = (uint8_t)((s.color[1] * a + px[1] * ia + 127u) / 255u);
                    px[2] = (uint8_t)((s.color[2] * a + px[2] * ia + 127u) / 255u);
                }
            }
        }
    }
    free(mask);

    /* Remplace l'image courante par le canvas genere. */
    free_preview();
    g_image.width     = W;
    g_image.height    = H;
    g_image.has_alpha = 0;
    g_image.rgba      = canvas;   /* g_image possede le buffer ; image_free fera stbi_image_free */

    g_preview = build_preview_image(&g_image, PANEL_BG_R, PANEL_BG_G, PANEL_BG_B);
    snprintf(g_path_utf8, sizeof(g_path_utf8), "[test : %d cercles aleatoires]", N);
}

/* --- verifie que score_shape (formule delta) correspond au RMS reel
 * apres composite. Utilise l'image courante comme target, init current uniforme
 * sur la moyenne de target, genere 5 cercles, compare pred vs reel. --- */
static void scoring_report_reset(void)
{
    g_scoring_report_count = 0;
    for (int i = 0; i < SCORING_REPORT_LINES; ++i) g_scoring_report[i][0] = 0;
}

static void scoring_report_push(const char *fmt, ...)
{
    if (g_scoring_report_count >= SCORING_REPORT_LINES) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_scoring_report[g_scoring_report_count], SCORING_REPORT_LINE, fmt, ap);
    va_end(ap);
    ++g_scoring_report_count;
}

static void run_scoring_test(HWND owner)
{
    scoring_report_reset();
    if (!g_image.rgba) {
        MessageBoxW(owner, L"Charge d'abord une image cible.", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    const int W = g_image.width;
    const int H = g_image.height;
    const int N_CIRCLES = 5;

    uint8_t *target  = g_image.rgba;
    uint8_t *current = (uint8_t*)malloc((size_t)W * H * 4u);
    uint8_t *test    = (uint8_t*)malloc((size_t)W * H * 4u);
    uint8_t *mask    = (uint8_t*)malloc((size_t)W * H);
    if (!current || !test || !mask) {
        MessageBoxW(owner, L"Allocation echouee.", WINDOW_TITLE, MB_ICONERROR);
        free(current); free(test); free(mask);
        return;
    }

    /* current = moyenne par canal de target (initialisation pragmatique). */
    long sr = 0, sg = 0, sb = 0;
    const int n = W * H;
    for (int i = 0; i < n; ++i) {
        sr += target[i*4+0];
        sg += target[i*4+1];
        sb += target[i*4+2];
    }
    uint8_t mr = (uint8_t)(sr / n);
    uint8_t mg = (uint8_t)(sg / n);
    uint8_t mb = (uint8_t)(sb / n);
    for (int i = 0; i < n; ++i) {
        current[i*4+0] = mr;
        current[i*4+1] = mg;
        current[i*4+2] = mb;
        current[i*4+3] = 255;
    }

    ScoringBaseline baseline;
    scoring_precompute_baseline(current, target, NULL, W, H, &baseline);
    double rms_base = sqrt(baseline.diff_sq / baseline.n_norm);
    scoring_report_push("base %dx%d RMS=%.3f", W, H, rms_base);

    Rng rng;
    rng_seed(&rng, (uint64_t)GetTickCount64());

    double max_delta = 0.0;
    int    matched   = 0;
    for (int i = 0; i < N_CIRCLES; ++i) {
        Shape s;
        shape_random(&s, SHAPE_CIRCLE, &rng, W, H, 1.5f, 128);

        uint8_t color_pred[3];
        double rms_pred = score_shape(&s, current, target, NULL, W, H, mask,
                                       &baseline, color_pred);
        if (!isfinite(rms_pred)) {
            scoring_report_push("C%d : rejete (mask vide)", i);
            continue;
        }

        /* Composite reel : memcpy current -> test, applique le cercle, mesure. */
        memcpy(test, current, (size_t)W * H * 4u);
        Bbox bb;
        s.ops->rasterize_mask(&s, W, H, mask, &bb);
        apply_shape(test, W, H, mask, &bb, color_pred, 128);
        double rms_actual = rms_error(test, target, NULL, W, H);

        double delta = fabs(rms_pred - rms_actual);
        if (delta > max_delta) max_delta = delta;
        if (delta < 0.5) ++matched;

        scoring_report_push("C%d pred=%.4f reel=%.4f d=%.4f",
                            i, rms_pred, rms_actual, delta);
    }
    scoring_report_push("%s : %d/%d, max d=%.4f",
                        (matched == N_CIRCLES) ? "OK" : "ECART",
                        matched, N_CIRCLES, max_delta);

    free(current); free(test); free(mask);
}

/* --- engine asynchrone via thread worker.
 *
 *   start_engine_generation : alloue engine + target_copy + lance le worker.
 *   tick_engine_generation  : appele a chaque frame Nuklear ; draine les
 *                              previews + detecte la fin pour finaliser.
 *   cancel_engine_generation: set le cancel flag (le worker termine sous peu).
 *   teardown_engine_state   : libere tout (utilise au quit). --- */

/* Installe un buffer RGBA (alloue malloc, ownership transferred) dans g_image
 * + rebuild g_preview. Free l'ancien buffer si present. */
static void install_canvas_into_preview(uint8_t *rgba, int w, int h, const char *label)
{
    free_preview();
    g_image.width     = w;
    g_image.height    = h;
    g_image.has_alpha = 0;
    g_image.rgba      = rgba;
    g_preview = build_preview_image(&g_image, PANEL_BG_R, PANEL_BG_G, PANEL_BG_B);
    if (label) snprintf(g_path_utf8, sizeof(g_path_utf8), "%s", label);
}

static void teardown_engine_state(void)
{
    if (g_engine_active) {
        engine_thread_cancel(&g_engine_thread);
        engine_thread_free(&g_engine_thread);
        engine_free(&g_engine);
        free(g_engine_target); g_engine_target = NULL;
        g_engine_active = 0;
    }
}

static void start_engine_generation(HWND owner, int stop_at,
                                    int random_samples, int mutated_samples,
                                    int n_threads)
{
    if (g_engine_active) return;
    scoring_report_reset();
    if (!g_image.rgba) {
        MessageBoxW(owner, L"Charge d'abord une image cible.", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    const int W = g_image.width;
    const int H = g_image.height;
    const size_t bytes = (size_t)W * H * 4u;

    g_engine_target = (uint8_t*)malloc(bytes);
    if (!g_engine_target) {
        MessageBoxW(owner, L"Allocation target_copy echouee.", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    memcpy(g_engine_target, g_image.rgba, bytes);

    uint32_t mask = 0;
    for (int i = 0; i < SHAPE_TYPE_COUNT; ++i)
        if (g_types_enabled[i]) mask |= (1u << i);
    if (mask == 0) {
        /* Aucun type coche : on impose rect par defaut pour eviter le fallback
         * silencieux dans pick_type. */
        MessageBoxW(owner, L"Aucun type de forme coche.", WINDOW_TITLE, MB_ICONWARNING);
        free(g_engine_target); g_engine_target = NULL;
        return;
    }

    EngineConfig cfg = {
        .stop_at         = stop_at,
        .random_samples  = imax(1, random_samples),
        .mutated_samples = imax(1, mutated_samples),
        .types_mask      = mask,
        .n_threads       = imax(1, n_threads),
        .seed            = (uint64_t)GetTickCount64(),
    };
    const uint8_t *amask = (g_sticker_enabled && g_alpha_mask) ? g_alpha_mask : NULL;

    /* Alpha detecte + sticker decoche + couleur custom : on compose l'alpha de
     * l'image sur la couleur choisie (les zones transparentes deviennent cette
     * couleur au lieu du noir des PNG) et on initialise le canvas a cette couleur. */
    g_run_custom_bg = 0;
    if (g_alpha_mask && !g_sticker_enabled && g_use_custom_bg) {
        uint8_t br = (uint8_t)iclamp((int)(g_bg_colorf.r * 255.0f + 0.5f), 0, 255);
        uint8_t bg = (uint8_t)iclamp((int)(g_bg_colorf.g * 255.0f + 0.5f), 0, 255);
        uint8_t bb = (uint8_t)iclamp((int)(g_bg_colorf.b * 255.0f + 0.5f), 0, 255);
        for (int i = 0; i < W * H; ++i) {
            unsigned a  = g_engine_target[i*4+3];
            unsigned ia = 255u - a;
            g_engine_target[i*4+0] = (uint8_t)((g_engine_target[i*4+0]*a + br*ia + 127u)/255u);
            g_engine_target[i*4+1] = (uint8_t)((g_engine_target[i*4+1]*a + bg*ia + 127u)/255u);
            g_engine_target[i*4+2] = (uint8_t)((g_engine_target[i*4+2]*a + bb*ia + 127u)/255u);
            g_engine_target[i*4+3] = 255;
        }
        cfg.use_custom_bg = 1;
        cfg.bg_color[0] = br; cfg.bg_color[1] = bg; cfg.bg_color[2] = bb;
        g_run_custom_bg = 1;
        g_run_bg[0] = br; g_run_bg[1] = bg; g_run_bg[2] = bb;
    }

    if (engine_init(&g_engine, g_engine_target, amask, W, H, &cfg) != 0) {
        free(g_engine_target); g_engine_target = NULL;
        MessageBoxW(owner, L"engine_init echoue.", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    g_engine_start_rms = g_engine.rms;
    g_engine_stop_at   = stop_at;
    g_engine_t0        = GetTickCount();

    int preview_every = imax(1, stop_at / 20); /* ~20 updates pendant le run */
    if (engine_thread_start(&g_engine_thread, &g_engine, preview_every) != 0) {
        engine_free(&g_engine);
        free(g_engine_target); g_engine_target = NULL;
        MessageBoxW(owner, L"Echec demarrage du thread.", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    g_engine_active = 1;
    scoring_report_push("En cours...");
}

static void cancel_engine_generation(void)
{
    if (g_engine_active) engine_thread_cancel(&g_engine_thread);
}

static void tick_engine_generation(void)
{
    if (!g_engine_active) return;

    const int W = g_engine.w;
    const int H = g_engine.h;
    const size_t bytes = (size_t)W * H * 4u;

    /* Drain preview (best-effort). */
    uint8_t *buf = (uint8_t*)malloc(bytes);
    if (buf) {
        int prev_count = 0;
        double prev_rms = 0.0;
        if (engine_thread_take_preview(&g_engine_thread, buf, &prev_count, &prev_rms)) {
            char lbl[128];
            snprintf(lbl, sizeof(lbl), "[engine : %d / %d formes, RMS %.2f]",
                     prev_count, g_engine_stop_at, prev_rms);
            install_canvas_into_preview(buf, W, H, lbl);
            buf = NULL;  /* ownership transferred */
            scoring_report_reset();
            scoring_report_push("En cours : %d/%d", prev_count, g_engine_stop_at);
            scoring_report_push("RMS courant : %.3f", prev_rms);
        }
        free(buf);
    }

    if (engine_thread_done(&g_engine_thread)) {
        /* Worker sorti : recopie le canvas final (le worker n'ecrit plus). */
        engine_thread_join(&g_engine_thread);
        int final_count    = g_engine.shapes_count;
        double final_rms   = g_engine.rms;
        DWORD t1           = GetTickCount();
        double dt          = (double)(t1 - g_engine_t0) / 1000.0;

        uint8_t *final_buf = (uint8_t*)malloc(bytes);
        if (final_buf) {
            memcpy(final_buf, g_engine.canvas, bytes);
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "[engine : %d formes]", final_count);
            install_canvas_into_preview(final_buf, W, H, lbl);
        }

        scoring_report_reset();
        scoring_report_push("%d formes en %.2fs", final_count, dt);
        scoring_report_push("RMS %.3f -> %.3f", g_engine_start_rms, final_rms);
        if (dt > 0.0)
            scoring_report_push("Vitesse : %.1f shapes/s", (double)final_count / dt);

        /* Snapshot pour l'export FH6 ulterieur. */
        free(g_result_shapes); g_result_shapes = NULL;
        if (final_count > 0) {
            g_result_shapes = (Shape*)malloc((size_t)final_count * sizeof(Shape));
            if (g_result_shapes) {
                memcpy(g_result_shapes, g_engine.shapes,
                       (size_t)final_count * sizeof(Shape));
                g_result_count = final_count;
                g_result_w     = W;
                g_result_h     = H;
                g_result_sticker = (g_engine.alpha_mask != NULL);
                if (g_result_sticker) {
                    /* fond transparent : couleur peu importe, FH6 attend juste a=0. */
                    g_result_bg[0] = g_result_bg[1] = g_result_bg[2] = 0;
                    g_result_bg[3] = 0;
                } else if (g_run_custom_bg) {
                    g_result_bg[0] = g_run_bg[0];
                    g_result_bg[1] = g_run_bg[1];
                    g_result_bg[2] = g_run_bg[2];
                    g_result_bg[3] = 255;
                } else {
                    image_median_per_channel(g_engine_target, W * H, g_result_bg);
                    g_result_bg[3] = 255;
                }
                g_result_available = 1;
            }
        }

        engine_thread_free(&g_engine_thread);
        engine_free(&g_engine);
        free(g_engine_target); g_engine_target = NULL;
        g_engine_active = 0;
    }
}

/* --- export JSON FH6. Ouvre GetSaveFileNameW, warning lossy si
 * applicable, ecriture via fh6_export_to_file. --- */
static void save_fh6_dialog(HWND owner)
{
    if (!g_result_available || !g_result_shapes || g_result_count <= 0) {
        MessageBoxW(owner, L"Aucun resultat a exporter. Lance d'abord une generation.",
                    WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    wchar_t buf[MAX_PATH] = L"vinyl.fh6.json";
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = L"JSON FH6 (*.json)\0*.json\0Tous (*.*)\0*.*\0";
    ofn.lpstrDefExt = L"json";
    ofn.Flags       = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;

    int lossy = fh6_count_lossy(g_result_shapes, g_result_count);
    if (lossy > 0) {
        wchar_t msg[256];
        swprintf(msg, 256,
                 L"%d forme(s) seront approximees (rotated_rectangle/triangle).\n"
                 L"Le rendu dans le jeu sera different du preview.\n\nContinuer ?",
                 lossy);
        if (MessageBoxW(owner, msg, WINDOW_TITLE, MB_OKCANCEL | MB_ICONWARNING) != IDOK)
            return;
    }

    if (fh6_export_to_file(g_result_shapes, g_result_count,
                           g_result_w, g_result_h, g_result_bg, buf) != 0) {
        MessageBoxW(owner, L"Erreur a l'ecriture du JSON.", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    scoring_report_push("Export OK (%d lossy)", lossy);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (nk_gdi_handle_event(hwnd, msg, wp, lp))
        return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main(void)
{
    HINSTANCE hInst = GetModuleHandleW(NULL);

    WNDCLASSW wc = {0};
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = WINDOW_CLASS;
    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"RegisterClassW failed", WINDOW_TITLE, MB_ICONERROR);
        return 1;
    }

    RECT rect = { 0, 0, WINDOW_W, WINDOW_H };
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectEx(&rect, style, FALSE, 0);

    HWND wnd = CreateWindowExW(
        0, WINDOW_CLASS, WINDOW_TITLE, style | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInst, NULL
    );
    if (!wnd) {
        MessageBoxW(NULL, L"CreateWindowExW failed", WINDOW_TITLE, MB_ICONERROR);
        return 1;
    }
    HDC dc = GetDC(wnd);

    GdiFont *font = nk_gdifont_create("Segoe UI", 14);
    struct nk_context *ctx = nk_gdi_init(font, dc, WINDOW_W, WINDOW_H);

    int preset_idx       = DEFAULT_PRESET_IDX;
    int last_preset_idx  = -1;   /* force la synchro au premier passage */
    int shapes_count     = PRESETS[DEFAULT_PRESET_IDX].stop_at;
    int random_samples   = PRESETS[DEFAULT_PRESET_IDX].random_samples;
    int mutated_samples  = PRESETS[DEFAULT_PRESET_IDX].mutated_samples;

    /* Detecte le nombre de coeurs au demarrage pour le spinner Threads. */
    SYSTEM_INFO sys_info; GetSystemInfo(&sys_info);
    int n_threads = (int)sys_info.dwNumberOfProcessors;
    if (n_threads < 1)  n_threads = 1;
    if (n_threads > 32) n_threads = 32;

    /* Noms des presets aplatis pour nk_combo. */
    const char *preset_names[N_PRESETS];
    for (int i = 0; i < N_PRESETS; ++i) preset_names[i] = PRESETS[i].name;

    int running = 1;
    while (running) {
        MSG msg;
        nk_input_begin(ctx);
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        nk_input_end(ctx);

        /* Drain les events du thread engine et finalise si termine. */
        tick_engine_generation();

        /* Quand l'utilisateur change le preset, on aligne les 3 spinners
         * sur ses defaults. Ensuite il peut les ajuster individuellement. */
        if (preset_idx != last_preset_idx) {
            shapes_count    = PRESETS[preset_idx].stop_at;
            random_samples  = PRESETS[preset_idx].random_samples;
            mutated_samples = PRESETS[preset_idx].mutated_samples;
            last_preset_idx = preset_idx;
        }

        /* --- Panneau Controles --- */
        if (nk_begin(ctx, "Controles", nk_rect(20, 20, 380, 720),
                NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE))
        {
            nk_layout_row_dynamic(ctx, 22, 1);
            nk_label(ctx, "Image source", NK_TEXT_LEFT);
            nk_label(ctx, g_path_utf8[0] ? g_path_utf8 : "(aucune)", NK_TEXT_LEFT);

            nk_layout_row_dynamic(ctx, 30, 1);
            if (nk_button_label(ctx, "Choisir image...")) {
                open_file_dialog(wnd);
            }
            if (nk_button_label(ctx, "Test : 10 cercles aleatoires")) {
                run_circles_test(wnd, 10);
            }
            if (nk_button_label(ctx, "Test : scoring")) {
                run_scoring_test(wnd);
            }

            if (g_scoring_report_count > 0) {
                nk_layout_row_dynamic(ctx, 16, 1);
                for (int i = 0; i < g_scoring_report_count; ++i) {
                    nk_label(ctx, g_scoring_report[i], NK_TEXT_LEFT);
                }
            }

            if (g_image.rgba) {
                char info[128];
                nk_layout_row_dynamic(ctx, 20, 1);
                snprintf(info, sizeof(info), "Dimensions : %d x %d",
                         g_image.width, g_image.height);
                nk_label(ctx, info, NK_TEXT_LEFT);
                nk_label(ctx, g_image.has_alpha ? "Alpha : oui" : "Alpha : non",
                         NK_TEXT_LEFT);
            }

            nk_layout_row_dynamic(ctx, 8, 1);
            nk_spacing(ctx, 1);

            nk_layout_row_dynamic(ctx, 22, 1);
            nk_label(ctx, "Preset", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 26, 1);
            preset_idx = nk_combo(ctx, preset_names, N_PRESETS, preset_idx, 26,
                                  nk_vec2(340, 200));

            nk_layout_row_dynamic(ctx, 22, 1);
            nk_property_int(ctx, "Nombre de formes", 50, &shapes_count, FH6_MAX_SHAPES, 50, 5);
            nk_property_int(ctx, "Candidats random", 50, &random_samples, 10000, 50, 10);
            nk_property_int(ctx, "Mutations", 10, &mutated_samples, 2000, 10, 5);
            nk_property_int(ctx, "Threads", 1, &n_threads, 32, 1, 1);

            nk_layout_row_dynamic(ctx, 8, 1);
            nk_spacing(ctx, 1);

            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, "Types autorises", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 22, 2);
            for (int i = 0; i < SHAPE_TYPE_COUNT; ++i) {
                nk_checkbox_label(ctx, g_types_labels[i], &g_types_enabled[i]);
            }

            nk_layout_row_dynamic(ctx, 8, 1);
            nk_spacing(ctx, 1);

            /* Checkbox sticker : actif seulement si l'image a un canal alpha. */
            nk_layout_row_dynamic(ctx, 22, 1);
            if (g_alpha_mask) {
                nk_checkbox_label(ctx, "Mode sticker (preserve la transparence)",
                                  &g_sticker_enabled);
            } else {
                nk_label(ctx, "Mode sticker : pas d'alpha detecte", NK_TEXT_LEFT);
            }

            /* Couleur de fond : seulement si alpha detecte ET sticker decoche,
             * i.e. l'utilisateur a choisi de remplacer la transparence par une
             * couleur. Sans alpha, il n'y a pas de fond a remplacer. */
            if (g_alpha_mask && !g_sticker_enabled) {
                nk_layout_row_dynamic(ctx, 22, 1);
                nk_checkbox_label(ctx, "Couleur de fond personnalisee", &g_use_custom_bg);
                if (g_use_custom_bg) {
                    nk_layout_row_dynamic(ctx, 26, 1);
                    if (nk_combo_begin_color(ctx, nk_rgb_cf(g_bg_colorf),
                                             nk_vec2(300, 350))) {
                        nk_layout_row_dynamic(ctx, 120, 1);
                        g_bg_colorf = nk_color_picker(ctx, g_bg_colorf, NK_RGB);
                        nk_layout_row_dynamic(ctx, 22, 1);
                        g_bg_colorf.r = nk_propertyf(ctx, "#R", 0, g_bg_colorf.r, 1.0f, 0.01f, 0.005f);
                        g_bg_colorf.g = nk_propertyf(ctx, "#G", 0, g_bg_colorf.g, 1.0f, 0.01f, 0.005f);
                        g_bg_colorf.b = nk_propertyf(ctx, "#B", 0, g_bg_colorf.b, 1.0f, 0.01f, 0.005f);
                        nk_combo_end(ctx);
                    }
                }
            }

            nk_layout_row_dynamic(ctx, 8, 1);
            nk_spacing(ctx, 1);

            nk_layout_row_dynamic(ctx, 30, 2);
            if (nk_button_label(ctx, "Generer")) {
                start_engine_generation(wnd, shapes_count, random_samples,
                                        mutated_samples, n_threads);
            }
            if (nk_button_label(ctx, "Annuler")) {
                cancel_engine_generation();
            }

            nk_layout_row_dynamic(ctx, 30, 1);
            if (nk_button_label(ctx, "Exporter JSON FH6...")) {
                save_fh6_dialog(wnd);
            }

            nk_layout_row_dynamic(ctx, 8, 1);
            nk_spacing(ctx, 1);

            nk_layout_row_dynamic(ctx, 30, 1);
            if (nk_button_label(ctx, "Quitter")) {
                PostQuitMessage(0);
            }
        }
        nk_end(ctx);

        /* --- Panneau Apercu --- */
        if (nk_begin(ctx, "Apercu", nk_rect(420, 20, 840, 720),
                NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE))
        {
            if (g_preview.handle.ptr) {
                struct nk_rect cr = nk_window_get_content_region(ctx);
                float pad  = 10.0f;
                float maxw = cr.w - 2.0f * pad;
                float maxh = cr.h - 2.0f * pad;
                if (maxw < 1.0f) maxw = 1.0f;
                if (maxh < 1.0f) maxh = 1.0f;

                float ar = (float)g_image.width / (float)g_image.height;
                float dw = maxw;
                float dh = maxw / ar;
                if (dh > maxh) { dh = maxh; dw = maxh * ar; }

                float x = (cr.w - dw) * 0.5f;
                float y = (cr.h - dh) * 0.5f;

                nk_layout_space_begin(ctx, NK_STATIC, cr.h, 1);
                nk_layout_space_push(ctx, nk_rect(x, y, dw, dh));
                nk_image(ctx, g_preview);
                nk_layout_space_end(ctx);
            } else {
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_label(ctx, "Aucune image - clique sur \"Choisir image...\"",
                         NK_TEXT_CENTERED);
            }
        }
        nk_end(ctx);

        nk_gdi_render(nk_rgb(PANEL_BG_R, PANEL_BG_G, PANEL_BG_B));
        /* Limite la boucle a ~120 FPS si rien d'urgent en cours. Sleep court
         * pour ne pas amputer la reactivite de l'engine_thread (preview). */
        Sleep(8);
    }

    teardown_engine_state();
    free(g_result_shapes); g_result_shapes = NULL;
    free_preview();
    nk_gdifont_del(font);
    ReleaseDC(wnd, dc);
    UnregisterClassW(WINDOW_CLASS, hInst);
    return 0;
}

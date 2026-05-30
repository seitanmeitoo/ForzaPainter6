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
#include "fh6_inject.h"
#include "fh6_thread.h"
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

/* Etape 2b : JSON FH6 charge pour apercu + injection (onglet Import). */
static Fh6JsonDoc g_import_doc       = {0};
static int        g_import_loaded    = 0;
static char       g_import_name[256] = {0};
static int        g_import_layer_count = 0;  /* count template in-game ; 0 = auto */

/* Etape 2b : worker de fond pour la localisation/injection (hors thread UI). */
#define FH6_SCAN_BUDGET_MS 120000   /* delai max du scan (cf. forza-painter ref) */
static Fh6Worker g_fh6_worker = {0};
static int       g_fh6_active = 0;
static long      g_fh6_seq    = 0;
static DWORD     g_fh6_t0     = 0;

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
    if (g_fh6_active) {
        MessageBoxW(owner, L"Un scan FH6 est en cours ; attends la fin avant de generer.",
                    WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
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

/* --- Etape 2b : injection memoire FH6. Le profil par defaut est surcharge par
 * un fh6_inject.cfg place a cote de l'exe (offsets/divisors ajustables sans
 * recompiler). Chaque action attache le processus, agit, puis se detache. --- */
static void fh6_cfg_path(wchar_t *out, int n)
{
    wchar_t exe[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) { out[0] = 0; return; }
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (slash) slash[1] = 0; else exe[0] = 0;
    _snwprintf(out, (size_t)n, L"%lsfh6_inject.cfg", exe);
}

static void fh6_report_to_ui(HWND owner, const char *report)
{
    wchar_t wmsg[2048];
    MultiByteToWideChar(CP_UTF8, 0, report, -1, wmsg, 2048);
    MessageBoxW(owner, wmsg, WINDOW_TITLE, MB_ICONINFORMATION);
}

/* Demarre un job FH6 (localisation ou injection) sur le thread de fond. L'UI
 * reste reactive ; tick_fh6_job() draine le statut et finalise. */
static void start_fh6_job(HWND owner, Fh6JobKind kind)
{
    if (g_fh6_active) return;
    if (g_engine_active) {
        MessageBoxW(owner, L"Une generation est en cours ; attends la fin avant d'agir.",
                    WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    if (kind == FH6_JOB_INJECT && !g_import_loaded) {
        MessageBoxW(owner, L"Charge d'abord un JSON (bouton \"Charger un JSON...\").",
                    WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    const wchar_t *prompt = (kind == FH6_JOB_INJECT)
        ? L"L'injection va ECRIRE dans la memoire de Forza Horizon 6.\n\n"
          L"Dans FH6 : ouvre l'editeur de groupes de vinyles, charge un template\n"
          L"de spheres (500 a 3000), DEGROUPE-le, et garde l'editeur au premier plan.\n"
          L"Renseigne le nombre de layers du template (champ \"Layers template\").\n\n"
          L"L'app reste reactive ; tu peux Annuler. Continuer ?"
        : L"Localisation (lecture seule) du groupe de vinyles FH6.\n\n"
          L"Dans FH6 : ouvre l'editeur de groupes de vinyles, charge un template\n"
          L"de spheres (500 a 3000), DEGROUPE-le, et garde l'editeur au premier plan.\n"
          L"Renseigne le nombre de layers du template pour un scan cible (sinon auto).\n\n"
          L"L'app reste reactive ; tu peux Annuler. Continuer ?";
    if (MessageBoxW(owner, prompt, WINDOW_TITLE, MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return;

    Fh6Profile prof;
    fh6_profile_default(&prof);
    wchar_t cfg[MAX_PATH];
    fh6_cfg_path(cfg, MAX_PATH);
    fh6_profile_load_cfg(&prof, cfg);

    const Fh6JsonDoc *doc = (kind == FH6_JOB_INJECT) ? &g_import_doc : NULL;
    if (fh6_worker_start(&g_fh6_worker, kind, &prof, g_import_layer_count, doc,
                         FH6_SCAN_BUDGET_MS) != 0) {
        MessageBoxW(owner, L"Echec demarrage du thread FH6.", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    g_fh6_active = 1;
    g_fh6_seq    = 0;
    g_fh6_t0     = GetTickCount();
    scoring_report_reset();
    scoring_report_push(kind == FH6_JOB_INJECT ? "Injection : scan en cours..."
                                               : "Localisation : scan en cours...");
}

static void cancel_fh6_job(void)
{
    if (g_fh6_active) fh6_worker_cancel(&g_fh6_worker);
}

/* Un seul job de fond a la fois : 1 si engine OU worker FH6 tourne. */
static int any_job_active(void) 
{ 
    return g_engine_active || g_fh6_active; 
}

/* Draine le statut du worker et finalise quand il a termine. Appele chaque frame. */
static void tick_fh6_job(HWND owner)
{
    if (!g_fh6_active) return;

    char line[256];
    if (fh6_worker_take_status(&g_fh6_worker, line, sizeof(line), &g_fh6_seq)) {
        double dt = (double)(GetTickCount() - g_fh6_t0) / 1000.0;
        scoring_report_reset();
        scoring_report_push("%s", line);
        scoring_report_push("%.0fs ecoulees - bouton Annuler pour stopper", dt);
    }

    if (!fh6_worker_done(&g_fh6_worker)) return;

    fh6_worker_join(&g_fh6_worker);
    int  result = g_fh6_worker.result;
    char report[1024];
    strncpy(report, g_fh6_worker.report, sizeof(report) - 1);
    report[sizeof(report) - 1] = 0;
    Fh6JobKind kind = g_fh6_worker.kind;
    fh6_worker_free(&g_fh6_worker);
    g_fh6_active = 0;

    fh6_report_to_ui(owner, report);
    scoring_report_reset();
    if (kind == FH6_JOB_INJECT) {
        if (result >= 0) scoring_report_push("Injection : %d formes ecrites", result);
        else             scoring_report_push("Injection echouee (voir message)");
    } else {
        scoring_report_push(result == 0 ? "Localisation OK (voir message)"
                                        : "Localisation : rien trouve (voir message)");
    }
}

/* Rend l'apercu d'un JSON charge : composite les formes sur un canvas et
 * l'installe dans le panneau Apercu (reutilise la rasterisation de shapes.c). */
static void preview_json_doc(const Fh6JsonDoc *doc)
{
    int W = doc->image_w, H = doc->image_h;
    if (W <= 0 || H <= 0) return;
    uint8_t *canvas = (uint8_t *)malloc((size_t)W * H * 4u);
    uint8_t *mask   = (uint8_t *)malloc((size_t)W * H);
    if (!canvas || !mask) { free(canvas); free(mask); return; }

    uint8_t br = 220, bg = 220, bb = 220;  /* gris neutre pour le mode sticker */
    if (doc->bg_alpha > 0 && doc->count > 0 && doc->shapes[0].is_bg) {
        br = doc->shapes[0].rgba[0];
        bg = doc->shapes[0].rgba[1];
        bb = doc->shapes[0].rgba[2];
    }
    for (int i = 0; i < W * H; ++i) {
        canvas[i*4+0] = br; canvas[i*4+1] = bg; canvas[i*4+2] = bb; canvas[i*4+3] = 255;
    }

    for (int i = 0; i < doc->count; ++i) {
        const Fh6JsonShape *js = &doc->shapes[i];
        if (js->is_bg || js->rgba[3] <= 0) continue;
        Shape s;
        if (js->type == 1) {
            s.ops = &SHAPE_OPS_RECT;
            s.params[0] = js->x; s.params[1] = js->y;
            s.params[2] = js->w * 0.5f; s.params[3] = js->h * 0.5f;
        } else if (js->type == 16) {
            s.ops = &SHAPE_OPS_ELLIPSE_ROT;
            s.params[0] = js->x; s.params[1] = js->y;
            s.params[2] = js->w * 0.5f; s.params[3] = js->h * 0.5f;
            s.params[4] = js->angle;
        } else {
            continue;
        }
        s.color[0] = js->rgba[0]; s.color[1] = js->rgba[1];
        s.color[2] = js->rgba[2]; s.color[3] = js->rgba[3];
        Bbox bbx;
        s.ops->rasterize_mask(&s, W, H, mask, &bbx);
        if (bbox_empty(&bbx)) continue;
        apply_shape(canvas, W, H, mask, &bbx, s.color, s.color[3]);
    }
    free(mask);

    char label[64];
    snprintf(label, sizeof(label), "[JSON : %d formes]", doc->count > 0 ? doc->count - 1 : 0);
    install_canvas_into_preview(canvas, W, H, label);  /* prend possession de canvas */
}

static void run_fh6_load(HWND owner)
{
    wchar_t path[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = L"JSON FH6 (*.json)\0*.json\0Tous (*.*)\0*.*\0";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    Fh6JsonDoc doc;
    if (fh6_load_json(path, &doc) != 0) {
        MessageBoxW(owner, L"Fichier JSON FH6 invalide ou illisible.", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    if (g_import_loaded) fh6_free_json(&g_import_doc);
    g_import_doc = doc;
    g_import_loaded = 1;

    const wchar_t *fn = wcsrchr(path, L'\\');
    fn = fn ? fn + 1 : path;
    WideCharToMultiByte(CP_UTF8, 0, fn, -1, g_import_name, sizeof(g_import_name), NULL, NULL);

    preview_json_doc(&g_import_doc);
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

    int active_tab       = 0;    /* 0 = generation JSON, 1 = import FH6 */
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

    int last_tab = -1;                   /* detecte le changement d'onglet */
    int cw = WINDOW_W, ch = WINDOW_H;   /* taille client courante (mise a jour chaque frame) */

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

        /* Taille client pour le layout responsive (mise a jour chaque frame). */
        { RECT _rc; if (GetClientRect(wnd, &_rc)) { cw = _rc.right; ch = _rc.bottom; } }
        if (cw < 100) cw = 100;
        if (ch < 100) ch = 100;

        /* Drain les events du thread engine et finalise si termine. */
        tick_engine_generation();
        /* Idem pour le worker de localisation/injection FH6. */
        tick_fh6_job(wnd);

        /* Quand l'utilisateur change le preset, on aligne les 3 spinners
         * sur ses defaults. Ensuite il peut les ajuster individuellement. */
        if (preset_idx != last_preset_idx) {
            shapes_count    = PRESETS[preset_idx].stop_at;
            random_samples  = PRESETS[preset_idx].random_samples;
            mutated_samples = PRESETS[preset_idx].mutated_samples;
            last_preset_idx = preset_idx;
        }

        /* Layout responsive : dimensionnement depuis la taille client courante. */
        int ctrl_h = ch - 40; if (ctrl_h < 80) ctrl_h = 80;
        int prev_x = 420;   /* 20 + 380 + 20 */
        int prev_w = cw - 440; if (prev_w < 80) prev_w = 80;   /* 440 = prev_x + 20 */

        /* --- Panneau Controles --- */
        if (nk_begin(ctx, "Controles", nk_rect(20, 20, 380, (float)ctrl_h),
                NK_WINDOW_BORDER | NK_WINDOW_TITLE))
        {
            /* Onglets : Generation JSON / Import FH6 */
            nk_layout_row_dynamic(ctx, 28, 2);
            if (nk_option_label(ctx, "Generation JSON", active_tab == 0)) active_tab = 0;
            if (nk_option_label(ctx, "Import FH6", active_tab == 1)) active_tab = 1;

            /* Efface le statut au changement d'onglet (sauf si un job tourne). */
            if (active_tab != last_tab) {
                if (!any_job_active()) scoring_report_reset();
                last_tab = active_tab;
            }

            nk_layout_row_dynamic(ctx, 6, 1);
            nk_spacing(ctx, 1);

            if (active_tab == 0) {
                /* ===== Onglet Generation JSON ===== */
                nk_layout_row_dynamic(ctx, 22, 1);
                nk_label(ctx, "Image source", NK_TEXT_LEFT);
                nk_label(ctx, g_path_utf8[0] ? g_path_utf8 : "(aucune)", NK_TEXT_LEFT);

                nk_layout_row_dynamic(ctx, 30, 1);
                if (any_job_active()) nk_widget_disable_begin(ctx);
                if (nk_button_label(ctx, "Choisir image...")) {
                    open_file_dialog(wnd);
                }
                if (nk_button_label(ctx, "Test : 10 cercles aleatoires")) {
                    run_circles_test(wnd, 10);
                }
                if (nk_button_label(ctx, "Test : scoring")) {
                    run_scoring_test(wnd);
                }
                if (any_job_active()) nk_widget_disable_end(ctx);

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

                /* Couleur de fond : seulement si alpha detecte ET sticker decoche. */
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

                if (g_engine_active) {
                    /* Generation en cours : seul "Annuler" est accessible. */
                    nk_layout_row_dynamic(ctx, 30, 1);
                    if (nk_button_label(ctx, "Annuler la generation")) {
                        cancel_engine_generation();
                    }
                } else {
                    nk_layout_row_dynamic(ctx, 30, 1);
                    if (g_fh6_active) nk_widget_disable_begin(ctx);
                    if (nk_button_label(ctx, "Generer")) {
                        start_engine_generation(wnd, shapes_count, random_samples,
                                                mutated_samples, n_threads);
                    }
                    if (g_fh6_active) nk_widget_disable_end(ctx);
                }

                nk_layout_row_dynamic(ctx, 30, 1);
                if (any_job_active()) nk_widget_disable_begin(ctx);
                if (nk_button_label(ctx, "Exporter JSON FH6...")) {
                    save_fh6_dialog(wnd);
                }
                if (any_job_active()) nk_widget_disable_end(ctx);
            } else {
                /* ===== Onglet Import FH6 (etape 2b) ===== */
                nk_layout_row_dynamic(ctx, 18, 1);
                nk_label(ctx, "1. Charger un JSON FH6 (apercu + infos)", NK_TEXT_LEFT);

                nk_layout_row_dynamic(ctx, 30, 1);
                if (any_job_active()) nk_widget_disable_begin(ctx);
                if (nk_button_label(ctx, "Charger un JSON...")) {
                    run_fh6_load(wnd);
                }
                if (any_job_active()) nk_widget_disable_end(ctx);

            
                if (g_import_loaded) {
                    char info[160];
                    int drawables = g_import_doc.n_rect + g_import_doc.n_ellipse;
                    nk_layout_row_dynamic(ctx, 18, 1);
                    snprintf(info, sizeof(info), "Fichier : %s", g_import_name);
                    nk_label(ctx, info, NK_TEXT_LEFT);
                    snprintf(info, sizeof(info), "Formes : %d (rect %d, ellipse %d)",
                             drawables, g_import_doc.n_rect, g_import_doc.n_ellipse);
                    nk_label(ctx, info, NK_TEXT_LEFT);
                    snprintf(info, sizeof(info), "Dimensions : %d x %d",
                             g_import_doc.image_w, g_import_doc.image_h);
                    nk_label(ctx, info, NK_TEXT_LEFT);
                    snprintf(info, sizeof(info), "Fond : %s",
                             g_import_doc.bg_alpha > 0 ? "opaque" : "transparent (sticker)");
                    nk_label(ctx, info, NK_TEXT_LEFT);
                } else {
                    nk_layout_row_dynamic(ctx, 18, 1);
                    nk_label(ctx, "(aucun JSON charge)", NK_TEXT_LEFT);
                }

                nk_layout_row_dynamic(ctx, 8, 1);
                nk_spacing(ctx, 1);

                nk_layout_row_dynamic(ctx, 16, 1);
                nk_label(ctx, "2. DANS LE JEU (FH6) : editeur de groupes de", NK_TEXT_LEFT);
                nk_label(ctx, "   vinyles ouvert, template de spheres CHARGE", NK_TEXT_LEFT);
                nk_label(ctx, "   (500 a 3000) puis DEGROUPE. Garder au 1er plan.", NK_TEXT_LEFT);
                nk_label(ctx, "   Admin si version Microsoft Store/Xbox.", NK_TEXT_LEFT);

                nk_layout_row_dynamic(ctx, 22, 1);
                nk_property_int(ctx, "Layers template (0=auto)", 0,
                                &g_import_layer_count, FH6_MAX_SHAPES, 50, 5);

                nk_layout_row_dynamic(ctx, 8, 1);
                nk_spacing(ctx, 1);

                if (g_fh6_active) {
                    nk_layout_row_dynamic(ctx, 30, 1);
                    if (nk_button_label(ctx, "Annuler le scan FH6")) {
                        cancel_fh6_job();
                    }
                } else {
                    nk_layout_row_dynamic(ctx, 30, 1);
                    if (g_engine_active) nk_widget_disable_begin(ctx);
                    if (nk_button_label(ctx, "Localiser le groupe FH6 (test)")) {
                        start_fh6_job(wnd, FH6_JOB_LOCATE);
                    }
                    if (nk_button_label(ctx, "Injecter dans le jeu")) {
                        start_fh6_job(wnd, FH6_JOB_INJECT);
                    }
                    if (g_engine_active) nk_widget_disable_end(ctx);
                }

                nk_layout_row_dynamic(ctx, 16, 1);
                nk_label(ctx, "Etape 2b : a valider in-game.", NK_TEXT_LEFT);
            }
            /* Zone de statut partagee (tests / generation / injection). */
            if (g_scoring_report_count > 0) {
                nk_layout_row_dynamic(ctx, 8, 1);
                nk_spacing(ctx, 1);
                nk_layout_row_dynamic(ctx, 16, 1);
                for (int i = 0; i < g_scoring_report_count; ++i) {
                    nk_label(ctx, g_scoring_report[i], NK_TEXT_LEFT);
                }
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
        if (nk_begin(ctx, "Apercu", nk_rect((float)prev_x, 20, (float)prev_w, (float)ctrl_h),
                NK_WINDOW_BORDER | NK_WINDOW_TITLE))
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
        /* Si un job tourne : poll court pour ne pas amputer la reactivite
         * (previews engine, statut FH6). Sinon : veille CPU jusqu'a la
         * prochaine entree (souris / clavier / WM_PAINT) -> ~0 % CPU au repos. */
        if (any_job_active()) {
            Sleep(8);
        } else {
            MsgWaitForMultipleObjectsEx(0, NULL, INFINITE, QS_ALLINPUT,
                                        MWMO_INPUTAVAILABLE);
        }
    }

    teardown_engine_state();
    if (g_fh6_active) {
        fh6_worker_cancel(&g_fh6_worker);
        fh6_worker_free(&g_fh6_worker);  /* join + cleanup */
        g_fh6_active = 0;
    }
    free(g_result_shapes); g_result_shapes = NULL;
    fh6_free_json(&g_import_doc);
    free_preview();
    nk_gdifont_del(font);
    ReleaseDC(wnd, dc);
    UnregisterClassW(WINDOW_CLASS, hInst);
    return 0;
}

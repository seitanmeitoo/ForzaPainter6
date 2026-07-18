/* Harnais de test console (make test). Compile a part de main.c (GUI) :
 * memes flags que la release, pour tester ce qui ship reellement. */

#include "engine.h"
#include "scoring.h"
#include "shapes.h"
#include "rng.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cible synthetique procedurale : degrade + quelques rectangles, aucun
 * fichier requis. RGBA, alpha toujours 255. */
static uint8_t *build_synthetic_target(int w, int h)
{
    uint8_t *buf = (uint8_t*)malloc((size_t)w * (size_t)h * 4u);
    if (!buf) return NULL;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t *px = buf + ((size_t)y * w + x) * 4u;
            px[0] = (uint8_t)(x * 255 / (w - 1));
            px[1] = (uint8_t)(y * 255 / (h - 1));
            px[2] = (uint8_t)((x + y) * 255 / (w + h - 2));
            px[3] = 255;
        }
    }
    /* Quelques rectangles unis pour casser le degrade pur. */
    struct { int x0, y0, x1, y1; uint8_t r, g, b; } rects[] = {
        { 20,  20,  80,  90, 220,  30,  30 },
        { 140, 30, 220, 100,  30, 220,  30 },
        { 60, 140, 200, 220,  30,  30, 220 },
    };
    for (size_t i = 0; i < sizeof(rects) / sizeof(rects[0]); ++i) {
        for (int y = rects[i].y0; y < rects[i].y1 && y < h; ++y) {
            for (int x = rects[i].x0; x < rects[i].x1 && x < w; ++x) {
                uint8_t *px = buf + ((size_t)y * w + x) * 4u;
                px[0] = rects[i].r; px[1] = rects[i].g; px[2] = rects[i].b; px[3] = 255;
            }
        }
    }
    return buf;
}

/* --- Test 1 : equivalence scoring (score_shape predit vs RMS reel apres
 * composite). Port console de run_scoring_test (main.c). --- */
static int test_scoring_equivalence(void)
{
    const int W = 256, H = 256;
    const int N_CIRCLES = 5;
    int ok = 1;

    uint8_t *target  = build_synthetic_target(W, H);
    uint8_t *current = (uint8_t*)malloc((size_t)W * H * 4u);
    uint8_t *test    = (uint8_t*)malloc((size_t)W * H * 4u);
    uint8_t *mask    = (uint8_t*)malloc((size_t)W * H);
    if (!target || !current || !test || !mask) {
        printf("[FAIL] test_scoring_equivalence: allocation echouee\n");
        free(target); free(current); free(test); free(mask);
        return 0;
    }

    long sr = 0, sg = 0, sb = 0;
    const int n = W * H;
    for (int i = 0; i < n; ++i) {
        sr += target[i*4+0]; sg += target[i*4+1]; sb += target[i*4+2];
    }
    uint8_t mr = (uint8_t)(sr / n), mg = (uint8_t)(sg / n), mb = (uint8_t)(sb / n);
    for (int i = 0; i < n; ++i) {
        current[i*4+0] = mr; current[i*4+1] = mg; current[i*4+2] = mb; current[i*4+3] = 255;
    }

    ScoringBaseline baseline;
    scoring_precompute_baseline(current, target, NULL, W, H, &baseline);

    Rng rng;
    rng_seed(&rng, 12345ull);

    double max_delta = 0.0;
    int matched = 0;
    for (int i = 0; i < N_CIRCLES; ++i) {
        Shape s;
        shape_random(&s, SHAPE_CIRCLE, &rng, W, H, 1.5f, 128);

        uint8_t color_pred[3];
        double rms_pred = score_shape(&s, current, target, NULL, W, H, mask,
                                       &baseline, color_pred);
        if (!isfinite(rms_pred)) {
            printf("  C%d : rejete (mask vide)\n", i);
            continue;
        }

        memcpy(test, current, (size_t)W * H * 4u);
        Bbox bb;
        s.ops->rasterize_mask(&s, W, H, mask, &bb);
        apply_shape(test, W, H, mask, &bb, color_pred, 128);
        double rms_actual = rms_error(test, target, NULL, W, H);

        double delta = fabs(rms_pred - rms_actual);
        if (delta > max_delta) max_delta = delta;
        if (delta < 0.5) ++matched;
        printf("  C%d pred=%.4f reel=%.4f d=%.4f\n", i, rms_pred, rms_actual, delta);
    }
    ok = (matched == N_CIRCLES);
    printf("[%s] test_scoring_equivalence: %d/%d, max d=%.4f\n",
           ok ? "PASS" : "FAIL", matched, N_CIRCLES, max_delta);

    free(target); free(current); free(test); free(mask);
    return ok;
}

/* --- Test 2 : sentinelles isfinite/INFINITY. Peut deja echouer sous
 * -ffast-math (attendu avant la phase 2, cf. rapport de phase). --- */
static int test_sentinels(void)
{
    int ok = 1;

    if (isfinite(INFINITY)) {
        printf("  isfinite(INFINITY) == true (attendu false)\n");
        ok = 0;
    }

    const int W = 64, H = 64;
    uint8_t *target  = build_synthetic_target(W, H);
    uint8_t *current = (uint8_t*)calloc((size_t)W * H * 4u, 1);
    uint8_t *mask    = (uint8_t*)malloc((size_t)W * H);
    if (!target || !current || !mask) {
        printf("[FAIL] test_sentinels: allocation echouee\n");
        free(target); free(current); free(mask);
        return 0;
    }

    ScoringBaseline baseline;
    scoring_precompute_baseline(current, target, NULL, W, H, &baseline);

    /* Cercle centre tres loin hors canvas -> bbox vide apres clamp. */
    Shape s = { .ops = &SHAPE_OPS_CIRCLE };
    s.params[0] = -10000.0f;
    s.params[1] = -10000.0f;
    s.params[2] = 1.0f;
    s.color[0] = s.color[1] = s.color[2] = 128; s.color[3] = 128;

    uint8_t rgb[3];
    double rms = score_shape(&s, current, target, NULL, W, H, mask, &baseline, rgb);
    if (isfinite(rms)) {
        printf("  score_shape(bbox vide) = %.4f, attendu non-fini\n", rms);
        ok = 0;
    }

    printf("[%s] test_sentinels\n", ok ? "PASS" : "FAIL");

    free(target); free(current); free(mask);
    return ok;
}

/* --- Test 3 : determinisme + bench. seed fixe, 1 thread -> run reproductible. --- */
static int test_determinism_and_bench(void)
{
    const int W = 256, H = 256;
    int ok = 1;

    uint8_t *target = build_synthetic_target(W, H);
    if (!target) {
        printf("[FAIL] test_determinism_and_bench: allocation echouee\n");
        return 0;
    }

    EngineConfig cfg = {
        .stop_at         = 100,
        .random_samples  = 200,
        .mutated_samples = 50,
        .types_mask      = (1u << SHAPE_TYPE_COUNT) - 1u,
        .n_threads       = 1,
        .seed            = 42ull,
        .use_custom_bg   = 0,
    };

    Engine e;
    if (engine_init(&e, target, NULL, W, H, &cfg) != 0) {
        printf("[FAIL] test_determinism_and_bench: engine_init echoue\n");
        free(target);
        return 0;
    }
    double start_rms = e.start_rms;

    ULONGLONG t0 = GetTickCount64();
    engine_run(&e, NULL, NULL, 0);
    ULONGLONG t1 = GetTickCount64();

    double final_rms = e.rms;
    int shapes_done = e.shapes_count;
    engine_free(&e);

    printf("  1 thread : %d formes, RMS %.3f -> %.3f, %llu ms\n",
           shapes_done, start_rms, final_rms, (unsigned long long)(t1 - t0));

    if (!(final_rms < start_rms)) {
        printf("  RMS final (%.3f) >= RMS initial (%.3f)\n", final_rms, start_rms);
        ok = 0;
    }

    /* 4 threads : imprime uniquement, non asserte (parallelisme non deterministe). */
    cfg.n_threads = 4;
    Engine e4;
    if (engine_init(&e4, target, NULL, W, H, &cfg) == 0) {
        ULONGLONG t2 = GetTickCount64();
        engine_run(&e4, NULL, NULL, 0);
        ULONGLONG t3 = GetTickCount64();
        printf("  4 threads (info) : %d formes, RMS %.3f -> %.3f, %llu ms\n",
               e4.shapes_count, e4.start_rms, e4.rms, (unsigned long long)(t3 - t2));
        engine_free(&e4);
    }

    printf("[%s] test_determinism_and_bench\n", ok ? "PASS" : "FAIL");

    free(target);
    return ok;
}

int main(void)
{
    int r1 = test_scoring_equivalence();
    int r2 = test_sentinels();
    int r3 = test_determinism_and_bench();

    int all_ok = r1 && r2 && r3;
    printf("\n=== %s (scoring=%s sentinels=%s determinism=%s) ===\n",
           all_ok ? "TOUS LES TESTS PASSENT" : "ECHEC",
           r1 ? "OK" : "FAIL", r2 ? "OK" : "FAIL", r3 ? "OK" : "FAIL");

    return all_ok ? 0 : 1;
}

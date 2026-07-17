#include "engine.h"
#include "image_io.h"
#include "util.h"

#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ENGINE_MAX_THREADS 32

/* Type de job dispatche au pool de workers. */
enum { JOB_RANDOM = 0, JOB_MUTATE = 1 };

/* Forward decls : pool defini plus bas, mais utilise par engine_init/free. */
static int         detect_cpu_count(void);
static WorkerPool *pool_create(Engine *e, int n_workers, uint64_t base_seed);
static void        pool_destroy(WorkerPool *p);

/* Heuristiques portees de legacy/shapegen/engine.py */
#define ALPHA_HIGH        220
#define ALPHA_LOW         100
#define SIZE_SCALE_START  3.0f
#define SIZE_SCALE_END    1.0f
#define SIZE_SCALE_RAMP   0.5f

static int alpha_for_stop_at(int stop_at)
{
    if (stop_at <= 200)  return ALPHA_HIGH;
    if (stop_at >= 3000) return ALPHA_LOW;
    float t = (float)(stop_at - 200) / (float)(3000 - 200);
    return (int)lroundf(ALPHA_HIGH + (ALPHA_LOW - ALPHA_HIGH) * t);
}

static float size_scale_for_progress(float progress)
{
    if (progress >= SIZE_SCALE_RAMP) return SIZE_SCALE_END;
    float t = progress / SIZE_SCALE_RAMP;
    return SIZE_SCALE_START + (SIZE_SCALE_END - SIZE_SCALE_START) * t;
}

int engine_init(Engine *e, const uint8_t *target_rgba,
                const uint8_t *alpha_mask, int w, int h,
                const EngineConfig *cfg)
{
    memset(e, 0, sizeof(*e));
    e->w = w; e->h = h;
    e->config = *cfg;
    e->alpha_init = alpha_for_stop_at(cfg->stop_at);

    rng_seed(&e->rng, cfg->seed ? cfg->seed : 0xCAFE1234DEADBEEFull);

    const int n_px = w * h;
    e->target = (uint8_t*)malloc((size_t)n_px * 4u);
    e->canvas = (uint8_t*)malloc((size_t)n_px * 4u);
    e->mask   = (uint8_t*)malloc((size_t)n_px);
    if (!e->target || !e->canvas || !e->mask) {
        engine_free(e);
        return -1;
    }
    memcpy(e->target, target_rgba, (size_t)n_px * 4u);

    if (alpha_mask) {
        e->alpha_mask = (uint8_t*)malloc((size_t)n_px);
        if (!e->alpha_mask) {
            engine_free(e);
            return -1;
        }
        memcpy(e->alpha_mask, alpha_mask, (size_t)n_px);
        /* Masque RGB du target hors silhouette : evite que les pixels
         * transparents (souvent stockes en noir dans les PNG) ne contaminent
         * la couleur optimale via les bords des shapes. */
        for (int i = 0; i < n_px; ++i) {
            if (e->alpha_mask[i] < 128) {
                e->target[i*4+0] = 0;
                e->target[i*4+1] = 0;
                e->target[i*4+2] = 0;
            }
        }
        /* Sticker : canvas init uniforme gris fonce (porte de Python). */
        for (int i = 0; i < n_px; ++i) {
            e->canvas[i*4+0] = 40;
            e->canvas[i*4+1] = 40;
            e->canvas[i*4+2] = 40;
            e->canvas[i*4+3] = 255;
        }
    } else {
        /* Mode opaque : canvas = couleur custom si demandee, sinon mediane par canal. */
        uint8_t bg[3];
        if (cfg->use_custom_bg) {
            bg[0] = cfg->bg_color[0];
            bg[1] = cfg->bg_color[1];
            bg[2] = cfg->bg_color[2];
        } else {
            image_median_per_channel(e->target, n_px, bg);
        }
        for (int i = 0; i < n_px; ++i) {
            e->canvas[i*4+0] = bg[0];
            e->canvas[i*4+1] = bg[1];
            e->canvas[i*4+2] = bg[2];
            e->canvas[i*4+3] = 255;
        }
    }

    ScoringBaseline base;
    scoring_precompute_baseline(e->canvas, e->target, e->alpha_mask, w, h, &base);
    e->rms = sqrt(base.diff_sq / base.n_norm);
    e->start_rms = e->rms;

    /* Pool de workers : 0 = auto (nb de coeurs), 1 = sequentiel, >=2 = pool actif. */
    int requested = cfg->n_threads <= 0 ? detect_cpu_count() : cfg->n_threads;
    if (requested < 1) requested = 1;
    if (requested > ENGINE_MAX_THREADS) requested = ENGINE_MAX_THREADS;
    if (requested >= 2) {
        e->pool = pool_create(e, requested,
                              cfg->seed ? cfg->seed : 0xCAFE1234DEADBEEFull);
        e->n_workers = e->pool ? requested : 1;
    } else {
        e->n_workers = 1;
    }
    return 0;
}

void engine_free(Engine *e)
{
    if (!e) return;
    pool_destroy(e->pool);
    e->pool = NULL;
    e->n_workers = 0;
    free(e->canvas);     e->canvas     = NULL;
    free(e->target);     e->target     = NULL;
    free(e->alpha_mask); e->alpha_mask = NULL;
    free(e->mask);       e->mask       = NULL;
    free(e->shapes);     e->shapes     = NULL;
    e->shapes_count = e->shapes_cap = 0;
}

void engine_request_stop(Engine *e) { e->cancel = 1; }

static void shapes_push(Engine *e, const Shape *s)
{
    if (e->shapes_count >= e->shapes_cap) {
        int new_cap = e->shapes_cap ? e->shapes_cap * 2 : 64;
        Shape *na = (Shape*)realloc(e->shapes, (size_t)new_cap * sizeof(Shape));
        if (!na) return;
        e->shapes = na;
        e->shapes_cap = new_cap;
    }
    e->shapes[e->shapes_count++] = *s;
}

/* Tire un type uniformement parmi les bits set du mask. Fallback CIRCLE si 0. */
static ShapeType pick_type(Rng *rng, uint32_t mask)
{
    ShapeType allowed[SHAPE_TYPE_COUNT];
    int n = 0;
    for (int i = 0; i < SHAPE_TYPE_COUNT; ++i) {
        if (mask & (1u << i)) allowed[n++] = (ShapeType)i;
    }
    if (n == 0) return SHAPE_CIRCLE;
    return allowed[rng_u32_range(rng, (uint32_t)n)];
}

/* ============================================================
 * Worker pool : paralleliser best_of_random.
 * Le main thread (= le thread "engine" unique, lui-meme un worker
 * vis-a-vis de la GUI) prepare un job partage et signale tous les
 * workers via leur start_event ; ils prennent chacun un sous-batch,
 * le scorent en local, et signalent leur done_event. Le main reduit
 * les N bests en un seul.
 * canvas/target/baseline sont read-only pendant le job - pas de lock. */

typedef struct WorkerCtx {
    HANDLE         thread;
    HANDLE         start_event;
    HANDLE         done_event;
    Rng            rng;
    uint8_t       *mask;            /* scratch w*h octets, owned */
    int            worker_idx;
    volatile LONG  shutdown;
    /* best local apres execution du dernier job */
    double         local_best_rms;
    Shape          local_best_shape;
    uint8_t        local_best_rgb[3];
    /* pointer back pour accder au job partage et a l'engine */
    struct WorkerPool *pool;
} WorkerCtx;

struct WorkerPool {
    Engine          *engine;
    int              n_workers;
    WorkerCtx       *workers;
    HANDLE          *done_events;    /* array taille n_workers pour WaitForMultipleObjects */
    /* job partage (read-only par les workers pendant l'execution) */
    int              job_kind;       /* JOB_RANDOM | JOB_MUTATE */
    int              job_n_total;    /* RANDOM : N tirages (split) ; MUTATE : profondeur de chaine par worker */
    float            job_size_scale; /* JOB_RANDOM seulement */
    uint8_t          job_alpha;      /* JOB_RANDOM seulement */
    uint32_t         job_types_mask; /* JOB_RANDOM seulement */
    const Shape     *job_base_shape; /* JOB_MUTATE : shape de base a muter (read-only) */
    double           job_base_rms;   /* JOB_MUTATE : rms de la shape de base */
    uint8_t          job_base_rgb[3];/* JOB_MUTATE : couleur de la shape de base */
    const ScoringBaseline *job_baseline;
};

static int detect_cpu_count(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
    if (n < 1) n = 1;
    if (n > ENGINE_MAX_THREADS) n = ENGINE_MAX_THREADS;
    return n;
}

static DWORD WINAPI worker_proc(LPVOID arg)
{
    WorkerCtx *w = (WorkerCtx*)arg;
    for (;;) {
        WaitForSingleObject(w->start_event, INFINITE);
        if (InterlockedCompareExchange(&w->shutdown, 0, 0)) break;

        WorkerPool *p = w->pool;
        Engine     *e = p->engine;

        double  best_rms = INFINITY;
        Shape   best_shape;
        uint8_t best_rgb[3] = {0, 0, 0};

        if (p->job_kind == JOB_MUTATE) {
            /* Multi-start hill climb : chaque worker raffine une chaine complete
             * et independante depuis la meme shape de base, avec son propre rng.
             * On garde la meilleure des n_workers chaines. Profondeur = job_n_total
             * (== mutated_samples), preservee contrairement au batching. */
            Shape   cur = *p->job_base_shape;
            double  cur_rms = p->job_base_rms;
            uint8_t cur_rgb[3] = { p->job_base_rgb[0],
                                   p->job_base_rgb[1],
                                   p->job_base_rgb[2] };
            for (int i = 0; i < p->job_n_total; ++i) {
                if (e->cancel) break;
                Shape candidate = cur;
                candidate.ops->mutate(&candidate, &w->rng, e->w, e->h);
                uint8_t rgb[3];
                double rms = score_shape(&candidate, e->canvas, e->target,
                                         e->alpha_mask, e->w, e->h, w->mask,
                                         p->job_baseline, rgb);
                if (rms < cur_rms) {
                    cur_rms = rms;
                    cur = candidate;
                    cur_rgb[0] = rgb[0]; cur_rgb[1] = rgb[1]; cur_rgb[2] = rgb[2];
                }
            }
            best_rms     = cur_rms;
            best_shape   = cur;
            best_rgb[0]  = cur_rgb[0]; best_rgb[1] = cur_rgb[1]; best_rgb[2] = cur_rgb[2];
        } else {
            int N = p->job_n_total;
            int per = (N + p->n_workers - 1) / p->n_workers;
            int i_start = w->worker_idx * per;
            int i_end   = i_start + per;
            if (i_end > N) i_end = N;
            for (int i = i_start; i < i_end; ++i) {
                if (e->cancel) break;
                ShapeType t = pick_type(&w->rng, p->job_types_mask);
                Shape candidate;
                shape_random(&candidate, t, &w->rng, e->w, e->h,
                             p->job_size_scale, p->job_alpha);
                uint8_t rgb[3];
                double rms = score_shape(&candidate, e->canvas, e->target,
                                         e->alpha_mask, e->w, e->h, w->mask,
                                         p->job_baseline, rgb);
                if (rms < best_rms) {
                    best_rms = rms;
                    best_shape = candidate;
                    best_rgb[0] = rgb[0]; best_rgb[1] = rgb[1]; best_rgb[2] = rgb[2];
                }
            }
        }
        w->local_best_rms      = best_rms;
        w->local_best_shape    = best_shape;
        w->local_best_rgb[0]   = best_rgb[0];
        w->local_best_rgb[1]   = best_rgb[1];
        w->local_best_rgb[2]   = best_rgb[2];
        SetEvent(w->done_event);
    }
    return 0;
}

static WorkerPool *pool_create(Engine *e, int n_workers, uint64_t base_seed)
{
    if (n_workers < 2) return NULL;
    if (n_workers > ENGINE_MAX_THREADS) n_workers = ENGINE_MAX_THREADS;

    WorkerPool *p = (WorkerPool*)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->engine      = e;
    p->n_workers   = n_workers;
    p->workers     = (WorkerCtx*)calloc((size_t)n_workers, sizeof(WorkerCtx));
    p->done_events = (HANDLE*)calloc((size_t)n_workers, sizeof(HANDLE));
    if (!p->workers || !p->done_events) goto fail;

    for (int i = 0; i < n_workers; ++i) {
        WorkerCtx *w = &p->workers[i];
        w->worker_idx  = i;
        w->pool        = p;
        w->mask        = (uint8_t*)malloc((size_t)e->w * (size_t)e->h);
        w->start_event = CreateEventW(NULL, FALSE, FALSE, NULL); /* auto-reset */
        w->done_event  = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!w->mask || !w->start_event || !w->done_event) goto fail;
        rng_seed(&w->rng, base_seed ^ (0x9E3779B97F4A7C15ull * (uint64_t)(i + 1)));
        p->done_events[i] = w->done_event;
        w->thread = CreateThread(NULL, 0, worker_proc, w, 0, NULL);
        if (!w->thread) goto fail;
    }
    return p;

fail:
    if (p) {
        if (p->workers) {
            for (int i = 0; i < n_workers; ++i) {
                WorkerCtx *w = &p->workers[i];
                if (w->thread) { InterlockedExchange(&w->shutdown, 1);
                                 SetEvent(w->start_event);
                                 WaitForSingleObject(w->thread, INFINITE);
                                 CloseHandle(w->thread); }
                if (w->start_event) CloseHandle(w->start_event);
                if (w->done_event)  CloseHandle(w->done_event);
                free(w->mask);
            }
            free(p->workers);
        }
        free(p->done_events);
        free(p);
    }
    return NULL;
}

static void pool_destroy(WorkerPool *p)
{
    if (!p) return;
    for (int i = 0; i < p->n_workers; ++i) {
        InterlockedExchange(&p->workers[i].shutdown, 1);
        SetEvent(p->workers[i].start_event);
    }
    for (int i = 0; i < p->n_workers; ++i) {
        WaitForSingleObject(p->workers[i].thread, INFINITE);
        CloseHandle(p->workers[i].thread);
        CloseHandle(p->workers[i].start_event);
        CloseHandle(p->workers[i].done_event);
        free(p->workers[i].mask);
    }
    free(p->workers);
    free(p->done_events);
    free(p);
}

/* Distribue N candidats sur tous les workers, attend, reduit. */
static double best_of_random_mt(Engine *e, int N, float size_scale,
                                 const ScoringBaseline *baseline,
                                 Shape *out_shape, uint8_t out_rgb[3])
{
    WorkerPool *p = e->pool;
    p->job_kind       = JOB_RANDOM;
    p->job_n_total    = N;
    p->job_size_scale = size_scale;
    p->job_alpha      = (uint8_t)e->alpha_init;
    p->job_types_mask = e->config.types_mask;
    p->job_baseline   = baseline;
    for (int i = 0; i < p->n_workers; ++i) SetEvent(p->workers[i].start_event);
    WaitForMultipleObjects((DWORD)p->n_workers, p->done_events, TRUE, INFINITE);

    double best_rms = INFINITY;
    for (int i = 0; i < p->n_workers; ++i) {
        WorkerCtx *w = &p->workers[i];
        if (w->local_best_rms < best_rms) {
            best_rms      = w->local_best_rms;
            *out_shape    = w->local_best_shape;
            out_rgb[0]    = w->local_best_rgb[0];
            out_rgb[1]    = w->local_best_rgb[1];
            out_rgb[2]    = w->local_best_rgb[2];
        }
    }
    return best_rms;
}

/* Multi-start parallel hill_climb : chaque worker lance une chaine de raffinement
 * complete (K mutations, profondeur preservee) depuis la meme shape de base, avec
 * son propre rng. On garde la meilleure des n_workers chaines. Le hill_climb d'une
 * shape est intrinsequement sequentiel (chaque mutation part du best courant) donc
 * on ne peut pas raccourcir son wall-clock sans perdre en qualite ; a la place on
 * exploite les coeurs (inactifs pendant le hill_climb mono-thread) pour explorer
 * K trajectoires en parallele et garder la meilleure -> qualite >= sequentiel. */
static double hill_climb_mt(Engine *e, int K, const ScoringBaseline *baseline,
                            Shape *shape, double initial_rms, uint8_t rgb[3])
{
    WorkerPool *p = e->pool;
    p->job_kind       = JOB_MUTATE;
    p->job_n_total    = K;
    p->job_baseline   = baseline;
    p->job_base_shape = shape;
    p->job_base_rms   = initial_rms;
    p->job_base_rgb[0] = rgb[0];
    p->job_base_rgb[1] = rgb[1];
    p->job_base_rgb[2] = rgb[2];

    for (int i = 0; i < p->n_workers; ++i) SetEvent(p->workers[i].start_event);
    WaitForMultipleObjects((DWORD)p->n_workers, p->done_events, TRUE, INFINITE);

    double best_rms = initial_rms;
    for (int i = 0; i < p->n_workers; ++i) {
        WorkerCtx *w = &p->workers[i];
        if (w->local_best_rms < best_rms) {
            best_rms   = w->local_best_rms;
            *shape     = w->local_best_shape;
            rgb[0]     = w->local_best_rgb[0];
            rgb[1]     = w->local_best_rgb[1];
            rgb[2]     = w->local_best_rgb[2];
        }
    }
    return best_rms;
}

/* Trouve la meilleure shape parmi N tirages aleatoires.
 * out_shape recoit la shape (sans la couleur optimale), out_rgb la couleur. */
static double best_of_random(Engine *e, int N, float size_scale,
                              const ScoringBaseline *baseline,
                              Shape *out_shape, uint8_t out_rgb[3])
{
    double best_rms = INFINITY;
    Shape candidate;
    uint8_t cand_rgb[3];
    for (int i = 0; i < N; ++i) {
        if (e->cancel) break;
        ShapeType t = pick_type(&e->rng, e->config.types_mask);
        shape_random(&candidate, t, &e->rng, e->w, e->h,
                     size_scale, (uint8_t)e->alpha_init);
        double rms = score_shape(&candidate, e->canvas, e->target,
                                 e->alpha_mask, e->w, e->h, e->mask,
                                 baseline, cand_rgb);
        if (rms < best_rms) {
            best_rms = rms;
            *out_shape = candidate;
            out_rgb[0] = cand_rgb[0]; out_rgb[1] = cand_rgb[1]; out_rgb[2] = cand_rgb[2];
        }
    }
    return best_rms;
}

/* Hill climb : K mutations, garde la meilleure. shape/rgb mis a jour in-place. */
static double hill_climb(Engine *e, int K, const ScoringBaseline *baseline,
                          Shape *shape, double initial_rms, uint8_t rgb[3])
{
    double best_rms = initial_rms;
    Shape candidate;
    uint8_t cand_rgb[3];
    for (int i = 0; i < K; ++i) {
        if (e->cancel) break;
        candidate = *shape;
        candidate.ops->mutate(&candidate, &e->rng, e->w, e->h);
        double rms = score_shape(&candidate, e->canvas, e->target,
                                 e->alpha_mask, e->w, e->h, e->mask,
                                 baseline, cand_rgb);
        if (rms < best_rms) {
            best_rms = rms;
            *shape = candidate;
            rgb[0] = cand_rgb[0]; rgb[1] = cand_rgb[1]; rgb[2] = cand_rgb[2];
        }
    }
    return best_rms;
}

void engine_run(Engine *e, EngineCallback on_event, void *user_data,
                int preview_every)
{
    /* Anti-blocage : en mode sticker, une forme doit etre 100 % dans la silhouette
     * (STICKER_OVERLAP_MIN). Au debut size_scale vaut 3.0 -> les formes sont trop
     * grandes pour tenir dans une petite silhouette -> toutes rejetees (rms = inf).
     * Comme size_scale derive de la progression (qui reste a 0 tant qu'aucune forme
     * n'est commitee), la boucle tournerait a l'infini. On retrecit donc les formes
     * a chaque iteration sterile (et on relache doucement apres un succes) ; si meme
     * a taille minimale rien ne tient (silhouette degeneree), on s'arrete proprement. */
    int stall = 0;
    const int   STALL_MAX  = 60;
    const float STALL_SHRINK = 0.85f;
    const float SIZE_FLOOR = 0.05f;
    const int   BASELINE_RESYNC_EVERY = 256;

    /* Baseline maintenue incrementalement (voir bloc commit ci-dessous) au lieu
     * d'etre recalculee plein-image a chaque iteration : n_norm et alpha_mask ne
     * changent jamais pendant un run. */
    ScoringBaseline baseline;
    scoring_precompute_baseline(e->canvas, e->target, e->alpha_mask,
                                 e->w, e->h, &baseline);

    while (e->shapes_count < e->config.stop_at && !e->cancel) {
        float progress = (float)e->shapes_count / (float)e->config.stop_at;
        float size_scale = size_scale_for_progress(progress);
        for (int s = 0; s < stall; ++s) size_scale *= STALL_SHRINK;
        if (size_scale < SIZE_FLOOR) size_scale = SIZE_FLOOR;

        Shape best;
        uint8_t best_rgb[3] = {0, 0, 0};
        double rms_random;
        if (e->pool) {
            rms_random = best_of_random_mt(e, e->config.random_samples, size_scale,
                                            &baseline, &best, best_rgb);
        } else {
            rms_random = best_of_random(e, e->config.random_samples, size_scale,
                                         &baseline, &best, best_rgb);
        }
        if (!isfinite(rms_random)) {
            /* Aucune forme placable a cette taille : retrecir et reessayer. */
            if (e->cancel) break;
            if (++stall >= STALL_MAX) break;
            continue;
        }
        if (stall > 0) --stall;  /* succes : relache vers des formes plus grandes */

        if (e->pool) {
            (void)hill_climb_mt(e, e->config.mutated_samples, &baseline,
                                &best, rms_random, best_rgb);
        } else {
            (void)hill_climb(e, e->config.mutated_samples, &baseline,
                              &best, rms_random, best_rgb);
        }

        /* Re-rasterize la shape "best" (le mask scratch contient la derniere
         * shape testee, pas forcement la best). Commit avec sa couleur. */
        best.color[0] = best_rgb[0];
        best.color[1] = best_rgb[1];
        best.color[2] = best_rgb[2];
        best.color[3] = (uint8_t)e->alpha_init;

        Bbox bb;
        best.ops->rasterize_mask(&best, e->w, e->h, e->mask, &bb);
        double old_sq = scoring_region_diff_sq(e->canvas, e->target, e->alpha_mask, e->w, &bb);
        apply_shape(e->canvas, e->w, e->h, e->mask, &bb, best_rgb, (uint8_t)e->alpha_init);
        double new_sq = scoring_region_diff_sq(e->canvas, e->target, e->alpha_mask, e->w, &bb);
        baseline.diff_sq += new_sq - old_sq;
        if (baseline.diff_sq < 0.0) baseline.diff_sq = 0.0;
        shapes_push(e, &best);

        /* Garde anti-derive : purge le bruit de sommation fp accumule par le delta. */
        if (e->shapes_count % BASELINE_RESYNC_EVERY == 0) {
            scoring_precompute_baseline(e->canvas, e->target, e->alpha_mask,
                                         e->w, e->h, &baseline);
        }
        e->rms = sqrt(baseline.diff_sq / baseline.n_norm);

        if (on_event) {
            EngineEvent ev = { EE_SHAPE_COMMITTED, e->shapes_count, e->rms };
            on_event(&ev, user_data);
            if (preview_every > 0 && (e->shapes_count % preview_every == 0)) {
                EngineEvent pv = { EE_PREVIEW, e->shapes_count, e->rms };
                on_event(&pv, user_data);
            }
        }
    }

    /* Resync finale : le rms rapporte a EE_DONE est exact, pas seulement incremental. */
    scoring_precompute_baseline(e->canvas, e->target, e->alpha_mask, e->w, e->h, &baseline);
    e->rms = sqrt(baseline.diff_sq / baseline.n_norm);

    if (on_event) {
        EngineEvent ev = { EE_DONE, e->shapes_count, e->rms };
        on_event(&ev, user_data);
    }
}

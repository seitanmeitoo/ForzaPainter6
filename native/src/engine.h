#ifndef VINYL_ENGINE_H
#define VINYL_ENGINE_H

#include <stdint.h>
#include "shapes.h"
#include "rng.h"
#include "scoring.h"

typedef struct EngineConfig {
    int      stop_at;          /* nombre de formes a generer */
    int      random_samples;   /* N candidats par best_of_random */
    int      mutated_samples;  /* K iterations dans hill_climb */
    uint32_t types_mask;       /* bit i = 1 -> SHAPE_TYPE i autorise */
    int      n_threads;        /* 0 = auto-detect (nb de coeurs), clip [1..32] */
    uint64_t seed;             /* 0 = depuis GetTickCount64 */
    int      use_custom_bg;    /* mode opaque : 1 = canvas init = bg_color, 0 = mediane */
    uint8_t  bg_color[3];      /* couleur de fond custom (si use_custom_bg) */
} EngineConfig;

/* Pool de workers pour paralleliser best_of_random. Definition opaque -
 * voir engine.c pour le detail. */
typedef struct WorkerPool WorkerPool;

typedef enum EngineEventKind {
    EE_SHAPE_COMMITTED = 0,
    EE_PREVIEW,
    EE_DONE,
} EngineEventKind;

typedef struct EngineEvent {
    EngineEventKind kind;
    int    shape_count;
    double rms;
} EngineEvent;

typedef struct Engine {
    int             w, h;
    uint8_t        *target;        /* RGBA, owned (copie du target caller) */
    uint8_t        *alpha_mask;    /* mask {0,255} w*h, owned, NULL si mode opaque */
    uint8_t        *canvas;        /* RGBA, owned */
    Shape          *shapes;        /* dynamic array, owned */
    int             shapes_count;
    int             shapes_cap;

    Rng             rng;
    EngineConfig    config;
    double          rms;
    double          start_rms;
    int             alpha_init;
    volatile int    cancel;        /* set par engine_request_stop (autre thread autorise) */

    uint8_t        *mask;          /* scratch w*h octets, utilise par hill_climb */
    WorkerPool     *pool;          /* NULL si n_threads <= 1, sinon pool actif */
    int             n_workers;     /* taille effective du pool (1 = sequentiel) */
} Engine;

typedef void (*EngineCallback)(const EngineEvent *ev, void *user_data);

/* engine_init copie target_rgba (et alpha_mask si fourni) en interne,
 * donc le caller peut liberer ses buffers apres l'appel.
 * - alpha_mask : NULL = mode fond opaque, sinon buffer w*h {0,255} qui
 *                active le mode sticker (zones transparentes du target
 *                seront mises a 0 dans la copie interne, canvas init
 *                uniforme gris 40,40,40 au lieu de la mediane).
 * Renvoie 0 si OK, -1 si allocation echouee. */
int  engine_init(Engine *e, const uint8_t *target_rgba,
                 const uint8_t *alpha_mask, int w, int h,
                 const EngineConfig *cfg);
void engine_free(Engine *e);
void engine_request_stop(Engine *e);

/* Boucle synchrone : sort quand shapes_count == stop_at ou cancel.
 * preview_every : 0 = pas de PREVIEW events ; sinon callback PREVIEW
 * tous les N shapes commit. EE_SHAPE_COMMITTED appele apres chaque shape. */
void engine_run(Engine *e, EngineCallback on_event, void *user_data,
                int preview_every);

#endif

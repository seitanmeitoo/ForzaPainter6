#ifndef VINYL_THREADS_H
#define VINYL_THREADS_H

#include <windows.h>
#include <stdint.h>
#include "engine.h"

/* Hote Win32 pour faire tourner Engine dans son propre thread. Communication
 * main <-> worker via une CRITICAL_SECTION et un buffer preview single-slot
 * (on accepte de perdre les previews intermediaires si le main ne draine pas
 * assez vite ; c'est OK pour des updates visuelles toutes les K shapes). */
typedef struct EngineThread {
    HANDLE             hThread;
    Engine            *engine;
    int                preview_every;

    CRITICAL_SECTION   lock;
    int                has_preview;       /* true quand un canvas est dispo */
    int                preview_count;     /* shape_count au moment du snapshot */
    double             preview_rms;
    uint8_t           *preview_canvas;    /* alloue 1x, taille w*h*4 */
    int                preview_w, preview_h;

    /* Stats temps-reel (updates SHAPE_COMMITTED). */
    int                live_count;
    double             live_rms;

    volatile LONG      done;              /* 1 quand le worker a fini */
    volatile LONG      final_count;       /* shape_count final */
    volatile double    final_rms;
} EngineThread;

/* Initialise et demarre le thread. L'engine doit etre prepare (engine_init
 * appele en amont). Le caller transfere la propriete logique pendant la duree
 * du run, mais ne doit PAS toucher a l'engine jusqu'a engine_thread_join. */
int  engine_thread_start(EngineThread *t, Engine *e, int preview_every);

/* Test non-bloquant : 1 si le worker a fini, 0 sinon. */
int  engine_thread_done(const EngineThread *t);

/* Si un preview est dispo, copie le canvas dans dst_rgba (w*h*4) et remplit
 * *out_count / *out_rms ; renvoie 1. Sinon renvoie 0. Non bloquant. */
int  engine_thread_take_preview(EngineThread *t, uint8_t *dst_rgba,
                                 int *out_count, double *out_rms);

/* Snapshot des stats temps-reel sans preview (cheap, lock court). */
void engine_thread_get_stats(EngineThread *t, int *out_count, double *out_rms);

/* Demande l'annulation propre. Le worker termine au prochain check. */
void engine_thread_cancel(EngineThread *t);

/* Attend la fin du worker (blocking). Sortie : le canvas final est dans
 * t->engine->canvas, accessible directement apres ce point. */
void engine_thread_join(EngineThread *t);

/* Libere les ressources internes (lock, preview_canvas, handle). NE libere
 * PAS l'engine - c'est au caller. */
void engine_thread_free(EngineThread *t);

#endif

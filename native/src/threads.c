#include "threads.h"

#include <stdlib.h>
#include <string.h>

/* Callback Engine -> thread worker. Tourne dans le worker, push dans le slot
 * partage avec le main via la CRITICAL_SECTION. */
static void thread_engine_cb(const EngineEvent *ev, void *user_data)
{
    EngineThread *t = (EngineThread*)user_data;
    EnterCriticalSection(&t->lock);
    t->live_count = ev->shape_count;
    t->live_rms   = ev->rms;
    if (ev->kind == EE_PREVIEW && t->preview_canvas && t->engine->canvas) {
        memcpy(t->preview_canvas, t->engine->canvas,
               (size_t)t->preview_w * (size_t)t->preview_h * 4u);
        t->preview_count = ev->shape_count;
        t->preview_rms   = ev->rms;
        t->has_preview   = 1;
    }
    LeaveCriticalSection(&t->lock);
}

static DWORD WINAPI thread_proc(LPVOID arg)
{
    EngineThread *t = (EngineThread*)arg;
    engine_run(t->engine, thread_engine_cb, t, t->preview_every);
    InterlockedExchange((LONG volatile*)&t->final_count, t->engine->shapes_count);
    /* final_rms est un double, on le pose simplement sous le lock. */
    EnterCriticalSection(&t->lock);
    t->final_rms = t->engine->rms;
    LeaveCriticalSection(&t->lock);
    InterlockedExchange(&t->done, 1);
    return 0;
}

int engine_thread_start(EngineThread *t, Engine *e, int preview_every)
{
    memset(t, 0, sizeof(*t));
    t->engine        = e;
    t->preview_every = preview_every;
    t->preview_w     = e->w;
    t->preview_h     = e->h;
    t->preview_canvas = (uint8_t*)malloc((size_t)e->w * (size_t)e->h * 4u);
    if (!t->preview_canvas) return -1;
    InitializeCriticalSection(&t->lock);

    t->hThread = CreateThread(NULL, 0, thread_proc, t, 0, NULL);
    if (!t->hThread) {
        DeleteCriticalSection(&t->lock);
        free(t->preview_canvas);
        t->preview_canvas = NULL;
        return -1;
    }
    return 0;
}

int engine_thread_done(const EngineThread *t)
{
    return InterlockedCompareExchange((LONG volatile*)&t->done, 0, 0) != 0;
}

int engine_thread_take_preview(EngineThread *t, uint8_t *dst, int *out_count,
                                double *out_rms)
{
    int taken = 0;
    EnterCriticalSection(&t->lock);
    if (t->has_preview && dst) {
        memcpy(dst, t->preview_canvas,
               (size_t)t->preview_w * (size_t)t->preview_h * 4u);
        if (out_count) *out_count = t->preview_count;
        if (out_rms)   *out_rms   = t->preview_rms;
        t->has_preview = 0;
        taken = 1;
    }
    LeaveCriticalSection(&t->lock);
    return taken;
}

void engine_thread_get_stats(EngineThread *t, int *out_count, double *out_rms)
{
    EnterCriticalSection(&t->lock);
    if (out_count) *out_count = t->live_count;
    if (out_rms)   *out_rms   = t->live_rms;
    LeaveCriticalSection(&t->lock);
}

void engine_thread_cancel(EngineThread *t)
{
    if (t->engine) engine_request_stop(t->engine);
}

void engine_thread_join(EngineThread *t)
{
    if (t->hThread) {
        WaitForSingleObject(t->hThread, INFINITE);
        CloseHandle(t->hThread);
        t->hThread = NULL;
    }
}

void engine_thread_free(EngineThread *t)
{
    engine_thread_join(t);
    if (t->preview_canvas) {
        free(t->preview_canvas);
        t->preview_canvas = NULL;
    }
    DeleteCriticalSection(&t->lock);
    memset(t, 0, sizeof(*t));
}

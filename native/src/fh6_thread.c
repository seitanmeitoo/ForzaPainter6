#include "fh6_thread.h"

#include <string.h>

/* Callback de statut : appele depuis le worker, pousse la ligne sous lock. */
static void worker_on_status(void *ctx, const char *msg)
{
    Fh6Worker *w = (Fh6Worker *)ctx;
    EnterCriticalSection(&w->lock);
    strncpy(w->status, msg, sizeof(w->status) - 1);
    w->status[sizeof(w->status) - 1] = 0;
    ++w->status_seq;
    LeaveCriticalSection(&w->lock);
}

static DWORD WINAPI fh6_thread_proc(LPVOID arg)
{
    Fh6Worker *w = (Fh6Worker *)arg;

    Fh6Progress prog;
    prog.cancel      = &w->cancel;
    prog.on_status   = worker_on_status;
    prog.ctx         = w;
    prog.deadline_ms = w->deadline_ms;

    Fh6Session sess;
    char rep[1024] = {0};
    int rc = fh6_attach(&sess, &w->profile, rep, sizeof(rep));
    if (rc == 0) {
        if (w->kind == FH6_JOB_INJECT)
            rc = fh6_inject_doc(&sess, w->doc, w->want_count, &prog, rep, sizeof(rep));
        else
            rc = fh6_locate(&sess, w->want_count, &prog, rep, sizeof(rep));
        fh6_detach(&sess);
    }

    EnterCriticalSection(&w->lock);
    strncpy(w->report, rep, sizeof(w->report) - 1);
    w->report[sizeof(w->report) - 1] = 0;
    w->result = rc;
    LeaveCriticalSection(&w->lock);

    InterlockedExchange(&w->done, 1);
    return 0;
}

int fh6_worker_start(Fh6Worker *w, Fh6JobKind kind, const Fh6Profile *prof,
                     int want_count, const Fh6JsonDoc *doc, int budget_ms)
{
    memset(w, 0, sizeof(*w));
    w->kind        = kind;
    w->profile     = *prof;
    w->want_count  = want_count;
    w->doc         = doc;
    w->deadline_ms = (budget_ms > 0)
                   ? GetTickCount64() + (unsigned long long)budget_ms : 0;
    InitializeCriticalSection(&w->lock);

    w->hThread = CreateThread(NULL, 0, fh6_thread_proc, w, 0, NULL);
    if (!w->hThread) {
        DeleteCriticalSection(&w->lock);
        return -1;
    }
    return 0;
}

int fh6_worker_done(const Fh6Worker *w)
{
    return InterlockedCompareExchange((LONG volatile *)&w->done, 0, 0) != 0;
}

int fh6_worker_take_status(Fh6Worker *w, char *dst, int dstsz, long *io_seq)
{
    int taken = 0;
    EnterCriticalSection(&w->lock);
    if (w->status_seq != *io_seq && dst && dstsz > 0) {
        strncpy(dst, w->status, (size_t)dstsz - 1);
        dst[dstsz - 1] = 0;
        *io_seq = w->status_seq;
        taken = 1;
    }
    LeaveCriticalSection(&w->lock);
    return taken;
}

void fh6_worker_cancel(Fh6Worker *w)
{
    InterlockedExchange(&w->cancel, 1);
}

void fh6_worker_join(Fh6Worker *w)
{
    if (w->hThread) {
        WaitForSingleObject(w->hThread, INFINITE);
        CloseHandle(w->hThread);
        w->hThread = NULL;
    }
}

void fh6_worker_free(Fh6Worker *w)
{
    fh6_worker_join(w);
    DeleteCriticalSection(&w->lock);
    memset(w, 0, sizeof(*w));
}
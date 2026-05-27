#ifndef VINYL_FH6_THREAD_H
#define VINYL_FH6_THREAD_H

#include <windows.h>
#include "fh6_inject.h"

/* Hote Win32 pour faire tourner les scans memoire FH6 (localisation / injection)
 * hors du thread UI. Sans ca, le scan (potentiellement plusieurs minutes) bloque
 * la pompe de messages et la fenetre passe "Ne repond pas". Communication via une
 * CRITICAL_SECTION : le worker pousse une ligne de statut, le main la draine chaque
 * frame ; annulation via un flag, fin via un flag atomique. */

typedef enum {
    FH6_JOB_LOCATE,   /* fh6_locate (diagnostic lecture seule) */
    FH6_JOB_INJECT    /* fh6_inject_doc (ecrit dans le jeu) */
} Fh6JobKind;

typedef struct Fh6Worker {
    HANDLE             hThread;
    CRITICAL_SECTION   lock;
    volatile long      cancel;        /* 1 => abandon demande */
    volatile long      done;          /* 1 => worker termine */

    /* Entrees (lues par le worker ; ne pas modifier pendant le run). */
    Fh6JobKind         kind;
    Fh6Profile         profile;       /* copie locale (default + cfg) */
    int                want_count;    /* count template ; 0 = auto */
    const Fh6JsonDoc  *doc;           /* pour INJECT ; doit survivre au job */
    unsigned long long deadline_ms;   /* GetTickCount64() limite ; 0 = aucune */

    /* Sorties. */
    char               status[256];   /* derniere ligne de statut (sous lock) */
    long               status_seq;    /* incremente a chaque nouveau statut */
    char               report[1024];  /* rapport final (valide apres done) */
    int                result;        /* code retour de l'op (>=0 = nb formes pour inject) */
} Fh6Worker;

/* Demarre le worker. profile est copie. doc doit rester valide jusqu'au join (INJECT).
 * budget_ms : delai max du scan (0 = aucun). Renvoie 0 si OK, -1 sinon. */
int  fh6_worker_start(Fh6Worker *w, Fh6JobKind kind, const Fh6Profile *prof,
                      int want_count, const Fh6JsonDoc *doc, int budget_ms);

/* 1 si le worker a fini (non bloquant). */
int  fh6_worker_done(const Fh6Worker *w);

/* Si le statut a change depuis *io_seq, le copie dans dst, met *io_seq a jour et
 * renvoie 1 ; sinon 0. Non bloquant. Le caller initialise *io_seq a 0. */
int  fh6_worker_take_status(Fh6Worker *w, char *dst, int dstsz, long *io_seq);

/* Demande l'annulation (le worker sort au prochain check). */
void fh6_worker_cancel(Fh6Worker *w);

/* Attend la fin du worker (bloquant) et ferme le handle de thread. */
void fh6_worker_join(Fh6Worker *w);

/* Libere les ressources internes (join + CRITICAL_SECTION). */
void fh6_worker_free(Fh6Worker *w);

#endif
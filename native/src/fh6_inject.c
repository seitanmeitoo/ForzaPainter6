/* Injection memoire FH6 (etape 2b). Port C de ForzaDesigner6 "Multi-Support
 * v3456" (MIT). Localisation dynamique du CLiveryGroup (empreinte de spheres +
 * fallback RTTI), validation stricte, puis ecriture des champs de chaque Layer. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fh6_inject.h"

#define FH6_ACCESS (PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION)

#define READABLE_FLAGS (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY \
                        | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
#define WRITABLE_FLAGS (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)

#define SPHERE_FULL_TABLE_THRESHOLD 0.85
#define RTTI_FULL_TABLE_THRESHOLD   0.95
#define SAMPLE_LAYERS 16

static void report_set(char *report, int sz, const char *fmt, ...)
{
    if (!report || sz <= 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(report, (size_t)sz, fmt, ap);
    va_end(ap);
}

/* --------------------------------------------------- progression / annulation */

/* 1 si le scan doit s'arreter (annulation utilisateur ou deadline depassee). */
static int prog_abort(const Fh6Progress *prog)
{
    if (!prog) return 0;
    if (prog->cancel && *prog->cancel) return 1;
    if (prog->deadline_ms && GetTickCount64() >= prog->deadline_ms) return 1;
    return 0;
}

static void prog_status(const Fh6Progress *prog, const char *fmt, ...)
{
    if (!prog || !prog->on_status) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    prog->on_status(prog->ctx, buf);
}

/* ---------------------------------------------------------------- profil --- */

void fh6_profile_default(Fh6Profile *p)
{
    memset(p, 0, sizeof(*p));
    p->process_names[0] = "forzahorizon6.exe";
    p->process_names[1] = "ForzaHorizon6-Win64-Shipping.exe";
    p->n_process_names  = 2;
    p->rtti_class_name  = ".?AVCLiveryGroup@@";
    p->count_off          = 0x5A;
    p->table_off          = 0x78;
    p->layer_pos_off      = 0x18;
    p->layer_scale_off    = 0x28;
    p->layer_rot_off      = 0x50;
    p->layer_color_off    = 0x74;
    p->layer_mask_off     = 0x78;
    p->layer_shape_id_off = 0x7A;
    p->scale_div_ellipse  = 63.0f;
    p->scale_div_other    = 127.0f;
    p->half_extent        = 1;
    p->shape_id_ellipse   = 102;
    p->shape_id_other     = 101;
    p->force_layer_count  = 0;
    p->clear_unused       = 1;
    p->add_boundary_masks = 0;
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = 0;
    return s;
}

static int set_profile_key(Fh6Profile *p, const char *key, const char *val)
{
    /* Entiers : strtoul base 0 accepte 0x.. et decimal. Flottants : strtod. */
    unsigned long uv = strtoul(val, NULL, 0);
    double dv = strtod(val, NULL);
    if      (strcmp(key, "count_off") == 0)          p->count_off = (uint32_t)uv;
    else if (strcmp(key, "table_off") == 0)          p->table_off = (uint32_t)uv;
    else if (strcmp(key, "layer_pos_off") == 0)      p->layer_pos_off = (uint32_t)uv;
    else if (strcmp(key, "layer_scale_off") == 0)    p->layer_scale_off = (uint32_t)uv;
    else if (strcmp(key, "layer_rot_off") == 0)      p->layer_rot_off = (uint32_t)uv;
    else if (strcmp(key, "layer_color_off") == 0)    p->layer_color_off = (uint32_t)uv;
    else if (strcmp(key, "layer_mask_off") == 0)     p->layer_mask_off = (uint32_t)uv;
    else if (strcmp(key, "layer_shape_id_off") == 0) p->layer_shape_id_off = (uint32_t)uv;
    else if (strcmp(key, "scale_div_ellipse") == 0)  p->scale_div_ellipse = (float)dv;
    else if (strcmp(key, "scale_div_other") == 0)    p->scale_div_other = (float)dv;
    else if (strcmp(key, "half_extent") == 0)        p->half_extent = (int)uv;
    else if (strcmp(key, "shape_id_ellipse") == 0)   p->shape_id_ellipse = (int)uv;
    else if (strcmp(key, "shape_id_other") == 0)     p->shape_id_other = (int)uv;
    else if (strcmp(key, "force_layer_count") == 0)  p->force_layer_count = (int)uv;
    else if (strcmp(key, "clear_unused") == 0)       p->clear_unused = (int)uv;
    else if (strcmp(key, "add_boundary_masks") == 0) p->add_boundary_masks = (int)uv;
    else return 0;
    return 1;
}

int fh6_profile_load_cfg(Fh6Profile *p, const wchar_t *path)
{
    FILE *fp = _wfopen(path, L"r");
    if (!fp) return -1;
    char line[256];
    int applied = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *s = trim(line);
        if (*s == 0 || *s == '#' || *s == ';' || (s[0] == '/' && s[1] == '/'))
            continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(s);
        char *val = trim(eq + 1);
        if (set_profile_key(p, key, val)) ++applied;
    }
    fclose(fp);
    return applied;
}

/* ------------------------------------------------------ primitives memoire - */

static DWORD find_pid(const Fh6Profile *p)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            char name[MAX_PATH];
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, sizeof(name), NULL, NULL);
            for (int i = 0; i < p->n_process_names; ++i) {
                if (p->process_names[i] && _stricmp(name, p->process_names[i]) == 0) {
                    pid = pe.th32ProcessID;
                    break;
                }
            }
            if (pid) break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static uint64_t module_base(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    uint64_t base = 0;
    if (Module32FirstW(snap, &me)) base = (uint64_t)(uintptr_t)me.modBaseAddr;
    CloseHandle(snap);
    return base;
}

static size_t rpm(HANDLE h, uint64_t addr, void *buf, size_t size)
{
    SIZE_T n = 0;
    if (!ReadProcessMemory(h, (LPCVOID)(uintptr_t)addr, buf, size, &n)) return 0;
    return (size_t)n;
}

static int read_exact(HANDLE h, uint64_t addr, void *buf, size_t size)
{
    return rpm(h, addr, buf, size) == size;
}

static int wpm(HANDLE h, uint64_t addr, const void *buf, size_t size)
{
    SIZE_T n = 0;
    if (!WriteProcessMemory(h, (LPVOID)(uintptr_t)addr, buf, size, &n)) return 0;
    return (size_t)n == size;
}

static uint64_t read_u64(HANDLE h, uint64_t addr)
{
    uint64_t v = 0;
    return read_exact(h, addr, &v, 8) ? v : 0;
}

static int is_user_ptr(uint64_t v)
{
    return v > 0x000001000000ULL && v < 0x800000000000ULL;
}

/* --------------------------------------------------------- regions memoire - */

typedef struct {
    uint64_t base;
    uint64_t size;
    DWORD    protect;
    DWORD    type;
} Region;

static int region_readable(const Region *r)
{
    return (r->protect & READABLE_FLAGS) && !(r->protect & PAGE_GUARD);
}
static int region_writable(const Region *r)
{
    return (r->protect & WRITABLE_FLAGS) && !(r->protect & PAGE_GUARD);
}
static int region_is_image(const Region *r)   { return r->type == MEM_IMAGE; }
static int region_is_private(const Region *r)  { return r->type == MEM_PRIVATE; }

static int cmp_region_size_desc(const void *a, const void *b)
{
    const Region *ra = (const Region *)a, *rb = (const Region *)b;
    if (ra->size < rb->size) return 1;
    if (ra->size > rb->size) return -1;
    return 0;
}

/* Enumere les regions committed. Renvoie un tableau malloc (a free), count via out. */
static Region *enumerate_regions(HANDLE h, int *out_count)
{
    Region *arr = NULL;
    int cap = 0, n = 0;
    MEMORY_BASIC_INFORMATION mbi;
    uint64_t addr = 0;
    const uint64_t ceiling = 0x7FFFFFFFFFFFULL;
    while (addr < ceiling) {
        SIZE_T got = VirtualQueryEx(h, (LPCVOID)(uintptr_t)addr, &mbi, sizeof(mbi));
        if (got == 0) break;
        if (mbi.State == MEM_COMMIT) {
            if (n == cap) {
                int ncap = cap ? cap * 2 : 256;
                Region *na = (Region *)realloc(arr, (size_t)ncap * sizeof(Region));
                if (!na) { free(arr); return NULL; }
                arr = na; cap = ncap;
            }
            arr[n].base    = (uint64_t)(uintptr_t)mbi.BaseAddress;
            arr[n].size    = (uint64_t)mbi.RegionSize;
            arr[n].protect = mbi.Protect;
            arr[n].type    = mbi.Type;
            ++n;
        }
        uint64_t next = (uint64_t)(uintptr_t)mbi.BaseAddress + (uint64_t)mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
    *out_count = n;
    return arr;
}

/* Recherche de motif (memchr + memcmp). Renvoie l'offset ou -1. */
static long long mem_find(const uint8_t *hay, size_t hlen,
                          const uint8_t *ndl, size_t nlen, size_t start)
{
    if (nlen == 0 || hlen < nlen || start > hlen - nlen) return -1;
    size_t i = start;
    while (i + nlen <= hlen) {
        const uint8_t *hit = (const uint8_t *)memchr(hay + i, ndl[0], hlen - i);
        if (!hit) return -1;
        size_t pos = (size_t)(hit - hay);
        if (pos + nlen > hlen) return -1;
        if (memcmp(hay + pos, ndl, nlen) == 0) return (long long)pos;
        i = pos + 1;
    }
    return -1;
}

/* ----------------------------------------------- scan par chunks (4 Mo) ----- */

/* Au lieu de malloc(taille_region) + lecture en bloc (qui echoue ou rame sur les
 * grosses regions multi-Go du jeu, et fait sauter la region si une seule sous-page
 * est illisible), on lit chaque region par chunks de 4 Mo dans un buffer reutilise,
 * avec un recouvrement de (patlen-1) octets pour ne pas rater un motif a cheval. */
#define SCAN_CHUNK (4u * 1024u * 1024u)

typedef struct {
    uint8_t *buf;
    size_t   cap;
} ScanBuf;

static void scanbuf_free(ScanBuf *sb)
{
    free(sb->buf);
    sb->buf = NULL;
    sb->cap = 0;
}

/* Callback par chunk : buf[0..len) = octets lus, cbase = adresse absolue du debut
 * du chunk, limit = ne traiter que les motifs dont le debut est < limit (evite le
 * double comptage sur le recouvrement). Renvoie 1 pour arreter le scan (trouve). */
typedef int (*chunk_cb)(void *ctx, const uint8_t *buf, size_t len,
                        uint64_t cbase, size_t limit);

/* Lit [base, base+size) par chunks et appelle cb. Renvoie 1 si cb a stoppe (trouve),
 * 0 sinon. Honore l'annulation/timeout via prog. Compte les octets lus dans *scanned. */
static int scan_chunks(HANDLE h, uint64_t base, uint64_t size, size_t overlap,
                       ScanBuf *sb, chunk_cb cb, void *ctx,
                       const Fh6Progress *prog, uint64_t *scanned)
{
    if (size == 0) return 0;
    size_t need = (size_t)SCAN_CHUNK + overlap;
    if (sb->cap < need) {
        uint8_t *nb = (uint8_t *)realloc(sb->buf, need);
        if (!nb) return 0;
        sb->buf = nb;
        sb->cap = need;
    }
    uint64_t off = 0;
    while (off < size) {
        if (prog_abort(prog)) return 0;
        uint64_t remain = size - off;
        size_t read_len = (remain > (uint64_t)need) ? need : (size_t)remain;
        SIZE_T got = 0;
        ReadProcessMemory(h, (LPCVOID)(uintptr_t)(base + off), sb->buf, read_len, &got);
        if (got > 0) {
            int is_last = (off + (uint64_t)read_len >= size) || ((size_t)got < read_len);
            size_t limit = is_last ? (size_t)got : (size_t)SCAN_CHUNK;
            if (limit > (size_t)got) limit = (size_t)got;
            if (cb(ctx, sb->buf, (size_t)got, base + off, limit)) return 1;
            if (scanned) *scanned += (uint64_t)got;
        }
        off += SCAN_CHUNK;
    }
    return 0;
}

/* ------------------------------------------------------- scoring des layers - */

static int score_layer(HANDLE h, const Fh6Profile *p, uint64_t lptr)
{
    if (!is_user_ptr(lptr)) return 0;
    int score = 0;
    float pos[2];
    if (read_exact(h, lptr + p->layer_pos_off, pos, 8)
        && isfinite(pos[0]) && isfinite(pos[1])
        && pos[0] >= -8192.0f && pos[0] <= 8192.0f
        && pos[1] >= -8192.0f && pos[1] <= 8192.0f) ++score;
    float sc[2];
    if (read_exact(h, lptr + p->layer_scale_off, sc, 8)
        && isfinite(sc[0]) && isfinite(sc[1])
        && fabsf(sc[0]) > 0.0f && fabsf(sc[0]) <= 64.0f
        && fabsf(sc[1]) > 0.0f && fabsf(sc[1]) <= 64.0f) ++score;
    uint8_t col[4];
    if (read_exact(h, lptr + p->layer_color_off, col, 4)) ++score;
    uint8_t sid;
    if (read_exact(h, lptr + p->layer_shape_id_off, &sid, 1) && (sid == 101 || sid == 102)) ++score;
    uint8_t mask;
    if (read_exact(h, lptr + p->layer_mask_off, &mask, 1) && (mask == 0 || mask == 1)) ++score;
    return score;
}

static int loose_validate_layer(HANDLE h, const Fh6Profile *p, uint64_t lptr)
{
    if (!is_user_ptr(lptr)) return 0;
    float pos[2];
    if (!read_exact(h, lptr + p->layer_pos_off, pos, 8) || !isfinite(pos[0]) || !isfinite(pos[1]))
        return 0;
    float sc[2];
    if (!read_exact(h, lptr + p->layer_scale_off, sc, 8) || !isfinite(sc[0]) || !isfinite(sc[1]))
        return 0;
    uint8_t col[4];
    if (!read_exact(h, lptr + p->layer_color_off, col, 4)) return 0;
    return 1;
}

static int read_table_bulk(HANDLE h, uint64_t table, int n, uint64_t *out)
{
    if (read_exact(h, table, out, (size_t)n * 8)) return 1;
    for (int i = 0; i < n; ++i) out[i] = read_u64(h, table + (uint64_t)i * 8);
    return 1;
}

static int count_valid_layers(HANDLE h, const Fh6Profile *p, const uint64_t *addrs, int n)
{
    int valid = 0;
    for (int k = 0; k < n; ++k)
        if (score_layer(h, p, addrs[k]) >= 5) ++valid;
    return valid;
}

static int count_loose_layers(HANDLE h, const Fh6Profile *p, const uint64_t *addrs, int n)
{
    int valid = 0;
    for (int k = 0; k < n; ++k)
        if (loose_validate_layer(h, p, addrs[k])) ++valid;
    return valid;
}

/* ------------------------------------------------------------ diagnostics --- */

/* Compteurs accumules pendant la recherche, restitues dans le rapport d'echec
 * pour distinguer "pas de template charge" (0 hit) d'"offsets casses" (candidats
 * trouves mais validation a 0 %). */
typedef struct {
    long hits;        /* motifs count trouves */
    long table_ok;    /* candidats avec un pointeur de table plausible */
    long sample_ok;   /* candidats passant l'echantillon 16/16 */
    int  best_valid;  /* meilleur nb de layers valides vu (table pleine) */
    int  best_count;  /* count correspondant au best_valid */
} SphereDiag;

typedef struct {
    int  string_found; /* chaine RTTI .?AVCLiveryGroup@@ localisee */
    int  nvt;          /* vtables resolues */
    long cand;         /* objets avec count attendu + table plausible */
    int  best_valid;   /* meilleur nb de layers valides vu */
    int  best_count;
} RttiDiag;

/* ------------------------------------------------- locator empreinte spheres */

typedef struct {
    HANDLE            h;
    const Fh6Profile *p;
    int               count;
    uint8_t           pat[2];
    uint64_t         *tbl;         /* taille count */
    uint64_t          region_base; /* base de la region en cours */
    SphereDiag       *diag;
    uint64_t          out_group, out_table;
} SphereCtx;

static int sphere_chunk_cb(void *vctx, const uint8_t *buf, size_t len,
                           uint64_t cbase, size_t limit)
{
    SphereCtx *c = (SphereCtx *)vctx;
    const Fh6Profile *p = c->p;
    size_t start = 0;
    long long pos;
    while ((pos = mem_find(buf, len, c->pat, 2, start)) >= 0) {
        if ((size_t)pos >= limit) break;
        start = (size_t)pos + 1;
        uint64_t count_addr = cbase + (uint64_t)pos;
        c->diag->hits++;
        if (count_addr < (uint64_t)p->count_off) continue;
        uint64_t group = count_addr - p->count_off;
        if (group < c->region_base) continue;
        uint64_t table = read_u64(c->h, group + p->table_off);
        if (!is_user_ptr(table)) continue;
        c->diag->table_ok++;
        int ok = 1;
        int sample = c->count < SAMPLE_LAYERS ? c->count : SAMPLE_LAYERS;
        for (int k = 0; k < sample; ++k) {
            uint64_t lptr = read_u64(c->h, table + (uint64_t)k * 8);
            if (score_layer(c->h, p, lptr) < 5) { ok = 0; break; }
        }
        if (!ok) continue;
        c->diag->sample_ok++;
        read_table_bulk(c->h, table, c->count, c->tbl);
        int valid = count_valid_layers(c->h, p, c->tbl, c->count);
        if (valid > c->diag->best_valid) {
            c->diag->best_valid = valid;
            c->diag->best_count = c->count;
        }
        if (valid >= (int)(c->count * SPHERE_FULL_TABLE_THRESHOLD)) {
            c->out_group = group;
            c->out_table = table;
            return 1;
        }
    }
    return 0;
}

/* Cherche un CLiveryGroup dont le compteur (u16) vaut count, valide par
 * empreinte stricte. Renvoie 1 + group/table si trouve, 0 sinon. */
static int locate_sphere(Fh6Session *s, int count, ScanBuf *sb,
                         const Fh6Progress *prog, SphereDiag *diag,
                         uint64_t *out_group, uint64_t *out_table)
{
    HANDLE h = s->handle;
    const Fh6Profile *p = s->profile;

    int nreg = 0;
    Region *regs = enumerate_regions(h, &nreg);
    if (!regs) return 0;
    qsort(regs, (size_t)nreg, sizeof(Region), cmp_region_size_desc);

    uint64_t *tbl = (uint64_t *)malloc((size_t)count * 8);
    if (!tbl) { free(regs); return 0; }

    SphereCtx ctx;
    ctx.h = h; ctx.p = p; ctx.count = count; ctx.tbl = tbl; ctx.diag = diag;
    ctx.pat[0] = (uint8_t)(count & 0xFF);
    ctx.pat[1] = (uint8_t)((count >> 8) & 0xFF);
    ctx.out_group = ctx.out_table = 0;

    uint64_t scanned = 0;
    int found = 0;
    for (int i = 0; i < nreg && !found; ++i) {
        Region *r = &regs[i];
        if (!region_readable(r) || !region_writable(r) || region_is_image(r)) continue;
        if (prog_abort(prog)) break;
        ctx.region_base = r->base;
        prog_status(prog, "Empreinte count=%d : region %d/%d (%llu Mo lus)",
                    count, i + 1, nreg, (unsigned long long)(scanned >> 20));
        if (scan_chunks(h, r->base, r->size, 1, sb, sphere_chunk_cb, &ctx, prog, &scanned)) {
            *out_group = ctx.out_group;
            *out_table = ctx.out_table;
            found = 1;
        }
    }
    free(tbl);
    free(regs);
    return found;
}

/* ---------------------------------------------------------- locator RTTI ---- */

typedef struct {
    const uint8_t *pat;
    size_t         patlen;
    int            alignment;
    int            stop_after;
    uint64_t      *out;
    int            max_out;
    int            n;       /* total trouve (peut depasser max_out) */
} CollectCtx;

static int collect_chunk_cb(void *vctx, const uint8_t *buf, size_t len,
                            uint64_t cbase, size_t limit)
{
    CollectCtx *c = (CollectCtx *)vctx;
    size_t start = 0;
    long long pos;
    while ((pos = mem_find(buf, len, c->pat, c->patlen, start)) >= 0) {
        if ((size_t)pos >= limit) break;
        start = (size_t)pos + 1;
        uint64_t addr = cbase + (uint64_t)pos;
        if (c->alignment <= 1 || (addr % (uint64_t)c->alignment) == 0) {
            if (c->n < c->max_out) c->out[c->n] = addr;
            ++c->n;
            if (c->stop_after > 0 && c->n >= c->stop_after) return 1;
        }
    }
    return 0;
}

/* Cherche un motif dans les regions MEM_IMAGE (chunke). Remplit out (cap max_out),
 * renvoie le nombre total trouve (peut depasser max_out). */
static int find_in_image(HANDLE h, Region *regs, int nreg,
                         const uint8_t *pat, size_t patlen,
                         int alignment, int stop_after,
                         uint64_t *out, int max_out,
                         ScanBuf *sb, const Fh6Progress *prog)
{
    CollectCtx c;
    c.pat = pat; c.patlen = patlen; c.alignment = alignment;
    c.stop_after = stop_after; c.out = out; c.max_out = max_out; c.n = 0;
    for (int i = 0; i < nreg; ++i) {
        Region *r = &regs[i];
        if (!region_is_image(r) || !region_readable(r)) continue;
        if (prog_abort(prog)) break;
        if (scan_chunks(h, r->base, r->size, patlen - 1, sb,
                        collect_chunk_cb, &c, prog, NULL))
            break; /* stop_after atteint */
    }
    return c.n;
}

static int contains_u64(const uint64_t *a, int n, uint64_t v)
{
    for (int i = 0; i < n; ++i) if (a[i] == v) return 1;
    return 0;
}

/* Resout les vtables candidates de la classe RTTI (independant du count : a faire
 * une seule fois). Renvoie le nombre (cap max_vt). Remplit diag->string_found. */
static int rtti_find_vtables(HANDLE h, uint64_t base, const Fh6Profile *p,
                             Region *regs, int nreg, uint64_t *vtables, int max_vt,
                             ScanBuf *sb, const Fh6Progress *prog, RttiDiag *diag)
{
    /* 1. chaine RTTI dans la section image */
    const uint8_t *name = (const uint8_t *)p->rtti_class_name;
    size_t namelen = strlen(p->rtti_class_name);
    uint64_t name_hit = 0;
    if (find_in_image(h, regs, nreg, name, namelen, 1, 1, &name_hit, 1, sb, prog) < 1)
        return 0;
    if (diag) diag->string_found = 1;
    uint64_t type_descriptor = name_hit - 0x10;
    if (type_descriptor < base || type_descriptor > 0x7FFFFFFFFFFFULL) return 0;
    uint64_t rva = type_descriptor - base;
    if (rva > 0xFFFFFFFFULL) return 0;

    /* 2. references u32 == RVA -> CompleteObjectLocator */
    uint8_t rva_pat[4] = {
        (uint8_t)(rva & 0xFF), (uint8_t)((rva >> 8) & 0xFF),
        (uint8_t)((rva >> 16) & 0xFF), (uint8_t)((rva >> 24) & 0xFF)
    };
    enum { MAX_REFS = 4096 };
    uint64_t *refs = (uint64_t *)malloc(MAX_REFS * sizeof(uint64_t));
    if (!refs) return 0;
    int nrefs = find_in_image(h, regs, nreg, rva_pat, 4, 4, 0, refs, MAX_REFS, sb, prog);
    if (nrefs > MAX_REFS) nrefs = MAX_REFS;

    enum { MAX_COL = 512 };
    uint64_t cols[MAX_COL];
    int ncol = 0;
    for (int i = 0; i < nrefs && ncol < MAX_COL; ++i) {
        uint64_t col = refs[i] - 0x0C;
        uint8_t sig;
        if (read_exact(h, col, &sig, 1) && sig == 1 && !contains_u64(cols, ncol, col))
            cols[ncol++] = col;
    }
    free(refs);
    if (ncol == 0) return 0;

    /* 3. pointeurs u64 vers chaque COL -> vtable = hit + 8 */
    enum { MAX_PTRS = 4096 };
    uint64_t *ptrs = (uint64_t *)malloc(MAX_PTRS * sizeof(uint64_t));
    if (!ptrs) return 0;
    int nvt = 0;
    for (int c = 0; c < ncol; ++c) {
        uint8_t col_pat[8];
        for (int b = 0; b < 8; ++b) col_pat[b] = (uint8_t)((cols[c] >> (8 * b)) & 0xFF);
        int np = find_in_image(h, regs, nreg, col_pat, 8, 8, 0, ptrs, MAX_PTRS, sb, prog);
        if (np > MAX_PTRS) np = MAX_PTRS;
        for (int i = 0; i < np && nvt < max_vt; ++i) {
            uint64_t vt = ptrs[i] + 8;
            if (!contains_u64(vtables, nvt, vt)) vtables[nvt++] = vt;
        }
    }
    free(ptrs);
    return nvt;
}

typedef struct {
    HANDLE            h;
    const Fh6Profile *p;
    int               count;
    const uint64_t   *vtables;
    int               nvt;
    uint64_t         *tbl;
    RttiDiag         *diag;
    uint64_t          out_group, out_table;
} RttiHeapCtx;

static int rtti_chunk_cb(void *vctx, const uint8_t *buf, size_t len,
                         uint64_t cbase, size_t limit)
{
    RttiHeapCtx *c = (RttiHeapCtx *)vctx;
    const Fh6Profile *p = c->p;
    for (int v = 0; v < c->nvt; ++v) {
        uint8_t pat[8];
        for (int b = 0; b < 8; ++b) pat[b] = (uint8_t)((c->vtables[v] >> (8 * b)) & 0xFF);
        size_t start = 0;
        long long pos;
        while ((pos = mem_find(buf, len, pat, 8, start)) >= 0) {
            if ((size_t)pos >= limit) break;
            start = (size_t)pos + 1;
            uint64_t group = cbase + (uint64_t)pos;
            uint32_t cnt;
            if (!read_exact(c->h, group + p->count_off, &cnt, 4)) continue;
            if ((int)(cnt & 0xFFFF) != c->count) continue;
            uint64_t table = read_u64(c->h, group + p->table_off);
            if (!is_user_ptr(table)) continue;
            c->diag->cand++;
            int ok = 1;
            int sample = c->count < SAMPLE_LAYERS ? c->count : SAMPLE_LAYERS;
            for (int k = 0; k < sample; ++k) {
                uint64_t lptr = read_u64(c->h, table + (uint64_t)k * 8);
                if (!loose_validate_layer(c->h, p, lptr)) { ok = 0; break; }
            }
            if (!ok) continue;
            read_table_bulk(c->h, table, c->count, c->tbl);
            int valid = count_loose_layers(c->h, p, c->tbl, c->count);
            if (valid > c->diag->best_valid) {
                c->diag->best_valid = valid;
                c->diag->best_count = c->count;
            }
            if (valid >= (int)(c->count * RTTI_FULL_TABLE_THRESHOLD)) {
                c->out_group = group;
                c->out_table = table;
                return 1;
            }
        }
    }
    return 0;
}

/* Scan du tas pour les objets de vtable RTTI connue, count attendu. vtables est
 * precalcule par rtti_find_vtables (une fois). Renvoie 1 + group/table si trouve. */
static int rtti_locate(Fh6Session *s, int count, const uint64_t *vtables, int nvt,
                       ScanBuf *sb, const Fh6Progress *prog, RttiDiag *diag,
                       uint64_t *out_group, uint64_t *out_table)
{
    HANDLE h = s->handle;
    const Fh6Profile *p = s->profile;
    if (nvt == 0) return 0;

    int nreg = 0;
    Region *regs = enumerate_regions(h, &nreg);
    if (!regs) return 0;
    qsort(regs, (size_t)nreg, sizeof(Region), cmp_region_size_desc);

    uint64_t *tbl = (uint64_t *)malloc((size_t)count * 8);
    if (!tbl) { free(regs); return 0; }

    RttiHeapCtx ctx;
    ctx.h = h; ctx.p = p; ctx.count = count; ctx.vtables = vtables; ctx.nvt = nvt;
    ctx.tbl = tbl; ctx.diag = diag; ctx.out_group = ctx.out_table = 0;

    uint64_t scanned = 0;
    int found = 0;
    for (int i = 0; i < nreg && !found; ++i) {
        Region *r = &regs[i];
        if (!region_is_private(r) || !region_readable(r) || !region_writable(r)) continue;
        if (prog_abort(prog)) break;
        prog_status(prog, "RTTI count=%d : region %d/%d (%llu Mo lus)",
                    count, i + 1, nreg, (unsigned long long)(scanned >> 20));
        if (scan_chunks(h, r->base, r->size, 7, sb, rtti_chunk_cb, &ctx, prog, &scanned)) {
            *out_group = ctx.out_group;
            *out_table = ctx.out_table;
            found = 1;
        }
    }
    free(tbl);
    free(regs);
    return found;
}

/* --------------------------------------------------------- parseur JSON ----- */

typedef struct {
    int    type;
    double data[5];
    int    n_data;
    int    color[4];
} JShape;

static const char *skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

static const char *parse_num(const char *p, double *out)
{
    char *end;
    *out = strtod(p, &end);
    return (end == p) ? NULL : end;
}

static const char *skip_string(const char *p)
{
    if (*p != '"') return NULL;
    ++p;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        ++p;
    }
    return (*p == '"') ? p + 1 : NULL;
}

static const char *parse_string(const char *p, char *buf, int bufsz)
{
    if (*p != '"') return NULL;
    ++p;
    int i = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) ++p;
        if (i < bufsz - 1) buf[i++] = *p;
        ++p;
    }
    if (*p != '"') return NULL;
    buf[i] = 0;
    return p + 1;
}

static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (*p == '"') return skip_string(p);
    if (*p == '[' || *p == '{') {
        char open = *p, close = (open == '[') ? ']' : '}';
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                const char *q = skip_string(p);
                if (!q) return NULL;
                p = q;
                continue;
            }
            if (*p == open) ++depth;
            else if (*p == close) { --depth; if (depth == 0) return p + 1; }
            ++p;
        }
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p)) ++p;
    return p;
}

static const char *parse_num_array(const char *p, double *buf, int max, int *cnt)
{
    p = skip_ws(p);
    if (*p != '[') return NULL;
    ++p;
    int c = 0;
    p = skip_ws(p);
    if (*p == ']') { *cnt = 0; return p + 1; }
    while (1) {
        double v;
        p = skip_ws(p);
        const char *q = parse_num(p, &v);
        if (!q) return NULL;
        p = q;
        if (c < max) buf[c] = v;
        ++c;
        p = skip_ws(p);
        if (*p == ',') { ++p; continue; }
        if (*p == ']') { ++p; break; }
        return NULL;
    }
    *cnt = c;
    return p;
}

static const char *parse_shape_obj(const char *p, JShape *sh)
{
    p = skip_ws(p);
    if (*p != '{') return NULL;
    ++p;
    sh->type = 0;
    sh->n_data = 0;
    sh->color[0] = sh->color[1] = sh->color[2] = 0;
    sh->color[3] = 255;
    while (1) {
        p = skip_ws(p);
        if (*p == '}') { ++p; break; }
        char key[32];
        const char *q = parse_string(p, key, sizeof(key));
        if (!q) return NULL;
        p = skip_ws(q);
        if (*p != ':') return NULL;
        ++p;
        p = skip_ws(p);
        if (strcmp(key, "type") == 0) {
            double v;
            q = parse_num(p, &v);
            if (!q) return NULL;
            sh->type = (int)v;
            p = q;
        } else if (strcmp(key, "data") == 0) {
            double buf[8];
            int c;
            q = parse_num_array(p, buf, 8, &c);
            if (!q) return NULL;
            sh->n_data = c < 5 ? c : 5;
            for (int i = 0; i < sh->n_data; ++i) sh->data[i] = buf[i];
            p = q;
        } else if (strcmp(key, "color") == 0) {
            double buf[8];
            int c;
            q = parse_num_array(p, buf, 8, &c);
            if (!q) return NULL;
            for (int i = 0; i < 4 && i < c; ++i) sh->color[i] = (int)buf[i];
            p = q;
        } else {
            q = skip_value(p);
            if (!q) return NULL;
            p = q;
        }
        p = skip_ws(p);
        if (*p == ',') { ++p; continue; }
        if (*p == '}') { ++p; break; }
        return NULL;
    }
    return p;
}

/* Lit le JSON exporte par fh6_export.c. shapes[0] = fond. Renvoie 0/-1. */
static int parse_json_file(const wchar_t *path, JShape **out_shapes, int *out_n)
{
    FILE *fp = _wfopen(path, L"rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[rd] = 0;

    const char *p = strstr(buf, "\"shapes\"");
    if (!p) { free(buf); return -1; }
    p = skip_ws(p + 8);
    if (*p != ':') { free(buf); return -1; }
    p = skip_ws(p + 1);
    if (*p != '[') { free(buf); return -1; }
    ++p;

    int cap = 64, n = 0;
    JShape *arr = (JShape *)malloc((size_t)cap * sizeof(JShape));
    if (!arr) { free(buf); return -1; }
    p = skip_ws(p);
    if (*p == ']') { free(buf); *out_shapes = arr; *out_n = 0; return 0; }
    while (1) {
        if (n == cap) {
            cap *= 2;
            JShape *na = (JShape *)realloc(arr, (size_t)cap * sizeof(JShape));
            if (!na) { free(arr); free(buf); return -1; }
            arr = na;
        }
        const char *q = parse_shape_obj(p, &arr[n]);
        if (!q) { free(arr); free(buf); return -1; }
        p = q;
        ++n;
        p = skip_ws(p);
        if (*p == ',') { p = skip_ws(p + 1); continue; }
        if (*p == ']') break;
        free(arr); free(buf); return -1;
    }
    free(buf);
    *out_shapes = arr;
    *out_n = n;
    return 0;
}

int fh6_load_json(const wchar_t *json_path, Fh6JsonDoc *out)
{
    memset(out, 0, sizeof(*out));
    JShape *js = NULL;
    int jn = 0;
    if (parse_json_file(json_path, &js, &jn) != 0 || jn < 1) { free(js); return -1; }

    Fh6JsonShape *arr = (Fh6JsonShape *)malloc((size_t)jn * sizeof(Fh6JsonShape));
    if (!arr) { free(js); return -1; }
    for (int i = 0; i < jn; ++i) {
        Fh6JsonShape *d = &arr[i];
        d->type  = js[i].type;
        d->x     = (js[i].n_data >= 1) ? (float)js[i].data[0] : 0.0f;
        d->y     = (js[i].n_data >= 2) ? (float)js[i].data[1] : 0.0f;
        d->w     = (js[i].n_data >= 3) ? (float)js[i].data[2] : 0.0f;
        d->h     = (js[i].n_data >= 4) ? (float)js[i].data[3] : 0.0f;
        d->angle = (js[i].n_data >= 5) ? (float)js[i].data[4] : 0.0f;
        d->rgba[0] = (uint8_t)js[i].color[0]; d->rgba[1] = (uint8_t)js[i].color[1];
        d->rgba[2] = (uint8_t)js[i].color[2]; d->rgba[3] = (uint8_t)js[i].color[3];
        d->is_bg = (i == 0) ? 1 : 0;
    }
    free(js);

    out->shapes   = arr;
    out->count    = jn;
    out->image_w  = (int)arr[0].w;
    out->image_h  = (int)arr[0].h;
    out->bg_alpha = arr[0].rgba[3];
    int nr = 0, ne = 0;
    for (int i = 1; i < jn; ++i) {
        if (arr[i].rgba[3] <= 0) continue;
        if (arr[i].type == 1) ++nr;
        else if (arr[i].type == 16) ++ne;
    }
    out->n_rect = nr;
    out->n_ellipse = ne;
    return 0;
}

void fh6_free_json(Fh6JsonDoc *doc)
{
    if (!doc) return;
    free(doc->shapes);
    doc->shapes = NULL;
    doc->count = 0;
}

/* ----------------------------------------------------------- ecriture ------- */

typedef struct {
    int     type;     /* 1 = rect, 16 = ellipse */
    float   x, y, w, h, angle;
    uint8_t rgba[4];
    int     is_mask;
} MShape;

static int write_layer(HANDLE h, const Fh6Profile *p, uint64_t lptr, const MShape *m)
{
    int is_ellipse = (m->type == 16);
    float div = is_ellipse ? p->scale_div_ellipse : p->scale_div_other;
    float ax = m->w, ay = m->h;
    if (p->half_extent) { ax *= 0.5f; ay *= 0.5f; }

    float pos[2] = { m->x, -m->y };
    if (!wpm(h, lptr + p->layer_pos_off, pos, 8)) return 0;
    float sc[2] = { ax / div, ay / div };
    if (!wpm(h, lptr + p->layer_scale_off, sc, 8)) return 0;
    float ang = fmodf(m->angle, 360.0f);
    if (ang < 0) ang += 360.0f;
    ang = fmodf(360.0f - ang, 360.0f);
    if (!wpm(h, lptr + p->layer_rot_off, &ang, 4)) return 0;
    uint8_t col[4] = { m->rgba[0], m->rgba[1], m->rgba[2], 255 };
    if (!wpm(h, lptr + p->layer_color_off, col, 4)) return 0;
    uint8_t sid = (uint8_t)(is_ellipse ? p->shape_id_ellipse : p->shape_id_other);
    if (!wpm(h, lptr + p->layer_shape_id_off, &sid, 1)) return 0;
    uint8_t mask = m->is_mask ? 1 : 0;
    if (!wpm(h, lptr + p->layer_mask_off, &mask, 1)) return 0;
    return 1;
}

static void clear_layer(HANDLE h, const Fh6Profile *p, uint64_t lptr)
{
    uint8_t col[4] = { 0, 0, 0, 0 };
    wpm(h, lptr + p->layer_color_off, col, 4);
}

/* ----------------------------------------------------------- API publique --- */

int fh6_attach(Fh6Session *s, const Fh6Profile *p, char *report, int report_sz)
{
    memset(s, 0, sizeof(*s));
    s->profile = p;
    DWORD pid = find_pid(p);
    if (pid == 0) {
        report_set(report, report_sz,
                   "Forza Horizon 6 introuvable. Lance le jeu, puis reessaie.");
        return -1;
    }
    HANDLE h = OpenProcess(FH6_ACCESS, FALSE, pid);
    if (!h) {
        DWORD err = GetLastError();
        report_set(report, report_sz,
                   "OpenProcess a echoue (code %lu).\n"
                   "Si FH6 est la version Microsoft Store/Xbox (ou lance en admin), "
                   "relance vinyl-painter en administrateur. La version Steam ne le "
                   "demande generalement pas.",
                   (unsigned long)err);
        return -2;
    }
    s->pid = pid;
    s->handle = h;
    return 0;
}

void fh6_detach(Fh6Session *s)
{
    if (s->handle) { CloseHandle((HANDLE)s->handle); s->handle = NULL; }
    free(s->layer_addrs);
    s->layer_addrs = NULL;
    s->layer_count = 0;
}

int fh6_locate(Fh6Session *s, int want_count, const Fh6Progress *prog,
               char *report, int report_sz)
{
    if (!s->handle) { report_set(report, report_sz, "Non attache au processus."); return -1; }
    const Fh6Profile *p = s->profile;

    int tries[16], nt = 0;
    if (p->force_layer_count > 0) {
        tries[nt++] = p->force_layer_count;
    } else {
        static const int common[] = { 500, 1500, 3000, 1000, 100, 50, 20, 10 };
        if (want_count > 0) {
            tries[nt++] = want_count;
            for (int i = 0; i < 8; ++i)
                if (common[i] > want_count && nt < 16) tries[nt++] = common[i];
        } else {
            for (int i = 0; i < 8 && nt < 16; ++i) tries[nt++] = common[i];
        }
    }

    ScanBuf    sb = {0};
    SphereDiag sd = {0};
    RttiDiag   rd = {0};

    /* Resolution des vtables RTTI : independant du count, donc une seule fois. */
    enum { MAX_VT = 256 };
    uint64_t vtables[MAX_VT];
    int nvt = 0;
    {
        prog_status(prog, "Resolution RTTI CLiveryGroup...");
        uint64_t base = module_base(s->pid);
        int nreg = 0;
        Region *regs = enumerate_regions(s->handle, &nreg);
        if (regs) {
            if (base)
                nvt = rtti_find_vtables(s->handle, base, p, regs, nreg,
                                        vtables, MAX_VT, &sb, prog, &rd);
            free(regs);
        }
        rd.nvt = nvt;
    }

    int aborted = 0;
    for (int t = 0; t < nt; ++t) {
        if (prog_abort(prog)) { aborted = 1; break; }
        int count = tries[t];
        if (count <= 0) continue;
        uint64_t group = 0, table = 0;
        int via_rtti = 0;
        if (locate_sphere(s, count, &sb, prog, &sd, &group, &table)) {
            via_rtti = 0;
        } else if (nvt > 0 &&
                   rtti_locate(s, count, vtables, nvt, &sb, prog, &rd, &group, &table)) {
            via_rtti = 1;
        } else {
            continue;
        }
        s->group_addr = group;
        s->table_addr = table;
        s->layer_count = count;
        s->located_via_rtti = via_rtti;
        free(s->layer_addrs);
        s->layer_addrs = (uint64_t *)malloc((size_t)count * 8);
        if (!s->layer_addrs) {
            report_set(report, report_sz, "Memoire insuffisante.");
            scanbuf_free(&sb);
            return -1;
        }
        read_table_bulk(s->handle, table, count, s->layer_addrs);
        int valid = count_valid_layers(s->handle, p, s->layer_addrs, count);
        report_set(report, report_sz,
                   "Groupe localise (%s) : %d slots, %d/%d layers valides.\n"
                   "group=0x%llX  table=0x%llX",
                   via_rtti ? "RTTI" : "empreinte spheres", count, valid, count,
                   (unsigned long long)group, (unsigned long long)table);
        scanbuf_free(&sb);
        return 0;
    }

    scanbuf_free(&sb);

    if (aborted) {
        report_set(report, report_sz,
                   "Recherche interrompue (annulation ou delai depasse).\n"
                   "Empreinte : %ld hits, %ld tables, meilleur %d valides.\n"
                   "RTTI : chaine %s, %d vtables, %ld candidats, meilleur %d valides.",
                   sd.hits, sd.table_ok, sd.best_valid,
                   rd.string_found ? "OK" : "absente", rd.nvt, rd.cand, rd.best_valid);
        return -3;
    }

    report_set(report, report_sz,
               "Aucun groupe de vinyles confiant trouve.\n"
               "Dans FH6 : editeur de vinyles ouvert, template de spheres\n"
               "CHARGE et DEGROUPE (500 a 3000), editeur garde au premier plan.\n"
               "Diag empreinte : %ld hits, %ld tables, %ld ech.OK, meilleur %d valides.\n"
               "Diag RTTI : chaine %s, %d vtables, %ld candidats, meilleur %d valides.",
               sd.hits, sd.table_ok, sd.sample_ok, sd.best_valid,
               rd.string_found ? "OK" : "absente", rd.nvt, rd.cand, rd.best_valid);
    return -2;
}

/* Ajoute les 4 masques de bordure (forza-painter) a partir de l'index *mn. */
static void append_boundary_masks(MShape *ms, int *mn, int iw, int ih)
{
    const struct { float x, y, w, h; } m[4] = {
        { -(float)iw / 4.0f,        (float)ih / 2.0f,        (float)iw / 2.0f,  (float)ih * 1.5f },
        {  (float)iw + (float)iw/4, (float)ih / 2.0f,        (float)iw / 2.0f,  (float)ih * 1.5f },
        {  (float)iw / 2.0f,       -(float)ih / 4.0f,        (float)iw * 2.0f,  (float)ih / 2.0f },
        {  (float)iw / 2.0f,        (float)ih + (float)ih/4, (float)iw * 2.0f,  (float)ih / 2.0f },
    };
    for (int i = 0; i < 4; ++i) {
        MShape *d = &ms[(*mn)++];
        d->type = 1;
        d->x = m[i].x; d->y = m[i].y; d->w = m[i].w; d->h = m[i].h; d->angle = 0;
        d->rgba[0] = d->rgba[1] = d->rgba[2] = 0; d->rgba[3] = 255;
        d->is_mask = 1;
    }
}

int fh6_inject_doc(Fh6Session *s, const Fh6JsonDoc *doc, int template_count,
                   const Fh6Progress *prog, char *report, int report_sz)
{
    if (!s->handle) { report_set(report, report_sz, "Non attache au processus."); return -1; }
    const Fh6Profile *p = s->profile;
    if (!doc || doc->count < 1) { report_set(report, report_sz, "Document JSON vide."); return -1; }

    int iw = doc->image_w;
    int ih = doc->image_h;

    /* +8 : marge pour les masques de bordure eventuels. */
    MShape *ms = (MShape *)malloc((size_t)(doc->count + 8) * sizeof(MShape));
    if (!ms) { report_set(report, report_sz, "Memoire insuffisante."); return -1; }
    int mn = 0;

    if (doc->shapes[0].is_bg && doc->bg_alpha > 0) {
        const Fh6JsonShape *b = &doc->shapes[0];
        MShape *d = &ms[mn++];
        d->type = 1;
        d->x = b->x; d->y = b->y; d->w = b->w; d->h = b->h; d->angle = 0;
        d->rgba[0] = b->rgba[0]; d->rgba[1] = b->rgba[1]; d->rgba[2] = b->rgba[2]; d->rgba[3] = 255;
        d->is_mask = 0;
    }
    for (int i = 1; i < doc->count; ++i) {
        const Fh6JsonShape *b = &doc->shapes[i];
        if (b->rgba[3] <= 0) continue;
        if (b->type != 1 && b->type != 16) continue;
        MShape *d = &ms[mn++];
        d->type = b->type;
        d->x = b->x; d->y = b->y; d->w = b->w; d->h = b->h;
        d->angle = (b->type == 16) ? b->angle : 0.0f;
        d->rgba[0] = b->rgba[0]; d->rgba[1] = b->rgba[1]; d->rgba[2] = b->rgba[2]; d->rgba[3] = 255;
        d->is_mask = 0;
    }

    if (mn == 0) {
        report_set(report, report_sz, "Aucune forme exploitable dans le JSON.");
        free(ms);
        return -1;
    }

    int rc = fh6_locate(s, template_count > 0 ? template_count : mn, prog, report, report_sz);
    if (rc != 0) { free(ms); return -2; } /* report deja rempli par fh6_locate */

    int cap = s->layer_count;
    if (p->add_boundary_masks && cap >= mn + 4 && iw > 0 && ih > 0)
        append_boundary_masks(ms, &mn, iw, ih);

    if (mn > cap) {
        report_set(report, report_sz,
                   "Template trop petit : %d slots pour %d formes.\n"
                   "Charge un groupe de vinyles plus grand (1500 ou 3000 spheres).",
                   cap, mn);
        free(ms);
        return -3;
    }

    int written = 0, skipped = 0;
    for (int i = 0; i < cap; ++i) {
        uint64_t lptr = s->layer_addrs[i];
        if (i < mn) {
            if (!is_user_ptr(lptr) || score_layer(s->handle, p, lptr) < 5) { ++skipped; continue; }
            if (write_layer(s->handle, p, lptr, &ms[i])) ++written; else ++skipped;
        } else if (p->clear_unused) {
            if (is_user_ptr(lptr)) clear_layer(s->handle, p, lptr);
        }
    }
    free(ms);

    report_set(report, report_sz,
               "%d/%d formes ecrites (%s), %d ignorees.\n"
               "Template : %d slots. Verifie le rendu dans l'editeur FH6.",
               written, mn, s->located_via_rtti ? "RTTI" : "empreinte", skipped, cap);
    return written;
}

int fh6_inject_json(Fh6Session *s, const wchar_t *json_path, int template_count,
                    const Fh6Progress *prog, char *report, int report_sz)
{
    Fh6JsonDoc doc;
    if (fh6_load_json(json_path, &doc) != 0) {
        report_set(report, report_sz, "Fichier JSON FH6 invalide ou vide.");
        return -1;
    }
    int r = fh6_inject_doc(s, &doc, template_count, prog, report, report_sz);
    fh6_free_json(&doc);
    return r;
}
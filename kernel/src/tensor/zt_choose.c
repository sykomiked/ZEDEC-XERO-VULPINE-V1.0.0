/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_choose.c — zt_choose.h: measure each engine on the model, keep the
 * fastest exact ones. */
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(_DEFAULT_SOURCE)
#    define _DEFAULT_SOURCE 1 /* sysconf's _SC_PHYS_PAGES, clock_gettime */
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#    define _DARWIN_C_SOURCE 1
#endif
#include "zt_choose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zt_simd.h"

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <time.h>
#    include <unistd.h>
#    if defined(__APPLE__)
#        include <sys/sysctl.h>
#    endif
#endif

#define ZC_PROMPT  16u /* prompt tokens each engine reads */
#define ZC_MAXCAND 12u

static uint64_t now_ns(void)
{
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t) (c.QuadPart / f.QuadPart) * 1000000000u +
           (uint64_t) (c.QuadPart % f.QuadPart) * 1000000000u / (uint64_t) f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000u + (uint64_t) ts.tv_nsec;
#endif
}

/* ---- the machine ---- */

static void ram(uint64_t *total, uint64_t *avail)
{
    *total = *avail = 0;
#if defined(_WIN32)
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) {
        *total = ms.ullTotalPhys;
        *avail = ms.ullAvailPhys;
    }
#elif defined(__APPLE__)
    uint64_t v = 0;
    size_t l = sizeof v;
    if (sysctlbyname("hw.memsize", &v, &l, NULL, 0) == 0) *total = v;
#else
#    if defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
    long pg = sysconf(_SC_PAGESIZE), n = sysconf(_SC_PHYS_PAGES);
    if (pg > 0 && n > 0) *total = (uint64_t) pg * (uint64_t) n;
#        if defined(_SC_AVPHYS_PAGES)
    long a = sysconf(_SC_AVPHYS_PAGES);
    if (pg > 0 && a > 0) *avail = (uint64_t) pg * (uint64_t) a;
#        endif
#    endif
#endif
}

void zt_choose_host(zt_host_t *h, bool open_gpus)
{
    memset(h, 0, sizeof *h);
    h->ncpu = zt_pool_ncpu();
    ram(&h->ram_bytes, &h->ram_free_bytes);
    const zt_kern_t *ks[8];
    uint32_t n = zt_simd_list(ks, 8);
    for (uint32_t i = 0; i < n; i++) h->sets[h->n_sets++] = ks[i]->name;
    if (!open_gpus) return;
    const char *apis[ZT_CHOOSE_MAX_GPUS];
    uint32_t na = zt_accel_apis(apis, ZT_CHOOSE_MAX_GPUS);
    for (uint32_t i = 0; i < na; i++) {
        char why[256];
        zt_accel_t *a = zt_accel_open_api(apis[i], why, sizeof why);
        if (a) {
            snprintf(h->gpus[h->n_gpus++], sizeof h->gpus[0], "%s: %s", apis[i], zt_accel_name(a));
            zt_accel_free(a);
        } else {
            size_t l = strlen(h->why);
            if (l + 1 < sizeof h->why)
                snprintf(h->why + l, sizeof h->why - l, "%s%s", l ? "; " : "", why);
        }
    }
}

void zt_host_line(const zt_host_t *h, char *out, size_t cap)
{
    size_t l = 0;
    if (!cap) return;
    l += (size_t) snprintf(
        out, cap, "cpus=%u ram=%llu.%01lluGiB free=%llu.%01lluGiB sets=", (unsigned) h->ncpu,
        (unsigned long long) (h->ram_bytes >> 30),
        (unsigned long long) ((h->ram_bytes & ((1ull << 30) - 1)) * 10 >> 30),
        (unsigned long long) (h->ram_free_bytes >> 30),
        (unsigned long long) ((h->ram_free_bytes & ((1ull << 30) - 1)) * 10 >> 30));
    for (uint32_t i = 0; i < h->n_sets && l < cap; i++)
        l += (size_t) snprintf(out + l, cap - l, "%s%s", i ? "," : "", h->sets[i]);
    if (l < cap) l += (size_t) snprintf(out + l, cap - l, " gpus=");
    for (uint32_t i = 0; i < h->n_gpus && l < cap; i++)
        l += (size_t) snprintf(out + l, cap - l, "%s%s", i ? "; " : "", h->gpus[i]);
    if (!h->n_gpus && l < cap) snprintf(out + l, cap - l, "none");
}

/* ---- one model ---- */

typedef struct {
    char tag[48];         /* "cpu:avx2/8", "gpu:vulkan" */
    const zt_kern_t *set; /* CPU: the kernel set */
    uint32_t threads;
    zt_pool_t *pool;
    zt_accel_t *acc; /* GPU */
    zt_kern_t k;
    bool ok;      /* ran, and every logit matched */
    char why[96]; /* when not ok */
    uint64_t prompt_ns, step_ns;
} cand_t;

struct zt_engine {
    zt_pool_t *pool;
    zt_accel_t *acc;
    zt_kern_t cpu, k;
    char record[1536];
    char device[192];
};

static void add(char *b, size_t cap, const char *fmt, const char *s1, const char *s2)
{
    size_t l = strlen(b);
    if (l + 1 < cap) snprintf(b + l, cap - l, fmt, s1, s2);
}

/* The prompt (ZC_PROMPT tokens) and one step through k: last-token logits of
 * each into lp and ls, times into *pn and *sn. 0 or the engine's error. */
static int32_t run(zt_model_t *m, zt_model_state_t *s, const zt_kern_t *k, uint32_t np, zt_fx *lp,
                   zt_fx *ls, uint64_t *pn, uint64_t *sn)
{
    int32_t ids[ZC_PROMPT + 1];
    for (uint32_t i = 0; i <= np; i++) ids[i] = (int32_t) ((i * 7919u + 13u) % m->cfg.n_vocab);
    m->kern = k;
    zt_model_state_seek(s, 0);
    uint64_t t0 = now_ns();
    int32_t r = zt_model_eval(m, s, ids, np, lp, 0);
    uint64_t t1 = now_ns();
    if (!r) r = zt_model_eval(m, s, ids + np, 1, ls, 0);
    uint64_t t2 = now_ns();
    zt_model_state_seek(s, 0);
    *pn = t1 - t0;
    *sn = t2 - t1;
    return r;
}

static uint32_t env_u(const char *name, uint32_t dflt)
{
    const char *v = getenv(name);
    return v && *v ? (uint32_t) strtoul(v, NULL, 10) : dflt;
}

static void cand_cpu(cand_t *c, const zt_kern_t *set, uint32_t threads)
{
    memset(c, 0, sizeof *c);
    c->set = set;
    c->threads = threads;
    c->pool = threads > 1 ? zt_pool_new(threads) : NULL;
    if (threads > 1 && !c->pool) c->threads = 1;
    zt_kern_threaded(&c->k, set, c->pool);
    snprintf(c->tag, sizeof c->tag, "cpu:%s/%u", set->name, (unsigned) c->threads);
}

static void cand_drop(cand_t *c)
{
    zt_accel_free(c->acc);
    zt_pool_free(c->pool);
    c->acc = NULL;
    c->pool = NULL;
}

/* ZT_CHOICE: "cpu=SET/THREADS gpu=API:LO-HI" or "gpu=none"; 0 when parsed */
static int parse_choice(const char *v, char *set, size_t setcap, uint32_t *thr, char *api,
                        size_t apicap, uint32_t *lo, uint32_t *hi)
{
    const char *c = strstr(v, "cpu="), *g = strstr(v, "gpu=");
    if (!c || !g) return -1;
    c += 4;
    g += 4;
    size_t n = strcspn(c, "/ ");
    if (!n || n >= setcap || c[n] != '/') return -1;
    memcpy(set, c, n);
    set[n] = 0;
    *thr = (uint32_t) strtoul(c + n + 1, NULL, 10);
    size_t m = strcspn(g, ": ");
    if (!m || m >= apicap) return -1;
    memcpy(api, g, m);
    api[m] = 0;
    *lo = 0;
    *hi = UINT32_MAX;
    if (!strcmp(api, "none")) return 0;
    if (g[m] != ':') return -1;
    const char *r = g + m + 1;
    *lo = (uint32_t) strtoul(r, NULL, 10);
    const char *d = strchr(r, '-');
    if (!d) return -1;
    *hi = !strncmp(d + 1, "max", 3) ? UINT32_MAX : (uint32_t) strtoul(d + 1, NULL, 10);
    return 0;
}

static void finish(zt_engine_t *e, zt_model_t *m, const cand_t *cpu, uint32_t lo, uint32_t hi)
{
    char head[256], range[32];
    if (hi == UINT32_MAX)
        snprintf(range, sizeof range, "%u-max", (unsigned) lo);
    else
        snprintf(range, sizeof range, "%u-%u", (unsigned) lo, (unsigned) hi);
    snprintf(head, sizeof head, "zt-choice/1 cpu=%s/%u gpu=%s%s%s model=%s/%ux%u", cpu->set->name,
             (unsigned) cpu->threads, e->acc ? zt_accel_api(e->acc) : "none", e->acc ? ":" : "",
             e->acc ? range : "",
             m->cfg.arch == ZT_ARCH_QWEN2   ? "qwen2"
             : m->cfg.arch == ZT_ARCH_QWEN3 ? "qwen3"
                                            : "llama",
             (unsigned) m->cfg.n_embd, (unsigned) m->cfg.n_layer);
    /* the head in front of the measurements already in record */
    size_t hl = strlen(head), tl = strlen(e->record), sep = tl ? 3 : 0;
    if (hl + sep + tl >= sizeof e->record) tl = sizeof e->record - 1 - hl - sep;
    memmove(e->record + hl + sep, e->record, tl);
    memcpy(e->record, head, hl);
    if (sep) memcpy(e->record + hl, " | ", 3);
    e->record[hl + sep + tl] = 0;
    if (!e->acc)
        snprintf(e->device, sizeof e->device, "%s/%u", cpu->set->name, (unsigned) cpu->threads);
    else if (lo <= 1 && hi == UINT32_MAX)
        snprintf(e->device, sizeof e->device, "%s: %s (prompts and steps)", zt_accel_api(e->acc),
                 zt_accel_name(e->acc));
    else if (lo >= 2)
        snprintf(e->device, sizeof e->device, "%s: %s (prompts); %s/%u (steps)",
                 zt_accel_api(e->acc), zt_accel_name(e->acc), cpu->set->name,
                 (unsigned) cpu->threads);
    else
        snprintf(e->device, sizeof e->device, "%s/%u (prompts); %s: %s (steps)", cpu->set->name,
                 (unsigned) cpu->threads, zt_accel_api(e->acc), zt_accel_name(e->acc));
    const char *log = getenv("ZT_CHOICE_LOG");
    if (log && *log) {
        FILE *f = fopen(log, "a");
        if (f) {
            fprintf(f, "%s\n", e->record);
            fclose(f);
        }
    }
}

/* the GPU behind e for passes of lo..hi tokens, on top of e->cpu */
static void attach(zt_engine_t *e, zt_accel_t *acc, uint32_t lo, uint32_t hi)
{
    e->acc = acc;
    zt_accel_set_tokens(acc, lo, hi);
    zt_accel_kern(acc, &e->k, &e->cpu, true);
}

static zt_accel_t *open_gpu(const char *api, const zt_choose_in_t *in, char *why, uint32_t cap)
{
    zt_accel_t *a = zt_accel_open_api(api, why, cap);
    if (!a) return NULL;
    if (zt_accel_map(a, in->weights, in->weights_len)) {
        snprintf(why, cap, "the weights do not fit");
    } else if (zt_accel_map_state(a, in->state_mem, in->state_len)) {
        snprintf(why, cap, "cannot share the cache");
    } else {
        return a;
    }
    zt_accel_free(a);
    return NULL;
}

static zt_engine_t *replay(zt_model_t *m, const zt_choose_in_t *in, const char *v)
{
    char set[24], api[24];
    uint32_t thr, lo, hi;
    zt_engine_t *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    const zt_kern_t *k = NULL;
    if (parse_choice(v, set, sizeof set, &thr, api, sizeof api, &lo, &hi) == 0)
        k = zt_simd_find(set);
    cand_t c;
    if (!k) { /* not this machine's, or malformed: measure instead */
        free(e);
        return NULL;
    }
    cand_cpu(&c, k, thr ? thr : 1);
    e->pool = c.pool;
    e->cpu = c.k;
    e->k = c.k;
    snprintf(e->record, sizeof e->record, "replayed ZT_CHOICE");
    if (strcmp(api, "none")) {
        char why[256];
        zt_accel_t *a = open_gpu(api, in, why, sizeof why);
        if (a)
            attach(e, a, lo, hi);
        else
            add(e->record, sizeof e->record, "; gpu:%s unavailable (%s)", api, why);
    }
    m->kern = &e->k;
    finish(e, m, &c, lo, hi);
    return e;
}

zt_engine_t *zt_choose(zt_model_t *m, const zt_choose_in_t *in)
{
    const char *rv = getenv("ZT_CHOICE");
    if (rv && *rv) {
        zt_engine_t *e = replay(m, in, rv);
        if (e) return e;
    }
    zt_engine_t *e = calloc(1, sizeof *e);
    uint32_t nv = m->cfg.n_vocab;
    zt_fx *ref = malloc((size_t) nv * 2 * sizeof *ref),
          *got = malloc((size_t) nv * 2 * sizeof *got);
    cand_t *cs = calloc(ZC_MAXCAND, sizeof *cs);
    if (!e || !ref || !got || !cs) {
        free(e), free(ref), free(got), free(cs);
        m->kern = zt_simd_default();
        return NULL;
    }
    uint32_t np = in->s->n_ctx > ZC_PROMPT + 1 ? ZC_PROMPT : in->s->n_ctx - 1;

    /* the CPU engines: every set (C only when alone), all threads; the
     * fastest set again at half the threads (cores shared by two threads) */
    const zt_kern_t *sets[8];
    uint32_t nsets = zt_simd_list(sets, 8), nc = 0;
    const char *want = getenv("ZT_KERN");
    uint32_t thr = env_u("ZT_THREADS", zt_pool_ncpu());
    if (thr > 16) thr = 16;
    if (thr == 0) thr = 1;
    for (uint32_t i = 0; i < nsets && nc < ZC_MAXCAND; i++) {
        if (want && *want ? strcmp(sets[i]->name, want) != 0
                          : !strcmp(sets[i]->name, "c") && nsets > 1)
            continue;
        cand_cpu(&cs[nc++], sets[i], thr);
    }
    if (nc == 0) cand_cpu(&cs[nc++], sets[nsets - 1], thr); /* ZT_KERN names no set: C */
    if (!getenv("ZT_THREADS") && thr >= 4 && nc < ZC_MAXCAND)
        cand_cpu(&cs[nc++], cs[0].set, thr / 2);
    uint32_t ncpu_c = nc;

    /* measure: a warm-up run, then the timed one; logits against the first */
    bool have_ref = false;
    for (uint32_t i = 0; i < ncpu_c; i++) {
        uint64_t pn, sn;
        if (run(m, in->s, &cs[i].k, np, got, got + nv, &pn, &sn) ||
            run(m, in->s, &cs[i].k, np, got, got + nv, &pn, &sn)) {
            snprintf(cs[i].why, sizeof cs[i].why, "the pass failed");
            continue;
        }
        if (!have_ref) {
            memcpy(ref, got, (size_t) nv * 2 * sizeof *ref);
            have_ref = true;
        } else if (memcmp(ref, got, (size_t) nv * 2 * sizeof *ref)) {
            snprintf(cs[i].why, sizeof cs[i].why, "logits differ from %s", cs[0].tag);
            continue;
        }
        cs[i].ok = true;
        cs[i].prompt_ns = pn;
        cs[i].step_ns = sn;
    }
    int32_t bc = -1;
    for (uint32_t i = 0; i < ncpu_c; i++)
        if (cs[i].ok && (bc < 0 || cs[i].step_ns < cs[bc].step_ns)) bc = (int32_t) i;
    if (bc < 0) { /* no CPU engine ran: the default, unmeasured */
        for (uint32_t i = 0; i < nc; i++) cand_drop(&cs[i]);
        m->kern = zt_simd_default();
        snprintf(e->record, sizeof e->record, "no CPU engine ran the model");
        e->cpu = *zt_simd_default();
        e->k = e->cpu;
        free(ref), free(got), free(cs);
        cand_t d;
        memset(&d, 0, sizeof d);
        d.set = zt_simd_default();
        d.threads = 0;
        finish(e, m, &d, 0, 0);
        return e;
    }

    /* the GPUs, each with the chosen CPU engine under it */
    const char *g = getenv("ZT_GPU");
    bool no_gpu = g && (!strcmp(g, "cpu") || !strcmp(g, "off") || !strcmp(g, "none"));
    const char *apis[ZT_CHOOSE_MAX_GPUS];
    uint32_t na = no_gpu ? 0 : zt_accel_apis(apis, ZT_CHOOSE_MAX_GPUS);
    for (uint32_t i = 0; i < na && nc < ZC_MAXCAND; i++) {
        if (g && *g && strcmp(g, apis[i])) continue;
        cand_t *c = &cs[nc++];
        memset(c, 0, sizeof *c);
        snprintf(c->tag, sizeof c->tag, "gpu:%s", apis[i]);
        c->acc = open_gpu(apis[i], in, c->why, sizeof c->why);
        if (!c->acc) continue;
        zt_accel_kern(c->acc, &c->k, &cs[bc].k, true);
        uint64_t pn, sn;
        if (run(m, in->s, &c->k, np, got, got + nv, &pn, &sn) ||
            run(m, in->s, &c->k, np, got, got + nv, &pn, &sn)) {
            snprintf(c->why, sizeof c->why, "the pass failed");
        } else if (memcmp(ref, got, (size_t) nv * 2 * sizeof *ref)) {
            snprintf(c->why, sizeof c->why, "logits differ from %s", cs[0].tag);
        } else {
            zt_accel_stats_t st;
            zt_accel_stats(c->acc, &st);
            c->ok = true;
            c->prompt_ns = pn;
            c->step_ns = sn;
            if (st.passes == 0 && st.products == 0) {
                c->ok = false; /* exact only because the CPU did all of it */
                snprintf(c->why, sizeof c->why, "declined every pass and product");
            }
        }
        if (!c->ok) cand_drop(c);
    }
    int32_t bg = -1;
    for (uint32_t i = ncpu_c; i < nc; i++) {
        if (!cs[i].ok) continue;
        uint64_t w = cs[i].prompt_ns + cs[i].step_ns * np;
        if (bg < 0 || w < cs[bg].prompt_ns + cs[bg].step_ns * np) bg = (int32_t) i;
    }

    /* the record: every engine measured, then the choice in front */
    for (uint32_t i = 0; i < nc; i++) {
        size_t l = strlen(e->record);
        if (cs[i].ok)
            snprintf(e->record + l, sizeof e->record - l, "%s%s prompt %llu us/token, step %llu us",
                     l ? "; " : "", cs[i].tag, (unsigned long long) (cs[i].prompt_ns / np / 1000u),
                     (unsigned long long) (cs[i].step_ns / 1000u));
        else
            snprintf(e->record + l, sizeof e->record - l, "%s%s unavailable (%s)", l ? "; " : "",
                     cs[i].tag, cs[i].why);
    }

    cand_t chosen = cs[bc];
    e->pool = cs[bc].pool;
    e->cpu = cs[bc].k;
    e->k = e->cpu;
    uint32_t lo = 0, hi = UINT32_MAX;
    if (bg >= 0) {
        bool prompts = cs[bg].prompt_ns < cs[bc].prompt_ns, steps = cs[bg].step_ns < cs[bc].step_ns;
        if (prompts || steps) {
            lo = steps ? 0 : 2;
            hi = prompts ? UINT32_MAX : 1;
            zt_accel_t *a = cs[bg].acc;
            cs[bg].acc = NULL;
            attach(e, a, lo, hi);
        }
    }
    for (uint32_t i = 0; i < nc; i++) {
        if ((int32_t) i == bc) continue;
        cand_drop(&cs[i]);
    }
    m->kern = &e->k;
    zt_model_state_seek(in->s, 0);
    finish(e, m, &chosen, lo, hi);
    free(ref), free(got), free(cs);
    return e;
}

void zt_engine_free(zt_engine_t *e)
{
    if (!e) return;
    zt_accel_free(e->acc);
    zt_pool_free(e->pool);
    free(e);
}

const char *zt_engine_record(const zt_engine_t *e)
{
    return e ? e->record : "";
}

const char *zt_engine_device(const zt_engine_t *e)
{
    return e ? e->device : "";
}

zt_accel_t *zt_engine_accel(zt_engine_t *e)
{
    return e ? e->acc : NULL;
}

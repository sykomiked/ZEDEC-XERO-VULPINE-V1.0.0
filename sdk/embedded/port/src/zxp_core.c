/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_core.c — the OS-independent half of the port layer: the fail-closed
 * entropy gate, the counting allocator wrapper, mutex and condition logic,
 * single-owner checks and the fatal path. Each RTOS supplies zxp_os.h. */
#include <string.h>

#include "zxp.h"
#include "zxp_os.h"

/* ===================================================================== */
/* State                                                                 */
/* ===================================================================== */

static zxp_entropy_fn g_entropy;
static void *g_entropy_user;
static volatile int g_ready;        /* zxp_init succeeded                 */
static volatile int g_entropy_dead; /* a health test failed: latched      */
static uint8_t g_last_block[16];
static int g_have_last;

static zxp_allocator g_alloc = {zxp_os_alloc, zxp_os_free, NULL};
static size_t g_limit;
static zxp_heap_stats_t g_stats;

static void *volatile g_owner;
static zxp_fatal_fn g_fatal;

const char *zxp_status_name(int status)
{
    switch (status) {
    case ZXP_OK: return "ZXP_OK";
    case ZXP_E_ARG: return "ZXP_E_ARG";
    case ZXP_E_NO_ENTROPY: return "ZXP_E_NO_ENTROPY";
    case ZXP_E_ENTROPY: return "ZXP_E_ENTROPY";
    case ZXP_E_STATE: return "ZXP_E_STATE";
    case ZXP_E_NOMEM: return "ZXP_E_NOMEM";
    case ZXP_E_OS: return "ZXP_E_OS";
    default: return "ZXP_E_UNKNOWN";
    }
}

/* ===================================================================== */
/* Fatal                                                                 */
/* ===================================================================== */

void zxp_set_fatal_handler(zxp_fatal_fn fn)
{
    g_fatal = fn;
}

void zxp_fatal(const char *why)
{
    zxp_fatal_fn fn = g_fatal;
    if (fn) fn(why ? why : "zxp_fatal");
    zxp_os_halt(why ? why : "zxp_fatal");
}

/* ===================================================================== */
/* Entropy                                                               */
/* ===================================================================== */

int zxp_set_entropy(zxp_entropy_fn fn, void *user)
{
    if (!fn) return ZXP_E_ARG;
    if (g_ready) return ZXP_E_STATE;
    g_entropy = fn;
    g_entropy_user = user;
    return ZXP_OK;
}

static int all_same(const uint8_t *b, size_t n)
{
    for (size_t i = 1; i < n; i++)
        if (b[i] != b[0]) return 0;
    return 1;
}

static void latch_dead(void)
{
    g_entropy_dead = 1;
    g_ready = 0;
}

int zxp_init(void)
{
    if (g_ready) return ZXP_E_STATE;
    if (g_entropy_dead) return ZXP_E_ENTROPY;
    if (!g_entropy) return ZXP_E_NO_ENTROPY;
    uint8_t a[32], b[32];
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    if (g_entropy(g_entropy_user, a, sizeof a) != 0 || g_entropy(g_entropy_user, b, sizeof b) != 0 ||
        all_same(a, sizeof a) || all_same(b, sizeof b) || memcmp(a, b, sizeof a) == 0) {
        memset(a, 0, sizeof a);
        memset(b, 0, sizeof b);
        latch_dead();
        return ZXP_E_ENTROPY;
    }
    zxp_os_crit_enter();
    memcpy(g_last_block, b, sizeof g_last_block);
    g_have_last = 1;
    zxp_os_crit_exit();
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    g_ready = 1;
    return ZXP_OK;
}

int zxp_ready(void)
{
    return g_ready && !g_entropy_dead;
}

int zxp_entropy_fill(uint8_t *buf, size_t len)
{
    if (!buf && len) return ZXP_E_ARG;
    if (g_entropy_dead) return ZXP_E_ENTROPY;
    if (!g_entropy) return ZXP_E_NO_ENTROPY;
    if (!g_ready) return ZXP_E_STATE;
    if (len == 0) return ZXP_OK;
    if (g_entropy(g_entropy_user, buf, len) != 0) {
        memset(buf, 0, len);
        latch_dead();
        return ZXP_E_ENTROPY;
    }
    /* Continuous test on the first block of each request of >= 16 bytes. */
    if (len >= sizeof g_last_block) {
        int repeat;
        zxp_os_crit_enter();
        repeat = g_have_last && memcmp(g_last_block, buf, sizeof g_last_block) == 0;
        memcpy(g_last_block, buf, sizeof g_last_block);
        g_have_last = 1;
        zxp_os_crit_exit();
        if (repeat || all_same(buf, sizeof g_last_block)) {
            memset(buf, 0, len);
            latch_dead();
            return ZXP_E_ENTROPY;
        }
    }
    return ZXP_OK;
}

int zxp_getentropy(void *buf, size_t len)
{
    return zxp_entropy_fill((uint8_t *) buf, len) == ZXP_OK ? 0 : -1;
}

/* ===================================================================== */
/* Allocator                                                             */
/* ===================================================================== */

/* Every block carries a 16-byte header (size and a tag), so realloc works
 * on any backend that only offers alloc/free, the live byte count is exact,
 * and freeing a pointer this layer did not hand out is caught (fatal)
 * instead of corrupting the heap. 16 bytes keeps the backend's alignment. */
#define ZXP_HDR   16u
#define ZXP_TAG   0x5A58504Du /* "ZXPM" */
#define ZXP_FREED 0x46524545u /* "FREE" */

typedef struct {
    uint64_t size;
    uint32_t tag;
    uint32_t pad;
} zxp_hdr;

_Static_assert(sizeof(zxp_hdr) <= ZXP_HDR, "header fits");

int zxp_set_allocator(const zxp_allocator *a)
{
    if (g_ready) return ZXP_E_STATE;
    if (a && (!a->alloc || !a->free)) return ZXP_E_ARG;
    if (a) {
        g_alloc = *a;
    } else {
        g_alloc.alloc = zxp_os_alloc;
        g_alloc.free = zxp_os_free;
        g_alloc.user = NULL;
    }
    return ZXP_OK;
}

void zxp_set_heap_limit(size_t bytes)
{
    zxp_os_crit_enter();
    g_limit = bytes;
    zxp_os_crit_exit();
}

void zxp_heap_stats(zxp_heap_stats_t *out)
{
    if (!out) return;
    zxp_os_crit_enter();
    *out = g_stats;
    zxp_os_crit_exit();
}

static int reserve(size_t n)
{
    int ok;
    zxp_os_crit_enter();
    ok = !(g_limit && (n > g_limit || g_stats.live_bytes > g_limit - n));
    if (ok) {
        g_stats.live_bytes += n;
        g_stats.live_blocks++;
    } else {
        g_stats.failures++;
    }
    zxp_os_crit_exit();
    return ok;
}

static void unreserve(size_t n, int failed)
{
    zxp_os_crit_enter();
    g_stats.live_bytes -= n;
    g_stats.live_blocks--;
    if (failed) g_stats.failures++;
    zxp_os_crit_exit();
}

void *zxp_malloc(size_t n)
{
    if (n == 0) n = 1;
    if (n > SIZE_MAX - ZXP_HDR) return NULL;
    if (!reserve(n)) return NULL;
    uint8_t *raw = (uint8_t *) g_alloc.alloc(g_alloc.user, n + ZXP_HDR);
    if (!raw) {
        unreserve(n, 1);
        return NULL;
    }
    zxp_os_crit_enter(); /* peak counts only blocks the backend really gave */
    if (g_stats.live_bytes > g_stats.peak_bytes) g_stats.peak_bytes = g_stats.live_bytes;
    zxp_os_crit_exit();
    zxp_hdr h = {(uint64_t) n, ZXP_TAG, 0};
    memcpy(raw, &h, sizeof h);
    return raw + ZXP_HDR;
}

static zxp_hdr *hdr_of(void *p)
{
    zxp_hdr *h = (zxp_hdr *) (void *) ((uint8_t *) p - ZXP_HDR);
    if (h->tag != ZXP_TAG)
        zxp_fatal(h->tag == ZXP_FREED ? "zxp_free: double free" : "zxp_free: pointer not from zxp_malloc");
    return h;
}

void zxp_free(void *p)
{
    if (!p) return;
    zxp_hdr *h = hdr_of(p);
    size_t n = (size_t) h->size;
    h->tag = ZXP_FREED;
    g_alloc.free(g_alloc.user, (uint8_t *) p - ZXP_HDR);
    unreserve(n, 0);
}

void *zxp_calloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size) {
        zxp_os_crit_enter();
        g_stats.failures++;
        zxp_os_crit_exit();
        return NULL;
    }
    size_t n = count * size;
    void *p = zxp_malloc(n);
    if (p) memset(p, 0, n ? n : 1);
    return p;
}

void *zxp_realloc(void *p, size_t n)
{
    if (!p) return zxp_malloc(n);
    if (n == 0) n = 1;
    size_t old = (size_t) hdr_of(p)->size;
    void *q = zxp_malloc(n);
    if (!q) return NULL; /* the old block stays valid, as realloc requires */
    memcpy(q, p, old < n ? old : n);
    zxp_free(p);
    return q;
}

char *zxp_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *) zxp_malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* ===================================================================== */
/* Mutex and condition                                                   */
/* ===================================================================== */

void zxp_set_owner_current(void)
{
    g_owner = zxp_os_task_self();
}

int zxp_no_tls_build(void)
{
#ifdef ZXP_NO_TLS
    return 1;
#else
    return 0;
#endif
}

static void check_owner(void)
{
#ifdef ZXP_NO_TLS
    void *o = g_owner;
    if (o && zxp_os_task_self() != o)
        zxp_fatal("libzxv called from a task other than the owner (ZXP_NO_TLS build)");
#endif
}

int zxp_mutex_init(zxp_mutex_t *m)
{
    if (!m) return ZXP_E_ARG;
    if (!zxp_ready()) return ZXP_E_STATE; /* fail closed: no entropy, no contexts */
    check_owner();
    memset(m, 0, sizeof *m);
    if (zxp_os_mutex_create(m) != 0) return ZXP_E_OS;
    m->state = 1;
    return ZXP_OK;
}

int zxp_mutex_destroy(zxp_mutex_t *m)
{
    if (!m) return ZXP_E_ARG;
    if (m->state == 1) zxp_os_mutex_delete(m);
    m->state = 2;
    return ZXP_OK;
}

int zxp_mutex_lock(zxp_mutex_t *m)
{
    if (!m) zxp_fatal("zxp_mutex_lock: NULL");
    check_owner();
    if (m->state == 0) {
        /* PTHREAD_MUTEX_INITIALIZER: create on first use. */
        if (!zxp_ready()) zxp_fatal("zxp_mutex_lock before zxp_init (no entropy source)");
        zxp_os_crit_enter();
        int rc = 0;
        if (m->state == 0) {
            rc = zxp_os_mutex_create(m);
            if (rc == 0) m->state = 1;
        }
        zxp_os_crit_exit();
        if (rc != 0) zxp_fatal("zxp_mutex_lock: lazy mutex creation failed");
    }
    if (m->state != 1) zxp_fatal("zxp_mutex_lock: mutex destroyed or never initialised");
    if (zxp_os_mutex_lock(m) != 0) zxp_fatal("zxp_mutex_lock: OS refused the lock");
    return ZXP_OK;
}

int zxp_mutex_unlock(zxp_mutex_t *m)
{
    if (!m || m->state != 1) zxp_fatal("zxp_mutex_unlock: bad mutex");
    if (zxp_os_mutex_unlock(m) != 0) zxp_fatal("zxp_mutex_unlock: OS refused the unlock");
    return ZXP_OK;
}

int zxp_cond_init(zxp_cond_t *c)
{
    if (!c) return ZXP_E_ARG;
    c->seq = 0;
    return ZXP_OK;
}

int zxp_cond_destroy(zxp_cond_t *c)
{
    (void) c;
    return ZXP_OK;
}

int zxp_cond_wait(zxp_cond_t *c, zxp_mutex_t *m)
{
    (void) c;
    zxp_mutex_unlock(m);
    zxp_os_sleep_tick();
    return zxp_mutex_lock(m);
}

int zxp_cond_broadcast(zxp_cond_t *c)
{
    if (c) c->seq++;
    return ZXP_OK;
}

/* ===================================================================== */
/* Time                                                                  */
/* ===================================================================== */

uint64_t zxp_monotonic_us(void)
{
    return zxp_os_monotonic_us();
}

void zxp_sleep_tick(void)
{
    zxp_os_sleep_tick();
}

#ifdef ZXP_TESTING
void zxp_test_reset(void)
{
    g_entropy = 0;
    g_entropy_user = 0;
    g_ready = 0;
    g_entropy_dead = 0;
    g_have_last = 0;
    memset(g_last_block, 0, sizeof g_last_block);
    g_alloc.alloc = zxp_os_alloc;
    g_alloc.free = zxp_os_free;
    g_alloc.user = 0;
    g_limit = 0;
    g_owner = 0;
    g_fatal = 0;
}
#endif

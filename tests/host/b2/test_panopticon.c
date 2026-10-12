/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_panopticon.c: unit tests for kernel/src/panopticon/panopticon.c.
 *
 * Watcher registration (dedup on every address byte and the port, full
 * table, slot reuse), per-type starting friendliness, per-action deltas,
 * every threat threshold, clamping, the tick's watching timeout, address
 * classification boundaries, every name table, friendliness labels, the
 * event ring (wrap, since-tick filter, max), the exact text of the self,
 * watcher and alert reports, the summary, auto-rating, and the module's
 * declared bring-up hook. Watcher ids come from a file-static counter that
 * init does not reset, so ids are tracked across the whole run. */
#define _GNU_SOURCE
#include <link.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panopticon.h"
#include "zxv_decl.h"

static int checks, failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) {                                                           \
            failures++;                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #c);                       \
        }                                                                     \
    } while (0)

/* ---- finding the file's static ZXV_DECLARE record (see desktop test) ---- */
struct seg { uintptr_t lo, hi; int x, w; };
static struct seg segs[32];
static int nsegs;

static int phdr_cb(struct dl_phdr_info *info, size_t sz, void *data) {
    (void)sz;
    (void)data;
    for (int i = 0; i < info->dlpi_phnum && nsegs < 32; i++) {
        const ElfW(Phdr) *p = &info->dlpi_phdr[i];
        if (p->p_type != PT_LOAD || !(p->p_flags & PF_R)) continue;
        segs[nsegs].lo = info->dlpi_addr + p->p_vaddr;
        segs[nsegs].hi = segs[nsegs].lo + p->p_memsz;
        segs[nsegs].x = (p->p_flags & PF_X) != 0;
        segs[nsegs].w = (p->p_flags & PF_W) != 0;
        nsegs++;
    }
    return 1;
}

static int in_seg(uintptr_t p, size_t n, int need_x) {
    for (int i = 0; i < nsegs; i++)
        if (p >= segs[i].lo && p + n <= segs[i].hi && (!need_x || segs[i].x)) return 1;
    return 0;
}

__attribute__((no_sanitize_address)) static zxv_bringup_fn find_bringup(const char *name) {
    size_t len = 0;
    while (name[len]) len++;
    len++;
    nsegs = 0;
    dl_iterate_phdr(phdr_cb, NULL);
    for (int s = 0; s < nsegs; s++) {
        for (uintptr_t a = (segs[s].lo + 7u) & ~(uintptr_t)7u; a + 16u <= segs[s].hi; a += 8u) {
            const uintptr_t *w = (const uintptr_t *)a;
            uintptr_t m = w[0], f = w[1];
            if (!f || !in_seg(f, 1, 1) || !in_seg(m, len, 0)) continue;
            const char *mn = (const char *)m;
            size_t i = 0;
            while (i < len && mn[i] == name[i]) i++;
            if (i == len) return (zxv_bringup_fn)f;
        }
    }
    return NULL;
}

#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static panopticon_state_t *new_state(void) {
    panopticon_state_t *s = malloc(sizeof(*s));
    memset(s, 0xA5, sizeof(*s));
    CHECK(panopticon_init(s) == 0);
    return s;
}

static panopticon_watcher_t *find_w(panopticon_state_t *s, int id) {
    for (int i = 0; i < PANOPTICON_MAX_WATCHERS; i++)
        if (s->watchers[i].active && s->watchers[i].id == (uint32_t)id) return &s->watchers[i];
    return NULL;
}

static const char *SELF_DESC =
    "Panopticon: Active surveillance awareness system. Monitoring all "
    "incoming connections, classifying watchers, rating friendliness, "
    "and providing real-time transparency about observation activities.";

/* The bring-up works on a function-static panopticon_state_t. Every state
 * this test makes is on the heap, so the only one in the writable segments
 * carrying the init self-description is that one. Uses segs from
 * find_bringup(). */
__attribute__((no_sanitize_address)) static panopticon_state_t *find_static_state(void) {
    size_t off = offsetof(panopticon_state_t, self_description);
    size_t len = strlen(SELF_DESC) + 1;
    for (int s = 0; s < nsegs; s++) {
        if (!segs[s].w) continue;
        for (uintptr_t b = (segs[s].lo + 7u) & ~(uintptr_t)7u;
             b + sizeof(panopticon_state_t) <= segs[s].hi; b += 8u) {
            const char *c = (const char *)(b + off);
            size_t i = 0;
            while (i < len && c[i] == SELF_DESC[i]) i++;
            if (i == len) return (panopticon_state_t *)b;
        }
    }
    return NULL;
}

/* Call the bring-up first: it is the only caller that sees next_watcher_id
 * at 1, and it registers one watcher (id 1). Every later id follows. */
static int next_id = 1;

static void test_bringup(void) {
    zxv_bringup_fn fn = find_bringup("panopticon");
    CHECK(fn != NULL);
    if (fn) {
        CHECK(fn() == 0);
        next_id = 2;
    }
    panopticon_state_t *st = find_static_state();
    CHECK(st != NULL);
    if (!st) return;
    CHECK(st->phase_tick == 1);
    CHECK(st->watcher_count == 1);
    panopticon_watcher_t *w = &st->watchers[0];
    CHECK(w->active == 1 && w->id == 1);
    CHECK(w->ip[0] == 10 && w->ip[1] == 0 && w->ip[2] == 0 && w->ip[3] == 1);
    CHECK(w->port == 443);
    CHECK(w->type == WATCHER_NETWORK && w->friendliness == -10);
    CHECK(strcmp((char *)w->name, "probe") == 0);
    CHECK(w->first_seen == 1 && w->last_seen == 1);
    CHECK(st->watchers[1].active == 0);
}

static void test_init_tick(void) {
    CHECK(panopticon_init(NULL) == -1);
    CHECK(panopticon_tick(NULL) == -1);
    panopticon_state_t *s = new_state();
    CHECK(s->watcher_count == 0);
    CHECK(s->event_count == 0);
    CHECK(s->event_head == 0);
    CHECK(s->phase_tick == 0);
    CHECK(s->watchers[0].active == 0 && s->watchers[127].active == 0);
    CHECK(s->events[255].timestamp == 0);
    CHECK(s->self_active == 1 && s->self_scanning == 1);
    CHECK(s->self_logging == 1 && s->self_reporting == 1);
    CHECK(strcmp((char *)s->self_description, SELF_DESC) == 0);

    CHECK(panopticon_tick(s) == 0);
    CHECK(s->phase_tick == 1);

    /* watching_you drops once more than 100 ticks have passed since last_seen */
    int id = panopticon_register_watcher(s, IP(8, 8, 8, 8), 53, WATCHER_UNKNOWN, "dns");
    CHECK(id == next_id);
    next_id++;
    panopticon_watcher_t *w = find_w(s, id);
    CHECK(w == &s->watchers[0]);
    CHECK(w->last_seen == 1 && w->first_seen == 1);
    /* a watcher in the last slot, and an inactive one that must be skipped */
    s->watchers[127] = *w;
    s->watchers[127].id = 9999;
    s->watchers[126] = *w;
    s->watchers[126].active = 0;
    for (int i = 0; i < 100; i++) panopticon_tick(s);
    CHECK(s->phase_tick == 101);
    CHECK(w->watching_you == 1); /* 101 - 1 == 100: not yet */
    CHECK(s->watchers[127].watching_you == 1);
    panopticon_tick(s);
    CHECK(s->phase_tick == 102);
    CHECK(w->watching_you == 0);
    CHECK(s->watchers[127].watching_you == 0);
    CHECK(s->watchers[126].watching_you == 1);
    CHECK(w->active == 1);
    free(s);
}

static void test_register(void) {
    CHECK(panopticon_register_watcher(NULL, 1, 1, WATCHER_UNKNOWN, "x") == -1);
    panopticon_state_t *s = new_state();
    s->phase_tick = 77;
    int id = panopticon_register_watcher(s, IP(10, 20, 30, 40), 443, WATCHER_P2P_PEER, "peer");
    CHECK(id == next_id);
    next_id++;
    panopticon_watcher_t *w = find_w(s, id);
    CHECK(w != NULL);
    CHECK(w->id == (uint32_t)id);
    CHECK(w->ip[0] == 10 && w->ip[1] == 20 && w->ip[2] == 30 && w->ip[3] == 40);
    CHECK(w->port == 443);
    CHECK(w->type == WATCHER_P2P_PEER);
    CHECK(w->friendliness == 20);
    CHECK(w->threat == THREAT_NONE);
    CHECK(w->last_action == ACTION_OBSERVE);
    CHECK(w->first_seen == 77 && w->last_seen == 77);
    CHECK(w->event_count == 0);
    CHECK(w->active == 1 && w->watching_you == 1);
    CHECK(strcmp((char *)w->name, "peer") == 0);
    CHECK(w->description[0] == 0);
    CHECK(w->pubkey[0] == 0 && w->pubkey[31] == 0);
    CHECK(s->watcher_count == 1);

    /* duplicate: same id, last_seen refreshed, watching restored, no new slot */
    s->phase_tick = 90;
    w->watching_you = 0;
    CHECK(panopticon_register_watcher(s, IP(10, 20, 30, 40), 443, WATCHER_GOVERNMENT, "again") == id);
    CHECK(w->last_seen == 90 && w->first_seen == 77);
    CHECK(w->watching_you == 1);
    CHECK(w->type == WATCHER_P2P_PEER);
    CHECK(strcmp((char *)w->name, "peer") == 0);
    CHECK(s->watcher_count == 1);
    CHECK(s->watchers[1].active == 0);

    /* any differing byte or port is a different watcher */
    uint32_t variants[4] = {IP(11, 20, 30, 40), IP(10, 21, 30, 40), IP(10, 20, 31, 40),
                            IP(10, 20, 30, 41)};
    for (int i = 0; i < 4; i++) {
        CHECK(panopticon_register_watcher(s, variants[i], 443, WATCHER_UNKNOWN, NULL) == next_id);
        next_id++;
    }
    CHECK(panopticon_register_watcher(s, IP(10, 20, 30, 40), 444, WATCHER_UNKNOWN, NULL) == next_id);
    next_id++;
    CHECK(s->watcher_count == 6);
    CHECK(strcmp((char *)s->watchers[1].name, "Unknown") == 0);
    CHECK(s->watchers[1].friendliness == 0);
    CHECK(s->watchers[1].ip[0] == 11);
    CHECK(s->watchers[2].ip[1] == 21);
    CHECK(s->watchers[3].ip[2] == 31);
    CHECK(s->watchers[4].ip[3] == 41);
    CHECK(s->watchers[5].port == 444);

    /* starting friendliness per type */
    static const struct { panopticon_watcher_type_t t; int f; } types[] = {
        {WATCHER_UNKNOWN, 0},     {WATCHER_LOCAL_USER, 50}, {WATCHER_P2P_PEER, 20},
        {WATCHER_NETWORK, -10},   {WATCHER_SYSTEM, 30},     {WATCHER_EXTERNAL, -20},
        {WATCHER_GOVERNMENT, -50}, {WATCHER_CORPORATE, -40}, {WATCHER_ADMIRALTY, 60},
        {(panopticon_watcher_type_t)42, 0},
    };
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        int t = panopticon_register_watcher(s, IP(1, 1, 1, i), 1, types[i].t, "t");
        CHECK(t == next_id);
        next_id++;
        CHECK(panopticon_get_friendliness(s, (uint32_t)t) == types[i].f);
        CHECK(panopticon_get_threat(s, (uint32_t)t) == THREAT_NONE);
        CHECK(find_w(s, t)->type == types[i].t);
    }

    /* name truncation at 63 */
    char big[200];
    memset(big, 'N', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    int b = panopticon_register_watcher(s, IP(2, 2, 2, 2), 2, WATCHER_UNKNOWN, big);
    next_id++;
    CHECK(strlen((char *)find_w(s, b)->name) == 63);
    free(s);

    /* full table, removal, slot reuse */
    s = new_state();
    int first = next_id;
    for (int i = 0; i < PANOPTICON_MAX_WATCHERS; i++) {
        CHECK(panopticon_register_watcher(s, IP(5, 0, i, 1), 80, WATCHER_UNKNOWN, "f") == next_id);
        next_id++;
    }
    CHECK(s->watcher_count == 128);
    CHECK(panopticon_register_watcher(s, IP(6, 0, 0, 1), 80, WATCHER_UNKNOWN, "f") == -1);
    CHECK(s->watcher_count == 128);
    /* the last slot is still found as a duplicate */
    CHECK(panopticon_register_watcher(s, IP(5, 0, 127, 1), 80, WATCHER_UNKNOWN, "f") == first + 127);
    CHECK(panopticon_remove_watcher(s, (uint32_t)(first + 5)) == 0);
    CHECK(s->watcher_count == 127);
    CHECK(s->watchers[5].active == 0 && s->watchers[5].watching_you == 0);
    /* a removed watcher's address is not a duplicate any more */
    CHECK(panopticon_register_watcher(s, IP(5, 0, 5, 1), 80, WATCHER_SYSTEM, "re") == next_id);
    CHECK(s->watchers[5].id == (uint32_t)next_id);
    CHECK(s->watchers[5].friendliness == 30);
    next_id++;
    CHECK(s->watcher_count == 128);
    free(s);
}

static void test_update_remove_rate(void) {
    CHECK(panopticon_update_watcher(NULL, 1, ACTION_OBSERVE, "x") == -1);
    CHECK(panopticon_remove_watcher(NULL, 1) == -1);
    CHECK(panopticon_rate_friendliness(NULL, 1, 1) == -1);
    CHECK(panopticon_get_friendliness(NULL, 1) == 0);
    CHECK(panopticon_get_threat(NULL, 1) == THREAT_NONE);

    panopticon_state_t *s = new_state();
    /* per-action delta from a 50-point local user */
    static const struct { panopticon_action_t a; int d; } acts[] = {
        {ACTION_OBSERVE, -1},      {ACTION_PROBE, -5},        {ACTION_SCAN, -10},
        {ACTION_CONNECT, -2},      {ACTION_DATA_REQUEST, -3}, {ACTION_FINGERPRINT, -15},
        {ACTION_TRACK, -20},       {ACTION_INJECT, -40},      {ACTION_MITM, -50},
        {(panopticon_action_t)77, 0},
    };
    for (unsigned i = 0; i < sizeof(acts) / sizeof(acts[0]); i++) {
        s->phase_tick = 1000 + i;
        int id = panopticon_register_watcher(s, IP(9, 9, 9, i), 9, WATCHER_LOCAL_USER, "u");
        next_id++;
        panopticon_watcher_t *w = find_w(s, id);
        w->watching_you = 0;
        s->phase_tick = 2000 + i;
        CHECK(panopticon_update_watcher(s, (uint32_t)id, acts[i].a, "did a thing") == 0);
        CHECK(w->friendliness == 50 + acts[i].d);
        CHECK(w->last_action == acts[i].a);
        CHECK(w->last_seen == 2000 + i);
        CHECK(w->first_seen == 1000 + i);
        CHECK(w->watching_you == 1);
        CHECK(w->event_count == 1);
        CHECK(strcmp((char *)w->description, "did a thing") == 0);
        CHECK(w->threat == THREAT_NONE);
        CHECK(s->event_count == i + 1);
        panopticon_event_t *e = &s->events[i];
        CHECK(e->watcher_id == (uint32_t)id);
        CHECK(e->action == acts[i].a);
        CHECK(e->friendliness_delta == acts[i].d);
        CHECK(e->timestamp == 2000 + i);
        CHECK(strcmp((char *)e->detail, "did a thing") == 0);
    }

    /* threat thresholds, each approached by an OBSERVE (-1) */
    int id = panopticon_register_watcher(s, IP(3, 3, 3, 3), 3, WATCHER_UNKNOWN, "thr");
    next_id++;
    panopticon_watcher_t *w = find_w(s, id);
    static const struct { int start; panopticon_threat_t t; } th[] = {
        {1, THREAT_NONE},    {0, THREAT_LOW},     {-18, THREAT_LOW},     {-19, THREAT_MEDIUM},
        {-48, THREAT_MEDIUM}, {-49, THREAT_HIGH}, {-78, THREAT_HIGH},    {-79, THREAT_CRITICAL},
        {-99, THREAT_CRITICAL},
    };
    for (unsigned i = 0; i < sizeof(th) / sizeof(th[0]); i++) {
        w->friendliness = (int8_t)th[i].start;
        CHECK(panopticon_update_watcher(s, (uint32_t)id, ACTION_OBSERVE, NULL) == 0);
        CHECK(w->friendliness == th[i].start - 1);
        CHECK(w->threat == th[i].t);
        CHECK(panopticon_get_threat(s, (uint32_t)id) == th[i].t);
    }
    /* NULL detail keeps the description, logs "" */
    CHECK(w->description[0] == 0);
    w->friendliness = 0;
    CHECK(panopticon_update_watcher(s, (uint32_t)id, ACTION_PROBE, "first") == 0);
    CHECK(panopticon_update_watcher(s, (uint32_t)id, ACTION_PROBE, NULL) == 0);
    CHECK(strcmp((char *)w->description, "first") == 0);
    CHECK(w->event_count == 11);
    CHECK(w->friendliness == -10);
    uint32_t last = (s->event_head + PANOPTICON_MAX_EVENTS - 1) % PANOPTICON_MAX_EVENTS;
    CHECK(s->events[last].detail[0] == 0);
    char big[400];
    memset(big, 'D', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    panopticon_update_watcher(s, (uint32_t)id, ACTION_PROBE, big);
    CHECK(strlen((char *)w->description) == 255);

    /* unknown and removed ids */
    CHECK(panopticon_update_watcher(s, 999999, ACTION_OBSERVE, "x") == -1);
    CHECK(panopticon_rate_friendliness(s, 999999, 5) == -1);
    CHECK(panopticon_get_friendliness(s, 999999) == 0);
    CHECK(panopticon_get_threat(s, 999999) == THREAT_NONE);
    uint32_t before = s->watcher_count;
    CHECK(panopticon_remove_watcher(s, 999999) == -1);
    CHECK(s->watcher_count == before);
    w->threat = THREAT_HIGH;
    CHECK(panopticon_remove_watcher(s, (uint32_t)id) == 0);
    CHECK(s->watcher_count == before - 1);
    CHECK(panopticon_remove_watcher(s, (uint32_t)id) == -1);
    CHECK(s->watcher_count == before - 1);
    uint32_t ev = s->event_count;
    CHECK(panopticon_update_watcher(s, (uint32_t)id, ACTION_OBSERVE, "x") == -1);
    CHECK(s->event_count == ev);
    CHECK(panopticon_rate_friendliness(s, (uint32_t)id, 5) == -1);
    CHECK(panopticon_get_friendliness(s, (uint32_t)id) == 0);
    CHECK(panopticon_get_threat(s, (uint32_t)id) == THREAT_NONE);
    free(s);

    /* rating and clamping (sums kept inside int8 range) */
    s = new_state();
    int a = panopticon_register_watcher(s, IP(4, 4, 4, 4), 4, WATCHER_ADMIRALTY, "adm");
    int g = panopticon_register_watcher(s, IP(4, 4, 4, 5), 4, WATCHER_GOVERNMENT, "gov");
    next_id += 2;
    CHECK(panopticon_rate_friendliness(s, (uint32_t)a, 15) == 0);
    CHECK(panopticon_get_friendliness(s, (uint32_t)a) == 75);
    CHECK(panopticon_rate_friendliness(s, (uint32_t)a, 25) == 0);
    CHECK(panopticon_get_friendliness(s, (uint32_t)a) == 100);
    CHECK(panopticon_rate_friendliness(s, (uint32_t)a, 0) == 0);
    CHECK(panopticon_get_friendliness(s, (uint32_t)a) == 100);
    CHECK(panopticon_rate_friendliness(s, (uint32_t)a, 27) == 0);
    CHECK(panopticon_get_friendliness(s, (uint32_t)a) == 100);
    CHECK(panopticon_rate_friendliness(s, (uint32_t)a, -1) == 0);
    CHECK(panopticon_get_friendliness(s, (uint32_t)a) == 99);
    CHECK(panopticon_rate_friendliness(s, (uint32_t)g, -50) == 0);
    CHECK(panopticon_get_friendliness(s, (uint32_t)g) == -100);
    CHECK(panopticon_rate_friendliness(s, (uint32_t)g, -28) == 0);
    CHECK(panopticon_get_friendliness(s, (uint32_t)g) == -100);
    CHECK(panopticon_rate_friendliness(s, (uint32_t)g, 1) == 0);
    CHECK(panopticon_get_friendliness(s, (uint32_t)g) == -99);
    /* rating does not touch the threat level */
    CHECK(panopticon_get_threat(s, (uint32_t)g) == THREAT_NONE);
    /* update clamps too: -60 - 50 = -110 -> -100 */
    find_w(s, g)->friendliness = -60;
    CHECK(panopticon_update_watcher(s, (uint32_t)g, ACTION_MITM, "m") == 0);
    CHECK(panopticon_get_friendliness(s, (uint32_t)g) == -100);
    CHECK(panopticon_get_threat(s, (uint32_t)g) == THREAT_CRITICAL);
    /* remove with a zero count keeps the count at zero */
    s->watcher_count = 0;
    CHECK(panopticon_remove_watcher(s, (uint32_t)a) == 0);
    CHECK(s->watcher_count == 0);
    free(s);
}

static void test_classify_names(void) {
    CHECK(panopticon_classify(IP(10, 0, 0, 1), 80, ACTION_OBSERVE) == WATCHER_LOCAL_USER);
    CHECK(panopticon_classify(IP(127, 0, 0, 1), 22, ACTION_MITM) == WATCHER_LOCAL_USER);
    CHECK(panopticon_classify(IP(9, 0, 0, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(11, 0, 0, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(126, 0, 0, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(128, 0, 0, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(192, 168, 1, 1), 22, ACTION_MITM) == WATCHER_NETWORK);
    CHECK(panopticon_classify(IP(192, 167, 1, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(192, 169, 1, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(193, 168, 1, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(191, 168, 1, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(172, 16, 0, 1), 80, ACTION_OBSERVE) == WATCHER_NETWORK);
    CHECK(panopticon_classify(IP(172, 31, 0, 1), 22, ACTION_OBSERVE) == WATCHER_NETWORK);
    CHECK(panopticon_classify(IP(172, 15, 0, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(172, 32, 0, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(171, 16, 0, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(173, 16, 0, 1), 80, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 22, ACTION_OBSERVE) == WATCHER_EXTERNAL);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 23, ACTION_MITM) == WATCHER_EXTERNAL);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 21, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 24, ACTION_OBSERVE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 80, ACTION_SCAN) == WATCHER_EXTERNAL);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 80, ACTION_FINGERPRINT) == WATCHER_EXTERNAL);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 80, ACTION_INJECT) == WATCHER_GOVERNMENT);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 80, ACTION_MITM) == WATCHER_GOVERNMENT);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 80, ACTION_PROBE) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 80, ACTION_TRACK) == WATCHER_UNKNOWN);
    CHECK(panopticon_classify(IP(8, 8, 8, 8), 80, ACTION_CONNECT) == WATCHER_UNKNOWN);

    CHECK(strcmp(panopticon_watcher_type_name(WATCHER_UNKNOWN), "Unknown") == 0);
    CHECK(strcmp(panopticon_watcher_type_name(WATCHER_LOCAL_USER), "Local User") == 0);
    CHECK(strcmp(panopticon_watcher_type_name(WATCHER_P2P_PEER), "P2P Peer") == 0);
    CHECK(strcmp(panopticon_watcher_type_name(WATCHER_NETWORK), "Network Node") == 0);
    CHECK(strcmp(panopticon_watcher_type_name(WATCHER_SYSTEM), "System Service") == 0);
    CHECK(strcmp(panopticon_watcher_type_name(WATCHER_EXTERNAL), "External Entity") == 0);
    CHECK(strcmp(panopticon_watcher_type_name(WATCHER_GOVERNMENT), "Government Actor") == 0);
    CHECK(strcmp(panopticon_watcher_type_name(WATCHER_CORPORATE), "Corporate Actor") == 0);
    CHECK(strcmp(panopticon_watcher_type_name(WATCHER_ADMIRALTY), "Admiralty/Sovereign") == 0);
    CHECK(strcmp(panopticon_watcher_type_name((panopticon_watcher_type_t)9), "Unknown") == 0);

    CHECK(strcmp(panopticon_threat_name(THREAT_NONE), "None") == 0);
    CHECK(strcmp(panopticon_threat_name(THREAT_LOW), "Low") == 0);
    CHECK(strcmp(panopticon_threat_name(THREAT_MEDIUM), "Medium") == 0);
    CHECK(strcmp(panopticon_threat_name(THREAT_HIGH), "High") == 0);
    CHECK(strcmp(panopticon_threat_name(THREAT_CRITICAL), "CRITICAL") == 0);
    CHECK(strcmp(panopticon_threat_name((panopticon_threat_t)5), "Unknown") == 0);

    CHECK(strcmp(panopticon_action_name(ACTION_OBSERVE), "Observing") == 0);
    CHECK(strcmp(panopticon_action_name(ACTION_PROBE), "Probing") == 0);
    CHECK(strcmp(panopticon_action_name(ACTION_SCAN), "Scanning") == 0);
    CHECK(strcmp(panopticon_action_name(ACTION_CONNECT), "Connecting") == 0);
    CHECK(strcmp(panopticon_action_name(ACTION_DATA_REQUEST), "Requesting Data") == 0);
    CHECK(strcmp(panopticon_action_name(ACTION_FINGERPRINT), "Fingerprinting") == 0);
    CHECK(strcmp(panopticon_action_name(ACTION_TRACK), "Tracking") == 0);
    CHECK(strcmp(panopticon_action_name(ACTION_INJECT), "Injecting") == 0);
    CHECK(strcmp(panopticon_action_name(ACTION_MITM), "MITM Attack") == 0);
    CHECK(strcmp(panopticon_action_name((panopticon_action_t)9), "Unknown") == 0);

    static const struct { int s; const char *l; } lab[] = {
        {127, "Trusted Ally"}, {75, "Trusted Ally"},  {74, "Friendly"},     {50, "Friendly"},
        {49, "Cooperative"},   {25, "Cooperative"},   {24, "Neutral"},      {0, "Neutral"},
        {-1, "Suspicious"},    {-25, "Suspicious"},   {-26, "Hostile"},     {-50, "Hostile"},
        {-51, "Adversarial"},  {-75, "Adversarial"},  {-76, "Hostile Threat"},
        {-128, "Hostile Threat"},
    };
    for (unsigned i = 0; i < sizeof(lab) / sizeof(lab[0]); i++)
        CHECK(strcmp(panopticon_friendliness_label((int8_t)lab[i].s), lab[i].l) == 0);
}

static void test_events(void) {
    CHECK(panopticon_log_event(NULL, 1, ACTION_OBSERVE, "x", 0) == -1);
    panopticon_event_t out[300];
    CHECK(panopticon_get_events(NULL, out, 10, 0) == 0);
    panopticon_state_t *s = new_state();
    CHECK(panopticon_get_events(s, NULL, 10, 0) == 0);
    CHECK(panopticon_get_events(s, out, 10, 0) == 0);

    s->phase_tick = 5;
    CHECK(panopticon_log_event(s, 7, ACTION_TRACK, "trk", -20) == 0);
    s->phase_tick = 6;
    CHECK(panopticon_log_event(s, 8, ACTION_SCAN, NULL, 3) == 0);
    CHECK(s->event_count == 2 && s->event_head == 2);
    CHECK(s->events[0].timestamp == 5 && s->events[0].watcher_id == 7);
    CHECK(s->events[0].action == ACTION_TRACK && s->events[0].friendliness_delta == -20);
    CHECK(strcmp((char *)s->events[0].detail, "trk") == 0);
    CHECK(s->events[1].detail[0] == 0 && s->events[1].friendliness_delta == 3);

    CHECK(panopticon_get_events(s, out, 10, 0) == 2);
    CHECK(out[0].watcher_id == 7 && out[1].watcher_id == 8);
    CHECK(panopticon_get_events(s, out, 10, 6) == 1);
    CHECK(out[0].watcher_id == 8);
    CHECK(panopticon_get_events(s, out, 10, 5) == 2);
    CHECK(panopticon_get_events(s, out, 10, 7) == 0);
    CHECK(panopticon_get_events(s, out, 1, 0) == 1);
    CHECK(out[0].watcher_id == 7);
    CHECK(panopticon_get_events(s, out, 0, 0) == 0);

    /* fill to exactly 256: head wraps to 0, count stays 256 */
    for (uint32_t i = 2; i < PANOPTICON_MAX_EVENTS; i++) {
        s->phase_tick = 100 + i;
        panopticon_log_event(s, 1000 + i, ACTION_OBSERVE, "e", 0);
    }
    CHECK(s->event_count == 256);
    CHECK(s->event_head == 0);
    CHECK(panopticon_get_events(s, out, 300, 0) == 256);
    CHECK(out[0].watcher_id == 7 && out[255].watcher_id == 1255);
    /* one more overwrites the oldest; reading starts at the head */
    s->phase_tick = 999;
    panopticon_log_event(s, 4242, ACTION_MITM, "new", -50);
    CHECK(s->event_count == 256);
    CHECK(s->event_head == 1);
    CHECK(s->events[0].watcher_id == 4242);
    CHECK(panopticon_get_events(s, out, 300, 0) == 256);
    CHECK(out[0].watcher_id == 8);
    CHECK(out[1].watcher_id == 1002);
    CHECK(out[254].watcher_id == 1255);
    CHECK(out[255].watcher_id == 4242);
    CHECK(panopticon_get_events(s, out, 300, 999) == 1);
    CHECK(out[0].watcher_id == 4242);
    CHECK(panopticon_get_events(s, out, 300, 355) == 2);
    CHECK(out[0].watcher_id == 1255);
    free(s);
}

static void test_reports(void) {
    char buf[2048], exp[2048];
    panopticon_state_t *s = new_state();
    CHECK(panopticon_self_report(NULL, buf, sizeof(buf)) == -1);
    CHECK(panopticon_self_report(s, NULL, 10) == -1);
    CHECK(panopticon_set_self_description(NULL, "x") == -1);
    CHECK(panopticon_set_self_description(s, NULL) == -1);
    CHECK(panopticon_watcher_report(NULL, 1, buf, sizeof(buf)) == -1);
    CHECK(panopticon_watcher_report(s, 1, NULL, 10) == -1);

    s->phase_tick = 12345678901ull;
    panopticon_log_event(s, 1, ACTION_OBSERVE, "a", 0);
    panopticon_log_event(s, 1, ACTION_OBSERVE, "b", 0);
    panopticon_log_event(s, 1, ACTION_OBSERVE, "c", 0);
    panopticon_register_watcher(s, IP(1, 2, 3, 4), 5, WATCHER_UNKNOWN, "a");
    panopticon_register_watcher(s, IP(1, 2, 3, 5), 5, WATCHER_UNKNOWN, "b");
    next_id += 2;
    int n = panopticon_self_report(s, buf, sizeof(buf));
    int e = snprintf(exp, sizeof(exp),
                     "=== PANOPTICON STATUS REPORT ===\n"
                     "Phase Tick: 12345678901\n"
                     "Active: YES | Scanning: YES | Logging: YES | Reporting: YES\n"
                     "Total Watchers: 2 | Active Watchers: 2\n"
                     "Event Log: 3 entries\n"
                     "Description: %s\n",
                     SELF_DESC);
    CHECK(strcmp(buf, exp) == 0);
    CHECK(n == e);

    s->self_active = 0;
    s->self_scanning = 0;
    s->self_logging = 0;
    s->self_reporting = 0;
    CHECK(panopticon_set_self_description(s, "quiet") == 0);
    n = panopticon_self_report(s, buf, sizeof(buf));
    e = snprintf(exp, sizeof(exp),
                 "=== PANOPTICON STATUS REPORT ===\n"
                 "Phase Tick: 12345678901\n"
                 "Active: NO | Scanning: NO | Logging: NO | Reporting: NO\n"
                 "Total Watchers: 2 | Active Watchers: 2\n"
                 "Event Log: 3 entries\n"
                 "Description: quiet\n");
    CHECK(strcmp(buf, exp) == 0);
    CHECK(n == e);
    /* each flag independently */
    s->self_scanning = 1;
    panopticon_self_report(s, buf, sizeof(buf));
    CHECK(strstr(buf, "Active: NO | Scanning: YES | Logging: NO | Reporting: NO\n") != NULL);
    s->self_scanning = 0;
    s->self_logging = 1;
    panopticon_self_report(s, buf, sizeof(buf));
    CHECK(strstr(buf, "Active: NO | Scanning: NO | Logging: YES | Reporting: NO\n") != NULL);
    s->self_logging = 0;
    s->self_reporting = 1;
    panopticon_self_report(s, buf, sizeof(buf));
    CHECK(strstr(buf, "Active: NO | Scanning: NO | Logging: NO | Reporting: YES\n") != NULL);
    s->self_reporting = 0;
    s->self_active = 1;
    panopticon_self_report(s, buf, sizeof(buf));
    CHECK(strstr(buf, "Active: YES | Scanning: NO | Logging: NO | Reporting: NO\n") != NULL);
    /* small buffer: truncated, NUL-terminated, length counts what was written */
    char small[16];
    CHECK(panopticon_self_report(s, small, sizeof(small)) == 15);
    CHECK(strcmp(small, "=== PANOPTICON ") == 0);
    char big[400];
    memset(big, 'S', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    CHECK(panopticon_set_self_description(s, big) == 0);
    CHECK(strlen((char *)s->self_description) == 255);

    /* watcher report */
    s->phase_tick = 40;
    int id = panopticon_register_watcher(s, IP(203, 0, 113, 9), 8080, WATCHER_CORPORATE, "Acme");
    next_id++;
    s->phase_tick = 41;
    panopticon_update_watcher(s, (uint32_t)id, ACTION_TRACK, "following");
    n = panopticon_watcher_report(s, (uint32_t)id, buf, sizeof(buf));
    e = snprintf(exp, sizeof(exp),
                 "=== WATCHER REPORT #%d ===\n"
                 "Name: Acme\n"
                 "IP: 203.0.113.9:8080\n"
                 "Type: Corporate Actor\n"
                 "Friendliness: -60 (Adversarial)\n"
                 "Threat Level: High\n"
                 "Last Action: Tracking\n"
                 "Events: 1\n"
                 "First Seen: tick 40\n"
                 "Last Seen: tick 41\n"
                 "Currently Watching: YES\n"
                 "Description: following\n",
                 id);
    CHECK(strcmp(buf, exp) == 0);
    CHECK(n == e);
    find_w(s, id)->watching_you = 0;
    panopticon_watcher_report(s, (uint32_t)id, buf, sizeof(buf));
    CHECK(strstr(buf, "Currently Watching: NO\n") != NULL);
    /* the watcher in the last slot is found */
    s->watchers[127] = *find_w(s, id);
    s->watchers[127].id = 777777;
    panopticon_watcher_report(s, 777777, buf, sizeof(buf));
    CHECK(strncmp(buf, "=== WATCHER REPORT #777777 ===\n", 31) == 0);
    n = panopticon_watcher_report(s, 424242, buf, sizeof(buf));
    CHECK(strcmp(buf, "Watcher #424242 not found\n") == 0);
    /* the watcher in slot 0 is found too */
    panopticon_watcher_report(s, s->watchers[0].id, buf, sizeof(buf));
    CHECK(strstr(buf, "Name: a\nIP: 1.2.3.4:5\n") != NULL);
    CHECK(n == 26);
    s->watchers[127].active = 0;
    panopticon_watcher_report(s, 777777, buf, sizeof(buf));
    CHECK(strcmp(buf, "Watcher #777777 not found\n") == 0);
    free(s);
}

static void test_list_summary(void) {
    panopticon_watcher_t out[PANOPTICON_MAX_WATCHERS];
    CHECK(panopticon_list_watchers(NULL, out, 5) == 0);
    CHECK(panopticon_summary(NULL, NULL, NULL, NULL, NULL) == -1);
    panopticon_state_t *s = new_state();
    CHECK(panopticon_list_watchers(s, NULL, 5) == 0);
    CHECK(panopticon_list_watchers(s, out, 5) == 0);

    uint32_t total = 99, active = 99;
    int8_t avg = 99;
    panopticon_threat_t mt = THREAT_CRITICAL;
    CHECK(panopticon_summary(s, &total, &active, &avg, &mt) == 0);
    CHECK(total == 0 && active == 0 && avg == 0 && mt == THREAT_NONE);
    /* a single watcher: its own friendliness, its own threat */
    int one = panopticon_register_watcher(s, IP(1, 0, 0, 9), 1, WATCHER_ADMIRALTY, "one");
    next_id++;
    find_w(s, one)->threat = THREAT_LOW;
    CHECK(panopticon_get_threat(s, (uint32_t)one) == THREAT_LOW);
    CHECK(panopticon_summary(s, &total, &active, &avg, NULL) == 0);
    CHECK(total == 1 && active == 1 && avg == 60);
    CHECK(panopticon_summary(s, NULL, NULL, NULL, &mt) == 0);
    CHECK(mt == THREAT_LOW);
    CHECK(panopticon_remove_watcher(s, (uint32_t)one) == 0);
    CHECK(s->watcher_count == 0);

    int a = panopticon_register_watcher(s, IP(1, 0, 0, 1), 1, WATCHER_LOCAL_USER, "a"); /* 50 */
    int b = panopticon_register_watcher(s, IP(1, 0, 0, 2), 1, WATCHER_SYSTEM, "b");     /* 30 */
    int c = panopticon_register_watcher(s, IP(1, 0, 0, 3), 1, WATCHER_P2P_PEER, "c");   /* 20 */
    next_id += 3;
    CHECK(panopticon_summary(s, &total, &active, &avg, &mt) == 0);
    CHECK(total == 3 && active == 3);
    CHECK(avg == 33); /* 100 / 3 */
    CHECK(mt == THREAT_NONE);
    find_w(s, b)->threat = THREAT_MEDIUM;
    find_w(s, c)->threat = THREAT_LOW;
    CHECK(panopticon_summary(s, NULL, NULL, NULL, &mt) == 0);
    CHECK(mt == THREAT_MEDIUM);
    /* the last slot counts */
    s->watchers[127] = *find_w(s, a);
    s->watchers[127].id = 5555;
    s->watchers[127].friendliness = 100;
    s->watchers[127].threat = THREAT_HIGH;
    CHECK(panopticon_summary(s, &total, &active, &avg, &mt) == 0);
    CHECK(total == 3 && active == 4 && avg == 50 && mt == THREAT_HIGH);
    CHECK(panopticon_list_watchers(s, out, PANOPTICON_MAX_WATCHERS) == 4);
    CHECK(out[0].id == (uint32_t)a && out[1].id == (uint32_t)b && out[2].id == (uint32_t)c);
    CHECK(out[3].id == 5555);
    CHECK(panopticon_list_watchers(s, out, 2) == 2);
    CHECK(out[1].id == (uint32_t)b);
    CHECK(panopticon_list_watchers(s, out, 0) == 0);
    /* removed watchers are skipped */
    panopticon_remove_watcher(s, (uint32_t)b);
    CHECK(panopticon_list_watchers(s, out, PANOPTICON_MAX_WATCHERS) == 3);
    CHECK(out[1].id == (uint32_t)c);
    CHECK(panopticon_summary(s, &total, &active, &avg, &mt) == 0);
    CHECK(total == 2 && active == 3 && avg == 56 && mt == THREAT_HIGH); /* 170 / 3 */
    s->watchers[127].threat = THREAT_CRITICAL;
    CHECK(panopticon_summary(s, &total, NULL, NULL, &mt) == 0);
    CHECK(mt == THREAT_CRITICAL);
    free(s);
}

static void test_alerts(void) {
    char buf[2048], exp[512];
    CHECK(panopticon_check_alerts(NULL, buf, sizeof(buf)) == 0);
    panopticon_state_t *s = new_state();
    CHECK(panopticon_check_alerts(s, NULL, 10) == 0);
    CHECK(panopticon_check_alerts(s, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "No critical threats detected.\n") == 0);

    int h = panopticon_register_watcher(s, IP(7, 8, 9, 2), 1, WATCHER_CORPORATE, "hi");
    int m = panopticon_register_watcher(s, IP(7, 7, 7, 1), 1, WATCHER_UNKNOWN, "med");
    int c = panopticon_register_watcher(s, IP(11, 12, 13, 3), 1, WATCHER_GOVERNMENT, "crit");
    next_id += 3;
    find_w(s, m)->friendliness = -48;
    panopticon_update_watcher(s, (uint32_t)m, ACTION_OBSERVE, NULL); /* -49 medium */
    panopticon_update_watcher(s, (uint32_t)h, ACTION_OBSERVE, NULL); /* -41 medium */
    find_w(s, h)->friendliness = -49;
    panopticon_update_watcher(s, (uint32_t)h, ACTION_OBSERVE, NULL); /* -50 high */
    panopticon_update_watcher(s, (uint32_t)c, ACTION_INJECT, NULL);  /* -90 critical */
    CHECK(find_w(s, m)->threat == THREAT_MEDIUM);
    CHECK(find_w(s, h)->threat == THREAT_HIGH);
    CHECK(find_w(s, c)->threat == THREAT_CRITICAL);

    int n = panopticon_check_alerts(s, buf, sizeof(buf));
    CHECK(n == 2);
    char e1[256], e2[256];
    snprintf(e1, sizeof(e1),
             "[ALERT] hi (ID:%d) — Threat: High | Friendliness: -50 (Hostile) | "
             "Action: Observing | IP: 7.8.9.2\n",
             h);
    snprintf(e2, sizeof(e2),
             "[ALERT] crit (ID:%d) — Threat: CRITICAL | Friendliness: -90 (Hostile Threat) | "
             "Action: Injecting | IP: 11.12.13.3\n",
             c);
    snprintf(exp, sizeof(exp), "%s%s", e1, e2);
    CHECK(strcmp(buf, exp) == 0);
    CHECK(h == (int)s->watchers[0].id);
    /* an exact-size buffer: the second alert is cut, never overrun */
    size_t cap = strlen(e1) + 10;
    char *tight = malloc(cap);
    CHECK(panopticon_check_alerts(s, tight, (uint16_t)cap) == 2);
    CHECK(strncmp(tight, e1, strlen(e1)) == 0);
    CHECK(strncmp(tight + strlen(e1), e2, 9) == 0);
    CHECK(strlen(tight) == cap - 1);
    free(tight);

    /* not watching, or inactive: no alert */
    find_w(s, h)->watching_you = 0;
    CHECK(panopticon_check_alerts(s, buf, sizeof(buf)) == 1);
    CHECK(strcmp(buf, e2) == 0);
    panopticon_remove_watcher(s, (uint32_t)c);
    CHECK(panopticon_check_alerts(s, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "No critical threats detected.\n") == 0);
    /* an alert in the last slot */
    panopticon_watcher_t *w = &s->watchers[127];
    *w = *find_w(s, h);
    w->id = 31337;
    w->watching_you = 1;
    CHECK(panopticon_check_alerts(s, buf, sizeof(buf)) == 1);
    CHECK(strstr(buf, "(ID:31337)") != NULL);
    free(s);
}

static void test_auto_rate(void) {
    CHECK(panopticon_auto_rate(NULL, 1) == -1);
    panopticon_state_t *s = new_state();
    CHECK(panopticon_auto_rate(s, 999999) == -1);
    static const struct { panopticon_watcher_type_t t; int base; } types[] = {
        {WATCHER_UNKNOWN, 0},      {WATCHER_LOCAL_USER, 50},  {WATCHER_P2P_PEER, 20},
        {WATCHER_SYSTEM, 30},      {WATCHER_ADMIRALTY, 60},   {WATCHER_NETWORK, -10},
        {WATCHER_EXTERNAL, -20},   {WATCHER_CORPORATE, -40},  {WATCHER_GOVERNMENT, -50},
        {(panopticon_watcher_type_t)33, 0},
    };
    int id = panopticon_register_watcher(s, IP(6, 6, 6, 6), 6, WATCHER_UNKNOWN, "ar");
    next_id++;
    panopticon_watcher_t *w = find_w(s, id);
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        w->type = types[i].t;
        w->friendliness = 77;
        w->threat = THREAT_CRITICAL;
        w->last_action = ACTION_OBSERVE;
        w->event_count = 0;
        CHECK(panopticon_auto_rate(s, (uint32_t)id) == 0);
        CHECK(w->friendliness == types[i].base);
    }
    static const struct { panopticon_action_t a; int pen; } acts[] = {
        {ACTION_OBSERVE, 0},  {ACTION_PROBE, 0},        {ACTION_SCAN, -10},
        {ACTION_CONNECT, 0},  {ACTION_DATA_REQUEST, 0}, {ACTION_FINGERPRINT, -15},
        {ACTION_TRACK, -20},  {ACTION_INJECT, -40},     {ACTION_MITM, -50},
    };
    w->type = WATCHER_ADMIRALTY;
    for (unsigned i = 0; i < sizeof(acts) / sizeof(acts[0]); i++) {
        w->last_action = acts[i].a;
        w->event_count = 0;
        CHECK(panopticon_auto_rate(s, (uint32_t)id) == 0);
        CHECK(w->friendliness == 60 + acts[i].pen);
    }
    /* event-volume penalties at 50/51 and 100/101 */
    w->last_action = ACTION_OBSERVE;
    w->event_count = 50;
    panopticon_auto_rate(s, (uint32_t)id);
    CHECK(w->friendliness == 60);
    w->event_count = 51;
    panopticon_auto_rate(s, (uint32_t)id);
    CHECK(w->friendliness == 50);
    w->event_count = 100;
    panopticon_auto_rate(s, (uint32_t)id);
    CHECK(w->friendliness == 50);
    w->event_count = 101;
    panopticon_auto_rate(s, (uint32_t)id);
    CHECK(w->friendliness == 40);
    CHECK(w->threat == THREAT_NONE);
    /* clamp: -50 - 50 - 20 = -120 -> -100 */
    w->type = WATCHER_GOVERNMENT;
    w->last_action = ACTION_MITM;
    panopticon_auto_rate(s, (uint32_t)id);
    CHECK(w->friendliness == -100);
    CHECK(w->threat == THREAT_CRITICAL);
    /* threat thresholds from auto-rating */
    static const struct {
        panopticon_watcher_type_t t; panopticon_action_t a; uint32_t ev;
        int f; panopticon_threat_t th;
    } tt[] = {
        {WATCHER_UNKNOWN, ACTION_OBSERVE, 0, 0, THREAT_NONE},
        {WATCHER_NETWORK, ACTION_OBSERVE, 0, -10, THREAT_LOW},
        {WATCHER_UNKNOWN, ACTION_SCAN, 0, -10, THREAT_LOW},
        {WATCHER_LOCAL_USER, ACTION_MITM, 51, -10, THREAT_LOW},
        {WATCHER_NETWORK, ACTION_SCAN, 0, -20, THREAT_MEDIUM},
        {WATCHER_EXTERNAL, ACTION_OBSERVE, 0, -20, THREAT_MEDIUM},
        {WATCHER_CORPORATE, ACTION_OBSERVE, 0, -40, THREAT_MEDIUM},
        {WATCHER_GOVERNMENT, ACTION_OBSERVE, 0, -50, THREAT_HIGH},
        {WATCHER_EXTERNAL, ACTION_TRACK, 101, -60, THREAT_HIGH},
        {WATCHER_NETWORK, ACTION_MITM, 101, -80, THREAT_CRITICAL},
        {WATCHER_CORPORATE, ACTION_INJECT, 0, -80, THREAT_CRITICAL},
        {WATCHER_SYSTEM, ACTION_MITM, 0, -20, THREAT_MEDIUM},
        {WATCHER_P2P_PEER, ACTION_TRACK, 0, 0, THREAT_NONE},
        {WATCHER_GOVERNMENT, ACTION_SCAN, 51, -70, THREAT_HIGH},
        {WATCHER_CORPORATE, ACTION_SCAN, 0, -50, THREAT_HIGH},
        {WATCHER_NETWORK, ACTION_INJECT, 101, -70, THREAT_HIGH},
        {WATCHER_NETWORK, ACTION_INJECT, 51, -60, THREAT_HIGH},
        {WATCHER_EXTERNAL, ACTION_INJECT, 101, -80, THREAT_CRITICAL},
        {WATCHER_LOCAL_USER, ACTION_FINGERPRINT, 101, 15, THREAT_NONE},
        {WATCHER_UNKNOWN, ACTION_PROBE, 51, -10, THREAT_LOW},
    };
    for (unsigned i = 0; i < sizeof(tt) / sizeof(tt[0]); i++) {
        w->type = tt[i].t;
        w->last_action = tt[i].a;
        w->event_count = tt[i].ev;
        CHECK(panopticon_auto_rate(s, (uint32_t)id) == 0);
        CHECK(w->friendliness == tt[i].f);
        CHECK(w->threat == tt[i].th);
    }
    /* last slot reachable, removed watcher refused */
    s->watchers[127] = *w;
    s->watchers[127].id = 8888;
    s->watchers[127].type = WATCHER_SYSTEM;
    s->watchers[127].last_action = ACTION_OBSERVE;
    s->watchers[127].event_count = 0;
    CHECK(panopticon_auto_rate(s, 8888) == 0);
    CHECK(s->watchers[127].friendliness == 30);
    panopticon_remove_watcher(s, (uint32_t)id);
    CHECK(panopticon_auto_rate(s, (uint32_t)id) == -1);
    free(s);
}

int main(void) {
    test_bringup();
    test_init_tick();
    test_register();
    test_update_remove_rate();
    test_classify_names();
    test_events();
    test_reports();
    test_list_summary();
    test_alerts();
    test_auto_rate();
    if (failures) {
        printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}

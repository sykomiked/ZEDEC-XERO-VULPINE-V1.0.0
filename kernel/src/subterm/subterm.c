/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * subterm.c — the tree, the lockstep, the routing, the tutorials.
 *
 * No libc, no malloc, no float. Fixed arrays, integer ordinals, phase-tick
 * ordering. The portholes are real; the glyphs behind them are somebody
 * else's (font/graphics) department.
 */
#include "subterm.h"

/* --- tiny freestanding-safe string helpers (no libc dependency) --- */
static void st_zero(void *p, uint32_t n) {
    uint8_t *d = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static void st_strncpy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (cap == 0) return;
    if (src) {
        for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    }
    for (; i < cap; i++) dst[i] = 0; /* pad + always NUL-terminate */
}

/* --- validity --- */
static bool node_valid(const subterm_tree_t *t, int32_t id) {
    return t && id >= 0 && id < (int32_t)SUBTERM_MAX && t->node[id].used;
}

/* Allocate the next free node slot, or -1. Root is slot 0. */
static int32_t node_alloc(subterm_tree_t *t) {
    for (int32_t i = 0; i < (int32_t)SUBTERM_MAX; i++) {
        if (!t->node[i].used) return i;
    }
    return -1;
}

/* Count free node slots. */
static uint32_t node_free_count(const subterm_tree_t *t) {
    uint32_t f = 0;
    for (int32_t i = 0; i < (int32_t)SUBTERM_MAX; i++) {
        if (!t->node[i].used) f++;
    }
    return f;
}

static int32_t group_alloc(subterm_tree_t *t) {
    for (int32_t g = 0; g < (int32_t)SUBTERM_MAX_GROUPS; g++) {
        if (!t->group_used[g]) return g;
    }
    return -1;
}

/* ===== init ===== */
void subterm_init(subterm_tree_t *t) {
    if (!t) return;
    st_zero(t, (uint32_t)sizeof(*t));
    /* Root terminal — the one prompt behind all the portholes. */
    subterm_node_t *r = &t->node[SUBTERM_ROOT];
    r->used   = true;
    r->parent = SUBTERM_NO_GROUP; /* root has no parent (-1) */
    r->group  = SUBTERM_NO_GROUP;
    st_strncpy(r->title, "root", SUBTERM_TITLE_LEN);
    t->num_nodes   = 1;
    t->num_groups  = 0;
    t->tut         = 0;
    t->tut_pos     = 0;
    t->tut_active  = false;
    t->initialized = true;
}

/* ===== open a child ===== */
int32_t subterm_open(subterm_tree_t *t, int32_t parent, const char *title) {
    if (!node_valid(t, parent)) return -1;
    int32_t id = node_alloc(t);
    if (id < 0) return -1;
    subterm_node_t *n = &t->node[id];
    st_zero(n, (uint32_t)sizeof(*n));
    n->used   = true;
    n->parent = parent;
    n->group  = SUBTERM_NO_GROUP;
    st_strncpy(n->title, title, SUBTERM_TITLE_LEN);
    t->num_nodes++;
    return id;
}

/* ===== spawn a synchronized group ===== */
int32_t subterm_spawn_synced(subterm_tree_t *t, int32_t parent, uint32_t n) {
    if (!node_valid(t, parent)) return -1;
    if (n == 0) return -1;
    if (node_free_count(t) < n) return -1;   /* all-or-nothing */
    int32_t g = group_alloc(t);
    if (g < 0) return -1;

    t->group_used[g] = true;
    t->group_tick[g] = 0;                     /* shared clock starts at 0 */
    t->group_size[g] = 0;
    t->num_groups++;

    for (uint32_t k = 0; k < n; k++) {
        int32_t id = node_alloc(t);
        /* Guaranteed by the free-count check above, but stay honest. */
        if (id < 0) break;
        subterm_node_t *nd = &t->node[id];
        st_zero(nd, (uint32_t)sizeof(*nd));
        nd->used   = true;
        nd->parent = parent;
        nd->group  = g;                       /* shares the one clock     */
        st_strncpy(nd->title, "synced", SUBTERM_TITLE_LEN);
        t->num_nodes++;
        t->group_size[g]++;
    }
    return g;
}

/* ===== advance the phase-tick clocks (LOCKSTEP) ===== */
void subterm_tick(subterm_tree_t *t) {
    if (!t) return;
    /* One increment per group clock advances all its members together —
     * a single source of truth cannot desync. */
    for (int32_t g = 0; g < (int32_t)SUBTERM_MAX_GROUPS; g++) {
        if (t->group_used[g]) t->group_tick[g]++;
    }
}

uint64_t subterm_tick_of(const subterm_tree_t *t, int32_t id) {
    if (!node_valid(t, id)) return 0;
    const subterm_node_t *n = &t->node[id];
    if (n->group != SUBTERM_NO_GROUP) return t->group_tick[n->group];
    return n->tick;
}

/* ===== output routing (bounded ring) ===== */
int32_t subterm_write(subterm_tree_t *t, int32_t id,
                      const uint8_t *data, uint32_t len) {
    if (!node_valid(t, id)) return -1;
    if (!data || len == 0) return 0;
    subterm_node_t *n = &t->node[id];
    for (uint32_t i = 0; i < len; i++) {
        uint32_t tail = (n->ring_head + n->ring_len) % SUBTERM_RING;
        n->ring[tail] = data[i];
        if (n->ring_len < SUBTERM_RING) {
            n->ring_len++;
        } else {
            /* full: drop the oldest byte to make room (true ring) */
            n->ring_head = (n->ring_head + 1) % SUBTERM_RING;
        }
    }
    return (int32_t)len;
}

int32_t subterm_read(const subterm_tree_t *t, int32_t id,
                     uint8_t *out, uint32_t cap) {
    if (!node_valid(t, id)) return -1;
    if (!out || cap == 0) return 0;
    const subterm_node_t *n = &t->node[id];
    uint32_t take = n->ring_len < cap ? n->ring_len : cap;
    for (uint32_t i = 0; i < take; i++) {
        out[i] = n->ring[(n->ring_head + i) % SUBTERM_RING];
    }
    return (int32_t)take;
}

/* ===== tree introspection ===== */
int32_t subterm_parent_of(const subterm_tree_t *t, int32_t id) {
    if (!node_valid(t, id)) return -1;
    return t->node[id].parent;
}

uint32_t subterm_group_count(const subterm_tree_t *t, int32_t group) {
    if (!t || group < 0 || group >= (int32_t)SUBTERM_MAX_GROUPS) return 0;
    if (!t->group_used[group]) return 0;
    return t->group_size[group];
}

int32_t subterm_group_member(const subterm_tree_t *t, int32_t group, uint32_t k) {
    if (!t || group < 0 || group >= (int32_t)SUBTERM_MAX_GROUPS) return -1;
    if (!t->group_used[group]) return -1;
    uint32_t seen = 0;
    for (int32_t i = 0; i < (int32_t)SUBTERM_MAX; i++) {
        if (t->node[i].used && t->node[i].group == group) {
            if (seen == k) return i;
            seen++;
        }
    }
    return -1;
}

/* ===== tutorials (scripted, deterministic) ===== */
const char *subterm_tutorial_begin(subterm_tree_t *t,
                                   const subterm_lesson_t *lesson) {
    if (!t || !lesson || lesson->n_steps == 0) {
        if (t) t->tut_active = false;
        return 0;
    }
    t->tut        = lesson;
    t->tut_pos    = 0;
    t->tut_active = true;
    return lesson->steps[0];
}

const char *subterm_tutorial_step(subterm_tree_t *t) {
    if (!t || !t->tut_active || !t->tut) return 0;
    t->tut_pos++;
    if (t->tut_pos >= t->tut->n_steps) {
        t->tut_active = false;          /* lesson ends, deterministically */
        return 0;
    }
    return t->tut->steps[t->tut_pos];
}

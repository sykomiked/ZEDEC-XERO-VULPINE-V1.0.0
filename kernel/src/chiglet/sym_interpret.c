/* sym_interpret.c — the Symbolic Interpretation Engine (see header).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no RNG, no allocation. Every output is a
 * pure function of (telemetry, tick, phase).
 */
#include "sym_interpret.h"

/* --gc-sections KEEP — see mirror_ui.h: no boot caller yet, retain the API. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#    define SYM_KEEP __attribute__((used, retain))
#else
#    define SYM_KEEP __attribute__((used))
#endif

/* ---- helpers -------------------------------------------------------------- */

static uint32_t digit_sum(uint64_t x)
{
    uint32_t s = 0u;
    while (x) {
        s += (uint32_t) (x % 10u);
        x /= 10u;
    }
    return s;
}

static bool is_master(uint32_t v)
{
    return v == 11u || v == 22u || v == 33u;
}

/* ---- reduction ------------------------------------------------------------- */

SYM_KEEP sym_archetype_t sym_reduce(uint64_t x, sym_master_t *master)
{
    uint32_t v;

    if (master) *master = SYM_MASTER_NONE;
    if (x == 0u) return (sym_archetype_t) 0;

    v = digit_sum(x);
    /* preserve a master wherever it appears in the reduction chain */
    if (is_master(v)) {
        if (master) *master = (sym_master_t) v;
        /* the master's own archetype is its digit sum reduced: 11->2,
         * 22->4, 33->6 — the base-9 value the master "stands on". */
        v = digit_sum(v);
    } else {
        while (v > 9u) {
            v = digit_sum(v);
            if (is_master(v)) {
                if (master) *master = (sym_master_t) v;
                v = digit_sum(v);
                break;
            }
        }
    }
    if (v == 0u) v = 9u; /* 9 ≡ 0 mod 9: the completion archetype */
    return (sym_archetype_t) v;
}

SYM_KEEP const char *sym_archetype_name(sym_archetype_t a)
{
    static const char *const n[10] = {
        "none", "init", "dual", "intent", "mem", "phase", "heur", "purge", "lattice", "converge",
    };
    if (a < 1u || a > 9u) return "none";
    return n[a];
}

SYM_KEEP const char *sym_master_name(sym_master_t m)
{
    switch (m) {
    case SYM_MASTER_11:
        return "triadic-resonance";
    case SYM_MASTER_22:
        return "milestone-complete";
    case SYM_MASTER_33:
        return "harmonic-balance";
    default:
        return "none";
    }
}

/* ---- soul/expression split -------------------------------------------------- */

static bool is_vowel(char c)
{
    switch (c) {
    case 'a':
    case 'e':
    case 'i':
    case 'o':
    case 'u':
        return true;
    default:
        return false;
    }
}

SYM_KEEP void sym_split(const char *s, sym_split_t *out)
{
    uint32_t i = 0u;
    if (!out) return;
    out->vowels = 0u;
    out->consonants = 0u;
    out->separators = 0u;
    out->valence = LPRES_STATE_NEITHER;
    if (!s) return;

    while (s[i]) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') {
            if (is_vowel(c))
                out->vowels++;
            else
                out->consonants++;
        } else {
            out->separators++;
        }
        i++;
    }

    /* the pair (has-intent, has-boundary) is the kernel's told-true /
     * told-false encoding: bit0 = intent, bit1 = boundary. (0,0)=NEITHER,
     * (1,0)=TRUE, (0,1)=FALSE, (1,1)=BOTH. This is lpres's own bit shape,
     * not a re-derived table. */
    out->valence =
        (lpres_state_t) ((out->vowels > 0u ? 1u : 0u) | (out->consonants > 0u ? 2u : 0u));
}

/* ---- recursive cycle stacking ------------------------------------------------ */

SYM_KEEP void sym_context_init(sym_context_t *c)
{
    uint32_t i;
    if (!c) return;
    for (i = 0; i < SYM_CYCLE_LEN; i++) c->period[i] = 0u;
    c->period_head = 0u;
    c->period_count = 0u;
    c->cycles = 0u;
    for (i = 0; i < SYM_AWARE_DEPTH; i++) c->aware[i] = 0u;
}

SYM_KEEP void sym_period_push(sym_context_t *c, sym_archetype_t a)
{
    if (!c) return;
    c->period[c->period_head] = (uint32_t) a;
    c->period_head = (c->period_head + 1u) % SYM_CYCLE_LEN;
    if (c->period_count < SYM_CYCLE_LEN) c->period_count++;
    if (c->period_count == SYM_CYCLE_LEN && c->period_head == 0u)
        c->cycles++; /* a full nine-period macro-cycle completed           */
}

/* a deterministic fold of a value and a tick — no RNG, no float */
static uint64_t fold(uint64_t v, uint64_t tick)
{
    uint64_t x = v * 0x9E3779B97F4A7C15ull + tick;
    x ^= x >> 29;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 32;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 29;
    return x;
}

SYM_KEEP uint64_t sym_awareness_advance(sym_context_t *c, uint64_t tick)
{
    uint32_t k;
    if (!c) return 0u;
    /* level 0: the environment = the cycle count itself (what happened). */
    c->aware[SYM_AWARE_ENV] = fold((uint64_t) c->cycles, tick);
    /* each higher level is a deterministic fold of the level below: the
     * swarm is aware of the environment, of that awareness, and of THAT. */
    for (k = 1u; k < SYM_AWARE_DEPTH; k++) c->aware[k] = fold(c->aware[k - 1u], tick);
    return c->aware[SYM_AWARE_TWO];
}

/* ---- narrative formatting ---------------------------------------------------- */

SYM_KEEP uint32_t sym_narrate(const sym_narrative_t *n, char *buf, uint32_t cap)
{
    static const char *const line[10] = {
        "",
        "init",
        "dual-track sync",
        "user intent",
        "memory alloc",
        "phase transition",
        "heuristic opt",
        "fault purge",
        "lattice scaling",
        "network converge",
    };
    const char *arch, *pol, *harm;
    uint32_t w = 0u;

    if (!buf || cap == 0u) return 0u;
    buf[0] = 0;
    if (!n) return 0u;

    arch = (n->arch >= 1u && n->arch <= 9u) ? line[n->arch] : "none";
    pol = (n->polarity > 0) ? "+555" : (n->polarity < 0) ? "-555" : "0";
    harm = n->harmonic ? "locked" : "free";

    /* "arch [master] polarity harmonic" — bounded writes */
    {
        const char *parts[8];
        uint32_t np = 0u, i;
        parts[np++] = arch;
        if (n->master != SYM_MASTER_NONE) parts[np++] = sym_master_name(n->master);
        parts[np++] = pol;
        parts[np++] = harm;
        for (i = 0; i < np && w + 1u < cap; i++) {
            const char *p = parts[i];
            uint32_t len = 0u;
            while (p[len] && w + len + 1u < cap) {
                buf[w + len] = p[len];
                len++;
            }
            w += len;
            if (i + 1u < np && w + 1u < cap) {
                buf[w] = ' ';
                w++;
            }
        }
    }
    buf[w] = 0;
    return w;
}
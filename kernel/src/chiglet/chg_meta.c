/* chg_meta.c — metacognition and self-awareness for the Chiglet (see header).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no RNG, no allocation. The reflection is
 * a pure function of (chiglet state, tick); the recursion is bounded.
 */
#include "chg_meta.h"

/* --gc-sections KEEP — see mirror_ui.h: no boot caller yet, retain the API. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#    define META_KEEP __attribute__((used, retain))
#else
#    define META_KEEP __attribute__((used))
#endif

/* a deterministic fold — no RNG, no float */
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

META_KEEP uint64_t chg_meta_reflect(chg_meta_t *meta, const chiglet_t *c, uint64_t tick)
{
    if (!meta) return 0u;

    /* ---- level 0: the ENVIRONMENT ---------------------------------------- */
    /* what happened outside the swarm: the tick itself plus the model's
     * loaded-ness, folded. */
    meta->aware[CHG_META_ENV] = fold(c ? (uint64_t) (c->model.loaded ? 1u : 0u) : 0u, tick);

    /* ---- the live self-model --------------------------------------------- */
    if (c) {
        meta->inferences = c->inferences;
        meta->uncertain = c->uncertain_count;
        meta->decided =
            (c->inferences >= c->uncertain_count) ? c->inferences - c->uncertain_count : 0u;
        meta->caps = c->caps;
        meta->state = (uint32_t) (c->model.loaded ? CHG_DECIDED : CHG_UNAVAILABLE);
    } else {
        meta->inferences = 0u;
        meta->uncertain = 0u;
        meta->decided = 0u;
        meta->caps = 0u;
        meta->state = (uint32_t) CHG_UNAVAILABLE;
    }
    /* the self-model digest: the swarm's own measurable behaviour. */
    meta->self_model =
        fold(fold(fold(meta->inferences, meta->uncertain), (uint64_t) meta->caps), meta->decided);

    /* ---- levels 1..3: the recursion --------------------------------------- */
    /* level 1: awareness of the SELF = the self-model at this tick. */
    meta->aware[CHG_META_SELF] = fold(meta->self_model, tick);
    /* level 2: awareness of that awareness. */
    meta->aware[CHG_META_ONE] = fold(meta->aware[CHG_META_SELF], tick);
    /* level 3: awareness of the awareness of the awareness. */
    meta->aware[CHG_META_TWO] = fold(meta->aware[CHG_META_ONE], tick);

    meta->last_tick = tick;
    return meta->aware[CHG_META_TWO];
}

META_KEEP uint64_t chg_meta_self_model(const chg_meta_t *meta)
{
    return meta ? meta->self_model : 0u;
}

META_KEEP uint32_t chg_meta_narrate(const chg_meta_t *meta, char *buf, uint32_t cap)
{
    static const char *const state_name[3] = {
        "unavailable",
        "decided",
        "uncertain",
    };
    const char *st;
    uint32_t w = 0u;

    if (!buf || cap == 0u) return 0u;
    buf[0] = 0;
    if (!meta) return 0u;

    st = (meta->state < 3u) ? state_name[meta->state] : "?";
    {
        const char *p[8];
        uint32_t np = 0u, i;
        p[np++] = "aware env";
        p[np++] = "self";
        p[np++] = st;
        p[np++] = "meta";
        p[np++] = (meta->aware[CHG_META_TWO] != 0u) ? "live" : "idle";
        for (i = 0; i < np && w + 1u < cap; i++) {
            const char *s = p[i];
            uint32_t len = 0u;
            while (s[len] && w + len + 1u < cap) {
                buf[w + len] = s[len];
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
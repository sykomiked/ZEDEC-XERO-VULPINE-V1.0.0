/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxl_launch.c — app launch screens: themes, sound policy, and the sequence
 * driven by real loading. See zxl_launch.h. */
#include "zxl_launch.h"

/* ---- themes ------------------------------------------------------------- */
/* One default per group of the app list. Keys avoid E: that is the boot's. */
/* clang-format off */
static const zxl_theme_t GROUPS[ZXL_GROUPS] = {
    /* app   grp key     oct timbre               reveal               ambient        */
    { 0,  1, ZXL_D,  4, ZXL_TIMBRE_MARIMBA, ZXL_REVEAL_VAULT,     ZXL_AMB_ARCHES }, /* System core            */
    { 0,  2, ZXL_A,  4, ZXL_TIMBRE_CHIP,    ZXL_REVEAL_VAULT, ZXL_AMB_SPARKS }, /* Terminal and monitors  */
    { 0,  3, ZXL_FS, 5, ZXL_TIMBRE_PLUCK,   ZXL_REVEAL_VAULT,   ZXL_AMB_RINGS  }, /* Network, mesh, Web4    */
    { 0,  4, ZXL_BB, 5, ZXL_TIMBRE_GLASS,   ZXL_REVEAL_VAULT,   ZXL_AMB_DUST   }, /* Writing and office     */
    { 0,  5, ZXL_C,  5, ZXL_TIMBRE_STRINGS, ZXL_REVEAL_VAULT,     ZXL_AMB_WATER  }, /* Media and creative     */
    { 0,  6, ZXL_B,  5, ZXL_TIMBRE_FLUTE,   ZXL_REVEAL_VAULT,  ZXL_AMB_FLOCK  }, /* AI and the swarm       */
    { 0,  7, ZXL_G,  5, ZXL_TIMBRE_COIN,    ZXL_REVEAL_VAULT,      ZXL_AMB_DUST   }, /* Money and the economy  */
    { 0,  8, ZXL_F,  5, ZXL_TIMBRE_PLUCK,   ZXL_REVEAL_VAULT,     ZXL_AMB_RINGS  }, /* Places, spaces, life   */
    { 0,  9, ZXL_EB, 4, ZXL_TIMBRE_MARIMBA, ZXL_REVEAL_VAULT,   ZXL_AMB_SPARKS }, /* Learning, play, family */
    { 0, 10, ZXL_AB, 5, ZXL_TIMBRE_GLASS,   ZXL_REVEAL_VAULT, ZXL_AMB_DUST   }, /* Desk utilities         */
    { 0, 11, ZXL_DB, 4, ZXL_TIMBRE_CHIP,    ZXL_REVEAL_VAULT, ZXL_AMB_SPARKS }, /* Developer tools        */
    { 0, 12, ZXL_F,  5, ZXL_TIMBRE_GLASS,   ZXL_REVEAL_VAULT,     ZXL_AMB_RINGS  }, /* Access, language, safety */
};

/* Apps with their own theme (the six mocked up for the owner's approval). */
static const zxl_theme_t CURATED[] = {
    { "Burrow",      1, ZXL_D, 4, ZXL_TIMBRE_MARIMBA, ZXL_REVEAL_VAULT,     ZXL_AMB_ARCHES },
    { "Flint",       2, ZXL_A, 4, ZXL_TIMBRE_CHIP,    ZXL_REVEAL_VAULT, ZXL_AMB_SPARKS },
    { "Commons",     3, ZXL_F, 5, ZXL_TIMBRE_PLUCK,   ZXL_REVEAL_VAULT,   ZXL_AMB_RINGS  },
    { "Cutwater",    5, ZXL_C, 5, ZXL_TIMBRE_STRINGS, ZXL_REVEAL_VAULT,     ZXL_AMB_WATER  },
    { "Murmuration", 6, ZXL_E, 5, ZXL_TIMBRE_FLUTE,   ZXL_REVEAL_VAULT,  ZXL_AMB_FLOCK  },
    { "Coffer",      7, ZXL_G, 5, ZXL_TIMBRE_COIN,    ZXL_REVEAL_VAULT,      ZXL_AMB_DUST   },
};
/* clang-format on */
#define N_CURATED (sizeof CURATED / sizeof CURATED[0])

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

const zxl_theme_t *zxl_group_theme(uint32_t group)
{
    return (group >= 1 && group <= ZXL_GROUPS) ? &GROUPS[group - 1] : 0;
}

const zxl_theme_t *zxl_theme_for(const char *app, uint32_t group)
{
    if (app)
        for (uint32_t i = 0; i < N_CURATED; i++)
            if (streq(app, CURATED[i].app)) return &CURATED[i];
    const zxl_theme_t *g = zxl_group_theme(group);
    return g ? g : &GROUPS[0];
}

uint32_t zxl_curated_count(void)
{
    return (uint32_t) N_CURATED;
}

const zxl_theme_t *zxl_curated(uint32_t i)
{
    return i < N_CURATED ? &CURATED[i] : 0;
}

/* Major pentatonic, two octaves' worth: 0 2 4 7 9 12 14. */
static const uint8_t PENTA[ZXL_TOP_STEP + 1] = {0, 2, 4, 7, 9, 12, 14};

uint32_t zxl_tick_note(const zxl_theme_t *t, uint32_t step)
{
    if (!t) return 0;
    if (step > ZXL_TOP_STEP) step = ZXL_TOP_STEP;
    /* MIDI: C4 = 60, so octave o starts at 12 * (o + 1). */
    return 12u * ((uint32_t) t->octave + 1u) + t->key + PENTA[step];
}

/* ---- preferences -------------------------------------------------------- */
zxl_prefs_t zxl_prefs_default(void)
{
    zxl_prefs_t p = {1, 0, 0, 0, 0, 256};
    return p;
}

zxl_reveal_t zxl_effective_reveal(const zxl_theme_t *t, const zxl_prefs_t *p)
{
    if (p && p->reduce_motion) return ZXL_REVEAL_FADE;
    if (!t || t->reveal >= ZXL_REVEAL_COUNT) return ZXL_REVEAL_VAULT;
    return (zxl_reveal_t) t->reveal;
}

/* ---- one launch --------------------------------------------------------- */
static void push(zxl_launch_t *l, zxl_event_kind_t k, uint32_t part, uint32_t step)
{
    if (l->qn >= ZXL_QUEUE) return; /* cannot happen: a launch makes <= 10 */
    zxl_event_t *e = &l->q[(l->qh + l->qn) % ZXL_QUEUE];
    const zxl_prefs_t *p = &l->prefs;
    uint32_t on = p->sounds && !p->system_muted && !p->focus_quiet && p->volume_q8 > 0;
    uint32_t vol = p->volume_q8 > 256 ? 256 : p->volume_q8;
    e->kind = (uint8_t) k;
    e->part = (uint8_t) part;
    e->step = (uint8_t) step;
    e->note = (uint16_t) (k == ZXL_EV_TICK ? zxl_tick_note(l->theme, step) : 0);
    if (k == ZXL_EV_LIGHT) on = 0;                    /* the mark lights silently */
    if (k == ZXL_EV_TICK && p->screen_reader) on = 0; /* never chatter over speech */
    e->audible = (uint8_t) (on ? 1 : 0);
    e->gain_q8 = (uint16_t) (on ? (p->screen_reader ? vol * ZXL_DUCK_Q8 / 256 : vol) : 0);
    l->qn++;
}

uint32_t zxl_gather_p(const zxl_launch_t *l)
{
    if (!l || !l->n) return 0;
    uint32_t s = 0;
    for (uint32_t i = 0; i < l->n; i++) s += l->part_p[i];
    return s / l->n;
}

/* Rank of a part counted in from the nearer edge; the innermost rank is
 * (n-1)/2 (the centre part, or the inner pair when n is even). */
static uint32_t rank(uint32_t i, uint32_t n)
{
    uint32_t r = n - 1 - i;
    return i < r ? i : r;
}

static void advance(zxl_launch_t *l)
{
    if (l->phase == ZXL_PH_GATHER) {
        uint32_t inner = (l->n - 1) / 2, g = zxl_gather_p(l);
        for (uint32_t i = 0; i < l->n; i++) {
            if (l->lit & (1u << i) || l->part_p[i] < ZXL_FULL) continue;
            uint32_t r = rank(i, l->n);
            /* the head reaches rank r at the middle of its span; the innermost
             * rank lights only when everything is loaded */
            uint32_t need = r == inner ? ZXL_FULL : ZXL_FULL * (2 * r + 1) / (2 * (inner + 1));
            if (g < need) continue;
            l->lit |= 1u << i;
            push(l, ZXL_EV_TICK, i, r == inner ? ZXL_TOP_STEP : l->nlit);
            if (r != inner) l->nlit++;
        }
        if (l->lit == (1u << l->n) - 1u) {
            push(l, ZXL_EV_LIGHT, 0, 0);
            l->phase = ZXL_PH_RELEASE;
        }
    }
    if (l->phase == ZXL_PH_RELEASE && l->rest == ZXL_FULL) {
        push(l, ZXL_EV_END, 0, 0);
        l->phase = ZXL_PH_OPEN;
    }
}

int zxl_launch_init(zxl_launch_t *l, const zxl_theme_t *t, uint32_t n, const zxl_prefs_t *prefs,
                    int warm)
{
    if (!l || !t) return -1;
    uint8_t *b = (uint8_t *) l;
    for (uint32_t i = 0; i < sizeof *l; i++) b[i] = 0;
    l->theme = t;
    l->prefs = prefs ? *prefs : zxl_prefs_default();
    l->n = n < 1 ? 1 : n > ZXL_MAX_PARTS ? ZXL_MAX_PARTS : n;
    l->warm = (uint8_t) (warm ? 1 : 0);
    if (warm) {
        for (uint32_t i = 0; i < l->n; i++) l->part_p[i] = ZXL_FULL;
        l->lit = (1u << l->n) - 1u;
        l->rest = ZXL_FULL;
        push(l, ZXL_EV_END, 0, 0);
        l->phase = ZXL_PH_OPEN;
        return 0;
    }
    push(l, ZXL_EV_BEGIN, 0, 0);
    return 0;
}

int zxl_launch_part(zxl_launch_t *l, uint32_t part, uint32_t p)
{
    if (!l || part >= l->n) return -1;
    if (p > ZXL_FULL) p = ZXL_FULL;
    if (p > l->part_p[part]) l->part_p[part] = p;
    advance(l);
    return 0;
}

void zxl_launch_rest(zxl_launch_t *l, uint32_t p)
{
    if (!l) return;
    if (p != ZXL_UNKNOWN && p > ZXL_FULL) p = ZXL_FULL;
    if (p == ZXL_UNKNOWN || l->rest == ZXL_UNKNOWN || p > l->rest) l->rest = p;
    advance(l);
}

int zxl_launch_next(zxl_launch_t *l, zxl_event_t *ev)
{
    if (!l || !ev || !l->qn) return 0;
    *ev = l->q[l->qh];
    l->qh = (l->qh + 1) % ZXL_QUEUE;
    l->qn--;
    return 1;
}

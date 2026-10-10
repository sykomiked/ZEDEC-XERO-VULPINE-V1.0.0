/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* games.c — the deterministic sea.
 *
 * The engine is a pure integer state machine. The ONLY source of "chance" is
 * a seeded LCG, so every peer that starts from the same seed and replays the
 * same input stream recomputes a bit-identical world. The companion is the
 * real Chiglet (src/chiglet) reading game features, never a die roll. Display
 * and network transport live above this file — they are ops boundaries.
 */

#include "games.h"

/* ---- tiny freestanding memory helpers (no libc on target) ---- */
static void g_memzero(void *dst, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

/* ---- the deterministic engine of chance: Knuth MMIX LCG ----
 * A pure integer recurrence. Same rng in => same rng out, forever. We draw
 * from the HIGH bits, which mix best in a multiplicative LCG. */
static uint32_t lcg_next(game_t *g) {
    g->rng = g->rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(g->rng >> 33);
}

/* ---- entity helpers (bounded — never write past GAME_MAX_ENTITIES) ---- */
static uint32_t ent_alloc(game_t *g) {
    /* reuse a dead slot first, then grow — always within bounds */
    for (uint32_t i = 0; i < g->count; i++)
        if (!g->ents[i].alive) return i;
    if (g->count < GAME_MAX_ENTITIES) return g->count++;
    return GAME_MAX_ENTITIES;   /* full: caller must check */
}

static uint32_t food_count(const game_t *g) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < g->count; i++)
        if (g->ents[i].alive && g->ents[i].kind == GAME_ENT_FOOD) n++;
    return n;
}

/* Place one food at an LCG-chosen cell that is not already the player, the
 * companion, or an existing food. Bounded scan; if we can't find a free cell
 * in a fixed number of tries we simply skip (no infinite loops, no OOB). */
static void spawn_food(game_t *g) {
    if (food_count(g) >= GAME_MAX_FOOD) return;
    for (uint32_t tries = 0; tries < 8u; tries++) {
        int16_t fx = (int16_t)(lcg_next(g) % g->width);
        int16_t fy = (int16_t)(lcg_next(g) % g->height);
        bool occupied = false;
        for (uint32_t i = 0; i < g->count; i++) {
            if (!g->ents[i].alive) continue;
            if (g->ents[i].x == fx && g->ents[i].y == fy) { occupied = true; break; }
        }
        if (occupied) continue;
        uint32_t slot = ent_alloc(g);
        if (slot >= GAME_MAX_ENTITIES) return;   /* array full: drop, bounded */
        g->ents[slot].kind  = GAME_ENT_FOOD;
        g->ents[slot].alive = 1;
        g->ents[slot].x = fx;
        g->ents[slot].y = fy;
        return;
    }
}

void game_init(game_t *g, uint64_t seed) {
    g_memzero(g, (uint32_t)sizeof(*g));
    g->seed = seed;
    /* Decorrelate the LCG start so seed 0 is not a degenerate stream. */
    g->rng    = seed ^ 0x9E3779B97F4A7C15ULL;
    g->tick   = 0;
    g->score  = 0;
    g->width  = (uint16_t)GAME_GRID_W;
    g->height = (uint16_t)GAME_GRID_H;

    /* the captain and the Chiglet start amidships, one cell apart */
    g->ents[0].kind = GAME_ENT_PLAYER;    g->ents[0].alive = 1;
    g->ents[0].x = (int16_t)(GAME_GRID_W / 2); g->ents[0].y = (int16_t)(GAME_GRID_H / 2);
    g->ents[1].kind = GAME_ENT_COMPANION; g->ents[1].alive = 1;
    g->ents[1].x = (int16_t)(GAME_GRID_W / 2 - 1); g->ents[1].y = (int16_t)(GAME_GRID_H / 2);
    g->player    = 0;
    g->companion = 1;
    g->count     = 2;

    for (uint32_t i = 0; i < GAME_INIT_FOOD; i++) spawn_food(g);

    g->chg = 0;
    g->companion_available = 0;
    g->last_companion_action = GAME_MOVE_STAY;
}

void game_bind_companion(game_t *g, chiglet_t *chg) {
    g->chg = chg;
}

/* apply a move to a coordinate pair, clamped inside the grid */
static void apply_move(const game_t *g, int16_t *x, int16_t *y, uint32_t mv) {
    switch (mv) {
        case GAME_MOVE_UP:    if (*y > 0)                       (*y)--; break;
        case GAME_MOVE_DOWN:  if (*y < (int16_t)(g->height - 1)) (*y)++; break;
        case GAME_MOVE_LEFT:  if (*x > 0)                       (*x)--; break;
        case GAME_MOVE_RIGHT: if (*x < (int16_t)(g->width - 1))  (*x)++; break;
        case GAME_MOVE_STAY:
        default: break;
    }
}

/* nearest food to (cx,cy) by Manhattan distance; returns false if none.
 * Ties broken by lowest entity index so the choice is deterministic. */
static bool nearest_food(const game_t *g, int16_t cx, int16_t cy,
                         int16_t *fx, int16_t *fy) {
    bool found = false;
    int32_t best = 0;
    for (uint32_t i = 0; i < g->count; i++) {
        if (!g->ents[i].alive || g->ents[i].kind != GAME_ENT_FOOD) continue;
        int32_t dx = g->ents[i].x - cx; if (dx < 0) dx = -dx;
        int32_t dy = g->ents[i].y - cy; if (dy < 0) dy = -dy;
        int32_t d = dx + dy;
        if (!found || d < best) { best = d; *fx = g->ents[i].x; *fy = g->ents[i].y; found = true; }
    }
    return found;
}

/* Prototype dimensions, shared by the model and the evidence builder:
 *   dim0 = UP, dim1 = DOWN, dim2 = LEFT, dim3 = RIGHT.
 *   dim5 = an independent "food is present" axis. It is NOT a prototype dim,
 *          so it does not bias the label scores, but it keeps the evidence
 *          set to >= 2 DISTINCT directions so the ISF gate can DECIDE. */
#define PDIM_UP    0u
#define PDIM_DOWN  1u
#define PDIM_LEFT  2u
#define PDIM_RIGHT 3u
#define PDIM_CONF  5u

void game_companion_model(chg_model_t *m) {
    g_memzero(m, (uint32_t)sizeof(*m));
    m->K = 3; m->D = CHG_DIM; m->L = 4; m->epoch = 1;
    /* four directional prototypes, one per basis axis */
    m->proto[0][PDIM_UP]    = SR_FROM_INT(1);   /* label 0 -> UP    */
    m->proto[1][PDIM_DOWN]  = SR_FROM_INT(1);   /* label 1 -> DOWN  */
    m->proto[2][PDIM_LEFT]  = SR_FROM_INT(1);   /* label 2 -> LEFT  */
    m->proto[3][PDIM_RIGHT] = SR_FROM_INT(1);   /* label 3 -> RIGHT */
    m->R_min      = SR_FROM_FLOAT(1.5);         /* need 2 distinct directions */
    m->margin_min = SR_FROM_FLOAT(0.05);        /* a clear top-1 vs top-2 gap */
    m->loaded     = true;
}

/* Build the evidence the Chiglet reads this tick, from the game features.
 * Returns k (number of evidence vectors). Deterministic in the features. */
static uint32_t build_evidence(const game_t *g, surplus_real_t ev[][CHG_DIM],
                               uint32_t *k_out) {
    for (uint32_t i = 0; i < CHG_MAX_EXPERTS; i++)
        for (uint32_t d = 0; d < CHG_DIM; d++) ev[i][d] = SR_ZERO;

    const game_entity_t *comp = &g->ents[g->companion];
    int16_t fx, fy;
    if (!nearest_food(g, comp->x, comp->y, &fx, &fy)) { *k_out = 0; return 0; }

    int32_t dx = fx - comp->x;
    int32_t dy = fy - comp->y;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;

    /* choose primary (stronger) and secondary axis so the top-1/top-2 margin
     * reflects which way the food really lies */
    uint32_t prim_dim, sec_dim; int32_t prim_w, sec_w;
    if (adx >= ady) {
        prim_dim = (dx > 0) ? PDIM_RIGHT : PDIM_LEFT; prim_w = adx;
        sec_dim  = (dy > 0) ? PDIM_DOWN  : PDIM_UP;   sec_w  = ady;
    } else {
        prim_dim = (dy > 0) ? PDIM_DOWN  : PDIM_UP;   prim_w = ady;
        sec_dim  = (dx > 0) ? PDIM_RIGHT : PDIM_LEFT; sec_w  = adx;
    }

    uint32_t k = 0;
    if (prim_w > 0) { ev[k][prim_dim] = SR_FROM_INT(prim_w * 2); k++; } /* strong */
    if (sec_w  > 0) { ev[k][sec_dim]  = SR_FROM_INT(sec_w);      k++; } /* weaker */
    /* independent confidence axis: keeps >= 2 DISTINCT directions so a lone
     * on-axis food (secondary == 0) can still be DECIDED, not stuck UNCERTAIN */
    ev[k][PDIM_CONF] = SR_FROM_INT(1); k++;

    *k_out = k;
    return k;
}

game_move_t game_companion_decide(const game_t *g, uint8_t *available) {
    if (available) *available = 0;
    if (!g->chg || !g->chg->model.loaded) return GAME_MOVE_STAY;  /* typed N/A */

    surplus_real_t ev[CHG_MAX_EXPERTS][CHG_DIM];
    uint32_t k = 0;
    build_evidence(g, ev, &k);
    if (k == 0) {                 /* no food in sight: hold, honestly */
        if (available) *available = 1;
        return GAME_MOVE_STAY;
    }

    chg_result_t r;
    chg_status_t st = chg_infer(g->chg, ev, k, &r);
    if (st != CHG_OK) return GAME_MOVE_STAY;   /* runtime refused: not available */
    if (available) *available = 1;

    if (r.state != CHG_DECIDED) return GAME_MOVE_STAY; /* uncertain -> hold */
    /* label -> move: proto index 0..3 == UP/DOWN/LEFT/RIGHT == move 1..4 */
    return (game_move_t)(r.label + 1u);
}

/* resolve a forager landing on food: score and remove; a replacement spawns */
static void collect_at(game_t *g, int16_t x, int16_t y) {
    for (uint32_t i = 0; i < g->count; i++) {
        if (g->ents[i].alive && g->ents[i].kind == GAME_ENT_FOOD &&
            g->ents[i].x == x && g->ents[i].y == y) {
            g->ents[i].alive = 0;
            g->ents[i].kind  = GAME_ENT_EMPTY;
            g->score++;
            spawn_food(g);
        }
    }
}

void game_step(game_t *g, uint32_t input) {
    g->tick++;

    /* 1) the companion decides BEFORE anyone moves, reading the current sea */
    uint8_t avail = 0;
    game_move_t cmove = game_companion_decide(g, &avail);
    g->companion_available   = avail;
    g->last_companion_action = (uint8_t)cmove;

    /* 2) the player moves per input (low 3 bits) */
    uint32_t pmove = input & 0x7u;
    apply_move(g, &g->ents[g->player].x, &g->ents[g->player].y, pmove);

    /* 3) the companion moves */
    apply_move(g, &g->ents[g->companion].x, &g->ents[g->companion].y, cmove);

    /* 4) both captains forage the same sea */
    collect_at(g, g->ents[g->player].x,    g->ents[g->player].y);
    collect_at(g, g->ents[g->companion].x, g->ents[g->companion].y);

    /* 5) a deterministic bloom of food, keyed to the TICK (never a clock) */
    if ((g->tick % GAME_SPAWN_PERIOD) == 0u) spawn_food(g);
}

/* ---- state hash: FNV-1a over the full simulation state ----
 * Field-by-field so struct padding never leaks into the digest and the hash
 * is identical across compilers and word sizes. */
static uint32_t fnv1a_u32(uint32_t h, uint32_t v) {
    for (int b = 0; b < 4; b++) {
        h ^= (v & 0xffu);
        h *= 16777619u;
        v >>= 8;
    }
    return h;
}

uint32_t game_state_hash(const game_t *g) {
    uint32_t h = 2166136261u;                 /* FNV offset basis */
    h = fnv1a_u32(h, g->tick);
    h = fnv1a_u32(h, (uint32_t)g->score);
    h = fnv1a_u32(h, (uint32_t)(g->rng & 0xffffffffu));
    h = fnv1a_u32(h, (uint32_t)(g->rng >> 32));
    h = fnv1a_u32(h, g->count);
    for (uint32_t i = 0; i < g->count; i++) {
        h = fnv1a_u32(h, g->ents[i].kind);
        h = fnv1a_u32(h, g->ents[i].alive);
        h = fnv1a_u32(h, (uint32_t)(uint16_t)g->ents[i].x);
        h = fnv1a_u32(h, (uint32_t)(uint16_t)g->ents[i].y);
    }
    return h;
}

/* ---- P2P: content-address a ruleset ----
 * Serialize the def into a CANONICAL byte layout (fixed order, little-endian)
 * so the CID depends on the rules, not on struct padding, then hash it through
 * the kernel's IPFS content-address primitive (SHA-256). */
static void put_u16(uint8_t *b, uint32_t *o, uint16_t v) {
    b[(*o)++] = (uint8_t)(v & 0xffu);
    b[(*o)++] = (uint8_t)((v >> 8) & 0xffu);
}
static void put_u32(uint8_t *b, uint32_t *o, uint32_t v) {
    b[(*o)++] = (uint8_t)(v & 0xffu);
    b[(*o)++] = (uint8_t)((v >> 8) & 0xffu);
    b[(*o)++] = (uint8_t)((v >> 16) & 0xffu);
    b[(*o)++] = (uint8_t)((v >> 24) & 0xffu);
}

int32_t game_def_publish(const game_def_t *def, uint8_t out_cid[IPFS_CID_LEN]) {
    if (!def || !out_cid) return -1;
    uint8_t buf[GAME_NAME_LEN + 24];
    uint32_t o = 0;
    for (uint32_t i = 0; i < GAME_NAME_LEN; i++) buf[o++] = (uint8_t)def->name[i];
    put_u32(buf, &o, def->version);
    put_u16(buf, &o, def->width);
    put_u16(buf, &o, def->height);
    put_u16(buf, &o, def->init_food);
    put_u16(buf, &o, def->max_food);
    put_u16(buf, &o, def->spawn_period);
    put_u32(buf, &o, (uint32_t)def->food_reward);
    put_u32(buf, &o, def->rule_flags);
    /* out_cid = sha256(canonical bytes) via the IPFS layer — self-certifying */
    return ipfs_cid_from_bytes(buf, o, out_cid);
}

void game_def_default(game_def_t *def) {
    g_memzero(def, (uint32_t)sizeof(*def));
    const char nm[] = "zxv.forager.alongside";
    uint32_t i = 0;
    for (; nm[i] && i < GAME_NAME_LEN - 1u; i++) def->name[i] = nm[i];
    def->version      = 1;
    def->width        = (uint16_t)GAME_GRID_W;
    def->height       = (uint16_t)GAME_GRID_H;
    def->init_food    = (uint16_t)GAME_INIT_FOOD;
    def->max_food     = (uint16_t)GAME_MAX_FOOD;
    def->spawn_period = (uint16_t)GAME_SPAWN_PERIOD;
    def->food_reward  = 1;
    def->rule_flags   = 0;
}

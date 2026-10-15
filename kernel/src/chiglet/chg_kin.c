/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_kin.c — digital DNA, hobbies and visits. See chg_kin.h. */
#include "chg_kin.h"
#include "../../include/zxv_fixed.h"
#include "keccak.h"
#include "surplus.h"
#include "swarm_dna.h"

#define DNA_MAGIC   0x4e44585au /* "ZXDN" */
#define GRANT_MAGIC 0x4b47585au /* "ZXGK" */

static void kzero(void *p, uint32_t n)
{
    volatile uint8_t *d = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static void kcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static bool keq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < n; i++) x |= (uint8_t) (a[i] ^ b[i]);
    return x == 0;
}

static bool kzero_p(const uint8_t *a, uint32_t n)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < n; i++) x |= a[i];
    return x == 0;
}

static void put(uint8_t *o, uint32_t *at, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) o[(*at)++] = (uint8_t) (v >> (8 * i));
}

static uint64_t get(const uint8_t *in, uint32_t *at, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) v |= (uint64_t) in[(*at)++] << (8 * i);
    return v;
}

/* ISF u = 1 - (a.b)^2 / (|a|^2 |b|^2) in permille, signed integer inputs
 * with |x| <= 1000 and n <= 8 (so every product fits in 64 bits). */
static uint32_t isf_u(const int32_t *a, const int32_t *b, uint32_t n)
{
    int64_t dot = 0;
    uint64_t na = 0, nb = 0;
    for (uint32_t i = 0; i < n; i++) {
        dot += (int64_t) a[i] * b[i];
        na += (uint64_t) ((int64_t) a[i] * a[i]);
        nb += (uint64_t) ((int64_t) b[i] * b[i]);
    }
    if (!na || !nb) return 0;
    uint64_t d2 = (uint64_t) (dot < 0 ? -dot : dot);
    /* |dot| <= 8e6, so d2^2 < 2^46 and x1000 < 2^56; na*nb < 2^46 */
    uint64_t c = fx_udiv64(d2 * d2 * 1000u, na * nb, 0);
    return c >= 1000u ? 0 : 1000u - (uint32_t) c;
}

/* ===== K1 ===== */
void chgk_registry_init(chgk_registry_t *r)
{
    if (r) kzero(r, sizeof(*r));
}

static bool reg_has(const chgk_registry_t *r, const uint8_t id[32])
{
    for (uint32_t i = 0; i < r->n; i++)
        if (keq(r->id[i], id, 32)) return true;
    return false;
}

uint32_t chgk_dna_encode(const chgk_dna_t *d, uint8_t *out, uint32_t cap)
{
    if (!d || !out || cap < CHGK_DNA_ENC) return 0;
    uint32_t at = 0;
    put(out, &at, DNA_MAGIC, 4);
    kcpy(out + at, d->owner, 32), at += 32;
    kcpy(out + at, d->species, 32), at += 32;
    kcpy(out + at, d->lineage, 32), at += 32;
    kcpy(out + at, d->parent, 32), at += 32;
    put(out, &at, d->seed, 8);
    kcpy(out + at, d->genes, CHGK_GENES), at += CHGK_GENES;
    out[at++] = d->named;
    put(out, &at, d->generation, 4);
    return at;
}

/* The genes a name may lean; every other gene is the seed's. */
static bool name_leans(uint32_t gene)
{
    return gene == SWARM_GENE_TEMPERAMENT || gene == SWARM_GENE_WARMTH || gene == SWARM_GENE_PACE;
}

static bool genes_fit_seed(const uint8_t genes[CHGK_GENES], uint64_t seed, bool named)
{
    swarm_dna_t g = swarm_dna_from_seed(seed);
    for (uint32_t i = 0; i < CHGK_GENES; i++)
        if (genes[i] != g.gene[i] && !(named && name_leans(i))) return false;
    return true;
}

static void dna_hash(chgk_dna_t *d)
{
    uint8_t buf[CHGK_DNA_ENC];
    uint32_t n = chgk_dna_encode(d, buf, sizeof(buf));
    sha3_256(buf, n, d->id);
}

chgk_status_t chgk_dna_birth(chgk_registry_t *r, const uint8_t owner[32], const uint8_t species[32],
                             const uint8_t lineage[32], const chgk_dna_t *parent, uint64_t seed,
                             chgk_dna_t *out)
{
    if (!owner || !species || !lineage || !out) return CHGK_ERR_ARG;
    if (r && r->n >= CHGK_REG_MAX) return CHGK_ERR_FULL;
    chgk_dna_t d;
    kzero(&d, sizeof(d));
    kcpy(d.owner, owner, 32);
    kcpy(d.species, species, 32);
    kcpy(d.lineage, lineage, 32);
    if (parent) {
        kcpy(d.parent, parent->id, 32);
        d.generation = parent->generation + 1u;
    }
    for (uint32_t tries = 0;; tries++) {
        if (tries > 1000000u) return CHGK_ERR_FULL;
        d.seed = seed + tries;
        swarm_dna_t g = swarm_dna_from_seed(d.seed);
        kcpy(d.genes, g.gene, CHGK_GENES);
        dna_hash(&d);
        if (!r || !reg_has(r, d.id)) break;
    }
    if (r) kcpy(r->id[r->n++], d.id, 32);
    kcpy(out, &d, sizeof(d));
    return CHGK_OK;
}

chgk_status_t chgk_dna_birth_named(chgk_registry_t *r, const uint8_t owner[32],
                                   const uint8_t species[32], const uint8_t lineage[32],
                                   uint64_t name_seed, const uint8_t genes[CHGK_GENES],
                                   chgk_dna_t *out)
{
    if (!owner || !species || !lineage || !genes || !out) return CHGK_ERR_ARG;
    if (!genes_fit_seed(genes, name_seed, true)) return CHGK_ERR_ARG;
    if (r && r->n >= CHGK_REG_MAX) return CHGK_ERR_FULL;
    chgk_dna_t d;
    kzero(&d, sizeof(d));
    kcpy(d.owner, owner, 32);
    kcpy(d.species, species, 32);
    kcpy(d.lineage, lineage, 32);
    d.seed = name_seed;
    kcpy(d.genes, genes, CHGK_GENES);
    d.named = 1;
    dna_hash(&d);
    if (r && reg_has(r, d.id)) return CHGK_ERR_DUP; /* never re-seed a name */
    if (r) kcpy(r->id[r->n++], d.id, 32);
    kcpy(out, &d, sizeof(d));
    return CHGK_OK;
}

chgk_status_t chgk_dna_decode(const uint8_t *in, uint32_t len, chgk_dna_t *d)
{
    if (!in || !d) return CHGK_ERR_ARG;
    if (len != CHGK_DNA_ENC) return CHGK_ERR_PARSE;
    uint32_t at = 0;
    if ((uint32_t) get(in, &at, 4) != DNA_MAGIC) return CHGK_ERR_PARSE;
    chgk_dna_t t;
    kzero(&t, sizeof(t));
    kcpy(t.owner, in + at, 32), at += 32;
    kcpy(t.species, in + at, 32), at += 32;
    kcpy(t.lineage, in + at, 32), at += 32;
    kcpy(t.parent, in + at, 32), at += 32;
    t.seed = get(in, &at, 8);
    kcpy(t.genes, in + at, CHGK_GENES), at += CHGK_GENES;
    t.named = in[at++];
    t.generation = (uint32_t) get(in, &at, 4);
    if (t.named > 1) return CHGK_ERR_PARSE;
    /* the genes are not free: they are what the seed grows (a name leans
     * three of them), and a named chiglet is born to a person */
    if (!genes_fit_seed(t.genes, t.seed, t.named == 1)) return CHGK_ERR_PARSE;
    if (t.named && !kzero_p(t.parent, 32)) return CHGK_ERR_PARSE;
    if (kzero_p(t.parent, 32) != (t.generation == 0)) return CHGK_ERR_PARSE;
    sha3_256(in, len, t.id);
    kcpy(d, &t, sizeof(t));
    return CHGK_OK;
}

uint32_t chgk_dna_distance(const chgk_dna_t *a, const chgk_dna_t *b)
{
    if (!a || !b) return 0;
    int32_t x[CHGK_GENES], y[CHGK_GENES];
    for (uint32_t i = 0; i < CHGK_GENES; i++) {
        x[i] = (int32_t) a->genes[i] - 128;
        y[i] = (int32_t) b->genes[i] - 128;
    }
    return isf_u(x, y, CHGK_GENES);
}

/* ===== K2 ===== */
static const char *const HOBBIES[CHGK_HOBBY_CAT] = {
    "number patterns",
    "word origins",
    "maps",
    "old machines",
    "music theory",
    "weather",
    "puzzles",
    "tidal tables",
    "star charts",
    "bird calls",
    "knots",
    "ciphers",
    "tea",
    "clockwork",
    "folk tales",
    "chess endings",
    "origami",
    "bridges",
    "rivers",
    "lighthouses",
    "old alphabets",
    "proverbs",
    "bees",
    "mushrooms",
    "rock layers",
    "calendars",
    "sundials",
    "kites",
    "boats",
    "trains",
    "typefaces",
    "ink",
    "paper making",
    "bookbinding",
    "tiling",
    "symmetry",
    "prime numbers",
    "fractals",
    "sea shells",
    "ferns",
    "seeds",
    "orchards",
    "bread",
    "fermenting",
    "spices",
    "herbs",
    "honey",
    "salt",
    "weaving",
    "dyes",
    "pottery",
    "glass",
    "metal casting",
    "joinery",
    "bells",
    "drums",
    "string instruments",
    "wind instruments",
    "harmony",
    "rhythm",
    "poetry metre",
    "riddles",
    "palindromes",
    "anagrams",
    "place names",
    "surnames",
    "heraldry",
    "flags",
    "coins",
    "stamps",
    "postcards",
    "letters",
    "railway timetables",
    "canals",
    "windmills",
    "watermills",
    "dams",
    "aqueducts",
    "castles",
    "towers",
    "domes",
    "arches",
    "gardens",
    "mazes",
    "card games",
    "board games",
    "dice odds",
    "logic grids",
    "magic squares",
    "sudoku",
    "radio",
    "morse code",
    "semaphore",
    "signal lamps",
    "telegraphs",
    "old computers",
    "slide rules",
    "abacus",
    "counting boards",
    "tally sticks",
    "ledgers",
    "weights and measures",
    "clouds",
    "snowflakes",
    "rain",
    "wind",
    "tides",
    "moon phases",
    "eclipses",
    "comets",
    "meteors",
    "auroras",
    "rainbows",
    "volcanoes",
    "caves",
    "deserts",
    "forests",
    "meadows",
    "wetlands",
    "coral reefs",
    "foxes",
    "owls",
    "whales",
    "butterflies",
    "dragonflies",
    "beetles",
    "trees",
    "moss",
    "lichens",
    "wildflowers",
    "cacti",
    "bonsai",
    "travel journals",
    "sketching",
    "calligraphy",
    "map drawing",
    "model building",
    "dioramas",
    "cooking",
    "recipes",
    "markets",
    "craft fairs",
    "festivals",
    "story circles",
};

const char *chgk_hobby_name(uint32_t idx)
{
    return idx < CHGK_HOBBY_CAT ? HOBBIES[idx] : "?";
}

static bool reg_has_set(const chgk_registry_t *r, const uint8_t set[CHGK_HOBBY_MAX])
{
    for (uint32_t i = 0; i < r->n_sets; i++)
        if (keq(r->set[i], set, CHGK_HOBBY_MAX)) return true;
    return false;
}

static void pick_set(const chgk_dna_t *d, uint32_t salt, chgk_hobbies_t *h)
{
    uint8_t in[32 + 4 + 7], hash[32];
    kcpy(in, d->id, 32);
    in[32] = (uint8_t) salt, in[33] = (uint8_t) (salt >> 8), in[34] = (uint8_t) (salt >> 16),
    in[35] = (uint8_t) (salt >> 24);
    kcpy(in + 36, "hobbies", 7);
    sha3_256(in, sizeof(in), hash);
    kzero(h, sizeof(*h));
    uint32_t want = CHGK_HOBBY_MIN + hash[0] % (CHGK_HOBBY_MAX - CHGK_HOBBY_MIN + 1u);
    uint32_t pos = 1;
    while (h->n < want) {
        if (pos + 2 > 32) { /* out of bytes: stretch */
            sha3_256(hash, 32, hash);
            pos = 0;
        }
        uint32_t idx = (((uint32_t) hash[pos] << 8) | hash[pos + 1]) % CHGK_HOBBY_CAT;
        pos += 2;
        bool dup = false;
        for (uint32_t i = 0; i < h->n; i++) dup |= h->pick[i] == idx;
        if (!dup) h->pick[h->n++] = (uint8_t) idx;
    }
    for (uint32_t i = 1; i < h->n; i++) /* sort, insertion */
        for (uint32_t j = i; j > 0 && h->pick[j - 1] > h->pick[j]; j--) {
            uint8_t t = h->pick[j];
            h->pick[j] = h->pick[j - 1], h->pick[j - 1] = t;
        }
    for (uint32_t i = h->n; i < CHGK_HOBBY_MAX; i++) h->pick[i] = 0xff;
    sha3_256(hash, 32, hash);
    for (uint32_t i = 0; i < h->n; i++) {
        h->dna_w[i] = (uint16_t) (200u + (uint32_t) hash[i] * 800u / 255u);
        h->w[i] = h->dna_w[i];
    }
}

chgk_status_t chgk_hobbies_for(chgk_registry_t *r, const chgk_dna_t *d, chgk_hobbies_t *h)
{
    if (!d || !h) return CHGK_ERR_ARG;
    if (r && r->n_sets >= CHGK_REG_MAX) return CHGK_ERR_FULL;
    for (uint32_t salt = 0;; salt++) {
        if (salt > 100000u) return CHGK_ERR_FULL;
        pick_set(d, salt, h);
        if (!r || !reg_has_set(r, h->pick)) break;
    }
    if (r) kcpy(r->set[r->n_sets++], h->pick, CHGK_HOBBY_MAX);
    return CHGK_OK;
}

/* ===== K3 ===== */
uint32_t chgk_hobby_drift(const chgk_hobbies_t *h)
{
    if (!h || !h->n) return 0;
    int32_t a[CHGK_HOBBY_MAX], b[CHGK_HOBBY_MAX];
    for (uint32_t i = 0; i < h->n; i++) a[i] = h->w[i], b[i] = h->dna_w[i];
    return isf_u(a, b, h->n);
}

bool chgk_hobby_step(chgk_hobbies_t *h, const uint16_t interest[CHGK_HOBBY_MAX], bool task_ok)
{
    if (!h || !interest) return false;
    h->cycles++;
    if (!task_ok) return false; /* the task comes first: no growth this cycle */
    for (uint32_t i = 0; i < h->n; i++) {
        int32_t t = interest[i] > 1000 ? 1000 : interest[i];
        int32_t w = h->w[i], d = t - w;
        if (d > (int32_t) CHGK_HOBBY_STEP) d = (int32_t) CHGK_HOBBY_STEP;
        if (d < -(int32_t) CHGK_HOBBY_STEP) d = -(int32_t) CHGK_HOBBY_STEP;
        w += d;
        if (w < 1) w = 1;
        if (w > 1000) w = 1000;
        h->w[i] = (uint16_t) w;
    }
    if (chgk_hobby_drift(h) > CHGK_DRIFT_CAP) {
        for (uint32_t i = 0; i < h->n; i++) h->w[i] = h->dna_w[i];
        h->resets++;
        return true;
    }
    return false;
}

uint64_t chgk_hobby_budget(const chgk_hobbies_t *h, uint64_t growth_idle,
                           uint64_t out[CHGK_HOBBY_MAX])
{
    if (!out) return 0;
    for (uint32_t i = 0; i < CHGK_HOBBY_MAX; i++) out[i] = 0;
    if (!h || !h->n) return 0;
    uint64_t sum = 0, given = 0;
    for (uint32_t i = 0; i < h->n; i++) sum += h->w[i];
    uint64_t rem;
    uint64_t q = fx_udiv64(growth_idle, sum, &rem);
    for (uint32_t i = 0; i < h->n; i++) {
        out[i] = q * h->w[i] + fx_udiv64(rem * h->w[i], sum, 0);
        given += out[i];
    }
    return given;
}

int32_t chgk_hobby_tiebreak(const chgk_hobbies_t *h, const uint32_t *score, const uint8_t *tag,
                            uint32_t n)
{
    if (!score || !n) return -1;
    uint32_t best = 0;
    for (uint32_t i = 1; i < n; i++)
        if (score[i] > score[best]) best = i;
    if (!h || !tag) return (int32_t) best;
    int32_t pick = (int32_t) best;
    uint32_t pick_w = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (score[i] != score[best]) continue;
        uint32_t w = 0;
        for (uint32_t k = 0; k < h->n; k++)
            if (h->pick[k] == tag[i]) w = h->w[k];
        if (w > pick_w) pick = (int32_t) i, pick_w = w;
    }
    return pick;
}

/* ===== K4 ===== */
uint32_t chgk_grant_encode(const chgk_grant_t *g, uint8_t *out, uint32_t cap)
{
    if (!g || !out || cap < CHGK_GRANT_ENC) return 0;
    if (g->depth < CHGK_DEPTH_CLAIMS || g->depth > CHGK_DEPTH_VERDICTS) return 0;
    if (!g->topics || g->expires <= g->not_before || keq(g->visitor, g->host, 32)) return 0;
    uint32_t at = 0;
    put(out, &at, GRANT_MAGIC, 4);
    kcpy(out + at, g->gid, 16), at += 16;
    kcpy(out + at, g->visitor, 32), at += 32;
    kcpy(out + at, g->host, 32), at += 32;
    put(out, &at, g->topics, 4);
    out[at++] = g->depth;
    put(out, &at, g->not_before, 8);
    put(out, &at, g->expires, 8);
    return at;
}

chgk_status_t chgk_grant_decode(const uint8_t *in, uint32_t len, chgk_grant_t *g)
{
    if (!in || !g) return CHGK_ERR_ARG;
    if (len != CHGK_GRANT_ENC) return CHGK_ERR_PARSE;
    uint32_t at = 0;
    if ((uint32_t) get(in, &at, 4) != GRANT_MAGIC) return CHGK_ERR_PARSE;
    chgk_grant_t t;
    kzero(&t, sizeof(t));
    kcpy(t.gid, in + at, 16), at += 16;
    kcpy(t.visitor, in + at, 32), at += 32;
    kcpy(t.host, in + at, 32), at += 32;
    t.topics = (uint32_t) get(in, &at, 4);
    t.depth = in[at++];
    t.not_before = get(in, &at, 8);
    t.expires = get(in, &at, 8);
    uint8_t chk[CHGK_GRANT_ENC];
    if (chgk_grant_encode(&t, chk, sizeof(chk)) != len) return CHGK_ERR_PARSE;
    kcpy(g, &t, sizeof(t));
    return CHGK_OK;
}

chgk_status_t chgk_grant_verify(const uint8_t *body, uint32_t len, const uint8_t *pk_v,
                                const uint8_t *pk_h, uint32_t pk_len, const uint8_t *sig_v,
                                const uint8_t *sig_h, uint32_t sig_len, chgk_verify_fn vf,
                                void *vctx, chgk_grant_t *out)
{
    if (!body || !pk_v || !pk_h || !sig_v || !sig_h || !vf || !out) return CHGK_ERR_ARG;
    chgk_grant_t g;
    chgk_status_t st = chgk_grant_decode(body, len, &g);
    if (st != CHGK_OK) return st;
    if (!vf(pk_v, pk_len, body, len, sig_v, sig_len, vctx)) return CHGK_ERR_SIG;
    if (!vf(pk_h, pk_len, body, len, sig_h, sig_len, vctx)) return CHGK_ERR_SIG;
    kcpy(out, &g, sizeof(g));
    return CHGK_OK;
}

void chgk_grantbook_init(chgk_grantbook_t *b)
{
    if (b) kzero(b, sizeof(*b));
}

static bool book_has(const uint8_t list[][16], uint32_t n, const uint8_t gid[16])
{
    for (uint32_t i = 0; i < n; i++)
        if (keq(list[i], gid, 16)) return true;
    return false;
}

chgk_status_t chgk_revoke(chgk_grantbook_t *b, const uint8_t gid[16])
{
    if (!b || !gid) return CHGK_ERR_ARG;
    if (book_has(b->revoked, b->n_revoked, gid)) return CHGK_OK;
    if (b->n_revoked >= CHGK_SEEN_MAX) return CHGK_ERR_FULL;
    kcpy(b->revoked[b->n_revoked++], gid, 16);
    return CHGK_OK;
}

/* ===== K5 ===== */
void chgk_xp_init(chgk_xp_t *x, const chgk_dna_t *self)
{
    if (!x) return;
    kzero(x, sizeof(*x));
    if (self) kcpy(x->self, self->id, 32);
}

void chgk_log_step(uint8_t head[32], const uint8_t partner[32], const chgk_claim_t *c, bool taken)
{
    uint8_t in[32 + 32 + 3 + 32 + 32 + 1];
    uint32_t at = 0;
    kcpy(in + at, head, 32), at += 32;
    kcpy(in + at, partner, 32), at += 32;
    in[at++] = c->kind, in[at++] = c->topic, in[at++] = c->verdict;
    kcpy(in + at, c->claim, 32), at += 32;
    kcpy(in + at, c->evidence, 32), at += 32;
    in[at++] = taken ? 1 : 0;
    sha3_256(in, at, head);
}

chgk_status_t chgk_visit_open(chgk_visit_t *v, chgk_xp_t *x, chgk_grantbook_t *book,
                              const chgk_grant_t *g, const chgk_dna_t *self,
                              const chgk_dna_t *partner, uint64_t now)
{
    if (!v || !x || !book || !g || !self || !partner) return CHGK_ERR_ARG;
    kzero(v, sizeof(*v));
    bool fwd = keq(g->visitor, self->id, 32) && keq(g->host, partner->id, 32);
    bool rev = keq(g->host, self->id, 32) && keq(g->visitor, partner->id, 32);
    if (!(fwd || rev) || !keq(x->self, self->id, 32)) return CHGK_ERR_PARTY;
    if (book_has(book->revoked, book->n_revoked, g->gid)) return CHGK_ERR_REVOKED;
    if (book_has(book->seen, book->n_seen, g->gid)) return CHGK_ERR_REPLAY;
    if (now < g->not_before || now >= g->expires) return CHGK_ERR_EXPIRED;
    if (book->n_seen >= CHGK_SEEN_MAX) return CHGK_ERR_FULL;
    kcpy(book->seen[book->n_seen++], g->gid, 16);

    int32_t p = -1;
    for (uint32_t i = 0; i < x->n_partners; i++)
        if (keq(x->partner[i], partner->id, 32)) p = (int32_t) i;
    if (p < 0) {
        if (x->n_partners < CHGK_PARTNERS) {
            p = (int32_t) x->n_partners++;
        } else { /* forget the friend met least */
            p = 0;
            for (uint32_t i = 1; i < CHGK_PARTNERS; i++)
                if (x->meetings[i] < x->meetings[p]) p = (int32_t) i;
        }
        kcpy(x->partner[p], partner->id, 32);
        x->meetings[p] = 0;
    }
    x->meetings[p]++;
    v->open = true;
    kcpy(&v->g, g, sizeof(*g));
    kcpy(v->partner, partner->id, 32);
    v->partner_gap = chgk_dna_distance(self, partner);
    v->meeting = x->meetings[p];
    return CHGK_OK;
}

void chgk_visit_close(chgk_visit_t *v)
{
    if (v) v->open = false;
}

chgk_status_t chgk_visit_receive(chgk_visit_t *v, chgk_xp_t *x, const chgk_claim_t *c,
                                 bool checker_passed, uint64_t now, uint32_t *xp_gained)
{
    if (xp_gained) *xp_gained = 0;
    if (!v || !x || !c) return CHGK_ERR_ARG;
    if (!v->open) return CHGK_ERR_CLOSED;
    if (now >= v->g.expires) {
        v->open = false;
        return CHGK_ERR_EXPIRED;
    }
    if (c->topic >= CHGK_TOPICS || !(v->g.topics & (1u << c->topic))) return CHGK_ERR_SCOPE;
    if (c->kind == CHGK_K_CLAIM) {
        if (c->verdict) return CHGK_ERR_ARG;
    } else if (c->kind == CHGK_K_VERDICT) {
        if (v->g.depth < CHGK_DEPTH_VERDICTS) return CHGK_ERR_SCOPE;
        if (c->verdict > 3) return CHGK_ERR_ARG;
    } else {
        return CHGK_ERR_SCOPE; /* nothing but claims and verdicts may pass */
    }
    chgk_log_step(x->log_head, v->partner, c, checker_passed);
    x->log_len++;
    v->exchanged++;
    if (!checker_passed) {
        x->refused++;
        return CHGK_OK;
    }
    /* f(u) = ln(1 + (N-1) u), N = number of genes, in milli-nats */
    surplus_real_t u = sr_from_ratio_u64(v->partner_gap, 1000u);
    uint32_t f = SR_TO_PERMILLE(surplus_f(u, CHGK_GENES));
    uint32_t gain = (uint32_t) fx_udiv64(100u + f, v->meeting ? v->meeting : 1u, 0);
    if (!gain) gain = 1;
    x->xp[c->topic] = x->xp[c->topic] > 0xffffffffu - gain ? 0xffffffffu : x->xp[c->topic] + gain;
    x->social++;
    x->taken++;
    if (xp_gained) *xp_gained = gain;
    return CHGK_OK;
}

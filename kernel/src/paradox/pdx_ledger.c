/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pdx_ledger.c — the contradiction-tolerant ledger (P1.1-P1.4). See
 * pdx_ledger.h and docs/PARACONSISTENT_LEDGER.md.
 *
 * Claims are kept sorted by (epoch, from, nonce, id) and votes by (epoch,
 * kind, voter, claim, positive), so the stores are canonical whatever order
 * events arrived in. A slot's escrow lives at its first claim in that order
 * (its "head"); a claim joining a slot later always sorts after the head. */
#include "pdx_ledger.h"
#include "../robin_debanks/sha256.h"

/* ---- small helpers ---- */
static int cmp32(const uint8_t *a, const uint8_t *b)
{
    for (uint32_t i = 0; i < PLG_HASH; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

static void cp32(uint8_t *dst, const uint8_t *src)
{
    for (uint32_t i = 0; i < PLG_HASH; i++) dst[i] = src[i];
}

static int cmp_u64(uint64_t a, uint64_t b)
{
    return a < b ? -1 : a > b ? 1 : 0;
}

static void h_str(sha256_ctx_t *c, const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    sha256_update(c, (const uint8_t *) s, n);
}

static void h_u32(sha256_ctx_t *c, uint32_t v)
{
    uint8_t b[4];
    for (uint32_t i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
    sha256_update(c, b, 4);
}

static void h_u64(sha256_ctx_t *c, uint64_t v)
{
    uint8_t b[8];
    for (uint32_t i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8 * i));
    sha256_update(c, b, 8);
}

static bool add_ok(uint64_t *x, uint64_t v)
{
    if (*x > UINT64_MAX - v) return false;
    *x += v;
    return true;
}

static bool log_room(const pdx_log_t *l)
{
    return l->n < l->cap;
}

/* ---- canonical order ---- */
static int claim_cmp(const plg_claim_t *a, const plg_claim_t *b)
{
    int c = cmp_u64(a->epoch, b->epoch);
    if (!c) c = cmp_u64(a->from, b->from);
    if (!c) c = cmp_u64(a->nonce, b->nonce);
    if (!c) c = cmp32(a->id, b->id);
    return c;
}

static bool claim_same(const plg_claim_t *a, const plg_claim_t *b)
{
    return !cmp32(a->id, b->id) && a->epoch == b->epoch && a->from == b->from && a->to == b->to &&
           a->nonce == b->nonce && a->amount == b->amount && a->sig_seq == b->sig_seq;
}

static bool same_slot(const plg_claim_t *a, const plg_claim_t *b)
{
    return a->from == b->from && a->nonce == b->nonce;
}

static int vote_cmp(const plg_vote_t *a, const plg_vote_t *b)
{
    int c = cmp_u64(a->epoch, b->epoch);
    if (!c) c = cmp_u64(a->kind, b->kind);
    if (!c) c = cmp_u64(a->voter, b->voter);
    if (!c) c = cmp32(a->claim, b->claim);
    if (!c) c = cmp_u64(a->positive, b->positive);
    return c;
}

/* the slot's first claim in canonical order */
static uint32_t head_of(const plg_t *lg, uint32_t i)
{
    for (uint32_t j = 0; j < i; j++)
        if (same_slot(&lg->cl[j].c, &lg->cl[i].c)) return j;
    return i;
}

/* claims of the slot whose head is h, visible at epoch e */
static bool in_slot(const plg_t *lg, uint32_t h, uint32_t j, uint32_t e)
{
    return lg->cl[j].c.epoch <= e && same_slot(&lg->cl[j].c, &lg->cl[h].c);
}

int32_t plg_find(const plg_t *lg, const uint8_t id[PLG_HASH])
{
    if (!lg || !id) return -1;
    for (uint32_t i = 0; i < lg->n_cl; i++)
        if (!cmp32(lg->cl[i].c.id, id)) return (int32_t) i;
    return -1;
}

/* ---- names and tags ---- */
lpx_t plg_tag(uint8_t st)
{
    switch (st) {
    case PLG_CS_PENDING:
        return lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_EMERGING);
    case PLG_CS_SETTLED:
        return lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    case PLG_CS_LOST:
        return lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    case PLG_CS_HELD:
        return lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    case PLG_CS_FROZEN:
        return lpx_make(LPX_B_BOTH, LPX_P_ENTROPIC, LPX_PH_MANIFEST);
    case PLG_CS_INSUFFICIENT:
        return lpx_make(LPX_B_FALSE, LPX_P_FORBIDDEN, LPX_PH_MANIFEST);
    default:
        return lpx_make(LPX_B_NEITHER, LPX_P_FORBIDDEN, LPX_PH_POTENTIAL);
    }
}

const char *plg_state_name(uint8_t st)
{
    static const char *const n[6] = {"pending", "settled", "lost",
                                     "held",    "frozen",  "insufficient"};
    return st < 6u ? n[st] : "?";
}

const char *plg_res_name(plg_res_t r)
{
    static const char *const n[10] = {"added",           "added-both",   "duplicate",     "ok",
                                      "refused-invalid", "refused-late", "refused-funds", "full",
                                      "halted",          "forbidden"};
    return (unsigned) r < 10u ? n[r] : "?";
}

/* ---- conservation (L2) ---- */
bool plg_liabilities(const plg_t *lg, uint64_t *out)
{
    if (!lg || !out) return false;
    uint64_t l = 0, held = 0;
    for (uint32_t a = 0; a < lg->cfg.n_acct; a++)
        if (!add_ok(&l, lg->bal[a])) return false;
    for (uint32_t i = 0; i < lg->n_cl; i++)
        if (!add_ok(&held, lg->hold[i])) return false;
    if (held != lg->escrow || !add_ok(&l, held)) return false;
    *out = l;
    return true;
}

bool plg_conserved(const plg_t *lg)
{
    uint64_t l;
    if (!plg_liabilities(lg, &l)) return false;
    return l == lg->liab0 && l <= lg->cfg.assets && lg->cfg.assets - l == lg->cfg.equity;
}

/* ---- digest ---- */
static void state_digest(const plg_t *lg, uint8_t out[PLG_HASH])
{
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/plg-state/v1");
    h_u32(&c, lg->horizon);
    h_u32(&c, lg->cfg.n_acct);
    for (uint32_t a = 0; a < lg->cfg.n_acct; a++) h_u64(&c, lg->bal[a]);
    h_u64(&c, lg->escrow);
    h_u32(&c, lg->n_cl);
    for (uint32_t i = 0; i < lg->n_cl; i++) {
        sha256_update(&c, lg->cl[i].c.id, PLG_HASH);
        uint8_t st = lg->cl[i].st;
        sha256_update(&c, &st, 1);
    }
    h_u32(&c, lg->n_vt);
    for (uint32_t k = 0; k < lg->n_vt; k++) {
        const plg_vote_t *v = &lg->vt[k];
        sha256_update(&c, v->claim, PLG_HASH);
        h_u32(&c, v->epoch);
        h_u32(&c, v->voter);
        uint8_t f[2] = {v->kind, (uint8_t) v->positive};
        sha256_update(&c, f, 2);
    }
    sha256_final(&c, out);
}

/* ---- init ---- */
bool plg_init(plg_t *lg, const plg_cfg_t *cfg, plg_entry_t *claims, uint32_t claim_cap,
              plg_vote_t *votes, uint32_t vote_cap, uint64_t *bal, uint64_t *hold, pdx_log_t *log,
              lpx_qlog_t *q)
{
    if (!lg || !cfg || !claims || !votes || !bal || !hold || !log || !q || !cfg->n_acct ||
        !cfg->genesis || cfg->conflict_limit < 2u || cfg->conflict_limit > PLG_MAX_BRANCH)
        return false;
    uint64_t sum = 0;
    for (uint32_t a = 0; a < cfg->n_acct; a++)
        if (!add_ok(&sum, cfg->genesis[a])) return false;
    if (cfg->assets < cfg->equity || cfg->assets - cfg->equity != sum) return false;
    lg->cfg = *cfg;
    if (!lg->cfg.decisive) lg->cfg.decisive = 1u;
    lg->liab0 = sum;
    lg->cl = claims;
    lg->cl_cap = claim_cap;
    lg->n_cl = 0;
    lg->vt = votes;
    lg->vt_cap = vote_cap;
    lg->n_vt = 0;
    lg->bal = bal;
    lg->hold = hold;
    for (uint32_t a = 0; a < cfg->n_acct; a++) bal[a] = cfg->genesis[a];
    lg->escrow = 0;
    lg->horizon = 0;
    lg->halted = false;
    lg->log = log;
    lg->q = q;
    state_digest(lg, lg->digest);
    return true;
}

/* ---- the replay (L3) ---- */
typedef struct {
    plg_t *lg;
    uint32_t e;
    bool ok; /* false on an overflow: conservation has already failed */
} rp_t;

static void credit(rp_t *r, uint32_t a, uint64_t v)
{
    if (!add_ok(&r->lg->bal[a], v)) r->ok = false;
}

static void set_slot(rp_t *r, uint32_t h, int32_t w, uint8_t wst, uint8_t rest)
{
    for (uint32_t j = h; j < r->lg->n_cl; j++)
        if (in_slot(r->lg, h, j, r->e)) r->lg->cl[j].st = (int32_t) j == w ? wst : rest;
}

/* Can the slot pay claim w from its escrow plus the sender's balance? */
static bool affordable(const rp_t *r, uint32_t h, uint32_t w)
{
    const plg_t *lg = r->lg;
    uint64_t amt = lg->cl[w].c.amount, held = lg->hold[h];
    return amt <= held || amt - held <= lg->bal[lg->cl[h].c.from];
}

/* Pay w (affordable) and close the slot: the rest LOST. */
static void settle(rp_t *r, uint32_t h, uint32_t w)
{
    plg_t *lg = r->lg;
    uint32_t from = lg->cl[h].c.from;
    uint64_t amt = lg->cl[w].c.amount, held = lg->hold[h];
    if (amt <= held)
        credit(r, from, held - amt);
    else
        lg->bal[from] -= amt - held;
    credit(r, lg->cl[w].c.to, amt);
    lg->escrow -= held;
    lg->hold[h] = 0;
    set_slot(r, h, (int32_t) w, PLG_CS_SETTLED, PLG_CS_LOST);
}

/* w cannot be funded: refund the escrow, w INSUFFICIENT, the rest LOST. */
static void insufficient(rp_t *r, uint32_t h, uint32_t w)
{
    plg_t *lg = r->lg;
    credit(r, lg->cl[h].c.from, lg->hold[h]);
    lg->escrow -= lg->hold[h];
    lg->hold[h] = 0;
    set_slot(r, h, (int32_t) w, PLG_CS_INSUFFICIENT, PLG_CS_LOST);
}

/* The claim with the unique lowest signed sequence, or -1 (a tie). */
static int32_t first_sig(const rp_t *r, uint32_t h)
{
    int32_t best = -1;
    bool tie = false;
    for (uint32_t j = h; j < r->lg->n_cl; j++) {
        if (!in_slot(r->lg, h, j, r->e)) continue;
        uint64_t s = r->lg->cl[j].c.sig_seq;
        if (best < 0 || s < r->lg->cl[best].c.sig_seq) {
            best = (int32_t) j;
            tie = false;
        } else if (s == r->lg->cl[best].c.sig_seq) {
            tie = true;
        }
    }
    return tie ? -1 : best;
}

/* One slot in the claims step of epoch e, called for a slot with a pending
 * claim in this epoch. True when it processed a claim (money may have moved,
 * so another pass may fund more). */
static bool claim_step(rp_t *r, uint32_t h, uint32_t lo, bool last)
{
    plg_t *lg = r->lg;
    uint32_t cnt = 0;
    uint64_t top = 0;
    for (uint32_t j = h; j < lg->n_cl && lg->cl[j].c.epoch <= r->e; j++) {
        if (!in_slot(lg, h, j, r->e)) continue;
        cnt++;
        if (lg->cl[j].c.amount > top) top = lg->cl[j].c.amount;
    }
    uint8_t hs = h < lo ? lg->cl[h].st : PLG_CS_PENDING;
    if (h < lo && hs != PLG_CS_HELD && hs != PLG_CS_FROZEN) {
        /* the slot closed in an earlier epoch: a late double spend */
        for (uint32_t j = lo; j < lg->n_cl && lg->cl[j].c.epoch <= r->e; j++)
            if (in_slot(lg, h, j, r->e) && lg->cl[j].st == PLG_CS_PENDING)
                lg->cl[j].st = PLG_CS_LOST;
        return true;
    }
    uint32_t from = lg->cl[h].c.from;
    if (cnt == 1) {
        if (affordable(r, h, h)) {
            settle(r, h, h);
            return true;
        }
        if (!last) return false;
        insufficient(r, h, h);
        return true;
    }
    /* Both (L1) */
    bool frozen = hs == PLG_CS_FROZEN || cnt > lg->cfg.conflict_limit;
    if (!frozen && lg->cfg.first_sig_rule) {
        int32_t w = first_sig(r, h);
        if (w >= 0) {
            if (affordable(r, h, (uint32_t) w)) {
                settle(r, h, (uint32_t) w);
                return true;
            }
            if (!last) return false;
            insufficient(r, h, (uint32_t) w);
            return true;
        }
    }
    uint64_t need = top > lg->hold[h] ? top - lg->hold[h] : 0;
    if (need > lg->bal[from]) {
        if (!last) return false;
        need = lg->bal[from]; /* hold what there is */
    }
    lg->bal[from] -= need;
    lg->hold[h] += need;
    lg->escrow += need;
    set_slot(r, h, -1, 0, frozen ? PLG_CS_FROZEN : PLG_CS_HELD);
    return true;
}

/* The first (canonical order) positive ruling of `kind` by `voter` for a
 * claim of the slot, or -1. */
static int32_t ruling(const rp_t *r, uint32_t h, uint8_t kind, uint32_t voter)
{
    const plg_t *lg = r->lg;
    for (uint32_t k = 0; k < lg->n_vt && lg->vt[k].epoch <= r->e; k++) {
        const plg_vote_t *v = &lg->vt[k];
        if (v->kind != kind || v->voter != voter || !v->positive) continue;
        int32_t j = plg_find(lg, v->claim);
        if (j >= 0 && in_slot(lg, h, (uint32_t) j, r->e)) return j;
    }
    return -1;
}

/* Witness evidence through the paradox engine's neutral hold: each witness
 * counts once per claim (its first vote in canonical order). */
static int32_t witnesses(const rp_t *r, uint32_t h)
{
    const plg_t *lg = r->lg;
    pdx_branch_t br[PLG_MAX_BRANCH];
    uint32_t map[PLG_MAX_BRANCH];
    pdx_hold_t hd;
    pdx_hold_init(&hd, br, PLG_MAX_BRANCH, lg->cfg.decisive);
    for (uint32_t j = h; j < lg->n_cl; j++)
        if (in_slot(lg, h, j, r->e)) {
            if (hd.n >= PLG_MAX_BRANCH) return -1;
            map[hd.n] = j;
            pdx_hold_add(&hd, lg->cl[j].c.id);
        }
    for (uint32_t k = 0; k < lg->n_vt && lg->vt[k].epoch <= r->e; k++) {
        const plg_vote_t *v = &lg->vt[k];
        if (v->kind != PLG_V_WITNESS) continue; /* registered witnesses only (plg_vote) */
        int32_t j = plg_find(lg, v->claim);
        if (j < 0 || !in_slot(lg, h, (uint32_t) j, r->e)) continue;
        bool seen = false;
        for (uint32_t p = 0; p < k && !seen; p++)
            seen = lg->vt[p].kind == PLG_V_WITNESS && lg->vt[p].voter == v->voter &&
                   !cmp32(lg->vt[p].claim, v->claim);
        if (!seen) pdx_hold_evidence(&hd, v->claim, v->positive);
    }
    int32_t d = pdx_hold_decisive(&hd);
    return d >= 0 ? (int32_t) map[d] : -1;
}

/* The rulings step of epoch e: the dispute funnel (P1.2). */
static void rulings(rp_t *r)
{
    plg_t *lg = r->lg;
    for (uint32_t h = 0; h < lg->n_cl && lg->cl[h].c.epoch <= r->e; h++) {
        uint8_t st = lg->cl[h].st;
        if ((st != PLG_CS_HELD && st != PLG_CS_FROZEN) || head_of(lg, h) != h) continue;
        int32_t w = ruling(r, h, PLG_V_OWNER, lg->cfg.owner);
        if (w < 0 && st == PLG_CS_HELD) w = ruling(r, h, PLG_V_ARBITER, lg->cfg.arbiter);
        if (w < 0 && st == PLG_CS_HELD) w = witnesses(r, h);
        if (w < 0) continue;
        if (affordable(r, h, (uint32_t) w))
            settle(r, h, (uint32_t) w);
        else
            insufficient(r, h, (uint32_t) w);
    }
}

/* The smallest event epoch above cur and at or below the horizon. */
static bool next_epoch(const plg_t *lg, uint32_t cur, uint32_t *out)
{
    bool any = false;
    uint32_t best = 0;
    for (uint32_t i = 0; i < lg->n_cl; i++) {
        uint32_t e = lg->cl[i].c.epoch;
        if (e > cur && e <= lg->horizon && (!any || e < best)) {
            best = e;
            any = true;
        }
    }
    for (uint32_t k = 0; k < lg->n_vt; k++) {
        uint32_t e = lg->vt[k].epoch;
        if (e > cur && e <= lg->horizon && (!any || e < best)) {
            best = e;
            any = true;
        }
    }
    *out = best;
    return any;
}

static bool replay(plg_t *lg)
{
    for (uint32_t a = 0; a < lg->cfg.n_acct; a++) lg->bal[a] = lg->cfg.genesis[a];
    for (uint32_t i = 0; i < lg->n_cl; i++) {
        lg->cl[i].st = PLG_CS_PENDING;
        lg->hold[i] = 0;
    }
    lg->escrow = 0;
    rp_t r = {lg, 0, true};
    if (!plg_conserved(lg)) return false;
    while (next_epoch(lg, r.e, &r.e)) {
        uint32_t lo = 0, hi;
        while (lo < lg->n_cl && lg->cl[lo].c.epoch < r.e) lo++;
        hi = lo;
        while (hi < lg->n_cl && lg->cl[hi].c.epoch == r.e) hi++;
        /* claims: passes until nothing moves, then the last passes */
        for (int last = 0; last < 2; last++) {
            bool moved = true;
            while (moved) {
                moved = false;
                for (uint32_t i = lo; i < hi; i++)
                    if (lg->cl[i].st == PLG_CS_PENDING && claim_step(&r, head_of(lg, i), lo, last))
                        moved = true;
            }
        }
        rulings(&r);
        if (!r.ok || !plg_conserved(lg)) return false;
    }
    return r.ok;
}

/* ---- events ---- */
plg_res_t plg_claim(plg_t *lg, const plg_claim_t *c, bool sig_ok, bool local)
{
    if (!lg || !c) return PLG_REFUSED_INVALID;
    if (lg->halted) return PLG_HALTED;
    if (!sig_ok || c->from >= lg->cfg.n_acct || c->to >= lg->cfg.n_acct || c->from == c->to ||
        !c->amount || !c->epoch)
        return PLG_REFUSED_INVALID;
    int32_t f = plg_find(lg, c->id);
    if (f >= 0) return claim_same(&lg->cl[f].c, c) ? PLG_DUPLICATE : PLG_REFUSED_INVALID;
    if (c->epoch <= lg->horizon) return PLG_REFUSED_LATE;
    bool both = false;
    for (uint32_t j = 0; j < lg->n_cl; j++)
        if (same_slot(&lg->cl[j].c, c)) both = true;
    if (local && !both) {
        /* final balance less the largest claim of each other pending slot */
        uint64_t pend = 0;
        for (uint32_t j = 0; j < lg->n_cl; j++) {
            const plg_claim_t *o = &lg->cl[j].c;
            if (o->from != c->from || o->epoch <= lg->horizon || head_of(lg, j) != j) continue;
            uint64_t m = 0;
            for (uint32_t k = j; k < lg->n_cl; k++)
                if (same_slot(&lg->cl[k].c, o) && lg->cl[k].c.amount > m) m = lg->cl[k].c.amount;
            if (!add_ok(&pend, m)) return PLG_REFUSED_FUNDS;
        }
        uint64_t b = lg->bal[c->from];
        if (c->amount > b || pend > b - c->amount) return PLG_REFUSED_FUNDS;
    }
    if (lg->n_cl >= lg->cl_cap || !log_room(lg->log)) return PLG_FULL;
    uint32_t p = lg->n_cl;
    while (p > 0 && claim_cmp(&lg->cl[p - 1].c, c) > 0) {
        lg->cl[p] = lg->cl[p - 1];
        p--;
    }
    lg->cl[p].c = *c;
    lg->cl[p].st = PLG_CS_PENDING;
    lg->cl[p].logged = 0;
    lg->hold[lg->n_cl] = 0; /* conditional claims hold nothing, so no hold moves */
    lg->n_cl++;
    plg_res_t res = both ? PLG_ADDED_BOTH : PLG_ADDED;
    pdx_log_append(lg->log, PLG_OP_CLAIM, both ? PDX_KIND_NEUTRAL : PDX_KIND_NONE,
                   plg_tag(PLG_CS_PENDING), (uint8_t) res, c->epoch, c->id, 0);
    return res;
}

plg_res_t plg_vote(plg_t *lg, const plg_vote_t *v, bool sig_ok)
{
    if (!lg || !v) return PLG_REFUSED_INVALID;
    if (lg->halted) return PLG_HALTED;
    bool who = (v->kind == PLG_V_WITNESS && v->voter < lg->cfg.n_witness) ||
               (v->kind == PLG_V_ARBITER && v->voter == lg->cfg.arbiter && v->positive) ||
               (v->kind == PLG_V_OWNER && v->voter == lg->cfg.owner && v->positive);
    if (!sig_ok || !who || !v->epoch) return PLG_REFUSED_INVALID;
    for (uint32_t k = 0; k < lg->n_vt; k++)
        if (!vote_cmp(&lg->vt[k], v)) return PLG_DUPLICATE;
    if (v->epoch <= lg->horizon) return PLG_REFUSED_LATE;
    if (lg->n_vt >= lg->vt_cap || !log_room(lg->log)) return PLG_FULL;
    uint32_t p = lg->n_vt;
    while (p > 0 && vote_cmp(&lg->vt[p - 1], v) > 0) {
        lg->vt[p] = lg->vt[p - 1];
        p--;
    }
    lg->vt[p] = *v;
    lg->n_vt++;
    pdx_log_append(lg->log, PLG_OP_VOTE, PDX_KIND_NONE, plg_tag(PLG_CS_PENDING), PLG_ADDED,
                   v->epoch, v->claim, 0);
    return PLG_ADDED;
}

plg_res_t plg_merge(plg_t *dst, const plg_t *src, uint32_t *added, uint32_t *late)
{
    uint32_t na = 0, nl = 0;
    plg_res_t out = PLG_OK;
    if (!dst || !src) return PLG_REFUSED_INVALID;
    for (uint32_t i = 0; i < src->n_cl && out == PLG_OK; i++) {
        plg_res_t r = plg_claim(dst, &src->cl[i].c, true, false);
        if (r == PLG_ADDED || r == PLG_ADDED_BOTH) na++;
        if (r == PLG_REFUSED_LATE) nl++;
        if (r == PLG_FULL || r == PLG_HALTED) out = r;
    }
    for (uint32_t k = 0; k < src->n_vt && out == PLG_OK; k++) {
        plg_res_t r = plg_vote(dst, &src->vt[k], true);
        if (r == PLG_ADDED) na++;
        if (r == PLG_REFUSED_LATE) nl++;
        if (r == PLG_FULL || r == PLG_HALTED) out = r;
    }
    if (added) *added = na;
    if (late) *late = nl;
    return out;
}

/* The equivocation subject: SHA-256 of "zxv/plg-equiv/v1" || the slot's
 * other claim id (its head, or the next claim when this is the head) ||
 * this claim's id. */
static void equiv_subject(const plg_t *lg, uint32_t i, uint8_t out[PLG_HASH])
{
    uint32_t h = head_of(lg, i), o = h;
    if (h == i)
        for (uint32_t j = i + 1; j < lg->n_cl; j++)
            if (same_slot(&lg->cl[j].c, &lg->cl[i].c)) {
                o = j;
                break;
            }
    sha256_ctx_t c;
    sha256_init(&c);
    h_str(&c, "zxv/plg-equiv/v1");
    sha256_update(&c, lg->cl[o].c.id, PLG_HASH);
    sha256_update(&c, lg->cl[i].c.id, PLG_HASH);
    sha256_final(&c, out);
}

static bool needs_qrec(const plg_entry_t *e, uint32_t horizon)
{
    return e->c.epoch <= horizon && !e->logged &&
           (e->st == PLG_CS_INSUFFICIENT || e->st == PLG_CS_LOST);
}

plg_res_t plg_advance(plg_t *lg, uint32_t horizon)
{
    if (!lg) return PLG_REFUSED_INVALID;
    if (lg->halted) return PLG_HALTED;
    if (horizon < lg->horizon) return PLG_REFUSED_INVALID;
    lg->horizon = horizon;
    if (!replay(lg)) {
        /* L2: forbidden. Halt, quarantine, log; fail closed. */
        lg->halted = true;
        state_digest(lg, lg->digest);
        lpx_t t = lpx_make(LPX_B_FALSE, LPX_P_FORBIDDEN, LPX_PH_MANIFEST);
        lpx_qlog_append(lg->q, t, PLG_Q_CONSERVATION, 0, lg->digest);
        pdx_log_append(lg->log, PLG_OP_ADVANCE, PDX_KIND_NONE, t, PLG_FORBIDDEN, horizon,
                       lg->digest, lg->q->head);
        return PLG_FORBIDDEN;
    }
    uint32_t need = 0;
    for (uint32_t i = 0; i < lg->n_cl; i++)
        if (needs_qrec(&lg->cl[i], horizon)) need++;
    if (lg->q->n > lg->q->cap || lg->q->cap - lg->q->n < need || !log_room(lg->log)) {
        lg->halted = true;
        return PLG_FULL;
    }
    for (uint32_t i = 0; i < lg->n_cl; i++) {
        plg_entry_t *e = &lg->cl[i];
        if (!needs_qrec(e, horizon)) continue;
        uint8_t subj[PLG_HASH];
        uint8_t why = PLG_Q_INSUFFICIENT;
        if (e->st == PLG_CS_LOST) {
            equiv_subject(lg, i, subj);
            why = PLG_Q_EQUIVOCATION;
        } else {
            cp32(subj, e->c.id);
        }
        lpx_qlog_append(lg->q, lpx_forbid(plg_tag(e->st)), why, e->c.from, subj);
        e->logged = 1;
    }
    state_digest(lg, lg->digest);
    pdx_log_append(lg->log, PLG_OP_ADVANCE, PDX_KIND_NONE,
                   lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST), PLG_OK, horizon,
                   lg->digest, lg->q->head);
    return PLG_OK;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_abi.c — the chiglet's gated system ABI (chg_abi.h A1-A6). */
#include "chg_abi.h"
#include "../robin_debanks/sha256.h"

typedef struct {
    const char *name;
    uint8_t kind;
    uint8_t nargs;
    uint64_t lo[CHGA_ARGS], hi[CHGA_ARGS];
} op_t;

/* A1: index order is the enum's; append only. Bounds past nargs are unused. */
static const op_t OPS[CHGA_OPS] = {
    {.name = "storage_dma_read",
     .kind = CHGA_K_LOOK,
     .nargs = 2,
     .lo = {[1] = 1},
     .hi = {UINT64_MAX, CHGA_READ_MAX}},
    {.name = "framebuffer_blit",
     .kind = CHGA_K_LOOK,
     .nargs = 4,
     .lo = {[2] = 1, [3] = 1},
     .hi = {CHGA_SCREEN_MAX - 1, CHGA_SCREEN_MAX - 1, CHGA_SCREEN_MAX, CHGA_SCREEN_MAX}},
    {.name = "cell_reload", .kind = CHGA_K_CHANGE, .nargs = 1, .hi = {CHGA_CELLS_MAX - 1}},
    {.name = "network_pulse", .kind = CHGA_K_CHANGE},
    {.name = "cell_quarantine", .kind = CHGA_K_CHANGE, .nargs = 1, .hi = {CHGA_CELLS_MAX - 1}},
    {.name = "model_exec",
     .kind = CHGA_K_LOOK,
     .nargs = 2,
     .lo = {(uint64_t) CHGA_DOMAIN_MODEL << 56, 1},
     .hi = {((uint64_t) CHGA_DOMAIN_MODEL << 56) | 0x00FFFFFFFFFFFFFFull, UINT64_MAX}},
};

const char *chga_op_name(uint32_t op)
{
    return op < CHGA_OPS ? OPS[op].name : "";
}

int chga_op_kind(uint32_t op)
{
    return op < CHGA_OPS ? (int) OPS[op].kind : -1;
}

uint8_t chga_op_nargs(uint32_t op)
{
    return op < CHGA_OPS ? OPS[op].nargs : 0;
}

static bool same(const char *lit, const char *w, size_t n)
{
    size_t i = 0;
    while (i < n && lit[i] && lit[i] == w[i]) i++;
    return i == n && lit[i] == '\0';
}

int chga_op_find(const char *name, size_t len)
{
    if (!name || len == 0) return -1;
    for (uint32_t i = 0; i < CHGA_OPS; i++)
        if (same(OPS[i].name, name, len)) return (int) i;
    return -1;
}

/* A2 */
int chga_parse(const char *s, size_t len, chga_call_t *c)
{
    if (!s || !c) return CHGA_E_ARG;
    size_t i = 0;
    while (i < len && s[i] != ' ') i++;
    int op = chga_op_find(s, i);
    if (op < 0) return CHGA_E_ARG;
    chga_call_t t = {(uint8_t) op, 0, {0, 0, 0, 0}};
    while (i < len) {
        if (s[i] != ' ' || i + 1 >= len || t.nargs >= CHGA_ARGS) return CHGA_E_ARG;
        i++;
        size_t start = i;
        uint64_t v = 0;
        while (i < len && s[i] >= '0' && s[i] <= '9') {
            uint64_t d = (uint64_t) (s[i] - '0');
            if (v > (UINT64_MAX - d) / 10u) return CHGA_E_ARG;
            v = v * 10u + d;
            i++;
        }
        if (i == start) return CHGA_E_ARG;
        t.arg[t.nargs++] = v;
    }
    if (t.nargs != OPS[op].nargs) return CHGA_E_ARG;
    *c = t;
    return CHGA_OK;
}

int chga_init(chga_session_t *s, uint32_t grants, chga_rule_fn rule, void *rule_ctx,
              chga_run_fn run, void *run_ctx)
{
    if (!s || !rule || !run) return CHGA_E_ARG;
    s->grants = grants;
    s->rule = rule;
    s->rule_ctx = rule_ctx;
    s->run = run;
    s->run_ctx = run_ctx;
    s->seq = 0;
    for (uint32_t i = 0; i < CHGA_HELD; i++) s->held[i].used = false;
    s->head = s->tail = 0;
    for (uint32_t i = 0; i < CHGA_DIGEST; i++) s->chain[i] = 0;
    return CHGA_OK;
}

void chga_set_grants(chga_session_t *s, uint32_t grants)
{
    if (s) s->grants = grants;
}

static lpres_state_t truth(bool b)
{
    return b ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

static bool valid(lpres_state_t v)
{
    return v == LPRES_STATE_NEITHER || v == LPRES_STATE_TRUE || v == LPRES_STATE_FALSE ||
           v == LPRES_STATE_BOTH;
}

static bool well_formed(const chga_call_t *c)
{
    return c && c->op < CHGA_OPS && c->nargs == OPS[c->op].nargs;
}

/* A3: in bounds, granted (for the chiglet), and the rulebook */
static lpres_state_t gate(const chga_session_t *s, const chga_call_t *c, uint8_t from)
{
    const op_t *o = &OPS[c->op];
    bool in = true;
    for (uint32_t i = 0; i < o->nargs; i++)
        if (c->arg[i] < o->lo[i] || c->arg[i] > o->hi[i]) in = false;
    bool granted = from == CHGA_FROM_PERSON || ((s->grants >> c->op) & 1u);
    lpres_state_t r = s->rule(s->rule_ctx, c);
    if (!valid(r)) r = LPRES_STATE_FALSE; /* a broken rulebook never authorises */
    return lpres_conjoin(lpres_conjoin(truth(in), truth(granted)), r);
}

static bool journal_full(const chga_session_t *s)
{
    return s->head - s->tail >= CHGA_JOURNAL;
}

static const char CHAIN_TAG[] = "zxv/chga/event/v1";

void chga_rec_encode(const chga_rec_t *r, uint8_t out[CHGA_REC_BYTES])
{
    size_t n = 0;
    for (uint32_t k = 0; k < 8; k++) out[n++] = (uint8_t) (r->seq >> (8 * k));
    out[n++] = r->op;
    out[n++] = r->from;
    out[n++] = r->outcome;
    out[n++] = (uint8_t) r->verdict;
    for (uint32_t k = 0; k < 4; k++) out[n++] = (uint8_t) ((uint32_t) r->rc >> (8 * k));
    out[n++] = r->nargs;
    for (uint32_t i = 0; i < CHGA_ARGS; i++) {
        uint64_t a = i < r->nargs ? r->arg[i] : 0;
        for (uint32_t k = 0; k < 8; k++) out[n++] = (uint8_t) (a >> (8 * k));
    }
}

void chga_chain_next(const uint8_t prev[CHGA_DIGEST], const chga_rec_t *r, uint8_t out[CHGA_DIGEST])
{
    uint8_t b[CHGA_REC_BYTES];
    chga_rec_encode(r, b);
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) CHAIN_TAG, sizeof CHAIN_TAG - 1);
    sha256_update(&c, prev, CHGA_DIGEST);
    sha256_update(&c, b, sizeof b);
    sha256_final(&c, out);
}

const uint8_t *chga_chain_head(const chga_session_t *s)
{
    return s ? s->chain : 0;
}

/* A5 commit, A8 chain */
static void commit(chga_session_t *s, uint64_t seq, const chga_call_t *c, uint8_t from,
                   uint8_t outcome, lpres_state_t v, int32_t rc)
{
    chga_rec_t *r = &s->journal[s->head % CHGA_JOURNAL];
    r->seq = seq;
    r->op = c->op;
    r->from = from;
    r->outcome = outcome;
    r->verdict = v;
    r->rc = rc;
    r->nargs = c->nargs;
    for (uint32_t i = 0; i < CHGA_ARGS; i++) r->arg[i] = i < c->nargs ? c->arg[i] : 0;
    chga_chain_next(s->chain, r, r->head);
    for (uint32_t i = 0; i < CHGA_DIGEST; i++) s->chain[i] = r->head[i];
    s->head++;
}

static void digest_of(uint64_t seq, const chga_call_t *c, uint8_t out[CHGA_DIGEST])
{
    uint8_t b[8 + 2 + 8 * CHGA_ARGS];
    size_t n = 0;
    for (uint32_t k = 0; k < 8; k++) b[n++] = (uint8_t) (seq >> (8 * k));
    b[n++] = c->op;
    b[n++] = c->nargs;
    for (uint32_t i = 0; i < c->nargs; i++)
        for (uint32_t k = 0; k < 8; k++) b[n++] = (uint8_t) (c->arg[i] >> (8 * k));
    sha256(b, n, out);
}

static void result(chga_result_t *res, uint64_t seq, uint8_t outcome, lpres_state_t v, int32_t rc)
{
    if (!res) return;
    res->seq = seq;
    res->outcome = outcome;
    res->verdict = v;
    res->rc = rc;
}

int chga_submit(chga_session_t *s, const chga_call_t *c, uint8_t from, chga_result_t *res)
{
    if (!s || !well_formed(c) || from > CHGA_FROM_PERSON) return CHGA_E_ARG;
    lpres_state_t v = gate(s, c, from); /* check */
    bool runs =
        v == LPRES_STATE_TRUE && (OPS[c->op].kind == CHGA_K_LOOK || from == CHGA_FROM_PERSON);
    bool holds = v != LPRES_STATE_FALSE && !runs;
    if (journal_full(s)) return CHGA_E_FULL; /* reserve */
    int slot = -1;
    if (holds) {
        for (uint32_t i = 0; i < CHGA_HELD && slot < 0; i++)
            if (!s->held[i].used) slot = (int) i;
        if (slot < 0) return CHGA_E_FULL;
    }
    uint64_t seq = ++s->seq;
    if (v == LPRES_STATE_FALSE) {
        commit(s, seq, c, from, CHGA_REFUSED, v, 0);
        result(res, seq, CHGA_REFUSED, v, 0);
        return CHGA_OK;
    }
    if (holds) {
        chga_held_t *h = &s->held[slot];
        h->used = true;
        h->seq = seq;
        h->call = *c;
        digest_of(seq, c, h->digest);
        commit(s, seq, c, from, CHGA_HELD_CALL, v, 0);
        result(res, seq, CHGA_HELD_CALL, v, 0);
        if (res)
            for (uint32_t k = 0; k < CHGA_DIGEST; k++) res->digest[k] = h->digest[k];
        return CHGA_OK;
    }
    int rc = s->run(s->run_ctx, c); /* compute */
    uint8_t out = rc == 0 ? CHGA_RAN : CHGA_FAILED;
    commit(s, seq, c, from, out, v, rc);
    result(res, seq, out, v, rc);
    return CHGA_OK;
}

static chga_held_t *find_held(chga_session_t *s, uint64_t seq)
{
    for (uint32_t i = 0; i < CHGA_HELD; i++)
        if (s->held[i].used && s->held[i].seq == seq) return &s->held[i];
    return 0;
}

const chga_held_t *chga_held(const chga_session_t *s, uint64_t seq)
{
    if (!s) return 0;
    return find_held((chga_session_t *) s, seq);
}

int chga_accept(chga_session_t *s, uint64_t seq, const uint8_t *digest, size_t dlen,
                chga_result_t *res)
{
    if (!s || !digest || dlen < CHGA_ACCEPT_MIN || dlen > CHGA_DIGEST) return CHGA_E_ARG;
    chga_held_t *h = find_held(s, seq);
    if (!h) return CHGA_E_NOHELD;
    uint8_t diff = 0;
    for (size_t k = 0; k < dlen; k++) diff |= (uint8_t) (h->digest[k] ^ digest[k]);
    if (diff) return CHGA_E_DIGEST;
    if (journal_full(s)) return CHGA_E_FULL;
    /* A4: checked again now; the person's accept answers "not sure" and a
     * dispute, but a withdrawn grant or a rule that now says FALSE refuses */
    lpres_state_t v = gate(s, &h->call, CHGA_FROM_CHIGLET);
    chga_call_t c = h->call;
    h->used = false;
    if (v == LPRES_STATE_FALSE) {
        commit(s, seq, &c, CHGA_FROM_PERSON, CHGA_REFUSED, v, 0);
        result(res, seq, CHGA_REFUSED, v, 0);
        return CHGA_OK;
    }
    int rc = s->run(s->run_ctx, &c);
    uint8_t out = rc == 0 ? CHGA_RAN : CHGA_FAILED;
    commit(s, seq, &c, CHGA_FROM_PERSON, out, v, rc);
    result(res, seq, out, v, rc);
    return CHGA_OK;
}

int chga_refuse(chga_session_t *s, uint64_t seq)
{
    if (!s) return CHGA_E_ARG;
    chga_held_t *h = find_held(s, seq);
    if (!h) return CHGA_E_NOHELD;
    if (journal_full(s)) return CHGA_E_FULL;
    h->used = false;
    commit(s, seq, &h->call, CHGA_FROM_PERSON, CHGA_DROPPED, LPRES_STATE_FALSE, 0);
    return CHGA_OK;
}

bool chga_pop(chga_session_t *s, chga_rec_t *out)
{
    if (!s || !out || s->head == s->tail) return false;
    *out = s->journal[s->tail % CHGA_JOURNAL];
    s->tail++;
    return true;
}

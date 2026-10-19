/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mint.c — minting against attested capacity. See mint.h M1-M11. */
#include "mint.h"

/* rung r (1..45) has face F(r+1) */
static const uint64_t LADDER[MINT_LADDER_RUNGS] = {
    1ull,         2ull,          3ull,         5ull,         8ull,         13ull,
    21ull,        34ull,         55ull,        89ull,        144ull,       233ull,
    377ull,       610ull,        987ull,       1597ull,      2584ull,      4181ull,
    6765ull,      10946ull,      17711ull,     28657ull,     46368ull,     75025ull,
    121393ull,    196418ull,     317811ull,    514229ull,    832040ull,    1346269ull,
    2178309ull,   3524578ull,    5702887ull,   9227465ull,   14930352ull,  24157817ull,
    39088169ull,  63245986ull,   102334155ull, 165580141ull, 267914296ull, 433494437ull,
    701408733ull, 1134903170ull, 1836311903ull};

static const pay_account_t *acct(const mint_t *m, uint32_t id)
{
    return pay_ledger_account(m->L, id); /* NULL when unknown or inactive */
}

static bool is_issuer(const pay_account_t *a)
{
    return (a->flags & PAY_ACCT_ISSUER) != 0;
}

mint_status_t mint_init(mint_t *m, pay_ledger_t *L, uint32_t vino_issuer, uint32_t vbill_issuer,
                        uint64_t vbill_backing, bool exclusive, mint_verify_fn verify, void *vctx,
                        const uint8_t seed[32])
{
    if (!m || !L || !verify || !vbill_backing) return MINT_ERR_ARG;
    pay_memset(m, 0, sizeof *m);
    m->L = L;
    const pay_account_t *v = acct(m, vino_issuer), *b = acct(m, vbill_issuer);
    if (!v || !b || !is_issuer(v) || !is_issuer(b) || v->asset == b->asset) return MINT_ERR_STATE;
    m->issuer[MINT_TARGET_VINO] = vino_issuer;
    m->issuer[MINT_TARGET_VBILL] = vbill_issuer;
    m->vbill_backing = vbill_backing;
    m->exclusive = exclusive;
    m->verify = verify;
    m->vctx = vctx;
    if (seed) pay_memcpy(m->seed, seed, 32);
    m->last_ledger = PAY_OK;
    m->ready = true;
    return MINT_OK;
}

mint_status_t mint_set_backing(mint_t *m, uint64_t vbill_backing)
{
    if (!m || !m->ready || !vbill_backing) return MINT_ERR_ARG;
    m->vbill_backing = vbill_backing;
    return MINT_OK;
}

/* ---- M1 / M2 / M3 ---- */

static mint_juris_t *juris_find(const mint_t *m, uint32_t code)
{
    for (uint32_t i = 0; i < MINT_MAX_JURIS; i++)
        if (m->juris[i].used && m->juris[i].code == code) return (mint_juris_t *) &m->juris[i];
    return 0;
}

const mint_juris_t *mint_juris(const mint_t *m, uint32_t code)
{
    return m ? juris_find(m, code) : 0;
}

mint_status_t mint_juris_set(mint_t *m, uint32_t code, bool enabled, uint32_t haircut_bps,
                             uint64_t cap)
{
    if (!m || !m->ready || haircut_bps > MINT_BPS || cap >= PAY_BAL_MAX) return MINT_ERR_ARG;
    mint_juris_t *j = juris_find(m, code);
    if (j && cap < j->used_epoch) return MINT_ERR_STATE;
    for (uint32_t i = 0; !j && i < MINT_MAX_JURIS; i++)
        if (!m->juris[i].used) j = &m->juris[i];
    if (!j) return MINT_ERR_FULL;
    j->used = true;
    j->code = code;
    j->enabled = enabled;
    j->haircut_bps = haircut_bps;
    j->cap = cap;
    return MINT_OK;
}

static mint_attestor_t *att_find(const mint_t *m, uint32_t id)
{
    for (uint32_t i = 0; i < MINT_MAX_ATTESTORS; i++)
        if (m->att[i].used && m->att[i].id == id) return (mint_attestor_t *) &m->att[i];
    return 0;
}

mint_status_t mint_attestor_set(mint_t *m, uint32_t id, bool enabled, uint32_t haircut_bps)
{
    if (!m || !m->ready || haircut_bps > MINT_BPS) return MINT_ERR_ARG;
    mint_attestor_t *a = att_find(m, id);
    for (uint32_t i = 0; !a && i < MINT_MAX_ATTESTORS; i++)
        if (!m->att[i].used) a = &m->att[i];
    if (!a) return MINT_ERR_FULL;
    a->used = true;
    a->id = id;
    a->enabled = enabled;
    a->haircut_bps = haircut_bps;
    return MINT_OK;
}

mint_status_t mint_unit_set(mint_t *m, uint8_t kind, uint64_t num, uint64_t den)
{
    if (!m || !m->ready || kind >= MINT_KINDS || !den) return MINT_ERR_ARG;
    m->unit[kind].num = num;
    m->unit[kind].den = den;
    return MINT_OK;
}

/* ---- M5 / M6 pure ---- */

uint64_t mint_haircut(uint64_t x, uint32_t bps)
{
    uint64_t out = 0;
    if (bps <= MINT_BPS) pay_muldiv(x, MINT_BPS - bps, MINT_BPS, &out, 0); /* <= x: fits */
    return out;
}

bool mint_value(uint64_t qty, uint64_t num, uint64_t den, uint32_t att_bps, uint32_t juris_bps,
                uint64_t *gross, uint64_t *net)
{
    uint64_t g;
    if (!gross || !net || !den || !pay_muldiv(qty, num, den, &g, 0)) return false;
    *gross = g;
    *net = mint_haircut(mint_haircut(g, att_bps), juris_bps);
    return true;
}

uint64_t mint_coin_face(uint32_t rung)
{
    return (rung >= 1 && rung <= MINT_LADDER_RUNGS) ? LADDER[rung - 1] : 0;
}

bool mint_zeckendorf(uint64_t amount, mint_coins_t *out)
{
    uint64_t rest;
    if (!out) return false;
    pay_memset(out, 0, sizeof *out);
    out->top = pay_udiv64(amount, LADDER[MINT_LADDER_RUNGS - 1], &rest);
    for (uint32_t r = MINT_LADDER_RUNGS; r >= 1; r--) { /* rest < top face */
        if (rest >= LADDER[r - 1]) {
            rest -= LADDER[r - 1];
            out->rung[out->n++] = (uint8_t) r;
        }
    }
    return true;
}

/* ---- M4 ---- */

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

void mint_claim_msg(const mint_capacity_t *c, uint8_t out[MINT_CLAIM_MSG])
{
    static const char tag[] = "zxv/mint-claim/v1";
    for (uint32_t i = 0; i < MINT_TAG_LEN; i++) out[i] = (uint8_t) tag[i];
    put32(out + 17, c->attestor);
    pay_memcpy(out + 21, c->id, 32);
    out[53] = c->kind;
    put32(out + 54, c->juris);
    put64(out + 58, c->qty);
    put64(out + 66, c->expiry);
}

/* ---- ledger postings ---- */

/* SHA3-256 of "zxv/mint/req" || seed || kind || seq (M9) */
static void derive(const mint_t *m, uint8_t kind, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "zxv/mint/req");
    pay_hbuf_put(&h, m->seed, 32);
    pay_hbuf_put(&h, &kind, 1);
    pay_hbuf_u64(&h, m->seq);
    pay_hbuf_final(&h, out);
}

static mint_status_t post(mint_t *m,
                          pay_status_t (*fn)(pay_ledger_t *, pay_posting_req_t *, uint32_t,
                                             uint32_t, uint64_t, pay_receipt_t *),
                          uint32_t a, uint32_t b, uint64_t amount, uint64_t tick)
{
    pay_posting_req_t r;
    uint8_t d[32];
    pay_memset(&r, 0, sizeof r);
    derive(m, 'I', r.idem_key);
    derive(m, 'U', d);
    pay_uetr_from_random(d, r.uetr);
    pay_w w;
    pay_w_init(&w, r.e2e, sizeof r.e2e);
    pay_w_s(&w, "MINT-");
    pay_w_u64(&w, m->seq);
    pay_w_finish(&w);
    r.tick = tick;
    r.ext_cap = PAY_CAP_FINANCIAL;
    m->seq++;
    pay_receipt_t rc;
    m->last_ledger = fn(m->L, &r, a, b, amount, &rc);
    return m->last_ledger == PAY_OK ? MINT_OK : MINT_ERR_LEDGER;
}

/* ---- M4-M9 ---- */

static mint_claim_t *claim_find(const mint_t *m, const uint8_t id[32])
{
    for (uint32_t i = 0; i < MINT_MAX_CLAIMS; i++)
        if (m->claim[i].state != MINT_FREE && pay_memeq(m->claim[i].cap.id, id, 32))
            return (mint_claim_t *) &m->claim[i];
    return 0;
}

const mint_claim_t *mint_find(const mint_t *m, const uint8_t id[32])
{
    return (m && id) ? claim_find(m, id) : 0;
}

mint_status_t mint_issue(mint_t *m, const mint_capacity_t *c, const uint8_t *sig, uint32_t sig_len,
                         uint8_t target, uint32_t holder, uint64_t now, uint32_t *index)
{
    if (!m || !m->ready || !c || !sig || c->kind >= MINT_KINDS || !c->qty || target >= MINT_TARGETS)
        return MINT_ERR_ARG;
    const pay_account_t *h = acct(m, holder), *iss = acct(m, m->issuer[target]);
    if (!h || !iss || h->asset != iss->asset || is_issuer(h)) return MINT_ERR_ARG;
    const mint_attestor_t *a = att_find(m, c->attestor);
    if (!a || !a->enabled) return MINT_ERR_UNKNOWN;
    mint_juris_t *j = juris_find(m, c->juris);
    if (!j || !j->enabled) return MINT_ERR_JURIS;
    const mint_unit_t *u = &m->unit[c->kind];
    if (!u->num) return MINT_ERR_PRICE;
    if (c->expiry <= now) return MINT_ERR_EXPIRED;
    uint8_t msg[MINT_CLAIM_MSG];
    mint_claim_msg(c, msg);
    if (!m->verify(m->vctx, c->attestor, msg, sizeof msg, sig, sig_len)) return MINT_ERR_SIG;
    if (claim_find(m, c->id)) return MINT_ERR_DUP;
    uint64_t gross, net, units, face, used, total;
    if (!mint_value(c->qty, u->num, u->den, a->haircut_bps, j->haircut_bps, &gross, &net))
        return MINT_ERR_OVERFLOW;
    if (target == MINT_TARGET_VINO) {
        units = face = net;
    } else {
        units = pay_udiv64(net, m->vbill_backing, 0);
        face = units * m->vbill_backing; /* <= net */
    }
    if (!units) return MINT_ERR_ZERO;
    if (!pay_add_ok(j->used_epoch, face, &used) || used > j->cap) return MINT_ERR_CAP;
    if (!pay_add_ok(m->issued[target], units, &total)) return MINT_ERR_OVERFLOW;
    mint_claim_t *k = 0;
    uint32_t slot = 0;
    for (uint32_t i = 0; !k && i < MINT_MAX_CLAIMS; i++) {
        mint_claim_t *x = &m->claim[i];
        if (x->state == MINT_FREE ||
            (x->state == MINT_RETIRED && x->cap.expiry <= now && x->epoch < m->epoch)) {
            k = x;
            slot = i;
        }
    }
    if (!k) return MINT_ERR_FULL;
    mint_status_t st = post(m, pay_ledger_issue, m->issuer[target], holder, units, now);
    if (st != MINT_OK) return st;
    pay_memset(k, 0, sizeof *k);
    k->state = MINT_LIVE;
    k->cap = *c;
    k->epoch = m->epoch;
    k->unit_num = u->num;
    k->unit_den = u->den;
    k->att_bps = a->haircut_bps;
    k->juris_bps = j->haircut_bps;
    k->gross = gross;
    k->net = net;
    k->target = target;
    k->holder = holder;
    k->units = units;
    k->face = face;
    k->remainder = net - face;
    if (target == MINT_TARGET_VINO)
        mint_zeckendorf(units, &k->coins);
    else
        k->backing = m->vbill_backing;
    j->used_epoch = used;
    m->issued[target] = total;
    m->live[target] += units; /* <= issued */
    if (index) *index = slot;
    return MINT_OK;
}

mint_status_t mint_epoch_advance(mint_t *m)
{
    if (!m || !m->ready) return MINT_ERR_ARG;
    m->epoch++;
    for (uint32_t i = 0; i < MINT_MAX_JURIS; i++) m->juris[i].used_epoch = 0;
    return MINT_OK;
}

mint_status_t mint_quarantine(mint_t *m, const uint8_t id[32], uint64_t *face)
{
    if (!m || !m->ready || !id) return MINT_ERR_ARG;
    mint_claim_t *k = claim_find(m, id);
    if (!k) return MINT_ERR_UNKNOWN;
    if (k->state != MINT_LIVE) return MINT_ERR_STATE;
    k->state = MINT_QUARANTINED;
    m->live[k->target] -= k->units;
    m->quarantined[k->target] += k->units;
    if (face) *face = k->face;
    return MINT_OK;
}

mint_status_t mint_retire(mint_t *m, const uint8_t id[32], uint32_t from)
{
    if (!m || !m->ready || !id) return MINT_ERR_ARG;
    mint_claim_t *k = claim_find(m, id);
    if (!k) return MINT_ERR_UNKNOWN;
    if (k->state != MINT_LIVE && k->state != MINT_QUARANTINED) return MINT_ERR_STATE;
    const pay_account_t *f = acct(m, from), *iss = acct(m, m->issuer[k->target]);
    if (!f || !iss || f->asset != iss->asset || from == m->issuer[k->target]) return MINT_ERR_ARG;
    mint_status_t st = post(m, pay_ledger_redeem, from, m->issuer[k->target], k->units, 0);
    if (st != MINT_OK) return st;
    if (k->state == MINT_LIVE)
        m->live[k->target] -= k->units;
    else
        m->quarantined[k->target] -= k->units;
    m->retired[k->target] += k->units; /* <= issued */
    k->state = MINT_RETIRED;
    return MINT_OK;
}

/* ---- M10 / M11 ---- */

uint64_t mint_backing(const mint_t *m, uint64_t now)
{
    uint64_t s = 0;
    for (uint32_t i = 0; m && i < MINT_MAX_CLAIMS; i++) {
        const mint_claim_t *k = &m->claim[i];
        /* faces sum below the lifetime units issued times the backing; a
         * sum that would overflow saturates (never reached in practice) */
        if (k->state == MINT_LIVE && now < k->cap.expiry && !pay_add_ok(s, k->face, &s))
            return UINT64_MAX;
    }
    return s;
}

static bool claim_ok(const mint_claim_t *k)
{
    uint64_t f;
    if (k->target >= MINT_TARGETS || k->net > k->gross || !k->units) return false;
    if (!pay_add_ok(k->face, k->remainder, &f) || f != k->net) return false;
    if (k->target == MINT_TARGET_VINO) return k->units == k->face && k->remainder == 0;
    return k->backing && !__builtin_mul_overflow(k->units, k->backing, &f) && f == k->face &&
           k->remainder < k->backing;
}

bool mint_check(const mint_t *m)
{
    if (!m || !m->ready) return false;
    uint64_t live[MINT_TARGETS] = {0, 0}, quar[MINT_TARGETS] = {0, 0};
    for (uint32_t i = 0; i < MINT_MAX_CLAIMS; i++) {
        const mint_claim_t *k = &m->claim[i];
        if (k->state == MINT_FREE) continue;
        if (k->state > MINT_RETIRED || !claim_ok(k)) return false;
        if (k->state == MINT_LIVE && !pay_add_ok(live[k->target], k->units, &live[k->target]))
            return false;
        if (k->state == MINT_QUARANTINED &&
            !pay_add_ok(quar[k->target], k->units, &quar[k->target]))
            return false;
    }
    for (uint32_t t = 0; t < MINT_TARGETS; t++) {
        uint64_t out, net;
        const pay_account_t *iss = acct(m, m->issuer[t]);
        if (!iss || live[t] != m->live[t] || quar[t] != m->quarantined[t]) return false;
        if (!pay_add_ok(live[t], quar[t], &out) || !pay_sub_ok(m->issued[t], m->retired[t], &net) ||
            net != out)
            return false;
        if (iss->credit < out || (m->exclusive && iss->credit != out)) return false;
    }
    for (uint32_t j = 0; j < MINT_MAX_JURIS; j++) {
        const mint_juris_t *x = &m->juris[j];
        if (!x->used) continue;
        uint64_t s = 0;
        for (uint32_t i = 0; i < MINT_MAX_CLAIMS; i++) {
            const mint_claim_t *k = &m->claim[i];
            if (k->state != MINT_FREE && k->epoch == m->epoch && k->cap.juris == x->code &&
                !pay_add_ok(s, k->face, &s))
                return false;
        }
        if (s != x->used_epoch || x->used_epoch > x->cap) return false;
    }
    return true;
}

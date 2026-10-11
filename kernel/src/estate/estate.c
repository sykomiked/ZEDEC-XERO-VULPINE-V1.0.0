/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* estate.c — estate succession control. See estate.h E1-E6. */
#include "estate.h"
#include "../pay/pay_util.h"

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

est_status_t est_init(est_t *e, const est_cfg_t *cfg, est_verify_fn verify, void *vctx)
{
    if (!e || !cfg || !verify) return EST_ERR_ARG;
    if (cfg->n_guardians > EST_MAX_GUARDIANS || cfg->k == 0 || cfg->k > cfg->n_guardians ||
        cfg->n_attestors == 0 || cfg->n_attestors > EST_MAX_ATTESTORS)
        return EST_ERR_ARG;
    pay_memset(e, 0, sizeof *e);
    e->cfg = *cfg;
    e->verify = verify;
    e->vctx = vctx;
    return EST_OK;
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

void est_msg(const est_t *e, uint8_t kind, uint64_t value, uint8_t out[EST_MSG])
{
    static const char tag[] = "zxv/est-msg/v1";
    out[14] = kind;
    for (uint32_t i = 0; i < 14; i++) out[i] = (uint8_t) tag[i];
    pay_memcpy(out + 15, e->cfg.owner, EST_KEY);
    put64(out + 15 + EST_KEY, e->epoch);
    put64(out + 23 + EST_KEY, value);
    pay_memcpy(out + 31 + EST_KEY, e->digest, 32);
}

uint64_t est_contest_value(const est_t *e, uint32_t heir)
{
    return (e->ccount[heir % EST_MAX_HEIRS] << 8) | (heir & 0xFFu);
}

static bool signed_by(const est_t *e, const uint8_t key[EST_KEY], uint8_t kind, uint64_t value,
                      const uint8_t *sig, uint32_t sig_len)
{
    uint8_t m[EST_MSG];
    est_msg(e, kind, value, m);
    return e->verify(e->vctx, key, m, sizeof m, sig, sig_len);
}

/* a guardian index or EST_COURT; 0 for anyone else */
static const uint8_t *finder(const est_t *e, uint32_t who)
{
    if (who == EST_COURT) return e->cfg.court;
    return who < e->cfg.n_guardians ? e->cfg.guardian[who] : 0;
}

est_status_t est_report(est_t *e, uint32_t attestor, uint64_t bond, uint64_t tick,
                        const uint8_t *sig, uint32_t sig_len)
{
    if (!e || !sig || bond == 0 || attestor >= e->cfg.n_attestors) return EST_ERR_ARG;
    if (e->phase != EST_ALIVE) return EST_ERR_STATE;
    if (!signed_by(e, e->cfg.attestor[attestor], EST_MSG_REPORT, bond, sig, sig_len))
        return EST_ERR_SIG;
    e->phase = EST_REPORTED;
    e->reported = tick;
    e->bond = bond;
    e->bond_state = EST_BOND_HELD;
    e->approved = 0;
    return EST_OK;
}

est_status_t est_approve(est_t *e, uint32_t guardian, const uint8_t *sig, uint32_t sig_len)
{
    if (!e || !sig || guardian >= e->cfg.n_guardians) return EST_ERR_ARG;
    if (e->phase != EST_REPORTED) return EST_ERR_STATE;
    if (e->approved & (1u << guardian)) return EST_DUP;
    if (!signed_by(e, e->cfg.guardian[guardian], EST_MSG_APPROVE, 0, sig, sig_len))
        return EST_ERR_SIG;
    e->approved |= 1u << guardian;
    return EST_OK;
}

est_status_t est_confirm(est_t *e, uint64_t tick)
{
    if (!e) return EST_ERR_ARG;
    if (e->phase != EST_REPORTED) return EST_ERR_STATE;
    uint32_t n = 0;
    for (uint32_t x = e->approved; x; x &= x - 1) n++;
    if (n < e->cfg.k) return EST_ERR_QUORUM;
    uint64_t due;
    if (__builtin_add_overflow(e->reported, e->cfg.wait_ticks, &due) || tick < due)
        return EST_ERR_TIME;
    e->phase = EST_CONFIRMED;
    e->confirmed = tick;
    return EST_OK;
}

est_status_t est_rule(est_t *e, const uint8_t *sig, uint32_t sig_len)
{
    if (!e || !sig) return EST_ERR_ARG;
    if (e->phase != EST_CONFIRMED) return EST_ERR_STATE;
    if (e->enclave == EST_NONE) return EST_ERR_NOMANIFEST;
    if (e->enclave == EST_COMMON) return EST_ERR_STATE;
    if (!signed_by(e, e->cfg.gateway, EST_MSG_RULING, 0, sig, sig_len)) return EST_ERR_SIG;
    e->phase = EST_RULED;
    return EST_OK;
}

est_status_t est_begin(est_t *e)
{
    if (!e) return EST_ERR_ARG;
    if (e->enclave == EST_NONE) return EST_ERR_NOMANIFEST;
    bool ok = e->phase == EST_RULED || (e->phase == EST_CONFIRMED && e->enclave == EST_COMMON);
    if (!ok) return EST_ERR_STATE;
    e->phase = EST_DISTRIBUTING;
    return EST_OK;
}

est_status_t est_close(est_t *e)
{
    if (!e) return EST_ERR_ARG;
    if (e->phase != EST_DISTRIBUTING) return EST_ERR_STATE;
    e->phase = EST_CLOSED;
    if (e->bond_state == EST_BOND_HELD) e->bond_state = EST_BOND_REFUNDED;
    return EST_OK;
}

est_status_t est_veto(est_t *e, const uint8_t *sig, uint32_t sig_len)
{
    if (!e || !sig) return EST_ERR_ARG;
    if (e->phase != EST_REPORTED && e->phase != EST_CONFIRMED) return EST_ERR_STATE;
    if (!signed_by(e, e->cfg.owner, EST_MSG_VETO, 0, sig, sig_len)) return EST_ERR_SIG;
    e->phase = EST_ALIVE;
    e->approved = 0;
    e->epoch++;
    if (e->bond_state == EST_BOND_HELD) e->bond_state = EST_BOND_REFUNDED;
    return EST_OK;
}

est_status_t est_fraud_finding(est_t *e, uint32_t who, const uint8_t *sig, uint32_t sig_len)
{
    if (!e || !sig) return EST_ERR_ARG;
    const uint8_t *key = finder(e, who);
    if (!key) return EST_ERR_ARG;
    if (e->bond_state != EST_BOND_HELD) return EST_ERR_STATE;
    if (!signed_by(e, key, EST_MSG_FRAUD, 0, sig, sig_len)) return EST_ERR_SIG;
    e->bond_state = EST_BOND_FORFEITED;
    return EST_OK;
}

static void digest(est_t *e)
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "zxv/est-manifest/v1");
    pay_hbuf_u64(&h, (uint64_t) e->enclave);
    pay_hbuf_u64(&h, e->n);
    pay_hbuf_u64(&h, e->den);
    for (uint32_t i = 0; i < e->n; i++) {
        pay_hbuf_u64(&h, e->id[i]);
        pay_hbuf_u64(&h, e->num[i]);
    }
    pay_hbuf_final(&h, e->digest); /* at most 27 + 24 + 16 * 16 bytes: fits */
}

est_status_t est_set_manifest(est_t *e, est_enclave_t enc, uint32_t n, const uint32_t id[],
                              const uint64_t num[], const uint64_t den[])
{
    if (!e || !id || !num || !den || n == 0 || n > EST_MAX_HEIRS) return EST_ERR_ARG;
    if (enc != EST_COMMON && enc != EST_FORCED && enc != EST_FARAID) return EST_ERR_ARG;
    if (enc == EST_COMMON ? e->phase != EST_ALIVE : e->phase > EST_CONFIRMED) return EST_ERR_STATE;
    uint64_t L = 1, s[EST_MAX_HEIRS], sum = 0, g = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (den[i] == 0 || num[i] > den[i]) return EST_ERR_ARG;
        if (__builtin_mul_overflow(L / gcd64(L, den[i]), den[i], &L)) return EST_ERR_OVERFLOW;
    }
    for (uint32_t i = 0; i < n; i++) {
        s[i] = num[i] * (L / den[i]); /* <= L: num <= den */
        if (__builtin_add_overflow(sum, s[i], &sum)) return EST_ERR_OVERFLOW;
        g = gcd64(g, s[i]);
    }
    if (sum != L) return EST_ERR_SUM;
    g = gcd64(g, L); /* > 0: L > 0 */
    pay_memset(e->contested, 0, sizeof e->contested);
    for (uint32_t i = 0; i < n; i++) {
        e->id[i] = id[i];
        e->num[i] = s[i] / g;
    }
    e->n = n;
    e->den = L / g;
    e->enclave = enc;
    digest(e);
    return EST_OK;
}

bool est_advisory(const est_t *e)
{
    return e && (e->enclave == EST_FORCED || e->enclave == EST_FARAID) && e->phase < EST_RULED;
}

/* floor(a * num / den) and the remainder; num <= den so it fits */
static uint64_t part(uint64_t a, uint64_t num, uint64_t den, uint64_t *rem)
{
    return pay_udiv128_64(pay_mul64(a, num), den, rem).lo;
}

/* hand part[i] to out[i] or, for a contested heir, to its escrow */
static est_status_t pay_out(est_t *e, const uint64_t p[], uint64_t out[], uint64_t *to_escrow)
{
    uint64_t esc = 0, tot, ei[EST_MAX_HEIRS];
    for (uint32_t i = 0; i < e->n; i++) {
        ei[i] = e->escrow[i];
        if (e->contested[i]) {
            if (__builtin_add_overflow(ei[i], p[i], &ei[i])) return EST_ERR_OVERFLOW;
            esc += p[i]; /* the parts sum to at most one uint64 amount */
        }
    }
    if (__builtin_add_overflow(e->escrow_total, esc, &tot)) return EST_ERR_OVERFLOW;
    for (uint32_t i = 0; i < e->n; i++) {
        out[i] = e->contested[i] ? 0 : p[i];
        e->escrow[i] = ei[i];
    }
    e->escrow_total = tot;
    if (to_escrow) *to_escrow = esc;
    return EST_OK;
}

est_status_t est_distribute(est_t *e, uint64_t amount, uint64_t out[EST_MAX_HEIRS],
                            uint64_t *to_escrow)
{
    if (!e || !out) return EST_ERR_ARG;
    if (e->phase != EST_DISTRIBUTING) return EST_ERR_STATE;
    uint64_t p[EST_MAX_HEIRS], r[EST_MAX_HEIRS], left = amount;
    for (uint32_t i = 0; i < e->n; i++) {
        p[i] = part(amount, e->num[i], e->den, &r[i]);
        left -= p[i];
    }
    /* left < n: one unit each by largest remainder, ties to the lower index */
    while (left) {
        uint32_t best = 0;
        for (uint32_t i = 1; i < e->n; i++)
            if (r[i] > r[best]) best = i;
        p[best]++;
        r[best] = 0;
        left--;
    }
    return pay_out(e, p, out, to_escrow);
}

est_status_t est_stream(est_t *e, uint64_t amount, uint64_t out[EST_MAX_HEIRS], uint64_t *to_escrow)
{
    if (!e || !out) return EST_ERR_ARG;
    if (e->phase != EST_DISTRIBUTING && e->phase != EST_CLOSED) return EST_ERR_STATE;
    uint64_t T, p[EST_MAX_HEIRS] = {0}, target[EST_MAX_HEIRS], rem; /* {0}: for -O2 analysis */
    if (__builtin_add_overflow(e->stream_total, amount, &T)) return EST_ERR_OVERFLOW;
    for (uint32_t i = 0; i < e->n; i++) {
        target[i] = part(T, e->num[i], e->den, &rem);
        p[i] = target[i] - e->paid[i];
    }
    est_status_t st = pay_out(e, p, out, to_escrow);
    if (st != EST_OK) return st;
    e->stream_total = T;
    for (uint32_t i = 0; i < e->n; i++) e->paid[i] = target[i];
    return EST_OK;
}

uint64_t est_stream_held(const est_t *e)
{
    if (!e) return 0;
    uint64_t h = e->stream_total;
    for (uint32_t i = 0; i < e->n; i++) h -= e->paid[i];
    return h;
}

est_status_t est_contest(est_t *e, uint32_t heir, uint32_t who, const uint8_t *sig,
                         uint32_t sig_len)
{
    if (!e || !sig) return EST_ERR_ARG;
    const uint8_t *key = finder(e, who);
    if (!key || heir >= e->n) return EST_ERR_ARG;
    if (e->phase == EST_CLOSED) return EST_ERR_STATE;
    if (e->contested[heir]) return EST_DUP;
    if (!signed_by(e, key, EST_MSG_CONTEST, est_contest_value(e, heir), sig, sig_len))
        return EST_ERR_SIG;
    e->contested[heir] = true;
    e->ccount[heir]++;
    return EST_OK;
}

est_status_t est_resolve(est_t *e, uint32_t heir, const uint8_t *sig, uint32_t sig_len,
                         uint64_t *released)
{
    if (!e || !sig || !released || heir >= e->n) return EST_ERR_ARG;
    if (!e->contested[heir]) return EST_ERR_STATE;
    if (!signed_by(e, e->cfg.court, EST_MSG_RESOLVE, est_contest_value(e, heir), sig, sig_len))
        return EST_ERR_SIG;
    *released = e->escrow[heir];
    e->escrow_total -= e->escrow[heir];
    e->escrow[heir] = 0;
    e->contested[heir] = false;
    e->ccount[heir]++;
    return EST_OK;
}

bool est_key_release_ok(const est_t *e, uint64_t tick)
{
    if (!e || (e->phase != EST_DISTRIBUTING && e->phase != EST_CLOSED)) return false;
    uint64_t due;
    return !__builtin_add_overflow(e->confirmed, e->cfg.release_ticks, &due) && tick >= due;
}

/* ===== E5 forced heirship ===== */

void est_forced_init(est_forced_table_t *t)
{
    if (!t) return;
    pay_memset(t, 0, sizeof *t);
    /* France, Code civil art. 913 */
    t->row[0] = (est_forced_row_t){EST_JUR('F', 'R'), 3, {{1, 2}, {2, 3}, {3, 4}}};
    t->n = 1;
}

const est_forced_row_t *est_forced_find(const est_forced_table_t *t, uint16_t jur)
{
    if (!t) return 0;
    for (uint32_t i = 0; i < t->n; i++)
        if (t->row[i].jur == jur) return &t->row[i];
    return 0;
}

est_status_t est_forced_register(est_forced_table_t *t, const est_forced_row_t *row)
{
    if (!t || !row || row->n_steps == 0 || row->n_steps > EST_FORCED_STEPS) return EST_ERR_ARG;
    for (uint32_t i = 0; i < row->n_steps; i++)
        if (row->reserve[i].den == 0 || row->reserve[i].num > row->reserve[i].den)
            return EST_ERR_ARG;
    if (est_forced_find(t, row->jur)) return EST_ERR_ARG;
    if (t->n >= EST_MAX_JUR) return EST_ERR_FULL;
    t->row[t->n++] = *row;
    return EST_OK;
}

/* *out = a * b, false on overflow */
static bool mul(uint64_t a, uint64_t b, uint64_t *out)
{
    return !__builtin_mul_overflow(a, b, out);
}

est_status_t est_forced_shares(const est_forced_row_t *row, uint32_t n_children, uint32_t n_gifts,
                               const uint64_t gift_num[], uint64_t will_den, est_forced_out_t *out)
{
    uint32_t c = n_children, n = n_children + n_gifts;
    if (!row || !out || (n_gifts && !gift_num) || will_den == 0 || n == 0 || n > EST_MAX_HEIRS ||
        n < n_children)
        return EST_ERR_ARG;
    uint64_t A = 0, W = will_den, lhs, rhs, rn, rd, child, gmul, den;
    bool cut = false;
    for (uint32_t j = 0; j < n_gifts; j++)
        if (__builtin_add_overflow(A, gift_num[j], &A)) return EST_ERR_OVERFLOW;
    if (A > W || (c == 0 && A != W)) return EST_ERR_SUM;
    if (c) {
        if (row->n_steps == 0 || row->n_steps > EST_FORCED_STEPS) return EST_ERR_ARG;
        const est_frac_t *r = &row->reserve[c - 1 < row->n_steps ? c - 1 : row->n_steps - 1];
        if (r->den == 0 || r->num > r->den) return EST_ERR_ARG;
        rn = r->num;
        rd = r->den;
        /* the gifts exceed the disposable quota: A / W > (rd - rn) / rd */
        if (!mul(A, rd, &lhs) || !mul(rd - rn, W, &rhs)) return EST_ERR_OVERFLOW;
        cut = lhs > rhs;
    }
    pay_memset(out, 0, sizeof *out);
    if (cut) {
        /* gift j: a_j (rd - rn) c / (rd A c); each child rn A / (rd A c);
         * rn A <= rd A = lhs fits */
        out->reduced = true;
        child = rn * A;
        if (!mul(rd - rn, c, &gmul) || !mul(lhs, c, &den)) return EST_ERR_OVERFLOW;
    } else {
        /* gift j: a_j c / (W c); each child (W - A) / (W c); no child: W */
        child = W - A;
        gmul = c ? c : 1;
        if (!mul(W, gmul, &den)) return EST_ERR_OVERFLOW;
    }
    /* a gift a_j gmul <= A gmul <= den, which fits */
    uint64_t g = den;
    for (uint32_t i = 0; i < n; i++) {
        out->num[i] = i < c ? child : gift_num[i - c] * gmul;
        g = gcd64(g, out->num[i]);
    }
    for (uint32_t i = 0; i < n; i++) out->num[i] /= g;
    out->den = den / g;
    out->n = n;
    out->advisory = true;
    return EST_OK;
}

est_status_t est_set_forced(est_t *e, const est_forced_table_t *t, uint16_t jur,
                            uint32_t n_children, const uint32_t child_id[], uint32_t n_gifts,
                            const uint32_t gift_id[], const uint64_t gift_num[], uint64_t will_den)
{
    if (!e || (n_children && !child_id) || (n_gifts && !gift_id)) return EST_ERR_ARG;
    const est_forced_row_t *row = est_forced_find(t, jur);
    if (!row) return EST_ERR_UNKNOWN;
    est_forced_out_t o;
    est_status_t st = est_forced_shares(row, n_children, n_gifts, gift_num, will_den, &o);
    if (st != EST_OK) return st;
    uint32_t id[EST_MAX_HEIRS];
    uint64_t den[EST_MAX_HEIRS];
    for (uint32_t i = 0; i < o.n; i++) {
        id[i] = i < n_children ? child_id[i] : gift_id[i - n_children];
        den[i] = o.den;
    }
    return est_set_manifest(e, EST_FORCED, o.n, id, o.num, den);
}

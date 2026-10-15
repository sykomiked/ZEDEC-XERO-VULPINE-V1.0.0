/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_paradox.c — the paradox engine (paradox/paradox.c, paradox_graph.c)
 * driven by API sequences: classifier, the four operators with small,
 * fuzzed log capacities (so every LOG_FULL path runs), a RepairProof
 * verifier written from the documented layout, and the complex-paradox
 * graph loaded twice in different orders.
 *
 * Byte 0: bit 0 picks the mode (0 operators, 1 graph); the rest is the
 * op stream / graph description.
 *
 * Invariants:
 *   X1  the operator log and the quarantine log verify after every call;
 *       each call adds exactly the records the header documents (one; two
 *       for a special whose frontier is the log) or none on LOG_FULL
 *   X2  classify keeps base and phase, keeps forbidden, gives NONE exactly
 *       for True/False, and a registry hit decides a Both/Neither
 *   X3  constructive matches a reference model of (val, prev, armed,
 *       tension_at); live values never leave [lo, hi] once applied; a
 *       REFUSED / PROPOSAL / LOG_FULL call changes nothing
 *   X4  destructive: CONTAINED iff (trips or destructive) and room; then the
 *       state equals the checkpoint, the quarantine record and the operator
 *       record bind proof.what, and the RepairProof verifies with the
 *       reference verifier; flipping any one bit of the proof makes it fail
 *       verification (a tampered proof never verifies); REFUSED leaves the
 *       state alone; LOG_FULL still restores the state
 *   X5  neutral: settles only on a decisive candidate or a valid owner
 *       pick, and once settled the choice never changes
 *   X6  registry is append-only: existing entries never change; review
 *       accepts only with the owner (money/device drafts) or owner or
 *       consensus (others), and a tampered draft (record hash or signature)
 *       is refused; so is a draft whose kind, reason or flags changed after
 *       pdx_draft, unless the log holds a DRAFT record committing to exactly
 *       the altered fields (checked with a reference of the documented
 *       draft binding)
 *   X7  graph: the same nodes and updates in any order give the same
 *       states, tension and chain after every pass; contained, rolled back
 *       and tainted are sticky; every dependant of contained work is
 *       tainted; a node newly applied in a pass has no contained, tainted,
 *       sandboxed, waiting or held node among its dependencies */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_in.h"
#include "paradox.h"
#include "sha256.h"

/* ---------------- reference hashes (from the documented layouts) ---------------- */
static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void ref_state(const uint8_t *s, uint32_t len, uint8_t out[32])
{
    sha256_ctx_t c;
    uint8_t l[4];
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/pdx-state/v1", 16);
    le32(l, len);
    sha256_update(&c, l, 4);
    sha256_update(&c, s, len);
    sha256_final(&c, out);
}
static void ref_sig(const uint8_t *d, uint32_t len, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/pdx-sig/v1", 14);
    if (d) sha256_update(&c, d, len);
    sha256_final(&c, out);
}

/* SHA-256 of "zxv/pdx-draft/v1" || rec || rec hash || sig || kind || reason || flags */
static void ref_draft_bind(const pdx_draft_t *d, uint8_t out[32])
{
    uint8_t buf[16 + 4 + 64 + 3];
    memcpy(buf, "zxv/pdx-draft/v1", 16);
    le32(buf + 16, d->rec);
    memcpy(buf + 20, d->rec_hash, 32);
    memcpy(buf + 52, d->sig, 32);
    buf[84] = d->kind;
    buf[85] = d->reason;
    buf[86] = d->flags;
    sha256(buf, sizeof buf, out);
}
/* Does log hold a DRAFT record committing to exactly *d? */
static bool ref_draft_logged(const pdx_log_t *log, const pdx_draft_t *d)
{
    uint8_t b[32];
    ref_draft_bind(d, b);
    for (uint32_t k = 0; k < log->n; k++) {
        const pdx_rec_t *r = &log->rec[k];
        if (r->op == PDX_OP_DRAFT && r->outcome == PDX_OUT_RECORDED && r->aux == d->rec &&
            r->kind == d->kind && !memcmp(r->a, d->rec_hash, 32) && !memcmp(r->b, b, 32))
            return true;
    }
    return false;
}

typedef struct {
    uint32_t spent, trace_n, submitter, len;
    const uint8_t *orig, *ckpt, *pattern;
    uint32_t plen;
} repair_ctx_t;

/* The verifier a peer with the context would run. */
static bool verify_repair(const pdx_repair_t *p, const repair_ctx_t *x, const lpx_qlog_t *q,
                          const pdx_log_t *log, uint32_t logidx)
{
    uint8_t before[32], after[32], sig[32], what[32], buf[17 + 24 + 96];
    ref_state(x->orig, x->len, before);
    ref_state(x->ckpt, x->len, after);
    ref_sig(x->pattern, x->plen, sig);
    if (memcmp(p->before, before, 32) || memcmp(p->after, after, 32) || memcmp(p->sig, sig, 32))
        return false;
    memcpy(buf, "zxv/pdx-repair/v1", 17);
    le32(buf + 17, p->trips);
    le32(buf + 21, p->steps);
    le32(buf + 25, x->spent);
    le32(buf + 29, x->trace_n);
    le32(buf + 33, x->submitter);
    le32(buf + 37, x->len);
    memcpy(buf + 41, p->before, 32);
    memcpy(buf + 73, p->after, 32);
    memcpy(buf + 105, p->sig, 32);
    sha256(buf, sizeof buf, what);
    if (memcmp(what, p->what, 32) || p->trips > 31u) return false;
    if (p->qseq >= q->n) return false;
    const lpx_qrec_t *r = &q->rec[p->qseq];
    if (memcmp(r->subject, p->what, 32) || r->reason != (uint8_t) p->trips ||
        r->submitter != x->submitter || !lpx_forbidden(r->tag))
        return false;
    if (logidx >= log->n) return false;
    const pdx_rec_t *o = &log->rec[logidx];
    if (o->op != PDX_OP_DESTRUCTIVE || o->outcome != PDX_OUT_CONTAINED || o->aux != p->steps ||
        memcmp(o->a, p->what, 32) || memcmp(o->b, p->sig, 32))
        return false;
    return true;
}

/* ---------------- operator state ---------------- */
static pdx_rec_t g_rec[64], g_frec[16];
static pdx_log_t g_log, g_front;
static lpx_qrec_t g_qrec[16];
static lpx_qlog_t g_q;
static pdx_sig_t g_sigs[8];
static pdx_registry_t g_reg;
#define NP 4
static int64_t g_val[NP], g_prev[NP], g_lo[NP], g_hi[NP];
static pdx_params_t g_par;
/* the reference model */
static int64_t m_val[NP], m_prev[NP];
static bool m_armed;
static uint32_t m_tension_at;
static pdx_branch_t g_br[6];
static pdx_hold_t g_hold;
static pdx_draft_t g_draft;
static bool g_have_draft;

static void check_logs(void)
{
    if (!pdx_log_verify(&g_log) || !pdx_log_verify(&g_front) || !lpx_qlog_verify(&g_q)) abort();
}

static pdx_class_t mk_class(fz_in *in)
{
    pdx_class_t c;
    uint8_t b = fz_u8(in);
    c.kind = (uint8_t) (b % 5u);
    c.forbidden = (b & 0x80) != 0;
    c.tag = (lpx_t) (fz_u8(in) & 63u);
    if (c.forbidden) c.tag = lpx_forbid(c.tag);
    c.reg_index = -1;
    return c;
}

static void op_classify(fz_in *in)
{
    lpx_t tag = fz_u8(in); /* any byte: the classifier must mask it */
    pdx_evidence_t ev;
    ev.pos0 = fz_u8(in);
    ev.neg0 = fz_u8(in);
    ev.pos1 = fz_u8(in);
    ev.neg1 = fz_u8(in);
    ev.stale = (fz_u8(in) & 1) != 0;
    uint8_t sel = fz_u8(in);
    const uint8_t *sig = NULL;
    uint8_t s[32];
    if ((sel & 3) == 1 && g_reg.n) sig = g_reg.ent[sel % g_reg.n].sig;
    if ((sel & 3) == 2) {
        fz_bytes(in, s, 32);
        sig = s;
    }
    pdx_class_t c = pdx_classify(tag, (sel & 4) ? NULL : &ev, &g_reg, sig);
    lpx_t t = (lpx_t) (tag & 63u);
    if (lpx_base(c.tag) != lpx_base(t) || lpx_phase(c.tag) != lpx_phase(t)) abort();
    if (c.forbidden != lpx_forbidden(t) || (c.forbidden && !lpx_forbidden(c.tag))) abort();
    bool bn = lpx_base(t) == LPX_B_BOTH || lpx_base(t) == LPX_B_NEITHER;
    if ((c.kind == PDX_KIND_NONE) != !bn) abort();
    if (c.reg_index >= 0) {
        if (!bn || (uint32_t) c.reg_index >= g_reg.n || c.kind != g_reg.ent[c.reg_index].kind)
            abort();
    } else if (bn && sig && pdx_reg_find(&g_reg, sig) >= 0) {
        abort(); /* a known signature must decide */
    }
    if (c.kind > PDX_KIND_NONE) abort();
}

static void op_constructive(fz_in *in)
{
    pdx_class_t c = mk_class(in);
    int64_t synth[NP];
    bool has = (fz_u8(in) & 3) != 0;
    for (int i = 0; i < NP; i++) synth[i] = (int64_t) fz_edge64(in);
    uint32_t tension = fz_u8(in);
    uint32_t n0 = g_log.n;
    int64_t v0[NP];
    memcpy(v0, g_val, sizeof v0);
    pdx_out_t o = pdx_constructive(&g_par, has ? synth : NULL, tension, &c, &g_log);
    /* the reference */
    pdx_out_t want;
    bool restore = false, apply = false;
    if (c.forbidden) {
        want = PDX_OUT_ROLLED_BACK;
        restore = m_armed;
    } else if (c.kind != PDX_KIND_CONSTRUCTIVE) {
        want = PDX_OUT_REFUSED;
    } else if (m_armed && tension > m_tension_at) {
        want = PDX_OUT_ROLLED_BACK;
        restore = true;
    } else if (!has) {
        want = PDX_OUT_KEPT;
    } else {
        bool inb = true;
        for (int i = 0; i < NP; i++)
            if (g_lo[i] > g_hi[i] || synth[i] < g_lo[i] || synth[i] > g_hi[i]) inb = false;
        want = inb ? PDX_OUT_APPLIED : PDX_OUT_PROPOSAL_FOR_OWNER;
        apply = inb;
    }
    if (n0 >= g_log.cap) {
        if (o != PDX_OUT_LOG_FULL || g_log.n != n0 || memcmp(v0, g_val, sizeof v0)) abort();
        return;
    }
    if (o != want || g_log.n != n0 + 1) abort();
    if (want != PDX_OUT_REFUSED) {
        if (restore) memcpy(m_val, m_prev, sizeof m_val);
        if (apply) {
            memcpy(m_prev, m_val, sizeof m_prev);
            memcpy(m_val, synth, sizeof m_val);
            m_tension_at = tension;
        }
        m_armed = apply;
    }
    if (memcmp(m_val, g_val, sizeof m_val) || m_armed != g_par.armed) abort();
    if (m_armed && (memcmp(m_prev, g_prev, sizeof m_prev) || g_par.tension_at != m_tension_at))
        abort();
    if (o == PDX_OUT_APPLIED)
        for (int i = 0; i < NP; i++)
            if (g_val[i] < g_lo[i] || g_val[i] > g_hi[i]) abort();
}

static bool inv_byte0(const uint8_t *s, uint32_t len, void *ctx)
{
    (void) ctx;
    return len == 0 || s[0] != 0xEE;
}

static void op_destructive(fz_in *in)
{
    pdx_class_t c = mk_class(in);
    uint8_t len = fz_u8(in) % 48u;
    uint8_t state[48], ck[48], orig[48];
    fz_bytes(in, state, len);
    fz_bytes(in, ck, len);
    memcpy(orig, state, len);
    uint64_t trace[6];
    pdx_watch_t w;
    memset(&w, 0, sizeof w);
    uint8_t f = fz_u8(in);
    w.steps = fz_u8(in);
    w.step_limit = (f & 1) ? fz_u8(in) : 0;
    w.spent = fz_u8(in);
    w.budget = (f & 2) ? fz_u8(in) : 0;
    w.trace_n = fz_u8(in) % 7u;
    for (uint32_t i = 0; i < w.trace_n && i < 6; i++) trace[i] = fz_u8(in) % 8u;
    if (w.trace_n > 6) w.trace_n = 6;
    w.trace = (f & 4) ? trace : NULL;
    w.invariant = (f & 8) ? inv_byte0 : NULL;
    uint8_t pattern[24];
    uint32_t plen = fz_u8(in) % 25u;
    fz_bytes(in, pattern, plen);
    uint32_t submitter = fz_u32(in);
    pdx_repair_t pr;
    memset(&pr, 0x5A, sizeof pr);
    uint32_t n0 = g_log.n, q0 = g_q.n;
    uint32_t trips = pdx_detect((f & 0x10) ? NULL : &w, state, len);
    pdx_out_t o = pdx_destructive(&c, (f & 0x10) ? NULL : &w, state, ck, len,
                                  plen ? pattern : NULL, plen, submitter, &g_q, &g_log, &pr);
    bool contain = trips || c.forbidden || c.kind == PDX_KIND_DESTRUCTIVE;
    if (!contain) {
        if (memcmp(state, orig, len) || g_q.n != q0) abort();
        if (n0 < g_log.cap ? (o != PDX_OUT_REFUSED || g_log.n != n0 + 1)
                           : (o != PDX_OUT_LOG_FULL || g_log.n != n0))
            abort();
        return;
    }
    if (memcmp(state, ck, len)) abort(); /* rolled back, even on LOG_FULL */
    if (n0 >= g_log.cap || q0 >= g_q.cap) {
        if (o != PDX_OUT_LOG_FULL || g_log.n != n0 || g_q.n != q0) abort();
        return;
    }
    if (o != PDX_OUT_CONTAINED || g_log.n != n0 + 1 || g_q.n != q0 + 1) abort();
    repair_ctx_t x = {(f & 0x10) ? 0 : w.spent, (f & 0x10) ? 0 : w.trace_n, submitter, len,
                      orig,  ck,  plen ? pattern : NULL, plen};
    if (!verify_repair(&pr, &x, &g_q, &g_log, n0)) abort(); /* X4: a genuine proof verifies */
    /* X4: any single-bit tamper fails */
    pdx_repair_t t = pr;
    uint8_t *tb = (uint8_t *) &t;
    uint32_t bit = fz_u16(in) % (uint32_t) (sizeof t * 8u);
    tb[bit / 8u] ^= (uint8_t) (1u << (bit % 8u));
    bool same_q = false;
    if (t.qseq != pr.qseq && t.qseq < g_q.n) /* another record with identical content */
        same_q = !memcmp(g_q.rec[t.qseq].subject, pr.what, 32) &&
                 g_q.rec[t.qseq].submitter == submitter &&
                 g_q.rec[t.qseq].reason == (uint8_t) pr.trips && lpx_forbidden(g_q.rec[t.qseq].tag);
    if (!same_q && verify_repair(&t, &x, &g_q, &g_log, n0)) abort();
    /* tampering with the context (another submitter or length) also fails */
    repair_ctx_t y = x;
    y.submitter ^= 1u << (fz_u8(in) % 32u);
    if (verify_repair(&pr, &y, &g_q, &g_log, n0)) abort();
}

static void op_neutral(fz_in *in)
{
    uint8_t a = fz_u8(in);
    uint8_t id[32] = {0};
    id[0] = (uint8_t) (a % 7u);
    int32_t settled0 = g_hold.settled;
    uint32_t n0 = g_hold.n;
    switch ((a >> 3) & 3) {
    case 0: {
        bool ok = pdx_hold_add(&g_hold, id);
        if (settled0 >= 0 && ok) abort();
        if (g_hold.n < n0) abort();
        break;
    }
    case 1: case 2: {
        uint32_t i;
        uint32_t before = 0;
        for (i = 0; i < g_hold.n; i++)
            if (!memcmp(g_br[i].id, id, 32)) break;
        if (i < g_hold.n) before = (a & 0x80) ? g_br[i].pos : g_br[i].neg;
        bool ok = pdx_hold_evidence(&g_hold, id, (a & 0x80) != 0);
        if (ok && (settled0 >= 0 || i >= g_hold.n)) abort();
        if (ok && before != UINT32_MAX && ((a & 0x80) ? g_br[i].pos : g_br[i].neg) != before + 1)
            abort();
        break;
    }
    default: {
        pdx_class_t c = mk_class(in);
        int32_t pick = (int32_t) (fz_u8(in) % 9u) - 1;
        int32_t dec = pdx_hold_decisive(&g_hold);
        uint32_t l0 = g_log.n;
        pdx_out_t o = pdx_neutral(&g_hold, &c, pick, &g_log);
        if (l0 >= g_log.cap) {
            if (o != PDX_OUT_LOG_FULL || g_hold.settled != settled0) abort();
            break;
        }
        if (g_log.n != l0 + 1) abort();
        if (c.forbidden) {
            if (o != PDX_OUT_ROLLED_BACK || g_hold.settled != settled0) abort();
        } else if (c.kind != PDX_KIND_NEUTRAL) {
            if (o != PDX_OUT_REFUSED || g_hold.settled != settled0) abort();
        } else {
            int32_t want = settled0 >= 0                              ? settled0
                           : (pick >= 0 && (uint32_t) pick < g_hold.n) ? pick
                                                                       : dec;
            if ((o == PDX_OUT_SETTLED) != (want >= 0) || g_hold.settled != want) abort();
            if (want < 0 && o != PDX_OUT_DEFERRED) abort();
        }
        break;
    }
    }
    if (settled0 >= 0 && g_hold.settled != settled0) abort(); /* X5 */
}

static void op_special(fz_in *in)
{
    pdx_sig_t snap[8];
    uint32_t rn = g_reg.n;
    memcpy(snap, g_sigs, sizeof snap);
    uint8_t a = fz_u8(in);
    switch (a & 3) {
    case 0: { /* frontier record */
        pdx_class_t c = mk_class(in);
        uint8_t sig[32] = {0}, subj[32] = {0};
        sig[0] = fz_u8(in) % 6u;
        subj[0] = fz_u8(in);
        uint32_t rec = 0xFFFFFFFFu;
        bool same = (a & 4) != 0;
        uint32_t f0 = g_front.n, l0 = g_log.n;
        pdx_out_t o = pdx_special(same ? &g_log : &g_front, &c, sig, subj, &g_log, &rec);
        if (o == PDX_OUT_RECORDED) {
            if (c.forbidden || c.kind != PDX_KIND_SPECIAL) abort();
            if (same ? g_log.n != l0 + 2 : (g_front.n != f0 + 1 || g_log.n != l0 + 1)) abort();
        } else if (o == PDX_OUT_LOG_FULL) {
            if (g_front.n != f0 || g_log.n != l0) abort();
        } else if (g_front.n != f0 || g_log.n != l0 + 1) {
            abort();
        }
        break;
    }
    case 1: { /* draft */
        uint32_t rec = fz_u8(in) % 20u;
        uint8_t kind = fz_u8(in) % 6u;
        uint8_t reason = fz_u8(in), flags = fz_u8(in) % 4u;
        pdx_out_t o = pdx_draft(&g_front, rec, kind, reason, flags, &g_draft, &g_log);
        if (o == PDX_OUT_RECORDED) {
            if (rec >= g_front.n || kind == PDX_KIND_SPECIAL || kind >= PDX_KIND_NONE) abort();
            if (!ref_draft_logged(&g_log, &g_draft)) abort(); /* the record binds the draft */
            g_have_draft = true;
        }
        break;
    }
    default: { /* review, maybe of a tampered draft */
        if (!g_have_draft) break;
        pdx_draft_t d = g_draft;
        uint8_t t = fz_u8(in);
        bool tampered = false;
        if ((t & 3) == 1) {
            d.rec_hash[fz_u8(in) % 32u] ^= (uint8_t) (1u + (t >> 2));
            tampered = true;
        } else if ((t & 3) == 2) {
            d.sig[fz_u8(in) % 32u] ^= (uint8_t) (1u + (t >> 2));
            tampered = true;
        } else if ((t & 3) == 3) { /* kind, reason or flags changed after pdx_draft */
            uint8_t x = (uint8_t) (1u + (t >> 4));
            uint8_t w = (uint8_t) ((t >> 2) & 3u);
            if (w == 0)
                d.flags ^= x;
            else if (w == 1)
                d.kind ^= x;
            else if (w == 2)
                d.reason ^= x;
            else
                d.flags &= (uint8_t) ~PDX_DRAFT_MONEY;
            /* an alteration that matches another recorded draft is that draft */
            tampered = !ref_draft_logged(&g_log, &d);
        }
        bool owner = (a & 4) != 0, cons = (a & 8) != 0;
        bool needs_owner = (d.flags & (PDX_DRAFT_MONEY | PDX_DRAFT_DEVICE)) != 0;
        bool known = pdx_reg_find(&g_reg, d.sig) >= 0;
        pdx_out_t o = pdx_review(&g_reg, &g_front, &d, owner, cons, &g_log);
        if (tampered && o != PDX_OUT_REFUSED) abort(); /* X6 */
        if (o == PDX_OUT_ACCEPTED) {
            if (!(owner || (!needs_owner && cons))) abort();
            if (known || g_reg.n != rn + 1 || memcmp(g_reg.ent[rn].sig, d.sig, 32)) abort();
        } else if (g_reg.n != rn) {
            abort();
        }
        break;
    }
    }
    if (g_reg.n < rn || memcmp(snap, g_sigs, rn * sizeof snap[0])) abort(); /* append-only */
}

static void mode_ops(fz_in *in)
{
    uint8_t caps = fz_u8(in);
    pdx_log_init(&g_log, g_rec, 4u + (caps & 63u) % 61u);
    pdx_log_init(&g_front, g_frec, 1u + (caps >> 6) * 5u);
    lpx_qlog_init(&g_q, g_qrec, 1u + fz_u8(in) % 16u);
    pdx_reg_init(&g_reg, g_sigs, 1u + fz_u8(in) % 8u);
    memset(g_sigs, 0, sizeof g_sigs);
    for (uint32_t k = 0; k < 2; k++) {
        uint8_t s[32] = {0};
        s[0] = (uint8_t) k;
        (void) pdx_reg_add(&g_reg, s, (uint8_t) (k ? PDX_KIND_DESTRUCTIVE : PDX_KIND_NEUTRAL), 0, 0,
                           PDX_SRC_SEED);
    }
    for (int i = 0; i < NP; i++) {
        g_lo[i] = (int64_t) fz_edge64(in);
        g_hi[i] = (int64_t) fz_edge64(in);
        g_val[i] = m_val[i] = g_prev[i] = m_prev[i] = 0;
    }
    g_par.val = g_val;
    g_par.prev = g_prev;
    g_par.lo = g_lo;
    g_par.hi = g_hi;
    g_par.n = NP;
    g_par.armed = m_armed = false;
    g_par.tension_at = m_tension_at = 0;
    pdx_hold_init(&g_hold, g_br, 1u + fz_u8(in) % 6u, fz_u8(in) % 3u);
    g_have_draft = false;
    for (int k = 0; k < 96 && in->n; k++) {
        switch (fz_u8(in) % 6u) {
        case 0: op_classify(in); break;
        case 1: op_constructive(in); break;
        case 2: op_destructive(in); break;
        case 3: op_neutral(in); break;
        default: op_special(in); break;
        }
        check_logs();
    }
}

/* ---------------- graph ---------------- */
static pdx_node_t g_n1[PDX_GRAPH_MAX], g_n2[PDX_GRAPH_MAX];
static pdx_graph_t G1, G2;
static pdx_rec_t g_l1[80], g_l2[80];

static void node_id(uint32_t k, uint8_t id[32])
{
    memset(id, 0, 32);
    id[0] = (uint8_t) (k * 37u + 11u); /* not in index order */
    id[1] = (uint8_t) k;
}

static uint64_t deps_closure(const pdx_graph_t *g, uint32_t p)
{
    uint64_t r = g->dep[p], prev = 0;
    while (r != prev) {
        prev = r;
        for (uint32_t q = 0; q < g->n; q++)
            if (r >> q & 1u) r |= g->dep[q];
    }
    return r;
}

static bool is_sticky(uint8_t s)
{
    return s == PDX_NS_CONTAINED || s == PDX_NS_ROLLED_BACK || s == PDX_NS_TAINTED;
}

static void mode_graph(fz_in *in)
{
    uint32_t n = 1u + fz_u8(in) % PDX_GRAPH_MAX;
    memset(g_n1, 0, sizeof g_n1);
    for (uint32_t k = 0; k < n; k++) {
        pdx_node_t *x = &g_n1[k];
        node_id(k, x->id);
        uint8_t b = fz_u8(in);
        x->kind = (uint8_t) (b % 5u);
        x->forbidden = (b & 0xF0) == 0xF0;
        x->t.mag = fz_u8(in);
        x->t.dir = (int8_t) ((int) (fz_u8(in) % 3u) - 1);
        x->ndeps = (uint8_t) (fz_u8(in) % (PDX_MAX_DEPS + 1u));
        for (uint32_t d = 0; d < x->ndeps; d++) node_id(fz_u8(in) % n, x->deps[d]);
    }
    /* a permuted copy */
    uint8_t perm[PDX_GRAPH_MAX];
    for (uint32_t k = 0; k < n; k++) perm[k] = (uint8_t) k;
    for (uint32_t k = n - 1; k > 0; k--) {
        uint32_t j = fz_u8(in) % (k + 1u);
        uint8_t t = perm[k];
        perm[k] = perm[j];
        perm[j] = t;
    }
    for (uint32_t k = 0; k < n; k++) g_n2[k] = g_n1[perm[k]];
    uint32_t limit = 1u + fz_u8(in) % 8u;
    bool l1 = pdx_graph_load(&G1, g_n1, n, limit), l2 = pdx_graph_load(&G2, g_n2, n, limit);
    if (l1 != l2) abort();
    if (!l1) return;
    pdx_log_t L1, L2;
    pdx_log_init(&L1, g_l1, 80);
    pdx_log_init(&L2, g_l2, 80);
    for (int round = 0; round < 10 && in->n; round++) {
        /* a batch of updates, applied in two orders */
        pdx_update_t u[8], v[8];
        uint32_t nu = fz_u8(in) % 9u;
        for (uint32_t i = 0; i < nu; i++) {
            uint8_t b = fz_u8(in);
            node_id((b & 0x80) ? n + fz_u8(in) % 3u : fz_u8(in) % n, u[i].id); /* sometimes unknown */
            u[i].kind = (uint8_t) (fz_u8(in) % 5u);
            u[i].forbidden = (b & 0x40) && (b & 0x20);
            u[i].t.mag = fz_u8(in);
            u[i].t.dir = (int8_t) ((int) (b % 3u) - 1);
        }
        for (uint32_t i = 0; i < nu; i++) v[i] = u[nu - 1u - i];
        uint8_t st0[PDX_GRAPH_MAX];
        memcpy(st0, G1.st, sizeof st0);
        bool r1 = pdx_graph_update(&G1, u, nu), r2 = pdx_graph_update(&G2, v, nu);
        if (r1 != r2) abort();
        pdx_out_t o1 = pdx_graph_pass(&G1, &L1), o2 = pdx_graph_pass(&G2, &L2);
        if (o1 != o2) abort();
        if (memcmp(G1.chain, G2.chain, 32) || memcmp(G1.st, G2.st, n) ||
            memcmp(G1.kind, G2.kind, n) || G1.total.mag != G2.total.mag ||
            G1.total.dir != G2.total.dir)
            abort(); /* X7: order-free */
        if (!pdx_log_verify(&L1) || !pdx_log_verify(&L2)) abort();
        if (o1 == PDX_OUT_LOG_FULL) break;
        for (uint32_t p = 0; p < n; p++) {
            if (is_sticky(st0[p]) && G1.st[p] != st0[p]) abort(); /* sticky */
            uint64_t dc = deps_closure(&G1, p);
            for (uint32_t q = 0; q < n; q++) {
                if (!(dc >> q & 1u)) continue;
                uint8_t s = G1.st[q];
                if ((s == PDX_NS_CONTAINED || s == PDX_NS_ROLLED_BACK || s == PDX_NS_TAINTED) &&
                    !is_sticky(G1.st[p]))
                    abort(); /* taint reaches every dependant */
                if (G1.st[p] == PDX_NS_APPLIED && st0[p] != PDX_NS_APPLIED &&
                    (s == PDX_NS_SANDBOXED || s == PDX_NS_WAITING || s == PDX_NS_HELD))
                    abort(); /* applied only when nothing unresolved reaches it */
            }
            if (G1.st[p] == PDX_NS_APPLIED && st0[p] != PDX_NS_APPLIED &&
                G1.kind[p] != PDX_KIND_CONSTRUCTIVE)
                abort();
        }
        if (o1 == PDX_OUT_RESOLVED)
            for (uint32_t p = 0; p < n; p++)
                if (G1.st[p] != PDX_NS_CLEAR && G1.st[p] != PDX_NS_APPLIED && !is_sticky(G1.st[p]))
                    abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 8192) return 0;
    fz_in in;
    fz_init(&in, data + 1, size - 1);
    if (data[0] & 1)
        mode_graph(&in);
    else
        mode_ops(&in);
    return 0;
}

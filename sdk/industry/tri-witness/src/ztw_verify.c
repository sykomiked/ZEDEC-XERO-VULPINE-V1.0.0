/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztw_verify.c — Tri-Witness: the replay. Every record of a Sutra-Clear
 * chain export is checked on its own; every clearing-cycle record is
 * re-netted, re-feed, re-posted on paper and re-rendered, and compared with
 * what the house recorded and with the ledger journal. Each discrepancy is
 * one Forbidden finding in a signed report. Nothing here can post: the
 * witness refuses a capability that holds LEDGER_POST. Integer code only
 * (R11). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ztw_internal.h"

#define REPORT_DOMAIN  "tri-witness/report"
#define OPERATOR_CTX   "sutra-clear/operator"
#define DECISION_TAG   "sutra-clear/decision/v1"
#define MAX_TRUST      8u
#define MAX_LISTED     ZTW_MAX_LISTED
#define SETTLED_CAP    (1u << 18)
#define BATCH_PARTIES  16u
#define BATCH_OWES     64u

static const char *const k_bucket[4] = {"reserve", "dividend", "infra", "regen"};

struct ztw_witness {
    const zxv_cap *cap;
    const zind_signer *signer;
    uint8_t house_pk[MAX_TRUST][ZIND_PK_BYTES], house_id[MAX_TRUST][32];
    uint32_t n_house;
    uint8_t op_pk[MAX_TRUST][ZIND_PK_BYTES], op_id[MAX_TRUST][32];
    uint32_t n_op;
    ztw_finding *f;
    uint32_t n_f, total_f;
    zind_chain *report;
};

const char *ztw_status_name(int32_t status) {
    switch (status) {
    case ZTW_E_DISCREPANCY: return "ZTW_E_DISCREPANCY";
    case ZTW_E_WRITABLE: return "ZTW_E_WRITABLE";
    case ZTW_E_NOTRUST: return "ZTW_E_NOTRUST";
    default: return zind_status_name(status);
    }
}

const char *ztw_finding_name(uint16_t code) {
    static const char *const n[] = {"?",          "IMPORT",      "LINK",      "SIGNATURE",
                                    "UNTRUSTED",  "KIND",        "FRAGMENT",  "FORMAT",
                                    "SEQUENCE",   "INSTRUCTION", "REPLAY",    "HELD",
                                    "DECISION",   "NETTING",     "CONSERVATION", "FEE",
                                    "SPLIT",      "CARRY",       "POSTING",   "JOURNAL",
                                    "BALANCE",    "JDIGEST",     "ISO",       "JFORMAT"};
    return code < sizeof n / sizeof n[0] ? n[code] : "?";
}

/* ===== witness ===== */

int32_t ztw_create(const zxv_cap *cap, const zind_signer *signer, ztw_witness **out) {
    uint64_t caps = 0;
    int32_t att = 0, revoked = 0, st;
    uint32_t pid = 0;
    ztw_witness *w;
    if (!cap || !signer || !out) return ZXV_E_ARG;
    *out = NULL;
    st = zxv_cap_info(cap, &caps, &pid, &att, &revoked);
    if (st != ZXV_OK) return st;
    if (caps & ZXV_CAP_LEDGER_POST) {
        zind_set_error("ztw_create: the cap can post to a ledger; a witness takes a read-only cap "
                       "(attenuate it without ZXV_CAP_LEDGER_POST)");
        return ZTW_E_WRITABLE;
    }
    st = zxv_cap_check(cap, ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) {
        zind_set_error("ztw_create: the cap needs LEDGER_READ and CRYPTO_SIGN");
        return st;
    }
    w = (ztw_witness *) calloc(1, sizeof *w);
    if (!w) return ZXV_E_NOMEM;
    w->cap = cap;
    w->signer = signer;
    *out = w;
    return ZXV_OK;
}

void ztw_destroy(ztw_witness *w) {
    if (!w) return;
    free(w->f);
    zind_chain_destroy(w->report);
    free(w);
}

static int32_t trust(uint8_t pks[][ZIND_PK_BYTES], uint8_t ids[][32], uint32_t *n,
                     const uint8_t *pk) {
    uint8_t id[32];
    int32_t st;
    if (!pk) return ZXV_E_ARG;
    st = zind_key_id(pk, id);
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < *n; i++)
        if (memcmp(ids[i], id, 32) == 0) return ZXV_E_DUPLICATE;
    if (*n >= MAX_TRUST) return ZXV_E_CAPACITY;
    memcpy(pks[*n], pk, ZIND_PK_BYTES);
    memcpy(ids[*n], id, 32);
    (*n)++;
    return ZXV_OK;
}

int32_t ztw_trust_house(ztw_witness *w, const uint8_t pk[ZIND_PK_BYTES]) {
    if (!w) return ZXV_E_ARG;
    return trust(w->house_pk, w->house_id, &w->n_house, pk);
}

int32_t ztw_trust_operator(ztw_witness *w, const uint8_t pk[ZIND_PK_BYTES]) {
    if (!w) return ZXV_E_ARG;
    return trust(w->op_pk, w->op_id, &w->n_op, pk);
}

int32_t ztw_finding_count(const ztw_witness *w, uint32_t *out_count) {
    if (!w || !out_count) return ZXV_E_ARG;
    *out_count = w->n_f;
    return ZXV_OK;
}

int32_t ztw_finding_at(const ztw_witness *w, uint32_t i, ztw_finding *out) {
    if (!w || !out) return ZXV_E_ARG;
    if (i >= w->n_f) return ZXV_E_NOT_FOUND;
    *out = w->f[i];
    return ZXV_OK;
}

const zind_chain *ztw_report(const ztw_witness *w) { return w ? w->report : NULL; }

/* ===== the replay's state ===== */

typedef struct {
    char code[12];
    char ccy[4];
    uint64_t rem;
} carry_t;

typedef struct {
    char e2e[36];
    uint32_t n;
    uint8_t dig[4][32];
} pheld_t;

typedef struct {
    ztw_witness *w;
    const zind_chain *ch;
    uint32_t n_rec;
    uint8_t forbid_tag;
    int32_t fatal;
    /* the journal */
    ztw_journal j;
    int have_j;
    uint8_t *jused;
    /* across cycles */
    uint32_t cycles_seen, prev_no;
    char house[9];
    uint32_t n_ccy, n_part;
    char ccy[ZTW_MAX_CCY][4];
    char (*part)[12];
    carry_t *carry;
    size_t n_carry, cap_carry;
    int lost; /* a cycle record could not be replayed: carries are unknown from then on */
    zind_seen *settled;
    pheld_t *ph;
    uint32_t n_ph;
    uint64_t prev_head, max_head;
    /* the current cycle */
    uint32_t cno, crec, cnrec;
    const ztw_fieldlog *log;
    const uint8_t *blob;
    ztw_summary sum;
} V;

static int add64(int64_t a, int64_t b, int64_t *o) { return !__builtin_add_overflow(a, b, o); }
static int addu64(uint64_t a, uint64_t b, uint64_t *o) { return !__builtin_add_overflow(a, b, o); }

/* x += d, saturating: a crafted journal can hold more than int64 can sum;
 * a saturated balance never matches and is reported, never wrapped. */
static void sat_add(int64_t *x, int64_t d) {
    if (__builtin_add_overflow(*x, d, x)) *x = d > 0 ? INT64_MAX : INT64_MIN + 1;
}

/* The chain record holding byte `off` of the current cycle record. */
static uint32_t frag_of(const V *v, size_t off) {
    size_t end = 0;
    for (uint32_t i = 0; i < v->cnrec; i++) {
        zind_record r;
        if (zind_chain_at(v->ch, v->crec + i, &r) != ZXV_OK || r.payload_len < 8) break;
        end += r.payload_len - 8;
        if (off < end) return v->crec + i;
    }
    return v->crec + (v->cnrec ? v->cnrec - 1 : 0);
}

/* Record a finding. rec/raw/subject come from the field (when one is
 * named and found in the current cycle record), else from `rec`. */
static void finding_v(V *v, uint16_t code, uint32_t cyc, uint32_t rec, uint32_t item,
                      const char *field, int64_t exp, int64_t got, const uint8_t *raw,
                      size_t raw_len, const char *fmt, va_list ap) {
    ztw_witness *w = v->w;
    ztw_finding *f;
    zind_record r;
    w->total_f++;
    if (w->n_f >= MAX_LISTED) return;
    if (!w->f) {
        w->f = (ztw_finding *) calloc(MAX_LISTED, sizeof *w->f);
        if (!w->f) {
            v->fatal = ZXV_E_NOMEM;
            return;
        }
    }
    f = &w->f[w->n_f++];
    memset(f, 0, sizeof *f);
    f->code = code;
    f->cycle_no = cyc;
    f->record = rec;
    f->item = item;
    f->expected = exp;
    f->found = got;
    f->tag = v->forbid_tag;
    if (field) snprintf(f->field, sizeof f->field, "%s", field);
    vsnprintf(f->detail, sizeof f->detail, fmt, ap);
    if (field && v->log && v->blob) {
        for (size_t i = 0; i < v->log->n; i++)
            if (strcmp(v->log->f[i].name, field) == 0) {
                const ztw_field *x = &v->log->f[i];
                raw = v->blob + x->offset;
                raw_len = x->len;
                f->record = frag_of(v, x->offset);
                break;
            }
    }
    if (raw && raw_len) {
        f->raw_len = (uint8_t) (raw_len > sizeof f->raw ? sizeof f->raw : raw_len);
        memcpy(f->raw, raw, f->raw_len);
    }
    if (f->record == UINT32_MAX) {
        if (v->have_j) memcpy(f->subject, v->sum.journal_digest, 32);
    } else if (v->ch && zind_chain_at(v->ch, f->record, &r) == ZXV_OK) {
        memcpy(f->subject, r.hash, 32);
    }
}

#if defined(__GNUC__) || defined(__clang__)
#define FMT(a, b) __attribute__((format(printf, a, b)))
#else
#define FMT(a, b)
#endif

/* A finding in the current cycle record. */
static void FMT(7, 8) fc(V *v, uint16_t code, uint32_t item, const char *field, int64_t exp,
                         int64_t got, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    finding_v(v, code, v->cno, v->crec, item, field, exp, got, NULL, 0, fmt, ap);
    va_end(ap);
}

/* A finding tied to a chain record or to the journal (rec UINT32_MAX). */
static void FMT(9, 10) fr(V *v, uint16_t code, uint32_t cyc, uint32_t rec, uint32_t item,
                          const char *field, const uint8_t *raw, size_t raw_len, const char *fmt,
                          ...) {
    va_list ap;
    const ztw_fieldlog *keep = v->log;
    va_start(ap, fmt);
    v->log = NULL;
    finding_v(v, code, cyc, rec, item, field, 0, 0, raw, raw_len, fmt, ap);
    v->log = keep;
    va_end(ap);
}

/* ===== field checks (an independent copy of the house's rules) ===== */

static int up(char c) { return c >= 'A' && c <= 'Z'; }
static int dig(char c) { return c >= '0' && c <= '9'; }

static int code_ok(const char *s) {
    size_t n;
    for (n = 0; s[n]; n++)
        if (n >= 11 || !(up(s[n]) || dig(s[n]))) return 0;
    return n >= 1;
}

static int house_ok(const char *s) {
    size_t n;
    for (n = 0; s[n]; n++)
        if (n >= 8 || !(up(s[n]) || dig(s[n]) || s[n] == '-')) return 0;
    return n >= 1;
}

static int text_ok(const char *s, size_t max) {
    size_t n;
    for (n = 0; s[n]; n++)
        if (n >= max || (unsigned char) s[n] < 0x21 || (unsigned char) s[n] > 0x7e) return 0;
    return n >= 1;
}

static int name_ok(const char *s) {
    const unsigned char *u = (const unsigned char *) s;
    size_t n = strlen(s), i = 0;
    if (n < 1 || n > 70 || s[0] == ' ' || s[n - 1] == ' ') return 0;
    while (i < n) {
        unsigned c = u[i];
        uint32_t cp;
        size_t k;
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f) return 0;
            i++;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf) k = 1, cp = c & 0x1f;
        else if (c >= 0xe0 && c <= 0xef) k = 2, cp = c & 0x0f;
        else if (c >= 0xf0 && c <= 0xf4) k = 3, cp = c & 0x07;
        else return 0;
        if (n - i <= k) return 0;
        for (size_t j = 1; j <= k; j++) {
            if ((u[i + j] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (u[i + j] & 0x3f);
        }
        if ((k == 2 && cp < 0x800) || (k == 3 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp <= 0x9f))
            return 0;
        i += k + 1;
    }
    return 1;
}

static int two(const char *s, int lo, int hi) {
    int x;
    if (!dig(s[0]) || !dig(s[1])) return 0;
    x = (s[0] - '0') * 10 + (s[1] - '0');
    return x >= lo && x <= hi;
}

static int dt_ok(const char *s) {
    size_t n = strlen(s), i = 19, f = 0;
    if (n < 19 || n > 32) return 0;
    for (int k = 0; k < 4; k++)
        if (!dig(s[k])) return 0;
    if (s[4] != '-' || !two(s + 5, 1, 12) || s[7] != '-' || !two(s + 8, 1, 31) || s[10] != 'T' ||
        !two(s + 11, 0, 23) || s[13] != ':' || !two(s + 14, 0, 59) || s[16] != ':' ||
        !two(s + 17, 0, 59))
        return 0;
    if (s[i] == '.') {
        i++;
        while (dig(s[i]) && f < 7) i++, f++;
        if (f < 1 || f > 6) return 0;
    }
    if (s[i] == 0) return 1;
    if (s[i] == 'Z') return s[i + 1] == 0;
    if (s[i] == '+' || s[i] == '-')
        return two(s + i + 1, 0, 14) && s[i + 3] == ':' && two(s + i + 4, 0, 59) && s[i + 6] == 0;
    return 0;
}

static int ccy_minor(const char *ccy) {
    uint8_t minor = 0;
    uint32_t flags = 0;
    if (strlen(ccy) != 3 || !up(ccy[0]) || !up(ccy[1]) || !up(ccy[2])) return -1;
    if (zxv_iso4217_by_alpha(ccy, NULL, &minor, &flags) != ZXV_OK) return -1;
    if (!(flags & ZXV_CCYF_PAYABLE) || minor == ZXV_MINOR_NA || minor > 8) return -1;
    return minor;
}

/* Why an instruction is invalid, or NULL. */
static const char *instr_bad(const ztw_instr *in, const char **field) {
    if (!text_ok(in->msg_id, 35)) return *field = "msg_id", "MsgId is not 1..35 printable ASCII";
    if (!text_ok(in->e2e, 35)) return *field = "e2e", "EndToEndId is not 1..35 printable ASCII";
    if (!dt_ok(in->cre_dt_tm)) return *field = "cre_dt_tm", "CreDtTm is not ISO 8601";
    if (ccy_minor(in->ccy) < 0) return *field = "ccy", "not a payable ISO 4217 currency";
    if (in->amount <= 0 || in->amount > ZXV_AMOUNT_MAX)
        return *field = "amount", "the amount is outside 1..2^53-1";
    if (!code_ok(in->dbtr_agt)) return *field = "dbtr_agt", "not a participant code";
    if (!code_ok(in->cdtr_agt)) return *field = "cdtr_agt", "not a participant code";
    if (strcmp(in->dbtr_agt, in->cdtr_agt) == 0)
        return *field = "cdtr_agt", "the debtor and creditor agents are the same";
    if (!name_ok(in->dbtr_nm)) return *field = "dbtr_nm", "not a valid name";
    if (!name_ok(in->cdtr_nm)) return *field = "cdtr_nm", "not a valid name";
    if (!text_ok(in->dbtr_acct, 34)) return *field = "dbtr_acct", "not a valid account id";
    if (!text_ok(in->cdtr_acct, 34)) return *field = "cdtr_acct", "not a valid account id";
    if (in->source > 3) return *field = "source", "not a known source";
    return NULL;
}

static int32_t instr_digest(const ztw_instr *in, uint8_t out[32]) {
    uint8_t buf[512];
    zind_wr w;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_str(&w, in->e2e);
    zind_wr_str(&w, in->ccy);
    zind_wr_u64(&w, (uint64_t) in->amount);
    zind_wr_str(&w, in->dbtr_agt);
    zind_wr_str(&w, in->cdtr_agt);
    zind_wr_str(&w, in->dbtr_nm);
    zind_wr_str(&w, in->dbtr_acct);
    zind_wr_str(&w, in->cdtr_nm);
    zind_wr_str(&w, in->cdtr_acct);
    if (w.overflow) return ZXV_E_ARG;
    return zind_subject("sutra-clear/instr", buf, w.len, out);
}

static int32_t e2e_key(const char *e2e, uint8_t out[32]) {
    return zind_subject("tri-witness/e2e", (const uint8_t *) e2e, strlen(e2e), out);
}

/* ===== the chain's records ===== */

static int32_t check_records(V *v) {
    uint8_t prev[32], h[32];
    uint8_t *buf = (uint8_t *) malloc(64 + ZIND_DOMAIN_MAX + ZIND_PAYLOAD_MAX + 64);
    const char *dom = zind_chain_domain(v->ch);
    if (!buf) return ZXV_E_NOMEM;
    memset(prev, 0, 32);
    for (uint32_t i = 0; i < v->n_rec; i++) {
        zind_record r;
        zind_wr wr;
        uint32_t k;
        int32_t st;
        if (zind_chain_at(v->ch, i, &r) != ZXV_OK) continue;
        zind_wr_init(&wr, buf, 64 + ZIND_DOMAIN_MAX + ZIND_PAYLOAD_MAX + 64);
        zind_wr_bytes(&wr, "zind/rec/v1", 11);
        zind_wr_str(&wr, dom);
        zind_wr_u64(&wr, r.seq);
        zind_wr_u16(&wr, r.kind);
        zind_wr_u32(&wr, (uint32_t) r.payload_len);
        zind_wr_bytes(&wr, r.payload, r.payload_len);
        zind_wr_bytes(&wr, r.prev, 32);
        zind_wr_bytes(&wr, r.key_id, 32);
        if (wr.overflow || zxv_sha3_256(buf, wr.len, h) != ZXV_OK || memcmp(h, r.hash, 32) != 0)
            fr(v, ZTW_F_LINK, 0, i, UINT32_MAX, "hash", r.hash, 32,
               "record %u: its hash is not the hash of its contents", i);
        if (memcmp(prev, r.prev, 32) != 0)
            fr(v, ZTW_F_LINK, 0, i, UINT32_MAX, "prev", r.prev, 32,
               "record %u: its link does not name the record before it", i);
        memcpy(prev, r.hash, 32);
        for (k = 0; k < v->w->n_house && memcmp(v->w->house_id[k], r.key_id, 32) != 0; k++) {
        }
        if (k == v->w->n_house) {
            fr(v, ZTW_F_UNTRUSTED, 0, i, UINT32_MAX, "key_id", r.key_id, 32,
               "record %u is signed by a key the witness was not told to trust", i);
            continue;
        }
        st = zind_verify(v->w->cap, v->w->house_pk[k], r.hash, 32, dom, r.sig);
        if (st == ZXV_E_CAP) {
            free(buf);
            return st;
        }
        if (st != ZXV_OK)
            fr(v, ZTW_F_SIGNATURE, 0, i, UINT32_MAX, "sig", r.sig, 64,
               "record %u: the ML-DSA-87 signature does not verify", i);
    }
    free(buf);
    return ZXV_OK;
}

/* ===== netting: the treaty engine, batch by batch ===== */

static int32_t net_batch(const char *ccy, uint32_t batch, uint32_t n_local, const uint32_t *gidx,
                         const uint32_t *lf, const uint32_t *lt, const int64_t *amt, size_t n,
                         int64_t *acc) {
    char text[ZXV_TREATY_SUTRA_MAX + 64];
    size_t o;
    int k;
    uint32_t off = 0, parties = 0, legs = 0;
    int32_t fault = 0, st;
    int64_t fees = 0, sum = 0;
    zxv_treaty *t = NULL;
    zxv_treaty_result *r = NULL;
    k = snprintf(text, sizeof text, "TREATY W%u %s NET\n", batch, ccy);
    if (k < 0 || (size_t) k >= sizeof text) return ZXV_E_INTERNAL;
    o = (size_t) k;
    for (uint32_t i = 0; i < n_local; i++) {
        k = snprintf(text + o, sizeof text - o, "PARTY W%u 0 0 %s\n", i + 1, i ? "0/1" : "1/1");
        if (k < 0 || (size_t) k >= sizeof text - o) return ZXV_E_INTERNAL;
        o += (size_t) k;
    }
    for (size_t j = 0; j < n; j++) {
        k = snprintf(text + o, sizeof text - o, "OWE W%u W%u %lld\n", lf[j] + 1, lt[j] + 1,
                     (long long) amt[j]);
        if (k < 0 || (size_t) k >= sizeof text - o) return ZXV_E_INTERNAL;
        o += (size_t) k;
    }
    k = snprintf(text + o, sizeof text - o, "END\n");
    if (k < 0 || (size_t) k >= sizeof text - o) return ZXV_E_INTERNAL;
    st = zxv_treaty_create(&t);
    if (st != ZXV_OK) return st;
    st = zxv_treaty_parse_sutra(t, text, &off, &fault);
    if (st == ZXV_OK) st = zxv_treaty_run(t, &r);
    zxv_treaty_destroy(t);
    if (st != ZXV_OK) return st;
    st = zxv_treaty_result_summary(r, &parties, &legs, &fees);
    if (st == ZXV_OK && (parties != n_local || fees != 0)) st = ZXV_E_INTERNAL;
    for (uint32_t i = 0; st == ZXV_OK && i < n_local; i++) {
        int64_t net = 0, bal = 0, dv = 0;
        st = zxv_treaty_result_party(r, i, &net, &bal, &dv);
        if (st == ZXV_OK && (!add64(sum, net, &sum) || !add64(acc[gidx[i]], net, &acc[gidx[i]])))
            st = ZXV_E_RANGE;
    }
    if (st == ZXV_OK && sum != 0) st = ZXV_E_INTERNAL;
    zxv_treaty_result_destroy(r);
    return st;
}

/* Net positions of np participants from n obligations; pos[] on ZXV_OK. */
static int32_t renet(const char *ccy, uint32_t np, const uint32_t *from, const uint32_t *to,
                     const int64_t *amt, size_t n, int64_t *pos) {
    int64_t *acc = (int64_t *) calloc(np + 1u, sizeof *acc);
    int64_t *plain = (int64_t *) calloc(np + 1u, sizeof *plain);
    int32_t *loc = (int32_t *) malloc((np + 1u) * sizeof *loc);
    uint32_t gidx[BATCH_PARTIES], lf[BATCH_OWES], lt[BATCH_OWES], nl = 0, batch = 0;
    int64_t bamt[BATCH_OWES];
    size_t cnt = 0;
    int32_t st = ZXV_OK;
    if (!acc || !plain || !loc) {
        st = ZXV_E_NOMEM;
        goto out;
    }
    for (uint32_t i = 0; i < np; i++) loc[i] = -1;
    for (size_t j = 0; j <= n && st == ZXV_OK; j++) {
        uint32_t need = 0;
        if (j < n) {
            need = (uint32_t) (loc[from[j]] < 0) + (uint32_t) (loc[to[j]] < 0);
            if (!add64(plain[to[j]], amt[j], &plain[to[j]]) ||
                !add64(plain[from[j]], -amt[j], &plain[from[j]])) {
                st = ZXV_E_RANGE;
                break;
            }
        }
        if (cnt && (j == n || cnt == BATCH_OWES || nl + need > BATCH_PARTIES)) {
            st = net_batch(ccy, ++batch, nl, gidx, lf, lt, bamt, cnt, acc);
            for (uint32_t i = 0; i < nl; i++) loc[gidx[i]] = -1;
            nl = 0;
            cnt = 0;
        }
        if (j == n || st != ZXV_OK) break;
        if (loc[from[j]] < 0) loc[from[j]] = (int32_t) nl, gidx[nl++] = from[j];
        if (loc[to[j]] < 0) loc[to[j]] = (int32_t) nl, gidx[nl++] = to[j];
        lf[cnt] = (uint32_t) loc[from[j]];
        lt[cnt] = (uint32_t) loc[to[j]];
        bamt[cnt++] = amt[j];
    }
    for (uint32_t i = 0; st == ZXV_OK && i < np; i++)
        if (acc[i] != plain[i]) st = ZXV_E_INTERNAL;
    if (st == ZXV_OK) memcpy(pos, acc, np * sizeof *pos);
out:
    free(acc);
    free(plain);
    free(loc);
    return st;
}

/* ===== the journal ===== */

static int32_t jfind_name(const V *v, const char *name, uint32_t *out) {
    uint32_t hits = 0;
    for (uint32_t a = 0; a < v->j.n_acct; a++)
        if (strcmp(v->j.acct[a].name, name) == 0) {
            if (!hits) *out = a;
            hits++;
        }
    return hits == 1 ? ZXV_OK : hits ? ZXV_E_DUPLICATE : ZXV_E_NOT_FOUND;
}

static const ztw_jpost *jfind_id(const V *v, uint64_t id, size_t *out_i) {
    size_t lo = 0, hi = v->j.n_post;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (v->j.post[mid].id == id) {
            if (out_i) *out_i = mid;
            return &v->j.post[mid];
        }
        if (v->j.post[mid].id < id) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

/* The journal's own consistency; on a structural fault the journal is
 * dropped from the replay (have_j = 0). */
static void check_journal(V *v) {
    int64_t *bal = (int64_t *) calloc(v->j.n_acct + 1u, sizeof *bal);
    if (!bal) {
        v->fatal = ZXV_E_NOMEM;
        return;
    }
    for (size_t i = 0; i < v->j.n_post; i++) {
        const ztw_jpost *p = &v->j.post[i];
        const uint8_t *raw = (const uint8_t *) p->ref;
        size_t rl = strlen(p->ref);
        if (i && p->id <= v->j.post[i - 1].id) {
            fr(v, ZTW_F_JOURNAL, 0, UINT32_MAX, (uint32_t) i, "journal.id", raw, rl,
               "journal posting %zu: id %llu is not after %llu", i, (unsigned long long) p->id,
               (unsigned long long) v->j.post[i - 1].id);
            v->have_j = 0;
            break;
        }
        if (p->from >= v->j.n_acct || p->to >= v->j.n_acct || p->from == p->to) {
            fr(v, ZTW_F_JOURNAL, 0, UINT32_MAX, (uint32_t) i, "journal.account", raw, rl,
               "journal posting %llu names an account that does not exist",
               (unsigned long long) p->id);
            v->have_j = 0;
            break;
        }
        if (p->amount <= 0 || p->amount > ZXV_AMOUNT_MAX)
            fr(v, ZTW_F_JOURNAL, 0, UINT32_MAX, (uint32_t) i, "journal.amount", raw, rl,
               "journal posting %llu moves %lld, outside 1..2^53-1", (unsigned long long) p->id,
               (long long) p->amount);
        if (strcmp(v->j.acct[p->from].ccy, v->j.acct[p->to].ccy) != 0)
            fr(v, ZTW_F_JOURNAL, 0, UINT32_MAX, (uint32_t) i, "journal.ccy", raw, rl,
               "journal posting %llu moves %s into a %s account", (unsigned long long) p->id,
               v->j.acct[p->from].ccy, v->j.acct[p->to].ccy);
        if (p->amount <= 0 || p->amount > ZXV_AMOUNT_MAX) continue; /* reported above */
        sat_add(&bal[p->from], -p->amount);
        sat_add(&bal[p->to], p->amount);
        if (v->j.acct[p->from].kind == ZXV_ACCT_HOLDER && bal[p->from] < 0)
            fr(v, ZTW_F_JOURNAL, 0, UINT32_MAX, (uint32_t) i, "journal.holder", raw, rl,
               "journal posting %llu takes holder account %s below zero",
               (unsigned long long) p->id, v->j.acct[p->from].name);
    }
    free(bal);
}

/* Balances of the house accounts as of two cut points (ids < a; ids <= b). */
static void jbalances(const V *v, uint64_t below, uint64_t upto, int64_t *before, int64_t *after) {
    memset(before, 0, (v->j.n_acct + 1u) * sizeof *before);
    memset(after, 0, (v->j.n_acct + 1u) * sizeof *after);
    for (size_t i = 0; i < v->j.n_post; i++) {
        const ztw_jpost *p = &v->j.post[i];
        if (p->id > upto && p->id >= below) break;
        if (p->amount <= 0 || p->amount > ZXV_AMOUNT_MAX) continue; /* a JOURNAL finding */
        if (p->id < below) {
            sat_add(&before[p->from], -p->amount);
            sat_add(&before[p->to], p->amount);
        }
        if (p->id <= upto) {
            sat_add(&after[p->from], -p->amount);
            sat_add(&after[p->to], p->amount);
        }
    }
}

static int32_t jdigest(const V *v, uint64_t upto, uint8_t out[32]) {
    size_t cap = 64, len = 5;
    uint8_t *b;
    int32_t st;
    for (size_t i = 0; i < v->j.n_post && v->j.post[i].id <= upto; i++) cap += 8 + 8 + 4 * 256;
    b = (uint8_t *) malloc(cap);
    if (!b) return ZXV_E_NOMEM;
    memcpy(b, "ZSJP1", 5);
    for (size_t i = 0; i < v->j.n_post && v->j.post[i].id <= upto; i++) {
        const ztw_jpost *p = &v->j.post[i];
        zind_wr w;
        zind_wr_init(&w, b + len, cap - len);
        zind_wr_u64(&w, p->id);
        zind_wr_str(&w, v->j.acct[p->from].name);
        zind_wr_str(&w, v->j.acct[p->to].name);
        zind_wr_str(&w, v->j.acct[p->from].ccy);
        zind_wr_u64(&w, (uint64_t) p->amount);
        zind_wr_str(&w, p->ref);
        len += w.len;
    }
    st = zind_subject("sutra-clear/journal", b, len, out);
    free(b);
    return st;
}

/* ===== one cycle ===== */

typedef struct {
    uint8_t kind, c, b;
    uint16_t p;
    int64_t amount;
    char from[64], to[64], ref[36];
} xpost_t;

typedef struct {
    char name[64];
    uint8_t c;
    uint16_t p;
    uint8_t role; /* 0 participant, 1 hub, 2..5 bucket */
    int64_t open, close;
} xbal_t;

typedef struct {
    uint8_t kind;
    char msg_id[36];
    uint8_t dig[32];
    int ok;
} xiso_t;

static void acct_name(const ztw_cycle *c, const char *who, uint32_t ci, char out[64]) {
    char tmp[96];
    int n = snprintf(tmp, sizeof tmp, "%s/%s/%s", c->house, who, c->ccy[ci]);
    if (n < 0 || n >= 64) n = 0;
    memcpy(out, tmp, (size_t) n);
    out[n] = 0;
}

static uint64_t *carry_slot(V *v, const char *code, const char *ccy) {
    for (size_t i = 0; i < v->n_carry; i++)
        if (strcmp(v->carry[i].code, code) == 0 && strcmp(v->carry[i].ccy, ccy) == 0)
            return &v->carry[i].rem;
    if (v->n_carry == v->cap_carry) {
        size_t a = v->cap_carry ? v->cap_carry * 2 : 64;
        carry_t *n = (carry_t *) realloc(v->carry, a * sizeof *n);
        if (!n) return NULL;
        v->carry = n;
        v->cap_carry = a;
    }
    memset(&v->carry[v->n_carry], 0, sizeof v->carry[0]);
    snprintf(v->carry[v->n_carry].code, sizeof v->carry[0].code, "%s", code);
    snprintf(v->carry[v->n_carry].ccy, sizeof v->carry[0].ccy, "%s", ccy);
    return &v->carry[v->n_carry++].rem;
}

static int find_idx(char (*list)[12], uint32_t n, const char *s) {
    for (uint32_t i = 0; i < n; i++)
        if (strcmp(list[i], s) == 0) return (int) i;
    return -1;
}

static int find_ccy(const ztw_cycle *c, const char *s) {
    for (uint32_t i = 0; i < c->n_ccy; i++)
        if (strcmp(c->ccy[i], s) == 0) return (int) i;
    return -1;
}

static int32_t render(zxv_msg *m, uint8_t out[32]) {
    size_t xl = 0;
    char *x;
    int32_t st = zxv_msg_render(m, NULL, 0, &xl);
    if (st != ZXV_E_BUFFER) return st == ZXV_OK ? ZXV_E_INTERNAL : st;
    x = (char *) malloc(xl + 1);
    if (!x) return ZXV_E_NOMEM;
    st = zxv_msg_render(m, x, xl + 1, &xl);
    if (st == ZXV_OK) st = zxv_sha3_256((const uint8_t *) x, xl, out);
    free(x);
    return st;
}

static int32_t iso_pacs(V *v, const ztw_cycle *c, const xpost_t *x, xiso_t *o) {
    zxv_msg *m = NULL;
    int32_t st = zxv_msg_create_cap(v->w->cap, ZXV_MX_PACS008, &m);
    if (st != ZXV_OK) return st;
    if (!(st = zxv_msg_set_text(m, ZXV_FLD_MSG_ID, x->ref)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_CRE_DT_TM, c->settle_dt)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_DEBTOR_NAME,
                                x->kind == 1 ? c->code[x->p] : c->house)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_DEBTOR_ACCT, x->from)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_CREDITOR_NAME,
                                x->kind == 2 ? c->code[x->p] : c->house)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_CREDITOR_ACCT, x->to)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_END_TO_END_ID, x->ref)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_CCY, c->ccy[x->c])) &&
        !(st = zxv_msg_set_amount(m, ZXV_AMT_SETTLEMENT, x->amount, c->minor[x->c])))
        st = render(m, o->dig);
    zxv_msg_destroy(m);
    o->kind = 1;
    snprintf(o->msg_id, sizeof o->msg_id, "%s", x->ref);
    o->ok = st == ZXV_OK;
    return st == ZXV_E_CAP || st == ZXV_E_NOMEM ? st : ZXV_OK;
}

static int32_t iso_camt(V *v, const ztw_cycle *c, uint32_t ci, uint32_t p, const xbal_t *b,
                        const xpost_t *xp, uint32_t nxp, xiso_t *o) {
    zxv_msg *m = NULL;
    char tmp[96];
    int32_t st;
    int n = snprintf(tmp, sizeof tmp, "%s/%u/%s/%u/ST", c->house, c->no, c->ccy[ci], p);
    o->kind = 2;
    o->ok = 0;
    o->msg_id[0] = 0;
    if (n > 0 && n < 36) memcpy(o->msg_id, tmp, (size_t) n + 1);
    st = zxv_msg_create_cap(v->w->cap, ZXV_MX_CAMT053, &m);
    if (st != ZXV_OK) return st;
    if (!(st = zxv_msg_set_text(m, ZXV_FLD_MSG_ID, o->msg_id)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_CRE_DT_TM, c->settle_dt)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_ACCT_ID, b->name)) &&
        !(st = zxv_msg_set_text(m, ZXV_FLD_CCY, c->ccy[ci])) &&
        !(st = zxv_msg_set_amount(m, ZXV_AMT_OPENING, b->open, c->minor[ci])) &&
        !(st = zxv_msg_set_amount(m, ZXV_AMT_CLOSING, b->close, c->minor[ci]))) {
        for (uint32_t i = 0; i < nxp && st == ZXV_OK; i++)
            if (xp[i].c == ci && xp[i].p == p)
                st = zxv_msg_add_entry(m, xp[i].amount, c->minor[ci],
                                       xp[i].kind == 2 ? ZXV_CRDT : ZXV_DBIT, ZXV_STS_BOOK);
        if (st == ZXV_OK) st = render(m, o->dig);
    }
    zxv_msg_destroy(m);
    o->ok = st == ZXV_OK;
    return st == ZXV_E_CAP || st == ZXV_E_NOMEM ? st : ZXV_OK;
}

/* Header, lists and continuity with the cycle before. */
static void check_header(V *v, const ztw_cycle *c, uint32_t frag_no) {
    zind_record r;
    char nm[48];
    if (c->no != frag_no)
        fc(v, ZTW_F_SEQUENCE, UINT32_MAX, "cycle_no", frag_no, c->no,
           "the record says cycle %u; its fragments say %u", c->no, frag_no);
    if (c->no != v->prev_no + 1)
        fc(v, ZTW_F_SEQUENCE, UINT32_MAX, "cycle_no", (int64_t) v->prev_no + 1, c->no,
           "cycle %u follows cycle %u", c->no, v->prev_no);
    if (!house_ok(c->house))
        fc(v, ZTW_F_FORMAT, UINT32_MAX, "house_id", 0, 0, "the house id is not 1..8 of [A-Z0-9-]");
    if (v->cycles_seen && strcmp(c->house, v->house) != 0)
        fc(v, ZTW_F_SEQUENCE, UINT32_MAX, "house_id", 0, 0,
           "the house id changed from %s to %s", v->house, c->house);
    for (uint32_t i = 0; i < v->cnrec; i++)
        if (zind_chain_at(v->ch, v->crec + i, &r) == ZXV_OK && memcmp(r.key_id, c->key_id, 32) != 0)
            fc(v, ZTW_F_SEQUENCE, UINT32_MAX, "house_key_id", 0, 0,
               "the record names a house key other than the one that signed record %u",
               v->crec + i);
    if (!dt_ok(c->settle_dt))
        fc(v, ZTW_F_FORMAT, UINT32_MAX, "settle_dt", 0, 0, "the settlement time is not ISO 8601");
    if (c->backend != 1 && c->backend != 2)
        fc(v, ZTW_F_FORMAT, UINT32_MAX, "backend", 2, c->backend, "unknown netting backend");
    for (uint32_t i = 0; i < c->n_ccy; i++) {
        int mn = ccy_minor(c->ccy[i]);
        snprintf(nm, sizeof nm, "ccy[%u]", i);
        if (mn < 0) fc(v, ZTW_F_FORMAT, i, nm, 0, 0, "%s is not a payable ISO 4217 currency", c->ccy[i]);
        for (uint32_t k = 0; k < i; k++)
            if (strcmp(c->ccy[k], c->ccy[i]) == 0) fc(v, ZTW_F_FORMAT, i, nm, 0, 0, "listed twice");
        snprintf(nm, sizeof nm, "ccy[%u].minor", i);
        if (mn >= 0 && mn != c->minor[i])
            fc(v, ZTW_F_FORMAT, i, nm, mn, c->minor[i], "%s has %d minor units in ISO 4217",
               c->ccy[i], mn);
        if (i < v->n_ccy && strcmp(v->ccy[i], c->ccy[i]) != 0) {
            snprintf(nm, sizeof nm, "ccy[%u]", i);
            fc(v, ZTW_F_SEQUENCE, i, nm, 0, 0, "currency %u was %s in the cycle before", i,
               v->ccy[i]);
        }
    }
    if (c->n_ccy < v->n_ccy)
        fc(v, ZTW_F_SEQUENCE, UINT32_MAX, "n_ccy", v->n_ccy, c->n_ccy, "a currency disappeared");
    for (uint32_t i = 0; i < c->n_part; i++) {
        snprintf(nm, sizeof nm, "part[%u]", i);
        if (!code_ok(c->code[i])) fc(v, ZTW_F_FORMAT, i, nm, 0, 0, "not a participant code");
        for (uint32_t k = 0; k < i; k++)
            if (strcmp(c->code[k], c->code[i]) == 0) fc(v, ZTW_F_FORMAT, i, nm, 0, 0, "listed twice");
        if (i < v->n_part && strcmp(v->part[i], c->code[i]) != 0)
            fc(v, ZTW_F_SEQUENCE, i, nm, 0, 0, "participant %u was %s in the cycle before", i,
               v->part[i]);
    }
    if (c->n_part < v->n_part)
        fc(v, ZTW_F_SEQUENCE, UINT32_MAX, "n_part", v->n_part, c->n_part,
           "a participant disappeared");
}

static int32_t decision_ok(V *v, const ztw_cycle *c, const ztw_res *x, int *op_known) {
    uint8_t msg[256];
    zind_wr w;
    uint32_t k;
    int32_t st;
    *op_known = 0;
    for (k = 0; k < v->w->n_op && memcmp(v->w->op_id[k], x->op_id, 32) != 0; k++) {
    }
    if (k == v->w->n_op) return ZXV_E_VERIFY;
    *op_known = 1;
    zind_wr_init(&w, msg, sizeof msg);
    zind_wr_bytes(&w, DECISION_TAG, strlen(DECISION_TAG));
    zind_wr_bytes(&w, c->key_id, 32);
    zind_wr_str(&w, x->e2e);
    zind_wr_u8(&w, x->choice);
    zind_wr_bytes(&w, x->group, 32);
    zind_wr_bytes(&w, x->chosen, 32);
    if (w.overflow) return ZXV_E_VERIFY;
    st = zind_verify(v->w->cap, v->w->op_pk[k], msg, w.len, OPERATOR_CTX, x->sig);
    return st == ZXV_E_CAP ? st : st == ZXV_OK ? ZXV_OK : ZXV_E_VERIFY;
}

/* Instructions, replays, held groups and decisions. ok[i]: instruction i
 * can be netted (valid, known currency and agents). */
static int32_t check_instructions(V *v, const ztw_cycle *c, uint8_t *ok, int32_t *ci,
                                  int32_t *di, int32_t *ki) {
    zind_seen *here = NULL;
    char nm[48];
    uint8_t key[32];
    int32_t st = zind_seen_create(c->n_instr + c->n_wd + c->n_res + 16, &here), present = 0;
    if (st != ZXV_OK) return st;
    for (uint32_t i = 0; i < c->n_instr; i++) {
        const ztw_instr *in = &c->instr[i];
        const char *f = NULL, *why = instr_bad(in, &f);
        ok[i] = 0;
        if (why) {
            snprintf(nm, sizeof nm, "instr[%u].%s", i, f);
            fc(v, ZTW_F_INSTRUCTION, i, nm, 0, 0, "%s: %s", in->e2e, why);
            continue;
        }
        ci[i] = (int32_t) find_ccy(c, in->ccy);
        di[i] = (int32_t) find_idx(c->code, c->n_part, in->dbtr_agt);
        ki[i] = (int32_t) find_idx(c->code, c->n_part, in->cdtr_agt);
        if (ci[i] < 0) {
            snprintf(nm, sizeof nm, "instr[%u].ccy", i);
            fc(v, ZTW_F_INSTRUCTION, i, nm, 0, 0, "%s: %s is not a currency of the house", in->e2e,
               in->ccy);
            continue;
        }
        if (di[i] < 0 || ki[i] < 0) {
            snprintf(nm, sizeof nm, "instr[%u].%s", i, di[i] < 0 ? "dbtr_agt" : "cdtr_agt");
            fc(v, ZTW_F_INSTRUCTION, i, nm, 0, 0, "%s: %s is not a participant", in->e2e,
               di[i] < 0 ? in->dbtr_agt : in->cdtr_agt);
            continue;
        }
        ok[i] = 1;
        if (i && strcmp(c->instr[i - 1].e2e, in->e2e) >= 0) {
            snprintf(nm, sizeof nm, "instr[%u].e2e", i);
            fc(v, ZTW_F_INSTRUCTION, i, nm, 0, 0,
               "%s is not after %s: the record lists instructions in end-to-end id order", in->e2e,
               c->instr[i - 1].e2e);
        }
    }
    /* replays: against every earlier cycle, and inside this one */
    for (uint32_t s = 0; s < 3; s++) {
        uint32_t n = s == 0 ? c->n_instr : s == 1 ? c->n_wd : c->n_res;
        for (uint32_t i = 0; i < n; i++) {
            const char *e2e = s == 0 ? c->instr[i].e2e : s == 1 ? c->wd[i].e2e : c->res[i].e2e;
            const char *sec = s == 0 ? "instr" : s == 1 ? "withdrawn" : "res";
            snprintf(nm, sizeof nm, "%s[%u].e2e", sec, i);
            if ((st = e2e_key(e2e, key)) != ZXV_OK) goto out;
            if ((st = zind_seen_has(v->settled, key, &present)) != ZXV_OK) goto out;
            if (present)
                fc(v, ZTW_F_REPLAY, i, nm, 0, 0, "%s settled, was withdrawn or was decided in an "
                   "earlier cycle", e2e);
            if (s == 2) continue; /* a decision names its instruction or withdrawal */
            st = zind_seen_add(here, key);
            if (st == ZIND_E_REPLAY)
                fc(v, ZTW_F_REPLAY, i, nm, 0, 0, "%s appears twice in this cycle", e2e);
            else if (st != ZXV_OK) goto out;
            st = ZXV_OK;
        }
    }
    /* decisions */
    for (uint32_t i = 0; i < c->n_res; i++) {
        const ztw_res *x = &c->res[i];
        const ztw_instr *hit = NULL;
        int known = 0;
        uint8_t d[32];
        snprintf(nm, sizeof nm, "res[%u].e2e", i);
        for (uint32_t k = 0; k < i; k++)
            if (strcmp(c->res[k].e2e, x->e2e) == 0)
                fc(v, ZTW_F_DECISION, i, nm, 0, 0, "%s is decided twice", x->e2e);
        for (uint32_t k = 0; k < c->n_instr && !hit; k++)
            if (strcmp(c->instr[k].e2e, x->e2e) == 0) hit = &c->instr[k];
        for (uint32_t k = 0; k < c->n_wd && !hit; k++)
            if (strcmp(c->wd[k].e2e, x->e2e) == 0) hit = &c->wd[k];
        snprintf(nm, sizeof nm, "res[%u].choice", i);
        if (x->choice > 4) fc(v, ZTW_F_DECISION, i, nm, 4, x->choice, "a choice is 0..4");
        if (x->choice == 0 && hit)
            fc(v, ZTW_F_DECISION, i, nm, 0, 0, "%s was rejected outright yet appears in the cycle",
               x->e2e);
        snprintf(nm, sizeof nm, "res[%u].chosen", i);
        if (x->choice != 0) {
            if (!hit) {
                fc(v, ZTW_F_DECISION, i, nm, 0, 0, "the chosen version of %s is not in the cycle",
                   x->e2e);
            } else if ((st = instr_digest(hit, d)) != ZXV_OK || memcmp(d, x->chosen, 32) != 0) {
                if (st != ZXV_OK && st != ZXV_E_ARG) goto out;
                fc(v, ZTW_F_DECISION, i, nm, 0, 0,
                   "%s settled in a form other than the version the operator chose", x->e2e);
            }
        } else {
            static const uint8_t z[32] = {0};
            if (memcmp(x->chosen, z, 32) != 0)
                fc(v, ZTW_F_DECISION, i, nm, 0, 0, "a rejection names no version");
        }
        st = ZXV_OK;
        for (uint32_t k = 0; k < v->n_ph; k++)
            if (strcmp(v->ph[k].e2e, x->e2e) == 0 && x->choice >= 1 && x->choice <= v->ph[k].n &&
                memcmp(v->ph[k].dig[x->choice - 1], x->chosen, 32) != 0)
                fc(v, ZTW_F_DECISION, i, nm, 0, 0,
                   "version %u of %s held at the last close is not the version chosen", x->choice,
                   x->e2e);
        st = decision_ok(v, c, x, &known);
        if (st == ZXV_E_CAP) goto out;
        if (st != ZXV_OK) {
            snprintf(nm, sizeof nm, known ? "res[%u].sig" : "res[%u].operator", i);
            fc(v, ZTW_F_DECISION, i, nm, 0, 0,
               known ? "the operator's signature on the decision for %s does not verify"
                     : "the decision for %s is signed by a key that is not a trusted operator",
               x->e2e);
        }
        st = ZXV_OK;
    }
    /* groups still held at the close */
    for (uint32_t g = 0; g < c->n_held; g++) {
        const ztw_held *h = &c->held[g];
        snprintf(nm, sizeof nm, "held[%u].e2e", g);
        if (!text_ok(h->e2e, 35)) fc(v, ZTW_F_HELD, g, nm, 0, 0, "not an end-to-end id");
        if ((st = e2e_key(h->e2e, key)) != ZXV_OK) goto out;
        if ((st = zind_seen_has(v->settled, key, &present)) != ZXV_OK) goto out;
        if (!present) st = zind_seen_has(here, key, &present);
        if (st != ZXV_OK) goto out;
        for (uint32_t k = 0; k < c->n_res && !present; k++)
            present = strcmp(c->res[k].e2e, h->e2e) == 0;
        if (present)
            fc(v, ZTW_F_HELD, g, nm, 0, 0, "%s is held and also settled, withdrawn or decided",
               h->e2e);
        for (uint32_t k = 0; k < g; k++)
            if (strcmp(c->held[k].e2e, h->e2e) == 0) fc(v, ZTW_F_HELD, g, nm, 0, 0, "held twice");
        snprintf(nm, sizeof nm, "held[%u].n", g);
        if (h->n < 2 || h->n > 4) fc(v, ZTW_F_HELD, g, nm, 2, h->n, "a held group has 2..4 versions");
        for (uint32_t k = 0; k < h->n; k++) {
            uint8_t dk[32], dj[32];
            const char *f = NULL;
            snprintf(nm, sizeof nm, "held[%u].v[%u].e2e", g, k);
            if (strcmp(h->v[k].e2e, h->e2e) != 0)
                fc(v, ZTW_F_HELD, g, nm, 0, 0, "a version of %s is filed under another id", h->e2e);
            if (instr_bad(&h->v[k], &f)) fc(v, ZTW_F_HELD, g, nm, 0, 0, "a held version is invalid");
            if (instr_digest(&h->v[k], dk) != ZXV_OK) continue;
            for (uint32_t m = 0; m < k; m++)
                if (instr_digest(&h->v[m], dj) == ZXV_OK && memcmp(dj, dk, 32) == 0)
                    fc(v, ZTW_F_HELD, g, nm, 0, 0, "two held versions of %s are the same", h->e2e);
        }
        for (uint32_t k = 0; k < v->n_ph; k++) {
            const pheld_t *p = &v->ph[k];
            if (strcmp(p->e2e, h->e2e) != 0) continue;
            for (uint32_t m = 0; m < p->n; m++) {
                uint8_t dk[32];
                if (m >= h->n || instr_digest(&h->v[m], dk) != ZXV_OK || memcmp(dk, p->dig[m], 32)) {
                    snprintf(nm, sizeof nm, "held[%u].n", g);
                    fc(v, ZTW_F_HELD, g, nm, 0, 0,
                       "version %u of %s held at the last close is gone or changed", m + 1, h->e2e);
                    break;
                }
            }
        }
    }
    st = ZXV_OK;
out:
    zind_seen_destroy(here);
    return st;
}

static int32_t check_cycle(V *v, const ztw_cycle *c, uint32_t frag_no) {
    uint32_t np = c->n_part, nc = c->n_ccy, nk = nc * np;
    uint8_t *ok = (uint8_t *) calloc(c->n_instr + 1u, 1);
    int32_t *ci = (int32_t *) calloc(c->n_instr + 1u, sizeof *ci);
    int32_t *di = (int32_t *) calloc(c->n_instr + 1u, sizeof *di);
    int32_t *ki = (int32_t *) calloc(c->n_instr + 1u, sizeof *ki);
    int64_t *pos = (int64_t *) calloc(nk + 1u, sizeof *pos);
    uint8_t *act = (uint8_t *) calloc(nk + 1u, 1);
    ztw_fee *fee = (ztw_fee *) calloc(nk + 1u, sizeof *fee);
    uint8_t *used = (uint8_t *) calloc(nk + 1u, 1);
    uint32_t *from = (uint32_t *) calloc(c->n_instr + 1u, sizeof *from);
    uint32_t *to = (uint32_t *) calloc(c->n_instr + 1u, sizeof *to);
    int64_t *amt = (int64_t *) calloc(c->n_instr + 1u, sizeof *amt);
    xpost_t *xp = (xpost_t *) calloc((size_t) nk * 6 + 1, sizeof *xp);
    xbal_t *xb = (xbal_t *) calloc((size_t) nc * (np + 5) + 1, sizeof *xb);
    xiso_t *xi = (xiso_t *) calloc((size_t) nk * 7 + 1, sizeof *xi);
    int64_t *jb0 = NULL, *jb1 = NULL;
    uint32_t nxp = 0, nxb = 0, nxi = 0, n_rows = 0;
    char nm[48];
    int replay_ok = 1;
    int32_t st = ZXV_OK;
    if (!ok || !ci || !di || !ki || !pos || !act || !fee || !used || !from || !to || !amt || !xp ||
        !xb || !xi) {
        st = ZXV_E_NOMEM;
        goto out;
    }
    if (v->have_j) {
        jb0 = (int64_t *) calloc(v->j.n_acct + 1u, sizeof *jb0);
        jb1 = (int64_t *) calloc(v->j.n_acct + 1u, sizeof *jb1);
        if (!jb0 || !jb1) {
            st = ZXV_E_NOMEM;
            goto out;
        }
    }
    check_header(v, c, frag_no);
    st = check_instructions(v, c, ok, ci, di, ki);
    if (st != ZXV_OK) goto out;
    v->sum.instructions += c->n_instr;
    /* netting, per currency, in the record's order */
    for (uint32_t cc = 0; cc < nc; cc++) {
        size_t n = 0;
        for (uint32_t i = 0; i < c->n_instr; i++) {
            if (!ok[i] || (uint32_t) ci[i] != cc) continue;
            from[n] = (uint32_t) di[i];
            to[n] = (uint32_t) ki[i];
            amt[n++] = c->instr[i].amount;
            act[cc * np + (uint32_t) di[i]] = act[cc * np + (uint32_t) ki[i]] = 1;
        }
        if (!n) continue;
        st = renet(c->ccy[cc], np, from, to, amt, n, pos + (size_t) cc * np);
        if (st == ZXV_E_NOMEM) goto out;
        if (st != ZXV_OK) {
            fc(v, ZTW_F_NETTING, cc, "n_instr", 0, 0,
               "the replay cannot net %s (%s): the house could not have closed this cycle",
               c->ccy[cc], zxv_status_name(st));
            replay_ok = 0;
        }
        st = ZXV_OK;
    }
    for (uint32_t k = 0; k < nk && replay_ok; k++)
        if (pos[k] > ZXV_AMOUNT_MAX || pos[k] < -ZXV_AMOUNT_MAX) {
            fc(v, ZTW_F_NETTING, k, "n_pos", 0, pos[k],
               "a net position is beyond 2^53-1: the house could not have closed this cycle");
            replay_ok = 0;
        }
    /* the record's positions against the replay */
    for (uint32_t k = 0; k < nk; k++) n_rows += act[k];
    if (c->n_pos != n_rows)
        fc(v, ZTW_F_NETTING, UINT32_MAX, "n_pos", n_rows, c->n_pos,
           "the record lists %u net positions; the replay has %u", c->n_pos, n_rows);
    {
        uint32_t r = 0;
        int64_t sums[ZTW_MAX_CCY] = {0};
        int sum_ok[ZTW_MAX_CCY];
        for (uint32_t cc = 0; cc < ZTW_MAX_CCY; cc++) sum_ok[cc] = 1;
        for (uint32_t k = 0; k < nk; k++) {
            const ztw_pos *x;
            if (!act[k]) continue;
            if (r >= c->n_pos) break;
            x = &c->pos[r];
            snprintf(nm, sizeof nm, "pos[%u].%s", r,
                     x->c != k / np ? "ccy" : x->p != k % np ? "part" : "net");
            if (x->c != k / np || x->p != k % np)
                fc(v, ZTW_F_NETTING, r, nm, x->c != k / np ? (int64_t) (k / np) : (int64_t) (k % np),
                   x->c != k / np ? x->c : x->p, "the position row names another currency or "
                   "participant than the replay");
            else if (x->net != pos[k])
                fc(v, ZTW_F_NETTING, r, nm, pos[k], x->net, "%s's net position in %s",
                   c->code[k % np], c->ccy[k / np]);
            r++;
        }
        for (uint32_t i = 0; i < c->n_pos; i++) {
            uint8_t cc = c->pos[i].c;
            if (cc < ZTW_MAX_CCY && !add64(sums[cc], c->pos[i].net, &sums[cc])) sum_ok[cc] = 0;
        }
        for (uint32_t cc = 0; cc < ZTW_MAX_CCY; cc++)
            if (sums[cc] != 0 || !sum_ok[cc])
                fc(v, ZTW_F_CONSERVATION, cc, "n_pos", 0, sums[cc],
                   "the recorded positions in currency %u do not sum to zero", cc);
    }
    /* fees, with each participant's carry */
    for (uint32_t i = 0; i < c->n_instr; i++) {
        uint32_t k;
        ztw_fee *r;
        uint64_t f = 0, rem = 0;
        if (!ok[i]) continue;
        k = (uint32_t) ci[i] * np + (uint32_t) di[i];
        r = &fee[k];
        if (!used[k]) {
            uint64_t *slot = carry_slot(v, c->code[di[i]], c->ccy[ci[i]]);
            if (!slot) {
                st = ZXV_E_NOMEM;
                goto out;
            }
            used[k] = 1;
            r->c = (uint8_t) ci[i];
            r->p = (uint16_t) di[i];
            r->rem_in = *slot;
            if (v->lost) /* continuity lost: take the record's carry in as given */
                for (uint32_t x = 0; x < c->n_fee; x++)
                    if (c->fee[x].c == r->c && c->fee[x].p == r->p) r->rem_in = c->fee[x].rem_in;
            r->rem_out = r->rem_in;
        }
        if (zxv_fee_assure_carry(r->rem_out, (uint64_t) c->instr[i].amount, &f, &rem) != ZXV_OK ||
            !addu64(r->fee, f, &r->fee) ||
            !addu64(r->gross, (uint64_t) c->instr[i].amount, &r->gross)) {
            fc(v, ZTW_F_FEE, i, "n_fee", 0, 0, "the replay cannot compute the fee");
            replay_ok = 0;
            break;
        }
        r->rem_out = rem;
    }
    n_rows = 0;
    for (uint32_t k = 0; k < nk; k++)
        if (used[k]) {
            n_rows++;
            if (zxv_fee_split(fee[k].fee, fee[k].split) != ZXV_OK) replay_ok = 0;
        }
    if (c->n_fee != n_rows)
        fc(v, ZTW_F_FEE, UINT32_MAX, "n_fee", n_rows, c->n_fee,
           "the record lists %u fee rows; the replay has %u", c->n_fee, n_rows);
    {
        uint32_t r = 0;
        for (uint32_t k = 0; k < nk && r < c->n_fee; k++) {
            const ztw_fee *x = &c->fee[r], *e = &fee[k];
            uint64_t sp[4];
            if (!used[k]) continue;
            if (x->c != e->c || x->p != e->p) {
                snprintf(nm, sizeof nm, "fee[%u].%s", r, x->c != e->c ? "ccy" : "part");
                fc(v, ZTW_F_FEE, r, nm, x->c != e->c ? e->c : e->p, x->c != e->c ? x->c : x->p,
                   "the fee row names another currency or participant than the replay");
            } else {
                static const char *const fn[4] = {"gross", "rem_in", "fee", "rem_out"};
                const uint64_t xv[4] = {x->gross, x->rem_in, x->fee, x->rem_out};
                const uint64_t ev[4] = {e->gross, e->rem_in, e->fee, e->rem_out};
                for (int q = 0; q < 4; q++)
                    if (xv[q] != ev[q]) {
                        snprintf(nm, sizeof nm, "fee[%u].%s", r, fn[q]);
                        fc(v, q == 1 ? ZTW_F_CARRY : ZTW_F_FEE, r, nm, (int64_t) ev[q],
                           (int64_t) xv[q],
                           q == 1 ? "%s's carry in %s is not the carry out of its last fee"
                                  : "%s's fee in %s differs from zxv_fee_assure_carry",
                           c->code[e->p], c->ccy[e->c]);
                    }
                for (int b = 0; b < 4; b++)
                    if (x->split[b] != e->split[b]) {
                        snprintf(nm, sizeof nm, "fee[%u].split[%d]", r, b);
                        fc(v, ZTW_F_SPLIT, r, nm, (int64_t) e->split[b], (int64_t) x->split[b],
                           "the %s share of %s's fee differs from zxv_fee_split", k_bucket[b],
                           c->code[e->p]);
                    }
                if (zxv_fee_split(x->fee, sp) == ZXV_OK && memcmp(sp, x->split, sizeof sp) != 0 &&
                    x->fee != e->fee) {
                    snprintf(nm, sizeof nm, "fee[%u].fee", r);
                    fc(v, ZTW_F_SPLIT, r, nm, 0, 0, "the recorded split is not a split of the "
                       "recorded fee");
                }
            }
            r++;
        }
        /* the carry each participant takes into its next fee */
        for (uint32_t k = 0; k < nk; k++)
            if (used[k]) {
                uint64_t *slot = carry_slot(v, c->code[k % np], c->ccy[k / np]);
                if (!slot) {
                    st = ZXV_E_NOMEM;
                    goto out;
                }
                *slot = fee[k].rem_out;
            }
    }
    /* the postings the replay needs, the balances and the ISO output */
    for (uint32_t cc = 0; cc < nc && replay_ok; cc++) {
        int any = 0;
        int64_t bin[4] = {0, 0, 0, 0};
        char hub[64], bk[4][64];
        for (uint32_t p = 0; p < np; p++) any |= act[cc * np + p];
        if (!any) continue;
        acct_name(c, "hub", cc, hub);
        for (int b = 0; b < 4; b++) acct_name(c, k_bucket[b], cc, bk[b]);
        for (uint32_t p = 0; p < np; p++) {
            xbal_t *b = &xb[nxb];
            const ztw_fee *e = &fee[cc * np + p];
            if (!act[cc * np + p]) continue;
            acct_name(c, c->code[p], cc, b->name);
            b->c = (uint8_t) cc;
            b->p = (uint16_t) p;
            b->role = 0;
            b->close = pos[cc * np + p] - (used[cc * np + p] ? (int64_t) e->fee : 0); /* a delta for now */
            for (int q = 0; q < 4 && used[cc * np + p]; q++) sat_add(&bin[q], (int64_t) e->split[q]);
            nxb++;
        }
        memcpy(xb[nxb].name, hub, 64);
        xb[nxb].c = (uint8_t) cc;
        xb[nxb].role = 1;
        xb[nxb++].close = 0;
        for (int b = 0; b < 4; b++) {
            memcpy(xb[nxb].name, bk[b], 64);
            xb[nxb].c = (uint8_t) cc;
            xb[nxb].role = (uint8_t) (2 + b);
            xb[nxb++].close = bin[b];
        }
        for (uint32_t pass = 0; pass < 2; pass++)
            for (uint32_t p = 0; p < np; p++) {
                int64_t net = pos[cc * np + p];
                xpost_t *x = &xp[nxp];
                char tmp[96];
                int n;
                if (pass == 0 ? net >= 0 : net <= 0) continue;
                x->kind = (uint8_t) (pass + 1);
                x->c = (uint8_t) cc;
                x->p = (uint16_t) p;
                x->amount = pass == 0 ? -net : net;
                acct_name(c, c->code[p], cc, pass == 0 ? x->from : x->to);
                memcpy(pass == 0 ? x->to : x->from, hub, 64);
                n = snprintf(tmp, sizeof tmp, "%s/%u/%s/%u/%s", c->house, c->no, c->ccy[cc], p,
                             pass == 0 ? "S" : "R");
                memcpy(x->ref, tmp, n > 0 && n < 36 ? (size_t) n + 1 : 1);
                nxp++;
            }
        for (uint32_t p = 0; p < np; p++) {
            const ztw_fee *e = &fee[cc * np + p];
            if (!used[cc * np + p]) continue;
            for (uint8_t b = 0; b < 4; b++) {
                xpost_t *x = &xp[nxp];
                char tmp[96];
                int n;
                if (e->split[b] == 0) continue;
                x->kind = 3;
                x->c = (uint8_t) cc;
                x->p = (uint16_t) p;
                x->b = b;
                x->amount = (int64_t) e->split[b];
                acct_name(c, c->code[p], cc, x->from);
                memcpy(x->to, bk[b], 64);
                n = snprintf(tmp, sizeof tmp, "%s/%u/%s/%u/F%u", c->house, c->no, c->ccy[cc], p,
                             (unsigned) b);
                memcpy(x->ref, tmp, n > 0 && n < 36 ? (size_t) n + 1 : 1);
                nxp++;
            }
        }
    }
    if (!replay_ok) goto out;
    v->sum.postings += c->n_post;
    if (c->n_post != nxp)
        fc(v, ZTW_F_POSTING, UINT32_MAX, "n_post", nxp, c->n_post,
           "the record lists %u postings; the replay needs %u", c->n_post, nxp);
    for (uint32_t i = 0; i < c->n_post && i < nxp; i++) {
        const ztw_post *x = &c->post[i];
        const xpost_t *e = &xp[i];
        const char *bad = x->kind != e->kind ? "kind" : x->c != e->c ? "ccy" : x->p != e->p ? "part"
                        : x->b != e->b ? "bucket" : x->amount != e->amount ? "amount"
                        : strcmp(x->from, e->from) ? "from" : strcmp(x->to, e->to) ? "to"
                        : strcmp(x->ref, e->ref) ? "ref" : NULL;
        if (!bad) continue;
        snprintf(nm, sizeof nm, "post[%u].%s", i, bad);
        fc(v, ZTW_F_POSTING, i, nm, e->amount, x->amount,
           "posting %u differs from the replay: it should be %s -> %s, %lld, ref %s", i, e->from,
           e->to, (long long) e->amount, e->ref);
    }
    for (uint32_t i = 1; i < c->n_post; i++)
        if (c->post[i].entry <= c->post[i - 1].entry) {
            snprintf(nm, sizeof nm, "post[%u].entry", i);
            fc(v, ZTW_F_POSTING, i, nm, (int64_t) (c->post[i - 1].entry + 1), (int64_t) c->post[i].entry,
               "ledger entries do not increase");
        }
    if (c->first != (c->n_post ? c->post[0].entry : 0))
        fc(v, ZTW_F_POSTING, UINT32_MAX, "first_entry", c->n_post ? (int64_t) c->post[0].entry : 0,
           (int64_t) c->first, "first_entry is not the first posting's entry");
    if (c->last != (c->n_post ? c->post[c->n_post - 1].entry : 0))
        fc(v, ZTW_F_POSTING, UINT32_MAX, "last_entry",
           c->n_post ? (int64_t) c->post[c->n_post - 1].entry : 0, (int64_t) c->last,
           "last_entry is not the last posting's entry");
    if (c->head < v->prev_head)
        fc(v, ZTW_F_SEQUENCE, UINT32_MAX, "ledger_head", (int64_t) v->prev_head, (int64_t) c->head,
           "the ledger head went back");
    /* against the journal */
    if (v->have_j) {
        size_t in_range = 0, matched = 0;
        uint8_t d[32];
        for (uint32_t i = 0; i < c->n_post; i++) {
            const ztw_post *x = &c->post[i];
            size_t at = 0;
            const ztw_jpost *p = jfind_id(v, x->entry, &at);
            snprintf(nm, sizeof nm, "post[%u].entry", i);
            if (!p) {
                fc(v, ZTW_F_JOURNAL, i, nm, 0, (int64_t) x->entry,
                   "ledger entry %llu is not in the journal", (unsigned long long) x->entry);
                continue;
            }
            if (strcmp(v->j.acct[p->from].name, x->from) || strcmp(v->j.acct[p->to].name, x->to) ||
                p->amount != x->amount || strcmp(p->ref, x->ref) ||
                (x->c < nc && strcmp(v->j.acct[p->from].ccy, c->ccy[x->c]) != 0)) {
                fc(v, ZTW_F_JOURNAL, i, nm, x->amount, p->amount,
                   "the journal's entry %llu is %s -> %s, %lld, ref %s", (unsigned long long) p->id,
                   v->j.acct[p->from].name, v->j.acct[p->to].name, (long long) p->amount, p->ref);
                v->jused[at] = 1; /* reported here, not again as uncovered */
                continue;
            }
            if (v->jused[at])
                fc(v, ZTW_F_JOURNAL, i, nm, 0, (int64_t) x->entry,
                   "ledger entry %llu is claimed by two postings", (unsigned long long) x->entry);
            v->jused[at] = 1;
            matched++;
        }
        for (size_t i = 0; c->n_post && i < v->j.n_post; i++)
            if (v->j.post[i].id >= c->first && v->j.post[i].id <= c->last) in_range++;
        if (in_range != matched && matched == c->n_post)
            fc(v, ZTW_F_JOURNAL, UINT32_MAX, "first_entry", (int64_t) c->n_post, (int64_t) in_range,
               "the journal has %zu postings between the cycle's first and last entry; the cycle "
               "made %u", in_range, c->n_post);
        if (c->head != 0 && !jfind_id(v, c->head, NULL))
            fc(v, ZTW_F_JOURNAL, UINT32_MAX, "ledger_head", 0, (int64_t) c->head,
               "the ledger head %llu is not a journal entry", (unsigned long long) c->head);
        if (c->head < c->last)
            fc(v, ZTW_F_JOURNAL, UINT32_MAX, "ledger_head", (int64_t) c->last, (int64_t) c->head,
               "the ledger head is before the cycle's last entry");
        st = jdigest(v, c->head, d);
        if (st != ZXV_OK) goto out;
        if (memcmp(d, c->jdig, 32) != 0)
            fc(v, ZTW_F_JDIGEST, UINT32_MAX, "journal_digest", 0, 0,
               "the journal up to entry %llu does not hash to the recorded digest",
               (unsigned long long) c->head);
        if (c->head > v->max_head) v->max_head = c->head;
        /* balances from the journal: before the first entry, after the last */
        jbalances(v, c->n_post ? c->first : c->head + 1, c->n_post ? c->last : c->head, jb0, jb1);
        for (uint32_t i = 0; i < nxb; i++) {
            uint32_t a = 0;
            if (jfind_name(v, xb[i].name, &a) != ZXV_OK) {
                xb[i].open = INT64_MIN; /* unknown: check_accounts reports it */
                continue;
            }
            xb[i].open = jb0[a];
            xb[i].close = jb1[a];
        }
    } else {
        /* no journal: the arithmetic on the record's own opening balances */
        for (uint32_t i = 0; i < nxb; i++) {
            int64_t d = xb[i].close;
            xb[i].open = i < c->n_bal ? c->bal[i].open : 0;
            if (!add64(xb[i].open, d, &xb[i].close)) xb[i].close = INT64_MIN;
        }
    }
    if (c->n_bal != nxb)
        fc(v, ZTW_F_BALANCE, UINT32_MAX, "n_bal", nxb, c->n_bal,
           "the record lists %u balances; the replay has %u", c->n_bal, nxb);
    for (uint32_t i = 0; i < c->n_bal && i < nxb; i++) {
        const ztw_bal *x = &c->bal[i];
        if (strcmp(x->name, xb[i].name) != 0 || x->c != xb[i].c) {
            snprintf(nm, sizeof nm, "bal[%u].%s", i, strcmp(x->name, xb[i].name) ? "account" : "ccy");
            fc(v, ZTW_F_BALANCE, i, nm, 0, 0, "balance row %u should be for %s", i, xb[i].name);
            continue;
        }
        if (xb[i].open == INT64_MIN) continue;
        if (x->open != xb[i].open) {
            snprintf(nm, sizeof nm, "bal[%u].open", i);
            fc(v, ZTW_F_BALANCE, i, nm, xb[i].open, x->open, "%s's opening balance", x->name);
        }
        if (x->close != xb[i].close) {
            snprintf(nm, sizeof nm, "bal[%u].close", i);
            fc(v, ZTW_F_BALANCE, i, nm, xb[i].close, x->close, "%s's closing balance", x->name);
        }
    }
    if (v->have_j) /* the participant's close must also be its open plus net less fee */
        for (uint32_t i = 0; i < nxb; i++) {
            const xbal_t *b = &xb[i];
            int64_t want;
            uint32_t k = (uint32_t) b->c * np + b->p;
            if (b->open == INT64_MIN || b->role != 0) continue;
            want = b->open;
            sat_add(&want, pos[k]);
            sat_add(&want, used[k] ? -(int64_t) fee[k].fee : 0);
            snprintf(nm, sizeof nm, "bal[%u].close", i);
            if (want != b->close)
                fc(v, ZTW_F_BALANCE, i, nm, want, b->close,
                   "in the journal, %s did not move by its net position less its fee", b->name);
        }
    /* ISO 20022: re-rendered from the replay */
    for (uint32_t i = 0; i < nxp; i++) {
        if (xp[i].kind == 3) continue;
        st = iso_pacs(v, c, &xp[i], &xi[nxi++]);
        if (st != ZXV_OK) goto out;
    }
    for (uint32_t cc = 0; cc < nc; cc++)
        for (uint32_t p = 0; p < np; p++) {
            const xbal_t *b = NULL;
            uint32_t ent = 0;
            for (uint32_t i = 0; i < nxb; i++)
                if (xb[i].role == 0 && xb[i].c == cc && xb[i].p == p) b = &xb[i];
            for (uint32_t i = 0; i < nxp; i++) ent += xp[i].c == cc && xp[i].p == p;
            if (!b || !ent) continue;
            if (b->open == INT64_MIN) {
                xi[nxi].kind = 2;
                xi[nxi++].ok = 0;
                continue;
            }
            st = iso_camt(v, c, cc, p, b, xp, nxp, &xi[nxi++]);
            if (st != ZXV_OK) goto out;
        }
    if (c->n_iso != nxi)
        fc(v, ZTW_F_ISO, UINT32_MAX, "n_iso", nxi, c->n_iso,
           "the record lists %u ISO 20022 messages; the replay renders %u", c->n_iso, nxi);
    for (uint32_t i = 0; i < c->n_iso && i < nxi; i++) {
        const ztw_iso *x = &c->iso[i];
        const char *bad = x->kind != xi[i].kind ? "kind" : strcmp(x->msg_id, xi[i].msg_id) ? "msg_id"
                        : !xi[i].ok || memcmp(x->dig, xi[i].dig, 32) ? "sha3" : NULL;
        if (!bad) continue;
        snprintf(nm, sizeof nm, "iso[%u].%s", i, bad);
        fc(v, ZTW_F_ISO, i, nm, xi[i].kind, x->kind,
           xi[i].ok ? "message %s differs from the re-rendered %s"
                    : "message %s: the replay cannot render %s (its balances and entries do not "
                      "reconcile, or a field is invalid)",
           x->msg_id, xi[i].msg_id);
    }
    st = ZXV_OK;
out:
    free(ok);
    free(ci);
    free(di);
    free(ki);
    free(pos);
    free(act);
    free(fee);
    free(used);
    free(from);
    free(to);
    free(amt);
    free(xp);
    free(xb);
    free(xi);
    free(jb0);
    free(jb1);
    return st;
}

/* Carry the state into the next cycle. */
static int32_t advance(V *v, const ztw_cycle *c) {
    uint8_t key[32];
    int32_t st;
    v->prev_no = c->no;
    if (!v->cycles_seen) snprintf(v->house, sizeof v->house, "%s", c->house);
    v->cycles_seen++;
    if (c->head > v->prev_head) v->prev_head = c->head;
    v->n_ccy = c->n_ccy;
    for (uint32_t i = 0; i < c->n_ccy; i++) memcpy(v->ccy[i], c->ccy[i], 4);
    free(v->part);
    v->part = (char(*)[12]) calloc(c->n_part + 1u, 12);
    if (!v->part) return ZXV_E_NOMEM;
    memcpy(v->part, c->code, (size_t) c->n_part * 12);
    v->n_part = c->n_part;
    for (uint32_t s = 0; s < 3; s++) {
        uint32_t n = s == 0 ? c->n_instr : s == 1 ? c->n_wd : c->n_res;
        for (uint32_t i = 0; i < n; i++) {
            st = e2e_key(s == 0 ? c->instr[i].e2e : s == 1 ? c->wd[i].e2e : c->res[i].e2e, key);
            if (st == ZXV_OK) st = zind_seen_add(v->settled, key);
            if (st != ZXV_OK && st != ZIND_E_REPLAY) return st;
        }
    }
    free(v->ph);
    v->n_ph = 0;
    v->ph = (pheld_t *) calloc(c->n_held + 1u, sizeof *v->ph);
    if (!v->ph) return ZXV_E_NOMEM;
    for (uint32_t g = 0; g < c->n_held; g++) {
        pheld_t *p = &v->ph[v->n_ph];
        snprintf(p->e2e, sizeof p->e2e, "%s", c->held[g].e2e);
        p->n = c->held[g].n > 4 ? 4 : c->held[g].n;
        for (uint32_t k = 0; k < p->n; k++)
            if (instr_digest(&c->held[g].v[k], p->dig[k]) != ZXV_OK) p->n = k;
        v->n_ph++;
    }
    return ZXV_OK;
}

/* Every account the house keeps, for the currencies and participants of
 * its last cycle record, is in the journal once, in its currency, and is
 * a holder account (it can never go below zero). */
static void check_accounts(V *v) {
    for (uint32_t c = 0; c < v->n_ccy; c++)
        for (uint32_t k = 0; k < 5u + v->n_part; k++) {
            char name[96];
            uint32_t a = 0;
            int32_t fs;
            snprintf(name, sizeof name, "%s/%s/%s", v->house,
                     k == 0 ? "hub" : k < 5 ? k_bucket[k - 1] : v->part[k - 5], v->ccy[c]);
            fs = jfind_name(v, name, &a);
            if (fs != ZXV_OK) {
                fr(v, ZTW_F_JOURNAL, 0, UINT32_MAX, UINT32_MAX, "journal.account",
                   (const uint8_t *) name, strlen(name), "the journal has %s account %s",
                   fs == ZXV_E_DUPLICATE ? "more than one" : "no", name);
                continue;
            }
            if (strcmp(v->j.acct[a].ccy, v->ccy[c]) != 0)
                fr(v, ZTW_F_JOURNAL, 0, UINT32_MAX, a, "journal.ccy", (const uint8_t *) name,
                   strlen(name), "account %s is in %s", name, v->j.acct[a].ccy);
            if (v->j.acct[a].kind != ZXV_ACCT_HOLDER)
                fr(v, ZTW_F_JOURNAL, 0, UINT32_MAX, a, "journal.kind", (const uint8_t *) name,
                   strlen(name), "house account %s is not a holder account (it could go below zero)",
                   name);
        }
}

static int32_t on_frag(void *arg, int what, uint32_t rec, uint32_t cyc, const uint8_t *blob,
                       size_t len, uint32_t nrec) {
    V *v = (V *) arg;
    ztw_cycle c;
    ztw_fieldlog log;
    int32_t st;
    if (v->fatal) return v->fatal;
    switch (what) {
    case ZTW_FRAG_BADKIND:
        fr(v, ZTW_F_KIND, 0, rec, UINT32_MAX, "kind", NULL, 0,
           "record %u is of a kind the house never writes", rec);
        return ZXV_OK;
    case ZTW_FRAG_BROKEN:
        fr(v, ZTW_F_FRAGMENT, cyc, rec, UINT32_MAX, "fragment", NULL, 0,
           "record %u breaks the sequence of cycle fragments", rec);
        v->lost = 1;
        return ZXV_OK;
    case ZTW_FRAG_INCOMPLETE:
        fr(v, ZTW_F_FRAGMENT, cyc, rec, UINT32_MAX, "fragment", NULL, 0,
           "the cycle record starting at record %u is incomplete", rec);
        v->lost = 1;
        return ZXV_OK;
    default: break;
    }
    memset(&log, 0, sizeof log);
    v->cno = cyc;
    v->crec = rec;
    v->cnrec = nrec;
    v->blob = blob;
    v->log = NULL;
    v->sum.cycles++;
    st = ztw_parse_cycle(blob, len, &c, &log);
    if (st == ZXV_E_NOMEM) {
        ztw_cycle_free(&c);
        ztw_fieldlog_free(&log);
        return st;
    }
    if (st != ZXV_OK) {
        size_t at = c.bad_at < len ? c.bad_at : len;
        fr(v, ZTW_F_FORMAT, cyc, frag_of(v, at), (uint32_t) at, "record",
           blob + at, len - at > 16 ? 16 : len - at,
           "the cycle record does not parse at or after byte %zu", at);
        v->lost = 1;
        v->prev_no = cyc;
        ztw_cycle_free(&c);
        ztw_fieldlog_free(&log);
        return ZXV_OK;
    }
    v->log = &log;
    v->cno = c.no;
    st = check_cycle(v, &c, cyc);
    if (st == ZXV_OK) st = advance(v, &c);
    v->log = NULL;
    v->blob = NULL;
    ztw_cycle_free(&c);
    ztw_fieldlog_free(&log);
    if (st != ZXV_OK) v->fatal = st;
    return st;
}

/* ===== the report ===== */

static int32_t write_report(V *v, uint8_t verdict_tag) {
    ztw_witness *w = v->w;
    uint8_t buf[512];
    int32_t st;
    zind_chain_destroy(w->report);
    w->report = NULL;
    st = zind_chain_create(REPORT_DOMAIN, w->n_f + 2u, &w->report);
    for (uint32_t i = 0; i < w->n_f && st == ZXV_OK; i++) {
        const ztw_finding *f = &w->f[i];
        zind_wr wr;
        zind_wr_init(&wr, buf, sizeof buf);
        zind_wr_u16(&wr, f->code);
        zind_wr_u32(&wr, f->cycle_no);
        zind_wr_u32(&wr, f->record);
        zind_wr_u32(&wr, f->item);
        zind_wr_str(&wr, f->field);
        zind_wr_u64(&wr, (uint64_t) f->expected);
        zind_wr_u64(&wr, (uint64_t) f->found);
        zind_wr_str(&wr, f->detail);
        zind_wr_u8(&wr, f->raw_len);
        zind_wr_bytes(&wr, f->raw, f->raw_len);
        zind_wr_u8(&wr, f->tag);
        zind_wr_bytes(&wr, f->subject, 32);
        if (wr.overflow) return ZXV_E_INTERNAL;
        st = zind_chain_append(w->cap, w->report, w->signer, ZTW_REC_FINDING, buf, wr.len, NULL,
                               NULL);
    }
    if (st == ZXV_OK) {
        zind_wr wr;
        zind_wr_init(&wr, buf, sizeof buf);
        zind_wr_bytes(&wr, "ZTWS1", 5);
        zind_wr_u32(&wr, v->sum.records);
        zind_wr_u32(&wr, v->sum.cycles);
        zind_wr_u32(&wr, v->sum.instructions);
        zind_wr_u32(&wr, v->sum.postings);
        zind_wr_u32(&wr, v->sum.findings);
        zind_wr_u32(&wr, w->n_f);
        zind_wr_u64(&wr, v->sum.journal_postings);
        zind_wr_u64(&wr, v->sum.covered_postings);
        zind_wr_bytes(&wr, v->sum.chain_digest, 32);
        zind_wr_bytes(&wr, v->sum.journal_digest, 32);
        zind_wr_u8(&wr, verdict_tag);
        st = zind_chain_append(w->cap, w->report, w->signer, ZTW_REC_SUMMARY, buf, wr.len, NULL,
                               NULL);
    }
    return st;
}

int32_t ztw_verify(ztw_witness *w, const uint8_t *chain, size_t chain_len, const uint8_t *journal,
                   size_t journal_len, ztw_summary *out) {
    V v;
    zind_chain *ch = NULL;
    uint8_t true_tag = 0;
    int32_t st;
    if (!w || (!chain && chain_len) || (!journal && journal_len)) return ZXV_E_ARG;
    if (w->n_house == 0) {
        zind_set_error("ztw_verify: trust at least one house key first (ztw_trust_house)");
        return ZTW_E_NOTRUST;
    }
    st = zxv_cap_check(w->cap, ZXV_CAP_LEDGER_READ | ZXV_CAP_CRYPTO_SIGN);
    if (st != ZXV_OK) return st;
    memset(&v, 0, sizeof v);
    v.w = w;
    w->n_f = w->total_f = 0;
    zind_chain_destroy(w->report);
    w->report = NULL;
    if ((st = zind_tag(ZXV_LP_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_MANIFEST, &v.forbid_tag)) !=
            ZXV_OK ||
        (st = zind_tag(ZXV_LP_TRUE, ZXV_HEX_POL_NEGENTROPIC, ZXV_HEX_PHASE_MANIFEST, &true_tag)) !=
            ZXV_OK)
        return st;
    if (chain) (void) zxv_sha3_256(chain, chain_len, v.sum.chain_digest);
    if (journal) (void) zxv_sha3_256(journal, journal_len, v.sum.journal_digest);
    st = zind_seen_create(SETTLED_CAP, &v.settled);
    if (st != ZXV_OK) return st;
    /* the journal */
    if (journal) {
        st = ztw_parse_journal(journal, journal_len, &v.j);
        if (st == ZXV_E_NOMEM) goto done;
        v.have_j = st == ZXV_OK;
    }
    if (!v.have_j)
        fr(&v, ZTW_F_JFORMAT, 0, UINT32_MAX, UINT32_MAX, "journal", journal,
           journal_len > 16 ? 16 : journal_len,
           journal ? "the journal export does not parse as ZSJ1"
                   : "no journal was given: postings and balances are not checked against a ledger");
    if (v.have_j) {
        v.sum.journal_postings = v.j.n_post;
        check_journal(&v);
        v.jused = (uint8_t *) calloc(v.j.n_post + 1u, 1);
        if (!v.jused) {
            st = ZXV_E_NOMEM;
            goto done;
        }
    }
    /* the chain */
    st = chain ? zind_chain_import(ZTW_HOUSE_DOMAIN, chain, chain_len, &ch) : ZXV_E_FORMAT;
    if (st == ZXV_E_NOMEM) goto done;
    if (st != ZXV_OK) {
        fr(&v, ZTW_F_IMPORT, 0, 0, UINT32_MAX, "chain", chain, chain_len > 16 ? 16 : chain_len,
           "the chain export does not import as a %s chain (%s)", ZTW_HOUSE_DOMAIN,
           zxv_status_name(st));
        st = ZXV_OK;
    } else {
        v.ch = ch;
        (void) zind_chain_count(ch, &v.n_rec);
        v.sum.records = v.n_rec;
        st = check_records(&v);
        if (st == ZXV_OK) st = ztw_fragments(ch, on_frag, &v);
        if (st == ZXV_OK && v.fatal) st = v.fatal;
        if (st != ZXV_OK) goto done;
        if (v.have_j && v.cycles_seen) check_accounts(&v);
        if (v.have_j) {
            char pfx[16];
            int pl = snprintf(pfx, sizeof pfx, "%s/", v.house);
            for (size_t i = 0; i < v.j.n_post; i++) {
                const ztw_jpost *p = &v.j.post[i];
                if (p->id <= v.max_head) v.sum.covered_postings++;
                if (v.cycles_seen && !v.jused[i] && pl > 1 && p->id <= v.max_head &&
                    strncmp(p->ref, pfx, (size_t) pl) == 0)
                    fr(&v, ZTW_F_JOURNAL, 0, UINT32_MAX, (uint32_t) i, "journal.ref",
                       (const uint8_t *) p->ref, strlen(p->ref),
                       "journal entry %llu carries a house reference (%s) that no cycle record "
                       "covers",
                       (unsigned long long) p->id, p->ref);
            }
        }
    }
    if (v.fatal) {
        st = v.fatal;
        goto done;
    }
    v.sum.findings = w->total_f;
    st = write_report(&v, w->total_f ? v.forbid_tag : true_tag);
    if (st == ZXV_OK && out) *out = v.sum;
    if (st == ZXV_OK && w->total_f) st = ZTW_E_DISCREPANCY;
done:
    zind_chain_destroy(ch);
    zind_seen_destroy(v.settled);
    ztw_journal_free(&v.j);
    free(v.jused);
    free(v.part);
    free(v.carry);
    free(v.ph);
    return st;
}

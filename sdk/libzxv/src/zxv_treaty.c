/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_treaty.c — ABI 1.5: treaties (copybook records and Sutra text) over
 * kernel/src/treaty (zxt.h: parse, validate, emit, PIC S9(18) arithmetic;
 * zxt_exec.h: run on a scratch engine).
 *
 * zxv_engine.h (pulled in by zxt_exec.h) has its own enum of ZXV_E_*
 * names, which zxv_api.h defines as macros with different values. So the
 * kernel headers come FIRST, the engine statuses are copied into ENG_*
 * constants, and only then is zxv_api.h included: from there on every
 * ZXV_E_* in this file is the ABI's. */
#include "zxt_exec.h"

enum {
    ENG_ARG = ZXV_E_ARG,
    ENG_FULL = ZXV_E_FULL,
    ENG_NO_ROUTE = ZXV_E_NO_ROUTE,
    ENG_FUNDS = ZXV_E_FUNDS,
    ENG_LEDGER = ZXV_E_LEDGER,
    ENG_OVERFLOW = ZXV_E_OVERFLOW
};

#include <stdlib.h>
#include <string.h>

#include "zxv_internal.h"

/* The ABI numbers are the kernel's; keep them so. */
_Static_assert(ZXV_TREATY_REC_LEN == ZXT_REC_LEN, "record length");
_Static_assert(ZXV_TREATY_RESULT_LEN == ZXT_RES_LEN, "result length");
_Static_assert(ZXV_TREATY_SUTRA_MAX == ZXT_SUTRA_MAX, "sutra max");
_Static_assert(ZXV_TREATY_MAX_PARTIES == ZXT_MAX_PARTIES, "parties");
_Static_assert(ZXV_TREATY_MAX_OWES == ZXT_MAX_OWES, "owes");
_Static_assert(ZXV_TREATY_NAME_MAX == ZXT_NAME_MAX, "name");
_Static_assert(ZXV_TREATY_ID_MAX == ZXT_ID_MAX, "id");
_Static_assert(ZXV_TREATY_PIC18_MAX == ZXT_PIC18_MAX, "pic");
_Static_assert(ZXV_TREATY_WEIGHT_MAX == ZXT_WEIGHT_MAX, "weight");
_Static_assert(ZXV_TREATY_OP_NET == ZXT_OP_NET && ZXV_TREATY_OP_SETL == ZXT_OP_SETL, "op");
_Static_assert(ZXV_TREATY_FAULT_MAGIC == -ZXT_E_MAGIC && ZXV_TREATY_FAULT_SYNTAX == -ZXT_E_SYNTAX &&
                   ZXV_TREATY_FAULT_NAME == -ZXT_E_NAME && ZXV_TREATY_FAULT_ASSET == -ZXT_E_ASSET &&
                   ZXV_TREATY_FAULT_OP == -ZXT_E_OP && ZXV_TREATY_FAULT_COUNT == -ZXT_E_COUNT &&
                   ZXV_TREATY_FAULT_NUMBER == -ZXT_E_NUMBER &&
                   ZXV_TREATY_FAULT_PARTY == -ZXT_E_PARTY &&
                   ZXV_TREATY_FAULT_WEIGHT == -ZXT_E_WEIGHT &&
                   ZXV_TREATY_FAULT_CLAIM == -ZXT_E_CLAIM && ZXV_TREATY_FAULT_OWE == -ZXT_E_OWE &&
                   ZXV_TREATY_FAULT_PAD == -ZXT_E_PAD,
               "fault codes are the kernel's, negated");

struct zxv_treaty {
    zxt_treaty_t t;
};

struct zxv_treaty_result {
    zxt_result_t r;
};

uint32_t zxvi_feat2_treaty(void)
{
    return ZXV_FEAT2_TREATY;
}

/* ------------------------------------------------------------ status */

static const char *fault_text(int32_t fault)
{
    switch (fault) {
    case ZXV_TREATY_FAULT_MAGIC: return "record does not start with \"ZXT1\"";
    case ZXV_TREATY_FAULT_SYNTAX: return "text outside the Sutra grammar";
    case ZXV_TREATY_FAULT_NAME: return "treaty name is not 1..30 of A-Z 0-9 '-' starting with a letter";
    case ZXV_TREATY_FAULT_ASSET: return "asset is not three capital letters";
    case ZXV_TREATY_FAULT_OP: return "operation is not NET or SETL";
    case ZXV_TREATY_FAULT_COUNT: return "party count not 1..16 or obligation count not 0..64";
    case ZXV_TREATY_FAULT_NUMBER: return "malformed number or outside PIC S9(18)";
    case ZXV_TREATY_FAULT_PARTY: return "bad, duplicate or unknown party id";
    case ZXV_TREATY_FAULT_WEIGHT:
        return "weights not in lowest terms with num <= den, or not summing to exactly 1 (R1)";
    case ZXV_TREATY_FAULT_CLAIM: return "a claim exceeds its deposit (R2)";
    case ZXV_TREATY_FAULT_OWE: return "an obligation to itself or not above zero";
    case ZXV_TREATY_FAULT_PAD: return "an unused slot or field is not blank (INITIALIZE)";
    default: return "unknown fault";
    }
}

/* A kernel refusal of a treaty: ZXV_E_TREATY with the fault, or
 * ZXV_E_INTERNAL for a kernel status this file does not expect. */
static int32_t treaty_fault(zxt_status_t st, const char *what, bool has_off, uint32_t off,
                            uint32_t *out_off, int32_t *out_fault)
{
    if (st > ZXT_E_MAGIC || st < ZXT_E_PAD)
        return zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, (int) st);
    int32_t fault = -(int32_t) st;
    if (out_off) *out_off = off;
    if (out_fault) *out_fault = fault;
    if (has_off)
        return zxvi_fail(ZXV_E_TREATY, "%s: %s at byte %u", what, fault_text(fault), off);
    return zxvi_fail(ZXV_E_TREATY, "%s: %s", what, fault_text(fault));
}

static int32_t treaty_ok(uint32_t *out_off, int32_t *out_fault)
{
    if (out_off) *out_off = 0;
    if (out_fault) *out_fault = ZXV_TREATY_FAULT_NONE;
    return zxvi_ok();
}

/* ------------------------------------------------------------ handle */

int32_t zxv_treaty_create(zxv_treaty **out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "out is NULL");
    zxv_treaty *t = calloc(1, sizeof *t);
    if (!t) return zxvi_fail(ZXV_E_NOMEM, "treaty_create: out of memory");
    *out = t;
    return zxvi_ok();
}

void zxv_treaty_destroy(zxv_treaty *t)
{
    free(t);
}

int32_t zxv_treaty_clear(zxv_treaty *t)
{
    if (!t) return zxvi_fail(ZXV_E_ARG, "t is NULL");
    zxt_clear(&t->t);
    return zxvi_ok();
}

int32_t zxv_treaty_parse_sutra(zxv_treaty *t, const char *text, uint32_t *out_fault_off,
                               int32_t *out_fault)
{
    if (!t) return zxvi_fail(ZXV_E_ARG, "t is NULL");
    int32_t rc = zxvi_check_str(text, ZXT_SUTRA_MAX, "text", true);
    if (rc) return rc;
    zxt_treaty_t tmp;
    uint32_t off = 0;
    zxt_status_t st = zxt_parse_sutra(text, (uint32_t) strlen(text), &tmp, &off);
    if (st != ZXT_OK) return treaty_fault(st, "parse_sutra", true, off, out_fault_off, out_fault);
    t->t = tmp;
    return treaty_ok(out_fault_off, out_fault);
}

int32_t zxv_treaty_parse_record(zxv_treaty *t, const uint8_t *rec, size_t len,
                                uint32_t *out_fault_off, int32_t *out_fault)
{
    if (!t || !rec) return zxvi_fail(ZXV_E_ARG, "t or rec is NULL");
    if (len != ZXT_REC_LEN)
        return zxvi_fail(ZXV_E_ARG, "record is %zu bytes; a treaty record is exactly %u", len,
                         (unsigned) ZXT_REC_LEN);
    zxt_treaty_t tmp;
    uint32_t off = 0;
    zxt_status_t st = zxt_parse_record(rec, ZXT_REC_LEN, &tmp, &off);
    if (st != ZXT_OK) return treaty_fault(st, "parse_record", true, off, out_fault_off, out_fault);
    t->t = tmp;
    return treaty_ok(out_fault_off, out_fault);
}

int32_t zxv_treaty_validate(const zxv_treaty *t, int32_t *out_fault)
{
    if (!t) return zxvi_fail(ZXV_E_ARG, "t is NULL");
    zxt_status_t st = zxt_validate(&t->t);
    if (st != ZXT_OK) return treaty_fault(st, "validate", false, 0, NULL, out_fault);
    return treaty_ok(NULL, out_fault);
}

int32_t zxv_treaty_emit_sutra(const zxv_treaty *t, char *buf, size_t cap, size_t *out_len)
{
    if (!t || !out_len) return zxvi_fail(ZXV_E_ARG, "t or out_len is NULL");
    char text[ZXT_SUTRA_MAX];
    uint32_t n = 0;
    zxt_status_t st = zxt_emit_sutra(&t->t, text, (uint32_t) sizeof text, &n);
    if (st != ZXT_OK) return treaty_fault(st, "emit_sutra", false, 0, NULL, NULL);
    return zxvi_out_text_n(text, n, buf, cap, out_len);
}

/* R4 for bytes: *out_len = n always; NULL buf or cap < n is ZXV_E_BUFFER. */
static int32_t out_bytes(const uint8_t *src, size_t n, uint8_t *buf, size_t cap, size_t *out_len,
                         const char *what)
{
    *out_len = n;
    if (!buf || cap < n) return zxvi_fail(ZXV_E_BUFFER, "%s: buffer needs %zu bytes", what, n);
    memcpy(buf, src, n);
    return zxvi_ok();
}

int32_t zxv_treaty_emit_record(const zxv_treaty *t, uint8_t *buf, size_t cap, size_t *out_len)
{
    if (!t || !out_len) return zxvi_fail(ZXV_E_ARG, "t or out_len is NULL");
    uint8_t rec[ZXT_REC_LEN];
    zxt_status_t st = zxt_emit_record(&t->t, rec, (uint32_t) sizeof rec);
    if (st != ZXT_OK) return treaty_fault(st, "emit_record", false, 0, NULL, NULL);
    return out_bytes(rec, sizeof rec, buf, cap, out_len, "emit_record");
}

int32_t zxv_treaty_equal(const zxv_treaty *a, const zxv_treaty *b, int32_t *out_equal)
{
    if (!a || !b || !out_equal) return zxvi_fail(ZXV_E_ARG, "a, b or out_equal is NULL");
    *out_equal = zxt_equal(&a->t, &b->t) ? 1 : 0;
    return zxvi_ok();
}

/* ------------------------------------------------------------ getters */

/* A NUL-terminated kernel field of at most `max` bytes. */
static int32_t out_field(const char *s, size_t max, char *buf, size_t cap, size_t *out_len)
{
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "out_len is NULL");
    size_t n = 0;
    while (n < max && s[n]) n++;
    return zxvi_out_text_n(s, n, buf, cap, out_len);
}

int32_t zxv_treaty_name(const zxv_treaty *t, char *buf, size_t cap, size_t *out_len)
{
    if (!t) return zxvi_fail(ZXV_E_ARG, "t is NULL");
    return out_field(t->t.name, ZXT_NAME_MAX, buf, cap, out_len);
}

int32_t zxv_treaty_asset(const zxv_treaty *t, char *buf, size_t cap, size_t *out_len)
{
    if (!t) return zxvi_fail(ZXV_E_ARG, "t is NULL");
    return out_field(t->t.asset, 3, buf, cap, out_len);
}

int32_t zxv_treaty_header(const zxv_treaty *t, int32_t *out_op, uint32_t *out_parties,
                          uint32_t *out_owes)
{
    if (!t || !out_op || !out_parties || !out_owes)
        return zxvi_fail(ZXV_E_ARG, "t or an out pointer is NULL");
    *out_op = t->t.op;
    *out_parties = t->t.n_party;
    *out_owes = t->t.n_owe;
    return zxvi_ok();
}

int32_t zxv_treaty_party_id(const zxv_treaty *t, uint32_t index, char *buf, size_t cap,
                            size_t *out_len)
{
    if (!t || !out_len) return zxvi_fail(ZXV_E_ARG, "t or out_len is NULL");
    if (index >= t->t.n_party)
        return zxvi_fail(ZXV_E_NOT_FOUND, "party %u: the treaty has %u", index, t->t.n_party);
    return out_field(t->t.party[index].id, ZXT_ID_MAX, buf, cap, out_len);
}

int32_t zxv_treaty_party(const zxv_treaty *t, uint32_t index, int64_t *out_deposit,
                         int64_t *out_claim, uint32_t *out_w_num, uint32_t *out_w_den)
{
    if (!t || !out_deposit || !out_claim || !out_w_num || !out_w_den)
        return zxvi_fail(ZXV_E_ARG, "t or an out pointer is NULL");
    if (index >= t->t.n_party)
        return zxvi_fail(ZXV_E_NOT_FOUND, "party %u: the treaty has %u", index, t->t.n_party);
    const zxt_party_t *p = &t->t.party[index];
    *out_deposit = p->deposit;
    *out_claim = p->claim;
    *out_w_num = p->w_num;
    *out_w_den = p->w_den;
    return zxvi_ok();
}

int32_t zxv_treaty_owe(const zxv_treaty *t, uint32_t index, uint32_t *out_from, uint32_t *out_to,
                       int64_t *out_amount)
{
    if (!t || !out_from || !out_to || !out_amount)
        return zxvi_fail(ZXV_E_ARG, "t or an out pointer is NULL");
    if (index >= t->t.n_owe)
        return zxvi_fail(ZXV_E_NOT_FOUND, "obligation %u: the treaty has %u", index, t->t.n_owe);
    const zxt_owe_t *o = &t->t.owe[index];
    *out_from = o->from;
    *out_to = o->to;
    *out_amount = o->amount;
    return zxvi_ok();
}

/* ------------------------------------------------------------ PIC S9(18) */

static int32_t pic_size_error(const char *op, int64_t a, int64_t b)
{
    if (!zxt_pic_ok(a) || !zxt_pic_ok(b))
        return zxvi_fail(ZXV_E_RANGE, "pic_%s: an operand is outside PIC S9(18)", op);
    return zxvi_fail(ZXV_E_RANGE, "pic_%s: the result is outside PIC S9(18) (size error)", op);
}

int32_t zxv_treaty_pic_add(int64_t a, int64_t b, int64_t *out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "out is NULL");
    int64_t r;
    if (!zxt_pic_add(a, b, &r)) return pic_size_error("add", a, b);
    *out = r;
    return zxvi_ok();
}

int32_t zxv_treaty_pic_sub(int64_t a, int64_t b, int64_t *out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "out is NULL");
    int64_t r;
    if (!zxt_pic_sub(a, b, &r)) return pic_size_error("sub", a, b);
    *out = r;
    return zxvi_ok();
}

int32_t zxv_treaty_pic_mul(int64_t a, int64_t b, int64_t *out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "out is NULL");
    int64_t r;
    if (!zxt_pic_mul(a, b, &r)) return pic_size_error("mul", a, b);
    *out = r;
    return zxvi_ok();
}

int32_t zxv_treaty_pic_div(int64_t a, int64_t b, int64_t *out_q, int64_t *out_rem)
{
    if (!out_q || !out_rem) return zxvi_fail(ZXV_E_ARG, "out_q or out_rem is NULL");
    int64_t q, m;
    if (!zxt_pic_div(a, b, &q, &m)) {
        if (b == 0 && zxt_pic_ok(a))
            return zxvi_fail(ZXV_E_RANGE, "pic_div: division by zero (size error)");
        return pic_size_error("div", a, b);
    }
    *out_q = q;
    *out_rem = m;
    return zxvi_ok();
}

/* ------------------------------------------------------------ run */

static int32_t run_status(int32_t st)
{
    if (st == ZXT_E_SIZE)
        return zxvi_fail(ZXV_E_RANGE, "treaty_run: a net position leaves PIC S9(18)");
    switch (st - ZXT_E_EXEC) {
    case ENG_FUNDS:
        return zxvi_fail(ZXV_E_FUNDS, "treaty_run: a payer cannot cover its legs and the fee, or "
                                      "a claim exceeds what is left after settlement");
    case ENG_ARG:
        return zxvi_fail(ZXV_E_RANGE, "treaty_run: an amount exceeds the engine's per-line "
                                      "limit (2^59) with its fee");
    case ENG_OVERFLOW:
        return zxvi_fail(ZXV_E_RANGE, "treaty_run: an engine sum overflows");
    case ENG_FULL: return zxvi_fail(ZXV_E_CAPACITY, "treaty_run: an engine table is full");
    case ENG_LEDGER:
        return zxvi_fail(ZXV_E_INTERNAL, "treaty_run: the scratch engine failed its invariants");
    default: break;
    }
    return zxvi_fail(ZXV_E_INTERNAL, "treaty_run: unexpected kernel status %d", (int) st);
}

int32_t zxv_treaty_run(const zxv_treaty *t, zxv_treaty_result **out)
{
    if (!t || !out) return zxvi_fail(ZXV_E_ARG, "t or out is NULL");
    zxt_status_t vs = zxt_validate(&t->t);
    if (vs != ZXT_OK) return treaty_fault(vs, "treaty_run", false, 0, NULL, NULL);
    /* The seed only makes the scratch engine's posting ids unique; nothing
     * it changes is reported. Use what CALL 'ZXV_ENGINE' uses (record
     * bytes 4..35: the name and asset), so both give the same bytes. */
    uint8_t rec[ZXT_REC_LEN], seed[32];
    if (zxt_emit_record(&t->t, rec, (uint32_t) sizeof rec) != ZXT_OK)
        return zxvi_fail(ZXV_E_INTERNAL, "treaty_run: a valid treaty failed to emit");
    memcpy(seed, rec + 4, sizeof seed);
    zxv_engine_t *ws = malloc(sizeof *ws);
    zxv_treaty_result *res = calloc(1, sizeof *res);
    if (!ws || !res) {
        free(ws);
        free(res);
        return zxvi_fail(ZXV_E_NOMEM, "treaty_run: out of memory");
    }
    int32_t st = zxt_exec(&t->t, ws, seed, &res->r);
    free(ws);
    if (st != ZXT_OK) {
        free(res);
        return run_status(st);
    }
    *out = res;
    return zxvi_ok();
}

void zxv_treaty_result_destroy(zxv_treaty_result *r)
{
    free(r);
}

int32_t zxv_treaty_result_summary(const zxv_treaty_result *r, uint32_t *out_parties,
                                  uint32_t *out_legs, int64_t *out_fees)
{
    if (!r || !out_parties || !out_legs || !out_fees)
        return zxvi_fail(ZXV_E_ARG, "r or an out pointer is NULL");
    *out_parties = r->r.n_party;
    *out_legs = r->r.n_leg;
    *out_fees = r->r.fees;
    return zxvi_ok();
}

int32_t zxv_treaty_result_party(const zxv_treaty_result *r, uint32_t index, int64_t *out_net,
                                int64_t *out_balance, int64_t *out_dividend)
{
    if (!r || !out_net || !out_balance || !out_dividend)
        return zxvi_fail(ZXV_E_ARG, "r or an out pointer is NULL");
    if (index >= r->r.n_party)
        return zxvi_fail(ZXV_E_NOT_FOUND, "party %u: the result has %u", index, r->r.n_party);
    *out_net = r->r.net[index];
    *out_balance = r->r.balance[index];
    *out_dividend = r->r.dividend[index];
    return zxvi_ok();
}

int32_t zxv_treaty_result_leg(const zxv_treaty_result *r, uint32_t index, uint32_t *out_from,
                              uint32_t *out_to, int64_t *out_amount)
{
    if (!r || !out_from || !out_to || !out_amount)
        return zxvi_fail(ZXV_E_ARG, "r or an out pointer is NULL");
    if (index >= r->r.n_leg)
        return zxvi_fail(ZXV_E_NOT_FOUND, "leg %u: the result has %u", index, r->r.n_leg);
    *out_from = r->r.leg[index].from;
    *out_to = r->r.leg[index].to;
    *out_amount = r->r.leg[index].amount;
    return zxvi_ok();
}

int32_t zxv_treaty_result_record(const zxv_treaty_result *r, uint8_t *buf, size_t cap,
                                 size_t *out_len)
{
    if (!r || !out_len) return zxvi_fail(ZXV_E_ARG, "r or out_len is NULL");
    uint8_t rec[ZXT_RES_LEN];
    if (zxt_emit_result(&r->r, rec, (uint32_t) sizeof rec) != ZXT_OK)
        return zxvi_fail(ZXV_E_INTERNAL, "result_record: a run result failed to emit");
    return out_bytes(rec, sizeof rec, buf, cap, out_len, "result_record");
}

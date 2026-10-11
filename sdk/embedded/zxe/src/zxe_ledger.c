/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxe_ledger.c — an exact double-entry ledger in fixed caller memory, with
 * libzxv's posting rules (see zxe.h). Integers only: every amount and rail
 * total is an int64 bounded by ZXE_AMOUNT_LIMIT (2^53 - 1, or 2^31 - 1 in the
 * q32 profile, as libzxv). */
#include <string.h>

#include "zxe.h"

static int32_t check_str(const char *s, size_t max, int allow_empty)
{
    if (!s) return ZXV_E_ARG;
    const unsigned char *p = (const unsigned char *) s;
    while (*p) { /* the same UTF-8 rules as libzxv (zxvi_utf8_valid) */
        unsigned char c = *p;
        if (c < 0x80) {
            p++;
            continue;
        }
        int n;
        uint32_t cp;
        if (c >= 0xC2 && c <= 0xDF) {
            n = 1;
            cp = c & 0x1Fu;
        } else if (c >= 0xE0 && c <= 0xEF) {
            n = 2;
            cp = c & 0x0Fu;
        } else if (c >= 0xF0 && c <= 0xF4) {
            n = 3;
            cp = c & 0x07u;
        } else {
            return ZXV_E_UTF8;
        }
        for (int i = 1; i <= n; i++) {
            if ((p[i] & 0xC0u) != 0x80u) return ZXV_E_UTF8;
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        if ((n == 2 && cp < 0x800u) || (n == 3 && cp < 0x10000u) || cp > 0x10FFFFu ||
            (cp >= 0xD800u && cp <= 0xDFFFu))
            return ZXV_E_UTF8;
        p += n + 1;
    }
    size_t len = (size_t) ((const char *) p - s);
    if (len > max) return ZXV_E_TOO_LONG;
    if (len == 0 && !allow_empty) return ZXV_E_ARG;
    return ZXV_OK;
}

static int is_upper3(const char *s)
{
    return s && s[0] >= 'A' && s[0] <= 'Z' && s[1] >= 'A' && s[1] <= 'Z' && s[2] >= 'A' &&
           s[2] <= 'Z' && s[3] == 0;
}

static int poisoned(zxe_ledger *l)
{
    int32_t p = 0;
    if (l->poisoned) return 1;
    if (zxv_ctx_is_poisoned(l->ctx, &p) != ZXV_OK || p) {
        l->poisoned = 1;
        return 1;
    }
    return 0;
}

/* The cap gate, then the ledger's own fail-closed check. */
static int32_t gate(zxe_ledger *l, const zxv_cap *cap, uint64_t bit)
{
    if (!l || !l->ready) return ZXV_E_ARG;
    int32_t rc = zxv_cap_check(cap, bit);
    if (rc) return rc;
    return poisoned(l) ? ZXV_E_POISONED : ZXV_OK;
}

static int32_t halt(zxe_ledger *l, const char *why)
{
    l->poisoned = 1;
    (void) zxv_ctx_halt(l->ctx, why);
    return ZXV_E_INVARIANT;
}

int32_t zxe_ledger_init(zxe_ledger *l, zxv_ctx *ctx, zxe_account *accounts, uint32_t max_accounts,
                        zxe_refkey *refs, uint32_t ref_slots)
{
    if (!l || !ctx || !accounts || !max_accounts || !refs || ref_slots < 2 ||
        (ref_slots & (ref_slots - 1)) != 0)
        return ZXV_E_ARG;
    memset(l, 0, sizeof *l);
    if (zxp_mutex_init(&l->mu) != ZXP_OK) return ZXV_E_INTERNAL; /* port not initialised */
    memset(accounts, 0, (size_t) max_accounts * sizeof *accounts);
    memset(refs, 0, (size_t) ref_slots * sizeof *refs);
    l->ctx = ctx;
    l->acct = accounts;
    l->max_acct = max_accounts;
    l->refs = refs;
    l->ref_slots = ref_slots;
    l->next_entry_id = 1;
    l->ready = 1;
    return ZXV_OK;
}

void zxe_ledger_destroy(zxe_ledger *l)
{
    if (!l || !l->ready) return;
    zxp_mutex_destroy(&l->mu);
    memset(l->refs, 0, (size_t) l->ref_slots * sizeof *l->refs);
    l->ready = 0;
}

int32_t zxe_ledger_set_sink(zxe_ledger *l, const zxv_cap *cap, zxe_journal_fn fn, void *user)
{
    int32_t rc = gate(l, cap, ZXV_CAP_LEDGER_POST);
    if (rc) return rc;
    zxp_mutex_lock(&l->mu);
    l->sink = fn;
    l->sink_user = fn ? user : NULL;
    zxp_mutex_unlock(&l->mu);
    return ZXV_OK;
}

static int ccy_find(const zxe_ledger *l, const char *code)
{
    for (uint32_t i = 0; i < l->n_ccy; i++)
        if (strcmp(l->ccy[i].code, code) == 0) return (int) i;
    return -1;
}

int32_t zxe_ledger_ccy_enable(zxe_ledger *l, const zxv_cap *cap, const char *alpha,
                              uint8_t minor_units)
{
    int32_t rc = gate(l, cap, ZXV_CAP_LEDGER_POST);
    if (rc) return rc;
    if (!alpha) return ZXV_E_ARG;
    if (!is_upper3(alpha)) return ZXV_E_CURRENCY;
    if (minor_units > 4) return ZXV_E_ARG;
    zxp_mutex_lock(&l->mu);
    if (ccy_find(l, alpha) >= 0) {
        rc = ZXV_OK; /* idempotent, as libzxv */
    } else if (l->n_ccy >= ZXE_LEDGER_MAX_CCY) {
        rc = ZXV_E_CAPACITY;
    } else {
        memcpy(l->ccy[l->n_ccy].code, alpha, 4);
        l->ccy[l->n_ccy].minor = minor_units;
        l->n_ccy++;
        rc = ZXV_OK;
    }
    zxp_mutex_unlock(&l->mu);
    return rc;
}

int32_t zxe_ledger_open_account(zxe_ledger *l, const zxv_cap *cap, const char *name,
                                const char *ccy, int32_t kind, uint32_t *out_account_id)
{
    int32_t rc = gate(l, cap, ZXV_CAP_LEDGER_POST);
    if (rc) return rc;
    if (!out_account_id) return ZXV_E_ARG;
    if ((rc = check_str(name, ZXE_LEDGER_NAME_MAX, 0))) return rc;
    if ((rc = check_str(ccy, 8, 0))) return rc;
    if (kind != ZXV_ACCT_HOLDER && kind != ZXV_ACCT_ISSUER) return ZXV_E_ARG;
    zxp_mutex_lock(&l->mu);
    int ci = ccy_find(l, ccy);
    if (ci < 0) {
        rc = ZXV_E_CURRENCY;
    } else if (l->n_acct >= l->max_acct) {
        rc = ZXV_E_CAPACITY;
    } else {
        zxe_account *a = &l->acct[l->n_acct];
        memset(a, 0, sizeof *a);
        memcpy(a->name, name, strlen(name) + 1);
        a->ccy = (uint8_t) ci;
        a->kind = (uint8_t) kind;
        *out_account_id = l->n_acct++;
        rc = ZXV_OK;
    }
    zxp_mutex_unlock(&l->mu);
    return rc;
}

static void ref_key(const char *ref, zxe_refkey *out)
{
    uint8_t h[32];
    (void) zxv_sha3_256((const uint8_t *) ref, strlen(ref), h);
    memcpy(out->k, h, sizeof out->k);
    static const zxe_refkey zero;
    if (memcmp(out->k, zero.k, sizeof out->k) == 0) out->k[0] = 1; /* 0 marks an empty slot */
}

static uint32_t ref_slot(const zxe_ledger *l, const zxe_refkey *k, int *found)
{
    static const zxe_refkey zero;
    uint32_t mask = l->ref_slots - 1;
    uint32_t i = ((uint32_t) k->k[0] | (uint32_t) k->k[1] << 8 | (uint32_t) k->k[2] << 16 |
                  (uint32_t) k->k[3] << 24) &
                 mask;
    while (memcmp(l->refs[i].k, zero.k, sizeof zero.k) != 0) {
        if (memcmp(l->refs[i].k, k->k, sizeof k->k) == 0) {
            *found = 1;
            return i;
        }
        i = (i + 1) & mask;
    }
    *found = 0;
    return i;
}

static void put_le(uint8_t *p, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void chain(const uint8_t prev[32], uint64_t id, uint32_t from, uint32_t to, int64_t amount,
                  const char *ccy, const char *ref, uint8_t out[32])
{
    static const char dom[] = "zxe/ledger/v1";
    uint8_t buf[sizeof dom - 1 + 32 + 8 + 4 + 4 + 8 + 1 + 8 + 1 + ZXE_LEDGER_REF_MAX];
    size_t n = 0, lc = strlen(ccy), lr = strlen(ref);
    memcpy(buf, dom, sizeof dom - 1);
    n += sizeof dom - 1;
    memcpy(buf + n, prev, 32);
    n += 32;
    put_le(buf + n, id, 8);
    n += 8;
    put_le(buf + n, from, 4);
    n += 4;
    put_le(buf + n, to, 4);
    n += 4;
    put_le(buf + n, (uint64_t) amount, 8);
    n += 8;
    buf[n++] = (uint8_t) lc;
    memcpy(buf + n, ccy, lc);
    n += lc;
    buf[n++] = (uint8_t) lr;
    memcpy(buf + n, ref, lr);
    n += lr;
    (void) zxv_sha3_256(buf, n, out);
}

int32_t zxe_ledger_post(zxe_ledger *l, const zxv_cap *cap, uint32_t from_account,
                        uint32_t to_account, int64_t amount_minor, const char *ccy,
                        const char *reference, uint64_t *out_entry_id)
{
    int32_t rc = gate(l, cap, ZXV_CAP_LEDGER_POST);
    if (rc) return rc;
    if ((rc = check_str(ccy, 8, 0))) return rc;
    if ((rc = check_str(reference, ZXE_LEDGER_REF_MAX, 0))) return rc;
    if (amount_minor <= 0 || amount_minor > ZXE_AMOUNT_LIMIT) return ZXV_E_RANGE;
    if (from_account == to_account) return ZXV_E_ARG;

    zxp_mutex_lock(&l->mu);
    zxe_account *f = from_account < l->n_acct ? &l->acct[from_account] : NULL;
    zxe_account *t = to_account < l->n_acct ? &l->acct[to_account] : NULL;
    zxe_refkey key;
    uint32_t slot = 0;
    int dup = 0;
    uint8_t head[32];
    if (!f || !t) {
        rc = ZXV_E_NOT_FOUND;
        goto out;
    }
    if (strcmp(l->ccy[f->ccy].code, ccy) != 0 || strcmp(l->ccy[t->ccy].code, ccy) != 0) {
        rc = ZXV_E_CURRENCY;
        goto out;
    }
    /* Validate everything before writing anything. */
    if (f->kind == ZXV_ACCT_HOLDER && f->debit - f->credit < amount_minor) {
        rc = ZXV_E_FUNDS;
        goto out;
    }
    if (f->credit > ZXE_AMOUNT_LIMIT - amount_minor || t->debit > ZXE_AMOUNT_LIMIT - amount_minor) {
        rc = ZXV_E_RANGE;
        goto out;
    }
#ifdef ZXE_Q32
    /* q32, as libzxv: all debits in the ledger together must fit as well. */
    {
        int64_t sum_debit = 0;
        for (uint32_t i = 0; i < l->n_acct; i++) sum_debit += l->acct[i].debit;
        if (sum_debit > ZXE_AMOUNT_LIMIT - amount_minor) {
            rc = ZXV_E_RANGE;
            goto out;
        }
    }
#endif
    ref_key(reference, &key);
    slot = ref_slot(l, &key, &dup);
    if (dup) {
        rc = ZXV_E_DUPLICATE;
        goto out;
    }
    if (l->n_refs + 1 > l->ref_slots / 2) {
        rc = ZXV_E_CAPACITY;
        goto out;
    }
    chain(l->head, l->next_entry_id, from_account, to_account, amount_minor, ccy, reference, head);
    if (l->sink && l->sink(l->sink_user, l->next_entry_id, from_account, to_account, amount_minor,
                           ccy, reference, head) != 0) {
        rc = ZXV_E_IO; /* not journalled, not posted */
        goto out;
    }
    l->refs[slot] = key;
    l->n_refs++;
    f->credit += amount_minor;
    t->debit += amount_minor;
    memcpy(l->head, head, sizeof head);
    if (out_entry_id) *out_entry_id = l->next_entry_id;
    l->next_entry_id++;
    rc = ZXV_OK;
out:
    zxp_mutex_unlock(&l->mu);
    return rc;
}

int32_t zxe_ledger_balance(zxe_ledger *l, const zxv_cap *cap, uint32_t account,
                           int64_t *out_debit_minor, int64_t *out_credit_minor,
                           int64_t *out_equity_minor)
{
    int32_t rc = gate(l, cap, ZXV_CAP_LEDGER_READ);
    if (rc) return rc;
    zxp_mutex_lock(&l->mu);
    if (account >= l->n_acct) {
        rc = ZXV_E_NOT_FOUND;
    } else {
        const zxe_account *a = &l->acct[account];
        if (out_debit_minor) *out_debit_minor = a->debit;
        if (out_credit_minor) *out_credit_minor = a->credit;
        if (out_equity_minor) *out_equity_minor = a->debit - a->credit;
    }
    zxp_mutex_unlock(&l->mu);
    return rc;
}

int32_t zxe_ledger_check(zxe_ledger *l, const zxv_cap *cap)
{
    int32_t rc = gate(l, cap, ZXV_CAP_LEDGER_READ);
    if (rc) return rc;
    zxp_mutex_lock(&l->mu);
    for (uint32_t ci = 0; ci < l->n_ccy && rc == ZXV_OK; ci++) {
        int64_t sd = 0, sc = 0;
        for (uint32_t i = 0; i < l->n_acct; i++) {
            if (l->acct[i].ccy != ci) continue;
            sd += l->acct[i].debit;
            sc += l->acct[i].credit;
        }
        if (sd != sc) rc = halt(l, "zxe_ledger: sum DEBIT != sum CREDIT");
    }
    for (uint32_t i = 0; i < l->n_acct && rc == ZXV_OK; i++) {
        const zxe_account *a = &l->acct[i];
        if (a->debit < 0 || a->credit < 0 || a->debit > ZXE_AMOUNT_LIMIT ||
            a->credit > ZXE_AMOUNT_LIMIT)
            rc = halt(l, "zxe_ledger: rail total out of range");
        else if (a->kind == ZXV_ACCT_HOLDER && a->debit - a->credit < 0)
            rc = halt(l, "zxe_ledger: holder account is negative");
    }
    zxp_mutex_unlock(&l->mu);
    return rc;
}

int32_t zxe_ledger_head(zxe_ledger *l, const zxv_cap *cap, uint8_t out_head[32])
{
    int32_t rc = gate(l, cap, ZXV_CAP_LEDGER_READ);
    if (rc) return rc;
    if (!out_head) return ZXV_E_ARG;
    zxp_mutex_lock(&l->mu);
    memcpy(out_head, l->head, 32);
    zxp_mutex_unlock(&l->mu);
    return ZXV_OK;
}

int32_t zxe_ledger_reset_refs(zxe_ledger *l, const zxv_cap *cap)
{
    int32_t rc = gate(l, cap, ZXV_CAP_LEDGER_POST);
    if (rc) return rc;
    zxp_mutex_lock(&l->mu);
    memset(l->refs, 0, (size_t) l->ref_slots * sizeof *l->refs);
    l->n_refs = 0;
    zxp_mutex_unlock(&l->mu);
    return ZXV_OK;
}

#ifdef ZXE_TESTING
void zxe_ledger_test_corrupt(zxe_ledger *l, uint32_t account, int64_t delta_debit)
{
    if (l && account < l->n_acct) l->acct[account].debit += delta_debit;
}
#endif

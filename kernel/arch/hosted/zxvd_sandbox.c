/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_sandbox.c — the sandbox books, Ed25519 keys and readers the economy
 * method groups share. See zxvd_sandbox.h C1-C4. */
#include "zxvd_sandbox.h"
#include "ed25519_verify.h"
#include "sha256.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int zxvd_sandbox_book_open(zxvd_sandbox_book_t *b)
{
    pay_ledger_init(&b->L, NULL);
    b->seq = b->funded = 0;
    b->open = false;
    for (uint32_t i = 0; i < b->n_assets; i++) {
        zxvd_sandbox_asset_t *a = &b->a[i];
        if (!a->code) continue;
        if (pay_ledger_add_asset(&b->L, a->code, a->minor, PAY_ASSET_UNIT, &a->asset) != PAY_OK)
            return -1;
        if (a->fundable && pay_ledger_open(&b->L, ZXVD_SANDBOX_ISSUER, a->asset, PAY_CAP_FINANCIAL,
                                           PAY_ACCT_ISSUER, &a->issuer) != PAY_OK)
            return -1;
    }
    b->open = true;
    return 0;
}

const zxvd_sandbox_asset_t *zxvd_sandbox_asset(const zxvd_sandbox_book_t *b, const char *name)
{
    for (uint32_t i = 0; i < b->n_assets; i++)
        if (!strcmp(b->a[i].name, name)) return &b->a[i];
    return NULL;
}

const char *zxvd_sandbox_asset_name(const zxvd_sandbox_book_t *b, uint32_t acct)
{
    const pay_account_t *x = pay_ledger_account(&b->L, acct);
    for (uint32_t i = 0; x && i < b->n_assets; i++)
        if (b->a[i].asset == x->asset) return b->a[i].name;
    return "?";
}

/* ---- C2 ---- */

bool zxvd_sandbox_verify_pk(const uint8_t pk[32], const uint8_t *msg, uint32_t len, const uint8_t *sig,
                         uint32_t sig_len)
{
    return sig_len == ED25519_SIGNATURE_LEN && ed25519_verify(msg, len, sig, pk);
}

bool zxvd_sandbox_verify(const zxvd_sandbox_keys_t *k, uint32_t id, const uint8_t *msg, uint32_t len,
                      const uint8_t *sig, uint32_t sig_len)
{
    for (uint32_t i = 0; i < k->n; i++)
        if (k->id[i] == id) return zxvd_sandbox_verify_pk(k->pk[i], msg, len, sig, sig_len);
    return false; /* an unregistered key signs nothing */
}

/* ---- C3 ---- */

bool zxvd_sandbox_u64(zxvd_call_t *c, const char *key, bool required, uint64_t def, uint64_t *v)
{
    *v = def;
    if (!zxvd_p_has(c, key)) {
        if (required) zxvd_fail(c, ZXVD_E_PARAMS, "missing integer parameter \"%s\"", key);
        return !required;
    }
    int64_t x;
    if (!zxvd_p_amount(c, key, &x)) return false;
    if (x < 0) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must not be negative", key);
        return false;
    }
    *v = (uint64_t) x;
    return true;
}

bool zxvd_sandbox_u32(zxvd_call_t *c, const char *key, bool required, uint32_t def, uint32_t *v)
{
    *v = def;
    if (!zxvd_p_has(c, key)) {
        if (required) zxvd_fail(c, ZXVD_E_PARAMS, "missing integer parameter \"%s\"", key);
        return !required;
    }
    return zxvd_p_u32(c, key, v);
}

bool zxvd_sandbox_need_u64(zxvd_call_t *c, const char *key, uint64_t *v)
{
    return zxvd_sandbox_u64(c, key, true, 0, v);
}

bool zxvd_sandbox_need_u32(zxvd_call_t *c, const char *key, uint32_t *v)
{
    return zxvd_sandbox_u32(c, key, true, 0, v);
}

bool zxvd_sandbox_now(zxvd_call_t *c, uint64_t *now)
{
    time_t t = time(NULL);
    return zxvd_sandbox_u64(c, "now", false, t > 0 ? (uint64_t) t : 0, now);
}

static bool fixed_hex(zxvd_call_t *c, const char *key, uint8_t *out, uint32_t len)
{
    uint32_t n;
    if (!zxvd_p_hex(c, key, out, len, &n, true)) return false;
    if (n != len) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be %u bytes of hex", key, (unsigned) len);
        return false;
    }
    return true;
}

bool zxvd_sandbox_sig(zxvd_call_t *c, const char *key, uint8_t sig[64])
{
    return fixed_hex(c, key, sig, 64);
}

bool zxvd_sandbox_key32(zxvd_call_t *c, const char *key, uint8_t out[32])
{
    return fixed_hex(c, key, out, 32);
}

bool zxvd_sandbox_owner(zxvd_call_t *c, const char *key, uint32_t *v)
{
    if (!zxvd_sandbox_u32(c, key, true, 0, v)) return false;
    if (*v >= ZXVD_SANDBOX_OWNER_MAX) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be below %u (the rest are zxvd's own)", key,
                  (unsigned) ZXVD_SANDBOX_OWNER_MAX);
        return false;
    }
    return true;
}

/* ---- C4 and output ---- */

void zxvd_sandbox_refuse(zxvd_call_t *c, const char *name, const char *why)
{
    zxvd_fail(c, ZXVD_E_ENGINE, "%s: %s", name, why);
}

void zxvd_sandbox_put_u64(zxvd_buf_t *b, uint64_t v)
{
    zxvd_buf_printf(b, "\"%llu\"", (unsigned long long) v);
}

void zxvd_sandbox_kv(zxvd_buf_t *b, const char *prefix, uint64_t v)
{
    zxvd_buf_raw(b, prefix, strlen(prefix));
    zxvd_sandbox_put_u64(b, v);
}

void zxvd_sandbox_put_msg(zxvd_call_t *c, const uint8_t *m, uint32_t n)
{
    zxvd_buf_t *r = zxvd_result(c);
    ZXVD_RAW(r, "{\"message\":");
    zxvd_buf_hex(r, m, n);
    zxvd_buf_printf(r, ",\"bytes\":%u,\"scheme\":\"ed25519\"}", (unsigned) n);
}

bool zxvd_sandbox_ready(zxvd_call_t *c, const zxvd_sandbox_book_t *b, const char *group)
{
    if (b->open) return true;
    zxvd_fail(c, ZXVD_E_ENGINE, "%s is not set up: call %s.setup first", group, group);
    return false;
}

/* ---- the shared methods ---- */

void zxvd_sandbox_m_key(zxvd_call_t *c, zxvd_sandbox_keys_t *k)
{
    uint32_t id;
    uint8_t pk[32];
    if (!zxvd_sandbox_need_u32(c, "key", &id) || !zxvd_sandbox_key32(c, "public_key", pk)) return;
    bool added = true;
    uint32_t i = 0;
    while (i < k->n && k->id[i] != id) i++;
    if (i < k->n) {
        if (memcmp(k->pk[i], pk, 32))
            return zxvd_sandbox_refuse(c, "KEY_TAKEN", "this key id has another public key");
        added = false;
    } else if (k->n == ZXVD_SANDBOX_MAX_KEYS) {
        return zxvd_sandbox_refuse(c, "KEYS_FULL", "the key table is full");
    } else {
        k->id[k->n] = id;
        memcpy(k->pk[k->n++], pk, 32);
    }
    zxvd_buf_printf(zxvd_result(c), "{\"key\":%u,\"added\":%s,\"keys\":%u}", (unsigned) id,
                    added ? "true" : "false", (unsigned) k->n);
}

void zxvd_sandbox_m_account_open(zxvd_call_t *c, zxvd_sandbox_book_t *b)
{
    uint32_t owner, acct;
    char name[16];
    if (!zxvd_sandbox_owner(c, "owner", &owner) || !zxvd_p_str(c, "asset", name, sizeof name, true))
        return;
    const zxvd_sandbox_asset_t *a = zxvd_sandbox_asset(b, name);
    if (!a) return zxvd_fail(c, ZXVD_E_PARAMS, "unknown asset \"%s\"", name);
    if (pay_ledger_open(&b->L, owner, a->asset, PAY_CAP_FINANCIAL, 0, &acct) != PAY_OK)
        return zxvd_sandbox_refuse(c, "PAY_ERR_FULL", "the sandbox ledger has no room");
    zxvd_buf_printf(zxvd_result(c), "{\"account\":%u,\"owner\":%u,\"asset\":\"%s\"}", (unsigned) acct,
                    (unsigned) owner, a->name);
}

void zxvd_sandbox_m_fund(zxvd_call_t *c, zxvd_sandbox_book_t *b)
{
    uint32_t acct;
    uint64_t amt;
    if (!zxvd_sandbox_need_u32(c, "account", &acct) || !zxvd_sandbox_need_u64(c, "amount_minor", &amt))
        return;
    const pay_account_t *x = pay_ledger_account(&b->L, acct);
    const zxvd_sandbox_asset_t *a = NULL;
    for (uint32_t i = 0; x && i < b->n_assets; i++)
        if (b->a[i].asset == x->asset && b->a[i].fundable) a = &b->a[i];
    if (!a || x->owner >= ZXVD_SANDBOX_OWNER_MAX)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"account\" must be a fundable sandbox account");
    pay_posting_req_t r;
    memset(&r, 0, sizeof r);
    uint8_t in[16] = "zxvd-econ-fund", d[32];
    for (int i = 0; i < 8; i++) in[8 + i] ^= (uint8_t) (b->seq >> (8 * i));
    sha256(in, sizeof in, r.idem_key);
    sha256(r.idem_key, 32, d);
    pay_uetr_from_random(d, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "ZXVD-FUND-%llu", (unsigned long long) b->seq);
    snprintf(r.memo, sizeof r.memo, "sandbox units, not money");
    b->seq++;
    pay_receipt_t rc;
    pay_status_t st = pay_ledger_issue(&b->L, &r, a->issuer, acct, amt, &rc);
    if (st != PAY_OK)
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused the amount");
    b->funded += amt;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"account\":%u,\"balance_minor\":", (unsigned) acct);
    zxvd_sandbox_put_u64(o, x->debit);
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

void zxvd_sandbox_m_account(zxvd_call_t *c, const zxvd_sandbox_book_t *b)
{
    uint32_t acct;
    if (!zxvd_sandbox_need_u32(c, "account", &acct)) return;
    const pay_account_t *x = pay_ledger_account(&b->L, acct);
    if (!x) return zxvd_fail(c, ZXVD_E_PARAMS, "no account %u", (unsigned) acct);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"account\":%u,\"owner\":%u,\"asset\":\"%s\",\"balance_minor\":",
                    (unsigned) acct, (unsigned) x->owner, zxvd_sandbox_asset_name(b, acct));
    zxvd_sandbox_put_u64(o, x->debit);
    ZXVD_RAW(o, ",\"credit_minor\":");
    zxvd_sandbox_put_u64(o, x->credit);
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

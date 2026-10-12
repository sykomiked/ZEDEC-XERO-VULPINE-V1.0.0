/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_instruments.c — V-Bonds (bonds.*, kernel/src/vbond) and V-Calls /
 * V-Perps (derivatives.*, kernel/src/vderiv) on zxvd's socket, each behind
 * its own regulated feature switch (zxvd_ext.h X5) and each on its own
 * sandbox ledger (zxvd_sandbox.h C1).
 *
 *   I1 Two switches. features.set turns the method group on; the engine's
 *      own jurisdiction switch (vbond.h B0, vderiv.h V5) stays OFF until a
 *      manifest signed by the gateway key named at setup turns it on
 *      (<group>.switch). Leaving is never blocked: redeeming, settling,
 *      closing and pushing work with the engine switch off.
 *   I2 No interest and no leverage. A V-Bond has no rate: its only return is
 *      a share of what is actually in its pool. Every derivative position is
 *      fully collateralised at open (no margin, no borrowing, no funding
 *      rate); this file adds no switch that could change that.
 *   I3 Ticks are "now" (unix seconds, zxvd_sandbox.h C3): a 233-tick bond
 *      matures 233 seconds after issue. Derivative epochs are pushed by the
 *      caller (derivatives.push), never by the clock.
 *   I4 One setup per run per group; the state lives in memory only.
 */
#include "zxvd_groups.h"
#include "zxvd_sandbox.h"
#include "vbond.h"
#include "vderiv.h"

#include <string.h>

/* ===================== bonds.* ===================== */

static zxvd_sandbox_book_t BB = {.a = {{.name = "vino", .code = "VINO", .minor = 2, .fundable = true},
                                    {.name = "vbill", .code = "VBILL", .fundable = true}},
                              .n_assets = 2};
static zxvd_sandbox_keys_t BK;
static vbond_t BS;

static bool bond_verify(void *ctx, uint32_t key, const uint8_t *m, uint32_t len, const uint8_t *s,
                        uint32_t sl)
{
    return zxvd_sandbox_verify(ctx, key, m, len, s, sl);
}

static void vb_refuse(zxvd_call_t *c, vbond_status_t st)
{
    static const char *const N[][2] = {
        {"VBOND_OK", ""},
        {"VBOND_ERR_ARG", "a bad argument (an account that is not the owner's, a zero amount)"},
        {"VBOND_ERR_DISABLED",
         "issuing is off until a signed jurisdiction manifest turns it on (bonds.switch)"},
        {"VBOND_ERR_SIG", "the signature does not verify"},
        {"VBOND_ERR_EPOCH", "the manifest epoch is not above the last"},
        {"VBOND_ERR_JURIS", "the manifest is for another jurisdiction"},
        {"VBOND_ERR_TERM", "the term is not one of the allowed Fibonacci indices"},
        {"VBOND_ERR_FUNDS", "the payer does not hold enough"},
        {"VBOND_ERR_OVERFLOW", "a value does not fit"},
        {"VBOND_ERR_STATE", "a broken record or ledger"},
        {"VBOND_ERR_LEDGER", "the ledger refused the posting"},
        {"VBOND_ERR_EMPTY", "the pool is empty: nothing to pay"},
        {"VBOND_ERR_NOHOLD", "no live bond to pay"},
        {"VBOND_ERR_FULL", "the bond table is full"},
        {"VBOND_ERR_IMMATURE", "the bond has not matured (no early redemption)"},
        {"VBOND_ERR_PARTIAL", "a later batch was refused; the earlier ones stand"},
        {"VBOND_ERR_NOT_FOUND", "no live bond has that serial"},
        {"VBOND_ERR_OWNER", "not the bond's current owner"},
    };
    uint32_t i = (uint32_t) -st;
    if (i >= sizeof N / sizeof N[0]) i = 9;
    zxvd_sandbox_refuse(c, N[i][0], N[i][1]);
}

static bool bond_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &BB, "bonds");
}

static void put_bond(zxvd_buf_t *o, const vbond_bond_t *b)
{
    zxvd_sandbox_kv(o, "{\"serial\":", b->serial);
    zxvd_buf_printf(o, ",\"owner\":%u,\"payee\":%u", (unsigned) b->owner, (unsigned) b->payee);
    zxvd_sandbox_kv(o, ",\"qty\":", b->qty);
    zxvd_buf_printf(o, ",\"k\":%u", (unsigned) BS.cfg.k[b->term]);
    zxvd_sandbox_kv(o, ",\"start\":", b->start);
    zxvd_sandbox_kv(o, ",\"maturity\":", b->maturity);
    zxvd_sandbox_kv(o, ",\"nonce\":", b->nonce);
    zxvd_sandbox_kv(o, ",\"last_paid_minor\":", b->last_paid);
    ZXVD_RAW(o, "}");
}

static void m_bond_setup(zxvd_call_t *c)
{
    if (BB.open) return zxvd_sandbox_refuse(c, "SET_UP", "bonds are set up already (this run)");
    vbond_cfg_t cfg;
    vbond_cfg_default(&cfg);
    if (!zxvd_sandbox_need_u32(c, "juris", &cfg.juris) ||
        !zxvd_sandbox_need_u32(c, "gateway", &cfg.gateway) ||
        !zxvd_sandbox_u64(c, "share_num", false, cfg.share_num, &cfg.share_num) ||
        !zxvd_sandbox_u64(c, "share_den", false, cfg.share_den, &cfg.share_den))
        return;
    int32_t n = zxvd_p_len(c, "terms"), w = zxvd_p_len(c, "weights");
    if (n < -1 || w < -1) return;
    if (n == 0 || n > (int32_t) VBOND_MAX_TERMS || (w >= 0 && w != n))
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"terms\" must hold 1..%u indices, \"weights\" as many",
                         (unsigned) VBOND_MAX_TERMS);
    for (int32_t i = 0; i < n; i++) {
        int64_t k, wt;
        if (!zxvd_p_i64_at(c, "terms", (uint32_t) i, &k)) return;
        if (k < 0 || k > (int64_t) VBOND_FIB_KMAX)
            return zxvd_fail(c, ZXVD_E_PARAMS, "\"terms\"[%d] is out of range", (int) i);
        cfg.k[i] = (uint32_t) k;
        cfg.weight[i] = vbond_fib((uint32_t) k);
        if (w > 0) {
            if (!zxvd_p_i64_at(c, "weights", (uint32_t) i, &wt)) return;
            cfg.weight[i] = wt > 0 ? (uint64_t) wt : 0;
        }
    }
    if (n > 0) cfg.n_terms = (uint32_t) n;
    cfg.verify = bond_verify;
    cfg.vctx = &BK;
    memcpy(cfg.seed, "zxvd bonds sandbox seed, v1.....", 32);
    if (zxvd_sandbox_book_open(&BB) != 0)
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    cfg.vino = BB.a[0].asset;
    cfg.vbill = BB.a[1].asset;
    vbond_status_t st = pay_ledger_open(&BB.L, ZXVD_SANDBOX_MODULE, BB.a[1].asset, PAY_CAP_FINANCIAL,
                                        0, &cfg.lock) == PAY_OK
                            ? vbond_init(&BS, &BB.L, &cfg)
                            : VBOND_ERR_LEDGER;
    if (st != VBOND_OK) {
        BB.open = false;
        return vb_refuse(c, st);
    }
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"juris\":%u,\"gateway\":%u,\"lock_account\":%u,\"terms\":[",
                    (unsigned) cfg.juris, (unsigned) cfg.gateway, (unsigned) cfg.lock);
    for (uint32_t i = 0; i < cfg.n_terms; i++) {
        zxvd_buf_printf(o, "%s{\"k\":%u", i ? "," : "", (unsigned) cfg.k[i]);
        zxvd_sandbox_kv(o, ",\"ticks\":", vbond_fib(cfg.k[i]));
        zxvd_sandbox_kv(o, ",\"weight\":", cfg.weight[i]);
        ZXVD_RAW(o, "}");
    }
    zxvd_sandbox_kv(o, "],\"share_num\":", cfg.share_num);
    zxvd_sandbox_kv(o, ",\"share_den\":", cfg.share_den);
    ZXVD_RAW(o, ",\"issuing_enabled\":false}");
}

static void m_bond_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_key(c, &BK);
}
static void m_bond_account_open(zxvd_call_t *c)
{
    if (bond_ready(c)) zxvd_sandbox_m_account_open(c, &BB);
}
static void m_bond_fund(zxvd_call_t *c)
{
    if (bond_ready(c)) zxvd_sandbox_m_fund(c, &BB);
}
static void m_bond_account(zxvd_call_t *c)
{
    if (bond_ready(c)) zxvd_sandbox_m_account(c, &BB);
}

static void m_bond_switch(zxvd_call_t *c)
{
    bool on;
    uint64_t epoch;
    uint8_t sig[64];
    if (!bond_ready(c) || !zxvd_p_bool(c, "enable", true, &on) ||
        !zxvd_sandbox_need_u64(c, "epoch", &epoch) || !zxvd_sandbox_sig(c, "sig", sig))
        return;
    vbond_status_t st = vbond_enable(&BS, BS.cfg.juris, on, epoch, sig, 64);
    if (st != VBOND_OK) return vb_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"issuing_enabled\":%s,\"epoch\":", on ? "true" : "false");
    zxvd_sandbox_put_u64(zxvd_result(c), epoch);
    ZXVD_RAW(zxvd_result(c), "}");
}

static void m_bond_message(zxvd_call_t *c)
{
    char kind[16];
    if (!bond_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (!strcmp(kind, "switch")) {
        bool on;
        uint64_t epoch;
        uint8_t m[VBOND_SWITCH_MSG];
        if (!zxvd_p_bool(c, "enable", true, &on) || !zxvd_sandbox_need_u64(c, "epoch", &epoch)) return;
        vbond_switch_msg(BS.cfg.juris, on, epoch, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    if (!strcmp(kind, "transfer")) {
        uint64_t serial;
        uint32_t to, payee;
        uint8_t m[VBOND_TX_MSG];
        if (!zxvd_sandbox_need_u64(c, "serial", &serial) || !zxvd_sandbox_need_u32(c, "to", &to) ||
            !zxvd_sandbox_need_u32(c, "payee", &payee))
            return;
        const vbond_bond_t *b = vbond_get(&BS, serial);
        if (!b) return vb_refuse(c, VBOND_ERR_NOT_FOUND);
        vbond_transfer_msg(BS.cfg.juris, serial, to, payee, b->nonce, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"switch\" or \"transfer\"");
}

static void m_bond_issue(zxvd_call_t *c)
{
    uint32_t owner, vbill, payee, k;
    uint64_t qty, now, serial = 0;
    if (!bond_ready(c) || !zxvd_sandbox_owner(c, "owner", &owner) ||
        !zxvd_sandbox_need_u32(c, "vbill_account", &vbill) ||
        !zxvd_sandbox_need_u32(c, "payee", &payee) || !zxvd_sandbox_need_u64(c, "qty", &qty) ||
        !zxvd_sandbox_need_u32(c, "k", &k) || !zxvd_sandbox_now(c, &now))
        return;
    vbond_status_t st = vbond_issue(&BS, owner, vbill, payee, qty, k, now, &serial);
    if (st != VBOND_OK) return vb_refuse(c, st);
    put_bond(zxvd_result(c), vbond_get(&BS, serial));
}

static void m_bond_transfer(zxvd_call_t *c)
{
    uint64_t serial;
    uint32_t from, to, payee;
    uint8_t sig[64];
    if (!bond_ready(c) || !zxvd_sandbox_need_u64(c, "serial", &serial) ||
        !zxvd_sandbox_need_u32(c, "from", &from) || !zxvd_sandbox_owner(c, "to", &to) ||
        !zxvd_sandbox_need_u32(c, "payee", &payee) || !zxvd_sandbox_sig(c, "sig", sig))
        return;
    vbond_status_t st = vbond_transfer(&BS, serial, from, to, payee, sig, 64);
    if (st != VBOND_OK) return vb_refuse(c, st);
    put_bond(zxvd_result(c), vbond_get(&BS, serial));
}

static void m_bond_redeem(zxvd_call_t *c)
{
    uint64_t serial, now;
    uint32_t owner, vbill;
    if (!bond_ready(c) || !zxvd_sandbox_need_u64(c, "serial", &serial) ||
        !zxvd_sandbox_need_u32(c, "owner", &owner) ||
        !zxvd_sandbox_need_u32(c, "vbill_account", &vbill) || !zxvd_sandbox_now(c, &now))
        return;
    const vbond_bond_t *b = vbond_get(&BS, serial);
    uint64_t qty = b ? b->qty : 0;
    vbond_status_t st = vbond_redeem(&BS, serial, owner, vbill, now);
    if (st != VBOND_OK) return vb_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_sandbox_kv(o, "{\"serial\":", serial);
    zxvd_sandbox_kv(o, ",\"returned_qty\":", qty);
    zxvd_buf_printf(o, ",\"vbill_account\":%u}", (unsigned) vbill);
}

/* B4: realised revenue into the pool, given (amount_minor) or as the
 * premium share of an auction epoch (price, floor, qty) */
static void m_bond_credit(zxvd_call_t *c)
{
    uint32_t src, pool;
    uint64_t now, amt = 0;
    if (!bond_ready(c) || !zxvd_sandbox_need_u32(c, "src", &src) ||
        !zxvd_sandbox_need_u32(c, "pool", &pool) || !zxvd_sandbox_now(c, &now))
        return;
    vbond_status_t st;
    bool premium = zxvd_p_has(c, "price");
    if (premium) {
        uint64_t price, fl, qty;
        if (!zxvd_sandbox_need_u64(c, "price", &price) || !zxvd_sandbox_need_u64(c, "floor", &fl) ||
            !zxvd_sandbox_need_u64(c, "qty", &qty))
            return;
        st = vbond_premium_credit(&BS, src, pool, price, fl, qty, now, &amt);
    } else {
        if (!zxvd_sandbox_need_u64(c, "amount_minor", &amt)) return;
        st = vbond_pool_credit(&BS, src, pool, amt, now);
    }
    if (st != VBOND_OK) return vb_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_sandbox_kv(o, "{\"credited_minor\":", amt);
    zxvd_sandbox_kv(o, ",\"pool_minor\":", pay_ledger_account(&BB.L, pool)->debit);
    zxvd_buf_printf(o, ",\"premium\":%s}", premium ? "true" : "false");
}

static void m_bond_distribute(zxvd_call_t *c)
{
    uint32_t pool;
    uint64_t now;
    vbond_dist_t d;
    if (!bond_ready(c) || !zxvd_sandbox_need_u32(c, "pool", &pool) || !zxvd_sandbox_now(c, &now)) return;
    vbond_status_t st = vbond_distribute(&BS, pool, now, &d);
    if (st != VBOND_OK) return vb_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_sandbox_kv(o, "{\"pool_minor\":", d.pool);
    zxvd_sandbox_kv(o, ",\"units\":", d.units);
    zxvd_sandbox_kv(o, ",\"paid_minor\":", d.paid);
    zxvd_buf_printf(o, ",\"payees\":%u,\"postings\":%u,\"paid\":[", (unsigned) d.payees,
                    (unsigned) d.postings);
    bool first = true;
    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++)
        if (BS.bond[i].used && BS.bond[i].last_paid) {
            zxvd_sandbox_kv(o, first ? "{\"serial\":" : ",{\"serial\":", BS.bond[i].serial);
            zxvd_buf_printf(o, ",\"payee\":%u", (unsigned) BS.bond[i].payee);
            zxvd_sandbox_kv(o, ",\"minor\":", BS.bond[i].last_paid);
            ZXVD_RAW(o, "}");
            first = false;
        }
    ZXVD_RAW(o, "]}");
}

static void m_bond_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u", BB.open ? "true" : "false", (unsigned) BK.n);
    if (BB.open) {
        zxvd_buf_printf(o, ",\"issuing_enabled\":%s,\"juris\":%u,\"gateway\":%u",
                        BS.enabled ? "true" : "false", (unsigned) BS.cfg.juris,
                        (unsigned) BS.cfg.gateway);
        zxvd_sandbox_kv(o, ",\"switch_epoch\":", BS.switch_epoch);
        zxvd_sandbox_kv(o, ",\"locked_qty\":", pay_ledger_account(&BB.L, BS.cfg.lock)->debit);
        zxvd_sandbox_kv(o, ",\"postings\":", BS.posted);
        ZXVD_RAW(o, ",\"bonds\":[");
        bool first = true;
        for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++)
            if (BS.bond[i].used) {
                if (!first) ZXVD_RAW(o, ",");
                put_bond(o, &BS.bond[i]);
                first = false;
            }
        zxvd_buf_printf(o, "],\"check\":%s", vbond_check(&BS) == VBOND_OK ? "true" : "false");
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void bond_close(void)
{
    BB.open = false;
    BK.n = 0;
    memset(&BS, 0, sizeof BS);
}

static const zxvd_group_method_t BOND_M[] = {
    {"bonds.setup", m_bond_setup, false},
    {"bonds.key", m_bond_key, false},
    {"bonds.account_open", m_bond_account_open, false},
    {"bonds.fund", m_bond_fund, false},
    {"bonds.account", m_bond_account, true},
    {"bonds.switch", m_bond_switch, false},
    {"bonds.message", m_bond_message, true},
    {"bonds.issue", m_bond_issue, false},
    {"bonds.transfer", m_bond_transfer, false},
    {"bonds.redeem", m_bond_redeem, false},
    {"bonds.credit", m_bond_credit, false},
    {"bonds.distribute", m_bond_distribute, false},
    {"bonds.status", m_bond_status, true},
};

const zxvd_group_t *zxvd_bonds_group(void)
{
    static const zxvd_group_t G = {
        "bonds",
        true,
        "V-Bonds (vbond.h): no rate, a share of realised revenue only; likely a security; "
        "sandbox ledger in memory",
        BOND_M,
        sizeof BOND_M / sizeof BOND_M[0],
        bond_close};
    return &G;
}

/* ===================== derivatives.* ===================== */

static zxvd_sandbox_book_t DB = {.a = {{.name = "vino", .code = "VINO", .minor = 2, .fundable = true}},
                              .n_assets = 1};
static zxvd_sandbox_keys_t DK;
static vderiv_t DS;

static bool deriv_verify(void *ctx, uint32_t key, const uint8_t *m, uint32_t len, const uint8_t *s,
                         uint32_t sl)
{
    return zxvd_sandbox_verify(ctx, key, m, len, s, sl);
}

static void vd_refuse(zxvd_call_t *c, vderiv_status_t st)
{
    static const char *const N[][2] = {
        {"VDERIV_OK", ""},
        {"VDERIV_ERR_ARG",
         "a bad argument (an unusable account, the same party twice, a zero size)"},
        {"VDERIV_ERR_DISABLED",
         "opening is off until a signed jurisdiction manifest turns it on (derivatives.switch)"},
        {"VDERIV_ERR_SIG", "a signature does not verify"},
        {"VDERIV_ERR_EPOCH", "an epoch out of order, an expiry already passed, or no index yet"},
        {"VDERIV_ERR_JURIS", "the manifest is for another jurisdiction"},
        {"VDERIV_ERR_NONCE", "the terms' nonce is not the next one"},
        {"VDERIV_ERR_FULL", "the position table is full"},
        {"VDERIV_ERR_OVERFLOW", "a lock or a total past 2^59"},
        {"VDERIV_ERR_STATE", "wrong position state (not fixed yet, or not open)"},
        {"VDERIV_ERR_FUNDS", "a payer does not hold enough"},
        {"VDERIV_ERR_LEDGER", "the ledger refused the posting"},
    };
    uint32_t i = (uint32_t) -st;
    if (i >= sizeof N / sizeof N[0]) i = 11;
    zxvd_sandbox_refuse(c, N[i][0], N[i][1]);
}

static bool deriv_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &DB, "derivatives");
}

static void m_deriv_setup(zxvd_call_t *c)
{
    if (DB.open) return zxvd_sandbox_refuse(c, "SET_UP", "derivatives are set up already (this run)");
    vderiv_cfg_t cfg;
    vderiv_cfg_default(&cfg);
    if (!zxvd_sandbox_need_u32(c, "juris", &cfg.juris) ||
        !zxvd_sandbox_need_u32(c, "gateway", &cfg.gateway) ||
        !zxvd_sandbox_u64(c, "first_epoch", false, 1, &cfg.first_epoch))
        return;
    cfg.verify = deriv_verify;
    cfg.vctx = &DK;
    memcpy(cfg.seed, "zxvd derivatives sandbox seed v1", 32);
    if (zxvd_sandbox_book_open(&DB) != 0)
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    cfg.vino = DB.a[0].asset;
    uint32_t *acct[4] = {&cfg.escrow, &cfg.esc_long, &cfg.esc_short, &cfg.reserve};
    vderiv_status_t st = VDERIV_OK;
    for (int i = 0; i < 4 && st == VDERIV_OK; i++)
        if (pay_ledger_open(&DB.L, ZXVD_SANDBOX_MODULE, DB.a[0].asset, PAY_CAP_FINANCIAL, 0,
                            acct[i]) != PAY_OK)
            st = VDERIV_ERR_LEDGER;
    if (st == VDERIV_OK) st = vderiv_init(&DS, &DB.L, &cfg);
    if (st != VDERIV_OK) {
        DB.open = false;
        return vd_refuse(c, st);
    }
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o,
                    "{\"juris\":%u,\"gateway\":%u,\"call_escrow\":%u,\"long_escrow\":%u,"
                    "\"short_escrow\":%u,\"reserve\":%u",
                    (unsigned) cfg.juris, (unsigned) cfg.gateway, (unsigned) cfg.escrow,
                    (unsigned) cfg.esc_long, (unsigned) cfg.esc_short, (unsigned) cfg.reserve);
    zxvd_sandbox_kv(o, ",\"first_epoch\":", cfg.first_epoch);
    ZXVD_RAW(o, ",\"opening_enabled\":false,\"leverage\":false}");
}

static void m_deriv_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_key(c, &DK);
}
static void m_deriv_account_open(zxvd_call_t *c)
{
    if (deriv_ready(c)) zxvd_sandbox_m_account_open(c, &DB);
}
static void m_deriv_fund(zxvd_call_t *c)
{
    if (deriv_ready(c)) zxvd_sandbox_m_fund(c, &DB);
}
static void m_deriv_account(zxvd_call_t *c)
{
    if (deriv_ready(c)) zxvd_sandbox_m_account(c, &DB);
}

static void m_deriv_switch(zxvd_call_t *c)
{
    bool on;
    uint64_t epoch;
    uint8_t sig[64];
    if (!deriv_ready(c) || !zxvd_p_bool(c, "enable", true, &on) ||
        !zxvd_sandbox_need_u64(c, "epoch", &epoch) || !zxvd_sandbox_sig(c, "sig", sig))
        return;
    vderiv_status_t st = vderiv_enable(&DS, DS.cfg.juris, on, epoch, sig, 64);
    if (st != VDERIV_OK) return vd_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"opening_enabled\":%s,\"epoch\":", on ? "true" : "false");
    zxvd_sandbox_put_u64(zxvd_result(c), epoch);
    ZXVD_RAW(zxvd_result(c), ",\"leverage\":false}");
}

static bool call_terms(zxvd_call_t *c, vderiv_call_terms_t *t)
{
    return zxvd_sandbox_u64(c, "nonce", false, DS.nonce, &t->nonce) &&
           zxvd_sandbox_need_u32(c, "writer_key", &t->writer_key) &&
           zxvd_sandbox_need_u32(c, "writer", &t->writer) &&
           zxvd_sandbox_need_u32(c, "buyer_key", &t->buyer_key) &&
           zxvd_sandbox_need_u32(c, "buyer", &t->buyer) &&
           zxvd_sandbox_need_u64(c, "strike", &t->strike) && zxvd_sandbox_need_u64(c, "cap", &t->cap) &&
           zxvd_sandbox_need_u64(c, "size", &t->size) && zxvd_sandbox_need_u64(c, "expiry", &t->expiry) &&
           zxvd_sandbox_need_u64(c, "premium", &t->premium);
}

static bool perp_terms(zxvd_call_t *c, vderiv_perp_terms_t *t)
{
    return zxvd_sandbox_u64(c, "nonce", false, DS.nonce, &t->nonce) &&
           zxvd_sandbox_need_u32(c, "long_key", &t->long_key) &&
           zxvd_sandbox_need_u32(c, "long_account", &t->long_acct) &&
           zxvd_sandbox_need_u32(c, "short_key", &t->short_key) &&
           zxvd_sandbox_need_u32(c, "short_account", &t->short_acct) &&
           zxvd_sandbox_need_u64(c, "size", &t->size) && zxvd_sandbox_need_u64(c, "band", &t->band) &&
           zxvd_sandbox_need_u64(c, "lock_long", &t->lock_long) &&
           zxvd_sandbox_need_u64(c, "lock_short", &t->lock_short);
}

static bool side_of(zxvd_call_t *c, uint8_t *side)
{
    char s[8];
    if (!zxvd_p_str(c, "side", s, sizeof s, true)) return false;
    if (strcmp(s, "long") && strcmp(s, "short")) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"side\" must be \"long\" or \"short\"");
        return false;
    }
    *side = s[0] == 'l' ? VDERIV_LONG : VDERIV_SHORT;
    return true;
}

static bool perp_id(zxvd_call_t *c, uint32_t *id)
{
    if (!zxvd_sandbox_need_u32(c, "id", id)) return false;
    if (*id < VDERIV_MAX_PERPS && DS.perp[*id].state != VDERIV_FREE) return true;
    vd_refuse(c, VDERIV_ERR_STATE);
    return false;
}

static void m_deriv_message(zxvd_call_t *c)
{
    char kind[16];
    if (!deriv_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (!strcmp(kind, "switch")) {
        bool on;
        uint64_t epoch;
        uint8_t m[VDERIV_SWITCH_MSG];
        if (!zxvd_p_bool(c, "enable", true, &on) || !zxvd_sandbox_need_u64(c, "epoch", &epoch)) return;
        vderiv_switch_msg(DS.cfg.juris, on, epoch, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    if (!strcmp(kind, "call")) {
        vderiv_call_terms_t t;
        uint8_t m[VDERIV_CALL_MSG];
        if (!call_terms(c, &t)) return;
        vderiv_call_msg(&t, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    if (!strcmp(kind, "perp")) {
        vderiv_perp_terms_t t;
        uint8_t m[VDERIV_PERP_MSG];
        if (!perp_terms(c, &t)) return;
        vderiv_perp_msg(&t, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    if (!strcmp(kind, "close")) {
        uint32_t id;
        uint8_t side, m[VDERIV_CLOSE_MSG];
        if (!perp_id(c, &id) || !side_of(c, &side)) return;
        vderiv_close_msg(DS.perp[id].t.nonce, side, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"switch\", \"call\", \"perp\" or \"close\"");
}

static void m_deriv_push(zxvd_call_t *c)
{
    uint64_t epoch, price, qty, now;
    vderiv_push_t p;
    if (!deriv_ready(c) || !zxvd_sandbox_need_u64(c, "epoch", &epoch) ||
        !zxvd_sandbox_need_u64(c, "price", &price) || !zxvd_sandbox_need_u64(c, "qty", &qty) ||
        !zxvd_sandbox_now(c, &now))
        return;
    vderiv_status_t st = vderiv_index_push(&DS, epoch, price, qty, now, &p);
    if (st != VDERIV_OK) return vd_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_sandbox_kv(o, "{\"epoch\":", epoch);
    zxvd_sandbox_kv(o, ",\"index\":", qty);
    zxvd_buf_printf(o, ",\"moved\":%u,\"stopped\":%u,\"fixed\":%u", (unsigned) p.moved,
                    (unsigned) p.stopped, (unsigned) p.fixed);
    zxvd_sandbox_kv(o, ",\"fees_minor\":", p.fees);
    ZXVD_RAW(o, "}");
}

static void m_deriv_call_open(zxvd_call_t *c)
{
    vderiv_call_terms_t t;
    uint8_t ws[64], bs[64];
    uint64_t now;
    uint32_t id = 0;
    if (!deriv_ready(c) || !call_terms(c, &t) || !zxvd_sandbox_sig(c, "writer_sig", ws) ||
        !zxvd_sandbox_sig(c, "buyer_sig", bs) || !zxvd_sandbox_now(c, &now))
        return;
    vderiv_status_t st = vderiv_call_open(&DS, &t, ws, 64, bs, 64, now, &id);
    if (st != VDERIV_OK) return vd_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"id\":%u", (unsigned) id);
    zxvd_sandbox_kv(o, ",\"nonce\":", t.nonce);
    zxvd_sandbox_kv(o, ",\"lock_minor\":", DS.call[id].lock);
    zxvd_sandbox_kv(o, ",\"premium_minor\":", t.premium);
    zxvd_sandbox_kv(o, ",\"fee_minor\":", pay_assure_fee(t.premium));
    zxvd_sandbox_kv(o, ",\"expiry\":", t.expiry);
    ZXVD_RAW(o, "}");
}

static void m_deriv_call_settle(zxvd_call_t *c)
{
    uint32_t id;
    uint64_t now;
    vderiv_settle_t s;
    if (!deriv_ready(c) || !zxvd_sandbox_need_u32(c, "id", &id) || !zxvd_sandbox_now(c, &now)) return;
    uint64_t fixing = id < VDERIV_MAX_CALLS ? DS.call[id].fixing : 0;
    vderiv_status_t st = vderiv_call_settle(&DS, id, now, &s);
    if (st != VDERIV_OK) return vd_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"id\":%u", (unsigned) id);
    zxvd_sandbox_kv(o, ",\"fixing\":", fixing);
    zxvd_sandbox_kv(o, ",\"payoff_minor\":", s.payoff);
    zxvd_sandbox_kv(o, ",\"fee_minor\":", s.fee);
    zxvd_sandbox_kv(o, ",\"to_buyer_minor\":", s.to_buyer);
    zxvd_sandbox_kv(o, ",\"to_writer_minor\":", s.to_writer);
    ZXVD_RAW(o, "}");
}

static void m_deriv_perp_open(zxvd_call_t *c)
{
    vderiv_perp_terms_t t;
    uint8_t ls[64], ss[64];
    uint64_t now;
    uint32_t id = 0;
    if (!deriv_ready(c) || !perp_terms(c, &t) || !zxvd_sandbox_sig(c, "long_sig", ls) ||
        !zxvd_sandbox_sig(c, "short_sig", ss) || !zxvd_sandbox_now(c, &now))
        return;
    vderiv_status_t st = vderiv_perp_open(&DS, &t, ls, 64, ss, 64, now, &id);
    if (st != VDERIV_OK) return vd_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"id\":%u", (unsigned) id);
    zxvd_sandbox_kv(o, ",\"nonce\":", t.nonce);
    zxvd_sandbox_kv(o, ",\"ref_index\":", DS.perp[id].ref);
    zxvd_sandbox_kv(o, ",\"opened_epoch\":", DS.perp[id].opened);
    ZXVD_RAW(o, "}");
}

static void m_deriv_perp_close(zxvd_call_t *c)
{
    uint32_t id;
    uint8_t side, sig[64];
    uint64_t now, paid[2] = {0, 0};
    if (!deriv_ready(c) || !perp_id(c, &id) || !side_of(c, &side) || !zxvd_sandbox_now(c, &now))
        return;
    bool has_sig = zxvd_p_has(c, "sig");
    if (has_sig && !zxvd_sandbox_sig(c, "sig", sig)) return;
    vderiv_status_t st = vderiv_perp_close(&DS, id, side, has_sig ? sig : NULL, 64, now, paid);
    if (st != VDERIV_OK) return vd_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"id\":%u", (unsigned) id);
    zxvd_sandbox_kv(o, ",\"to_long_minor\":", paid[VDERIV_LONG]);
    zxvd_sandbox_kv(o, ",\"to_short_minor\":", paid[VDERIV_SHORT]);
    ZXVD_RAW(o, "}");
}

static void m_deriv_status(zxvd_call_t *c)
{
    static const char *const ST[] = {"free", "open", "fixed", "stopped"};
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u,\"leverage\":false", DB.open ? "true" : "false",
                    (unsigned) DK.n);
    if (DB.open) {
        zxvd_buf_printf(o, ",\"opening_enabled\":%s,\"juris\":%u,\"gateway\":%u",
                        DS.enabled ? "true" : "false", (unsigned) DS.cfg.juris,
                        (unsigned) DS.cfg.gateway);
        zxvd_sandbox_kv(o, ",\"switch_epoch\":", DS.switch_epoch);
        zxvd_sandbox_kv(o, ",\"next_nonce\":", DS.nonce);
        zxvd_sandbox_kv(o, ",\"pushed\":", DS.n_pushed);
        zxvd_sandbox_kv(o, ",\"last_epoch\":", DS.last_epoch);
        zxvd_sandbox_kv(o, ",\"fees_minor\":", DS.fees_total);
        ZXVD_RAW(o, ",\"calls\":[");
        bool first = true;
        for (uint32_t i = 0; i < VDERIV_MAX_CALLS; i++) {
            const vderiv_call_t *k = &DS.call[i];
            if (k->state == VDERIV_FREE) continue;
            zxvd_buf_printf(o, "%s{\"id\":%u,\"state\":\"%s\",\"writer\":%u,\"buyer\":%u",
                            first ? "" : ",", (unsigned) i, ST[k->state & 3],
                            (unsigned) k->t.writer, (unsigned) k->t.buyer);
            zxvd_sandbox_kv(o, ",\"strike\":", k->t.strike);
            zxvd_sandbox_kv(o, ",\"cap\":", k->t.cap);
            zxvd_sandbox_kv(o, ",\"size\":", k->t.size);
            zxvd_sandbox_kv(o, ",\"expiry\":", k->t.expiry);
            zxvd_sandbox_kv(o, ",\"lock_minor\":", k->lock);
            zxvd_sandbox_kv(o, ",\"fixing\":", k->fixing);
            ZXVD_RAW(o, "}");
            first = false;
        }
        ZXVD_RAW(o, "],\"perps\":[");
        first = true;
        for (uint32_t i = 0; i < VDERIV_MAX_PERPS; i++) {
            const vderiv_perp_t *p = &DS.perp[i];
            if (p->state == VDERIV_FREE) continue;
            zxvd_buf_printf(o, "%s{\"id\":%u,\"state\":\"%s\",\"long\":%u,\"short\":%u",
                            first ? "" : ",", (unsigned) i, ST[p->state & 3],
                            (unsigned) p->t.long_acct, (unsigned) p->t.short_acct);
            zxvd_sandbox_kv(o, ",\"size\":", p->t.size);
            zxvd_sandbox_kv(o, ",\"band\":", p->t.band);
            zxvd_sandbox_kv(o, ",\"long_minor\":", p->bal[VDERIV_LONG]);
            zxvd_sandbox_kv(o, ",\"short_minor\":", p->bal[VDERIV_SHORT]);
            zxvd_sandbox_kv(o, ",\"ref_index\":", p->ref);
            zxvd_sandbox_kv(o, ",\"fees_minor\":", p->fees);
            ZXVD_RAW(o, "}");
            first = false;
        }
        zxvd_buf_printf(o, "],\"check\":%s", vderiv_check(&DS) == VDERIV_OK ? "true" : "false");
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void deriv_close(void)
{
    DB.open = false;
    DK.n = 0;
    memset(&DS, 0, sizeof DS);
}

static const zxvd_group_method_t DERIV_M[] = {
    {"derivatives.setup", m_deriv_setup, false},
    {"derivatives.key", m_deriv_key, false},
    {"derivatives.account_open", m_deriv_account_open, false},
    {"derivatives.fund", m_deriv_fund, false},
    {"derivatives.account", m_deriv_account, true},
    {"derivatives.switch", m_deriv_switch, false},
    {"derivatives.message", m_deriv_message, true},
    {"derivatives.push", m_deriv_push, false},
    {"derivatives.call_open", m_deriv_call_open, false},
    {"derivatives.call_settle", m_deriv_call_settle, false},
    {"derivatives.perp_open", m_deriv_perp_open, false},
    {"derivatives.perp_close", m_deriv_perp_close, false},
    {"derivatives.status", m_deriv_status, true},
};

const zxvd_group_t *zxvd_derivatives_group(void)
{
    static const zxvd_group_t G = {
        "derivatives",
        true,
        "V-Calls and V-Perps (vderiv.h): fully collateralised, no leverage, no funding rate; "
        "regulated derivatives; sandbox ledger in memory",
        DERIV_M,
        sizeof DERIV_M / sizeof DERIV_M[0],
        deriv_close};
    return &G;
}

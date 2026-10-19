/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_econ.c — the economy modules as zxvd method groups (zxvd_ext.h):
 *
 *   vault.*    feature "vault"     svault.h    the protocol solvency vault
 *   swap.*     feature "swap"      vswap.h     Vino <-> V-Bills, lock tiers,
 *                                              the dividend
 *   auction.*  feature "auctions"  vauction.h  V-Bill issuance auctions,
 *                                              backed only
 *   zenny.*    feature "zenny"     zenny.h     Zenny chips
 *   stats.*    feature "stats"     zstats.h    the aggregate statistics feed
 *
 * ONE ECONOMY IN MEMORY (E1-E6)
 *   E1  The five groups share one pay_ledger (kernel/src/pay) built at the
 *       first call: assets VNO (Vino, 2 minor units) and VBL (V-Bills, whole
 *       units), and the system accounts: the Vino issuer, the reserve, the
 *       vault's cash, the auction escrow, the dividend pool, the bounty and
 *       regenerative buckets, the V-Bill issuer, the vault's V-Bills and the
 *       two lock tiers. It lives in memory only: nothing here is saved, it
 *       is not the owner's books (ledger.*), and zxvd_rpc_close forgets it.
 *   E2  ONE RESERVE, as the headers intend: the reserve-floor bucket of the
 *       assurance fee (pay_assure.h B1) is svault's `reserve`, vswap's
 *       `reserve` and vauction's `reserve`. Fees, swap-ins and auction fiat
 *       all land there; the vault sweeps a third of the fee's reserve share
 *       out of it (S1); swap-outs are paid from it (W4). One V-Bill issuer
 *       serves the vault, the swap and the auctions.
 *   E3  zxvd sees no fee flow, so the data it lacks comes in as explicit
 *       parameters: vault.deposit records an assurance fee collected
 *       elsewhere (issued into the four bucket accounts by the exact B1 split,
 *       then the reserve share is swept, S1), and swap.deposit /
 *       swap.withdraw move Vino of a holder into and out of these books.
 *       A holder is a number the owner chooses; swap.deposit opens its Vino
 *       and V-Bill accounts (at most ECON_MAX_HOLDERS).
 *   E4  SIGNATURES. The engines take a verify hook. zxvd holds no
 *       jurisdiction-gateway, minter, oracle, custodian or publisher key yet,
 *       so: swap.enable and zenny.mint are signed by zxvd itself with a
 *       stand-in hook (SHA-256 over a fixed tag, the signer and the message:
 *       it proves only that zxvd's own method made the call, which the
 *       socket's same-user check already says); a co-minter's signature and
 *       a custodian's attestation never verify (BILATERAL chips and commodity
 *       legs are refused); the auction's hook refuses every oracle quote and
 *       warehouse receipt, so its reserve R is the reserve account alone; the
 *       statistics are hash-chained but not signed.
 *   E5  Time: "now" (unix seconds, default time(NULL)) is the engines' tick.
 *   E6  Amounts follow zxvd_rpc.h M3: exact integers in, 64-bit results as
 *       decimal strings. An engine refusal is ZXVD_E_ECON with the engine's
 *       status name (and the ledger's, when the ledger refused).
 *
 * HONEST LIMITS. V-Bills and Zenny chips are likely securities (or deposits,
 * e-money or collective investment schemes) in many jurisdictions; no
 * exemption is claimed and the four regulated features start off. "Zenny"
 * has had no trademark clearance. Nothing here is connected to a bank, a
 * custodian, an oracle or the owner's books.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "zxvd_groups.h"
#include "svault.h"
#include "vswap.h"
#include "vauction.h"
#include "zenny.h"
#include "zstats.h"
#include "sha256.h"

#define ZXVD_E_ECON      (-32050) /* E6: an economy engine refused */
#define ECON_MAX_HOLDERS 32u
#define ECON_SYS         0x80000000u /* owner ids of the system accounts */
#define ECON_PAR         100u        /* Vino minor units per V-Bill at par */
#define ECON_GATEWAY     1u          /* vswap's gateway key id (E4) */
#define ECON_JURIS       0u          /* vswap's jurisdiction: this node */
#define ECON_MAX_U64     ((uint64_t) INT64_MAX)

typedef struct {
    uint32_t id, vino, vbill;
} holder_t;

static struct {
    bool ready;
    pay_ledger_t L;
    uint16_t vno, vbl;
    uint32_t viss, rsv, vcash, esc, dpool, bounty, regen, biss, vvb, lock_m, lock_d;
    holder_t h[ECON_MAX_HOLDERS];
    uint32_t n_h;
    uint64_t seq, fees, nonce;
    svault_t V;
    vswap_t S;
    vauc_t A;
    zenny_t Z;
    uint64_t published;
    uint8_t head[32];
} E;

/* ----- names (E6) ----- */

static const char *const PAY_N[] = {
    "PAY_OK",           "PAY_ERR_ARG",        "PAY_ERR_FULL",      "PAY_ERR_NO_ACCOUNT",
    "PAY_ERR_NO_ASSET", "PAY_ERR_UNBALANCED", "PAY_ERR_FUNDS",     "PAY_ERR_CREDIT",
    "PAY_ERR_OVERFLOW", "PAY_ERR_REPLAY",     "PAY_ERR_DUP_UETR",  "PAY_ERR_UETR",
    "PAY_ERR_CROWN",    "PAY_ERR_INVARIANT",  "PAY_ERR_NOT_FOUND", "PAY_ERR_STATE",
    "PAY_ERR_LIMIT",    "PAY_ERR_POLICY",     "PAY_ERR_USURY"};
static const char *const SV_N[] = {"SVAULT_OK",           "SVAULT_ERR_ARG",    "SVAULT_ERR_STATE",
                                   "SVAULT_ERR_OVERFLOW", "SVAULT_ERR_LEDGER", "SVAULT_ERR_FLOOR",
                                   "SVAULT_ERR_AUTH"};
static const char *const VS_N[] = {"VSWAP_OK",          "VSWAP_ERR_ARG",    "VSWAP_ERR_DISABLED",
                                   "VSWAP_ERR_SIG",     "VSWAP_ERR_EPOCH",  "VSWAP_ERR_JURIS",
                                   "VSWAP_ERR_DUST",    "VSWAP_ERR_FUNDS",  "VSWAP_ERR_OVERFLOW",
                                   "VSWAP_ERR_STATE",   "VSWAP_ERR_LEDGER", "VSWAP_ERR_EMPTY",
                                   "VSWAP_ERR_NOHOLD",  "VSWAP_ERR_FULL",   "VSWAP_ERR_LOCKED",
                                   "VSWAP_ERR_PARTIAL", "VSWAP_ERR_AUTH"};
static const char *const VA_N[] = {"VAUC_OK",           "VAUC_ERR_ARG",    "VAUC_ERR_FULL",
                                   "VAUC_ERR_SIG",      "VAUC_ERR_STATE",  "VAUC_ERR_UNKNOWN",
                                   "VAUC_ERR_DUP",      "VAUC_ERR_STALE",  "VAUC_ERR_EXPIRED",
                                   "VAUC_ERR_QUOTES",   "VAUC_ERR_LEDGER", "VAUC_ERR_FLOOR",
                                   "VAUC_ERR_OVERFLOW", "VAUC_ERR_AUTH"};
static const char *const ZE_N[] = {"ZENNY_OK",
                                   "ZENNY_ERR_ARG",
                                   "ZENNY_ERR_FULL",
                                   "ZENNY_ERR_KIND",
                                   "ZENNY_ERR_LEGS",
                                   "ZENNY_ERR_KAPPA",
                                   "ZENNY_ERR_PRICE",
                                   "ZENNY_ERR_OVERFLOW",
                                   "ZENNY_ERR_INEXACT",
                                   "ZENNY_ERR_WEIGHT",
                                   "ZENNY_ERR_ANCHOR",
                                   "ZENNY_ERR_SIG",
                                   "ZENNY_ERR_ATTEST",
                                   "ZENNY_ERR_LEVERAGE",
                                   "ZENNY_ERR_CREDIT",
                                   "ZENNY_ERR_DUP",
                                   "ZENNY_ERR_IMMUTABLE",
                                   "ZENNY_ERR_CLAIM",
                                   "ZENNY_ERR_NOT_ALLOWED",
                                   "ZENNY_ERR_FUNDS",
                                   "ZENNY_ERR_CHAIN",
                                   "ZENNY_ERR_LOG",
                                   "ZENNY_ERR_UNBACKED",
                                   "ZENNY_ERR_STALE"};
static const char *const ZS_N[] = {"ZSTATS_OK",         "ZSTATS_ERR_ARG",   "ZSTATS_ERR_OVERFLOW",
                                   "ZSTATS_ERR_STATE",  "ZSTATS_ERR_SPACE", "ZSTATS_ERR_FORMAT",
                                   "ZSTATS_ERR_SIG",    "ZSTATS_ERR_CHAIN", "ZSTATS_ERR_EPOCH",
                                   "ZSTATS_ERR_CONSIST"};

/* a string literal */
#define RAW(b, s) zxvd_buf_raw(b, s, sizeof(s) - 1)

#define NAME(t, st) ((st) <= 0 && -(st) < (int) (sizeof t / sizeof t[0]) ? t[-(st)] : "UNKNOWN")

/* a refusal: the engine's name, and the ledger's when the ledger refused */
static void refuse(zxvd_call_t *c, const char *name, bool ledger, pay_status_t ls)
{
    if (ledger)
        zxvd_fail(c, ZXVD_E_ECON, "%s (%s)", name, NAME(PAY_N, (int) ls));
    else
        zxvd_fail(c, ZXVD_E_ECON, "%s", name);
}

/* ----- E4: the stand-in signature ----- */

static void local_sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    sha256_ctx_t x;
    uint8_t w[4] = {(uint8_t) who, (uint8_t) (who >> 8), (uint8_t) (who >> 16),
                    (uint8_t) (who >> 24)};
    sha256_init(&x);
    sha256_update(&x, (const uint8_t *) "zxvd/econ/local1", 16);
    sha256_update(&x, w, 4);
    sha256_update(&x, m, n);
    sha256_final(&x, sig);
}

static bool local_verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                         uint32_t sl)
{
    uint8_t want[32];
    (void) ctx;
    local_sign(who, m, n, want);
    return sl == 32 && !memcmp(want, sig, 32);
}

/* the auction's oracles and custodians: none is wired (E4) */
static bool no_verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                      uint32_t sl)
{
    (void) ctx, (void) who, (void) m, (void) n, (void) sig, (void) sl;
    return false;
}

/* ----- E1: the world ----- */

static bool open_acct(uint16_t asset, uint8_t flags, uint32_t *a)
{
    return pay_ledger_open(&E.L, ECON_SYS, asset, PAY_CAP_FINANCIAL, flags, a) == PAY_OK;
}

static vauc_config_t auc_default(void)
{
    vauc_config_t c;
    memset(&c, 0, sizeof c);
    c.floor_num = 1;
    c.floor_den = 1;
    c.backing = ECON_PAR;
    c.cap = 1000;
    c.p_start = 200;
    c.p_step = 10;
    c.p_min = 100;
    c.max_age = 1;
    c.min_quotes = 1;
    return c;
}

static bool world(void)
{
    if (E.ready) return true;
    memset(&E, 0, sizeof E);
    pay_ledger_init(&E.L, NULL);
    bool ok = pay_ledger_add_asset(&E.L, "VNO", 2, PAY_ASSET_UNIT, &E.vno) == PAY_OK &&
              pay_ledger_add_asset(&E.L, "VBL", 0, PAY_ASSET_UNIT, &E.vbl) == PAY_OK &&
              open_acct(E.vno, PAY_ACCT_ISSUER, &E.viss) && open_acct(E.vno, 0, &E.rsv) &&
              open_acct(E.vno, 0, &E.vcash) && open_acct(E.vno, 0, &E.esc) &&
              open_acct(E.vno, 0, &E.dpool) && open_acct(E.vno, 0, &E.bounty) &&
              open_acct(E.vno, 0, &E.regen) && open_acct(E.vbl, PAY_ACCT_ISSUER, &E.biss) &&
              open_acct(E.vbl, 0, &E.vvb) && open_acct(E.vbl, 0, &E.lock_m) &&
              open_acct(E.vbl, 0, &E.lock_d) &&
              svault_init(&E.V, &E.L, E.rsv, E.vcash, E.biss, E.vvb) == SVAULT_OK &&
              /* the sweep keeps the reserve at the auction floor (1/1 at par) */
              svault_set_floor(&E.V, 1, 1, ECON_PAR) == SVAULT_OK;
    vswap_cfg_t sc;
    vswap_cfg_default(&sc);
    sc.vino = E.vno;
    sc.vbill = E.vbl;
    sc.reserve = E.rsv;
    sc.issuer = E.biss;
    sc.vault = E.vvb;
    sc.lock[VSWAP_TIER_MEDIUM] = E.lock_m;
    sc.lock[VSWAP_TIER_DEEP] = E.lock_d;
    sc.gateway = ECON_GATEWAY;
    sc.juris = ECON_JURIS;
    sc.par_num = ECON_PAR;
    sc.verify = local_verify;
    memcpy(sc.seed, "zxvd/econ/vswap", 15);
    static const uint8_t aseed[32] = "zxvd/econ/vauction";
    vauc_config_t ac = auc_default();
    ok = ok && vswap_init(&E.S, &E.L, &sc) == VSWAP_OK &&
         vauc_init(&E.A, &E.L, &ac, E.rsv, E.esc, E.biss, no_verify, NULL, aseed) == VAUC_OK;
    zenny_init(&E.Z, local_verify, NULL);
    E.ready = ok;
    return ok;
}

static bool ready(zxvd_call_t *c)
{
    if (world()) return true;
    zxvd_fail(c, ZXVD_E_INTERNAL, "the economy could not be set up");
    return false;
}

static void econ_close(void)
{
    memset(&E, 0, sizeof E);
}

/* a posting request of our own (vault.deposit, swap.deposit/withdraw, the
 * vault's sweep, buy and first loss): unique ids from a counter */
static void req_init(pay_posting_req_t *r, uint64_t now, const char *memo)
{
    uint8_t in[17], d[32];
    memset(r, 0, sizeof *r);
    memcpy(in, "zxvd/econ/post/1", 16);
    in[16] = 0;
    sha256_ctx_t x;
    uint8_t s[8];
    for (int i = 0; i < 8; i++) s[i] = (uint8_t) (E.seq >> (8 * i));
    sha256_init(&x);
    sha256_update(&x, in, 16);
    sha256_update(&x, s, 8);
    sha256_final(&x, d);
    memcpy(r->idem_key, d, 32);
    pay_uetr_from_random(d, r->uetr);
    snprintf(r->e2e, sizeof r->e2e, "ZXVD-ECON-%llu", (unsigned long long) E.seq);
    snprintf(r->memo, sizeof r->memo, "%s", memo);
    r->tick = now;
    r->kind = PAY_KIND_TRANSFER;
    E.seq++;
}

/* ----- parameters ----- */

static bool amt(zxvd_call_t *c, const char *key, bool required, uint64_t def, uint64_t *v)
{
    int64_t x;
    if (!required && !zxvd_p_has(c, key)) {
        *v = def;
        return true;
    }
    if (!zxvd_p_amount(c, key, &x)) return false;
    if (x < 0) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must not be negative", key);
        return false;
    }
    *v = (uint64_t) x;
    return true;
}

/* A required amount: integer or decimal string, never negative. */
static bool need(zxvd_call_t *c, const char *key, uint64_t *v)
{
    return amt(c, key, true, 0, v);
}

static bool nowp(zxvd_call_t *c, uint64_t *now)
{
    return amt(c, "now", false, (uint64_t) time(NULL), now);
}

static holder_t *holder_of(uint32_t id)
{
    for (uint32_t i = 0; i < E.n_h; i++)
        if (E.h[i].id == id) return &E.h[i];
    return NULL;
}

static holder_t *holder_by_acct(uint32_t a)
{
    for (uint32_t i = 0; i < E.n_h; i++)
        if (E.h[i].vino == a || E.h[i].vbill == a) return &E.h[i];
    return NULL;
}

static holder_t *holder(zxvd_call_t *c, const char *key)
{
    uint32_t id;
    if (!zxvd_p_u32(c, key, &id)) return NULL;
    holder_t *h = holder_of(id);
    if (!h) zxvd_fail(c, ZXVD_E_ECON, "unknown holder %u: swap.deposit opens one", (unsigned) id);
    return h;
}

static uint64_t debit(uint32_t a)
{
    return pay_ledger_account(&E.L, a)->debit;
}

/* ----- output ----- */

static void u64s(zxvd_buf_t *b, const char *key, uint64_t v)
{
    zxvd_buf_printf(b, ",\"%s\":\"%llu\"", key, (unsigned long long) v);
}

static void flag(zxvd_buf_t *b, const char *key, bool v)
{
    zxvd_buf_printf(b, ",\"%s\":%s", key, v ? "true" : "false");
}

static void rat(zxvd_buf_t *b, const char *key, uint64_t num, uint64_t den)
{
    zxvd_buf_printf(b, ",\"%s\":{\"num\":\"%llu\",\"den\":\"%llu\"}", key, (unsigned long long) num,
                    (unsigned long long) den);
}

static void put_holder(zxvd_buf_t *b, const holder_t *h)
{
    zxvd_buf_printf(b, "{\"holder\":%u", (unsigned) h->id);
    u64s(b, "vino_minor", debit(h->vino));
    u64s(b, "vbills", debit(h->vbill));
    RAW(b, "}");
}

/* ===================== vault.* (svault.h) ===================== */

static const uint32_t *buckets(void)
{
    static uint32_t b[PAY_ASSURE_BUCKETS];
    b[PAY_ASSURE_RESERVE_FLOOR] = E.rsv;
    b[PAY_ASSURE_VBILL_DIVIDEND] = E.dpool;
    b[PAY_ASSURE_INFRA_BOUNTY] = E.bounty;
    b[PAY_ASSURE_REGEN_CAPITAL] = E.regen;
    return b;
}

/* E3: an assurance fee collected elsewhere enters the four buckets (B1),
 * then the vault sweeps a third of the reserve share (S1) */
static void m_vault_deposit(zxvd_call_t *c)
{
    uint64_t fee, now, parts[PAY_ASSURE_BUCKETS];
    if (!ready(c) || !need(c, "fee_minor", &fee) || !nowp(c, &now)) return;
    if (!fee) return zxvd_fail(c, ZXVD_E_PARAMS, "\"fee_minor\" must be at least 1");
    pay_assure_split(fee, parts);
    pay_posting_req_t r;
    pay_receipt_t rc;
    req_init(&r, now, "assurance fee recorded");
    r.kind = PAY_KIND_ISSUE;
    r.lines[0] = (pay_line_t){E.viss, 0, (int64_t) fee};
    r.n_lines = 1;
    for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++)
        if (parts[i]) r.lines[r.n_lines++] = (pay_line_t){buckets()[i], (int64_t) parts[i], 0};
    pay_status_t ps = pay_ledger_post(&E.L, &r, &rc);
    if (ps != PAY_OK) return refuse(c, NAME(PAY_N, (int) ps), false, PAY_OK);
    E.fees += fee;
    uint64_t swept = E.V.swept_total, kept = E.V.overflow_total;
    req_init(&r, now, "vault sweep");
    svault_status_t st = svault_sweep_split(&E.V, &E.L, &r, parts);
    if (st < 0) return refuse(c, NAME(SV_N, (int) st), st == SVAULT_ERR_LEDGER, E.V.last_ledger);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"fee_minor\":\"%llu\"", (unsigned long long) fee);
    u64s(b, "reserve_minor", parts[PAY_ASSURE_RESERVE_FLOOR]);
    u64s(b, "dividend_minor", parts[PAY_ASSURE_VBILL_DIVIDEND]);
    u64s(b, "bounty_minor", parts[PAY_ASSURE_INFRA_BOUNTY]);
    u64s(b, "regen_minor", parts[PAY_ASSURE_REGEN_CAPITAL]);
    u64s(b, "swept_minor", E.V.swept_total - swept);
    u64s(b, "kept_minor", E.V.overflow_total - kept);
    u64s(b, "carry", E.V.carry);
    RAW(b, "}");
}

static void m_vault_buy(zxvd_call_t *c)
{
    uint64_t price, max, now;
    holder_t *h;
    if (!ready(c) || !(h = holder(c, "seller")) || !need(c, "price_minor", &price) ||
        !amt(c, "max_qty", false, ECON_MAX_U64, &max) || !nowp(c, &now))
        return;
    pay_posting_req_t r;
    req_init(&r, now, "vault v-bill buy");
    svault_status_t st = svault_buy(&E.V, &E.L, &r, h->vino, h->vbill, price, max);
    if (st < 0) return refuse(c, NAME(SV_N, (int) st), st == SVAULT_ERR_LEDGER, E.V.last_ledger);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"bought\":\"%llu\"", (unsigned long long) E.V.last_qty);
    u64s(b, "cost_minor", E.V.last_cost);
    u64s(b, "price_minor", price);
    RAW(b, ",\"seller\":");
    put_holder(b, h);
    RAW(b, "}");
}

static void m_vault_first_loss(zxvd_call_t *c)
{
    uint64_t loss, now, left;
    if (!ready(c) || !need(c, "loss_vbills", &loss) || !nowp(c, &now)) return;
    pay_posting_req_t r;
    req_init(&r, now, "vault first loss");
    r.kind = PAY_KIND_REDEEM;
    svault_status_t st = svault_first_loss(&E.V, &E.L, &r, loss, &left);
    if (st < 0) return refuse(c, NAME(SV_N, (int) st), st == SVAULT_ERR_LEDGER, E.V.last_ledger);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"burned\":\"%llu\"", (unsigned long long) (loss - left));
    u64s(b, "uncovered", left);
    RAW(b, "}");
}

static void m_vault_status(zxvd_call_t *c)
{
    uint64_t n, h, cash, rs, num, den;
    if (!ready(c)) return;
    svault_balances(&E.V, &E.L, &n, &h, &cash, &rs);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"reserve_minor\":\"%llu\"", (unsigned long long) rs);
    u64s(b, "cash_minor", cash);
    u64s(b, "outstanding", n);
    u64s(b, "held", h);
    u64s(b, "cap", svault_cap(n));
    u64s(b, "room", svault_room(&E.V, &E.L));
    if (svault_floor(&E.V, &E.L, &num, &den) == SVAULT_OK)
        rat(b, "floor", num, den);
    else
        RAW(b, ",\"floor\":null");
    u64s(b, "fees_minor", E.fees);
    u64s(b, "share_total_minor", E.V.share_total);
    u64s(b, "swept_minor", E.V.swept_total);
    u64s(b, "kept_minor", E.V.overflow_total);
    u64s(b, "spent_minor", E.V.spent_total);
    u64s(b, "bought", E.V.vbills_bought);
    u64s(b, "burned", E.V.vbills_burned);
    u64s(b, "carry", E.V.carry);
    u64s(b, "dividend_pool_minor", debit(E.dpool));
    u64s(b, "bounty_minor", debit(E.bounty));
    u64s(b, "regen_minor", debit(E.regen));
    RAW(b, "}");
}

static void m_vault_check(zxvd_call_t *c)
{
    uint64_t fn, fd, bk;
    if (!ready(c) || !amt(c, "floor_num", false, 1, &fn) || !amt(c, "floor_den", false, 1, &fd) ||
        !amt(c, "backing", false, ECON_PAR, &bk))
        return;
    zxvd_buf_t *b = zxvd_result(c);
    bool ok = svault_check(&E.V, &E.L), lok = pay_ledger_check(&E.L);
    zxvd_buf_printf(b, "{\"ok\":%s", ok && lok ? "true" : "false");
    flag(b, "vault_ok", ok);
    flag(b, "ledger_ok", lok);
    flag(b, "solvent", svault_verify_solvency(&E.V, &E.L, fn, fd, bk));
    u64s(b, "floor_num", fn);
    u64s(b, "floor_den", fd);
    u64s(b, "backing", bk);
    RAW(b, "}");
}

static const zxvd_group_method_t VAULT_M[] = {
    {"vault.deposit", m_vault_deposit, false},
    {"vault.buy", m_vault_buy, false},
    {"vault.first_loss", m_vault_first_loss, false},
    {"vault.status", m_vault_status, true},
    {"vault.check", m_vault_check, true},
};

const zxvd_group_t *zxvd_vault_group(void)
{
    static const zxvd_group_t G = {
        "vault",
        true,
        "protocol solvency vault (svault): fee reserve share, V-Bill buys, first loss; in "
        "memory; V-Bills are likely securities",
        VAULT_M,
        sizeof VAULT_M / sizeof VAULT_M[0],
        econ_close};
    return &G;
}

/* ===================== swap.* (vswap.h) ===================== */

static void put_quote(zxvd_buf_t *b, const vswap_quote_t *q)
{
    rat(b, "rate", q->num, q->den);
    flag(b, "par", q->par);
    u64s(b, "vbills", q->vbills);
    u64s(b, "vino_minor", q->vino);
}

static void swap_refuse(zxvd_call_t *c, vswap_status_t st)
{
    refuse(c, NAME(VS_N, (int) st), st == VSWAP_ERR_LEDGER || st == VSWAP_ERR_PARTIAL,
           E.S.last_ledger);
}

/* E3: Vino of a holder enters these books (the holder's accounts open on
 * first use) */
static void m_swap_deposit(zxvd_call_t *c)
{
    uint32_t id;
    uint64_t a, now;
    if (!ready(c) || !zxvd_p_u32(c, "holder", &id) || !need(c, "amount_minor", &a) ||
        !nowp(c, &now))
        return;
    if (!a) return zxvd_fail(c, ZXVD_E_PARAMS, "\"amount_minor\" must be at least 1");
    holder_t *h = holder_of(id);
    if (!h) {
        if (E.n_h == ECON_MAX_HOLDERS)
            return zxvd_fail(c, ZXVD_E_ECON, "holder table full (%u)", ECON_MAX_HOLDERS);
        holder_t n = {id, 0, 0};
        if (pay_ledger_open(&E.L, id, E.vno, PAY_CAP_FINANCIAL, 0, &n.vino) != PAY_OK ||
            pay_ledger_open(&E.L, id, E.vbl, PAY_CAP_FINANCIAL, 0, &n.vbill) != PAY_OK)
            return zxvd_fail(c, ZXVD_E_ECON, "the ledger could not open the holder's accounts");
        E.h[E.n_h] = n;
        h = &E.h[E.n_h++];
    }
    pay_posting_req_t r;
    pay_receipt_t rc;
    req_init(&r, now, "vino deposit");
    pay_status_t ps = pay_ledger_issue(&E.L, &r, E.viss, h->vino, a, &rc);
    if (ps != PAY_OK) return refuse(c, NAME(PAY_N, (int) ps), false, PAY_OK);
    put_holder(zxvd_result(c), h);
}

static void m_swap_withdraw(zxvd_call_t *c)
{
    uint64_t a, now;
    holder_t *h;
    if (!ready(c) || !(h = holder(c, "holder")) || !need(c, "amount_minor", &a) || !nowp(c, &now))
        return;
    if (!a) return zxvd_fail(c, ZXVD_E_PARAMS, "\"amount_minor\" must be at least 1");
    pay_posting_req_t r;
    pay_receipt_t rc;
    req_init(&r, now, "vino withdrawal");
    pay_status_t ps = pay_ledger_redeem(&E.L, &r, h->vino, E.viss, a, &rc);
    if (ps != PAY_OK) return refuse(c, NAME(PAY_N, (int) ps), false, PAY_OK);
    put_holder(zxvd_result(c), h);
}

/* W1, signed by zxvd as the gateway (E4) */
static void m_swap_enable(zxvd_call_t *c)
{
    bool on;
    if (!ready(c) || !zxvd_p_bool(c, "enabled", true, &on)) return;
    uint64_t epoch = E.S.switch_epoch + 1;
    uint8_t m[VSWAP_SWITCH_MSG], sig[32];
    vswap_switch_msg(ECON_JURIS, on, epoch, m);
    local_sign(ECON_GATEWAY, m, sizeof m, sig);
    vswap_status_t st = vswap_enable(&E.S, ECON_JURIS, on, epoch, sig, 32);
    if (st != VSWAP_OK) return swap_refuse(c, st);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"enabled\":%s", on ? "true" : "false");
    u64s(b, "epoch", epoch);
    RAW(b, "}");
}

static void m_swap_quote(zxvd_call_t *c)
{
    char dir[4];
    uint64_t a;
    if (!ready(c) || !zxvd_p_str(c, "direction", dir, sizeof dir, true) || !need(c, "amount", &a))
        return;
    bool in = !strcmp(dir, "in");
    if (!in && strcmp(dir, "out"))
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"direction\" must be \"in\" or \"out\"");
    vswap_quote_t q;
    vswap_status_t st = in ? vswap_quote_in(&E.S, a, &q) : vswap_quote_out(&E.S, a, &q);
    if (st != VSWAP_OK) return swap_refuse(c, st);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"direction\":\"%s\"", dir);
    put_quote(b, &q);
    RAW(b, "}");
}

static void swap_move(zxvd_call_t *c, bool in)
{
    uint64_t a, now;
    holder_t *h;
    if (!ready(c) || !(h = holder(c, "holder")) || !need(c, in ? "amount_minor" : "vbills", &a) ||
        !nowp(c, &now))
        return;
    vswap_quote_t q;
    vswap_status_t st = in ? vswap_swap_in(&E.S, h->vino, h->vbill, a, now, &q)
                           : vswap_swap_out(&E.S, h->vbill, h->vino, a, now, &q);
    if (st != VSWAP_OK) return swap_refuse(c, st);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"direction\":\"%s\"", in ? "in" : "out");
    put_quote(b, &q);
    RAW(b, ",\"holder\":");
    put_holder(b, h);
    RAW(b, "}");
}

static void m_swap_in(zxvd_call_t *c)
{
    swap_move(c, true);
}

static void m_swap_out(zxvd_call_t *c)
{
    swap_move(c, false);
}

static const char *const TIER[VSWAP_TIERS] = {"low", "medium", "deep"};

static void m_swap_lock(zxvd_call_t *c)
{
    char t[8];
    uint64_t v, now;
    bool re;
    holder_t *h;
    if (!ready(c) || !(h = holder(c, "holder")) || !zxvd_p_str(c, "tier", t, sizeof t, true) ||
        !need(c, "vbills", &v) || !zxvd_p_bool(c, "reinvest", false, &re) || !nowp(c, &now))
        return;
    uint8_t tier = !strcmp(t, "medium") ? VSWAP_TIER_MEDIUM
                   : !strcmp(t, "deep") ? VSWAP_TIER_DEEP
                                        : VSWAP_TIER_LOW;
    if (tier == VSWAP_TIER_LOW)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"tier\" must be \"medium\" or \"deep\"");
    uint32_t id;
    vswap_status_t st = vswap_lock(&E.S, h->vbill, h->vino, tier, v, re, now, &id);
    if (st != VSWAP_OK) return swap_refuse(c, st);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"position\":%u,\"tier\":\"%s\"", (unsigned) id, TIER[tier]);
    u64s(b, "vbills", v);
    u64s(b, "notice_period", E.S.period[tier]);
    flag(b, "reinvest", re);
    RAW(b, "}");
}

static bool position(zxvd_call_t *c, holder_t **h, uint32_t *id, uint64_t *now)
{
    return ready(c) && (*h = holder(c, "holder")) && zxvd_p_u32(c, "position", id) && nowp(c, now);
}

static void m_swap_notice(zxvd_call_t *c)
{
    holder_t *h;
    uint32_t id;
    uint64_t now;
    if (!position(c, &h, &id, &now)) return;
    vswap_status_t st = vswap_notice(&E.S, id, h->vbill, now);
    if (st != VSWAP_OK) return swap_refuse(c, st);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"position\":%u", (unsigned) id);
    u64s(b, "unlock_at", now + E.S.period[E.S.pos[id].tier]);
    RAW(b, "}");
}

static void m_swap_unlock(zxvd_call_t *c)
{
    holder_t *h;
    uint32_t id;
    uint64_t now;
    if (!position(c, &h, &id, &now)) return;
    uint64_t v = id < VSWAP_MAX_POS ? E.S.pos[id].vbills : 0;
    vswap_status_t st = vswap_unlock(&E.S, id, h->vbill, now);
    if (st != VSWAP_OK) return swap_refuse(c, st);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"position\":%u", (unsigned) id);
    u64s(b, "vbills", v);
    RAW(b, ",\"holder\":");
    put_holder(b, h);
    RAW(b, "}");
}

/* W5: the whole dividend pool to every holder (LOW weight) and every lock
 * position (its tier's weight); the vault is not listed (its cash must stay
 * swept - spent, S6) */
static void m_swap_dividend(zxvd_call_t *c)
{
    uint64_t now;
    if (!ready(c) || !nowp(c, &now)) return;
    vswap_holder_t hs[ECON_MAX_HOLDERS];
    for (uint32_t i = 0; i < E.n_h; i++) hs[i] = (vswap_holder_t){E.h[i].vbill, E.h[i].vino};
    vswap_div_t d;
    vswap_status_t st = vswap_dividend(&E.S, E.dpool, hs, E.n_h, true, now, NULL, &d);
    if (st != VSWAP_OK) return swap_refuse(c, st);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"pool_minor\":\"%llu\"", (unsigned long long) d.pool);
    u64s(b, "units", d.units);
    u64s(b, "paid_minor", d.paid);
    u64s(b, "reinvest_minor", d.reinvest);
    u64s(b, "vbills", d.vbills);
    zxvd_buf_printf(b, ",\"postings\":%u}", (unsigned) d.postings);
}

static void m_swap_status(zxvd_call_t *c)
{
    if (!ready(c)) return;
    vswap_quote_t q;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"enabled\":%s", E.S.enabled ? "true" : "false");
    u64s(b, "switch_epoch", E.S.switch_epoch);
    if (vswap_rate(&E.S, &q) == VSWAP_OK) { /* W2; refused: no rate to swap in at */
        rat(b, "rate", q.num, q.den);
        flag(b, "par", q.par);
    } else {
        RAW(b, ",\"rate\":null,\"par\":false");
    }
    u64s(b, "reserve_minor", debit(E.rsv));
    u64s(b, "outstanding", pay_ledger_account(&E.L, E.biss)->credit);
    u64s(b, "vault_vbills", debit(E.vvb));
    u64s(b, "dividend_pool_minor", debit(E.dpool));
    u64s(b, "notice_medium", E.S.period[VSWAP_TIER_MEDIUM]);
    u64s(b, "notice_deep", E.S.period[VSWAP_TIER_DEEP]);
    zxvd_buf_printf(
        b, ",\"weights\":[%u,%u,%u],\"holders\":[", (unsigned) E.S.cfg.weight[VSWAP_TIER_LOW],
        (unsigned) E.S.cfg.weight[VSWAP_TIER_MEDIUM], (unsigned) E.S.cfg.weight[VSWAP_TIER_DEEP]);
    for (uint32_t i = 0; i < E.n_h; i++) {
        if (i) RAW(b, ",");
        put_holder(b, &E.h[i]);
    }
    RAW(b, "],\"positions\":[");
    bool first = true;
    for (uint32_t i = 0; i < VSWAP_MAX_POS; i++) {
        const vswap_pos_t *p = &E.S.pos[i];
        if (!p->used) continue;
        zxvd_buf_printf(b, "%s{\"position\":%u,\"holder\":%u,\"tier\":\"%s\"", first ? "" : ",",
                        (unsigned) i, (unsigned) holder_by_acct(p->owner)->id, TIER[p->tier]);
        first = false;
        u64s(b, "vbills", p->vbills);
        flag(b, "reinvest", p->reinvest);
        flag(b, "noticed", p->noticed);
        u64s(b, "notice_tick", p->notice_tick);
        u64s(b, "last_vino_minor", p->last_vino);
        u64s(b, "last_vbills", p->last_vbills);
        RAW(b, "}");
    }
    RAW(b, "]}");
}

static void m_swap_check(zxvd_call_t *c)
{
    if (!ready(c)) return;
    vswap_status_t st = vswap_epoch_check(&E.S);
    zxvd_buf_printf(zxvd_result(c), "{\"ok\":%s,\"status\":\"%s\"}",
                    st == VSWAP_OK ? "true" : "false", NAME(VS_N, (int) st));
}

static const zxvd_group_method_t SWAP_M[] = {
    {"swap.deposit", m_swap_deposit, false}, {"swap.withdraw", m_swap_withdraw, false},
    {"swap.enable", m_swap_enable, false},   {"swap.quote", m_swap_quote, true},
    {"swap.in", m_swap_in, false},           {"swap.out", m_swap_out, false},
    {"swap.lock", m_swap_lock, false},       {"swap.notice", m_swap_notice, false},
    {"swap.unlock", m_swap_unlock, false},   {"swap.dividend", m_swap_dividend, false},
    {"swap.status", m_swap_status, true},    {"swap.check", m_swap_check, true},
};

const zxvd_group_t *zxvd_swap_group(void)
{
    static const zxvd_group_t G = {
        "swap",
        true,
        "Vino <-> V-Bill swap, lock tiers and the dividend (vswap); in memory; V-Bills are "
        "likely securities",
        SWAP_M,
        sizeof SWAP_M / sizeof SWAP_M[0],
        econ_close};
    return &G;
}

/* ===================== auction.* (vauction.h) ===================== */

static const char *const ASTATE[] = {"idle", "open", "cleared", "settled"};

static void auc_refuse(zxvd_call_t *c, vauc_status_t st)
{
    refuse(c, NAME(VA_N, (int) st), st == VAUC_ERR_LEDGER, E.A.last_ledger);
}

static void put_config(zxvd_buf_t *b, const vauc_config_t *k)
{
    zxvd_buf_printf(b, "{\"floor_num\":\"%llu\"", (unsigned long long) k->floor_num);
    u64s(b, "floor_den", k->floor_den);
    u64s(b, "backing", k->backing);
    u64s(b, "cap", k->cap);
    u64s(b, "p_start", k->p_start);
    u64s(b, "p_step", k->p_step);
    u64s(b, "p_min", k->p_min);
    RAW(b, "}");
}

static void put_record(zxvd_buf_t *b, const vauc_record_t *r)
{
    zxvd_buf_printf(b, "{\"epoch\":\"%llu\"", (unsigned long long) r->epoch);
    u64s(b, "price_minor", r->price);
    u64s(b, "qty", r->qty);
    zxvd_buf_printf(b, ",\"winners\":%u", (unsigned) r->winners);
    flag(b, "cancelled", r->cancelled);
    u64s(b, "reserve_before_minor", r->r_before);
    u64s(b, "reserve_after_minor", r->r_after);
    u64s(b, "outstanding_before", r->n_before);
    u64s(b, "outstanding_after", r->n_after);
    flag(b, "floor_ok", r->floor_ok);
    RAW(b, "}");
}

static void m_auction_configure(zxvd_call_t *c)
{
    static const char *const K[] = {"floor_num", "floor_den", "backing", "cap",
                                    "p_start",   "p_step",    "p_min"};
    if (!ready(c)) return;
    vauc_config_t k = E.A.cfg;
    uint64_t *f[] = {&k.floor_num, &k.floor_den, &k.backing, &k.cap,
                     &k.p_start,   &k.p_step,    &k.p_min};
    for (uint32_t i = 0; i < sizeof K / sizeof K[0]; i++)
        if (!amt(c, K[i], false, *f[i], f[i])) return;
    vauc_status_t st = vauc_set_config(&E.A, &k);
    if (st != VAUC_OK) return auc_refuse(c, st);
    put_config(zxvd_result(c), &E.A.cfg);
}

static void m_auction_open(zxvd_call_t *c)
{
    if (!ready(c)) return;
    vauc_status_t st = vauc_open(&E.A);
    if (st != VAUC_OK) return auc_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"epoch\":\"%llu\",\"state\":\"open\"}",
                    (unsigned long long) E.A.epoch);
}

static void m_auction_bid(zxvd_call_t *c)
{
    uint64_t qty, limit, now;
    holder_t *h;
    if (!ready(c) || !(h = holder(c, "holder")) || !need(c, "qty", &qty) ||
        !need(c, "limit_minor", &limit) || !nowp(c, &now))
        return;
    uint32_t i;
    vauc_status_t st = vauc_bid(&E.A, h->vino, h->vbill, qty, limit, now, &i);
    if (st != VAUC_OK) return auc_refuse(c, st);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"epoch\":\"%llu\",\"bid\":%u", (unsigned long long) E.A.epoch,
                    (unsigned) i);
    u64s(b, "locked_minor", E.A.bid[i].locked);
    RAW(b, "}");
}

/* A6 + A7: clear an open epoch, then settle it (a settlement the ledger
 * stopped resumes here) */
static void m_auction_run(zxvd_call_t *c)
{
    uint64_t now;
    if (!ready(c) || !nowp(c, &now)) return;
    vauc_status_t st = E.A.state == VAUC_OPEN ? vauc_clear(&E.A, now) : VAUC_OK;
    if (st == VAUC_OK) st = vauc_settle(&E.A, now);
    if (st != VAUC_OK) return auc_refuse(c, st);
    put_record(zxvd_result(c), vauc_record(&E.A, E.A.epoch));
}

static void m_auction_cancel(zxvd_call_t *c)
{
    uint64_t now;
    if (!ready(c) || !nowp(c, &now)) return;
    vauc_status_t st = vauc_cancel(&E.A, now);
    if (st != VAUC_OK) return auc_refuse(c, st);
    put_record(zxvd_result(c), vauc_record(&E.A, E.A.epoch));
}

static void m_auction_quote(zxvd_call_t *c)
{
    uint64_t p, now, R;
    if (!ready(c) || !need(c, "price_minor", &p) || !nowp(c, &now)) return;
    vauc_reserve(&E.A, now, &R); /* E4: no receipts, so R is the reserve account */
    uint64_t N = vauc_outstanding(&E.A);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"price_minor\":\"%llu\"", (unsigned long long) p);
    u64s(b, "reserve_minor", R);
    u64s(b, "outstanding", N);
    u64s(b, "supply", vauc_supply(&E.A, R, N, p));
    u64s(b, "demand", vauc_demand(&E.A, p));
    flag(b, "floor_ok", vauc_floor_ok(&E.A, now));
    RAW(b, "}");
}

static void m_auction_status(zxvd_call_t *c)
{
    uint64_t now, R;
    if (!ready(c) || !nowp(c, &now)) return;
    vauc_reserve(&E.A, now, &R);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"state\":\"%s\"", ASTATE[E.A.state]);
    u64s(b, "epoch", E.A.epoch);
    RAW(b, ",\"config\":");
    put_config(b, &E.A.cfg);
    u64s(b, "reserve_minor", R);
    u64s(b, "outstanding", vauc_outstanding(&E.A));
    flag(b, "floor_ok", vauc_floor_ok(&E.A, now));
    RAW(b, ",\"bids\":[");
    for (uint32_t i = 0; i < E.A.n_bids; i++) {
        const vauc_bid_t *d = &E.A.bid[i];
        zxvd_buf_printf(b, "%s{\"bid\":%u,\"holder\":%u", i ? "," : "", (unsigned) i,
                        (unsigned) holder_by_acct(d->fiat)->id);
        u64s(b, "qty", d->qty);
        u64s(b, "limit_minor", d->limit);
        u64s(b, "locked_minor", d->locked);
        u64s(b, "fill", d->fill);
        flag(b, "settled", d->settled);
        RAW(b, "}");
    }
    RAW(b, "],\"history\":[");
    bool first = true;
    for (uint64_t e = E.A.epoch; e > 0 && E.A.epoch - e < VAUC_HISTORY; e--) {
        const vauc_record_t *r = vauc_record(&E.A, e);
        if (!r) continue;
        if (!first) RAW(b, ",");
        first = false;
        put_record(b, r);
    }
    RAW(b, "]}");
}

static void m_auction_check(zxvd_call_t *c)
{
    uint64_t now;
    if (!ready(c) || !nowp(c, &now)) return;
    bool f = vauc_floor_ok(&E.A, now), l = pay_ledger_check(&E.L);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"ok\":%s", f && l ? "true" : "false");
    flag(b, "floor_ok", f);
    flag(b, "ledger_ok", l);
    RAW(b, "}");
}

static const zxvd_group_method_t AUC_M[] = {
    {"auction.configure", m_auction_configure, false},
    {"auction.open", m_auction_open, false},
    {"auction.bid", m_auction_bid, false},
    {"auction.run", m_auction_run, false},
    {"auction.cancel", m_auction_cancel, false},
    {"auction.quote", m_auction_quote, true},
    {"auction.status", m_auction_status, true},
    {"auction.check", m_auction_check, true},
};

const zxvd_group_t *zxvd_auctions_group(void)
{
    static const zxvd_group_t G = {
        "auctions",
        true,
        "V-Bill issuance by uniform-price descending auction, backed only (vauction); in "
        "memory; V-Bills are likely securities",
        AUC_M,
        sizeof AUC_M / sizeof AUC_M[0],
        econ_close};
    return &G;
}

/* ===================== zenny.* (zenny.h) ===================== */

static const char *const KIND[] = {"", "vbill", "fiat", "commodity", "compute"};

static void zen_refuse(zxvd_call_t *c, zenny_status_t st)
{
    refuse(c, NAME(ZE_N, (int) st), false, PAY_OK);
}

static bool chip(zxvd_call_t *c, uint32_t *slot)
{
    uint8_t id[32];
    uint32_t n;
    if (!ready(c) || !zxvd_p_hex(c, "chip", id, 32, &n, true)) return false;
    if (n == 32 && zenny_find(&E.Z, id, slot)) return true;
    zxvd_fail(c, ZXVD_E_ECON, "unknown chip");
    return false;
}

/* one {num, den} per leg; absent and not required: *has = false */
static bool prices(zxvd_call_t *c, uint32_t n_legs, bool required, zenny_price_t *p, bool *has)
{
    int32_t n = zxvd_p_len(c, "prices"), saved;
    *has = n >= 0;
    if (n == -2) return false;
    if (n < 0 && !required) return true;
    if (n != (int32_t) n_legs) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"prices\" must have one {num, den} per leg (%u)",
                  (unsigned) n_legs);
        return false;
    }
    for (uint32_t i = 0; i < n_legs; i++) {
        if (!zxvd_p_enter(c, "prices", i, &saved)) return false;
        bool ok = need(c, "num", &p[i].num) && need(c, "den", &p[i].den);
        zxvd_p_leave(c, saved);
        if (!ok) return false;
    }
    return true;
}

static bool leg(zxvd_call_t *c, zenny_leg_t *l, zenny_price_t *p)
{
    char k[12];
    uint32_t i;
    if (!zxvd_p_str(c, "kind", k, sizeof k, true)) return false;
    for (i = ZENNY_VBILL; i <= ZENNY_COMPUTE && strcmp(k, KIND[i]); i++) continue;
    if (i > ZENNY_COMPUTE) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be vbill, fiat, commodity or compute");
        return false;
    }
    l->kind = (uint8_t) i;
    l->custodian = 0;
    return (!zxvd_p_has(c, "custodian") || zxvd_p_u32(c, "custodian", &l->custodian)) &&
           need(c, "asset", &l->asset) && need(c, "qty", &l->qty) &&
           need(c, "weight", &l->weight) && need(c, "price_num", &p->num) &&
           amt(c, "price_den", false, 1, &p->den);
}

static void put_receipt(zxvd_buf_t *b, const zenny_receipt_t *r)
{
    zxvd_buf_printf(b, "{\"seq\":\"%llu\",\"chip\":", (unsigned long long) r->seq);
    zxvd_buf_hex(b, r->chip_id, 32);
    u64s(b, "version", r->version);
    u64s(b, "chips", r->chips);
    for (int pass = 0; pass < 2; pass++) {
        if (pass)
            RAW(b, ",\"value\":[");
        else
            RAW(b, ",\"out\":[");
        for (uint32_t i = 0; i < r->n_legs; i++)
            zxvd_buf_printf(b, "%s\"%llu\"", i ? "," : "",
                            (unsigned long long) (pass ? r->value[i] : r->out[i]));
        RAW(b, "]");
    }
    u64s(b, "total", r->total);
    u64s(b, "fee", r->fee);
    u64s(b, "tick", r->tick);
    RAW(b, "}");
}

/* Z1/Z2: zxvd signs as the minter (E4); a co-minter's signature and a
 * custodian's attestation are empty and never verify */
static void m_zenny_mint(zxvd_call_t *c)
{
    static const uint8_t none[32];
    zenny_req_t r;
    uint64_t now;
    int32_t saved;
    memset(&r, 0, sizeof r);
    if (!ready(c) || !zxvd_p_u32(c, "minter", &r.minter) ||
        (zxvd_p_has(c, "co_minter") && !zxvd_p_u32(c, "co_minter", &r.co_minter)) ||
        !need(c, "kappa", &r.kappa) ||
        (zxvd_p_has(c, "rules") && !zxvd_p_u32(c, "rules", &r.rules)) ||
        !amt(c, "nonce", false, E.nonce, &r.nonce) || !nowp(c, &now))
        return;
    int32_t n = zxvd_p_len(c, "legs");
    if (n == -2) return;
    if (n < 1 || n > (int32_t) ZENNY_MAX_LEGS)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"legs\" must have 1 to %u legs", ZENNY_MAX_LEGS);
    r.n_legs = (uint32_t) n;
    for (uint32_t i = 0; i < r.n_legs; i++) {
        if (!zxvd_p_enter(c, "legs", i, &saved)) return;
        bool ok = leg(c, &r.leg[i], &r.price[i]);
        zxvd_p_leave(c, saved);
        if (!ok) return;
        r.att[i] = none;
        r.att_len[i] = 32;
    }
    r.tick = now;
    uint8_t d[32], m[ZENNY_SIG_MSG], sig[32];
    zenny_mint_digest(&r, d);
    zenny_sign_msg(ZENNY_SIG_MINT, d, m);
    local_sign(r.minter, m, sizeof m, sig);
    r.sig_minter = sig;
    r.sig_minter_len = 32;
    r.sig_co = none;
    r.sig_co_len = 32;
    uint32_t slot;
    zenny_status_t st = zenny_mint(&E.Z, &r, &slot);
    if (st != ZENNY_OK) return zen_refuse(c, st);
    if (!zxvd_p_has(c, "nonce")) E.nonce++;
    const zenny_version_t *v = zenny_current(&E.Z, slot);
    zxvd_buf_t *b = zxvd_result(c);
    RAW(b, "{\"chip\":");
    zxvd_buf_hex(b, E.Z.chip[slot].id, 32);
    zxvd_buf_printf(b, ",\"slot\":%u", (unsigned) slot);
    u64s(b, "nonce", r.nonce);
    u64s(b, "version", v->version);
    u64s(b, "chips", v->chips);
    RAW(b, "}");
}

static void m_zenny_quote(zxvd_call_t *c)
{
    uint32_t s;
    uint64_t want;
    zenny_price_t p[ZENNY_MAX_LEGS];
    bool has;
    if (!chip(c, &s) || !need(c, "want", &want) ||
        !prices(c, zenny_current(&E.Z, s)->n_legs, false, p, &has))
        return;
    zxvd_buf_printf(zxvd_result(c), "{\"chips\":\"%llu\"}",
                    (unsigned long long) zenny_quote(&E.Z, s, want, has ? p : NULL));
}

static void m_zenny_decompress(zxvd_call_t *c)
{
    uint32_t s;
    uint64_t n, now;
    zenny_price_t p[ZENNY_MAX_LEGS];
    bool has;
    if (!chip(c, &s) || !need(c, "chips", &n) ||
        !prices(c, zenny_current(&E.Z, s)->n_legs, true, p, &has) || !nowp(c, &now))
        return;
    zenny_receipt_t r;
    zenny_status_t st = zenny_decompress(&E.Z, s, n, p, now, &r);
    if (st != ZENNY_OK) return zen_refuse(c, st);
    put_receipt(zxvd_result(c), &r);
}

static void m_zenny_receipts(zxvd_call_t *c)
{
    uint64_t upto;
    if (!ready(c) || !amt(c, "ack", false, E.Z.r_acked, &upto)) return;
    zenny_status_t st = zenny_receipt_ack(&E.Z, upto);
    if (st != ZENNY_OK) return zen_refuse(c, st);
    zxvd_buf_t *b = zxvd_result(c);
    RAW(b, "{\"receipts\":[");
    for (uint64_t q = E.Z.r_acked; q < E.Z.r_next; q++) {
        zenny_receipt_t r;
        zenny_receipt_get(&E.Z, q, &r);
        if (q > E.Z.r_acked) RAW(b, ",");
        put_receipt(b, &r);
    }
    RAW(b, "]");
    u64s(b, "next", E.Z.r_next);
    u64s(b, "acked", E.Z.r_acked);
    RAW(b, "}");
}

static void m_zenny_status(zxvd_call_t *c)
{
    if (!ready(c)) return;
    zxvd_buf_t *b = zxvd_result(c);
    RAW(b, "{\"chips\":[");
    bool first = true;
    for (uint32_t s = 0; s < ZENNY_MAX_CHIPS; s++) {
        const zenny_chip_t *ch = &E.Z.chip[s];
        if (!ch->used) continue;
        const zenny_version_t *v = zenny_current(&E.Z, s);
        zxvd_buf_printf(b, "%s{\"chip\":", first ? "" : ",");
        first = false;
        zxvd_buf_hex(b, ch->id, 32);
        zxvd_buf_printf(b, ",\"slot\":%u,\"minter\":%u,\"co_minter\":%u,\"rules\":%u", (unsigned) s,
                        (unsigned) ch->minter, (unsigned) ch->co_minter, (unsigned) ch->rules);
        u64s(b, "kappa", ch->kappa);
        zxvd_buf_printf(b, ",\"versions\":%u", (unsigned) ch->n_ver);
        u64s(b, "outstanding", v->chips);
        RAW(b, ",\"legs\":[");
        for (uint32_t i = 0; i < v->n_legs; i++) {
            const zenny_leg_t *l = &v->leg[i];
            zxvd_buf_printf(b, "%s{\"kind\":\"%s\",\"custodian\":%u", i ? "," : "", KIND[l->kind],
                            (unsigned) l->custodian);
            u64s(b, "asset", l->asset);
            u64s(b, "qty", l->qty);
            u64s(b, "weight", l->weight);
            rat(b, "price", v->price[i].num, v->price[i].den);
            RAW(b, "}");
        }
        RAW(b, "]}");
    }
    RAW(b, "]");
    u64s(b, "receipts_pending", E.Z.r_next - E.Z.r_acked);
    RAW(b, "}");
}

static void m_zenny_check(zxvd_call_t *c)
{
    if (!ready(c)) return;
    zxvd_buf_t *b = zxvd_result(c);
    bool all = true;
    RAW(b, "{\"chips\":[");
    for (uint32_t s = 0, k = 0; s < ZENNY_MAX_CHIPS; s++) {
        if (!E.Z.chip[s].used) continue;
        bool chain = zenny_verify_chain(&E.Z, s) == ZENNY_OK,
             claim = zenny_check(&E.Z, s) == ZENNY_OK;
        all = all && chain && claim;
        zxvd_buf_printf(b, "%s{\"chip\":", k ? "," : "");
        k++;
        zxvd_buf_hex(b, E.Z.chip[s].id, 32);
        flag(b, "chain_ok", chain);
        flag(b, "claim_ok", claim);
        RAW(b, "}");
    }
    RAW(b, "]");
    flag(b, "ok", all);
    RAW(b, "}");
}

static const zxvd_group_method_t ZEN_M[] = {
    {"zenny.mint", m_zenny_mint, false},
    {"zenny.quote", m_zenny_quote, true},
    {"zenny.decompress", m_zenny_decompress, false},
    {"zenny.receipts", m_zenny_receipts, false},
    {"zenny.status", m_zenny_status, true},
    {"zenny.check", m_zenny_check, true},
};

const zxvd_group_t *zxvd_zenny_group(void)
{
    static const zxvd_group_t G = {
        "zenny",
        true,
        "Zenny chips: baskets with a V-Bill anchor (zenny); in memory; likely securities; the "
        "name has no trademark clearance",
        ZEN_M,
        sizeof ZEN_M / sizeof ZEN_M[0],
        econ_close};
    return &G;
}

/* ===================== stats.* (zstats.h) ===================== */

/* S1-S6: one snapshot of the whole economy, hash-chained to the last one
 * (unsigned: zxvd holds no publisher key, E4) */
static void m_stats_snapshot(zxvd_call_t *c)
{
    zstats_sources_t src;
    uint64_t now, mc;
    zstats_sources_default(&src);
    if (!ready(c) || !nowp(c, &now) || !amt(c, "req_num", false, 1, &src.req_num) ||
        !amt(c, "req_den", false, 1, &src.req_den) ||
        !amt(c, "min_cohort", false, ZSTATS_MIN_COHORT, &mc))
        return;
    if (mc < 1 || mc > UINT32_MAX)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"min_cohort\" must be 1 to 4294967295");
    src.min_cohort = (uint32_t) mc;
    src.L = &E.L;
    src.reserve_acct = E.rsv;
    src.claims_asset = E.vno;
    src.vault = &E.V;
    src.swap = &E.S;
    src.auction = &E.A;
    src.zenny = &E.Z;
    static zstats_snapshot_t s;
    static uint8_t enc[ZSTATS_ENC_LEN];
    static char js[8192];
    uint32_t elen, jlen;
    uint8_t d[32];
    zstats_status_t st = zstats_collect(&s, E.published + 1, now, &src);
    if (st == ZSTATS_OK) st = zstats_encode(&s, E.head, enc, sizeof enc, &elen);
    if (st == ZSTATS_OK) st = zstats_json(&s, js, sizeof js, &jlen);
    if (st != ZSTATS_OK) return refuse(c, NAME(ZS_N, (int) st), false, PAY_OK);
    sha256(enc, elen, d);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"epoch\":\"%llu\",\"prev\":", (unsigned long long) s.epoch);
    zxvd_buf_hex(b, E.head, 32);
    RAW(b, ",\"digest\":");
    zxvd_buf_hex(b, d, 32);
    RAW(b, ",\"signed\":false,\"encoded\":");
    zxvd_buf_hex(b, enc, elen);
    RAW(b, ",\"snapshot\":");
    zxvd_buf_raw(b, js, jlen);
    RAW(b, "}");
    memcpy(E.head, d, 32);
    E.published = s.epoch;
}

static void m_stats_status(zxvd_call_t *c)
{
    if (!ready(c)) return;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"published\":\"%llu\",\"head\":", (unsigned long long) E.published);
    zxvd_buf_hex(b, E.head, 32);
    RAW(b, ",\"signed\":false}");
}

static const zxvd_group_method_t STATS_M[] = {
    {"stats.snapshot", m_stats_snapshot, false},
    {"stats.status", m_stats_status, true},
};

const zxvd_group_t *zxvd_stats_group(void)
{
    static const zxvd_group_t G = {"stats",
                                   false,
                                   "aggregate economy statistics (zstats), read-only, "
                                   "hash-chained, unsigned",
                                   STATS_M,
                                   sizeof STATS_M / sizeof STATS_M[0],
                                   econ_close};
    return &G;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_money.c — the money engines that had no method group, on zxvd's
 * socket (zxvd_ext.h), each behind its own feature switch:
 *
 *   mint.*      feature "mint"      mint.h     minting against attested capacity   regulated
 *   qnote.*     feature "qnote"     qnote.h    Q-Note quarantine lots, Dutch sale  regulated
 *   fx.*        feature "fx"        fxepoch.h  FX by uniform-price batch epochs    regulated
 *   vhub.*      feature "vhub"      vhub.h     cross-currency clearing by V-Bill   regulated
 *   capmkt.*    feature "capmkt"    capmkt.h   the capacity market                 regulated
 *   market.*    feature "market"    market.h   storefronts, orders, escrow         regulated
 *   royalty.*   feature "royalty"   royalty.h  royalties from realised sales only
 *   cred.*      feature "cred"      cred.h     EigenTrust credibility
 *   dispute.*   feature "dispute"   dispute.h  the three-level dispute funnel
 *
 * Not here, because another group already serves them: kernel/src/cbank
 * (netting.*, iso20022.*, card.check in zxvd_pay.c through libzxv), settle
 * (the swarm's settlement spine inside zxv_host.c), sicily (player.card)
 * and acct (jurisdiction.*).
 *
 *   Y1 SANDBOX. Every ledger-backed group keeps its own pay_ledger book
 *      (zxvd_sandbox.h C1): practice units issued by <group>.fund, not money,
 *      never posted to the owner's books, gone when zxvd stops. capmkt and
 *      cred keep their engine's own tables (capmkt.deposit adds practice
 *      units to the engine's deposit mirror). Nothing here feeds player.card,
 *      ledger.* or any figure that reports real money or a real tier: cred's
 *      card is marked "sandbox".
 *   Y2 KEYS. Every signature the engines check goes through the group's
 *      sandbox key table (zxvd_sandbox.h C2): <group>.key registers a public
 *      key under the engine's numeric signer id; <group>.message returns the
 *      exact bytes to sign. market names a party by a 32-byte id; here that id
 *      is the key id, little-endian, then 28 zero bytes, so the same table
 *      serves. capmkt's proof evidence is the buyer's 64-byte signature over
 *      "zxvd/capmkt-proof/v1" || contract || seq || units || at_ms || prev.
 *      dispute's jury draw is checked by ECVRF (kernel/src/vrf, RFC 9381)
 *      under the VRF public key named at dispute.setup.
 *   Y3 SWITCHES. The six regulated features (zxvd_ext.h X5) start off like
 *      every feature; an engine's own off-by-default switch stays the
 *      engine's: fx.config turns the batch market on (leverage is always 0),
 *      mint's jurisdictions start empty, cred's credit lines and escrow-hold
 *      unlock stay off (cred is not regulated, so it offers no switch for a
 *      regulated lending policy).
 *   Y4 AUDIT. Every status method reports the engine's own audit as
 *      "check" (mint_check, qnote_check, fx_check, cm_conserved,
 *      mk_market_conserved with pay_ledger_check, roy_check, cred_check,
 *      dsp_check). vhub has no audit of its own: its "check" is
 *      pay_ledger_check and lpx_qlog_verify of the quarantine log.
 *   Y5 One setup per run per group; nothing is saved (as zxvd_instruments.c
 *      I4); "now" follows zxvd_sandbox.h C3.
 */
#include "zxvd_groups.h"
#include "zxvd_sandbox.h"
#include "sha256.h"
#include "mint.h"
#include "qnote.h"
#include "fxepoch.h"
#include "vhub.h"
#include "capmkt.h"
#include "market.h"
#include "mk_pay.h"
#include "royalty.h"
#include "cred.h"
#include "dispute.h"
#include "vrf.h"

#include <stdio.h>
#include <string.h>

/* ===================== shared ===================== */

static bool key_verify(void *ctx, uint32_t id, const uint8_t *m, uint32_t len, const uint8_t *s,
                       uint32_t sl)
{
    return zxvd_sandbox_verify(ctx, id, m, len, s, sl);
}

/* the index of the value of `key` in names[0..n), failing with `hint` */
static bool pick(zxvd_call_t *c, const char *key, const char *const *names, uint32_t n,
                 const char *hint, uint32_t *out)
{
    char s[24];
    if (!zxvd_p_str(c, key, s, sizeof s, true)) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(s, names[i])) {
            *out = i;
            return true;
        }
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be %s", key, hint);
    return false;
}

static bool not_set_up(zxvd_call_t *c, bool open, const char *g)
{
    if (open) return false;
    zxvd_fail(c, ZXVD_E_ENGINE, "%s is not set up: call %s.setup first", g, g);
    return true;
}

static bool hexn(zxvd_call_t *c, const char *key, uint8_t *out, uint32_t len)
{
    uint32_t n;
    if (!zxvd_p_hex(c, key, out, len, &n, true)) return false;
    if (n == len) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be %u bytes of hex", key, (unsigned) len);
    return false;
}

/* a posting request with identifiers from the book's counter, apart from
 * zxvd_sandbox_m_fund's ("zxvd-econ-fund") by its tag */
static void book_req(zxvd_sandbox_book_t *b, const char *tag, uint64_t tick, pay_posting_req_t *r)
{
    memset(r, 0, sizeof *r);
    uint8_t in[24] = {0}, d[32];
    memcpy(in, "zxvd-money:", 11);
    for (uint32_t i = 0; i < 5 && tag[i]; i++) in[11 + i] = (uint8_t) tag[i];
    for (int i = 0; i < 8; i++) in[16 + i] = (uint8_t) (b->seq >> (8 * i));
    sha256(in, sizeof in, r->idem_key);
    sha256(r->idem_key, 32, d);
    pay_uetr_from_random(d, r->uetr);
    snprintf(r->e2e, sizeof r->e2e, "ZXVD-%.5s-%llu", tag, (unsigned long long) b->seq);
    snprintf(r->memo, sizeof r->memo, "sandbox units, not money");
    r->initiator = ZXVD_SANDBOX_MODULE;
    r->tick = tick;
    b->seq++;
}

static bool open_acct(zxvd_sandbox_book_t *b, uint16_t asset, uint8_t flags, uint32_t *out)
{
    return pay_ledger_open(&b->L, ZXVD_SANDBOX_MODULE, asset, PAY_CAP_FINANCIAL, flags, out) ==
           PAY_OK;
}

static uint64_t bal(const zxvd_sandbox_book_t *b, uint32_t acct)
{
    const pay_account_t *x = pay_ledger_account(&b->L, acct);
    return x ? x->debit : 0;
}

static void put_hex32(zxvd_buf_t *o, const char *prefix, const uint8_t v[32])
{
    zxvd_buf_raw(o, prefix, strlen(prefix));
    zxvd_buf_hex(o, v, 32);
}

static void put_check(zxvd_buf_t *o, bool ok)
{
    zxvd_buf_printf(o, ",\"check\":%s", ok ? "true" : "false");
}

static void refuse_tab(zxvd_call_t *c, const char *const (*N)[2], uint32_t n, int st, uint32_t dflt)
{
    uint32_t i = st < 0 ? (uint32_t) -st : 0;
    if (i >= n) i = dflt;
    zxvd_sandbox_refuse(c, N[i][0], N[i][1]);
}

/* ===================== mint.* ===================== */

static zxvd_sandbox_book_t MB = {
    .a = {{.name = "vino", .code = "VINO", .minor = 2}, {.name = "vbill", .code = "VBILL"}},
    .n_assets = 2};
static zxvd_sandbox_keys_t MNK;
static mint_t MS;

static void mint_refuse(zxvd_call_t *c, mint_status_t st)
{
    static const char *const N[][2] = {
        {"MINT_OK", ""},
        {"MINT_ERR_ARG", "a bad argument (a zero quantity, a holder that is not of the target's "
                         "asset or is an issuer)"},
        {"MINT_ERR_FULL", "the table is full"},
        {"MINT_ERR_SIG", "the attestor's signature does not verify"},
        {"MINT_ERR_UNKNOWN", "an unknown or disabled attestor, or no claim with that id"},
        {"MINT_ERR_JURIS", "the jurisdiction is unknown or not enabled (minting starts off)"},
        {"MINT_ERR_DUP", "a claim with that id is already held (replay)"},
        {"MINT_ERR_EXPIRED", "the claim has expired"},
        {"MINT_ERR_PRICE", "no unit value is set for that capacity"},
        {"MINT_ERR_CAP", "the jurisdiction's cap for this epoch would be passed"},
        {"MINT_ERR_ZERO", "the value comes to no whole unit: nothing minted"},
        {"MINT_ERR_OVERFLOW", "a value does not fit"},
        {"MINT_ERR_LEDGER", "the ledger refused the posting"},
        {"MINT_ERR_STATE", "wrong state (a cap below this epoch's use, or a claim not live)"},
    };
    refuse_tab(c, N, sizeof N / sizeof N[0], st, 13);
}

static bool mint_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &MB, "mint");
}

static const char *const MINT_KIND[] = {"ewhr", "compute", "kwh"};
static const char *const MINT_TARGET[] = {"vino", "vbill"};
static const char *const MINT_STATE[] = {"free", "live", "quarantined", "retired"};

static void put_claim(zxvd_buf_t *o, const mint_claim_t *k)
{
    put_hex32(o, "{\"id\":", k->cap.id);
    zxvd_buf_printf(o, ",\"state\":\"%s\",\"attestor\":%u,\"capacity\":\"%s\",\"juris\":%u",
                    MINT_STATE[k->state & 3], (unsigned) k->cap.attestor,
                    MINT_KIND[k->cap.kind % MINT_KINDS], (unsigned) k->cap.juris);
    zxvd_sandbox_kv(o, ",\"qty\":", k->cap.qty);
    zxvd_sandbox_kv(o, ",\"expiry\":", k->cap.expiry);
    zxvd_sandbox_kv(o, ",\"epoch\":", k->epoch);
    zxvd_buf_printf(o, ",\"target\":\"%s\",\"holder\":%u", MINT_TARGET[k->target & 1],
                    (unsigned) k->holder);
    zxvd_sandbox_kv(o, ",\"gross_minor\":", k->gross);
    zxvd_sandbox_kv(o, ",\"net_minor\":", k->net);
    zxvd_sandbox_kv(o, ",\"units\":", k->units);
    zxvd_sandbox_kv(o, ",\"face_minor\":", k->face);
    zxvd_sandbox_kv(o, ",\"remainder_minor\":", k->remainder);
    if (k->target == MINT_TARGET_VINO) {
        zxvd_sandbox_kv(o, ",\"coins\":{\"top\":", k->coins.top);
        ZXVD_RAW(o, ",\"rungs\":[");
        for (uint32_t i = 0; i < k->coins.n && i < MINT_ZECK_MAX; i++)
            zxvd_buf_printf(o, "%s%u", i ? "," : "", (unsigned) k->coins.rung[i]);
        ZXVD_RAW(o, "]}");
    }
    ZXVD_RAW(o, "}");
}

static void m_mint_setup(zxvd_call_t *c)
{
    if (MB.open) return zxvd_sandbox_refuse(c, "SET_UP", "mint is set up already (this run)");
    uint64_t backing;
    if (!zxvd_sandbox_u64(c, "vbill_backing", false, 100, &backing)) return;
    if (!backing) return zxvd_fail(c, ZXVD_E_PARAMS, "\"vbill_backing\" must be above 0");
    uint32_t iv, ib;
    if (zxvd_sandbox_book_open(&MB) != 0 || !open_acct(&MB, MB.a[0].asset, PAY_ACCT_ISSUER, &iv) ||
        !open_acct(&MB, MB.a[1].asset, PAY_ACCT_ISSUER, &ib)) {
        MB.open = false;
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    }
    mint_status_t st = mint_init(&MS, &MB.L, iv, ib, backing, true, key_verify, &MNK,
                                 (const uint8_t *) "zxvd mint sandbox seed, v1......");
    if (st != MINT_OK) {
        MB.open = false;
        return mint_refuse(c, st);
    }
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"vino_issuer\":%u,\"vbill_issuer\":%u", (unsigned) iv, (unsigned) ib);
    zxvd_sandbox_kv(o, ",\"vbill_backing\":", backing);
    ZXVD_RAW(o, ",\"exclusive\":true,\"jurisdictions\":0}");
}

static void m_mint_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_key(c, &MNK);
}
static void m_mint_account_open(zxvd_call_t *c)
{
    if (mint_ready(c)) zxvd_sandbox_m_account_open(c, &MB);
}
static void m_mint_account(zxvd_call_t *c)
{
    if (mint_ready(c)) zxvd_sandbox_m_account(c, &MB);
}

static void m_mint_juris(zxvd_call_t *c)
{
    uint32_t code, bps;
    uint64_t cap;
    bool on;
    if (!mint_ready(c) || !zxvd_sandbox_need_u32(c, "code", &code) ||
        !zxvd_p_bool(c, "enabled", true, &on) ||
        !zxvd_sandbox_u32(c, "haircut_bps", false, 0, &bps) ||
        !zxvd_sandbox_need_u64(c, "cap_minor", &cap))
        return;
    mint_status_t st = mint_juris_set(&MS, code, on, bps, cap);
    if (st != MINT_OK) return mint_refuse(c, st);
    const mint_juris_t *j = mint_juris(&MS, code);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"code\":%u,\"enabled\":%s,\"haircut_bps\":%u", (unsigned) j->code,
                    j->enabled ? "true" : "false", (unsigned) j->haircut_bps);
    zxvd_sandbox_kv(o, ",\"cap_minor\":", j->cap);
    zxvd_sandbox_kv(o, ",\"used_minor\":", j->used_epoch);
    ZXVD_RAW(o, "}");
}

static void m_mint_attestor(zxvd_call_t *c)
{
    uint32_t id, bps;
    bool on;
    if (!mint_ready(c) || !zxvd_sandbox_need_u32(c, "id", &id) ||
        !zxvd_p_bool(c, "enabled", true, &on) ||
        !zxvd_sandbox_u32(c, "haircut_bps", false, 0, &bps))
        return;
    mint_status_t st = mint_attestor_set(&MS, id, on, bps);
    if (st != MINT_OK) return mint_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"id\":%u,\"enabled\":%s,\"haircut_bps\":%u}", (unsigned) id,
                    on ? "true" : "false", (unsigned) bps);
}

static void m_mint_unit(zxvd_call_t *c)
{
    uint32_t kind;
    uint64_t num, den;
    if (!mint_ready(c) ||
        !pick(c, "capacity", MINT_KIND, MINT_KINDS, "\"ewhr\", \"compute\" or \"kwh\"", &kind) ||
        !zxvd_sandbox_need_u64(c, "num", &num) || !zxvd_sandbox_need_u64(c, "den", &den))
        return;
    mint_status_t st = mint_unit_set(&MS, (uint8_t) kind, num, den);
    if (st != MINT_OK) return mint_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"capacity\":\"%s\"", MINT_KIND[kind]);
    zxvd_sandbox_kv(o, ",\"num\":", num);
    zxvd_sandbox_kv(o, ",\"den\":", den);
    ZXVD_RAW(o, "}");
}

static bool mint_cap(zxvd_call_t *c, mint_capacity_t *k)
{
    uint32_t kind;
    memset(k, 0, sizeof *k);
    if (!zxvd_sandbox_need_u32(c, "attestor", &k->attestor) ||
        !zxvd_sandbox_key32(c, "id", k->id) ||
        !pick(c, "capacity", MINT_KIND, MINT_KINDS, "\"ewhr\", \"compute\" or \"kwh\"", &kind) ||
        !zxvd_sandbox_need_u32(c, "juris", &k->juris) ||
        !zxvd_sandbox_need_u64(c, "qty", &k->qty) ||
        !zxvd_sandbox_need_u64(c, "expiry", &k->expiry))
        return false;
    k->kind = (uint8_t) kind;
    return true;
}

static void m_mint_message(zxvd_call_t *c)
{
    char kind[16];
    mint_capacity_t k;
    uint8_t m[MINT_CLAIM_MSG];
    if (!mint_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (strcmp(kind, "claim")) return zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"claim\"");
    if (!mint_cap(c, &k)) return;
    mint_claim_msg(&k, m);
    zxvd_sandbox_put_msg(c, m, sizeof m);
}

static void m_mint_issue(zxvd_call_t *c)
{
    mint_capacity_t k;
    uint8_t sig[64];
    uint32_t target, holder, idx = 0;
    uint64_t now;
    if (!mint_ready(c) || !mint_cap(c, &k) || !zxvd_sandbox_sig(c, "sig", sig) ||
        !pick(c, "target", MINT_TARGET, MINT_TARGETS, "\"vino\" or \"vbill\"", &target) ||
        !zxvd_sandbox_need_u32(c, "holder", &holder) || !zxvd_sandbox_now(c, &now))
        return;
    mint_status_t st = mint_issue(&MS, &k, sig, 64, (uint8_t) target, holder, now, &idx);
    if (st != MINT_OK) return mint_refuse(c, st);
    put_claim(zxvd_result(c), &MS.claim[idx]);
}

static void m_mint_epoch(zxvd_call_t *c)
{
    if (!mint_ready(c)) return;
    mint_status_t st = mint_epoch_advance(&MS);
    if (st != MINT_OK) return mint_refuse(c, st);
    zxvd_sandbox_kv(zxvd_result(c), "{\"epoch\":", MS.epoch);
    ZXVD_RAW(zxvd_result(c), "}");
}

static void m_mint_quarantine(zxvd_call_t *c)
{
    uint8_t id[32];
    uint64_t face = 0;
    if (!mint_ready(c) || !zxvd_sandbox_key32(c, "id", id)) return;
    mint_status_t st = mint_quarantine(&MS, id, &face);
    if (st != MINT_OK) return mint_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    put_hex32(o, "{\"id\":", id);
    zxvd_sandbox_kv(o, ",\"face_minor\":", face);
    ZXVD_RAW(o, ",\"state\":\"quarantined\",\"moved\":false}");
}

static void m_mint_retire(zxvd_call_t *c)
{
    uint8_t id[32];
    uint32_t from;
    if (!mint_ready(c) || !zxvd_sandbox_key32(c, "id", id) ||
        !zxvd_sandbox_need_u32(c, "from", &from))
        return;
    mint_status_t st = mint_retire(&MS, id, from);
    if (st != MINT_OK) return mint_refuse(c, st);
    put_claim(zxvd_result(c), mint_find(&MS, id));
}

static void m_mint_status(zxvd_call_t *c)
{
    uint64_t now;
    if (!zxvd_sandbox_now(c, &now)) return;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u", MB.open ? "true" : "false", (unsigned) MNK.n);
    if (MB.open) {
        zxvd_sandbox_kv(o, ",\"epoch\":", MS.epoch);
        zxvd_sandbox_kv(o, ",\"vbill_backing\":", MS.vbill_backing);
        zxvd_sandbox_kv(o, ",\"backing_minor\":", mint_backing(&MS, now));
        for (uint32_t t = 0; t < MINT_TARGETS; t++) {
            zxvd_buf_printf(o, ",\"%s\":{\"issuer\":%u", MINT_TARGET[t], (unsigned) MS.issuer[t]);
            zxvd_sandbox_kv(o, ",\"issued\":", MS.issued[t]);
            zxvd_sandbox_kv(o, ",\"retired\":", MS.retired[t]);
            zxvd_sandbox_kv(o, ",\"live\":", MS.live[t]);
            zxvd_sandbox_kv(o, ",\"quarantined\":", MS.quarantined[t]);
            ZXVD_RAW(o, "}");
        }
        ZXVD_RAW(o, ",\"jurisdictions\":[");
        bool first = true;
        for (uint32_t i = 0; i < MINT_MAX_JURIS; i++) {
            const mint_juris_t *j = &MS.juris[i];
            if (!j->used) continue;
            zxvd_buf_printf(o, "%s{\"code\":%u,\"enabled\":%s,\"haircut_bps\":%u", first ? "" : ",",
                            (unsigned) j->code, j->enabled ? "true" : "false",
                            (unsigned) j->haircut_bps);
            zxvd_sandbox_kv(o, ",\"cap_minor\":", j->cap);
            zxvd_sandbox_kv(o, ",\"used_minor\":", j->used_epoch);
            ZXVD_RAW(o, "}");
            first = false;
        }
        ZXVD_RAW(o, "],\"attestors\":[");
        first = true;
        for (uint32_t i = 0; i < MINT_MAX_ATTESTORS; i++) {
            const mint_attestor_t *a = &MS.att[i];
            if (!a->used) continue;
            zxvd_buf_printf(o, "%s{\"id\":%u,\"enabled\":%s,\"haircut_bps\":%u}", first ? "" : ",",
                            (unsigned) a->id, a->enabled ? "true" : "false",
                            (unsigned) a->haircut_bps);
            first = false;
        }
        ZXVD_RAW(o, "],\"units\":[");
        for (uint32_t k = 0; k < MINT_KINDS; k++) {
            zxvd_buf_printf(o, "%s{\"capacity\":\"%s\"", k ? "," : "", MINT_KIND[k]);
            zxvd_sandbox_kv(o, ",\"num\":", MS.unit[k].num);
            zxvd_sandbox_kv(o, ",\"den\":", MS.unit[k].den);
            ZXVD_RAW(o, "}");
        }
        ZXVD_RAW(o, "],\"claims\":[");
        first = true;
        for (uint32_t i = 0; i < MINT_MAX_CLAIMS; i++) {
            if (MS.claim[i].state == MINT_FREE) continue;
            if (!first) ZXVD_RAW(o, ",");
            put_claim(o, &MS.claim[i]);
            first = false;
        }
        ZXVD_RAW(o, "]");
        put_check(o, mint_check(&MS));
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void mint_close(void)
{
    MB.open = false;
    MNK.n = 0;
    memset(&MS, 0, sizeof MS);
}

static const zxvd_group_method_t MINT_M[] = {
    {"mint.setup", m_mint_setup, false},
    {"mint.key", m_mint_key, false},
    {"mint.account_open", m_mint_account_open, false},
    {"mint.account", m_mint_account, true},
    {"mint.juris", m_mint_juris, false},
    {"mint.attestor", m_mint_attestor, false},
    {"mint.unit", m_mint_unit, false},
    {"mint.message", m_mint_message, true},
    {"mint.issue", m_mint_issue, false},
    {"mint.epoch", m_mint_epoch, false},
    {"mint.quarantine", m_mint_quarantine, false},
    {"mint.retire", m_mint_retire, false},
    {"mint.status", m_mint_status, true},
};

const zxvd_group_t *zxvd_mint_group(void)
{
    static const zxvd_group_t G = {
        "mint",
        true,
        "minting Vino or V-Bills against attested capacity (mint.h); every jurisdiction off "
        "until set; likely securities; sandbox ledger in memory",
        MINT_M,
        sizeof MINT_M / sizeof MINT_M[0],
        mint_close};
    return &G;
}

/* ===================== qnote.* ===================== */

static zxvd_sandbox_book_t QB = {
    .a = {{.name = "fiat", .code = "XTS", .minor = 2, .fundable = true}}, .n_assets = 1};
static qnote_t QS;
static uint32_t QISS[QNOTE_MAX_LOTS]; /* one issuer per Q-Note asset, made at setup */

static void qn_refuse(zxvd_call_t *c, qnote_status_t st)
{
    static const char *const N[][2] = {
        {"QNOTE_OK", ""},
        {"QNOTE_ERR_ARG", "a bad argument (an unusable account, a price outside the ladder, a "
                          "zero quantity)"},
        {"QNOTE_ERR_FULL", "the table is full"},
        {"QNOTE_ERR_STATE", "wrong lot state for this step"},
        {"QNOTE_ERR_DUP", "that claim already has a lot"},
        {"QNOTE_ERR_LEDGER", "the ledger refused the posting"},
        {"QNOTE_ERR_PAYEE", "a holder has no fiat account to be paid in"},
        {"QNOTE_ERR_VAULT", "the vault refused"},
        {"QNOTE_ERR_OVERFLOW", "a value does not fit"},
    };
    refuse_tab(c, N, sizeof N / sizeof N[0], st, 3);
}

static bool qn_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &QB, "qnote");
}

static bool qn_lot(zxvd_call_t *c, uint32_t *lot)
{
    if (!zxvd_sandbox_need_u32(c, "lot", lot)) return false;
    if (*lot < QNOTE_MAX_LOTS && QS.lot[*lot].state != QNOTE_FREE) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "no lot %u", (unsigned) *lot);
    return false;
}

static void put_lot(zxvd_buf_t *o, uint32_t i)
{
    static const char *const ST[] = {"free", "open", "cleared", "settled", "closed"};
    const qnote_lot_t *k = &QS.lot[i];
    const pay_asset_t *a = pay_ledger_asset(&QB.L, k->asset);
    zxvd_buf_printf(o, "{\"lot\":%u,\"state\":\"%s\",\"asset\":\"%s\",\"issuer\":%u", (unsigned) i,
                    ST[k->state % 5], a ? a->code : "?", (unsigned) k->issuer);
    put_hex32(o, ",\"claim\":", k->claim);
    zxvd_sandbox_kv(o, ",\"face_minor\":", k->face);
    zxvd_sandbox_kv(o, ",\"unit_face_minor\":", k->unit_face);
    zxvd_sandbox_kv(o, ",\"units\":", k->units);
    zxvd_sandbox_kv(o, ",\"dust_minor\":", k->dust);
    zxvd_sandbox_kv(o, ",\"p_start\":", k->p_start);
    zxvd_sandbox_kv(o, ",\"p_step\":", k->p_step);
    zxvd_sandbox_kv(o, ",\"p_min\":", k->p_min);
    zxvd_buf_printf(o, ",\"bids\":%u,\"winners\":%u", (unsigned) k->n_bids, (unsigned) k->winners);
    zxvd_sandbox_kv(o, ",\"locked_minor\":", k->locked);
    zxvd_sandbox_kv(o, ",\"price\":", k->price);
    zxvd_sandbox_kv(o, ",\"sold\":", k->sold);
    zxvd_sandbox_kv(o, ",\"unsold\":", k->unsold);
    zxvd_sandbox_kv(o, ",\"proceeds_minor\":", k->proceeds);
    zxvd_sandbox_kv(o, ",\"loss_minor\":", k->loss);
    zxvd_sandbox_kv(o, ",\"carry_minor\":", k->carry);
    zxvd_sandbox_kv(o, ",\"paid_minor\":", k->paid);
    zxvd_sandbox_kv(o, ",\"retired\":", k->retired);
    zxvd_sandbox_kv(o, ",\"excess_minor\":", k->excess);
    zxvd_sandbox_kv(o, ",\"written_off\":", k->written_off);
    ZXVD_RAW(o, "}");
}

static void m_qn_setup(zxvd_call_t *c)
{
    if (QB.open) return zxvd_sandbox_refuse(c, "SET_UP", "qnote is set up already (this run)");
    uint32_t res, esc;
    bool ok = zxvd_sandbox_book_open(&QB) == 0 && open_acct(&QB, QB.a[0].asset, 0, &res) &&
              open_acct(&QB, QB.a[0].asset, 0, &esc);
    for (uint32_t i = 0; ok && i < QNOTE_MAX_LOTS; i++) {
        /* QNOTE_MAX_LOTS is 8: one digit names the lot */
        char code[8] = {'Q', 'N', (char) ('0' + i % 10u), 0};
        uint16_t as;
        ok = pay_ledger_add_asset(&QB.L, code, 0, PAY_ASSET_UNIT, &as) == PAY_OK &&
             open_acct(&QB, as, PAY_ACCT_ISSUER, &QISS[i]);
    }
    if (!ok) {
        QB.open = false;
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    }
    qnote_status_t st =
        qnote_init(&QS, &QB.L, res, esc, (const uint8_t *) "zxvd qnote sandbox seed, v1.....");
    if (st != QNOTE_OK) {
        QB.open = false;
        return qn_refuse(c, st);
    }
    zxvd_buf_printf(zxvd_result(c), "{\"reserve\":%u,\"escrow\":%u,\"lots_max\":%u}",
                    (unsigned) res, (unsigned) esc, (unsigned) QNOTE_MAX_LOTS);
}

static void m_qn_account_open(zxvd_call_t *c)
{
    if (qn_ready(c)) zxvd_sandbox_m_account_open(c, &QB);
}
static void m_qn_fund(zxvd_call_t *c)
{
    if (qn_ready(c)) zxvd_sandbox_m_fund(c, &QB);
}

/* the account's asset by its ledger code (a lot's Q-Notes are QN0..QN7) */
static void m_qn_account(zxvd_call_t *c)
{
    uint32_t acct;
    if (!qn_ready(c) || !zxvd_sandbox_need_u32(c, "account", &acct)) return;
    const pay_account_t *x = pay_ledger_account(&QB.L, acct);
    if (!x) return zxvd_fail(c, ZXVD_E_PARAMS, "no account %u", (unsigned) acct);
    const pay_asset_t *a = pay_ledger_asset(&QB.L, x->asset);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"account\":%u,\"owner\":%u,\"asset\":\"%s\"", (unsigned) acct,
                    (unsigned) x->owner, x->asset == QB.a[0].asset ? "fiat" : a->code);
    zxvd_sandbox_kv(o, ",\"balance_minor\":", x->debit);
    zxvd_sandbox_kv(o, ",\"credit_minor\":", x->credit);
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void m_qn_lot_open(zxvd_call_t *c)
{
    uint8_t claim[32];
    uint64_t face, uf, ps, step, pm;
    uint32_t lot = 0;
    if (!qn_ready(c) || !zxvd_sandbox_key32(c, "claim", claim) ||
        !zxvd_sandbox_need_u64(c, "face_minor", &face) ||
        !zxvd_sandbox_need_u64(c, "unit_face_minor", &uf) ||
        !zxvd_sandbox_need_u64(c, "p_start", &ps) || !zxvd_sandbox_need_u64(c, "p_step", &step) ||
        !zxvd_sandbox_need_u64(c, "p_min", &pm))
        return;
    /* the first prepared issuer no lot has taken (a lot keeps its asset) */
    uint32_t i = 0;
    for (; i < QNOTE_MAX_LOTS; i++) {
        bool used = false;
        for (uint32_t k = 0; k < QNOTE_MAX_LOTS; k++)
            if (QS.lot[k].state != QNOTE_FREE && QS.lot[k].issuer == QISS[i]) used = true;
        if (!used) break;
    }
    if (i == QNOTE_MAX_LOTS) return qn_refuse(c, QNOTE_ERR_FULL);
    qnote_status_t st = qnote_lot_open(&QS, claim, face, uf, QISS[i], ps, step, pm, &lot);
    if (st != QNOTE_OK) return qn_refuse(c, st);
    put_lot(zxvd_result(c), lot);
}

static void m_qn_lot_account(zxvd_call_t *c)
{
    uint32_t lot, owner, acct;
    if (!qn_ready(c) || !qn_lot(c, &lot) || !zxvd_sandbox_owner(c, "owner", &owner)) return;
    if (pay_ledger_open(&QB.L, owner, QS.lot[lot].asset, PAY_CAP_FINANCIAL, 0, &acct) != PAY_OK)
        return zxvd_sandbox_refuse(c, "PAY_ERR_FULL", "the sandbox ledger has no room");
    zxvd_buf_printf(zxvd_result(c), "{\"account\":%u,\"owner\":%u,\"lot\":%u}", (unsigned) acct,
                    (unsigned) owner, (unsigned) lot);
}

static void m_qn_bid(zxvd_call_t *c)
{
    uint32_t lot, fiat, qa, idx = 0;
    uint64_t qty, limit, now;
    if (!qn_ready(c) || !qn_lot(c, &lot) || !zxvd_sandbox_need_u32(c, "fiat_account", &fiat) ||
        !zxvd_sandbox_need_u32(c, "qnote_account", &qa) || !zxvd_sandbox_need_u64(c, "qty", &qty) ||
        !zxvd_sandbox_need_u64(c, "limit", &limit) || !zxvd_sandbox_now(c, &now))
        return;
    qnote_status_t st = qnote_bid(&QS, lot, fiat, qa, qty, limit, now, &idx);
    if (st != QNOTE_OK) return qn_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"lot\":%u,\"bid\":%u", (unsigned) lot, (unsigned) idx);
    zxvd_sandbox_kv(o, ",\"locked_minor\":", QS.lot[lot].bid[idx].locked);
    ZXVD_RAW(o, "}");
}

static void m_qn_clear(zxvd_call_t *c)
{
    uint32_t lot;
    if (!qn_ready(c) || !qn_lot(c, &lot)) return;
    qnote_status_t st = qnote_clear(&QS, lot);
    if (st != QNOTE_OK) return qn_refuse(c, st);
    put_lot(zxvd_result(c), lot);
}

static void m_qn_settle(zxvd_call_t *c)
{
    uint32_t lot;
    uint64_t now;
    if (!qn_ready(c) || !qn_lot(c, &lot) || !zxvd_sandbox_now(c, &now)) return;
    qnote_status_t st = qnote_settle(&QS, lot, now);
    if (st != QNOTE_OK) return qn_refuse(c, st);
    put_lot(zxvd_result(c), lot);
}

static void m_qn_recover(zxvd_call_t *c)
{
    uint32_t lot, from;
    uint64_t amt, now;
    if (!qn_ready(c) || !qn_lot(c, &lot) || !zxvd_sandbox_need_u32(c, "from", &from) ||
        !zxvd_sandbox_need_u64(c, "amount_minor", &amt) || !zxvd_sandbox_now(c, &now))
        return;
    qnote_status_t st = qnote_recover(&QS, lot, from, amt, now);
    if (st != QNOTE_OK) return qn_refuse(c, st);
    put_lot(zxvd_result(c), lot);
}

static void m_qn_close(zxvd_call_t *c)
{
    uint32_t lot;
    uint64_t now;
    if (!qn_ready(c) || !qn_lot(c, &lot) || !zxvd_sandbox_now(c, &now)) return;
    qnote_status_t st = qnote_close(&QS, lot, now);
    if (st != QNOTE_OK) return qn_refuse(c, st);
    put_lot(zxvd_result(c), lot);
}

static void m_qn_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s", QB.open ? "true" : "false");
    if (QB.open) {
        zxvd_buf_printf(o, ",\"reserve\":%u,\"escrow\":%u", (unsigned) QS.reserve,
                        (unsigned) QS.escrow);
        zxvd_sandbox_kv(o, ",\"reserve_minor\":", bal(&QB, QS.reserve));
        zxvd_sandbox_kv(o, ",\"escrow_minor\":", bal(&QB, QS.escrow));
        zxvd_sandbox_kv(o, ",\"reserve_in_minor\":", QS.reserve_in);
        ZXVD_RAW(o, ",\"lots\":[");
        bool first = true;
        for (uint32_t i = 0; i < QNOTE_MAX_LOTS; i++) {
            if (QS.lot[i].state == QNOTE_FREE) continue;
            if (!first) ZXVD_RAW(o, ",");
            put_lot(o, i);
            first = false;
        }
        ZXVD_RAW(o, "]");
        put_check(o, qnote_check(&QS));
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void qn_close(void)
{
    QB.open = false;
    memset(&QS, 0, sizeof QS);
}

static const zxvd_group_method_t QN_M[] = {
    {"qnote.setup", m_qn_setup, false},       {"qnote.account_open", m_qn_account_open, false},
    {"qnote.fund", m_qn_fund, false},         {"qnote.account", m_qn_account, true},
    {"qnote.lot_open", m_qn_lot_open, false}, {"qnote.lot_account", m_qn_lot_account, false},
    {"qnote.bid", m_qn_bid, false},           {"qnote.clear", m_qn_clear, false},
    {"qnote.settle", m_qn_settle, false},     {"qnote.recover", m_qn_recover, false},
    {"qnote.close", m_qn_close, false},       {"qnote.status", m_qn_status, true},
};

const zxvd_group_t *zxvd_qnote_group(void)
{
    static const zxvd_group_t G = {
        "qnote",
        true,
        "Q-Notes (qnote.h): quarantine lots sold by Dutch auction, a claim on salvage only; "
        "likely securities; sandbox ledger in memory",
        QN_M,
        sizeof QN_M / sizeof QN_M[0],
        qn_close};
    return &G;
}

/* ===================== fx.* ===================== */

static zxvd_sandbox_book_t FB = {
    .a = {{.name = "xta", .code = "XTA", .minor = 2, .fundable = true},
          {.name = "xtb", .code = "XTB", .minor = 2, .fundable = true}},
    .n_assets = 2};
static zxvd_sandbox_keys_t FK;
static fx_t FS;

static void fx_refuse(zxvd_call_t *c, fx_status_t st)
{
    static const char *const N[][2] = {
        {"FX_OK", ""},
        {"FX_ERR_ARG", "a bad argument (an account of another asset or owner, an escrow or fee "
                       "account, a zero quantity or limit)"},
        {"FX_ERR_DISABLED", "the batch market is off until fx.config turns it on"},
        {"FX_ERR_STATE", "out of order for the epoch"},
        {"FX_ERR_FULL", "the order table is full"},
        {"FX_ERR_SIG", "the owner's signature does not verify"},
        {"FX_ERR_DUP", "this owner already used that nonce in this epoch (replay)"},
        {"FX_ERR_LEDGER", "the ledger refused the posting"},
        {"FX_ERR_OVERFLOW", "a value does not fit"},
        {"FX_ERR_LEVERAGE", "leverage is not offered"},
    };
    refuse_tab(c, N, sizeof N / sizeof N[0], st, 3);
}

static bool fx_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &FB, "fx");
}

static const char *const FX_SIDE[] = {"sell", "buy"};
static const char *const FX_STATE[] = {"idle", "open", "cleared", "settled"};

static void put_fx_record(zxvd_buf_t *o, const fx_record_t *r)
{
    zxvd_sandbox_kv(o, "{\"epoch\":", r->epoch);
    zxvd_sandbox_kv(o, ",\"market_price\":", r->market);
    zxvd_sandbox_kv(o, ",\"price\":", r->price);
    zxvd_sandbox_kv(o, ",\"ref_before\":", r->ref_before);
    zxvd_sandbox_kv(o, ",\"supply\":", r->supply);
    zxvd_sandbox_kv(o, ",\"demand\":", r->demand);
    zxvd_sandbox_kv(o, ",\"volume\":", r->volume);
    zxvd_sandbox_kv(o, ",\"gross_minor\":", r->gross);
    zxvd_sandbox_kv(o, ",\"fee_minor\":", r->fee);
    zxvd_buf_printf(o, ",\"fills\":%u,\"cooling\":%s,\"cancelled\":%s}", (unsigned) r->fills,
                    r->cooling ? "true" : "false", r->cancelled ? "true" : "false");
}

static void m_fx_setup(zxvd_call_t *c)
{
    if (FB.open) return zxvd_sandbox_refuse(c, "SET_UP", "fx is set up already (this run)");
    fx_config_t cfg;
    fx_config_default(&cfg);
    uint32_t market;
    uint64_t scale;
    if (!zxvd_sandbox_u32(c, "market", false, 1, &market) ||
        !zxvd_sandbox_u64(c, "scale", false, 100, &scale) ||
        !zxvd_sandbox_u32(c, "band_bps", false, cfg.band_bps, &cfg.band_bps) ||
        !zxvd_sandbox_u32(c, "max_orders", false, cfg.max_orders, &cfg.max_orders))
        return;
    uint32_t eb, eq, fee;
    if (zxvd_sandbox_book_open(&FB) != 0 || !open_acct(&FB, FB.a[0].asset, 0, &eb) ||
        !open_acct(&FB, FB.a[1].asset, 0, &eq) || !open_acct(&FB, FB.a[1].asset, 0, &fee)) {
        FB.open = false;
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    }
    fx_status_t st = fx_init(&FS, &FB.L, &cfg, market, key_verify, &FK,
                             (const uint8_t *) "zxvd fx sandbox seed, v1........");
    if (st == FX_OK) st = fx_add_book(&FS, eb, eq, fee, scale, NULL);
    if (st != FX_OK) {
        FB.open = false;
        return fx_refuse(c, st);
    }
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o,
                    "{\"market\":%u,\"base\":\"xta\",\"quote\":\"xtb\",\"base_escrow\":%u,"
                    "\"quote_escrow\":%u,\"fee_account\":%u",
                    (unsigned) market, (unsigned) eb, (unsigned) eq, (unsigned) fee);
    zxvd_sandbox_kv(o, ",\"scale\":", scale);
    zxvd_buf_printf(o, ",\"band_bps\":%u,\"max_orders\":%u,\"enabled\":false,\"leverage\":0}",
                    (unsigned) cfg.band_bps, (unsigned) cfg.max_orders);
}

static void m_fx_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_key(c, &FK);
}
static void m_fx_account_open(zxvd_call_t *c)
{
    if (fx_ready(c)) zxvd_sandbox_m_account_open(c, &FB);
}
static void m_fx_fund(zxvd_call_t *c)
{
    if (fx_ready(c)) zxvd_sandbox_m_fund(c, &FB);
}
static void m_fx_account(zxvd_call_t *c)
{
    if (fx_ready(c)) zxvd_sandbox_m_account(c, &FB);
}

/* F1: the engine's own switch (no leverage parameter: it is always 0) */
static void m_fx_config(zxvd_call_t *c)
{
    if (!fx_ready(c)) return;
    fx_config_t cfg = FS.cfg;
    if (!zxvd_p_bool(c, "enabled", cfg.enabled, &cfg.enabled) ||
        !zxvd_sandbox_u32(c, "band_bps", false, cfg.band_bps, &cfg.band_bps) ||
        !zxvd_sandbox_u32(c, "max_orders", false, cfg.max_orders, &cfg.max_orders))
        return;
    cfg.leverage = 0;
    fx_status_t st = fx_set_config(&FS, &cfg);
    if (st != FX_OK) return fx_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c),
                    "{\"enabled\":%s,\"band_bps\":%u,\"max_orders\":%u,\"leverage\":0}",
                    FS.cfg.enabled ? "true" : "false", (unsigned) FS.cfg.band_bps,
                    (unsigned) FS.cfg.max_orders);
}

static void m_fx_open(zxvd_call_t *c)
{
    if (!fx_ready(c)) return;
    fx_status_t st = fx_open(&FS);
    if (st != FX_OK) return fx_refuse(c, st);
    zxvd_sandbox_kv(zxvd_result(c), "{\"epoch\":", FS.epoch);
    ZXVD_RAW(zxvd_result(c), ",\"state\":\"open\"}");
}

typedef struct {
    uint32_t side, base, quote;
    uint64_t qty, limit, nonce;
} fx_terms_t;

static bool fx_terms(zxvd_call_t *c, fx_terms_t *t)
{
    return pick(c, "side", FX_SIDE, 2, "\"sell\" or \"buy\"", &t->side) &&
           zxvd_sandbox_need_u32(c, "base_account", &t->base) &&
           zxvd_sandbox_need_u32(c, "quote_account", &t->quote) &&
           zxvd_sandbox_need_u64(c, "qty", &t->qty) &&
           zxvd_sandbox_need_u64(c, "limit", &t->limit) &&
           zxvd_sandbox_need_u64(c, "nonce", &t->nonce);
}

static void m_fx_message(zxvd_call_t *c)
{
    char kind[16];
    fx_terms_t t;
    uint8_t m[FX_ORDER_MSG];
    if (!fx_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (strcmp(kind, "order")) return zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"order\"");
    if (!fx_terms(c, &t)) return;
    fx_order_msg(FS.market_id, FS.epoch, 0, (uint8_t) t.side, t.base, t.quote, t.qty, t.limit,
                 t.nonce, m);
    zxvd_sandbox_put_msg(c, m, sizeof m);
}

static void m_fx_order(zxvd_call_t *c)
{
    fx_terms_t t;
    uint8_t sig[64];
    uint64_t now;
    uint32_t idx = 0;
    if (!fx_ready(c) || !fx_terms(c, &t) || !zxvd_sandbox_sig(c, "sig", sig) ||
        !zxvd_sandbox_now(c, &now))
        return;
    fx_status_t st = fx_order(&FS, 0, (uint8_t) t.side, t.base, t.quote, t.qty, t.limit, t.nonce,
                              now, sig, 64, &idx);
    if (st != FX_OK) return fx_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"order\":%u,\"side\":\"%s\",\"owner\":%u", (unsigned) idx,
                    FX_SIDE[t.side], (unsigned) FS.order[idx].owner);
    zxvd_sandbox_kv(o, ",\"locked_minor\":", FS.order[idx].locked);
    zxvd_sandbox_kv(o, ",\"epoch\":", FS.epoch);
    ZXVD_RAW(o, "}");
}

static void m_fx_clear(zxvd_call_t *c)
{
    if (!fx_ready(c)) return;
    fx_status_t st = fx_clear(&FS);
    if (st != FX_OK) return fx_refuse(c, st);
    put_fx_record(zxvd_result(c), &FS.book[0].cur);
}

static void m_fx_settle(zxvd_call_t *c)
{
    uint64_t now;
    if (!fx_ready(c) || !zxvd_sandbox_now(c, &now)) return;
    fx_status_t st = fx_settle(&FS, now);
    if (st != FX_OK) return fx_refuse(c, st);
    const fx_record_t *r = fx_record(&FS, FS.epoch, 0);
    if (r)
        put_fx_record(zxvd_result(c), r);
    else
        ZXVD_RAW(zxvd_result(c), "{\"state\":\"settled\"}");
}

static void m_fx_cancel(zxvd_call_t *c)
{
    uint64_t now;
    if (!fx_ready(c) || !zxvd_sandbox_now(c, &now)) return;
    fx_status_t st = fx_cancel(&FS, now);
    if (st != FX_OK) return fx_refuse(c, st);
    zxvd_sandbox_kv(zxvd_result(c), "{\"epoch\":", FS.epoch);
    ZXVD_RAW(zxvd_result(c), ",\"state\":\"settled\",\"cancelled\":true}");
}

static void m_fx_net(zxvd_call_t *c)
{
    static fx_net_t n;
    if (!fx_ready(c)) return;
    fx_status_t st = fx_net(&FS, &n);
    if (st != FX_OK) return fx_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    ZXVD_RAW(o, "{\"positions\":[");
    for (uint32_t i = 0; i < n.n_pos; i++) {
        const fx_pos_t *p = &n.pos[i];
        zxvd_buf_printf(o, "%s{\"owner\":%u,\"asset\":\"%s\",\"net_minor\":\"%lld\"}", i ? "," : "",
                        (unsigned) p->owner, p->asset == FB.a[0].asset ? "xta" : "xtb",
                        (long long) p->net);
    }
    zxvd_buf_printf(o,
                    "],\"nonzero\":%u,\"gross_legs\":%u,\"net_legs\":%u,\"saved\":%u,"
                    "\"balanced\":%s}",
                    (unsigned) n.nonzero, (unsigned) n.gross_legs, (unsigned) n.net_legs,
                    (unsigned) n.saved, n.balanced ? "true" : "false");
}

static void m_fx_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u,\"leverage\":0", FB.open ? "true" : "false",
                    (unsigned) FK.n);
    if (FB.open) {
        const fx_book_t *b = &FS.book[0];
        zxvd_buf_printf(o, ",\"enabled\":%s,\"band_bps\":%u,\"max_orders\":%u,\"state\":\"%s\"",
                        FS.cfg.enabled ? "true" : "false", (unsigned) FS.cfg.band_bps,
                        (unsigned) FS.cfg.max_orders, FX_STATE[FS.state & 3]);
        zxvd_sandbox_kv(o, ",\"epoch\":", FS.epoch);
        zxvd_sandbox_kv(o, ",\"reference\":", b->has_ref ? b->ref : 0);
        zxvd_sandbox_kv(o, ",\"volume_total\":", b->volume_total);
        zxvd_sandbox_kv(o, ",\"gross_total_minor\":", b->gross_total);
        zxvd_sandbox_kv(o, ",\"fee_total_minor\":", b->fee_total);
        zxvd_sandbox_kv(o, ",\"fee_account_minor\":", bal(&FB, b->fee_acct));
        ZXVD_RAW(o, ",\"orders\":[");
        for (uint32_t i = 0; i < FS.n_orders; i++) {
            const fx_order_t *r = &FS.order[i];
            zxvd_buf_printf(o, "%s{\"order\":%u,\"side\":\"%s\",\"owner\":%u", i ? "," : "",
                            (unsigned) i, FX_SIDE[r->side & 1], (unsigned) r->owner);
            zxvd_sandbox_kv(o, ",\"qty\":", r->qty);
            zxvd_sandbox_kv(o, ",\"limit\":", r->limit);
            zxvd_sandbox_kv(o, ",\"locked_minor\":", r->locked);
            zxvd_sandbox_kv(o, ",\"fill\":", r->fill);
            zxvd_sandbox_kv(o, ",\"quote_minor\":", r->quote);
            zxvd_sandbox_kv(o, ",\"fee_minor\":", r->fee);
            zxvd_buf_printf(o, ",\"settled\":%s}", r->settled ? "true" : "false");
        }
        ZXVD_RAW(o, "]");
        put_check(o, fx_check(&FS));
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void fx_close_group(void)
{
    FB.open = false;
    FK.n = 0;
    memset(&FS, 0, sizeof FS);
}

static const zxvd_group_method_t FX_M[] = {
    {"fx.setup", m_fx_setup, false},
    {"fx.key", m_fx_key, false},
    {"fx.account_open", m_fx_account_open, false},
    {"fx.fund", m_fx_fund, false},
    {"fx.account", m_fx_account, true},
    {"fx.config", m_fx_config, false},
    {"fx.open", m_fx_open, false},
    {"fx.message", m_fx_message, true},
    {"fx.order", m_fx_order, false},
    {"fx.clear", m_fx_clear, false},
    {"fx.settle", m_fx_settle, false},
    {"fx.cancel", m_fx_cancel, false},
    {"fx.net", m_fx_net, true},
    {"fx.status", m_fx_status, true},
};

const zxvd_group_t *zxvd_fx_group(void)
{
    static const zxvd_group_t G = {
        "fx",
        true,
        "FX by uniform-price batch epochs (fxepoch.h): no leverage, cooling bands; FX dealing is "
        "regulated; sandbox ledger in memory",
        FX_M,
        sizeof FX_M / sizeof FX_M[0],
        fx_close_group};
    return &G;
}

/* ===================== vhub.* ===================== */

#define VHUB_CCYS   2u /* xta (slot 0), xtb (slot 1) */
#define VHUB_QLOG   16u
#define VHUB_LIMITS (VHUB_CCYS * VHUB_MAX_RULES)

static zxvd_sandbox_book_t VB = {
    .a = {{.name = "vbill", .code = "VBILL", .fundable = true},
          {.name = "xta", .code = "XTA", .minor = 2, .fundable = true},
          {.name = "xtb", .code = "XTB", .minor = 2, .fundable = true}},
    .n_assets = 3};
static zxvd_sandbox_keys_t VK;
static vhub_t VS;
static lpx_qrec_t VQ[VHUB_QLOG];
static vhub_limit_t VL[VHUB_LIMITS];
static uint32_t VL_N;

static const char *const VHUB_CCY[] = {"xta", "xtb"};

static void vh_refuse(zxvd_call_t *c, vhub_status_t st)
{
    static const char *const N[][2] = {
        {"VHUB_OK", ""},
        {"VHUB_ERR_ARG", "a bad argument (the same currency twice, a hub account as a party, an "
                         "amount past 2^59, a peg not set)"},
        {"VHUB_ERR_FULL", "the rule table is full"},
        {"VHUB_ERR_SIG", "the issuer's signature does not verify"},
        {"VHUB_ERR_EPOCH", "the peg epoch is not above the last (replay)"},
        {"VHUB_ERR_INEXACT", "the amount does not convert exactly: see vhub.quote"},
        {"VHUB_ERR_LEDGER", "the ledger refused (funds or backing)"},
        {"VHUB_REFUSED", "a jurisdiction rule refused the swap"},
        {"VHUB_WAIT", "a rule has not decided, or a currency has no rule (fails closed)"},
        {"VHUB_ESCALATE", "a rule escalates the swap to a person"},
        {"VHUB_FORBIDDEN", "a rule forbids the swap; it is in the quarantine log"},
        {"VHUB_HALTED", "the quarantine log is full: the hub fails closed"},
    };
    refuse_tab(c, N, sizeof N / sizeof N[0], st, 1);
}

static bool vh_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &VB, "vhub");
}

static bool vh_ccy(zxvd_call_t *c, const char *key, uint32_t *s)
{
    return pick(c, key, VHUB_CCY, VHUB_CCYS, "\"xta\" or \"xtb\"", s);
}

static void m_vh_setup(zxvd_call_t *c)
{
    if (VB.open) return zxvd_sandbox_refuse(c, "SET_UP", "vhub is set up already (this run)");
    uint32_t iss[VHUB_CCYS];
    if (!zxvd_sandbox_need_u32(c, "issuer_xta", &iss[0]) ||
        !zxvd_sandbox_need_u32(c, "issuer_xtb", &iss[1]))
        return;
    uint32_t pool[VHUB_CCYS], vf[VHUB_CCYS];
    bool ok = zxvd_sandbox_book_open(&VB) == 0;
    for (uint32_t i = 0; ok && i < VHUB_CCYS; i++)
        ok = open_acct(&VB, VB.a[1 + i].asset, 0, &pool[i]) &&
             open_acct(&VB, VB.a[0].asset, 0, &vf[i]);
    if (!ok) {
        VB.open = false;
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    }
    vhub_init(&VS, &VB.L, VB.a[0].asset, key_verify, &VK,
              (const uint8_t *) "zxvd vhub sandbox seed, v1......", VQ, VHUB_QLOG);
    for (uint32_t i = 0; i < VHUB_CCYS; i++)
        vhub_add_ccy(&VS, (uint8_t) i, VB.a[1 + i].asset, iss[i], pool[i], vf[i]);
    VL_N = 0;
    zxvd_buf_t *o = zxvd_result(c);
    ZXVD_RAW(o, "{\"currencies\":[");
    for (uint32_t i = 0; i < VHUB_CCYS; i++)
        zxvd_buf_printf(o, "%s{\"ccy\":\"%s\",\"issuer_key\":%u,\"pool\":%u,\"vbill_float\":%u}",
                        i ? "," : "", VHUB_CCY[i], (unsigned) iss[i], (unsigned) pool[i],
                        (unsigned) vf[i]);
    ZXVD_RAW(o, "],\"pegs_set\":false,\"rules\":0}");
}

static void m_vh_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_key(c, &VK);
}
static void m_vh_account_open(zxvd_call_t *c)
{
    if (vh_ready(c)) zxvd_sandbox_m_account_open(c, &VB);
}
static void m_vh_fund(zxvd_call_t *c)
{
    if (vh_ready(c)) zxvd_sandbox_m_fund(c, &VB);
}
static void m_vh_account(zxvd_call_t *c)
{
    if (vh_ready(c)) zxvd_sandbox_m_account(c, &VB);
}

/* H4: the issuer's backing, as sandbox units: V-Bills into the float, or the
 * currency into the pool */
static void m_vh_back(zxvd_call_t *c)
{
    static const char *const LEG[] = {"float", "pool"};
    uint32_t s, leg;
    uint64_t amt, now;
    if (!vh_ready(c) || !vh_ccy(c, "ccy", &s) ||
        !pick(c, "leg", LEG, 2, "\"float\" or \"pool\"", &leg) ||
        !zxvd_sandbox_need_u64(c, "amount_minor", &amt) || !zxvd_sandbox_now(c, &now))
        return;
    const vhub_ccy_t *x = &VS.ccy[s];
    uint32_t to = leg ? x->pool : x->vfloat, from = leg ? VB.a[1 + s].issuer : VB.a[0].issuer;
    pay_posting_req_t r;
    pay_receipt_t rc;
    book_req(&VB, "back", now, &r);
    if (pay_ledger_issue(&VB.L, &r, from, to, amt, &rc) != PAY_OK)
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused the amount");
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"ccy\":\"%s\",\"leg\":\"%s\",\"account\":%u", VHUB_CCY[s], LEG[leg],
                    (unsigned) to);
    zxvd_sandbox_kv(o, ",\"balance_minor\":", bal(&VB, to));
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void m_vh_message(zxvd_call_t *c)
{
    char kind[16];
    uint32_t s;
    uint64_t num, den, epoch;
    uint8_t m[VHUB_PEG_MSG];
    if (!vh_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (strcmp(kind, "peg")) return zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"peg\"");
    if (!vh_ccy(c, "ccy", &s) || !zxvd_sandbox_need_u64(c, "num", &num) ||
        !zxvd_sandbox_need_u64(c, "den", &den) || !zxvd_sandbox_need_u64(c, "epoch", &epoch))
        return;
    vhub_peg_msg((uint8_t) s, num, den, epoch, m);
    zxvd_sandbox_put_msg(c, m, sizeof m);
}

static void m_vh_peg(zxvd_call_t *c)
{
    uint32_t s;
    uint64_t num, den, epoch;
    uint8_t sig[64];
    if (!vh_ready(c) || !vh_ccy(c, "ccy", &s) || !zxvd_sandbox_need_u64(c, "num", &num) ||
        !zxvd_sandbox_need_u64(c, "den", &den) || !zxvd_sandbox_need_u64(c, "epoch", &epoch) ||
        !zxvd_sandbox_sig(c, "sig", sig))
        return;
    vhub_status_t st = vhub_set_peg(&VS, (uint8_t) s, num, den, epoch, sig, 64);
    if (st != VHUB_OK) return vh_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"ccy\":\"%s\"", VHUB_CCY[s]);
    zxvd_sandbox_kv(o, ",\"num\":", num);
    zxvd_sandbox_kv(o, ",\"den\":", den);
    zxvd_sandbox_kv(o, ",\"epoch\":", epoch);
    ZXVD_RAW(o, "}");
}

/* H6: the ready-made capital control, at most `limit` out per sender per epoch */
static void m_vh_rule(zxvd_call_t *c)
{
    uint32_t s;
    uint64_t limit;
    if (!vh_ready(c) || !vh_ccy(c, "ccy", &s) || !zxvd_sandbox_need_u64(c, "limit_minor", &limit))
        return;
    if (VL_N >= VHUB_LIMITS) return vh_refuse(c, VHUB_ERR_FULL);
    vhub_limit_t *l = &VL[VL_N];
    memset(l, 0, sizeof *l);
    l->limit = limit;
    vhub_status_t st = vhub_add_rule(&VS, (uint8_t) s, vhub_rule_limit, vhub_limit_commit, l);
    if (st != VHUB_OK) return vh_refuse(c, st);
    VL_N++;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"ccy\":\"%s\",\"rule\":\"limit\"", VHUB_CCY[s]);
    zxvd_sandbox_kv(o, ",\"limit_minor\":", limit);
    zxvd_buf_printf(o, ",\"rules\":%u}", (unsigned) VS.ccy[s].n_rules);
}

static void m_vh_quote(zxvd_call_t *c)
{
    uint32_t a, b;
    uint64_t want;
    if (!vh_ready(c) || !vh_ccy(c, "from", &a) || !vh_ccy(c, "to", &b) ||
        !zxvd_sandbox_need_u64(c, "amount_minor", &want))
        return;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"from\":\"%s\",\"to\":\"%s\"", VHUB_CCY[a], VHUB_CCY[b]);
    zxvd_sandbox_kv(o, ",\"exact_minor\":", vhub_quote(&VS, (uint8_t) a, (uint8_t) b, want));
    ZXVD_RAW(o, "}");
}

static void m_vh_swap(zxvd_call_t *c)
{
    uint32_t a, b;
    uint64_t now;
    vhub_swap_t sw;
    memset(&sw, 0, sizeof sw);
    if (!vh_ready(c) || !vh_ccy(c, "from", &a) || !vh_ccy(c, "to", &b) ||
        !zxvd_sandbox_need_u32(c, "sender", &sw.sender) ||
        !zxvd_sandbox_need_u32(c, "receiver", &sw.receiver) ||
        !zxvd_sandbox_need_u64(c, "amount_minor", &sw.amount) ||
        !zxvd_sandbox_u64(c, "epoch", false, 0, &sw.epoch) || !zxvd_sandbox_now(c, &now))
        return;
    sw.from_ccy = (uint8_t) a;
    sw.to_ccy = (uint8_t) b;
    vhub_status_t st = vhub_swap(&VS, &sw, now);
    if (st != VHUB_OK) return vh_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"from\":\"%s\",\"to\":\"%s\",\"sender\":%u,\"receiver\":%u", VHUB_CCY[a],
                    VHUB_CCY[b], (unsigned) sw.sender, (unsigned) sw.receiver);
    zxvd_sandbox_kv(o, ",\"amount_minor\":", sw.amount);
    zxvd_sandbox_kv(o, ",\"vbills\":", sw.vbills);
    zxvd_sandbox_kv(o, ",\"out_minor\":", sw.out);
    ZXVD_RAW(o, "}");
}

static void m_vh_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u", VB.open ? "true" : "false", (unsigned) VK.n);
    if (VB.open) {
        ZXVD_RAW(o, ",\"currencies\":[");
        for (uint32_t i = 0; i < VHUB_CCYS; i++) {
            const vhub_ccy_t *x = &VS.ccy[i];
            zxvd_buf_printf(o, "%s{\"ccy\":\"%s\",\"issuer_key\":%u", i ? "," : "", VHUB_CCY[i],
                            (unsigned) x->issuer);
            zxvd_sandbox_kv(o, ",\"num\":", x->num);
            zxvd_sandbox_kv(o, ",\"den\":", x->den);
            zxvd_sandbox_kv(o, ",\"peg_epoch\":", x->peg_epoch);
            zxvd_sandbox_kv(o, ",\"pool_minor\":", bal(&VB, x->pool));
            zxvd_sandbox_kv(o, ",\"float_vbills\":", bal(&VB, x->vfloat));
            zxvd_buf_printf(o, ",\"rules\":%u}", (unsigned) x->n_rules);
        }
        zxvd_sandbox_kv(o, "],\"posted\":", VS.posted);
        zxvd_buf_printf(o, ",\"quarantined\":%u", (unsigned) VS.qlog.n);
        put_check(o, pay_ledger_check(&VB.L) && lpx_qlog_verify(&VS.qlog));
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void vh_close(void)
{
    VB.open = false;
    VK.n = 0;
    VL_N = 0;
    memset(&VS, 0, sizeof VS);
}

static const zxvd_group_method_t VH_M[] = {
    {"vhub.setup", m_vh_setup, false},
    {"vhub.key", m_vh_key, false},
    {"vhub.account_open", m_vh_account_open, false},
    {"vhub.fund", m_vh_fund, false},
    {"vhub.account", m_vh_account, true},
    {"vhub.back", m_vh_back, false},
    {"vhub.message", m_vh_message, true},
    {"vhub.peg", m_vh_peg, false},
    {"vhub.rule", m_vh_rule, false},
    {"vhub.quote", m_vh_quote, true},
    {"vhub.swap", m_vh_swap, false},
    {"vhub.status", m_vh_status, true},
};

const zxvd_group_t *zxvd_vhub_group(void)
{
    static const zxvd_group_t G = {
        "vhub",
        true,
        "cross-currency clearing through V-Bills (vhub.h): exact pegs, rules fail closed; "
        "V-Bills are likely securities; sandbox ledger in memory",
        VH_M,
        sizeof VH_M / sizeof VH_M[0],
        vh_close};
    return &G;
}

/* ===================== capmkt.* ===================== */

#define CM_PROOF_TAG "zxvd/capmkt-proof/v1"
#define CM_PROOF_MSG (20u + 4u + 4u + 8u + 8u + 32u)

static cm_market_t CMS;
static zxvd_sandbox_keys_t CMK;
static bool CM_OPEN;

static const char *const CM_RES[] = {"compute", "inference", "storage", "bandwidth"};
static const char *const CM_TEN[] = {"hour", "day", "week", "month"};

static void cm_refuse(zxvd_call_t *c, cm_status_t st)
{
    static const char *const N[][2] = {
        {"CM_OK", ""},
        {"CM_ERR_ARG", "a bad argument (a zero quantity or price, a delivery outside the window, "
                       "an order the account does not own)"},
        {"CM_ERR_FULL", "the table is full"},
        {"CM_ERR_FUNDS", "not enough deposit for the bid lock"},
        {"CM_ERR_OVERFLOW", "an amount past 2^62"},
        {"CM_ERR_UNKNOWN", "no such account, order or contract"},
        {"CM_ERR_STATE", "wrong state"},
        {"CM_ERR_PROOF", "the proof chain or the buyer's signature is refused"},
        {"CM_ERR_LATE", "delivered after the window: not paid, no penalty"},
        {"CM_ERR_STATE", "wrong state"}, /* -9 is no capmkt status: the default row */
        {"CM_ERR_SELF", "bid and ask from the same account"},
    };
    refuse_tab(c, N, sizeof N / sizeof N[0], st, 6);
}

static bool cm_ready(zxvd_call_t *c)
{
    return !not_set_up(c, CM_OPEN, "capmkt");
}

static void cm_id(uint32_t a, uint8_t id[CM_ID_BYTES])
{
    memset(id, 0, CM_ID_BYTES);
    for (int i = 0; i < 4; i++) id[i] = (uint8_t) (a >> (8 * i));
}

static uint32_t cm_num(const uint8_t id[CM_ID_BYTES])
{
    return (uint32_t) id[0] | (uint32_t) id[1] << 8 | (uint32_t) id[2] << 16 |
           (uint32_t) id[3] << 24;
}

static void put_le(uint8_t *p, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void cm_proof_msg(uint32_t contract, uint32_t seq, uint64_t units, uint64_t at_ms,
                         const uint8_t prev[32], uint8_t m[CM_PROOF_MSG])
{
    memcpy(m, CM_PROOF_TAG, 20);
    put_le(m + 20, contract, 4);
    put_le(m + 24, seq, 4);
    put_le(m + 28, units, 8);
    put_le(m + 36, at_ms, 8);
    memcpy(m + 44, prev, 32);
}

/* Y2: the evidence is the buyer's signature over the proof's fields */
static bool cm_verify(void *ctx, const cm_contract_t *k, const cm_proof_t *p)
{
    uint8_t m[CM_PROOF_MSG];
    cm_proof_msg(p->contract_id, p->seq, p->units, p->at_ms, p->prev, m);
    return zxvd_sandbox_verify(ctx, cm_num(k->buyer), m, sizeof m, p->evidence, 64);
}

/* C3: seconds on the wire, milliseconds in the engine */
static bool cm_secs(zxvd_call_t *c, const char *key, bool required, uint64_t *ms)
{
    uint64_t s;
    if (!zxvd_sandbox_u64(c, key, required, 0, &s)) return false;
    if (s > UINT64_MAX / 1000u) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" is out of range", key);
        return false;
    }
    *ms = s * 1000u;
    return true;
}

static bool cm_now(zxvd_call_t *c, uint64_t *ms)
{
    uint64_t s;
    if (!zxvd_sandbox_now(c, &s)) return false;
    if (s > UINT64_MAX / 1000u) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"now\" is out of range");
        return false;
    }
    *ms = s * 1000u;
    return true;
}

static bool cm_key(zxvd_call_t *c, cm_key_t *k)
{
    uint32_t r, t, region;
    if (!pick(c, "resource", CM_RES, 4, "\"compute\", \"inference\", \"storage\" or \"bandwidth\"",
              &r) ||
        !pick(c, "tenor", CM_TEN, 4, "\"hour\", \"day\", \"week\" or \"month\"", &t) ||
        !zxvd_sandbox_u32(c, "region", false, 0, &region))
        return false;
    if (region > 0xFFFFu) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"region\" must be below 65536");
        return false;
    }
    k->resource = (uint8_t) (r + 1);
    k->tenor = (uint8_t) (t + 1);
    k->region = (uint16_t) region;
    return true;
}

static void put_cm_account(zxvd_buf_t *o, uint32_t a)
{
    uint8_t id[CM_ID_BYTES];
    cm_id(a, id);
    const cm_account_t *x = cm_account(&CMS, id);
    zxvd_buf_printf(o, "{\"account\":%u", (unsigned) a);
    zxvd_sandbox_kv(o, ",\"available\":", x ? x->available : 0);
    zxvd_sandbox_kv(o, ",\"locked\":", x ? x->locked : 0);
    zxvd_sandbox_kv(o, ",\"escrow\":", x ? x->escrow : 0);
    zxvd_sandbox_kv(o, ",\"earned\":", x ? x->earned : 0);
    zxvd_sandbox_kv(o, ",\"delivered\":", x ? x->delivered : 0);
    zxvd_sandbox_kv(o, ",\"missed\":", x ? x->missed : 0);
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void put_contract(zxvd_buf_t *o, const cm_contract_t *k)
{
    zxvd_buf_printf(o,
                    "{\"contract\":%u,\"state\":\"%s\",\"bid\":%u,\"ask\":%u,\"buyer\":%u,"
                    "\"provider\":%u",
                    (unsigned) k->id, k->state == CM_C_OPEN ? "open" : "closed",
                    (unsigned) k->bid_id, (unsigned) k->ask_id, (unsigned) cm_num(k->buyer),
                    (unsigned) cm_num(k->provider));
    zxvd_sandbox_kv(o, ",\"qty\":", k->qty);
    zxvd_sandbox_kv(o, ",\"delivered\":", k->delivered);
    zxvd_sandbox_kv(o, ",\"price\":", k->price);
    zxvd_sandbox_kv(o, ",\"escrow\":", k->escrow);
    zxvd_sandbox_kv(o, ",\"paid\":", k->paid);
    zxvd_sandbox_kv(o, ",\"fee\":", k->fee);
    zxvd_sandbox_kv(o, ",\"start\":", k->start_ms / 1000u);
    zxvd_sandbox_kv(o, ",\"end\":", k->end_ms / 1000u);
    zxvd_buf_printf(o, ",\"proofs\":%u}", (unsigned) k->proof_seq);
}

static void m_cm_setup(zxvd_call_t *c)
{
    if (CM_OPEN) return zxvd_sandbox_refuse(c, "SET_UP", "capmkt is set up already (this run)");
    cm_params_t p;
    cm_params_default(&p);
    if (!zxvd_sandbox_u32(c, "share_num", false, p.share_num, &p.share_num) ||
        !zxvd_sandbox_u32(c, "share_den", false, p.share_den, &p.share_den))
        return;
    if (!p.share_num || p.share_num > p.share_den)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"share_num\" must be 1..\"share_den\"");
    p.verify = cm_verify;
    p.ctx = &CMK;
    cm_init(&CMS, &p);
    CM_OPEN = true;
    zxvd_buf_printf(zxvd_result(c), "{\"share_num\":%u,\"share_den\":%u,\"fee\":\"0.08889%%\"}",
                    (unsigned) p.share_num, (unsigned) p.share_den);
}

static void m_cm_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_key(c, &CMK);
}

static void m_cm_deposit(zxvd_call_t *c)
{
    uint32_t a;
    uint64_t amt;
    uint8_t id[CM_ID_BYTES];
    if (!cm_ready(c) || !zxvd_sandbox_owner(c, "account", &a) ||
        !zxvd_sandbox_need_u64(c, "amount_minor", &amt))
        return;
    cm_id(a, id);
    cm_status_t st = cm_deposit(&CMS, id, amt);
    if (st != CM_OK) return cm_refuse(c, st);
    put_cm_account(zxvd_result(c), a);
}

static void m_cm_withdraw(zxvd_call_t *c)
{
    uint32_t a;
    uint64_t amt;
    uint8_t id[CM_ID_BYTES];
    if (!cm_ready(c) || !zxvd_sandbox_owner(c, "account", &a) ||
        !zxvd_sandbox_need_u64(c, "amount_minor", &amt))
        return;
    cm_id(a, id);
    cm_status_t st = cm_withdraw(&CMS, id, amt);
    if (st != CM_OK) return cm_refuse(c, st);
    put_cm_account(zxvd_result(c), a);
}

static void m_cm_account(zxvd_call_t *c)
{
    uint32_t a;
    if (cm_ready(c) && zxvd_sandbox_owner(c, "account", &a)) put_cm_account(zxvd_result(c), a);
}

static void cm_order_m(zxvd_call_t *c, bool ask)
{
    uint32_t a, oid = 0;
    cm_key_t k;
    uint64_t qty, price, exp;
    uint8_t id[CM_ID_BYTES];
    if (!cm_ready(c) || !zxvd_sandbox_owner(c, "account", &a) || !cm_key(c, &k) ||
        !zxvd_sandbox_need_u64(c, "qty", &qty) ||
        !zxvd_sandbox_need_u64(c, ask ? "price" : "limit", &price) ||
        !cm_secs(c, "expires", true, &exp))
        return;
    cm_id(a, id);
    cm_status_t st = ask ? cm_ask(&CMS, id, k, qty, price, exp, &oid)
                         : cm_bid(&CMS, id, k, qty, price, exp, &oid);
    if (st != CM_OK) return cm_refuse(c, st);
    const cm_order_t *r = cm_order(&CMS, oid);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"order\":%u,\"side\":\"%s\"", (unsigned) oid, ask ? "ask" : "bid");
    zxvd_sandbox_kv(o, ",\"qty\":", r->qty);
    zxvd_sandbox_kv(o, ",\"price\":", r->price);
    zxvd_sandbox_kv(o, ",\"locked\":", r->locked);
    ZXVD_RAW(o, "}");
}

static void m_cm_ask(zxvd_call_t *c)
{
    cm_order_m(c, true);
}
static void m_cm_bid(zxvd_call_t *c)
{
    cm_order_m(c, false);
}

static void m_cm_cancel(zxvd_call_t *c)
{
    uint32_t a, oid;
    uint8_t id[CM_ID_BYTES];
    if (!cm_ready(c) || !zxvd_sandbox_owner(c, "account", &a) ||
        !zxvd_sandbox_need_u32(c, "order", &oid))
        return;
    cm_id(a, id);
    cm_status_t st = cm_cancel(&CMS, id, oid);
    if (st != CM_OK) return cm_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"order\":%u,\"cancelled\":true}", (unsigned) oid);
}

static void m_cm_quote(zxvd_call_t *c)
{
    cm_key_t k;
    uint64_t now;
    cm_quote_t q;
    if (!cm_ready(c) || !cm_key(c, &k) || !cm_now(c, &now)) return;
    cm_quote(&CMS, k, now, &q);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_sandbox_kv(o, "{\"last_price\":", q.last_price);
    zxvd_sandbox_kv(o, ",\"last_volume\":", q.last_volume);
    zxvd_sandbox_kv(o, ",\"best_bid\":", q.best_bid);
    zxvd_sandbox_kv(o, ",\"best_ask\":", q.best_ask);
    zxvd_sandbox_kv(o, ",\"bid_units\":", q.bid_units);
    zxvd_sandbox_kv(o, ",\"ask_units\":", q.ask_units);
    zxvd_buf_printf(o, ",\"providers\":%u,\"concentrated\":%s}", (unsigned) q.providers,
                    q.concentrated ? "true" : "false");
}

static void m_cm_clear(zxvd_call_t *c)
{
    static cm_result_t r;
    cm_key_t k;
    uint64_t now;
    if (!cm_ready(c) || !cm_key(c, &k) || !cm_now(c, &now)) return;
    uint32_t before = CMS.next_contract;
    cm_status_t st = cm_clear(&CMS, k, now, &r);
    if (st != CM_OK) return cm_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_sandbox_kv(o, "{\"round\":", r.round);
    zxvd_sandbox_kv(o, ",\"price\":", r.price);
    zxvd_sandbox_kv(o, ",\"volume\":", r.volume);
    zxvd_sandbox_kv(o, ",\"lo\":", r.lo);
    zxvd_sandbox_kv(o, ",\"hi\":", r.hi);
    zxvd_buf_printf(o, ",\"providers\":%u", (unsigned) r.providers);
    zxvd_sandbox_kv(o, ",\"cap\":", r.cap);
    zxvd_sandbox_kv(o, ",\"overflow\":", r.overflow);
    zxvd_sandbox_kv(o, ",\"unmet_demand\":", r.unmet_demand);
    put_hex32(o, ",\"digest\":", r.digest);
    ZXVD_RAW(o, ",\"fills\":[");
    for (uint32_t i = 0; i < r.nfills && i < CM_MAX_FILLS; i++) {
        zxvd_buf_printf(o, "%s{\"bid\":%u,\"ask\":%u", i ? "," : "", (unsigned) r.fills[i].bid_id,
                        (unsigned) r.fills[i].ask_id);
        zxvd_sandbox_kv(o, ",\"qty\":", r.fills[i].qty);
        ZXVD_RAW(o, "}");
    }
    ZXVD_RAW(o, "],\"contracts\":[");
    bool first = true;
    for (uint32_t i = 0; i < CM_MAX_CONTRACTS; i++) {
        const cm_contract_t *x = &CMS.con[i];
        if (!x->used || x->id < before) continue;
        if (!first) ZXVD_RAW(o, ",");
        put_contract(o, x);
        first = false;
    }
    ZXVD_RAW(o, "]}");
}

static bool cm_contract_of(zxvd_call_t *c, const cm_contract_t **k)
{
    uint32_t id;
    if (!zxvd_sandbox_need_u32(c, "contract", &id)) return false;
    *k = cm_contract(&CMS, id);
    if (*k) return true;
    cm_refuse(c, CM_ERR_UNKNOWN);
    return false;
}

static void m_cm_message(zxvd_call_t *c)
{
    char kind[16];
    const cm_contract_t *k;
    uint64_t units, at;
    uint8_t m[CM_PROOF_MSG];
    if (!cm_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (strcmp(kind, "proof")) return zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"proof\"");
    if (!cm_contract_of(c, &k) || !zxvd_sandbox_need_u64(c, "units", &units) ||
        !cm_secs(c, "at", true, &at))
        return;
    cm_proof_msg(k->id, k->proof_seq + 1u, units, at, k->proof_tip, m);
    zxvd_sandbox_put_msg(c, m, sizeof m);
}

static void m_cm_deliver(zxvd_call_t *c)
{
    const cm_contract_t *k;
    cm_proof_t p;
    memset(&p, 0, sizeof p);
    if (!cm_ready(c) || !cm_contract_of(c, &k) || !zxvd_sandbox_need_u64(c, "units", &p.units) ||
        !cm_secs(c, "at", true, &p.at_ms) || !zxvd_sandbox_sig(c, "sig", p.evidence))
        return;
    p.contract_id = k->id;
    p.seq = k->proof_seq + 1u;
    memcpy(p.prev, k->proof_tip, 32);
    cm_status_t st = cm_deliver(&CMS, &p);
    if (st != CM_OK) return cm_refuse(c, st);
    put_contract(zxvd_result(c), k);
}

static void m_cm_expire(zxvd_call_t *c)
{
    uint64_t now;
    if (!cm_ready(c) || !cm_now(c, &now)) return;
    zxvd_buf_printf(zxvd_result(c), "{\"closed\":%u}", (unsigned) cm_expire(&CMS, now));
}

static void m_cm_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u", CM_OPEN ? "true" : "false", (unsigned) CMK.n);
    if (CM_OPEN) {
        zxvd_sandbox_kv(o, ",\"deposits\":", CMS.deposits);
        zxvd_sandbox_kv(o, ",\"commons\":", CMS.commons);
        ZXVD_RAW(o, ",\"fee_buckets\":[");
        for (uint32_t i = 0; i < PAY_ASSURE_BUCKETS; i++) {
            if (i) ZXVD_RAW(o, ",");
            zxvd_sandbox_put_u64(o, CMS.fee_bucket[i]);
        }
        zxvd_sandbox_kv(o, "],\"round\":", CMS.round);
        ZXVD_RAW(o, ",\"contracts\":[");
        bool first = true;
        for (uint32_t i = 0; i < CM_MAX_CONTRACTS; i++) {
            if (!CMS.con[i].used) continue;
            if (!first) ZXVD_RAW(o, ",");
            put_contract(o, &CMS.con[i]);
            first = false;
        }
        ZXVD_RAW(o, "]");
        put_check(o, cm_conserved(&CMS));
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void cm_close_group(void)
{
    CM_OPEN = false;
    CMK.n = 0;
    memset(&CMS, 0, sizeof CMS);
}

static const zxvd_group_method_t CM_M[] = {
    {"capmkt.setup", m_cm_setup, false},     {"capmkt.key", m_cm_key, false},
    {"capmkt.deposit", m_cm_deposit, false}, {"capmkt.withdraw", m_cm_withdraw, false},
    {"capmkt.account", m_cm_account, true},  {"capmkt.ask", m_cm_ask, false},
    {"capmkt.bid", m_cm_bid, false},         {"capmkt.cancel", m_cm_cancel, false},
    {"capmkt.quote", m_cm_quote, true},      {"capmkt.clear", m_cm_clear, false},
    {"capmkt.message", m_cm_message, true},  {"capmkt.deliver", m_cm_deliver, false},
    {"capmkt.expire", m_cm_expire, false},   {"capmkt.status", m_cm_status, true},
};

const zxvd_group_t *zxvd_capmkt_group(void)
{
    static const zxvd_group_t G = {
        "capmkt",
        true,
        "the capacity market (capmkt.h): uniform-price double auction, escrow, pay on delivery; "
        "an exchange may be regulated; practice deposits in memory",
        CM_M,
        sizeof CM_M / sizeof CM_M[0],
        cm_close_group};
    return &G;
}

/* ===================== market.* ===================== */

#define MK_VFV 1u /* mk_init registers VFV as currency handle 1 */

static zxvd_sandbox_book_t KB = {
    .a = {{.name = "vino", .code = "VINO", .minor = 2, .fundable = true}}, .n_assets = 1};
static zxvd_sandbox_keys_t KK;
static mk_market_t KS;
static mk_pay_t KP;

static void mk_refuse(zxvd_call_t *c, int st)
{
    static const char *const N[][2] = {
        {"MK_OK", ""},
        {"MK_ERR_ARG", "a bad argument"},
        {"MK_ERR_FULL", "the table is full"},
        {"MK_ERR_NOT_FOUND", "no such store, listing or order"},
        {"MK_ERR_STATE", "not allowed in the order's state"},
        {"MK_ERR_AUTH", "the wrong party, a signature that does not verify, or a delivered CID "
                        "that is not the listed one"},
        {"MK_ERR_STOCK", "not enough stock"},
        {"MK_ERR_CCY", "an unknown or mismatched currency"},
        {"MK_ERR_OVERFLOW", "a value does not fit"},
        {"MK_ERR_SETTLE", "the ledger refused the settlement; nothing changed"},
        {"MK_ERR_HOOK", "a required hook is missing"},
        {"MK_ERR_CONFIRM", "the order lacks the user's confirmation"},
        {"MK_ERR_MANDATE", "outside the agent's mandate"},
        {"MK_ERR_WINDOW", "outside the time window"},
        {"MK_ERR_DUPLICATE", "a version not above the last (replay or rollback)"},
        {"MK_ERR_POLICY", "refused by policy"},
    };
    refuse_tab(c, N, sizeof N / sizeof N[0], st, 4);
}

static bool mk_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &KB, "market");
}

/* Y2: a market id is the key id, little-endian, then zeros */
static void mk_idk(uint32_t k, mk_id_t *id)
{
    memset(id, 0, sizeof *id);
    for (int i = 0; i < 4; i++) id->b[i] = (uint8_t) (k >> (8 * i));
}

static bool mk_keyof(const mk_id_t *id, uint32_t *k)
{
    for (uint32_t i = 4; i < 32; i++)
        if (id->b[i]) return false;
    *k = (uint32_t) id->b[0] | (uint32_t) id->b[1] << 8 | (uint32_t) id->b[2] << 16 |
         (uint32_t) id->b[3] << 24;
    return true;
}

static bool mk_verify(void *ctx, const mk_id_t *signer, const uint8_t *m, uint32_t len,
                      const uint8_t *s, uint32_t sl)
{
    (void) ctx;
    uint32_t k;
    return mk_keyof(signer, &k) && zxvd_sandbox_verify(&KK, k, m, len, s, sl);
}

static bool mk_settle(void *ctx, const mk_settle_t *s)
{
    (void) ctx;
    KP.tick = KS.now;
    return mk_pay_settle(&KP, s);
}

/* P3: a party's account is its first Vino account in the book */
static uint32_t mk_account_of(void *ctx, const mk_id_t *id, uint8_t ccy)
{
    (void) ctx;
    uint32_t k;
    if (ccy != MK_VFV || !mk_keyof(id, &k)) return MK_PAY_NONE;
    for (uint32_t i = 0; i < KB.L.n_accounts; i++) {
        const pay_account_t *x = pay_ledger_account(&KB.L, i);
        if (x && x->owner == k && x->asset == KB.a[0].asset && !(x->flags & PAY_ACCT_ISSUER))
            return i;
    }
    return MK_PAY_NONE;
}

static bool mk_party(zxvd_call_t *c, const char *key, mk_id_t *id)
{
    uint32_t k;
    if (!zxvd_sandbox_owner(c, key, &k)) return false;
    if (!k) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must not be 0", key);
        return false;
    }
    mk_idk(k, id);
    return true;
}

static bool mk_text(zxvd_call_t *c, const char *key, char *out, uint32_t cap, bool required)
{
    memset(out, 0, cap);
    return zxvd_p_str(c, key, out, cap, required);
}

static bool store_spec(zxvd_call_t *c, mk_store_spec_t *s)
{
    memset(s, 0, sizeof *s);
    if (!mk_party(c, "owner", &s->owner) || !mk_text(c, "name", s->name, sizeof s->name, true) ||
        !mk_text(c, "region", s->region, sizeof s->region, true) ||
        !zxvd_sandbox_need_u32(c, "version", &s->version) ||
        !zxvd_sandbox_u32(c, "fulfil_within", false, 0, &s->fulfil_within))
        return false;
    if (zxvd_p_has(c, "arbiter")) {
        if (!mk_party(c, "arbiter", &s->arbiter)) return false;
        s->has_arbiter = true;
    }
    return true;
}

static const char *const MK_KIND[] = {"physical", "digital", "service"};

static bool listing_spec(zxvd_call_t *c, mk_listing_spec_t *l)
{
    uint32_t store, kind, cat;
    memset(l, 0, sizeof *l);
    if (!zxvd_sandbox_need_u32(c, "store", &store) ||
        !pick(c, "type", MK_KIND, 3, "\"physical\", \"digital\" or \"service\"", &kind) ||
        !mk_text(c, "title", l->title, sizeof l->title, true) ||
        !zxvd_sandbox_u32(c, "category", false, 0, &cat) ||
        !zxvd_sandbox_need_u64(c, "price_minor", &l->price) ||
        !zxvd_sandbox_u64(c, "shipping_minor", false, 0, &l->shipping) ||
        !zxvd_sandbox_need_u32(c, "stock", &l->stock) ||
        !mk_text(c, "cid", l->cid, sizeof l->cid, false) ||
        !zxvd_sandbox_u32(c, "return_window", false, 0, &l->return_window) ||
        !zxvd_sandbox_u32(c, "auto_release", false, 0, &l->auto_release) ||
        !zxvd_sandbox_need_u32(c, "version", &l->version))
        return false;
    if (store > 0xFFFFu || cat > 63u) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"store\" or \"category\" is out of range");
        return false;
    }
    l->store = (uint16_t) store;
    l->kind = (mk_kind_t) kind;
    l->category = (uint16_t) cat;
    l->ccy = MK_VFV;
    return true;
}

static bool mk_handle(zxvd_call_t *c, const char *key, bool required, uint16_t *h)
{
    uint32_t v;
    if (!zxvd_sandbox_u32(c, key, required, 0, &v)) return false;
    if (v > 0xFFFFu) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" is out of range", key);
        return false;
    }
    *h = (uint16_t) v;
    return true;
}

static void put_order(zxvd_buf_t *o, uint16_t h)
{
    const mk_order_t *r = mk_order(&KS, h);
    uint32_t b = 0, s = 0;
    mk_keyof(&r->buyer, &b);
    mk_keyof(&r->seller, &s);
    zxvd_buf_printf(o, "{\"order\":%u,\"state\":\"%s\",\"store\":%u,\"buyer\":%u,\"seller\":%u",
                    (unsigned) h, mk_order_state_name(r->state), (unsigned) r->store, (unsigned) b,
                    (unsigned) s);
    zxvd_sandbox_kv(o, ",\"total_minor\":", r->total);
    zxvd_sandbox_kv(o, ",\"tax_minor\":", r->tax);
    zxvd_sandbox_kv(o, ",\"held_minor\":", r->held_base + r->held_tax);
    zxvd_sandbox_kv(o, ",\"to_seller_minor\":", r->to_seller);
    zxvd_sandbox_kv(o, ",\"to_commons_minor\":", r->to_commons);
    zxvd_sandbox_kv(o, ",\"to_buyer_minor\":", r->to_buyer);
    zxvd_sandbox_kv(o, ",\"fee_minor\":", r->fee_charged);
    zxvd_sandbox_kv(o, ",\"release_at\":", r->release_at);
    zxvd_buf_printf(o, ",\"lines\":%u,\"conserved\":%s}", (unsigned) r->n_lines,
                    mk_order_conserved(&KS, h) ? "true" : "false");
}

static void m_mk_setup(zxvd_call_t *c)
{
    if (KB.open) return zxvd_sandbox_refuse(c, "SET_UP", "market is set up already (this run)");
    uint64_t now;
    if (!zxvd_sandbox_now(c, &now)) return;
    uint32_t e, k, t;
    if (zxvd_sandbox_book_open(&KB) != 0 || !open_acct(&KB, KB.a[0].asset, 0, &e) ||
        !open_acct(&KB, KB.a[0].asset, 0, &k) || !open_acct(&KB, KB.a[0].asset, 0, &t)) {
        KB.open = false;
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    }
    mk_pay_init(&KP, &KB.L, mk_account_of, NULL,
                (const uint8_t *) "zxvd market sandbox seed, v1....", ZXVD_SANDBOX_MODULE);
    mk_pay_bind(&KP, MK_VFV, e, k, t);
    mk_hooks_t h;
    memset(&h, 0, sizeof h);
    h.verify = mk_verify;
    h.settle = mk_settle;
    mk_init(&KS, &h, NULL);
    mk_tick(&KS, now);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o,
                    "{\"currency\":\"VFV\",\"asset\":\"vino\",\"escrow\":%u,\"commons\":%u,"
                    "\"tax\":%u",
                    (unsigned) e, (unsigned) k, (unsigned) t);
    zxvd_sandbox_kv(o, ",\"now\":", KS.now);
    ZXVD_RAW(o, "}");
}

static void m_mk_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_key(c, &KK);
}
static void m_mk_account_open(zxvd_call_t *c)
{
    if (mk_ready(c)) zxvd_sandbox_m_account_open(c, &KB);
}
static void m_mk_fund(zxvd_call_t *c)
{
    if (mk_ready(c)) zxvd_sandbox_m_fund(c, &KB);
}
static void m_mk_account(zxvd_call_t *c)
{
    if (mk_ready(c)) zxvd_sandbox_m_account(c, &KB);
}

static void m_mk_message(zxvd_call_t *c)
{
    char kind[16];
    uint8_t d[32];
    if (!mk_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (!strcmp(kind, "store")) {
        mk_store_spec_t s;
        if (!store_spec(c, &s)) return;
        mk_store_digest(&s, d);
    } else if (!strcmp(kind, "listing")) {
        mk_listing_spec_t l;
        if (!listing_spec(c, &l)) return;
        mk_listing_digest(&KS, &l, d);
    } else if (!strcmp(kind, "ruling")) {
        uint16_t h;
        uint64_t award;
        if (!mk_handle(c, "order", true, &h) || !zxvd_sandbox_need_u64(c, "buyer_award", &award))
            return;
        if (!mk_order(&KS, h)) return mk_refuse(c, MK_ERR_NOT_FOUND);
        mk_ruling_digest(&KS, h, award, d);
    } else {
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"store\", \"listing\" or \"ruling\"");
    }
    zxvd_sandbox_put_msg(c, d, 32);
}

static void m_mk_store(zxvd_call_t *c)
{
    mk_store_spec_t s;
    uint8_t sig[64];
    uint16_t h;
    if (!mk_ready(c) || !store_spec(c, &s) || !mk_handle(c, "handle", false, &h) ||
        !zxvd_sandbox_sig(c, "sig", sig))
        return;
    mk_status_t st = mk_store_publish(&KS, &s, sig, 64, &h);
    if (st != MK_OK) return mk_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"store\":%u,\"version\":%u}", (unsigned) h,
                    (unsigned) s.version);
}

static void m_mk_listing(zxvd_call_t *c)
{
    mk_listing_spec_t l;
    uint8_t sig[64];
    uint16_t h;
    if (!mk_ready(c) || !listing_spec(c, &l) || !mk_handle(c, "handle", false, &h) ||
        !zxvd_sandbox_sig(c, "sig", sig))
        return;
    mk_status_t st = mk_listing_publish(&KS, &l, sig, 64, &h);
    if (st != MK_OK) return mk_refuse(c, st);
    /* digital goods are not stocked: the engine keeps MK_UNLIMITED */
    uint32_t av = mk_available(&KS, h);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"listing\":%u,\"version\":%u,", (unsigned) h, (unsigned) l.version);
    if (av == MK_UNLIMITED)
        ZXVD_RAW(o, "\"available\":\"unlimited\"}");
    else
        zxvd_buf_printf(o, "\"available\":%u}", (unsigned) av);
}

/* one listing in one cart, checked out at once; the cart is freed whatever
 * happens, so a refused order leaves nothing behind */
static void m_mk_order(zxvd_call_t *c)
{
    mk_id_t b;
    uint16_t lst;
    uint32_t qty;
    mk_checkout_opts_t opt;
    memset(&opt, 0, sizeof opt);
    if (!mk_ready(c) || !mk_party(c, "buyer", &b) || !mk_handle(c, "listing", true, &lst) ||
        !zxvd_sandbox_need_u32(c, "qty", &qty) ||
        !mk_text(c, "region", opt.region, sizeof opt.region, true) ||
        !zxvd_p_bool(c, "community", false, &opt.community_mediation))
        return;
    int32_t cart = mk_cart_open(&KS, &b);
    if (cart < 0) return mk_refuse(c, cart);
    uint16_t out[1] = {0};
    int32_t n = mk_cart_add(&KS, (uint16_t) cart, lst, qty, 0);
    if (n == MK_OK) n = mk_checkout(&KS, (uint16_t) cart, &opt, out, 1);
    mk_cart_clear(&KS, (uint16_t) cart);
    KS.cart[cart - 1].used = false; /* as the engine's own test frees a cart */
    if (n < 0) return mk_refuse(c, n);
    put_order(zxvd_result(c), out[0]);
}

typedef mk_status_t (*mk_act_fn)(mk_market_t *, uint16_t, const mk_id_t *);

static void mk_act(zxvd_call_t *c, const char *who, mk_act_fn fn)
{
    uint16_t h;
    mk_id_t a;
    if (!mk_ready(c) || !mk_handle(c, "order", true, &h) || !mk_party(c, who, &a)) return;
    mk_status_t st = fn(&KS, h, &a);
    if (st != MK_OK) return mk_refuse(c, st);
    put_order(zxvd_result(c), h);
}

static void m_mk_pay(zxvd_call_t *c)
{
    mk_act(c, "buyer", mk_order_pay);
}
static void m_mk_confirm(zxvd_call_t *c)
{
    mk_act(c, "buyer", mk_order_confirm);
}
static void m_mk_cancel(zxvd_call_t *c)
{
    mk_act(c, "actor", mk_order_cancel);
}
static void m_mk_dispute(zxvd_call_t *c)
{
    mk_act(c, "actor", mk_dispute_open);
}

static void m_mk_fulfil(zxvd_call_t *c)
{
    uint16_t h;
    mk_id_t s;
    char proof[MK_CID_MAX];
    if (!mk_ready(c) || !mk_handle(c, "order", true, &h) || !mk_party(c, "seller", &s) ||
        !mk_text(c, "proof", proof, sizeof proof, true))
        return;
    mk_status_t st = mk_order_fulfil(&KS, h, &s, (const uint8_t *) proof, (uint32_t) strlen(proof));
    if (st != MK_OK) return mk_refuse(c, st);
    put_order(zxvd_result(c), h);
}

static void m_mk_refund(zxvd_call_t *c)
{
    uint16_t h;
    mk_id_t s;
    uint32_t line, qty;
    if (!mk_ready(c) || !mk_handle(c, "order", true, &h) || !mk_party(c, "seller", &s) ||
        !zxvd_sandbox_u32(c, "line", false, 0, &line) || !zxvd_sandbox_need_u32(c, "qty", &qty))
        return;
    if (line >= MK_ORDER_LINES) return zxvd_fail(c, ZXVD_E_PARAMS, "\"line\" is out of range");
    mk_status_t st = mk_order_refund_line(&KS, h, &s, (uint8_t) line, qty);
    if (st != MK_OK) return mk_refuse(c, st);
    put_order(zxvd_result(c), h);
}

static void m_mk_rule(zxvd_call_t *c)
{
    uint16_t h;
    mk_id_t med;
    uint64_t award;
    uint8_t sig[64];
    if (!mk_ready(c) || !mk_handle(c, "order", true, &h) || !mk_party(c, "mediator", &med) ||
        !zxvd_sandbox_need_u64(c, "buyer_award", &award) || !zxvd_sandbox_sig(c, "sig", sig))
        return;
    mk_status_t st = mk_dispute_rule(&KS, h, &med, award, sig, 64);
    if (st != MK_OK) return mk_refuse(c, st);
    put_order(zxvd_result(c), h);
}

static void m_mk_tick(zxvd_call_t *c)
{
    uint64_t now;
    if (!mk_ready(c) || !zxvd_sandbox_now(c, &now)) return;
    if (now < KS.now) return zxvd_fail(c, ZXVD_E_PARAMS, "\"now\" must not go back");
    mk_tick(&KS, now);
    zxvd_sandbox_kv(zxvd_result(c), "{\"now\":", KS.now);
    ZXVD_RAW(zxvd_result(c), "}");
}

static void m_mk_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u", KB.open ? "true" : "false", (unsigned) KK.n);
    if (KB.open) {
        uint32_t ns = 0, nl = 0;
        for (uint32_t i = 0; i < MK_MAX_STORES; i++) ns += KS.store[i].used;
        for (uint32_t i = 0; i < MK_MAX_LISTINGS; i++) nl += KS.listing[i].used;
        zxvd_sandbox_kv(o, ",\"now\":", KS.now);
        zxvd_buf_printf(o, ",\"stores\":%u,\"listings\":%u", (unsigned) ns, (unsigned) nl);
        zxvd_sandbox_kv(o, ",\"escrow_minor\":", bal(&KB, KP.pool[MK_VFV].escrow));
        zxvd_sandbox_kv(o, ",\"commons_minor\":", bal(&KB, KP.pool[MK_VFV].commons));
        zxvd_sandbox_kv(o, ",\"postings\":", KP.posted);
        zxvd_sandbox_kv(o, ",\"refused\":", KP.refused);
        ZXVD_RAW(o, ",\"orders\":[");
        bool first = true;
        for (uint32_t i = 0; i < MK_MAX_ORDERS; i++) {
            if (!KS.order[i].used) continue;
            if (!first) ZXVD_RAW(o, ",");
            put_order(o, (uint16_t) (i + 1));
            first = false;
        }
        ZXVD_RAW(o, "]");
        put_check(o, mk_market_conserved(&KS) && pay_ledger_check(&KB.L));
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void mk_close_group(void)
{
    KB.open = false;
    KK.n = 0;
    memset(&KS, 0, sizeof KS);
    memset(&KP, 0, sizeof KP);
}

static const zxvd_group_method_t MK_M[] = {
    {"market.setup", m_mk_setup, false},
    {"market.key", m_mk_key, false},
    {"market.account_open", m_mk_account_open, false},
    {"market.fund", m_mk_fund, false},
    {"market.account", m_mk_account, true},
    {"market.message", m_mk_message, true},
    {"market.store", m_mk_store, false},
    {"market.listing", m_mk_listing, false},
    {"market.order", m_mk_order, false},
    {"market.pay", m_mk_pay, false},
    {"market.fulfil", m_mk_fulfil, false},
    {"market.confirm", m_mk_confirm, false},
    {"market.refund", m_mk_refund, false},
    {"market.cancel", m_mk_cancel, false},
    {"market.dispute", m_mk_dispute, false},
    {"market.rule", m_mk_rule, false},
    {"market.tick", m_mk_tick, false},
    {"market.status", m_mk_status, true},
};

const zxvd_group_t *zxvd_market_group(void)
{
    static const zxvd_group_t G = {
        "market",
        true,
        "peer-to-peer commerce with escrow (market.h): marketplace and consumer law apply; "
        "sandbox ledger in memory",
        MK_M,
        sizeof MK_M / sizeof MK_M[0],
        mk_close_group};
    return &G;
}

/* ===================== royalty.* ===================== */

static zxvd_sandbox_book_t RB = {
    .a = {{.name = "vino", .code = "VINO", .minor = 2, .fundable = true}}, .n_assets = 1};
static zxvd_sandbox_keys_t RK;
static roy_t RS;

static void roy_refuse(zxvd_call_t *c, roy_status_t st)
{
    static const char *const N[][2] = {
        {"ROY_OK", ""},
        {"ROY_ERR_ARG", "a bad argument (bad terms, owners or accounts, or not one signature "
                        "per current owner)"},
        {"ROY_ERR_FULL", "the table is full"},
        {"ROY_ERR_SIG", "a signature does not verify"},
        {"ROY_ERR_DUP", "the work is registered already, or the receipt is used (replay)"},
        {"ROY_ERR_UNKNOWN", "no such work, parent or owner"},
        {"ROY_ERR_VERSION", "the version or sequence is not above the last (replay)"},
        {"ROY_ERR_DEPTH", "the derivative chain is too deep"},
        {"ROY_ERR_LINES", "a payment would need more than 8 ledger lines"},
        {"ROY_ERR_UNREALISED", "no realised sale to the payer for this receipt"},
        {"ROY_ERR_STALE_QUOTE", "the quote changed: ask royalty.quote again"},
        {"ROY_ERR_OVERFLOW", "a value does not fit"},
        {"ROY_ERR_LEDGER", "the ledger refused the posting"},
        {"ROY_ERR_PARENT", "a later version may not change the parent or the upstream share"},
    };
    refuse_tab(c, N, sizeof N / sizeof N[0], st, 1);
}

static bool roy_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &RB, "royalty");
}

static bool roy_manifest(zxvd_call_t *c, roy_manifest_t *m)
{
    static const char *const TERMS[] = {"percent", "flat", "zxv_hw"};
    uint32_t t;
    memset(m, 0, sizeof *m);
    if (!zxvd_sandbox_key32(c, "work", m->id) || !zxvd_sandbox_need_u64(c, "version", &m->version))
        return false;
    int32_t n = zxvd_p_len(c, "owners");
    if (n < -1) return false;
    if (n < 1 || n > (int32_t) ROY_MAX_OWNERS) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"owners\" must hold 1..%u objects",
                  (unsigned) ROY_MAX_OWNERS);
        return false;
    }
    for (int32_t i = 0; i < n; i++) {
        int32_t sv;
        roy_owner_t *w = &m->owner[i];
        if (!zxvd_p_enter(c, "owners", (uint32_t) i, &sv)) return false;
        bool ok = zxvd_sandbox_need_u32(c, "id", &w->id) &&
                  zxvd_sandbox_need_u32(c, "account", &w->account) &&
                  zxvd_sandbox_need_u64(c, "weight", &w->weight);
        zxvd_p_leave(c, sv);
        if (!ok) return false;
    }
    m->n_owners = (uint32_t) n;
    if (zxvd_p_has(c, "parent")) {
        if (!zxvd_sandbox_key32(c, "parent", m->parent) ||
            !zxvd_sandbox_need_u64(c, "up_num", &m->up_num) ||
            !zxvd_sandbox_need_u64(c, "up_den", &m->up_den))
            return false;
        m->has_parent = true;
    }
    if (!pick(c, "terms", TERMS, 3, "\"percent\", \"flat\" or \"zxv_hw\"", &t)) return false;
    if (t == 2) {
        roy_terms_zxv_hw(&m->terms);
        return true;
    }
    m->terms.kind = t == 0 ? ROY_TERMS_PERCENT : ROY_TERMS_FLAT;
    if (t == 0)
        return zxvd_sandbox_need_u64(c, "num", &m->terms.num) &&
               zxvd_sandbox_need_u64(c, "den", &m->terms.den);
    return zxvd_sandbox_need_u64(c, "flat_minor", &m->terms.flat);
}

static bool roy_split(zxvd_call_t *c, uint8_t work[32], uint32_t *owner, roy_split_t *s)
{
    uint32_t gb, ib;
    memset(s, 0, sizeof *s);
    if (!zxvd_sandbox_key32(c, "work", work) || !zxvd_sandbox_need_u32(c, "owner", owner) ||
        !zxvd_sandbox_u32(c, "giving_account", false, 0, &s->giving_acct) ||
        !zxvd_sandbox_u32(c, "giving_bps", false, 0, &gb) ||
        !zxvd_sandbox_u32(c, "invest_account", false, 0, &s->invest_acct) ||
        !zxvd_sandbox_u32(c, "invest_bps", false, 0, &ib) ||
        !zxvd_sandbox_need_u64(c, "seq", &s->seq))
        return false;
    if (gb > ROY_BPS || ib > ROY_BPS) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"giving_bps\" and \"invest_bps\" must be at most 10000");
        return false;
    }
    s->giving_bps = (uint16_t) gb;
    s->invest_bps = (uint16_t) ib;
    return true;
}

static void put_work(zxvd_buf_t *o, const roy_work_t *w)
{
    put_hex32(o, "{\"work\":", w->m.id);
    zxvd_sandbox_kv(o, ",\"version\":", w->m.version);
    zxvd_buf_printf(o, ",\"depth\":%u,\"owners\":[", (unsigned) w->depth);
    for (uint32_t i = 0; i < w->m.n_owners; i++) {
        zxvd_buf_printf(o, "%s{\"id\":%u,\"account\":%u", i ? "," : "", (unsigned) w->m.owner[i].id,
                        (unsigned) w->m.owner[i].account);
        zxvd_sandbox_kv(o, ",\"weight\":", w->m.owner[i].weight);
        zxvd_buf_printf(o, ",\"giving_bps\":%u,\"invest_bps\":%u}",
                        (unsigned) w->split[i].giving_bps, (unsigned) w->split[i].invest_bps);
    }
    ZXVD_RAW(o, "]");
    if (w->m.has_parent) put_hex32(o, ",\"parent\":", w->m.parent);
    zxvd_sandbox_kv(o, ",\"payments\":", w->tot.payments);
    zxvd_sandbox_kv(o, ",\"gross_minor\":", w->tot.gross);
    zxvd_sandbox_kv(o, ",\"royalty_minor\":", w->tot.royalty);
    zxvd_sandbox_kv(o, ",\"fee_minor\":", w->tot.fee);
    zxvd_sandbox_kv(o, ",\"up_in_minor\":", w->tot.up_in);
    zxvd_sandbox_kv(o, ",\"up_out_minor\":", w->tot.up_out);
    zxvd_sandbox_kv(o, ",\"owners_minor\":", w->tot.owners);
    zxvd_sandbox_kv(o, ",\"giving_minor\":", w->tot.giving);
    zxvd_sandbox_kv(o, ",\"investing_minor\":", w->tot.investing);
    ZXVD_RAW(o, "}");
}

static void put_quote(zxvd_buf_t *o, const roy_quote_t *q)
{
    zxvd_sandbox_kv(o, "{\"gross_minor\":", q->gross);
    zxvd_sandbox_kv(o, ",\"royalty_minor\":", q->royalty);
    zxvd_sandbox_kv(o, ",\"fee_minor\":", q->fee);
    ZXVD_RAW(o, ",\"levels\":[");
    for (uint32_t i = 0; i < q->n_levels; i++) {
        put_hex32(o, i ? ",{\"work\":" : "{\"work\":", q->level[i].id);
        zxvd_sandbox_kv(o, ",\"received_minor\":", q->level[i].received);
        zxvd_sandbox_kv(o, ",\"upstream_minor\":", q->level[i].upstream);
        zxvd_sandbox_kv(o, ",\"kept_minor\":", q->level[i].kept);
        ZXVD_RAW(o, "}");
    }
    ZXVD_RAW(o, "],\"shares\":[");
    for (uint32_t i = 0; i < q->n_payouts; i++) {
        const roy_payout_t *p = &q->payout[i];
        zxvd_buf_printf(o, "%s{\"level\":%u,\"owner\":%u,\"account\":%u", i ? "," : "",
                        (unsigned) p->level, (unsigned) p->owner_id, (unsigned) p->account);
        zxvd_sandbox_kv(o, ",\"gross_minor\":", p->gross);
        zxvd_sandbox_kv(o, ",\"giving_minor\":", p->giving);
        zxvd_sandbox_kv(o, ",\"investing_minor\":", p->investing);
        zxvd_sandbox_kv(o, ",\"net_minor\":", p->net);
        ZXVD_RAW(o, "}");
    }
    put_hex32(o, "],\"digest\":", q->digest);
    ZXVD_RAW(o, "}");
}

static void m_roy_setup(zxvd_call_t *c)
{
    if (RB.open) return zxvd_sandbox_refuse(c, "SET_UP", "royalty is set up already (this run)");
    uint32_t fee;
    if (zxvd_sandbox_book_open(&RB) != 0 || !open_acct(&RB, RB.a[0].asset, 0, &fee)) {
        RB.open = false;
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    }
    roy_status_t st = roy_init(&RS, &RB.L, fee, key_verify, &RK);
    if (st != ROY_OK) {
        RB.open = false;
        return roy_refuse(c, st);
    }
    zxvd_buf_printf(zxvd_result(c), "{\"fee_account\":%u,\"asset\":\"vino\",\"advances\":false}",
                    (unsigned) fee);
}

static void m_roy_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_key(c, &RK);
}
static void m_roy_account_open(zxvd_call_t *c)
{
    if (roy_ready(c)) zxvd_sandbox_m_account_open(c, &RB);
}
static void m_roy_fund(zxvd_call_t *c)
{
    if (roy_ready(c)) zxvd_sandbox_m_fund(c, &RB);
}
static void m_roy_account(zxvd_call_t *c)
{
    if (roy_ready(c)) zxvd_sandbox_m_account(c, &RB);
}

static void m_roy_message(zxvd_call_t *c)
{
    char kind[16];
    if (!roy_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (!strcmp(kind, "manifest")) {
        roy_manifest_t m;
        uint8_t b[ROY_MANIFEST_MSG];
        if (!roy_manifest(c, &m)) return;
        roy_manifest_msg(&m, b);
        return zxvd_sandbox_put_msg(c, b, sizeof b);
    }
    if (!strcmp(kind, "split")) {
        uint8_t w[32], b[ROY_SPLIT_MSG];
        uint32_t owner;
        roy_split_t s;
        if (!roy_split(c, w, &owner, &s)) return;
        const roy_work_t *x = roy_work(&RS, w);
        if (!x) return roy_refuse(c, ROY_ERR_UNKNOWN);
        roy_split_msg(w, x->m.version, owner, &s, b);
        return zxvd_sandbox_put_msg(c, b, sizeof b);
    }
    zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"manifest\" or \"split\"");
}

static void m_roy_register(zxvd_call_t *c)
{
    roy_manifest_t m;
    uint8_t sig[64];
    if (!roy_ready(c) || !roy_manifest(c, &m) || !zxvd_sandbox_sig(c, "sig", sig)) return;
    roy_status_t st = roy_register(&RS, &RB.L, &m, sig, 64);
    if (st != ROY_OK) return roy_refuse(c, st);
    put_work(zxvd_result(c), roy_work(&RS, m.id));
}

/* R4: "sigs" holds one {"sig": hex} per current owner, in owner order */
static void m_roy_update(zxvd_call_t *c)
{
    roy_manifest_t m;
    uint8_t sb[ROY_MAX_OWNERS][64];
    roy_sig_t sigs[ROY_MAX_OWNERS];
    if (!roy_ready(c) || !roy_manifest(c, &m)) return;
    int32_t n = zxvd_p_len(c, "sigs");
    if (n < -1) return;
    if (n < 1 || n > (int32_t) ROY_MAX_OWNERS)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"sigs\" must hold 1..%u objects",
                         (unsigned) ROY_MAX_OWNERS);
    for (int32_t i = 0; i < n; i++) {
        int32_t sv;
        if (!zxvd_p_enter(c, "sigs", (uint32_t) i, &sv)) return;
        bool ok = zxvd_sandbox_sig(c, "sig", sb[i]);
        zxvd_p_leave(c, sv);
        if (!ok) return;
        sigs[i].sig = sb[i];
        sigs[i].len = 64;
    }
    roy_status_t st = roy_update(&RS, &RB.L, &m, sigs, (uint32_t) n);
    if (st != ROY_OK) return roy_refuse(c, st);
    put_work(zxvd_result(c), roy_work(&RS, m.id));
}

static void m_roy_split(zxvd_call_t *c)
{
    uint8_t w[32], sig[64];
    uint32_t owner;
    roy_split_t s;
    if (!roy_ready(c) || !roy_split(c, w, &owner, &s) || !zxvd_sandbox_sig(c, "sig", sig)) return;
    roy_status_t st = roy_split_set(&RS, &RB.L, w, owner, &s, sig, 64);
    if (st != ROY_OK) return roy_refuse(c, st);
    put_work(zxvd_result(c), roy_work(&RS, w));
}

/* R5: a realised sale in the sandbox: buyer -> payer, its receipt the
 * posting's provenance hash */
static void m_roy_sale(zxvd_call_t *c)
{
    uint32_t from, to;
    uint64_t amt, now;
    if (!roy_ready(c) || !zxvd_sandbox_need_u32(c, "from", &from) ||
        !zxvd_sandbox_need_u32(c, "to", &to) || !zxvd_sandbox_need_u64(c, "amount_minor", &amt) ||
        !zxvd_sandbox_now(c, &now))
        return;
    pay_posting_req_t r;
    pay_receipt_t rc;
    book_req(&RB, "sale", now, &r);
    if (pay_ledger_transfer(&RB.L, &r, from, to, amt, &rc) != PAY_OK)
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused the sale");
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"from\":%u,\"to\":%u", (unsigned) from, (unsigned) to);
    zxvd_sandbox_kv(o, ",\"amount_minor\":", amt);
    put_hex32(o, ",\"receipt\":", rc.hash);
    ZXVD_RAW(o, "}");
}

static void m_roy_quote(zxvd_call_t *c)
{
    static roy_quote_t q;
    uint8_t w[32];
    uint64_t gross;
    if (!roy_ready(c) || !zxvd_sandbox_key32(c, "work", w) ||
        !zxvd_sandbox_need_u64(c, "gross_minor", &gross))
        return;
    roy_status_t st = roy_quote(&RS, w, gross, &q);
    if (st != ROY_OK) return roy_refuse(c, st);
    put_quote(zxvd_result(c), &q);
}

static void m_roy_pay(zxvd_call_t *c)
{
    static roy_quote_t q;
    uint8_t w[32], rcpt[32], dg[32];
    uint32_t payer;
    uint64_t gross, now;
    if (!roy_ready(c) || !zxvd_sandbox_key32(c, "work", w) ||
        !zxvd_sandbox_need_u32(c, "payer", &payer) ||
        !zxvd_sandbox_need_u64(c, "gross_minor", &gross) ||
        !zxvd_sandbox_key32(c, "receipt", rcpt) || !zxvd_sandbox_key32(c, "digest", dg) ||
        !zxvd_sandbox_now(c, &now))
        return;
    pay_posting_req_t r;
    book_req(&RB, "roy", now, &r);
    memset(&q, 0, sizeof q);
    roy_status_t st = roy_pay(&RS, &RB.L, &r, payer, w, gross, rcpt, dg, &q);
    if (st != ROY_OK && st != ROY_ZERO) return roy_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"posted\":%s,\"quote\":", st == ROY_OK ? "true" : "false");
    put_quote(o, &q);
    ZXVD_RAW(o, "}");
}

static void m_roy_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u", RB.open ? "true" : "false", (unsigned) RK.n);
    if (RB.open) {
        zxvd_buf_printf(o, ",\"fee_account\":%u", (unsigned) RS.fee_acct);
        zxvd_sandbox_kv(o, ",\"fee_minor\":", bal(&RB, RS.fee_acct));
        zxvd_buf_printf(o, ",\"receipts_used\":%u,\"works\":[", (unsigned) RS.n_receipts);
        bool first = true;
        for (uint32_t i = 0; i < ROY_MAX_WORKS; i++) {
            if (!RS.work[i].used) continue;
            if (!first) ZXVD_RAW(o, ",");
            put_work(o, &RS.work[i]);
            first = false;
        }
        ZXVD_RAW(o, "]");
        put_check(o, roy_check(&RS, &RB.L));
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void roy_close(void)
{
    RB.open = false;
    RK.n = 0;
    memset(&RS, 0, sizeof RS);
}

static const zxvd_group_method_t ROY_M[] = {
    {"royalty.setup", m_roy_setup, false},
    {"royalty.key", m_roy_key, false},
    {"royalty.account_open", m_roy_account_open, false},
    {"royalty.fund", m_roy_fund, false},
    {"royalty.account", m_roy_account, true},
    {"royalty.message", m_roy_message, true},
    {"royalty.register", m_roy_register, false},
    {"royalty.update", m_roy_update, false},
    {"royalty.split", m_roy_split, false},
    {"royalty.sale", m_roy_sale, false},
    {"royalty.quote", m_roy_quote, true},
    {"royalty.pay", m_roy_pay, false},
    {"royalty.status", m_roy_status, true},
};

const zxvd_group_t *zxvd_royalty_group(void)
{
    static const zxvd_group_t G = {
        "royalty",
        false,
        "IP royalties from realised sales only (royalty.h): no advances, exact splits; "
        "sandbox ledger in memory",
        ROY_M,
        sizeof ROY_M / sizeof ROY_M[0],
        roy_close};
    return &G;
}

/* ===================== cred.* ===================== */

static cred_t CRS;
static bool CR_OPEN;

static void cr_refuse(zxvd_call_t *c, cred_status_t st)
{
    static const char *const N[][2] = {
        {"CRED_OK", ""},
        {"CRED_ERR_ARG", "a bad argument"},
        {"CRED_ERR_FULL", "the subject table is full"},
        {"CRED_ERR_UNKNOWN", "no such subject"},
        {"CRED_ERR_DUP", "that receipt or ruling was seen already (replay), or the subject "
                         "exists"},
        {"CRED_ERR_SELF", "a subject cannot rate itself"},
        {"CRED_ERR_STATE", "trust is not computed for this subject set: call cred.compute"},
    };
    refuse_tab(c, N, sizeof N / sizeof N[0], st, 1);
}

static bool cr_ready(zxvd_call_t *c)
{
    return !not_set_up(c, CR_OPEN, "cred");
}

static void m_cr_setup(zxvd_call_t *c)
{
    if (CR_OPEN) return zxvd_sandbox_refuse(c, "SET_UP", "cred is set up already (this run)");
    cred_config_t cfg;
    cred_config_default(&cfg);
    if (!zxvd_sandbox_u32(c, "decay_num", false, cfg.decay_num, &cfg.decay_num) ||
        !zxvd_sandbox_u32(c, "decay_den", false, cfg.decay_den, &cfg.decay_den) ||
        !zxvd_sandbox_u32(c, "alpha_num", false, cfg.alpha_num, &cfg.alpha_num) ||
        !zxvd_sandbox_u32(c, "alpha_den", false, cfg.alpha_den, &cfg.alpha_den) ||
        !zxvd_sandbox_u32(c, "iters", false, cfg.iters, &cfg.iters) ||
        !zxvd_sandbox_u64(c, "fail_base", false, cfg.fail_base, &cfg.fail_base) ||
        !zxvd_sandbox_u32(c, "fail_cap", false, cfg.fail_cap, &cfg.fail_cap))
        return;
    cfg.credit_lines_enabled = false; /* Y3 */
    cred_status_t st = cred_init(&CRS, &cfg);
    if (st != CRED_OK) return cr_refuse(c, st);
    CR_OPEN = true;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o,
                    "{\"decay_num\":%u,\"decay_den\":%u,\"alpha_num\":%u,\"alpha_den\":%u,"
                    "\"iters\":%u",
                    (unsigned) cfg.decay_num, (unsigned) cfg.decay_den, (unsigned) cfg.alpha_num,
                    (unsigned) cfg.alpha_den, (unsigned) cfg.iters);
    zxvd_sandbox_kv(o, ",\"fail_base\":", cfg.fail_base);
    zxvd_buf_printf(o, ",\"fail_cap\":%u,\"credit_lines\":false}", (unsigned) cfg.fail_cap);
}

static void m_cr_subject(zxvd_call_t *c)
{
    uint32_t id;
    bool pre;
    if (!cr_ready(c) || !zxvd_sandbox_need_u32(c, "id", &id) ||
        !zxvd_p_bool(c, "pretrusted", false, &pre))
        return;
    cred_status_t st = cred_subject_add(&CRS, id, pre);
    if (st != CRED_OK) return cr_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"id\":%u,\"pretrusted\":%s,\"subjects\":%u}", (unsigned) id,
                    pre ? "true" : "false", (unsigned) CRS.n);
}

static void m_cr_fulfil(zxvd_call_t *c)
{
    uint32_t i, j;
    uint64_t v;
    uint8_t rc[32];
    if (!cr_ready(c) || !zxvd_sandbox_need_u32(c, "rater", &i) ||
        !zxvd_sandbox_need_u32(c, "ratee", &j) || !zxvd_sandbox_need_u64(c, "value_minor", &v) ||
        !zxvd_sandbox_key32(c, "receipt", rc))
        return;
    cred_status_t st = cred_fulfil(&CRS, i, j, v, rc);
    if (st != CRED_OK) return cr_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"rater\":%u,\"ratee\":%u", (unsigned) i, (unsigned) j);
    zxvd_sandbox_kv(o, ",\"gain\":", cred_gain(v));
    zxvd_sandbox_kv(o, ",\"local\":", cred_local(&CRS, i, j));
    ZXVD_RAW(o, ",\"computed\":false}");
}

static void m_cr_fail(zxvd_call_t *c)
{
    uint32_t i, j;
    uint8_t ru[32];
    if (!cr_ready(c) || !zxvd_sandbox_need_u32(c, "rater", &i) ||
        !zxvd_sandbox_need_u32(c, "ratee", &j) || !zxvd_sandbox_key32(c, "ruling", ru))
        return;
    cred_status_t st = cred_fail(&CRS, i, j, ru);
    if (st != CRED_OK) return cr_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"rater\":%u,\"ratee\":%u", (unsigned) i, (unsigned) j);
    zxvd_sandbox_kv(o, ",\"local\":", cred_local(&CRS, i, j));
    ZXVD_RAW(o, ",\"computed\":false}");
}

static void m_cr_decay(zxvd_call_t *c)
{
    if (!cr_ready(c)) return;
    cred_status_t st = cred_decay_epoch(&CRS);
    if (st != CRED_OK) return cr_refuse(c, st);
    zxvd_sandbox_kv(zxvd_result(c), "{\"epoch\":", CRS.epoch);
    ZXVD_RAW(zxvd_result(c), "}");
}

static void m_cr_compute(zxvd_call_t *c)
{
    if (!cr_ready(c)) return;
    cred_status_t st = cred_trust_compute(&CRS);
    if (st != CRED_OK) return cr_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"iters\":%u", (unsigned) CRS.iters_used);
    zxvd_sandbox_kv(o, ",\"last_delta\":", CRS.last_delta);
    ZXVD_RAW(o, ",\"computed\":true}");
}

static void m_cr_card(zxvd_call_t *c)
{
    static char js[4096];
    uint32_t id, up;
    uint64_t bond, giving;
    cred_card_t card;
    if (!cr_ready(c) || !zxvd_sandbox_need_u32(c, "subject", &id) ||
        !zxvd_sandbox_u64(c, "bond", false, 0, &bond) ||
        !zxvd_sandbox_u32(c, "uptime_bps", false, 0, &up) ||
        !zxvd_sandbox_u64(c, "giving", false, 0, &giving))
        return;
    cred_status_t st = cred_player_card(&CRS, id, bond, up, giving, NULL, &card);
    if (st != CRED_OK) return cr_refuse(c, st);
    card.src_bond = card.src_uptime = card.src_giving = "sandbox input";
    int32_t n = cred_card_json(&card, js, sizeof js);
    if (n < 0) return zxvd_sandbox_refuse(c, "CRED_ERR_ARG", "the card does not fit");
    zxvd_buf_t *o = zxvd_result(c);
    ZXVD_RAW(o, "{\"card\":");
    zxvd_buf_raw(o, js, (size_t) n);
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void m_cr_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"credit_lines\":false", CR_OPEN ? "true" : "false");
    if (CR_OPEN) {
        zxvd_sandbox_kv(o, ",\"epoch\":", CRS.epoch);
        zxvd_sandbox_kv(o, ",\"fulfilments\":", CRS.n_fulfil);
        zxvd_sandbox_kv(o, ",\"failures\":", CRS.n_fail);
        zxvd_buf_printf(o, ",\"computed\":%s,\"subjects\":[", CRS.computed ? "true" : "false");
        for (uint32_t i = 0; i < CRS.n; i++) {
            const cred_subject_t *s = &CRS.subj[i];
            zxvd_buf_printf(o, "%s{\"id\":%u,\"pretrusted\":%s,\"fails\":%u", i ? "," : "",
                            (unsigned) s->id, s->pretrusted ? "true" : "false",
                            (unsigned) s->fails);
            zxvd_sandbox_kv(o, ",\"trust\":", cred_trust(&CRS, s->id));
            zxvd_sandbox_kv(o, ",\"credibility\":", cred_credibility(&CRS, s->id));
            ZXVD_RAW(o, "}");
        }
        ZXVD_RAW(o, "]");
        put_check(o, cred_check(&CRS));
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void cr_close(void)
{
    CR_OPEN = false;
    memset(&CRS, 0, sizeof CRS);
}

static const zxvd_group_method_t CR_M[] = {
    {"cred.setup", m_cr_setup, false},   {"cred.subject", m_cr_subject, false},
    {"cred.fulfil", m_cr_fulfil, false}, {"cred.fail", m_cr_fail, false},
    {"cred.decay", m_cr_decay, false},   {"cred.compute", m_cr_compute, false},
    {"cred.card", m_cr_card, true},      {"cred.status", m_cr_status, true},
};

const zxvd_group_t *zxvd_cred_group(void)
{
    static const zxvd_group_t G = {
        "cred",
        false,
        "EigenTrust credibility over fulfilled trades (cred.h): no credit lines here, not a "
        "credit score; in memory",
        CR_M,
        sizeof CR_M / sizeof CR_M[0],
        cr_close};
    return &G;
}

/* ===================== dispute.* ===================== */

/* vrf_dsp_verify reads its ctx as a vrf_dsp_ctx_t: it is the first member */
typedef struct {
    vrf_dsp_ctx_t vrf;
    zxvd_sandbox_keys_t keys;
} dsp_hook_ctx_t;

static zxvd_sandbox_book_t DPB = {
    .a = {{.name = "vino", .code = "VINO", .minor = 2, .fundable = true}}, .n_assets = 1};
static dsp_hook_ctx_t DPX;
static dsp_t DPS;

static bool dsp_key_verify(void *ctx, uint32_t id, const uint8_t *m, uint32_t len, const uint8_t *s,
                           uint32_t sl)
{
    return zxvd_sandbox_verify(&((dsp_hook_ctx_t *) ctx)->keys, id, m, len, s, sl);
}

static void dp_refuse(zxvd_call_t *c, dsp_status_t st)
{
    static const char *const N[][2] = {
        {"DSP_OK", ""},
        {"DSP_ERR_ARG", "a bad argument (an unusable account, the same party twice)"},
        {"DSP_ERR_FULL", "the table is full"},
        {"DSP_ERR_UNKNOWN", "no such case or juror"},
        {"DSP_ERR_STATE", "out of order for the case"},
        {"DSP_ERR_SIG", "the signature does not verify"},
        {"DSP_ERR_VRF", "the VRF proof does not verify for this case's seed"},
        {"DSP_ERR_DEADLINE", "too early or too late"},
        {"DSP_ERR_COMMIT", "the reveal does not match the commitment"},
        {"DSP_ERR_DUP", "already done (replay), the order is open already, or the juror exists"},
        {"DSP_ERR_POOL", "not enough eligible jurors"},
        {"DSP_ERR_LEDGER", "the ledger refused a bond"},
        {"DSP_ERR_PARTY", "not the party allowed to act"},
        {"DSP_ERR_OVERFLOW", "a value does not fit"},
    };
    refuse_tab(c, N, sizeof N / sizeof N[0], st, 4);
}

static bool dp_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &DPB, "dispute");
}

static const char *const DSP_SIDE[] = {"buyer", "seller"};
static const char *const DSP_ST[] = {"free",   "proofs",  "select", "vote",
                                     "appeal", "gateway", "final"};

static bool dp_case(zxvd_call_t *c, uint32_t *id)
{
    if (!zxvd_sandbox_need_u32(c, "case", id)) return false;
    if (dsp_case(&DPS, *id)) return true;
    dp_refuse(c, DSP_ERR_UNKNOWN);
    return false;
}

static bool dp_side(zxvd_call_t *c, const char *key, uint8_t *p)
{
    uint32_t s;
    if (!pick(c, key, DSP_SIDE, 2, "\"buyer\" or \"seller\"", &s)) return false;
    *p = (uint8_t) (s + 1); /* DSP_BUYER 1, DSP_SELLER 2 */
    return true;
}

static void put_case(zxvd_buf_t *o, const dsp_case_t *k)
{
    zxvd_buf_printf(o, "{\"case\":%u,\"state\":\"%s\",\"level\":%u,\"round\":%u", (unsigned) k->id,
                    DSP_ST[k->state % 7], (unsigned) k->level, (unsigned) k->round);
    put_hex32(o, ",\"order\":", k->order);
    zxvd_buf_printf(o, ",\"buyer\":%u,\"seller\":%u,\"proofs\":%u,\"jury\":[", (unsigned) k->buyer,
                    (unsigned) k->seller, (unsigned) k->n_proofs);
    for (uint32_t i = 0; i < k->n_jury && i < DSP_MAX_JURY; i++)
        zxvd_buf_printf(o, "%s%u", i ? "," : "", (unsigned) DPS.pool[k->seat[i] % DSP_MAX_POOL].id);
    zxvd_sandbox_kv(o, "],\"commit_deadline\":", k->commit_deadline);
    zxvd_sandbox_kv(o, ",\"reveal_deadline\":", k->reveal_deadline);
    zxvd_sandbox_kv(o, ",\"appeal_deadline\":", k->appeal_deadline);
    zxvd_sandbox_kv(o, ",\"appeal_bond_minor\":", k->appeal_live ? k->appeal_bond : 0);
    zxvd_sandbox_kv(o, ",\"locked_minor\":", dsp_case_locked(&DPS, k->id));
    zxvd_buf_printf(o, ",\"award_bps\":%u}", (unsigned) k->award_bps);
}

static void m_dp_setup(zxvd_call_t *c)
{
    if (DPB.open) return zxvd_sandbox_refuse(c, "SET_UP", "dispute is set up already (this run)");
    dsp_config_t cfg;
    dsp_config_default(&cfg);
    uint32_t gw;
    uint8_t vpk[32];
    if (!zxvd_sandbox_need_u32(c, "gateway", &gw) ||
        !zxvd_sandbox_key32(c, "vrf_public_key", vpk) ||
        !zxvd_sandbox_u64(c, "base_bond", false, cfg.base_bond, &cfg.base_bond) ||
        !zxvd_sandbox_u64(c, "juror_bond", false, cfg.juror_bond, &cfg.juror_bond) ||
        !zxvd_sandbox_u64(c, "commit_ticks", false, cfg.commit_ticks, &cfg.commit_ticks) ||
        !zxvd_sandbox_u64(c, "reveal_ticks", false, cfg.reveal_ticks, &cfg.reveal_ticks) ||
        !zxvd_sandbox_u64(c, "appeal_ticks", false, cfg.appeal_ticks, &cfg.appeal_ticks))
        return;
    int32_t n = zxvd_p_len(c, "jury");
    if (n < -1) return;
    if (n >= 0 && n != (int32_t) DSP_ROUNDS)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"jury\" must hold 3 sizes");
    for (int32_t i = 0; i < n; i++) {
        int64_t v;
        if (!zxvd_p_i64_at(c, "jury", (uint32_t) i, &v)) return;
        if (v < 1 || v > (int64_t) DSP_MAX_JURY)
            return zxvd_fail(c, ZXVD_E_PARAMS, "\"jury\"[%d] is out of range", (int) i);
        cfg.jury[i] = (uint32_t) v;
    }
    if (!vrf_point_valid(vpk))
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"vrf_public_key\" is not a curve point");
    uint32_t esc, res;
    if (zxvd_sandbox_book_open(&DPB) != 0 || !open_acct(&DPB, DPB.a[0].asset, 0, &esc) ||
        !open_acct(&DPB, DPB.a[0].asset, 0, &res)) {
        DPB.open = false;
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    }
    memcpy(DPX.vrf.pk, vpk, 32);
    dsp_status_t st = dsp_init(&DPS, &DPB.L, &cfg, esc, res, gw, dsp_key_verify, vrf_dsp_verify,
                               NULL, &DPX, (const uint8_t *) "zxvd dispute sandbox seed, v1...");
    if (st != DSP_OK) {
        DPB.open = false;
        return dp_refuse(c, st);
    }
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"gateway\":%u,\"escrow\":%u,\"reserve\":%u", (unsigned) gw,
                    (unsigned) esc, (unsigned) res);
    zxvd_sandbox_kv(o, ",\"base_bond_minor\":", cfg.base_bond);
    zxvd_sandbox_kv(o, ",\"juror_bond_minor\":", cfg.juror_bond);
    zxvd_buf_printf(o, ",\"jury\":[%u,%u,%u],\"vrf\":\"ecvrf-edwards25519-sha512-tai\"}",
                    (unsigned) cfg.jury[0], (unsigned) cfg.jury[1], (unsigned) cfg.jury[2]);
}

static void m_dp_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_key(c, &DPX.keys);
}
static void m_dp_account_open(zxvd_call_t *c)
{
    if (dp_ready(c)) zxvd_sandbox_m_account_open(c, &DPB);
}
static void m_dp_fund(zxvd_call_t *c)
{
    if (dp_ready(c)) zxvd_sandbox_m_fund(c, &DPB);
}
static void m_dp_account(zxvd_call_t *c)
{
    if (dp_ready(c)) zxvd_sandbox_m_account(c, &DPB);
}

static void m_dp_juror(zxvd_call_t *c)
{
    uint32_t id, a;
    uint64_t stake;
    bool rm;
    if (!dp_ready(c) || !zxvd_sandbox_need_u32(c, "id", &id) ||
        !zxvd_p_bool(c, "remove", false, &rm))
        return;
    dsp_status_t st;
    if (rm) {
        st = dsp_juror_remove(&DPS, id);
    } else {
        if (!zxvd_sandbox_need_u32(c, "account", &a) || !zxvd_sandbox_need_u64(c, "stake", &stake))
            return;
        st = dsp_juror_add(&DPS, id, a, stake);
    }
    if (st != DSP_OK) return dp_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"id\":%u,\"removed\":%s,\"pool\":%u}", (unsigned) id,
                    rm ? "true" : "false", (unsigned) DPS.n_pool);
}

static void m_dp_open(zxvd_call_t *c)
{
    uint8_t order[32], terms[32];
    uint32_t b, s, ba, sa, id = 0;
    uint64_t esc, now;
    if (!dp_ready(c) || !zxvd_sandbox_key32(c, "order", order) ||
        !zxvd_sandbox_key32(c, "terms", terms) || !zxvd_sandbox_need_u32(c, "buyer", &b) ||
        !zxvd_sandbox_need_u32(c, "seller", &s) ||
        !zxvd_sandbox_need_u32(c, "buyer_account", &ba) ||
        !zxvd_sandbox_need_u32(c, "seller_account", &sa) ||
        !zxvd_sandbox_need_u64(c, "escrow_minor", &esc) || !zxvd_sandbox_now(c, &now))
        return;
    dsp_status_t st = dsp_open(&DPS, order, terms, b, s, ba, sa, esc, now, &id);
    if (st != DSP_OK) return dp_refuse(c, st);
    put_case(zxvd_result(c), dsp_case(&DPS, id));
}

static void m_dp_message(zxvd_call_t *c)
{
    char kind[16];
    uint32_t id;
    if (!dp_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (!dp_case(c, &id)) return;
    const dsp_case_t *k = dsp_case(&DPS, id);
    if (!strcmp(kind, "proof")) {
        uint8_t p, h[32], m[DSP_PROOF_MSG];
        uint32_t pk;
        if (!dp_side(c, "party", &p) || !zxvd_sandbox_need_u32(c, "proof_kind", &pk) ||
            !zxvd_sandbox_key32(c, "hash", h))
            return;
        if (pk > 0xFFu) return zxvd_fail(c, ZXVD_E_PARAMS, "\"proof_kind\" must be below 256");
        dsp_proof_msg(id, k->order, p, (uint8_t) pk, h, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    if (!strcmp(kind, "commit")) {
        uint32_t j;
        uint8_t cm[32], m[DSP_COMMIT_MSG];
        if (!zxvd_sandbox_need_u32(c, "juror", &j) || !zxvd_sandbox_key32(c, "commit", cm)) return;
        dsp_commit_msg(id, k->round, j, cm, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    if (!strcmp(kind, "gateway")) {
        uint32_t aw;
        uint8_t m[DSP_GW_MSG];
        if (!zxvd_sandbox_need_u32(c, "award_bps", &aw)) return;
        dsp_gw_msg(id, aw, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    if (!strcmp(kind, "seed")) {
        uint8_t m[32];
        dsp_vrf_seed(&DPS, id, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    if (!strcmp(kind, "vote")) {
        uint32_t j;
        uint8_t v, salt[32], m[32];
        if (!zxvd_sandbox_need_u32(c, "juror", &j) || !dp_side(c, "vote", &v) ||
            !zxvd_sandbox_key32(c, "salt", salt))
            return;
        dsp_vote_commit(id, j, v, salt, m);
        return zxvd_sandbox_put_msg(c, m, sizeof m);
    }
    zxvd_fail(c, ZXVD_E_PARAMS,
              "\"kind\" must be \"proof\", \"commit\", \"gateway\", \"seed\" or \"vote\"");
}

static void dp_done(zxvd_call_t *c, uint32_t id, dsp_status_t st)
{
    static const char *const R[] = {"ok", "pending", "tie"};
    if (st < 0) return dp_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"result\":\"%s\",\"case\":", R[st % 3]);
    put_case(o, dsp_case(&DPS, id));
    ZXVD_RAW(o, "}");
}

static void m_dp_proof(zxvd_call_t *c)
{
    uint32_t id, pk;
    uint8_t p, h[32], sig[64];
    if (!dp_ready(c) || !dp_case(c, &id) || !dp_side(c, "party", &p) ||
        !zxvd_sandbox_need_u32(c, "proof_kind", &pk) || !zxvd_sandbox_key32(c, "hash", h) ||
        !zxvd_sandbox_sig(c, "sig", sig))
        return;
    if (pk > 0xFFu) return zxvd_fail(c, ZXVD_E_PARAMS, "\"proof_kind\" must be below 256");
    dp_done(c, id, dsp_proof(&DPS, id, p, (uint8_t) pk, h, sig, 64));
}

static void m_dp_escalate(zxvd_call_t *c)
{
    uint32_t id;
    uint8_t p;
    uint64_t now;
    if (!dp_ready(c) || !dp_case(c, &id) || !dp_side(c, "party", &p) || !zxvd_sandbox_now(c, &now))
        return;
    dp_done(c, id, dsp_escalate(&DPS, id, p, now));
}

static void m_dp_select(zxvd_call_t *c)
{
    uint32_t id;
    uint8_t pi[VRF_PROOF_LEN], out[32];
    uint64_t now;
    if (!dp_ready(c) || !dp_case(c, &id) || !hexn(c, "proof", pi, VRF_PROOF_LEN) ||
        !zxvd_sandbox_key32(c, "out", out) || !zxvd_sandbox_now(c, &now))
        return;
    dp_done(c, id, dsp_select(&DPS, id, pi, VRF_PROOF_LEN, out, now));
}

static void m_dp_commit(zxvd_call_t *c)
{
    uint32_t id, j;
    uint8_t cm[32], sig[64];
    uint64_t now;
    if (!dp_ready(c) || !dp_case(c, &id) || !zxvd_sandbox_need_u32(c, "juror", &j) ||
        !zxvd_sandbox_key32(c, "commit", cm) || !zxvd_sandbox_sig(c, "sig", sig) ||
        !zxvd_sandbox_now(c, &now))
        return;
    dp_done(c, id, dsp_commit(&DPS, id, j, cm, sig, 64, now));
}

static void m_dp_reveal(zxvd_call_t *c)
{
    uint32_t id, j;
    uint8_t v, salt[32];
    uint64_t now;
    if (!dp_ready(c) || !dp_case(c, &id) || !zxvd_sandbox_need_u32(c, "juror", &j) ||
        !dp_side(c, "vote", &v) || !zxvd_sandbox_key32(c, "salt", salt) ||
        !zxvd_sandbox_now(c, &now))
        return;
    dp_done(c, id, dsp_reveal(&DPS, id, j, v, salt, now));
}

static void m_dp_tally(zxvd_call_t *c)
{
    uint32_t id;
    uint64_t now;
    if (!dp_ready(c) || !dp_case(c, &id) || !zxvd_sandbox_now(c, &now)) return;
    dp_done(c, id, dsp_tally(&DPS, id, now));
}

static void m_dp_finalize(zxvd_call_t *c)
{
    uint32_t id;
    uint64_t now;
    if (!dp_ready(c) || !dp_case(c, &id) || !zxvd_sandbox_now(c, &now)) return;
    dp_done(c, id, dsp_finalize(&DPS, id, now));
}

static void m_dp_gateway(zxvd_call_t *c)
{
    uint32_t id, aw;
    uint8_t sig[64];
    if (!dp_ready(c) || !dp_case(c, &id) || !zxvd_sandbox_need_u32(c, "award_bps", &aw) ||
        !zxvd_sandbox_sig(c, "sig", sig))
        return;
    dp_done(c, id, dsp_gateway_rule(&DPS, id, aw, sig, 64));
}

static void m_dp_ruling(zxvd_call_t *c)
{
    uint32_t id;
    dsp_ruling_t r;
    uint8_t d[32];
    if (!dp_ready(c) || !dp_case(c, &id)) return;
    dsp_status_t st = dsp_ruling(&DPS, id, &r);
    if (st == DSP_OK) st = dsp_ruling_digest(&DPS, id, d);
    if (st != DSP_OK) return dp_refuse(c, st);
    uint64_t to_b = 0, to_s = 0;
    dsp_award_amount(dsp_case(&DPS, id)->escrow, r.award_bps, &to_b, &to_s);
    static const char *const L[] = {"none", "buyer", "seller"};
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o,
                    "{\"case\":%u,\"award_bps\":%u,\"level\":%u,\"round\":%u,\"loser\":\"%s\","
                    "\"loser_id\":%u",
                    (unsigned) id, (unsigned) r.award_bps, (unsigned) r.level, (unsigned) r.round,
                    L[r.loser % 3], (unsigned) r.loser_id);
    zxvd_sandbox_kv(o, ",\"buyer_minor\":", to_b);
    zxvd_sandbox_kv(o, ",\"seller_minor\":", to_s);
    put_hex32(o, ",\"digest\":", d);
    ZXVD_RAW(o, "}");
}

static void m_dp_release(zxvd_call_t *c)
{
    uint32_t id;
    if (!dp_ready(c) || !dp_case(c, &id)) return;
    dsp_status_t st = dsp_release(&DPS, id);
    if (st != DSP_OK) return dp_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"case\":%u,\"released\":true}", (unsigned) id);
}

static void m_dp_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u", DPB.open ? "true" : "false",
                    (unsigned) DPX.keys.n);
    if (DPB.open) {
        zxvd_sandbox_kv(o, ",\"locked_minor\":", DPS.locked_total);
        zxvd_sandbox_kv(o, ",\"returned_minor\":", DPS.returned_total);
        zxvd_sandbox_kv(o, ",\"slashed_minor\":", DPS.slashed_total);
        zxvd_sandbox_kv(o, ",\"escrow_minor\":", bal(&DPB, DPS.escrow));
        zxvd_sandbox_kv(o, ",\"reserve_minor\":", bal(&DPB, DPS.reserve));
        ZXVD_RAW(o, ",\"pool\":[");
        for (uint32_t i = 0; i < DPS.n_pool; i++) {
            const dsp_juror_t *j = &DPS.pool[i];
            zxvd_buf_printf(o, "%s{\"id\":%u,\"account\":%u,\"active\":%s", i ? "," : "",
                            (unsigned) j->id, (unsigned) j->acct, j->active ? "true" : "false");
            zxvd_sandbox_kv(o, ",\"stake\":", j->stake);
            ZXVD_RAW(o, "}");
        }
        ZXVD_RAW(o, "],\"cases\":[");
        bool first = true;
        for (uint32_t i = 0; i < DSP_MAX_CASES; i++) {
            if (DPS.c[i].state == DSP_ST_FREE) continue;
            if (!first) ZXVD_RAW(o, ",");
            put_case(o, &DPS.c[i]);
            first = false;
        }
        ZXVD_RAW(o, "]");
        put_check(o, dsp_check(&DPS));
    }
    ZXVD_RAW(o, ",\"sandbox\":true}");
}

static void dp_close(void)
{
    DPB.open = false;
    DPX.keys.n = 0;
    memset(&DPS, 0, sizeof DPS);
}

static const zxvd_group_method_t DP_M[] = {
    {"dispute.setup", m_dp_setup, false},
    {"dispute.key", m_dp_key, false},
    {"dispute.account_open", m_dp_account_open, false},
    {"dispute.fund", m_dp_fund, false},
    {"dispute.account", m_dp_account, true},
    {"dispute.juror", m_dp_juror, false},
    {"dispute.open", m_dp_open, false},
    {"dispute.message", m_dp_message, true},
    {"dispute.proof", m_dp_proof, false},
    {"dispute.escalate", m_dp_escalate, false},
    {"dispute.select", m_dp_select, false},
    {"dispute.commit", m_dp_commit, false},
    {"dispute.reveal", m_dp_reveal, false},
    {"dispute.tally", m_dp_tally, false},
    {"dispute.finalize", m_dp_finalize, false},
    {"dispute.gateway", m_dp_gateway, false},
    {"dispute.ruling", m_dp_ruling, true},
    {"dispute.release", m_dp_release, false},
    {"dispute.status", m_dp_status, true},
};

const zxvd_group_t *zxvd_dispute_group(void)
{
    static const zxvd_group_t G = {
        "dispute",
        false,
        "the dispute funnel (dispute.h): proofs, an ECVRF-drawn staked jury, a jurisdiction "
        "gateway; a protocol outcome, not a court; sandbox ledger in memory",
        DP_M,
        sizeof DP_M / sizeof DP_M[0],
        dp_close};
    return &G;
}

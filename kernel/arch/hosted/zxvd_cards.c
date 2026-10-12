/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_cards.c — cards.*: the Dragon / Phoenix / Thunderbird charge-card
 * issuer (kernel/src/cardnet) on zxvd's socket, behind the "cards" feature
 * switch (zxvd_ext.h X3-X5; regulated, off by default).
 *
 * WHAT IT SERVES
 *   K1  One issuer per run, in memory only (cn_issuer_t in static storage):
 *       cards.issuer picks the network once; a restart starts empty. The
 *       engine's fixed tables bound everything (8 accounts, 16 cards, 32
 *       authorizations, 64 ledger transactions); a full table is an error.
 *   K2  Cards bind the holder's ML-DSA-65 public key. Every authorization
 *       carries the holder's signature over cards.challenge's bytes; the
 *       holder's secret key never comes to zxvd. The issuer seed (PAN
 *       derivation) comes from the OS, or from "seed" for a test; it is
 *       never sent back.
 *   K3  cards.authorize is the funding decision: approved or declined is a
 *       result (with its ISO 8583 field 39 code and the 0110 response), not
 *       an error. A request may come as fields + "sig" or as the EMV-shaped
 *       TLV container ("emv", cn_emv.h) that carries the signature.
 *   K4  capture settles an approval in full (DR 555 / CR 777 legs and a
 *       hash-chained receipt); reverse releases an approval's hold; refund
 *       posts the mirror of a settled charge. Partial amounts are not in the
 *       engine. Charge, not credit: no interest field exists anywhere.
 *   K5  Times on the wire are unix seconds; "now" defaults to the clock.
 *       Amounts are minor units, 64-bit results as decimal strings.
 *   K6  Errors: -32602 bad params, ZXVD_E_CARDS (-32040) a refusal by the
 *       engine (the message names it).
 * HONEST LIMITS: no card-scheme membership or certification, no registered
 * IIN or RID, no PCI DSS claim, no licence; the 880/882/884 numbers are
 * valid inside this deployment only (cardnet.h).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "zxvd_groups.h"
#include "zxv_http_guard.h" /* zxv_os_random */
#include "cardnet.h"
#include "cn_emv.h"
#include "cn_iso8583.h"

#define ZXVD_E_CARDS (-32040)
#define CN_EPOCH     946684800ll /* 2000-01-01T00:00:00Z, cardnet's time zero */
#define NONE         UINT32_MAX

static struct {
    bool ready;
    cn_issuer_t iss;
    uint8_t sig[CN_SIG_BYTES];
    uint8_t pk[CN_PK_BYTES];
    uint8_t tlv[CN_EMV_AUTH_MAX];
    uint8_t wire[CN8583_MAX_WIRE];
    cn8583_msg_t msg;
} C;

static int (*RANDOM)(uint8_t *p, size_t n) = zxv_os_random;

void zxvd_cards_set_random(int (*fn)(uint8_t *p, size_t n))
{
    RANDOM = fn ? fn : zxv_os_random;
}

static const char *const STATE_ACCT[] = {"?", "active", "delinquent", "closed"};
static const char *const STATE_CARD[] = {"?", "active", "frozen", "revoked"};
static const char *const STATE_AUTH[] = {"?", "approved", "declined", "settled", "reversed"};
static const char *const DECLINE[] = {"none",    "bad_pan",    "unknown_card", "revoked",
                                      "frozen",  "expired",    "bad_sig",      "replay",
                                      "form",    "currency",   "txn_limit",    "cycle_limit",
                                      "ceiling", "delinquent", "format",       "system"};

static const char *rc_name(cn_rc_t rc)
{
    switch (rc) {
    case CN_ERR_CONFIG:
        return "the configuration was refused";
    case CN_ERR_FULL:
        return "a fixed table is full (in-memory limits)";
    case CN_ERR_NOT_FOUND:
        return "not found";
    case CN_ERR_STATE:
        return "not allowed in its current state";
    case CN_ERR_FORM:
        return "that capital form cannot carry a card";
    case CN_ERR_MINT:
        return "no unique PAN could be minted";
    case CN_ERR_AMOUNT:
        return "the amount is zero or more than is owed";
    default:
        return "bad argument";
    }
}

static bool refused(zxvd_call_t *c, const char *what, cn_rc_t rc)
{
    if (rc == CN_OK) return false;
    zxvd_fail(c, ZXVD_E_CARDS, "%s: %s", what, rc_name(rc));
    return true;
}

static bool need_issuer(zxvd_call_t *c)
{
    if (C.ready) return true;
    zxvd_fail(c, ZXVD_E_CARDS, "no issuer yet: call cards.issuer first");
    return false;
}

/* K5: a unix time in cardnet's range; "now" (or the given key) absent = clock */
static bool p_time(zxvd_call_t *c, const char *key, bool required, cn_time_t *t)
{
    int64_t v;
    if (!zxvd_p_has(c, key)) {
        if (required) return zxvd_p_i64(c, key, &v); /* fails: missing */
        v = (int64_t) time(NULL);
    } else if (!zxvd_p_i64(c, key, &v)) {
        return false;
    }
    if (v < CN_EPOCH || v - CN_EPOCH > (int64_t) UINT32_MAX) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be unix seconds from 2000 to 2136", key);
        return false;
    }
    *t = (cn_time_t) (v - CN_EPOCH);
    return true;
}

static bool p_hex_exact(zxvd_call_t *c, const char *key, uint8_t *out, uint32_t len)
{
    uint32_t n;
    if (!zxvd_p_hex(c, key, out, len, &n, true)) return false;
    if (n != len) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be %u bytes of hex", key, len);
        return false;
    }
    return true;
}

/* 32 bytes from "key" (exactly), or from the OS when absent */
static bool p_entropy(zxvd_call_t *c, const char *key, uint8_t out[32])
{
    if (zxvd_p_has(c, key)) return p_hex_exact(c, key, out, 32);
    if (RANDOM(out, 32) == 0) return true;
    zxvd_fail(c, ZXVD_E_INTERNAL, "no random bytes from the OS");
    return false;
}

static bool p_index(zxvd_call_t *c, const char *key, uint32_t lim, uint32_t *v)
{
    if (!zxvd_p_u32(c, key, v)) return false;
    if (*v < lim) return true;
    zxvd_fail(c, ZXVD_E_CARDS, "%s %u: not found", key, (unsigned) *v);
    return false;
}

static void put_u64(zxvd_buf_t *b, const char *key, uint64_t v)
{
    zxvd_buf_printf(b, ",\"%s\":\"%llu\"", key, (unsigned long long) v);
}

static void put_card(zxvd_buf_t *b, uint32_t i)
{
    const cn_card_t *k = &C.iss.cards[i];
    zxvd_buf_printf(b, "{\"card\":%u,\"pan\":\"%s\",\"network\":\"%s\",\"account\":%u,\"form\":%u",
                    (unsigned) i, k->pan, cn_network_name(cn_pan_network(k->pan, CN_PAN_LEN)),
                    (unsigned) k->account, (unsigned) k->form);
    zxvd_buf_printf(b, ",\"state\":\"%s\",\"exp_year\":%u,\"exp_month\":%u,\"atc\":%u",
                    STATE_CARD[k->state <= 3 ? k->state : 0], (unsigned) k->exp_year,
                    (unsigned) k->exp_month, (unsigned) k->atc);
    if (k->replaces == NONE)
        zxvd_buf_raw(b, ",\"replaces\":null}", 17);
    else
        zxvd_buf_printf(b, ",\"replaces\":%u}", (unsigned) k->replaces);
}

static void put_account(zxvd_buf_t *b, uint32_t i)
{
    const cn_account_t *a = &C.iss.accounts[i];
    zxvd_buf_printf(b, "{\"account\":%u,\"state\":\"%s\"", (unsigned) i,
                    STATE_ACCT[a->state <= 3 ? a->state : 0]);
    put_u64(b, "held_minor", a->held);
    put_u64(b, "unbilled_minor", a->unbilled);
    put_u64(b, "billed_minor", a->billed);
    put_u64(b, "owed_minor", cn_account_owed(&C.iss, i));
    if (a->billed)
        zxvd_buf_printf(b, ",\"due\":%lld}",
                        (long long) a->due_day * CN_SECS_PER_DAY + (long long) CN_EPOCH);
    else
        zxvd_buf_raw(b, ",\"due\":null}", 12);
}

static bool account_ok(zxvd_call_t *c, uint32_t *a)
{
    if (!p_index(c, "account", CN_MAX_ACCOUNTS, a)) return false;
    if (C.iss.accounts[*a].used) return true;
    zxvd_fail(c, ZXVD_E_CARDS, "account %u: not found", (unsigned) *a);
    return false;
}

static bool card_ok(zxvd_call_t *c, uint32_t *k)
{
    if (!p_index(c, "card", CN_MAX_CARDS, k)) return false;
    if (C.iss.cards[*k].used) return true;
    zxvd_fail(c, ZXVD_E_CARDS, "card %u: not found", (unsigned) *k);
    return false;
}

static bool auth_ok(zxvd_call_t *c, uint32_t *au)
{
    return p_index(c, "auth", C.iss.n_auths, au);
}

/* ----- methods ----- */

static void m_status(zxvd_call_t *c)
{
    zxvd_buf_t *b = zxvd_result(c);
    if (!C.ready) {
        zxvd_buf_raw(b, "{\"issuer\":false,\"in_memory\":true}", 33);
        return;
    }
    const cn_issuer_t *s = &C.iss;
    uint32_t accts = 0, cards = 0, approved = 0, declined = 0;
    for (uint32_t i = 0; i < CN_MAX_ACCOUNTS; i++) accts += s->accounts[i].used;
    for (uint32_t i = 0; i < CN_MAX_CARDS; i++) cards += s->cards[i].used;
    for (uint32_t i = 0; i < s->n_auths; i++) {
        approved += s->auths[i].state != CN_AUTH_DECLINED;
        declined += s->auths[i].state == CN_AUTH_DECLINED;
    }
    zxvd_buf_printf(b,
                    "{\"issuer\":true,\"network\":\"%s\",\"prefix\":\"%s\",\"issuer_code\":\"%s\"",
                    cn_network_name((cn_network_t) s->cfg.network),
                    cn_network_prefix((cn_network_t) s->cfg.network), s->cfg.issuer_code);
    put_u64(b, "fee_minor", s->cfg.merchant_fee.amount_minor);
    zxvd_buf_printf(b,
                    ",\"accounts\":%u,\"cards\":%u,\"approved\":%u,\"declined\":%u,\"txns\":%u,"
                    "\"receipts\":%u,\"statements\":%u,\"ledger_balanced\":%s,\"receipts_ok\":%s,"
                    "\"in_memory\":true}",
                    (unsigned) accts, (unsigned) cards, (unsigned) approved, (unsigned) declined,
                    (unsigned) s->n_txns, (unsigned) s->n_receipts, (unsigned) s->n_statements,
                    cn_ledger_balanced(s) ? "true" : "false",
                    cn_receipts_verify(s) ? "true" : "false");
}

static void m_check_pan(zxvd_call_t *c)
{
    char pan[20];
    if (!zxvd_p_str(c, "pan", pan, sizeof pan, true)) return;
    uint32_t n = (uint32_t) strlen(pan);
    bool digits = n >= 2;
    for (uint32_t i = 0; i < n; i++) digits = digits && pan[i] >= '0' && pan[i] <= '9';
    if (!digits) return zxvd_fail(c, ZXVD_E_PARAMS, "\"pan\" must be 2 to 19 digits");
    cn_network_t net = cn_pan_network(pan, n);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"digits\":%u,\"iin\":\"%.6s\",\"luhn\":%s,\"damm\":%s,\"verhoeff\":%s",
                    (unsigned) n, pan, cn_luhn_valid(pan, n) ? "true" : "false",
                    cn_damm_valid(pan, n) ? "true" : "false",
                    cn_verhoeff_valid(pan, n) ? "true" : "false");
    if (net == CN_NET_NONE)
        zxvd_buf_raw(b, ",\"network\":null}", 16);
    else
        zxvd_buf_printf(b, ",\"network\":\"%s\"}", cn_network_name(net));
}

static void m_issuer(zxvd_call_t *c)
{
    static const char *const NET[] = {"dragon", "phoenix", "thunderbird"};
    char net[16], code[8];
    int64_t fee = 0;
    cn_issuer_cfg_t cfg;
    if (!zxvd_p_str(c, "network", net, sizeof net, true) ||
        !zxvd_p_str(c, "issuer_code", code, sizeof code, false) ||
        (zxvd_p_has(c, "fee_minor") && !zxvd_p_amount(c, "fee_minor", &fee)))
        return;
    uint32_t k = 0;
    while (k < CN_NET_COUNT && strcmp(net, NET[k])) k++;
    if (k == CN_NET_COUNT)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"network\" must be dragon, phoenix or thunderbird");
    if (C.ready) return zxvd_fail(c, ZXVD_E_CARDS, "the issuer is already set up for this run");
    cn_cfg_default(&cfg, (cn_network_t) k);
    if (code[0]) {
        if (strlen(code) != CN_ISSUER_CODE_LEN)
            return zxvd_fail(c, ZXVD_E_PARAMS, "\"issuer_code\" must be 3 digits");
        memcpy(cfg.issuer_code, code, sizeof cfg.issuer_code);
    }
    if (fee < 0 || (uint64_t) fee > CN_FEE_MAX_MINOR)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"fee_minor\" must be 0 to %llu",
                         (unsigned long long) CN_FEE_MAX_MINOR);
    cfg.merchant_fee.kind = fee ? CN_FEE_FLAT_PER_TXN : CN_FEE_NONE;
    cfg.merchant_fee.amount_minor = (uint64_t) fee;
    if (!p_entropy(c, "seed", cfg.issuer_seed)) return;
    cn_rc_t rc = cn_issuer_init(&C.iss, &cfg);
    memset(cfg.issuer_seed, 0, sizeof cfg.issuer_seed);
    if (refused(c, "issuer", rc)) return;
    C.ready = true;
    m_status(c);
}

static void m_open_account(zxvd_call_t *c)
{
    cn_time_t now;
    uint32_t a;
    if (!p_time(c, "now", false, &now) || !need_issuer(c)) return;
    if (refused(c, "open_account", cn_open_account(&C.iss, now, &a))) return;
    put_account(zxvd_result(c), a);
}

static void m_issue(zxvd_call_t *c)
{
    uint32_t a, form, k;
    uint8_t e[32];
    cn_time_t now;
    if (!need_issuer(c) || !account_ok(c, &a) || !zxvd_p_u32(c, "form", &form) ||
        !p_hex_exact(c, "holder_pk", C.pk, CN_PK_BYTES) || !p_time(c, "now", false, &now) ||
        !p_entropy(c, "entropy", e))
        return;
    if (refused(c, "issue", cn_issue_card(&C.iss, a, form, C.pk, e, now, &k))) return;
    put_card(zxvd_result(c), k);
}

static void m_reissue(zxvd_call_t *c)
{
    uint32_t k, nk, n;
    uint8_t e[32];
    cn_time_t now;
    if (!need_issuer(c) || !card_ok(c, &k) ||
        !zxvd_p_hex(c, "holder_pk", C.pk, CN_PK_BYTES, &n, false) ||
        !p_time(c, "now", false, &now) || !p_entropy(c, "entropy", e))
        return;
    if (n && n != CN_PK_BYTES)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"holder_pk\" must be %u bytes of hex", CN_PK_BYTES);
    if (refused(c, "reissue", cn_reissue_card(&C.iss, k, n ? C.pk : NULL, e, now, &nk))) return;
    put_card(zxvd_result(c), nk);
}

static void m_freeze(zxvd_call_t *c)
{
    uint32_t k;
    bool on;
    if (!need_issuer(c) || !card_ok(c, &k) || !zxvd_p_bool(c, "frozen", true, &on)) return;
    if (refused(c, "freeze", cn_set_frozen(&C.iss, k, on))) return;
    put_card(zxvd_result(c), k);
}

static void m_card(zxvd_call_t *c)
{
    char pan[CN_PAN_LEN + 2];
    uint32_t k;
    if (!need_issuer(c)) return;
    if (zxvd_p_has(c, "pan")) {
        if (!zxvd_p_str(c, "pan", pan, sizeof pan, true)) return;
        if (strlen(pan) != CN_PAN_LEN || cn_find_card(&C.iss, pan, &k) != CN_OK)
            return zxvd_fail(c, ZXVD_E_CARDS, "card: not found");
    } else if (!card_ok(c, &k)) {
        return;
    }
    put_card(zxvd_result(c), k);
}

static void m_account(zxvd_call_t *c)
{
    uint32_t a;
    if (!need_issuer(c) || !account_ok(c, &a)) return;
    put_account(zxvd_result(c), a);
}

/* the authorization request from its fields (cards.challenge and authorize) */
static bool p_req(zxvd_call_t *c, cn_auth_req_t *r, bool time_required)
{
    char pan[CN_PAN_LEN + 2], tid[CN_TID_LEN + 2], mid[CN_MID_LEN + 2];
    int64_t amount;
    uint32_t stan, cur = CN_RAIL_DEBIT;
    memset(r, 0, sizeof *r);
    if (!zxvd_p_str(c, "pan", pan, sizeof pan, true) ||
        !zxvd_p_amount(c, "amount_minor", &amount) || !zxvd_p_u32(c, "form", &r->form) ||
        !zxvd_p_u32(c, "atc", &r->atc) || !p_hex_exact(c, "un", r->un, 4) ||
        !zxvd_p_u32(c, "stan", &stan) || !zxvd_p_str(c, "terminal_id", tid, sizeof tid, true) ||
        !zxvd_p_str(c, "merchant_id", mid, sizeof mid, true) ||
        (zxvd_p_has(c, "currency") && !zxvd_p_u32(c, "currency", &cur)) ||
        !p_time(c, "time", time_required, &r->time))
        return false;
    if (amount < 1 || (uint64_t) amount > CN_AMOUNT_MAX_MINOR) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"amount_minor\" must be 1 to %llu",
                  (unsigned long long) CN_AMOUNT_MAX_MINOR);
        return false;
    }
    if (strlen(pan) > CN_PAN_LEN || strlen(tid) > CN_TID_LEN || strlen(mid) > CN_MID_LEN) {
        zxvd_fail(c, ZXVD_E_PARAMS, "pan, terminal_id or merchant_id is too long (16, 8, 15)");
        return false;
    }
    /* shorter ones go to the engine, which declines them as "format" */
    memcpy(r->pan, pan, strlen(pan) + 1);
    memcpy(r->terminal_id, tid, strlen(tid) + 1);
    memcpy(r->merchant_id, mid, strlen(mid) + 1);
    r->amount_minor = (uint64_t) amount;
    r->currency = cur;
    r->stan = stan;
    return true;
}

static void m_challenge(zxvd_call_t *c)
{
    cn_auth_req_t r;
    uint8_t ch[CN_CHALLENGE_MAX];
    uint32_t n;
    if (!p_req(c, &r, false)) return;
    if (refused(c, "challenge", cn_challenge(&r, ch, &n))) return;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"challenge\":", 13);
    zxvd_buf_hex(b, ch, n);
    zxvd_buf_printf(b, ",\"context\":\"%s\",\"time\":%lld}", CN_SIG_CTX,
                    (long long) r.time + (long long) CN_EPOCH);
}

static void m_authorize(zxvd_call_t *c)
{
    cn_auth_req_t r;
    uint32_t n, ai;
    if (!need_issuer(c)) return;
    if (zxvd_p_has(c, "emv")) {
        if (!zxvd_p_hex(c, "emv", C.tlv, sizeof C.tlv, &n, true)) return;
        if (cn_emv_decode_auth(C.tlv, n, &r, C.sig) != CN_TLV_OK)
            return zxvd_fail(c, ZXVD_E_PARAMS, "\"emv\" is not a valid authorization container");
    } else if (!p_req(c, &r, true) || !p_hex_exact(c, "sig", C.sig, CN_SIG_BYTES)) {
        return;
    }
    if (refused(c, "authorize", cn_authorize(&C.iss, &r, C.sig, &ai))) return;
    const cn_auth_t *au = &C.iss.auths[ai];
    if (cn8583_response_for(&C.msg, 110, au) != CN8583_OK ||
        cn8583_pack(&C.msg, C.wire, sizeof C.wire, &n) != CN8583_OK)
        n = 0;
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"auth\":%u,\"approved\":%s,\"rc\":\"%s\",\"decline\":\"%s\"",
                    (unsigned) ai, au->state == CN_AUTH_APPROVED ? "true" : "false", au->rc,
                    DECLINE[au->decline < 16 ? au->decline : 15]);
    if (au->state == CN_AUTH_APPROVED)
        zxvd_buf_printf(b, ",\"approval\":\"%s\"", au->approval);
    else
        zxvd_buf_raw(b, ",\"approval\":null", 16);
    zxvd_buf_raw(b, ",\"cryptogram\":", 14);
    zxvd_buf_hex(b, au->cryptogram, 8);
    put_u64(b, "amount_minor", au->req.amount_minor);
    zxvd_buf_raw(b, ",\"iso8583\":", 11);
    zxvd_buf_hex(b, C.wire, n);
    zxvd_buf_raw(b, "}", 1);
}

static void put_posted(zxvd_buf_t *b, uint32_t ai)
{
    const cn_auth_t *au = &C.iss.auths[ai];
    const cn_receipt_t *r = &C.iss.receipts[C.iss.n_receipts - 1];
    zxvd_buf_printf(b, "{\"auth\":%u,\"state\":\"%s\",\"txn\":%u,\"receipt\":%u", (unsigned) ai,
                    STATE_AUTH[au->state], (unsigned) au->txn, (unsigned) r->id);
    put_u64(b, "amount_minor", r->amount);
    put_u64(b, "fee_minor", r->fee);
    zxvd_buf_raw(b, ",\"digest\":", 10);
    zxvd_buf_hex(b, r->digest, 32);
    zxvd_buf_printf(b, ",\"account\":%u}", (unsigned) au->account);
}

static void m_capture(zxvd_call_t *c)
{
    uint32_t ai;
    if (!need_issuer(c) || !auth_ok(c, &ai)) return;
    if (refused(c, "capture", cn_settle(&C.iss, ai))) return;
    put_posted(zxvd_result(c), ai);
}

static void m_reverse(zxvd_call_t *c)
{
    uint32_t ai;
    cn_time_t now;
    if (!need_issuer(c) || !auth_ok(c, &ai) || !p_time(c, "now", false, &now)) return;
    if (C.iss.auths[ai].state != CN_AUTH_APPROVED)
        return zxvd_fail(c, ZXVD_E_CARDS, "reverse: auth %u is %s, not approved", (unsigned) ai,
                         STATE_AUTH[C.iss.auths[ai].state]);
    if (refused(c, "reverse", cn_reverse(&C.iss, ai, now))) return;
    zxvd_buf_printf(zxvd_result(c),
                    "{\"auth\":%u,\"state\":\"reversed\",\"released_minor\":\"%llu\"}",
                    (unsigned) ai, (unsigned long long) C.iss.auths[ai].req.amount_minor);
}

static void m_refund(zxvd_call_t *c)
{
    uint32_t ai;
    cn_time_t now;
    if (!need_issuer(c) || !auth_ok(c, &ai) || !p_time(c, "now", false, &now)) return;
    if (C.iss.auths[ai].state != CN_AUTH_SETTLED)
        return zxvd_fail(c, ZXVD_E_CARDS, "refund: auth %u is %s, not settled", (unsigned) ai,
                         STATE_AUTH[C.iss.auths[ai].state]);
    if (refused(c, "refund", cn_reverse(&C.iss, ai, now))) return;
    put_posted(zxvd_result(c), ai);
}

static void m_pay(zxvd_call_t *c)
{
    uint32_t a;
    int64_t amount;
    cn_time_t now;
    if (!need_issuer(c) || !account_ok(c, &a) || !zxvd_p_amount(c, "amount_minor", &amount) ||
        !p_time(c, "now", false, &now))
        return;
    if (amount < 1) return zxvd_fail(c, ZXVD_E_PARAMS, "\"amount_minor\" must be positive");
    if (refused(c, "pay", cn_pay(&C.iss, a, (uint64_t) amount, now))) return;
    put_account(zxvd_result(c), a);
}

static void m_advance(zxvd_call_t *c)
{
    cn_time_t now;
    if (!need_issuer(c) || !p_time(c, "now", false, &now)) return;
    uint32_t before = C.iss.n_statements;
    if (refused(c, "advance", cn_advance(&C.iss, now))) return;
    uint32_t late = 0;
    for (uint32_t i = 0; i < CN_MAX_ACCOUNTS; i++)
        late += C.iss.accounts[i].used && C.iss.accounts[i].state == CN_ACCT_DELINQUENT;
    zxvd_buf_printf(zxvd_result(c), "{\"statements\":%u,\"new_statements\":%u,\"delinquent\":%u}",
                    (unsigned) C.iss.n_statements, (unsigned) (C.iss.n_statements - before),
                    (unsigned) late);
}

static void m_iso8583_parse(zxvd_call_t *c)
{
    uint32_t n;
    char field[1000];
    if (!zxvd_p_hex(c, "hex", C.wire, sizeof C.wire, &n, true)) return;
    if (cn8583_unpack(&C.msg, C.wire, n) != CN8583_OK)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"hex\" is not an ISO 8583 message this codec reads");
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_printf(b, "{\"mti\":\"%04u\",\"fields\":{", (unsigned) C.msg.mti);
    bool first = true;
    for (uint32_t f = 2; f <= 128; f++) {
        const char *d;
        if (cn8583_get(&C.msg, f, &d, &n) != CN8583_OK) continue;
        memcpy(field, d, n);
        field[n] = 0;
        zxvd_buf_printf(b, "%s\"%u\":", first ? "" : ",", (unsigned) f);
        zxvd_buf_json_str(b, field);
        first = false;
    }
    zxvd_buf_raw(b, "}}", 2);
}

static void cards_close(void)
{
    memset(&C, 0, sizeof C); /* K1: in memory only */
}

static const zxvd_group_method_t METHODS[] = {
    {"cards.status", m_status, true},
    {"cards.check_pan", m_check_pan, true},
    {"cards.iso8583_parse", m_iso8583_parse, true},
    {"cards.issuer", m_issuer, false},
    {"cards.open_account", m_open_account, false},
    {"cards.account", m_account, false},
    {"cards.issue", m_issue, false},
    {"cards.reissue", m_reissue, false},
    {"cards.freeze", m_freeze, false},
    {"cards.card", m_card, false},
    {"cards.challenge", m_challenge, false},
    {"cards.authorize", m_authorize, false},
    {"cards.capture", m_capture, false},
    {"cards.reverse", m_reverse, false},
    {"cards.refund", m_refund, false},
    {"cards.pay", m_pay, false},
    {"cards.advance", m_advance, false},
};

static const zxvd_group_t GROUP = {
    "cards",
    true,
    "Dragon/Phoenix/Thunderbird charge cards (kernel/src/cardnet): in memory, no scheme "
    "membership, no PCI DSS claim",
    METHODS,
    sizeof METHODS / sizeof METHODS[0],
    cards_close};

const zxvd_group_t *zxvd_cards_group(void)
{
    return &GROUP;
}

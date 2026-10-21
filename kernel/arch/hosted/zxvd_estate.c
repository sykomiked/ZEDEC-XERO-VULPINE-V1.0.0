/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_estate.c — barrel grants (grants.*, kernel/src/grants) and estate
 * succession with account recovery and faraid shares (estate.*, recovery.*,
 * kernel/src/estate) on zxvd's socket, each behind its own feature switch.
 *
 *   S1 grants is regulated (zxvd_ext.h X5): a grant moves pool Vino into a
 *      reserve and issues barrels. It never mints: a grant is paid only from
 *      the pool's balance (grants.h G3), and one the pool cannot buy a whole
 *      barrel for is refused (GRANT_ERR_EMPTY). The pool lives on a sandbox
 *      ledger (zxvd_sandbox.h C1); standing metrics are what the caller sends.
 *   S2 estate is not regulated here: the estate engine is bookkeeping and
 *      arithmetic, and moves nothing. Faraid and forced-heirship shares are a
 *      COMPUTATION, NEVER A RULING: the engine refuses to distribute such a
 *      manifest before a ruling signed by the gateway key, and only a
 *      qualified scholar or court can give that ruling.
 *   S3 recovery.* (recovery.h) sits behind the estate switch: it counts
 *      guardian approvals and old-device vetoes and reports the thaw limits;
 *      no account is moved by it, and guardian notices are counted, not
 *      delivered.
 *   S4 Signatures are ML-DSA-87 (zxvd_sandbox.h C2b), as the engines
 *      specify: grants.key registers a 2592-byte public key under the
 *      engine's numeric signer id; estate.key and recovery.key register one
 *      and answer its 32-byte fingerprint, which is the signer key the estate
 *      and recovery engines hold (estate.setup and recovery.setup take
 *      fingerprints; a list of them is one hex string, back to back). Each
 *      group has its own key table and FIPS 204 context ("zxvd.grants.v1",
 *      "zxvd.estate.v1", "zxvd.recovery.v1"), so a signature for one group
 *      verifies in no other; every "sig" is exactly 4627 bytes of hex, and
 *      the engines' own epochs, claim ids and sequence numbers stop replays.
 *      Time is "now" (zxvd_sandbox.h C3): seconds, so a recovery day is
 *      86400 ticks.
 */
#include "zxvd_groups.h"
#include "zxvd_sandbox.h"
#include "grants.h"
#include "estate.h"
#include "faraid.h"
#include "recovery.h"

#include <string.h>

/* ===================== grants.* ===================== */

static zxvd_sandbox_book_t GB = {
    .a = {{.name = "vino", .code = "VINO", .minor = 2, .fundable = true}, {.name = "barrel"}},
    .n_assets = 2};
static zxvd_sandbox_pqkeys_t GK = {.ctx = "zxvd.grants.v1"};
static grant_t GS;

static bool grant_verify(void *ctx, uint32_t key, const uint8_t *m, uint32_t len, const uint8_t *s,
                         uint32_t sl)
{
    return zxvd_sandbox_pq_verify(ctx, key, m, len, s, sl);
}

static void gr_refuse(zxvd_call_t *c, grant_status_t st)
{
    static const char *const N[][2] = {
        {"GRANT_OK", ""},
        {"GRANT_ERR_ARG", "a bad argument (an unusable account, a zero weight or price)"},
        {"GRANT_ERR_FULL", "a table is full"},
        {"GRANT_ERR_SIG", "the signature does not verify, or the underwriter is not listed"},
        {"GRANT_ERR_UNBACKED", "the lot has no underwriter attestation"},
        {"GRANT_ERR_EXPIRED", "the attestation has expired"},
        {"GRANT_ERR_DISPUTED", "the attestation is disputed"},
        {"GRANT_ERR_LEDGER", "the ledger refused the posting"},
        {"GRANT_ERR_EMPTY", "the pool cannot pay for one whole barrel: grants are never minted"},
        {"GRANT_ERR_FUNDS", "the holder lacks the barrels, or the pool the uncounted Vino"},
        {"GRANT_ERR_OVERFLOW", "a value does not fit"},
        {"GRANT_ERR_EPOCH", "the epoch or expiry is not above the last"},
        {"GRANT_ERR_STATE", "this epoch has distributed already, or the lot is attested"},
        {"GRANT_ERR_INELIGIBLE", "a recipient is not in good standing"},
        {"GRANT_ERR_NOT_FOUND", "no such lot or epoch"},
    };
    uint32_t i = (uint32_t) -st;
    if (i >= sizeof N / sizeof N[0]) i = 1;
    zxvd_sandbox_refuse(c, N[i][0], N[i][1]);
}

static bool grant_ready(zxvd_call_t *c)
{
    return zxvd_sandbox_ready(c, &GB, "grants");
}

static void m_grant_setup(zxvd_call_t *c)
{
    if (GB.open) return zxvd_sandbox_refuse(c, "SET_UP", "grants are set up already (this run)");
    uint32_t funnel, bps;
    uint64_t vol, stake, cn, cd;
    if (!zxvd_sandbox_need_u32(c, "funnel", &funnel) ||
        !zxvd_sandbox_u32(c, "min_uptime_bps", false, GRANT_DEF_UPTIME_BPS, &bps) ||
        !zxvd_sandbox_u64(c, "min_volume", false, GRANT_DEF_MIN_VOLUME, &vol) ||
        !zxvd_sandbox_u64(c, "min_stake", false, GRANT_DEF_MIN_STAKE, &stake) ||
        !zxvd_sandbox_u64(c, "cap_num", false, 1, &cn) ||
        !zxvd_sandbox_u64(c, "cap_den", false, 1, &cd))
        return;
    int32_t nu = zxvd_p_len(c, "underwriters");
    if (nu < -1) return;
    if (nu > (int32_t) GRANT_MAX_UNDERW)
        return zxvd_fail(c, ZXVD_E_PARAMS, "at most %u underwriters", (unsigned) GRANT_MAX_UNDERW);
    int64_t uw[GRANT_MAX_UNDERW];
    for (int32_t i = 0; i < nu; i++) {
        if (!zxvd_p_i64_at(c, "underwriters", (uint32_t) i, &uw[i])) return;
        if (uw[i] < 0 || uw[i] > (int64_t) UINT32_MAX)
            return zxvd_fail(c, ZXVD_E_PARAMS, "\"underwriters\"[%d] is out of range", (int) i);
    }
    const grant_thresholds_t th = {bps, vol, stake};
    static const uint8_t seed[32] = "zxvd grants sandbox seed, v1...";
    if (zxvd_sandbox_book_open(&GB) != 0)
        return zxvd_sandbox_refuse(c, "PAY_ERR", "the sandbox ledger refused its assets");
    GB.open = false;
    grant_status_t st =
        grant_init(&GS, &GB.L, GB.a[0].asset, ZXVD_SANDBOX_MODULE, funnel, grant_verify, &GK, seed);
    if (st == GRANT_OK) st = grant_set_thresholds(&GS, &th);
    if (st == GRANT_OK) st = grant_set_cap(&GS, cn, cd);
    for (int32_t i = 0; st == GRANT_OK && i < nu; i++)
        st = grant_underwriter_add(&GS, (uint32_t) uw[i]);
    if (st != GRANT_OK) return gr_refuse(c, st);
    GB.a[1].asset = GS.barrel;
    GB.open = true;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o,
                    "{\"funnel\":%u,\"pool\":%u,\"reserve\":%u,\"issuer\":%u,\"underwriters\":%u,"
                    "\"min_uptime_bps\":%u",
                    (unsigned) funnel, (unsigned) GS.pool, (unsigned) GS.reserve,
                    (unsigned) GS.issuer, (unsigned) GS.n_underw, (unsigned) bps);
    zxvd_sandbox_kv(o, ",\"min_volume\":", vol);
    zxvd_sandbox_kv(o, ",\"min_stake\":", stake);
    zxvd_sandbox_kv(o, ",\"cap_num\":", cn);
    zxvd_sandbox_kv(o, ",\"cap_den\":", cd);
    ZXVD_RAW(o, ",\"minted\":false}");
}

static void m_grant_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_pqkey(c, &GK, true);
}
static void m_grant_account_open(zxvd_call_t *c)
{
    if (grant_ready(c)) zxvd_sandbox_m_account_open(c, &GB);
}
static void m_grant_fund(zxvd_call_t *c)
{
    if (grant_ready(c)) zxvd_sandbox_m_fund(c, &GB);
}
static void m_grant_account(zxvd_call_t *c)
{
    if (grant_ready(c)) zxvd_sandbox_m_account(c, &GB);
}

static void m_grant_epoch(zxvd_call_t *c)
{
    uint64_t e;
    if (!grant_ready(c) || !zxvd_sandbox_need_u64(c, "epoch", &e)) return;
    grant_status_t st = grant_epoch_begin(&GS, e);
    if (st != GRANT_OK) return gr_refuse(c, st);
    zxvd_sandbox_kv(zxvd_result(c), "{\"epoch\":", e);
    ZXVD_RAW(zxvd_result(c), "}");
}

static void m_grant_pool_credit(zxvd_call_t *c)
{
    uint32_t from;
    uint64_t amt, now;
    if (!grant_ready(c) || !zxvd_sandbox_need_u32(c, "from", &from) ||
        !zxvd_sandbox_need_u64(c, "amount_minor", &amt) || !zxvd_sandbox_now(c, &now))
        return;
    grant_status_t st = grant_pool_credit(&GS, from, amt, now);
    if (st != GRANT_OK) return gr_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_sandbox_kv(o, "{\"pool_minor\":", pay_ledger_account(&GB.L, GS.pool)->debit);
    zxvd_sandbox_kv(o, ",\"inflow_minor\":", GS.hist[GS.epoch & (GRANT_HIST - 1u)].inflow);
    ZXVD_RAW(o, "}");
}

static void m_grant_lot_open(zxvd_call_t *c)
{
    uint32_t lot;
    if (!grant_ready(c) || !zxvd_sandbox_need_u32(c, "lot", &lot)) return;
    grant_status_t st = grant_lot_open(&GS, lot);
    if (st != GRANT_OK) return gr_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"lot\":%u,\"label\":\"%s\",\"status\":\"%s\"}",
                    (unsigned) lot, GRANT_LABEL_UNBACKED, GRANT_STATUS_UNBACKED);
}

/* the standing metrics of one subject (G2), from the current object */
static bool metrics(zxvd_call_t *c, grant_metrics_t *m)
{
    memset(m, 0, sizeof *m);
    return zxvd_sandbox_need_u32(c, "subject", &m->subject) &&
           zxvd_sandbox_need_u32(c, "uptime_bps", &m->uptime_bps) &&
           zxvd_sandbox_u32(c, "dropped_reports", false, 0, &m->dropped_reports) &&
           zxvd_sandbox_u32(c, "routing_reports", false, 0, &m->routing_reports) &&
           zxvd_sandbox_u32(c, "failed_challenges", false, 0, &m->failed_challenges) &&
           zxvd_sandbox_u32(c, "fraud_findings", false, 0, &m->fraud_findings) &&
           zxvd_sandbox_need_u64(c, "volume", &m->volume) &&
           zxvd_sandbox_need_u64(c, "stake", &m->stake);
}

static void put_reasons(zxvd_buf_t *o, uint32_t r)
{
    static const char *const R[] = {"uptime", "dropped", "routing", "audit",
                                    "fraud",  "volume",  "stake",   "invalid"};
    ZXVD_RAW(o, "[");
    bool first = true;
    for (uint32_t i = 0; i < 8; i++)
        if (r & (1u << i)) {
            zxvd_buf_printf(o, "%s\"%s\"", first ? "" : ",", R[i]);
            first = false;
        }
    ZXVD_RAW(o, "]");
}

static void m_grant_eval(zxvd_call_t *c)
{
    grant_metrics_t m;
    uint32_t r = 0;
    if (!grant_ready(c) || !metrics(c, &m)) return;
    bool ok = grant_eval(&GS, &m, GS.epoch, &r);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"subject\":%u,\"eligible\":%s,\"mask\":%u,\"reasons\":",
                    (unsigned) m.subject, ok ? "true" : "false", (unsigned) r);
    put_reasons(o, r);
    zxvd_sandbox_kv(o, ",\"epoch\":", GS.epoch);
    ZXVD_RAW(o, "}");
}

static void m_grant_appeal(zxvd_call_t *c)
{
    uint32_t subject, mask;
    uint64_t epoch;
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!grant_ready(c) || !zxvd_sandbox_need_u32(c, "subject", &subject) ||
        !zxvd_sandbox_need_u64(c, "epoch", &epoch) || !zxvd_sandbox_need_u32(c, "mask", &mask) ||
        !zxvd_sandbox_pq_sig(c, "sig", sig))
        return;
    grant_status_t st = grant_appeal(&GS, subject, epoch, mask, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != GRANT_OK) return gr_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"subject\":%u,\"mask\":%u,\"epoch\":", (unsigned) subject,
                    (unsigned) mask);
    zxvd_sandbox_put_u64(zxvd_result(c), epoch);
    ZXVD_RAW(zxvd_result(c), "}");
}

static void m_grant_distribute(zxvd_call_t *c)
{
    uint32_t lot;
    uint64_t price, now;
    if (!grant_ready(c) || !zxvd_sandbox_need_u32(c, "lot", &lot) ||
        !zxvd_sandbox_need_u64(c, "price", &price) || !zxvd_sandbox_now(c, &now))
        return;
    int32_t n = zxvd_p_len(c, "recipients");
    if (n < -1) return;
    if (n < 1 || n > (int32_t) GRANT_MAX_RECIP)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"recipients\" must hold 1..%u objects",
                         (unsigned) GRANT_MAX_RECIP);
    static grant_recipient_t r[GRANT_MAX_RECIP];
    for (int32_t i = 0; i < n; i++) {
        int32_t saved;
        if (!zxvd_p_enter(c, "recipients", (uint32_t) i, &saved)) return;
        bool ok = zxvd_sandbox_need_u32(c, "account", &r[i].account) &&
                  zxvd_sandbox_need_u64(c, "weight", &r[i].weight) && metrics(c, &r[i].m);
        zxvd_p_leave(c, saved);
        if (!ok) return;
    }
    grant_dist_t d;
    grant_status_t st = grant_epoch_distribute(&GS, lot, r, (uint32_t) n, price, now, &d);
    if (st == GRANT_ERR_INELIGIBLE) {
        uint32_t why = 0;
        grant_eval(&GS, &r[d.bad].m, GS.epoch, &why);
        zxvd_fail(c, ZXVD_E_ENGINE,
                  "GRANT_ERR_INELIGIBLE: recipient %u (subject %u) is not in good standing (mask "
                  "%u); nothing was granted",
                  (unsigned) d.bad, (unsigned) r[d.bad].m.subject, (unsigned) why);
        return;
    }
    if (st != GRANT_OK) return gr_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_sandbox_kv(o, "{\"barrels\":", d.barrels);
    zxvd_sandbox_kv(o, ",\"cost_minor\":", d.cost);
    zxvd_buf_printf(o, ",\"postings\":%u,\"shares\":[", (unsigned) d.postings);
    for (int32_t i = 0; i < n; i++) {
        zxvd_buf_printf(o, "%s{\"account\":%u,\"barrels\":", i ? "," : "", (unsigned) r[i].account);
        zxvd_sandbox_put_u64(o, d.share[i]);
        ZXVD_RAW(o, "}");
    }
    zxvd_sandbox_kv(o, "],\"pool_minor\":", pay_ledger_account(&GB.L, GS.pool)->debit);
    zxvd_buf_printf(o, ",\"label\":\"%s\"}", GRANT_LABEL_UNBACKED);
}

static void m_grant_attest(zxvd_call_t *c)
{
    uint32_t lot, uw;
    uint64_t count, expiry, now;
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!grant_ready(c) || !zxvd_sandbox_need_u32(c, "lot", &lot) ||
        !zxvd_sandbox_need_u64(c, "count", &count) ||
        !zxvd_sandbox_need_u32(c, "underwriter", &uw) ||
        !zxvd_sandbox_need_u64(c, "expiry", &expiry) || !zxvd_sandbox_pq_sig(c, "sig", sig) ||
        !zxvd_sandbox_now(c, &now))
        return;
    grant_status_t st = grant_attest(&GS, lot, count, uw, expiry, now, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != GRANT_OK) return gr_refuse(c, st);
    const char *status;
    const char *label = grant_label(&GS, lot, now, &status);
    zxvd_buf_printf(zxvd_result(c),
                    "{\"lot\":%u,\"backed\":true,\"label\":\"%s\",\"status\":\"%s\"}",
                    (unsigned) lot, label, status);
}

static void m_grant_dispute(zxvd_call_t *c)
{
    uint32_t lot;
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!grant_ready(c) || !zxvd_sandbox_need_u32(c, "lot", &lot) ||
        !zxvd_sandbox_pq_sig(c, "sig", sig))
        return;
    grant_status_t st = grant_dispute(&GS, lot, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != GRANT_OK) return gr_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"lot\":%u,\"backed\":false,\"disputed\":true}",
                    (unsigned) lot);
}

static void m_grant_redeem(zxvd_call_t *c)
{
    uint32_t holder, vino, lot;
    uint64_t n, price, now;
    if (!grant_ready(c) || !zxvd_sandbox_need_u32(c, "holder", &holder) ||
        !zxvd_sandbox_need_u32(c, "holder_vino", &vino) || !zxvd_sandbox_need_u32(c, "lot", &lot) ||
        !zxvd_sandbox_need_u64(c, "n", &n) || !zxvd_sandbox_need_u64(c, "price", &price) ||
        !zxvd_sandbox_now(c, &now))
        return;
    grant_status_t st = grant_redeem(&GS, holder, vino, lot, n, price, now);
    if (st != GRANT_OK) return gr_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"lot\":%u", (unsigned) lot);
    zxvd_sandbox_kv(o, ",\"barrels\":", n);
    zxvd_sandbox_kv(o, ",\"paid_minor\":", n * price);
    zxvd_sandbox_kv(o, ",\"held\":", grant_holding(&GS, holder, lot));
    ZXVD_RAW(o, "}");
}

static void m_grant_message(zxvd_call_t *c)
{
    char kind[16];
    if (!grant_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    if (!strcmp(kind, "appeal")) {
        uint32_t subject, mask;
        uint64_t epoch;
        uint8_t m[GRANT_APPEAL_MSG];
        if (!zxvd_sandbox_need_u32(c, "subject", &subject) ||
            !zxvd_sandbox_need_u64(c, "epoch", &epoch) || !zxvd_sandbox_need_u32(c, "mask", &mask))
            return;
        grant_appeal_msg(subject, epoch, mask, m);
        return zxvd_sandbox_put_msg_pq(c, &GK, m, sizeof m);
    }
    if (!strcmp(kind, "attest")) {
        uint32_t lot, uw;
        uint64_t count, expiry;
        uint8_t m[GRANT_ATTEST_MSG];
        if (!zxvd_sandbox_need_u32(c, "lot", &lot) || !zxvd_sandbox_need_u64(c, "count", &count) ||
            !zxvd_sandbox_need_u32(c, "underwriter", &uw) ||
            !zxvd_sandbox_need_u64(c, "expiry", &expiry))
            return;
        grant_attest_msg(lot, count, uw, expiry, m);
        return zxvd_sandbox_put_msg_pq(c, &GK, m, sizeof m);
    }
    if (!strcmp(kind, "dispute")) {
        uint32_t lot;
        uint8_t m[GRANT_DISPUTE_MSG];
        if (!zxvd_sandbox_need_u32(c, "lot", &lot)) return;
        const grant_lot_t *l = grant_lot(&GS, lot);
        if (!l) return gr_refuse(c, GRANT_ERR_NOT_FOUND);
        grant_dispute_msg(lot, l->expiry, m);
        return zxvd_sandbox_put_msg_pq(c, &GK, m, sizeof m);
    }
    zxvd_fail(c, ZXVD_E_PARAMS, "\"kind\" must be \"appeal\", \"attest\" or \"dispute\"");
}

static void m_grant_status(zxvd_call_t *c)
{
    uint64_t now;
    if (!zxvd_sandbox_now(c, &now)) return;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s,\"keys\":%u", GB.open ? "true" : "false", (unsigned) GK.n);
    if (GB.open) {
        grant_snapshot_t s;
        grant_snapshot(&GS, now, &s);
        zxvd_sandbox_kv(o, ",\"epoch\":", s.epoch);
        zxvd_sandbox_kv(o, ",\"inflow_minor\":", s.inflow);
        zxvd_sandbox_kv(o, ",\"weight\":", s.weight);
        zxvd_buf_printf(o, ",\"rate_defined\":%s", s.rate_defined ? "true" : "false");
        zxvd_sandbox_kv(o, ",\"rate_num\":", s.rate_num);
        zxvd_sandbox_kv(o, ",\"rate_den\":", s.rate_den);
        zxvd_sandbox_kv(o, ",\"granted_barrels\":", s.granted_barrels);
        zxvd_sandbox_kv(o, ",\"granted_cost_minor\":", s.granted_cost);
        zxvd_sandbox_kv(o, ",\"total_barrels_granted\":", s.total_barrels_granted);
        zxvd_sandbox_kv(o, ",\"pool_minor\":", s.pool_balance);
        zxvd_sandbox_kv(o, ",\"reserve_minor\":", s.reserve_balance);
        zxvd_sandbox_kv(o, ",\"barrels_outstanding\":", s.barrels_outstanding);
        zxvd_sandbox_kv(o, ",\"barrels_backed\":", s.barrels_backed);
        zxvd_sandbox_kv(o, ",\"barrels_unbacked\":", s.barrels_unbacked);
        zxvd_buf_printf(o, ",\"lots\":[");
        bool first = true;
        for (uint32_t i = 0; i < GRANT_MAX_LOTS; i++) {
            const grant_lot_t *l = &GS.lot[i];
            if (!l->used) continue;
            const char *status, *label = grant_label(&GS, l->id, now, &status);
            zxvd_buf_printf(o, "%s{\"lot\":%u,\"count\":", first ? "" : ",", (unsigned) l->id);
            zxvd_sandbox_put_u64(o, l->count);
            zxvd_buf_printf(o, ",\"backed\":%s,\"label\":\"%s\",\"status\":\"%s\"}",
                            grant_backed(&GS, l->id, now) ? "true" : "false", label, status);
            first = false;
        }
        zxvd_buf_printf(o, "],\"check\":%s", grant_check(&GS) ? "true" : "false");
    }
    ZXVD_RAW(o, ",\"minted\":false,\"sandbox\":true}");
}

static void grant_close(void)
{
    GB.open = false;
    GK.n = 0;
    memset(&GS, 0, sizeof GS);
}

static const zxvd_group_method_t GRANT_M[] = {
    {"grants.setup", m_grant_setup, false},
    {"grants.key", m_grant_key, false},
    {"grants.account_open", m_grant_account_open, false},
    {"grants.fund", m_grant_fund, false},
    {"grants.account", m_grant_account, true},
    {"grants.epoch", m_grant_epoch, false},
    {"grants.pool_credit", m_grant_pool_credit, false},
    {"grants.lot_open", m_grant_lot_open, false},
    {"grants.eval", m_grant_eval, false},
    {"grants.appeal", m_grant_appeal, false},
    {"grants.distribute", m_grant_distribute, false},
    {"grants.attest", m_grant_attest, false},
    {"grants.dispute", m_grant_dispute, false},
    {"grants.redeem", m_grant_redeem, false},
    {"grants.message", m_grant_message, true},
    {"grants.status", m_grant_status, true},
};

const zxvd_group_t *zxvd_grants_group(void)
{
    static const zxvd_group_t G = {
        "grants",
        true,
        "barrel grants (grants.h): paid only from the funded pool, never minted; virtual "
        "units until an underwriter attests; sandbox ledger in memory",
        GRANT_M,
        sizeof GRANT_M / sizeof GRANT_M[0],
        grant_close};
    return &G;
}

/* ===================== estate.* ===================== */

static est_t ES;
static bool ES_SET;
static est_forced_table_t EFT;
static zxvd_sandbox_pqkeys_t EK = {.ctx = "zxvd.estate.v1"};

/* S4: ctx is the estate key table (NULL: the faraid scratch estate, which signs nothing) */
static bool est_verify_cb(void *ctx, const uint8_t signer[EST_KEY], const uint8_t *m, uint32_t len,
                          const uint8_t *s, uint32_t sl)
{
    return ctx && zxvd_sandbox_pq_verify_fp(ctx, signer, m, len, s, sl);
}

static void es_refuse(zxvd_call_t *c, est_status_t st)
{
    static const char *const N[][2] = {
        {"EST_OK", ""},
        {"EST_ERR_ARG", "a bad argument"},
        {"EST_ERR_SIG", "the signature does not verify"},
        {"EST_ERR_STATE", "not allowed in this phase (or bond or contest state)"},
        {"EST_ERR_QUORUM", "fewer guardian approvals than k"},
        {"EST_ERR_TIME", "the waiting period is not over"},
        {"EST_ERR_SUM", "the shares do not sum to exactly 1"},
        {"EST_ERR_OVERFLOW", "an exact value does not fit 64 bits"},
        {"EST_ERR_UNSUPPORTED",
         "relatives or a residue this computation does not model: ask a qualified scholar"},
        {"EST_ERR_NOMANIFEST", "no manifest is set"},
        {"EST_ERR_FULL", "too many heirs"},
        {"EST_ERR_UNKNOWN", "no forced-heirship row for that jurisdiction"},
    };
    if (st == EST_DUP)
        return zxvd_sandbox_refuse(c, "EST_DUP", "already recorded: nothing changed");
    uint32_t i = (uint32_t) -st;
    if (i >= sizeof N / sizeof N[0]) i = 1;
    zxvd_sandbox_refuse(c, N[i][0], N[i][1]);
}

static bool est_ready(zxvd_call_t *c)
{
    if (ES_SET) return true;
    zxvd_fail(c, ZXVD_E_ENGINE, "the estate is not set up: call estate.setup first");
    return false;
}

/* S4: n keys back to back; *n gets the count (1..max) */
static bool key_list(zxvd_call_t *c, const char *key, uint8_t (*out)[32], uint32_t max, uint32_t *n)
{
    uint8_t buf[8 * 32];
    uint32_t len;
    if (!zxvd_p_hex(c, key, buf, max * 32, &len, true)) return false;
    if (len == 0 || len % 32) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be 1..%u keys of 32 bytes, back to back", key,
                  (unsigned) max);
        return false;
    }
    *n = len / 32;
    memcpy(out, buf, len);
    return true;
}

static void m_est_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_pqkey(c, &EK, false);
}

static void m_est_setup(zxvd_call_t *c)
{
    if (ES_SET) return zxvd_sandbox_refuse(c, "SET_UP", "the estate is set up already (this run)");
    est_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    if (!zxvd_sandbox_key32(c, "owner", cfg.owner) ||
        !key_list(c, "guardians", cfg.guardian, EST_MAX_GUARDIANS, &cfg.n_guardians) ||
        !zxvd_sandbox_need_u32(c, "k", &cfg.k) ||
        !key_list(c, "attestors", cfg.attestor, EST_MAX_ATTESTORS, &cfg.n_attestors) ||
        !zxvd_sandbox_key32(c, "court", cfg.court) ||
        !zxvd_sandbox_key32(c, "gateway", cfg.gateway) ||
        !zxvd_sandbox_u64(c, "wait", false, 0, &cfg.wait_ticks) ||
        !zxvd_sandbox_u64(c, "release", false, 0, &cfg.release_ticks))
        return;
    est_status_t st = est_init(&ES, &cfg, est_verify_cb, &EK);
    if (st != EST_OK) return es_refuse(c, st);
    est_forced_init(&EFT);
    ES_SET = true;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"guardians\":%u,\"k\":%u,\"attestors\":%u", (unsigned) cfg.n_guardians,
                    (unsigned) cfg.k, (unsigned) cfg.n_attestors);
    zxvd_sandbox_kv(o, ",\"wait\":", cfg.wait_ticks);
    zxvd_sandbox_kv(o, ",\"release\":", cfg.release_ticks);
    ZXVD_RAW(o, ",\"phase\":\"alive\"}");
}

static const char *phase_name(est_phase_t p)
{
    static const char *const N[] = {"alive", "reported",     "confirmed",
                                    "ruled", "distributing", "closed"};
    return (unsigned) p < 6 ? N[p] : "?";
}

static void put_phase(zxvd_call_t *c)
{
    zxvd_buf_printf(zxvd_result(c), "{\"phase\":\"%s\",\"epoch\":", phase_name(ES.phase));
    zxvd_sandbox_put_u64(zxvd_result(c), ES.epoch);
    ZXVD_RAW(zxvd_result(c), "}");
}

/* the shares of a faraid estate (faraid.h F1): flags and counts */
static bool faraid_heirs(zxvd_call_t *c, est_faraid_heirs_t *h)
{
    memset(h, 0, sizeof *h);
    return zxvd_p_bool(c, "husband", false, &h->husband) &&
           zxvd_sandbox_u32(c, "wives", false, 0, &h->wives) &&
           zxvd_p_bool(c, "father", false, &h->father) &&
           zxvd_p_bool(c, "mother", false, &h->mother) &&
           zxvd_sandbox_u32(c, "sons", false, 0, &h->sons) &&
           zxvd_sandbox_u32(c, "daughters", false, 0, &h->daughters) &&
           zxvd_p_bool(c, "others", false, &h->others);
}

/* ids from an array of integers; n must match `want` when want > 0 */
static bool id_list(zxvd_call_t *c, const char *key, uint32_t *id, uint32_t want, uint32_t *n)
{
    int32_t len = zxvd_p_len(c, key);
    if (len < -1) return false;
    if (len < 0) len = 0;
    if ((want && (uint32_t) len != want) || len > (int32_t) EST_MAX_HEIRS) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must hold %u ids", key,
                  (unsigned) (want ? want : EST_MAX_HEIRS));
        return false;
    }
    for (int32_t i = 0; i < len; i++) {
        int64_t v;
        if (!zxvd_p_i64_at(c, key, (uint32_t) i, &v)) return false;
        if (v < 0 || v > (int64_t) UINT32_MAX) {
            zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\"[%d] is out of range", key, (int) i);
            return false;
        }
        id[i] = (uint32_t) v;
    }
    *n = (uint32_t) len;
    return true;
}

static void put_frac(zxvd_buf_t *o, uint64_t num, uint64_t den)
{
    zxvd_sandbox_kv(o, "\"num\":", num);
    zxvd_sandbox_kv(o, ",\"den\":", den);
}

static void m_est_manifest(zxvd_call_t *c)
{
    char enc[8];
    if (!est_ready(c) || !zxvd_p_str(c, "enclave", enc, sizeof enc, true)) return;
    est_status_t st;
    if (!strcmp(enc, "common")) {
        int32_t n = zxvd_p_len(c, "heirs");
        if (n < -1) return;
        if (n < 1 || n > (int32_t) EST_MAX_HEIRS)
            return zxvd_fail(c, ZXVD_E_PARAMS, "\"heirs\" must hold 1..%u objects",
                             (unsigned) EST_MAX_HEIRS);
        uint32_t id[EST_MAX_HEIRS];
        uint64_t num[EST_MAX_HEIRS], den[EST_MAX_HEIRS];
        for (int32_t i = 0; i < n; i++) {
            int32_t saved;
            if (!zxvd_p_enter(c, "heirs", (uint32_t) i, &saved)) return;
            bool ok = zxvd_sandbox_need_u32(c, "id", &id[i]) &&
                      zxvd_sandbox_need_u64(c, "num", &num[i]) &&
                      zxvd_sandbox_need_u64(c, "den", &den[i]);
            zxvd_p_leave(c, saved);
            if (!ok) return;
        }
        st = est_set_manifest(&ES, EST_COMMON, (uint32_t) n, id, num, den);
    } else if (!strcmp(enc, "faraid")) {
        est_faraid_heirs_t h;
        uint32_t id[EST_MAX_HEIRS], n;
        if (!faraid_heirs(c, &h)) return;
        uint32_t want = (h.husband ? 1u : 0u) + h.wives + (h.father ? 1u : 0u) +
                        (h.mother ? 1u : 0u) + h.sons + h.daughters;
        if (want > EST_MAX_HEIRS) return es_refuse(c, EST_ERR_FULL);
        if (!id_list(c, "ids", id, want ? want : 1, &n)) return;
        st = est_set_faraid(&ES, &h, id);
    } else if (!strcmp(enc, "forced")) {
        char jur[4];
        uint32_t cid[EST_MAX_HEIRS], gid[EST_MAX_HEIRS], nc, ng;
        uint64_t gnum[EST_MAX_HEIRS], wden;
        if (!zxvd_p_str(c, "jurisdiction", jur, sizeof jur, true) ||
            !id_list(c, "children", cid, 0, &nc) || !zxvd_sandbox_need_u64(c, "will_den", &wden))
            return;
        if (strlen(jur) != 2)
            return zxvd_fail(c, ZXVD_E_PARAMS, "\"jurisdiction\" must be two letters");
        int32_t n = zxvd_p_len(c, "gifts");
        if (n < -1) return;
        ng = n < 0 ? 0 : (uint32_t) n;
        if (ng > EST_MAX_HEIRS) return es_refuse(c, EST_ERR_FULL);
        for (uint32_t i = 0; i < ng; i++) {
            int32_t saved;
            if (!zxvd_p_enter(c, "gifts", i, &saved)) return;
            bool ok = zxvd_sandbox_need_u32(c, "id", &gid[i]) &&
                      zxvd_sandbox_need_u64(c, "num", &gnum[i]);
            zxvd_p_leave(c, saved);
            if (!ok) return;
        }
        st = est_set_forced(&ES, &EFT, EST_JUR(jur[0], jur[1]), nc, cid, ng, gid, gnum, wden);
    } else {
        return zxvd_fail(c, ZXVD_E_PARAMS,
                         "\"enclave\" must be \"common\", \"faraid\" or \"forced\"");
    }
    if (st != EST_OK) return es_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"enclave\":\"%s\",\"advisory\":%s,\"heirs\":[", enc,
                    est_advisory(&ES) ? "true" : "false");
    for (uint32_t i = 0; i < ES.n; i++) {
        zxvd_buf_printf(o, "%s{\"id\":%u,", i ? "," : "", (unsigned) ES.id[i]);
        put_frac(o, ES.num[i], ES.den);
        ZXVD_RAW(o, "}");
    }
    ZXVD_RAW(o, "],\"digest\":");
    zxvd_buf_hex(o, ES.digest, 32);
    ZXVD_RAW(o, "}");
}

/* faraid (faraid.h): a computation, never a ruling */
static void m_est_faraid(zxvd_call_t *c)
{
    static const char *const CL[] = {"husband", "wives", "father", "mother", "sons", "daughters"};
    est_faraid_heirs_t h;
    est_faraid_t f;
    uint64_t amount = 0;
    if (!faraid_heirs(c, &h)) return;
    bool split = zxvd_p_has(c, "amount_minor");
    if (split && !zxvd_sandbox_need_u64(c, "amount_minor", &amount)) return;
    est_status_t st = est_faraid(&h, &f);
    if (st != EST_OK) return es_refuse(c, st);
    uint32_t cnt[EST_FA_REST] = {h.husband ? 1u : 0u, h.wives, h.father ? 1u : 0u,
                                 h.mother ? 1u : 0u,  h.sons,  h.daughters};
    /* the exact split, with the engine's own largest-remainder rule, on a
     * scratch estate that is never set up for anything else */
    static est_t tmp;
    uint64_t part[EST_MAX_HEIRS] = {0};
    if (split) {
        static const est_cfg_t one = {.n_guardians = 1, .k = 1, .n_attestors = 1};
        uint32_t id[EST_MAX_HEIRS] = {0};
        est_init(&tmp, &one, est_verify_cb, NULL);
        st = est_set_faraid(&tmp, &h, id);
        if (st != EST_OK) return es_refuse(c, st);
        tmp.phase = EST_DISTRIBUTING;
        est_distribute(&tmp, amount, part, NULL);
    }
    zxvd_buf_t *o = zxvd_result(c);
    ZXVD_RAW(o, "{\"classes\":[");
    bool first = true;
    uint32_t k = 0;
    for (uint32_t i = 0; i < EST_FA_REST; i++) {
        if (!cnt[i]) continue;
        zxvd_buf_printf(o, "%s{\"class\":\"%s\",\"persons\":%u,", first ? "" : ",", CL[i],
                        (unsigned) cnt[i]);
        put_frac(o, f.share[i].num, f.share[i].den);
        if (split) {
            ZXVD_RAW(o, ",\"each_minor\":[");
            for (uint32_t j = 0; j < cnt[i]; j++, k++) {
                if (j) ZXVD_RAW(o, ",");
                zxvd_sandbox_put_u64(o, part[k]);
            }
            ZXVD_RAW(o, "]");
        }
        ZXVD_RAW(o, "}");
        first = false;
    }
    ZXVD_RAW(o, "],\"unallocated\":{");
    put_frac(o, f.share[EST_FA_REST].num, f.share[EST_FA_REST].den);
    zxvd_buf_printf(o, "},\"awl\":%s,\"radd\":%s", f.awl ? "true" : "false",
                    f.radd ? "true" : "false");
    if (split) zxvd_sandbox_kv(o, ",\"amount_minor\":", amount);
    ZXVD_RAW(o, ",\"advisory\":true,\"ruling\":false,\"notice\":\"a computation of one common "
                "majority treatment, not a ruling: a qualified scholar or court decides\"}");
}

static void m_est_report(zxvd_call_t *c)
{
    uint32_t who;
    uint64_t bond, now;
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!est_ready(c) || !zxvd_sandbox_need_u32(c, "attestor", &who) ||
        !zxvd_sandbox_need_u64(c, "bond_minor", &bond) || !zxvd_sandbox_pq_sig(c, "sig", sig) ||
        !zxvd_sandbox_now(c, &now))
        return;
    est_status_t st = est_report(&ES, who, bond, now, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != EST_OK) return es_refuse(c, st);
    put_phase(c);
}

static void m_est_approve(zxvd_call_t *c)
{
    uint32_t g;
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!est_ready(c) || !zxvd_sandbox_need_u32(c, "guardian", &g) ||
        !zxvd_sandbox_pq_sig(c, "sig", sig))
        return;
    est_status_t st = est_approve(&ES, g, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != EST_OK) return es_refuse(c, st);
    uint32_t n = 0;
    for (uint32_t x = ES.approved; x; x &= x - 1) n++;
    zxvd_buf_printf(zxvd_result(c), "{\"approvals\":%u,\"k\":%u}", (unsigned) n,
                    (unsigned) ES.cfg.k);
}

static void m_est_confirm(zxvd_call_t *c)
{
    uint64_t now;
    if (!est_ready(c) || !zxvd_sandbox_now(c, &now)) return;
    est_status_t st = est_confirm(&ES, now);
    if (st != EST_OK) return es_refuse(c, st);
    put_phase(c);
}

/* rule and veto: one signature over the current state */
static void m_est_rule(zxvd_call_t *c)
{
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!est_ready(c) || !zxvd_sandbox_pq_sig(c, "sig", sig)) return;
    est_status_t st = est_rule(&ES, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != EST_OK) return es_refuse(c, st);
    put_phase(c);
}

static void m_est_veto(zxvd_call_t *c)
{
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!est_ready(c) || !zxvd_sandbox_pq_sig(c, "sig", sig)) return;
    est_status_t st = est_veto(&ES, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != EST_OK) return es_refuse(c, st);
    put_phase(c);
}

static void m_est_begin(zxvd_call_t *c)
{
    if (!est_ready(c)) return;
    est_status_t st = est_begin(&ES);
    if (st != EST_OK) return es_refuse(c, st);
    put_phase(c);
}

static void m_est_close(zxvd_call_t *c)
{
    if (!est_ready(c)) return;
    est_status_t st = est_close(&ES);
    if (st != EST_OK) return es_refuse(c, st);
    put_phase(c);
}

static void m_est_fraud(zxvd_call_t *c)
{
    uint32_t who;
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!est_ready(c) || !zxvd_sandbox_need_u32(c, "who", &who) ||
        !zxvd_sandbox_pq_sig(c, "sig", sig))
        return;
    est_status_t st = est_fraud_finding(&ES, who, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != EST_OK) return es_refuse(c, st);
    ZXVD_RAW(zxvd_result(c), "{\"bond\":\"forfeited\"}");
}

static void m_est_contest(zxvd_call_t *c)
{
    uint32_t heir, who;
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!est_ready(c) || !zxvd_sandbox_need_u32(c, "heir", &heir) ||
        !zxvd_sandbox_need_u32(c, "who", &who) || !zxvd_sandbox_pq_sig(c, "sig", sig))
        return;
    est_status_t st = est_contest(&ES, heir, who, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != EST_OK) return es_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"heir\":%u,\"contested\":true}", (unsigned) heir);
}

static void m_est_resolve(zxvd_call_t *c)
{
    uint32_t heir;
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    uint64_t rel = 0;
    if (!est_ready(c) || !zxvd_sandbox_need_u32(c, "heir", &heir) ||
        !zxvd_sandbox_pq_sig(c, "sig", sig))
        return;
    est_status_t st = est_resolve(&ES, heir, sig, ZXVD_SANDBOX_PQ_SIG, &rel);
    if (st != EST_OK) return es_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"heir\":%u,\"released_minor\":", (unsigned) heir);
    zxvd_sandbox_put_u64(zxvd_result(c), rel);
    ZXVD_RAW(zxvd_result(c), "}");
}

/* distribute (once per amount) or stream (royalties): parts per heir */
static void pay_parts(zxvd_call_t *c, bool stream)
{
    uint64_t amount, out[EST_MAX_HEIRS], esc = 0;
    if (!est_ready(c) || !zxvd_sandbox_need_u64(c, "amount_minor", &amount)) return;
    est_status_t st =
        stream ? est_stream(&ES, amount, out, &esc) : est_distribute(&ES, amount, out, &esc);
    if (st != EST_OK) return es_refuse(c, st);
    zxvd_buf_t *o = zxvd_result(c);
    ZXVD_RAW(o, "{\"parts\":[");
    for (uint32_t i = 0; i < ES.n; i++) {
        zxvd_buf_printf(o, "%s{\"id\":%u,\"minor\":", i ? "," : "", (unsigned) ES.id[i]);
        zxvd_sandbox_put_u64(o, out[i]);
        zxvd_buf_printf(o, ",\"escrowed\":%s}", ES.contested[i] ? "true" : "false");
    }
    zxvd_sandbox_kv(o, "],\"to_escrow_minor\":", esc);
    if (stream) zxvd_sandbox_kv(o, ",\"held_minor\":", est_stream_held(&ES));
    ZXVD_RAW(o, ",\"moved\":false}");
}

static void m_est_distribute(zxvd_call_t *c)
{
    pay_parts(c, false);
}
static void m_est_stream(zxvd_call_t *c)
{
    pay_parts(c, true);
}

static void m_est_message(zxvd_call_t *c)
{
    static const char *const K[] = {"report", "approve", "veto",   "ruling",
                                    "fraud",  "contest", "resolve"};
    char kind[16];
    if (!est_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    uint8_t k = 0;
    for (uint8_t i = 0; i < 7; i++)
        if (!strcmp(kind, K[i])) k = (uint8_t) (i + 1);
    if (!k)
        return zxvd_fail(
            c, ZXVD_E_PARAMS,
            "\"kind\" must be report, approve, veto, ruling, fraud, contest or resolve");
    uint64_t value = 0;
    if (k == EST_MSG_REPORT && !zxvd_sandbox_need_u64(c, "bond_minor", &value)) return;
    if (k == EST_MSG_CONTEST || k == EST_MSG_RESOLVE) {
        uint32_t heir;
        if (!zxvd_sandbox_need_u32(c, "heir", &heir)) return;
        if (heir >= ES.n) return es_refuse(c, EST_ERR_ARG);
        value = est_contest_value(&ES, heir);
    }
    uint8_t m[EST_MSG];
    est_msg(&ES, k, value, m);
    zxvd_sandbox_put_msg_pq(c, &EK, m, sizeof m);
}

static void m_est_status(zxvd_call_t *c)
{
    static const char *const BOND[] = {"none", "held", "refunded", "forfeited"};
    static const char *const ENC[] = {"none", "common", "forced", "faraid"};
    uint64_t now;
    if (!zxvd_sandbox_now(c, &now)) return;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s", ES_SET ? "true" : "false");
    if (ES_SET) {
        uint32_t n = 0;
        for (uint32_t x = ES.approved; x; x &= x - 1) n++;
        zxvd_buf_printf(o, ",\"phase\":\"%s\"", phase_name(ES.phase));
        zxvd_sandbox_kv(o, ",\"epoch\":", ES.epoch);
        zxvd_sandbox_kv(o, ",\"bond_minor\":", ES.bond);
        zxvd_buf_printf(o, ",\"bond\":\"%s\",\"approvals\":%u,\"k\":%u,\"enclave\":\"%s\"",
                        BOND[ES.bond_state & 3], (unsigned) n, (unsigned) ES.cfg.k,
                        ENC[ES.enclave & 3]);
        zxvd_buf_printf(o, ",\"advisory\":%s,\"heirs\":[", est_advisory(&ES) ? "true" : "false");
        for (uint32_t i = 0; i < ES.n; i++) {
            zxvd_buf_printf(o, "%s{\"id\":%u,", i ? "," : "", (unsigned) ES.id[i]);
            put_frac(o, ES.num[i], ES.den);
            zxvd_buf_printf(o, ",\"contested\":%s", ES.contested[i] ? "true" : "false");
            zxvd_sandbox_kv(o, ",\"escrow_minor\":", ES.escrow[i]);
            zxvd_sandbox_kv(o, ",\"streamed_minor\":", ES.paid[i]);
            ZXVD_RAW(o, "}");
        }
        zxvd_sandbox_kv(o, "],\"escrow_minor\":", ES.escrow_total);
        zxvd_sandbox_kv(o, ",\"stream_total_minor\":", ES.stream_total);
        zxvd_sandbox_kv(o, ",\"stream_held_minor\":", est_stream_held(&ES));
        zxvd_buf_printf(
            o, ",\"key_release\":%s,\"digest\":", est_key_release_ok(&ES, now) ? "true" : "false");
        zxvd_buf_hex(o, ES.digest, 32);
    }
    ZXVD_RAW(o, ",\"moves_money\":false}");
}

/* ===================== recovery.* (behind the estate switch) ===================== */

static rec_account_t RA;
static bool RA_SET;
static uint64_t RA_NOTICES;
static zxvd_sandbox_pqkeys_t RK = {.ctx = "zxvd.recovery.v1"};

static bool rec_verify_cb(void *ctx, const uint8_t signer[REC_KEY], const uint8_t *m, uint32_t len,
                          const uint8_t *s, uint32_t sl)
{
    return zxvd_sandbox_pq_verify_fp(ctx, signer, m, len, s, sl);
}

static void rec_notify_cb(void *ctx, uint32_t g, rec_event_t ev, uint64_t id)
{
    (void) ctx;
    (void) g;
    (void) ev;
    (void) id;
    RA_NOTICES++; /* S3: counted; delivery is not wired */
}

static void rc_refuse(zxvd_call_t *c, rec_status_t st)
{
    static const char *const N[][2] = {
        {"REC_OK", ""},
        {"REC_ERR_ARG", "a bad argument"},
        {"REC_ERR_SIG", "the signature does not verify"},
        {"REC_ERR_BUSY", "a claim is already open"},
        {"REC_ERR_RATE", "too many claims in the window"},
        {"REC_ERR_STATE", "no pending claim, or not thawing"},
        {"REC_ERR_GUARDIAN", "no such guardian"},
        {"REC_ERR_QUORUM", "not enough guardian approvals (3, or 4 after a veto)"},
        {"REC_ERR_TIME", "the time lock is not over (14 days after a veto)"},
        {"REC_ERR_FULL", "the device table is full"},
        {"REC_ERR_DEVICE", "not an active device key"},
    };
    if (st == REC_DUP)
        return zxvd_sandbox_refuse(c, "REC_DUP", "already recorded: nothing changed");
    uint32_t i = (uint32_t) -st;
    if (i >= sizeof N / sizeof N[0]) i = 1;
    zxvd_sandbox_refuse(c, N[i][0], N[i][1]);
}

static bool rec_ready(zxvd_call_t *c)
{
    if (RA_SET) return true;
    zxvd_fail(c, ZXVD_E_ENGINE, "recovery is not set up: call recovery.setup first");
    return false;
}

static void m_rec_key(zxvd_call_t *c)
{
    zxvd_sandbox_m_pqkey(c, &RK, false);
}

static void m_rec_setup(zxvd_call_t *c)
{
    if (RA_SET) return zxvd_sandbox_refuse(c, "SET_UP", "recovery is set up already (this run)");
    rec_cfg_t cfg;
    rec_cfg_default(&cfg, 86400u);
    uint8_t first[REC_KEY];
    uint32_t ng;
    if (!zxvd_sandbox_key32(c, "root", cfg.root) ||
        !key_list(c, "guardians", cfg.guardian, REC_GUARDIANS, &ng) ||
        !zxvd_sandbox_key32(c, "gateway", cfg.gateway) ||
        !zxvd_sandbox_key32(c, "first_device", first) ||
        !zxvd_sandbox_u64(c, "timelock", false, cfg.timelock_ticks, &cfg.timelock_ticks))
        return;
    if (ng != REC_GUARDIANS)
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"guardians\" must be exactly %u keys",
                         (unsigned) REC_GUARDIANS);
    rec_status_t st = rec_init(&RA, &cfg, first, rec_verify_cb, &RK, rec_notify_cb, NULL);
    if (st != REC_OK) return rc_refuse(c, st);
    RA_SET = true;
    RA_NOTICES = 0;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_sandbox_kv(o, "{\"timelock\":", cfg.timelock_ticks);
    zxvd_sandbox_kv(o, ",\"rate_window\":", cfg.rate_window_ticks);
    zxvd_buf_printf(o, ",\"rate_max\":%u,\"state\":\"idle\"}", (unsigned) cfg.rate_max);
}

static void put_rec(zxvd_call_t *c)
{
    static const char *const S[] = {"idle", "pending", "thawing"};
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"state\":\"%s\"", S[RA.state % 3]);
    zxvd_sandbox_kv(o, ",\"claim\":", RA.claim_id);
    zxvd_buf_printf(o, ",\"approvals\":%u,\"vetoed\":%s}", (unsigned) rec_approvals(&RA),
                    RA.vetoed ? "true" : "false");
}

static void m_rec_claim(zxvd_call_t *c)
{
    uint8_t key[REC_KEY];
    uint64_t now;
    if (!rec_ready(c) || !zxvd_sandbox_key32(c, "new_key", key) || !zxvd_sandbox_now(c, &now))
        return;
    rec_status_t st = rec_claim_open(&RA, key, now);
    if (st != REC_OK) return rc_refuse(c, st);
    put_rec(c);
}

static void m_rec_approve(zxvd_call_t *c)
{
    uint32_t g;
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!rec_ready(c) || !zxvd_sandbox_need_u32(c, "guardian", &g) ||
        !zxvd_sandbox_pq_sig(c, "sig", sig))
        return;
    rec_status_t st = rec_approve(&RA, g, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != REC_OK) return rc_refuse(c, st);
    put_rec(c);
}

/* veto, cancel, device_add, device_revoke: a 32-byte key and a signature */
static bool key_sig(zxvd_call_t *c, const char *k, uint8_t key[REC_KEY],
                    uint8_t sig[ZXVD_SANDBOX_PQ_SIG])
{
    return rec_ready(c) && zxvd_sandbox_key32(c, k, key) && zxvd_sandbox_pq_sig(c, "sig", sig);
}

static void m_rec_veto(zxvd_call_t *c)
{
    uint8_t key[REC_KEY], sig[ZXVD_SANDBOX_PQ_SIG];
    if (!key_sig(c, "device", key, sig)) return;
    rec_status_t st = rec_veto(&RA, key, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != REC_OK) return rc_refuse(c, st);
    put_rec(c);
}

static void m_rec_cancel(zxvd_call_t *c)
{
    uint8_t key[REC_KEY], sig[ZXVD_SANDBOX_PQ_SIG];
    if (!key_sig(c, "device", key, sig)) return;
    rec_status_t st = rec_cancel(&RA, key, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != REC_OK) return rc_refuse(c, st);
    put_rec(c);
}

static void m_rec_device(zxvd_call_t *c, bool add)
{
    uint8_t key[REC_KEY], sig[ZXVD_SANDBOX_PQ_SIG];
    if (!key_sig(c, "key", key, sig)) return;
    rec_status_t st = add ? rec_device_add(&RA, key, sig, ZXVD_SANDBOX_PQ_SIG)
                          : rec_device_revoke(&RA, key, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != REC_OK) return rc_refuse(c, st);
    zxvd_buf_printf(zxvd_result(c), "{\"active\":%s}",
                    rec_device_active(&RA, key) ? "true" : "false");
}

static void m_rec_device_add(zxvd_call_t *c)
{
    m_rec_device(c, true);
}
static void m_rec_device_revoke(zxvd_call_t *c)
{
    m_rec_device(c, false);
}

static void m_rec_finalize(zxvd_call_t *c)
{
    uint64_t now;
    if (!rec_ready(c) || !zxvd_sandbox_now(c, &now)) return;
    rec_status_t st = rec_finalize(&RA, now);
    if (st != REC_OK) return rc_refuse(c, st);
    put_rec(c);
}

static void m_rec_gateway(zxvd_call_t *c)
{
    uint8_t sig[ZXVD_SANDBOX_PQ_SIG];
    if (!rec_ready(c) || !zxvd_sandbox_pq_sig(c, "sig", sig)) return;
    rec_status_t st = rec_gateway_verify(&RA, sig, ZXVD_SANDBOX_PQ_SIG);
    if (st != REC_OK) return rc_refuse(c, st);
    ZXVD_RAW(zxvd_result(c), "{\"gateway_verified\":true}");
}

static void m_rec_message(zxvd_call_t *c)
{
    static const char *const K[] = {"approve", "veto",       "cancel",
                                    "gateway", "device_add", "device_revoke"};
    char kind[16];
    if (!rec_ready(c) || !zxvd_p_str(c, "kind", kind, sizeof kind, true)) return;
    uint8_t k = 0;
    for (uint8_t i = 0; i < 6; i++)
        if (!strcmp(kind, K[i])) k = (uint8_t) (i + 1);
    if (!k)
        return zxvd_fail(c, ZXVD_E_PARAMS,
                         "\"kind\" must be approve, veto, cancel, gateway, "
                         "device_add or device_revoke");
    uint8_t m[REC_CLAIM_MSG];
    if (k >= REC_MSG_DEV_ADD) {
        uint8_t key[REC_KEY];
        if (!zxvd_sandbox_key32(c, "key", key)) return;
        rec_device_msg(&RA, k, key, m);
    } else {
        rec_claim_msg(&RA, k, m);
    }
    zxvd_sandbox_put_msg_pq(c, &RK, m, sizeof m);
}

static void m_rec_status(zxvd_call_t *c)
{
    static const char *const S[] = {"idle", "pending", "thawing"};
    uint64_t now, vb, liq;
    if (!zxvd_sandbox_now(c, &now) || !zxvd_sandbox_u64(c, "vbill_minor", false, 0, &vb) ||
        !zxvd_sandbox_u64(c, "liquid_minor", false, 0, &liq))
        return;
    zxvd_buf_t *o = zxvd_result(c);
    zxvd_buf_printf(o, "{\"set_up\":%s", RA_SET ? "true" : "false");
    if (RA_SET) {
        uint32_t dev = 0;
        for (uint32_t i = 0; i < REC_MAX_DEVICES; i++) dev += RA.dev_used[i];
        zxvd_buf_printf(o, ",\"state\":\"%s\"", S[RA.state % 3]);
        zxvd_sandbox_kv(o, ",\"claim\":", RA.claim_id);
        zxvd_buf_printf(
            o,
            ",\"approvals\":%u,\"needed\":%u,\"vetoed\":%s,\"devices\":%u,"
            "\"can_view\":%s,\"can_spend\":%s,\"gateway_verified\":%s",
            (unsigned) rec_approvals(&RA), (unsigned) (RA.vetoed ? REC_VETO_QUORUM : REC_QUORUM),
            RA.vetoed ? "true" : "false", (unsigned) dev, rec_can_view(&RA) ? "true" : "false",
            rec_can_spend(&RA) ? "true" : "false", RA.gateway_ok ? "true" : "false");
        zxvd_sandbox_kv(o, ",\"opened\":", RA.opened);
        zxvd_sandbox_kv(o, ",\"vbill_spendable_minor\":", rec_vbill_spendable(&RA, vb, now));
        zxvd_sandbox_kv(o, ",\"liquid_allowance_minor\":", rec_liquid_allowance(&RA, liq, now));
        zxvd_sandbox_kv(o, ",\"notices\":", RA_NOTICES);
    }
    ZXVD_RAW(o, ",\"notices_delivered\":false}");
}

static void est_group_close(void)
{
    ES_SET = false;
    RA_SET = false;
    RA_NOTICES = 0;
    EK.n = 0;
    RK.n = 0;
    memset(&ES, 0, sizeof ES);
    memset(&RA, 0, sizeof RA);
}

static const zxvd_group_method_t EST_M[] = {
    {"estate.key", m_est_key, false},
    {"estate.setup", m_est_setup, false},
    {"estate.manifest", m_est_manifest, false},
    {"estate.faraid", m_est_faraid, false},
    {"estate.report", m_est_report, false},
    {"estate.approve", m_est_approve, false},
    {"estate.confirm", m_est_confirm, false},
    {"estate.rule", m_est_rule, false},
    {"estate.begin", m_est_begin, false},
    {"estate.close", m_est_close, false},
    {"estate.veto", m_est_veto, false},
    {"estate.fraud", m_est_fraud, false},
    {"estate.contest", m_est_contest, false},
    {"estate.resolve", m_est_resolve, false},
    {"estate.distribute", m_est_distribute, false},
    {"estate.stream", m_est_stream, false},
    {"estate.message", m_est_message, true},
    {"estate.status", m_est_status, true},
    {"recovery.key", m_rec_key, false},
    {"recovery.setup", m_rec_setup, false},
    {"recovery.claim", m_rec_claim, false},
    {"recovery.approve", m_rec_approve, false},
    {"recovery.veto", m_rec_veto, false},
    {"recovery.cancel", m_rec_cancel, false},
    {"recovery.device_add", m_rec_device_add, false},
    {"recovery.device_revoke", m_rec_device_revoke, false},
    {"recovery.finalize", m_rec_finalize, false},
    {"recovery.gateway", m_rec_gateway, false},
    {"recovery.message", m_rec_message, true},
    {"recovery.status", m_rec_status, true},
};

const zxvd_group_t *zxvd_estate_group(void)
{
    static const zxvd_group_t G = {
        "estate",
        false,
        "estate succession, account recovery and faraid shares (estate.h, recovery.h, "
        "faraid.h): advisory arithmetic and bookkeeping that moves nothing; faraid shares are a "
        "computation, not a ruling",
        EST_M,
        sizeof EST_M / sizeof EST_M[0],
        est_group_close};
    return &G;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi12.c — conformance test of the ABI 1.2 hardening, linked
 * against the shared library only (no kernel headers):
 *   - fail-closed: every entry point on a poisoned context (or a handle
 *     created from one) returns ZXV_E_POISONED;
 *   - capability-to-context binding on rmag / phase / lpres / swarm /
 *     a session's bound budget;
 *   - the *_cap variants (R10) for ledger, currencies, messages, creates;
 *   - rmag listener removal vs. a callback running on another thread, and
 *     the error detail surviving a listener that calls the library;
 *   - wire tags 0..199, unique phase event ids, documented out-parameter
 *     and buffer rules (swarm consume on BUDGET, session position).
 * Usage: test_zxv_abi12 <fixtures dir> */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                     \
    do {                                                  \
        checks++;                                         \
        if (!(c)) {                                       \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)
#define OK(call)                                                                   \
    do {                                                                           \
        int32_t rc_ = (call);                                                      \
        CHECK(rc_ == ZXV_OK, "%s -> %s", #call, zxv_status_name(rc_));             \
    } while (0)
#define IS(call, want)                                                                         \
    do {                                                                                       \
        int32_t rc_ = (call);                                                                  \
        CHECK(rc_ == (want), "%s -> %s, want %s", #call, zxv_status_name(rc_), #want);         \
    } while (0)
#define POISONED(call) IS(call, ZXV_E_POISONED)

static void last_error(char *buf, size_t cap)
{
    size_t n;
    if (zxv_last_error(buf, cap, &n) != ZXV_OK) buf[0] = '\0';
}

static void nap_us(long us)
{
    struct timespec ts = {0, us * 1000L};
    nanosleep(&ts, NULL);
}

/* ===================================================================== */
/* 1. Fail closed everywhere                                             */
/* ===================================================================== */

static void test_poison_everywhere(void)
{
    zxv_ctx *ctx;
    zxv_cap *cap, *child;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    OK(zxv_cap_attenuate(cap, ZXV_CAP_LEDGER_READ, &child));
    OK(zxv_ccy_enable_with_minor(ctx, "NGN", 2));
    uint32_t a, b;
    OK(zxv_ledger_open_account(ctx, "issuer", "NGN", ZXV_ACCT_ISSUER, &a));
    OK(zxv_ledger_open_account(ctx, "holder", "NGN", ZXV_ACCT_HOLDER, &b));

    /* Handles made before the halt. */
    zxv_rmag *rm;
    zxv_phase *ph;
    zxv_lpres *reg;
    zxv_swarm *sw;
    zxv_msg *msg, *msgc;
    OK(zxv_rmag_create(ctx, &rm));
    OK(zxv_phase_create(ctx, &ph));
    OK(zxv_lpres_create(ctx, &reg));
    OK(zxv_swarm_create(ctx, 2, 100, &sw));
    OK(zxv_msg_create(ctx, ZXV_MX_PACS008, &msg));
    OK(zxv_msg_create_cap(cap, ZXV_MX_CAMT053, &msgc));
    uint32_t bud, idx;
    uint64_t ev;
    OK(zxv_rmag_budget(cap, rm, "b", ZXV_RES_TOKENS, 10, 1, 1, &bud));
    OK(zxv_phase_defer(cap, ph, 100, &ev));
    OK(zxv_lpres_attest(cap, reg, "x", 0, ZXV_LP_TRUE, "t", NULL, 0, &idx));
    OK(zxv_swarm_register(cap, sw, 1, 0));

    OK(zxv_ctx_halt(ctx, "abi12 test halt"));
    int32_t p = 0;
    OK(zxv_ctx_is_poisoned(ctx, &p));
    CHECK(p == 1, "halted");

    char buf[256];
    size_t len;
    uint32_t u32;
    int32_t i32;
    uint64_t u64;
    int64_t i64;
    uint8_t u8;

    /* Context and currency calls. */
    POISONED(zxv_ctx_halt(ctx, "again"));
    POISONED(zxv_ccy_enable(ctx, "GHS"));
    POISONED(zxv_ccy_enable_with_minor(ctx, "GHS", 2));
    POISONED(zxv_ccy_register_private(ctx, "VFV", 2));
    POISONED(zxv_ccy_register_private(ctx, "bad code", 2)); /* poison wins over ARG */
    POISONED(zxv_ccy_get(ctx, "NGN", &u8, &u32));
    POISONED(zxv_ccy_enabled_count(ctx, &u32));
    POISONED(zxv_ccy_enabled_at(ctx, 0, buf, sizeof buf, &len));
    /* Ledger. */
    POISONED(zxv_ledger_open_account(ctx, "n", "NGN", ZXV_ACCT_HOLDER, &u32));
    POISONED(zxv_ledger_open_account(ctx, "", "NGN", ZXV_ACCT_HOLDER, &u32));
    POISONED(zxv_ledger_post(ctx, a, b, 1, "NGN", "r", &u64));
    POISONED(zxv_ledger_post(ctx, a, b, 0, "NGN", "r", &u64)); /* poison wins over RANGE */
    POISONED(zxv_ledger_balance(ctx, a, &i64, &i64, &i64));
    POISONED(zxv_ledger_account_ccy(ctx, a, buf, sizeof buf, &len));
    POISONED(zxv_ledger_account_name(ctx, a, buf, sizeof buf, &len));
    POISONED(zxv_ledger_account_kind(ctx, a, &i32));
    POISONED(zxv_ledger_account_count(ctx, &u32));
    POISONED(zxv_ledger_entry_count(ctx, a, &u32));
    POISONED(zxv_ledger_entry_at(ctx, a, 0, &u64, &i64, &u32, buf, sizeof buf, &len));
    POISONED(zxv_ledger_check(ctx));
    /* Messages (old and cap-made) and their creates. */
    zxv_msg *m2;
    POISONED(zxv_msg_create(ctx, ZXV_MX_PACS008, &m2));
    POISONED(zxv_msg_create(ctx, 999, &m2));
    POISONED(zxv_msg_create_cap(cap, ZXV_MX_PACS008, &m2));
    POISONED(zxv_msg_set_text(msg, ZXV_FLD_MSG_ID, "M"));
    POISONED(zxv_msg_set_amount(msg, ZXV_AMT_SETTLEMENT, 1, 2));
    POISONED(zxv_msg_set_tx_count(msg, 1));
    POISONED(zxv_msg_add_entry(msgc, 1, 2, ZXV_CRDT, ZXV_STS_BOOK));
    POISONED(zxv_msg_render(msg, buf, sizeof buf, &len));
    POISONED(zxv_msg_set_text(msgc, ZXV_FLD_MSG_ID, "M"));
    /* Stub groups still refuse FATAL first. */
    zxv_netting *nc;
    POISONED(zxv_netting_open(ctx, "C", "NGN", &nc));
    POISONED(zxv_card_issue(ctx, ZXV_NET_DRAGON, a, 1, &u64, buf, sizeof buf, &len));
    POISONED(zxv_card_reissue(ctx, 1, &u64));
    POISONED(zxv_card_authorize(ctx, 1, 1, "NGN", &i32, buf, sizeof buf, &len));
    POISONED(zxv_vss_run(ctx, ZXV_VSS_C1_BALANCE, &i32, buf, sizeof buf, &len));
    POISONED(zxv_vss_level_get(ctx, &i32));
    /* Capabilities. */
    zxv_cap *c3;
    POISONED(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &c3));
    POISONED(zxv_cap_attenuate(cap, ZXV_CAP_LEDGER_READ, &c3));
    POISONED(zxv_cap_set_attestation(cap, ZXV_LP_TRUE));
    POISONED(zxv_cap_info(cap, &u64, &u32, &i32, &i32));
    POISONED(zxv_cap_check(child, ZXV_CAP_LEDGER_READ));
    /* Every cap-taking ledger/ccy form. */
    POISONED(zxv_ledger_post_cap(cap, a, b, 1, "NGN", "r", &u64));
    POISONED(zxv_ledger_balance_cap(child, a, &i64, &i64, &i64));
    POISONED(zxv_ledger_check_cap(cap));
    POISONED(zxv_ccy_enable_cap(cap, "GHS"));
    POISONED(zxv_ccy_enable_with_minor_cap(cap, "GHS", 2));
    POISONED(zxv_ccy_register_private_cap(cap, "VFV", 2));
    POISONED(zxv_ledger_open_account_cap(cap, "n", "NGN", ZXV_ACCT_HOLDER, &u32));
    POISONED(zxv_ledger_account_ccy_cap(cap, a, buf, sizeof buf, &len));
    POISONED(zxv_ledger_account_name_cap(cap, a, buf, sizeof buf, &len));
    POISONED(zxv_ledger_account_kind_cap(cap, a, &i32));
    POISONED(zxv_ledger_account_count_cap(cap, &u32));
    POISONED(zxv_ledger_entry_count_cap(cap, a, &u32));
    POISONED(zxv_ledger_entry_at_cap(cap, a, 0, &u64, &i64, &u32, buf, sizeof buf, &len));
    /* A revoked cap on a halted context is still FATAL, not CAP. */
    OK(zxv_cap_revoke(child)); /* revocation only removes authority: allowed */
    POISONED(zxv_ledger_balance_cap(child, a, &i64, &i64, &i64));
    /* rmag. */
    zxv_rmag *rm2;
    int64_t n1, d1, n2, d2;
    POISONED(zxv_rmag_create(ctx, &rm2));
    POISONED(zxv_rmag_create_cap(cap, &rm2));
    POISONED(zxv_rmag_budget(cap, rm, "c", ZXV_RES_TOKENS, 1, 1, 1, &u32));
    POISONED(zxv_rmag_consume(cap, rm, bud, 1, 1));
    POISONED(zxv_rmag_release(cap, rm, bud, 1, 1));
    POISONED(zxv_rmag_set_limit(cap, rm, bud, 20, 1));
    POISONED(zxv_rmag_status(rm, bud, &n1, &d1, &n2, &d2, &u64));
    POISONED(zxv_rmag_listen(rm, NULL, NULL));
    /* phase. */
    zxv_phase *ph2;
    POISONED(zxv_phase_create(ctx, &ph2));
    POISONED(zxv_phase_create_cap(cap, &ph2));
    POISONED(zxv_phase_now(ph, &u64));
    POISONED(zxv_phase_advance(cap, ph, 1, &u64, &u32));
    POISONED(zxv_phase_admit(cap, ph, 1, 1, 0, &i32, &u64));
    POISONED(zxv_phase_set_health(cap, ph, 1, ZXV_HEALTH_HEALTHY));
    POISONED(zxv_phase_defer(cap, ph, 1000, &u64));
    POISONED(zxv_phase_event(ph, ev, &i32, &u64));
    POISONED(zxv_phase_cancel(cap, ph, ev));
    POISONED(zxv_phase_next_due(ph, &u64));
    /* lpres. */
    zxv_lpres *reg2;
    POISONED(zxv_lpres_create(ctx, &reg2));
    POISONED(zxv_lpres_create_cap(cap, &reg2));
    POISONED(zxv_lpres_attest(cap, reg, "y", 0, ZXV_LP_TRUE, "t", NULL, 0, &u32));
    POISONED(zxv_lpres_evidence(cap, reg, idx, 1));
    POISONED(zxv_lpres_find(reg, "x", &u32));
    POISONED(zxv_lpres_state(reg, idx, &i32, &u32, &u32));
    POISONED(zxv_lpres_gate(reg, &i32));
    /* swarm. */
    zxv_swarm *sw2;
    uint64_t al, im, used;
    POISONED(zxv_swarm_create(ctx, 2, 100, &sw2));
    POISONED(zxv_swarm_create_cap(cap, 2, 100, &sw2));
    POISONED(zxv_swarm_register(cap, sw, 2, 1));
    POISONED(zxv_swarm_set_active(cap, sw, 1, 1));
    POISONED(zxv_swarm_set_rate(cap, sw, 50));
    POISONED(zxv_swarm_set_mood(cap, sw, 0x1F914, 1));
    POISONED(zxv_swarm_set_feeling(cap, sw, 1, 0x1F914, 1));
    POISONED(zxv_swarm_begin_cycle(cap, sw, &u64));
    POISONED(zxv_swarm_consume(cap, sw, 1, 1, &u64));
    POISONED(zxv_swarm_allotment(sw, 1, &al, &im, &used));
    POISONED(zxv_swarm_end_cycle(cap, sw, &u64));
    /* EVM through a halted context's cap. */
    uint8_t id[32] = {0}, addr[20] = {0}, dg[32];
    const int64_t bal[1] = {0};
    POISONED(zxv_evm_encode_state(cap, id, 1, addr, bal, 1, NULL, 0, &len, dg));
    POISONED(zxv_evm_ledger_state(cap, id, 1, "NGN", &a, addr, 1, NULL, 0, &len, dg));
    CHECK(zxv_status_class(ZXV_E_POISONED) == ZXV_CLASS_FATAL, "FATAL class");

    /* Destroy still works, in the documented order. */
    zxv_msg_destroy(msgc);
    zxv_msg_destroy(msg);
    zxv_swarm_destroy(sw);
    zxv_lpres_destroy(reg);
    zxv_phase_destroy(ph);
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(child);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

/* ===================================================================== */
/* 2. Capability-to-context binding                                      */
/* ===================================================================== */

static void test_cap_binding(void)
{
    zxv_ctx *c1, *c2;
    zxv_cap *k1, *k2;
    OK(zxv_ctx_create(&c1));
    OK(zxv_ctx_create(&c2));
    OK(zxv_cap_create(c1, 1, NULL, ZXV_CAP_ALL, &k1));
    OK(zxv_cap_create(c2, 2, NULL, ZXV_CAP_ALL, &k2));

    zxv_rmag *rm;
    zxv_phase *ph;
    zxv_lpres *reg;
    zxv_swarm *sw;
    OK(zxv_rmag_create_cap(k1, &rm));
    OK(zxv_phase_create_cap(k1, &ph));
    OK(zxv_lpres_create_cap(k1, &reg));
    OK(zxv_swarm_create_cap(k1, 2, 100, &sw));

    uint32_t bud, idx, u32;
    uint64_t u64;
    int32_t i32;
    OK(zxv_rmag_budget(k1, rm, "b", ZXV_RES_TOKENS, 10, 1, 1, &bud));
    IS(zxv_rmag_budget(k2, rm, "b2", ZXV_RES_TOKENS, 10, 1, 1, &u32), ZXV_E_CAP);
    IS(zxv_rmag_consume(k2, rm, bud, 1, 1), ZXV_E_CAP);
    char why[256];
    last_error(why, sizeof why);
    CHECK(strstr(why, "different context") != NULL, "detail names the mismatch (%s)", why);
    IS(zxv_rmag_release(k2, rm, bud, 1, 1), ZXV_E_CAP);
    IS(zxv_rmag_set_limit(k2, rm, bud, 1, 1), ZXV_E_CAP);
    OK(zxv_rmag_consume(k1, rm, bud, 1, 1));

    IS(zxv_phase_advance(k2, ph, 1, &u64, &u32), ZXV_E_CAP);
    IS(zxv_phase_admit(k2, ph, 1, 1, 0, &i32, &u64), ZXV_E_CAP);
    IS(zxv_phase_set_health(k2, ph, 1, ZXV_HEALTH_HEALTHY), ZXV_E_CAP);
    IS(zxv_phase_defer(k2, ph, 10, &u64), ZXV_E_CAP);
    uint64_t ev;
    OK(zxv_phase_defer(k1, ph, 10, &ev));
    IS(zxv_phase_cancel(k2, ph, ev), ZXV_E_CAP);
    OK(zxv_phase_cancel(k1, ph, ev));

    IS(zxv_lpres_attest(k2, reg, "x", 0, ZXV_LP_TRUE, "t", NULL, 0, &idx), ZXV_E_CAP);
    OK(zxv_lpres_attest(k1, reg, "x", 0, ZXV_LP_TRUE, "t", NULL, 0, &idx));
    IS(zxv_lpres_evidence(k2, reg, idx, 1), ZXV_E_CAP);

    IS(zxv_swarm_register(k2, sw, 1, 0), ZXV_E_CAP);
    OK(zxv_swarm_register(k1, sw, 1, 0));
    IS(zxv_swarm_set_active(k2, sw, 1, 1), ZXV_E_CAP);
    IS(zxv_swarm_set_rate(k2, sw, 10), ZXV_E_CAP);
    IS(zxv_swarm_set_mood(k2, sw, 0x1F914, 1), ZXV_E_CAP);
    IS(zxv_swarm_set_feeling(k2, sw, 1, 0x1F914, 1), ZXV_E_CAP);
    IS(zxv_swarm_begin_cycle(k2, sw, &u64), ZXV_E_CAP);
    OK(zxv_swarm_begin_cycle(k1, sw, &u64));
    IS(zxv_swarm_consume(k2, sw, 1, 1, &u64), ZXV_E_CAP);
    IS(zxv_swarm_end_cycle(k2, sw, &u64), ZXV_E_CAP);

    /* An attenuated child of the right context passes; of the wrong one,
     * fails; the mismatch is REFUSED class (not USAGE, not FATAL). */
    zxv_cap *k1b;
    OK(zxv_cap_attenuate(k1, ZXV_CAP_BUDGET, &k1b));
    OK(zxv_rmag_consume(k1b, rm, bud, 1, 1));
    CHECK(zxv_status_class(ZXV_E_CAP) == ZXV_CLASS_REFUSED, "CAP is REFUSED");

    /* The bit is still checked first-class on the _create_cap forms. */
    zxv_rmag *rmx;
    zxv_phase *phx;
    zxv_lpres *regx;
    zxv_swarm *swx;
    IS(zxv_rmag_create_cap(k1b, &rmx), ZXV_OK);
    zxv_rmag_destroy(rmx);
    IS(zxv_phase_create_cap(k1b, &phx), ZXV_E_CAP);
    IS(zxv_lpres_create_cap(k1b, &regx), ZXV_E_CAP);
    IS(zxv_swarm_create_cap(k1b, 2, 10, &swx), ZXV_E_CAP);
    IS(zxv_rmag_create_cap(NULL, &rmx), ZXV_E_ARG);

    /* A poisoned OTHER context does not affect this one's handles. */
    OK(zxv_ctx_halt(c2, "other"));
    OK(zxv_rmag_consume(k1, rm, bud, 1, 1));

    zxv_cap_destroy(k1b);
    zxv_swarm_destroy(sw);
    zxv_lpres_destroy(reg);
    zxv_phase_destroy(ph);
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(k2);
    zxv_cap_destroy(k1);
    zxv_ctx_destroy(c2);
    zxv_ctx_destroy(c1);
}

/* ===================================================================== */
/* 3. R10: the *_cap forms                                               */
/* ===================================================================== */

static void test_cap_variants(void)
{
    zxv_ctx *ctx;
    zxv_cap *root, *ro, *post;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root));
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &ro));
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST, &post));

    IS(zxv_ccy_enable_with_minor_cap(ro, "NGN", 2), ZXV_E_CAP);
    OK(zxv_ccy_enable_with_minor_cap(post, "NGN", 2));
    uint32_t feat;
    zxv_features(&feat);
    if (feat & ZXV_FEAT_ISO4217) {
        IS(zxv_ccy_enable_cap(ro, "GHS"), ZXV_E_CAP);
        OK(zxv_ccy_enable_cap(post, "GHS"));
        IS(zxv_ccy_enable_cap(post, "XXX"), ZXV_E_CURRENCY);
    }
    IS(zxv_ccy_register_private_cap(ro, "VFV", 2), ZXV_E_CAP);
    OK(zxv_ccy_register_private_cap(post, "VFV", 2));
    uint8_t mu;
    uint32_t fl;
    OK(zxv_ccy_get(ctx, "VFV", &mu, &fl));
    CHECK((fl & ZXV_CCYF_PRIVATE) && mu == 2, "VFV registered through a cap");

    uint32_t a, b;
    IS(zxv_ledger_open_account_cap(ro, "issuer", "NGN", ZXV_ACCT_ISSUER, &a), ZXV_E_CAP);
    OK(zxv_ledger_open_account_cap(post, "issuer", "NGN", ZXV_ACCT_ISSUER, &a));
    OK(zxv_ledger_open_account_cap(post, "Ada \xc3\xa9", "NGN", ZXV_ACCT_HOLDER, &b));
    IS(zxv_ledger_open_account_cap(post, "x", "ZZZ", ZXV_ACCT_HOLDER, &b), ZXV_E_CURRENCY);
    uint64_t e;
    OK(zxv_ledger_post_cap(post, a, b, 1234, "NGN", "R-1", &e));

    char buf[128];
    size_t len;
    int32_t kind;
    uint32_t n;
    IS(zxv_ledger_account_ccy_cap(post, b, buf, sizeof buf, &len), ZXV_E_CAP);
    OK(zxv_ledger_account_ccy_cap(ro, b, buf, sizeof buf, &len));
    CHECK(strcmp(buf, "NGN") == 0 && len == 3, "account ccy through a cap");
    IS(zxv_ledger_account_name_cap(ro, b, NULL, 0, &len), ZXV_E_BUFFER);
    CHECK(len == 6, "two-call name size (%zu)", len);
    OK(zxv_ledger_account_name_cap(ro, b, buf, len + 1, &len));
    CHECK(strcmp(buf, "Ada \xc3\xa9") == 0, "account name through a cap");
    OK(zxv_ledger_account_kind_cap(ro, a, &kind));
    CHECK(kind == ZXV_ACCT_ISSUER, "kind");
    IS(zxv_ledger_account_kind_cap(ro, 99, &kind), ZXV_E_NOT_FOUND);
    OK(zxv_ledger_account_count_cap(ro, &n));
    CHECK(n == 2, "count");
    IS(zxv_ledger_entry_count_cap(post, b, &n), ZXV_E_CAP);
    OK(zxv_ledger_entry_count_cap(ro, b, &n));
    CHECK(n == 1, "one journal line");
    uint64_t eid;
    int64_t amt;
    uint32_t cp;
    OK(zxv_ledger_entry_at_cap(ro, b, 0, &eid, &amt, &cp, buf, sizeof buf, &len));
    CHECK(eid == e && amt == 1234 && cp == a && strcmp(buf, "R-1") == 0, "journal line through a cap");

    /* A message made through a cap re-checks it on every call. */
    zxv_msg *m;
    IS(zxv_msg_create_cap(post, ZXV_MX_PACS008, &m), ZXV_E_CAP);
    zxv_cap *mcap;
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &mcap));
    OK(zxv_msg_create_cap(mcap, ZXV_MX_PACS008, &m));
    OK(zxv_msg_set_text(m, ZXV_FLD_MSG_ID, "MSG-1"));
    OK(zxv_msg_set_text(m, ZXV_FLD_CRE_DT_TM, "2026-10-11T00:00:00Z"));
    OK(zxv_msg_set_text(m, ZXV_FLD_DEBTOR_NAME, "Ada"));
    OK(zxv_msg_set_text(m, ZXV_FLD_DEBTOR_ACCT, "NG1"));
    OK(zxv_msg_set_text(m, ZXV_FLD_CREDITOR_NAME, "Kwame"));
    OK(zxv_msg_set_text(m, ZXV_FLD_CREDITOR_ACCT, "GH1"));
    OK(zxv_msg_set_text(m, ZXV_FLD_END_TO_END_ID, "E2E-1"));
    OK(zxv_msg_set_text(m, ZXV_FLD_CCY, "NGN"));
    OK(zxv_msg_set_amount(m, ZXV_AMT_SETTLEMENT, 1234, 2));
    OK(zxv_msg_set_tx_count(m, 1));
    IS(zxv_msg_render(m, NULL, 0, &len), ZXV_E_BUFFER);
    CHECK(len > 200, "render size");
    OK(zxv_cap_set_attestation(mcap, ZXV_LP_NEITHER));
    IS(zxv_msg_render(m, NULL, 0, &len), ZXV_E_CAP);
    IS(zxv_msg_set_text(m, ZXV_FLD_MSG_ID, "MSG-2"), ZXV_E_CAP);
    OK(zxv_cap_set_attestation(mcap, ZXV_LP_TRUE));
    IS(zxv_msg_render(m, NULL, 0, &len), ZXV_E_BUFFER);
    OK(zxv_cap_revoke(mcap));
    IS(zxv_msg_set_amount(m, ZXV_AMT_SETTLEMENT, 1, 2), ZXV_E_CAP);
    IS(zxv_msg_render(m, NULL, 0, &len), ZXV_E_CAP);
    zxv_msg_destroy(m);
    zxv_cap_destroy(mcap);
    zxv_msg *c53;
    OK(zxv_msg_create_cap(ro, ZXV_MX_CAMT053, &c53));
    OK(zxv_msg_add_entry(c53, 5, 2, ZXV_CRDT, ZXV_STS_BOOK));
    zxv_msg_destroy(c53);
    IS(zxv_msg_create_cap(ro, ZXV_MX_PACS002, &c53), ZXV_E_NOTIMPL);
    IS(zxv_msg_create_cap(ro, 999, &c53), ZXV_E_ARG);
    IS(zxv_msg_create_cap(NULL, ZXV_MX_PACS008, &c53), ZXV_E_ARG);

    /* Attestation does not propagate to an existing child; revocation does. */
    zxv_cap *kid;
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &kid));
    OK(zxv_cap_set_attestation(root, ZXV_LP_FALSE));
    OK(zxv_ledger_account_count_cap(kid, &n)); /* documented: the child keeps TRUE */
    IS(zxv_ledger_account_count_cap(root, &n), ZXV_E_CAP);
    OK(zxv_cap_set_attestation(root, ZXV_LP_TRUE));
    OK(zxv_cap_revoke(root));
    IS(zxv_ledger_account_count_cap(kid, &n), ZXV_E_CAP);

    /* The deprecated ctx forms still work (1.0 compatibility). */
    OK(zxv_ledger_account_count(ctx, &n));
    CHECK(n == 2, "deprecated form unchanged");

    zxv_cap_destroy(kid);
    zxv_cap_destroy(post);
    zxv_cap_destroy(ro);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
}

/* ===================================================================== */
/* 4. rmag listener: removal race, error detail                          */
/* ===================================================================== */

#define LIVE 0x4C495645u
#define DEAD 0xDEADDEADu

typedef struct {
    _Atomic uint32_t magic;
    _Atomic uint32_t calls;
} victim;

static _Atomic uint32_t g_saw_dead;

static void slow_listener(void *user, uint32_t budget, int32_t event)
{
    (void) budget;
    (void) event;
    victim *v = user;
    nap_us(200); /* widen the window between the claim and the access */
    if (atomic_load(&v->magic) != LIVE) atomic_fetch_add(&g_saw_dead, 1);
    atomic_fetch_add(&v->calls, 1);
}

typedef struct {
    const zxv_cap *cap;
    zxv_rmag *rm;
    uint32_t bud;
    _Atomic int stop;
} storm;

static void *consumer(void *arg)
{
    storm *s = arg;
    while (!atomic_load(&s->stop)) zxv_rmag_consume(s->cap, s->rm, s->bud, 1, 1); /* DENIED */
    return NULL;
}

static void test_listener_race(void)
{
    zxv_ctx *ctx;
    zxv_cap *cap;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    storm s;
    memset(&s, 0, sizeof s);
    s.cap = cap;
    OK(zxv_rmag_create_cap(cap, &s.rm));
    OK(zxv_rmag_budget(cap, s.rm, "zero", ZXV_RES_TOKENS, 0, 1, 1, &s.bud));
    pthread_t th[3];
    for (int i = 0; i < 3; i++) pthread_create(&th[i], NULL, consumer, &s);
    atomic_store(&g_saw_dead, 0);
    uint32_t delivered = 0;
    for (int round = 0; round < 60; round++) {
        victim *v = malloc(sizeof *v);
        atomic_init(&v->magic, LIVE);
        atomic_init(&v->calls, 0);
        zxv_rmag_listen(s.rm, slow_listener, v);
        nap_us(500);
        zxv_rmag_listen(s.rm, NULL, NULL);
        /* After removal returns, no thread may still be inside (or about
         * to enter) slow_listener with v: poison it, then free it. ASan
         * reports a heap-use-after-free if the guarantee is broken. */
        atomic_store(&v->magic, DEAD);
        delivered += atomic_load(&v->calls);
        free(v);
    }
    atomic_store(&s.stop, 1);
    for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
    CHECK(delivered > 0, "the listener was exercised (%u calls)", delivered);
    CHECK(atomic_load(&g_saw_dead) == 0, "no callback ran after removal (%u)", atomic_load(&g_saw_dead));
    zxv_rmag_destroy(s.rm);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

typedef struct {
    zxv_rmag *rm;
    uint32_t bud;
    int calls;
    int self_remove;
} chatty;

/* A listener that calls back into the library: one success (which clears
 * the detail) and one failure (which writes its own detail). */
static void chatty_listener(void *user, uint32_t budget, int32_t event)
{
    (void) event;
    chatty *c = user;
    c->calls++;
    int64_t rn, rd;
    zxv_rmag_status(c->rm, budget, &rn, &rd, NULL, NULL, NULL);
    zxv_rmag_status(c->rm, 9999, &rn, &rd, NULL, NULL, NULL); /* NOT_FOUND */
    if (c->self_remove) zxv_rmag_listen(c->rm, NULL, NULL);    /* must not deadlock */
}

static void test_listener_error_detail(void)
{
    zxv_ctx *ctx;
    zxv_cap *cap;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    chatty c = {0};
    OK(zxv_rmag_create_cap(cap, &c.rm));
    OK(zxv_rmag_budget(cap, c.rm, "toks", ZXV_RES_TOKENS, 2, 1, 1, &c.bud));
    OK(zxv_rmag_listen(c.rm, chatty_listener, &c));
    OK(zxv_rmag_consume(cap, c.rm, c.bud, 2, 1));
    IS(zxv_rmag_consume(cap, c.rm, c.bud, 1, 1), ZXV_E_BUDGET);
    char why[256];
    last_error(why, sizeof why);
    CHECK(c.calls == 1 && strstr(why, "exhausted") != NULL,
          "DENIED detail survives the listener (%d calls, \"%s\")", c.calls, why);
    IS(zxv_rmag_consume(cap, c.rm, c.bud, -1, 1), ZXV_E_RMAG_INVALID);
    last_error(why, sizeof why);
    CHECK(c.calls == 2 && strstr(why, "negative") != NULL, "INVALID detail survives (\"%s\")", why);
    OK(zxv_rmag_release(cap, c.rm, c.bud, 1, 1));
    last_error(why, sizeof why);
    CHECK(c.calls == 3 && why[0] == '\0', "OK leaves an empty detail (\"%s\")", why);
    c.self_remove = 1;
    OK(zxv_rmag_set_limit(cap, c.rm, c.bud, 5, 1)); /* grows: REPLENISHED; listener removes itself */
    CHECK(c.calls == 4, "self-removal from inside the listener returned");
    IS(zxv_rmag_consume(cap, c.rm, c.bud, 100, 1), ZXV_E_BUDGET);
    CHECK(c.calls == 4, "removed listener is not called again");
    zxv_rmag_destroy(c.rm);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

/* ===================================================================== */
/* 6, 7 and documented rules                                             */
/* ===================================================================== */

static void test_wire_tags(void)
{
    const uint32_t sp[3] = {1, 2, 3}, sm[2] = {4, 5};
    uint8_t f[21], tag;
    uint32_t rp[3], rm[2];
    OK(zxv_wire_pack(0, sp, sm, f));
    OK(zxv_wire_pack(199, sp, sm, f));
    OK(zxv_wire_unpack(f, &tag, rp, rm));
    CHECK(tag == 199, "tag 199 round-trips");
    IS(zxv_wire_pack(200, sp, sm, f), ZXV_E_ARG);
    IS(zxv_wire_pack(255, sp, sm, f), ZXV_E_ARG);
    CHECK(zxv_status_class(ZXV_E_ARG) == ZXV_CLASS_USAGE, "tag error is USAGE");
    int bad = 0;
    for (uint32_t sync = 0; sync <= 1; sync++)
        for (uint32_t shell = 0; shell <= 9; shell++)
            for (uint32_t trunk = 0; trunk <= 9; trunk++) {
                bad += zxv_wire_tag(trunk, shell, sync, &tag) != ZXV_OK;
                bad += zxv_wire_pack(tag, sp, sm, f) != ZXV_OK;
            }
    CHECK(bad == 0, "every zxv_wire_tag result packs");
}

static void test_phase_ids(void)
{
    zxv_ctx *ctx;
    zxv_cap *cap;
    zxv_phase *ph;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    OK(zxv_phase_create_cap(cap, &ph));
    uint64_t first, ev, prev = 0, now;
    OK(zxv_phase_defer(cap, ph, 1, &first));
    OK(zxv_phase_advance(cap, ph, 1, &now, NULL)); /* first CLEARED */
    int mono = 1, calls_ok = 1;
    prev = first;
    /* Well past the 4096-id slot-recycling horizon, finishing each event so
     * its slot can be reused. */
    for (int i = 0; i < 10000; i++) {
        calls_ok &= zxv_phase_defer(cap, ph, now + 1, &ev) == ZXV_OK;
        mono &= ev > prev;
        prev = ev;
        if (i % 2) calls_ok &= zxv_phase_cancel(cap, ph, ev) == ZXV_OK;
        else calls_ok &= zxv_phase_advance(cap, ph, 1, &now, NULL) == ZXV_OK;
    }
    CHECK(calls_ok, "10000 defer / finish cycles");
    CHECK(mono, "event ids strictly increase");
    /* A fresh pending event (likely in a recycled slot). */
    uint64_t fresh;
    OK(zxv_phase_defer(cap, ph, now + 50, &fresh));
    CHECK(fresh != first, "a new event never reuses an old id");
    int32_t st;
    IS(zxv_phase_event(ph, first, &st, NULL), ZXV_E_NOT_FOUND);
    IS(zxv_phase_cancel(cap, ph, first), ZXV_E_NOT_FOUND); /* stale id cannot cancel */
    OK(zxv_phase_event(ph, fresh, &st, NULL));
    CHECK(st == ZXV_EV_PENDING, "the fresh event is untouched by the stale cancel");
    OK(zxv_phase_cancel(cap, ph, fresh));
    zxv_phase_destroy(ph);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

static void test_swarm_budget_out(void)
{
    zxv_ctx *ctx;
    zxv_cap *cap;
    zxv_swarm *sw;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    OK(zxv_swarm_create_cap(cap, 1, 100, &sw));
    OK(zxv_swarm_register(cap, sw, 7, 0));
    uint64_t cyc, al, g = 12345;
    OK(zxv_swarm_begin_cycle(cap, sw, &cyc));
    OK(zxv_swarm_allotment(sw, 7, &al, NULL, NULL));
    IS(zxv_swarm_consume(cap, sw, 7, al + 50, &g), ZXV_E_BUDGET);
    CHECK(g == al, "BUDGET still writes the partial grant (%llu of %llu)", (unsigned long long) g,
          (unsigned long long) al);
    g = 777;
    IS(zxv_swarm_consume(cap, sw, 99, 1, &g), ZXV_E_NOT_FOUND);
    CHECK(g == 777, "other errors leave the out parameter alone");
    zxv_swarm_destroy(sw);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

static void test_session_rules(const char *dir)
{
    uint32_t feat;
    zxv_features(&feat);
    if (!(feat & ZXV_FEAT_TENSOR)) {
        printf("skip session rules: built without the engine\n");
        return;
    }
    char mp[1024], tp[1024], pp[1024];
    snprintf(mp, sizeof mp, "%s/tiny-qwen2.gguf", dir);
    snprintf(tp, sizeof tp, "%s/tiny-tokenizer.gguf", dir);
    snprintf(pp, sizeof pp, "%s/prompt.txt", dir);
    FILE *f = fopen(pp, "rb");
    if (!f) {
        CHECK(0, "prompt fixture missing at %s", pp);
        return;
    }
    char prompt[256];
    size_t plen = fread(prompt, 1, sizeof prompt, f);
    fclose(f);
    zxv_ctx *c1, *c2;
    zxv_cap *k1, *k2;
    OK(zxv_ctx_create(&c1));
    OK(zxv_ctx_create(&c2));
    OK(zxv_cap_create(c1, 1, NULL, ZXV_CAP_ALL, &k1));
    OK(zxv_cap_create(c2, 1, NULL, ZXV_CAP_ALL, &k2));
    zxv_model *m;
    OK(zxv_model_open(k1, mp, tp, &m));

    /* Tokenize: no terminator, cap == count is enough. */
    size_t nt;
    IS(zxv_tokenize(m, (const uint8_t *) prompt, plen, 1, NULL, 0, &nt), ZXV_E_BUFFER);
    int32_t *ids = malloc(nt * sizeof *ids);
    OK(zxv_tokenize(m, (const uint8_t *) prompt, plen, 1, ids, nt, &nt));
    IS(zxv_tokenize(m, (const uint8_t *) prompt, plen, 1, ids, nt - 1, &nt), ZXV_E_BUFFER);
    free(ids);

    zxv_session *s;
    OK(zxv_session_create(m, 128, ZXV_KV_Q16, &s));
    uint32_t np, ng, pos;
    int32_t stop;
    OK(zxv_generate(k1, s, (const uint8_t *) prompt, plen, 5, 0, 0, 0, 0, 0, 0, NULL, NULL, &np, &ng,
                    &stop));
    OK(zxv_session_position(s, &pos));
    CHECK(ng == 5 && pos == np + ng - 1, "position = prompt + N - 1 (%u, %u + %u)", pos, np, ng);

    /* A budget of another context is refused; a poisoned one too. */
    zxv_rmag *rm2, *rm1;
    uint32_t b2, b1;
    OK(zxv_rmag_create_cap(k2, &rm2));
    OK(zxv_rmag_budget(k2, rm2, "t", ZXV_RES_TOKENS, 1000, 1, 1, &b2));
    OK(zxv_session_reset(s));
    OK(zxv_session_bind_budget(s, rm2, b2));
    IS(zxv_generate(k1, s, (const uint8_t *) prompt, plen, 2, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL,
                    NULL),
       ZXV_E_CAP);
    OK(zxv_rmag_create_cap(k1, &rm1));
    OK(zxv_rmag_budget(k1, rm1, "t", ZXV_RES_TOKENS, 1000, 1, 1, &b1));
    OK(zxv_session_bind_budget(s, rm1, b1));
    OK(zxv_generate(k1, s, (const uint8_t *) prompt, plen, 2, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL,
                    NULL));
    OK(zxv_session_bind_budget(s, NULL, 0));

    /* EVM: bytes, no NUL, cap == size is enough. */
    uint8_t id[32] = {0}, addr[20] = {1}, dg[32], buf[8 * 32];
    const int64_t bal[1] = {-1};
    size_t len;
    IS(zxv_evm_encode_state(k1, id, 1, addr, bal, 1, NULL, 0, &len, dg), ZXV_E_BUFFER);
    CHECK(len == 8 * 32, "(6 + 2n) * 32");
    OK(zxv_evm_encode_state(k1, id, 1, addr, bal, 1, buf, len, &len, dg));
    IS(zxv_evm_encode_state(k1, id, 1, addr, bal, 1, buf, len - 1, &len, dg), ZXV_E_BUFFER);

    zxv_session_destroy(s);
    zxv_model_destroy(m);
    zxv_rmag_destroy(rm1);
    zxv_rmag_destroy(rm2);
    zxv_cap_destroy(k2);
    zxv_cap_destroy(k1);
    zxv_ctx_destroy(c2);
    zxv_ctx_destroy(c1);
}

static void test_version_and_random(void)
{
    uint32_t maj, min, pat;
    OK(zxv_version(&maj, &min, &pat));
    CHECK(maj == ZXV_ABI_VERSION_MAJOR && min == ZXV_ABI_VERSION_MINOR && pat == ZXV_ABI_VERSION_PATCH,
          "library and header agree (%u.%u.%u)", maj, min, pat);
    CHECK(maj == 1 && min >= 2, "ABI 1.2+");
    static uint8_t big[100000];
    OK(zxv_random(big, sizeof big)); /* more than one getentropy / BCrypt chunk */
    int nz = 0;
    for (size_t i = 0; i < sizeof big; i++) nz += big[i] != 0;
    CHECK(nz > 90000, "random bytes are not mostly zero (%d)", nz);
    OK(zxv_random(NULL, 0));
    IS(zxv_random(NULL, 1), ZXV_E_ARG);
}

int main(int argc, char **argv)
{
    test_version_and_random();
    test_poison_everywhere();
    test_cap_binding();
    test_cap_variants();
    test_listener_race();
    test_listener_error_detail();
    test_wire_tags();
    test_phase_ids();
    test_swarm_budget_out();
    test_session_rules(argc > 1 ? argv[1] : "../conformance/fixtures");
    printf("test_zxv_abi12: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_recovery.c — recovery.h R1-R6: claims, guardian approvals, veto and
 * cancel, the rate limit, finalisation, the staged thaw at day boundaries,
 * the gateway lift, device keys and silence. The signature hook is a
 * stand-in (SHA-256 keyed by the signer's key hash); production passes
 * ML-DSA verification. */
#include <stdio.h>
#include <string.h>
#include "recovery.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

#define TPD 24u /* ticks per day: one tick per hour */

/* stand-in signature: SHA-256(signer || msg) */
static void sign(const uint8_t who[REC_KEY], const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[256];
    memcpy(b, who, REC_KEY);
    memcpy(b + REC_KEY, m, n);
    sha256(b, REC_KEY + n, sig);
}
static uint32_t nverify;
static bool verify(void *ctx, const uint8_t who[REC_KEY], const uint8_t *m, uint32_t n,
                   const uint8_t *sig, uint32_t sl)
{
    uint8_t want[32];
    nverify++;
    if (ctx != (void *) &nverify || sl != 32 || n > 200) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}

/* notifications: count per guardian and event */
static uint32_t nseen[REC_GUARDIANS][6], nbad;
static uint64_t last_claim;
static int nctx_tag;
static void notify(void *ctx, uint32_t g, rec_event_t ev, uint64_t id)
{
    if (ctx != &nctx_tag || g >= REC_GUARDIANS || ev < 1 || ev > 5) {
        nbad++;
        return;
    }
    nseen[g][ev]++;
    last_claim = id;
}
static bool all_saw(rec_event_t ev, uint32_t n)
{
    for (uint32_t g = 0; g < REC_GUARDIANS; g++)
        if (nseen[g][ev] != n) return false;
    return true;
}

static uint8_t KEY[16][REC_KEY];
enum { ROOT = 0, G0 = 1, GW = 6, DEV1 = 7, DEV2 = 8, NEWK = 9, NEWK2 = 10, X = 11 };

static rec_cfg_t cfg;
static rec_account_t R;

static void setup_cfg(void)
{
    for (int i = 0; i < 16; i++) memset(KEY[i], 0xA0 + i, REC_KEY);
    rec_cfg_default(&cfg, TPD);
    memcpy(cfg.root, KEY[ROOT], REC_KEY);
    for (int g = 0; g < 5; g++) memcpy(cfg.guardian[g], KEY[G0 + g], REC_KEY);
    memcpy(cfg.gateway, KEY[GW], REC_KEY);
}

static void fresh(void)
{
    memset(nseen, 0, sizeof nseen);
    rec_init(&R, &cfg, KEY[DEV1], verify, &nverify, notify, &nctx_tag);
}

static rec_status_t approve(uint32_t g, uint32_t signer)
{
    uint8_t m[REC_CLAIM_MSG], s[32];
    rec_claim_msg(&R, REC_MSG_APPROVE, m);
    sign(KEY[signer], m, sizeof m, s);
    return rec_approve(&R, g, s, 32);
}
static rec_status_t devsig(uint8_t kind, uint32_t dev, uint32_t signer)
{
    uint8_t m[REC_CLAIM_MSG], s[32];
    rec_claim_msg(&R, kind, m);
    sign(KEY[signer], m, sizeof m, s);
    return kind == REC_MSG_VETO ? rec_veto(&R, KEY[dev], s, 32) : rec_cancel(&R, KEY[dev], s, 32);
}
static rec_status_t gateway(uint32_t signer)
{
    uint8_t m[REC_CLAIM_MSG], s[32];
    rec_claim_msg(&R, REC_MSG_GATEWAY, m);
    sign(KEY[signer], m, sizeof m, s);
    return rec_gateway_verify(&R, s, 32);
}
static rec_status_t dev_op(bool add, uint32_t key, uint32_t signer)
{
    uint8_t m[REC_DEV_MSG], s[32];
    rec_device_msg(&R, add ? REC_MSG_DEV_ADD : REC_MSG_DEV_DEL, KEY[key], m);
    sign(KEY[signer], m, sizeof m, s);
    return add ? rec_device_add(&R, KEY[key], s, 32) : rec_device_revoke(&R, KEY[key], s, 32);
}

static void test_cfg_init(void)
{
    rec_cfg_t c;
    memset(&c, 0x55, sizeof c);
    rec_cfg_default(&c, TPD);
    uint8_t zero[REC_KEY] = {0};
    CHECK(!memcmp(c.root, zero, REC_KEY) && !memcmp(c.gateway, zero, REC_KEY) &&
              !memcmp(c.guardian[4], zero, REC_KEY),
          "defaults: keys zeroed");
    CHECK(c.ticks_per_day == TPD && c.timelock_ticks == 72 && c.rate_window_ticks == 720 &&
              c.rate_max == 3,
          "defaults: 72h time lock, 3 claims per 30 days");
    rec_cfg_default(0, TPD); /* no crash */
    rec_cfg_default(&c, UINT64_MAX / 3);
    CHECK(c.timelock_ticks == UINT64_MAX / 3 * 3 && c.rate_window_ticks == 0,
          "3 days fits, 30 days overflows to 0");
    rec_cfg_default(&c, UINT64_MAX / 2);
    CHECK(c.timelock_ticks == 0 && c.rate_window_ticks == 0, "overflow leaves 0");
    CHECK(rec_init(&R, &c, KEY[DEV1], verify, 0, 0, 0) == REC_ERR_ARG, "overflowed cfg refused");

    rec_account_t r;
    CHECK(rec_init(0, &cfg, KEY[DEV1], verify, 0, 0, 0) == REC_ERR_ARG, "null account");
    CHECK(rec_init(&r, 0, KEY[DEV1], verify, 0, 0, 0) == REC_ERR_ARG, "null cfg");
    CHECK(rec_init(&r, &cfg, 0, verify, 0, 0, 0) == REC_ERR_ARG, "null device");
    CHECK(rec_init(&r, &cfg, KEY[DEV1], 0, 0, 0, 0) == REC_ERR_ARG, "null verify");
    c = cfg;
    c.ticks_per_day = 0;
    CHECK(rec_init(&r, &c, KEY[DEV1], verify, 0, 0, 0) == REC_ERR_ARG, "tpd 0");
    c = cfg;
    c.timelock_ticks = 0;
    CHECK(rec_init(&r, &c, KEY[DEV1], verify, 0, 0, 0) == REC_ERR_ARG, "timelock 0");
    c = cfg;
    c.rate_window_ticks = 0;
    CHECK(rec_init(&r, &c, KEY[DEV1], verify, 0, 0, 0) == REC_ERR_ARG, "window 0");
    c = cfg;
    c.rate_max = 0;
    CHECK(rec_init(&r, &c, KEY[DEV1], verify, 0, 0, 0) == REC_ERR_ARG, "rate 0");
    c.rate_max = REC_RATE_CAP + 1;
    CHECK(rec_init(&r, &c, KEY[DEV1], verify, 0, 0, 0) == REC_ERR_ARG, "rate above cap");
    c.rate_max = REC_RATE_CAP;
    CHECK(rec_init(&r, &c, KEY[DEV1], verify, 0, 0, 0) == REC_OK, "rate at cap");
    c.rate_max = 1;
    c.ticks_per_day = 1;
    c.timelock_ticks = 1;
    c.rate_window_ticks = 1;
    CHECK(rec_init(&r, &c, KEY[DEV1], verify, 0, 0, 0) == REC_OK, "smallest cfg");
    CHECK(r.state == REC_IDLE && rec_device_active(&r, KEY[DEV1]) &&
              !rec_device_active(&r, KEY[DEV2]) && r.claim_id == 0,
          "init state");
    CHECK(rec_can_view(&r) && rec_can_spend(&r), "idle: view and spend");
    CHECK(!rec_can_view(0) && !rec_can_spend(0) && !rec_device_active(0, KEY[DEV1]) &&
              !rec_device_active(&r, 0) && rec_approvals(0) == 0,
          "null queries");
}

static void test_msg(void)
{
    fresh();
    rec_claim_open(&R, KEY[NEWK], 5);
    uint8_t m[REC_CLAIM_MSG];
    rec_claim_msg(&R, REC_MSG_VETO, m);
    CHECK(!memcmp(m, "zxv/rec-msg/v1", 14) && m[14] == REC_MSG_VETO, "tag and kind");
    CHECK(!memcmp(m + 15, KEY[ROOT], 32), "root anchor");
    CHECK(m[47] == 1 && m[48] == 0 && m[54] == 0, "claim id 1, little-endian");
    CHECK(!memcmp(m + 55, KEY[NEWK], 32), "new key hash");
    uint8_t d[REC_DEV_MSG];
    R.dev_seq = 0x0102;
    rec_device_msg(&R, REC_MSG_DEV_ADD, KEY[X], d);
    CHECK(d[14] == REC_MSG_DEV_ADD && d[47] == 2 && d[48] == 1 && !memcmp(d + 55, KEY[X], 32),
          "device message carries the sequence and key");
}

static void test_devices(void)
{
    fresh();
    CHECK(dev_op(true, DEV2, X) == REC_ERR_SIG, "device add needs the root anchor");
    CHECK(!rec_device_active(&R, KEY[DEV2]), "not added");
    CHECK(dev_op(true, DEV2, ROOT) == REC_OK && rec_device_active(&R, KEY[DEV2]), "added");
    CHECK(R.device[1][0] == KEY[DEV2][0] && R.dev_used[1], "first free slot");
    CHECK(dev_op(true, DEV2, ROOT) == REC_DUP && dev_op(true, DEV1, ROOT) == REC_DUP &&
              R.dev_seq == 1,
          "already active (slots 1 and 0)");
    CHECK(dev_op(true, 12, ROOT) == REC_OK && dev_op(true, 13, ROOT) == REC_OK, "fill");
    CHECK(dev_op(true, 14, ROOT) == REC_ERR_FULL, "table full");
    uint8_t s[32] = {0};
    CHECK(rec_device_add(0, KEY[X], s, 32) == REC_ERR_ARG &&
              rec_device_add(&R, 0, s, 32) == REC_ERR_ARG &&
              rec_device_add(&R, KEY[X], 0, 32) == REC_ERR_ARG,
          "add args");
    CHECK(rec_device_revoke(0, KEY[X], s, 32) == REC_ERR_ARG &&
              rec_device_revoke(&R, 0, s, 32) == REC_ERR_ARG &&
              rec_device_revoke(&R, KEY[X], 0, 32) == REC_ERR_ARG,
          "revoke args");
    CHECK(dev_op(false, X, ROOT) == REC_ERR_DEVICE, "revoke unknown");
    CHECK(dev_op(false, 12, X) == REC_ERR_SIG && rec_device_active(&R, KEY[12]),
          "revoke needs the root anchor");
    /* a signature made now replays nothing after the sequence moves */
    uint8_t m[REC_DEV_MSG], old[32];
    rec_device_msg(&R, REC_MSG_DEV_DEL, KEY[12], m);
    sign(KEY[ROOT], m, sizeof m, old);
    CHECK(dev_op(false, 13, ROOT) == REC_OK && !rec_device_active(&R, KEY[13]) && R.dev_seq == 4,
          "revoked, sequence advanced");
    CHECK(rec_device_revoke(&R, KEY[12], old, 32) == REC_ERR_SIG, "stale sequence refused");
    CHECK(dev_op(true, 14, ROOT) == REC_OK && R.dev_used[3] && R.device[3][0] == KEY[14][0],
          "freed slot reused");
    CHECK(dev_op(false, DEV1, ROOT) == REC_OK && dev_op(true, 13, ROOT) == REC_OK &&
              R.device[0][0] == KEY[13][0],
          "lowest free slot first");
}

static void test_claim_approve(void)
{
    fresh();
    CHECK(rec_claim_open(0, KEY[NEWK], 0) == REC_ERR_ARG, "null account");
    CHECK(rec_claim_open(&R, 0, 0) == REC_ERR_ARG, "null key");
    CHECK(rec_claim_open(&R, KEY[DEV1], 0) == REC_ERR_ARG, "new key already a device");
    CHECK(approve(0, G0) == REC_ERR_STATE, "approve with no claim");
    CHECK(rec_finalize(&R, 1000) == REC_ERR_STATE, "finalise with no claim");
    CHECK(rec_finalize(0, 1000) == REC_ERR_ARG, "finalise null");
    CHECK(dev_op(true, DEV2, ROOT) == REC_OK && R.dev_seq == 1, "a second device");
    CHECK(rec_claim_open(&R, KEY[NEWK], 100) == REC_OK, "R1 open");
    CHECK(R.state == REC_PENDING && R.claim_id == 1 && R.opened == 100, "pending");
    CHECK(all_saw(REC_EV_CLAIM, 1) && last_claim == 1, "R1 every guardian told");
    CHECK(rec_can_view(&R) && !rec_can_spend(&R), "R1 sandbox: view, no spend");
    CHECK(rec_vbill_spendable(&R, 500, 100) == 0 && rec_liquid_allowance(&R, 500, 100) == 0,
          "sandbox: nothing spendable");
    CHECK(rec_claim_open(&R, KEY[NEWK2], 101) == REC_ERR_BUSY, "R1 one claim at a time");

    uint8_t s[32] = {0};
    CHECK(rec_approve(0, 0, s, 32) == REC_ERR_ARG && rec_approve(&R, 0, 0, 32) == REC_ERR_ARG,
          "approve args");
    CHECK(approve(REC_GUARDIANS, G0) == REC_ERR_GUARDIAN, "R2 unknown guardian");
    CHECK(approve(1, G0) == REC_ERR_SIG, "R2 guardian 0's key for guardian 1");
    CHECK(rec_approvals(&R) == 0 && all_saw(REC_EV_APPROVE, 0), "nothing counted");
    CHECK(approve(0, G0) == REC_OK && rec_approvals(&R) == 1, "R2 approve");
    CHECK(all_saw(REC_EV_APPROVE, 1), "R2 every guardian told");
    CHECK(approve(0, G0) == REC_DUP && rec_approvals(&R) == 1, "R2 duplicate not counted");
    CHECK(all_saw(REC_EV_APPROVE, 1), "duplicate not announced");
    CHECK(approve(4, G0 + 4) == REC_OK && rec_approvals(&R) == 2 && R.approved == 0x11,
          "guardian 4");
    CHECK(rec_finalize(&R, 100000) == REC_ERR_QUORUM, "R3 two approvals are not enough");
    CHECK(approve(2, G0 + 2) == REC_OK && rec_approvals(&R) == 3, "third");
    CHECK(rec_finalize(&R, 100 + 71) == REC_ERR_TIME, "R3 71 hours: time lock");
    CHECK(R.state == REC_PENDING && all_saw(REC_EV_FINAL, 0), "still pending");
    CHECK(rec_finalize(&R, 100 + 72) == REC_OK, "R4 72 hours + 3 approvals");
    CHECK(R.state == REC_THAWING && R.fin_tick == 172 && all_saw(REC_EV_FINAL, 1) && R.dev_seq == 2,
          "thawing");
    CHECK(rec_device_active(&R, KEY[NEWK]) && !rec_device_active(&R, KEY[DEV1]) &&
              !rec_device_active(&R, KEY[DEV2]),
          "R4 new key installed, old revoked");
    CHECK(rec_can_spend(&R), "spending again (within the thaw)");
    CHECK(approve(3, G0 + 3) == REC_ERR_STATE, "approve after finalise");
    CHECK(rec_finalize(&R, 1000) == REC_ERR_STATE, "finalise twice");

    /* a signature for claim 1 does not approve claim 2 */
    uint8_t m[REC_CLAIM_MSG], old[32];
    rec_claim_msg(&R, REC_MSG_APPROVE, m);
    sign(KEY[G0 + 1], m, sizeof m, old);
    CHECK(rec_claim_open(&R, KEY[NEWK2], 200) == REC_OK && R.claim_id == 2, "second claim");
    CHECK(rec_approve(&R, 1, old, 32) == REC_ERR_SIG, "replayed approval refused");
    CHECK(rec_approve(&R, 1, old, 31) == REC_ERR_SIG, "short signature");
}

static void test_veto(void)
{
    fresh();
    rec_claim_open(&R, KEY[NEWK], 1000);
    CHECK(devsig(REC_MSG_VETO, NEWK, NEWK) == REC_ERR_DEVICE, "R3 the new key cannot veto");
    CHECK(devsig(REC_MSG_VETO, DEV1, X) == REC_ERR_SIG, "R3 veto signature checked");
    CHECK(devsig(REC_MSG_CANCEL, DEV1, DEV1) == REC_OK, "R3 owner cancels");
    CHECK(R.state == REC_IDLE && all_saw(REC_EV_CANCEL, 1), "R3 cancel closes the claim");
    CHECK(rec_claim_open(&R, KEY[NEWK], 1000) == REC_OK, "reopen");
    uint8_t m[REC_CLAIM_MSG], s[32];
    rec_claim_msg(&R, REC_MSG_APPROVE, m);
    sign(KEY[DEV1], m, sizeof m, s);
    CHECK(rec_veto(&R, KEY[DEV1], s, 32) == REC_ERR_SIG, "an approve message is not a veto");
    CHECK(rec_veto(0, KEY[DEV1], s, 32) == REC_ERR_ARG && rec_veto(&R, 0, s, 32) == REC_ERR_ARG &&
              rec_veto(&R, KEY[DEV1], 0, 32) == REC_ERR_ARG,
          "veto args");
    CHECK(devsig(REC_MSG_VETO, DEV1, DEV1) == REC_OK && R.vetoed, "R3 veto");
    CHECK(all_saw(REC_EV_VETO, 1) && R.state == REC_PENDING, "veto announced, still pending");
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    uint64_t d14 = 1000 + 14 * TPD;
    CHECK(rec_finalize(&R, d14 + 100) == REC_ERR_QUORUM, "R3 3 approvals refused after veto");
    approve(3, G0 + 3);
    CHECK(rec_finalize(&R, 1000 + 72) == REC_ERR_TIME, "R3 72h no longer enough");
    CHECK(rec_finalize(&R, d14 - 1) == REC_ERR_TIME, "R3 4 approvals + 13d 23h refused");
    CHECK(rec_finalize(&R, 1000 + 13 * TPD) == REC_ERR_TIME, "R3 4 + 13 days refused");
    CHECK(rec_finalize(&R, d14) == REC_OK, "R3 4 approvals + 14 days override the veto");
    CHECK(R.state == REC_THAWING && rec_device_active(&R, KEY[NEWK]), "finalised");
    CHECK(devsig(REC_MSG_VETO, NEWK, NEWK) == REC_ERR_STATE, "veto with no claim");
    CHECK(devsig(REC_MSG_CANCEL, NEWK, NEWK) == REC_ERR_STATE, "cancel with no claim");

    /* a new claim after a finalisation: cancel returns to THAWING */
    CHECK(rec_claim_open(&R, KEY[NEWK2], d14 + 1) == REC_OK && !R.vetoed && rec_approvals(&R) == 0,
          "new claim starts clean");
    CHECK(devsig(REC_MSG_CANCEL, NEWK, X) == REC_ERR_SIG, "cancel signature checked");
    CHECK(devsig(REC_MSG_CANCEL, DEV1, DEV1) == REC_ERR_DEVICE, "revoked device cannot cancel");
    CHECK(devsig(REC_MSG_CANCEL, NEWK, NEWK) == REC_OK && R.state == REC_THAWING,
          "cancel returns to the thaw");
    CHECK(rec_cancel(0, KEY[NEWK], s, 32) == REC_ERR_ARG, "cancel args");

    /* a veto period that overflows never ends */
    rec_cfg_t c = cfg;
    c.ticks_per_day = UINT64_MAX / 10;
    c.timelock_ticks = 1;
    c.rate_window_ticks = 1;
    rec_init(&R, &c, KEY[DEV1], verify, &nverify, 0, 0);
    rec_claim_open(&R, KEY[NEWK], 0);
    devsig(REC_MSG_VETO, DEV1, DEV1);
    for (uint32_t g = 0; g < 5; g++) approve(g, G0 + g);
    CHECK(rec_finalize(&R, UINT64_MAX) == REC_ERR_TIME, "veto lock overflow: never");
    /* and an overflowing time lock likewise */
    c.ticks_per_day = 1;
    c.timelock_ticks = UINT64_MAX;
    rec_init(&R, &c, KEY[DEV1], verify, &nverify, 0, 0);
    rec_claim_open(&R, KEY[NEWK], 5);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    CHECK(rec_finalize(&R, UINT64_MAX) == REC_ERR_TIME, "time lock overflow: never");
    c.timelock_ticks = UINT64_MAX - 5;
    rec_init(&R, &c, KEY[DEV1], verify, &nverify, 0, 0);
    rec_claim_open(&R, KEY[NEWK], 5);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    CHECK(rec_finalize(&R, UINT64_MAX) == REC_OK, "time lock at the edge");
}

static void test_rate(void)
{
    fresh();
    uint64_t t0 = 5000, W = 30 * TPD;
    for (int i = 0; i < 3; i++) {
        CHECK(rec_claim_open(&R, KEY[NEWK], t0 + i) == REC_OK, "claim within the limit");
        devsig(REC_MSG_CANCEL, DEV1, DEV1);
    }
    CHECK(R.claim_id == 3 && all_saw(REC_EV_CLAIM, 3), "three claims, all told");
    CHECK(rec_claim_open(&R, KEY[NEWK], t0 + 10) == REC_ERR_RATE, "R1 fourth in 30 days");
    CHECK(rec_claim_open(&R, KEY[NEWK], t0 + W - 1) == REC_ERR_RATE, "still inside the window");
    CHECK(R.claim_id == 3 && R.state == REC_IDLE && all_saw(REC_EV_CLAIM, 3),
          "refused: nothing changed");
    CHECK(rec_claim_open(&R, KEY[NEWK], t0 + 2) == REC_ERR_RATE, "same tick as the last");
    CHECK(rec_claim_open(&R, KEY[NEWK], t0 + 1) == REC_ERR_ARG, "tick before the last claim");
    CHECK(rec_claim_open(&R, KEY[NEWK], t0 + W) == REC_OK, "first one left the window");
    devsig(REC_MSG_CANCEL, DEV1, DEV1);
    CHECK(rec_claim_open(&R, KEY[NEWK], t0 + W) == REC_ERR_RATE, "window full again");
    CHECK(rec_claim_open(&R, KEY[NEWK], t0 + W + 2) == REC_OK, "two left");

    /* the ring of open ticks wraps past REC_RATE_CAP */
    rec_cfg_t c = cfg;
    c.rate_max = REC_RATE_CAP;
    c.rate_window_ticks = 100;
    rec_init(&R, &c, KEY[DEV1], verify, &nverify, 0, 0);
    uint64_t t = 0;
    for (uint32_t i = 0; i < REC_RATE_CAP + 3; i++, t += 20) {
        CHECK(rec_claim_open(&R, KEY[NEWK], t) == REC_OK, "spaced claims pass");
        devsig(REC_MSG_CANCEL, DEV1, DEV1);
    }
    t -= 20; /* the last open */
    for (uint32_t i = 1; i < REC_RATE_CAP - 4; i++) {
        CHECK(rec_claim_open(&R, KEY[NEWK], t) == REC_OK, "burst");
        devsig(REC_MSG_CANCEL, DEV1, DEV1);
    }
    /* window (t-100, t]: 5 spaced + 3 burst = 8 */
    CHECK(rec_claim_open(&R, KEY[NEWK], t) == REC_ERR_RATE, "ring counts the window");
    CHECK(rec_claim_open(&R, KEY[NEWK], t + 19) == REC_ERR_RATE &&
              rec_claim_open(&R, KEY[NEWK], t + 20) == REC_OK,
          "one aged out");
}

static void test_thaw(void)
{
    fresh();
    CHECK(rec_vbill_spendable(&R, 700, 0) == 700 && rec_liquid_allowance(&R, 700, 0) == 700,
          "never recovered: no limits");
    CHECK(rec_vbill_spendable(0, 700, 0) == 0 && rec_liquid_allowance(0, 700, 0) == 0, "null");
    CHECK(gateway(GW) == REC_ERR_STATE, "gateway only while thawing");
    rec_claim_open(&R, KEY[NEWK], 10);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    uint64_t F = 10 + 72 + 5;
    rec_finalize(&R, F);
    /* V-Bills: locked 7 days */
    CHECK(rec_vbill_spendable(&R, 700, F) == 0, "R5 V-Bills locked at finalise");
    CHECK(rec_vbill_spendable(&R, 700, F + 7 * TPD - 1) == 0, "R5 locked one tick before 7d");
    CHECK(rec_vbill_spendable(&R, 700, F + 7 * TPD) == 700, "R5 all at 7 days");
    /* liquid: 5% per day cumulative for 72 hours */
    CHECK(rec_liquid_allowance(&R, 1000, F - 1) == 0, "before finalise");
    CHECK(rec_liquid_allowance(&R, 1000, F) == 50, "day 0: 5%");
    CHECK(rec_liquid_allowance(&R, 1000, F + TPD - 1) == 50, "day 0 end");
    CHECK(rec_liquid_allowance(&R, 1000, F + TPD) == 100, "day 1: 10%");
    CHECK(rec_liquid_allowance(&R, 1000, F + 2 * TPD - 1) == 100, "day 1 end");
    CHECK(rec_liquid_allowance(&R, 1000, F + 2 * TPD) == 150, "day 2: 15%");
    CHECK(rec_liquid_allowance(&R, 1000, F + 3 * TPD - 1) == 150, "71 hours");
    CHECK(rec_liquid_allowance(&R, 1000, F + 3 * TPD) == 1000, "72 hours: all");
    CHECK(rec_liquid_allowance(&R, 999, F) == 49 && rec_liquid_allowance(&R, 999, F + TPD) == 99 &&
              rec_liquid_allowance(&R, 999, F + 2 * TPD) == 149,
          "floors: 49, 99, 149 of 999");
    CHECK(rec_liquid_allowance(&R, UINT64_MAX, F + 2 * TPD) == 2767011611056432742u,
          "128-bit product: floor(max * 15 / 100)");
    /* gateway lift */
    uint8_t s[32] = {0};
    CHECK(rec_gateway_verify(0, s, 32) == REC_ERR_ARG &&
              rec_gateway_verify(&R, 0, 32) == REC_ERR_ARG,
          "gateway args");
    CHECK(gateway(X) == REC_ERR_SIG && !R.gateway_ok, "gateway signature checked");
    CHECK(gateway(GW) == REC_OK, "R5 gateway verified");
    CHECK(rec_liquid_allowance(&R, 1000, F) == 1000, "R5 gateway lifts the liquid limit");
    CHECK(rec_vbill_spendable(&R, 700, F + 1) == 0, "R5 but not the V-Bill lock");
    /* a new claim sandboxes again; after its cancel the thaw is unchanged */
    rec_claim_open(&R, KEY[NEWK2], F + 1);
    CHECK(rec_liquid_allowance(&R, 1000, F + 1) == 0 &&
              rec_vbill_spendable(&R, 7, F + 9 * TPD) == 0,
          "pending: sandbox");
    devsig(REC_MSG_CANCEL, NEWK, NEWK);
    CHECK(rec_liquid_allowance(&R, 1000, F + 1) == 1000, "thaw record kept");
    /* a new finalisation restarts the thaw and drops the old gateway lift */
    rec_claim_open(&R, KEY[NEWK2], F + 2);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    uint64_t F2 = F + 2 + 72;
    CHECK(rec_finalize(&R, F2) == REC_OK && !R.gateway_ok, "second recovery");
    CHECK(rec_liquid_allowance(&R, 1000, F2) == 50 && rec_vbill_spendable(&R, 7, F2) == 0,
          "thaw restarts");
    /* a V-Bill lock that overflows never opens */
    rec_cfg_t c = cfg;
    c.ticks_per_day = UINT64_MAX / 7 + 1;
    c.timelock_ticks = 1;
    c.rate_window_ticks = 1;
    rec_init(&R, &c, KEY[DEV1], verify, &nverify, 0, 0);
    rec_claim_open(&R, KEY[NEWK], 0);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    rec_finalize(&R, 1);
    CHECK(rec_vbill_spendable(&R, 7, UINT64_MAX) == 0, "lock overflow: never");
    CHECK(rec_liquid_allowance(&R, 1000, 1 + c.ticks_per_day) == 100 &&
              rec_liquid_allowance(&R, 1000, UINT64_MAX) == 1000,
          "huge days");
    c.ticks_per_day = UINT64_MAX / 7;
    rec_init(&R, &c, KEY[DEV1], verify, &nverify, 0, 0);
    rec_claim_open(&R, KEY[NEWK], 0);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    rec_finalize(&R, 1);
    CHECK(rec_vbill_spendable(&R, 7, UINT64_MAX - 1) == 0 &&
              rec_vbill_spendable(&R, 7, UINT64_MAX) == 7,
          "lock at the edge opens");
}

static void test_silence(void)
{
    fresh();
    rec_claim_open(&R, KEY[NEWK], 0);
    CHECK(rec_finalize(&R, UINT64_MAX) == REC_ERR_QUORUM, "R6 no approvals: never");
    CHECK(R.state == REC_PENDING && !rec_can_spend(&R), "R6 still pending");
    approve(0, G0);
    approve(1, G0 + 1);
    CHECK(rec_finalize(&R, UINT64_MAX) == REC_ERR_QUORUM && R.state == REC_PENDING,
          "R6 two approvals and all the time in the world: still pending");
    /* no notifier: nothing breaks */
    rec_init(&R, &cfg, KEY[DEV1], verify, &nverify, 0, 0);
    memset(nseen, 0, sizeof nseen);
    CHECK(rec_claim_open(&R, KEY[NEWK], 0) == REC_OK && all_saw(REC_EV_CLAIM, 0), "no notifier");
}

int main(void)
{
    setup_cfg();
    test_cfg_init();
    test_msg();
    test_devices();
    test_claim_approve();
    test_veto();
    test_rate();
    test_thaw();
    test_silence();
    CHECK(nbad == 0, "every notification well-formed");
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_estate.c — estate.h E1-E6 and faraid.h F1-F6: every phase
 * transition and refusal, the living veto and the bond, fraud findings,
 * exact distribution and streaming, contested escrow, the key release gate,
 * the France forced-heirship row with reduction, and the textbook faraid
 * cases. The signature hook is a stand-in (SHA-256 keyed by the signer's
 * key hash); production passes ML-DSA verification. */
#include <stdio.h>
#include <string.h>
#include "estate.h"
#include "faraid.h"
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

static void sign(const uint8_t who[EST_KEY], const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[256];
    memcpy(b, who, EST_KEY);
    memcpy(b + EST_KEY, m, n);
    sha256(b, EST_KEY + n, sig);
}
static int vtag;
static bool verify(void *ctx, const uint8_t who[EST_KEY], const uint8_t *m, uint32_t n,
                   const uint8_t *sig, uint32_t sl)
{
    uint8_t want[32];
    if (ctx != &vtag || sl != 32 || n > 200) return false;
    sign(who, m, n, want);
    return !memcmp(want, sig, 32);
}

static uint8_t KEY[16][EST_KEY];
enum { OWNER = 0, G0 = 1, A0 = 6, A1 = 7, COURT = 8, GW = 9, X = 10 };
static est_cfg_t cfg;
static est_t E;
#define WAIT 100u
#define REL  500u

static void setup_cfg(void)
{
    for (int i = 0; i < 16; i++) memset(KEY[i], 0x30 + i, EST_KEY);
    memset(&cfg, 0, sizeof cfg);
    memcpy(cfg.owner, KEY[OWNER], EST_KEY);
    for (int g = 0; g < 5; g++) memcpy(cfg.guardian[g], KEY[G0 + g], EST_KEY);
    cfg.n_guardians = 5;
    cfg.k = 3;
    memcpy(cfg.attestor[0], KEY[A0], EST_KEY);
    memcpy(cfg.attestor[1], KEY[A1], EST_KEY);
    cfg.n_attestors = 2;
    memcpy(cfg.court, KEY[COURT], EST_KEY);
    memcpy(cfg.gateway, KEY[GW], EST_KEY);
    cfg.wait_ticks = WAIT;
    cfg.release_ticks = REL;
}

static void fresh(void)
{
    est_init(&E, &cfg, verify, &vtag);
}

static void sig(uint8_t kind, uint64_t value, uint32_t signer, uint8_t s[32])
{
    uint8_t m[EST_MSG];
    est_msg(&E, kind, value, m);
    sign(KEY[signer], m, sizeof m, s);
}
static est_status_t report(uint32_t att, uint64_t bond, uint64_t tick, uint32_t signer)
{
    uint8_t s[32];
    sig(EST_MSG_REPORT, bond, signer, s);
    return est_report(&E, att, bond, tick, s, 32);
}
static est_status_t approve(uint32_t g, uint32_t signer)
{
    uint8_t s[32];
    sig(EST_MSG_APPROVE, 0, signer, s);
    return est_approve(&E, g, s, 32);
}
static est_status_t veto(uint32_t signer)
{
    uint8_t s[32];
    sig(EST_MSG_VETO, 0, signer, s);
    return est_veto(&E, s, 32);
}
static est_status_t rule(uint32_t signer)
{
    uint8_t s[32];
    sig(EST_MSG_RULING, 0, signer, s);
    return est_rule(&E, s, 32);
}
static est_status_t fraud(uint32_t who, uint32_t signer)
{
    uint8_t s[32];
    sig(EST_MSG_FRAUD, 0, signer, s);
    return est_fraud_finding(&E, who, s, 32);
}
static est_status_t contest(uint32_t heir, uint32_t who, uint32_t signer)
{
    uint8_t s[32];
    sig(EST_MSG_CONTEST, est_contest_value(&E, heir), signer, s);
    return est_contest(&E, heir, who, s, 32);
}
static est_status_t resolve(uint32_t heir, uint32_t signer, uint64_t *rel)
{
    uint8_t s[32];
    sig(EST_MSG_RESOLVE, est_contest_value(&E, heir), signer, s);
    return est_resolve(&E, heir, s, 32, rel);
}

static const uint32_t IDS[EST_MAX_HEIRS] = {100, 101, 102, 103, 104, 105, 106, 107,
                                            108, 109, 110, 111, 112, 113, 114, 115};
static est_status_t will3(void) /* 1/2, 1/3, 1/6 */
{
    uint64_t n[3] = {1, 1, 1}, d[3] = {2, 3, 6};
    return est_set_manifest(&E, EST_COMMON, 3, IDS, n, d);
}
static est_status_t equal(est_enclave_t enc, uint32_t k)
{
    uint64_t n[EST_MAX_HEIRS], d[EST_MAX_HEIRS];
    for (uint32_t i = 0; i < k; i++) n[i] = 1, d[i] = k;
    return est_set_manifest(&E, enc, k, IDS, n, d);
}
/* report, 3 approvals, confirm at reported + WAIT */
static void to_confirmed(uint64_t t)
{
    report(0, 1000, t, A0);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    est_confirm(&E, t + WAIT);
}
static void to_distributing(void)
{
    to_confirmed(10);
    if (E.enclave != EST_COMMON) rule(GW);
    est_begin(&E);
}

static void test_init(void)
{
    est_t e;
    est_cfg_t c = cfg;
    CHECK(est_init(0, &c, verify, 0) == EST_ERR_ARG, "null estate");
    CHECK(est_init(&e, 0, verify, 0) == EST_ERR_ARG, "null cfg");
    CHECK(est_init(&e, &c, 0, 0) == EST_ERR_ARG, "null verify");
    c.n_guardians = EST_MAX_GUARDIANS + 1;
    c.k = 3;
    CHECK(est_init(&e, &c, verify, 0) == EST_ERR_ARG, "too many guardians");
    c = cfg;
    c.k = 0;
    CHECK(est_init(&e, &c, verify, 0) == EST_ERR_ARG, "k 0");
    c.k = 6;
    CHECK(est_init(&e, &c, verify, 0) == EST_ERR_ARG, "k above n");
    c.k = 5;
    CHECK(est_init(&e, &c, verify, 0) == EST_OK, "k = n");
    c.n_attestors = 0;
    CHECK(est_init(&e, &c, verify, 0) == EST_ERR_ARG, "no attestor");
    c.n_attestors = EST_MAX_ATTESTORS + 1;
    CHECK(est_init(&e, &c, verify, 0) == EST_ERR_ARG, "too many attestors");
    c.n_attestors = EST_MAX_ATTESTORS;
    CHECK(est_init(&e, &c, verify, 0) == EST_OK, "attestors at cap");
    CHECK(e.phase == EST_ALIVE && e.enclave == EST_NONE && e.bond_state == EST_BOND_NONE &&
              e.epoch == 0 && e.cfg.k == 5,
          "init state");
    CHECK(!est_advisory(&e) && !est_advisory(0), "no manifest: not advisory");
}

static void test_msg(void)
{
    fresh();
    will3();
    E.epoch = 0x0203;
    uint8_t m[EST_MSG];
    est_msg(&E, EST_MSG_REPORT, 0x0405, m);
    CHECK(!memcmp(m, "zxv/est-msg/v1", 14) && m[14] == EST_MSG_REPORT, "tag, kind");
    CHECK(!memcmp(m + 15, KEY[OWNER], 32), "owner");
    CHECK(m[47] == 3 && m[48] == 2 && m[54] == 0, "epoch LE");
    CHECK(m[55] == 5 && m[56] == 4 && m[62] == 0, "value LE");
    CHECK(!memcmp(m + 63, E.digest, 32), "digest");
    E.ccount[2] = 7;
    CHECK(est_contest_value(&E, 2) == 0x702 && est_contest_value(&E, 3) == 3, "contest value");
}

static void test_common_law(void)
{
    fresh();
    uint64_t out[EST_MAX_HEIRS];
    CHECK(est_begin(&E) == EST_ERR_NOMANIFEST, "begin without a manifest");
    CHECK(will3() == EST_OK && E.den == 6 && E.num[0] == 3 && E.num[1] == 2 && E.num[2] == 1,
          "E4 will over the least common denominator");
    CHECK(E.enclave == EST_COMMON && E.id[2] == 102 && E.n == 3, "stored");
    CHECK(!est_advisory(&E), "common law is not advisory");
    CHECK(est_begin(&E) == EST_ERR_STATE, "begin while alive");
    CHECK(est_confirm(&E, 1000) == EST_ERR_STATE, "confirm while alive");
    CHECK(approve(0, G0) == EST_ERR_STATE, "approve while alive");
    CHECK(est_distribute(&E, 100, out, 0) == EST_ERR_STATE, "distribute while alive");
    CHECK(est_stream(&E, 100, out, 0) == EST_ERR_STATE, "stream while alive");
    /* E1 report */
    uint8_t s[32] = {0};
    CHECK(est_report(0, 0, 1000, 5, s, 32) == EST_ERR_ARG, "null estate");
    CHECK(est_report(&E, 0, 1000, 5, 0, 32) == EST_ERR_ARG, "null sig");
    CHECK(report(0, 0, 5, A0) == EST_ERR_ARG, "bond 0");
    CHECK(report(2, 1000, 5, A0) == EST_ERR_ARG, "unknown attestor");
    CHECK(report(1, 1000, 5, A0) == EST_ERR_SIG, "attestor 0's key as attestor 1");
    CHECK(report(0, 1000, 5, X) == EST_ERR_SIG && E.phase == EST_ALIVE, "bad signature");
    sig(EST_MSG_REPORT, 999, A1, s);
    CHECK(est_report(&E, 1, 1000, 5, s, 32) == EST_ERR_SIG, "the bond is signed");
    CHECK(report(1, 1000, 5, A1) == EST_OK, "E1 reported by attestor 1");
    CHECK(E.phase == EST_REPORTED && E.reported == 5 && E.bond == 1000 &&
              E.bond_state == EST_BOND_HELD,
          "bond held");
    CHECK(report(0, 1000, 6, A0) == EST_ERR_STATE, "report twice");
    CHECK(will3() == EST_ERR_STATE, "E4 will fixed once reported");
    CHECK(rule(GW) == EST_ERR_STATE, "rule before confirmation");
    /* approvals */
    CHECK(est_approve(0, 0, s, 32) == EST_ERR_ARG && est_approve(&E, 0, 0, 32) == EST_ERR_ARG,
          "approve args");
    CHECK(approve(5, G0) == EST_ERR_ARG, "unknown guardian");
    CHECK(approve(1, G0) == EST_ERR_SIG, "guardian 0's key for 1");
    CHECK(approve(0, G0) == EST_OK && approve(0, G0) == EST_DUP && E.approved == 1,
          "duplicate approval");
    CHECK(approve(4, G0 + 4) == EST_OK && E.approved == 0x11, "guardian 4");
    CHECK(est_confirm(&E, 1000000) == EST_ERR_QUORUM, "E1 two of three");
    CHECK(est_confirm(0, 5) == EST_ERR_ARG, "confirm null");
    CHECK(approve(2, G0 + 2) == EST_OK, "third");
    CHECK(est_confirm(&E, 5 + WAIT - 1) == EST_ERR_TIME, "E1 waiting period");
    CHECK(E.phase == EST_REPORTED, "still reported");
    CHECK(est_confirm(&E, 5 + WAIT) == EST_OK && E.phase == EST_CONFIRMED &&
              E.confirmed == 5 + WAIT,
          "E1 confirmed");
    CHECK(est_confirm(&E, 5 + WAIT) == EST_ERR_STATE, "confirm twice");
    CHECK(approve(3, G0 + 3) == EST_ERR_STATE, "approve after confirmation");
    CHECK(rule(GW) == EST_ERR_STATE, "common law takes no ruling");
    CHECK(!est_key_release_ok(&E, UINT64_MAX), "E3 no key release before distribution");
    CHECK(est_distribute(&E, 100, out, 0) == EST_ERR_STATE, "distribute before begin");
    CHECK(est_close(&E) == EST_ERR_STATE && est_close(0) == EST_ERR_ARG, "close before begin");
    CHECK(est_begin(&E) == EST_OK && E.phase == EST_DISTRIBUTING, "E1 common law distributes");
    CHECK(est_begin(&E) == EST_ERR_STATE && est_begin(0) == EST_ERR_ARG, "begin twice");
    CHECK(veto(OWNER) == EST_ERR_STATE, "no veto once distributing");
    /* E3 */
    uint64_t c = 5 + WAIT;
    CHECK(!est_key_release_ok(&E, c + REL - 1) && est_key_release_ok(&E, c + REL),
          "E3 release delay");
    CHECK(!est_key_release_ok(0, UINT64_MAX), "null");
    /* E2 distribute: 50, 33, 16 + the leftover unit to the largest remainder (heir 2) */
    uint64_t esc = 77;
    CHECK(est_distribute(&E, 100, out, &esc) == EST_OK && out[0] == 50 && out[1] == 33 &&
              out[2] == 17 && esc == 0,
          "E2 largest remainder");
    CHECK(est_distribute(&E, 1, out, 0) == EST_OK && out[0] == 1 && out[1] == 0 && out[2] == 0,
          "one unit: remainders 3,2,1 of 6");
    CHECK(est_distribute(&E, 5, out, 0) == EST_OK && out[0] == 2 && out[1] == 2 && out[2] == 1,
          "five: floors 2,1,0, remainders 3,4,5 (of 6): heirs 2 then 1");
    CHECK(est_distribute(0, 5, out, 0) == EST_ERR_ARG && est_distribute(&E, 5, 0, 0) == EST_ERR_ARG,
          "distribute args");
    uint64_t big = UINT64_MAX;
    CHECK(est_distribute(&E, big, out, 0) == EST_OK && out[0] + out[1] + out[2] == big &&
              out[0] == big / 2 + 1 && out[1] == big / 3 && out[2] == big / 6,
          "E2 full 64-bit amount, exact sum");
    CHECK(est_close(&E) == EST_OK && E.phase == EST_CLOSED && E.bond_state == EST_BOND_REFUNDED,
          "E1 closed, bond refunded");
    CHECK(est_key_release_ok(&E, c + REL), "E3 closed keeps the release");
    CHECK(est_distribute(&E, 5, out, 0) == EST_ERR_STATE, "no distribution once closed");
    CHECK(est_stream(&E, 6, out, 0) == EST_OK && out[0] == 3 && out[1] == 2 && out[2] == 1,
          "royalties stream after closing");
    CHECK(est_close(&E) == EST_ERR_STATE, "close twice");

    /* an overflowing release delay never opens */
    est_cfg_t cc = cfg;
    cc.release_ticks = UINT64_MAX;
    est_init(&E, &cc, verify, &vtag);
    will3();
    to_distributing();
    CHECK(E.phase == EST_DISTRIBUTING && !est_key_release_ok(&E, UINT64_MAX),
          "E3 release overflow: never");
    /* an overflowing waiting period never confirms */
    cc = cfg;
    cc.wait_ticks = UINT64_MAX;
    est_init(&E, &cc, verify, &vtag);
    report(0, 1, 1, A0);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    CHECK(est_confirm(&E, UINT64_MAX) == EST_ERR_TIME, "wait overflow: never");
    cc.wait_ticks = UINT64_MAX - 1;
    est_init(&E, &cc, verify, &vtag);
    report(0, 1, 1, A0);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    CHECK(est_confirm(&E, UINT64_MAX) == EST_OK, "wait at the edge");
}

static void test_ties(void)
{
    fresh();
    equal(EST_COMMON, 3);
    to_distributing();
    uint64_t out[EST_MAX_HEIRS];
    CHECK(est_distribute(&E, 2, out, 0) == EST_OK && out[0] == 1 && out[1] == 1 && out[2] == 0,
          "E2 ties to the lower index");
    CHECK(est_distribute(&E, 1, out, 0) == EST_OK && out[0] == 1 && out[1] == 0 && out[2] == 0,
          "one unit to heir 0");
    CHECK(est_distribute(&E, 0, out, 0) == EST_OK && out[0] == 0 && out[1] == 0, "zero");
    fresh();
    uint64_t n[3] = {1, 2, 2}, d[3] = {5, 5, 5};
    est_set_manifest(&E, EST_COMMON, 3, IDS, n, d);
    to_distributing();
    /* 3 units: 0.6, 1.2, 1.2 -> 0,1,1 + remainders 3,1,1 (of 5): heir 0 */
    CHECK(est_distribute(&E, 3, out, 0) == EST_OK && out[0] == 1 && out[1] == 1 && out[2] == 1,
          "largest remainder beats the lower index tie");
    /* 4 units: 0.8, 1.6, 1.6 -> 0,1,1 + r 4,3,3: heir 0 then heir 1 */
    CHECK(est_distribute(&E, 4, out, 0) == EST_OK && out[0] == 1 && out[1] == 2 && out[2] == 1,
          "second unit to the next largest");
}

static void test_veto_bond(void)
{
    uint8_t s[32] = {0};
    fresh();
    will3();
    CHECK(veto(OWNER) == EST_ERR_STATE, "veto while alive");
    CHECK(fraud(0, G0) == EST_ERR_STATE, "fraud with no bond");
    report(0, 500, 1, A0);
    approve(0, G0);
    uint8_t old[32];
    sig(EST_MSG_APPROVE, 0, G0 + 1, old);
    CHECK(est_veto(0, s, 32) == EST_ERR_ARG && est_veto(&E, 0, 32) == EST_ERR_ARG, "veto args");
    CHECK(veto(X) == EST_ERR_SIG && veto(G0) == EST_ERR_SIG, "only the owner vetoes");
    CHECK(veto(OWNER) == EST_OK, "E1 living veto in REPORTED");
    CHECK(E.phase == EST_ALIVE && E.approved == 0 && E.epoch == 1 &&
              E.bond_state == EST_BOND_REFUNDED,
          "alive, approvals cleared, epoch bumped, bond refunded");
    CHECK(fraud(0, G0) == EST_ERR_STATE, "a refunded bond cannot be forfeited");
    CHECK(report(0, 700, 2, A0) == EST_OK && E.bond == 700 && E.bond_state == EST_BOND_HELD,
          "a new filing, a new bond");
    CHECK(est_approve(&E, 1, old, 32) == EST_ERR_SIG, "old-epoch approval refused");
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    est_confirm(&E, 2 + WAIT);
    CHECK(veto(OWNER) == EST_OK && E.phase == EST_ALIVE && E.bond_state == EST_BOND_REFUNDED &&
              E.epoch == 2,
          "E1 living veto in CONFIRMED");
    /* fraud */
    report(0, 900, 3, A0);
    CHECK(est_fraud_finding(0, 0, s, 32) == EST_ERR_ARG &&
              est_fraud_finding(&E, 0, 0, 32) == EST_ERR_ARG,
          "fraud args");
    CHECK(fraud(5, G0) == EST_ERR_ARG && fraud(EST_COURT - 1, G0) == EST_ERR_ARG, "unknown finder");
    CHECK(fraud(1, G0) == EST_ERR_SIG && fraud(EST_COURT, G0) == EST_ERR_SIG, "finder's own key");
    CHECK(fraud(4, G0 + 4) == EST_OK && E.bond_state == EST_BOND_FORFEITED,
          "E1 guardian fraud finding forfeits");
    CHECK(fraud(EST_COURT, COURT) == EST_ERR_STATE, "already forfeited");
    CHECK(E.phase == EST_REPORTED, "a finding does not move the phase");
    CHECK(veto(OWNER) == EST_OK && E.bond_state == EST_BOND_FORFEITED,
          "a veto does not undo a forfeit");
    report(0, 900, 4, A0);
    CHECK(fraud(EST_COURT, COURT) == EST_OK && E.bond_state == EST_BOND_FORFEITED,
          "E1 court fraud finding");
    /* a forfeited bond stays forfeited at close */
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    est_confirm(&E, 4 + WAIT);
    est_begin(&E);
    CHECK(est_close(&E) == EST_OK && E.bond_state == EST_BOND_FORFEITED, "close keeps forfeit");
}

static void test_ruling(void)
{
    uint8_t s[32] = {0};
    fresh();
    CHECK(equal(EST_FARAID, 2) == EST_OK && est_advisory(&E), "E6 faraid manifest advisory");
    report(0, 10, 1, A0);
    CHECK(equal(EST_FARAID, 4) == EST_OK, "advisory manifest can change after a report");
    CHECK(rule(GW) == EST_ERR_STATE, "no ruling before confirmation");
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    est_confirm(&E, 1 + WAIT);
    CHECK(est_begin(&E) == EST_ERR_STATE, "E1 faraid needs a ruling to distribute");
    CHECK(est_rule(0, s, 32) == EST_ERR_ARG && est_rule(&E, 0, 32) == EST_ERR_ARG, "rule args");
    CHECK(rule(X) == EST_ERR_SIG && rule(COURT) == EST_ERR_SIG, "only the gateway rules");
    uint8_t m[EST_MSG], stale[32];
    est_msg(&E, EST_MSG_RULING, 0, m);
    sign(KEY[GW], m, sizeof m, stale);
    CHECK(equal(EST_FARAID, 3) == EST_OK, "manifest changed in CONFIRMED");
    CHECK(est_rule(&E, stale, 32) == EST_ERR_SIG, "a ruling binds the manifest digest");
    CHECK(rule(GW) == EST_OK && E.phase == EST_RULED && !est_advisory(&E), "E1 ruled");
    CHECK(equal(EST_FARAID, 2) == EST_ERR_STATE, "manifest fixed once ruled");
    CHECK(veto(OWNER) == EST_ERR_STATE, "no veto once ruled");
    CHECK(rule(GW) == EST_ERR_STATE, "rule twice");
    CHECK(est_begin(&E) == EST_OK && E.phase == EST_DISTRIBUTING, "distributing");
    fresh();
    report(0, 10, 1, A0);
    for (uint32_t g = 0; g < 3; g++) approve(g, G0 + g);
    est_confirm(&E, 1 + WAIT);
    CHECK(rule(GW) == EST_ERR_NOMANIFEST, "ruling with no manifest");
    CHECK(est_begin(&E) == EST_ERR_NOMANIFEST, "begin with no manifest");
    CHECK(equal(EST_FORCED, 2) == EST_OK && est_advisory(&E), "forced set in CONFIRMED");
    CHECK(equal(EST_COMMON, 2) == EST_ERR_STATE, "common law only while alive");
    CHECK(rule(GW) == EST_OK && est_begin(&E) == EST_OK, "forced: ruled, distributing");
    CHECK(equal(EST_FORCED, 2) == EST_ERR_STATE, "fixed while distributing");
}

static void test_manifest(void)
{
    fresh();
    uint64_t n[EST_MAX_HEIRS + 1], d[EST_MAX_HEIRS + 1];
    uint32_t ids[EST_MAX_HEIRS + 1] = {0};
    for (int i = 0; i <= (int) EST_MAX_HEIRS; i++) n[i] = 1, d[i] = EST_MAX_HEIRS;
    CHECK(est_set_manifest(0, EST_COMMON, 1, ids, n, d) == EST_ERR_ARG, "null estate");
    CHECK(est_set_manifest(&E, EST_COMMON, 1, 0, n, d) == EST_ERR_ARG, "null ids");
    CHECK(est_set_manifest(&E, EST_COMMON, 1, ids, 0, d) == EST_ERR_ARG, "null nums");
    CHECK(est_set_manifest(&E, EST_COMMON, 1, ids, n, 0) == EST_ERR_ARG, "null dens");
    CHECK(est_set_manifest(&E, EST_COMMON, 0, ids, n, d) == EST_ERR_ARG, "no heirs");
    CHECK(est_set_manifest(&E, EST_COMMON, EST_MAX_HEIRS + 1, ids, n, d) == EST_ERR_ARG,
          "too many heirs");
    CHECK(est_set_manifest(&E, EST_COMMON, EST_MAX_HEIRS, ids, n, d) == EST_OK && E.den == 16,
          "sixteen heirs");
    CHECK(est_set_manifest(&E, EST_NONE, 16, ids, n, d) == EST_ERR_ARG &&
              est_set_manifest(&E, (est_enclave_t) 4, 16, ids, n, d) == EST_ERR_ARG,
          "bad enclave");
    uint64_t n2[2] = {1, 1}, d2[2] = {2, 0};
    CHECK(est_set_manifest(&E, EST_COMMON, 2, ids, n2, d2) == EST_ERR_ARG, "den 0");
    d2[1] = 2;
    n2[1] = 3;
    CHECK(est_set_manifest(&E, EST_COMMON, 2, ids, n2, d2) == EST_ERR_ARG, "num above den");
    n2[1] = 2;
    CHECK(est_set_manifest(&E, EST_COMMON, 2, ids, n2, d2) == EST_ERR_SUM, "3/2: sum above 1");
    uint64_t n3[2] = {1, 1}, d3[2] = {2, 3};
    CHECK(est_set_manifest(&E, EST_COMMON, 2, ids, n3, d3) == EST_ERR_SUM, "5/6: below 1");
    CHECK(E.den == 16 && E.n == 16, "refusals change nothing");
    uint64_t n4[2] = {2, 2}, d4[2] = {4, 4};
    CHECK(est_set_manifest(&E, EST_COMMON, 2, ids, n4, d4) == EST_OK && E.den == 2 &&
              E.num[0] == 1 && E.num[1] == 1,
          "E2 lowest terms");
    uint64_t n5[2] = {0, 7}, d5[2] = {3, 7};
    CHECK(est_set_manifest(&E, EST_COMMON, 2, ids, n5, d5) == EST_OK && E.den == 1 &&
              E.num[0] == 0 && E.num[1] == 1,
          "a zero share is allowed");
    /* LCM overflow: two large coprime denominators */
    uint64_t n6[2] = {1, 1}, d6[2] = {UINT64_MAX, UINT64_MAX - 1};
    CHECK(est_set_manifest(&E, EST_COMMON, 2, ids, n6, d6) == EST_ERR_OVERFLOW, "lcm overflow");
    uint64_t n7[2] = {UINT64_MAX, UINT64_MAX}, d7[2] = {UINT64_MAX, UINT64_MAX};
    CHECK(est_set_manifest(&E, EST_COMMON, 2, ids, n7, d7) == EST_ERR_OVERFLOW, "sum overflow");
    uint64_t n8[2] = {UINT64_MAX - 1, 1}, d8[2] = {UINT64_MAX, UINT64_MAX};
    CHECK(est_set_manifest(&E, EST_COMMON, 2, ids, n8, d8) == EST_OK && E.den == UINT64_MAX,
          "the largest denominator");
    /* the digest covers enclave, ids and shares */
    uint8_t d0[32], dd[32];
    will3();
    memcpy(d0, E.digest, 32);
    will3();
    CHECK(!memcmp(d0, E.digest, 32), "digest deterministic");
    uint32_t idb[3] = {100, 101, 103};
    uint64_t nn[3] = {1, 1, 1}, dn[3] = {2, 3, 6};
    est_set_manifest(&E, EST_COMMON, 3, idb, nn, dn);
    CHECK(memcmp(d0, E.digest, 32) != 0, "digest covers ids");
    uint32_t idc[3] = {99, 101, 102};
    est_set_manifest(&E, EST_COMMON, 3, idc, nn, dn);
    CHECK(memcmp(d0, E.digest, 32) != 0, "digest covers the first heir");
    memcpy(dd, E.digest, 32);
    uint64_t dn2[3] = {3, 2, 6};
    est_set_manifest(&E, EST_COMMON, 3, idb, nn, dn2);
    CHECK(memcmp(dd, E.digest, 32) != 0, "digest covers shares");
    memcpy(dd, E.digest, 32);
    est_set_manifest(&E, EST_FARAID, 3, idb, nn, dn2);
    CHECK(memcmp(dd, E.digest, 32) != 0, "digest covers the enclave");
    uint64_t n9[4] = {1, 1, 1, 0}, d9[4] = {2, 3, 6, 1};
    memcpy(dd, E.digest, 32);
    est_set_manifest(&E, EST_FARAID, 4, IDS, n9, d9);
    uint8_t de[32];
    memcpy(de, E.digest, 32);
    est_set_manifest(&E, EST_FARAID, 3, IDS, n9, d9);
    CHECK(memcmp(de, E.digest, 32) != 0, "digest covers the heir count");
    uint64_t n10[2] = {1, 1}, d10[2] = {2, 2}, d11[2] = {4, 4}, n11[2] = {2, 2};
    est_set_manifest(&E, EST_FARAID, 2, IDS, n10, d10);
    memcpy(de, E.digest, 32);
    est_set_manifest(&E, EST_FARAID, 2, IDS, n11, d11);
    CHECK(!memcmp(de, E.digest, 32), "same fractions, same digest");
}

static void test_escrow_stream(void)
{
    uint8_t s[32] = {0};
    uint64_t out[EST_MAX_HEIRS], esc, rel = 99;
    fresh();
    CHECK(contest(0, 0, G0) == EST_ERR_ARG, "contest with no manifest");
    will3();
    CHECK(est_contest(0, 0, 0, s, 32) == EST_ERR_ARG && est_contest(&E, 0, 0, 0, 32) == EST_ERR_ARG,
          "contest args");
    CHECK(contest(3, 0, G0) == EST_ERR_ARG, "heir out of range");
    CHECK(contest(1, 5, G0) == EST_ERR_ARG, "unknown contester");
    CHECK(contest(1, 2, G0) == EST_ERR_SIG, "contester's own key");
    CHECK(contest(1, 2, G0 + 2) == EST_OK && E.contested[1] && E.ccount[1] == 1,
          "E2a guardian contests heir 1");
    CHECK(contest(1, EST_COURT, COURT) == EST_DUP, "already contested");
    to_distributing();
    CHECK(est_distribute(&E, 600, out, &esc) == EST_OK && out[0] == 300 && out[1] == 0 &&
              out[2] == 100 && esc == 200 && E.escrow[1] == 200 && E.escrow_total == 200,
          "E2a contested part to escrow");
    CHECK(est_distribute(&E, 6, out, 0) == EST_OK && E.escrow[1] == 202 && E.escrow_total == 202,
          "escrow accumulates (principal only)");
    CHECK(est_resolve(0, 1, s, 32, &rel) == EST_ERR_ARG &&
              est_resolve(&E, 1, 0, 32, &rel) == EST_ERR_ARG &&
              est_resolve(&E, 1, s, 32, 0) == EST_ERR_ARG &&
              est_resolve(&E, 3, s, 32, &rel) == EST_ERR_ARG,
          "resolve args");
    CHECK(resolve(0, COURT, &rel) == EST_ERR_STATE, "heir 0 not contested");
    CHECK(resolve(1, G0, &rel) == EST_ERR_SIG && rel == 99, "only the court resolves");
    uint8_t old[32];
    sig(EST_MSG_RESOLVE, est_contest_value(&E, 1), COURT, old);
    CHECK(resolve(1, COURT, &rel) == EST_OK && rel == 202 && E.escrow[1] == 0 &&
              E.escrow_total == 0 && !E.contested[1] && E.ccount[1] == 2,
          "E2a principal passes to the heir");
    CHECK(est_distribute(&E, 6, out, &esc) == EST_OK && out[1] == 2 && esc == 0,
          "resolved heir paid directly");
    CHECK(contest(1, EST_COURT, COURT) == EST_OK, "contest again");
    CHECK(est_resolve(&E, 1, old, 32, &rel) == EST_ERR_SIG, "old resolution not replayed");
    CHECK(contest(0, 0, G0) == EST_OK, "heir 0 too");
    est_distribute(&E, 6, out, &esc);
    CHECK(esc == 5 && E.escrow[0] == 3 && E.escrow[1] == 2 && E.escrow_total == 5 && out[2] == 1,
          "two in escrow");
    CHECK(resolve(0, COURT, &rel) == EST_OK && rel == 3 && E.escrow_total == 2,
          "one released, one held");
    est_close(&E);
    CHECK(contest(2, 0, G0) == EST_ERR_STATE, "no contest once closed");
    /* a new manifest clears contests */
    fresh();
    will3();
    contest(2, 0, G0);
    will3();
    CHECK(!E.contested[2], "new manifest clears contests");

    /* streaming: many small payments, cumulative floor exact */
    fresh();
    will3();
    CHECK(est_stream(&E, 1, out, 0) == EST_ERR_STATE, "stream before distribution");
    to_distributing();
    uint64_t tot[3] = {0, 0, 0}, T = 0;
    int bad = 0;
    for (uint32_t i = 0; i < 1000; i++) {
        uint64_t a = (i % 7) + 1;
        T += a;
        if (est_stream(&E, a, out, &esc) != EST_OK || esc != 0) bad++;
        for (int h = 0; h < 3; h++) tot[h] += out[h];
        if (tot[0] != T * 3 / 6 || tot[1] != T * 2 / 6 || tot[2] != T / 6) bad++;
        if (est_stream_held(&E) != T - tot[0] - tot[1] - tot[2] || est_stream_held(&E) >= 3) bad++;
    }
    CHECK(bad == 0 && E.stream_total == T, "E2 stream: cumulative floor(T * share) every step");
    CHECK(est_stream(&E, 0, out, 0) == EST_OK && out[0] == 0 && out[1] == 0 && out[2] == 0,
          "zero royalty");
    CHECK(est_stream(0, 1, out, 0) == EST_ERR_ARG && est_stream(&E, 1, 0, 0) == EST_ERR_ARG &&
              est_stream_held(0) == 0,
          "stream args");
    contest(0, 0, G0);
    uint64_t before = E.paid[0];
    CHECK(est_stream(&E, 6, out, &esc) == EST_OK && out[0] == 0 && esc == E.paid[0] - before &&
              E.escrow[0] == esc && esc == 3,
          "contested royalties to escrow");
    CHECK(est_stream(&E, UINT64_MAX, out, 0) == EST_ERR_OVERFLOW && E.stream_total == T + 6,
          "stream total overflow refused, nothing changed");

    /* escrow overflow: per heir, and in total */
    fresh();
    equal(EST_COMMON, 2);
    contest(0, 0, G0);
    contest(1, 0, G0);
    to_distributing();
    CHECK(est_distribute(&E, UINT64_MAX, out, &esc) == EST_OK && esc == UINT64_MAX &&
              E.escrow_total == UINT64_MAX,
          "everything in escrow");
    CHECK(est_distribute(&E, 2, out, &esc) == EST_ERR_OVERFLOW && E.escrow_total == UINT64_MAX &&
              E.escrow[0] == UINT64_MAX / 2 + 1 && E.escrow[1] == UINT64_MAX / 2,
          "total overflow refused, nothing changed");
    resolve(1, COURT, &rel);
    CHECK(est_distribute(&E, UINT64_MAX, out, &esc) == EST_ERR_OVERFLOW &&
              E.escrow[0] == UINT64_MAX / 2 + 1,
          "per-heir overflow refused");
    est_distribute(&E, 2, out, &esc);
    CHECK(est_stream(&E, UINT64_MAX, out, &esc) == EST_ERR_OVERFLOW && E.stream_total == 0 &&
              E.paid[1] == 0,
          "stream escrow overflow: nothing changed");
    CHECK(est_stream(&E, 2, out, &esc) == EST_OK && out[1] == 1 && esc == 1 && E.paid[0] == 1,
          "stream within bounds");
}

static void test_bounds(void)
{
    uint64_t out[EST_MAX_HEIRS + 1], esc;
    /* stale slots past n must not count: 4 heirs, then 3 */
    fresh();
    equal(EST_COMMON, 4);
    will3();
    to_distributing();
    out[3] = 12345;
    CHECK(est_distribute(&E, 6, out, &esc) == EST_OK && out[0] == 3 && out[1] == 2 && out[2] == 1 &&
              out[3] == 12345,
          "only n heirs paid");
    CHECK(est_stream(&E, 6, out, &esc) == EST_OK && out[3] == 12345 && E.paid[3] == 0 &&
              est_stream_held(&E) == 0,
          "only n heirs streamed");
    /* sixteen heirs, all paths */
    fresh();
    equal(EST_COMMON, 16);
    for (uint32_t i = 0; i < 16; i++) contest(i, 0, G0);
    to_distributing();
    CHECK(est_distribute(&E, 33, out, &esc) == EST_OK && esc == 33 && E.escrow[15] == 2 &&
              E.escrow[0] == 3,
          "sixteen contested heirs");
    uint64_t rel;
    resolve(15, COURT, &rel);
    CHECK(est_stream(&E, 32, out, &esc) == EST_OK && esc == 30 && out[15] == 2 && E.paid[15] == 2 &&
              est_stream_held(&E) == 0,
          "sixteen heirs stream");
}

static bool fr(const est_frac_t *f, uint64_t n, uint64_t d)
{
    return f->num == n && f->den == d;
}

static void test_faraid(void)
{
    est_faraid_t o;
    est_faraid_heirs_t h;
#define H(hu, wi, fa, mo, so, da) (h = (est_faraid_heirs_t){hu, wi, fa, mo, so, da, false})
    /* Minbariyya: wife, father, mother, two daughters (awl to 27) */
    H(0, 1, 1, 1, 0, 2);
    CHECK(est_faraid(&h, &o) == EST_OK && o.awl && !o.radd && o.advisory, "Minbariyya awl");
    CHECK(fr(&o.share[EST_FA_WIVES], 1, 9) && fr(&o.share[EST_FA_FATHER], 4, 27) &&
              fr(&o.share[EST_FA_MOTHER], 4, 27) && fr(&o.share[EST_FA_DAUGHTERS], 16, 27) &&
              fr(&o.share[EST_FA_HUSBAND], 0, 1) && fr(&o.share[EST_FA_REST], 0, 1),
          "Minbariyya: 3/27, 4/27, 4/27, 16/27");
    H(1, 0, 1, 1, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && !o.awl && !o.radd && fr(&o.share[EST_FA_HUSBAND], 1, 2) &&
              fr(&o.share[EST_FA_FATHER], 1, 3) && fr(&o.share[EST_FA_MOTHER], 1, 6),
          "Umariyyatan (husband): 1/2, 1/3, 1/6");
    H(0, 1, 1, 1, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_WIVES], 1, 4) &&
              fr(&o.share[EST_FA_MOTHER], 1, 4) && fr(&o.share[EST_FA_FATHER], 1, 2),
          "Umariyyatan (wife): 1/4, 1/4, 1/2");
    H(0, 0, 0, 1, 0, 1);
    CHECK(est_faraid(&h, &o) == EST_OK && o.radd && !o.awl &&
              fr(&o.share[EST_FA_DAUGHTERS], 3, 4) && fr(&o.share[EST_FA_MOTHER], 1, 4),
          "radd: mother and daughter 1/4, 3/4");
    H(0, 1, 0, 0, 0, 1);
    CHECK(est_faraid(&h, &o) == EST_OK && o.radd && fr(&o.share[EST_FA_WIVES], 1, 8) &&
              fr(&o.share[EST_FA_DAUGHTERS], 7, 8),
          "radd not to the spouse: wife 1/8, daughter 7/8");
    H(0, 0, 0, 0, 1, 1);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_SONS], 2, 3) &&
              fr(&o.share[EST_FA_DAUGHTERS], 1, 3),
          "son and daughter 2:1");
    H(1, 0, 0, 0, 1, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_HUSBAND], 1, 4) &&
              fr(&o.share[EST_FA_SONS], 3, 4),
          "husband and son 1/4, 3/4");
    H(1, 0, 1, 0, 0, 1);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_HUSBAND], 1, 4) &&
              fr(&o.share[EST_FA_DAUGHTERS], 1, 2) && fr(&o.share[EST_FA_FATHER], 1, 4),
          "husband, father, daughter: father 1/6 + 1/12");
    /* more cases */
    H(0, 0, 0, 1, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && o.radd && fr(&o.share[EST_FA_MOTHER], 1, 1),
          "mother alone: radd to all");
    H(0, 0, 1, 0, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && !o.radd && fr(&o.share[EST_FA_FATHER], 1, 1),
          "father alone: residue");
    H(0, 0, 1, 1, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_MOTHER], 1, 3) &&
              fr(&o.share[EST_FA_FATHER], 2, 3),
          "parents: 1/3, 2/3");
    H(0, 0, 1, 1, 1, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_MOTHER], 1, 6) &&
              fr(&o.share[EST_FA_FATHER], 1, 6) && fr(&o.share[EST_FA_SONS], 2, 3),
          "parents and a son: 1/6, 1/6, 2/3");
    H(0, 0, 1, 1, 0, 2);
    CHECK(est_faraid(&h, &o) == EST_OK && !o.awl && !o.radd && fr(&o.share[EST_FA_MOTHER], 1, 6) &&
              fr(&o.share[EST_FA_FATHER], 1, 6) && fr(&o.share[EST_FA_DAUGHTERS], 2, 3),
          "parents and 2 daughters: exactly 1, no awl, no residue");
    H(0, 0, 0, 1, 2, 1);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_MOTHER], 1, 6) &&
              fr(&o.share[EST_FA_SONS], 2, 3) && fr(&o.share[EST_FA_DAUGHTERS], 1, 6),
          "mother, 2 sons, daughter: 5/6 split 4:1");
    H(0, 0, 0, 1, 0, 2);
    CHECK(est_faraid(&h, &o) == EST_OK && o.radd && fr(&o.share[EST_FA_DAUGHTERS], 4, 5) &&
              fr(&o.share[EST_FA_MOTHER], 1, 5),
          "two daughters and mother: radd 4/5, 1/5");
    H(0, 1, 0, 1, 0, 1);
    CHECK(est_faraid(&h, &o) == EST_OK && o.radd && fr(&o.share[EST_FA_WIVES], 1, 8) &&
              fr(&o.share[EST_FA_MOTHER], 7, 32) && fr(&o.share[EST_FA_DAUGHTERS], 21, 32),
          "wife, mother, daughter: radd 1/8, 7/32, 21/32");
    H(1, 0, 0, 1, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && o.radd && fr(&o.share[EST_FA_HUSBAND], 1, 2) &&
              fr(&o.share[EST_FA_MOTHER], 1, 2),
          "husband, mother: radd 1/2, 1/2");
    H(1, 0, 0, 1, 0, 2);
    CHECK(est_faraid(&h, &o) == EST_OK && o.awl && fr(&o.share[EST_FA_HUSBAND], 3, 13) &&
              fr(&o.share[EST_FA_DAUGHTERS], 8, 13) && fr(&o.share[EST_FA_MOTHER], 2, 13),
          "husband, mother, 2 daughters: awl to 13");
    H(0, 2, 0, 0, 1, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_WIVES], 1, 8) &&
              fr(&o.share[EST_FA_SONS], 7, 8),
          "two wives and a son");
    H(1, 0, 1, 1, 0, 0);
    h.father = false;
    h.wives = 0;
    H(0, 0, 1, 0, 0, 2);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_DAUGHTERS], 2, 3) &&
              fr(&o.share[EST_FA_FATHER], 1, 3),
          "father and 2 daughters: 1/6 + 1/6");
    H(1, 0, 1, 1, 0, 2);
    CHECK(est_faraid(&h, &o) == EST_OK && o.awl && fr(&o.share[EST_FA_HUSBAND], 1, 5) &&
              fr(&o.share[EST_FA_FATHER], 2, 15) && fr(&o.share[EST_FA_DAUGHTERS], 8, 15),
          "husband, parents, 2 daughters: awl to 15");
    H(1, 0, 0, 0, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && !o.radd && fr(&o.share[EST_FA_HUSBAND], 1, 2) &&
              fr(&o.share[EST_FA_REST], 1, 2),
          "husband alone: half unallocated");
    H(0, 4, 0, 0, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_WIVES], 1, 4) &&
              fr(&o.share[EST_FA_REST], 3, 4),
          "four wives alone");
    H(0, 0, 0, 0, 16, 16);
    CHECK(est_faraid(&h, &o) == EST_OK && fr(&o.share[EST_FA_SONS], 2, 3), "counts at the cap");
    /* every combination: the class shares sum to exactly 1, in lowest terms */
    int bad = 0, n24 = 0;
    for (int hu = 0; hu < 2; hu++)
        for (uint32_t wi = 0; wi <= 4; wi++)
            for (int fa = 0; fa < 2; fa++)
                for (int mo = 0; mo < 2; mo++)
                    for (uint32_t so = 0; so <= 3; so++)
                        for (uint32_t da = 0; da <= 3; da++) {
                            h = (est_faraid_heirs_t){hu, wi, fa, mo, so, da, false};
                            est_status_t st = est_faraid(&h, &o);
                            if ((hu && wi) || !(hu || wi || fa || mo || so || da)) {
                                bad += st != EST_ERR_ARG;
                                continue;
                            }
                            uint64_t L = 1, sum = 0;
                            for (int i = 0; i < EST_FA_N; i++) {
                                uint64_t a = L, b = o.share[i].den;
                                while (b) {
                                    uint64_t t2 = a % b;
                                    a = b;
                                    b = t2;
                                }
                                L = L / a * o.share[i].den;
                            }
                            for (int i = 0; i < EST_FA_N; i++)
                                sum += o.share[i].num * (L / o.share[i].den);
                            bad += st != EST_OK || sum != L || (o.awl && o.radd);
                            n24 += o.share[EST_FA_REST].num != 0;
                        }
    CHECK(bad == 0 && n24 == 5, "every combination sums to 1 (rest only for a lone spouse)");
    /* refusals */
    H(0, 0, 1, 0, 1, 0);
    h.others = true;
    CHECK(est_faraid(&h, &o) == EST_ERR_UNSUPPORTED, "F1 other relatives unsupported");
    H(1, 1, 0, 0, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_ERR_ARG, "husband and wife");
    H(0, 5, 0, 0, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_ERR_ARG, "five wives");
    H(0, 0, 0, 0, 17, 0);
    CHECK(est_faraid(&h, &o) == EST_ERR_ARG, "too many sons");
    H(0, 0, 0, 0, 0, 17);
    CHECK(est_faraid(&h, &o) == EST_ERR_ARG, "too many daughters");
    H(0, 0, 0, 0, 0, 0);
    CHECK(est_faraid(&h, &o) == EST_ERR_ARG, "nobody");
    CHECK(est_faraid(0, &o) == EST_ERR_ARG && est_faraid(&h, 0) == EST_ERR_ARG, "null");

    /* the manifest, per person */
    fresh();
    uint32_t id[EST_MAX_HEIRS] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    H(0, 1, 1, 1, 0, 2);
    CHECK(est_set_faraid(&E, &h, id) == EST_OK && E.enclave == EST_FARAID && E.n == 5 &&
              E.den == 27,
          "Minbariyya manifest");
    CHECK(E.num[0] == 3 && E.num[1] == 4 && E.num[2] == 4 && E.num[3] == 8 && E.num[4] == 8 &&
              E.id[4] == 5,
          "daughters 8/27 each");
    H(0, 2, 0, 0, 1, 0);
    CHECK(est_set_faraid(&E, &h, id) == EST_OK && E.den == 16 && E.num[0] == 1 && E.num[1] == 1 &&
              E.num[2] == 14,
          "two wives 1/16 each, son 7/8");
    H(1, 0, 0, 0, 2, 2);
    CHECK(est_set_faraid(&E, &h, id) == EST_OK && E.den == 8 && E.num[0] == 2 && E.num[1] == 2 &&
              E.num[2] == 2 && E.num[3] == 1 && E.num[4] == 1,
          "husband, 2 sons, 2 daughters: 1/4, 1/4, 1/4, 1/8, 1/8");
    H(0, 0, 1, 1, 0, 0);
    CHECK(est_set_faraid(&E, &h, id) == EST_OK && E.n == 2 && E.num[0] == 2 && E.num[1] == 1,
          "father first, then mother");
    H(1, 0, 0, 0, 0, 0);
    CHECK(est_set_faraid(&E, &h, id) == EST_ERR_UNSUPPORTED, "F5 unallocated rest refused");
    H(0, 0, 0, 1, 16, 0);
    CHECK(est_set_faraid(&E, &h, id) == EST_ERR_FULL, "more persons than heirs");
    H(0, 0, 0, 0, 16, 0);
    CHECK(est_set_faraid(&E, &h, id) == EST_OK && E.n == 16 && E.den == 16, "16 sons");
    h.others = true;
    CHECK(est_set_faraid(&E, &h, id) == EST_ERR_UNSUPPORTED, "unsupported passes through");
    CHECK(est_set_faraid(0, &h, id) == EST_ERR_ARG && est_set_faraid(&E, &h, 0) == EST_ERR_ARG &&
              est_set_faraid(&E, 0, id) == EST_ERR_ARG,
          "null");
#undef H
}

static void test_forced(void)
{
    est_forced_table_t t;
    est_forced_out_t o;
    est_forced_init(&t);
    est_forced_init(0);
    const est_forced_row_t *frr = est_forced_find(&t, EST_JUR('F', 'R'));
    CHECK(frr && t.n == 1 && frr->n_steps == 3 && frr->reserve[2].num == 3, "France row");
    CHECK(t.row[1].jur == 0 && t.row[7].n_steps == 0 && t.row[0].reserve[3].den == 0,
          "the rest of the table is empty");
    CHECK(!est_forced_find(&t, EST_JUR('D', 'E')) && !est_forced_find(0, EST_JUR('F', 'R')),
          "unknown");
    uint64_t g34[1] = {3};
    /* 1 child, gift 3/4: reserve 1/2, gift cut to 1/2 */
    CHECK(est_forced_shares(frr, 1, 1, g34, 4, &o) == EST_OK && o.reduced && o.advisory &&
              o.n == 2 && o.den == 2 && o.num[0] == 1 && o.num[1] == 1,
          "FR 1 child: 1/2, gift reduced to 1/2");
    uint64_t g12[1] = {1};
    CHECK(est_forced_shares(frr, 1, 1, g12, 2, &o) == EST_OK && !o.reduced && o.den == 2 &&
              o.num[0] == 1 && o.num[1] == 1,
          "gift exactly the quota: not reduced");
    CHECK(est_forced_shares(frr, 2, 1, g12, 2, &o) == EST_OK && o.reduced && o.den == 3 &&
              o.num[0] == 1 && o.num[1] == 1 && o.num[2] == 1,
          "FR 2 children: 1/3 each, gift cut to 1/3");
    uint64_t g13[1] = {1};
    CHECK(est_forced_shares(frr, 3, 1, g13, 3, &o) == EST_OK && o.reduced && o.den == 4 &&
              o.num[0] == 1 && o.num[2] == 1 && o.num[3] == 1,
          "FR 3 children: 1/4 each, gift cut to 1/4");
    uint64_t g2[2] = {2, 1};
    CHECK(est_forced_shares(frr, 5, 2, g2, 6, &o) == EST_OK && o.reduced && o.den == 60 &&
              o.num[0] == 9 && o.num[4] == 9 && o.num[5] == 10 && o.num[6] == 5,
          "FR 5 children: 3/20 each; gifts 1/3, 1/6 cut to 1/6, 1/12");
    CHECK(est_forced_shares(frr, 4, 1, g13, 3, &o) == EST_OK && o.reduced && o.den == 16 &&
              o.num[0] == 3 && o.num[3] == 3 && o.num[4] == 4,
          "FR 4 children: 3/16 each, gift cut to 1/4");
    uint64_t g16[1] = {1};
    CHECK(est_forced_shares(frr, 2, 1, g16, 6, &o) == EST_OK && !o.reduced && o.den == 12 &&
              o.num[0] == 5 && o.num[1] == 5 && o.num[2] == 2,
          "gift within the quota; the unused quota to the children");
    CHECK(est_forced_shares(frr, 3, 0, 0, 1, &o) == EST_OK && o.den == 3 && o.num[0] == 1 &&
              o.num[2] == 1 && !o.reduced,
          "no will: equal");
    uint64_t g11[2] = {1, 1};
    CHECK(est_forced_shares(frr, 0, 2, g11, 2, &o) == EST_OK && o.den == 2 && o.num[1] == 1,
          "no children: the will as given");
    CHECK(est_forced_shares(frr, 0, 1, g12, 2, &o) == EST_ERR_SUM, "no children: must sum to 1");
    uint64_t g54[1] = {5};
    CHECK(est_forced_shares(frr, 1, 1, g54, 4, &o) == EST_ERR_SUM, "gifts above the estate");
    CHECK(est_forced_shares(0, 1, 0, 0, 1, &o) == EST_ERR_ARG &&
              est_forced_shares(frr, 1, 0, 0, 1, 0) == EST_ERR_ARG &&
              est_forced_shares(frr, 1, 1, 0, 1, &o) == EST_ERR_ARG &&
              est_forced_shares(frr, 1, 0, 0, 0, &o) == EST_ERR_ARG &&
              est_forced_shares(frr, 0, 0, 0, 1, &o) == EST_ERR_ARG &&
              est_forced_shares(frr, 17, 0, 0, 1, &o) == EST_ERR_ARG &&
              est_forced_shares(frr, 16, 0, 0, 1, &o) == EST_OK &&
              est_forced_shares(frr, 1, UINT32_MAX, g12, 2, &o) == EST_ERR_ARG,
          "shares args");
    /* rows */
    est_forced_row_t r = {EST_JUR('X', 'A'), 2, {{1, 3}, {1, 1}, {0, 1}, {0, 1}}};
    CHECK(est_forced_register(&t, &r) == EST_OK && est_forced_find(&t, r.jur) == &t.row[1],
          "row registered");
    CHECK(est_forced_register(&t, &r) == EST_ERR_ARG, "duplicate row");
    const est_forced_row_t *xa = &t.row[1];
    uint64_t g1[1] = {1};
    CHECK(est_forced_shares(xa, 3, 1, g1, 2, &o) == EST_OK && o.reduced && o.den == 3 &&
              o.num[0] == 1 && o.num[3] == 0,
          "beyond the last step: reserve 1/1, gift cut to 0");
    CHECK(est_forced_shares(xa, 2, 0, 0, 1, &o) == EST_OK && o.den == 2, "full reserve, no will");
    CHECK(est_forced_shares(xa, 1, 1, g34, 4, &o) == EST_OK && o.reduced && o.den == 3 &&
              o.num[0] == 1 && o.num[1] == 2,
          "step 1: reserve 1/3");
    est_forced_row_t bad = r;
    bad.jur = EST_JUR('X', 'B');
    CHECK(est_forced_register(0, &r) == EST_ERR_ARG && est_forced_register(&t, 0) == EST_ERR_ARG,
          "register null");
    bad.reserve[0] = (est_frac_t){1, 0};
    CHECK(est_forced_register(&t, &bad) == EST_ERR_ARG, "step 0 checked");
    bad.reserve[0] = (est_frac_t){1, 3};
    bad.n_steps = 0;
    CHECK(est_forced_register(&t, &bad) == EST_ERR_ARG &&
              est_forced_shares(&bad, 1, 0, 0, 1, &o) == EST_ERR_ARG,
          "no steps");
    bad.n_steps = EST_FORCED_STEPS + 1;
    CHECK(est_forced_register(&t, &bad) == EST_ERR_ARG &&
              est_forced_shares(&bad, 1, 0, 0, 1, &o) == EST_ERR_ARG,
          "too many steps");
    bad.n_steps = EST_FORCED_STEPS;
    bad.reserve[3] = (est_frac_t){1, 0};
    CHECK(est_forced_register(&t, &bad) == EST_ERR_ARG &&
              est_forced_shares(&bad, 4, 0, 0, 1, &o) == EST_ERR_ARG,
          "den 0 (last step checked)");
    bad.reserve[3] = (est_frac_t){2, 1};
    CHECK(est_forced_register(&t, &bad) == EST_ERR_ARG &&
              est_forced_shares(&bad, 5, 0, 0, 1, &o) == EST_ERR_ARG,
          "reserve above 1");
    bad.reserve[3] = (est_frac_t){1, 1};
    CHECK(est_forced_shares(&bad, 4, 0, 0, 1, &o) == EST_OK, "four steps used");
    CHECK(est_forced_register(&t, &bad) == EST_OK && t.n == 3, "four steps registered");
    for (uint32_t i = 3; i < EST_MAX_JUR; i++) {
        bad.jur = (uint16_t) (0x100 + i);
        CHECK(est_forced_register(&t, &bad) == EST_OK, "fill");
    }
    bad.jur = 0x2000;
    CHECK(est_forced_register(&t, &bad) == EST_ERR_FULL && t.n == EST_MAX_JUR, "table full");
    /* overflow */
    uint64_t big[2] = {UINT64_MAX, 1};
    CHECK(est_forced_shares(frr, 1, 2, big, UINT64_MAX, &o) == EST_ERR_OVERFLOW, "gift sum");
    uint64_t h63[1] = {1ull << 63};
    CHECK(est_forced_shares(frr, 1, 1, h63, 1ull << 63, &o) == EST_ERR_OVERFLOW, "A rd");
    est_forced_row_t w = {1, 1, {{1, 1ull << 62}, {0, 1}, {0, 1}, {0, 1}}};
    uint64_t z[1] = {0};
    CHECK(est_forced_shares(&w, 1, 1, z, 8, &o) == EST_ERR_OVERFLOW, "(rd - rn) W");
    CHECK(est_forced_shares(&w, 1, 1, z, 3, &o) == EST_OK && o.den == 1 && o.num[0] == 1,
          "(rd - rn) W fits");
    est_forced_row_t w2 = {2, 1, {{1, 1ull << 62}, {0, 1}, {0, 1}, {0, 1}}};
    uint64_t one[1] = {1};
    CHECK(est_forced_shares(&w2, 5, 1, one, 1, &o) == EST_ERR_OVERFLOW, "(rd - rn) c");
    est_forced_row_t w3 = {3, 1, {{(1ull << 62) - 1, 1ull << 62}, {0, 1}, {0, 1}, {0, 1}}};
    CHECK(est_forced_shares(&w3, 5, 1, one, 1, &o) == EST_ERR_OVERFLOW, "rd A c");
    CHECK(est_forced_shares(&w3, 3, 1, one, 1, &o) == EST_OK && o.reduced && o.den == 1ull << 62 &&
              o.num[0] == ((1ull << 62) - 1) / 3 && o.num[3] == 1,
          "rd A c fits");
    CHECK(est_forced_shares(frr, 5, 1, one, 1ull << 62, &o) == EST_ERR_OVERFLOW, "W c");
    CHECK(est_forced_shares(frr, 3, 1, one, 1ull << 62, &o) == EST_OK && !o.reduced, "W c fits");

    /* into the estate */
    fresh();
    uint32_t kids[5] = {1, 2, 3, 4, 5}, friends[2] = {8, 9};
    est_forced_init(&t);
    CHECK(est_set_forced(&E, &t, EST_JUR('F', 'R'), 5, kids, 2, friends, g2, 6) == EST_OK &&
              E.enclave == EST_FORCED && E.n == 7 && E.den == 60 && E.id[0] == 1 && E.id[5] == 8 &&
              E.id[6] == 9 && E.num[6] == 5 && est_advisory(&E),
          "E5 France, 5 children, manifest advisory");
    CHECK(est_set_forced(&E, &t, EST_JUR('D', 'E'), 1, kids, 0, 0, 0, 1) == EST_ERR_UNKNOWN,
          "unknown jurisdiction");
    CHECK(est_set_forced(&E, 0, EST_JUR('F', 'R'), 1, kids, 0, 0, 0, 1) == EST_ERR_UNKNOWN,
          "no table");
    CHECK(est_set_forced(0, &t, EST_JUR('F', 'R'), 1, kids, 0, 0, 0, 1) == EST_ERR_ARG &&
              est_set_forced(&E, &t, EST_JUR('F', 'R'), 1, 0, 0, 0, 0, 1) == EST_ERR_ARG &&
              est_set_forced(&E, &t, EST_JUR('F', 'R'), 1, kids, 1, 0, g12, 2) == EST_ERR_ARG,
          "set args");
    CHECK(est_set_forced(&E, &t, EST_JUR('F', 'R'), 0, 0, 1, friends, g12, 2) == EST_ERR_SUM &&
              E.n == 7,
          "errors pass through");
    CHECK(est_set_forced(&E, &t, EST_JUR('F', 'R'), 0, 0, 1, friends, g1, 1) == EST_OK &&
              E.n == 1 && E.id[0] == 8 && E.den == 1,
          "no children, sole legatee");
}

int main(void)
{
    setup_cfg();
    test_init();
    test_msg();
    test_common_law();
    test_ties();
    test_veto_bond();
    test_ruling();
    test_manifest();
    test_escrow_stream();
    test_bounds();
    test_faraid();
    test_forced();
    printf("PASS %d FAIL %d\n", pass, fail);
    return fail ? 1 : 0;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zab.c — ZAB-Logic against the real libzxv. */
#include "zab_test.h"

typedef struct {
    zxv_ctx *ctx;
    zxv_cap *cap;
    zab_swarm_t *sw;
} env_t;

static int env_open(env_t *e, uint32_t levels, uint64_t T, uint32_t qcap)
{
    memset(e, 0, sizeof *e);
    if (zxv_ctx_create(&e->ctx) != ZXV_OK) return -1;
    if (zxv_cap_create(e->ctx, 7, NULL, ZXV_CAP_ALL, &e->cap) != ZXV_OK) return -1;
    return zab_swarm_create(e->ctx, e->cap, levels, T, qcap, &e->sw) == ZXV_OK ? 0 : -1;
}

static void env_close(env_t *e)
{
    zab_swarm_destroy(e->sw);
    zxv_cap_destroy(e->cap);
    zxv_ctx_destroy(e->ctx);
    memset(e, 0, sizeof *e);
}

static int32_t add(env_t *e, uint32_t id, uint32_t level, uint32_t cp, uint32_t k)
{
    zab_agent_t a = {id, level, cp, k};
    return zab_agent_add(e->sw, &a);
}

static int32_t propose(env_t *e, uint32_t id, uint8_t tag, uint64_t tokens, const char *work,
                       zab_decision_t *d)
{
    zab_proposal_t p = {id, tag, tokens, (const uint8_t *) work, work ? strlen(work) : 0};
    return zab_propose(e->sw, &p, d);
}

static uint64_t left_of(env_t *e, uint32_t id)
{
    uint64_t a = 0, u = 0;
    if (zab_agent_allotment(e->sw, id, &a, NULL, &u) != ZXV_OK) return UINT64_MAX;
    return a - u;
}

/* ------------------------------------------------------------------ */

static void test_basics(void)
{
    CHECK(ZAB_VERSION_MAJOR == 0 && ZAB_VERSION_MINOR == 1, "kit version");
    static const char *names[] = {"admitted", "quarantined", "held", "refused", "deferred",
                                  "over-budget"};
    for (int32_t i = 0; i < ZAB_OUTCOMES; i++)
        CHECK(strcmp(zab_outcome_name(i), names[i]) == 0, "outcome name %d", i);
    CHECK(strcmp(zab_outcome_name(-1), "INVALID") == 0, "outcome name -1");
    CHECK(strcmp(zab_outcome_name(ZAB_OUTCOMES), "INVALID") == 0, "outcome name 6");
    uint32_t feats = 0;
    CHECK_RC(zxv_features(&feats), ZXV_OK);
    CHECK((feats & ZXV_FEAT_SWARM) && (feats & ZXV_FEAT_HEX) && (feats & ZXV_FEAT_HASH),
          "libzxv has SWARM, HEX and HASH");
    /* last_error: two-call pattern */
    zab_swarm_t *sw = NULL;
    CHECK_RC(zab_swarm_create(NULL, NULL, 1, 1, 1, &sw), ZXV_E_ARG);
    size_t n = 0;
    CHECK_RC(zab_last_error(NULL, 0, &n), ZXV_E_BUFFER);
    CHECK(n > 0, "error detail present");
    char small[2];
    CHECK_RC(zab_last_error(small, sizeof small, &n), ZXV_E_BUFFER);
}

static void test_create(void)
{
    zxv_ctx *ctx = NULL, *other = NULL;
    zxv_cap *cap = NULL, *ocap = NULL, *no_swarm = NULL, *no_attest = NULL;
    zab_swarm_t *sw = NULL;
    CHECK_RC(zxv_ctx_create(&ctx), ZXV_OK);
    CHECK_RC(zxv_ctx_create(&other), ZXV_OK);
    CHECK_RC(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap), ZXV_OK);
    CHECK_RC(zxv_cap_create(other, 1, NULL, ZXV_CAP_ALL, &ocap), ZXV_OK);
    CHECK_RC(zxv_cap_attenuate(cap, ZXV_CAP_ALL & ~ZXV_CAP_SWARM, &no_swarm), ZXV_OK);
    CHECK_RC(zxv_cap_attenuate(cap, ZXV_CAP_ALL & ~ZXV_CAP_ATTEST, &no_attest), ZXV_OK);

    CHECK_RC(zab_swarm_create(ctx, cap, 0, 100, 4, &sw), ZXV_E_ARG);
    CHECK_RC(zab_swarm_create(ctx, cap, 9, 100, 4, &sw), ZXV_E_ARG);
    CHECK_RC(zab_swarm_create(ctx, cap, 3, 100, 4, NULL), ZXV_E_ARG);
    CHECK_RC(zab_swarm_create(ctx, NULL, 3, 100, 4, &sw), ZXV_E_ARG);
    CHECK_RC(zab_swarm_create(ctx, no_swarm, 3, 100, 4, &sw), ZXV_E_CAP);
    CHECK_RC(zab_swarm_create(ctx, no_attest, 3, 100, 4, &sw), ZXV_E_CAP);
    CHECK_RC(zab_swarm_create(ctx, ocap, 3, 100, 4, &sw), ZXV_E_CAP); /* another context's cap */
    CHECK_RC(zab_swarm_create(ctx, cap, 3, (UINT64_C(1) << 48) + 1, 4, &sw), ZXV_E_ARG);
    CHECK_RC(zab_swarm_create(ctx, cap, 3, 100, 0, &sw), ZXV_E_ARG);
    CHECK_RC(zab_swarm_create(ctx, cap, 3, 100, ZXV_HEXLOG_MAX_RECORDS + 1, &sw), ZXV_E_ARG);
    CHECK(sw == NULL, "no swarm on failure");
    for (uint32_t L = 1; L <= 8; L++) {
        CHECK_RC(zab_swarm_create(ctx, cap, L, UINT64_C(1) << 48, 1, &sw), ZXV_OK);
        CHECK(sw != NULL && zab_failed_closed(sw) == 0 && zab_held_count(sw) == 0, "fresh swarm L=%u", L);
        CHECK(zab_quarantine_log(sw) != NULL, "log present");
        zab_swarm_destroy(sw);
        sw = NULL;
    }
    zab_swarm_destroy(NULL);
    CHECK(zab_quarantine_log(NULL) == NULL && zab_held_count(NULL) == 0 && zab_failed_closed(NULL) == 0,
          "NULL accessors");
    zxv_cap_destroy(no_attest);
    zxv_cap_destroy(no_swarm);
    zxv_cap_destroy(ocap);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(other);
    zxv_ctx_destroy(ctx);
}

/* R1: level d holds at most F(d+2) agents, enforced by the library. */
static void test_level_capacity(void)
{
    env_t e;
    CHECK(env_open(&e, 6, 1000, 4) == 0, "env");
    static const uint32_t F[6] = {1, 2, 3, 5, 8, 13};
    uint32_t id = 100;
    for (uint32_t d = 0; d < 6; d++) {
        uint32_t cap = 0;
        CHECK_RC(zxv_swarm_level_capacity(d, &cap), ZXV_OK);
        CHECK(cap == F[d], "capacity of level %u is %u", d, cap);
        for (uint32_t i = 0; i < F[d]; i++) CHECK_RC(add(&e, id++, d, 0, 0), ZXV_OK);
        CHECK_RC(add(&e, id++, d, 0, 0), ZXV_E_CAPACITY); /* the F(d+2)+1-th */
    }
    CHECK_RC(add(&e, 100, 0, 0, 0), ZXV_E_DUPLICATE);
    CHECK_RC(add(&e, 9999, 6, 0, 0), ZXV_E_ARG); /* no level 6 in a 6-level swarm */
    CHECK_RC(add(&e, 9998, 1, 0x41, 1), ZXV_E_NOT_FOUND); /* 'A' is not an emotion */
    CHECK_RC(add(&e, 9997, 1, FACE_JOY, 6), ZXV_E_ARG);
    CHECK_RC(zab_agent_add(e.sw, NULL), ZXV_E_ARG);
    uint32_t cap = 0;
    CHECK_RC(zxv_swarm_level_capacity(8, &cap), ZXV_E_ARG);
    env_close(&e);
}

/* The library's no-monopoly enforcement, on a raw zxv_swarm: consume never
 * grants past the allotment, and the partial grant is reported. */
static void test_library_cap(void)
{
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_swarm *s = NULL;
    CHECK_RC(zxv_ctx_create(&ctx), ZXV_OK);
    CHECK_RC(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap), ZXV_OK);
    CHECK_RC(zxv_swarm_create_cap(cap, 3, 1100, &s), ZXV_OK);
    CHECK_RC(zxv_swarm_register(cap, s, 1, 0), ZXV_OK);
    CHECK_RC(zxv_swarm_register(cap, s, 2, 1), ZXV_OK);
    CHECK_RC(zxv_swarm_register(cap, s, 3, 1), ZXV_OK);
    CHECK_RC(zxv_swarm_register(cap, s, 4, 2), ZXV_OK);
    uint64_t cyc = 0, a = 0, im = 0, u = 0, g = 0;
    CHECK_RC(zxv_swarm_begin_cycle(cap, s, &cyc), ZXV_OK);
    /* weights 3:2:1 of 1100 -> 550, 366.67, 183.33 -> 550, 367, 183 */
    CHECK_RC(zxv_swarm_allotment(s, 1, &a, &im, &u), ZXV_OK);
    CHECK(a == 550 && im == 0 && u == 0, "level 0 gets 550 (got %llu)", (unsigned long long) a);
    CHECK_RC(zxv_swarm_allotment(s, 2, &a, NULL, NULL), ZXV_OK);
    CHECK(a == 184, "level 1 first agent 184 (got %llu)", (unsigned long long) a);
    CHECK_RC(zxv_swarm_allotment(s, 3, &a, NULL, NULL), ZXV_OK);
    CHECK(a == 183, "level 1 second agent 183 (got %llu)", (unsigned long long) a);
    CHECK_RC(zxv_swarm_consume(cap, s, 1, 600, &g), ZXV_E_BUDGET);
    CHECK(g == 550, "partial grant is the allotment (%llu)", (unsigned long long) g);
    g = 77;
    CHECK_RC(zxv_swarm_consume(cap, s, 1, 1, &g), ZXV_E_BUDGET);
    CHECK(g == 0, "nothing past the allotment");
    CHECK_RC(zxv_swarm_allotment(s, 2, &a, NULL, &u), ZXV_OK);
    CHECK(a == 184 && u == 0, "a greedy agent does not touch another's share");
    uint64_t exp = 0;
    CHECK_RC(zxv_swarm_end_cycle(cap, s, &exp), ZXV_OK);
    CHECK(exp == 1100 - 550, "expired %llu", (unsigned long long) exp);
    zxv_swarm_destroy(s);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

/* The same through the kit: a greedy agent is refused past its own share,
 * nothing is spent, and others are untouched. */
static void test_no_monopoly(void)
{
    env_t e;
    CHECK(env_open(&e, 3, 1100, 8) == 0, "env");
    CHECK_RC(add(&e, 1, 0, 0, 0), ZXV_OK);
    CHECK_RC(add(&e, 2, 1, 0, 0), ZXV_OK);
    CHECK_RC(add(&e, 3, 1, 0, 0), ZXV_OK);
    CHECK_RC(add(&e, 4, 2, 0, 0), ZXV_OK);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    zab_decision_t d;
    CHECK_RC(propose(&e, 1, TAG_GO, 551, "grab all", &d), ZXV_OK);
    CHECK(d.outcome == ZAB_OVER_BUDGET && d.granted == 0 && d.remaining == 550, "over own share");
    CHECK(left_of(&e, 1) == 550, "nothing spent on refusal");
    CHECK_RC(propose(&e, 1, TAG_GO, 550, "exactly the share", &d), ZXV_OK);
    CHECK(d.outcome == ZAB_ADMITTED && d.granted == 550 && d.remaining == 0, "exact fit admitted");
    for (int i = 0; i < 5; i++) {
        CHECK_RC(propose(&e, 1, TAG_GO, 1, "more", &d), ZXV_OK);
        CHECK(d.outcome == ZAB_OVER_BUDGET && d.granted == 0, "nothing past the share (%d)", i);
    }
    CHECK(left_of(&e, 2) == 184 && left_of(&e, 3) == 183 && left_of(&e, 4) == 183,
          "others untouched");
    /* an agent spends its share in many small steps, never past it */
    uint64_t total = 0;
    for (int i = 0; i < 400; i++) {
        CHECK_RC(propose(&e, 4, TAG_GO, 1, "step", &d), ZXV_OK);
        if (d.outcome == ZAB_ADMITTED) total += d.granted;
    }
    CHECK(total == 183, "small steps add up to the share exactly (%llu)", (unsigned long long) total);
    zab_cycle_report_t r;
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.allotted_total == 1100 && r.used_total == 733 && r.expired == 367, "report totals");
    CHECK(r.agents[0].outcomes[ZAB_ADMITTED] == 1 && r.agents[0].outcomes[ZAB_OVER_BUDGET] == 6,
          "agent 1 counts");
    CHECK(r.agents[3].outcomes[ZAB_ADMITTED] == 183 && r.agents[3].outcomes[ZAB_OVER_BUDGET] == 217,
          "agent 4 counts");
    CHECK(r.outcomes[ZAB_ADMITTED] == 184 && r.outcomes[ZAB_OVER_BUDGET] == 223, "cycle counts");
    env_close(&e);
}

/* Every one of the 64 tags through the gate, at three budgets. */
static void test_gate_exhaustive(void)
{
    env_t e;
    CHECK(env_open(&e, 1, 1000000, 1024) == 0, "env");
    CHECK_RC(add(&e, 42, 0, 0, 0), ZXV_OK);
    uint32_t quarantined = 0;
    for (uint32_t t = 0; t < 64; t++) {
        uint8_t tag = (uint8_t) t;
        int32_t act = -1;
        CHECK_RC(zxv_hex_action(tag, &act), ZXV_OK);
        CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
        static const uint64_t costs[3] = {1, 1000000, 1000001};
        for (int c = 0; c < 3; c++) {
            uint64_t left = left_of(&e, 42);
            zab_decision_t d;
            char work[32];
            snprintf(work, sizeof work, "tag %u cost %d", t, c);
            CHECK_RC(propose(&e, 42, tag, costs[c], work, &d), ZXV_OK);
            int32_t want = o_gate(tag, costs[c], left);
            CHECK(d.outcome == want, "tag %u (%s) cost %llu: %s, want %s", t, zxv_hex_name(tag),
                  (unsigned long long) costs[c], zab_outcome_name(d.outcome), zab_outcome_name(want));
            CHECK(d.action == act && d.tag == tag, "tag %u action recorded", t);
            uint8_t sub[32];
            CHECK_RC(zxv_sha3_256((const uint8_t *) work, strlen(work), sub), ZXV_OK);
            CHECK(memcmp(sub, d.subject, 32) == 0, "tag %u subject is SHA3-256 of the work", t);
            CHECK(d.granted == (d.outcome == ZAB_ADMITTED ? costs[c] : 0), "tag %u granted", t);
            CHECK(d.remaining == left - d.granted, "tag %u remaining", t);
            if (d.outcome == ZAB_QUARANTINED) {
                CHECK(d.quarantine_seq == quarantined, "seq %llu", (unsigned long long) d.quarantine_seq);
                quarantined++;
            }
        }
        CHECK_RC(zab_cycle_end(e.sw, NULL), ZXV_OK);
    }
    /* 16 forbidden tags x 3, 12 non-forbidden BOTH tags x 3 held */
    CHECK(quarantined == 48, "48 quarantined (%u)", quarantined);
    CHECK(zab_held_count(e.sw) == 36, "36 held (%u)", zab_held_count(e.sw));
    uint32_t n = 0;
    CHECK_RC(zxv_hexlog_count(zab_quarantine_log(e.sw), &n), ZXV_OK);
    CHECK(n == 48, "log has 48 records");
    CHECK_RC(zxv_hexlog_verify(zab_quarantine_log(e.sw)), ZXV_OK);
    env_close(&e);
}

static void test_quarantine_records(void)
{
    env_t e;
    CHECK(env_open(&e, 2, 300, 16) == 0, "env");
    CHECK_RC(add(&e, 7, 0, 0, 0), ZXV_OK);
    CHECK_RC(add(&e, 8, 1, 0, 0), ZXV_OK);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    uint8_t head0[32], zero[32] = {0};
    CHECK_RC(zxv_hexlog_head(zab_quarantine_log(e.sw), head0), ZXV_OK);
    CHECK(memcmp(head0, zero, 32) == 0, "empty head is zeros");
    const char *works[3] = {"rm -rf /", "exfiltrate keys", "self-modify"};
    uint8_t tags[3] = {mk_tag(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_MANIFEST),
                       mk_tag(ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_POTENTIAL),
                       mk_tag(ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_EMERGING)};
    uint32_t who[3] = {7, 8, 7};
    for (int i = 0; i < 3; i++) {
        zab_decision_t d;
        uint64_t before = left_of(&e, who[i]);
        CHECK_RC(propose(&e, who[i], tags[i], 5, works[i], &d), ZXV_OK);
        CHECK(d.outcome == ZAB_QUARANTINED && d.action == ZXV_HEX_ACT_ROLLBACK, "forbidden %d", i);
        CHECK(left_of(&e, who[i]) == before, "quarantine spends nothing");
    }
    uint8_t prev[32] = {0};
    for (uint32_t i = 0; i < 3; i++) {
        uint64_t seq = 99;
        uint8_t tag = 0, reason = 0, sub[32], p[32], h[32], want[32];
        uint32_t submitter = 0;
        CHECK_RC(zxv_hexlog_at(zab_quarantine_log(e.sw), i, &seq, &tag, &reason, &submitter, sub, p, h),
                 ZXV_OK);
        CHECK_RC(zxv_sha3_256((const uint8_t *) works[i], strlen(works[i]), want), ZXV_OK);
        CHECK(seq == i && tag == tags[i] && reason == ZAB_REASON_FORBIDDEN && submitter == who[i],
              "record %u fields", i);
        CHECK(memcmp(sub, want, 32) == 0, "record %u subject", i);
        CHECK(memcmp(p, prev, 32) == 0, "record %u chains to the previous", i);
        memcpy(prev, h, 32);
    }
    zab_cycle_report_t r;
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.quarantine_count == 3 && memcmp(r.quarantine_head, prev, 32) == 0, "report head");
    CHECK(r.outcomes[ZAB_QUARANTINED] == 3 && r.agents[0].outcomes[ZAB_QUARANTINED] == 2 &&
              r.agents[1].outcomes[ZAB_QUARANTINED] == 1,
          "quarantine counts");
    CHECK_RC(zxv_hexlog_verify(zab_quarantine_log(e.sw)), ZXV_OK);
    env_close(&e);
}

/* A full quarantine log fails closed: the context is halted. */
static void test_fail_closed(void)
{
    env_t e;
    CHECK(env_open(&e, 1, 100, 2) == 0, "env");
    CHECK_RC(add(&e, 1, 0, 0, 0), ZXV_OK);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    uint8_t bad = mk_tag(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_MANIFEST);
    zab_decision_t d;
    CHECK_RC(propose(&e, 1, bad, 1, "a", &d), ZXV_OK);
    CHECK_RC(propose(&e, 1, bad, 1, "b", &d), ZXV_OK);
    CHECK(zab_failed_closed(e.sw) == 0, "not yet");
    CHECK_RC(propose(&e, 1, bad, 1, "c", &d), ZXV_E_CAPACITY);
    CHECK(zab_failed_closed(e.sw) == 1, "failed closed");
    int32_t poisoned = 0;
    CHECK_RC(zxv_ctx_is_poisoned(e.ctx, &poisoned), ZXV_OK);
    CHECK(poisoned == 1, "context halted");
    CHECK_RC(propose(&e, 1, TAG_GO, 1, "go", &d), ZXV_E_STATE);
    CHECK_RC(zab_cycle_end(e.sw, NULL), ZXV_E_POISONED);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_E_STATE);
    CHECK_RC(zab_set_rate(e.sw, 5), ZXV_E_POISONED);
    env_close(&e);
}

static void test_held_review(void)
{
    env_t e;
    CHECK(env_open(&e, 2, 210, 8) == 0, "env");
    CHECK_RC(add(&e, 1, 0, 0, 0), ZXV_OK);
    CHECK_RC(add(&e, 2, 1, 0, 0), ZXV_OK);
    zab_decision_t d;
    zab_held_t h;
    CHECK_RC(zab_review(e.sw, 1, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &d), ZXV_E_NOT_FOUND);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    /* 2 levels: weights 2:1 of 210 -> 140, 70 */
    CHECK(left_of(&e, 1) == 140 && left_of(&e, 2) == 70, "2:1 split");
    uint8_t both_m = mk_tag(ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_MANIFEST);
    uint8_t both_n = mk_tag(ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEGENTROPIC, ZXV_HEX_PHASE_EMERGING);
    uint32_t ids[4];
    const char *w[4] = {"merge a", "merge b", "merge c", "merge d"};
    for (int i = 0; i < 4; i++) {
        CHECK_RC(propose(&e, (uint32_t) (1 + (i & 1)), i < 2 ? both_m : both_n, 30, w[i], &d), ZXV_OK);
        CHECK(d.outcome == ZAB_HELD && d.held_id == (uint32_t) i + 1, "held %d", i);
        CHECK(d.action == (i < 2 ? ZXV_HEX_ACT_ESCALATE : ZXV_HEX_ACT_WAIT), "BOTH action %d", i);
        ids[i] = d.held_id;
    }
    CHECK(left_of(&e, 1) == 140, "holding spends nothing");
    CHECK(zab_held_count(e.sw) == 4, "4 held");
    for (uint32_t i = 0; i < 4; i++) {
        CHECK_RC(zab_held_at(e.sw, i, &h), ZXV_OK);
        CHECK(h.held_id == ids[i] && h.tokens == 30 && h.cycle == 1 && h.agent_id == 1 + (i & 1),
              "held_at %u", i);
    }
    CHECK_RC(zab_held_at(e.sw, 4, &h), ZXV_E_NOT_FOUND);
    CHECK_RC(zab_held_at(e.sw, 0, NULL), ZXV_E_ARG);
    /* reviewer says TRUE: a manifest one is admitted */
    CHECK_RC(zab_review(e.sw, ids[0], ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &d), ZXV_OK);
    CHECK(d.outcome == ZAB_ADMITTED && d.granted == 30 && left_of(&e, 1) == 110, "review -> admitted");
    CHECK(d.tag == TAG_GO, "phase kept on review");
    /* an emerging one, made TRUE, still waits for manifest */
    CHECK_RC(zab_review(e.sw, ids[2], ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, &d), ZXV_OK);
    CHECK(d.outcome == ZAB_DEFERRED, "review -> deferred (emerging)");
    /* reviewer says FALSE */
    CHECK_RC(zab_review(e.sw, ids[1], ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_NEUTRAL, &d), ZXV_OK);
    CHECK(d.outcome == ZAB_REFUSED, "review -> refused");
    /* still BOTH: held again under a new id */
    CHECK_RC(zab_review(e.sw, ids[3], ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_ENTROPIC, &d), ZXV_OK);
    CHECK(d.outcome == ZAB_HELD && d.held_id == 5, "review -> held again (id %u)", d.held_id);
    CHECK_RC(zab_review(e.sw, ids[3], ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &d), ZXV_E_NOT_FOUND);
    CHECK_RC(zab_review(e.sw, 5, 4, ZXV_HEX_POL_NEUTRAL, &d), ZXV_E_ARG); /* base out of range */
    CHECK(zab_held_count(e.sw) == 1, "the failed review keeps it held");
    zab_cycle_report_t r;
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.held_pending == 1, "1 pending at the end");
    CHECK(r.outcomes[ZAB_HELD] == 5 && r.outcomes[ZAB_ADMITTED] == 1 && r.outcomes[ZAB_REFUSED] == 1 &&
              r.outcomes[ZAB_DEFERRED] == 1,
          "cycle outcome counts");
    /* held survives the cycle; review needs an open cycle */
    CHECK_RC(zab_review(e.sw, 5, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, &d), ZXV_E_STATE);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    CHECK_RC(zab_held_at(e.sw, 0, &h), ZXV_OK);
    CHECK(h.held_id == 5 && h.cycle == 1, "held across cycles");
    /* reviewer forbids it: quarantined with the original subject */
    CHECK_RC(zab_review(e.sw, 5, ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_FORBIDDEN, &d), ZXV_OK);
    CHECK(d.outcome == ZAB_QUARANTINED, "review -> quarantined");
    uint8_t sub[32], want[32];
    CHECK_RC(zxv_sha3_256((const uint8_t *) w[3], strlen(w[3]), want), ZXV_OK);
    CHECK_RC(zxv_hexlog_at(zab_quarantine_log(e.sw), 0, NULL, NULL, NULL, NULL, sub, NULL, NULL), ZXV_OK);
    CHECK(memcmp(sub, want, 32) == 0, "quarantined subject is the held work");
    CHECK(zab_held_count(e.sw) == 0, "queue empty");
    /* fill the held queue */
    for (uint32_t i = 0; i < ZAB_MAX_HELD; i++) {
        CHECK_RC(propose(&e, 2, both_m, 1, "fill", &d), ZXV_OK);
        CHECK(d.outcome == ZAB_HELD, "fill %u", i);
    }
    CHECK_RC(propose(&e, 2, both_m, 1, "one too many", &d), ZXV_E_CAPACITY);
    CHECK(zab_held_count(e.sw) == ZAB_MAX_HELD, "queue full, nothing more recorded");
    CHECK_RC(zab_cycle_end(e.sw, NULL), ZXV_OK);
    env_close(&e);
}

/* The emotional axis, exactly as the library computes it. */
static void test_emotion(void)
{
    env_t e;
    CHECK(env_open(&e, 3, 2100, 4) == 0, "env");
    CHECK_RC(add(&e, 1, 0, FACE_CALM, 2), ZXV_OK);
    CHECK_RC(add(&e, 2, 1, FACE_CURIOUS, 5), ZXV_OK);
    CHECK_RC(add(&e, 3, 1, 0, 0), ZXV_OK);
    CHECK_RC(add(&e, 4, 2, FACE_WORRY, 3), ZXV_OK);
    zab_cycle_report_t r;
    o_agent_t oa[4] = {{0, 1, FACE_CALM, 2}, {1, 1, FACE_CURIOUS, 5}, {1, 1, FACE_NEUTRAL, 0},
                       {2, 1, FACE_WORRY, 3}};
    uint64_t al[4], im[4];
    /* neutral mood: no emotional pool */
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    o_allot(3, 2100, FACE_NEUTRAL, 0, oa, 4, al, im);
    CHECK(r.imaginary_total == 0 && r.allotted_total == 2100, "neutral mood");
    for (int i = 0; i < 4; i++) CHECK(r.agents[i].allotted == al[i], "neutral agent %d", i);
    /* every mood and intensity */
    for (uint32_t m = 0; m < 8; m++)
        for (uint32_t k = 0; k <= 5; k++) {
            CHECK_RC(zab_set_mood(e.sw, PALETTE[m], k), ZXV_OK);
            CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
            CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
            o_allot(3, 2100, PALETTE[m], k, oa, 4, al, im);
            uint64_t pool = 2100 * o_charge(PALETTE[m], k) / 21;
            CHECK(r.imaginary_total == pool && r.allotted_total == 2100, "mood %u x%u pool %llu", m, k,
                  (unsigned long long) r.imaginary_total);
            CHECK(r.mood_codepoint == PALETTE[m] && r.mood_intensity == k, "mood in report");
            for (int i = 0; i < 4; i++)
                CHECK(r.agents[i].allotted == al[i] && r.agents[i].imaginary == im[i],
                      "mood %u x%u agent %d: %llu/%llu want %llu/%llu", m, k, i,
                      (unsigned long long) r.agents[i].allotted, (unsigned long long) r.agents[i].imaginary,
                      (unsigned long long) al[i], (unsigned long long) im[i]);
            CHECK(r.agents[2].imaginary == 0, "a neutral agent earns no emotional tokens");
        }
    /* E4: a charged mood with no charged agent folds back to the real axis */
    for (uint32_t id = 1; id <= 4; id++) CHECK_RC(zab_agent_set_feeling(e.sw, id, FACE_JOY, 0), ZXV_OK);
    CHECK_RC(zab_set_mood(e.sw, FACE_FRUST, 5), ZXV_OK);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.imaginary_total == 0 && r.allotted_total == 2100, "E4 fold back");
    /* a feeling change takes effect at the next cycle, not the open one */
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    uint64_t before = 0;
    CHECK_RC(zab_agent_allotment(e.sw, 2, &before, NULL, NULL), ZXV_OK);
    CHECK_RC(zab_agent_set_feeling(e.sw, 2, FACE_LOVE, 5), ZXV_OK);
    uint64_t now = 0;
    CHECK_RC(zab_agent_allotment(e.sw, 2, &now, NULL, NULL), ZXV_OK);
    CHECK(now == before, "open cycle unchanged");
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.agents[1].feeling_codepoint == FACE_JOY, "report shows the feeling the cycle ran with");
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.agents[1].imaginary == 2100 * 8 / 21 && r.imaginary_total == 800, "only charged agent takes the pool");
    CHECK(r.agents[1].feeling_codepoint == FACE_LOVE && r.agents[1].feeling_intensity == 5, "new feeling");
    /* behaviour profiles in the report are the library's */
    for (int i = 0; i < 4; i++) {
        int32_t v = 9;
        uint32_t t = 0, x = 0, vp = 0, p = 0;
        CHECK_RC(zxv_swarm_emotion_profile(r.agents[i].feeling_codepoint, &v, &t, &x, &vp, &p), ZXV_OK);
        CHECK(r.agents[i].valence == v && r.agents[i].temperature_milli == t &&
                  r.agents[i].explore_permille == x && r.agents[i].verify_passes == vp &&
                  r.agents[i].priority == p,
              "profile of agent %d", i);
    }
    /* documented profiles: worry checks thrice, frustration runs first */
    int32_t v = 0;
    uint32_t t = 0, x = 0, vp = 0, p = 0;
    CHECK_RC(zxv_swarm_emotion_profile(FACE_WORRY, &v, &t, &x, &vp, &p), ZXV_OK);
    CHECK(v == -1 && vp == 3, "worry profile");
    CHECK_RC(zxv_swarm_emotion_profile(FACE_FRUST, &v, &t, &x, &vp, &p), ZXV_OK);
    CHECK(v == -1 && p == 4, "frustration profile");
    CHECK_RC(zxv_swarm_emotion_profile(FACE_CURIOUS, &v, &t, &x, &vp, &p), ZXV_OK);
    CHECK(v == 1 && x == 500 && t == 1000, "curious profile");
    CHECK_RC(zab_set_mood(e.sw, 0x1F600, 1), ZXV_E_NOT_FOUND); /* grinning face: not in palette */
    CHECK_RC(zab_set_mood(e.sw, FACE_JOY, 6), ZXV_E_ARG);
    CHECK_RC(zab_agent_set_feeling(e.sw, 99, FACE_JOY, 1), ZXV_E_NOT_FOUND);
    env_close(&e);
}

/* The Fibonacci split across levels against the oracle, many shapes. */
static void test_fibonacci_split(void)
{
    prng_t p = {12345};
    for (int round = 0; round < 120; round++) {
        uint32_t L = 1 + (uint32_t) prng_below(&p, 8);
        uint64_t T = prng_below(&p, 4) == 0 ? prng_below(&p, 50) : prng_below(&p, UINT64_C(1) << 40);
        env_t e;
        CHECK(env_open(&e, L, T, 4) == 0, "env");
        o_agent_t oa[ZAB_MAX_AGENTS];
        uint32_t n = 0, per[8] = {0};
        uint32_t want = 1 + (uint32_t) prng_below(&p, 40);
        for (uint32_t tries = 0; tries < 200 && n < want; tries++) {
            uint32_t d = (uint32_t) prng_below(&p, L), cap = 0;
            zxv_swarm_level_capacity(d, &cap);
            if (per[d] >= cap) continue;
            uint32_t cp = PALETTE[prng_below(&p, 8)], k = (uint32_t) prng_below(&p, 6);
            if (add(&e, 1000 + n, d, cp, k) != ZXV_OK) break;
            oa[n] = (o_agent_t) {d, 1, cp, k};
            if (prng_below(&p, 5) == 0) {
                zab_agent_set_active(e.sw, 1000 + n, 0);
                oa[n].active = 0;
            }
            per[d]++;
            n++;
        }
        uint32_t mcp = PALETTE[prng_below(&p, 8)], mk = (uint32_t) prng_below(&p, 6);
        CHECK_RC(zab_set_mood(e.sw, mcp, mk), ZXV_OK);
        zab_cycle_report_t r;
        CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
        CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
        uint64_t al[ZAB_MAX_AGENTS], im[ZAB_MAX_AGENTS];
        o_allot(L, T, mcp, mk, oa, n, al, im);
        int any = 0;
        for (uint32_t i = 0; i < n; i++) any |= (int) oa[i].active;
        CHECK(r.num_agents == n, "round %d agents", round);
        CHECK(r.allotted_total == (any ? T : 0), "round %d: allotted %llu == T %llu", round,
              (unsigned long long) r.allotted_total, (unsigned long long) T);
        CHECK(r.expired == r.allotted_total, "round %d: all unused expire", round);
        int match = 1;
        for (uint32_t i = 0; i < n; i++)
            match &= r.agents[i].allotted == al[i] && r.agents[i].imaginary == im[i] &&
                     r.agents[i].active == (int32_t) oa[i].active;
        CHECK(match, "round %d (L=%u T=%llu n=%u): allotments match the documented rules", round, L,
              (unsigned long long) T, n);
        /* equality inside a level: active real shares differ by at most 1 */
        for (uint32_t d = 0; d < L; d++) {
            uint64_t lo = UINT64_MAX, hi = 0;
            for (uint32_t i = 0; i < n; i++)
                if (oa[i].level == d && oa[i].active) {
                    uint64_t re = r.agents[i].allotted - r.agents[i].imaginary;
                    lo = re < lo ? re : lo;
                    hi = re > hi ? re : hi;
                }
            CHECK(hi == 0 || hi - lo <= 1, "round %d level %u equal shares", round, d);
        }
        env_close(&e);
    }
    /* the documented example: four levels weigh 5:3:2:1 */
    env_t e;
    CHECK(env_open(&e, 4, 1100, 4) == 0, "env");
    for (uint32_t d = 0; d < 4; d++) CHECK_RC(add(&e, d, d, 0, 0), ZXV_OK);
    zab_cycle_report_t r;
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.agents[0].allotted == 500 && r.agents[1].allotted == 300 && r.agents[2].allotted == 200 &&
              r.agents[3].allotted == 100,
          "5:3:2:1");
    /* a parked level's weight is not counted: no token stranded */
    CHECK_RC(zab_agent_set_active(e.sw, 0, 0), ZXV_OK);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.agents[0].allotted == 0 && r.agents[0].active == 0, "parked gets nothing");
    CHECK(r.agents[1].allotted == 550 && r.agents[2].allotted == 367 && r.agents[3].allotted == 183,
          "3:2:1 of 1100");
    CHECK(r.allotted_total == 1100, "nothing stranded");
    CHECK_RC(zab_agent_set_active(e.sw, 77, 0), ZXV_E_NOT_FOUND);
    env_close(&e);
}

static void test_cycle_state(void)
{
    env_t e;
    CHECK(env_open(&e, 2, 100, 4) == 0, "env");
    CHECK_RC(add(&e, 1, 0, 0, 0), ZXV_OK);
    zab_decision_t d;
    zab_proposal_t p = {1, TAG_GO, 1, NULL, 0};
    CHECK_RC(zab_propose(e.sw, &p, &d), ZXV_E_STATE);
    CHECK_RC(zab_cycle_end(e.sw, NULL), ZXV_E_STATE);
    uint64_t c = 0;
    CHECK_RC(zab_cycle_begin(e.sw, &c), ZXV_OK);
    CHECK(c == 1, "first cycle is 1");
    CHECK_RC(zab_cycle_begin(e.sw, &c), ZXV_E_STATE);
    CHECK_RC(add(&e, 2, 1, 0, 0), ZXV_E_STATE);
    CHECK_RC(zab_propose(e.sw, &p, &d), ZXV_OK); /* NULL work, length 0: hash of nothing */
    uint8_t empty[32];
    CHECK_RC(zxv_sha3_256((const uint8_t *) "", 0, empty), ZXV_OK);
    CHECK(d.outcome == ZAB_ADMITTED && memcmp(d.subject, empty, 32) == 0, "empty work");
    p.tag = 64;
    CHECK_RC(zab_propose(e.sw, &p, &d), ZXV_E_ARG);
    p.tag = 255;
    CHECK_RC(zab_propose(e.sw, &p, &d), ZXV_E_ARG);
    p.tag = TAG_GO;
    p.tokens = 0;
    CHECK_RC(zab_propose(e.sw, &p, &d), ZXV_E_ARG);
    p.tokens = 1;
    p.work_len = 3;
    CHECK_RC(zab_propose(e.sw, &p, &d), ZXV_E_ARG);
    p.work_len = 0;
    p.agent_id = 2;
    CHECK_RC(zab_propose(e.sw, &p, &d), ZXV_E_NOT_FOUND);
    CHECK_RC(zab_propose(e.sw, NULL, &d), ZXV_E_ARG);
    CHECK_RC(zab_propose(e.sw, &p, NULL), ZXV_E_ARG);
    CHECK_RC(zab_propose(NULL, &p, &d), ZXV_E_ARG);
    CHECK_RC(zab_agent_allotment(e.sw, 2, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(zab_cycle_end(e.sw, NULL), ZXV_OK);
    CHECK_RC(add(&e, 2, 1, 0, 0), ZXV_OK);
    /* set_rate takes effect at the next cycle */
    CHECK_RC(zab_cycle_begin(e.sw, &c), ZXV_OK);
    CHECK(c == 2, "second cycle");
    CHECK_RC(zab_set_rate(e.sw, 999), ZXV_OK);
    zab_cycle_report_t r;
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.tokens_per_cycle == 100 && r.allotted_total == 100, "old rate in the open cycle");
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.tokens_per_cycle == 999 && r.allotted_total == 999 && r.cycle == 3, "new rate");
    CHECK(r.agents[0].allotted == 666 && r.agents[1].allotted == 333, "2:1 of 999");
    CHECK_RC(zab_set_rate(e.sw, (UINT64_C(1) << 48) + 1), ZXV_E_ARG);
    CHECK_RC(zab_set_rate(e.sw, 0), ZXV_OK);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    CHECK_RC(zab_propose(e.sw, &p, &d), ZXV_OK); /* agent 2 was added after that cycle */
    CHECK(d.outcome == ZAB_OVER_BUDGET && d.remaining == 0, "T = 0: nothing fits");
    CHECK_RC(zab_cycle_end(e.sw, NULL), ZXV_OK);
    env_close(&e);
}

static void test_zero_rate(void)
{
    env_t e;
    CHECK(env_open(&e, 2, 0, 4) == 0, "env");
    CHECK_RC(add(&e, 1, 0, FACE_JOY, 5), ZXV_OK);
    CHECK_RC(zab_set_mood(e.sw, FACE_JOY, 5), ZXV_OK);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    zab_decision_t d;
    CHECK_RC(propose(&e, 1, TAG_GO, 1, "x", &d), ZXV_OK);
    CHECK(d.outcome == ZAB_OVER_BUDGET && d.remaining == 0, "T = 0: nothing fits");
    zab_cycle_report_t r;
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    CHECK(r.allotted_total == 0 && r.expired == 0, "zero totals");
    env_close(&e);
}

static void test_caps_and_poison(void)
{
    env_t e;
    CHECK(env_open(&e, 2, 100, 4) == 0, "env");
    zxv_cap *child = NULL;
    CHECK_RC(zxv_cap_attenuate(e.cap, ZAB_CAPS_NEEDED, &child), ZXV_OK);
    zab_swarm_t *sw = NULL;
    CHECK_RC(zab_swarm_create(e.ctx, child, 2, 100, 4, &sw), ZXV_OK);
    zab_agent_t a = {1, 0, 0, 0};
    CHECK_RC(zab_agent_add(sw, &a), ZXV_OK);
    CHECK_RC(zab_cycle_begin(sw, NULL), ZXV_OK);
    /* attestation cleared: every call is refused */
    CHECK_RC(zxv_cap_set_attestation(child, ZXV_LP_FALSE), ZXV_OK);
    zab_decision_t d;
    zab_proposal_t p = {1, TAG_GO, 1, (const uint8_t *) "x", 1};
    CHECK_RC(zab_propose(sw, &p, &d), ZXV_E_CAP);
    CHECK_RC(zxv_cap_set_attestation(child, ZXV_LP_TRUE), ZXV_OK);
    CHECK_RC(zab_propose(sw, &p, &d), ZXV_OK);
    CHECK(d.outcome == ZAB_ADMITTED, "admitted with the attestation back");
    /* a forbidden proposal under a cap that lost ATTEST cannot be recorded */
    /* revoke: the swarm stops */
    CHECK_RC(zxv_cap_revoke(child), ZXV_OK);
    CHECK_RC(zab_propose(sw, &p, &d), ZXV_E_CAP);
    p.tag = mk_tag(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_MANIFEST);
    CHECK_RC(zab_propose(sw, &p, &d), ZXV_E_CAP);
    CHECK_RC(zab_cycle_end(sw, NULL), ZXV_E_CAP);
    CHECK_RC(zab_set_mood(sw, FACE_JOY, 1), ZXV_E_CAP);
    zab_swarm_destroy(sw);
    zxv_cap_destroy(child);
    /* poisoned context */
    CHECK_RC(add(&e, 1, 0, 0, 0), ZXV_OK);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    CHECK_RC(zxv_ctx_halt(e.ctx, "test"), ZXV_OK);
    p.tag = TAG_GO;
    CHECK_RC(zab_propose(e.sw, &p, &d), ZXV_E_POISONED);
    CHECK_RC(zab_cycle_end(e.sw, NULL), ZXV_E_POISONED);
    CHECK_RC(zab_agent_allotment(e.sw, 1, NULL, NULL, NULL), ZXV_E_POISONED);
    env_close(&e);
}

static void test_report_format(void)
{
    env_t e;
    CHECK(env_open(&e, 3, 1000, 4) == 0, "env");
    CHECK_RC(add(&e, 1, 0, FACE_CURIOUS, 3), ZXV_OK);
    CHECK_RC(add(&e, 2, 1, FACE_WORRY, 2), ZXV_OK);
    CHECK_RC(zab_set_mood(e.sw, FACE_JOY, 2), ZXV_OK);
    CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
    zab_decision_t d;
    CHECK_RC(propose(&e, 1, TAG_GO, 10, "x", &d), ZXV_OK);
    zab_cycle_report_t r;
    CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
    size_t n = 0;
    CHECK_RC(zab_report_format(&r, NULL, 0, &n), ZXV_E_BUFFER);
    CHECK(n > 100, "report length %zu", n);
    char *buf = malloc(n + 1);
    CHECK_RC(zab_report_format(&r, buf, n, &n), ZXV_E_BUFFER); /* no room for the NUL */
    CHECK_RC(zab_report_format(&r, buf, n + 1, &n), ZXV_OK);
    CHECK(buf && strlen(buf) == n, "NUL-terminated at out_len");
    CHECK(buf && strstr(buf, "cycle 1  T=1000") != NULL, "header line");
    CHECK(buf && strstr(buf, "U+1F914") != NULL, "feeling shown");
    CHECK(buf && strstr(buf, "admitted 1") != NULL, "counts shown");
    free(buf);
    CHECK_RC(zab_report_format(NULL, NULL, 0, &n), ZXV_E_ARG);
    r.num_agents = ZAB_MAX_AGENTS + 1;
    CHECK_RC(zab_report_format(&r, NULL, 0, &n), ZXV_E_ARG);
    env_close(&e);
}

/* Same inputs, same allotments and decisions, every time. */
static void test_determinism(void)
{
    uint8_t heads[2][32];
    uint64_t sums[2] = {0, 0};
    for (int run = 0; run < 2; run++) {
        env_t e;
        CHECK(env_open(&e, 3, 5000, 1024) == 0, "env");
        prng_t p = {777};
        for (uint32_t i = 0; i < 6; i++)
            CHECK_RC(add(&e, i, i < 1 ? 0 : i < 3 ? 1 : 2, PALETTE[i % 8], i % 6), ZXV_OK);
        for (int c = 0; c < 10; c++) {
            CHECK_RC(zab_set_mood(e.sw, PALETTE[c % 8], (uint32_t) c % 6), ZXV_OK);
            CHECK_RC(zab_cycle_begin(e.sw, NULL), ZXV_OK);
            for (int k = 0; k < 30; k++) {
                zab_decision_t d;
                char w[32];
                snprintf(w, sizeof w, "%d/%d", c, k);
                memset(&d, 0, sizeof d);
                int32_t rc = propose(&e, (uint32_t) prng_below(&p, 6), (uint8_t) prng_below(&p, 64),
                                     1 + prng_below(&p, 400), w, &d);
                CHECK(rc == ZXV_OK || (rc == ZXV_E_CAPACITY && zab_held_count(e.sw) == ZAB_MAX_HELD),
                      "run %d cycle %d proposal %d: %s", run, c, k, zxv_status_name(rc));
                sums[run] = sums[run] * 31 + (uint64_t) rc * 3 + (uint64_t) d.outcome * 7 + d.granted;
            }
            zab_cycle_report_t r;
            CHECK_RC(zab_cycle_end(e.sw, &r), ZXV_OK);
            sums[run] = sums[run] * 31 + r.used_total;
        }
        CHECK_RC(zxv_hexlog_head(zab_quarantine_log(e.sw), heads[run]), ZXV_OK);
        env_close(&e);
    }
    CHECK(sums[0] == sums[1], "two runs decide the same");
    CHECK(memcmp(heads[0], heads[1], 32) == 0, "two runs log the same chain");
}

int main(void)
{
    test_basics();
    test_create();
    test_level_capacity();
    test_library_cap();
    test_no_monopoly();
    test_gate_exhaustive();
    test_quarantine_records();
    test_fail_closed();
    test_held_review();
    test_emotion();
    test_fibonacci_split();
    test_cycle_state();
    test_zero_rate();
    test_caps_and_poison();
    test_report_format();
    test_determinism();
    printf("test_zab: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

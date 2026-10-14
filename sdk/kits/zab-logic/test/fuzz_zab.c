/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_zab.c — deterministic fuzzer for ZAB-Logic.
 *
 * Each iteration builds a random swarm and drives it with random calls
 * (agents, moods, feelings, parking, rates, cycles, proposals with any of
 * the 64 tags, reviews, bad arguments). After every cycle it checks the
 * invariants:
 *   - every allotment equals the independent model of the documented rules
 *     (zab_test.h), and they sum to T when any agent is active;
 *   - no agent used more than its allotment, and an agent's admitted tokens
 *     equal what the library says it used;
 *   - expired = allotted - used; the gate outcomes match the documented
 *     table; the quarantine log counts every quarantined proposal and its
 *     chain verifies.
 *
 * usage: fuzz_zab [ITERATIONS]   (ZAB_FUZZ_SEED=n picks another stream)
 */
#include "zab_test.h"

typedef struct {
    uint32_t id;
    o_agent_t o;
    uint64_t admitted; /* tokens admitted this cycle */
} f_agent_t;

static void one(prng_t *p, uint64_t iter)
{
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zab_swarm_t *sw = NULL;
    if (zxv_ctx_create(&ctx) || zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap)) {
        CHECK(0, "setup");
        return;
    }
    uint32_t L = 1 + (uint32_t) prng_below(p, 8);
    uint64_t T = prng_below(p, 3) == 0 ? prng_below(p, 100) : prng_below(p, UINT64_C(1) << 44);
    uint32_t qcap = 1 + (uint32_t) prng_below(p, 40);
    CHECK_RC(zab_swarm_create(ctx, cap, L, T, qcap, &sw), ZXV_OK);
    if (!sw) goto out;

    f_agent_t ag[ZAB_MAX_AGENTS];
    uint32_t n = 0, per[8] = {0};
    uint32_t mood_cp = FACE_NEUTRAL, mood_k = 0;
    uint64_t quarantined = 0;
    int open = 0;
    /* what the open cycle runs with */
    f_agent_t snap[ZAB_MAX_AGENTS];
    uint32_t snap_n = 0, snap_mcp = 0, snap_mk = 0;
    uint64_t snap_T = 0, al[ZAB_MAX_AGENTS], im[ZAB_MAX_AGENTS];
    uint32_t ops = 20 + (uint32_t) prng_below(p, 200);

    for (uint32_t op = 0; op < ops && !zab_failed_closed(sw); op++) {
        uint32_t what = (uint32_t) prng_below(p, 16);
        if (what == 0 && !open) { /* add an agent */
            uint32_t d = (uint32_t) prng_below(p, L), capd = 0;
            zxv_swarm_level_capacity(d, &capd);
            uint32_t cp = PALETTE[prng_below(p, 8)], k = (uint32_t) prng_below(p, 6);
            uint32_t id = (uint32_t) prng_below(p, 1000);
            int dup = 0;
            for (uint32_t i = 0; i < n; i++) dup |= ag[i].id == id;
            zab_agent_t a = {id, d, cp, k};
            int32_t want = dup ? ZXV_E_DUPLICATE : (per[d] >= capd || n >= ZAB_MAX_AGENTS) ? ZXV_E_CAPACITY : ZXV_OK;
            CHECK_RC(zab_agent_add(sw, &a), want);
            if (want == ZXV_OK) {
                ag[n] = (f_agent_t) {id, {d, 1, cp, k}, 0};
                n++;
                per[d]++;
            }
        } else if (what == 1 && n) {
            f_agent_t *a = &ag[prng_below(p, n)];
            uint32_t act = (uint32_t) prng_below(p, 2);
            CHECK_RC(zab_agent_set_active(sw, a->id, (int32_t) act), ZXV_OK);
            a->o.active = act;
        } else if (what == 2 && n) {
            f_agent_t *a = &ag[prng_below(p, n)];
            uint32_t cp = PALETTE[prng_below(p, 8)], k = (uint32_t) prng_below(p, 7);
            CHECK_RC(zab_agent_set_feeling(sw, a->id, cp, k), k > 5 ? ZXV_E_ARG : ZXV_OK);
            if (k <= 5) {
                a->o.cp = cp;
                a->o.k = k;
            }
        } else if (what == 3) {
            uint32_t cp = PALETTE[prng_below(p, 8)], k = (uint32_t) prng_below(p, 6);
            CHECK_RC(zab_set_mood(sw, cp, k), ZXV_OK);
            mood_cp = cp;
            mood_k = k;
        } else if (what == 4) {
            uint64_t t = prng_below(p, UINT64_C(1) << 44);
            CHECK_RC(zab_set_rate(sw, t), ZXV_OK);
            T = t;
        } else if (what == 5 && !open) {
            CHECK_RC(zab_cycle_begin(sw, NULL), ZXV_OK);
            open = 1;
            memcpy(snap, ag, sizeof(f_agent_t) * n);
            snap_n = n;
            snap_mcp = mood_cp;
            snap_mk = mood_k;
            snap_T = T;
            o_agent_t oa[ZAB_MAX_AGENTS];
            for (uint32_t i = 0; i < n; i++) {
                oa[i] = ag[i].o;
                ag[i].admitted = 0;
                snap[i].admitted = 0;
            }
            o_allot(L, T, mood_cp, mood_k, oa, n, al, im);
        } else if ((what == 6 || what == 7) && open) {
            zab_cycle_report_t r;
            CHECK_RC(zab_cycle_end(sw, &r), ZXV_OK);
            open = 0;
            uint64_t sum = 0, used = 0;
            int any = 0, match = 1;
            for (uint32_t i = 0; i < snap_n; i++) {
                const zab_agent_report_t *a = &r.agents[i];
                any |= (int) snap[i].o.active;
                match &= a->id == snap[i].id && a->allotted == al[i] && a->imaginary == im[i];
                CHECK(a->used <= a->allotted, "iter %llu: agent used past its allotment",
                      (unsigned long long) iter);
                CHECK(a->used == snap[i].admitted, "iter %llu: used %llu != admitted %llu",
                      (unsigned long long) iter, (unsigned long long) a->used,
                      (unsigned long long) snap[i].admitted);
                sum += a->allotted;
                used += a->used;
            }
            CHECK(match, "iter %llu: allotments match the documented rules (L=%u T=%llu mood %x x%u)",
                  (unsigned long long) iter, L, (unsigned long long) snap_T, snap_mcp, snap_mk);
            CHECK(r.num_agents == snap_n && sum == r.allotted_total && used == r.used_total,
                  "iter %llu: report totals", (unsigned long long) iter);
            CHECK(r.allotted_total == (any ? snap_T : 0), "iter %llu: allotted == T",
                  (unsigned long long) iter);
            CHECK(r.expired == r.allotted_total - r.used_total, "iter %llu: expired",
                  (unsigned long long) iter);
            CHECK(r.quarantine_count == quarantined, "iter %llu: log count", (unsigned long long) iter);
            CHECK_RC(zxv_hexlog_verify(zab_quarantine_log(sw)), ZXV_OK);
        } else if (what >= 8 && what <= 12 && open && snap_n) {
            uint32_t i = (uint32_t) prng_below(p, snap_n);
            uint8_t tag = (uint8_t) prng_below(p, 64);
            if (prng_below(p, 3) == 0) tag = TAG_GO;
            uint64_t a = 0, u = 0;
            CHECK_RC(zab_agent_allotment(sw, snap[i].id, &a, NULL, &u), ZXV_OK);
            uint64_t left = a - u;
            uint64_t tokens = 1 + prng_below(p, left / 2 + 3);
            uint8_t work[24];
            for (size_t b = 0; b < sizeof work; b++) work[b] = (uint8_t) prng_next(p);
            zab_proposal_t pr = {snap[i].id, tag, tokens, work, (size_t) prng_below(p, sizeof work + 1)};
            zab_decision_t d;
            int32_t want_o = o_gate(tag, tokens, left);
            int32_t rc = zab_propose(sw, &pr, &d);
            if (want_o == ZAB_QUARANTINED && quarantined == qcap) {
                CHECK(rc == ZXV_E_CAPACITY && zab_failed_closed(sw), "iter %llu: full log fails closed",
                      (unsigned long long) iter);
                int32_t poisoned = 0;
                zxv_ctx_is_poisoned(ctx, &poisoned);
                CHECK(poisoned == 1, "iter %llu: context halted", (unsigned long long) iter);
                break;
            }
            if (want_o == ZAB_HELD && zab_held_count(sw) == ZAB_MAX_HELD) {
                CHECK(rc == ZXV_E_CAPACITY, "iter %llu: held queue full", (unsigned long long) iter);
                continue;
            }
            CHECK(rc == ZXV_OK && d.outcome == want_o, "iter %llu: tag %u tokens %llu left %llu: %s want %s",
                  (unsigned long long) iter, tag, (unsigned long long) tokens, (unsigned long long) left,
                  rc ? zxv_status_name(rc) : zab_outcome_name(d.outcome), zab_outcome_name(want_o));
            if (rc == ZXV_OK && d.outcome == ZAB_ADMITTED) snap[i].admitted += d.granted;
            if (rc == ZXV_OK && d.outcome == ZAB_QUARANTINED) quarantined++;
        } else if (what == 13 && open && zab_held_count(sw)) {
            zab_held_t h;
            CHECK_RC(zab_held_at(sw, (uint32_t) prng_below(p, zab_held_count(sw)), &h), ZXV_OK);
            uint8_t base = (uint8_t) prng_below(p, 4), pol = (uint8_t) prng_below(p, 4);
            uint32_t i = snap_n;
            for (uint32_t k = 0; k < snap_n; k++)
                if (snap[k].id == h.agent_id) i = k;
            if (i == snap_n) continue; /* held by an agent added after this cycle began */
            uint64_t a = 0, u = 0;
            CHECK_RC(zab_agent_allotment(sw, h.agent_id, &a, NULL, &u), ZXV_OK);
            uint8_t want_tag = 0;
            CHECK_RC(zxv_hex_set(h.tag, base, pol, &want_tag), ZXV_OK);
            int32_t want_o = o_gate(want_tag, h.tokens, a - u);
            zab_decision_t d;
            int32_t rc = zab_review(sw, h.held_id, base, pol, &d);
            if (want_o == ZAB_QUARANTINED && quarantined == qcap) {
                CHECK(rc == ZXV_E_CAPACITY && zab_failed_closed(sw), "iter %llu: review fails closed",
                      (unsigned long long) iter);
                break;
            }
            CHECK(rc == ZXV_OK && d.outcome == want_o && d.tag == want_tag, "iter %llu: review", (unsigned long long) iter);
            if (rc == ZXV_OK && d.outcome == ZAB_ADMITTED) snap[i].admitted += d.granted;
            if (rc == ZXV_OK && d.outcome == ZAB_QUARANTINED) quarantined++;
        } else if (what == 14) { /* bad arguments are refused and change nothing */
            zab_proposal_t pr = {n ? ag[0].id : 0, (uint8_t) (64 + prng_below(p, 192)), 1, NULL, 0};
            zab_decision_t d;
            CHECK_RC(zab_propose(sw, &pr, &d), ZXV_E_ARG);
            pr.tag = TAG_GO;
            pr.tokens = 0;
            CHECK_RC(zab_propose(sw, &pr, &d), ZXV_E_ARG);
            pr.tokens = 1;
            pr.agent_id = 5000 + (uint32_t) prng_below(p, 1000);
            CHECK_RC(zab_propose(sw, &pr, &d), open ? ZXV_E_NOT_FOUND : ZXV_E_STATE);
            CHECK_RC(zab_review(sw, 1u << 30, 0, 0, &d), ZXV_E_NOT_FOUND);
        } else if (what == 15) {
            CHECK_RC(zab_cycle_begin(sw, NULL), open ? ZXV_E_STATE : ZXV_OK);
            if (!open) {
                /* an extra cycle nobody proposes in: close it again */
                zab_cycle_report_t r;
                CHECK_RC(zab_cycle_end(sw, &r), ZXV_OK);
                CHECK(r.used_total == 0 && r.expired == r.allotted_total, "iter %llu: idle cycle",
                      (unsigned long long) iter);
            }
        }
    }
    zab_swarm_destroy(sw);
out:
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
}

int main(int argc, char **argv)
{
    uint64_t iters = argc > 1 ? strtoull(argv[1], NULL, 10) : 2000;
    const char *s = getenv("ZAB_FUZZ_SEED");
    prng_t p = {UINT64_C(0x5A4B) ^ (s ? strtoull(s, NULL, 10) * UINT64_C(0x9E3779B97F4A7C15) : 0)};
    for (uint64_t i = 0; i < iters && g_fail < 20; i++) one(&p, i);
    printf("fuzz_zab: %llu swarms, %d checks, %d failed (seed %s)\n", (unsigned long long) iters,
           g_checks, g_fail, s ? s : "0");
    return g_fail ? 1 : 0;
}

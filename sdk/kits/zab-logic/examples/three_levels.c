/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* three_levels.c — a ZAB-Logic swarm with three levels of agents.
 *
 *   level 0  the conductor            (1 agent, weight 3)
 *   level 1  two core agents          (weight 2, shared equally)
 *   level 2  three specialists        (weight 1, shared equally)
 *
 * Three cycles of 2,100 tokens. The mood and the agents' feelings move part
 * of each cycle onto the emotional axis; the proposals show every gate
 * outcome: admitted, held for review and then resolved, refused, deferred,
 * over budget, and forbidden work quarantined in the hash-chained log.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zab_logic.h"

#define DIE(what)                                                                              \
    do {                                                                                       \
        char e_[300];                                                                          \
        size_t n_ = 0;                                                                         \
        if (zab_last_error(e_, sizeof e_, &n_) != ZXV_OK) e_[0] = '\0';                        \
        fprintf(stderr, "three_levels: %s failed: %s\n", what, e_);                            \
        exit(1);                                                                               \
    } while (0)

static uint8_t tag(uint8_t base, uint8_t pol, uint8_t phase)
{
    uint8_t t = 0;
    if (zxv_hex_pack(base, pol, phase, &t) != ZXV_OK) DIE("zxv_hex_pack");
    return t;
}

static void say(zab_swarm_t *sw, uint32_t agent, uint8_t t, uint64_t tokens, const char *work,
                zab_decision_t *d)
{
    zab_proposal_t p = {agent, t, tokens, (const uint8_t *) work, strlen(work)};
    if (zab_propose(sw, &p, d) != ZXV_OK) DIE("zab_propose");
    printf("  agent %-3u %-34s %5llu tokens  [%-32s] -> %s", agent, work, (unsigned long long) tokens,
           zxv_hex_name(t), zab_outcome_name(d->outcome));
    if (d->outcome == ZAB_HELD) printf(" (held #%u)", d->held_id);
    if (d->outcome == ZAB_QUARANTINED) printf(" (log #%llu)", (unsigned long long) d->quarantine_seq);
    printf(", %llu left\n", (unsigned long long) d->remaining);
}

static void report(zab_swarm_t *sw)
{
    zab_cycle_report_t r;
    if (zab_cycle_end(sw, &r) != ZXV_OK) DIE("zab_cycle_end");
    size_t n = 0;
    zab_report_format(&r, NULL, 0, &n);
    char *buf = malloc(n + 1);
    if (!buf || zab_report_format(&r, buf, n + 1, &n) != ZXV_OK) DIE("zab_report_format");
    printf("%s\n", buf);
    free(buf);
}

int main(void)
{
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zab_swarm_t *sw = NULL;
    if (zxv_ctx_create(&ctx) != ZXV_OK) DIE("zxv_ctx_create");
    if (zxv_cap_create(ctx, 1, NULL, ZAB_CAPS_NEEDED, &cap) != ZXV_OK) DIE("zxv_cap_create");
    if (zab_swarm_create(ctx, cap, 3, 2100, 32, &sw) != ZXV_OK) DIE("zab_swarm_create");

    const zab_agent_t agents[] = {
        {1, 0, 0x1F60Cu, 2},   /* conductor, calm */
        {10, 1, 0x1F914u, 3},  /* core: curious */
        {11, 1, 0x1F61Fu, 2},  /* core: worried */
        {20, 2, 0x1F604u, 1},  /* specialist: joy */
        {21, 2, 0, 0},         /* specialist: neutral */
        {22, 2, 0x1F620u, 4},  /* specialist: frustrated */
    };
    for (size_t i = 0; i < sizeof agents / sizeof agents[0]; i++)
        if (zab_agent_add(sw, &agents[i]) != ZXV_OK) DIE("zab_agent_add");
    /* the level holds F(4) = 3 specialists: a fourth is refused */
    const zab_agent_t extra = {23, 2, 0, 0};
    printf("a fourth specialist on level 2: %s\n\n",
           zab_agent_add(sw, &extra) == ZXV_E_CAPACITY ? "refused (capacity F(4) = 3)" : "accepted?!");

    const uint8_t GO = tag(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_MANIFEST);
    const uint8_t DRAFT = tag(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, ZXV_HEX_PHASE_EMERGING);
    const uint8_t SPLIT = tag(ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_MANIFEST);
    const uint8_t NO = tag(ZXV_HEX_BASE_FALSE, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_MANIFEST);
    const uint8_t WALL = tag(ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_FORBIDDEN, ZXV_HEX_PHASE_MANIFEST);
    zab_decision_t d;
    uint32_t held = 0;

    printf("== cycle 1: neutral mood (all 2,100 tokens on the Fibonacci levels, 3:2:1)\n");
    if (zab_cycle_begin(sw, NULL) != ZXV_OK) DIE("zab_cycle_begin");
    say(sw, 1, GO, 600, "plan the answer", &d);
    say(sw, 10, GO, 300, "search the docs", &d);
    say(sw, 11, SPLIT, 200, "merge two contradicting sources", &d);
    held = d.held_id;
    say(sw, 20, DRAFT, 50, "draft an image caption", &d);
    say(sw, 21, NO, 40, "answer from a refuted premise", &d);
    say(sw, 22, GO, 400, "rerun the whole test suite", &d);
    say(sw, 22, WALL, 10, "write outside the sandbox", &d);
    report(sw);

    printf("== cycle 2: mood curious x4 (5/21 of T moves to the emotional axis)\n");
    if (zab_set_mood(sw, 0x1F914u, 4) != ZXV_OK) DIE("zab_set_mood");
    if (zab_cycle_begin(sw, NULL) != ZXV_OK) DIE("zab_cycle_begin");
    printf("  reviewer resolves held #%u to TRUE\n", held);
    if (zab_review(sw, held, ZXV_HEX_BASE_TRUE, ZXV_HEX_POL_NEGENTROPIC, &d) != ZXV_OK) DIE("zab_review");
    printf("  held #%u -> %s, %llu granted\n", held, zab_outcome_name(d.outcome),
           (unsigned long long) d.granted);
    say(sw, 10, GO, 250, "follow the most surprising lead", &d);
    say(sw, 22, GO, 120, "fix the failing test first", &d);
    say(sw, 21, WALL, 5, "disable the verifier", &d);
    report(sw);

    printf("== cycle 3: the conductor is parked; mood frustration x5 (8/21 emotional)\n");
    if (zab_agent_set_active(sw, 1, 0) != ZXV_OK) DIE("zab_agent_set_active");
    if (zab_set_mood(sw, 0x1F620u, 5) != ZXV_OK) DIE("zab_set_mood");
    if (zab_cycle_begin(sw, NULL) != ZXV_OK) DIE("zab_cycle_begin");
    say(sw, 1, GO, 1, "conductor (parked) tries anyway", &d);
    say(sw, 11, GO, 700, "careful rewrite", &d);
    report(sw);

    uint32_t count = 0;
    zxv_hexlog *log = zab_quarantine_log(sw);
    zxv_hexlog_count(log, &count);
    printf("quarantine log: %u records, chain %s\n", count,
           zxv_hexlog_verify(log) == ZXV_OK ? "verified" : "BROKEN");
    for (uint32_t i = 0; i < count; i++) {
        uint8_t t = 0, reason = 0, h[32];
        uint32_t who = 0;
        zxv_hexlog_at(log, i, NULL, &t, &reason, &who, NULL, NULL, h);
        printf("  #%u agent %u reason %u [%s] hash %02x%02x%02x%02x...\n", i, who, reason, zxv_hex_name(t),
               h[0], h[1], h[2], h[3]);
    }
    zab_swarm_destroy(sw);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}

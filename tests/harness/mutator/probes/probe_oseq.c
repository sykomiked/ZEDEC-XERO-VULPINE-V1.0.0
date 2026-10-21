/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* probe_oseq.c: differential probe for kernel/src/oseq/oseq.c (classify.py).
 * Random registries through the API, plus registries whose public event
 * table is edited afterwards (wide DAGs, loops at either end). */
#include <string.h>
#include "probe_common.h"
#include "oseq.h"

static oseq_registry_t R;

static const char *names[] = {"", "a", "b", "ab", "abc", "abd", "node-7", "0123456789012345678901234567890",
                              "01234567890123456789012345678901", "0123456789012345678901234567890123"};

static void queries(const char *tag, int depth_ok) {
    PROBE_STATE(tag, R);
    printf(" st %u %u %u %u %u n%u e%u rp%u/%u\n", oseq_count_by_status(&R, 0), oseq_count_by_status(&R, 1),
           oseq_count_by_status(&R, 2), oseq_count_by_status(&R, 3), oseq_count_by_status(&R, 4), R.node_count,
           R.event_count, R.replay_head, R.replay_count);
    uint32_t n = R.event_count;
    int acyclic = 1;
    printf(" cyc");
    for (uint32_t i = 0; i <= n && i < OSEQ_MAX_EVENTS + 1; i++) {
        int c = (int)oseq_detect_cycle(&R, i);
        if (i < n && c) acyclic = 0;
        printf("%d", c);
    }
    printf("\n anc");
    for (uint32_t k = 0; k < 300; k++) {
        uint32_t a = probe_below(n + 2), b = probe_below(n + 2);
        if (k % 5 == 0) b = a;
        printf(" %d%d", (int)oseq_is_ancestor(&R, a, b), (int)oseq_happens_before(&R, a, b));
    }
    printf("\n");
    if (depth_ok && acyclic && n <= 60) { /* get_depth recurses without a memo: small acyclic graphs only */
        printf(" depth");
        for (uint32_t i = 0; i <= n; i++) printf(" %u", oseq_get_depth(&R, i));
        printf("\n");
    }
}

int main(void) {
    printf("null %d %p %d %d %d %d %d %d %d %u %p %u\n", oseq_register_node(NULL, "a", 1), (void *)oseq_get_node(NULL, 0),
           oseq_find_node(NULL, "a"), oseq_register_event(NULL, 0, "s", 1, NULL, 0, NULL, 0),
           (int)oseq_commit_event(NULL, 0), (int)oseq_reject_event(NULL, 0), (int)oseq_happens_before(NULL, 0, 1),
           (int)oseq_is_ancestor(NULL, 0, 1), (int)oseq_detect_cycle(NULL, 0), oseq_get_depth(NULL, 0),
           (void *)oseq_get_event(NULL, 0), oseq_count_by_status(NULL, 0));
    oseq_registry_init(NULL);
    oseq_record_replay(NULL, (const uint8_t *)"x", 1);
    printf("replay null %d\n", (int)oseq_check_replay(NULL, (const uint8_t *)"x", 1));
    for (uint64_t o = 0; o < 4; o++)
        printf("ord %llu %d %d %d\n", (unsigned long long)o, (int)oseq_dag_is_valid_ordinal(o),
               (int)oseq_dag_is_later(o, 1), (int)oseq_dag_is_later(2, o));
    printf("ord max %d %d\n", (int)oseq_dag_is_valid_ordinal(UINT64_MAX), (int)oseq_dag_is_later(UINT64_MAX, 0));
    for (int s = -1; s < 6; s++) printf("status %d %s\n", s, oseq_status_name((oseq_status_t)s));

    uint8_t hs[64][40];
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 40; j++) hs[i][j] = (uint8_t)(i * 37 + j * 11);
    for (uint64_t seed = 1; seed <= 36; seed++) {
        probe_seed(seed);
        memset(&R, 0x5A, sizeof R);
        oseq_registry_init(&R);
        uint32_t nn = seed % 6 == 0 ? 70 : 1 + probe_below(10);
        for (uint32_t i = 0; i < nn; i++) {
            char nm[48];
            const char *base = names[probe_below(10)];
            if (seed % 6 == 0) { snprintf(nm, sizeof nm, "n%u", i % 66); base = nm; }
            int r = oseq_register_node(&R, base, probe_below(3));
            printf("node [%s] %d", base, r);
            if (r >= 0) printf(" inc%u seq%llu act%d", R.nodes[r].incarnation,
                               (unsigned long long)R.nodes[r].next_sequence, (int)R.nodes[r].active);
            printf(" find %d\n", oseq_find_node(&R, base));
        }
        for (uint32_t i = 0; i < 10; i++) printf("findn [%s] %d\n", names[i], oseq_find_node(&R, names[i]));
        oseq_node_t *g1 = oseq_get_node(&R, R.node_count), *g0 = oseq_get_node(&R, 0);
        printf("getnode %d %d\n", g1 ? (int)(g1 - R.nodes) : -1, g0 ? (int)(g0 - R.nodes) : -1);
        uint32_t ne = seed % 4 == 0 ? 530 : probe_below(200);
        uint32_t maxp = seed % 3 == 0 ? 4 : 2;
        for (uint32_t i = 0; i < ne; i++) {
            uint32_t par[5];
            uint8_t np = (uint8_t)probe_below(maxp + 1);
            if (probe_below(40) == 0) np = 5;
            for (uint32_t k = 0; k < 5; k++)
                par[k] = R.event_count && probe_below(20) ? probe_below(R.event_count) : probe_below(R.event_count + 3);
            uint32_t hl = probe_below(6) ? 32 : probe_below(41);
            const uint8_t *h = probe_below(8) ? hs[probe_below(probe_below(3) ? 64 : 4)] : NULL;
            int r = oseq_register_event(&R, probe_below(R.node_count + 1), probe_below(6) ? "zxv.ev" : NULL,
                                        (uint16_t)i, h, hl, probe_below(12) ? par : NULL, np);
            printf("ev %d", r);
            if (r >= 0) {
                oseq_event_t *e = oseq_get_event(&R, (uint32_t)r);
                printf(" o%llu n%u s%llu np%u p%u,%u,%u,%u hl%u", (unsigned long long)e->ordinal, e->node_idx,
                       (unsigned long long)e->local_sequence, e->num_parents, e->parent_ordinals[0],
                       e->parent_ordinals[1], e->parent_ordinals[2], e->parent_ordinals[3], e->payload_hash_len);
            }
            printf(" rd%u rj%u\n", R.replays_detected, R.rejections);
            if (probe_below(3) == 0) {
                uint32_t c = probe_below(R.event_count + 1);
                printf(" commit %u %d", c, (int)oseq_commit_event(&R, c));
                c = probe_below(R.event_count + 1);
                printf(" reject %u %d\n", c, (int)oseq_reject_event(&R, c));
            }
        }
        for (uint32_t i = 0; i < 70; i++) {
            uint32_t hl = probe_below(41);
            printf("chk %u %d", hl, (int)oseq_check_replay(&R, hs[probe_below(64)], hl));
            if (i % 7 == 0) oseq_record_replay(&R, hs[probe_below(64)], hl);
            printf("\n");
        }
        queries("api", 1);
        /* edit the public table: loops at the start, the end and through a far parent */
        if (R.event_count > 3) {
            for (int t = 0; t < 4; t++) {
                uint32_t i = t == 0 ? 0 : t == 1 ? R.event_count - 1 : probe_below(R.event_count);
                oseq_event_t *e = &R.events[i];
                uint8_t save_np = e->num_parents;
                uint32_t save_p[4];
                memcpy(save_p, e->parent_ordinals, sizeof save_p);
                e->num_parents = (uint8_t)(1 + probe_below(4));
                e->parent_ordinals[e->num_parents - 1] = t == 3 ? i : R.event_count - 1 - probe_below(2);
                if (e->status == OSEQ_STATUS_PENDING || probe_below(2)) e->status = OSEQ_STATUS_PENDING;
                printf("edit %u -> commit %d\n", i, (int)oseq_commit_event(&R, i));
                queries("edited", 0);
                e->num_parents = save_np;
                memcpy(e->parent_ordinals, save_p, sizeof save_p);
            }
        }
    }
    /* a full event table: every index query at the edge, 512 and past it */
    oseq_registry_init(&R);
    oseq_register_node(&R, "full", 1);
    for (uint32_t i = 0; i < OSEQ_MAX_EVENTS + 2; i++) {
        uint32_t par[2] = {i ? i - 1 : 0, i / 2};
        printf("%d ", oseq_register_event(&R, 0, "f", 1, NULL, 0, par, (uint8_t)(i ? 2 : 0)));
    }
    printf("\n");
    probe_seed(7);
    queries("full", 0);
    for (uint32_t i = OSEQ_MAX_EVENTS - 2; i < OSEQ_MAX_EVENTS + 2; i++)
        printf("edge %u %d %d %d %d %d\n", i, (int)oseq_is_ancestor(&R, 0, i), (int)oseq_is_ancestor(&R, i, 0),
               (int)oseq_happens_before(&R, 0, i), (int)oseq_detect_cycle(&R, i), (int)oseq_commit_event(&R, i));
    printf("depth tail %u %u\n", oseq_get_depth(&R, 20), oseq_get_depth(&R, OSEQ_MAX_EVENTS));
    return 0;
}

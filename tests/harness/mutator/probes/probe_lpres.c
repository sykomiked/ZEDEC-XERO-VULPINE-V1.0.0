/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* probe_lpres.c: differential probe for kernel/src/lpres/lpres.c (classify.py). */
#include <string.h>
#include "probe_common.h"
#include "lpres.h"

static lpres_registry_t R;

static void dump(const char *tag) {
    PROBE_STATE(tag, R);
    printf(" counts");
    for (int s = 0; s < 5; s++) printf(" %u", lpres_count_by_state(&R, (lpres_state_t)s));
    for (int t = 0; t < 16; t++) printf(" %u", lpres_count_by_type(&R, (lpres_subject_type_t)t));
    printf(" c%u u%u gate %d n%u t%llu\n", lpres_count_contradictions(&R), lpres_count_unattested(&R),
           (int)lpres_safety_gate_clear(&R), R.count, (unsigned long long)R.current_time);
}

static const char *names[] = {"", "a", "b", "ab", "abc", "abd", "xbc", "abx", "axc", "sensor.temp",
                              "0123456789012345678901234567890",  /* 31: fits */
                              "01234567890123456789012345678901", /* 32: clipped */
                              "012345678901234567890123456789012345"};

int main(void) {
    for (int a = 0; a < 6; a++) {
        printf("s%d neg %d cert %d contra %d unk %d auth %d name %s\n", a, (int)lpres_negate((lpres_state_t)a),
               (int)lpres_is_certain((lpres_state_t)a), (int)lpres_is_contradictory((lpres_state_t)a),
               (int)lpres_is_unknown((lpres_state_t)a), (int)lpres_implies_actuator_authority((lpres_state_t)a),
               lpres_state_name((lpres_state_t)a));
        for (int b = 0; b < 6; b++)
            printf(" %d.%d c%d d%d k%d\n", a, b, (int)lpres_conjoin((lpres_state_t)a, (lpres_state_t)b),
                   (int)lpres_disjoin((lpres_state_t)a, (lpres_state_t)b), (int)lpres_kjoin((lpres_state_t)a, (lpres_state_t)b));
    }
    for (int t = -1; t < 17; t++) printf("subj %d %s\n", t, lpres_subject_name((lpres_subject_type_t)t));
    printf("null %d %p %p %d %d %d %u %u %u %u %d\n", lpres_attest(NULL, "x", 0, 1, "y", NULL, 0),
           (void *)lpres_find(NULL, "x"), (void *)lpres_get(NULL, 0), (int)lpres_add_evidence(NULL, 0, true),
           (int)lpres_revoke(NULL, 0), (int)lpres_update_state(NULL, 0, 1), lpres_count_by_state(NULL, 1),
           lpres_count_by_type(NULL, 1), lpres_count_contradictions(NULL), lpres_count_unattested(NULL),
           (int)lpres_safety_gate_clear(NULL));
    lpres_registry_init(NULL);
    lpres_advance_time(NULL, 5);

    uint8_t dg[40];
    for (int i = 0; i < 40; i++) dg[i] = (uint8_t)(0xA0 + i);
    for (uint64_t seed = 1; seed <= 30; seed++) {
        probe_seed(seed);
        memset(&R, 0x5A, sizeof R);
        lpres_registry_init(&R);
        dump("init");
        printf("find empty %p gate %d\n", (void *)lpres_find(&R, "nothing"), (int)lpres_safety_gate_clear(&R));
        int steps = seed % 5 == 0 ? 400 : 120;
        for (int s = 0; s < steps; s++) {
            uint32_t op = probe_below(8);
            uint32_t idx = probe_below(R.count + 2);
            if (op < 3) {
                const char *nm = probe_below(10) ? names[probe_below(13)] : NULL;
                const char *at = probe_below(5) ? names[probe_below(13)] : NULL;
                uint32_t dl = probe_below(41);
                int r = lpres_attest(&R, nm, (lpres_subject_type_t)probe_below(16), (lpres_state_t)probe_below(5), at,
                                     probe_below(4) ? dg : NULL, dl);
                printf("att %d dl%u", r, dl);
                if (r >= 0) {
                    lpres_attestation_t *a = lpres_get(&R, (uint32_t)r);
                    printf(" [%s|%s] st%d ty%d dlen%u ts%llu p%u n%u rv%d", a->name, a->attester, (int)a->state,
                           (int)a->subject_type, a->digest_len, (unsigned long long)a->timestamp, a->positive_evidence,
                           a->negative_evidence, (int)a->revoked);
                    for (int i = 0; i < LPRES_MAX_DIGEST_LEN; i++) printf("%02x", a->digest[i]);
                }
                printf("\n");
            } else if (op == 3) {
                printf("ev %u %d", idx, (int)lpres_add_evidence(&R, idx, probe_below(2)));
                lpres_attestation_t *a = lpres_get(&R, idx);
                if (a) printf(" st%d p%u n%u", (int)a->state, a->positive_evidence, a->negative_evidence);
                printf("\n");
            } else if (op == 4) {
                printf("rev %u %d\n", idx, (int)lpres_revoke(&R, idx));
            } else if (op == 5) {
                lpres_state_t ns = (lpres_state_t)probe_below(5);
                printf("upd %u %d %d\n", idx, (int)ns, (int)lpres_update_state(&R, idx, ns));
            } else if (op == 6) {
                const char *nm = names[probe_below(13)];
                lpres_attestation_t *f = lpres_find(&R, nm);
                printf("find [%s] %d\n", nm, f ? (int)(f - R.attestations) : -1);
            } else {
                lpres_advance_time(&R, probe_below(1000));
                lpres_attestation_t *g = lpres_get(&R, idx);
                printf("get %u %d\n", idx, g ? (int)(g - R.attestations) : -1);
            }
        }
        dump("walk");
    }
    return 0;
}

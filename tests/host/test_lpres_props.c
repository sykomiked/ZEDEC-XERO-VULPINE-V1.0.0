/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_lpres_props.c — behaviour tests for the K3 presence logic and the
 * attestation registry (kernel/src/lpres/lpres.c).
 *
 * The four-valued tables are written out here in full from the rule in
 * lpres.c: conjoin is min and disjoin is max on the chain F < N < B < T, so
 * no mix of BOTH and NEITHER is ever TRUE (CLAUDE.md: not Belnap's lattice).
 * The registry is driven against a reference that follows the header: the
 * evidence counts decide the state, revoked entries drop out of every count,
 * and the safety gate opens only when every live entry is certain. */
#include <stdio.h>
#include <string.h>
#include "lpres.h"

static int failures = 0;
static unsigned checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            if (failures < 40) printf("  FAIL: %s (line %d)\n", msg, __LINE__);                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define N LPRES_STATE_NEITHER
#define T LPRES_STATE_TRUE
#define F LPRES_STATE_FALSE
#define B LPRES_STATE_BOTH

/* rows a, columns b, in the enum order N, T, F, B */
static const lpres_state_t AND[4][4] = {
    /*        N  T  F  B */
    /* N */ {N, N, F, N},
    /* T */ {N, T, F, B},
    /* F */ {F, F, F, F},
    /* B */ {N, B, F, B},
};
static const lpres_state_t OR[4][4] = {
    /*        N  T  F  B */
    /* N */ {N, T, N, B},
    /* T */ {T, T, T, T},
    /* F */ {N, T, F, B},
    /* B */ {B, T, B, B},
};
/* the information join: bits 0 (true evidence) and 1 (false evidence) */
static const lpres_state_t KJOIN[4][4] = {
    /*        N  T  F  B */
    /* N */ {N, T, F, B},
    /* T */ {T, T, B, B},
    /* F */ {F, B, F, B},
    /* B */ {B, B, B, B},
};

static int rank(lpres_state_t s) { return s == F ? 0 : s == N ? 1 : s == B ? 2 : 3; }

static void test_logic(void)
{
    static const lpres_state_t NEG[4] = {N, F, T, B};
    for (int a = 0; a < 4; a++) {
        CHECK(lpres_negate((lpres_state_t)a) == NEG[a], "negation table");
        CHECK(lpres_negate(lpres_negate((lpres_state_t)a)) == (lpres_state_t)a, "double negation");
        CHECK(lpres_is_certain((lpres_state_t)a) == (a == T || a == F), "certain = T or F");
        CHECK(lpres_is_contradictory((lpres_state_t)a) == (a == B), "contradictory = B");
        CHECK(lpres_is_unknown((lpres_state_t)a) == (a == N), "unknown = N");
        CHECK(lpres_implies_actuator_authority((lpres_state_t)a) == (a == T), "only TRUE authorises");
        for (int b = 0; b < 4; b++) {
            lpres_state_t x = (lpres_state_t)a, y = (lpres_state_t)b;
            CHECK(lpres_conjoin(x, y) == AND[a][b], "conjoin table");
            CHECK(lpres_disjoin(x, y) == OR[a][b], "disjoin table");
            CHECK(lpres_kjoin(x, y) == KJOIN[a][b], "kjoin table");
            /* the chain rule the tables come from */
            CHECK(rank(lpres_conjoin(x, y)) == (rank(x) < rank(y) ? rank(x) : rank(y)), "min");
            CHECK(rank(lpres_disjoin(x, y)) == (rank(x) > rank(y) ? rank(x) : rank(y)), "max");
            if (a != T && b != T)
                CHECK(lpres_conjoin(x, y) != T && lpres_disjoin(x, y) != T,
                      "nothing but TRUE inputs yields TRUE");
            CHECK(lpres_conjoin(x, y) == lpres_conjoin(y, x), "conjoin commutes");
            CHECK(lpres_disjoin(x, y) == lpres_disjoin(y, x), "disjoin commutes");
            for (int c = 0; c < 4; c++) {
                lpres_state_t z = (lpres_state_t)c;
                CHECK(lpres_conjoin(lpres_conjoin(x, y), z) == lpres_conjoin(x, lpres_conjoin(y, z)),
                      "conjoin associates");
                CHECK(lpres_disjoin(lpres_disjoin(x, y), z) == lpres_disjoin(x, lpres_disjoin(y, z)),
                      "disjoin associates");
            }
        }
    }
    CHECK(lpres_conjoin(B, N) == N && lpres_disjoin(B, N) == B,
          "the documented deviation: B and N meet at N, join at B");
    CHECK(lpres_negate((lpres_state_t)7) == N, "an out-of-range state negates to NEITHER");
    /* closure: whatever the inputs, the result is one of the four states, and
     * a value outside them reads as NEITHER, as in negation */
    for (int a = 0; a < 9; a++)
        for (int b = 0; b < 9; b++) {
            lpres_state_t x = (lpres_state_t)a, y = (lpres_state_t)b;
            lpres_state_t nx = a > 3 ? N : x, ny = b > 3 ? N : y;
            CHECK(lpres_conjoin(x, y) == lpres_conjoin(nx, ny) && lpres_disjoin(x, y) == lpres_disjoin(nx, ny),
                  "an out-of-range state is NEITHER to conjoin and disjoin");
            CHECK((unsigned)lpres_conjoin(x, y) <= 3u && (unsigned)lpres_disjoin(x, y) <= 3u,
                  "conjoin and disjoin are closed over the four states");
        }
    CHECK(lpres_conjoin(T, (lpres_state_t)5) == N && lpres_disjoin(F, (lpres_state_t)5) == N &&
              lpres_conjoin((lpres_state_t)5, T) == N && lpres_disjoin((lpres_state_t)5, F) == N,
          "the identities do not pass an out-of-range value through");
    CHECK(lpres_kjoin((lpres_state_t)4, T) == T, "kjoin keeps two bits");
    CHECK(strcmp(lpres_state_name(B), "both") == 0 && strcmp(lpres_state_name(N), "neither") == 0 &&
              strcmp(lpres_state_name(T), "true") == 0 && strcmp(lpres_state_name(F), "false") == 0 &&
              strcmp(lpres_state_name((lpres_state_t)9), "unknown") == 0,
          "state names");
    static const char *const SUBJ[15] = {"source", "dialect", "dependency", "runtime", "data",
                                         "symbol", "model_pack", "geometry", "parameter", "sensor",
                                         "actuator", "calibration", "format", "key", "firmware"};
    for (int k = 0; k < 15; k++)
        CHECK(strcmp(lpres_subject_name((lpres_subject_type_t)k), SUBJ[k]) == 0, "subject names");
    CHECK(strcmp(lpres_subject_name((lpres_subject_type_t)15), "unknown") == 0, "unknown subject");
}

/* ------------------------------------------------ registry reference */

typedef struct {
    lpres_state_t state;
    uint32_t pos, neg;
    int revoked;
    lpres_subject_type_t type;
} ref_t;

static uint64_t rs = 0x853C49E6748FEA9BULL;
static uint32_t rnd(uint32_t n)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return (uint32_t)(rs % n);
}

static void test_registry_random(void)
{
    static lpres_registry_t reg;
    for (int round = 0; round < 300; round++) {
        ref_t ref[LPRES_MAX_ATTESTATIONS];
        uint32_t n = 0;
        uint64_t now = 0;
        lpres_registry_init(&reg);
        CHECK(reg.count == 0 && reg.current_time == 0, "init clears");
        for (int step = 0; step < 400; step++) {
            uint32_t op = rnd(7);
            if (op == 0 && n < LPRES_MAX_ATTESTATIONS) {
                char name[24];
                uint8_t dg[40];
                snprintf(name, sizeof name, "subj%u", n);
                lpres_state_t s = (lpres_state_t)rnd(4);
                lpres_subject_type_t ty = (lpres_subject_type_t)rnd(15);
                uint32_t dl = rnd(41);
                for (uint32_t k = 0; k < dl; k++) dg[k] = (uint8_t)(n * 7 + k);
                int32_t idx = lpres_attest(&reg, name, ty, s, rnd(2) ? "kernel" : 0, dg, dl);
                CHECK(idx == (int32_t)n, "attest returns the next index");
                const lpres_attestation_t *a = lpres_get(&reg, n);
                CHECK(a && strcmp(a->name, name) == 0 && a->state == s && a->subject_type == ty,
                      "attest stores name, state and type");
                CHECK(a && a->timestamp == now && !a->revoked, "attest stamps the time");
                uint32_t keep = dl > LPRES_MAX_DIGEST_LEN ? LPRES_MAX_DIGEST_LEN : dl;
                CHECK(a && a->digest_len == keep && memcmp(a->digest, dg, keep) == 0,
                      "digest kept, clipped at 32");
                CHECK(a && a->positive_evidence == (s == T || s == B) &&
                          a->negative_evidence == (s == F || s == B),
                      "initial evidence follows the state");
                ref[n].state = s;
                ref[n].pos = (s == T || s == B);
                ref[n].neg = (s == F || s == B);
                ref[n].revoked = 0;
                ref[n].type = ty;
                n++;
            } else if (op == 1 && n) {
                uint32_t i = rnd(n);
                int pos = (int)rnd(2);
                bool ok = lpres_add_evidence(&reg, i, pos);
                CHECK(ok == !ref[i].revoked, "evidence refused only on a revoked entry");
                if (ok) {
                    if (pos) ref[i].pos++;
                    else ref[i].neg++;
                    ref[i].state = ref[i].pos && ref[i].neg ? B : ref[i].pos ? T : ref[i].neg ? F : N;
                }
            } else if (op == 2 && n) {
                uint32_t i = rnd(n);
                lpres_state_t s = (lpres_state_t)rnd(4);
                bool ok = lpres_update_state(&reg, i, s);
                CHECK(ok == !ref[i].revoked, "update refused only on a revoked entry");
                if (ok) ref[i].state = s;
            } else if (op == 3 && n && rnd(4) == 0) {
                uint32_t i = rnd(n);
                CHECK(lpres_revoke(&reg, i), "revoke");
                ref[i].revoked = 1;
            } else if (op == 4) {
                uint64_t d = rnd(1000);
                lpres_advance_time(&reg, d);
                now += d;
                CHECK(reg.current_time == now, "time advances by delta");
            }
            /* every query against the reference */
            uint32_t by_state[4] = {0}, by_type[15] = {0}, live = 0, uncertain = 0;
            for (uint32_t i = 0; i < n; i++) {
                const lpres_attestation_t *a = lpres_get(&reg, i);
                CHECK(a && a->state == ref[i].state && a->revoked == ref[i].revoked &&
                          a->positive_evidence == ref[i].pos && a->negative_evidence == ref[i].neg,
                      "entry = reference");
                if (ref[i].revoked) continue;
                live++;
                by_state[ref[i].state]++;
                by_type[ref[i].type]++;
                uncertain += !(ref[i].state == T || ref[i].state == F);
            }
            for (int s = 0; s < 4; s++)
                CHECK(lpres_count_by_state(&reg, (lpres_state_t)s) == by_state[s], "count by state");
            for (int t = 0; t < 15; t++)
                CHECK(lpres_count_by_type(&reg, (lpres_subject_type_t)t) == by_type[t], "count by type");
            CHECK(lpres_count_contradictions(&reg) == by_state[B], "contradictions = live BOTH");
            CHECK(lpres_count_unattested(&reg) == by_state[N], "unattested = live NEITHER");
            if (live) CHECK(lpres_safety_gate_clear(&reg) == (uncertain == 0),
                            "gate clear iff every live entry is certain");
        }
        CHECK(lpres_get(&reg, n) == 0, "get past the end");
        CHECK(!lpres_add_evidence(&reg, n, true) && !lpres_update_state(&reg, n, T) &&
                  !lpres_revoke(&reg, n),
              "operations past the end refused");
    }
}

static void test_registry_edges(void)
{
    static lpres_registry_t reg;
    lpres_registry_init(0);
    lpres_advance_time(0, 5);
    lpres_registry_init(&reg);
    CHECK(lpres_attest(0, "x", 0, T, "k", 0, 0) == -1, "attest NULL registry");
    CHECK(lpres_attest(&reg, 0, 0, T, "k", 0, 0) == -1, "attest NULL name");
    CHECK(reg.count == 0, "refusals add nothing");
    uint8_t d[4] = {1, 2, 3, 4};
    CHECK(lpres_attest(&reg, "nodigest", 0, T, "k", d, 0) == 0 &&
              lpres_get(&reg, 0)->digest_len == 0,
          "a zero-length digest stores none");
    CHECK(lpres_attest(&reg, "nullptr", 0, T, "k", 0, 4) == 1 && lpres_get(&reg, 1)->digest_len == 0,
          "a NULL digest stores none");
    /* names: clipped to 31 bytes + NUL, matched exactly, prefix is not a match */
    char longname[64];
    memset(longname, 'n', sizeof longname - 1);
    longname[63] = 0;
    CHECK(lpres_attest(&reg, longname, 0, T, "a-very-long-attester-name-past-thirty-two", 0, 0) == 2,
          "long name accepted");
    CHECK(strlen(lpres_get(&reg, 2)->name) == LPRES_MAX_NAME_LEN - 1 &&
              strlen(lpres_get(&reg, 2)->attester) == LPRES_MAX_NAME_LEN - 1,
          "name and attester clipped to 31");
    CHECK(lpres_attest(&reg, "abc", 0, F, 0, 0, 0) == 3 && lpres_get(&reg, 3)->attester[0] == 0,
          "NULL attester stored empty");
    CHECK(lpres_find(&reg, "abc") == lpres_get(&reg, 3), "find exact");
    CHECK(lpres_find(&reg, "ab") == 0 && lpres_find(&reg, "abcd") == 0, "a prefix is not a match");
    CHECK(lpres_find(&reg, "xbc") == 0 && lpres_find(&reg, "abx") == 0 &&
              lpres_find(&reg, "axc") == 0,
          "same length, one byte different (first, last, middle) is not a match");
    CHECK(lpres_find(&reg, "") == 0, "empty name matches nothing");
    /* the digest copy stops at digest_len: the byte after it stays zero */
    uint8_t d9[9] = {9, 8, 7, 6, 5, 4, 3, 2, 1};
    CHECK(lpres_attest(&reg, "d8", 0, T, "k", d9, 8) == 4 && lpres_get(&reg, 4)->digest_len == 8 &&
              memcmp(lpres_get(&reg, 4)->digest, d9, 8) == 0 && lpres_get(&reg, 4)->digest[8] == 0,
          "digest copies exactly digest_len bytes");
    CHECK(lpres_revoke(&reg, 4), "drop d8 from the counts below");
    CHECK(lpres_find(0, "abc") == 0 && lpres_find(&reg, 0) == 0, "find NULL");
    CHECK(lpres_get(0, 0) == 0, "get NULL");
    CHECK(!lpres_add_evidence(0, 0, true) && !lpres_update_state(0, 0, T) && !lpres_revoke(0, 0),
          "NULL registry refused");
    CHECK(lpres_count_by_state(0, T) == 0 && lpres_count_by_type(0, 0) == 0 &&
              !lpres_safety_gate_clear(0),
          "NULL queries");

    /* evidence: one negative on a TRUE entry makes it BOTH; the gate closes */
    CHECK(lpres_safety_gate_clear(&reg), "two TRUE, one FALSE, one long TRUE: certain");
    CHECK(lpres_add_evidence(&reg, 0, false) && lpres_get(&reg, 0)->state == B, "T + negative = B");
    CHECK(!lpres_safety_gate_clear(&reg), "a contradiction closes the gate");
    CHECK(lpres_revoke(&reg, 0) && lpres_safety_gate_clear(&reg), "revoking it reopens the gate");
    CHECK(!lpres_add_evidence(&reg, 0, true) && !lpres_update_state(&reg, 0, T),
          "a revoked entry is frozen");
    CHECK(lpres_get(&reg, 0)->state == B, "and keeps its state");
    CHECK(lpres_update_state(&reg, 1, N) && !lpres_safety_gate_clear(&reg), "NEITHER closes the gate");
    lpres_get(&reg, 1)->positive_evidence = 0;
    lpres_get(&reg, 1)->negative_evidence = 0;
    CHECK(lpres_add_evidence(&reg, 1, false) && lpres_get(&reg, 1)->state == F, "N + negative = F");
    /* evidence counts saturate instead of wrapping back to "nothing known" */
    lpres_get(&reg, 1)->negative_evidence = UINT32_MAX;
    CHECK(lpres_add_evidence(&reg, 1, false) && lpres_get(&reg, 1)->negative_evidence == UINT32_MAX &&
              lpres_get(&reg, 1)->state == F,
          "negative evidence at the ceiling stays FALSE");
    lpres_get(&reg, 1)->negative_evidence = 0;
    lpres_get(&reg, 1)->positive_evidence = UINT32_MAX;
    CHECK(lpres_add_evidence(&reg, 1, true) && lpres_get(&reg, 1)->positive_evidence == UINT32_MAX &&
              lpres_get(&reg, 1)->state == T,
          "positive evidence at the ceiling stays TRUE");

    /* capacity */
    lpres_registry_init(&reg);
    for (uint32_t i = 0; i < LPRES_MAX_ATTESTATIONS; i++)
        CHECK(lpres_attest(&reg, "x", 0, T, "k", 0, 0) == (int32_t)i, "fill to 128");
    CHECK(lpres_attest(&reg, "x", 0, T, "k", 0, 0) == -1 && reg.count == LPRES_MAX_ATTESTATIONS,
          "the 129th is refused");
}

int main(void)
{
    printf("test_lpres_props: K3 presence logic and attestation registry\n");
    test_logic();
    test_registry_edges();
    test_registry_random();
    printf("%u checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_lpres_props\n");
        return 1;
    }
    printf("[PASS] test_lpres_props\n");
    return 0;
}

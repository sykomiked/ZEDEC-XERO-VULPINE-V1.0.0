/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_sovereign_node.c — known-answer tests for the sovereign node.
 *
 * Host build only (TEST_HOST): stdio/file IO live here, never in the module.
 * Anchors: (1) stable identity valid before any recognition; (2) is_sovereign
 * TRUE for every constituted node, with no un-make function in the source;
 * (3) manifest round-trips; (4) real recognition (good passes, forged fails);
 * (5) between-not-over — recognizing a peer leaves the peer byte-for-byte intact.
 */

#include <stdio.h>
#include <string.h>
#include "sovereign_node.h"
#include "ipfs.h"

static int g_asserts = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do {                                          \
    g_asserts++;                                                       \
    if (!(cond)) { g_fail++; printf("  FAIL: %s\n", msg); }            \
} while (0)

static bool is_zero32(const uint8_t *b) {
    for (int i = 0; i < 32; i++) if (b[i]) return false;
    return true;
}

/* ===== 1. Stable identity, valid BEFORE any recognition ===== */
static void test_identity_precedes_recognition(void) {
    printf("[identity precedes recognition]\n");

    uint8_t sk[32];   for (int i = 0; i < 32; i++) sk[i] = (uint8_t)(i + 1);
    const uint8_t charter[] = "I, node one, constitute myself sovereign.";

    sovereign_node_t a;
    int32_t st = sovereign_constitute(&a, 1u, charter, (uint32_t)sizeof(charter), sk);
    CHECK(st == ZXV_OK, "constitute returns ZXV_OK");

    /* The keyprint is the node's HONEST commitment to its declared signing key:
     * keyprint == ipfs(sk). Assert it is actually computed (not a dead field) so
     * the "this is the key I stand behind" claim is verifiable. */
    {
        uint8_t expect[ZXV_HASH_LEN];
        ipfs_cid_from_bytes(sk, 32u, expect);
        CHECK(memcmp(a.keyprint, expect, ZXV_HASH_LEN) == 0,
              "keyprint == ipfs(sk): the declared-key commitment is real, not dead");
    }

    /* Identity exists and is non-degenerate the instant the node constitutes —
     * no peer has recognized it yet. */
    CHECK(!is_zero32(a.ms.identity_cid), "identity_cid is populated at birth");
    CHECK(a.ms.recognized_mask == 0u, "no recognizer at birth");
    CHECK(sovereign_is_sovereign(&a), "sovereign with zero recognizers");

    /* Determinism: same self + charter (+ key) => byte-identical identity. */
    sovereign_node_t a2;
    sovereign_constitute(&a2, 1u, charter, (uint32_t)sizeof(charter), sk);
    CHECK(memcmp(a.ms.identity_cid, a2.ms.identity_cid, 32) == 0,
          "identity_cid is stable/deterministic");

    /* Ties to the REAL machinery: a bare microstate constituted the same way
     * yields the same identity_cid. */
    microstate_t bare;
    microstate_constitute(&bare, 1u, charter, (uint32_t)sizeof(charter));
    CHECK(memcmp(a.ms.identity_cid, bare.identity_cid, 32) == 0,
          "identity matches underlying microstate_constitute");

    /* Existence never depends on a recognizer count: after a peer recognizes A,
     * A's identity_cid is unchanged. */
    uint8_t id_before[32];
    memcpy(id_before, a.ms.identity_cid, 32);
    sovereign_node_t b;
    sovereign_constitute(&b, 2u, charter, (uint32_t)sizeof(charter), sk);
    (void)sovereign_federate(&b, &a);   /* B recognizes A */
    CHECK(memcmp(a.ms.identity_cid, id_before, 32) == 0,
          "identity unchanged whether or not recognized");
}

/* ===== 2. is_sovereign TRUE for every constituted node; no un-make in source == */
static void test_no_exception_path(void) {
    printf("[no exception path to sovereignty]\n");

    uint8_t sk[32];   for (int i = 0; i < 32; i++) sk[i] = (uint8_t)(0xA0 ^ i);
    const uint8_t charter[] = "charter";

    /* Every constituted node — across a spread of ids — is sovereign. No id,
     * no manifest, no recognizer state produces false. */
    for (uint32_t id = 0; id < 32u; id++) {
        sovereign_node_t n;
        int32_t st = sovereign_constitute(&n, id, charter, (uint32_t)sizeof(charter), sk);
        CHECK(st == ZXV_OK, "constitute ok across ids");
        CHECK(sovereign_is_sovereign(&n), "every constituted node is sovereign");
    }

    /* An UN-constituted node is the only non-sovereign — and only because it
     * never crowned itself, not because anyone un-made it. */
    sovereign_node_t empty;
    memset(&empty, 0, sizeof(empty));
    CHECK(!sovereign_is_sovereign(&empty), "un-constituted node is not sovereign");
    CHECK(!sovereign_is_sovereign(NULL), "NULL is not sovereign");

    /* Grep the module SOURCE: there is no revoke/downgrade/ban-of-a-node
     * function. If someone adds one, this test fails on purpose. */
    FILE *f = fopen("src/sovereign_node/sovereign_node.c", "rb");
    CHECK(f != NULL, "can open module source for inspection");
    if (f) {
        static char buf[65536];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = '\0';
        CHECK(strstr(buf, "revoke") == NULL, "source contains no 'revoke'");
        CHECK(strstr(buf, "downgrade") == NULL, "source contains no 'downgrade'");
        CHECK(strstr(buf, " ban") == NULL, "source contains no ' ban'");
        CHECK(strstr(buf, "expel") == NULL, "source contains no 'expel' impl");
    }
}

/* ===== 3. Capability manifest round-trips ===== */
static void test_manifest_roundtrip(void) {
    printf("[manifest round-trip]\n");

    uint8_t sk[32];   for (int i = 0; i < 32; i++) sk[i] = (uint8_t)i;
    const uint8_t charter[] = "cap node";
    sovereign_node_t n;
    sovereign_constitute(&n, 3u, charter, (uint32_t)sizeof(charter), sk);

    CHECK(sovereign_manifest(&n) == 0u, "manifest starts empty");

    sovereign_advertise_cap(&n, 0);
    sovereign_advertise_cap(&n, 3);
    sovereign_advertise_cap(&n, 17);
    sovereign_advertise_cap(&n, 31);
    uint32_t expect = (1u << 0) | (1u << 3) | (1u << 17) | (1u << 31);
    CHECK(sovereign_manifest(&n) == expect, "advertised bits read back exactly");

    /* Idempotent; out-of-range is a silent no-op (only 32 bits). */
    sovereign_advertise_cap(&n, 3);
    sovereign_advertise_cap(&n, 32);
    sovereign_advertise_cap(&n, 999);
    CHECK(sovereign_manifest(&n) == expect, "idempotent + out-of-range ignored");
}

/* ===== 4. Real recognition: good passes, forged fails ===== */
static void test_federate_real_recognition(void) {
    printf("[federate: real recognition, not theatre]\n");

    uint8_t sk[32];   for (int i = 0; i < 32; i++) sk[i] = (uint8_t)(i * 7 + 1);
    const uint8_t charter_a[] = "node A charter";
    const uint8_t charter_b[] = "node B charter";

    sovereign_node_t a, b;
    sovereign_constitute(&a, 1u, charter_a, (uint32_t)sizeof(charter_a), sk);
    sovereign_constitute(&b, 2u, charter_b, (uint32_t)sizeof(charter_b), sk);

    /* Valid peer -> real verification passes. */
    int32_t st = sovereign_federate(&a, &b);
    CHECK(st == ZXV_OK, "valid peer recognized (ZXV_OK)");
    CHECK(sovereign_recognizes(&a, 2u), "A now recognizes B");
    CHECK(!sovereign_recognizes(&b, 1u), "recognition is not automatically mutual");

    /* Forged writ -> ZXV_EBADSIG (recognition theatre prohibited). */
    sovereign_node_t bad_writ;
    sovereign_constitute(&bad_writ, 5u, charter_b, (uint32_t)sizeof(charter_b), sk);
    bad_writ.ms.writ_sig[0] ^= 0xFFu;   /* flip a writ byte */
    st = sovereign_federate(&a, &bad_writ);
    CHECK(st == ZXV_EBADSIG, "forged writ rejected (ZXV_EBADSIG)");
    CHECK(!sovereign_recognizes(&a, 5u), "forged peer not admitted");

    /* Substituted charter -> ZXV_ECHARTER (identity no longer recomputes). */
    sovereign_node_t bad_charter;
    sovereign_constitute(&bad_charter, 6u, charter_b, (uint32_t)sizeof(charter_b), sk);
    bad_charter.ms.charter_cid[0] ^= 0xFFu;   /* tamper the charter CID */
    st = sovereign_federate(&a, &bad_charter);
    CHECK(st == ZXV_ECHARTER, "substituted charter rejected (ZXV_ECHARTER)");
    CHECK(!sovereign_recognizes(&a, 6u), "tampered peer not admitted");

    /* Degenerate inputs. */
    CHECK(sovereign_federate(NULL, &b) == ZXV_EDEGEN, "NULL self -> EDEGEN");
    CHECK(sovereign_federate(&a, NULL) == ZXV_EDEGEN, "NULL peer -> EDEGEN");
}

/* ===== 5. Between, never over: the peer is left untouched ===== */
static void test_between_not_over(void) {
    printf("[between, not over]\n");

    uint8_t sk[32];   for (int i = 0; i < 32; i++) sk[i] = (uint8_t)(i + 100);
    const uint8_t charter_a[] = "sovereign A";
    const uint8_t charter_b[] = "sovereign B";

    sovereign_node_t a, b;
    sovereign_constitute(&a, 1u, charter_a, (uint32_t)sizeof(charter_a), sk);
    sovereign_constitute(&b, 2u, charter_b, (uint32_t)sizeof(charter_b), sk);

    /* B advertises a capability and recognizes some third party first, so it
     * carries real internal state we can prove is preserved. */
    sovereign_advertise_cap(&b, 4);

    sovereign_node_t peer_snapshot;
    memcpy(&peer_snapshot, &b, sizeof(b));   /* full byte snapshot BEFORE */

    int32_t st = sovereign_federate(&a, &b);   /* A recognizes B */
    CHECK(st == ZXV_OK, "A recognizes B");

    /* The peer is byte-for-byte identical: recognizing installs NO rule,
     * obligation, or recognized-bit in the peer. Only the recognizer changed. */
    CHECK(memcmp(&peer_snapshot, &b, sizeof(b)) == 0,
          "peer state entirely unchanged by being recognized");
    CHECK(b.ms.recognized_mask == 0u, "no recognition forced back into the peer");
    CHECK(sovereign_manifest(&b) == (1u << 4), "peer manifest untouched");
    CHECK(sovereign_is_sovereign(&b), "peer still sovereign (never in question)");

    /* And the recognizer DID change — exactly, and only, its own recognized set. */
    CHECK(sovereign_recognizes(&a, 2u), "recognizer gained the recognized bit");
}

int main(void) {
    printf("=== sovereign_node: every system a crown unto itself ===\n");
    test_identity_precedes_recognition();
    test_no_exception_path();
    test_manifest_roundtrip();
    test_federate_real_recognition();
    test_between_not_over();

    printf("\n%d assertions, %d failures\n", g_asserts, g_fail);
    if (g_fail) { printf("[FAIL] test_sovereign_node\n"); return 1; }
    printf("[PASS] test_sovereign_node\n");
    return 0;
}

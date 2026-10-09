/* test_update.c — decentralized, opt-in, content-addressed updates.
 *
 * The three guarantees, each with a test that fails if it regresses: nothing
 * installs unless the user opts in; fetched content must hash to the address it
 * was requested by; and an update from an untrusted key (or a bad signature)
 * is refused. Plus dependency ordering and bundles.
 */
#include <stdio.h>
#include <string.h>
#include "update.h"
#include "sha256.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

static bool seq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; } return *a == 0 && *b == 0;
}

/* a stub P2P transport: serves content out of a small in-memory store keyed by
 * CID, exactly as a content-addressed swarm would */
struct blob { uint8_t cid[32]; const uint8_t *data; uint32_t len; };
static struct blob g_store[8]; static uint32_t g_nblob = 0;
static void store_put(const uint8_t *data, uint32_t len, uint8_t cid_out[32]) {
    sha256(data, len, g_store[g_nblob].cid);
    g_store[g_nblob].data = data; g_store[g_nblob].len = len;
    memcpy(cid_out, g_store[g_nblob].cid, 32);
    g_nblob++;
}
static int stub_fetch(const uint8_t cid[32], uint8_t *buf, uint32_t cap, uint32_t *out, void *ctx) {
    (void)ctx;
    for (uint32_t i = 0; i < g_nblob; i++)
        if (memcmp(g_store[i].cid, cid, 32) == 0) {
            if (g_store[i].len > cap) return -1;
            memcpy(buf, g_store[i].data, g_store[i].len); *out = g_store[i].len; return 0;
        }
    return -1;   /* not in the swarm */
}
/* a transport that corrupts what it serves (a hostile/faulty peer) */
static int evil_fetch(const uint8_t cid[32], uint8_t *buf, uint32_t cap, uint32_t *out, void *ctx) {
    if (stub_fetch(cid, buf, cap, out, ctx) != 0) return -1;
    if (*out > 0) buf[0] ^= 0xFF;   /* hand back different bytes */
    return 0;
}
/* a verifier stub: accepts a signature whose first byte is 0x5A */
static bool verify_stub(const uint8_t *m, uint32_t n, const uint8_t sig[64], const uint8_t pk[32]) {
    (void)m;(void)n;(void)pk; return sig && sig[0] == 0x5A;
}

int main(void) {
    printf("=== decentralized opt-in content-addressed updates ===\n");
    static upd_catalog_t cat; upd_init(&cat);

    /* two publishers */
    uint8_t alice[32]; memset(alice, 0xA1, 32);
    uint8_t mallory[32]; memset(mallory, 0x3E, 32);
    uint8_t goodsig[64]; memset(goodsig, 0, 64); goodsig[0] = 0x5A;
    uint8_t badsig[64];  memset(badsig, 0, 64);

    /* content for three updates; CID = sha256(content) */
    static const uint8_t A[] = "update-A: the base library v2";
    static const uint8_t B[] = "update-B: a feature that needs A";
    static const uint8_t C[] = "update-C: an unrelated optional tool";
    uint8_t cidA[32], cidB[32], cidC[32];
    store_put(A, sizeof A-1, cidA);
    store_put(B, sizeof B-1, cidB);
    store_put(C, sizeof C-1, cidC);

    /* publish: B depends on A; all by Alice. C by Mallory (untrusted). */
    const char depsB[1][UPD_ID_LEN] = { "A" };
    CHECK(upd_publish(&cat, "A", "Base", 2, cidA, alice, goodsig, sizeof A-1, 0, 0) == UPD_OK,
          "Alice publishes A");
    CHECK(upd_publish(&cat, "B", "Feature", 1, cidB, alice, goodsig, sizeof B-1, depsB, 1) == UPD_OK,
          "Alice publishes B (depends on A)");
    CHECK(upd_publish(&cat, "C", "Tool", 1, cidC, mallory, goodsig, sizeof C-1, 0, 0) == UPD_OK,
          "Mallory publishes C");

    upd_set_transport(&cat, &(upd_transport_t){ stub_fetch, 0 });
    upd_set_verifier(&cat, verify_stub);
    upd_trust_author(&cat, alice);   /* the user trusts Alice, NOT Mallory */

    /* ---- GUARANTEE 1: nothing installs unless selected ---- */
    {
        int32_t plan[UPD_MAX]; uint32_t n;
        CHECK(upd_resolve(&cat, plan, UPD_MAX, &n) == UPD_OK && n == 0,
              "with nothing selected, the install plan is EMPTY — updates are "
              "opt-in, never applied because they merely exist");
    }

    /* ---- selecting B pulls in its dependency A, in order ---- */
    {
        upd_select(&cat, "B");
        int32_t plan[UPD_MAX]; uint32_t n;
        CHECK(upd_resolve(&cat, plan, UPD_MAX, &n) == UPD_OK && n == 2,
              "selecting B resolves two updates (B and its dependency A)");
        CHECK(seq(cat.upd[plan[0]].id, "A") && seq(cat.upd[plan[1]].id, "B"),
              "A is ordered BEFORE B (dependencies first)");
    }

    /* ---- GUARANTEE 2: self-certifying content ---- */
    {
        static uint8_t buf[256]; uint32_t got;
        CHECK(upd_fetch_verify(&cat, "A", buf, sizeof buf, &got) == UPD_OK &&
              got == sizeof A-1 && memcmp(buf, A, got) == 0,
              "A fetches and verifies — bytes match the content address");

        /* a hostile peer serving different bytes is caught by the CID */
        upd_set_transport(&cat, &(upd_transport_t){ evil_fetch, 0 });
        CHECK(upd_fetch_verify(&cat, "A", buf, sizeof buf, &got) == UPD_ERR_CID_MISMATCH,
              "a peer that serves DIFFERENT bytes than the address is REJECTED "
              "(content-addressing makes tampering self-evident)");
        upd_set_transport(&cat, &(upd_transport_t){ stub_fetch, 0 });   /* restore */
    }

    /* ---- GUARANTEE 3: trusted authorship ---- */
    {
        static uint8_t buf[256]; uint32_t got;
        upd_select(&cat, "C");
        CHECK(upd_fetch_verify(&cat, "C", buf, sizeof buf, &got) == UPD_ERR_UNTRUSTED,
              "C is signed and its content matches, but Mallory is UNTRUSTED -> "
              "refused. Decentralized publishing does not mean trusting everyone.");
        /* the user chooses to trust Mallory -> now it passes */
        upd_trust_author(&cat, mallory);
        CHECK(upd_fetch_verify(&cat, "C", buf, sizeof buf, &got) == UPD_OK,
              "once the USER trusts Mallory's key, C verifies");

        /* a bad signature is refused even from a trusted author */
        upd_publish(&cat, "D", "Bad", 1, cidC, alice, badsig, sizeof C-1, 0, 0);
        upd_select(&cat, "D");
        CHECK(upd_fetch_verify(&cat, "D", buf, sizeof buf, &got) == UPD_ERR_BAD_SIG,
              "an update with a BAD signature is refused even from a trusted key");
    }

    /* ---- no transport bound -> cannot fetch, never fabricates ---- */
    {
        upd_catalog_t c2; upd_init(&c2);
        upd_publish(&c2, "A", "Base", 2, cidA, alice, goodsig, sizeof A-1, 0, 0);
        upd_trust_author(&c2, alice); upd_set_verifier(&c2, verify_stub);
        static uint8_t buf[256]; uint32_t got;
        CHECK(upd_fetch_verify(&c2, "A", buf, sizeof buf, &got) == UPD_ERR_NO_TRANSPORT,
              "with no P2P backend bound, a fetch fails cleanly (no fabricated content)");
    }

    /* ---- bundles ---- */
    {
        upd_catalog_t c3; upd_init(&c3);
        upd_publish(&c3, "x", "X", 1, cidA, alice, goodsig, sizeof A-1, 0, 0);
        upd_publish(&c3, "y", "Y", 1, cidB, alice, goodsig, sizeof B-1, 0, 0);
        upd_publish(&c3, "z", "Z", 1, cidC, alice, goodsig, sizeof C-1, 0, 0);
        const char mem[2][UPD_ID_LEN] = { "x", "y" };
        upd_bundle_define(&c3, "starter", "Starter bundle", mem, 2);
        CHECK(upd_select_bundle(&c3, "starter"), "a bundle is selected as a set");
        CHECK(upd_is_selected(&c3, "x") && upd_is_selected(&c3, "y") && !upd_is_selected(&c3, "z"),
              "the bundle opts in exactly its members (x,y) — z stays opt-out");
        int32_t plan[UPD_MAX]; uint32_t n;
        upd_resolve(&c3, plan, UPD_MAX, &n);
        CHECK(n == 2, "the plan is exactly the two bundle members");
    }

    /* ---- missing dependency and cycle are reported, not silently applied ---- */
    {
        upd_catalog_t c4; upd_init(&c4);
        const char needM[1][UPD_ID_LEN] = { "missing" };
        upd_publish(&c4, "p", "P", 1, cidA, alice, goodsig, sizeof A-1, needM, 1);
        upd_select(&c4, "p");
        int32_t plan[UPD_MAX]; uint32_t n;
        CHECK(upd_resolve(&c4, plan, UPD_MAX, &n) == UPD_ERR_MISSING_DEP,
              "a selection whose dependency is absent -> MISSING_DEP, not a partial install");

        upd_catalog_t c5; upd_init(&c5);
        const char depQ[1][UPD_ID_LEN] = { "q2" };
        const char depQ2[1][UPD_ID_LEN] = { "q1" };
        upd_publish(&c5, "q1", "Q1", 1, cidA, alice, goodsig, sizeof A-1, depQ, 1);
        upd_publish(&c5, "q2", "Q2", 1, cidB, alice, goodsig, sizeof B-1, depQ2, 1);
        upd_select(&c5, "q1"); upd_select(&c5, "q2");
        CHECK(upd_resolve(&c5, plan, UPD_MAX, &n) == UPD_ERR_CYCLE,
              "a dependency cycle is detected, not looped on forever");
    }

    /* ---- installed updates are not re-planned ---- */
    {
        upd_deselect(&cat, "C"); upd_deselect(&cat, "D");
        static uint8_t buf[256]; uint32_t got;
        upd_fetch_verify(&cat, "A", buf, sizeof buf, &got);
        CHECK(upd_mark_installed(&cat, "A"), "A is marked installed after staging");
        int32_t plan[UPD_MAX]; uint32_t n;
        upd_resolve(&cat, plan, UPD_MAX, &n);
        bool has_A = false; for (uint32_t k=0;k<n;k++) if (seq(cat.upd[plan[k]].id,"A")) has_A=true;
        CHECK(!has_A, "an already-installed update is not planned again");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}

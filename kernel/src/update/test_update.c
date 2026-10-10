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
#include "ed25519_verify.h"
#include "../provenance/zx_provenance.h"
#include "../provenance/test_signer.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static bool seq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

/* a stub P2P transport: serves content out of a small in-memory store keyed by
 * CID, exactly as a content-addressed swarm would */
struct blob {
    uint8_t cid[32];
    const uint8_t *data;
    uint32_t len;
};
static struct blob g_store[8];
static uint32_t g_nblob = 0;
static void store_put(const uint8_t *data, uint32_t len, uint8_t cid_out[32])
{
    sha256(data, len, g_store[g_nblob].cid);
    g_store[g_nblob].data = data;
    g_store[g_nblob].len = len;
    memcpy(cid_out, g_store[g_nblob].cid, 32);
    g_nblob++;
}
static int stub_fetch(const uint8_t cid[32], uint8_t *buf, uint32_t cap, uint32_t *out, void *ctx)
{
    (void) ctx;
    for (uint32_t i = 0; i < g_nblob; i++)
        if (memcmp(g_store[i].cid, cid, 32) == 0) {
            if (g_store[i].len > cap) return -1;
            memcpy(buf, g_store[i].data, g_store[i].len);
            *out = g_store[i].len;
            return 0;
        }
    return -1; /* not in the swarm */
}
/* a transport that corrupts what it serves (a hostile/faulty peer) */
static int evil_fetch(const uint8_t cid[32], uint8_t *buf, uint32_t cap, uint32_t *out, void *ctx)
{
    if (stub_fetch(cid, buf, cap, out, ctx) != 0) return -1;
    if (*out > 0) buf[0] ^= 0xFF; /* hand back different bytes */
    return 0;
}
/* a broken transport that reports more bytes than the buffer can hold */
static int liar_fetch(const uint8_t cid[32], uint8_t *buf, uint32_t cap, uint32_t *out, void *ctx)
{
    (void) cid;
    (void) buf;
    (void) ctx;
    *out = cap + 1000u;
    return 0;
}
/* a verifier stub: accepts a signature whose first byte is 0x5A */
static bool verify_stub(const uint8_t *m, uint32_t n, const uint8_t sig[64], const uint8_t pk[32])
{
    (void) m;
    (void) n;
    (void) pk;
    return sig && sig[0] == 0x5A;
}

/* the real kernel verifier, bound the way a deployment binds it */
static bool verify_ed25519(const uint8_t *m, uint32_t n, const uint8_t sig[64],
                           const uint8_t pk[32])
{
    return ed25519_verify(m, (size_t) n, sig, pk);
}

/* What a publisher does: sign the canonical manifest digest of exactly the
 * fields it publishes. Computed here from zx_provenance.h directly, not via
 * update.c, so the test does not trust the code under test. */
static void publisher_sign(const test_signer_t *s, const char *id, uint32_t version,
                           const char *arch, const char deps[][UPD_ID_LEN], uint32_t n_deps,
                           const uint8_t cid[32], uint8_t sig[64])
{
    zxp_bytes_t d[UPD_MAX_DEPS];
    for (uint32_t i = 0; i < n_deps; i++) d[i] = zxp_str(deps[i]);
    zxp_manifest_t m = {zxp_str(id), version, zxp_str(arch), d, n_deps, zxp_mem(cid, 32)};
    uint8_t md[32];
    zxp_manifest_digest(ZXP_DOMAIN_UPDATE, &m, md);
    test_signer_sign(s, md, 32, sig);
}

/* Publish (id, version, arch, deps, cid) with `sig` into a fresh catalog that
 * trusts `s`, then fetch+verify it. */
static upd_result_t publish_and_verify(const test_signer_t *s, const char *id, uint32_t version,
                                       const char *arch, const char deps[][UPD_ID_LEN],
                                       uint32_t n_deps, const uint8_t cid[32], uint32_t size,
                                       const uint8_t sig[64])
{
    static upd_catalog_t c;
    static uint8_t buf[256];
    uint32_t got;
    upd_init(&c);
    upd_set_transport(&c, &(upd_transport_t){stub_fetch, 0});
    upd_set_verifier(&c, verify_ed25519);
    upd_trust_author(&c, s->pk);
    const char dep_dummy[1][UPD_ID_LEN] = {"_"};
    upd_result_t r = upd_publish(&c, id, id, version, arch, cid, s->pk, sig, size,
                                 n_deps ? deps : dep_dummy, n_deps);
    if (r != UPD_OK) return r;
    return upd_fetch_verify(&c, id, buf, sizeof buf, &got);
}

int main(void)
{
    printf("=== decentralized opt-in content-addressed updates ===\n");
    static upd_catalog_t cat;
    upd_init(&cat);

    /* two publishers */
    uint8_t alice[32];
    memset(alice, 0xA1, 32);
    uint8_t mallory[32];
    memset(mallory, 0x3E, 32);
    uint8_t goodsig[64];
    memset(goodsig, 0, 64);
    goodsig[0] = 0x5A;
    uint8_t badsig[64];
    memset(badsig, 0, 64);

    /* content for three updates; CID = sha256(content) */
    static const uint8_t A[] = "update-A: the base library v2";
    static const uint8_t B[] = "update-B: a feature that needs A";
    static const uint8_t C[] = "update-C: an unrelated optional tool";
    uint8_t cidA[32], cidB[32], cidC[32];
    store_put(A, sizeof A - 1, cidA);
    store_put(B, sizeof B - 1, cidB);
    store_put(C, sizeof C - 1, cidC);

    /* publish: B depends on A; all by Alice. C by Mallory (untrusted). */
    const char depsB[1][UPD_ID_LEN] = {"A"};
    CHECK(upd_publish(&cat, "A", "Base", 2, "any", cidA, alice, goodsig, sizeof A - 1, 0, 0) ==
              UPD_OK,
          "Alice publishes A");
    CHECK(upd_publish(&cat, "B", "Feature", 1, "any", cidB, alice, goodsig, sizeof B - 1, depsB,
                      1) == UPD_OK,
          "Alice publishes B (depends on A)");
    CHECK(upd_publish(&cat, "C", "Tool", 1, "any", cidC, mallory, goodsig, sizeof C - 1, 0, 0) ==
              UPD_OK,
          "Mallory publishes C");

    upd_set_transport(&cat, &(upd_transport_t){stub_fetch, 0});
    upd_set_verifier(&cat, verify_stub);
    upd_trust_author(&cat, alice); /* the user trusts Alice, NOT Mallory */

    /* ---- GUARANTEE 1: nothing installs unless selected ---- */
    {
        int32_t plan[UPD_MAX];
        uint32_t n;
        CHECK(upd_resolve(&cat, plan, UPD_MAX, &n) == UPD_OK && n == 0,
              "with nothing selected, the install plan is EMPTY — updates are "
              "opt-in, never applied because they merely exist");
    }

    /* ---- selecting B pulls in its dependency A, in order ---- */
    {
        upd_select(&cat, "B");
        int32_t plan[UPD_MAX];
        uint32_t n;
        CHECK(upd_resolve(&cat, plan, UPD_MAX, &n) == UPD_OK && n == 2,
              "selecting B resolves two updates (B and its dependency A)");
        CHECK(seq(cat.upd[plan[0]].id, "A") && seq(cat.upd[plan[1]].id, "B"),
              "A is ordered BEFORE B (dependencies first)");
    }

    /* ---- GUARANTEE 2: self-certifying content ---- */
    {
        static uint8_t buf[256];
        uint32_t got;
        CHECK(upd_fetch_verify(&cat, "A", buf, sizeof buf, &got) == UPD_OK && got == sizeof A - 1 &&
                  memcmp(buf, A, got) == 0,
              "A fetches and verifies — bytes match the content address");

        /* a hostile peer serving different bytes is caught by the CID */
        upd_set_transport(&cat, &(upd_transport_t){evil_fetch, 0});
        CHECK(upd_fetch_verify(&cat, "A", buf, sizeof buf, &got) == UPD_ERR_CID_MISMATCH,
              "a peer that serves DIFFERENT bytes than the address is REJECTED "
              "(content-addressing makes tampering self-evident)");
        upd_set_transport(&cat, &(upd_transport_t){stub_fetch, 0}); /* restore */
    }

    /* ---- GUARANTEE 3: trusted authorship ---- */
    {
        static uint8_t buf[256];
        uint32_t got;
        upd_select(&cat, "C");
        CHECK(upd_fetch_verify(&cat, "C", buf, sizeof buf, &got) == UPD_ERR_UNTRUSTED,
              "C is signed and its content matches, but Mallory is UNTRUSTED -> "
              "refused. Decentralized publishing does not mean trusting everyone.");
        /* the user chooses to trust Mallory -> now it passes */
        upd_trust_author(&cat, mallory);
        CHECK(upd_fetch_verify(&cat, "C", buf, sizeof buf, &got) == UPD_OK,
              "once the USER trusts Mallory's key, C verifies");

        /* a bad signature is refused even from a trusted author */
        upd_publish(&cat, "D", "Bad", 1, "any", cidC, alice, badsig, sizeof C - 1, 0, 0);
        upd_select(&cat, "D");
        CHECK(upd_fetch_verify(&cat, "D", buf, sizeof buf, &got) == UPD_ERR_BAD_SIG,
              "an update with a BAD signature is refused even from a trusted key");
    }

    /* ---- no transport bound -> cannot fetch, never fabricates ---- */
    {
        upd_catalog_t c2;
        upd_init(&c2);
        upd_publish(&c2, "A", "Base", 2, "any", cidA, alice, goodsig, sizeof A - 1, 0, 0);
        upd_trust_author(&c2, alice);
        upd_set_verifier(&c2, verify_stub);
        static uint8_t buf[256];
        uint32_t got;
        CHECK(upd_fetch_verify(&c2, "A", buf, sizeof buf, &got) == UPD_ERR_NO_TRANSPORT,
              "with no P2P backend bound, a fetch fails cleanly (no fabricated content)");
    }

    /* ---- bundles ---- */
    {
        upd_catalog_t c3;
        upd_init(&c3);
        upd_publish(&c3, "x", "X", 1, "any", cidA, alice, goodsig, sizeof A - 1, 0, 0);
        upd_publish(&c3, "y", "Y", 1, "any", cidB, alice, goodsig, sizeof B - 1, 0, 0);
        upd_publish(&c3, "z", "Z", 1, "any", cidC, alice, goodsig, sizeof C - 1, 0, 0);
        const char mem[2][UPD_ID_LEN] = {"x", "y"};
        upd_bundle_define(&c3, "starter", "Starter bundle", mem, 2);
        CHECK(upd_select_bundle(&c3, "starter"), "a bundle is selected as a set");
        CHECK(upd_is_selected(&c3, "x") && upd_is_selected(&c3, "y") && !upd_is_selected(&c3, "z"),
              "the bundle opts in exactly its members (x,y) — z stays opt-out");
        int32_t plan[UPD_MAX];
        uint32_t n;
        upd_resolve(&c3, plan, UPD_MAX, &n);
        CHECK(n == 2, "the plan is exactly the two bundle members");
    }

    /* ---- missing dependency and cycle are reported, not silently applied ---- */
    {
        upd_catalog_t c4;
        upd_init(&c4);
        const char needM[1][UPD_ID_LEN] = {"missing"};
        upd_publish(&c4, "p", "P", 1, "any", cidA, alice, goodsig, sizeof A - 1, needM, 1);
        upd_select(&c4, "p");
        int32_t plan[UPD_MAX];
        uint32_t n;
        CHECK(upd_resolve(&c4, plan, UPD_MAX, &n) == UPD_ERR_MISSING_DEP,
              "a selection whose dependency is absent -> MISSING_DEP, not a partial install");

        upd_catalog_t c5;
        upd_init(&c5);
        const char depQ[1][UPD_ID_LEN] = {"q2"};
        const char depQ2[1][UPD_ID_LEN] = {"q1"};
        upd_publish(&c5, "q1", "Q1", 1, "any", cidA, alice, goodsig, sizeof A - 1, depQ, 1);
        upd_publish(&c5, "q2", "Q2", 1, "any", cidB, alice, goodsig, sizeof B - 1, depQ2, 1);
        upd_select(&c5, "q1");
        upd_select(&c5, "q2");
        CHECK(upd_resolve(&c5, plan, UPD_MAX, &n) == UPD_ERR_CYCLE,
              "a dependency cycle is detected, not looped on forever");
    }

    /* ---- installed updates are not re-planned ---- */
    {
        upd_deselect(&cat, "C");
        upd_deselect(&cat, "D");
        static uint8_t buf[256];
        uint32_t got;
        upd_fetch_verify(&cat, "A", buf, sizeof buf, &got);
        CHECK(upd_mark_installed(&cat, "A"), "A is marked installed after staging");
        int32_t plan[UPD_MAX];
        uint32_t n;
        upd_resolve(&cat, plan, UPD_MAX, &n);
        bool has_A = false;
        for (uint32_t k = 0; k < n; k++)
            if (seq(cat.upd[plan[k]].id, "A")) has_A = true;
        CHECK(!has_A, "an already-installed update is not planned again");
    }

    /* ---- a re-publish with different content does not inherit consent ---- */
    {
        static upd_catalog_t c6;
        upd_init(&c6);
        upd_set_transport(&c6, &(upd_transport_t){stub_fetch, 0});
        upd_set_verifier(&c6, verify_stub);
        upd_trust_author(&c6, alice);
        upd_publish(&c6, "r", "R", 1, "any", cidA, alice, goodsig, sizeof A - 1, 0, 0);
        upd_select(&c6, "r");
        upd_publish(&c6, "r", "R", 1, "any", cidA, alice, goodsig, sizeof A - 1, 0, 0);
        CHECK(upd_is_selected(&c6, "r"), "re-publishing identical content keeps the opt-in");
        upd_publish(&c6, "r", "R", 2, "any", cidB, alice, goodsig, sizeof B - 1, 0, 0);
        CHECK(!upd_is_selected(&c6, "r"),
              "re-publishing an id with different content drops the opt-in");
        int32_t plan[UPD_MAX];
        uint32_t n = 99;
        CHECK(upd_resolve(&c6, plan, UPD_MAX, &n) == UPD_OK && n == 0,
              "...so the swapped content is not in the install plan");
    }

    /* ---- a transport that over-reports its length is refused ---- */
    {
        static upd_catalog_t c7;
        upd_init(&c7);
        upd_set_transport(&c7, &(upd_transport_t){liar_fetch, 0});
        upd_set_verifier(&c7, verify_stub);
        upd_trust_author(&c7, alice);
        upd_publish(&c7, "s", "S", 1, "any", cidA, alice, goodsig, sizeof A - 1, 0, 0);
        upd_select(&c7, "s");
        uint8_t small[8];
        uint32_t got = 0;
        CHECK(upd_fetch_verify(&c7, "s", small, sizeof small, &got) == UPD_ERR_FETCH,
              "a fetch reporting more bytes than the buffer holds is refused, not hashed");
    }

    /* ---- provenance: the signature covers the COMPLETE manifest ---- */
    {
        test_signer_t pub;
        test_signer_init(&pub, 0x51);
        static const uint8_t P[] = "update-P: a package with two dependencies";
        static const uint8_t P2[] = "update-P: different bytes, same everything else";
        uint8_t cidP[32], cidP2[32], sig[64];
        store_put(P, sizeof P - 1, cidP);
        store_put(P2, sizeof P2 - 1, cidP2);
        const char deps[2][UPD_ID_LEN] = {"libA", "libB"};
        publisher_sign(&pub, "pkg", 5, "aarch64", deps, 2, cidP, sig);

        CHECK(publish_and_verify(&pub, "pkg", 5, "aarch64", deps, 2, cidP, sizeof P - 1, sig) ==
                  UPD_OK,
              "provenance: a manifest signed over (id, version, arch, deps, cid) verifies");
        {
            upd_entry_t e;
            memset(&e, 0, sizeof e);
            strcpy(e.id, "pkg");
            e.version = 5;
            strcpy(e.arch, "aarch64");
            strcpy(e.dep[0], "libA");
            strcpy(e.dep[1], "libB");
            e.n_deps = 2;
            memcpy(e.cid, cidP, 32);
            uint8_t a[32], b[32];
            zxp_bytes_t d[2] = {zxp_str("libA"), zxp_str("libB")};
            zxp_manifest_t m = {zxp_str("pkg"), 5, zxp_str("aarch64"), d, 2, zxp_mem(cidP, 32)};
            CHECK(upd_manifest_digest(&e, a) && zxp_manifest_digest(ZXP_DOMAIN_UPDATE, &m, b) &&
                      memcmp(a, b, 32) == 0,
                  "provenance: update.c signs exactly the shared canonical digest");
        }
        CHECK(publish_and_verify(&pub, "pkg", 6, "aarch64", deps, 2, cidP, sizeof P - 1, sig) ==
                  UPD_ERR_BAD_SIG,
              "provenance: changing the VERSION breaks the signature");
        CHECK(publish_and_verify(&pub, "pkg", 5, "x86_64", deps, 2, cidP, sizeof P - 1, sig) ==
                  UPD_ERR_BAD_SIG,
              "provenance: changing the ARCHITECTURE breaks the signature");
        const char deps_sub[2][UPD_ID_LEN] = {"libA", "libEvil"};
        CHECK(publish_and_verify(&pub, "pkg", 5, "aarch64", deps_sub, 2, cidP, sizeof P - 1, sig) ==
                  UPD_ERR_BAD_SIG,
              "provenance: substituting a DEPENDENCY breaks the signature");
        CHECK(publish_and_verify(&pub, "pkg", 5, "aarch64", deps, 1, cidP, sizeof P - 1, sig) ==
                  UPD_ERR_BAD_SIG,
              "provenance: dropping a DEPENDENCY breaks the signature");
        const char deps_rev[2][UPD_ID_LEN] = {"libB", "libA"};
        CHECK(publish_and_verify(&pub, "pkg", 5, "aarch64", deps_rev, 2, cidP, sizeof P - 1, sig) ==
                  UPD_ERR_BAD_SIG,
              "provenance: reordering the DEPENDENCIES breaks the signature");
        CHECK(publish_and_verify(&pub, "pkg", 5, "aarch64", deps, 2, cidP2, sizeof P2 - 1, sig) ==
                  UPD_ERR_BAD_SIG,
              "provenance: changing the CONTENT CID breaks the signature");
        CHECK(publish_and_verify(&pub, "pkh", 5, "aarch64", deps, 2, cidP, sizeof P - 1, sig) ==
                  UPD_ERR_BAD_SIG,
              "provenance: changing the PACKAGE ID breaks the signature");
        /* field boundaries are length-prefixed: "libA"+"libB" is not "lib"+"AlibB" */
        const char deps_shift[2][UPD_ID_LEN] = {"lib", "AlibB"};
        CHECK(publish_and_verify(&pub, "pkg", 5, "aarch64", deps_shift, 2, cidP, sizeof P - 1,
                                 sig) == UPD_ERR_BAD_SIG,
              "provenance: moving bytes across a field boundary breaks the signature");
        /* the old format (signature over the CID alone) is no longer accepted */
        uint8_t cid_only[64];
        test_signer_sign(&pub, cidP, 32, cid_only);
        CHECK(publish_and_verify(&pub, "pkg", 5, "aarch64", deps, 2, cidP, sizeof P - 1,
                                 cid_only) == UPD_ERR_BAD_SIG,
              "provenance: a legacy signature over the CID alone is refused");
    }

    /* ---- versions are strictly monotonic per id ---- */
    {
        test_signer_t pub;
        test_signer_init(&pub, 0x52);
        static upd_catalog_t c8;
        upd_init(&c8);
        upd_trust_author(&c8, pub.pk);
        uint8_t s5[64], s4[64], s5b[64], s6[64];
        publisher_sign(&pub, "mono", 5, "any", 0, 0, cidA, s5);
        publisher_sign(&pub, "mono", 4, "any", 0, 0, cidB, s4);
        publisher_sign(&pub, "mono", 5, "any", 0, 0, cidB, s5b);
        publisher_sign(&pub, "mono", 6, "any", 0, 0, cidB, s6);
        CHECK(upd_publish(&c8, "mono", "M", 5, "any", cidA, pub.pk, s5, sizeof A - 1, 0, 0) ==
                  UPD_OK,
              "monotonic: version 5 published");
        CHECK(upd_publish(&c8, "mono", "M", 4, "any", cidB, pub.pk, s4, sizeof B - 1, 0, 0) ==
                  UPD_ERR_ROLLBACK,
              "monotonic: a validly signed OLDER version 4 is refused (UPD_ERR_ROLLBACK)");
        CHECK(upd_publish(&c8, "mono", "M", 5, "any", cidB, pub.pk, s5b, sizeof B - 1, 0, 0) ==
                  UPD_ERR_ROLLBACK,
              "monotonic: the SAME version with other content is refused");
        CHECK(c8.upd[upd_find(&c8, "mono")].version == 5 &&
                  memcmp(c8.upd[upd_find(&c8, "mono")].cid, cidA, 32) == 0,
              "monotonic: refused re-publishes left the entry unchanged");
        CHECK(upd_publish(&c8, "mono", "M", 5, "any", cidA, pub.pk, s5, sizeof A - 1, 0, 0) ==
                  UPD_OK,
              "monotonic: an identical re-publish is a refresh");
        CHECK(upd_publish(&c8, "mono", "M", 6, "any", cidB, pub.pk, s6, sizeof B - 1, 0, 0) ==
                  UPD_OK,
              "monotonic: a newer version 6 replaces it");
        char longid[UPD_ID_LEN + 4];
        memset(longid, 'x', sizeof longid - 1);
        longid[sizeof longid - 1] = 0;
        CHECK(upd_publish(&c8, longid, "L", 1, "any", cidA, pub.pk, s5, 1, 0, 0) ==
                  UPD_ERR_BAD_MANIFEST,
              "an id too long to store whole is refused, not truncated and signed as another");
        CHECK(upd_publish(&c8, "noarch", "N", 1, "", cidA, pub.pk, s5, 1, 0, 0) ==
                  UPD_ERR_BAD_MANIFEST,
              "an empty architecture is refused");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_broker.c — Mister Shanghai gets audited. Every number is asserted against
 * a known answer (RFC 5869 vectors, exact integer splits), never against our own
 * output. stdio here is TEST_HOST only. */

#include <stdio.h>
#include <string.h>
#include "broker.h"
#include "sha256.h"
#include "ed25519_verify.h"
#include "../provenance/test_signer.h"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (cond) {                                                                                \
            printf("  [ok]  %s\n", msg);                                                           \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("  [FAIL] %s\n", msg);                                                          \
        }                                                                                          \
    } while (0)

/* ---- hex helper ---- */
static uint32_t hx(const char *h, uint8_t *out)
{
    uint32_t n = 0;
    while (h[0] && h[1]) {
        int hi = (h[0] <= '9') ? h[0] - '0' : (h[0] | 0x20) - 'a' + 10;
        int lo = (h[1] <= '9') ? h[1] - '0' : (h[1] | 0x20) - 'a' + 10;
        out[n++] = (uint8_t) ((hi << 4) | lo);
        h += 2;
    }
    return n;
}
static bool eqhex(const uint8_t *b, const char *h)
{
    uint8_t tmp[128];
    uint32_t n = hx(h, tmp);
    return memcmp(b, tmp, n) == 0;
}

/* ---- verify hooks (ops boundary) ---- */
static bool mock_accept(const uint8_t *m, uint32_t l, const uint8_t s[64], const uint8_t pk[32])
{
    (void) m;
    (void) l;
    (void) s;
    (void) pk;
    return true; /* "valid" seller sig */
}
/* width adapter: ed25519_verify takes size_t; the hook takes uint32_t (same as
 * the update system's upd_verify_fn). This is exactly how a deployment binds it. */
static bool ed25519_hook(const uint8_t *m, uint32_t l, const uint8_t s[64], const uint8_t pk[32])
{
    return ed25519_verify(m, (size_t) l, s, pk);
}

/* ---- mock ipfs transports (ops boundary) ---- */
static uint8_t g_payload[64];
static uint32_t g_payload_len;
static int transport_honest(const uint8_t cid[32], uint8_t *buf, uint32_t cap, uint32_t *out_len,
                            void *ctx)
{
    (void) cid;
    (void) ctx;
    if (cap < g_payload_len) return -1;
    memcpy(buf, g_payload, g_payload_len);
    *out_len = g_payload_len;
    return 0; /* serves the advertised bytes */
}
static int transport_fraud(const uint8_t cid[32], uint8_t *buf, uint32_t cap, uint32_t *out_len,
                           void *ctx)
{
    (void) cid;
    (void) ctx;
    if (cap < g_payload_len) return -1;
    memcpy(buf, g_payload, g_payload_len);
    buf[0] ^= 0xFF; /* swap one byte — different content entirely */
    *out_len = g_payload_len;
    return 0;
}

/* ---- popcount over a byte buffer ---- */
static int popdiff(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    int d = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t x = a[i] ^ b[i];
        while (x) {
            d += x & 1;
            x >>= 1;
        }
    }
    return d;
}

/* Build a completed, ALLOWED capability proof bound to `cid`. */
static void grant_capability(aipi_handshake_t *hs, const uint8_t cid[32])
{
    uint8_t key[AIPI_TAG_LEN];
    memset(key, 0xA5, sizeof key);
    uint8_t nonce[AIPI_NONCE_LEN];
    memset(nonce, 0x33, sizeof nonce);
    uint8_t tag[AIPI_TAG_LEN];
    aipi_hs_init(hs, key);
    aipi_hs_begin(hs, cid);
    aipi_hs_challenge(hs, nonce, 42u);
    aipi_hs_prove(hs, tag);
    aipi_hs_allow(hs);
}

/* Settlement backends: one confirms, one declines. */
static int settle_confirm(const uint8_t buyer[32], const uint8_t seller[32], uint64_t a, void *c)
{
    (void) buyer;
    (void) seller;
    (void) a;
    (void) c;
    return 0; /* CONFIRMS the transfer */
}
static int settle_decline(const uint8_t buyer[32], const uint8_t seller[32], uint64_t a, void *c)
{
    (void) buyer;
    (void) seller;
    (void) a;
    (void) c;
    return -1; /* backend refuses */
}

int main(void)
{
    printf("=== broker: Mister Shanghai's Information Brokerage House ===\n");

    /* (1) aipic_matches — 5/5 glob-prefix cases -------------------------- */
    printf("[1] aipic_matches glob-prefix (5 cases)\n");
    {
        aipic_contract_t any = {0};
        any.function = 7;
        strcpy(any.scope, "*");
        CHECK(aipic_matches(&any, 7, "literally:anything"), "\"*\" matches anything");

        aipic_contract_t pre = {0};
        pre.function = 7;
        strcpy(pre.scope, "prefix:*");
        CHECK(aipic_matches(&pre, 7, "prefix:foo"), "\"prefix:*\" matches \"prefix:foo\"");
        CHECK(!aipic_matches(&pre, 7, "other:foo"), "\"prefix:*\" rejects \"other:foo\"");

        aipic_contract_t ex = {0};
        ex.function = 7;
        strcpy(ex.scope, "sigil:card:42");
        CHECK(aipic_matches(&ex, 7, "sigil:card:42"), "exact match");
        CHECK(!aipic_matches(&ex, 7, "sigil:card:43"), "exact mismatch rejected");
        CHECK(!aipic_matches(&ex, 9, "sigil:card:42"), "function mismatch rejected");
    }

    /* (2) HKDF-SHA256 RFC 5869 anchor + avalanche ----------------------- */
    printf("[2] HKDF key derivation (RFC 5869 case 1) + avalanche\n");
    {
        uint8_t ikm[32], salt[32], info[32], prk[32];
        uint32_t ikn = hx("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", ikm);
        uint32_t sn = hx("000102030405060708090a0b0c", salt);
        uint32_t in = hx("f0f1f2f3f4f5f6f7f8f9", info);

        /* external anchor: the published PRK */
        hkdf_extract(salt, sn, ikm, ikn, prk);
        CHECK(eqhex(prk, "077709362c2e32df0ddc3f0dc47bba63"
                         "90b6c73bb50f9c3122ec844ad7c2b3e5"),
              "RFC 5869 case 1 PRK matches published vector");

        /* external anchor: aipi_derive_key == first 32 bytes of the RFC OKM */
        uint8_t k1[32];
        CHECK(aipi_derive_key(salt, sn, ikm, ikn, info, in, k1) == 0, "aipi_derive_key ok");
        CHECK(eqhex(k1, "3cb25f25faacd57a90434f64d0362f2a"
                        "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"),
              "derived key == RFC 5869 case 1 OKM[0..31]");

        /* avalanche: flip ONE bit of the IKM, re-derive, expect ~128/256 diff */
        uint8_t ikm2[32];
        memcpy(ikm2, ikm, ikn);
        ikm2[0] ^= 0x01;
        uint8_t k2[32];
        aipi_derive_key(salt, sn, ikm2, ikn, info, in, k2);
        int d = popdiff(k1, k2, 32);
        printf("       avalanche = %d / 256 bits\n", d);
        CHECK(d >= 96 && d <= 160, "1-bit input flip => ~128/256 output bits change");
    }

    /* (3) tribute_split 11/11/11/66/1 — exact, sums to the whole --------- */
    printf("[3] tribute_split 11/11/11/66/1\n");
    {
        const uint8_t w[5] = {11, 11, 11, 66, 1};
        uint64_t out[5];
        tribute_split(1000000u, w, out);
        /* 1,000,000 * {11,11,11,66,1}/100 = 110000,110000,110000,660000,10000 */
        CHECK(out[0] == 110000 && out[1] == 110000 && out[2] == 110000 && out[3] == 660000 &&
                  out[4] == 10000,
              "clean split exact");
        uint64_t sum = out[0] + out[1] + out[2] + out[3] + out[4];
        CHECK(sum == 1000000u, "clean split sums to the whole");

        /* an amount that does NOT divide evenly must still sum EXACTLY */
        tribute_split(1001u, w, out);
        uint64_t s2 = out[0] + out[1] + out[2] + out[3] + out[4];
        printf("       1001 -> %llu %llu %llu %llu %llu\n", (unsigned long long) out[0],
               (unsigned long long) out[1], (unsigned long long) out[2],
               (unsigned long long) out[3], (unsigned long long) out[4]);
        CHECK(s2 == 1001u, "indivisible amount still sums to the whole (no unit lost/invented)");
    }

    /* (4) forged / zero PROVE tag rejected in constant time ------------- */
    printf("[4] handshake: constant-time keyed transcript MAC\n");
    {
        uint8_t key[32];
        uint8_t sec[16];
        memset(sec, 0xA5, sizeof sec);
        aipi_derive_key(0, 0, sec, sizeof sec, (const uint8_t *) "aipi cap key", 12, key);

        uint8_t cid[32];
        sha256((const uint8_t *) "widget.wasm", 11, cid);
        uint8_t nonce[24];
        memset(nonce, 0x5C, sizeof nonce);

        /* honest path */
        aipi_handshake_t hs;
        aipi_hs_init(&hs, key);
        CHECK(aipi_hs_begin(&hs, cid) == 0, "HELLO ok");
        CHECK(aipi_hs_challenge(&hs, nonce, 42u) == 0, "CHALLENGE ok (beacon phase 42)");
        uint8_t tag[32];
        CHECK(aipi_hs_prove(&hs, tag) == 0, "PROVE ok");
        CHECK(aipi_hs_allow(&hs) == BROKER_OK, "ALLOW grants on a valid keyed MAC");

        /* all-zero forged tag -> reject */
        aipi_handshake_t hz;
        aipi_hs_init(&hz, key);
        aipi_hs_begin(&hz, cid);
        aipi_hs_challenge(&hz, nonce, 42u);
        uint8_t t2[32];
        aipi_hs_prove(&hz, t2);
        memset(hz.presented_tag, 0, 32); /* attacker submits a zero tag */
        CHECK(aipi_hs_allow(&hz) == BROKER_ERR_HS_MAC, "all-zero PROVE tag REJECTED");

        /* single-bit tamper -> reject */
        aipi_handshake_t ht;
        aipi_hs_init(&ht, key);
        aipi_hs_begin(&ht, cid);
        aipi_hs_challenge(&ht, nonce, 42u);
        uint8_t t3[32];
        aipi_hs_prove(&ht, t3);
        ht.presented_tag[7] ^= 0x02; /* flip one bit of the proof */
        CHECK(aipi_hs_allow(&ht) == BROKER_ERR_HS_MAC, "tampered PROVE tag REJECTED");

        /* wrong key (forgery attempt) -> reject */
        uint8_t wrongkey[32];
        memcpy(wrongkey, key, 32);
        wrongkey[0] ^= 0xFF;
        aipi_handshake_t hw;
        aipi_hs_init(&hw, wrongkey);
        aipi_hs_begin(&hw, cid);
        aipi_hs_challenge(&hw, nonce, 42u);
        memcpy(hw.presented_tag, tag, 32);
        hw.stage = AIPI_HS_PROVE; /* present honest tag under wrong key */
        CHECK(aipi_hs_allow(&hw) == BROKER_ERR_HS_MAC, "proof forged under wrong key REJECTED");

        /* out-of-order -> state error */
        aipi_handshake_t ho;
        aipi_hs_init(&ho, key);
        CHECK(aipi_hs_prove(&ho, tag) < 0, "PROVE before CHALLENGE refused");
    }

    /* (5) weak-hash (MD5/SHA-1) CID length HARD-ERRORED ----------------- */
    printf("[5] listing refuses MD5(16)/SHA-1(20) CID lengths\n");
    {
        broker_t b;
        broker_init(&b);
        broker_set_verifier(&b, mock_accept);
        uint8_t author[32];
        memset(author, 0x11, 32);
        broker_trust_author(&b, author);
        uint8_t sig[64];
        memset(sig, 0x22, 64);
        aipic_contract_t terms = {0};
        terms.function = 1;
        strcpy(terms.scope, "*");
        uint8_t weak[32];
        memset(weak, 0x33, 32);

        CHECK(broker_list_bytes(&b, weak, 16, author, sig, &terms) == -(int32_t) BROKER_ERR_BAD_CID,
              "16-byte (MD5) CID hard-errors");
        CHECK(broker_list_bytes(&b, weak, 20, author, sig, &terms) == -(int32_t) BROKER_ERR_BAD_CID,
              "20-byte (SHA-1) CID hard-errors");
        CHECK(broker_list_bytes(&b, weak, 32, author, sig, &terms) >= 0,
              "32-byte (SHA-256) CID accepted");

        /* untrusted author refused; unbound verifier fails closed */
        broker_t b2;
        broker_init(&b2);
        broker_set_verifier(&b2, mock_accept);
        uint8_t stranger[32];
        memset(stranger, 0x44, 32);
        CHECK(broker_list_bytes(&b2, weak, 32, stranger, sig, &terms) ==
                  -(int32_t) BROKER_ERR_UNTRUSTED,
              "untrusted author refused");
        broker_t b3;
        broker_init(&b3); /* no verifier bound */
        broker_trust_author(&b3, author);
        CHECK(broker_list_bytes(&b3, weak, 32, author, sig, &terms) ==
                  -(int32_t) BROKER_ERR_NO_VERIFY,
              "no verify hook => fail closed");

        /* real ed25519 primitive rejects a garbage signature */
        broker_t b4;
        broker_init(&b4);
        broker_set_verifier(&b4, ed25519_hook);
        broker_trust_author(&b4, author);
        CHECK(broker_list_bytes(&b4, weak, 32, author, sig, &terms) ==
                  -(int32_t) BROKER_ERR_BAD_SIG,
              "real ed25519_verify rejects garbage sig");
    }

    /* (6) fraud-proof delivery via mock ipfs transport ------------------ */
    printf("[6] delivery: advertised CID or nothing\n");
    {
        broker_t b;
        broker_init(&b);
        broker_set_verifier(&b, mock_accept);
        uint8_t author[32];
        memset(author, 0x11, 32);
        broker_trust_author(&b, author);
        uint8_t sig[64];
        memset(sig, 0x22, 64);

        /* the product: bytes and their true CID */
        const char *msg = "the sigil that opens the second gate";
        g_payload_len = (uint32_t) strlen(msg);
        memcpy(g_payload, msg, g_payload_len);
        uint8_t cid[32];
        ipfs_cid_from_bytes(g_payload, g_payload_len, cid);

        aipic_contract_t terms = {0};
        terms.function = 1;
        strcpy(terms.scope, "sigil:*");
        int32_t idx = broker_list(&b, cid, author, sig, &terms);
        CHECK(idx >= 0, "product listed by its content CID");

        uint8_t buf[128];
        uint32_t got = 0;
        ipfs_transport_t th = {transport_honest, 0};

        /* a completed, allowed capability proof bound to THIS product's CID */
        aipi_handshake_t cap;
        grant_capability(&cap, cid);

        /* NO capability proof => fail closed BEFORE any fetch (an un-allowed hs) */
        aipi_handshake_t noc;
        {
            uint8_t k[AIPI_TAG_LEN];
            memset(k, 0xA5, sizeof k);
            aipi_hs_init(&noc, k);
            aipi_hs_begin(&noc, cid);
        }
        ipfs_node_t nX;
        ipfs_node_init(&nX);
        ipfs_set_transport(&nX, &th);
        CHECK(broker_deliver(&b, idx, &noc, 1, "sigil:card:42", &nX, buf, sizeof buf, &got) ==
                  BROKER_ERR_HS_MAC,
              "delivery WITHOUT a capability proof fails closed");
        /* a valid proof but an unauthorised scope => SCOPE refusal */
        CHECK(broker_deliver(&b, idx, &cap, 1, "weapon:*", &nX, buf, sizeof buf, &got) ==
                  BROKER_ERR_SCOPE,
              "a proof for an unauthorised scope is refused");

        /* authorised, but no transport bound => fail closed, no bytes invented */
        ipfs_node_t n0;
        ipfs_node_init(&n0);
        CHECK(broker_deliver(&b, idx, &cap, 1, "sigil:card:42", &n0, buf, sizeof buf, &got) ==
                  BROKER_ERR_NO_TRANSPORT,
              "unbound transport fails closed");

        /* authorised + honest transport => the advertised bytes are delivered */
        ipfs_node_t n1;
        ipfs_node_init(&n1);
        ipfs_set_transport(&n1, &th);
        CHECK(broker_deliver(&b, idx, &cap, 1, "sigil:card:42", &n1, buf, sizeof buf, &got) ==
                  BROKER_OK,
              "authorised buyer + honest transport delivers");
        CHECK(got == g_payload_len && memcmp(buf, g_payload, got) == 0,
              "delivered bytes == advertised product");

        /* fraud transport => the ipfs CID gate rejects; buyer gets nothing */
        ipfs_node_t n2;
        ipfs_node_init(&n2);
        ipfs_transport_t tf = {transport_fraud, 0};
        ipfs_set_transport(&n2, &tf);
        CHECK(broker_deliver(&b, idx, &cap, 1, "sigil:card:42", &n2, buf, sizeof buf, &got) ==
                  BROKER_ERR_CID_MISMATCH,
              "transport serving different bytes is REJECTED (fraud-proof)");
    }

    /* (7) settlement is never a simulated fill -------------------------- */
    printf("[7] settlement ops boundary (never simulated)\n");
    {
        broker_t b;
        broker_init(&b);
        broker_set_verifier(&b, mock_accept);
        uint8_t author[32];
        memset(author, 0x11, 32);
        broker_trust_author(&b, author);
        uint8_t sig[64];
        memset(sig, 0x22, 64);
        uint8_t cid[32];
        sha256((const uint8_t *) "dataset.parquet", 15, cid);
        aipic_contract_t terms = {0};
        terms.function = 2;
        strcpy(terms.scope, "*");
        int32_t idx = broker_list(&b, cid, author, sig, &terms);
        uint8_t buyer[32];
        memset(buyer, 0x55, 32);

        CHECK(broker_settle(&b, idx, buyer, 500u) == BROKER_ERR_NO_SETTLEMENT,
              "no settlement backend => NO fill");

        /* a bound backend that DECLINES => still NO fill (never simulated) */
        broker_settlement_t decl = {settle_decline, 0};
        broker_set_settlement(&b, &decl);
        CHECK(broker_settle(&b, idx, buyer, 500u) == BROKER_ERR_NO_SETTLEMENT,
              "backend declines => NO fill");

        /* a bound backend that CONFIRMS => the fill is recorded */
        broker_settlement_t conf = {settle_confirm, 0};
        broker_set_settlement(&b, &conf);
        CHECK(broker_settle(&b, idx, buyer, 500u) == BROKER_OK,
              "backend confirms => fill recorded (settlement is real, not simulated)");
    }

    /* (8) the seller signature covers the CID AND the terms --------------- */
    printf("[8] listing provenance: signature over cid + function + scope + price\n");
    {
        test_signer_t seller;
        test_signer_init(&seller, 0x61);
        uint8_t cid[32], cid2[32], ld[32], sig[64];
        sha256((const uint8_t *) "sigil-pack-7", 12, cid);
        sha256((const uint8_t *) "sigil-pack-8", 12, cid2);
        aipic_contract_t terms = {0};
        terms.function = 3;
        strcpy(terms.scope, "sigil:card:*");
        terms.max_price = 900;
        CHECK(broker_listing_digest(cid, &terms, ld), "listing digest computed");
        test_signer_sign(&seller, ld, 32, sig);

        broker_t b;
        broker_init(&b);
        broker_set_verifier(&b, ed25519_hook);
        broker_trust_author(&b, seller.pk);
        CHECK(broker_list(&b, cid, seller.pk, sig, &terms) >= 0,
              "a listing signed over its CID and terms is accepted");

        aipic_contract_t t2 = terms;
        strcpy(t2.scope, "*");
        CHECK(broker_list(&b, cid, seller.pk, sig, &t2) == -(int32_t) BROKER_ERR_BAD_SIG,
              "a relay that WIDENS the scope breaks the signature");
        t2 = terms;
        t2.function = 4;
        CHECK(broker_list(&b, cid, seller.pk, sig, &t2) == -(int32_t) BROKER_ERR_BAD_SIG,
              "changing the capability function breaks the signature");
        t2 = terms;
        t2.max_price = 901;
        CHECK(broker_list(&b, cid, seller.pk, sig, &t2) == -(int32_t) BROKER_ERR_BAD_SIG,
              "raising the price ceiling breaks the signature");
        CHECK(broker_list(&b, cid2, seller.pk, sig, &terms) == -(int32_t) BROKER_ERR_BAD_SIG,
              "the same signed terms on another CID are refused");
        uint8_t cid_only[64];
        test_signer_sign(&seller, cid, 32, cid_only);
        CHECK(broker_list(&b, cid, seller.pk, cid_only, &terms) == -(int32_t) BROKER_ERR_BAD_SIG,
              "a legacy signature over the CID alone is refused");
        t2 = terms;
        memset(t2.scope, 'a', AIPIC_SCOPE_LEN);
        CHECK(broker_list(&b, cid, seller.pk, sig, &t2) == -(int32_t) BROKER_ERR_SCOPE,
              "a scope with no terminator inside its field is refused");

        int32_t idx = broker_list(&b, cid, seller.pk, sig, &terms);
        broker_settlement_t conf = {settle_confirm, 0};
        broker_set_settlement(&b, &conf);
        uint8_t buyer[32];
        memset(buyer, 0x56, 32);
        CHECK(broker_settle(&b, idx, buyer, 901u) == BROKER_ERR_SCOPE,
              "settling above the signed max_price is refused");
        CHECK(broker_settle(&b, idx, buyer, 900u) == BROKER_OK,
              "settling at the signed max_price is confirmed");
    }
    printf("\n=== %d checks, %d failures ===\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}

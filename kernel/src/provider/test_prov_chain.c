/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_prov_chain.c — chain adapters against published vectors (EIP-712
 * "Ether Mail", BIP-173, RFC 8032, well-known secp256k1 addresses, the
 * protobuf wire format) and cross-implementation vectors from
 * gen_prov_chain_vectors.py (Python `cryptography`, independent receipt
 * encoder). Also: Merkle anchors, anchor payloads, asset conversion, and
 * the external-signer families. */
#include <stdio.h>
#include <string.h>
#include "prov_chain.h"
#include "../web4/web4_web3.h"
#include "../robin_debanks/ed25519_verify.h"
#include "test_prov_chain_vectors.h"

static int g_pass, g_fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
            printf("[PASS] %s\n", msg);                                                            \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)

static void hex(const char *s, uint8_t *out, uint32_t n)
{
    w4_hex_decode(s, 2 * n, out, n);
}

/* The fixture receipt mirrored by gen_prov_chain_vectors.py. */
static void fixture(prov_receipt_t *r)
{
    memset(r, 0, sizeof *r);
    r->version = 1;
    memset(r->job_id, 1, 32);
    memset(r->provider_id, 2, 32);
    memset(r->user_id, 3, 32);
    memset(r->desc_hash, 4, 32);
    memset(r->fee_schedule, 5, 32);
    memset(r->request_hash, 6, 32);
    memset(r->response_hash, 7, 32);
    r->rclass = PROV_RC_INFERENCE;
    r->unit = PROV_UNIT_TOKEN;
    r->flags = PROV_RF_NO_TRAIN;
    strcpy(r->asset_code, "VFV");
    r->units = 700;
    r->unit_price = 10;
    r->gross = 7000;
    r->fee = 113;
    r->net = 6887;
    r->refund = 3000;
    r->hold = 10000;
    r->latency_ms = 120;
    r->start_tick = 100;
    r->end_tick = 160;
}

static void t_eip712(void)
{
    /* EIP-712 specification example ("Ether Mail"). */
    uint8_t contract[20], dom[32], want[32], th_mail[32], th_person[32], w[3][32], from[32], to[32],
        sh[32], dg[32];
    hex("CcCCccccCCCCcCCCCCCcCcCccCcCCCcCcccccccC", contract, 20);
    prov_eip712_domain("Ether Mail", "1", 1, contract, dom);
    hex("f2cee375fa42b42143804025fc449deafd50cc031ca257e0b194a650a912090f", want, 32);
    CHECK(memcmp(dom, want, 32) == 0, "EIP-712 spec: domain separator");
    prov_eip712_type_hash("Mail(Person from,Person to,string contents)Person(string name,address "
                          "wallet)",
                          th_mail);
    prov_eip712_type_hash("Person(string name,address wallet)", th_person);
    w4_keccak256((const uint8_t *) "Cow", 3, w[0]);
    memset(w[1], 0, 12);
    hex("CD2a3d9F938E13CD947Ec05AbC7FE734Df8DD826", w[1] + 12, 20);
    prov_eip712_hash_struct(th_person, (const uint8_t(*)[32]) w, 2, from);
    w4_keccak256((const uint8_t *) "Bob", 3, w[0]);
    memset(w[1], 0, 12);
    hex("bBbBBBBbbBBBbbbBbbBbbbbBBbBbbbbBbBbbBBbB", w[1] + 12, 20);
    prov_eip712_hash_struct(th_person, (const uint8_t(*)[32]) w, 2, to);
    memcpy(w[0], from, 32);
    memcpy(w[1], to, 32);
    w4_keccak256((const uint8_t *) "Hello, Bob!", 11, w[2]);
    prov_eip712_hash_struct(th_mail, (const uint8_t(*)[32]) w, 3, sh);
    hex("c52c0ee5d84264471806290a3f2c4cecfc5490626bf912d01f240d7a274b371e", want, 32);
    CHECK(memcmp(sh, want, 32) == 0, "EIP-712 spec: hashStruct(message)");
    prov_eip712_digest(dom, sh, dg);
    hex("be609aee343fb3c4b28e1df9e632fca64fcfaede20f02e86244efddf30957bd2", want, 32);
    CHECK(memcmp(dg, want, 32) == 0, "EIP-712 spec: signing digest");
    /* The spec's signature (v = 28) by keccak256("cow") recovers the sender. */
    w4_ecdsa_sig_t s;
    uint8_t pub[64], addr[20], expect[20], sk[32];
    hex("4355c47d63924e8a72e509b65029052eb6c299d53a04e167c5775fd466751c9d", s.r, 32);
    hex("07299936d304c153f6443dfa05f40ff007d72911b6f72307f996231605b91562", s.s, 32);
    s.recid = 1;
    hex("CD2a3d9F938E13CD947Ec05AbC7FE734Df8DD826", expect, 20);
    CHECK(w4_secp_recover(dg, &s, pub) == W4_OK && (w4_eth_address(pub, addr), 1) &&
              memcmp(addr, expect, 20) == 0,
          "EIP-712 spec: signature recovers the 'Cow' address");
    w4_keccak256((const uint8_t *) "cow", 3, sk);
    uint8_t pub2[64];
    w4_secp_pubkey(sk, pub2);
    CHECK(memcmp(pub, pub2, 64) == 0, "EIP-712 spec: key keccak256(\"cow\") is that signer");
}

static void t_evm(void)
{
    prov_chain_t c;
    prov_receipt_t r;
    memset(&c, 0, sizeof c);
    c.family = PROV_CHAIN_EVM;
    c.evm_chain_id = 1;
    fixture(&r);
    uint8_t sk[32], pub[64], addr[20], h[32], sig[65];
    memset(sk, 0, 32);
    sk[31] = 1;
    w4_secp_pubkey(sk, pub);
    char a[64];
    CHECK(prov_chain_address(&c, pub, 64, a, sizeof a) == 42 &&
              strcmp(a, "0x7E5F4552091A69125d5DfCb7b8C2659029395Bdf") == 0,
          "EVM address of secret key 1 (well-known), EIP-55 checksummed");
    CHECK(prov_chain_address_parse(&c, a, 42, addr, 20) == 20, "EVM address parses back");
    CHECK(prov_chain_address_parse(&c, "0x7e5F4552091A69125d5DfCb7b8C2659029395Bdf", 42, addr,
                                   20) == PROV_ERR_PARSE,
          "EVM: wrong EIP-55 case refused");
    /* receipt signed as typed data */
    w4_ecdsa_sig_t s;
    prov_eip712_receipt(&c, &r, h);
    w4_secp_sign(sk, h, &s);
    memcpy(sig, s.r, 32);
    memcpy(sig + 32, s.s, 32);
    sig[64] = (uint8_t) (27 + s.recid);
    w4_eth_address(pub, addr);
    CHECK(prov_chain_verify_receipt(&c, &r, addr, 20, sig, 65) == PROV_OK,
          "EVM: EIP-712 receipt signature binds the payout address");
    r.units++;
    CHECK(prov_chain_verify_receipt(&c, &r, addr, 20, sig, 65) == PROV_ERR_AUTH,
          "EVM: tampered receipt refused");
    r.units--;
    prov_chain_t c2 = c;
    c2.evm_chain_id = 10;
    CHECK(prov_chain_verify_receipt(&c2, &r, addr, 20, sig, 65) == PROV_ERR_AUTH,
          "EVM: EIP-155 chain id is in the domain (no cross-chain replay)");
    /* high-s form refused */
    uint8_t n_be[32];
    hex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141", n_be, 32);
    w4_u256 n, sv, hs;
    w4_u256_from_be(&n, n_be, 32);
    w4_u256_from_be(&sv, s.s, 32);
    w4_u256_sub(&hs, &n, &sv);
    uint8_t sig2[65];
    memcpy(sig2, sig, 65);
    w4_u256_to_be(&hs, sig2 + 32);
    sig2[64] = (uint8_t) (27 + (s.recid ^ 1));
    CHECK(prov_chain_verify_receipt(&c, &r, addr, 20, sig2, 65) == PROV_ERR_AUTH,
          "EVM: malleable high-s signature refused");
    /* anchor */
    uint8_t root[32], pl[64], sel[4];
    memset(root, 0x5A, 32);
    w4_abi_selector("transfer(address,uint256)", sel);
    CHECK(sel[0] == 0xa9 && sel[1] == 0x05 && sel[2] == 0x9c && sel[3] == 0xbb,
          "ABI selector primitive (ERC-20 transfer = a9059cbb)");
    w4_abi_selector("anchor(bytes32)", sel);
    CHECK(prov_chain_anchor_payload(&c, root, pl, sizeof pl) == 36 && memcmp(pl, sel, 4) == 0 &&
              memcmp(pl + 4, root, 32) == 0,
          "EVM anchor calldata = anchor(bytes32) selector || root");
    uint8_t tx[256], cad[20];
    memset(cad, 0x11, 20);
    int32_t tl =
        prov_evm_anchor_tx(&c, 7, 60000, 1000000000, 30000000000ull, cad, root, tx, sizeof tx);
    w4_eth_tx_t d;
    w4_ecdsa_sig_t ds;
    uint8_t signed_tx[300];
    w4_eth_tx_t t0;
    memset(&t0, 0, sizeof t0);
    CHECK(tl > 0 && tx[0] == 0x02, "EVM anchor: EIP-1559 typed transaction payload");
    /* sign it with the in-module signer and decode it back */
    uint8_t th[32];
    w4_eth_signing_hash(tx, (uint32_t) tl, th);
    w4_secp_sign(sk, th, &ds);
    t0.chain_id = 1;
    t0.nonce = 7;
    w4_u256_from_u64(&t0.max_priority, 1000000000);
    w4_u256_from_u64(&t0.max_fee, 30000000000ull);
    t0.gas_limit = 60000;
    t0.has_to = true;
    memcpy(t0.to, cad, 20);
    t0.data = pl;
    t0.data_len = 36;
    int32_t sl = w4_eth_1559_signed(&t0, &ds, signed_tx, sizeof signed_tx);
    CHECK(sl > 0 && w4_eth_1559_decode(signed_tx, (uint32_t) sl, &d, &ds) == W4_OK &&
              d.chain_id == 1 && d.nonce == 7 && d.data_len == 36 &&
              memcmp(d.data + 4, root, 32) == 0,
          "EVM anchor transaction round trip through web4's EIP-1559 codec");
}

static void t_utxo(void)
{
    prov_chain_t c;
    prov_receipt_t r;
    memset(&c, 0, sizeof c);
    c.family = PROV_CHAIN_UTXO;
    strcpy(c.hrp, "bc");
    uint8_t sk[32], pub[64], pub33[33], prog[40];
    memset(sk, 0, 32);
    sk[31] = 1;
    w4_secp_pubkey(sk, pub);
    w4_secp_compress(pub, pub33);
    char a[100];
    CHECK(prov_chain_address(&c, pub33, 33, a, sizeof a) > 0 &&
              strcmp(a, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4") == 0,
          "BIP-173 vector: P2WPKH of the generator");
    CHECK(prov_chain_address_parse(&c, a, (uint32_t) strlen(a), prog, sizeof prog) == 20,
          "segwit address parses");
    CHECK(prov_chain_address_parse(&c, "tb1qw508d6qejxtdg4y5r3zarvary0c5xw7kxpjzsx", 42, prog,
                                   sizeof prog) < 0,
          "wrong network HRP refused");
    fixture(&r);
    uint8_t text[PROV_CHAIN_MSG_LEN], h[32];
    prov_chain_receipt_text(&r, text);
    prov_btc_message_hash(text, sizeof text, h);
    CHECK(memcmp(h, V_BTC_HASH, 32) == 0,
          "Bitcoin signed-message hash matches the independent implementation");
    /* cross-implementation signature: find the header that recovers the key */
    int ok = 0;
    uint8_t sig[65];
    for (int hd = 31; hd <= 34; hd++) {
        sig[0] = (uint8_t) hd;
        memcpy(sig + 1, V_BTC_RS, 64);
        if (prov_chain_verify_receipt(&c, &r, V_BTC_KEYHASH, 20, sig, 65) == PROV_OK) ok++;
    }
    CHECK(ok == 1, "UTXO: external (Python) signature verifies under exactly one recovery header");
    /* in-module signature, compressed P2WPKH */
    w4_ecdsa_sig_t s;
    uint8_t kh[20];
    w4_secp_sign(sk, h, &s);
    sig[0] = (uint8_t) (31 + s.recid);
    memcpy(sig + 1, s.r, 32);
    memcpy(sig + 33, s.s, 32);
    w4_hash160(pub33, 33, kh);
    CHECK(prov_chain_verify_receipt(&c, &r, kh, 20, sig, 65) == PROV_OK,
          "UTXO: in-module signature binds the P2WPKH key hash");
    r.fee++;
    CHECK(prov_chain_verify_receipt(&c, &r, kh, 20, sig, 65) == PROV_ERR_AUTH,
          "UTXO: tampered receipt refused");
    uint8_t root[32], pl[64];
    memset(root, 0x33, 32);
    CHECK(prov_chain_anchor_payload(&c, root, pl, sizeof pl) == 34 && pl[0] == 0x6a &&
              pl[1] == 0x20 && memcmp(pl + 2, root, 32) == 0,
          "UTXO anchor: OP_RETURN PUSH32 root");
}

static void t_ed25519(void)
{
    /* RFC 8032 section 7.1, TEST 1 (empty message) on the primitive. */
    uint8_t pk[32], sig[64];
    hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", pk, 32);
    hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e3970"
        "1cf9b46bd25bf5f0595bbe24655141438e7a100b",
        sig, 64);
    CHECK(ed25519_verify((const uint8_t *) "", 0, sig, pk), "RFC 8032 TEST 1 verifies");
    prov_chain_t c;
    prov_receipt_t r;
    memset(&c, 0, sizeof c);
    c.family = PROV_CHAIN_ED25519;
    fixture(&r);
    uint8_t d[32];
    prov_receipt_digest(&r, d);
    CHECK(memcmp(d, V_RECEIPT_DIGEST, 32) == 0,
          "receipt digest matches the independent Python encoder");
    CHECK(prov_chain_verify_receipt(&c, &r, V_ED_PUB, 32, V_ED_SIG, 64) == PROV_OK,
          "Ed25519: external signature over the receipt text verifies");
    r.net--;
    CHECK(prov_chain_verify_receipt(&c, &r, V_ED_PUB, 32, V_ED_SIG, 64) == PROV_ERR_AUTH,
          "Ed25519: tampered receipt refused");
    char a[80];
    uint8_t back[32], zero[32];
    CHECK(prov_chain_address(&c, V_ED_PUB, 32, a, sizeof a) > 0 && strcmp(a, V_ED_ADDR) == 0,
          "Ed25519 address = base58(pubkey)");
    memset(zero, 0, 32);
    CHECK(prov_chain_address(&c, zero, 32, a, sizeof a) > 0 &&
              strcmp(a, "11111111111111111111111111111111") == 0,
          "base58 of 32 zero bytes is the well-known all-ones system address");
    CHECK(prov_chain_address_parse(&c, V_ED_ADDR, (uint32_t) strlen(V_ED_ADDR), back, 32) == 32 &&
              memcmp(back, V_ED_PUB, 32) == 0,
          "Ed25519 address parses back");
    uint8_t root[32], pl[80];
    memset(root, 0xAB, 32);
    CHECK(prov_chain_anchor_payload(&c, root, pl, sizeof pl) == 69 &&
              memcmp(pl, "zxv1:abab", 9) == 0,
          "Ed25519 anchor: memo text");
}

static void t_cosmos(void)
{
    prov_chain_t c;
    prov_receipt_t r;
    memset(&c, 0, sizeof c);
    c.family = PROV_CHAIN_COSMOS;
    strcpy(c.hrp, "cosmos");
    strcpy(c.chain_id, "cosmoshub-4");
    uint8_t sk[32], pub[64], pub33[33], kh[20];
    memset(sk, 0, 32);
    sk[31] = 1;
    w4_secp_pubkey(sk, pub);
    w4_secp_compress(pub, pub33);
    char a[100];
    CHECK(prov_chain_address(&c, pub33, 33, a, sizeof a) > 0 && strcmp(a, V_COSMOS_G_ADDR) == 0,
          "Cosmos bech32 address of the generator (HASH160 = BIP-173 program)");
    CHECK(prov_chain_address_parse(&c, a, (uint32_t) strlen(a), kh, 20) == 20 && kh[0] == 0x75 &&
              kh[19] == 0xd6,
          "Cosmos address parses to 751e...3bd6");
    CHECK(prov_chain_address_parse(&c, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", 42, kh, 20) <
              0,
          "Cosmos: other HRP refused");
    uint8_t doc[64],
        want[] = {0x0a, 0x02, 'a', 'b', 0x12, 0x01, 'c', 0x1a, 0x01, 'x', 0x20, 0xac, 0x02};
    CHECK(prov_cosmos_sign_doc((const uint8_t *) "ab", 2, (const uint8_t *) "c", 1, "x", 300, doc,
                               sizeof doc) == (int32_t) sizeof want &&
              memcmp(doc, want, sizeof want) == 0,
          "SignDoc: protobuf wire bytes (fields 1-4, varint 300 = ac 02)");
    CHECK(prov_cosmos_sign_doc(0, 0, 0, 0, "", 0, doc, sizeof doc) == 0,
          "SignDoc: proto3 defaults omitted");
    CHECK(prov_cosmos_sign_doc((const uint8_t *) "ab", 2, 0, 0, "x", 1, doc, 4) == PROV_ERR_SPACE,
          "SignDoc: small buffer refused");
    fixture(&r);
    CHECK(prov_chain_verify_receipt(&c, &r, V_COSMOS_PUB, 33, V_COSMOS_SIG, 64) == PROV_OK,
          "Cosmos: external secp256k1 signature (r||s, low-s) verifies");
    r.hold++;
    CHECK(prov_chain_verify_receipt(&c, &r, V_COSMOS_PUB, 33, V_COSMOS_SIG, 64) == PROV_ERR_AUTH,
          "Cosmos: tampered receipt refused");
    r.hold--;
    CHECK(prov_chain_verify_receipt(&c, &r, pub33, 33, V_COSMOS_SIG, 64) == PROV_ERR_AUTH,
          "Cosmos: other key refused");
}

static int g_ext_calls;
static bool ext_ok(void *ctx, const uint8_t *pub, uint32_t pl, const uint8_t *msg, uint32_t len,
                   const uint8_t *sig, uint32_t sl)
{
    (void) ctx, (void) pub, (void) pl, (void) sig;
    g_ext_calls++;
    return len == PROV_CHAIN_MSG_LEN && memcmp(msg, "zxv-prov-receipt:", 17) == 0 && sl == 64;
}

static int g_sub_len;
static int sub(void *ctx, uint8_t family, const uint8_t *p, uint32_t len)
{
    (void) ctx, (void) p;
    g_sub_len = (int) len + family * 1000;
    return 0;
}

static void t_external(void)
{
    prov_chain_t c;
    prov_receipt_t r;
    uint8_t pub[32] = {0}, sig[64] = {0};
    fixture(&r);
    memset(&c, 0, sizeof c);
    c.family = PROV_CHAIN_SUBSTRATE;
    CHECK(!prov_chain_native_verify(PROV_CHAIN_SUBSTRATE) &&
              prov_chain_verify_receipt(&c, &r, pub, 32, sig, 64) == PROV_ERR_UNSUPPORTED,
          "sr25519 is external: no hook -> UNSUPPORTED (never a fake pass)");
    c.ext_verify = ext_ok;
    CHECK(prov_chain_verify_receipt(&c, &r, pub, 32, sig, 64) == PROV_OK && g_ext_calls == 1,
          "sr25519 via the host's external verifier");
    char a[16];
    CHECK(prov_chain_address(&c, pub, 32, a, sizeof a) == PROV_ERR_UNSUPPORTED,
          "SS58 addresses are opaque here");
    c.family = PROV_CHAIN_OPAQUE;
    c.ext_verify = 0;
    CHECK(prov_chain_verify_receipt(&c, &r, pub, 32, sig, 64) == PROV_ERR_UNSUPPORTED,
          "opaque family without hooks refused");
    uint8_t root[32], pl[64];
    memset(root, 1, 32);
    CHECK(prov_chain_anchor_payload(&c, root, pl, sizeof pl) == 36 && memcmp(pl, "zxv1", 4) == 0,
          "opaque anchor payload");
    CHECK(prov_chain_submit(&c, pl, 36) == PROV_ERR_HOOK, "no transport hook: nothing submitted");
    c.submit = sub;
    CHECK(prov_chain_submit(&c, pl, 36) == PROV_OK && g_sub_len == 36 + PROV_CHAIN_OPAQUE * 1000,
          "transport is the host's hook");
}

static void t_anchor(void)
{
    static uint8_t leaves[9][32], path[PROV_ANCHOR_MAX_DEPTH][32], root[32], root2[32];
    for (int i = 0; i < 9; i++) memset(leaves[i], 0x10 + i, 32);
    int all = 1;
    for (uint32_t n = 1; n <= 9; n++) {
        prov_anchor_root((const uint8_t(*)[32]) leaves, n, root);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t dirs;
            int dpt = prov_anchor_proof((const uint8_t(*)[32]) leaves, n, k, path, &dirs);
            if (dpt < 0 || !prov_anchor_verify(leaves[k], (const uint8_t(*)[32]) path,
                                               (uint32_t) dpt, dirs, root))
                all = 0;
            if (n > 1 && prov_anchor_verify(leaves[(k + 1) % n], (const uint8_t(*)[32]) path,
                                            (uint32_t) dpt, dirs, root))
                all = 0;
        }
    }
    CHECK(all, "Merkle anchor: every leaf proves, no leaf proves at another's path (n = 1..9)");
    prov_anchor_root((const uint8_t(*)[32]) leaves, 4, root);
    leaves[2][0] ^= 1;
    prov_anchor_root((const uint8_t(*)[32]) leaves, 4, root2);
    CHECK(memcmp(root, root2, 32) != 0, "Merkle anchor: changing a receipt changes the root");
    uint8_t one[32], lh[33];
    prov_anchor_root((const uint8_t(*)[32]) leaves, 1, root);
    lh[0] = 0;
    memcpy(lh + 1, leaves[0], 32);
    prov_sha3(lh, 33, one);
    CHECK(memcmp(root, one, 32) == 0, "Merkle anchor: single leaf root = H(0x00 || digest)");
    CHECK(prov_anchor_root((const uint8_t(*)[32]) leaves, 0, root) == PROV_ERR_ARG,
          "empty batch refused");
}

static void t_convert(void)
{
    uint8_t amt[32], back[32];
    uint64_t q;
    w4_u256 a;
    /* 1.5 ETH at a posted 2500.00 EUR (250000 cents) per ETH = 375000 cents */
    w4_u256_from_dec(&a, "1500000000000000000", 19);
    w4_u256_to_be(&a, amt);
    CHECK(prov_chain_to_quote(amt, 32, 18, 250000, &q) == PROV_OK && q == 375000,
          "chain -> quote: 1.5 ETH at 2500.00 = 3750.00");
    CHECK(prov_quote_to_chain(375000, 18, 250000, back) == PROV_OK && memcmp(back, amt, 32) == 0,
          "quote -> chain: exact inverse");
    CHECK(prov_quote_to_chain(1, 18, 250000, back) == PROV_OK && back[31] == 0x00,
          "quote -> chain: 1 cent = 4e12 wei");
    w4_u256_from_be(&a, back, 32);
    uint64_t v;
    CHECK(w4_u256_to_u64(&a, &v) && v == 4000000000000ull, "1 cent = 4e12 wei exactly");
    w4_u256_from_dec(&a, "1", 1);
    w4_u256_to_be(&a, amt);
    CHECK(prov_chain_to_quote(amt, 32, 18, 250000, &q) == PROV_OK && q == 0,
          "1 wei floors to 0 cents (never rounds up against the payer)");
    uint8_t big[32];
    memset(big, 0xFF, 32);
    CHECK(prov_chain_to_quote(big, 32, 18, 250000, &q) == PROV_ERR_OVERFLOW,
          "overflow detected, not wrapped");
    /* 8-decimal UTXO asset, large price exercises the 64-bit divisor */
    w4_u256_from_dec(&a, "123456789", 9);
    w4_u256_to_be(&a, amt);
    CHECK(prov_chain_to_quote(amt, 32, 8, 6543210987ull, &q) == PROV_OK && q == 8078038182ull,
          "8-decimal asset: floor(123456789 * 6543210987 / 1e8)");
    CHECK(prov_quote_to_chain(8078038182ull, 8, 6543210987ull, back) == PROV_OK,
          "inverse with a >32-bit price");
    w4_u256_from_be(&a, back, 32);
    CHECK(w4_u256_to_u64(&a, &v) && v == 123456788ull,
          "inverse floors (123456788 base units; never over-pays)");
    CHECK(prov_quote_to_chain(1, 18, 0, back) == PROV_ERR_ARG, "zero price refused");
}

int main(void)
{
    t_eip712();
    t_evm();
    t_utxo();
    t_ed25519();
    t_cosmos();
    t_external();
    t_anchor();
    t_convert();
    printf("test_prov_chain: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

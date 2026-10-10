/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_web4_web3.c — Web 3 side against PUBLISHED vectors:
 *   Keccak-256          the empty string and "abc" (Keccak team / Ethereum)
 *   RLP                 the examples on ethereum.org "Recursive-length prefix"
 *   EIP-155             the worked example in the EIP (signing data, hash, v r s,
 *                       signed tx), signed here with our RFC 6979 signer
 *   EIP-1559            all signed transactions in ethereumjs tx 3.5.2 eip1559.json
 *                       (decode, signing hash, recover, re-sign, re-encode)
 *   EIP-55              every address listed in ERC-55
 *   secp256k1           k = 1 gives G; Bitcoin wiki key -> 16UwLL9...; BIP-173's
 *                       P2WPKH of G's compressed key
 *   RIPEMD-160          the algorithm authors' vectors
 *   bech32 / bech32m    BIP-173 and BIP-350, valid and invalid lists, addresses
 *   ABI                 transfer / balanceOf selectors (a9059cbb, 70a08231)
 * plus fuzz-style malformed input for every decoder. */
#include <stdio.h>
#include <string.h>
#include "web4_web3.h"
#include "test_web4_vectors.h"

static int failures = 0, passes = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
            passes++;                                                                              \
        }                                                                                          \
    } while (0)
static int qfail __attribute__((unused)) = 0;
#define QCHECK(c, m)                                                                               \
    do {                                                                                           \
        if (!(c) && qfail++ < 10) printf("  [fail] %s (line %d)\n", m, __LINE__);                  \
    } while (0)

#define N(a) (sizeof(a) / sizeof((a)[0]))

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next(void)
{
    uint64_t z = (rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static int unhex(const char *h, uint8_t *out, uint32_t cap)
{
    if (h[0] == '0' && h[1] == 'x') h += 2;
    return w4_hex_decode(h, (uint32_t) strlen(h), out, cap);
}

static bool hex_is(const uint8_t *b, uint32_t n, const char *h)
{
    uint8_t t[512];
    int k = unhex(h, t, sizeof t);
    return k == (int) n && memcmp(t, b, n) == 0;
}

static void test_keccak(void)
{
    uint8_t h[32];
    w4_keccak256((const uint8_t *) "", 0, h);
    CHECK(hex_is(h, 32, "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"),
          "keccak256(\"\")");
    w4_keccak256((const uint8_t *) "abc", 3, h);
    CHECK(hex_is(h, 32, "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45"),
          "keccak256(\"abc\")");
    uint8_t big[300];
    for (int i = 0; i < 300; i++) big[i] = (uint8_t) i;
    uint8_t h2[32];
    w4_keccak256(big, 136, h);
    w4_keccak256(big, 137, h2);
    CHECK(memcmp(h, h2, 32) != 0, "keccak256 block boundary inputs differ");
    /* HMAC-SHA256: RFC 4231 test case 2 */
    w4_hmac_sha256((const uint8_t *) "Jefe", 4, (const uint8_t *) "what do ya want for nothing?",
                   28, h);
    CHECK(hex_is(h, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
          "HMAC-SHA256 RFC 4231 case 2");
}

static bool rlp_is(void (*f)(w4_rlp_enc_t *), const char *hex)
{
    uint8_t buf[256];
    w4_rlp_enc_t e;
    w4_rlp_init(&e, buf, sizeof buf);
    f(&e);
    int32_t n = w4_rlp_finish(&e);
    return n > 0 && hex_is(buf, (uint32_t) n, hex);
}
static void r_dog(w4_rlp_enc_t *e)
{
    w4_rlp_str(e, "dog");
}
static void r_catdog(w4_rlp_enc_t *e)
{
    w4_rlp_list_begin(e);
    w4_rlp_str(e, "cat");
    w4_rlp_str(e, "dog");
    w4_rlp_list_end(e);
}
static void r_empty(w4_rlp_enc_t *e)
{
    w4_rlp_str(e, "");
}
static void r_elist(w4_rlp_enc_t *e)
{
    w4_rlp_list_begin(e);
    w4_rlp_list_end(e);
}
static void r_zero(w4_rlp_enc_t *e)
{
    w4_rlp_u64(e, 0);
}
static void r_b00(w4_rlp_enc_t *e)
{
    uint8_t b = 0;
    w4_rlp_bytes(e, &b, 1);
}
static void r_b0f(w4_rlp_enc_t *e)
{
    uint8_t b = 0x0f;
    w4_rlp_bytes(e, &b, 1);
}
static void r_b0400(w4_rlp_enc_t *e)
{
    uint8_t b[2] = {4, 0};
    w4_rlp_bytes(e, b, 2);
}
static void r_three(w4_rlp_enc_t *e)
{ /* [ [], [[]], [ [], [[]] ] ] */
    w4_rlp_list_begin(e);
    w4_rlp_list_begin(e);
    w4_rlp_list_end(e);
    w4_rlp_list_begin(e);
    w4_rlp_list_begin(e);
    w4_rlp_list_end(e);
    w4_rlp_list_end(e);
    w4_rlp_list_begin(e);
    w4_rlp_list_begin(e);
    w4_rlp_list_end(e);
    w4_rlp_list_begin(e);
    w4_rlp_list_begin(e);
    w4_rlp_list_end(e);
    w4_rlp_list_end(e);
    w4_rlp_list_end(e);
    w4_rlp_list_end(e);
}
static const char LOREM[] = "Lorem ipsum dolor sit amet, consectetur adipisicing elit";
static void r_lorem(w4_rlp_enc_t *e)
{
    w4_rlp_str(e, LOREM);
}
static void r_1024(w4_rlp_enc_t *e)
{
    w4_rlp_u64(e, 1024);
}

static void test_rlp(void)
{
    CHECK(rlp_is(r_dog, "83646f67"), "RLP \"dog\"");
    CHECK(rlp_is(r_catdog, "c88363617483646f67"), "RLP [\"cat\",\"dog\"]");
    CHECK(rlp_is(r_empty, "80"), "RLP empty string");
    CHECK(rlp_is(r_elist, "c0"), "RLP empty list");
    CHECK(rlp_is(r_zero, "80"), "RLP integer 0");
    CHECK(rlp_is(r_b00, "00"), "RLP byte 0x00");
    CHECK(rlp_is(r_b0f, "0f"), "RLP byte 0x0f");
    CHECK(rlp_is(r_b0400, "820400"), "RLP bytes 04 00");
    CHECK(rlp_is(r_1024, "820400"), "RLP integer 1024");
    CHECK(rlp_is(r_three, "c7c0c1c0c3c0c1c0"), "RLP set-theoretic three");
    {
        char want[2 * 58 + 8] = "b838";
        for (size_t i = 0; i < strlen(LOREM); i++)
            sprintf(want + 4 + 2 * i, "%02x", (unsigned char) LOREM[i]);
        CHECK(rlp_is(r_lorem, want), "RLP 56-byte Lorem ipsum string (long form)");
    }
    /* decode round trip + iteration */
    uint8_t cd[] = {0xc8, 0x83, 'c', 'a', 't', 0x83, 'd', 'o', 'g'};
    w4_rlp_item_t l, it;
    uint32_t off = 0;
    CHECK(w4_rlp_decode(cd, sizeof cd, &l) == W4_OK && l.is_list && l.len == 8, "RLP decode list");
    CHECK(w4_rlp_next(&l, &off, &it) == W4_OK && it.len == 3 && memcmp(it.p, "cat", 3) == 0 &&
              w4_rlp_next(&l, &off, &it) == W4_OK && memcmp(it.p, "dog", 3) == 0 &&
              w4_rlp_next(&l, &off, &it) == W4_ERR_NOTFOUND,
          "RLP iterate list");
    /* non-canonical forms are refused */
    uint8_t nc1[] = {0x81, 0x05};                /* single byte < 0x80 wrapped */
    uint8_t nc2[] = {0xb8, 0x05, 1, 2, 3, 4, 5}; /* long form for < 56 */
    uint8_t nc3[] = {0xb9, 0x00, 0x40};          /* leading zero in length */
    uint8_t nc4[] = {0x83, 'a', 'b'};            /* truncated */
    uint8_t nc5[] = {0xc3, 0x83, 'a', 'b'};      /* inner truncated */
    CHECK(w4_rlp_decode(nc1, 2, &it) == W4_ERR_PARSE, "RLP refuses 0x81 0x05");
    CHECK(w4_rlp_decode(nc2, sizeof nc2, &it) == W4_ERR_PARSE, "RLP refuses long form < 56");
    CHECK(w4_rlp_decode(nc3, sizeof nc3, &it) == W4_ERR_PARSE, "RLP refuses leading-zero length");
    CHECK(w4_rlp_decode(nc4, sizeof nc4, &it) == W4_ERR_PARSE, "RLP refuses truncation");
    off = 0;
    CHECK(w4_rlp_decode(nc5, sizeof nc5, &l) == W4_OK && w4_rlp_next(&l, &off, &it) == W4_ERR_PARSE,
          "RLP list whose element overruns the list is refused on iteration");
    uint8_t z0[] = {0x82, 0x00, 0x01};
    CHECK(w4_rlp_decode(z0, 3, &it) == W4_OK && w4_rlp_as_u64(&it, &(uint64_t){0}) == W4_ERR_PARSE,
          "RLP integer with leading zero refused");
    /* unbalanced encoder */
    uint8_t b[8];
    w4_rlp_enc_t e;
    w4_rlp_init(&e, b, sizeof b);
    w4_rlp_list_begin(&e);
    CHECK(w4_rlp_finish(&e) == W4_ERR_STATE, "RLP unbalanced list reported");
    w4_rlp_init(&e, b, 3);
    w4_rlp_str(&e, "dog");
    CHECK(w4_rlp_finish(&e) == W4_ERR_SPACE, "RLP overflow reported");
    /* fuzz: random buffers never crash and every OK item lies inside the buffer */
    int bad = 0;
    for (int i = 0; i < 20000; i++) {
        uint8_t fz[64];
        uint32_t n = (uint32_t) (next() % 64) + 1;
        for (uint32_t k = 0; k < n; k++) fz[k] = (uint8_t) next();
        if (w4_rlp_decode(fz, n, &it) == W4_OK) {
            if (it.total > n || it.p + it.len > fz + n) bad++;
            if (it.is_list) {
                uint32_t o2 = 0;
                w4_rlp_item_t x;
                int g = 0;
                while (w4_rlp_next(&it, &o2, &x) == W4_OK && g++ < 100)
                    if (x.p + x.len > it.p + it.len) bad++;
            }
        }
    }
    CHECK(bad == 0, "RLP fuzz: 20000 random inputs stay in bounds");
}

static void test_eip155(void)
{
    uint8_t sk[32];
    memset(sk, 0x46, 32);
    w4_eth_tx_t tx;
    memset(&tx, 0, sizeof tx);
    tx.chain_id = 1;
    tx.nonce = 9;
    w4_u256_from_u64(&tx.gas_price, 20000000000ull);
    tx.gas_limit = 21000;
    tx.has_to = true;
    memset(tx.to, 0x35, 20);
    w4_u256_from_dec(&tx.value, "1000000000000000000", 19);
    uint8_t buf[256], h[32];
    int32_t n = w4_eth_legacy_unsigned(&tx, buf, sizeof buf);
    CHECK(n > 0 && hex_is(buf, (uint32_t) n,
                          "ec098504a817c800825208943535353535353535353535353535353535353535880de0b"
                          "6b3a764000080018080"),
          "EIP-155 example: signing data");
    w4_eth_signing_hash(buf, (uint32_t) n, h);
    CHECK(hex_is(h, 32, "daf5a779ae972f972197303d7b574746c7ef83eadac0f2791ad23db92e4c8e53"),
          "EIP-155 example: signing hash");
    w4_ecdsa_sig_t sig;
    CHECK(w4_secp_sign(sk, h, &sig) == W4_OK, "secp256k1 sign (RFC 6979)");
    CHECK(
        hex_is(sig.r, 32, "28ef61340bd939bc2195fe537567866003e1a15d3c71ff63e1590620aa636276") &&
            hex_is(sig.s, 32, "67cbe9d8997f761aecb703304b3800ccf555c9f3dc64214b297fb1966a3b6d83") &&
            sig.recid == 0,
        "EIP-155 example: r, s, v = 37 reproduced by RFC 6979");
    n = w4_eth_legacy_signed(&tx, &sig, buf, sizeof buf);
    CHECK(n > 0 &&
              hex_is(buf, (uint32_t) n,
                     "f86c098504a817c800825208943535353535353535353535353535353535353535880de0b"
                     "6b3a76400008025a028ef61340bd939bc2195fe537567866003e1a15d3c71ff63e159"
                     "0620aa636276a067cbe9d8997f761aecb703304b3800ccf555c9f3dc64214b297fb196"
                     "6a3b6d83"),
          "EIP-155 example: signed transaction bytes");
    uint8_t pub[64], rec[64], a1[20], a2[20];
    CHECK(w4_secp_pubkey(sk, pub) == W4_OK, "secp256k1 public key of 0x46..46");
    CHECK(w4_secp_verify(pub, h, &sig, true) == W4_OK, "secp256k1 verify own signature");
    CHECK(w4_secp_recover(h, &sig, rec) == W4_OK && memcmp(rec, pub, 64) == 0,
          "secp256k1 recover returns the signer's key");
    w4_eth_address(pub, a1);
    w4_eth_address(rec, a2);
    char cs[43];
    w4_eth_checksum(a1, cs);
    CHECK(memcmp(a1, a2, 20) == 0 && strcmp(cs, "0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F") == 0,
          "EIP-155 example sender address 0x9d8A62f6...");
    uint8_t h2[32];
    memcpy(h2, h, 32);
    h2[0] ^= 1;
    CHECK(w4_secp_verify(pub, h2, &sig, true) == W4_ERR_SIG, "secp256k1 verify rejects other hash");
    w4_ecdsa_sig_t hi = sig;
    /* s' = n - s is the malleated twin: valid ECDSA, refused under low-s */
    static const uint8_t NBE[32] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
                                    0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b,
                                    0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x41};
    w4_u256 nn, ss, d;
    w4_u256_from_be(&nn, NBE, 32);
    w4_u256_from_be(&ss, sig.s, 32);
    w4_u256_sub(&d, &nn, &ss);
    w4_u256_to_be(&d, hi.s);
    hi.recid ^= 1;
    CHECK(w4_secp_verify(pub, h, &hi, false) == W4_OK &&
              w4_secp_verify(pub, h, &hi, true) == W4_ERR_SIG,
          "secp256k1 high-s twin: valid ECDSA, refused when low-s required (EIP-2)");
}

static void test_secp_misc(void)
{
    CHECK(w4_secp_selftest(), "secp256k1 Montgomery constants recomputed");
    uint8_t sk[32] = {0}, pub[64];
    sk[31] = 1;
    CHECK(w4_secp_pubkey(sk, pub) == W4_OK &&
              hex_is(pub, 64,
                     "79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a"
                     "3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"),
          "secp256k1: 1 * G = G");
    uint8_t addr[20];
    char cs[43];
    w4_eth_address(pub, addr);
    w4_eth_checksum(addr, cs);
    CHECK(strcmp(cs, "0x7E5F4552091A69125d5DfCb7b8C2659029395Bdf") == 0,
          "Ethereum address of private key 1");
    uint8_t c33[33];
    char a[100];
    w4_secp_compress(pub, c33);
    CHECK(w4_btc_p2wpkh("bc", c33, a, sizeof a) > 0 &&
              strcmp(a, "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4") == 0,
          "BIP-173: P2WPKH of G's compressed key");
    /* Bitcoin wiki "Technical background of version 1 Bitcoin addresses" */
    uint8_t wk[32], wp[65];
    unhex("18e14a7b6a307f426a94f8114701e7c8e774e7f9a47e2c2035db29a206321725", wk, 32);
    wp[0] = 4;
    CHECK(w4_secp_pubkey(wk, wp + 1) == W4_OK &&
              hex_is(wp, 65,
                     "0450863ad64a87ae8a2fe83c1af1a8403cb53f53e486d8511dad8a04887e5b23522cd47024"
                     "3453a299fa9e77237716103abc11a1df38855ed6f2ee187e9c582ba6"),
          "Bitcoin wiki private key -> public key");
    uint8_t h160[20];
    w4_hash160(wp, 65, h160);
    CHECK(hex_is(h160, 20, "010966776006953d5567439e5e39f86a0d273bee"), "Bitcoin wiki HASH160");
    CHECK(w4_btc_p2pkh(0, wp, 65, a, sizeof a) > 0 &&
              strcmp(a, "16UwLL9Risc3QfPqBUvKofHmBQ7wMtjvM") == 0,
          "Bitcoin wiki base58check P2PKH address");
    uint8_t dec[32];
    CHECK(w4_base58check_decode(a, (uint32_t) strlen(a), dec, sizeof dec) == 21 && dec[0] == 0 &&
              memcmp(dec + 1, h160, 20) == 0,
          "base58check decode round trip");
    a[5] = (a[5] == 'x') ? 'y' : 'x';
    CHECK(w4_base58check_decode(a, (uint32_t) strlen(a), dec, sizeof dec) == W4_ERR_HASH,
          "base58check detects a changed character");
    /* range checks */
    uint8_t zero[32] = {0}, n[32];
    unhex("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141", n, 32);
    CHECK(w4_secp_pubkey(zero, pub) == W4_ERR_RANGE && w4_secp_pubkey(n, pub) == W4_ERR_RANGE,
          "secp256k1 refuses secret keys 0 and n");
    /* random keys: sign/verify/recover agree */
    int bad = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t k[32], h[32], p[64], r[64];
        for (int j = 0; j < 32; j++) k[j] = (uint8_t) next(), h[j] = (uint8_t) next();
        k[0] &= 0x7f;
        w4_ecdsa_sig_t s;
        if (w4_secp_pubkey(k, p) || w4_secp_sign(k, h, &s) || w4_secp_verify(p, h, &s, true) ||
            w4_secp_recover(h, &s, r) || memcmp(p, r, 64))
            bad++;
    }
    CHECK(bad == 0, "secp256k1: 8 random keys sign / verify / recover");
    /* off-curve public key refused */
    uint8_t h[32] = {1};
    w4_ecdsa_sig_t s;
    w4_secp_sign(sk, h, &s);
    w4_secp_pubkey(sk, pub);
    pub[63] ^= 1;
    CHECK(w4_secp_verify(pub, h, &s, true) == W4_ERR_SIG, "secp256k1 refuses an off-curve key");
}

static void test_ripemd(void)
{
    uint8_t h[20];
    w4_ripemd160((const uint8_t *) "", 0, h);
    CHECK(hex_is(h, 20, "9c1185a5c5e9fc54612808977ee8f548b2258d31"), "RIPEMD-160 \"\"");
    w4_ripemd160((const uint8_t *) "abc", 3, h);
    CHECK(hex_is(h, 20, "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc"), "RIPEMD-160 \"abc\"");
    w4_ripemd160((const uint8_t *) "message digest", 14, h);
    CHECK(hex_is(h, 20, "5d0689ef49d2fae572b881b123a85ffa21595f36"), "RIPEMD-160 message digest");
    const char *az = "abcdefghijklmnopqrstuvwxyz";
    w4_ripemd160((const uint8_t *) az, 26, h);
    CHECK(hex_is(h, 20, "f71c27109c692c1b56bbdceb5b9d2865b3708dbc"), "RIPEMD-160 a..z");
    const char *l56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    w4_ripemd160((const uint8_t *) l56, 56, h);
    CHECK(hex_is(h, 20, "12a053384a9c0c88e405a06c27dcf49ada62eb2b"),
          "RIPEMD-160 56-byte (two-block padding)");
}

static void test_bech32(void)
{
    char hrp[100];
    uint8_t d[100];
    uint32_t n;
    int ok = 0;
    for (size_t i = 0; i < N(BIP173_VALID); i++)
        ok += w4_bech32_decode(BIP173_VALID[i], (uint32_t) strlen(BIP173_VALID[i]), hrp, sizeof hrp,
                               d, &n, sizeof d) == W4_BECH32;
    CHECK(ok == (int) N(BIP173_VALID), "BIP-173 valid bech32 strings");
    ok = 0;
    for (size_t i = 0; i < N(BIP173_INVALID); i++)
        ok += w4_bech32_decode(BIP173_INVALID[i], (uint32_t) strlen(BIP173_INVALID[i]), hrp,
                               sizeof hrp, d, &n, sizeof d) < 0;
    CHECK(ok == (int) N(BIP173_INVALID), "BIP-173 invalid bech32 strings rejected");
    ok = 0;
    for (size_t i = 0; i < N(BIP350_VALID); i++)
        ok += w4_bech32_decode(BIP350_VALID[i], (uint32_t) strlen(BIP350_VALID[i]), hrp, sizeof hrp,
                               d, &n, sizeof d) == W4_BECH32M;
    CHECK(ok == (int) N(BIP350_VALID), "BIP-350 valid bech32m strings");
    ok = 0;
    for (size_t i = 0; i < N(BIP350_INVALID); i++)
        ok += w4_bech32_decode(BIP350_INVALID[i], (uint32_t) strlen(BIP350_INVALID[i]), hrp,
                               sizeof hrp, d, &n, sizeof d) < 0;
    CHECK(ok == (int) N(BIP350_INVALID), "BIP-350 invalid bech32m strings rejected");
    /* re-encode the valid ones */
    ok = 0;
    for (size_t i = 0; i < N(BIP350_VALID); i++) {
        const char *s = BIP350_VALID[i];
        char out[100], low[100];
        if (w4_bech32_decode(s, (uint32_t) strlen(s), hrp, sizeof hrp, d, &n, sizeof d) !=
            W4_BECH32M)
            continue;
        for (size_t k = 0; k <= strlen(s); k++) low[k] = w4_lower(s[k]);
        ok += w4_bech32_encode(W4_BECH32M, hrp, d, n, out, sizeof out) > 0 && strcmp(out, low) == 0;
    }
    CHECK(ok == (int) N(BIP350_VALID), "BIP-350 valid strings re-encode identically");
    /* segwit addresses */
    ok = 0;
    for (size_t i = 0; i < N(BIP350_ADDR_VALID); i++) {
        const char *a = BIP350_ADDR_VALID[i].addr;
        char h[4] = {w4_lower(a[0]), w4_lower(a[1]), 0, 0};
        uint8_t ver, prog[40], spk[42];
        uint32_t pl;
        if (w4_segwit_decode(h, a, (uint32_t) strlen(a), &ver, prog, &pl) != W4_OK) continue;
        spk[0] = (uint8_t) (ver ? ver + 0x50 : 0);
        spk[1] = (uint8_t) pl;
        memcpy(spk + 2, prog, pl);
        char out[100], low[100];
        for (size_t k = 0; k <= strlen(a); k++) low[k] = w4_lower(a[k]);
        if (hex_is(spk, pl + 2, BIP350_ADDR_VALID[i].spk_hex) &&
            w4_segwit_encode(h, ver, prog, pl, out, sizeof out) > 0 && strcmp(out, low) == 0)
            ok++;
    }
    CHECK(ok == (int) N(BIP350_ADDR_VALID), "BIP-350 valid segwit addresses -> scriptPubKey");
    ok = 0;
    for (size_t i = 0; i < N(BIP350_ADDR_INVALID); i++) {
        const char *a = BIP350_ADDR_INVALID[i];
        uint8_t ver, prog[40];
        uint32_t pl;
        int r1 = w4_segwit_decode("bc", a, (uint32_t) strlen(a), &ver, prog, &pl);
        int r2 = w4_segwit_decode("tb", a, (uint32_t) strlen(a), &ver, prog, &pl);
        ok += (r1 != W4_OK && r2 != W4_OK);
    }
    CHECK(ok == (int) N(BIP350_ADDR_INVALID), "BIP-350 invalid segwit addresses rejected");
    /* every single-character substitution of a valid address is rejected */
    const char *base = "bc1p0xlxvlhemja6c4dqv22uapctqupfhlxm9h8z3k2e72q4k9hcz7vqzk5jj0";
    int caught = 0, tried = 0;
    for (size_t i = 3; i < strlen(base); i++) {
        char m[100];
        strcpy(m, base);
        m[i] = (m[i] == 'q') ? 'p' : 'q';
        uint8_t ver, prog[40];
        uint32_t pl;
        tried++;
        caught += w4_segwit_decode("bc", m, (uint32_t) strlen(m), &ver, prog, &pl) != W4_OK;
    }
    CHECK(caught == tried, "bech32m: every single substitution detected");
    /* fuzz */
    int crash = 0;
    for (int i = 0; i < 20000; i++) {
        char fz[100];
        uint32_t L = (uint32_t) (next() % 95);
        for (uint32_t k = 0; k < L; k++) fz[k] = (char) (33 + next() % 94);
        uint8_t ver, prog[40];
        uint32_t pl = 99;
        if (w4_segwit_decode("bc", fz, L, &ver, prog, &pl) == W4_OK && (pl < 2 || pl > 40)) crash++;
        uint8_t b[64];
        int r = w4_base58check_decode(fz, L, b, sizeof b);
        if (r > 64) crash++;
    }
    CHECK(crash == 0, "bech32 / base58check fuzz: 20000 random strings");
}

static void test_eip55(void)
{
    int ok = 0;
    for (size_t i = 0; i < N(ERC55_VALID); i++) {
        const char *s = ERC55_VALID[i];
        uint8_t a[20];
        bool chk;
        char out[43];
        if (w4_eth_address_parse(s, 42, a, &chk) != W4_OK) continue;
        w4_eth_checksum(a, out);
        bool mixed = false, lo = false, up = false;
        for (int k = 2; k < 42; k++) {
            if (s[k] >= 'a' && s[k] <= 'f') lo = true;
            if (s[k] >= 'A' && s[k] <= 'F') up = true;
        }
        mixed = lo && up;
        if (mixed ? (chk && strcmp(out, s) == 0) : !chk) ok++;
    }
    CHECK(ok == (int) N(ERC55_VALID), "ERC-55: every listed address parses and re-checksums");
    char bad[43];
    strcpy(bad, "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed");
    bad[3] = 'A'; /* flip case of one letter */
    uint8_t a[20];
    bool chk;
    CHECK(w4_eth_address_parse(bad, 42, a, &chk) == W4_ERR_HASH, "ERC-55: wrong case is refused");
    CHECK(w4_eth_address_parse("0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAe", 41, a, &chk) ==
              W4_ERR_PARSE,
          "address of 39 hex digits refused");
}

static void test_eip1559(void)
{
    int dec = 0, hash_ok = 0, resign = 0, reenc = 0;
    for (size_t i = 0; i < N(EIP1559_TX); i++) {
        const w4v_1559_t *v = &EIP1559_TX[i];
        uint8_t wrapped[512], sk[32];
        int wl = unhex(v->signed_rlp, wrapped, sizeof wrapped);
        unhex(v->sk, sk, 32);
        /* the fixture holds the typed transaction as an RLP string */
        w4_rlp_item_t it;
        if (wl <= 0 || w4_rlp_decode(wrapped, (uint32_t) wl, &it) || it.is_list) continue;
        w4_eth_tx_t tx;
        w4_ecdsa_sig_t sig;
        if (w4_eth_1559_decode(it.p, it.len, &tx, &sig) != W4_OK) continue;
        uint64_t mp, mf, val;
        uint8_t to[20];
        unhex(v->to, to, 20);
        if (w4_u256_to_u64(&tx.max_priority, &mp) && w4_u256_to_u64(&tx.max_fee, &mf) &&
            w4_u256_to_u64(&tx.value, &val) && tx.nonce == v->nonce &&
            tx.gas_limit == v->gas_limit && mp == v->max_priority && mf == v->max_fee &&
            val == v->value && tx.has_to && memcmp(tx.to, to, 20) == 0)
            dec++;
        uint8_t u[256], h[32], pub[64], rec[64];
        int32_t un = w4_eth_1559_unsigned(&tx, u, sizeof u);
        w4_eth_signing_hash(u, (uint32_t) un, h);
        w4_secp_pubkey(sk, pub);
        if (w4_secp_recover(h, &sig, rec) == W4_OK && memcmp(rec, pub, 64) == 0 &&
            w4_secp_verify(pub, h, &sig, true) == W4_OK)
            hash_ok++;
        w4_ecdsa_sig_t mine;
        w4_secp_sign(sk, h, &mine);
        if (memcmp(&mine, &sig, sizeof mine) == 0) resign++;
        uint8_t out[512];
        int32_t on = w4_eth_1559_signed(&tx, &mine, out, sizeof out);
        if (on == (int32_t) it.len && memcmp(out, it.p, it.len) == 0) reenc++;
    }
    printf("  (%u EIP-1559 vectors)\n", (unsigned) N(EIP1559_TX));
    CHECK(dec == (int) N(EIP1559_TX), "EIP-1559 vectors decode to their listed fields");
    CHECK(hash_ok == (int) N(EIP1559_TX),
          "EIP-1559 signing hash: published signature recovers the key's public key");
    CHECK(resign == (int) N(EIP1559_TX), "EIP-1559: RFC 6979 re-signing reproduces r, s, yParity");
    CHECK(reenc == (int) N(EIP1559_TX), "EIP-1559: signed encoding byte-identical");
    /* malformed typed transactions */
    uint8_t junk[3] = {0x02, 0xc0, 0x00};
    w4_eth_tx_t tx;
    w4_ecdsa_sig_t sig;
    CHECK(w4_eth_1559_decode(junk, 2, &tx, &sig) == W4_ERR_PARSE &&
              w4_eth_1559_decode(junk, 3, &tx, &sig) == W4_ERR_PARSE,
          "EIP-1559 decode refuses empty list and trailing bytes");
    int bad = 0;
    uint8_t base[512];
    w4_rlp_item_t it;
    int wl = unhex(EIP1559_TX[0].signed_rlp, base, sizeof base);
    w4_rlp_decode(base, (uint32_t) wl, &it);
    for (int i = 0; i < 5000; i++) {
        uint8_t m[512];
        memcpy(m, it.p, it.len);
        uint32_t L = it.len;
        int kind = (int) (next() % 3);
        if (kind == 0)
            m[next() % L] ^= (uint8_t) (1u << (next() % 8));
        else if (kind == 1)
            L = (uint32_t) (next() % L);
        else
            m[1 + next() % (L - 1)] = (uint8_t) next();
        if (w4_eth_1559_decode(m, L, &tx, &sig) == W4_OK) {
            if (tx.data_len && (tx.data < m || tx.data + tx.data_len > m + L)) bad++;
        }
    }
    CHECK(bad == 0, "EIP-1559 decode fuzz: 5000 mutations stay in bounds");
}

static void test_abi_rpc(void)
{
    uint8_t sel[4];
    w4_abi_selector("transfer(address,uint256)", sel);
    CHECK(hex_is(sel, 4, "a9059cbb"), "ABI selector transfer(address,uint256)");
    w4_abi_selector("balanceOf(address)", sel);
    CHECK(hex_is(sel, 4, "70a08231"), "ABI selector balanceOf(address)");
    uint8_t to[20], out[68];
    memset(to, 0x11, 20);
    w4_u256 amt;
    w4_u256_from_dec(&amt, "1000000", 7);
    w4_abi_erc20_transfer(to, &amt, out);
    CHECK(hex_is(out, 68,
                 "a9059cbb0000000000000000000000001111111111111111111111111111111111111111"
                 "00000000000000000000000000000000000000000000000000000000000f4240"),
          "ABI transfer(to, 1000000) encoding");
    char js[512];
    int32_t n = w4_rpc_eth_get_balance(js, sizeof js, 7, to, NULL);
    CHECK(n > 0 && strcmp(js, "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"eth_getBalance\","
                              "\"params\":[\"0x1111111111111111111111111111111111111111\","
                              "\"latest\"]}") == 0,
          "JSON-RPC eth_getBalance request");
    uint8_t bo[36];
    w4_abi_erc20_balance_of(to, bo);
    n = w4_rpc_eth_call(js, sizeof js, 8, to, bo, 36, "0x10");
    CHECK(n > 0 && strstr(js, "\"data\":\"0x70a08231") && strstr(js, "\"0x10\"]"),
          "JSON-RPC eth_call request");
    CHECK(w4_rpc_eth_call(js, sizeof js, 8, to, bo, 36, "0x010") == W4_ERR_ARG,
          "JSON-RPC refuses a non-canonical block number");
    w4_rpc_result_t r;
    const char *resp = "{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":\"0xde0b6b3a7640000\"}";
    w4_u256 v;
    char dec[100];
    w4_w w;
    w4_w_init(&w, dec, sizeof dec);
    CHECK(w4_rpc_parse(resp, (uint32_t) strlen(resp), &r) == W4_OK && r.id == 7 &&
              w4_rpc_result_qty(&r, &v) == W4_OK && (w4_w_u256_dec(&w, &v), w4_w_cstr(&w) > 0) &&
              strcmp(dec, "1000000000000000000") == 0,
          "JSON-RPC balance result: 1 ether in wei, exactly");
    const char *er = "{\"jsonrpc\":\"2.0\",\"id\":9,\"error\":{\"code\":-32000,\"message\":\"nonce "
                     "too low\"}}";
    CHECK(w4_rpc_parse(er, (uint32_t) strlen(er), &r) == W4_OK && r.is_error &&
              r.err_code == -32000 && strcmp(r.err_msg, "nonce too low") == 0,
          "JSON-RPC error object");
    const char *both = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0x1\",\"error\":{\"code\":1}}";
    CHECK(w4_rpc_parse(both, (uint32_t) strlen(both), &r) == W4_ERR_PARSE,
          "JSON-RPC with both result and error refused");
    uint8_t ph[32];
    w4_eth_personal_hash((const uint8_t *) "hello", 5, ph);
    uint8_t ref[64];
    memcpy(ref,
           "\x19"
           "Ethereum Signed Message:\n5hello",
           32);
    uint8_t rh[32];
    w4_keccak256(ref, 32, rh);
    CHECK(memcmp(ph, rh, 32) == 0, "EIP-191 personal message prefix");
    /* u256 arithmetic and exact rescale */
    w4_u256 a, b;
    w4_u256_from_dec(&a, "123450000", 9);
    CHECK(w4_u256_rescale(&b, &a, 6, 2) == W4_OK && w4_u256_to_u64(&b, &(uint64_t){0}),
          "rescale 6 -> 2 decimals exact");
    w4_u256_from_dec(&a, "123456789", 9);
    CHECK(w4_u256_rescale(&b, &a, 6, 2) == W4_ERR_RANGE, "rescale refuses to drop digits");
    CHECK(!w4_u256_from_dec(&a, "0123", 4) && !w4_u256_from_dec(&a, "", 0) &&
              !w4_u256_from_dec(&a,
                                "1157920892373161954235709850086879078532699846656405640394575840"
                                "07913129639936",
                                78),
          "u256 decimal parse: leading zero, empty, 2^256 refused");
}

static void test_cid(void)
{
    ipfsn_cid_t c, c2;
    const char *msg = "web4 content";
    CHECK(w4_cid_of((const uint8_t *) msg, (uint32_t) strlen(msg), &c) == W4_OK && c.version == 1,
          "CIDv1 raw of content");
    uint8_t d[32];
    uint32_t codec;
    CHECK(w4_cid_to_bytes32(&c, d, &codec) == W4_OK && codec == IPFSN_MC_RAW &&
              w4_cid_from_bytes32(d, codec, &c2) == W4_OK && ipfsn_cid_equal(&c, &c2),
          "CID -> bytes32 -> CID round trip");
    char uri[200], gw[200];
    CHECK(w4_cid_uri(&c, uri, sizeof uri) > 0 &&
              w4_cid_uri_parse(uri, (uint32_t) strlen(uri), &c2) == W4_OK &&
              ipfsn_cid_equal(&c, &c2),
          "ipfs:// URI round trip");
    CHECK(w4_cid_gateway_url(&c, "gw.example", gw, sizeof gw) > 0 &&
              strncmp(gw, "https://gw.example/ipfs/b", 25) == 0 &&
              w4_cid_gateway_url(&c, "evil.example/x@", gw, sizeof gw) == W4_ERR_ARG,
          "gateway URL built, injection refused");
    CHECK(w4_cid_uri_parse("ipfs://Qmnot-a-cid", 18, &c2) == W4_ERR_PARSE &&
              w4_cid_uri_parse("http://x", 8, &c2) == W4_ERR_PARSE,
          "ipfs:// URI parse refuses junk");
    /* the empty input has a published CID:
     * bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku */
    CHECK(w4_cid_of((const uint8_t *) "", 0, &c) == W4_OK &&
              ipfsn_cid_to_string(&c, uri, sizeof uri) > 0 &&
              strcmp(uri, "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku") == 0,
          "CIDv1 raw of the empty input (well-known)");
}

int main(void)
{
    printf("=== web4 Web 3 side ===\n");
    test_keccak();
    test_rlp();
    test_eip155();
    test_secp_misc();
    test_ripemd();
    test_bech32();
    test_eip55();
    test_eip1559();
    test_abi_rpc();
    test_cid();
    printf("web4_web3: %d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}

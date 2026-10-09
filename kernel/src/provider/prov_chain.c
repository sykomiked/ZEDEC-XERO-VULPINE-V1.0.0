/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_chain.c — chain adapters (see prov_chain.h). Primitives come from
 * kernel/src/web4, kernel/src/ipfs_node and kernel/src/robin_debanks. */
#include "prov_chain.h"
#include "../web4/web4_web3.h"
#include "../robin_debanks/sha256.h"
#include "../robin_debanks/ed25519_verify.h"

static const char HEX[] = "0123456789abcdef";

const char *prov_chain_family_name(uint8_t f)
{
    switch (f) {
    case PROV_CHAIN_EVM:
        return "evm";
    case PROV_CHAIN_UTXO:
        return "utxo";
    case PROV_CHAIN_ED25519:
        return "ed25519";
    case PROV_CHAIN_COSMOS:
        return "cosmos";
    case PROV_CHAIN_SUBSTRATE:
        return "substrate-external";
    case PROV_CHAIN_OPAQUE:
        return "opaque-external";
    default:
        return "none";
    }
}

bool prov_chain_native_verify(uint8_t f)
{
    return f == PROV_CHAIN_EVM || f == PROV_CHAIN_UTXO || f == PROV_CHAIN_ED25519 ||
           f == PROV_CHAIN_COSMOS;
}

/* ===== Merkle anchor ===== */

static void leaf_hash(const uint8_t d[32], uint8_t out[32])
{
    uint8_t b[33];
    b[0] = 0x00;
    prov_memcpy(b + 1, d, 32);
    prov_sha3(b, sizeof b, out);
}

static void node_hash(const uint8_t l[32], const uint8_t r[32], uint8_t out[32])
{
    uint8_t b[65];
    b[0] = 0x01;
    prov_memcpy(b + 1, l, 32);
    prov_memcpy(b + 33, r, 32);
    prov_sha3(b, sizeof b, out);
}

#define ANCHOR_MAX_LEAVES 1024u
static uint8_t g_level[ANCHOR_MAX_LEAVES][PROV_HASH_LEN]; /* not reentrant */

int prov_anchor_root(const uint8_t (*leaves)[PROV_HASH_LEN], uint32_t n,
                     uint8_t root[PROV_HASH_LEN])
{
    if (!leaves || !root || n == 0 || n > ANCHOR_MAX_LEAVES) return PROV_ERR_ARG;
    for (uint32_t i = 0; i < n; i++) leaf_hash(leaves[i], g_level[i]);
    while (n > 1) {
        uint32_t m = 0;
        for (uint32_t i = 0; i < n; i += 2, m++) {
            if (i + 1 < n)
                node_hash(g_level[i], g_level[i + 1], g_level[m]);
            else
                prov_memcpy(g_level[m], g_level[i], 32); /* promote */
        }
        n = m;
    }
    prov_memcpy(root, g_level[0], 32);
    return PROV_OK;
}

int prov_anchor_proof(const uint8_t (*leaves)[PROV_HASH_LEN], uint32_t n, uint32_t index,
                      uint8_t (*path)[PROV_HASH_LEN], uint32_t *dirs)
{
    if (!leaves || !path || !dirs || n == 0 || n > ANCHOR_MAX_LEAVES || index >= n)
        return PROV_ERR_ARG;
    uint32_t depth = 0;
    *dirs = 0;
    for (uint32_t i = 0; i < n; i++) leaf_hash(leaves[i], g_level[i]);
    while (n > 1) {
        uint32_t sib = index ^ 1u;
        if (sib < n) {
            if (depth >= PROV_ANCHOR_MAX_DEPTH) return PROV_ERR_OVERFLOW;
            prov_memcpy(path[depth], g_level[sib], 32);
            if (sib < index) *dirs |= 1u << depth;
            depth++;
        }
        uint32_t m = 0;
        for (uint32_t i = 0; i < n; i += 2, m++) {
            if (i + 1 < n)
                node_hash(g_level[i], g_level[i + 1], g_level[m]);
            else
                prov_memcpy(g_level[m], g_level[i], 32);
        }
        n = m;
        index >>= 1;
    }
    return (int) depth;
}

bool prov_anchor_verify(const uint8_t leaf[PROV_HASH_LEN], const uint8_t (*path)[PROV_HASH_LEN],
                        uint32_t depth, uint32_t dirs, const uint8_t root[PROV_HASH_LEN])
{
    uint8_t h[32];
    if (!leaf || !root || depth > PROV_ANCHOR_MAX_DEPTH || (depth && !path)) return false;
    leaf_hash(leaf, h);
    for (uint32_t i = 0; i < depth; i++) {
        if (dirs & (1u << i))
            node_hash(path[i], h, h);
        else
            node_hash(h, path[i], h);
    }
    return prov_ct_eq(h, root, 32);
}

int32_t prov_chain_anchor_payload(const prov_chain_t *c, const uint8_t root[PROV_HASH_LEN],
                                  uint8_t *out, uint32_t cap)
{
    if (!c || !root || !out) return PROV_ERR_ARG;
    switch (c->family) {
    case PROV_CHAIN_EVM:
        if (cap < 36) return PROV_ERR_SPACE;
        w4_abi_selector("anchor(bytes32)", out);
        prov_memcpy(out + 4, root, 32);
        return 36;
    case PROV_CHAIN_UTXO:
        if (cap < 34) return PROV_ERR_SPACE;
        out[0] = 0x6a; /* OP_RETURN */
        out[1] = 0x20; /* push 32 */
        prov_memcpy(out + 2, root, 32);
        return 34;
    case PROV_CHAIN_ED25519:
    case PROV_CHAIN_COSMOS:
        if (cap < 69) return PROV_ERR_SPACE;
        prov_memcpy(out, "zxv1:", 5);
        for (uint32_t i = 0; i < 32; i++) {
            out[5 + 2 * i] = (uint8_t) HEX[root[i] >> 4];
            out[6 + 2 * i] = (uint8_t) HEX[root[i] & 15];
        }
        return 69;
    case PROV_CHAIN_SUBSTRATE:
    case PROV_CHAIN_OPAQUE:
        if (cap < 36) return PROV_ERR_SPACE;
        prov_memcpy(out, "zxv1", 4);
        prov_memcpy(out + 4, root, 32);
        return 36;
    default:
        return PROV_ERR_ARG;
    }
}

int32_t prov_evm_anchor_tx(const prov_chain_t *c, uint64_t nonce, uint64_t gas_limit,
                           uint64_t max_priority_wei, uint64_t max_fee_wei,
                           const uint8_t contract[20], const uint8_t root[PROV_HASH_LEN],
                           uint8_t *out, uint32_t cap)
{
    uint8_t data[36];
    if (!c || c->family != PROV_CHAIN_EVM || c->evm_chain_id == 0 || !contract || !root || !out)
        return PROV_ERR_ARG;
    if (prov_chain_anchor_payload(c, root, data, sizeof data) != 36) return PROV_ERR_ARG;
    w4_eth_tx_t tx;
    prov_memset(&tx, 0, sizeof tx);
    tx.chain_id = c->evm_chain_id;
    tx.nonce = nonce;
    w4_u256_from_u64(&tx.max_priority, max_priority_wei);
    w4_u256_from_u64(&tx.max_fee, max_fee_wei);
    tx.gas_limit = gas_limit;
    tx.has_to = true;
    prov_memcpy(tx.to, contract, 20);
    tx.data = data;
    tx.data_len = sizeof data;
    int32_t n = w4_eth_1559_unsigned(&tx, out, cap);
    return n < 0 ? PROV_ERR_SPACE : n;
}

int prov_chain_submit(const prov_chain_t *c, const uint8_t *payload, uint32_t len)
{
    if (!c || !payload || len == 0) return PROV_ERR_ARG;
    if (!c->submit) return PROV_ERR_HOOK;
    return c->submit(c->submit_ctx, c->family, payload, len) == 0 ? PROV_OK : PROV_ERR_SETTLE;
}

/* ===== EIP-712 ===== */

void prov_eip712_type_hash(const char *type, uint8_t out[32])
{
    w4_keccak256((const uint8_t *) type, prov_strnlen(type, 4096), out);
}

static void word_u64(uint8_t w[32], uint64_t v)
{
    prov_memset(w, 0, 32);
    for (int i = 0; i < 8; i++) w[31 - i] = (uint8_t) (v >> (8 * i));
}

void prov_eip712_domain(const char *name, const char *version, uint64_t chain_id,
                        const uint8_t *contract20, uint8_t out[32])
{
    uint8_t words[4][32], th[32];
    prov_eip712_type_hash(contract20 ? "EIP712Domain(string name,string version,uint256 chainId,"
                                       "address verifyingContract)"
                                     : "EIP712Domain(string name,string version,uint256 chainId)",
                          th);
    w4_keccak256((const uint8_t *) name, prov_strnlen(name, 4096), words[0]);
    w4_keccak256((const uint8_t *) version, prov_strnlen(version, 4096), words[1]);
    word_u64(words[2], chain_id);
    if (contract20) {
        prov_memset(words[3], 0, 12);
        prov_memcpy(words[3] + 12, contract20, 20);
    }
    prov_eip712_hash_struct(th, (const uint8_t(*)[32]) words, contract20 ? 4u : 3u, out);
}

void prov_eip712_hash_struct(const uint8_t typehash[32], const uint8_t (*words)[32], uint32_t n,
                             uint8_t out[32])
{
    uint8_t buf[32 * 17];
    if (n > 16) n = 16;
    prov_memcpy(buf, typehash, 32);
    for (uint32_t i = 0; i < n; i++) prov_memcpy(buf + 32 * (i + 1), words[i], 32);
    w4_keccak256(buf, 32u * (n + 1u), out);
}

void prov_eip712_digest(const uint8_t domain[32], const uint8_t struct_hash[32], uint8_t out[32])
{
    uint8_t b[66];
    b[0] = 0x19;
    b[1] = 0x01;
    prov_memcpy(b + 2, domain, 32);
    prov_memcpy(b + 34, struct_hash, 32);
    w4_keccak256(b, sizeof b, out);
}

void prov_eip712_receipt(const prov_chain_t *c, const prov_receipt_t *r, uint8_t out[32])
{
    uint8_t words[10][32], th[32], sh[32], dom[32];
    prov_receipt_digest(r, words[0]);
    prov_memcpy(words[1], r->job_id, 32);
    prov_memcpy(words[2], r->provider_id, 32);
    prov_memcpy(words[3], r->user_id, 32);
    word_u64(words[4], r->units);
    word_u64(words[5], r->unit_price);
    word_u64(words[6], r->gross);
    word_u64(words[7], r->fee);
    word_u64(words[8], r->net);
    w4_keccak256((const uint8_t *) r->asset_code, prov_strnlen(r->asset_code, PROV_CODE_MAX),
                 words[9]);
    prov_eip712_type_hash(PROV_EIP712_RECEIPT_TYPE, th);
    prov_eip712_hash_struct(th, (const uint8_t(*)[32]) words, 10, sh);
    prov_eip712_domain("ZXV Provider Receipt", "1", c->evm_chain_id,
                       c->has_contract ? c->evm_contract : 0, dom);
    prov_eip712_digest(dom, sh, out);
}

/* ===== Bitcoin signed message ===== */

void prov_btc_message_hash(const uint8_t *msg, uint32_t len, uint8_t out[32])
{
    static const char magic[] = "Bitcoin Signed Message:\n";
    sha256_ctx_t c;
    uint8_t v[5], h[32];
    uint32_t vl;
    sha256_init(&c);
    v[0] = (uint8_t) (sizeof magic - 1);
    sha256_update(&c, v, 1);
    sha256_update(&c, (const uint8_t *) magic, sizeof magic - 1);
    if (len < 0xfd) {
        v[0] = (uint8_t) len;
        vl = 1;
    } else if (len <= 0xffff) {
        v[0] = 0xfd;
        v[1] = (uint8_t) len;
        v[2] = (uint8_t) (len >> 8);
        vl = 3;
    } else {
        v[0] = 0xfe;
        prov_le32_put(v + 1, len);
        vl = 5;
    }
    sha256_update(&c, v, vl);
    if (len) sha256_update(&c, msg, len);
    sha256_final(&c, h);
    sha256(h, 32, out);
}

/* ===== Cosmos SignDoc ===== */

typedef struct {
    uint8_t *b;
    uint32_t cap, len;
    bool err;
} pbuf_t;

static void pb_byte(pbuf_t *p, uint8_t v)
{
    if (p->len >= p->cap)
        p->err = true;
    else
        p->b[p->len++] = v;
}

static void pb_varint(pbuf_t *p, uint64_t v)
{
    while (v >= 0x80) {
        pb_byte(p, (uint8_t) (v | 0x80));
        v >>= 7;
    }
    pb_byte(p, (uint8_t) v);
}

static void pb_bytes(pbuf_t *p, uint32_t field, const uint8_t *d, uint32_t n)
{
    if (n == 0) return; /* proto3 default omitted */
    pb_varint(p, ((uint64_t) field << 3) | 2u);
    pb_varint(p, n);
    if (n > p->cap - p->len || p->err) {
        p->err = true;
        return;
    }
    prov_memcpy(p->b + p->len, d, n);
    p->len += n;
}

int32_t prov_cosmos_sign_doc(const uint8_t *body, uint32_t body_len, const uint8_t *auth,
                             uint32_t auth_len, const char *chain_id, uint64_t account_number,
                             uint8_t *out, uint32_t cap)
{
    if (!out || (body_len && !body) || (auth_len && !auth) || !chain_id) return PROV_ERR_ARG;
    pbuf_t p = {out, cap, 0, false};
    pb_bytes(&p, 1, body, body_len);
    pb_bytes(&p, 2, auth, auth_len);
    pb_bytes(&p, 3, (const uint8_t *) chain_id, (uint32_t) prov_strnlen(chain_id, 256));
    if (account_number) {
        pb_varint(&p, (4u << 3) | 0u);
        pb_varint(&p, account_number);
    }
    return p.err ? PROV_ERR_SPACE : (int32_t) p.len;
}

/* ===== Receipt binding ===== */

void prov_chain_receipt_text(const prov_receipt_t *r, uint8_t out[PROV_CHAIN_MSG_LEN])
{
    uint8_t d[32];
    prov_receipt_digest(r, d);
    prov_memcpy(out, "zxv-prov-receipt:", 17);
    for (uint32_t i = 0; i < 32; i++) {
        out[17 + 2 * i] = (uint8_t) HEX[d[i] >> 4];
        out[18 + 2 * i] = (uint8_t) HEX[d[i] & 15];
    }
}

/* Recover the signer of hash and check low-s; out pub64. */
static bool recover_low_s(const uint8_t hash[32], const uint8_t rs[64], uint8_t recid,
                          uint8_t pub64[64])
{
    w4_ecdsa_sig_t s;
    prov_memcpy(s.r, rs, 32);
    prov_memcpy(s.s, rs + 32, 32);
    s.recid = recid;
    if (w4_secp_recover(hash, &s, pub64) != W4_OK) return false;
    return w4_secp_verify(pub64, hash, &s, true) == W4_OK;
}

int prov_chain_verify_receipt(const prov_chain_t *c, const prov_receipt_t *r, const uint8_t *pub,
                              uint32_t pub_len, const uint8_t *sig, uint32_t sig_len)
{
    uint8_t text[PROV_CHAIN_MSG_LEN], h[32], pub64[64];
    if (!c || !r || !pub || !sig) return PROV_ERR_ARG;
    switch (c->family) {
    case PROV_CHAIN_EVM: {
        if (pub_len != 20 || sig_len != 65) return PROV_ERR_ARG;
        uint8_t v = sig[64], addr[20];
        if (v >= 27) v = (uint8_t) (v - 27);
        if (v > 1) return PROV_ERR_AUTH;
        prov_eip712_receipt(c, r, h);
        if (!recover_low_s(h, sig, v, pub64)) return PROV_ERR_AUTH;
        w4_eth_address(pub64, addr);
        return prov_ct_eq(addr, pub, 20) ? PROV_OK : PROV_ERR_AUTH;
    }
    case PROV_CHAIN_UTXO: {
        if (pub_len != 20 || sig_len != 65) return PROV_ERR_ARG;
        uint8_t hdr = sig[0], kh[20], key[65];
        if (hdr < 27 || hdr > 34) return PROV_ERR_AUTH;
        bool compressed = hdr >= 31;
        uint8_t recid = (uint8_t) ((hdr - 27) & 3);
        prov_chain_receipt_text(r, text);
        prov_btc_message_hash(text, sizeof text, h);
        if (!recover_low_s(h, sig + 1, recid, pub64)) return PROV_ERR_AUTH;
        if (compressed) {
            w4_secp_compress(pub64, key);
            w4_hash160(key, 33, kh);
        } else {
            key[0] = 0x04;
            prov_memcpy(key + 1, pub64, 64);
            w4_hash160(key, 65, kh);
        }
        return prov_ct_eq(kh, pub, 20) ? PROV_OK : PROV_ERR_AUTH;
    }
    case PROV_CHAIN_ED25519:
        if (pub_len != 32 || sig_len != 64) return PROV_ERR_ARG;
        prov_chain_receipt_text(r, text);
        return ed25519_verify(text, sizeof text, sig, pub) ? PROV_OK : PROV_ERR_AUTH;
    case PROV_CHAIN_COSMOS: {
        if (pub_len != 33 || sig_len != 64 || (pub[0] != 2 && pub[0] != 3)) return PROV_ERR_ARG;
        prov_chain_receipt_text(r, text);
        sha256(text, sizeof text, h);
        for (uint8_t id = 0; id < 4; id++) {
            uint8_t cmp[33];
            if (!recover_low_s(h, sig, id, pub64)) continue;
            w4_secp_compress(pub64, cmp);
            if (prov_ct_eq(cmp, pub, 33)) return PROV_OK;
        }
        return PROV_ERR_AUTH;
    }
    case PROV_CHAIN_SUBSTRATE:
    case PROV_CHAIN_OPAQUE:
        if (!c->ext_verify) return PROV_ERR_UNSUPPORTED;
        prov_chain_receipt_text(r, text);
        return c->ext_verify(c->ext_ctx, pub, pub_len, text, sizeof text, sig, sig_len)
                   ? PROV_OK
                   : PROV_ERR_AUTH;
    default:
        return PROV_ERR_ARG;
    }
}

/* ===== Addresses ===== */

static int convert_bits(const uint8_t *in, uint32_t n, uint32_t from, uint32_t to, bool pad,
                        uint8_t *out, uint32_t cap, uint32_t *outn)
{
    uint32_t acc = 0, bits = 0, k = 0, maxv = (1u << to) - 1u;
    for (uint32_t i = 0; i < n; i++) {
        if (in[i] >> from) return PROV_ERR_PARSE;
        acc = (acc << from) | in[i];
        bits += from;
        while (bits >= to) {
            bits -= to;
            if (k >= cap) return PROV_ERR_SPACE;
            out[k++] = (uint8_t) ((acc >> bits) & maxv);
        }
    }
    if (pad) {
        if (bits) {
            if (k >= cap) return PROV_ERR_SPACE;
            out[k++] = (uint8_t) ((acc << (to - bits)) & maxv);
        }
    } else if (bits >= from || ((acc << (to - bits)) & maxv)) {
        return PROV_ERR_PARSE;
    }
    *outn = k;
    return PROV_OK;
}

int32_t prov_chain_address(const prov_chain_t *c, const uint8_t *pub, uint32_t pub_len, char *out,
                           uint32_t cap)
{
    if (!c || !pub || !out) return PROV_ERR_ARG;
    switch (c->family) {
    case PROV_CHAIN_EVM: {
        uint8_t a[20];
        if (pub_len != 64) return PROV_ERR_ARG;
        if (cap < 43) return PROV_ERR_SPACE;
        w4_eth_address(pub, a);
        w4_eth_checksum(a, out);
        return 42;
    }
    case PROV_CHAIN_UTXO: {
        if (pub_len != 33) return PROV_ERR_ARG;
        int32_t n = w4_btc_p2wpkh(c->hrp, pub, out, cap);
        return n < 0 ? PROV_ERR_ARG : n;
    }
    case PROV_CHAIN_ED25519: {
        if (pub_len != 32) return PROV_ERR_ARG;
        int n = ipfsn_base58_encode(pub, 32, out, cap);
        return n < 0 ? PROV_ERR_SPACE : n;
    }
    case PROV_CHAIN_COSMOS: {
        uint8_t kh[20], d5[40];
        uint32_t n5 = 0;
        if (pub_len != 33 || (pub[0] != 2 && pub[0] != 3)) return PROV_ERR_ARG;
        w4_hash160(pub, 33, kh);
        if (convert_bits(kh, 20, 8, 5, true, d5, sizeof d5, &n5) != PROV_OK) return PROV_ERR_ARG;
        int32_t n = w4_bech32_encode(W4_BECH32, c->hrp, d5, n5, out, cap);
        return n < 0 ? PROV_ERR_ARG : n;
    }
    default:
        return PROV_ERR_UNSUPPORTED;
    }
}

int32_t prov_chain_address_parse(const prov_chain_t *c, const char *s, uint32_t len, uint8_t *out,
                                 uint32_t cap)
{
    if (!c || !s || !out) return PROV_ERR_ARG;
    switch (c->family) {
    case PROV_CHAIN_EVM: {
        bool checked = false;
        if (cap < 20) return PROV_ERR_SPACE;
        return w4_eth_address_parse(s, len, out, &checked) == W4_OK ? 20 : PROV_ERR_PARSE;
    }
    case PROV_CHAIN_UTXO: {
        uint8_t ver, prog[40];
        uint32_t pl = 0;
        if (w4_segwit_decode(c->hrp, s, len, &ver, prog, &pl) != W4_OK) return PROV_ERR_PARSE;
        if (pl > cap) return PROV_ERR_SPACE;
        prov_memcpy(out, prog, pl);
        return (int32_t) pl;
    }
    case PROV_CHAIN_ED25519: {
        uint8_t k[64];
        int n = ipfsn_base58_decode(s, len, k, sizeof k);
        if (n != 32) return PROV_ERR_PARSE;
        if (cap < 32) return PROV_ERR_SPACE;
        prov_memcpy(out, k, 32);
        return 32;
    }
    case PROV_CHAIN_COSMOS: {
        char hrp[84];
        uint8_t d5[90], b[64];
        uint32_t n5 = 0, nb = 0;
        if (w4_bech32_decode(s, len, hrp, sizeof hrp, d5, &n5, sizeof d5) != W4_BECH32)
            return PROV_ERR_PARSE;
        if (!prov_streq(hrp, c->hrp)) return PROV_ERR_PARSE;
        if (convert_bits(d5, n5, 5, 8, false, b, sizeof b, &nb) != PROV_OK) return PROV_ERR_PARSE;
        if (nb != 20 && nb != 32) return PROV_ERR_PARSE;
        if (nb > cap) return PROV_ERR_SPACE;
        prov_memcpy(out, b, nb);
        return (int32_t) nb;
    }
    default:
        return PROV_ERR_UNSUPPORTED;
    }
}

/* ===== Asset conversion ===== */

static bool u256_mul_u64(w4_u256 *r, const w4_u256 *a, uint64_t m)
{
    w4_u256 lo, hi;
    if (!w4_u256_mul_u32(&lo, a, (uint32_t) m)) return false;
    if (!w4_u256_mul_u32(&hi, a, (uint32_t) (m >> 32))) return false;
    if (hi.w[7]) return false; /* the shift by 32 would overflow */
    for (int i = 7; i > 0; i--) hi.w[i] = hi.w[i - 1];
    hi.w[0] = 0;
    return w4_u256_add(r, &lo, &hi);
}

/* r = floor(a / d) for a 64-bit d != 0 (shift-subtract, 65-bit remainder). */
static void u256_div_u64(w4_u256 *q, const w4_u256 *a, uint64_t d)
{
    uint64_t rem = 0;
    w4_u256_zero(q);
    for (int i = 255; i >= 0; i--) {
        uint64_t carry = rem >> 63;
        rem = (rem << 1) | ((a->w[i >> 5] >> (i & 31)) & 1u);
        if (carry || rem >= d) {
            rem -= d;
            q->w[i >> 5] |= 1u << (i & 31);
        }
    }
}

int prov_chain_to_quote(const uint8_t *amount_be, uint32_t amount_len, uint8_t decimals,
                        uint64_t price, uint64_t *quote)
{
    w4_u256 a, p, q;
    uint32_t rem;
    if (!amount_be || !quote || amount_len > 32 || decimals > 36) return PROV_ERR_ARG;
    if (!w4_u256_from_be(&a, amount_be, amount_len)) return PROV_ERR_ARG;
    if (!u256_mul_u64(&p, &a, price)) return PROV_ERR_OVERFLOW;
    for (uint32_t i = 0; i < decimals; i++) {
        w4_u256_divmod_u32(&q, &p, 10u, &rem);
        p = q;
    }
    return w4_u256_to_u64(&p, quote) ? PROV_OK : PROV_ERR_OVERFLOW;
}

int prov_quote_to_chain(uint64_t quote, uint8_t decimals, uint64_t price, uint8_t out_be[32])
{
    w4_u256 a, t, q;
    if (!out_be || price == 0 || decimals > 36) return PROV_ERR_ARG;
    w4_u256_from_u64(&a, quote);
    for (uint32_t i = 0; i < decimals; i++) {
        if (!w4_u256_mul_u32(&t, &a, 10u)) return PROV_ERR_OVERFLOW;
        a = t;
    }
    u256_div_u64(&q, &a, price);
    w4_u256_to_be(&q, out_be);
    return PROV_OK;
}

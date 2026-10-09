/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_web3.h — the Web 3 side of Web 4: what Web 3 is good at (verifiable
 * ownership, settlement, content addressing), as bounded integer codecs.
 *
 *   Ethereum   RLP (canonical, strict decode), legacy/EIP-155 and EIP-1559
 *              transactions with their Keccak-256 signing hashes, address from
 *              a public key, EIP-55 checksums, ABI for ERC-20 transfer and
 *              balanceOf, JSON-RPC builders (eth_call, eth_sendRawTransaction,
 *              eth_getBalance, eth_getTransactionCount) and a result parser,
 *              EIP-191 personal-message hashing.
 *   secp256k1  in-module ECDSA: RFC 6979 deterministic nonces (HMAC-SHA256),
 *              low-s (EIP-2), recovery id, verify and public-key recovery.
 *              See web4_secp256k1.c for the constant-time notes.
 *   Bitcoin    RIPEMD-160 / HASH160, base58check, bech32 (BIP-173) and
 *              bech32m (BIP-350), segwit addresses, P2PKH and P2WPKH from a
 *              public key.
 *   IPFS       CIDv1 to/from the Ethereum-friendly bytes32 digest form and
 *              ipfs:// URIs, on top of kernel/src/ipfs_node.
 *
 * HONEST LIMITS.
 *  - secp256k1 ECDSA is CLASSICAL cryptography and is broken by a large
 *    quantum computer. It exists here only because Ethereum and Bitcoin
 *    require it at their edge. Every Web 4 record that bridges into Web 3
 *    (link records, payment intents, content maps; see web4_bridge.h) is also
 *    signed with ML-DSA-65, so the platform side does not rest on it.
 *  - No networking: JSON-RPC requests are built and responses parsed; the
 *    host carries them. Nothing here has been run against a live node.
 *  - EIP-1559 access lists are encoded empty only; a non-empty access list,
 *    EIP-2930, EIP-4844 blob and EIP-7702 transactions are not supported.
 *  - Bitcoin: addresses only. No transaction building, no script engine.
 *  - The secp256k1 code has not been audited and is not claimed to be free of
 *    timing side channels on every CPU (see the .c file).
 */
#ifndef ZXV_WEB4_WEB3_H
#define ZXV_WEB4_WEB3_H

#include "web4_util.h"
#include "web4_web2.h"
#include "../ipfs_node/ipfs_node.h"

/* ======================================================================
 * RLP
 * ====================================================================== */
#define W4_RLP_DEPTH 16u

typedef struct {
    uint8_t *buf;
    uint32_t cap, len;
    uint32_t stack[W4_RLP_DEPTH]; /* payload start of each open list */
    uint8_t depth;
    bool err;
} w4_rlp_enc_t;

void w4_rlp_init(w4_rlp_enc_t *e, uint8_t *buf, uint32_t cap);
void w4_rlp_bytes(w4_rlp_enc_t *e, const uint8_t *p, uint32_t n);
void w4_rlp_str(w4_rlp_enc_t *e, const char *s);
void w4_rlp_u64(w4_rlp_enc_t *e, uint64_t v); /* minimal big-endian, 0 -> 0x80 */
void w4_rlp_u256(w4_rlp_enc_t *e, const w4_u256 *v);
void w4_rlp_list_begin(w4_rlp_enc_t *e);
void w4_rlp_list_end(w4_rlp_enc_t *e);
/* Bytes written, or W4_ERR_SPACE / W4_ERR_STATE (unbalanced lists). */
int32_t w4_rlp_finish(const w4_rlp_enc_t *e);

typedef struct {
    bool is_list;
    const uint8_t *p; /* payload */
    uint32_t len;     /* payload length */
    uint32_t total;   /* header + payload */
} w4_rlp_item_t;

/* Decode the item at the start of buf. Canonical only: a single byte < 0x80
 * must be itself, short forms for < 56 bytes, no leading zeros in lengths.
 * Returns W4_OK or W4_ERR_PARSE. */
int w4_rlp_decode(const uint8_t *buf, uint32_t len, w4_rlp_item_t *it);
/* Iterate a list: *off starts at 0; returns W4_OK and the next element,
 * W4_ERR_NOTFOUND at the end, W4_ERR_PARSE on malformed content. */
int w4_rlp_next(const w4_rlp_item_t *list, uint32_t *off, w4_rlp_item_t *it);
/* Scalar from a string item (no leading zeros, <= 8 / 32 bytes). */
int w4_rlp_as_u64(const w4_rlp_item_t *it, uint64_t *v);
int w4_rlp_as_u256(const w4_rlp_item_t *it, w4_u256 *v);

/* ======================================================================
 * Ethereum
 * ====================================================================== */
#define W4_ETH_ADDR_LEN 20u
#define W4_ETH_DATA_MAX 4096u

typedef struct {
    uint64_t chain_id; /* 0 = pre-EIP-155 legacy (no replay protection) */
    uint64_t nonce;
    w4_u256 gas_price;    /* legacy */
    w4_u256 max_priority; /* EIP-1559 maxPriorityFeePerGas */
    w4_u256 max_fee;      /* EIP-1559 maxFeePerGas */
    uint64_t gas_limit;
    bool has_to; /* false: contract creation */
    uint8_t to[W4_ETH_ADDR_LEN];
    w4_u256 value; /* wei */
    const uint8_t *data;
    uint32_t data_len;
} w4_eth_tx_t;

typedef struct {
    uint8_t r[32], s[32];
    uint8_t recid; /* 0..3: bit 0 = y parity, bit 1 = r overflowed n */
} w4_ecdsa_sig_t;

/* Payload whose Keccak-256 is signed. Legacy: RLP([nonce, gasPrice, gas, to,
 * value, data, chainId, 0, 0]) (EIP-155) or the 6-field list when chain_id
 * is 0. EIP-1559: 0x02 || RLP([chainId, nonce, maxPriority, maxFee, gas, to,
 * value, data, []]). Returns bytes or negative. */
int32_t w4_eth_legacy_unsigned(const w4_eth_tx_t *tx, uint8_t *out, uint32_t cap);
int32_t w4_eth_1559_unsigned(const w4_eth_tx_t *tx, uint8_t *out, uint32_t cap);
void w4_eth_signing_hash(const uint8_t *payload, uint32_t len, uint8_t hash[32]);
/* Signed raw transactions (the bytes eth_sendRawTransaction takes). Legacy
 * v = recid + 35 + 2*chainId (EIP-155) or recid + 27. EIP-1559 yParity. */
int32_t w4_eth_legacy_signed(const w4_eth_tx_t *tx, const w4_ecdsa_sig_t *sig, uint8_t *out,
                             uint32_t cap);
int32_t w4_eth_1559_signed(const w4_eth_tx_t *tx, const w4_ecdsa_sig_t *sig, uint8_t *out,
                           uint32_t cap);
/* Decode a signed EIP-1559 transaction (empty access list only). tx->data
 * points into raw. */
int w4_eth_1559_decode(const uint8_t *raw, uint32_t len, w4_eth_tx_t *tx, w4_ecdsa_sig_t *sig);
/* Transaction hash = Keccak-256 of the signed bytes. */
void w4_eth_tx_hash(const uint8_t *raw, uint32_t len, uint8_t hash[32]);

/* Address = last 20 bytes of Keccak-256(64-byte uncompressed public key, no 0x04). */
void w4_eth_address(const uint8_t pub64[64], uint8_t addr[W4_ETH_ADDR_LEN]);
/* EIP-55 mixed-case checksum: "0x" + 40 chars + NUL. */
void w4_eth_checksum(const uint8_t addr[W4_ETH_ADDR_LEN], char out[43]);
/* Parse "0x" + 40 hex. Mixed case must match EIP-55 exactly (else
 * W4_ERR_HASH); all-lower or all-upper is accepted with *checked = false. */
int w4_eth_address_parse(const char *s, uint32_t len, uint8_t addr[W4_ETH_ADDR_LEN], bool *checked);

/* ABI. selector = first 4 bytes of Keccak-256(signature text). */
void w4_abi_selector(const char *signature, uint8_t sel[4]);
/* transfer(address,uint256): 68 bytes. */
void w4_abi_erc20_transfer(const uint8_t to[W4_ETH_ADDR_LEN], const w4_u256 *amount,
                           uint8_t out[68]);
/* balanceOf(address): 36 bytes. */
void w4_abi_erc20_balance_of(const uint8_t who[W4_ETH_ADDR_LEN], uint8_t out[36]);
/* A 32-byte ABI word as uint256 / address (high 12 bytes must be zero). */
void w4_abi_word_u256(const uint8_t word[32], w4_u256 *v);
int w4_abi_word_address(const uint8_t word[32], uint8_t addr[W4_ETH_ADDR_LEN]);

/* EIP-191 version 0x45: Keccak-256("\x19Ethereum Signed Message:\n" ||
 * decimal(len) || msg), what personal_sign signs. */
void w4_eth_personal_hash(const uint8_t *msg, uint32_t len, uint8_t hash[32]);

/* JSON-RPC 2.0 request builders (block tag "latest" when NULL). */
int32_t w4_rpc_eth_call(char *out, uint32_t cap, uint64_t id, const uint8_t to[20],
                        const uint8_t *data, uint32_t dlen, const char *block);
int32_t w4_rpc_eth_send_raw(char *out, uint32_t cap, uint64_t id, const uint8_t *raw, uint32_t len);
int32_t w4_rpc_eth_get_balance(char *out, uint32_t cap, uint64_t id, const uint8_t addr[20],
                               const char *block);
int32_t w4_rpc_eth_get_tx_count(char *out, uint32_t cap, uint64_t id, const uint8_t addr[20],
                                const char *block);

typedef struct {
    uint64_t id;
    bool is_error;
    int64_t err_code;
    char err_msg[128];
    uint8_t data[W4_ETH_DATA_MAX]; /* "result" as DATA, when it is hex */
    uint32_t data_len;
    char result_str[160]; /* "result" as text when it is a short string */
} w4_rpc_result_t;

/* Parse a JSON-RPC response: checks "jsonrpc":"2.0", reads id, then result
 * (hex string decoded into data/data_len and also kept as text) or error. */
int w4_rpc_parse(const char *json, uint32_t len, w4_rpc_result_t *r);
/* A QUANTITY result as uint256. */
int w4_rpc_result_qty(const w4_rpc_result_t *r, w4_u256 *v);

/* ======================================================================
 * secp256k1 (web4_secp256k1.c)
 * ====================================================================== */
/* Public key (x || y, 64 bytes, big-endian) of a secret key in [1, n-1]. */
int w4_secp_pubkey(const uint8_t sk[32], uint8_t pub64[64]);
/* RFC 6979 (HMAC-SHA256) deterministic ECDSA over a 32-byte hash, low-s. */
int w4_secp_sign(const uint8_t sk[32], const uint8_t hash[32], w4_ecdsa_sig_t *sig);
/* Verify. `require_low_s`: also refuse s > n/2 (Ethereum / EIP-2 rule). */
int w4_secp_verify(const uint8_t pub64[64], const uint8_t hash[32], const w4_ecdsa_sig_t *sig,
                   bool require_low_s);
/* Recover the public key (ecrecover). Variable time: inputs are public. */
int w4_secp_recover(const uint8_t hash[32], const w4_ecdsa_sig_t *sig, uint8_t pub64[64]);
/* 33-byte SEC1 compressed form. */
void w4_secp_compress(const uint8_t pub64[64], uint8_t out[33]);
/* Self-check of the Montgomery constants (recomputes R^2 mod p and mod n). */
bool w4_secp_selftest(void);

/* Signer hook for hosts that keep the key elsewhere (HSM, wallet). The
 * in-module w4_secp_sign matches this shape via w4_secp_signer. */
typedef int (*w4_eth_sign_fn)(void *ctx, const uint8_t hash[32], w4_ecdsa_sig_t *sig);
int w4_secp_signer(void *ctx /* const uint8_t sk[32] */, const uint8_t hash[32],
                   w4_ecdsa_sig_t *sig);

/* ======================================================================
 * Bitcoin
 * ====================================================================== */
void w4_ripemd160(const uint8_t *data, uint32_t len, uint8_t out[20]);
void w4_hash160(const uint8_t *data, uint32_t len, uint8_t out[20]); /* RIPEMD160(SHA256) */
/* base58check: payload = version bytes || data; checksum = SHA256d[0..4). */
int32_t w4_base58check_encode(const uint8_t *payload, uint32_t len, char *out, uint32_t cap);
int32_t w4_base58check_decode(const char *s, uint32_t len, uint8_t *out, uint32_t cap);

enum { W4_BECH32 = 1, W4_BECH32M = 2 };
/* Generic bech32/bech32m over 5-bit values (BIP-173 / BIP-350; max 90 chars). */
int32_t w4_bech32_encode(int variant, const char *hrp, const uint8_t *data5, uint32_t n, char *out,
                         uint32_t cap);
/* Returns the variant (W4_BECH32 / W4_BECH32M) or negative. hrp is lowercased. */
int w4_bech32_decode(const char *s, uint32_t len, char *hrp, uint32_t hrp_cap, uint8_t *data5,
                     uint32_t *n, uint32_t cap);
/* Segwit address (BIP-173 v0 bech32, BIP-350 v1+ bech32m). */
int32_t w4_segwit_encode(const char *hrp, uint8_t version, const uint8_t *prog, uint32_t plen,
                         char *out, uint32_t cap);
/* `hrp` is the expected human-readable part ("bc", "tb"). */
int w4_segwit_decode(const char *hrp, const char *addr, uint32_t len, uint8_t *version,
                     uint8_t *prog, uint32_t *plen);
/* P2PKH (version 0x00 mainnet / 0x6f testnet) and P2WPKH from a SEC1 key. */
int32_t w4_btc_p2pkh(uint8_t version, const uint8_t *pubkey, uint32_t plen, char *out,
                     uint32_t cap);
int32_t w4_btc_p2wpkh(const char *hrp, const uint8_t pub33[33], char *out, uint32_t cap);

/* ======================================================================
 * Content addressing bridge (CIDv1 <-> Web 3 forms), on kernel/src/ipfs_node
 * ====================================================================== */
/* CIDv1 raw sha2-256 of content. */
int w4_cid_of(const uint8_t *data, uint32_t len, ipfsn_cid_t *cid);
/* The 32-byte digest an EVM contract can store, from a sha2-256 CIDv1. */
int w4_cid_to_bytes32(const ipfsn_cid_t *cid, uint8_t out[32], uint32_t *codec);
/* And back: CIDv1 (codec raw or dag-pb) with a sha2-256 multihash. */
int w4_cid_from_bytes32(const uint8_t d[32], uint32_t codec, ipfsn_cid_t *cid);
/* "ipfs://<cid>" and its parse. */
int32_t w4_cid_uri(const ipfsn_cid_t *cid, char *out, uint32_t cap);
int w4_cid_uri_parse(const char *s, uint32_t len, ipfsn_cid_t *cid);
/* Web 2 face of Web 3 content: "https://<gateway>/ipfs/<cid>". */
int32_t w4_cid_gateway_url(const ipfsn_cid_t *cid, const char *gateway_host, char *out,
                           uint32_t cap);

#endif /* ZXV_WEB4_WEB3_H */

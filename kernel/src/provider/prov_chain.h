/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* prov_chain.h — chain adapters: how a Web 3 provider (decentralised GPU
 * market, storage or pinning network, decentralised inference network) or
 * any user binds its chain identity to provider receipts, anchors receipt
 * batches on its chain, and maps chain-asset payments onto VFV / ISO 4217
 * over the existing pay rails.
 *
 * FAMILIES (by signature scheme / address / transaction encoding)
 *   EVM        secp256k1 ECDSA with recovery, Keccak-256, RLP, EIP-155 chain
 *              ids, EIP-1559 transactions, EIP-55 addresses, EIP-712 typed
 *              data. Receipts are signed as EIP-712 typed data
 *              ("ZXV Provider Receipt", version "1", chainId).
 *   UTXO       Bitcoin family: secp256k1, HASH160, bech32 / bech32m segwit
 *              addresses (BIP-173 / BIP-350). Receipts are signed in the
 *              classic signed-message form (double SHA-256 of
 *              "\x18Bitcoin Signed Message:\n" || varint(len) || msg) with a
 *              65-byte compact recoverable signature, bound to a P2WPKH /
 *              P2PKH key hash. Anchors are OP_RETURN outputs.
 *   ED25519    Solana-style: Ed25519 over the message bytes, base58 32-byte
 *              addresses. Anchors are memo payloads.
 *   COSMOS     Tendermint / Cosmos SDK: secp256k1 over SHA-256 of the bytes,
 *              64-byte r || s (low-s), compressed keys, bech32 addresses
 *              (HRP per chain), protobuf SignDoc (SIGN_MODE_DIRECT, no amino
 *              JSON) via prov_cosmos_sign_doc.
 *   SUBSTRATE  sr25519 (Schnorrkel). NOT implemented here: declared as an
 *              EXTERNAL signer / verifier. Without the host's ext_verify hook
 *              every verify returns PROV_ERR_UNSUPPORTED. SS58 addresses are
 *              treated as opaque text.
 *   OPAQUE     any other chain: the host supplies sign / verify / address
 *              hooks; the adapter only carries bytes.
 *
 * REUSE. Every primitive comes from kernel/src/web4 (Keccak-256, RLP,
 * EIP-1559, secp256k1 sign / verify / recover, EIP-55, ABI selectors, HASH160,
 * bech32 / bech32m, segwit) with its published vectors, from
 * kernel/src/ipfs_node (base58), kernel/src/robin_debanks (SHA-256, Ed25519
 * verify) and kernel/src/mlkem (SHA3-256). New code here is only EIP-712
 * hashing, the Bitcoin signed-message hash, the Cosmos SignDoc encoder, the
 * receipt Merkle anchor and the asset conversion.
 *
 * ANCHORING. Receipt digests are batched into a SHA3-256 Merkle tree
 * (leaf = H(0x00 || digest), node = H(0x01 || left || right), an odd node is
 * promoted); the 32-byte root is the commitment written on any chain by
 * prov_chain_anchor_payload. Anyone holding a receipt and its path can check
 * inclusion against the on-chain root.
 *
 * PAYMENT. A chain asset is converted to the market's quote asset at a
 * POSTED rate (quote minor units per whole chain token), exact integer
 * floor, both directions. Settlement then runs over the pay rails like any
 * other payment: no interest, no time-based charge.
 *
 * HONEST LIMITS. No live chain connectivity: the adapters encode, hash, sign
 * and verify; submission is the host's `submit` hook. secp256k1 and Ed25519
 * are classical (not post-quantum); the ZXV side of every receipt is also
 * ML-DSA-65 co-signed (prov.h G6), so chain signatures are bindings, not the
 * root of trust. Only the families above are implemented; sr25519 is
 * external.
 */
#ifndef ZXV_PROV_CHAIN_H
#define ZXV_PROV_CHAIN_H

#include "prov.h"

typedef bool (*prov_chain_ext_verify_fn)(void *ctx, const uint8_t *pub, uint32_t pub_len,
                                         const uint8_t *msg, uint32_t len, const uint8_t *sig,
                                         uint32_t sig_len);
typedef int (*prov_chain_ext_sign_fn)(void *ctx, const uint8_t *msg, uint32_t len, uint8_t *sig,
                                      uint32_t cap, uint32_t *sig_len);
/* Transport: hand bytes to the host (a wallet, an RPC client). */
typedef int (*prov_chain_submit_fn)(void *ctx, uint8_t family, const uint8_t *payload,
                                    uint32_t len);

typedef struct {
    uint8_t family;                   /* prov_chain_family_t */
    uint64_t evm_chain_id;            /* EVM: EIP-155 chain id                       */
    char chain_id[PROV_CHAIN_ID_MAX]; /* COSMOS chain-id, other families' tag */
    char hrp[16];                     /* UTXO ("bc", "tb"), COSMOS ("cosmos", ...)   */
    uint8_t evm_contract[20];         /* EIP-712 verifyingContract (optional)    */
    bool has_contract;
    prov_chain_ext_verify_fn ext_verify;
    prov_chain_ext_sign_fn ext_sign;
    void *ext_ctx;
    prov_chain_submit_fn submit;
    void *submit_ctx;
} prov_chain_t;

const char *prov_chain_family_name(uint8_t family);
/* True iff the family's signatures are verified in-module (not external). */
bool prov_chain_native_verify(uint8_t family);

/* ===== Merkle anchor of receipt digests ===== */
#define PROV_ANCHOR_MAX_DEPTH 24u
int prov_anchor_root(const uint8_t (*leaves)[PROV_HASH_LEN], uint32_t n,
                     uint8_t root[PROV_HASH_LEN]);
/* Path for leaf `index`: siblings bottom-up; a level where the node was
 * promoted contributes no sibling. dirs bit i = 1 when the sibling is on the
 * left. Returns the path length or negative. */
int prov_anchor_proof(const uint8_t (*leaves)[PROV_HASH_LEN], uint32_t n, uint32_t index,
                      uint8_t (*path)[PROV_HASH_LEN], uint32_t *dirs);
bool prov_anchor_verify(const uint8_t leaf[PROV_HASH_LEN], const uint8_t (*path)[PROV_HASH_LEN],
                        uint32_t depth, uint32_t dirs, const uint8_t root[PROV_HASH_LEN]);

/* Bytes that put `root` on chain:
 *   EVM        calldata anchor(bytes32): 4-byte selector || root (36 bytes)
 *   UTXO       scriptPubKey OP_RETURN PUSH32 root: 0x6a 0x20 || root (34)
 *   ED25519 /
 *   COSMOS     memo text "zxv1:" || 64 lowercase hex (69 bytes)
 *   SUBSTRATE /
 *   OPAQUE     "zxv1" || root (36 bytes) for the host's extrinsic builder */
int32_t prov_chain_anchor_payload(const prov_chain_t *c, const uint8_t root[PROV_HASH_LEN],
                                  uint8_t *out, uint32_t cap);
/* EVM: unsigned EIP-1559 transaction calling anchor(root) on `contract`
 * (w4_eth_1559_unsigned); its Keccak-256 is the signing hash. */
int32_t prov_evm_anchor_tx(const prov_chain_t *c, uint64_t nonce, uint64_t gas_limit,
                           uint64_t max_priority_wei, uint64_t max_fee_wei,
                           const uint8_t contract[20], const uint8_t root[PROV_HASH_LEN],
                           uint8_t *out, uint32_t cap);
/* Hand an anchor payload to the host. PROV_ERR_HOOK without `submit`. */
int prov_chain_submit(const prov_chain_t *c, const uint8_t *payload, uint32_t len);

/* ===== EIP-712 ===== */
void prov_eip712_type_hash(const char *type, uint8_t out[32]);
/* EIP712Domain(string name,string version,uint256 chainId[,address
 * verifyingContract]) separator. contract may be NULL. */
void prov_eip712_domain(const char *name, const char *version, uint64_t chain_id,
                        const uint8_t *contract20, uint8_t out[32]);
/* keccak256(typehash || words[0] || ... || words[n-1]) (32-byte words). */
void prov_eip712_hash_struct(const uint8_t typehash[32], const uint8_t (*words)[32], uint32_t n,
                             uint8_t out[32]);
/* keccak256(0x19 0x01 || domain || struct). */
void prov_eip712_digest(const uint8_t domain[32], const uint8_t struct_hash[32], uint8_t out[32]);
/* The receipt as typed data:
 *   ZXVReceipt(bytes32 digest,bytes32 jobId,bytes32 providerId,bytes32 userId,
 *              uint256 units,uint256 unitPrice,uint256 gross,uint256 fee,
 *              uint256 net,string asset)
 * under domain ("ZXV Provider Receipt", "1", evm_chain_id[, contract]). */
#define PROV_EIP712_RECEIPT_TYPE                                                                   \
    "ZXVReceipt(bytes32 digest,bytes32 jobId,bytes32 providerId,bytes32 userId,uint256 units,"     \
    "uint256 unitPrice,uint256 gross,uint256 fee,uint256 net,string asset)"
void prov_eip712_receipt(const prov_chain_t *c, const prov_receipt_t *r, uint8_t out[32]);

/* ===== Bitcoin signed-message hash ===== */
void prov_btc_message_hash(const uint8_t *msg, uint32_t len, uint8_t out[32]);

/* ===== Cosmos SignDoc (protobuf, SIGN_MODE_DIRECT) =====
 * message SignDoc { bytes body_bytes = 1; bytes auth_info_bytes = 2;
 *                   string chain_id = 3; uint64 account_number = 4; }
 * proto3 canonical: fields in order, defaults (empty / 0) omitted. */
int32_t prov_cosmos_sign_doc(const uint8_t *body, uint32_t body_len, const uint8_t *auth,
                             uint32_t auth_len, const char *chain_id, uint64_t account_number,
                             uint8_t *out, uint32_t cap);

/* ===== Receipt binding ===== */
/* The text a non-EVM chain key signs for a receipt:
 * "zxv-prov-receipt:" || 64 lowercase hex of the receipt digest (81 bytes). */
#define PROV_CHAIN_MSG_LEN 81u
void prov_chain_receipt_text(const prov_receipt_t *r, uint8_t out[PROV_CHAIN_MSG_LEN]);

/* Verify a chain signature over a receipt.
 *   EVM        pub = 20-byte address, sig = 65 bytes r||s||v (v 0/1/27/28)
 *              over prov_eip712_receipt.
 *   UTXO       pub = 20-byte HASH160 of the compressed key, sig = 65-byte
 *              compact (header 27..34) over prov_btc_message_hash(text).
 *   ED25519    pub = 32 bytes, sig = 64 bytes over the text.
 *   COSMOS     pub = 33-byte compressed key, sig = 64 bytes r||s (low-s)
 *              over SHA-256(text).
 *   SUBSTRATE / OPAQUE: ext_verify(pub, text, sig), else UNSUPPORTED.
 * PROV_OK, PROV_ERR_AUTH, PROV_ERR_ARG or PROV_ERR_UNSUPPORTED. */
int prov_chain_verify_receipt(const prov_chain_t *c, const prov_receipt_t *r, const uint8_t *pub,
                              uint32_t pub_len, const uint8_t *sig, uint32_t sig_len);

/* ===== Addresses ===== */
/* From a public key: EVM pub64 -> EIP-55 text; UTXO pub33 -> P2WPKH; ED25519
 * pub32 -> base58; COSMOS pub33 -> bech32(hrp, HASH160). Others:
 * UNSUPPORTED. Returns length or negative. */
int32_t prov_chain_address(const prov_chain_t *c, const uint8_t *pub, uint32_t pub_len, char *out,
                           uint32_t cap);
/* Parse / validate an address string into its payload bytes (20-byte EVM
 * address, segwit program, 32-byte Ed25519 key, 20/32-byte Cosmos hash).
 * Returns payload length or negative. Others: UNSUPPORTED. */
int32_t prov_chain_address_parse(const prov_chain_t *c, const char *s, uint32_t len, uint8_t *out,
                                 uint32_t cap);

/* ===== Asset conversion (posted rate; exact floor) ===== */
/* quote = floor(amount * price / 10^decimals), amount a big-endian integer
 * of chain base units (<= 32 bytes), price = quote minor units per whole
 * token, decimals <= 36. */
int prov_chain_to_quote(const uint8_t *amount_be, uint32_t amount_len, uint8_t decimals,
                        uint64_t price, uint64_t *quote);
/* base units = floor(quote * 10^decimals / price), as 32 big-endian bytes. */
int prov_quote_to_chain(uint64_t quote, uint8_t decimals, uint64_t price, uint8_t out_be[32]);

#endif /* ZXV_PROV_CHAIN_H */

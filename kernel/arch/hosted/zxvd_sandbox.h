/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_sandbox.h — what the economy method groups share (zxvd_instruments.c:
 * bonds, derivatives; zxvd_estate.c: grants, estate): a sandbox ledger,
 * tables of Ed25519 and ML-DSA-87 keys, and parameter readers. Not a group
 * itself.
 *
 *   C1 SANDBOX. Each ledger-backed group keeps its own pay_ledger in memory
 *      (a "book"), separate from the owner's books (ledger.*) and gone when
 *      zxvd stops. <group>.fund issues sandbox units from the book's own
 *      issuer account so a flow can be tried; they are not money, are not
 *      posted to the owner's books and convert to nothing.
 *   C2 KEYS. The engines check signatures through a verify hook. Two key
 *      tables, one per scheme:
 *      C2a ED25519 (bonds, derivatives: zxvd_instruments.c). The hook is
 *      ed25519_verify.c (the update checker's code): <group>.key registers a
 *      32-byte public key under the engine's numeric key id, once (a different
 *      key for a registered id is refused), and every signature is 64 bytes
 *      of hex. Not post-quantum.
 *      C2b ML-DSA-87 (grants, estate, recovery: zxvd_estate.c). FIPS 204
 *      pure ML-DSA-87 (pq_mldsa87.c) with a context string fixed per table
 *      (zxvd_sandbox_pqkeys_t.ctx), so a signature made for one group never
 *      verifies in another. A public key is 2592 bytes and its FINGERPRINT
 *      is SHA-256("zxvd.mldsa87.key.v1" || public key): the 32-byte signer
 *      id the estate and recovery engines hold (estate.h EST_KEY, recovery.h
 *      REC_KEY). <group>.key registers a public key (under a numeric id
 *      where the engine names keys by number, grants), refuses a "fingerprint"
 *      that is not that key's, and a signature verifies only when it is
 *      exactly 4627 bytes and the signer's key is registered: a 64-byte
 *      Ed25519 signature, a truncated or an oversized one, or one by a key
 *      only named by fingerprint signs nothing.
 *      A key is whatever the owner registers: an engine's jurisdiction switch
 *      is only as strong as the gateway key the owner chose.
 *   C3 NUMBERS. 64-bit inputs are JSON integers or decimal strings (no sign,
 *      no fraction); 64-bit results are decimal strings (zxvd_rpc.h M3).
 *      "now" is unix seconds, default the clock; it is the engines' tick.
 *   C4 REFUSALS. An engine's refusal is error ZXVD_E_ENGINE with the
 *      engine's status name and what it means; nothing changed.
 */
#ifndef ZXVD_SANDBOX_H
#define ZXVD_SANDBOX_H

#include "zxvd_ext.h"
#include "../../src/pay/pay_ledger.h"

#define ZXVD_E_ENGINE          (-32060)    /* C4 */
#define ZXVD_SANDBOX_OWNER_MAX 0xFFFFFF00u /* owner ids at or above are zxvd's own */
#define ZXVD_SANDBOX_MODULE    0xFFFFFFF0u /* owner of an engine's own accounts */
#define ZXVD_SANDBOX_ISSUER    0xFFFFFFF1u /* owner of the sandbox issuers (C1) */
#define ZXVD_SANDBOX_MAX_KEYS  32u
#define ZXVD_SANDBOX_ASSETS    3u

typedef struct {
    uint32_t n;
    uint32_t id[ZXVD_SANDBOX_MAX_KEYS];
    uint8_t pk[ZXVD_SANDBOX_MAX_KEYS][32];
} zxvd_sandbox_keys_t;

/* C2b: ML-DSA-87 (FIPS 204) sizes and the fingerprint's domain */
#define ZXVD_SANDBOX_PQ_PK     2592u
#define ZXVD_SANDBOX_PQ_SIG    4627u
#define ZXVD_SANDBOX_PQ_FP     32u
#define ZXVD_SANDBOX_PQ_DOMAIN "zxvd.mldsa87.key.v1"

typedef struct {
    const char *ctx; /* the FIPS 204 context string, 1..255 bytes, fixed per table */
    uint32_t n;
    uint32_t id[ZXVD_SANDBOX_MAX_KEYS];
    uint8_t fp[ZXVD_SANDBOX_MAX_KEYS][ZXVD_SANDBOX_PQ_FP];
    uint8_t pk[ZXVD_SANDBOX_MAX_KEYS][ZXVD_SANDBOX_PQ_PK];
} zxvd_sandbox_pqkeys_t;

typedef struct {
    const char *name; /* on the wire: "vino", "vbill", "barrel" */
    const char *code; /* the ledger's code; NULL: registered by the engine */
    uint8_t minor;
    bool fundable; /* C1: has a sandbox issuer */
    uint16_t asset;
    uint32_t issuer;
} zxvd_sandbox_asset_t;

typedef struct {
    pay_ledger_t L;
    bool open;
    zxvd_sandbox_asset_t a[ZXVD_SANDBOX_ASSETS];
    uint32_t n_assets;
    uint64_t seq, funded;
} zxvd_sandbox_book_t;

/* C1: a fresh ledger with the book's assets (those with a code) and their
 * issuers. 0, or -1 when the ledger refused one. */
int zxvd_sandbox_book_open(zxvd_sandbox_book_t *b);
/* the asset slot named `name`, or NULL */
const zxvd_sandbox_asset_t *zxvd_sandbox_asset(const zxvd_sandbox_book_t *b, const char *name);
/* the wire name of an account's asset ("?" when none) */
const char *zxvd_sandbox_asset_name(const zxvd_sandbox_book_t *b, uint32_t acct);

/* C2a */
bool zxvd_sandbox_verify(const zxvd_sandbox_keys_t *k, uint32_t id, const uint8_t *msg,
                         uint32_t len, const uint8_t *sig, uint32_t sig_len);
bool zxvd_sandbox_verify_pk(const uint8_t pk[32], const uint8_t *msg, uint32_t len,
                            const uint8_t *sig, uint32_t sig_len);

/* C2b: the fingerprint of an ML-DSA-87 public key */
void zxvd_sandbox_pq_fingerprint(const uint8_t pk[ZXVD_SANDBOX_PQ_PK],
                                 uint8_t fp[ZXVD_SANDBOX_PQ_FP]);
/* C2b: true when sig (exactly ZXVD_SANDBOX_PQ_SIG bytes) is the registered
 * key's signature over msg under the table's context; by numeric id, or by
 * fingerprint */
bool zxvd_sandbox_pq_verify(const zxvd_sandbox_pqkeys_t *k, uint32_t id, const uint8_t *msg,
                            uint32_t len, const uint8_t *sig, uint32_t sig_len);
bool zxvd_sandbox_pq_verify_fp(const zxvd_sandbox_pqkeys_t *k, const uint8_t fp[ZXVD_SANDBOX_PQ_FP],
                               const uint8_t *msg, uint32_t len, const uint8_t *sig,
                               uint32_t sig_len);

/* C3 readers; each fails the call and returns false on a bad value */
bool zxvd_sandbox_u64(zxvd_call_t *c, const char *key, bool required, uint64_t def, uint64_t *v);
bool zxvd_sandbox_u32(zxvd_call_t *c, const char *key, bool required, uint32_t def, uint32_t *v);
/* the same, for a parameter the method cannot do without */
bool zxvd_sandbox_need_u64(zxvd_call_t *c, const char *key, uint64_t *v);
bool zxvd_sandbox_need_u32(zxvd_call_t *c, const char *key, uint32_t *v);
bool zxvd_sandbox_now(zxvd_call_t *c, uint64_t *now);
bool zxvd_sandbox_sig(zxvd_call_t *c, const char *key, uint8_t sig[64]);
bool zxvd_sandbox_key32(zxvd_call_t *c, const char *key, uint8_t out[32]);
/* C2b: exactly ZXVD_SANDBOX_PQ_SIG bytes of hex */
bool zxvd_sandbox_pq_sig(zxvd_call_t *c, const char *key, uint8_t sig[ZXVD_SANDBOX_PQ_SIG]);
/* the owner id `key`: below ZXVD_SANDBOX_OWNER_MAX */
bool zxvd_sandbox_owner(zxvd_call_t *c, const char *key, uint32_t *v);

/* a string literal, raw */
#define ZXVD_RAW(b, lit) zxvd_buf_raw((b), (lit), sizeof(lit) - 1)

/* C4: fail with "<name>: <why>" */
void zxvd_sandbox_refuse(zxvd_call_t *c, const char *name, const char *why);
/* a 64-bit value as a JSON decimal string */
void zxvd_sandbox_put_u64(zxvd_buf_t *b, uint64_t v);
/* prefix (raw JSON, e.g. ",\"qty\":") then v as a decimal string */
void zxvd_sandbox_kv(zxvd_buf_t *b, const char *prefix, uint64_t v);
/* hex of a message to sign: {"message":"..","bytes":n,"scheme":"ed25519"} */
void zxvd_sandbox_put_msg(zxvd_call_t *c, const uint8_t *m, uint32_t n);
/* C2b: {"message":"..","bytes":n,"scheme":"ml-dsa-87","context":"<k->ctx>"} */
void zxvd_sandbox_put_msg_pq(zxvd_call_t *c, const zxvd_sandbox_pqkeys_t *k, const uint8_t *m,
                             uint32_t n);

/* The methods every ledger-backed group offers, on its own book and keys:
 *   key          {key, public_key}           C2a (C2b: zxvd_sandbox_m_pqkey)
 *   account_open {owner, asset}              a new sandbox account
 *   fund         {account, amount_minor}     C1
 *   account      {account}                   its owner, asset and balances */
void zxvd_sandbox_m_key(zxvd_call_t *c, zxvd_sandbox_keys_t *k);
/* C2b: {key (by_id only), public_key, fingerprint (optional)}; answers
 * {"key":id (by_id only),"fingerprint":"..","added":b,"keys":n}. A table
 * named by fingerprint (by_id false) holds each public key once. */
void zxvd_sandbox_m_pqkey(zxvd_call_t *c, zxvd_sandbox_pqkeys_t *k, bool by_id);
void zxvd_sandbox_m_account_open(zxvd_call_t *c, zxvd_sandbox_book_t *b);
void zxvd_sandbox_m_fund(zxvd_call_t *c, zxvd_sandbox_book_t *b);
void zxvd_sandbox_m_account(zxvd_call_t *c, const zxvd_sandbox_book_t *b);
/* false (and the call failed) when the book is not open: "<group> is not
 * set up: call <group>.setup first" */
bool zxvd_sandbox_ready(zxvd_call_t *c, const zxvd_sandbox_book_t *b, const char *group);

#endif

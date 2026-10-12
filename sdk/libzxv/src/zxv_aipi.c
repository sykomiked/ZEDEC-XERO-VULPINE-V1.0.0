/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_aipi.c — ABI 1.5: AIPI content-addressed functions and IPFS CIDs over
 * kernel/src/aipi and kernel/src/ipfs_node. Every hash, encoding and rule is
 * the kernel's; this file turns binary CIDs into ipfsn_cid_t and back,
 * validates arguments, maps statuses, and owns the zxv_aipi handle (an
 * offline in-memory IPFS node with the AIPI context over it). No money
 * moves here: the kernel's escrow hooks are only called from EXECUTE and
 * claim submission, which this group does not expose. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "aipi.h"
#include "ipfs_node.h"

/* The ABI constants are the kernel's; a mismatch must not compile. */
_Static_assert(ZXV_CID_MAX == IPFSN_CID_BIN_MAX, "binary CID size");
_Static_assert(ZXV_CID_TEXT_MAX + 1u == IPFSN_CID_STR_MAX, "CID text size");
_Static_assert(ZXV_CID_DIGEST_MAX == IPFSN_DIGEST_MAX, "digest size");
_Static_assert(ZXV_CID_CODEC_RAW == IPFSN_MC_RAW && ZXV_CID_CODEC_DAG_PB == IPFSN_MC_DAG_PB,
               "codecs");
_Static_assert(ZXV_CID_HASH_IDENTITY == IPFSN_MH_IDENTITY &&
                   ZXV_CID_HASH_SHA2_256 == IPFSN_MH_SHA2_256,
               "multihash codes");
_Static_assert(ZXV_AIPI_MC_FN == AIPI_MC_FN && ZXV_AIPI_MC_TENSOR_INFER == AIPI_MC_TENSOR_INFER &&
                   ZXV_AIPI_MC_SETTLE_LEDGER == AIPI_MC_SETTLE_LEDGER &&
                   ZXV_AIPI_MC_SWARM_ROUTE == AIPI_MC_SWARM_ROUTE && ZXV_AIPI_MC_CALL == AIPI_MC_CALL,
               "AIPI codecs");
_Static_assert(ZXV_AIPI_INTENT_FN == AIPI_INTENT_FN &&
                   ZXV_AIPI_INTENT_TENSOR_INFER == AIPI_INTENT_TENSOR_INFER &&
                   ZXV_AIPI_INTENT_SETTLE_LEDGER == AIPI_INTENT_SETTLE_LEDGER &&
                   ZXV_AIPI_INTENT_SWARM_ROUTE == AIPI_INTENT_SWARM_ROUTE,
               "intents");
_Static_assert(ZXV_AIPI_VERIFY_CHECKER == AIPI_VERIFY_CHECKER &&
                   ZXV_AIPI_VERIFY_CERT == AIPI_VERIFY_CERT &&
                   ZXV_AIPI_VERIFY_QUORUM == AIPI_VERIFY_QUORUM &&
                   ZXV_AIPI_VERIFY_LOCAL == AIPI_VERIFY_LOCAL,
               "verify modes");
_Static_assert(ZXV_AIPI_PRIV_PUBLIC == AIPI_PRIV_PUBLIC && ZXV_AIPI_PRIV_LOCAL == AIPI_PRIV_LOCAL,
               "privacy");
_Static_assert(ZXV_AIPI_DET_NONDETERMINISTIC == AIPI_DET_NONDETERMINISTIC &&
                   ZXV_AIPI_DET_DETERMINISTIC == AIPI_DET_DETERMINISTIC,
               "determinism");
_Static_assert(ZXV_AIPI_NAME_MAX == AIPI_NAME_MAX && ZXV_AIPI_IO_MAX == AIPI_IO_MAX &&
                   ZXV_AIPI_CERT_MAX == AIPI_CERT_MAX && ZXV_AIPI_QUORUM_MAX == AIPI_CLAIMS_MAX,
               "limits");
_Static_assert(ZXV_AIPI_FN_MAX == AIPI_FN_ENC_MAX && ZXV_AIPI_CALL_MAX == AIPI_CALL_ENC_MAX &&
                   ZXV_AIPI_CLAIM_MAX == AIPI_CLAIM_ENC_MAX,
               "record sizes");
_Static_assert(ZXV_AIPI_BLOCK_MAX == IPFSN_BLOCK_MAX, "block size");
_Static_assert(ZXV_AIPI_ROUTE_CACHE == AIPI_ROUTE_CACHE && ZXV_AIPI_ROUTE_LOCAL == AIPI_ROUTE_LOCAL &&
                   ZXV_AIPI_ROUTE_FETCH_LOCAL == AIPI_ROUTE_FETCH_LOCAL &&
                   ZXV_AIPI_ROUTE_MESH == AIPI_ROUTE_MESH &&
                   ZXV_AIPI_ROUTE_UNAVAILABLE == AIPI_ROUTE_UNAVAILABLE,
               "routes");

uint32_t zxvi_feat2_aipi(void)
{
    return ZXV_FEAT2_AIPI;
}

/* ===================================================================== */
/* Status mapping and CID conversion                                     */
/* ===================================================================== */

/* An AIPI_ERR_* (or AIPI_OK) as a ZXV status with the detail set. */
static int32_t map_aipi(int r, const char *what)
{
    switch (r) {
    case AIPI_OK: return zxvi_ok();
    case AIPI_ERR_ARG: return zxvi_fail(ZXV_E_ARG, "%s: invalid argument", what);
    case AIPI_ERR_MALFORMED: return zxvi_fail(ZXV_E_FORMAT, "%s: not a valid AIPI record", what);
    case AIPI_ERR_UNSUPP: return zxvi_fail(ZXV_E_CID, "%s: CID kind not supported here", what);
    case AIPI_ERR_SPACE: return zxvi_fail(ZXV_E_TOO_LONG, "%s: data exceeds a limit", what);
    case AIPI_ERR_HASH:
        return zxvi_fail(ZXV_E_CID, "%s: bytes do not hash to the CID (dropped)", what);
    case AIPI_ERR_NOTFOUND:
    case AIPI_ERR_OFFLINE:
        return zxvi_fail(ZXV_E_NOT_FOUND, "%s: not held by this node (libzxv has no network)",
                         what);
    case AIPI_ERR_PRIVATE:
        return zxvi_fail(ZXV_E_STATE, "%s: private block and this node has no key", what);
    case AIPI_ERR_STATE:
        return zxvi_fail(ZXV_E_STATE, "%s: already held with the other visibility", what);
    case AIPI_ERR_FULL: return zxvi_fail(ZXV_E_CAPACITY, "%s: block or pin table is full", what);
    case AIPI_ERR_CODEC:
        return zxvi_fail(ZXV_E_CID, "%s: function CID codec does not match its intent", what);
    case AIPI_ERR_TRANSPORT:
        /* The only storage is the handle's memory: a failed write means full. */
        return zxvi_fail(ZXV_E_CAPACITY, "%s: block store is full", what);
    default: return zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, r);
    }
}

/* Parse exactly one canonical binary CID. */
static int32_t cid_in(const uint8_t *b, size_t n, ipfsn_cid_t *c, const char *what)
{
    if (!b) return zxvi_fail(ZXV_E_ARG, "%s: CID is NULL", what);
    if (n == 0 || n > ZXV_CID_MAX)
        return zxvi_fail(ZXV_E_CID, "%s: CID is %zu bytes (1..%u)", what, n, ZXV_CID_MAX);
    if (ipfsn_cid_decode_exact(b, (uint32_t) n, c) != IPFSN_OK)
        return zxvi_fail(ZXV_E_CID, "%s: not one canonical CID", what);
    return ZXV_OK;
}

static int32_t cid_out(const ipfsn_cid_t *c, uint8_t *out, size_t *out_len, const char *what)
{
    uint8_t b[IPFSN_CID_BIN_MAX];
    int n = ipfsn_cid_encode(c, b, sizeof b);
    if (n <= 0) return zxvi_fail(ZXV_E_INTERNAL, "%s: CID encode failed (%d)", what, n);
    memcpy(out, b, (size_t) n);
    *out_len = (size_t) n;
    return zxvi_ok();
}

/* R4 for bytes: *out_len always, the copy only when it fits (no NUL; an
 * empty result needs no buffer). */
static int32_t bytes_out(const uint8_t *src, size_t n, uint8_t *buf, size_t cap, size_t *out_len,
                         const char *what)
{
    *out_len = n;
    if (cap < n || (n && !buf)) return zxvi_fail(ZXV_E_BUFFER, "%s: buffer needs %zu bytes", what, n);
    if (n) memcpy(buf, src, n);
    return zxvi_ok();
}

/* ===================================================================== */
/* CIDs                                                                  */
/* ===================================================================== */

int32_t zxv_cid_compute(uint32_t codec, const uint8_t *data, size_t len,
                        uint8_t out_cid[ZXV_CID_MAX], size_t *out_cid_len)
{
    if ((!data && len) || !out_cid || !out_cid_len)
        return zxvi_fail(ZXV_E_ARG, "cid_compute: NULL argument");
    if (len > UINT32_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "cid_compute: %zu bytes is over 2^32 - 1", len);
    ipfsn_cid_t c;
    if (ipfsn_cid_sha256(codec, data, (uint32_t) len, &c) != IPFSN_OK)
        return zxvi_fail(ZXV_E_INTERNAL, "cid_compute: kernel refused");
    return cid_out(&c, out_cid, out_cid_len, "cid_compute");
}

static int discard_sink(void *ctx, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len)
{
    (void) ctx;
    (void) cid;
    (void) block;
    (void) len;
    return 0;
}

int32_t zxv_cid_unixfs(const uint8_t *data, size_t len, uint8_t out_cid[ZXV_CID_MAX],
                       size_t *out_cid_len)
{
    if ((!data && len) || !out_cid || !out_cid_len)
        return zxvi_fail(ZXV_E_ARG, "cid_unixfs: NULL argument");
    ipfsn_ufs_t *u = calloc(1, sizeof *u);
    uint8_t *chunk = malloc(IPFSN_UFS_CHUNK);
    if (!u || !chunk) {
        free(u);
        free(chunk);
        return zxvi_fail(ZXV_E_NOMEM, "cid_unixfs: allocation failed");
    }
    ipfsn_cid_t root;
    uint64_t size = 0;
    int r = ipfsn_ufs_init(u, chunk, 0, 0, discard_sink, NULL);
    size_t off = 0;
    while (r == IPFSN_OK && off < len) {
        size_t take = len - off > (size_t) 0x40000000u ? (size_t) 0x40000000u : len - off;
        r = ipfsn_ufs_update(u, data + off, (uint32_t) take);
        off += take;
    }
    if (r == IPFSN_OK) r = ipfsn_ufs_final(u, &root, &size);
    free(u);
    free(chunk);
    if (r == IPFSN_ERR_DEPTH)
        return zxvi_fail(ZXV_E_TOO_LONG, "cid_unixfs: file is deeper than the DAG limit");
    if (r != IPFSN_OK || size != (uint64_t) len)
        return zxvi_fail(ZXV_E_INTERNAL, "cid_unixfs: kernel status %d", r);
    return cid_out(&root, out_cid, out_cid_len, "cid_unixfs");
}

int32_t zxv_cid_verify(const uint8_t *cid, size_t cid_len, const uint8_t *data, size_t len)
{
    ipfsn_cid_t c;
    int32_t rc = cid_in(cid, cid_len, &c, "cid_verify");
    if (rc) return rc;
    if (!data && len) return zxvi_fail(ZXV_E_ARG, "cid_verify: data is NULL");
    if (len > UINT32_MAX) return zxvi_fail(ZXV_E_CID, "cid_verify: bytes do not match the CID");
    int r = ipfsn_cid_verify(&c, data, (uint32_t) len);
    if (r == IPFSN_OK) return zxvi_ok();
    if (r == IPFSN_ERR_HASH) return zxvi_fail(ZXV_E_CID, "cid_verify: bytes do not match the CID");
    return zxvi_fail(ZXV_E_CID, "cid_verify: CID cannot be verified (status %d)", r);
}

int32_t zxv_cid_to_text(const uint8_t *cid, size_t cid_len, uint32_t base, char *buf, size_t cap,
                        size_t *out_len)
{
    ipfsn_cid_t c;
    char s[IPFSN_CID_STR_MAX];
    int n;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "cid_to_text: out_len is NULL");
    int32_t rc = cid_in(cid, cid_len, &c, "cid_to_text");
    if (rc) return rc;
    if (base == ZXV_CID_BASE_DEFAULT) {
        n = ipfsn_cid_to_string(&c, s, sizeof s);
    } else if (base == ZXV_CID_BASE_58BTC) {
        if (c.version != 1)
            return zxvi_fail(ZXV_E_CID, "cid_to_text: a CIDv0 has no multibase form (zxv_cid_to_v1)");
        n = aipi_cid_to_text(&c, s, sizeof s);
    } else {
        return zxvi_fail(ZXV_E_ARG, "cid_to_text: base %u is not a ZXV_CID_BASE_*", base);
    }
    if (n <= 0) return zxvi_fail(ZXV_E_INTERNAL, "cid_to_text: kernel status %d", n);
    return zxvi_out_text_n(s, (size_t) n, buf, cap, out_len);
}

int32_t zxv_cid_from_text(const char *text, uint8_t out_cid[ZXV_CID_MAX], size_t *out_cid_len)
{
    if (!out_cid || !out_cid_len) return zxvi_fail(ZXV_E_ARG, "cid_from_text: NULL out");
    int32_t rc = zxvi_check_str(text, ZXV_CID_TEXT_MAX, "cid_from_text: text", false);
    if (rc) return rc;
    uint32_t n = (uint32_t) strlen(text);
    ipfsn_cid_t c;
    int r = text[0] == 'z' ? aipi_cid_from_text(text, n, &c) : ipfsn_cid_parse(text, n, &c);
    if (r != 0) return zxvi_fail(ZXV_E_CID, "cid_from_text: not a canonical CID text");
    return cid_out(&c, out_cid, out_cid_len, "cid_from_text");
}

int32_t zxv_cid_info(const uint8_t *cid, size_t cid_len, uint32_t *out_version,
                     uint32_t *out_codec, uint32_t *out_hash, uint8_t *out_digest,
                     size_t *out_digest_len)
{
    ipfsn_cid_t c;
    int32_t rc = cid_in(cid, cid_len, &c, "cid_info");
    if (rc) return rc;
    if (out_version) *out_version = c.version;
    if (out_codec) *out_codec = c.codec;
    if (out_hash) *out_hash = c.mh_code;
    if (out_digest) memcpy(out_digest, c.digest, c.digest_len);
    if (out_digest_len) *out_digest_len = c.digest_len;
    return zxvi_ok();
}

int32_t zxv_cid_to_v1(const uint8_t *cid, size_t cid_len, uint8_t out_cid[ZXV_CID_MAX],
                      size_t *out_cid_len)
{
    ipfsn_cid_t c, v1;
    if (!out_cid || !out_cid_len) return zxvi_fail(ZXV_E_ARG, "cid_to_v1: NULL out");
    int32_t rc = cid_in(cid, cid_len, &c, "cid_to_v1");
    if (rc) return rc;
    ipfsn_cid_to_v1(&c, &v1);
    return cid_out(&v1, out_cid, out_cid_len, "cid_to_v1");
}

int32_t zxv_aipi_provider_key(const uint8_t *cid, size_t cid_len, uint8_t out_key[32])
{
    ipfsn_cid_t c;
    if (!out_key) return zxvi_fail(ZXV_E_ARG, "aipi_provider_key: out_key is NULL");
    int32_t rc = cid_in(cid, cid_len, &c, "aipi_provider_key");
    if (rc) return rc;
    return map_aipi(aipi_provider_key(&c, out_key), "aipi_provider_key");
}

/* ===================================================================== */
/* Function records                                                      */
/* ===================================================================== */

/* A name argument: NULL or "" is empty (allowed only when `optional`). */
static int32_t name_in(const char *s, char out[AIPI_NAME_MAX + 1u], bool optional,
                       const char *what)
{
    memset(out, 0, AIPI_NAME_MAX + 1u);
    if (!s || !s[0]) {
        if (optional) return ZXV_OK;
        return zxvi_fail(ZXV_E_ARG, "aipi_fn_encode: %s is %s", what, s ? "empty" : "NULL");
    }
    int32_t rc = zxvi_check_str(s, AIPI_NAME_MAX, what, false);
    if (rc) return rc;
    for (const char *p = s; *p; p++) {
        char ch = *p;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '_'))
            return zxvi_fail(ZXV_E_ARG, "aipi_fn_encode: %s has a character outside [A-Za-z0-9_]",
                             what);
    }
    memcpy(out, s, strlen(s));
    return ZXV_OK;
}

static int32_t raw_cid_in(const uint8_t *b, size_t n, ipfsn_cid_t *c, const char *what)
{
    int32_t rc = cid_in(b, n, c, what);
    if (rc) return rc;
    if (c->version != 1 || c->codec != IPFSN_MC_RAW || c->mh_code != IPFSN_MH_SHA2_256)
        return zxvi_fail(ZXV_E_CID, "%s: must be a CIDv1 raw sha2-256", what);
    return ZXV_OK;
}

static int32_t byte_field(uint32_t v, const char *what, uint8_t *out)
{
    if (v > 255u) return zxvi_fail(ZXV_E_ARG, "aipi_fn_encode: %s %u is out of range", what, v);
    *out = (uint8_t) v;
    return ZXV_OK;
}

int32_t zxv_aipi_fn_encode(uint32_t intent, uint32_t verify, uint32_t privacy,
                           uint32_t determinism, uint32_t quorum_n, uint32_t quorum_k,
                           uint64_t fuel_max, const uint8_t *module_cid, size_t module_cid_len,
                           const uint8_t *contract_cid, size_t contract_cid_len,
                           const char *entry, const char *checker, const char *certifier,
                           uint8_t *buf, size_t cap, size_t *out_len)
{
    aipi_fn_t fn;
    int32_t rc;
    memset(&fn, 0, sizeof fn);
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "aipi_fn_encode: out_len is NULL");
    if ((rc = byte_field(intent, "intent", &fn.intent)) || (rc = byte_field(verify, "verify", &fn.verify)) ||
        (rc = byte_field(privacy, "privacy", &fn.privacy)) ||
        (rc = byte_field(determinism, "determinism", &fn.determinism)) ||
        (rc = byte_field(quorum_n, "quorum_n", &fn.quorum_n)) ||
        (rc = byte_field(quorum_k, "quorum_k", &fn.quorum_k)))
        return rc;
    fn.fuel_max = fuel_max;
    if ((rc = raw_cid_in(module_cid, module_cid_len, &fn.module, "aipi_fn_encode: module_cid")))
        return rc;
    if (contract_cid || contract_cid_len) {
        if ((rc = raw_cid_in(contract_cid, contract_cid_len, &fn.contract,
                             "aipi_fn_encode: contract_cid")))
            return rc;
        fn.has_contract = true;
    }
    if ((rc = name_in(entry, fn.entry, false, "entry")) ||
        (rc = name_in(checker, fn.checker, true, "checker")) ||
        (rc = name_in(certifier, fn.certifier, true, "certifier")))
        return rc;
    if (aipi_fn_check(&fn) != AIPI_OK)
        return zxvi_fail(ZXV_E_ARG, "aipi_fn_encode: the fields break a descriptor rule "
                                    "(docs/AIPI.md section 3)");
    uint8_t rec[AIPI_FN_ENC_MAX];
    int n = aipi_fn_encode(&fn, rec, sizeof rec);
    if (n <= 0) return map_aipi(n, "aipi_fn_encode");
    return bytes_out(rec, (size_t) n, buf, cap, out_len, "aipi_fn_encode");
}

static int32_t fn_in(const uint8_t *rec, size_t len, aipi_fn_t *fn, const char *what)
{
    if (!rec) return zxvi_fail(ZXV_E_ARG, "%s: rec is NULL", what);
    if (len > AIPI_FN_ENC_MAX)
        return zxvi_fail(ZXV_E_FORMAT, "%s: %zu bytes is longer than any function record", what,
                         len);
    if (aipi_fn_decode(rec, (uint32_t) len, fn) != AIPI_OK)
        return zxvi_fail(ZXV_E_FORMAT, "%s: not a valid, canonical function record", what);
    return ZXV_OK;
}

int32_t zxv_aipi_fn_decode(const uint8_t *rec, size_t len, uint32_t *out_intent,
                           uint32_t *out_verify, uint32_t *out_privacy, uint32_t *out_determinism,
                           uint32_t *out_quorum_n, uint32_t *out_quorum_k, uint64_t *out_fuel_max)
{
    aipi_fn_t fn;
    int32_t rc = fn_in(rec, len, &fn, "aipi_fn_decode");
    if (rc) return rc;
    if (out_intent) *out_intent = fn.intent;
    if (out_verify) *out_verify = fn.verify;
    if (out_privacy) *out_privacy = fn.privacy;
    if (out_determinism) *out_determinism = fn.determinism;
    if (out_quorum_n) *out_quorum_n = fn.quorum_n;
    if (out_quorum_k) *out_quorum_k = fn.quorum_k;
    if (out_fuel_max) *out_fuel_max = fn.fuel_max;
    return zxvi_ok();
}

int32_t zxv_aipi_fn_get_cid(const uint8_t *rec, size_t len, uint32_t field,
                            uint8_t out_cid[ZXV_CID_MAX], size_t *out_cid_len)
{
    aipi_fn_t fn;
    if (!out_cid || !out_cid_len) return zxvi_fail(ZXV_E_ARG, "aipi_fn_get_cid: NULL out");
    if (field != ZXV_AIPI_FIELD_MODULE && field != ZXV_AIPI_FIELD_CONTRACT)
        return zxvi_fail(ZXV_E_ARG, "aipi_fn_get_cid: field %u is not MODULE or CONTRACT", field);
    int32_t rc = fn_in(rec, len, &fn, "aipi_fn_get_cid");
    if (rc) return rc;
    if (field == ZXV_AIPI_FIELD_MODULE) return cid_out(&fn.module, out_cid, out_cid_len, "aipi_fn_get_cid");
    if (!fn.has_contract)
        return zxvi_fail(ZXV_E_NOT_FOUND, "aipi_fn_get_cid: the function names no contract");
    return cid_out(&fn.contract, out_cid, out_cid_len, "aipi_fn_get_cid");
}

int32_t zxv_aipi_fn_get_name(const uint8_t *rec, size_t len, uint32_t field, char *buf, size_t cap,
                             size_t *out_len)
{
    aipi_fn_t fn;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "aipi_fn_get_name: out_len is NULL");
    if (field > ZXV_AIPI_FIELD_CERTIFIER)
        return zxvi_fail(ZXV_E_ARG, "aipi_fn_get_name: field %u is not ENTRY, CHECKER or CERTIFIER",
                         field);
    int32_t rc = fn_in(rec, len, &fn, "aipi_fn_get_name");
    if (rc) return rc;
    const char *s = field == ZXV_AIPI_FIELD_ENTRY     ? fn.entry
                    : field == ZXV_AIPI_FIELD_CHECKER ? fn.checker
                                                      : fn.certifier;
    return zxvi_out_text(s, buf, cap, out_len);
}

int32_t zxv_aipi_fn_cid(const uint8_t *rec, size_t len, uint8_t out_cid[ZXV_CID_MAX],
                        size_t *out_cid_len)
{
    aipi_fn_t fn;
    ipfsn_cid_t c;
    if (!out_cid || !out_cid_len) return zxvi_fail(ZXV_E_ARG, "aipi_fn_cid: NULL out");
    int32_t rc = fn_in(rec, len, &fn, "aipi_fn_cid");
    if (rc) return rc;
    if ((rc = map_aipi(aipi_fn_cid(&fn, &c), "aipi_fn_cid"))) return rc;
    return cid_out(&c, out_cid, out_cid_len, "aipi_fn_cid");
}

/* ===================================================================== */
/* Call and claim records                                                */
/* ===================================================================== */

static int32_t call_args(const uint8_t *fn_cid, size_t fn_cid_len, const uint8_t *input,
                         size_t in_len, ipfsn_cid_t *f, const char *what)
{
    int32_t rc = cid_in(fn_cid, fn_cid_len, f, what);
    if (rc) return rc;
    if (!aipi_is_fn_codec(f->codec) || f->version != 1 || f->mh_code != IPFSN_MH_SHA2_256)
        return zxvi_fail(ZXV_E_CID, "%s: not a CIDv1 sha2-256 function CID (codec 0x%x)", what,
                         f->codec);
    if (!input && in_len) return zxvi_fail(ZXV_E_ARG, "%s: input is NULL", what);
    if (in_len > AIPI_IO_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "%s: input is %zu bytes (at most %u)", what, in_len,
                         AIPI_IO_MAX);
    return ZXV_OK;
}

int32_t zxv_aipi_call_encode(const uint8_t *fn_cid, size_t fn_cid_len, const uint8_t *input,
                             size_t in_len, uint8_t *buf, size_t cap, size_t *out_len)
{
    ipfsn_cid_t f;
    uint8_t rec[AIPI_CALL_ENC_MAX];
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "aipi_call_encode: out_len is NULL");
    int32_t rc = call_args(fn_cid, fn_cid_len, input, in_len, &f, "aipi_call_encode");
    if (rc) return rc;
    int n = aipi_call_encode(&f, input, (uint32_t) in_len, rec, sizeof rec);
    if (n <= 0) return map_aipi(n, "aipi_call_encode");
    return bytes_out(rec, (size_t) n, buf, cap, out_len, "aipi_call_encode");
}

int32_t zxv_aipi_call_decode(const uint8_t *rec, size_t len, size_t *out_fn_cid_off,
                             size_t *out_fn_cid_len, size_t *out_input_off, size_t *out_input_len)
{
    ipfsn_cid_t f;
    const uint8_t *in;
    uint32_t in_len;
    if (!rec) return zxvi_fail(ZXV_E_ARG, "aipi_call_decode: rec is NULL");
    if (len > AIPI_CALL_ENC_MAX || aipi_call_decode(rec, (uint32_t) len, &f, &in, &in_len) != AIPI_OK)
        return zxvi_fail(ZXV_E_FORMAT, "aipi_call_decode: not a valid call record");
    /* layout: "AIPC" v flags | len cid | in_len(2) input */
    if (out_fn_cid_off) *out_fn_cid_off = 7u;
    if (out_fn_cid_len) *out_fn_cid_len = rec[6];
    if (out_input_off) *out_input_off = (size_t) (in - rec);
    if (out_input_len) *out_input_len = in_len;
    return zxvi_ok();
}

int32_t zxv_aipi_call_cid(const uint8_t *fn_cid, size_t fn_cid_len, const uint8_t *input,
                          size_t in_len, uint8_t out_cid[ZXV_CID_MAX], size_t *out_cid_len)
{
    ipfsn_cid_t f, c;
    if (!out_cid || !out_cid_len) return zxvi_fail(ZXV_E_ARG, "aipi_call_cid: NULL out");
    int32_t rc = call_args(fn_cid, fn_cid_len, input, in_len, &f, "aipi_call_cid");
    if (rc) return rc;
    if ((rc = map_aipi(aipi_call_cid(&f, input, (uint32_t) in_len, &c), "aipi_call_cid"))) return rc;
    return cid_out(&c, out_cid, out_cid_len, "aipi_call_cid");
}

int32_t zxv_aipi_claim_encode(const uint8_t *call_cid, size_t call_cid_len,
                              const uint8_t executor[32], const uint8_t *output, size_t output_len,
                              const uint8_t *cert, size_t cert_len, uint8_t *buf, size_t cap,
                              size_t *out_len)
{
    ipfsn_cid_t c;
    uint8_t rec[AIPI_CLAIM_ENC_MAX];
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "aipi_claim_encode: out_len is NULL");
    int32_t rc = cid_in(call_cid, call_cid_len, &c, "aipi_claim_encode");
    if (rc) return rc;
    if (c.codec != AIPI_MC_CALL || c.version != 1 || c.mh_code != IPFSN_MH_SHA2_256)
        return zxvi_fail(ZXV_E_CID, "aipi_claim_encode: not a CIDv1 sha2-256 call CID");
    if (!executor || (!output && output_len) || (!cert && cert_len))
        return zxvi_fail(ZXV_E_ARG, "aipi_claim_encode: NULL argument");
    if (output_len > AIPI_IO_MAX || cert_len > AIPI_CERT_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "aipi_claim_encode: output %zu / cert %zu bytes (at most %u each)",
                         output_len, cert_len, AIPI_IO_MAX);
    int n = aipi_claim_encode(&c, executor, output, (uint32_t) output_len, cert,
                              (uint32_t) cert_len, rec, sizeof rec);
    if (n <= 0) return map_aipi(n, "aipi_claim_encode");
    return bytes_out(rec, (size_t) n, buf, cap, out_len, "aipi_claim_encode");
}

int32_t zxv_aipi_claim_decode(const uint8_t *rec, size_t len, size_t *out_call_off,
                              size_t *out_call_len, size_t *out_executor_off,
                              size_t *out_output_off, size_t *out_output_len,
                              size_t *out_cert_off, size_t *out_cert_len)
{
    aipi_claim_t c;
    if (!rec) return zxvi_fail(ZXV_E_ARG, "aipi_claim_decode: rec is NULL");
    if (len > AIPI_CLAIM_ENC_MAX || aipi_claim_decode(rec, (uint32_t) len, &c) != AIPI_OK)
        return zxvi_fail(ZXV_E_FORMAT, "aipi_claim_decode: not a valid claim record");
    /* layout: "AIPR" v flags | len cid | executor(32) | out_len(2) out | cert_len(2) cert */
    if (out_call_off) *out_call_off = 7u;
    if (out_call_len) *out_call_len = rec[6];
    if (out_executor_off) *out_executor_off = 7u + (size_t) rec[6];
    if (out_output_off) *out_output_off = (size_t) (c.out - rec);
    if (out_output_len) *out_output_len = c.out_len;
    if (out_cert_off) *out_cert_off = (size_t) (c.cert - rec);
    if (out_cert_len) *out_cert_len = c.cert_len;
    return zxvi_ok();
}

/* ===================================================================== */
/* The node handle                                                       */
/* ===================================================================== */

#define AIPI_HANDLE_MAGIC 0x5A584149u /* "ZXAI" */

struct zxv_aipi {
    uint32_t magic;
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    ipfsn_memstore_t mem;
    ipfsn_storage_ops_t ops;
    ipfsn_bs_t bs;
    ipfsn_node_t node;
    aipi_ctx_t ac;
    aipi_job_t job; /* aipi_init needs one; no job is ever started here */
    ipfsn_bs_entry_t *tab;
    ipfsn_pin_t *pins;
    uint8_t *store, *scratch, *module_buf, *contract_buf, *get_buf;
    size_t store_cap;
};

static int32_t handle_ok(const zxv_aipi *a, const char *what)
{
    if (!a || a->magic != AIPI_HANDLE_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: handle is NULL or not a zxv_aipi", what);
    return zxvi_ctx_check(a->ctx, what);
}

static int32_t gated(const zxv_cap *cap, zxv_aipi *a, const char *what)
{
    if (!a || a->magic != AIPI_HANDLE_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: handle is NULL or not a zxv_aipi", what);
    return zxvi_cap_gate_ctx(cap, ZXV_CAP_AIPI, what, a->ctx);
}

static void handle_free(zxv_aipi *a)
{
    if (a->scratch) zxvi_wipe(a->scratch, IPFSN_SCRATCH_MIN);
    zxvi_wipe(&a->bs, sizeof a->bs); /* derived seal keys */
    free(a->tab);
    free(a->pins);
    free(a->store);
    free(a->scratch);
    free(a->module_buf);
    free(a->contract_buf);
    free(a->get_buf);
    free(a);
}

int32_t zxv_aipi_create(const zxv_cap *cap, size_t store_bytes, uint32_t max_blocks,
                        const uint8_t *node_key, zxv_aipi **out)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_AIPI, "aipi_create", &ctx);
    if (rc) return rc;
    if (!out) return zxvi_fail(ZXV_E_ARG, "aipi_create: out is NULL");
    if (store_bytes < ZXV_AIPI_STORE_MIN || store_bytes > ZXV_AIPI_STORE_MAX)
        return zxvi_fail(ZXV_E_ARG, "aipi_create: store_bytes %zu is not %u..%u", store_bytes,
                         ZXV_AIPI_STORE_MIN, ZXV_AIPI_STORE_MAX);
    if (max_blocks == 0 || max_blocks > ZXV_AIPI_BLOCKS_MAX)
        return zxvi_fail(ZXV_E_ARG, "aipi_create: max_blocks %u is not 1..%u", max_blocks,
                         ZXV_AIPI_BLOCKS_MAX);
    /* A private block takes two index entries (plaintext and sealed CID);
     * the kernel keeps the table at most 3/4 full. */
    uint32_t tab_cap = 4;
    while ((uint64_t) tab_cap * 3u < (uint64_t) max_blocks * 2u * 4u) tab_cap <<= 1;

    zxv_aipi *a = calloc(1, sizeof *a);
    if (!a) return zxvi_fail(ZXV_E_NOMEM, "aipi_create: allocation failed");
    a->tab = calloc(tab_cap, sizeof *a->tab);
    a->pins = calloc(max_blocks, sizeof *a->pins);
    a->store = calloc(1, store_bytes);
    a->module_buf = calloc(1, AIPI_MODULE_MAX);
    a->contract_buf = calloc(1, AIPI_CONTRACT_MAX);
    a->get_buf = calloc(1, IPFSN_BLOCK_MAX);
    if (node_key) a->scratch = calloc(1, IPFSN_SCRATCH_MIN);
    if (!a->tab || !a->pins || !a->store || !a->module_buf || !a->contract_buf || !a->get_buf ||
        (node_key && !a->scratch)) {
        handle_free(a);
        return zxvi_fail(ZXV_E_NOMEM, "aipi_create: allocation failed");
    }
    a->store_cap = store_bytes;
    a->mem.buf = a->store;
    a->mem.cap = store_bytes;
    a->mem.len = 0;
    ipfsn_memstore_ops(&a->mem, &a->ops);
    int r = ipfsn_bs_init(&a->bs, &a->ops, a->tab, tab_cap, a->scratch,
                          a->scratch ? IPFSN_SCRATCH_MIN : 0);
    if (r == IPFSN_OK && node_key) ipfsn_bs_set_key(&a->bs, node_key);
    if (r == IPFSN_OK) r = ipfsn_node_init(&a->node, &a->bs, a->pins, max_blocks, NULL, 0);
    if (r == IPFSN_OK)
        r = aipi_init(&a->ac, &a->node, &a->job, 1, NULL, 0, a->module_buf, a->contract_buf);
    if (r != 0) {
        handle_free(a);
        return zxvi_fail(ZXV_E_INTERNAL, "aipi_create: kernel init status %d", r);
    }
    /* Offline by construction: no peer, gateway or mesh hooks are bound. */
    a->ac.online = false;
    a->ctx = ctx;
    pthread_mutex_init(&a->mu, NULL);
    a->magic = AIPI_HANDLE_MAGIC;
    *out = a;
    return zxvi_ok();
}

void zxv_aipi_destroy(zxv_aipi *a)
{
    if (!a || a->magic != AIPI_HANDLE_MAGIC) return;
    a->magic = 0;
    pthread_mutex_destroy(&a->mu);
    handle_free(a);
}

/* Visibility is decided once. The kernel's blockstore lets a sealed block
 * be stored again in plaintext before the pin check refuses the conflict,
 * so the conflict is refused here before anything is written. */
static int32_t vis_conflict(zxv_aipi *a, const ipfsn_cid_t *c, bool priv, const char *what)
{
    bool held_private = ipfsn_is_private(&a->node, c);
    bool held_public = ipfsn_bs_visibility(&a->bs, c) == (int) IPFSN_VIS_PUBLIC && !held_private;
    if (priv ? held_public : held_private)
        return zxvi_fail(ZXV_E_STATE, "%s: already held %s; making it %s must be deliberate", what,
                         priv ? "public" : "private", priv ? "private" : "public");
    if (priv && !a->bs.have_key)
        return zxvi_fail(ZXV_E_STATE, "%s: private blocks need a node key (zxv_aipi_create)", what);
    return ZXV_OK;
}

int32_t zxv_aipi_put(const zxv_cap *cap, zxv_aipi *a, const uint8_t *data, size_t len,
                     int32_t is_private, uint8_t out_cid[ZXV_CID_MAX], size_t *out_cid_len)
{
    int32_t rc = gated(cap, a, "aipi_put");
    if (rc) return rc;
    if ((!data && len) || !out_cid || !out_cid_len)
        return zxvi_fail(ZXV_E_ARG, "aipi_put: NULL argument");
    if (len > IPFSN_BLOCK_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "aipi_put: %zu bytes (at most %u)", len, IPFSN_BLOCK_MAX);
    ipfsn_cid_t c;
    ipfsn_cid_sha256(IPFSN_MC_RAW, data, (uint32_t) len, &c);
    pthread_mutex_lock(&a->mu);
    rc = vis_conflict(a, &c, is_private != 0, "aipi_put");
    if (!rc)
        rc = map_aipi(aipi_put_blob(&a->ac, data, (uint32_t) len,
                                    is_private ? IPFSN_VIS_PRIVATE : IPFSN_VIS_PUBLIC, &c),
                      "aipi_put");
    pthread_mutex_unlock(&a->mu);
    if (rc) return rc;
    return cid_out(&c, out_cid, out_cid_len, "aipi_put");
}

int32_t zxv_aipi_get(zxv_aipi *a, const uint8_t *cid, size_t cid_len, uint8_t *buf, size_t cap,
                     size_t *out_len)
{
    ipfsn_cid_t c;
    int32_t rc = handle_ok(a, "aipi_get");
    if (rc) return rc;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "aipi_get: out_len is NULL");
    if ((rc = cid_in(cid, cid_len, &c, "aipi_get"))) return rc;
    pthread_mutex_lock(&a->mu);
    uint32_t n = 0;
    rc = map_aipi(aipi_get(&a->ac, &c, a->get_buf, IPFSN_BLOCK_MAX, &n, NULL), "aipi_get");
    if (!rc) rc = bytes_out(a->get_buf, n, buf, cap, out_len, "aipi_get");
    zxvi_wipe(a->get_buf, n); /* a private block's plaintext does not linger */
    pthread_mutex_unlock(&a->mu);
    return rc;
}

int32_t zxv_aipi_publish_fn(const zxv_cap *cap, zxv_aipi *a, const uint8_t *rec, size_t len,
                            uint8_t out_cid[ZXV_CID_MAX], size_t *out_cid_len)
{
    aipi_fn_t fn;
    ipfsn_cid_t c;
    int32_t rc = gated(cap, a, "aipi_publish_fn");
    if (rc) return rc;
    if (!out_cid || !out_cid_len) return zxvi_fail(ZXV_E_ARG, "aipi_publish_fn: NULL out");
    if ((rc = fn_in(rec, len, &fn, "aipi_publish_fn"))) return rc;
    if ((rc = map_aipi(aipi_fn_cid(&fn, &c), "aipi_publish_fn"))) return rc;
    pthread_mutex_lock(&a->mu);
    rc = vis_conflict(a, &c, fn.privacy == AIPI_PRIV_LOCAL, "aipi_publish_fn");
    if (!rc) rc = map_aipi(aipi_publish_fn(&a->ac, &fn, &c), "aipi_publish_fn");
    pthread_mutex_unlock(&a->mu);
    if (rc) return rc;
    return cid_out(&c, out_cid, out_cid_len, "aipi_publish_fn");
}

/* Load and check a function by CID, with the handle locked. */
static int32_t load_locked(zxv_aipi *a, const ipfsn_cid_t *f, aipi_fn_t *fn, const char *what)
{
    if (!aipi_is_fn_codec(f->codec))
        return zxvi_fail(ZXV_E_CID, "%s: codec 0x%x is not an AIPI function codec", what, f->codec);
    return map_aipi(aipi_load_fn(&a->ac, f, fn), what);
}

int32_t zxv_aipi_load_fn(zxv_aipi *a, const uint8_t *fn_cid, size_t fn_cid_len, uint8_t *buf,
                         size_t cap, size_t *out_len)
{
    ipfsn_cid_t f;
    aipi_fn_t fn;
    uint8_t rec[AIPI_FN_ENC_MAX];
    int32_t rc = handle_ok(a, "aipi_load_fn");
    if (rc) return rc;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "aipi_load_fn: out_len is NULL");
    if ((rc = cid_in(fn_cid, fn_cid_len, &f, "aipi_load_fn"))) return rc;
    pthread_mutex_lock(&a->mu);
    rc = load_locked(a, &f, &fn, "aipi_load_fn");
    pthread_mutex_unlock(&a->mu);
    if (rc) return rc;
    int n = aipi_fn_encode(&fn, rec, sizeof rec);
    if (n <= 0) return map_aipi(n, "aipi_load_fn");
    return bytes_out(rec, (size_t) n, buf, cap, out_len, "aipi_load_fn");
}

int32_t zxv_aipi_plan(zxv_aipi *a, const uint8_t *fn_cid, size_t fn_cid_len, const uint8_t *input,
                      size_t in_len, int32_t input_private, int32_t *out_route)
{
    ipfsn_cid_t f, call;
    aipi_fn_t fn;
    int32_t rc = handle_ok(a, "aipi_plan");
    if (rc) return rc;
    if (!out_route) return zxvi_fail(ZXV_E_ARG, "aipi_plan: out_route is NULL");
    if ((rc = call_args(fn_cid, fn_cid_len, input, in_len, &f, "aipi_plan"))) return rc;
    if ((rc = map_aipi(aipi_call_cid(&f, input, (uint32_t) in_len, &call), "aipi_plan"))) return rc;
    pthread_mutex_lock(&a->mu);
    rc = load_locked(a, &f, &fn, "aipi_plan");
    int route = rc ? 0 : aipi_plan(&a->ac, &fn, &call, input_private != 0);
    pthread_mutex_unlock(&a->mu);
    if (rc) return rc;
    if (route < AIPI_ROUTE_CACHE || route > AIPI_ROUTE_UNAVAILABLE)
        return map_aipi(route, "aipi_plan");
    *out_route = route;
    return zxvi_ok();
}

int32_t zxv_aipi_info(zxv_aipi *a, uint32_t *out_blocks, uint64_t *out_store_used,
                      uint32_t *out_gets_verified, uint32_t *out_forbidden)
{
    int32_t rc = handle_ok(a, "aipi_info");
    if (rc) return rc;
    pthread_mutex_lock(&a->mu);
    if (out_blocks) *out_blocks = a->bs.count;
    if (out_store_used) *out_store_used = a->bs.end;
    if (out_gets_verified) *out_gets_verified = a->ac.gets_verified;
    if (out_forbidden) *out_forbidden = a->ac.forbidden;
    pthread_mutex_unlock(&a->mu);
    return zxvi_ok();
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aipi.h — AIPI, the content-addressed AI Programming Interface.
 *
 * A REST URL says WHERE a function lives. An AIPI CID says WHAT it is: the
 * hash of a function descriptor that names the exact Z-Wasm module, the
 * export to call, the Z-Contract boundary and how a result is checked. Any
 * node that holds those bytes can serve or run the function, and the caller
 * checks what comes back. Design and limits: docs/AIPI.md.
 *
 * FOUR KINDS OF CID (all real CIDv1, sha2-256, full 256-bit digests)
 *   data       raw (0x55) / dag-pb (0x70): weights, ledger states, modules,
 *              contract sources. AIPI_GET verifies by hash, as IPFS does.
 *   function   codec = the intent (zxv-fn, zxv-tensor-infer,
 *              zxv-settle-ledger, zxv-swarm-route), digest = SHA-256 of the
 *              canonical function descriptor (aipi_fn_encode).
 *   call       codec zxv-aipi-call, digest = SHA-256 of (function CID, input).
 *              Names one invocation; verified results of deterministic
 *              functions are memoized under it.
 *   claim      not a CID: an executor's answer to a call (aipi_claim_encode).
 *   Every AIPI codec is in the multicodec PRIVATE-USE range (0x300000 to
 *   0x3FFFFF). None is registered; other IPFS software treats them as
 *   unknown codecs but still verifies the sha2-256 multihash.
 *
 * WHAT A CID CAN AND CANNOT PROVE
 *   GET     the bytes hash to the CID or they are dropped. That is proof.
 *   EXECUTE a function CID fixes WHICH code and contract ran, not whether a
 *           returned output is right. So a remote result is accepted only by
 *           one of these, named in the descriptor:
 *     CHECKER  a cheap checker export (an inverse: isqrt is checked by
 *              squaring, a settlement by A = L + E, a sort by order and
 *              multiset) runs locally on (input, output).
 *     CERT     as CHECKER, but the executor must also send a certificate the
 *              checker uses (a factorisation, a witness, a Merkle path).
 *     QUORUM   k of n independent executors return the same output bytes
 *              (k > n/2). Needs a deterministic function; ZXV's integer
 *              engine and float-free Z-Wasm give bit-identical results
 *              across machines. Distinct node IDs are not proof of distinct
 *              operators: the host chooses whom to ask (agreements, trust).
 *     LOCAL    never accept a remote result: run it here.
 *   There is no "trust the executor" mode.
 *
 * RULES
 *   A1 Private or key-bearing work (descriptor privacy LOCAL, or the caller
 *      marks the input private) is never broadcast. Only the public code may
 *      be fetched; the call runs here or not at all.
 *   A2 A bounty is reserved (escrow) when a call goes to the mesh and is paid
 *      only to executors whose result passed verification. Otherwise it is
 *      refunded. paid + refunded == reserved, always.
 *   A3 Offline, or with no mesh bound, a call runs from the local cache
 *      (blockstore) if the code is there; otherwise it fails with
 *      AIPI_ERR_OFFLINE. Nothing pretends to succeed.
 *   A4 Every job carries a 64-state hexagram tag (src/lpres/lpres_hex.h):
 *      potential -> emerging (dispatched) -> manifest (verified). A rejected
 *      result makes the job BOTH until a valid one arrives; a failed quorum
 *      is BOTH/entropic, whose action is ESCALATE (to a person).
 *   A5 Bytes that do not hash to the CID they were sent as are FORBIDDEN:
 *      dropped, and logged to the quarantine log if one is bound.
 *   A6 Availability is not guaranteed. A CID resolves only while some
 *      reachable node (or the local cache) holds the bytes it names.
 *
 * Freestanding C11: integer only, no libc, no allocation (all tables and
 * buffers are the caller's), no 64-bit division. Not thread-safe: one
 * context per thread, or a lock around it.
 */
#ifndef ZXV_AIPI_H
#define ZXV_AIPI_H

#include <stdbool.h>
#include <stdint.h>
#include "../ipfs_node/ipfs_node.h"
#include "../lpres/lpres_hex.h"

/* ===== Results ============================================================ */
enum {
    AIPI_OK = 0,
    AIPI_ERR_ARG = -1,       /* NULL or out-of-range argument                     */
    AIPI_ERR_MALFORMED = -2, /* bytes do not parse under the strict rules         */
    AIPI_ERR_UNSUPP = -3,    /* well-formed, but not an AIPI function codec etc.  */
    AIPI_ERR_SPACE = -4,     /* caller buffer or table too small                  */
    AIPI_ERR_HASH = -5,      /* bytes do not hash to the CID (A5)                 */
    AIPI_ERR_NOTFOUND = -6,  /* no reachable source holds the bytes (A6)          */
    AIPI_ERR_PRIVATE = -7,   /* refused: private work never leaves the node (A1)  */
    AIPI_ERR_OFFLINE = -8,   /* offline and not in the local cache (A3)           */
    AIPI_ERR_STATE = -9,     /* job is not in a state that accepts this           */
    AIPI_ERR_DUP = -10,      /* same executor answered twice                      */
    AIPI_ERR_FULL = -11,     /* job table or claim slots full                     */
    AIPI_ERR_FUNDS = -12,    /* the bounty could not be reserved                  */
    AIPI_ERR_EXEC = -13,     /* the executor (Z-Wasm) failed: trap, fuel, contract */
    AIPI_ERR_CODEC = -14,    /* function CID codec does not match its intent      */
    AIPI_ERR_TRANSPORT = -15 /* the mesh refused the broadcast                    */
};

/* ===== Codes (multicodec private-use range; NOT registered) =============== */
#define AIPI_MC_BASE          0x3A1000u
#define AIPI_MC_FN            0x3A1000u /* zxv-fn: a general function        */
#define AIPI_MC_TENSOR_INFER  0x3A1001u /* zxv-tensor-infer                  */
#define AIPI_MC_SETTLE_LEDGER 0x3A1002u /* zxv-settle-ledger                 */
#define AIPI_MC_SWARM_ROUTE   0x3A1003u /* zxv-swarm-route                   */
#define AIPI_MC_CALL          0x3A10C0u /* zxv-aipi-call: function + input   */

enum {
    AIPI_INTENT_FN = 0,
    AIPI_INTENT_TENSOR_INFER = 1,
    AIPI_INTENT_SETTLE_LEDGER = 2,
    AIPI_INTENT_SWARM_ROUTE = 3,
    AIPI_INTENT_COUNT = 4
};
enum {
    AIPI_VERIFY_CHECKER = 1,
    AIPI_VERIFY_CERT = 2,
    AIPI_VERIFY_QUORUM = 3,
    AIPI_VERIFY_LOCAL = 4
};
enum { AIPI_PRIV_PUBLIC = 0, AIPI_PRIV_LOCAL = 1 }; /* LOCAL: private or key-bearing */
enum { AIPI_DET_NONDETERMINISTIC = 0, AIPI_DET_DETERMINISTIC = 1 };

/* ===== Limits ============================================================== */
#define AIPI_NAME_MAX     63u        /* export names: [A-Za-z0-9_], 1..63     */
#define AIPI_IO_MAX       1024u      /* call input and output bytes           */
#define AIPI_CERT_MAX     1024u      /* certificate bytes in a claim          */
#define AIPI_CLAIMS_MAX   8u         /* claims kept per job (and quorum n)    */
#define AIPI_MODULE_MAX   (1u << 20) /* one raw block: the Z-Wasm module      */
#define AIPI_CONTRACT_MAX 16384u     /* Z-Contract source text                */
#define AIPI_FN_ENC_MAX                                                                            \
    (4u + 1u + 6u + 8u + 2u * (1u + IPFSN_CID_BIN_MAX) + 3u * (1u + AIPI_NAME_MAX))
#define AIPI_CALL_ENC_MAX (4u + 1u + 1u + IPFSN_CID_BIN_MAX + 2u + AIPI_IO_MAX)
#define AIPI_CLAIM_ENC_MAX                                                                         \
    (4u + 1u + 1u + IPFSN_CID_BIN_MAX + 32u + 2u + AIPI_IO_MAX + 2u + AIPI_CERT_MAX)
#define AIPI_TEXT_MAX 128u /* 'z' + base58btc of a CID + NUL        */

/* ===== Function descriptor ================================================ */
typedef struct {
    uint8_t intent;      /* AIPI_INTENT_*                                         */
    uint8_t verify;      /* AIPI_VERIFY_*                                         */
    uint8_t privacy;     /* AIPI_PRIV_*                                           */
    uint8_t determinism; /* AIPI_DET_*                                            */
    uint8_t quorum_n;    /* QUORUM only: executors asked (2..AIPI_CLAIMS_MAX)     */
    uint8_t quorum_k;    /* QUORUM only: matching results needed, k > n/2         */
    uint64_t fuel_max;   /* most fuel one run may use (> 0)                       */
    ipfsn_cid_t module;  /* CIDv1 raw sha2-256 of the Z-Wasm module bytes        */
    bool has_contract;
    ipfsn_cid_t contract;               /* CIDv1 raw sha2-256 of the contract text */
    char entry[AIPI_NAME_MAX + 1u];     /* export that computes the result        */
    char checker[AIPI_NAME_MAX + 1u];   /* CHECKER/CERT: export that checks it    */
    char certifier[AIPI_NAME_MAX + 1u]; /* CERT: export that makes the certificate */
} aipi_fn_t;

/* Validate the rules in docs/AIPI.md section 3 (names, modes, quorum,
 * determinism, codecs). AIPI_OK or AIPI_ERR_MALFORMED. */
int aipi_fn_check(const aipi_fn_t *fn);
/* Canonical bytes. Returns the length, or a negative AIPI_ERR_*. */
int aipi_fn_encode(const aipi_fn_t *fn, uint8_t *out, uint32_t cap);
/* Strict parse: every field checked, no trailing bytes. */
int aipi_fn_decode(const uint8_t *in, uint32_t len, aipi_fn_t *fn);
/* The function CID: codec from the intent, sha2-256 of the canonical bytes. */
int aipi_fn_cid(const aipi_fn_t *fn, ipfsn_cid_t *cid);
uint32_t aipi_intent_codec(uint8_t intent);
/* True for the four function codecs. */
bool aipi_is_fn_codec(uint32_t codec);

/* ===== Call records and claims ============================================ */
int aipi_call_encode(const ipfsn_cid_t *fn_cid, const uint8_t *in, uint32_t in_len, uint8_t *out,
                     uint32_t cap);
/* *in points into `rec`. */
int aipi_call_decode(const uint8_t *rec, uint32_t len, ipfsn_cid_t *fn_cid, const uint8_t **in,
                     uint32_t *in_len);
int aipi_call_cid(const ipfsn_cid_t *fn_cid, const uint8_t *in, uint32_t in_len, ipfsn_cid_t *cid);

typedef struct {
    ipfsn_cid_t call;     /* the call answered                           */
    uint8_t executor[32]; /* the executor's NodeID (Vinea: SHA3-256 of pk) */
    const uint8_t *out;   /* points into the claim bytes                 */
    uint32_t out_len;
    const uint8_t *cert;
    uint32_t cert_len;
} aipi_claim_t;

/* The claim bytes are what an executor signs (ML-DSA, Vinea identity) and
 * what the host verifies before aipi_submit: this library takes the executor
 * id as already authenticated by the session that delivered it. */
int aipi_claim_encode(const ipfsn_cid_t *call, const uint8_t executor[32], const uint8_t *out,
                      uint32_t out_len, const uint8_t *cert, uint32_t cert_len, uint8_t *buf,
                      uint32_t cap);
int aipi_claim_decode(const uint8_t *buf, uint32_t len, aipi_claim_t *c);

/* ===== Text form and DHT key ============================================== */
/* 'z' + base58btc of the binary CID (multibase), e.g. "zDP...". Returns the
 * length (NUL written) or negative. */
int aipi_cid_to_text(const ipfsn_cid_t *cid, char *out, uint32_t cap);
/* Accepts 'z' base58btc and 'b' base32-lower CIDv1 text. */
int aipi_cid_from_text(const char *s, uint32_t len, ipfsn_cid_t *cid);
/* Kademlia key under which providers of a CID are announced on Vinea:
 * SHA-256("zxv/aipi/v1/provider" || binary CID). Keys never truncate. */
int aipi_provider_key(const ipfsn_cid_t *cid, uint8_t key[32]);

/* ===== Host hooks ========================================================== */
typedef struct {
    const aipi_fn_t *fn;
    const uint8_t *module; /* verified against fn->module */
    uint32_t module_len;
    const uint8_t *contract; /* verified against fn->contract; NULL when none */
    uint32_t contract_len;
} aipi_code_t;

/* The sandbox (sdk/kits/aipi binds Z-Wasm + Z-Contract). Each returns
 * AIPI_OK / AIPI_ERR_* except check, which returns 1 (output is right for
 * the input), 0 (it is not) or a negative error (could not check). */
typedef struct {
    void *ctx;
    int (*run)(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len, uint8_t *out,
               uint32_t cap, uint32_t *out_len);
    int (*check)(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                 const uint8_t *out, uint32_t out_len, const uint8_t *cert, uint32_t cert_len);
    /* CERT functions, executor side: the certificate for (in, out). */
    int (*certify)(void *ctx, const aipi_code_t *code, const uint8_t *in, uint32_t in_len,
                   const uint8_t *out, uint32_t out_len, uint8_t *cert, uint32_t cap,
                   uint32_t *cert_len);
} aipi_exec_ops_t;

/* The mesh (Vinea) and the economy. broadcast sends the call record to up to
 * `want` executors (providers of the function CID, chosen by the host's
 * agreements and trust); 0 = sent. reserve puts `amount` in escrow; 0 = held.
 * pay / refund move escrowed funds; they are only ever called as A2 says. */
typedef struct {
    void *ctx;
    int (*broadcast)(void *ctx, const ipfsn_cid_t *call_cid, const uint8_t *call_rec, uint32_t len,
                     uint64_t bounty, uint32_t want);
    int (*reserve)(void *ctx, uint64_t amount);
    void (*pay)(void *ctx, const uint8_t executor[32], uint64_t amount);
    void (*refund)(void *ctx, uint64_t amount);
} aipi_mesh_ops_t;

/* ===== Jobs ================================================================ */
enum {
    AIPI_JOB_FREE = 0,
    AIPI_JOB_PENDING = 1,
    AIPI_JOB_DONE = 2,
    AIPI_JOB_DISPUTED = 3,
    AIPI_JOB_EXPIRED = 4,
    AIPI_JOB_FAILED = 5
};
enum {
    AIPI_ROUTE_CACHE = 1,
    AIPI_ROUTE_LOCAL = 2,
    AIPI_ROUTE_FETCH_LOCAL = 3,
    AIPI_ROUTE_MESH = 4,
    AIPI_ROUTE_UNAVAILABLE = 5
};
enum { AIPI_V_ACCEPTED = 1, AIPI_V_REJECTED = 2, AIPI_V_COUNTED = 3, AIPI_V_DISPUTED = 4 };

typedef struct {
    uint8_t executor[32];
    uint8_t verdict; /* AIPI_V_* (COUNTED while a quorum is open) */
    uint16_t out_len;
    uint8_t out[AIPI_IO_MAX];
} aipi_slot_t;

typedef struct {
    uint8_t status; /* AIPI_JOB_*            */
    uint8_t route;  /* AIPI_ROUTE_*          */
    lpx_t tag;      /* A4                    */
    uint8_t nclaims;
    uint8_t naccepted, nrejected;
    bool input_private;
    aipi_fn_t fn;
    ipfsn_cid_t fn_cid, call_cid;
    uint16_t in_len, out_len;
    uint8_t in[AIPI_IO_MAX];
    uint8_t out[AIPI_IO_MAX]; /* the verified result */
    uint64_t bounty, paid, refunded, deadline;
    aipi_slot_t slot[AIPI_CLAIMS_MAX];
} aipi_job_t;

typedef struct {
    ipfsn_cid_t call;
    ipfsn_cid_t output; /* raw CID of the output bytes in the blockstore */
    uint8_t used;
} aipi_memo_t;

typedef struct {
    ipfsn_node_t *node; /* local blockstore + peer/gateway sources (src/ipfs_node) */
    aipi_exec_ops_t exec;
    aipi_mesh_ops_t mesh;
    bool online;       /* false: local cache only (A3) */
    bool prefer_local; /* run here when the code is cached, even if online */
    uint8_t self[32];  /* this node's id, put in the claims it serves */
    aipi_job_t *jobs;
    uint32_t job_cap;
    aipi_memo_t *memo;
    uint32_t memo_cap, memo_next;
    lpx_qlog_t *qlog;      /* optional: A5 quarantine records */
    uint8_t *module_buf;   /* >= AIPI_MODULE_MAX */
    uint8_t *contract_buf; /* >= AIPI_CONTRACT_MAX */
    uint8_t fn_buf[AIPI_FN_ENC_MAX];
    uint8_t call_buf[AIPI_CALL_ENC_MAX];
    /* audit counters */
    uint32_t forbidden, gets_verified, broadcasts;
} aipi_ctx_t;

int aipi_init(aipi_ctx_t *c, ipfsn_node_t *node, aipi_job_t *jobs, uint32_t job_cap,
              aipi_memo_t *memo, uint32_t memo_cap, uint8_t *module_buf, uint8_t *contract_buf);

/* ===== AIPI_GET ============================================================ */
/* Fetch the bytes a CID names: local blockstore, then (online) Vinea peers,
 * then the trustless gateway, each verified by hash. A source that returns
 * other bytes is FORBIDDEN (A5) and the next source is tried. Private CIDs
 * stay local. *tag reports the outcome (may be NULL). */
int aipi_get(aipi_ctx_t *c, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len,
             lpx_t *tag);

/* Store bytes as a raw block (module, contract text, dataset) and pin them.
 * vis = IPFSN_VIS_PUBLIC or IPFSN_VIS_PRIVATE. */
int aipi_put_blob(aipi_ctx_t *c, const uint8_t *data, uint32_t len, uint32_t vis, ipfsn_cid_t *cid);
/* Store a descriptor (PRIVATE when fn->privacy is LOCAL) and return its CID. */
int aipi_publish_fn(aipi_ctx_t *c, const aipi_fn_t *fn, ipfsn_cid_t *fn_cid);
/* GET a descriptor by function CID, check its codec matches its intent. */
int aipi_load_fn(aipi_ctx_t *c, const ipfsn_cid_t *fn_cid, aipi_fn_t *fn);

/* ===== AIPI_EXECUTE ======================================================== */
/* The route a call would take now (pure: no side effects). */
int aipi_plan(aipi_ctx_t *c, const aipi_fn_t *fn, const ipfsn_cid_t *call_cid, bool input_private);

/* Start a call. CACHE / LOCAL / FETCH_LOCAL routes finish before returning
 * (job DONE or FAILED). MESH reserves `bounty`, broadcasts and leaves the
 * job PENDING until claims verify (aipi_submit) or it expires (aipi_tick).
 * Returns AIPI_OK with *job set, or AIPI_ERR_*. */
int aipi_execute(aipi_ctx_t *c, const ipfsn_cid_t *fn_cid, const uint8_t *in, uint32_t in_len,
                 bool input_private, uint64_t bounty, uint64_t now, uint64_t ttl, uint32_t *job);

/* A claim arrived for `job` (bytes as received; executor authenticated by
 * the host). *verdict is AIPI_V_*. Payment happens here, only on accept. */
int aipi_submit(aipi_ctx_t *c, uint32_t job, const uint8_t *claim, uint32_t len, uint8_t *verdict);

/* Expire pending jobs whose deadline has passed: refund, tag NEITHER/
 * entropic. Returns how many expired. */
uint32_t aipi_tick(aipi_ctx_t *c, uint64_t now);

/* The verified output (status DONE only). */
int aipi_result(const aipi_ctx_t *c, uint32_t job, uint8_t *out, uint32_t cap, uint32_t *len);
const aipi_job_t *aipi_job(const aipi_ctx_t *c, uint32_t job);
/* Free a finished job's slot. A PENDING job cannot be released (refund it by
 * letting it expire). */
int aipi_release(aipi_ctx_t *c, uint32_t job);

/* ===== Executor side ======================================================= */
/* Answer a call record received from the mesh: load the function (fetching
 * code by CID if needed), refuse private functions, run it, and write a claim
 * signed-over bytes into `claim`. Returns the claim length or AIPI_ERR_*. */
int aipi_serve(aipi_ctx_t *c, const uint8_t *call_rec, uint32_t len, uint8_t *claim, uint32_t cap);

const char *aipi_status_name(int status);

#endif /* ZXV_AIPI_H */

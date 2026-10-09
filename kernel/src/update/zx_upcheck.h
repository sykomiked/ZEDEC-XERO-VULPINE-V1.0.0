/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zx_upcheck.h — check IPFS update buckets for signed ZXV updates.
 *
 * WHAT THIS IS
 * ------------
 * An optional checker. With it switched off nothing is fetched, ever, and the
 * system runs as if it did not exist. Switched on, it looks at a list of
 * update BUCKETS. A bucket is a UnixFS directory on IPFS, named either by
 *
 *  - a fixed CID (immutable: the same CID always names the same bytes), or
 *  - an IPNS name (k51…, an Ed25519 key) whose signed record points at the
 *    bucket's current root, so the publisher can move it forward.
 *
 * The built-in entry is the owner's bucket, ZXU_BUILTIN_CID (a CIDv1 dag-pb
 * directory). Because a CID is immutable, following a CHANGING bucket needs
 * an IPNS name; zxu_bucket_set_ipns() switches any bucket, the built-in one
 * included, to a name. People can add their own buckets (zxu_bucket_add_*),
 * remove them, and turn each one on or off.
 *
 * TRUST
 * -----
 * Each bucket has its own set of trusted ML-DSA-65 (FIPS 204) release keys,
 * chosen by the user and nobody else. A listing's popularity or ratings
 * (kernel/src/social) never add a key: there is no API here that takes one.
 * A bucket's root must hold `zxv-update.manifest` and its detached signature
 * `zxv-update.manifest.sig`, made by build_system/zxv_publish_update.py. Only
 * a manifest whose signature verifies under one of THAT bucket's keys can
 * yield an installable update. Anything else (no manifest, no signature, a
 * bad signature, an unknown key, a bucket with no keys) is reported as
 * UNSIGNED, with the directory listing so the UI can show what is there, and
 * is never installable.
 *
 * Every byte comes through src/ipfs_node and is re-hashed against its CID:
 * a gateway or peer that lies is caught, whichever one it is. IPNS records
 * are checked as the IPNS spec says (signatureV2 over "ipns-signature:" +
 * DAG-CBOR data, the V1 fields matching, EOL in the future) and the highest
 * sequence number seen per bucket is kept: an older record is a rollback and
 * is refused, as is a second record with the same sequence and another value.
 * Manifests carry an `issued` time; an older one than the bucket has already
 * accepted is a rollback too.
 *
 * Buckets are checked independently: a failure in one (network, a bad
 * record, junk content) changes nothing in the others. A network failure
 * changes no state at all; the caller simply carries on.
 *
 * COMPOSITION
 * -----------
 * Outcomes feed the existing policy in update.h: zxu_to_catalog() publishes a
 * verified bucket's applicable entries as catalog entries (still opt-in:
 * nothing is selected), and zxu_catalog_verify / zxu_catalog_transport let
 * upd_fetch_verify() re-check them. Staging and rollback stay with
 * src/loader/abupdate. A plain callback reports outcomes to the notification
 * bus (another module), once per new root.
 *
 * MANIFEST FORMAT (strict ASCII, LF line ends, single spaces, nothing else)
 * ------------------------------------------------------------------------
 *     zxv-update-manifest 1
 *     release X.Y.Z
 *     min-version X.Y.Z
 *     issued UNIX_SECONDS
 *     entry KIND ARCH X.Y.Z SIZE CID SHA256HEX PATH     (1..32 lines)
 *     end
 *  - X, Y, Z are 0..999, no leading zeros. release >= min-version; each entry's
 *    version <= release.
 *  - KIND: kernel app module firmware data doc. ARCH: x86_64 aarch64 riscv64
 *    riscv32 arm32 x86 any.
 *  - SIZE: decimal file length, no leading zeros, < 2^32.
 *  - CID: CIDv1 base32, codec raw or dag-pb, sha2-256: the file's UnixFS root.
 *  - SHA256HEX: 64 lowercase hex digits, SHA-256 of the whole file.
 *  - PATH: relative to the bucket root, components [A-Za-z0-9._+-]+ (not "."
 *    or ".."), joined by '/', 1..200 bytes, never the manifest or its .sig.
 *  - entries strictly ascending by PATH bytes (so no duplicates); the file
 *    ends right after "end\n"; at most ZXU_MANIFEST_MAX bytes.
 *  - Signature: `zxv-update.manifest.sig` is exactly 3309 bytes, ML-DSA-65
 *    (pure, FIPS 204) with context string "zxv-update-manifest-v1" over the
 *    exact manifest bytes.
 *
 * HONEST LIMITS
 * -------------
 *  - IPNS: Ed25519 names only (k51… base36, b… base32 libp2p-key CIDs, and
 *    12D3KooW… peer IDs). RSA, secp256k1 and ECDSA names are refused. The
 *    record's Value must be exactly /ipfs/<cid> (no sub-path, no /ipns/
 *    chains, no DNSLink). ValidityType must be EOL. The record comes from one
 *    gateway, which cannot forge it but CAN withhold a newer one (a freeze):
 *    the TTL is reported but nothing here detects freezing beyond `issued`.
 *    The IPNS spec was fetched (raw.githubusercontent.com, ipfs/specs) and
 *    followed; its six conformance records and real Kubo 0.32.1 records are
 *    the tests.
 *  - Time comes from the caller (`now`, Unix seconds). With no clock (now ==
 *    0) IPNS buckets are refused (ZXU_ST_NO_CLOCK); fixed-CID buckets work.
 *  - Anti-rollback state (sequence, issued) lives in zxu_config_t; it is only
 *    as durable as the caller's storage of that struct.
 *  - ML-DSA verification is behind zxu_sigverify_fn. zx_upcheck_mldsa.c binds
 *    it to pq_mldsa65_verify; the pqsec glue pulls hosted headers and may
 *    not build bare-metal, so the checker core does not link it directly.
 *  - zxu_catalog_verify/transport keep a small static table (64 entries) of
 *    what verified, because upd_verify_fn has no context pointer; single
 *    threaded, like update.c.
 *  - The listing for the UI holds the first ZXU_LIST_MAX root entries.
 *  - Ed25519 comes from robin_debanks/ed25519_verify (orlp, vendored).
 *
 * Freestanding C11: integer only, no libc, no allocation, no 64-bit division.
 */
#ifndef ZXV_ZX_UPCHECK_H
#define ZXV_ZX_UPCHECK_H

#include <stdint.h>
#include <stdbool.h>
#include "../ipfs_node/ipfs_node.h"
#include "../ipfs_node/ipfsn_dir.h"
#include "update.h"

/* The owner's bucket. */
#define ZXU_BUILTIN_CID   "bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354"
#define ZXU_BUILTIN_LABEL "ZXV updates"

#define ZXU_MANIFEST_NAME "zxv-update.manifest"
#define ZXU_SIG_NAME      "zxv-update.manifest.sig"
#define ZXU_SIG_CONTEXT   "zxv-update-manifest-v1"

#define ZXU_MAX_BUCKETS      8u
#define ZXU_MAX_KEYS         4u
#define ZXU_PK_BYTES         1952u /* ML-DSA-65 public key */
#define ZXU_SIG_BYTES        3309u /* ML-DSA-65 signature  */
#define ZXU_MANIFEST_MAX     16384u
#define ZXU_MAX_ENTRIES      32u
#define ZXU_PATH_MAX         200u
#define ZXU_LABEL_MAX        48u
#define ZXU_NAME_MAX         128u
#define ZXU_IPNS_RECORD_MAX  10240u /* the spec's size limit */
#define ZXU_LIST_MAX         64u
#define ZXU_MIN_INTERVAL     900u /* 15 minutes: never hammer a gateway */
#define ZXU_DEFAULT_INTERVAL 86400u

/* Results of the building blocks. */
enum {
    ZXU_OK = 0,
    ZXU_ERR_ARG = -1,
    ZXU_ERR_MALFORMED = -2,
    ZXU_ERR_UNSUPP = -3,
    ZXU_ERR_SPACE = -4,
    ZXU_ERR_SIG = -5,      /* a signature does not verify             */
    ZXU_ERR_EXPIRED = -6,  /* IPNS record past its EOL                */
    ZXU_ERR_ROLLBACK = -7, /* older than what this bucket accepted    */
    ZXU_ERR_NAME = -8,     /* record does not belong to this name     */
    ZXU_ERR_FULL = -9,
    ZXU_ERR_EXISTS = -10,
    ZXU_ERR_BUILTIN = -11, /* the built-in bucket cannot be removed   */
    ZXU_ERR_NOTFOUND = -12,
    ZXU_ERR_NETWORK = -13,
    ZXU_ERR_DISABLED = -14,
    ZXU_ERR_HASH = -15,     /* downloaded bytes do not match the manifest */
    ZXU_ERR_NOTSIGNED = -16 /* asked to download from an unverified result */
};

/* Per-bucket outcome. Only ZXU_ST_UPDATE_AVAILABLE is installable. */
typedef enum {
    ZXU_ST_DISABLED = 0,      /* checker or bucket off: nothing was fetched */
    ZXU_ST_NOT_DUE,           /* checked recently; nothing was fetched      */
    ZXU_ST_NETWORK,           /* could not reach / got bad bytes: no change */
    ZXU_ST_NO_CLOCK,          /* IPNS needs the time                        */
    ZXU_ST_IPNS_INVALID,      /* malformed record, bad signature, wrong key */
    ZXU_ST_IPNS_EXPIRED,      /* record past its EOL                        */
    ZXU_ST_IPNS_ROLLBACK,     /* older sequence than already seen           */
    ZXU_ST_BAD_BUCKET,        /* root is not a (valid) directory            */
    ZXU_ST_UNSIGNED,          /* see unsigned_reason; NEVER installable     */
    ZXU_ST_MANIFEST_INVALID,  /* signed, but breaks the format              */
    ZXU_ST_MANIFEST_ROLLBACK, /* signed, but older than one already seen    */
    ZXU_ST_UP_TO_DATE,        /* signed; nothing newer for this system      */
    ZXU_ST_INCOMPATIBLE,      /* signed and newer, but needs min-version    */
    ZXU_ST_UPDATE_AVAILABLE   /* signed, newer, for this arch               */
} zxu_status_t;

enum {
    ZXU_UNS_NONE = 0,
    ZXU_UNS_NO_MANIFEST, /* no zxv-update.manifest in the root           */
    ZXU_UNS_NO_SIG,      /* manifest but no (well-sized) signature file  */
    ZXU_UNS_NO_KEYS,     /* the user trusts no key for this bucket yet   */
    ZXU_UNS_BAD_SIG,     /* no trusted key verifies (bad sig or unknown) */
    ZXU_UNS_TOO_BIG      /* manifest over ZXU_MANIFEST_MAX: unverifiable  */
};

/* Notification kinds for the notify callback. */
enum { ZXU_NOTIFY_UPDATE = 1, ZXU_NOTIFY_UNSIGNED = 2, ZXU_NOTIFY_INCOMPATIBLE = 3 };

enum { ZXU_SRC_CID = 1, ZXU_SRC_IPNS = 2 };

enum {
    ZXU_ARCH_ANY = 0,
    ZXU_ARCH_X86_64,
    ZXU_ARCH_AARCH64,
    ZXU_ARCH_RISCV64,
    ZXU_ARCH_RISCV32,
    ZXU_ARCH_ARM32,
    ZXU_ARCH_X86
};

enum {
    ZXU_KIND_KERNEL = 0,
    ZXU_KIND_APP,
    ZXU_KIND_MODULE,
    ZXU_KIND_FIRMWARE,
    ZXU_KIND_DATA,
    ZXU_KIND_DOC
};

/* Versions are packed X*1000000 + Y*1000 + Z. */
#define ZXU_VERSION(x, y, z) ((uint32_t) (x) * 1000000u + (uint32_t) (y) * 1000u + (uint32_t) (z))
int zxu_version_parse(const char *s, uint32_t len, uint32_t *v);
int zxu_version_format(uint32_t v, char *out, uint32_t cap); /* "X.Y.Z", length or negative */

/* ===== Manifest ============================================================ */

typedef struct {
    char path[ZXU_PATH_MAX + 1]; /* NUL-terminated */
    uint32_t path_len;
    uint8_t kind, arch;
    uint32_t version;
    uint32_t size;
    ipfsn_cid_t cid;
    uint8_t sha256[32];
    bool in_bucket; /* set by the checker: PATH resolves to CID in the bucket */
} zxu_entry_t;

typedef struct {
    uint32_t release, min_version;
    uint64_t issued;
    uint32_t n;
    zxu_entry_t e[ZXU_MAX_ENTRIES];
} zxu_manifest_t;

/* Strict parse (rules above). ZXU_OK or ZXU_ERR_MALFORMED. Does NOT check
 * the signature: that is zxu_manifest_verify. */
int zxu_manifest_parse(const uint8_t *text, uint32_t len, zxu_manifest_t *m);

/* ML-DSA-65 verify with a FIPS 204 context string. true iff valid. */
typedef bool (*zxu_sigverify_fn)(void *ctx, const uint8_t pk[ZXU_PK_BYTES], const uint8_t *msg,
                                 uint32_t len, const uint8_t *dom, uint32_t dom_len,
                                 const uint8_t sig[ZXU_SIG_BYTES]);

/* Try each of `nkeys` keys. ZXU_OK with *which = the key that verified, or
 * ZXU_ERR_SIG. */
int zxu_manifest_verify(zxu_sigverify_fn fn, void *ctx, const uint8_t (*keys)[ZXU_PK_BYTES],
                        uint32_t nkeys, const uint8_t *text, uint32_t len,
                        const uint8_t sig[ZXU_SIG_BYTES], uint32_t *which);

/* The pq_mldsa65_verify binding (zx_upcheck_mldsa.c, hosted / pqsec builds). */
bool zxu_mldsa65_verify(void *ctx, const uint8_t pk[ZXU_PK_BYTES], const uint8_t *msg, uint32_t len,
                        const uint8_t *dom, uint32_t dom_len, const uint8_t sig[ZXU_SIG_BYTES]);

/* ===== IPNS ================================================================ */

/* Ed25519 IPNS name -> its 32-byte public key. Accepts k51… (base36 CIDv1
 * libp2p-key), b… (base32 of the same) and 12D3KooW… (base58 peer ID). */
int zxu_ipns_name_parse(const char *s, uint32_t len, uint8_t pubkey[32]);
/* Canonical "k51…" string (what the gateway URL uses). Length or negative. */
int zxu_ipns_name_string(const uint8_t pubkey[32], char *out, uint32_t cap);

typedef struct {
    ipfsn_cid_t value; /* /ipfs/<value> */
    uint64_t sequence;
    uint64_t eol;    /* Unix seconds */
    uint32_t eol_ns; /* and nanoseconds */
    uint64_t ttl_ns;
    bool has_v1; /* legacy V1 fields were present (and matched) */
} zxu_ipns_t;

/* Verify a serialized IpnsEntry for the name whose key is `pubkey`, at
 * time `now` (Unix seconds, non-zero). scratch >= 15 + len. ZXU_OK, or
 * ZXU_ERR_MALFORMED / _SIG / _NAME / _EXPIRED / _UNSUPP. */
int zxu_ipns_verify(const uint8_t *rec, uint32_t len, const uint8_t pubkey[32], uint64_t now,
                    uint8_t *scratch, uint32_t scap, zxu_ipns_t *out);

/* RFC 3339 -> Unix seconds + ns (exposed for tests). Years 1970..9999. */
int zxu_rfc3339_parse(const uint8_t *s, uint32_t len, uint64_t *secs, uint32_t *ns);

/* ===== Configuration ======================================================= */

typedef struct {
    bool used, enabled, builtin;
    uint8_t source; /* ZXU_SRC_CID / ZXU_SRC_IPNS */
    char label[ZXU_LABEL_MAX];
    ipfsn_cid_t cid;     /* ZXU_SRC_CID */
    uint8_t ipns_pk[32]; /* ZXU_SRC_IPNS: the name's key */
    uint8_t keys[ZXU_MAX_KEYS][ZXU_PK_BYTES];
    uint32_t nkeys;
    /* State to persist (anti-rollback and scheduling): */
    bool have_seq;
    uint64_t ipns_seq;
    ipfsn_cid_t ipns_value;
    bool have_issued;
    uint64_t issued;
    uint8_t manifest_digest[32];
    uint64_t next_due;
    bool have_notified;
    ipfsn_cid_t notified_root;
} zxu_bucket_t;

typedef struct {
    bool enabled;        /* master switch: false -> nothing is ever fetched */
    uint8_t arch;        /* ZXU_ARCH_* of this system                       */
    uint32_t installed;  /* packed version installed now                    */
    uint32_t interval_s; /* 0 = only when asked (force); else >= 900        */
    zxu_bucket_t b[ZXU_MAX_BUCKETS];
} zxu_config_t;

/* Clear, then add the built-in bucket (ZXU_BUILTIN_CID, enabled, no keys:
 * until a release key is trusted its contents are reported UNSIGNED).
 * `release_pk` (may be NULL) is trusted for the built-in bucket. */
int zxu_config_init(zxu_config_t *c, bool enabled, uint8_t arch, uint32_t installed,
                    const uint8_t *release_pk);

/* Bucket management for the UI. idx is the slot (stable until removed). */
int zxu_bucket_add_cid(zxu_config_t *c, const char *label, const char *cid, uint32_t *idx);
int zxu_bucket_add_ipns(zxu_config_t *c, const char *label, const char *name, uint32_t *idx);
int zxu_bucket_remove(zxu_config_t *c, uint32_t idx);
int zxu_bucket_enable(zxu_config_t *c, uint32_t idx, bool on);
/* Change where a bucket points (resets its anti-rollback state). */
int zxu_bucket_set_cid(zxu_config_t *c, uint32_t idx, const char *cid);
int zxu_bucket_set_ipns(zxu_config_t *c, uint32_t idx, const char *name);
/* The user's per-bucket trust choice. */
int zxu_bucket_trust_key(zxu_config_t *c, uint32_t idx, const uint8_t pk[ZXU_PK_BYTES]);
int zxu_bucket_untrust_key(zxu_config_t *c, uint32_t idx, const uint8_t pk[ZXU_PK_BYTES]);
uint32_t zxu_bucket_count(const zxu_config_t *c);

/* ===== The checker ========================================================= */

typedef void (*zxu_notify_fn)(void *ctx, int kind, const char *title, const char *body);

/* Caller-owned work area (about 60 KiB plus the two pointed-to buffers). */
typedef struct {
    uint8_t *blk; /* >= IPFSN_BLOCK_MAX: blocks and directory-walk scratch */
    uint32_t blk_cap;
    ipfsn_walk_t *walk; /* file reassembly; walk->leaf/leaf_cap set by the caller */
    ipfsn_dir_walk_t dwalk;
    uint8_t manifest[ZXU_MANIFEST_MAX];
    uint8_t sig[ZXU_SIG_BYTES];
    uint8_t rec[ZXU_IPNS_RECORD_MAX];
    uint8_t msg[ZXU_IPNS_RECORD_MAX + 16];
} zxu_work_t;

typedef struct {
    zxu_config_t *cfg;
    ipfsn_node_t *node; /* blockstore + gateway + peers */
    zxu_work_t *work;
    zxu_sigverify_fn verify;
    void *verify_ctx;
    zxu_notify_fn notify; /* may be NULL */
    void *notify_ctx;
    uint32_t requests; /* audit: IPNS record requests made */
} zxu_t;

int zxu_init(zxu_t *u, zxu_config_t *cfg, ipfsn_node_t *node, zxu_work_t *work,
             zxu_sigverify_fn verify, void *verify_ctx, zxu_notify_fn notify, void *notify_ctx);

typedef struct {
    char name[IPFSN_DIR_NAME_MAX + 1];
    uint32_t name_len;
    ipfsn_cid_t cid;
    uint64_t tsize;
} zxu_listent_t;

typedef struct {
    zxu_status_t status;
    int detail; /* the underlying ZXU_ERR_* / IPFSN_ERR_* */
    int unsigned_reason;
    uint32_t bucket;
    bool have_root;
    ipfsn_cid_t root;
    uint64_t ipns_seq;
    bool have_manifest; /* signed AND well-formed */
    zxu_manifest_t manifest;
    uint32_t key_index;
    uint32_t n_applicable;
    uint8_t applicable[ZXU_MAX_ENTRIES]; /* manifest entry indices for this system */
    uint32_t n_list;
    bool list_truncated;
    zxu_listent_t list[ZXU_LIST_MAX];
    char title[64];
    char body[192];
} zxu_result_t;

/* Check one bucket. `force` ignores the interval. Never touches another
 * bucket's state. Returns the status (also in r->status). */
zxu_status_t zxu_check(zxu_t *u, uint32_t idx, uint64_t now, bool force, zxu_result_t *r);
/* Check every used bucket in slot order into rs[0..cap). Returns how many
 * results were written. */
uint32_t zxu_check_all(zxu_t *u, uint64_t now, bool force, zxu_result_t *rs, uint32_t cap);

bool zxu_installable(const zxu_result_t *r);
const char *zxu_status_str(zxu_status_t s);

/* ===== Downloads (resumable) =============================================== */

/* Blocks of `root` NOT in the local blockstore, as far as the present blocks
 * reveal (a missing interior node hides its children until it arrives).
 * *complete is true when nothing is missing. Purely local. */
int zxu_plan(zxu_t *u, const ipfsn_cid_t *root, ipfsn_cid_t *missing, uint32_t cap, uint32_t *n,
             bool *complete);
/* Fetch the listed blocks (peers, then gateway); each is verified and
 * stored. Continues past failures; *got = blocks now present. */
int zxu_fetch(zxu_t *u, const ipfsn_cid_t *missing, uint32_t n, uint32_t *got);
/* Plan + fetch until complete (at most max_rounds), then re-read the whole
 * file from the blockstore and check its size and SHA-256 against the signed
 * manifest. Only for an installable result. `list` is caller scratch. */
int zxu_download(zxu_t *u, const zxu_result_t *r, uint32_t entry, ipfsn_cid_t *list,
                 uint32_t list_cap, uint32_t max_rounds);

/* ===== update.h composition ================================================ */

/* Publish each applicable entry of an installable result into the catalog
 * (cid = SHA-256 of the file, author = SHA-256 of the release key, sig = an
 * attestation zxu_catalog_verify accepts). The author is added to the
 * catalog's trust set: the user's trust decision was the bucket key. Nothing
 * is selected. *published = entries added. */
int zxu_to_catalog(const zxu_result_t *r, const zxu_config_t *cfg, upd_catalog_t *c,
                   uint32_t *published);
/* upd_verify_fn: true only for a (key, content) pair zxu_to_catalog
 * recorded from a verified manifest. */
bool zxu_catalog_verify(const uint8_t *msg, uint32_t len, const uint8_t sig[64],
                        const uint8_t pubkey[32]);
/* upd_transport_t that serves recorded content out of the IPFS node. */
upd_transport_t zxu_catalog_transport(zxu_t *u);
/* Forget every recorded pair (tests, or after an uninstall). */
void zxu_catalog_reset(void);

#endif /* ZXV_ZX_UPCHECK_H */

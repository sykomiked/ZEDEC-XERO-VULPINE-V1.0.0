/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo.h — evolution without releases: capability-based compatibility,
 * first-class forks, user-chosen lineages, device profiles.
 *
 * THE IDEA
 * --------
 * There is no "ZXV 2.0". Nothing in this module reads a version number.
 * Compatibility comes from CAPABILITIES:
 *
 *   C1  Every protocol message, file format and module interface is a
 *       capability DESCRIPTOR: a family name plus a list of typed, tagged
 *       fields. Its identity is a CID of its canonical encoding (CIDv1, raw,
 *       multihash sha3-256), so two nodes that hold the same CID hold the same
 *       schema, byte for byte, whoever wrote it.
 *   C2  Peers advertise a capability SET (family name -> descriptor CID). A
 *       session negotiates, per family both sides have, the structural CORE:
 *       the fields both descriptors define the same way. Both sides compute
 *       the same core and the same core CID independently.
 *   C3  Messages are tag-length-value. A field the receiver does not know is
 *       PRESERVED and re-emitted unchanged when the message is forwarded or
 *       re-encoded (forward compatible). A known optional field that is absent
 *       takes its declared default (backward compatible).
 *   C4  MUST-UNDERSTAND. A field may carry the must-understand flag; it is
 *       sent on the wire (bit 15 of the tag). A receiver that does not know
 *       such a field rejects the whole message instead of guessing, and a
 *       sender refuses to send one to a peer whose core lacks it.
 *
 * Forks are first-class (F1-F4, evo_lineage below): a fork is a signed record
 * naming its parent records, its author key and its changes (add / replace /
 * remove of named slots). Lineage is a DAG, not a counter. Labels are free
 * text: users version their builds however they like, or not at all.
 *
 * A safety core (crypto, ledger invariants, the no-usury rule, the phi%
 * tithe, consent gates) is pinned by a conformance suite (evo_conform). A
 * peer that passes it may use money-class capabilities in a session; one
 * that fails keeps every other capability.
 *
 * HONEST LIMITS
 * -------------
 *  - Structural compatibility is not semantic compatibility. Two forks that
 *    give the same tag and type different meanings will exchange values that
 *    each reads its own way. Use evo_ext_tag() for extension tags to make
 *    accidental collisions unlikely (1 in ~28k per pair), not impossible.
 *  - Conformance is a behavioural spot check of the code a peer runs when
 *    challenged, with fresh random inputs. It cannot prove that the code the
 *    peer later runs on a real payment is the same code; the money paths
 *    should still cross-check each value (evo_tithe_is_exact) per payment.
 *  - Adoption counts are opt-in and unlinkable, which also means they are
 *    Sybil-inflatable: a hint about what people run, never a vote.
 *  - Signatures are verified through a caller hook (bind pq_matrix pqm_verify
 *    at PQM_SIG_PURPOSE_RELEASE level); this module holds no keys.
 *  - Fixed capacities (EVO_* below); exceeding one is an error, never a
 *    truncation. No transport here: descriptors, records and sketches are
 *    bytes the caller moves over Vinea / ipfs_node.
 *  - Not reentrant: evo_conform_respond / evo_conform_check and
 *    evo_profile_check use static scratch (a few KB, kept off small kernel
 *    stacks). Call them from one thread at a time.
 * Freestanding: integer only, no libc, no allocation, no 64-bit division
 * except through zt_udiv64.
 */
#ifndef ZXV_EVO_H
#define ZXV_EVO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ===== Limits ===== */
#define EVO_CID_LEN        36u   /* 0x01 0x55 0x16 0x20 || sha3-256 */
#define EVO_NAME_MAX       32u   /* family / slot / module names */
#define EVO_LABEL_MAX      32u   /* user labels on forks and profiles */
#define EVO_MAX_FIELDS     32u   /* fields per descriptor */
#define EVO_VAL_MAX        512u  /* largest BYTES/TEXT value */
#define EVO_MSG_MAX        2048u /* largest encoded message */
#define EVO_UNKNOWN_MAX    1024u /* bytes of preserved unknown fields */
#define EVO_REG_MAX        48u   /* descriptors in a registry */
#define EVO_CAPSET_MAX     32u   /* families in an advertised set */
#define EVO_DESC_ENC_MAX   (4u + 3u + EVO_NAME_MAX + 1u + EVO_MAX_FIELDS * 14u)
#define EVO_CAPSET_ENC_MAX (4u + 1u + EVO_CAPSET_MAX * (1u + EVO_NAME_MAX + EVO_CID_LEN))
#define EVO_EXT_TAG_FIRST  0x1000u /* evo_ext_tag() range start */
#define EVO_TAG_MAX        0x7fffu

/* ===== Status ===== */
typedef enum {
    EVO_OK = 0,
    EVO_ERR_ARG,              /* bad argument / malformed caller struct      */
    EVO_ERR_FULL,             /* a fixed capacity would be exceeded          */
    EVO_ERR_PARSE,            /* bytes are not a canonical encoding          */
    EVO_ERR_CID_MISMATCH,     /* content does not hash to the expected CID   */
    EVO_ERR_MISSING_REQUIRED, /* a required field is absent                  */
    EVO_ERR_MUST_UNDERSTAND,  /* an unknown must-understand field            */
    EVO_ERR_CONFLICT,         /* incompatible schemas / unresolved merge     */
    EVO_ERR_NOT_FOUND,
    EVO_ERR_DUP,            /* already present (idempotent insert)        */
    EVO_ERR_UNKNOWN_PARENT, /* lineage parent not in the DAG yet           */
    EVO_ERR_BAD_SIG,
    EVO_ERR_UNTRUSTED,
    EVO_ERR_UNSATISFIED,  /* a profile requirement has no provider       */
    EVO_ERR_NONCONFORMANT /* safety-core conformance failed              */
} evo_status_t;

const char *evo_strerror(evo_status_t s);

/* ===== Content identifiers ===== */
typedef struct {
    uint8_t b[EVO_CID_LEN];
} evo_cid_t;

/* CIDv1 / raw / sha3-256 of data[0..len). */
void evo_cid_of(const uint8_t *data, uint32_t len, evo_cid_t *out);
bool evo_cid_eq(const evo_cid_t *a, const evo_cid_t *b);
int evo_cid_cmp(const evo_cid_t *a, const evo_cid_t *b);
bool evo_cid_is_zero(const evo_cid_t *a);
/* Key id of a public key: SHA3-256 of its encoding (32 bytes). */
void evo_key_id(const uint8_t *pk, uint32_t pk_len, uint8_t out[32]);

/* ===== C1: capability descriptors ===== */
typedef enum { EVO_KIND_MESSAGE = 1, EVO_KIND_FILE = 2, EVO_KIND_MODULE_IFACE = 3 } evo_kind_t;
/* MONEY-class capabilities are gated by conformance (evo_session_gate). */
typedef enum { EVO_CLASS_GENERAL = 0, EVO_CLASS_MONEY = 1 } evo_class_t;
typedef enum { EVO_T_UINT = 1, EVO_T_BYTES = 2, EVO_T_TEXT = 3, EVO_T_CID = 4 } evo_type_t;
#define EVO_FF_REQUIRED        0x01u
#define EVO_FF_MUST_UNDERSTAND 0x02u

typedef struct {
    uint16_t tag;     /* 1..EVO_TAG_MAX */
    uint8_t type;     /* evo_type_t */
    uint8_t flags;    /* EVO_FF_* */
    uint16_t max_len; /* BYTES/TEXT: max bytes; UINT: 8; CID: EVO_CID_LEN */
    uint64_t def;     /* UINT default when absent (0 for every other type) */
} evo_field_t;

typedef struct {
    uint8_t kind;  /* evo_kind_t */
    uint8_t klass; /* evo_class_t */
    uint8_t name_len;
    char name[EVO_NAME_MAX]; /* the family: "chat.message", "zxpkg.manifest" */
    uint8_t n_fields;        /* fields kept sorted by tag */
    evo_field_t f[EVO_MAX_FIELDS];
} evo_desc_t;

/* Field names are documentation and are not part of a descriptor: renaming
 * a field never breaks anyone. The family name is part of it. */
evo_status_t evo_desc_init(evo_desc_t *d, const char *family, evo_kind_t kind, evo_class_t klass);
/* Insert in tag order. max_len 0 means the type's natural maximum. */
evo_status_t evo_desc_add(evo_desc_t *d, uint16_t tag, evo_type_t type, uint8_t flags,
                          uint16_t max_len, uint64_t def);
const evo_field_t *evo_desc_field(const evo_desc_t *d, uint16_t tag);
/* Canonical bytes. Returns length, or 0 on error / small cap. */
uint32_t evo_desc_encode(const evo_desc_t *d, uint8_t *out, uint32_t cap);
evo_status_t evo_desc_decode(const uint8_t *in, uint32_t len, evo_desc_t *d);
evo_status_t evo_desc_cid(const evo_desc_t *d, evo_cid_t *out);

/* C2: the structural core of two descriptors of the same family. A field is
 * in the core when both define its tag with the same type and default; its
 * flags are the OR, its max_len the minimum. A tag only one side defines, or
 * that the two define differently, stays out of the core; if that field is
 * REQUIRED on either side the pair is incompatible (EVO_ERR_CONFLICT).
 * Money class wins. core(a,b) == core(b,a). */
evo_status_t evo_desc_core(const evo_desc_t *a, const evo_desc_t *b, evo_desc_t *core);
/* True iff a peer speaking `want` is fully understood by `have`: the pair is
 * compatible and every field of `want` is in their core. */
bool evo_desc_satisfies(const evo_desc_t *have, const evo_desc_t *want);

/* A deterministic extension tag for a fork's own field, in
 * [EVO_EXT_TAG_FIRST, EVO_TAG_MAX], derived from a name the fork picks. */
uint16_t evo_ext_tag(const char *ext_name);

/* ===== Descriptor registry (content-addressed, self-certifying) ===== */
typedef struct {
    uint32_t n;
    evo_cid_t cid[EVO_REG_MAX];
    evo_desc_t d[EVO_REG_MAX];
} evo_registry_t;

void evo_reg_init(evo_registry_t *r);
/* Add a local descriptor; writes its CID. EVO_ERR_DUP if already held. */
evo_status_t evo_reg_add(evo_registry_t *r, const evo_desc_t *d, evo_cid_t *cid_out);
/* Add one fetched from a peer: the bytes must decode and hash to `expect`. */
evo_status_t evo_reg_add_bytes(evo_registry_t *r, const uint8_t *in, uint32_t len,
                               const evo_cid_t *expect);
const evo_desc_t *evo_reg_find(const evo_registry_t *r, const evo_cid_t *cid);

/* ===== Capability sets and sessions ===== */
typedef struct {
    uint8_t name_len;
    char name[EVO_NAME_MAX];
    evo_cid_t cid;
} evo_cap_t;

typedef struct {
    uint32_t n; /* sorted by name, one entry per family */
    evo_cap_t c[EVO_CAPSET_MAX];
} evo_capset_t;

void evo_capset_init(evo_capset_t *s);
/* Add or replace the descriptor this node uses for d's family. */
evo_status_t evo_capset_put(evo_capset_t *s, const evo_desc_t *d);
uint32_t evo_capset_encode(const evo_capset_t *s, uint8_t *out, uint32_t cap);
evo_status_t evo_capset_decode(const uint8_t *in, uint32_t len, evo_capset_t *s);

typedef struct {
    uint8_t name_len;
    char name[EVO_NAME_MAX];
    evo_cid_t local_cid, remote_cid, core_cid;
    evo_desc_t core;
    uint8_t status; /* evo_status_t: EVO_OK or EVO_ERR_CONFLICT */
    bool money;     /* core is money class */
    bool allowed;   /* usable now: compatible, and conformance-gated if money */
} evo_sess_cap_t;

typedef struct {
    uint32_t n;
    evo_sess_cap_t e[EVO_CAPSET_MAX];
    uint32_t n_need; /* remote descriptors to fetch, then negotiate again */
    evo_cid_t need[EVO_CAPSET_MAX];
} evo_session_t;

/* Negotiate. Families on only one side are simply not in the session. Every
 * local descriptor must be in `reg`; a remote one that is not is listed in
 * need[] (fetch it by CID, evo_reg_add_bytes, call again). Money-class
 * entries start with allowed = false until evo_session_gate. */
evo_status_t evo_negotiate(const evo_registry_t *reg, const evo_capset_t *local,
                           const evo_capset_t *remote, evo_session_t *s);
const evo_sess_cap_t *evo_session_find(const evo_session_t *s, const char *family);
/* Allow money-class entries iff both bitmaps are EVO_CHK_ALL. Returns the
 * number of entries allowed afterwards. */
uint32_t evo_session_gate(evo_session_t *s, uint32_t local_conform, uint32_t peer_conform);

/* ===== C3/C4: messages ===== */
typedef struct {
    uint8_t state; /* EVO_V_ABSENT / EVO_V_PRESENT / EVO_V_DEFAULTED */
    uint8_t mu;    /* must-understand bit as sent (decode) or declared (set) */
    uint16_t len;
    uint16_t off; /* into data[] */
    uint64_t u;
} evo_val_t;
#define EVO_V_ABSENT    0u
#define EVO_V_PRESENT   1u
#define EVO_V_DEFAULTED 2u

typedef struct {
    const evo_desc_t *d; /* the schema this side reads with */
    evo_val_t v[EVO_MAX_FIELDS];
    uint16_t data_len;
    uint8_t data[EVO_MSG_MAX];
    uint16_t unk_len; /* preserved unknown TLVs, ascending tag, verbatim */
    uint16_t n_unknown;
    uint8_t unk[EVO_UNKNOWN_MAX];
} evo_msg_t;

void evo_msg_init(evo_msg_t *m, const evo_desc_t *d);
evo_status_t evo_msg_set_uint(evo_msg_t *m, uint16_t tag, uint64_t v);
evo_status_t evo_msg_set_bytes(evo_msg_t *m, uint16_t tag, const uint8_t *p, uint32_t len);
evo_status_t evo_msg_clear(evo_msg_t *m, uint16_t tag);
/* Values are readable when PRESENT or DEFAULTED. */
bool evo_msg_get_uint(const evo_msg_t *m, uint16_t tag, uint64_t *v);
bool evo_msg_get_bytes(const evo_msg_t *m, uint16_t tag, const uint8_t **p, uint32_t *len);
uint8_t evo_msg_state(const evo_msg_t *m, uint16_t tag);

/* Wire: per field  u16 tag|mu<<15, u8 type, u16 len, value  (LE), fields in
 * strictly ascending tag order; UINT values minimal little-endian (0 = empty).
 * encode emits PRESENT fields and preserved unknowns, merged by tag.
 * `core` != NULL checks the message against a session core first: core-
 * required fields must be set and no set must-understand field may be
 * outside the core (EVO_ERR_MUST_UNDERSTAND). */
evo_status_t evo_msg_encode(const evo_msg_t *m, const evo_desc_t *core, uint8_t *out, uint32_t cap,
                            uint32_t *out_len);
/* decode under the local schema d. Unknown fields -> preserved; unknown
 * must-understand -> EVO_ERR_MUST_UNDERSTAND; a known optional field the
 * sender typed differently or over-long -> preserved as unknown; the same
 * for a required field -> EVO_ERR_PARSE; required absent ->
 * EVO_ERR_MISSING_REQUIRED; optional absent -> DEFAULTED. canonical input
 * re-encodes byte-identically. */
evo_status_t evo_msg_decode(const evo_desc_t *d, const uint8_t *in, uint32_t len, evo_msg_t *m);

/* ===== F1-F4: forks and lineage =====
 *   F1  A fork record = parents (0..4 record CIDs), author key id, free
 *       label, a stamp the author chooses, and changes to named slots:
 *       ADD (slot absent), REPLACE (old -> new) or REMOVE (old). Slot values
 *       are CIDs: descriptors, module manifests, model files, profiles.
 *   F2  Its CID is the CID of its canonical body; the author signs the body.
 *   F3  The state of a build is computed, not stored: for each slot, the
 *       writes in its ancestry that no later write in that ancestry
 *       supersedes are the heads; one head value is the value, several
 *       different head values are a conflict.
 *   F4  A record is accepted only if its parents are present, its signature
 *       verifies, its changes apply to its parents' merged state, and it
 *       resolves every conflict that merge has. So every accepted build has
 *       a conflict-free state, and any two lineages can always be merged by
 *       a record that says how. */
#define EVO_LIN_MAX_PARENTS 4u
#define EVO_LIN_MAX_CHANGES 8u
#define EVO_DAG_MAX         64u
#define EVO_STATE_MAX       64u
#define EVO_LIN_BODY_MAX                                                                           \
    (4u + 1u + EVO_LIN_MAX_PARENTS * EVO_CID_LEN + 32u + 1u + EVO_LABEL_MAX + 8u + 1u +            \
     EVO_LIN_MAX_CHANGES * (2u + EVO_NAME_MAX + 2u * EVO_CID_LEN))

typedef enum { EVO_CH_ADD = 1, EVO_CH_REPLACE = 2, EVO_CH_REMOVE = 3 } evo_change_op_t;

typedef struct {
    uint8_t op;
    uint8_t slot_len;
    char slot[EVO_NAME_MAX];
    evo_cid_t val; /* ADD / REPLACE: new value */
    evo_cid_t old; /* REPLACE / REMOVE: the value being replaced */
} evo_change_t;

typedef struct {
    uint8_t n_parents; /* kept sorted */
    evo_cid_t parent[EVO_LIN_MAX_PARENTS];
    uint8_t author[32];
    uint8_t label_len;
    char label[EVO_LABEL_MAX]; /* "mia-garden-build", "v7-ish", "" — anything */
    uint64_t stamp;            /* author's choice: a date, a counter, 0 */
    uint8_t n_changes;         /* kept sorted by slot, one change per slot */
    evo_change_t ch[EVO_LIN_MAX_CHANGES];
} evo_fork_t;

evo_status_t evo_fork_init(evo_fork_t *f, const uint8_t author_id[32], const char *label,
                           uint64_t stamp);
evo_status_t evo_fork_parent(evo_fork_t *f, const evo_cid_t *parent);
evo_status_t evo_fork_change(evo_fork_t *f, evo_change_op_t op, const char *slot,
                             const evo_cid_t *val, const evo_cid_t *old);
uint32_t evo_fork_encode(const evo_fork_t *f, uint8_t *out, uint32_t cap);
evo_status_t evo_fork_decode(const uint8_t *in, uint32_t len, evo_fork_t *f);

/* Signed wire form: u32 body_len | body | u32 pk_len | pk | u32 sig_len | sig.
 * The author field must equal evo_key_id(pk). */
uint32_t evo_fork_wire(const uint8_t *body, uint32_t body_len, const uint8_t *pk, uint32_t pk_len,
                       const uint8_t *sig, uint32_t sig_len, uint8_t *out, uint32_t cap);

/* Verify sig over msg under pk (both in the signer's own encoding). */
typedef bool (*evo_verify_fn)(const uint8_t *pk, uint32_t pk_len, const uint8_t *msg,
                              uint32_t msg_len, const uint8_t *sig, uint32_t sig_len, void *ctx);

typedef struct {
    evo_cid_t cid;
    evo_fork_t f;
    uint64_t anc; /* bit i: node i is a strict ancestor */
    uint8_t pidx[EVO_LIN_MAX_PARENTS];
} evo_dag_node_t;

typedef struct {
    uint32_t n;
    evo_dag_node_t node[EVO_DAG_MAX];
} evo_dag_t;

typedef struct {
    uint8_t slot_len;
    char slot[EVO_NAME_MAX];
    evo_cid_t val;
} evo_slot_t;

typedef struct {
    uint32_t n; /* sorted by slot */
    evo_slot_t s[EVO_STATE_MAX];
} evo_state_t;

void evo_dag_init(evo_dag_t *g);
/* F4. On EVO_OK or EVO_ERR_DUP, *idx is the node. */
evo_status_t evo_dag_add_wire(evo_dag_t *g, const uint8_t *wire, uint32_t len, evo_verify_fn vf,
                              void *vctx, uint32_t *idx);
int32_t evo_dag_find(const evo_dag_t *g, const evo_cid_t *cid);
bool evo_dag_is_ancestor(const evo_dag_t *g, uint32_t anc, uint32_t of);
/* F3: the build state of node idx. */
evo_status_t evo_dag_state(const evo_dag_t *g, uint32_t idx, evo_state_t *out);
/* The slots that merging these parents leaves in conflict (what a merge
 * record must resolve). Returns the count, or -1 on error. */
int32_t evo_dag_conflicts(const evo_dag_t *g, const evo_cid_t *parents, uint32_t n_parents,
                          char names[][EVO_NAME_MAX], uint32_t cap);
const evo_cid_t *evo_state_get(const evo_state_t *st, const char *slot);

/* ===== Trust (user-chosen) =====
 * Publisher trust is the update module's opt-in list (upd_trust_author /
 * upd_is_trusted, keyed by the 32-byte key id). On top of it the user may
 * PIN a lineage record (endorsing it and its whole ancestry) and RATE
 * publishers and records 1..5 stars; anything rated below min_stars is
 * blocked, as is everything descended from a blocked record. */
#define EVO_TRUST_PINS    16u
#define EVO_TRUST_RATINGS 32u
typedef bool (*evo_pub_trusted_fn)(const uint8_t key_id[32], void *ctx);

typedef struct {
    uint8_t is_key; /* 1: publisher key id; 0: record CID digest */
    uint8_t stars;  /* 1..5 */
    uint8_t id[32];
} evo_rating_t;

typedef struct {
    evo_pub_trusted_fn pub_trusted; /* bind upd_is_trusted via evo_trust_bind_upd */
    void *pub_ctx;
    uint32_t n_pins;
    evo_cid_t pin[EVO_TRUST_PINS];
    uint32_t n_ratings;
    evo_rating_t r[EVO_TRUST_RATINGS];
    uint8_t min_stars; /* 0: rating never blocks */
} evo_trust_t;

void evo_trust_init(evo_trust_t *t, evo_pub_trusted_fn fn, void *ctx);
/* Adapter for fn: ctx is an upd_catalog_t * (kernel/src/update), so the
 * publishers a user trusts for updates are the ones trusted for lineages.
 * Defined in evo_upd.c (link update.c with it). */
bool evo_trust_upd(const uint8_t key_id[32], void *upd_catalog);
/* Adapter for fn: ctx is a zxu_config_t * (kernel/src/update/zx_upcheck.h):
 * trusted iff an enabled update bucket trusts the ML-DSA-65 key whose
 * SHA3-256 is key_id. Defined in evo_zxu.c (header-only dependency). */
bool evo_trust_zxu(const uint8_t key_id[32], void *zxu_config);
evo_status_t evo_trust_pin(evo_trust_t *t, const evo_cid_t *record);
evo_status_t evo_trust_rate_key(evo_trust_t *t, const uint8_t key_id[32], uint8_t stars);
evo_status_t evo_trust_rate_record(evo_trust_t *t, const evo_cid_t *record, uint8_t stars);
uint8_t evo_trust_stars_key(const evo_trust_t *t, const uint8_t key_id[32]);
uint8_t evo_trust_stars_record(const evo_trust_t *t, const evo_cid_t *record);
/* A build is adoptable iff every record in its ancestry-or-self is either
 * endorsed by a pin (an ancestor-or-self of a pinned record) or authored by
 * a trusted, unblocked publisher, and none is blocked. On failure *why_idx
 * is the offending node. */
evo_status_t evo_trust_build_ok(const evo_trust_t *t, const evo_dag_t *g, uint32_t idx,
                                uint32_t *why_idx);

/* ===== Adoption (opt-in, privacy-preserving counts) =====
 * A k-minimum-values sketch per (variant CID, epoch). A participating node
 * contributes ONE token per epoch: SHA3-256("ZXV-EVO-adopt" || secret ||
 * variant || epoch)[0..8), where `secret` is 32 fresh random bytes it draws
 * for that epoch and then forgets. Tokens are unlinkable across epochs and
 * variants; merging sketches is idempotent, so gossip cannot double count. */
#define EVO_KMV_K         64u
#define EVO_ADOPT_ENC_MAX (4u + EVO_CID_LEN + 4u + 1u + EVO_KMV_K * 8u)
typedef struct {
    evo_cid_t variant;
    uint32_t epoch;
    uint32_t n; /* kept tokens, ascending */
    uint64_t h[EVO_KMV_K];
} evo_adopt_t;

void evo_adopt_init(evo_adopt_t *a, const evo_cid_t *variant, uint32_t epoch);
/* Does nothing (EVO_ERR_ARG) unless opted_in: consent is the caller's gate. */
evo_status_t evo_adopt_contribute(evo_adopt_t *a, bool opted_in, const uint8_t secret[32]);
evo_status_t evo_adopt_merge(evo_adopt_t *a, const evo_adopt_t *b);
/* Exact below k distinct tokens, else (k-1) * 2^64 / h_k (relative error
 * about 1/sqrt(k-2), ~13% at k = 64). */
uint64_t evo_adopt_estimate(const evo_adopt_t *a);
uint32_t evo_adopt_encode(const evo_adopt_t *a, uint8_t *out, uint32_t cap);
evo_status_t evo_adopt_decode(const uint8_t *in, uint32_t len, evo_adopt_t *a);

/* ===== Safety-core conformance ===== */
#define EVO_CHK_HASH    0x01u /* SHA3-256 (FIPS 202) */
#define EVO_CHK_TITHE   0x02u /* floor((a + isqrt(5a^2)) / 200), exact */
#define EVO_CHK_LEDGER  0x04u /* sum d_debit == sum d_credit, d_eq = d_dr - d_cr */
#define EVO_CHK_USURY   0x08u /* repayment due == principal, at any age */
#define EVO_CHK_CONSENT 0x10u /* granted && amount <= max && now < expiry */
#define EVO_CHK_ALL     0x1fu
#define EVO_RAIL_DEBIT  555u
#define EVO_RAIL_CREDIT 777u
#define EVO_RAIL_EQUITY 888u

typedef struct {
    int64_t d_debit, d_credit, d_equity; /* rails 555 / 777 / 888 */
} evo_line_t;

typedef struct {
    bool granted;
    uint64_t max_amount;
    uint64_t expires_at;
} evo_consent_t;

/* What a fork implements on its money paths. */
typedef struct {
    void (*hash)(const uint8_t *m, uint32_t len, uint8_t out[32]);
    uint64_t (*tithe)(uint64_t a);
    bool (*posting_ok)(const evo_line_t *l, uint32_t n);
    uint64_t (*repay_due)(uint64_t principal, uint32_t days);
    bool (*may_spend)(const evo_consent_t *c, uint64_t amount, uint64_t now);
} evo_core_impl_t;

/* Exact check of a claimed tithe without computing a square root:
 * t is right iff 200t - a <= a*sqrt5 < 200(t+1) - a, compared as squares
 * in 192-bit integers. */
bool evo_tithe_is_exact(uint64_t a, uint64_t t);
/* The CID of the pinned suite (its vectors and rule ids). */
void evo_conform_suite_cid(evo_cid_t *out);
/* Run the pinned vectors against impl. Returns the bitmap of passed checks. */
uint32_t evo_conform_local(const evo_core_impl_t *impl);

#define EVO_CHAL_TITHE   16u
#define EVO_CHAL_POST    8u
#define EVO_CHAL_LINES   6u
#define EVO_CHAL_USURY   8u
#define EVO_CHAL_CONSENT 8u
#define EVO_CHAL_MSG     64u
#define EVO_RESP_ENC_LEN                                                                           \
    (4u + EVO_CID_LEN + 32u + 32u + EVO_CHAL_TITHE * 8u + 1u + EVO_CHAL_USURY * 8u + 1u)

typedef struct {
    uint8_t seed[32]; /* fresh random from the verifier */
} evo_challenge_t;

typedef struct {
    evo_cid_t suite;
    uint8_t seed[32];
    uint8_t hash[32];
    uint64_t tithe[EVO_CHAL_TITHE];
    uint8_t post_ok; /* bit i: posting i accepted */
    uint64_t repay[EVO_CHAL_USURY];
    uint8_t spend_ok; /* bit i: consent case i allowed */
} evo_response_t;

/* The responder runs its own implementation on inputs expanded from seed. */
void evo_conform_respond(const evo_core_impl_t *impl, const evo_challenge_t *c, evo_response_t *r);
/* The verifier checks every answer against the suite's rules. Returns the
 * bitmap of passed checks (0 if the suite CID or seed differ). */
uint32_t evo_conform_check(const evo_challenge_t *c, const evo_response_t *r);
uint32_t evo_response_encode(const evo_response_t *r, uint8_t *out, uint32_t cap);
evo_status_t evo_response_decode(const uint8_t *in, uint32_t len, evo_response_t *r);

/* ===== Device profiles ===== */
#define EVO_MOD_MAX_CAPS 4u
#define EVO_PROFILE_MAX  24u
#define EVO_MOD_ENC_MAX                                                                            \
    (4u + EVO_CID_LEN + 1u + EVO_NAME_MAX + 2u + 2u * EVO_MOD_MAX_CAPS * EVO_CID_LEN)
#define EVO_PROFILE_ENC_MAX                                                                        \
    (4u + 1u + EVO_LABEL_MAX + 1u + EVO_PROFILE_MAX * (EVO_CID_LEN * 2u + 1u + 32u))

/* A module manifest: what a code blob provides and requires, by descriptor
 * CID. Its own CID (evo_module_cid) is what profiles and lineages name. */
typedef struct {
    evo_cid_t code; /* CID of the module's code blob */
    uint8_t name_len;
    char name[EVO_NAME_MAX];
    uint8_t n_prov, n_req;
    evo_cid_t prov[EVO_MOD_MAX_CAPS];
    evo_cid_t req[EVO_MOD_MAX_CAPS];
} evo_module_t;

uint32_t evo_module_encode(const evo_module_t *m, uint8_t *out, uint32_t cap);
evo_status_t evo_module_decode(const uint8_t *in, uint32_t len, evo_module_t *m);
evo_status_t evo_module_cid(const evo_module_t *m, evo_cid_t *out);

typedef enum { EVO_PLACE_OFF = 0, EVO_PLACE_LOCAL = 1, EVO_PLACE_PEER = 2 } evo_place_t;

typedef struct {
    evo_cid_t module;  /* manifest CID */
    evo_cid_t lineage; /* the fork record it came from (zero: unstated) */
    uint8_t place;     /* evo_place_t */
    uint8_t peer[32];  /* EVO_PLACE_PEER: key id of the device that runs it */
} evo_prof_entry_t;

typedef struct {
    uint8_t label_len;
    char label[EVO_LABEL_MAX];
    uint8_t n; /* sorted by module CID */
    evo_prof_entry_t e[EVO_PROFILE_MAX];
} evo_profile_t;

evo_status_t evo_profile_init(evo_profile_t *p, const char *label);
evo_status_t evo_profile_put(evo_profile_t *p, const evo_cid_t *module, const evo_cid_t *lineage,
                             evo_place_t place, const uint8_t peer[32]);
uint32_t evo_profile_encode(const evo_profile_t *p, uint8_t *out, uint32_t cap);
evo_status_t evo_profile_decode(const uint8_t *in, uint32_t len, evo_profile_t *p);
evo_status_t evo_profile_cid(const evo_profile_t *p, evo_cid_t *out);
/* Every required descriptor of every module that is not OFF must be
 * satisfied (evo_desc_satisfies) by a descriptor some non-OFF module
 * provides, whichever fork either came from. With g != NULL, an entry that
 * names a lineage must find its manifest CID in that build's state. On
 * failure *bad is the entry index. */
evo_status_t evo_profile_check(const evo_profile_t *p, const evo_module_t *mods, uint32_t n_mods,
                               const evo_registry_t *reg, const evo_dag_t *g, uint32_t *bad);

#endif /* ZXV_EVO_H */

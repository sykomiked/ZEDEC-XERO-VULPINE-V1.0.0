/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_kin.h — a chiglet's digital DNA, its hobbies, and its visits to other
 * chiglets. Implements section 5d of the swarm model lineup
 * (zxv-docs/SWARM_MODEL_LINEUP_2026-10-15.md).
 *
 * K1  DIGITAL DNA. A fixed record: owner key id, species (hash of the tier
 *     pack manifest it was born from), lineage (hash of base model and
 *     adapters), parent DNA hash (zero for a chiglet born to a person; set
 *     for every agent the agency spawns, so the family tree is a hash
 *     chain), a birth seed, and the 8 genes swarm_dna grows from that seed
 *     (the genes zui_chiglet draws, so a visiting chiglet looks like
 *     itself on any screen). Its id is SHA3-256 of the record. Birth checks
 *     the id against a registry and re-seeds on a clash, so ids are unique.
 *     The owner signs the record bytes in their companion manifest; this
 *     module only builds and checks them.
 * K2  HOBBIES. 3 to 5 picks from a catalogue of 144, chosen by the DNA id,
 *     with a per-agency registry that re-picks on an identical set. Each
 *     hobby has a weight that starts from the DNA.
 * K3  CONSTRUCTIVE DEVIATION WITHOUT DRIFT.
 *       - Hobby time comes only from the growth band of the token budget:
 *         chgk_hobby_budget() takes the idle growth tokens and nothing else.
 *       - Hobbies break ties between EQUALLY scored proposals and never
 *         change a score: chgk_hobby_tiebreak().
 *       - Weights move toward observed interest by at most 3 per cycle
 *         (swarm_dna's mutation step), and only while the caller reports
 *         task accuracy and surplus above their thresholds.
 *       - Drift = ISF independence u between the current weights and the
 *         DNA weights. Past the cap the hobbies reset to the DNA and the
 *         reset is counted.
 * K4  VISITS. When two people connect, their chiglets may talk inside a
 *     GRANT both owners sign: which topics, how deep (claims, or claims and
 *     verdicts), from when until when. Either owner can revoke it, a grant
 *     id is accepted once (replays refused), and the host can end a visit
 *     at any moment. What passes is claims and verdicts with an evidence
 *     hash, never raw data, files or keys: there is no message kind for
 *     them. Every exchange goes into both chiglets' hash-chained logs so
 *     each owner can see what their chiglet learned and from whom.
 * K5  EXPERIENCE. A chiglet takes home only claims that passed its own
 *     checker. Each one earns experience in its topic scaled by the ISF
 *     surplus of the pair, f(u) = ln(1 + (N-1)u) with u the independence of
 *     the two genomes (different chiglets teach more), and divided by how
 *     often the two have met (no farming one friend). Cooperation earns
 *     Social capital points the swarm economy can read.
 *
 * HONEST LIMITS
 *  - Uniqueness is per registry (one device or one agency), not global.
 *  - The ISF surplus here scores genome difference, a proxy for what a
 *    partner can teach; it is not a measure of the claims themselves.
 *  - Signatures go through a verify hook; this module holds no keys.
 *  - Freestanding: integer only (Q32.32 through surplus.h), no allocation.
 */
#ifndef ZXV_CHG_KIN_H
#define ZXV_CHG_KIN_H

#include <stdint.h>
#include <stdbool.h>

#define CHGK_GENES        8u
#define CHGK_HOBBY_CAT    144u
#define CHGK_HOBBY_MIN    3u
#define CHGK_HOBBY_MAX    5u
#define CHGK_HOBBY_STEP   3u   /* per-cycle bound, swarm_dna's F(4) */
#define CHGK_DRIFT_CAP    150u /* permille of ISF u from the DNA profile */
#define CHGK_REG_MAX      64u
#define CHGK_DNA_ENC      (4u + 32u * 4u + 8u + CHGK_GENES + 1u + 4u)
#define CHGK_TOPICS       32u
#define CHGK_PARTNERS     16u
#define CHGK_SEEN_MAX     32u

typedef enum {
    CHGK_OK = 0,
    CHGK_ERR_ARG = -1,
    CHGK_ERR_FULL = -2,
    CHGK_ERR_PARSE = -3,
    CHGK_ERR_SIG = -4,
    CHGK_ERR_REPLAY = -5,
    CHGK_ERR_REVOKED = -6,
    CHGK_ERR_EXPIRED = -7,
    CHGK_ERR_SCOPE = -8,   /* topic or depth not granted */
    CHGK_ERR_CLOSED = -9,  /* no open visit */
    CHGK_ERR_PARTY = -10,  /* the grant does not name these two chiglets */
    CHGK_ERR_DUP = -11     /* a named birth whose id is already registered */
} chgk_status_t;

/* ===== K1 ===== */
typedef struct {
    uint8_t owner[32];
    uint8_t species[32];
    uint8_t lineage[32];
    uint8_t parent[32]; /* zero: born to a person */
    uint64_t seed;
    uint8_t genes[CHGK_GENES];
    uint8_t named;      /* 1: born from the person's name (chg_name) */
    uint32_t generation;
    uint8_t id[32];     /* SHA3-256 of the encoded record */
} chgk_dna_t;

typedef struct {
    uint32_t n;
    uint8_t id[CHGK_REG_MAX][32];
    uint32_t n_sets;
    uint8_t set[CHGK_REG_MAX][CHGK_HOBBY_MAX]; /* sorted, 0xff padded */
} chgk_registry_t;

void chgk_registry_init(chgk_registry_t *r);
/* Birth: build the record, re-seed (seed+1, ...) until its id is not in r,
 * then register it. parent may be NULL. */
chgk_status_t chgk_dna_birth(chgk_registry_t *r, const uint8_t owner[32],
                             const uint8_t species[32], const uint8_t lineage[32],
                             const chgk_dna_t *parent, uint64_t seed, chgk_dna_t *out);
uint32_t chgk_dna_encode(const chgk_dna_t *d, uint8_t *out, uint32_t cap);
/* A person names their chiglet once (kernel/src/chiglet/chg_name.h): the
 * seed comes from the name and the name's sound leans temperament, warmth
 * and pace. The leaned genes are stored in the hashed record, so the
 * personality is fixed by the id. Every other gene must be the seed's. A
 * named birth never re-seeds: an id already in r is CHGK_ERR_DUP. Named
 * chiglets are born to a person (no parent). The name and salt are not in
 * the record and never travel. */
chgk_status_t chgk_dna_birth_named(chgk_registry_t *r, const uint8_t owner[32],
                                   const uint8_t species[32], const uint8_t lineage[32],
                                   uint64_t name_seed, const uint8_t genes[CHGK_GENES],
                                   chgk_dna_t *out);
/* Decode and recompute the id; the genes must be the ones the seed grows
 * (for a named record, except the three the name leans). */
chgk_status_t chgk_dna_decode(const uint8_t *in, uint32_t len, chgk_dna_t *d);
/* ISF independence of two genomes, permille (0 identical direction). */
uint32_t chgk_dna_distance(const chgk_dna_t *a, const chgk_dna_t *b);

/* ===== K2 / K3 ===== */
typedef struct {
    uint8_t n;
    uint8_t pick[CHGK_HOBBY_MAX];  /* catalogue indices, sorted */
    uint16_t dna_w[CHGK_HOBBY_MAX];/* weights the DNA gave, 1..1000 */
    uint16_t w[CHGK_HOBBY_MAX];    /* current weights */
    uint32_t cycles, resets;
} chgk_hobbies_t;

const char *chgk_hobby_name(uint32_t idx);
/* Picks from the DNA id; with r != NULL re-picks until no other member of
 * the registry has the same set, and registers it. */
chgk_status_t chgk_hobbies_for(chgk_registry_t *r, const chgk_dna_t *d, chgk_hobbies_t *h);
/* One growth cycle. interest[i] (0..1000) is how much hobby i engaged the
 * member this cycle; task_ok is the caller's Paper E test (task accuracy
 * and surplus above thresholds). Returns true if drift forced a reset. */
bool chgk_hobby_step(chgk_hobbies_t *h, const uint16_t interest[CHGK_HOBBY_MAX], bool task_ok);
/* Drift from the DNA profile, permille. */
uint32_t chgk_hobby_drift(const chgk_hobbies_t *h);
/* Split idle growth-band tokens across the hobbies by weight. Returns the
 * tokens handed out (<= growth_idle; never more). */
uint64_t chgk_hobby_budget(const chgk_hobbies_t *h, uint64_t growth_idle,
                           uint64_t out[CHGK_HOBBY_MAX]);
/* Among n proposals, the index with the highest score; when several share
 * that score, the one whose hobby tag (catalogue index, 0xff none) the
 * member weights most. Never prefers a lower score. -1 if n == 0. */
int32_t chgk_hobby_tiebreak(const chgk_hobbies_t *h, const uint32_t *score, const uint8_t *tag,
                            uint32_t n);

/* ===== K4 ===== */
#define CHGK_DEPTH_CLAIMS   1u
#define CHGK_DEPTH_VERDICTS 2u
#define CHGK_GRANT_ENC      (4u + 16u + 32u + 32u + 4u + 1u + 8u + 8u)

typedef struct {
    uint8_t gid[16];      /* fresh random per grant */
    uint8_t visitor[32];  /* DNA id of the visiting chiglet */
    uint8_t host[32];     /* DNA id of the host chiglet */
    uint32_t topics;      /* bitmap over CHGK_TOPICS */
    uint8_t depth;        /* CHGK_DEPTH_* */
    uint64_t not_before, expires;
} chgk_grant_t;

uint32_t chgk_grant_encode(const chgk_grant_t *g, uint8_t *out, uint32_t cap);
chgk_status_t chgk_grant_decode(const uint8_t *in, uint32_t len, chgk_grant_t *g);

typedef bool (*chgk_verify_fn)(const uint8_t *pk, uint32_t pk_len, const uint8_t *msg,
                               uint32_t msg_len, const uint8_t *sig, uint32_t sig_len, void *ctx);

/* Both owners must have signed the grant bytes. */
chgk_status_t chgk_grant_verify(const uint8_t *body, uint32_t len, const uint8_t *pk_visitor_owner,
                                const uint8_t *pk_host_owner, uint32_t pk_len,
                                const uint8_t *sig_v, const uint8_t *sig_h, uint32_t sig_len,
                                chgk_verify_fn vf, void *vctx, chgk_grant_t *out);

typedef struct {
    uint32_t n_seen, n_revoked;
    uint8_t seen[CHGK_SEEN_MAX][16];
    uint8_t revoked[CHGK_SEEN_MAX][16];
} chgk_grantbook_t;

void chgk_grantbook_init(chgk_grantbook_t *b);
chgk_status_t chgk_revoke(chgk_grantbook_t *b, const uint8_t gid[16]);

typedef enum { CHGK_K_CLAIM = 1, CHGK_K_VERDICT = 2 } chgk_kind_t;

typedef struct {
    uint8_t kind;          /* chgk_kind_t: there is no kind for raw data */
    uint8_t topic;         /* < CHGK_TOPICS */
    uint8_t verdict;       /* Belnap 0 N, 1 T, 2 F, 3 B (VERDICT only) */
    uint8_t claim[32];     /* hash of the claim text */
    uint8_t evidence[32];  /* hash of its evidence bundle */
} chgk_claim_t;

/* ===== K5 ===== */
typedef struct {
    uint8_t self[32];                 /* this chiglet's DNA id */
    uint32_t xp[CHGK_TOPICS];         /* experience per topic, permille units */
    uint64_t social;                  /* cooperation points */
    uint32_t n_partners;
    uint8_t partner[CHGK_PARTNERS][32];
    uint32_t meetings[CHGK_PARTNERS];
    uint32_t taken, refused;
    uint8_t log_head[32];             /* hash chain over every exchange */
    uint32_t log_len;
} chgk_xp_t;

typedef struct {
    bool open;
    chgk_grant_t g;
    uint8_t partner[32];
    uint32_t partner_gap;  /* genome independence, permille */
    uint32_t meeting;      /* how many times these two have met, 1-based */
    uint32_t exchanged;
} chgk_visit_t;

void chgk_xp_init(chgk_xp_t *x, const chgk_dna_t *self);
/* Open a visit from this chiglet's side (host or visitor): the grant must
 * name self and partner as its two parties, be in time, unrevoked and
 * never used before here. */
chgk_status_t chgk_visit_open(chgk_visit_t *v, chgk_xp_t *x, chgk_grantbook_t *book,
                              const chgk_grant_t *g, const chgk_dna_t *self,
                              const chgk_dna_t *partner, uint64_t now);
/* One claim from the partner. checker_passed is this chiglet's own checker
 * on it. In scope and passed: experience; in scope and not passed: logged,
 * nothing learned. Out of scope / expired / closed: refused, not logged. */
chgk_status_t chgk_visit_receive(chgk_visit_t *v, chgk_xp_t *x, const chgk_claim_t *c,
                                 bool checker_passed, uint64_t now, uint32_t *xp_gained);
/* Either side, any time. */
void chgk_visit_close(chgk_visit_t *v);
/* Recompute the log chain from entries (for the owner's audit). */
void chgk_log_step(uint8_t head[32], const uint8_t partner[32], const chgk_claim_t *c,
                   bool taken);

#endif /* ZXV_CHG_KIN_H */

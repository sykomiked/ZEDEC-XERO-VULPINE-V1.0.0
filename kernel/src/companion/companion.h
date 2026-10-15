/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* companion.h — the Chiglet companion (roadmap 6.6c, steps C.1-C.4 and C.7).
 *
 * A personal companion that wraps a language model: the model proposes, and
 * this module decides what may happen. It is built on three parts that
 * already exist: the Chiglet decision gate (src/chiglet, ISF-weighted
 * experts with a first-class UNCERTAIN), the 64-state tag and its action
 * table (src/lpres/lpres_hex.h), and ML-DSA-87 signatures (src/pqsec).
 *
 *   C.1 MANIFEST ("digital DNA"). A fixed 296-byte record: companion id,
 *       SHA-256 of the owner's ML-DSA-87 public key, name, the capability
 *       ceiling, behaviour limits (token budget, spend limits), memory
 *       layout and up to four model hashes (for example the .zxvc package
 *       digest of its language model). The owner signs SHA-256 of
 *       "zxv/companion/manifest/v1" || encoding. A companion starts only from
 *       a manifest its owner signed, and every change needs a higher epoch,
 *       the same companion id and the CURRENT owner's signature (so a new
 *       owner key is handed over by the old one).
 *   C.2 CAPABILITIES. The companion starts with none. A grant is a message
 *       the owner signs (companion id, manifest epoch, a sequence number and
 *       the capability bits); it must fall inside the manifest ceiling and
 *       use the next sequence number, so an old grant cannot be replayed.
 *       Revoking needs no signature (it only removes power). Every grant,
 *       revoke and manifest change is logged.
 *   C.3 REASONING LOOP. cmp_decide takes one proposal from the model and
 *       returns PROCEED, ASK (the owner), WAIT or REFUSE:
 *         1. the action's capabilities must be granted, else REFUSE;
 *         2. a spend above the manifest's hard cap is REFUSED;
 *         3. the Chiglet gate scores the evidence: DECIDED on label 0
 *            ("support") is True, on label 1 ("oppose") False; UNCERTAIN
 *            because the top two labels are too close is Both (the evidence
 *            pulls both ways); any other UNCERTAIN, or no gate, is Neither;
 *         4. each goal's verdict on the proposal is joined in: the join is
 *            Belnap's knowledge join (True with False is Both, Neither adds
 *            nothing), so conflicting goals are CARRIED AS BOTH instead of
 *            failing, and Both never proceeds;
 *         5. polarity from the evidence counts (lpx_polarity), phase
 *            emerging: one 64-state tag;
 *         6. INVARIANT GUARD: an action that moves money or touches a
 *            device (ledger post, spend, device) runs the caller's invariant
 *            check; a failed or missing check makes the tag FORBIDDEN, which
 *            the table turns into ROLLBACK: the proposal is refused and
 *            marked quarantined;
 *         7. lpx_action(tag): proceed, wait, refuse, escalate (= ASK),
 *            rollback (= REFUSE, quarantined);
 *         8. a spend that would proceed but is above the owner's automatic
 *            limit becomes ASK.
 *   C.4 EMOTION. The companion's mood is the swarm's integer emotion
 *       (swarm_emotion.h). It sets the tone profile (temperature, verify
 *       passes) and scales the token budget up to the manifest ceiling. It is
 *       NOT an input to cmp_decide or to grants: mood cannot change a
 *       permission, and the tests check every mood gives the same decisions.
 *   C.7 LOG. Every decision, grant, revoke and manifest change appends one
 *       record to a SHA-256 hash-chained log: the tag, the verdict and
 *       reason, the gate's effective independent experts R (per mille), the
 *       capabilities and the proposal digest. When the log is full, cmp_decide
 *       refuses everything (an action that cannot be logged does not happen).
 *
 * HONEST LIMITS. This contains and routes what the model proposes; it does
 * not make the model right. The gate only sees the evidence vectors the
 * caller extracts, and the invariant check is only as good as the caller's
 * callback (for money, the ledger's own A = L + E check belongs there).
 * Signing the log head and keeping the owner's secret key off the device
 * are the caller's jobs; hardware-backed keys arrive with R1. The zxvd
 * process wiring (C.2's capability broker), encrypted memory (C.5),
 * companion-to-companion trade (C.6) and the app face (C.8) are later
 * steps. Freestanding C11: no libc, no allocation, no floating point in
 * the kernel build (the gate is Q32.32 there).
 */
#ifndef ZXV_COMPANION_H
#define ZXV_COMPANION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../chiglet/chiglet.h"
#include "../lpres/lpres_hex.h"
#include "../swarm/swarm_emotion.h"

#define CMP_HASH           32u
#define CMP_NAME           32u
#define CMP_MAX_MODELS     4u
#define CMP_MAX_GOALS      8u
#define CMP_MANIFEST_BYTES 296u
#define CMP_MANIFEST_MAGIC 0x504D435Au /* "ZCMP" */
#define CMP_MANIFEST_VER   1u
#define CMP_PK_BYTES       2592u /* ML-DSA-87 */
#define CMP_SK_BYTES       4896u
#define CMP_SIG_BYTES      4627u
#define CMP_SIG_CTX        "zxv/companion/v1"

/* Capabilities: the companion may do nothing it was not granted. */
#define CMP_CAP_LEDGER     (1u << 0) /* post to the owner's books */
#define CMP_CAP_SPEND      (1u << 1) /* move money (also needs LEDGER) */
#define CMP_CAP_VINEA      (1u << 2) /* PQ sessions with other companions */
#define CMP_CAP_DEVICE     (1u << 3) /* act on a device */
#define CMP_CAP_NET        (1u << 4) /* outbound network */
#define CMP_CAP_MEM_EXPORT (1u << 5) /* send memory off the device */
#define CMP_CAP_ALL        0x3Fu

/* Model roles in the manifest. */
enum { CMP_ROLE_NONE = 0, CMP_ROLE_LANGUAGE = 1, CMP_ROLE_GATE = 2, CMP_ROLE_EVIDENCE = 3 };

typedef struct {
    uint8_t sha[CMP_HASH];
    uint32_t role;
} cmp_model_ref_t;

typedef struct {
    uint64_t epoch;
    uint8_t companion_id[CMP_HASH];
    uint8_t owner_pk_hash[CMP_HASH]; /* SHA-256 of the owner's ML-DSA-87 public key */
    char name[CMP_NAME];             /* NUL-padded, not necessarily NUL-terminated */
    uint32_t caps_max;               /* no grant beyond this */
    uint32_t max_tokens_cycle;       /* token budget ceiling per cycle */
    uint64_t auto_spend_max;         /* spends above this ASK (minor units); 0: always ask */
    uint64_t spend_cap;              /* spends above this are REFUSED */
    uint32_t mem_slots, mem_slot_bytes;
    uint32_t n_models;
    cmp_model_ref_t models[CMP_MAX_MODELS];
} cmp_manifest_t;

typedef enum {
    CMP_OK = 0,
    CMP_ERR_ARG = -1,
    CMP_ERR_FORMAT = -2,  /* manifest bytes malformed */
    CMP_ERR_OWNER = -3,   /* public key does not match the manifest's owner hash */
    CMP_ERR_SIG = -4,     /* signature does not verify */
    CMP_ERR_EPOCH = -5,   /* epoch did not increase */
    CMP_ERR_ID = -6,      /* companion id changed */
    CMP_ERR_SEQ = -7,     /* grant sequence number is not the next one */
    CMP_ERR_CEILING = -8, /* grant outside the manifest's capability ceiling */
    CMP_ERR_LOG = -9,     /* log full: nothing changed (fail closed) */
    CMP_ERR_STATE = -10   /* companion not started */
} cmp_status_t;

/* ---- the log ---- */

enum {
    CMP_REC_BORN = 1,
    CMP_REC_MANIFEST = 2,
    CMP_REC_GRANT = 3,
    CMP_REC_REVOKE = 4,
    CMP_REC_DECIDE = 5
};

typedef struct {
    uint64_t seq, epoch, amount;
    uint8_t kind, action, tag, verdict, why, gate;
    uint16_t r_permille; /* the gate's R x 1000, saturating at 65535 */
    uint32_t caps;
    uint8_t subject[CMP_HASH]; /* proposal digest, or the manifest/grant digest */
    uint8_t prev[CMP_HASH];
    uint8_t hash[CMP_HASH];
} cmp_rec_t;

typedef struct {
    cmp_rec_t *rec;
    uint32_t cap, n;
    uint8_t head[CMP_HASH];
} cmp_log_t;

bool cmp_log_verify(const cmp_log_t *l);

/* ---- C.3 proposals and decisions ---- */

typedef enum {
    CMP_ACT_REPLY = 0,     /* answer the owner: no capability */
    CMP_ACT_MEMORY_WRITE,  /* write local memory: no capability */
    CMP_ACT_NET,           /* NET */
    CMP_ACT_PEER,          /* VINEA */
    CMP_ACT_LEDGER_POST,   /* LEDGER; guarded */
    CMP_ACT_SPEND,         /* SPEND | LEDGER; guarded */
    CMP_ACT_DEVICE,        /* DEVICE; guarded */
    CMP_ACT_MEMORY_EXPORT, /* MEM_EXPORT */
    CMP_ACT_COUNT
} cmp_action_t;

typedef enum { CMP_PROCEED = 0, CMP_ASK = 1, CMP_WAIT = 2, CMP_REFUSE = 3 } cmp_verdict_t;

typedef enum {
    CMP_WHY_OK = 0,
    CMP_WHY_NO_CAP,      /* capability not granted */
    CMP_WHY_OVER_CAP,    /* spend above the hard cap */
    CMP_WHY_INVARIANT,   /* guarded action failed (or had no) invariant check: quarantined */
    CMP_WHY_OPPOSED,     /* the gate decided against it */
    CMP_WHY_UNKNOWN,     /* Neither: not enough independent evidence, or no gate */
    CMP_WHY_CONFLICT,    /* Both: evidence or goals pull both ways */
    CMP_WHY_WEAKENING,   /* True, but the evidence is weakening or stale */
    CMP_WHY_OWNER_LIMIT, /* spend above the owner's automatic limit */
    CMP_WHY_LOG_FULL,    /* nothing can be logged: refuse */
    CMP_WHY_BAD_INPUT    /* malformed proposal */
} cmp_why_t;

typedef struct {
    cmp_action_t action;
    uint64_t amount;                     /* CMP_ACT_SPEND: minor units */
    uint8_t digest[CMP_HASH];            /* SHA-256 of the model's proposal text */
    const surplus_real_t (*ev)[CHG_DIM]; /* evidence vectors for the gate */
    uint32_t k;
    uint8_t n_goals;
    uint8_t goal[CMP_MAX_GOALS];     /* each goal's verdict on the proposal: LPX_B_* */
    uint32_t pos0, neg0, pos1, neg1; /* evidence counts before and after (polarity) */
    bool stale;
} cmp_proposal_t;

/* The invariant guard for money and device actions: true if the action keeps
 * every invariant (for example the ledger's A = L + E after the posting). */
typedef bool (*cmp_invariant_fn)(const cmp_proposal_t *p, void *ctx);

typedef struct {
    cmp_verdict_t verdict;
    cmp_why_t why;
    lpx_t tag;
    lpx_action_t lpx;
    chg_state_t gate;
    chg_reason_t gate_reason;
    uint32_t r_permille;
    bool quarantined;
    uint64_t seq; /* the log record's sequence number */
} cmp_decision_t;

/* ---- the companion ---- */

typedef struct {
    cmp_manifest_t man;
    uint8_t owner_pk[CMP_PK_BYTES];
    uint32_t granted;
    uint64_t grant_seq; /* the next grant must carry this number */
    chiglet_t gate;
    cmp_log_t log;
    swarm_feeling_t mood;
    uint64_t decisions, asks, refusals;
    bool live;
} companion_t;

/* C.1 */
void cmp_manifest_encode(const cmp_manifest_t *m, uint8_t out[CMP_MANIFEST_BYTES]);
cmp_status_t cmp_manifest_decode(const uint8_t in[CMP_MANIFEST_BYTES], cmp_manifest_t *m);
/* The message the owner signs. */
void cmp_manifest_digest(const cmp_manifest_t *m, uint8_t out[CMP_HASH]);

/* Start a companion from a manifest its owner signed. `rec`/`cap` is the log
 * storage (cap >= 1). The gate starts with no model (every proposal is
 * Neither until cmp_load_gate). */
cmp_status_t cmp_start(companion_t *c, const cmp_manifest_t *m,
                       const uint8_t owner_pk[CMP_PK_BYTES], const uint8_t sig[CMP_SIG_BYTES],
                       cmp_rec_t *rec, uint32_t cap);
/* Replace the manifest: higher epoch, same companion id, signed by the
 * current owner. new_owner_pk is the new owner's key when the owner hash
 * changes (NULL otherwise). Grants beyond the new ceiling are dropped. */
cmp_status_t cmp_update_manifest(companion_t *c, const cmp_manifest_t *m,
                                 const uint8_t *new_owner_pk, const uint8_t sig[CMP_SIG_BYTES]);

/* C.2 */
void cmp_grant_digest(const companion_t *c, uint32_t caps, uint64_t seq, uint8_t out[CMP_HASH]);
cmp_status_t cmp_grant(companion_t *c, uint32_t caps, uint64_t seq,
                       const uint8_t sig[CMP_SIG_BYTES]);
cmp_status_t cmp_revoke(companion_t *c, uint32_t caps);
uint32_t cmp_action_caps(cmp_action_t a);
bool cmp_action_guarded(cmp_action_t a);

/* The gate model: labels 0 = support and 1 = oppose. The companion's gate
 * runs with CHG_CAP_INFER only. */
cmp_status_t cmp_load_gate(companion_t *c, const chg_model_t *m);

/* C.3 */
cmp_status_t cmp_decide(companion_t *c, const cmp_proposal_t *p, cmp_invariant_fn inv,
                        void *inv_ctx, cmp_decision_t *out);

/* C.4: tone and budget only. */
cmp_status_t cmp_set_mood(companion_t *c, swarm_feeling_t mood);
const swarm_emotion_profile_t *cmp_tone(const companion_t *c);
/* base * (21 + charge) / 21, charge 0..8 from the mood's intensity, capped
 * at the manifest's max_tokens_cycle. */
uint32_t cmp_token_budget(const companion_t *c, uint32_t base);

/* Belnap knowledge join of two lpres-hex bases (True + False = Both). */
uint8_t cmp_base_join(uint8_t a, uint8_t b);

const char *cmp_verdict_name(cmp_verdict_t v);
const char *cmp_why_name(cmp_why_t w);

#endif /* ZXV_COMPANION_H */

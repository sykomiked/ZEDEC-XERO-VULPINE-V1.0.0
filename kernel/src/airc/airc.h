/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* airc.h — AIRC, agent relay chat: IRC-shaped boards for the Chiglet
 * architecture on the post-quantum mesh (docs/AIRC.md). This is the
 * protocol core: posts, boards, the quorum gate for jumbo frames, the trust
 * tiers and the install decision for shared improvements. It moves no
 * bytes: a carrier (Vinea, the zxvd socket) hands it signed posts and asks
 * what to do with them. Freestanding C11: no libc, no allocation, no float.
 *
 * POSTS
 *   A1  A post is: board (32 bytes), author (32: SHA3-256 of the author's
 *       ML-DSA-87 public key), seq (the author's counter), ms (the author's
 *       clock), kind, truth, parent (32, zero for none), lease (32, zero
 *       unless a jumbo post, A13), the body and up to AIRC_REFS references.
 *       It is encoded field by field, big-endian, in one canonical form:
 *         "AIRC" ver kind truth nrefs board author seq:8 ms:8 parent lease
 *         body_len:4 body (hash:32 size:8 rkind:1) x nrefs
 *       Decoding reads it back field by field (no casts of packed structs)
 *       and refuses anything else: another magic or version, an unknown kind,
 *       truth or reference kind, more than AIRC_REFS references, a body
 *       over AIRC_JUMBO_MAX, a body over AIRC_FRAME_MAX without a lease,
 *       a lease on a body that fits a frame, or bytes left over.
 *   A2  SIGNED BY ITS AUTHOR. On the wire a post is
 *         encoding || ML-DSA-87 signature || public key
 *       (FIPS 204, context "zxv/airc/v1/post", over the encoding). A post is
 *       accepted only when the signature verifies and the author field is
 *       SHA3-256 of the key that signed it: a forwarder relays the bytes
 *       unchanged and is never the author (A16). The post id is
 *       SHA3-256(encoding || signature).
 *   A3  THE TRUTH TAG is one of lpres's four values (T, F, B, N): what the
 *       author claims, never that the body is true. A post is data, never a
 *       command: nothing here, and nothing a host builds on this, runs a
 *       post's body or what its references point to.
 *   A4  REFERENCES name content by its SHA3-256 hash and size: a chunk
 *       (AIRC_REF_CHUNK, fetched and checked against the hash by the
 *       carrier) or a freight packet (AIRC_REF_FREIGHT). Files are linked,
 *       never inlined past the frame, and never run.
 *   A5  KINDS. TEXT (a message), OFFER (a shared improvement: the body
 *       describes it, refs[0] is its manifest) and REVIEW (a verdict on an
 *       offer: parent = the offer's id, refs[0] = the manifest reviewed,
 *       truth T approves and F rejects).
 *
 * BOARDS
 *   A6  A board is a channel, named like an IRC channel ("#" and 1..50
 *       bytes, no space, comma, BEL or ':'), its id SHA3-256 of
 *       "zxv/airc/v1/board\n" and the name in ASCII lower case. Boards are
 *       content-addressed: any peer that visits one can host it, and a
 *       post is the same bytes wherever it is.
 *   A7  airc_board_accept keeps the newest AIRC_BOARD_POSTS posts of one
 *       board (oldest by ms, then id, makes room). A post already held is
 *       DUP. A post for another board is refused.
 *   A8  EQUIVOCATION. Two different posts by one author with the same seq
 *       are a self-contradiction proven by the author's own signatures: both
 *       are HELD as BOTH (never shown as T or F), and the author's fault is
 *       committed through the trust table (A17).
 *   A9  FLOODS. Each author has a bucket of posts refilled over time, its
 *       size and rate set by the author's tier (A15) and scaled by the
 *       author's token budget for the cycle (the caller's TPC share, 0 =
 *       the tier's default): an empty bucket refuses the post (RATE). A
 *       dropped author's posts are refused (DROPPED). Unknown authors are
 *       served as COLD.
 *
 * JUMBO FRAMES: THE QUORUM GATE (Quorum-Gated Bandwidth)
 *   A10 A body over AIRC_FRAME_MAX needs a lease. The author first sends a
 *       request, ML-DSA-87 signed (context "zxv/airc/v1/req"): board,
 *       author, len (32-bit), the body's full SHA3-256, a size tier and an
 *       intent hash. Tier t (1..AIRC_TIERS) allows up to
 *       airc_tier_max(t) bytes, Fibonacci steps of 1 KiB (5, 8, 13, 21, 34,
 *       55 KiB, then 64 KiB). The request id is SHA3-256 of its encoding.
 *   A11 VOTES are ML-DSA-87 signed (context "zxv/airc/v1/vote") over
 *       request id, voter id and a Belnap value. Only peers of the swarm vote
 *       (the voter set is the caller's swarm, up to AIRC_VOTERS, never a
 *       fixed bitmask), each once, weighted by its TPC budget (capped at
 *       AIRC_WEIGHT_MAX). A voter that sends two different votes counts as
 *       BOTH. Dropped peers do not vote.
 *   A12 THE TALLY. Any BOTH or NEITHER weight HOLDS the request (it is
 *       escalated, never counted as yes). Otherwise it is GRANTED when the T
 *       weight reaches 2/3 of the swarm's total weight (1/3 for a WARM
 *       author: earned trust skips the full quorum, A15), REFUSED once the F
 *       weight makes that impossible, and PENDING until then.
 *   A13 THE LEASE is one-time and expires AIRC_LEASE_MS after the grant. A
 *       jumbo post carries the request id as its lease; it is accepted only
 *       when the lease is granted, unused and unexpired, and the post's
 *       board, author and body match the request: length exactly, SHA3-256
 *       exactly. Signature, bounds and these checks always run, warm or not.
 *   A14 A MISMATCH (a signed post whose body does not match the author's
 *       own signed request) costs the author a bounded penalty, logged with
 *       the request id and the reason: AIRC_PENALTY per mismatch, saturating
 *       at AIRC_PENALTY_MAX, and the author is COLD while any is left. It
 *       decays by one per airc_trust_epoch. No balance is burned here; any
 *       token consequence is the economy's, under its own rules.
 *
 * TRUST (Adaptive Zero-Trust)
 *   A15 TIERS. COLD: every unknown peer and every peer below warm_min
 *       credibility or with a penalty; small buckets, full quorum. WARM:
 *       credibility (cred.h C7, EigenTrust over settled trades) at or above
 *       warm_min and no penalty. DROPPED: a committed fault (A17).
 *       Credibility comes only from settled trades (airc_trust_settle feeds
 *       cred_fulfil); chat itself earns nothing.
 *   A16 PEERS ARE NAMED by the full 32-byte SHA3-256 of their key, never a
 *       prefix. Faults are charged to the post's ORIGIN (its author), never
 *       to the peer that forwarded it.
 *   A17 FAULTS go through the paraconsistent action table (lpres_hex.h X2).
 *       A fault proven by the author's own signatures (A8 equivocation) is
 *       tagged BOTH with FORBIDDEN polarity: ROLLBACK, so the author is
 *       DROPPED and the two post ids are kept as evidence to share. A
 *       suspicion without such proof is tagged BOTH neutral: ESCALATE to a
 *       person; nothing is dropped. A drop is sticky until the owner clears
 *       it (airc_trust_clear).
 *   A18 A FULL TABLE NEVER REFUSES A NEW PEER: it is served as COLD without
 *       a slot (all such peers share one post bucket), and gets a slot when
 *       the least recently seen peer that has no penalty, no fault and no
 *       settled trade is evicted. DROPPED, penalised and trading peers are
 *       never evicted.
 *
 * SHARED IMPROVEMENTS
 *   A19 An OFFER is never applied by AIRC. airc_offer_decide says whether
 *       the owner's own rules allow installing it: the offer verifies, its
 *       author is on the owner's trusted-publisher list, and the owner (or
 *       a reviewer the owner trusts) signed a REVIEW with truth T of that
 *       offer and that manifest. A REVIEW with F from the owner rejects it;
 *       anything else waits. Installing is the host's update path (signed
 *       buckets, the same as any update), not this module's.
 */
#ifndef AIRC_H
#define AIRC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../cred/cred.h"
#include "../lpres/lpres_hex.h"

#define AIRC_VERSION     1u
#define AIRC_FRAME_MAX   512u   /* A1: a body without a lease */
#define AIRC_JUMBO_MAX   65536u /* A10 */
#define AIRC_REFS        8u
#define AIRC_NAME_MAX    50u /* after the '#' */
#define AIRC_HDR_LEN     (4u + 4u + 32u + 32u + 8u + 8u + 32u + 32u + 4u)
#define AIRC_REF_LEN     (32u + 8u + 1u)
#define AIRC_ENC_MAX     (AIRC_HDR_LEN + AIRC_JUMBO_MAX + AIRC_REFS * AIRC_REF_LEN)
#define AIRC_PK_LEN      2592u /* ML-DSA-87 */
#define AIRC_SK_LEN      4896u
#define AIRC_SIG_LEN     4627u
#define AIRC_WIRE_MAX    (AIRC_ENC_MAX + AIRC_SIG_LEN + AIRC_PK_LEN)
#define AIRC_BOARD_POSTS 256u
#define AIRC_PEERS       CRED_MAX_SUBJECTS
#define AIRC_TIERS       7u
#define AIRC_VOTERS      256u
#define AIRC_REQS        16u
#define AIRC_WEIGHT_MAX  ((uint64_t) 1 << 40)
#define AIRC_LEASE_MS    60000u
#define AIRC_PENALTY     8u
#define AIRC_PENALTY_MAX 64u
#define AIRC_LOG         64u
#define AIRC_REQ_LEN     (4u + 1u + 1u + 32u + 32u + 4u + 32u + 32u + 8u)
#define AIRC_VOTE_LEN    (4u + 32u + 32u + 1u)
#define AIRC_REQ_WIRE    (AIRC_REQ_LEN + AIRC_SIG_LEN + AIRC_PK_LEN)

typedef enum {
    AIRC_OK = 0,
    AIRC_ERR_ARG = -1,
    AIRC_ERR_FORMAT = -2,    /* A1 */
    AIRC_ERR_SIG = -3,       /* A2 */
    AIRC_ERR_AUTHOR = -4,    /* A2: author is not the signer */
    AIRC_ERR_BOARD = -5,     /* A7: another board */
    AIRC_ERR_DUP = -6,       /* A7 */
    AIRC_ERR_RATE = -7,      /* A9 */
    AIRC_ERR_DROPPED = -8,   /* A9, A17 */
    AIRC_ERR_LEASE = -9,     /* A13: no usable lease */
    AIRC_ERR_MISMATCH = -10, /* A14 */
    AIRC_ERR_FULL = -11,
    AIRC_ERR_NAME = -12 /* A6 */
} airc_status_t;

enum { AIRC_TEXT = 1, AIRC_OFFER = 2, AIRC_REVIEW = 3 };
enum { AIRC_REF_CHUNK = 1, AIRC_REF_FREIGHT = 2 };
/* truth: lpres_hex.h bases */
enum { AIRC_F = LPX_B_FALSE, AIRC_N = LPX_B_NEITHER, AIRC_B = LPX_B_BOTH, AIRC_T = LPX_B_TRUE };
typedef enum { AIRC_COLD = 0, AIRC_WARM = 1, AIRC_DROPPED = 2 } airc_tier_t;

typedef struct {
    uint8_t hash[32];
    uint64_t size;
    uint8_t kind; /* AIRC_REF_* */
} airc_ref_t;

typedef struct {
    uint8_t kind, truth, nrefs;
    uint8_t board[32], author[32];
    uint64_t seq, ms;
    uint8_t parent[32], lease[32];
    uint32_t body_len;
    const uint8_t *body; /* not owned */
    airc_ref_t refs[AIRC_REFS];
} airc_post_t;

/* A post that verified: the decoded post (its body points into the wire
 * bytes), its id and where the key and signature are in those bytes. */
typedef struct {
    airc_post_t p;
    uint8_t id[32];
    const uint8_t *pk, *sig;
    uint32_t enc_len;
} airc_signed_t;

/* ---- posts (A1-A6) ---- */

/* A1: the encoding's length, or 0 (bad post, or cap too small). */
uint32_t airc_post_encode(const airc_post_t *p, uint8_t *out, uint32_t cap);
airc_status_t airc_post_decode(const uint8_t *in, uint32_t len, airc_post_t *p);
/* A2: sign p (author is set from pk) into wire; rnd is 32 fresh bytes or
 * NULL (deterministic). The wire length, or 0. */
uint32_t airc_post_sign(airc_post_t *p, const uint8_t pk[AIRC_PK_LEN],
                        const uint8_t sk[AIRC_SK_LEN], const uint8_t *rnd, uint8_t *wire,
                        uint32_t cap);
/* A2: decode and verify wire bytes. */
airc_status_t airc_post_open(const uint8_t *wire, uint32_t len, airc_signed_t *out);
/* A6: the board id of "#name"; AIRC_ERR_NAME for a bad name. */
airc_status_t airc_board_id(const char *name, uint8_t id[32]);
/* A16: SHA3-256 of a public key */
void airc_key_id(const uint8_t pk[AIRC_PK_LEN], uint8_t id[32]);

/* ---- trust (A14-A18) ---- */

typedef struct {
    bool used;
    uint8_t id[32];
    uint32_t subject; /* cred subject id */
    uint64_t last_ms;
    bool dropped;
    lpx_t tag; /* the last fault's tag (A17) */
    uint8_t evidence[2][32];
    uint32_t penalty; /* A14 */
    /* A9: the post bucket */
    uint64_t tokens_milli, refill_ms;
} airc_peer_t;

typedef struct {
    uint8_t author[32], req[32];
    uint8_t reason; /* AIRC_LOG_* */
    uint64_t ms;
} airc_log_t;
enum { AIRC_LOG_MISMATCH = 1, AIRC_LOG_EQUIVOCATION = 2, AIRC_LOG_ESCALATE = 3 };

typedef struct {
    uint64_t warm_min; /* A15: credibility for WARM (C7; default 50, half the average) */
    uint32_t cold_burst, cold_per_min, warm_burst, warm_per_min; /* A9 */
} airc_trust_config_t;

typedef struct {
    airc_trust_config_t cfg;
    cred_t cred;
    airc_peer_t peer[AIRC_PEERS];
    uint32_t next_subject;
    airc_peer_t overflow; /* A18: the bucket of every peer without a slot */
    airc_log_t log[AIRC_LOG];
    uint32_t log_n, log_head;
    uint64_t escalations;
} airc_trust_t;

void airc_trust_config_default(airc_trust_config_t *cfg);
airc_status_t airc_trust_init(airc_trust_t *t, const airc_trust_config_t *cfg);
/* A15/A18: the tier of a peer (unknown: COLD) */
airc_tier_t airc_trust_tier(const airc_trust_t *t, const uint8_t id[32]);
/* A15: a settled trade, rater paid ratee `value` minor units with this
 * receipt id: credibility through cred_fulfil (both get a slot, A18). */
airc_status_t airc_trust_settle(airc_trust_t *t, const uint8_t rater[32], const uint8_t ratee[32],
                                uint64_t value, const uint8_t receipt[32], uint64_t now_ms);
/* C6: pre-trust a peer (the owner's own key): trust flows from it */
airc_status_t airc_trust_pretrust(airc_trust_t *t, const uint8_t id[32], uint64_t now_ms);
/* A17: a suspicion without signature proof: ESCALATE, nothing dropped */
void airc_trust_suspect(airc_trust_t *t, const uint8_t id[32], const uint8_t subject[32],
                        uint64_t now_ms);
/* A17: the owner clears a drop */
void airc_trust_clear(airc_trust_t *t, const uint8_t id[32]);
/* A14/C4: decay penalties and credibility, recompute trust */
void airc_trust_epoch(airc_trust_t *t);
/* the peer's slot, or NULL */
const airc_peer_t *airc_trust_peer(const airc_trust_t *t, const uint8_t id[32]);

/* ---- the quorum gate (A10-A14) ---- */

typedef struct {
    uint8_t board[32], author[32];
    uint32_t len;
    uint8_t hash[32];
    uint8_t tier;
    uint8_t intent[32];
    uint64_t ms;
} airc_req_t;

typedef enum { AIRC_PENDING = 0, AIRC_GRANTED, AIRC_REFUSED, AIRC_HELD } airc_verdict_t;

typedef struct {
    uint8_t voter[32];
    uint8_t value; /* AIRC_T/F/B/N; B after two different votes */
    uint64_t weight;
} airc_ballot_t;

typedef struct {
    bool used, spent;
    airc_req_t r;
    uint8_t id[32];
    airc_verdict_t verdict;
    uint64_t granted_ms;
    uint32_t n;
    airc_ballot_t b[AIRC_VOTERS];
} airc_lease_t;

/* A10: Fibonacci steps of 1 KiB, 0 for a bad tier */
uint32_t airc_tier_max(uint8_t tier);
/* A10: the request's encoding (AIRC_REQ_LEN) and id */
void airc_req_encode(const airc_req_t *r, uint8_t out[AIRC_REQ_LEN]);
airc_status_t airc_req_decode(const uint8_t *in, uint32_t len, airc_req_t *r);
void airc_req_id(const airc_req_t *r, uint8_t id[32]);
/* A10: a signed request is the encoding || signature || key (author is set
 * from pk). AIRC_REQ_WIRE, or 0 for a bad tier or length. */
uint32_t airc_req_sign(airc_req_t *r, const uint8_t pk[AIRC_PK_LEN], const uint8_t sk[AIRC_SK_LEN],
                       const uint8_t *rnd, uint8_t out[AIRC_REQ_WIRE]);
/* A11: a signed vote is the encoding (AIRC_VOTE_LEN) || signature || key */
#define AIRC_VOTE_WIRE (AIRC_VOTE_LEN + AIRC_SIG_LEN + AIRC_PK_LEN)
uint32_t airc_vote_sign(const uint8_t req_id[32], uint8_t value, const uint8_t pk[AIRC_PK_LEN],
                        const uint8_t sk[AIRC_SK_LEN], const uint8_t *rnd,
                        uint8_t out[AIRC_VOTE_WIRE]);

typedef struct {
    airc_lease_t lease[AIRC_REQS];
} airc_gate_t;

void airc_gate_init(airc_gate_t *g);
/* A10: open a signed request (a bad one: FORMAT, SIG or AUTHOR; a dropped
 * author: DROPPED; a full table: FULL, after reusing spent, refused and
 * expired leases). */
airc_status_t airc_gate_request(airc_gate_t *g, const airc_trust_t *t, const uint8_t *wire,
                                uint32_t len, uint64_t now_ms, uint8_t id[32]);
/* A11: count one signed vote; weight = the voter's TPC budget in the swarm
 * (capped; 0 = not in the swarm, refused). DROPPED voters are refused. */
airc_status_t airc_gate_vote(airc_gate_t *g, const airc_trust_t *t, const uint8_t *wire,
                             uint32_t len, uint64_t weight);
/* A12: the verdict for a swarm of total_weight (the sum of every voter's
 * capped weight, voted or not), for this author's tier */
airc_verdict_t airc_gate_tally(airc_gate_t *g, const airc_trust_t *t, const uint8_t id[32],
                               uint64_t total_weight, uint64_t now_ms);

/* ---- boards (A6-A9, A13) ---- */

typedef struct {
    bool used, held; /* held: BOTH (A8) */
    uint8_t id[32], author[32];
    uint64_t seq, ms;
    uint8_t kind, truth;
} airc_entry_t;

typedef struct {
    uint8_t id[32];
    airc_entry_t e[AIRC_BOARD_POSTS];
    uint64_t accepted, refused, held;
} airc_board_t;

airc_status_t airc_board_init(airc_board_t *b, const char *name);
/* A7-A9, A13, A14, A17: whether a post verified by airc_post_open joins the
 * board. budget is
 * the author's TPC share for this cycle (0: the tier's default bucket). On
 * AIRC_OK, *held says the post is held as BOTH (A8). */
airc_status_t airc_board_accept(airc_board_t *b, airc_trust_t *t, airc_gate_t *g,
                                const airc_signed_t *s, uint64_t budget, uint64_t now_ms,
                                bool *held);
/* the entry for a post id, or NULL */
const airc_entry_t *airc_board_find(const airc_board_t *b, const uint8_t id[32]);

/* ---- shared improvements (A19) ---- */

typedef enum {
    AIRC_INSTALL_NO = 0, /* not an offer, or it does not verify */
    AIRC_INSTALL_UNTRUSTED,
    AIRC_INSTALL_UNREVIEWED,
    AIRC_INSTALL_REJECTED,
    AIRC_INSTALL_ALLOWED
} airc_install_t;

/* offer: a verified OFFER. publishers: the owner's trusted publisher ids.
 * reviewers: ids whose REVIEWs count (the owner's own key first).
 * reviews: verified REVIEW posts. Never installs anything. */
airc_install_t airc_offer_decide(const airc_signed_t *offer, const uint8_t (*publishers)[32],
                                 uint32_t n_publishers, const uint8_t (*reviewers)[32],
                                 uint32_t n_reviewers, const airc_signed_t *reviews,
                                 uint32_t n_reviews);

const char *airc_status_str(airc_status_t s);

#endif

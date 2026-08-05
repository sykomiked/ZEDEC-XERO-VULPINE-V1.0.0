/* chronicle.h — the Chronicle: a hash-chained, self-resolving ledger of judgments
 *
 * THE FUNDAMENTAL GAP THIS FILLS
 * ------------------------------
 * Wyrmgate renders a Tri-Space verdict on an event, and then the verdict
 * evaporates. For an operating system whose entire premise is deterministic
 * replay and audit, that is a hole: the system judges but does not REMEMBER,
 * and an S0 DEFER is a black hole — nothing ever re-judges a deferred event
 * when the world changes. The Chronicle closes both.
 *
 * WHAT IT ADDS (two things, both fundamental)
 * -------------------------------------------
 *  1. MEMORY, tamper-evident. Every resolved judgment — S+ COMMIT and S-
 *     REJECT alike — is appended to a hash chain:
 *
 *         head_k = SHA256( head_{k-1} || seq || verdict || reason || digest(event) )
 *
 *     Rejections are recorded too, so history cannot be quietly rewritten to
 *     hide a refusal. Altering any past entry diverges the head; a verifier
 *     that recomputes the chain catches it. The head is a single 32-byte
 *     fingerprint of everything the system has ever decided.
 *
 *  2. PATIENCE, self-resolving. An S0 DEFER does not vanish — the event is
 *     held in a pending set with its deferral reason. When the world changes
 *     (corroboration arrives, a route comes up, a parent commits) a poke
 *     re-judges the pending events, and any that now pass are appended. So a
 *     deferred decision resolves ITSELF the moment its blocker clears, which
 *     is exactly the patient, event-sequenced behaviour the architecture
 *     promises.
 *
 * REPLAY
 * ------
 * Feeding the same event sequence to two Chronicles yields the same head:
 * the ledger is a deterministic fold over its inputs. (Within one platform;
 * the event digest hashes surplus_real_t in its native width, so a
 * cross-ISA-identical head would need a canonical serialisation — noted, not
 * yet done.)
 *
 * Composes Wyrmgate + SHA-256. Freestanding: integer only, no libc beyond the
 * kernel's own sha256, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Chronicle slice)
 * License: SEL-3.3
 */
#ifndef ZXV_CHRONICLE_H
#define ZXV_CHRONICLE_H

#include <stdint.h>
#include <stdbool.h>
#include "../wyrmgate/wyrmgate.h"

#define CHRON_MAX_ENTRIES  256u
#define CHRON_MAX_PENDING  32u
#define CHRON_HASH_LEN     32u

typedef struct {
    uint64_t       seq;
    uint8_t        verdict;                 /* wyrm_verdict_t (COMMIT/REJECT) */
    uint8_t        reason;                  /* wyrm_reason_t                  */
    uint8_t        event_digest[CHRON_HASH_LEN];
    uint8_t        entry_hash[CHRON_HASH_LEN];
} chronicle_entry_t;

typedef struct {
    wyrm_event_t   event;
    uint8_t        reason;                  /* why it is still deferred */
    bool           occupied;
} chronicle_pending_t;

typedef struct {
    uint8_t            head[CHRON_HASH_LEN];   /* fingerprint of all history */
    chronicle_entry_t  entry[CHRON_MAX_ENTRIES];
    uint32_t           n_entries;
    uint64_t           next_seq;
    chronicle_pending_t pending[CHRON_MAX_PENDING];
    uint32_t           n_pending;              /* occupied pending slots */
} chronicle_t;

typedef struct {
    wyrm_verdict_t verdict;
    wyrm_reason_t  reason;
    uint64_t       seq;         /* the entry's sequence, if it was chained */
    int32_t        pending_id;  /* >=0 if it was deferred into the pending set */
    bool           chained;     /* true if this resolved and was appended */
} chronicle_result_t;

void chronicle_init(chronicle_t *c);

/* Judge an event and record the outcome. S+/S- are appended to the chain;
 * S0 is held in the pending set (pending_id >= 0). */
chronicle_result_t chronicle_submit(chronicle_t *c, const wyrm_event_t *e);

/* A mutable handle to a pending event so its context can be updated (add
 * corroboration, mark a route up, note the parent committed) before a poke.
 * Returns NULL for an unknown id. */
wyrm_event_t *chronicle_pending_event(chronicle_t *c, uint32_t pending_id);

/* Re-judge every pending event against its (possibly updated) context.
 * Any that now resolve (S+ or S-) are appended and freed from pending.
 * Returns how many pending events resolved this poke. */
uint32_t chronicle_poke(chronicle_t *c);

/* The current chain head — a single fingerprint of all recorded history. */
const uint8_t *chronicle_head(const chronicle_t *c);
uint32_t chronicle_length(const chronicle_t *c);   /* chained entries */
uint32_t chronicle_pending_count(const chronicle_t *c);

/* Recompute the chain from its entries; true iff the stored head matches —
 * i.e. no entry has been tampered with. */
bool chronicle_verify(const chronicle_t *c);

#endif /* ZXV_CHRONICLE_H */

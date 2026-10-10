/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_feed.h — the feed, ranked by the Interaction Surplus Framework.
 *
 * NO ENGAGEMENT MAXIMISATION.  Nothing in this ranker reads likes, reaction
 * counts, reply counts, view counts, dwell time, shares or follower counts,
 * and nothing predicts what will keep a person scrolling. A post that many
 * people pushed is worth exactly what one copy of it is worth. The ranker
 * asks one question: what does this post ADD to what you have already seen?
 * (test_social_feed.c checks that 50 near-duplicate viral posts cannot
 * crowd out independent ones.) This is concord.h's stance applied to posts.
 *
 *   F1  EMBEDDING.  A post becomes an integer vector of SF_DIM = 64 Q16
 *       components, a signed hashed bag of words:
 *         - a word is a maximal run of ASCII letters and digits (folded to
 *           lower case) and bytes >= 0x80 (so UTF-8 text stays whole);
 *         - FNV-1a 32 over (salt, word) picks bucket h & 63 and sign
 *           (h >> 31 ? -1 : +1); signs make colliding words cancel instead
 *           of piling up;
 *         - a plain word adds 1/4 (salt 'W'), a topic "#word" adds 3/4
 *           (salt 'T'), and the author NodeID adds 2/4 once (salt 'A'), so
 *           the same author on the same topic is more alike than strangers.
 *       Only POST and REPLY bodies are embedded; a DM body (ciphertext,
 *       social_post.h P9) is never read.
 *   F2  SURPLUS OVER WHAT YOU HAVE SEEN.  For a candidate c and the seen set
 *       S, surplus(c | S) = min over s in S of f(u(c, s)), with
 *       u = 1 - cos^2 and f(u) = ln(1 + (N-1) u) in Q16 (zt_surplus_u,
 *       zt_surplus_f from tensor/zt.h). With S empty it is ln N. A post
 *       nearly collinear with ANY seen post scores near 0; one independent
 *       of everything scores near ln N. Using the minimum is the same rule
 *       as zt_surplus_gate: a vector must add surplus over every kept one.
 *   F3  GREEDY, SELF-DIVERSIFYING.  sf_rank picks the best candidate, adds
 *       it to the working seen set, updates every other candidate's minimum
 *       against just that pick (O(n DIM) per pick), and repeats.
 *   F4  BOUNDED BLENDS.  score = surplus + fresh + rep, where
 *         fresh = w_fresh * (horizon - age) / horizon, age in Lamport ticks,
 *                 0 at or beyond the horizon;
 *         rep   = w_rep * rep(author), rep() in [0, 1] Q16.
 *       sf_rank clamps w_fresh <= ln(N)/4 and w_rep <= ln(N)/8, so the two
 *       together stay at or below 3/8 of the ISF range: a fully redundant
 *       post (surplus 0) can never outrank a fully independent one (ln N).
 *       f is concave, so a small difference still earns real surplus: with
 *       N = 8 a copy that changes one word in twelve keeps about 0.2 ln N,
 *       which is why the bonuses are held this low.
 *       sf_rep_badges adapts reputation/reputation.h: earned badge score s
 *       maps to s / (s + 16). The pig badge is NOT a ranking input:
 *       reputation.h makes it a label the community can read, not a gag,
 *       and down-ranking by it would be a quiet ban.
 *   F5  DETERMINISM.  Integer arithmetic only; ties break by the higher
 *       Lamport time, then the lexicographically smaller record id. The same
 *       inputs give the same feed on every machine.
 *
 * RULE: freestanding C11. No libc, no malloc, no floating point, no 64-bit
 * division (zt_udiv64 where needed), no __int128. Scratch is the caller's.
 *
 * HONEST LIMITS
 * -------------
 * A hashed bag of words is a weak model of meaning: synonyms and
 * paraphrases look independent, two posts that share only stopwords look a
 * little alike, and 64 buckets collide (about n^2/64 shared buckets for two
 * n-word posts, softened by the random signs). A spammer can defeat
 * near-duplicate detection by rewording every copy; the ISF term then sees
 * independent posts, and only the bounded freshness and reputation terms
 * and the user's own follow graph stand in the way. Lamport ages assume
 * peers' clocks are roughly comparable. Ranking is O(k n DIM) plus
 * O(n |S| DIM) to seed, fine for hundreds of candidates, not millions. The
 * diversity metric (sf_diversity) is mean pairwise u, which measures
 * directional spread, not quality.
 */
#ifndef ZXV_SOCIAL_FEED_H
#define ZXV_SOCIAL_FEED_H

#include <stdint.h>
#include <stdbool.h>
#include "social_post.h"
#include "../tensor/zt.h"

#define SF_DIM      64u
#define SF_SEEN_MAX 32u

typedef struct {
    zt_fx v[SF_DIM];
} sf_vec_t;

/* The user's recently seen posts, a ring of their vectors. */
typedef struct {
    sf_vec_t v[SF_SEEN_MAX];
    uint32_t head, n;
} sf_seen_t;

/* Reputation in [0, ZT_ONE] for an author. */
typedef zt_fx (*sf_rep_fn)(void *ctx, const uint8_t author[SP_NODEID_LEN]);

typedef struct {
    uint32_t N;       /* ISF block count, >= 2 */
    uint64_t now;     /* Lamport now, for freshness */
    uint32_t horizon; /* ticks for freshness to fall to 0, >= 1 */
    zt_fx w_fresh;    /* max freshness bonus, Q16 (clamped, F4) */
    zt_fx w_rep;      /* max reputation bonus, Q16 (clamped, F4) */
    sf_rep_fn rep;    /* NULL = no reputation term */
    void *rep_ctx;
} sf_config_t;

/* Per-candidate scratch, one per candidate, the caller's memory. */
typedef struct {
    sf_vec_t vec;
    zt_fx min_surplus;
    bool eligible, taken;
} sf_work_t;

typedef struct {
    uint32_t index; /* into the candidate array */
    int32_t score;  /* surplus + fresh + rep, Q16 */
    zt_fx surplus, fresh, rep;
} sf_pick_t;

/* N = 8, horizon = 1024, w_fresh = ln(8)/4, w_rep = ln(8)/8, no rep(). */
void sf_config_default(sf_config_t *c, uint64_t now);

/* F1 */
void sf_embed_bytes(const uint8_t *body, uint32_t len, const uint8_t author[SP_NODEID_LEN],
                    sf_vec_t *out);
/* Embeds POST and REPLY; any other kind gives the zero vector. */
void sf_embed(const sp_record_t *r, sf_vec_t *out);

void sf_seen_init(sf_seen_t *s);
void sf_seen_add(sf_seen_t *s, const sf_vec_t *v);

/* F2 */
zt_fx sf_surplus_over(const sf_vec_t *c, const sf_seen_t *s, uint32_t N);

/* F2-F5: rank n candidates, write up to k picks best first, return the
 * count. `seen` is read, not changed (see sf_commit). Candidates that are
 * not POST/REPLY, NULL, or repeat an earlier candidate's id are skipped. */
uint32_t sf_rank(const sf_config_t *cfg, const sf_seen_t *seen, const sp_record_t *const *cand,
                 uint32_t n, sf_work_t *work, sf_pick_t *out, uint32_t k);

/* Add the picked posts to the seen set once the user has been shown them. */
void sf_commit(sf_seen_t *seen, const sf_work_t *work, const sf_pick_t *picks, uint32_t count);

/* Mean pairwise u over the picks' vectors, Q16 in [0, ZT_ONE]: 0 = all the
 * same direction, ZT_ONE = mutually orthogonal. */
zt_fx sf_diversity(const sf_work_t *work, const sf_pick_t *picks, uint32_t count);

/* F4: reputation/reputation.h adapter. ctx is a rep_state_t*; the subject id
 * is the first four bytes of the NodeID, big-endian. */
zt_fx sf_rep_badges(void *ctx, const uint8_t author[SP_NODEID_LEN]);
uint32_t sf_subject_of(const uint8_t node_id[SP_NODEID_LEN]);

#endif /* ZXV_SOCIAL_FEED_H */

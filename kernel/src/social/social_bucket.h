/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_bucket.h — update buckets that anyone can list, and anyone can rate.
 *
 * THE RULE: A RATING NEVER GRANTS INSTALL TRUST.  Stars, comments and
 * scores are a quality signal for reading and choosing, nothing more. Whether
 * a bucket may be installed is decided ONLY by the installing user's own
 * trusted-publisher list (sb_install_trusted), which compares the listing's
 * release-key fingerprint against fingerprints that user chose. No function
 * in this module reads a score when deciding trust, and none can.
 *
 * Like a decentralised feed (Bluesky-style), a listing is a signed record
 * that any peer can carry; there is no store owner who approves it.
 *
 *   B1  LISTING.  A record of kind SP_KIND_BUCKET_LISTING whose body is
 *         ver=1 category:u8 src_kind:u8 src_len:u8 src
 *         title_len:u8 title desc_len:u16 desc fingerprint[32]
 *       src is a CID (opaque bytes: binary CID or its text form) or an IPNS
 *       name (text). fingerprint is SHA-256 of the publisher's RELEASE
 *       public key, which signs the bucket's contents; it is not the
 *       listing author's social key, so a curator can list someone else's
 *       release. Decoding is strict: every length in range, the body used
 *       exactly.
 *   B2  RATING.  A record of kind SP_KIND_RATING with parent = the listing
 *       id and body = stars:u8 (1..5) then 0..280 bytes of comment.
 *       Threaded discussion uses ordinary REPLY records.
 *       ONE RATER, ONE VOTE: for each (author, listing) only the rating with
 *       the highest Lamport time counts (ties: the smaller record id).
 *   B3  WEIGHTS.  Each counted rater i gets w_i = ind_i * rep_i (Q16).
 *         rep_i = 1/4 + 3/4 * rep(author)   rep() from social_feed.h, e.g.
 *                                           sf_rep_badges (reputation.h)
 *         ind_i = 1 / sum_j sim(i, j)       over all counted raters j,
 *                                           including i itself (sim = 1)
 *       where sim(i, j) = 1 - f(u_ij) / ln N, u_ij = 1 - cos^2 of the two
 *       raters' RATING HISTORIES, and f the ISF functional. A history is a
 *       hashed vector over every rating the rater holds in the store (a
 *       presence term and a centred-stars term per listing). k raters with
 *       identical histories have sim = 1 with each other, so each gets
 *       1/k and the ring together weighs as ONE rater. Independent raters
 *       (u near 1, f near ln N) keep a weight near 1 each.
 *   B4  AGGREGATE.  count (raw raters), effective raters W = sum w_i, the
 *       weighted mean stars in Q16, confidence = W / (W + 3) in Q16, and a
 *       score = (W mean + 3 * 3 stars) / (W + 3): the mean shrunk toward 3
 *       stars until enough independent weight backs it.
 *   B5  ORDER.  sb_order lists the listings for the UI by score with an ISF
 *       diversity pass: greedily, each next pick maximises
 *       score * (1 + s) / 2, where s in [0, 1] is the ISF surplus (over ln N)
 *       of its title, description and category over the listings already
 *       picked, so near-duplicate listings sink. The built-in default bucket
 *       (SB_DEFAULT_CID, the user's own) is always first; if the store holds
 *       no listing for it, the first entry is marked builtin.
 *
 * RULE: freestanding C11. No libc, no malloc, no floating point, no 64-bit
 * division (zt_udiv64), no __int128. Every buffer is the caller's.
 *
 * HONEST LIMITS
 * -------------
 * Independence weighting discounts raters whose histories look ALIKE; a
 * sybil operator who gives every puppet a different, random history (rating
 * unrelated listings at random) looks independent and is not discounted by
 * B3. Only the reputation factor (new accounts carry 1/4 of an established
 * one's weight) and the shrinkage toward 3 stars slow that down. Honest
 * newcomers whose only rating is this one listing look alike too (their
 * histories share that one term), so a crowd of genuine first-time raters
 * is discounted as well. A history counts superseded ratings as well as the
 * latest. The pairwise pass is O(R^2 DIM) per listing for R raters.
 * Ratings are only as complete as the local store's view of the network.
 */
#ifndef ZXV_SOCIAL_BUCKET_H
#define ZXV_SOCIAL_BUCKET_H

#include <stdint.h>
#include <stdbool.h>
#include "social_post.h"
#include "social_feed.h"
#include "social_store.h"

#define SB_SRC_MAX     128u
#define SB_TITLE_MAX   96u
#define SB_DESC_MAX    600u
#define SB_FPR_LEN     32u
#define SB_COMMENT_MAX 280u
#define SB_BUILTIN     0xFFFFFFFFu

/* The user's own bucket: the built-in default listing (B5). */
#define SB_DEFAULT_CID "bafybeiczsscdsbs7ffqz55asqdf3smv6klcw3gofszvwlyarci47bgf354"

typedef enum { SB_SRC_CID = 0, SB_SRC_IPNS = 1 } sb_src_kind_t;

typedef enum {
    SB_CAT_SYSTEM = 0,
    SB_CAT_APPS,
    SB_CAT_GAMES,
    SB_CAT_MODELS,
    SB_CAT_MEDIA,
    SB_CAT_DOCS,
    SB_CAT_OTHER,
    SB_CAT_COUNT
} sb_category_t;

/* A parsed listing. Pointers point into the record body. */
typedef struct {
    uint8_t category, src_kind;
    const uint8_t *src;
    uint32_t src_len;
    const uint8_t *title;
    uint32_t title_len;
    const uint8_t *desc;
    uint32_t desc_len;
    const uint8_t *fingerprint; /* SB_FPR_LEN */
} sb_listing_t;

/* B1: fill a record's body with a listing payload. The record must already
 * be sp_init'ed as SP_KIND_BUCKET_LISTING. Returns SP_OK or negative. */
int32_t sb_make_listing(sp_record_t *r, sb_category_t cat, sb_src_kind_t src_kind,
                        const uint8_t *src, uint32_t src_len, const uint8_t *title,
                        uint32_t title_len, const uint8_t *desc, uint32_t desc_len,
                        const uint8_t fingerprint[SB_FPR_LEN]);
int32_t sb_parse_listing(const sp_record_t *r, sb_listing_t *out);

/* B2 */
int32_t sb_make_rating(sp_record_t *r, const uint8_t listing_id[SP_ID_LEN], uint8_t stars,
                       const uint8_t *comment, uint32_t comment_len);
/* Stars 1..5 of a rating record, or 0 if it is not a valid rating. */
uint8_t sb_rating_stars(const sp_record_t *r);

/* THE RULE: true only if the listing's release fingerprint is in the
 * installing user's own trusted list. Ratings are never consulted. */
bool sb_install_trusted(const sp_record_t *listing, const uint8_t (*trusted)[SB_FPR_LEN],
                        uint32_t n_trusted);

/* Per-rater scratch for sb_aggregate, the caller's memory. */
typedef struct {
    const sp_record_t *rating; /* the counted (latest) rating */
    sf_vec_t history;
    zt_fx rep, ind, weight;
} sb_rater_t;

typedef struct {
    uint32_t N;    /* ISF block count, >= 2 (default 8) */
    sf_rep_fn rep; /* NULL = everyone at the newcomer weight 1/4 */
    void *rep_ctx;
} sb_config_t;

typedef struct {
    uint32_t count;     /* raters counted (one per author) */
    uint32_t truncated; /* raters dropped because scratch was full */
    zt_fx effective;    /* W = sum of weights, Q16 raters */
    zt_fx mean;         /* weighted mean stars, Q16 (0 if no raters) */
    zt_fx confidence;   /* W / (W + 3), Q16 in [0, 1) */
    zt_fx score;        /* shrunk mean, Q16 stars (3.0 with no raters) */
    zt_fx unweighted;   /* plain mean of the counted stars, for comparison */
} sb_aggregate_t;

void sb_config_default(sb_config_t *c);

/* B2-B4 over every rating of `listing_id` held in the store. */
void sb_aggregate(const ss_store_t *s, const uint8_t listing_id[SP_ID_LEN], const sb_config_t *cfg,
                  sb_rater_t *scratch, uint32_t scratch_cap, sb_aggregate_t *out);

/* B5 */
typedef struct {
    uint32_t store_index;       /* ss_at index, or SB_BUILTIN */
    const sp_record_t *listing; /* NULL for the builtin entry */
    sb_aggregate_t agg;
    zt_fx surplus; /* ISF novelty over earlier picks, Q16 in [0, 1] */
    int32_t adjusted;
} sb_entry_t;

typedef struct {
    sf_vec_t vec;
    sb_aggregate_t agg;
    const sp_record_t *rec;
    uint32_t store_index;
    zt_fx min_surplus;
    bool taken;
} sb_order_work_t;

/* Writes up to max entries, builtin default first. Returns the count. */
uint32_t sb_order(const ss_store_t *s, const sb_config_t *cfg, sb_order_work_t *work,
                  uint32_t work_cap, sb_rater_t *raters, uint32_t raters_cap, sb_entry_t *out,
                  uint32_t max);

#endif /* ZXV_SOCIAL_BUCKET_H */

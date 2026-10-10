/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* stream_seg.h — live and VOD segmentation for ZXV swarm streaming.
 *
 * A stream is cut into time-sliced SEGMENTS (about one second each). Every
 * segment is named by its IPFS CIDv1 (raw codec, sha2-256, the same bytes
 * Kubo computes, via kernel/src/ipfs_node) and described by a signed
 * MANIFEST. The segment bytes are carried as a multi-freight set
 * (kernel/src/freight): freight f holds bytes [f*k*20, (f+1)*k*20) and is a
 * systematic Cauchy Reed-Solomon code over GF(2^8), poly 0x11d.
 *
 * ROW IDS. A row is 20 payload bytes of one freight plus its id. Ids decide
 * the coefficient vector over the k data rows:
 *   id <  k        systematic: data row `id` itself (identity vector)
 *   k <= id < 256  Cauchy parity, coefficient 1 / (id XOR j)
 *   256 <= id      random linear combination (RLNC), coefficients from a
 *                  keyed xorshift of (freight id, freight, id); never zero
 * Ids 0..167 are byte-for-byte the rows freight_encode emits (byte 0 of a
 * UBH-168 freight packet is the id); stream_seg_row computes them directly
 * from the segment, one row at a time, so any holder of a decoded freight
 * can mint any row on demand.
 *
 * THE FOUNTAIN PROPERTY. With ids 0..255 the code is MDS: ANY k distinct ids
 * rebuild the freight. A viewer can therefore fetch disjoint id sets from
 * many peers at once with no coordination beyond "do not ask twice for the
 * same id": whatever k distinct rows arrive first, from whoever, decode.
 *
 * RATELESS MODE, AND ITS LIMIT. Freight stops at row 167 only because a
 * freight is defined as 168 frames. The Cauchy construction itself works for
 * every evaluation point x that is a field element not used as a data point
 * y_j = j (0..k-1): x = k..255. So ids 168..255 are fresh MDS parity rows
 * "beyond 168", and stream_seg_row / stream_dec handle them. GF(256) has
 * only 256 elements, so that is the end: an MDS code over GF(q) of length n
 * needs n <= q + 1 (a doubly-extended RS code reaches 257; not used here).
 * Past id 255 rows are RLNC: a random combination of k rows is innovative
 * (raises the rank) with probability 1 - 256^-(k - rank), so k + e random
 * rows fail to decode with probability below 256^-e / 255. That is
 * probabilistic, not MDS, and the decoder reports "not yet" instead of
 * guessing. RLNC ids need a 16-bit id, which does not fit a 21-byte UBH-168
 * frame, so the swarm (stream_swarm.h) advertises and moves ids 0..255 only.
 *
 * DECODING is online Gaussian elimination (stream_dec_*): each row is
 * reduced on arrival; a redundant one (rank does not rise) is reported so the
 * caller can count waste. Finish back-substitutes, checks that the padding is
 * zero and checks the freight's SHA-256 from the manifest; the joined
 * segment is then checked against its CID.
 *
 * MANIFEST (little-endian), body then signature:
 *   0 4 "ZXSM"   4 1 version 1   5 1 flags   6 1 k   7 1 nfreights
 *   8 8 stream id   16 4 seq   20 4 duration us   24 8 start time us
 *  32 4 data length   36 8 freight id   44 32 SHA-256 of the previous
 *  manifest body (zero for seq 0 / a fresh start)   76 1 cid length
 *  77 c CID (binary CIDv1)   then nfreights x 32 SHA-256 per freight
 *  then 2 signature length, then the signature.
 * The signature covers the whole body. It is produced and checked by caller
 * callbacks so the PQ signature layer (ML-DSA in kernel/src/pqsec, or any
 * other) plugs in later without touching this file; a NULL verify callback
 * is refused. Chaining each body's hash into the next makes splicing,
 * reordering or dropping a segment detectable.
 *
 * SEGMENTATION. stream_cut_* is a live cutter: push media units (access
 * units) with presentation time and keyframe flag; a segment closes before
 * the first keyframe at least `target` us after its start, or before any
 * unit once `max` us have passed or the buffer would overflow. stream_vod_plan
 * runs the same rule over an index of recorded units without copying, so a
 * VOD file and a live recording of the same units produce the same segments
 * and the same CIDs.
 *
 * SECURITY CONTEXT. This module assumes an authenticated, encrypted
 * transport: the PQ-secured Carracho sessions (ML-KEM key exchange) carry
 * every row, request and advert. Here, integrity of the content comes from
 * the manifest signature, the per-freight SHA-256 and the segment CID.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Every buffer is supplied by the caller. Depends on
 * freight.h, sha256.h and ipfs_node.h (CIDs).
 *
 * HONEST LIMITS. Erasure coding repairs losses, not lies: a peer that sends a
 * wrong row makes the decode fail its SHA-256 check, and the decoder cannot
 * say which row was bad. The caller must refetch (from other peers) and
 * should penalise the peers involved; per-row authentication tags are not
 * implemented. The manifest signature is only as strong as the scheme the
 * callback implements; the test uses a keyed SHA-256 stand-in that is NOT a
 * signature. Large PQ signatures (SLH-DSA, up to ~50 KB) exceed
 * STREAM_SIG_MAX. A segment holds at most STREAM_SEG_MAX_FREIGHTS freights
 * (215 040 bytes at k = 168), so very high bitrates need shorter segments.
 * RLNC decoding costs O(k^2) GF multiplies per row; systematic rows are
 * nearly free, so receivers should prefer ids < k when they can choose.
 */
#ifndef STREAM_SEG_H
#define STREAM_SEG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freight.h"
#include "ipfs_node.h"

#define STREAM_ROW_BYTES        FREIGHT_ROW_PAYLOAD /* 20 */
#define STREAM_K_MAX            FREIGHT_ROWS        /* 168 */
#define STREAM_MDS_IDS          256u                /* ids 0..255 are MDS */
#define STREAM_SEG_MAX_FREIGHTS 64u
#define STREAM_SEG_MAX_BYTES    (STREAM_SEG_MAX_FREIGHTS * FREIGHT_CAPACITY(STREAM_K_MAX))
#define STREAM_SIG_MAX          4864u /* ML-DSA-87 is 4627 bytes */
#define STREAM_SHA_BYTES        32u
#define STREAM_MANIFEST_FIXED   77u
#define STREAM_MANIFEST_BODY_MAX                                                                   \
    (STREAM_MANIFEST_FIXED + IPFSN_CID_BIN_MAX + STREAM_SEG_MAX_FREIGHTS * STREAM_SHA_BYTES)
#define STREAM_MANIFEST_MAX (STREAM_MANIFEST_BODY_MAX + 2u + STREAM_SIG_MAX)
#define STREAM_MANIFEST_VER 1u
/* Work bytes for one stream_dec_t: the k x (k + 20) matrix. */
#define STREAM_DEC_WORK(k)    ((uint32_t) (k) * ((uint32_t) (k) + STREAM_ROW_BYTES))
#define STREAM_DEC_WORK_BYTES STREAM_DEC_WORK(STREAM_K_MAX)

/* manifest flags */
#define STREAM_MF_LIVE     0x01u /* cut from a live source */
#define STREAM_MF_KEYSTART 0x02u /* segment starts with a keyframe */
#define STREAM_MF_END      0x04u /* last segment of the stream */
#define STREAM_MF_ALL      0x07u

enum {
    STREAM_OK = 0,
    STREAM_ERR_ARG = -1,
    STREAM_ERR_SPACE = -2,
    STREAM_ERR_FORMAT = -3,
    STREAM_ERR_HASH = -4,
    STREAM_ERR_SIG = -5,
    STREAM_ERR_TOO_FEW = -6,
    STREAM_ERR_CHAIN = -7,
};

/* ---- manifest ---- */
typedef struct {
    uint64_t stream_id;
    uint32_t seq;
    uint32_t dur_us;
    uint64_t t_start_us;
    uint32_t data_len;
    uint8_t flags;
    uint8_t k;         /* data rows per freight, 1..168 */
    uint8_t nfreights; /* 1..STREAM_SEG_MAX_FREIGHTS */
    uint64_t freight_id;
    uint8_t prev_sha256[STREAM_SHA_BYTES];
    ipfsn_cid_t cid;
    uint8_t freight_sha256[STREAM_SEG_MAX_FREIGHTS][STREAM_SHA_BYTES];
} stream_manifest_t;

/* sign: write at most cap bytes, set *sig_len, return 0 on success.
 * verify: return true only for a valid signature over msg. */
typedef int (*stream_sign_fn)(void *ctx, const uint8_t *msg, uint32_t len, uint8_t *sig,
                              uint32_t cap, uint32_t *sig_len);
typedef bool (*stream_verify_fn)(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig,
                                 uint32_t sig_len);

/* Freights needed for `len` bytes at k; 0 if too big or k invalid. */
uint32_t stream_seg_nfreights(uint32_t len, uint8_t k);

/* Describe segment `data[0..len)`: CID, freight id (from the CID digest),
 * per-freight hashes. prev may be NULL (zero chain hash). */
int stream_manifest_build(stream_manifest_t *m, uint64_t stream_id, uint32_t seq,
                          uint64_t t_start_us, uint32_t dur_us, uint8_t flags, uint8_t k,
                          const uint8_t prev_sha256[STREAM_SHA_BYTES], const uint8_t *data,
                          uint32_t len);
/* Serialise body + signature. *body_sha (may be NULL) receives SHA-256 of the
 * body, which is the next manifest's prev_sha256. Returns bytes or < 0. */
int stream_manifest_serialize(const stream_manifest_t *m, uint8_t *out, uint32_t cap,
                              stream_sign_fn sign, void *sign_ctx,
                              uint8_t body_sha[STREAM_SHA_BYTES]);
/* Strict parse + signature check (exact length, reserved values, CID form).
 * verify must not be NULL. */
int stream_manifest_parse(const uint8_t *in, uint32_t len, stream_manifest_t *m,
                          stream_verify_fn verify, void *verify_ctx,
                          uint8_t body_sha[STREAM_SHA_BYTES]);
/* True when m continues the chain whose last body hash is prev. */
bool stream_manifest_chains(const stream_manifest_t *m, const uint8_t prev[STREAM_SHA_BYTES]);
/* Check the joined segment against the CID. */
int stream_seg_verify(const stream_manifest_t *m, const uint8_t *data, uint32_t len);

/* Freight header (freight.h) for freight `fr`, so the freight module can
 * encode/decode it (rows 0..167). */
int stream_freight_header(const stream_manifest_t *m, uint32_t fr, freight_header_t *h);
/* Payload bytes of freight fr. */
uint32_t stream_freight_len(const stream_manifest_t *m, uint32_t fr);

/* ---- rows ---- */
/* Coefficient of data row j in row `id` of freight fr. */
uint8_t stream_row_coef(const stream_manifest_t *m, uint32_t fr, uint32_t id, uint32_t j);
/* Compute row `id` (0..65535) of freight fr from the segment bytes. */
int stream_seg_row(const stream_manifest_t *m, const uint8_t *data, uint32_t len, uint32_t fr,
                   uint32_t id, uint8_t out[STREAM_ROW_BYTES]);

/* Online decoder for one freight. */
typedef struct {
    const stream_manifest_t *m;
    uint32_t fr;
    uint8_t k, rank;
    uint8_t piv[STREAM_K_MAX]; /* piv[col] = 1 + slot of the row whose pivot is col */
    uint8_t *work;             /* rank rows of (k + 20) bytes */
} stream_dec_t;

int stream_dec_init(stream_dec_t *d, const stream_manifest_t *m, uint32_t fr, uint8_t *work,
                    uint32_t work_len);
/* 1: innovative, 0: redundant (or already full), < 0: error. */
int stream_dec_add(stream_dec_t *d, uint32_t id, const uint8_t row[STREAM_ROW_BYTES]);
bool stream_dec_ready(const stream_dec_t *d);
/* Back-substitute and write the freight payload (stream_freight_len bytes)
 * to out; checks zero padding and the freight SHA-256. */
int stream_dec_finish(stream_dec_t *d, uint8_t *out, uint32_t cap);

/* ---- segmentation ---- */
typedef struct {
    uint8_t *buf;
    uint32_t cap, len;
    uint32_t target_us, max_us;
    uint64_t t0, last_pts;
    uint32_t seq;
    bool open, key_start;
} stream_cutter_t;

typedef struct {
    const uint8_t *data;
    uint32_t len;
    uint32_t seq;
    uint64_t t_start_us;
    uint32_t dur_us;
    bool key_start;
} stream_seg_out_t;

#define STREAM_CUT_NONE  0 /* unit absorbed */
#define STREAM_CUT_READY 1 /* a segment is ready in *out; the unit was NOT absorbed */

int stream_cut_init(stream_cutter_t *c, uint8_t *buf, uint32_t cap, uint32_t target_us,
                    uint32_t max_us);
/* Push one unit. On STREAM_CUT_READY take the segment from *out, call
 * stream_cut_next, then push the same unit again. pts must not go backwards. */
int stream_cut_push(stream_cutter_t *c, const uint8_t *unit, uint32_t len, uint64_t pts_us,
                    bool keyframe, stream_seg_out_t *out);
void stream_cut_next(stream_cutter_t *c);
/* Close the open segment at end of stream; tail_us is the last unit's
 * duration. Returns STREAM_CUT_READY with *out, or STREAM_CUT_NONE if empty. */
int stream_cut_flush(stream_cutter_t *c, uint32_t tail_us, stream_seg_out_t *out);

typedef struct {
    uint32_t offset, len; /* bytes in the recording */
    uint64_t pts_us;
    bool keyframe;
} stream_unit_t;

typedef struct {
    uint32_t first_unit, nunits;
    uint32_t offset, len;
    uint64_t t_start_us;
    uint32_t dur_us;
    bool key_start;
} stream_vod_seg_t;

/* Same cut rule as the live cutter over recorded units (units must be
 * contiguous: units[i+1].offset == units[i].offset + units[i].len). Returns
 * the number of segments written, or < 0. max_bytes bounds one segment. */
int stream_vod_plan(const stream_unit_t *units, uint32_t n, uint32_t target_us, uint32_t max_us,
                    uint32_t max_bytes, uint32_t tail_us, stream_vod_seg_t *segs, uint32_t cap);

#endif /* STREAM_SEG_H */

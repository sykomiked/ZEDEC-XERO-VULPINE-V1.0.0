/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* freight.h — freight packets for the ZXV peer network.
 *
 * A SMART PACKET is one UBH-168 frame: 21 octets (168 bits, see ubh.h).
 *   byte 0      row index, 0..167
 *   bytes 1..20 twenty payload bytes
 * A FREIGHT is a matrix of 168 smart packets, 3528 bytes on the wire.
 *
 * Three layers, each one honest about what it does:
 *
 *  1. "Holographic" recovery is ERASURE CODING. The 168 rows are a systematic
 *     Reed-Solomon code over GF(2^8) (polynomial 0x11d). Rows 0..k-1 carry the
 *     payload verbatim; rows k..167 are parity. Each of the 20 byte columns is
 *     coded independently across the rows. Parity row r, data column j has the
 *     Cauchy coefficient 1 / (r XOR j); every square submatrix of a Cauchy
 *     matrix is invertible, so ANY k of the 168 rows rebuild the freight (MDS).
 *     Decoding is Gauss-Jordan over GF(256) on the k x k submatrix of the rows
 *     that arrived. The code corrects erasures only; a corrupted row is caught
 *     (not located) by the SHA-256 of the payload carried in the header.
 *
 *  2. The FREIGHT HEADER (64 bytes when serialised) travels separately in the
 *     outer message: version, 64-bit freight id, k, payload length, sequence
 *     number and count for multi-freight sets, an expansion limit, and the
 *     SHA-256 of this freight's payload. Rebuild verifies that hash.
 *
 *  3. SMART CONTENT: the payload may be a small op stream that the receiver
 *     expands. Ops are LITERAL, COPY (LZ77-style back reference, overlap
 *     allowed, so self-similar data grows from few bytes), CIDREF (bytes the
 *     receiver resolves by content id through a caller callback) and SEED
 *     (deterministic generators: xorshift stream, repeat pattern, Fibonacci
 *     word, Sierpinski bitmap). The stream declares its total length and may
 *     carry a SHA-256 of the expanded output. Expansion refuses any stream
 *     whose declared length exceeds the caller's limit, refuses any op that
 *     would pass the declared length, and rejects malformed input without
 *     reading or writing outside the given buffers. A freight therefore can
 *     never act as a decompression bomb.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Every buffer is supplied by the caller. The only
 * external dependency is sha256 (kernel/src/robin_debanks/sha256.h).
 *
 * HONEST LIMITS. Random data does not compress: an op stream of random bytes
 * is slightly LARGER than the bytes, and parity plus row indices cost more on
 * top, so random data always expands to less than one byte per freight byte.
 * COPY only wins on data that actually repeats. CIDREF only "expands" because
 * the receiver already has, or fetches, the referenced content; the bytes
 * still exist somewhere, the freight just names them. SEED only helps for data
 * that was genuinely produced by one of the built-in generators; it cannot
 * find a seed for arbitrary data, and the encoder never tries. Nothing here
 * stores more information than the bits it carries plus what the receiver
 * already holds.
 */
#ifndef FREIGHT_H
#define FREIGHT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- geometry ---- */
#define FREIGHT_PACKET_BYTES 21 /* one UBH-168 frame */
#define FREIGHT_ROW_PAYLOAD  20 /* bytes 1..20 of each packet */
#define FREIGHT_ROWS         168
#define FREIGHT_BYTES        (FREIGHT_ROWS * FREIGHT_PACKET_BYTES) /* 3528 */
#define FREIGHT_MAX_PAYLOAD  (FREIGHT_ROWS * FREIGHT_ROW_PAYLOAD)  /* 3360 at k = 168 */
#define FREIGHT_CAPACITY(k)  ((uint32_t) (k) * FREIGHT_ROW_PAYLOAD)
#define FREIGHT_HEADER_BYTES 64
#define FREIGHT_VERSION      1
#define FREIGHT_MAX_SET      16384 /* freights in one multi-freight set */
#define FREIGHT_SHA256_BYTES 32

/* Work buffer freight_decode needs: the k x (k + 20) augmented matrix. */
#define FREIGHT_DECODE_WORK(k)    ((uint32_t) (k) * ((uint32_t) (k) + FREIGHT_ROW_PAYLOAD))
#define FREIGHT_DECODE_WORK_BYTES FREIGHT_DECODE_WORK(FREIGHT_ROWS) /* 31584 */

/* ---- header flags ---- */
#define FREIGHT_F_OPSTREAM 0x01u /* payload (of the whole set) is an op stream */

/* ---- results ---- */
enum {
    FREIGHT_OK = 0,
    FREIGHT_ERR_ARG = -1,      /* bad argument */
    FREIGHT_ERR_SPACE = -2,    /* caller buffer too small */
    FREIGHT_ERR_TOO_FEW = -3,  /* fewer than k distinct rows arrived */
    FREIGHT_ERR_HASH = -4,     /* SHA-256 mismatch: corruption */
    FREIGHT_ERR_FORMAT = -5,   /* malformed header or op stream */
    FREIGHT_ERR_LIMIT = -6,    /* declared expansion exceeds the caller's limit */
    FREIGHT_ERR_RESOLVE = -7,  /* CIDREF could not be resolved */
    FREIGHT_ERR_SEQ = -8,      /* freight does not belong to this set */
    FREIGHT_ERR_SINGULAR = -9, /* cannot happen for a valid code; defensive */
};

/* ---- GF(2^8), polynomial x^8+x^4+x^3+x^2+1 (0x11d), generator 2 ---- */
uint8_t freight_gf_mul(uint8_t a, uint8_t b);
uint8_t freight_gf_inv(uint8_t a);            /* a != 0; returns 0 for a == 0 */
uint8_t freight_gf_div(uint8_t a, uint8_t b); /* b != 0; returns 0 for b == 0 */
/* Generator matrix entry: row r (0..167) times data column j (0..k-1). */
uint8_t freight_gen_coef(uint8_t k, uint32_t r, uint32_t j);

/* ---- header ---- */
typedef struct {
    uint8_t version;       /* FREIGHT_VERSION */
    uint8_t k;             /* data rows, 1..168 */
    uint8_t flags;         /* FREIGHT_F_* */
    uint16_t payload_len;  /* this freight's payload bytes, <= k * 20 */
    uint16_t seq;          /* index in its set, < seq_count */
    uint16_t seq_count;    /* freights in the set, 1..FREIGHT_MAX_SET */
    uint64_t freight_id;   /* names the set; every member shares it */
    uint64_t expand_limit; /* max expanded bytes (op stream only, else 0) */
    uint8_t payload_sha256[FREIGHT_SHA256_BYTES];
} freight_header_t;

/* Fill a single-freight header (seq 0 of 1) for `payload`. */
int freight_header_init(freight_header_t *h, uint64_t freight_id, uint8_t k, uint8_t flags,
                        uint64_t expand_limit, const uint8_t *payload, uint32_t len);
int freight_header_check(const freight_header_t *h);
/* Little-endian: "ZXFH" ver k flags 0 | len seq count 0 0 | id | limit | sha. */
int freight_header_serialize(const freight_header_t *h, uint8_t out[FREIGHT_HEADER_BYTES]);
int freight_header_parse(const uint8_t in[FREIGHT_HEADER_BYTES], freight_header_t *h);

/* ---- erasure code ---- */
/* Write all 168 smart packets. The payload must match h's length and hash. */
int freight_encode(const freight_header_t *h, const uint8_t *payload, uint8_t out[FREIGHT_BYTES]);
/* Rebuild from `npackets` packets (21 bytes each, any order, duplicates and
 * out-of-range row indices ignored). Uses the first k distinct rows. Writes
 * h->payload_len bytes to `out` and verifies the payload hash. `work` must hold
 * FREIGHT_DECODE_WORK(h->k) bytes. */
int freight_decode(const freight_header_t *h, const uint8_t *packets, uint32_t npackets,
                   uint8_t *out, uint32_t out_cap, uint8_t *work, uint32_t work_len);

/* ---- multi-freight ---- */
/* Freights needed for `len` bytes at k (0 bytes still needs one); 0 if the
 * set would exceed FREIGHT_MAX_SET or k is invalid. */
uint32_t freight_set_count(uint64_t len, uint8_t k);
/* Build member `seq` of the set carrying data[0..len). */
int freight_split(uint64_t freight_id, uint8_t k, uint8_t flags, uint64_t expand_limit,
                  const uint8_t *data, uint64_t len, uint32_t seq, freight_header_t *h,
                  uint8_t packets[FREIGHT_BYTES]);

typedef struct {
    uint8_t *out;
    uint64_t cap;
    bool started;
    uint8_t k, flags;
    uint16_t count, received;
    uint64_t freight_id, expand_limit, total;
    uint8_t seen[FREIGHT_MAX_SET / 8];
} freight_join_t;

void freight_join_init(freight_join_t *j, uint8_t *out, uint64_t cap);
/* Decode one member into its place. A member already received returns OK. */
int freight_join_add(freight_join_t *j, const freight_header_t *h, const uint8_t *packets,
                     uint32_t npackets, uint8_t *work, uint32_t work_len);
/* True once every member has arrived; *total is the joined length. */
bool freight_join_complete(const freight_join_t *j, uint64_t *total);

/* ---- op stream ----
 * Header: "ZXOP" ver(1) flags total(u64 LE) [sha256 of the output if flag 1].
 * Then ops until the end of the stream. Integers are LEB128 varints (at most
 * 10 bytes, minimal encoding, no overflow); every op length is >= 1.
 *   0x01 LITERAL  n, n bytes
 *   0x02 COPY     distance (1..produced), length
 *   0x03 CIDREF   cid_len (1..FREIGHT_CID_MAX), cid bytes, length
 *   0x04 SEED     generator (u8), param_len (u8), params, length
 */
#define FREIGHT_OPS_MAGIC_LEN 4
#define FREIGHT_OPS_VERSION   1
#define FREIGHT_OPS_F_SHA     0x01u
#define FREIGHT_OPS_HDR       14
#define FREIGHT_OPS_HDR_SHA   (FREIGHT_OPS_HDR + FREIGHT_SHA256_BYTES)
#define FREIGHT_CID_MAX       128

#define FREIGHT_OP_LITERAL 0x01u
#define FREIGHT_OP_COPY    0x02u
#define FREIGHT_OP_CIDREF  0x03u
#define FREIGHT_OP_SEED    0x04u

/* SEED generators and their params. */
#define FREIGHT_GEN_XORSHIFT 1 /* 8 bytes: u64 LE seed, nonzero; xorshift64* bytes LE */
#define FREIGHT_GEN_REPEAT   2 /* 1..32 bytes: the pattern, repeated */
#define FREIGHT_GEN_FIBWORD  3 /* 2 bytes a != b: Fibonacci word abaababaab... */
#define FREIGHT_GEN_SIERPINSKI                                                                     \
    4 /* 3 bytes: log2 width (1..15), on, off:                                                     \
       * pixel (x, y) = on if (x & (y mod W)) == 0,                                                \
       * row-major, the integer IFS attractor */
#define FREIGHT_GEN_PARAM_MAX 32

typedef bool (*freight_resolve_fn)(void *ctx, const uint8_t *cid, uint32_t cid_len, uint8_t *out,
                                   uint64_t len);

/* Read the declared total and flags of a stream without expanding it. */
int freight_ops_peek(const uint8_t *ops, uint64_t ops_len, uint64_t *total, bool *has_sha);

/* Expand into out[0..total). Fails with FREIGHT_ERR_LIMIT before writing
 * anything if the declared total exceeds out_cap or limit. `resolve` may be
 * NULL (CIDREF then fails). Verifies the output SHA-256 when present. */
int freight_ops_expand(const uint8_t *ops, uint64_t ops_len, uint8_t *out, uint64_t out_cap,
                       uint64_t limit, freight_resolve_fn resolve, void *ctx, uint64_t *out_len);

/* Run one generator directly (used by SEED; exposed for tests and senders). */
int freight_gen_run(uint8_t gen, const uint8_t *params, uint32_t plen, uint8_t *out, uint64_t len);

/* Hand-built streams. Errors are sticky; finish reports the first one and
 * checks that the ops produce exactly the declared total. */
typedef struct {
    uint8_t *buf;
    uint64_t cap, len;
    uint64_t declared, produced;
    int err;
} freight_ops_writer_t;

void freight_ops_writer_init(freight_ops_writer_t *w, uint8_t *buf, uint64_t cap,
                             uint64_t declared_total, const uint8_t *sha256_or_null);
void freight_ops_put_literal(freight_ops_writer_t *w, const uint8_t *b, uint64_t n);
void freight_ops_put_copy(freight_ops_writer_t *w, uint64_t distance, uint64_t len);
void freight_ops_put_cidref(freight_ops_writer_t *w, const uint8_t *cid, uint32_t cid_len,
                            uint64_t len);
void freight_ops_put_seed(freight_ops_writer_t *w, uint8_t gen, const uint8_t *params,
                          uint32_t plen, uint64_t len);
int freight_ops_writer_finish(freight_ops_writer_t *w, uint64_t *out_len);

/* Greedy LZ77 encoder: in -> LITERAL/COPY op stream (with output SHA-256 if
 * with_sha). Hash chain over 4-byte prefixes, chain depth 128, match length
 * capped at 65536. Deterministic: the same input and work size always give
 * the same stream. `work` holds 4096 hash heads plus a power-of-two window of
 * chain links (window = largest power of two <= work_words - 4096, capped at
 * 65536; distances never exceed it). Needs work_words >= FREIGHT_ENC_WORK_MIN.
 * Input length must be < 2^32 - 1. */
#define FREIGHT_ENC_HASH_BITS  12
#define FREIGHT_ENC_WORK_MIN   ((1u << FREIGHT_ENC_HASH_BITS) + 256u)
#define FREIGHT_ENC_WORK_WORDS ((1u << FREIGHT_ENC_HASH_BITS) + 65536u)
int freight_ops_encode(const uint8_t *in, uint64_t n, bool with_sha, uint8_t *out, uint64_t cap,
                       uint64_t *out_len, uint32_t *work, uint32_t work_words);
/* Worst-case encoded size for n input bytes (all literals). */
uint64_t freight_ops_bound(uint64_t n, bool with_sha);

#endif /* FREIGHT_H */

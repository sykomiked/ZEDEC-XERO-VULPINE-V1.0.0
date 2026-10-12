/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_freight.c — ABI 1.5: erasure-coded freight packets over kernel/src/freight.
 * Every computation is the kernel's (freight.c: GF(2^8), the Cauchy
 * Reed-Solomon row code, headers, split/join; freight_ops.c: op streams).
 * This file keeps kernel structs off the ABI (headers cross as their 64-byte
 * serialized form), owns the work buffers the kernel asks its callers for,
 * and maps every kernel status to a ZXV_E_* code with a reason. */
#include "zxv_internal.h"

#include <stdlib.h>
#include <string.h>

#include "freight.h"

_Static_assert(ZXV_FREIGHT_PACKET_BYTES == FREIGHT_PACKET_BYTES, "packet size");
_Static_assert(ZXV_FREIGHT_ROW_PAYLOAD == FREIGHT_ROW_PAYLOAD, "row payload");
_Static_assert(ZXV_FREIGHT_ROWS == FREIGHT_ROWS, "rows");
_Static_assert(ZXV_FREIGHT_BYTES == FREIGHT_BYTES, "freight bytes");
_Static_assert(ZXV_FREIGHT_MAX_PAYLOAD == FREIGHT_MAX_PAYLOAD, "max payload");
_Static_assert(ZXV_FREIGHT_HEADER_BYTES == FREIGHT_HEADER_BYTES, "header bytes");
_Static_assert(ZXV_FREIGHT_VERSION == FREIGHT_VERSION, "version");
_Static_assert(ZXV_FREIGHT_MAX_SET == FREIGHT_MAX_SET, "set size");
_Static_assert(ZXV_FREIGHT_SET_MAX_BYTES == (uint64_t) FREIGHT_MAX_SET * FREIGHT_MAX_PAYLOAD,
               "set bytes");
_Static_assert(ZXV_FREIGHT_F_OPSTREAM == FREIGHT_F_OPSTREAM, "flag");
_Static_assert(ZXV_FREIGHT_CID_MAX == FREIGHT_CID_MAX, "cid max");
_Static_assert(ZXV_FREIGHT_GEN_PARAM_MAX == FREIGHT_GEN_PARAM_MAX, "param max");
_Static_assert(ZXV_FREIGHT_GEN_XORSHIFT == FREIGHT_GEN_XORSHIFT &&
                   ZXV_FREIGHT_GEN_REPEAT == FREIGHT_GEN_REPEAT &&
                   ZXV_FREIGHT_GEN_FIBWORD == FREIGHT_GEN_FIBWORD &&
                   ZXV_FREIGHT_GEN_SIERPINSKI == FREIGHT_GEN_SIERPINSKI,
               "generators");

/* The kernel's status, mapped. `what` names the call for the detail. */
static int32_t fr_status(int rc, const char *what)
{
    switch (rc) {
    case FREIGHT_OK: return zxvi_ok();
    case FREIGHT_ERR_ARG: return zxvi_fail(ZXV_E_ARG, "%s: invalid argument", what);
    case FREIGHT_ERR_SPACE: return zxvi_fail(ZXV_E_CAPACITY, "%s: buffer too small", what);
    case FREIGHT_ERR_TOO_FEW:
        return zxvi_fail(ZXV_E_ERASURE, "%s: fewer than k distinct rows arrived", what);
    case FREIGHT_ERR_HASH:
        return zxvi_fail(ZXV_E_VERIFY, "%s: SHA-256 mismatch (corrupted row or wrong header)",
                         what);
    case FREIGHT_ERR_FORMAT: return zxvi_fail(ZXV_E_FORMAT, "%s: malformed input", what);
    case FREIGHT_ERR_LIMIT:
        return zxvi_fail(ZXV_E_TOO_LONG, "%s: declared length exceeds the limit", what);
    case FREIGHT_ERR_RESOLVE:
        return zxvi_fail(ZXV_E_NOT_FOUND, "%s: a CIDREF could not be resolved", what);
    case FREIGHT_ERR_SEQ:
        return zxvi_fail(ZXV_E_STATE, "%s: freight does not belong to this set", what);
    case FREIGHT_ERR_SINGULAR:
        return zxvi_fail(ZXV_E_INTERNAL, "%s: singular decode matrix (cannot happen)", what);
    default: return zxvi_fail(ZXV_E_INTERNAL, "%s: unexpected kernel status %d", what, rc);
    }
}

/* R4 for bytes: needed first; ZXV_OK means the caller may write. */
static int32_t bytes_room(size_t needed, const uint8_t *buf, size_t cap, size_t *out_len,
                          const char *what)
{
    *out_len = needed;
    if (needed && (!buf || cap < needed))
        return zxvi_fail(ZXV_E_BUFFER, "%s: needs %zu bytes, buffer holds %zu", what, needed,
                         buf ? cap : (size_t) 0);
    return ZXV_OK;
}

static int32_t parse_hdr(const uint8_t *header, freight_header_t *h, const char *what)
{
    if (!header) return zxvi_fail(ZXV_E_ARG, "%s: header is NULL", what);
    int rc = freight_header_parse(header, h);
    if (rc == FREIGHT_OK) return ZXV_OK;
    if (rc == FREIGHT_ERR_FORMAT) return zxvi_fail(ZXV_E_FORMAT, "%s: malformed freight header", what);
    return fr_status(rc, what);
}

/* Validate (k, flags, expand_limit) the way header_check will, so the
 * caller gets a precise reason instead of a generic ZXV_E_ARG. */
static int32_t check_kfl(uint8_t k, uint8_t flags, uint64_t expand_limit, const char *what)
{
    if (k < 1 || k > FREIGHT_ROWS)
        return zxvi_fail(ZXV_E_ARG, "%s: k = %u is not 1..168", what, (unsigned) k);
    if (flags & ~FREIGHT_F_OPSTREAM)
        return zxvi_fail(ZXV_E_ARG, "%s: unknown flags 0x%02x", what, (unsigned) flags);
    if (!(flags & FREIGHT_F_OPSTREAM) && expand_limit != 0)
        return zxvi_fail(ZXV_E_ARG, "%s: expand_limit needs ZXV_FREIGHT_F_OPSTREAM", what);
    return ZXV_OK;
}

/* ===================================================================== */
/* Headers                                                               */
/* ===================================================================== */

int32_t zxv_freight_header_init(uint64_t freight_id, uint8_t k, uint8_t flags,
                                uint64_t expand_limit, const uint8_t *payload, size_t len,
                                uint8_t out_header[64])
{
    if (!out_header) return zxvi_fail(ZXV_E_ARG, "freight_header_init: out_header is NULL");
    if (len && !payload) return zxvi_fail(ZXV_E_ARG, "freight_header_init: payload is NULL");
    int32_t r = check_kfl(k, flags, expand_limit, "freight_header_init");
    if (r) return r;
    if (len > FREIGHT_CAPACITY(k))
        return zxvi_fail(ZXV_E_TOO_LONG, "freight_header_init: %zu bytes; k = %u carries %u",
                         len, (unsigned) k, (unsigned) FREIGHT_CAPACITY(k));
    freight_header_t h;
    int rc = freight_header_init(&h, freight_id, k, flags, expand_limit, payload, (uint32_t) len);
    if (rc == FREIGHT_OK) rc = freight_header_serialize(&h, out_header);
    return fr_status(rc, "freight_header_init");
}

int32_t zxv_freight_header_serialize(uint8_t k, uint8_t flags, uint16_t payload_len, uint16_t seq,
                                     uint16_t seq_count, uint64_t freight_id,
                                     uint64_t expand_limit, const uint8_t payload_sha256[32],
                                     uint8_t out_header[64])
{
    if (!payload_sha256 || !out_header)
        return zxvi_fail(ZXV_E_ARG, "freight_header_serialize: NULL argument");
    int32_t r = check_kfl(k, flags, expand_limit, "freight_header_serialize");
    if (r) return r;
    freight_header_t h;
    h.version = FREIGHT_VERSION;
    h.k = k;
    h.flags = flags;
    h.payload_len = payload_len;
    h.seq = seq;
    h.seq_count = seq_count;
    h.freight_id = freight_id;
    h.expand_limit = expand_limit;
    memcpy(h.payload_sha256, payload_sha256, 32);
    if (freight_header_check(&h) != FREIGHT_OK)
        return zxvi_fail(ZXV_E_ARG,
                         "freight_header_serialize: payload_len %u, seq %u of %u is not valid "
                         "at k = %u",
                         (unsigned) payload_len, (unsigned) seq, (unsigned) seq_count,
                         (unsigned) k);
    return fr_status(freight_header_serialize(&h, out_header), "freight_header_serialize");
}

int32_t zxv_freight_header_parse(const uint8_t header[64], uint8_t *out_version, uint8_t *out_k,
                                 uint8_t *out_flags, uint16_t *out_payload_len, uint16_t *out_seq,
                                 uint16_t *out_seq_count, uint64_t *out_freight_id,
                                 uint64_t *out_expand_limit, uint8_t out_payload_sha256[32])
{
    freight_header_t h;
    int32_t r = parse_hdr(header, &h, "freight_header_parse");
    if (r) return r;
    if (out_version) *out_version = h.version;
    if (out_k) *out_k = h.k;
    if (out_flags) *out_flags = h.flags;
    if (out_payload_len) *out_payload_len = h.payload_len;
    if (out_seq) *out_seq = h.seq;
    if (out_seq_count) *out_seq_count = h.seq_count;
    if (out_freight_id) *out_freight_id = h.freight_id;
    if (out_expand_limit) *out_expand_limit = h.expand_limit;
    if (out_payload_sha256) memcpy(out_payload_sha256, h.payload_sha256, 32);
    return zxvi_ok();
}

int32_t zxv_freight_header_check(const uint8_t header[64])
{
    freight_header_t h;
    int32_t r = parse_hdr(header, &h, "freight_header_check");
    return r ? r : zxvi_ok();
}

/* ===================================================================== */
/* The erasure code                                                      */
/* ===================================================================== */

int32_t zxv_freight_encode(const uint8_t header[64], const uint8_t *payload, size_t len,
                           uint8_t out_packets[3528])
{
    freight_header_t h;
    int32_t r = parse_hdr(header, &h, "freight_encode");
    if (r) return r;
    if (!out_packets) return zxvi_fail(ZXV_E_ARG, "freight_encode: out_packets is NULL");
    if (len && !payload) return zxvi_fail(ZXV_E_ARG, "freight_encode: payload is NULL");
    if (len != h.payload_len)
        return zxvi_fail(ZXV_E_ARG, "freight_encode: payload is %zu bytes; the header says %u",
                         len, (unsigned) h.payload_len);
    return fr_status(freight_encode(&h, payload, out_packets), "freight_encode");
}

/* Packets in, validated: a whole number of 21-byte packets. */
static int32_t packet_count(const uint8_t *packets, size_t packets_len, uint32_t *n,
                            const char *what)
{
    if (packets_len && !packets) return zxvi_fail(ZXV_E_ARG, "%s: packets is NULL", what);
    if (packets_len % FREIGHT_PACKET_BYTES)
        return zxvi_fail(ZXV_E_ARG, "%s: %zu bytes is not a whole number of 21-byte packets",
                         what, packets_len);
    size_t c = packets_len / FREIGHT_PACKET_BYTES;
    if (c > UINT32_MAX) return zxvi_fail(ZXV_E_TOO_LONG, "%s: too many packets", what);
    *n = (uint32_t) c;
    return ZXV_OK;
}

int32_t zxv_freight_decode(const uint8_t header[64], const uint8_t *packets, size_t packets_len,
                           uint8_t *buf, size_t cap, size_t *out_len)
{
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "freight_decode: out_len is NULL");
    freight_header_t h;
    int32_t r = parse_hdr(header, &h, "freight_decode");
    if (r) return r;
    uint32_t n;
    r = packet_count(packets, packets_len, &n, "freight_decode");
    if (r) return r;
    size_t need;
    r = bytes_room(h.payload_len, buf, cap, &need, "freight_decode");
    if (r) {
        *out_len = need;
        return r;
    }
    uint8_t *work = malloc(FREIGHT_DECODE_WORK(h.k));
    if (!work) return zxvi_fail(ZXV_E_NOMEM, "freight_decode: out of memory");
    uint8_t one = 0; /* decode wants a non-NULL out only for a nonempty payload */
    int rc = freight_decode(&h, packets, n, h.payload_len ? buf : &one, h.payload_len, work,
                            FREIGHT_DECODE_WORK(h.k));
    zxvi_wipe(work, FREIGHT_DECODE_WORK(h.k));
    free(work);
    if (rc) return fr_status(rc, "freight_decode");
    *out_len = h.payload_len;
    return zxvi_ok();
}

/* ===================================================================== */
/* Sets                                                                  */
/* ===================================================================== */

int32_t zxv_freight_set_count(uint64_t len, uint8_t k, uint32_t *out_count)
{
    if (!out_count) return zxvi_fail(ZXV_E_ARG, "freight_set_count: out_count is NULL");
    if (k < 1 || k > FREIGHT_ROWS)
        return zxvi_fail(ZXV_E_ARG, "freight_set_count: k = %u is not 1..168", (unsigned) k);
    uint32_t n = freight_set_count(len, k);
    if (n == 0)
        return zxvi_fail(ZXV_E_TOO_LONG, "freight_set_count: %llu bytes need more than %u freights",
                         (unsigned long long) len, (unsigned) FREIGHT_MAX_SET);
    *out_count = n;
    return zxvi_ok();
}

int32_t zxv_freight_split(uint64_t freight_id, uint8_t k, uint8_t flags, uint64_t expand_limit,
                          const uint8_t *data, size_t len, uint32_t seq, uint8_t out_header[64],
                          uint8_t out_packets[3528])
{
    if (!out_header || !out_packets || (len && !data))
        return zxvi_fail(ZXV_E_ARG, "freight_split: NULL argument");
    int32_t r = check_kfl(k, flags, expand_limit, "freight_split");
    if (r) return r;
    uint32_t n = freight_set_count(len, k);
    if (n == 0)
        return zxvi_fail(ZXV_E_TOO_LONG, "freight_split: %zu bytes need more than %u freights",
                         len, (unsigned) FREIGHT_MAX_SET);
    if (seq >= n)
        return zxvi_fail(ZXV_E_RANGE, "freight_split: seq %u; the set has %u members",
                         (unsigned) seq, (unsigned) n);
    freight_header_t h;
    uint8_t pk[FREIGHT_BYTES];
    int rc = freight_split(freight_id, k, flags, expand_limit, data, len, seq, &h, pk);
    if (rc == FREIGHT_OK) rc = freight_header_serialize(&h, out_header);
    if (rc) return fr_status(rc, "freight_split");
    memcpy(out_packets, pk, FREIGHT_BYTES);
    return zxvi_ok();
}

#define JOIN_MAGIC 0x5A584A4Eu /* "ZXJN" */

struct zxv_freight_join {
    uint32_t magic;
    freight_join_t j;
    uint8_t *buf;
    uint64_t cap;
    uint8_t *work; /* FREIGHT_DECODE_WORK_BYTES */
};

int32_t zxv_freight_join_create(uint64_t capacity, zxv_freight_join **out_join)
{
    if (!out_join) return zxvi_fail(ZXV_E_ARG, "freight_join_create: out_join is NULL");
    if (capacity > ZXV_FREIGHT_SET_MAX_BYTES)
        return zxvi_fail(ZXV_E_RANGE, "freight_join_create: capacity %llu is over %llu",
                         (unsigned long long) capacity,
                         (unsigned long long) ZXV_FREIGHT_SET_MAX_BYTES);
    zxv_freight_join *jn = calloc(1, sizeof *jn);
    if (jn) {
        jn->buf = malloc(capacity ? (size_t) capacity : 1);
        jn->work = malloc(FREIGHT_DECODE_WORK_BYTES);
    }
    if (!jn || !jn->buf || !jn->work) {
        if (jn) {
            free(jn->buf);
            free(jn->work);
        }
        free(jn);
        return zxvi_fail(ZXV_E_NOMEM, "freight_join_create: out of memory");
    }
    jn->magic = JOIN_MAGIC;
    jn->cap = capacity;
    freight_join_init(&jn->j, jn->buf, capacity);
    *out_join = jn;
    return zxvi_ok();
}

void zxv_freight_join_destroy(zxv_freight_join *join)
{
    if (!join) return;
    zxvi_wipe(join->buf, join->cap ? (size_t) join->cap : 1);
    zxvi_wipe(join->work, FREIGHT_DECODE_WORK_BYTES);
    free(join->buf);
    free(join->work);
    join->magic = 0;
    free(join);
}

static int32_t join_ok(const zxv_freight_join *join, const char *what)
{
    if (!join || join->magic != JOIN_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: join is NULL or not a join handle", what);
    return ZXV_OK;
}

int32_t zxv_freight_join_add(zxv_freight_join *join, const uint8_t header[64],
                             const uint8_t *packets, size_t packets_len)
{
    int32_t r = join_ok(join, "freight_join_add");
    if (r) return r;
    freight_header_t h;
    r = parse_hdr(header, &h, "freight_join_add");
    if (r) return r;
    uint32_t n;
    r = packet_count(packets, packets_len, &n, "freight_join_add");
    if (r) return r;
    int rc = freight_join_add(&join->j, &h, packets, n, join->work, FREIGHT_DECODE_WORK_BYTES);
    if (rc == FREIGHT_ERR_SPACE)
        return zxvi_fail(ZXV_E_CAPACITY,
                         "freight_join_add: the set (%u freights at k = %u) does not fit %llu bytes",
                         (unsigned) h.seq_count, (unsigned) h.k, (unsigned long long) join->cap);
    return fr_status(rc, "freight_join_add");
}

int32_t zxv_freight_join_status(const zxv_freight_join *join, uint32_t *out_received,
                                uint32_t *out_count, int32_t *out_complete, uint64_t *out_total)
{
    int32_t r = join_ok(join, "freight_join_status");
    if (r) return r;
    uint64_t total = 0;
    bool done = freight_join_complete(&join->j, &total);
    if (out_received) *out_received = join->j.received;
    if (out_count) *out_count = join->j.started ? join->j.count : 0;
    if (out_complete) *out_complete = done ? 1 : 0;
    if (out_total) *out_total = done ? total : 0;
    return zxvi_ok();
}

int32_t zxv_freight_join_result(const zxv_freight_join *join, uint8_t *buf, size_t cap,
                                size_t *out_len)
{
    int32_t r = join_ok(join, "freight_join_result");
    if (r) return r;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "freight_join_result: out_len is NULL");
    uint64_t total = 0;
    if (!freight_join_complete(&join->j, &total))
        return zxvi_fail(ZXV_E_STATE, "freight_join_result: %u of %u members received",
                         (unsigned) join->j.received, (unsigned) join->j.count);
    size_t need;
    r = bytes_room((size_t) total, buf, cap, &need, "freight_join_result");
    *out_len = need;
    if (r) return r;
    if (total) memcpy(buf, join->buf, (size_t) total);
    return zxvi_ok();
}

/* ===================================================================== */
/* Op streams                                                            */
/* ===================================================================== */

int32_t zxv_freight_ops_peek(const uint8_t *ops, size_t ops_len, uint64_t *out_total,
                             int32_t *out_has_sha)
{
    if (!ops) return zxvi_fail(ZXV_E_ARG, "freight_ops_peek: ops is NULL");
    uint64_t total;
    bool sha;
    int rc = freight_ops_peek(ops, ops_len, &total, &sha);
    if (rc) return fr_status(rc, "freight_ops_peek");
    if (out_total) *out_total = total;
    if (out_has_sha) *out_has_sha = sha ? 1 : 0;
    return zxvi_ok();
}

typedef struct {
    zxv_freight_resolve_fn fn;
    void *user;
} resolve_tramp_t;

static bool resolve_tramp(void *ctx, const uint8_t *cid, uint32_t cid_len, uint8_t *out,
                          uint64_t len)
{
    const resolve_tramp_t *t = ctx;
    char saved[ZXVI_ERR_MAX];
    zxvi_err_save(saved);
    int32_t r = t->fn(t->user, cid, cid_len, out, len);
    zxvi_err_restore(saved);
    return r == ZXV_OK;
}

int32_t zxv_freight_ops_expand(const uint8_t *ops, size_t ops_len, uint64_t limit,
                               zxv_freight_resolve_fn resolve, void *user, uint8_t *buf,
                               size_t cap, size_t *out_len)
{
    if (!ops || !out_len) return zxvi_fail(ZXV_E_ARG, "freight_ops_expand: NULL argument");
    if (limit > ZXV_FREIGHT_OPS_MAX)
        return zxvi_fail(ZXV_E_RANGE, "freight_ops_expand: limit %llu is over %llu",
                         (unsigned long long) limit, (unsigned long long) ZXV_FREIGHT_OPS_MAX);
    uint64_t total;
    bool sha;
    int rc = freight_ops_peek(ops, ops_len, &total, &sha);
    if (rc) return fr_status(rc, "freight_ops_expand");
    if (total > limit)
        return zxvi_fail(ZXV_E_TOO_LONG, "freight_ops_expand: declares %llu bytes; limit is %llu",
                         (unsigned long long) total, (unsigned long long) limit);
    size_t need;
    int32_t r = bytes_room((size_t) total, buf, cap, &need, "freight_ops_expand");
    if (r) {
        *out_len = need;
        return r;
    }
    resolve_tramp_t t = {resolve, user};
    uint8_t one = 0;
    uint64_t got = 0;
    rc = freight_ops_expand(ops, ops_len, total ? buf : &one, total, total,
                            resolve ? resolve_tramp : NULL, &t, &got);
    if (rc) return fr_status(rc, "freight_ops_expand");
    *out_len = (size_t) got;
    return zxvi_ok();
}

int32_t zxv_freight_gen_run(uint8_t gen, const uint8_t *params, size_t params_len, uint8_t *out,
                            size_t len)
{
    if ((params_len && !params) || (len && !out))
        return zxvi_fail(ZXV_E_ARG, "freight_gen_run: NULL argument");
    if (params_len > FREIGHT_GEN_PARAM_MAX)
        return zxvi_fail(ZXV_E_ARG, "freight_gen_run: %zu param bytes; maximum is 32", params_len);
    int rc = freight_gen_run(gen, params, (uint32_t) params_len, out, len);
    if (rc)
        return zxvi_fail(ZXV_E_ARG, "freight_gen_run: generator %u refuses these params",
                         (unsigned) gen);
    return zxvi_ok();
}

int32_t zxv_freight_ops_bound(uint64_t n, int32_t with_sha, uint64_t *out_bound)
{
    if (!out_bound) return zxvi_fail(ZXV_E_ARG, "freight_ops_bound: out_bound is NULL");
    if (n > ZXV_FREIGHT_OPS_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "freight_ops_bound: %llu bytes is over the maximum",
                         (unsigned long long) n);
    *out_bound = freight_ops_bound(n, with_sha != 0);
    return zxvi_ok();
}

int32_t zxv_freight_ops_encode(const uint8_t *in, size_t n, int32_t with_sha, uint8_t *buf,
                               size_t cap, size_t *out_len)
{
    if (!out_len || (n && !in)) return zxvi_fail(ZXV_E_ARG, "freight_ops_encode: NULL argument");
    if (n > ZXV_FREIGHT_OPS_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "freight_ops_encode: %zu bytes is over the maximum", n);
    uint64_t bound = freight_ops_bound(n, with_sha != 0);
    uint8_t *tmp = malloc((size_t) bound);
    uint32_t *work = malloc(FREIGHT_ENC_WORK_WORDS * sizeof(uint32_t));
    if (!tmp || !work) {
        free(tmp);
        free(work);
        return zxvi_fail(ZXV_E_NOMEM, "freight_ops_encode: out of memory");
    }
    uint64_t len = 0;
    int rc = freight_ops_encode(in, n, with_sha != 0, tmp, bound, &len, work,
                                FREIGHT_ENC_WORK_WORDS);
    int32_t r;
    if (rc) {
        r = fr_status(rc, "freight_ops_encode");
    } else {
        size_t need;
        r = bytes_room((size_t) len, buf, cap, &need, "freight_ops_encode");
        *out_len = need;
        if (r == ZXV_OK) {
            memcpy(buf, tmp, (size_t) len);
            r = zxvi_ok();
        }
    }
    zxvi_wipe(tmp, (size_t) bound);
    free(tmp);
    free(work);
    return r;
}

#define WRITER_MAGIC 0x5A584F57u /* "ZXOW" */

struct zxv_freight_ops_writer {
    uint32_t magic;
    freight_ops_writer_t w;
    uint8_t *buf;
    uint64_t cap;
};

int32_t zxv_freight_ops_writer_create(uint64_t declared_total, const uint8_t *sha256,
                                      uint64_t max_bytes, zxv_freight_ops_writer **out_writer)
{
    if (!out_writer) return zxvi_fail(ZXV_E_ARG, "freight_ops_writer_create: out_writer is NULL");
    uint64_t hdr = sha256 ? FREIGHT_OPS_HDR_SHA : FREIGHT_OPS_HDR;
    if (max_bytes < hdr)
        return zxvi_fail(ZXV_E_ARG, "freight_ops_writer_create: max_bytes %llu < header %llu",
                         (unsigned long long) max_bytes, (unsigned long long) hdr);
    if (max_bytes > ZXV_FREIGHT_OPS_MAX)
        return zxvi_fail(ZXV_E_RANGE, "freight_ops_writer_create: max_bytes is over %llu",
                         (unsigned long long) ZXV_FREIGHT_OPS_MAX);
    zxv_freight_ops_writer *w = calloc(1, sizeof *w);
    if (w) w->buf = malloc((size_t) max_bytes);
    if (!w || !w->buf) {
        free(w);
        return zxvi_fail(ZXV_E_NOMEM, "freight_ops_writer_create: out of memory");
    }
    w->magic = WRITER_MAGIC;
    w->cap = max_bytes;
    freight_ops_writer_init(&w->w, w->buf, max_bytes, declared_total, sha256);
    *out_writer = w;
    return zxvi_ok();
}

void zxv_freight_ops_writer_destroy(zxv_freight_ops_writer *w)
{
    if (!w) return;
    zxvi_wipe(w->buf, (size_t) w->cap);
    free(w->buf);
    w->magic = 0;
    free(w);
}

/* The writer's sticky error, as a status. */
static int32_t writer_status(const zxv_freight_ops_writer *w, const char *what)
{
    switch (w->w.err) {
    case FREIGHT_OK: return zxvi_ok();
    case FREIGHT_ERR_SPACE: return zxvi_fail(ZXV_E_CAPACITY, "%s: the writer's buffer is full", what);
    case FREIGHT_ERR_ARG:
        return zxvi_fail(ZXV_E_ARG, "%s: invalid op (an earlier error is sticky)", what);
    default: return fr_status(w->w.err, what);
    }
}

static int32_t writer_ok(const zxv_freight_ops_writer *w, const char *what)
{
    if (!w || w->magic != WRITER_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: writer is NULL or not a writer handle", what);
    if (w->w.err) return writer_status(w, what);
    return ZXV_OK;
}

/* Arguments the kernel would turn into a sticky error, refused up front with
 * a precise reason; the sticky error is set too, as the kernel would. */
static int32_t writer_refuse(zxv_freight_ops_writer *w, const char *what, const char *why)
{
    w->w.err = FREIGHT_ERR_ARG;
    return zxvi_fail(ZXV_E_ARG, "%s: %s", what, why);
}

static int32_t check_len(zxv_freight_ops_writer *w, uint64_t len, const char *what)
{
    if (len == 0) return writer_refuse(w, what, "an op must produce at least one byte");
    if (len > w->w.declared - w->w.produced)
        return writer_refuse(w, what, "the op passes the declared total");
    return ZXV_OK;
}

int32_t zxv_freight_ops_put_literal(zxv_freight_ops_writer *w, const uint8_t *bytes, size_t n)
{
    const char *what = "freight_ops_put_literal";
    int32_t r = writer_ok(w, what);
    if (r) return r;
    if ((r = check_len(w, n, what))) return r;
    if (!bytes) return writer_refuse(w, what, "bytes is NULL");
    freight_ops_put_literal(&w->w, bytes, n);
    return writer_status(w, what);
}

int32_t zxv_freight_ops_put_copy(zxv_freight_ops_writer *w, uint64_t distance, uint64_t len)
{
    const char *what = "freight_ops_put_copy";
    int32_t r = writer_ok(w, what);
    if (r) return r;
    if ((r = check_len(w, len, what))) return r;
    if (distance == 0 || distance > w->w.produced)
        return writer_refuse(w, what, "distance must be 1..bytes produced so far");
    freight_ops_put_copy(&w->w, distance, len);
    return writer_status(w, what);
}

int32_t zxv_freight_ops_put_cidref(zxv_freight_ops_writer *w, const uint8_t *cid, size_t cid_len,
                                   uint64_t len)
{
    const char *what = "freight_ops_put_cidref";
    int32_t r = writer_ok(w, what);
    if (r) return r;
    if ((r = check_len(w, len, what))) return r;
    if (!cid || cid_len == 0 || cid_len > FREIGHT_CID_MAX)
        return writer_refuse(w, what, "cid must be 1..128 bytes");
    freight_ops_put_cidref(&w->w, cid, (uint32_t) cid_len, len);
    return writer_status(w, what);
}

int32_t zxv_freight_ops_put_seed(zxv_freight_ops_writer *w, uint8_t gen, const uint8_t *params,
                                 size_t params_len, uint64_t len)
{
    const char *what = "freight_ops_put_seed";
    int32_t r = writer_ok(w, what);
    if (r) return r;
    if ((r = check_len(w, len, what))) return r;
    if ((params_len && !params) || params_len > FREIGHT_GEN_PARAM_MAX)
        return writer_refuse(w, what, "params must be 0..32 bytes");
    /* The kernel writer does not check the generator; expansion would. A
     * zero-length run checks the generator id and its params. */
    if (freight_gen_run(gen, params, (uint32_t) params_len, NULL, 0) != FREIGHT_OK)
        return writer_refuse(w, what, "unknown generator or params it refuses");
    freight_ops_put_seed(&w->w, gen, params, (uint32_t) params_len, len);
    return writer_status(w, what);
}

int32_t zxv_freight_ops_writer_finish(const zxv_freight_ops_writer *w, uint8_t *buf, size_t cap,
                                      size_t *out_len)
{
    const char *what = "freight_ops_writer_finish";
    int32_t r = writer_ok(w, what);
    if (r) return r;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "%s: out_len is NULL", what);
    if (w->w.produced != w->w.declared)
        return zxvi_fail(ZXV_E_STATE, "%s: ops produce %llu of %llu declared bytes", what,
                         (unsigned long long) w->w.produced, (unsigned long long) w->w.declared);
    size_t need;
    r = bytes_room((size_t) w->w.len, buf, cap, &need, what);
    *out_len = need;
    if (r) return r;
    memcpy(buf, w->buf, (size_t) w->w.len);
    return zxvi_ok();
}

/* ZXV_FEAT2_FREIGHT once this group is live (rule R9). */
uint32_t zxvi_feat2_freight(void)
{
    return ZXV_FEAT2_FREIGHT;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_rpc_modules.c — freight.*, harmonic.*, stream.*, ehop.* and web4.*
 * over zxvd's socket (see zxvd_rpc_modules.h X1-X4).
 *
 *   freight.pack / freight.unpack     kernel/src/freight: one UBH-168 freight
 *                                     (168 x 21-byte packets, any k rebuild)
 *   harmonic.encode / harmonic.decode kernel/src/harmonic: the 21-octet wire
 *                                     frame and its correlation resolver
 *   stream.plan / stream.segment      kernel/src/stream: the VOD cut rule and
 *                                     a segment's CID and freight plan
 *   ehop.bands / ehop.seal / ehop.open kernel/src/ehop: the band plan and one
 *                                     AEAD frame on a keyed channel
 *   web4.cid / web4.agent_card / web4.card_verify
 *                                     kernel/src/web4: CIDv1 forms and the
 *                                     ML-DSA-65 agent card (A1-A3)
 *
 * Single-threaded like the rest of zxvd: the large buffers are static. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "zxvd_rpc_modules.h"
#include "web4_util.h"
#include "web4_web3.h"
#include "web4_agent.h"
#include "freight.h"
#include "zt_harmonic_wire.h"
#include "zt_endian_mux.h" /* ZT_MUX_THRESHOLD_Q16, the default threshold */
#include "stream_seg.h"
#include "ehop.h"

#define E_PARAMS (-32602)

/* the most a request line can carry in hex (zxvd_rpc.h T3) */
#define HEX_MAX (ZXVD_RPC_MAX_LINE / 2)

typedef struct {
    const char *js;
    const w4_jtok_t *t;
    int32_t n, params;
    zxvd_buf_t *res;
    int *err;
    char *msg;
    size_t msg_cap;
} mc_t;

static void fail(mc_t *c, int code, const char *fmt, ...)
{
    if (*c->err) return;
    *c->err = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->msg, c->msg_cap, fmt, ap);
    va_end(ap);
}

static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) *v++ = 0;
}

/* ===================== parameters ===================== */

static int32_t param(mc_t *c, const char *key)
{
    if (c->params < 0) return W4_ERR_NOTFOUND;
    return w4_json_get(c->js, c->t, c->n, c->params, key);
}

/* A JSON integer or a decimal string ("123", no sign), as zxvd_rpc.h M3. */
static bool tok_u64(mc_t *c, int32_t i, uint64_t *v)
{
    if (c->t[i].type == W4_J_NUM) return w4_json_u64(c->js, &c->t[i], v) == W4_OK;
    if (c->t[i].type != W4_J_STR) return false;
    const char *p = c->js + c->t[i].start, *e = c->js + c->t[i].end;
    uint64_t x = 0;
    if (p == e || e - p > 20) return false;
    for (; p < e; p++) {
        if (*p < '0' || *p > '9') return false;
        uint64_t d = (uint64_t) (*p - '0');
        if (x > (UINT64_MAX - d) / 10u) return false;
        x = x * 10u + d;
    }
    *v = x;
    return true;
}

static bool p_u64(mc_t *c, const char *key, uint64_t *v, uint64_t max, bool required, uint64_t dflt)
{
    int32_t i = param(c, key);
    if (i < 0) {
        if (required) fail(c, E_PARAMS, "missing integer parameter \"%s\"", key);
        *v = dflt;
        return !required;
    }
    if (!tok_u64(c, i, v) || *v > max) {
        fail(c, E_PARAMS, "\"%s\" must be an integer from 0 to %llu", key,
             (unsigned long long) max);
        return false;
    }
    return true;
}

static bool p_u32(mc_t *c, const char *key, uint32_t *v, uint32_t max, bool required, uint32_t dflt)
{
    uint64_t x;
    if (!p_u64(c, key, &x, max, required, dflt)) return false;
    *v = (uint32_t) x;
    return true;
}

static bool p_bool(mc_t *c, const char *key, bool *v, bool dflt)
{
    int32_t i = param(c, key);
    *v = dflt;
    if (i < 0) return true;
    if (w4_json_bool(&c->t[i], v) != W4_OK) {
        fail(c, E_PARAMS, "\"%s\" must be true or false", key);
        return false;
    }
    return true;
}

static bool p_str(mc_t *c, const char *key, char *out, uint32_t cap)
{
    int32_t i = param(c, key);
    if (i < 0) {
        fail(c, E_PARAMS, "missing string parameter \"%s\"", key);
        return false;
    }
    int32_t got = c->t[i].type == W4_J_STR ? w4_json_str(c->js, &c->t[i], out, cap) : -1;
    if (got < 0 || (size_t) got != strlen(out)) {
        fail(c, E_PARAMS, "\"%s\" must be a string of at most %u bytes", key, cap - 1);
        return false;
    }
    return true;
}

/* Hex bytes: at most cap bytes, or exactly `exact` when exact != 0. */
static bool p_hex(mc_t *c, const char *key, uint8_t *out, uint32_t cap, uint32_t exact,
                  uint32_t *len)
{
    int32_t i = param(c, key);
    *len = 0;
    if (i < 0) {
        fail(c, E_PARAMS, "missing hex parameter \"%s\"", key);
        return false;
    }
    uint32_t l = c->t[i].type == W4_J_STR ? c->t[i].end - c->t[i].start : 1;
    int32_t got = c->t[i].type == W4_J_STR ? w4_hex_decode(c->js + c->t[i].start, l, out, cap) : -1;
    if (got < 0 || (exact && (uint32_t) got != exact)) {
        if (exact)
            fail(c, E_PARAMS, "\"%s\" must be %u bytes in hex", key, exact);
        else
            fail(c, E_PARAMS, "\"%s\" must be hex of at most %u bytes", key, cap);
        return false;
    }
    *len = (uint32_t) got;
    return true;
}

/* ===================== results ===================== */

static void put_hex(zxvd_buf_t *b, const uint8_t *p, uint32_t n)
{
    char h[513];
    zxvd_buf_raw(b, "\"", 1);
    while (n) {
        uint32_t k = n > 256 ? 256 : n;
        w4_hex_encode(p, k, h, sizeof h);
        zxvd_buf_raw(b, h, 2u * k);
        p += k;
        n -= k;
    }
    zxvd_buf_raw(b, "\"", 1);
}

static void put_key_hex(zxvd_buf_t *b, const char *key, const uint8_t *p, uint32_t n)
{
    zxvd_buf_printf(b, ",\"%s\":", key);
    put_hex(b, p, n);
}

/* ===================== freight ===================== */

static const char *freight_why(int rc)
{
    switch (rc) {
    case FREIGHT_ERR_ARG:
        return "bad argument";
    case FREIGHT_ERR_SPACE:
        return "buffer too small";
    case FREIGHT_ERR_TOO_FEW:
        return "fewer than k distinct rows arrived";
    case FREIGHT_ERR_HASH:
        return "the payload does not match its SHA-256";
    case FREIGHT_ERR_FORMAT:
        return "malformed header";
    case FREIGHT_ERR_SINGULAR:
        return "singular row set";
    default:
        return "refused";
    }
}

static uint8_t F_DATA[FREIGHT_MAX_PAYLOAD], F_PKT[HEX_MAX], F_WORK[FREIGHT_DECODE_WORK_BYTES];

static void m_freight_pack(mc_t *c)
{
    uint32_t len, k;
    uint64_t id;
    if (!p_hex(c, "data_hex", F_DATA, sizeof F_DATA, 0, &len)) return;
    /* default k: the fewest data rows, so the most parity (168 - k rows may be lost) */
    if (!p_u32(c, "k", &k, FREIGHT_ROWS, false,
               len ? (len + FREIGHT_ROW_PAYLOAD - 1) / FREIGHT_ROW_PAYLOAD : 1))
        return;
    if (k == 0 || len > FREIGHT_CAPACITY(k))
        return fail(c, E_PARAMS, "k = %u cannot carry %u bytes (1 <= k <= 168, 20 bytes per row)",
                    (unsigned) k, (unsigned) len);
    uint8_t sha[32];
    w4_sha256(F_DATA, len, sha);
    if (!p_u64(c, "freight_id", &id, UINT64_MAX, false, w4_le64_get(sha))) return;
    freight_header_t h;
    uint8_t hb[FREIGHT_HEADER_BYTES];
    int rc = freight_header_init(&h, id, (uint8_t) k, 0, 0, F_DATA, len);
    if (rc == FREIGHT_OK) rc = freight_encode(&h, F_DATA, F_PKT);
    if (rc == FREIGHT_OK) rc = freight_header_serialize(&h, hb);
    if (rc != FREIGHT_OK) return fail(c, ZXVD_E_MODULE, "freight: %s", freight_why(rc));
    zxvd_buf_printf(c->res, "{\"freight_id\":\"%llu\",\"k\":%u,\"payload_len\":%u,\"rows\":%u",
                    (unsigned long long) id, (unsigned) k, (unsigned) len, FREIGHT_ROWS);
    put_key_hex(c->res, "header_hex", hb, sizeof hb);
    put_key_hex(c->res, "packets_hex", F_PKT, FREIGHT_BYTES);
    zxvd_buf_raw(c->res, "}", 1);
}

static void m_freight_unpack(mc_t *c)
{
    uint8_t hb[FREIGHT_HEADER_BYTES];
    uint32_t hl, pl;
    if (!p_hex(c, "header_hex", hb, sizeof hb, sizeof hb, &hl) ||
        !p_hex(c, "packets_hex", F_PKT, sizeof F_PKT, 0, &pl))
        return;
    if (pl == 0 || pl % FREIGHT_PACKET_BYTES)
        return fail(c, E_PARAMS, "\"packets_hex\" must be whole 21-byte packets");
    freight_header_t h;
    int rc = freight_header_parse(hb, &h);
    if (rc == FREIGHT_OK)
        rc = freight_decode(&h, F_PKT, pl / FREIGHT_PACKET_BYTES, F_DATA, sizeof F_DATA, F_WORK,
                            sizeof F_WORK);
    if (rc != FREIGHT_OK) return fail(c, ZXVD_E_MODULE, "freight: %s", freight_why(rc));
    zxvd_buf_printf(c->res,
                    "{\"freight_id\":\"%llu\",\"k\":%u,\"flags\":%u,\"payload_len\":%u,"
                    "\"packets_received\":%u",
                    (unsigned long long) h.freight_id, (unsigned) h.k, (unsigned) h.flags,
                    (unsigned) h.payload_len, (unsigned) (pl / FREIGHT_PACKET_BYTES));
    put_key_hex(c->res, "data_hex", F_DATA, h.payload_len);
    zxvd_buf_raw(c->res, "}", 1);
}

/* ===================== harmonic ===================== */

/* `n` 32-bit words: integers from -2^31 to 2^32 - 1 (Q16.16 or raw). */
static bool p_words(mc_t *c, const char *key, uint32_t *w, uint32_t n)
{
    int32_t a = param(c, key);
    if (a < 0 || c->t[a].type != W4_J_ARR || w4_json_at(c->t, c->n, a, n) >= 0) goto bad;
    for (uint32_t k = 0; k < n; k++) {
        int32_t i = w4_json_at(c->t, c->n, a, k);
        int64_t x;
        if (i < 0 || c->t[i].type != W4_J_NUM || w4_json_i64(c->js, &c->t[i], &x) != W4_OK ||
            x < INT32_MIN || x > (int64_t) UINT32_MAX)
            goto bad;
        w[k] = (uint32_t) x;
    }
    return true;
bad:
    fail(c, E_PARAMS, "\"%s\" must be an array of %u integers from -2147483648 to 4294967295", key,
         (unsigned) n);
    return false;
}

static void m_harmonic_encode(mc_t *c)
{
    uint32_t trunk, shell, sp[3], sm[2];
    bool sync;
    if (!p_u32(c, "trunk", &trunk, ZT_NUM_TRUNK_LINES - 1, true, 0) ||
        !p_u32(c, "shell", &shell, ZT_WIRE_SHELLS - 1, false, 0) ||
        !p_bool(c, "sync", &sync, false) || !p_words(c, "s_plus", sp, 3) ||
        !p_words(c, "s_minus", sm, 2))
        return;
    uint8_t tag = zt_wire_make_tag((uint8_t) trunk, (uint8_t) shell, sync), oct[21];
    zt_ubh168_wire_frame_t f;
    zt_wire_pack_tagged(tag, sp, sm, &f);
    zt_wire_to_octets(&f, oct);
    zxvd_buf_printf(c->res, "{\"tag\":%u,\"trunk\":%u,\"trunk_hz\":%u", (unsigned) tag,
                    (unsigned) trunk, (unsigned) zt_wire_trunk_hz((uint8_t) trunk));
    put_key_hex(c->res, "frame_hex", oct, sizeof oct);
    zxvd_buf_raw(c->res, "}", 1);
}

static void m_harmonic_decode(mc_t *c)
{
    uint8_t oct[21];
    uint32_t l, thr;
    if (!p_hex(c, "frame_hex", oct, sizeof oct, sizeof oct, &l) ||
        !p_u32(c, "threshold_q16", &thr, INT32_MAX, false, ZT_MUX_THRESHOLD_Q16))
        return;
    zt_ubh168_wire_frame_t f;
    zt_unpacked_rails_t r;
    zt_wire_from_octets(oct, &f);
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(&f, &r);
    zt_truth_state_t s = zt_wire_resolve_rails(&r, (int32_t) thr);
    zxvd_buf_printf(c->res,
                    "{\"tag\":%u,\"trunk\":%u,\"shell\":%u,\"sync\":%s,\"axis_trunk\":%s,"
                    "\"trunk_hz\":%u,\"s_plus\":[%d,%d,%d],\"s_minus\":[%d,%d],\"idle\":%s,"
                    "\"correlation_q16\":%d,\"truth\":\"%s\"}",
                    (unsigned) r.tag, (unsigned) r.trunk_id, (unsigned) r.shell,
                    r.sync ? "true" : "false", r.is_axis_trunk ? "true" : "false",
                    (unsigned) zt_wire_trunk_hz(r.trunk_id), (int) (int32_t) r.s_plus[0],
                    (int) (int32_t) r.s_plus[1], (int) (int32_t) r.s_plus[2],
                    (int) (int32_t) r.s_minus[0], (int) (int32_t) r.s_minus[1],
                    zt_wire_is_idle(&r) ? "true" : "false", (int) zt_wire_correlation_q16(&r),
                    zt_truth_name(s));
}

/* ===================== stream ===================== */

/* each unit is 4 tokens ([len, pts, key]); the request holds at most 512 */
#define STREAM_PLAN_MAX 120u

static stream_unit_t S_UNITS[STREAM_PLAN_MAX];
static stream_vod_seg_t S_SEGS[STREAM_PLAN_MAX];

static void m_stream_plan(mc_t *c)
{
    uint32_t target, max, maxb, tail, n = 0;
    int32_t a = param(c, "units");
    if (a < 0 || c->t[a].type != W4_J_ARR)
        return fail(c, E_PARAMS, "\"units\" must be an array of [len, pts_us, keyframe]");
    uint64_t off = 0;
    for (int32_t u; (u = w4_json_at(c->t, c->n, a, n)) >= 0; n++) {
        if (n == STREAM_PLAN_MAX)
            return fail(c, E_PARAMS, "at most %u units per request", STREAM_PLAN_MAX);
        int32_t il = w4_json_at(c->t, c->n, u, 0), ip = w4_json_at(c->t, c->n, u, 1),
                ik = w4_json_at(c->t, c->n, u, 2);
        uint64_t len, pts;
        bool key;
        if (c->t[u].type != W4_J_ARR || il < 0 || ip < 0 || ik < 0 ||
            w4_json_at(c->t, c->n, u, 3) >= 0 || !tok_u64(c, il, &len) || len > UINT32_MAX ||
            !tok_u64(c, ip, &pts) || w4_json_bool(&c->t[ik], &key) != W4_OK)
            return fail(c, E_PARAMS, "unit %u must be [len, pts_us, keyframe]", (unsigned) n);
        if (off + len > UINT32_MAX) return fail(c, E_PARAMS, "the units add up to more than 4 GiB");
        S_UNITS[n] = (stream_unit_t){(uint32_t) off, (uint32_t) len, pts, key};
        off += len;
    }
    if (!p_u32(c, "target_us", &target, UINT32_MAX, true, 0) ||
        !p_u32(c, "max_us", &max, UINT32_MAX, true, 0) ||
        !p_u32(c, "max_bytes", &maxb, UINT32_MAX, false, STREAM_SEG_MAX_BYTES) ||
        !p_u32(c, "tail_us", &tail, UINT32_MAX, false, 0))
        return;
    int ns = stream_vod_plan(S_UNITS, n, target, max, maxb, tail, S_SEGS, STREAM_PLAN_MAX);
    if (ns == STREAM_ERR_ARG)
        return fail(c, E_PARAMS, "need target_us > 0, max_us >= target_us and max_bytes > 0");
    if (ns == STREAM_ERR_FORMAT) return fail(c, E_PARAMS, "pts_us must not go backwards");
    if (ns == STREAM_ERR_SPACE) return fail(c, ZXVD_E_MODULE, "stream: a unit is over max_bytes");
    if (ns < 0) return fail(c, ZXVD_E_MODULE, "stream: refused (%d)", ns);
    zxvd_buf_raw(c->res, "{\"segments\":[", 13);
    for (int i = 0; i < ns; i++) {
        const stream_vod_seg_t *s = &S_SEGS[i];
        zxvd_buf_printf(c->res,
                        "%s{\"first_unit\":%u,\"nunits\":%u,\"offset\":%u,\"len\":%u,"
                        "\"t_start_us\":\"%llu\",\"dur_us\":%u,\"key_start\":%s}",
                        i ? "," : "", (unsigned) s->first_unit, (unsigned) s->nunits,
                        (unsigned) s->offset, (unsigned) s->len, (unsigned long long) s->t_start_us,
                        (unsigned) s->dur_us, s->key_start ? "true" : "false");
    }
    zxvd_buf_raw(c->res, "]}", 2);
}

static uint8_t S_DATA[HEX_MAX];
static stream_manifest_t S_MAN;

static void m_stream_segment(mc_t *c)
{
    uint32_t len, k, seq;
    uint64_t sid;
    if (!p_hex(c, "data_hex", S_DATA, sizeof S_DATA, 0, &len) ||
        !p_u32(c, "k", &k, STREAM_K_MAX, false, STREAM_K_MAX) ||
        !p_u64(c, "stream_id", &sid, UINT64_MAX, false, 0) ||
        !p_u32(c, "seq", &seq, UINT32_MAX, false, 0))
        return;
    if (k == 0) return fail(c, E_PARAMS, "\"k\" must be 1 to 168");
    if (stream_seg_nfreights(len, (uint8_t) k) == 0)
        return fail(c, E_PARAMS, "k = %u carries at most %u bytes (%u freights)", (unsigned) k,
                    (unsigned) (STREAM_SEG_MAX_FREIGHTS * FREIGHT_CAPACITY(k)),
                    STREAM_SEG_MAX_FREIGHTS);
    int rc = stream_manifest_build(&S_MAN, sid, seq, 0, 0, 0, (uint8_t) k, NULL, S_DATA, len);
    char cid[IPFSN_CID_STR_MAX];
    if (rc != STREAM_OK || ipfsn_cid_to_string(&S_MAN.cid, cid, sizeof cid) < 0)
        return fail(c, ZXVD_E_MODULE, "stream: cannot describe this segment (%d)", rc);
    zxvd_buf_raw(c->res, "{\"cid\":", 7);
    zxvd_buf_json_str(c->res, cid);
    zxvd_buf_printf(c->res,
                    ",\"stream_id\":\"%llu\",\"seq\":%u,\"k\":%u,\"data_len\":%u,"
                    "\"nfreights\":%u,\"freight_id\":\"%llu\",\"freight_sha256\":[",
                    (unsigned long long) sid, (unsigned) seq, (unsigned) k, (unsigned) len,
                    (unsigned) S_MAN.nfreights, (unsigned long long) S_MAN.freight_id);
    for (uint32_t f = 0; f < S_MAN.nfreights; f++) {
        if (f) zxvd_buf_raw(c->res, ",", 1);
        put_hex(c->res, S_MAN.freight_sha256[f], STREAM_SHA_BYTES);
    }
    zxvd_buf_raw(c->res, "]}", 2);
}

/* ===================== ehop ===================== */

static const char *ehop_why(int rc)
{
    switch (rc) {
    case EHOP_EARG:
        return "invalid argument";
    case EHOP_ESIZE:
        return "too long for the band or the frame";
    case EHOP_EFORMAT:
        return "malformed frame";
    case EHOP_ECHANNEL:
        return "the frame is not on this channel";
    case EHOP_EEPOCH:
        return "the frame is for another epoch";
    case EHOP_EAUTH:
        return "the AEAD tag did not verify";
    case EHOP_EREPLAY:
        return "replayed sequence number";
    case EHOP_ESEQ:
        return "sequence space exhausted or moved backwards";
    case EHOP_EBAND:
        return "configuration outside its band";
    default:
        return "refused";
    }
}

static void m_ehop_bands(mc_t *c)
{
    zxvd_buf_raw(c->res, "{\"bands\":[", 10);
    for (uint32_t i = 0; i < EHOP_BAND_COUNT; i++) {
        const ehop_band_info_t *b = ehop_band_info(i);
        zxvd_buf_printf(c->res,
                        "%s{\"band\":%u,\"name\":\"%s\",\"fm_min\":%u,\"fm_max\":%u,"
                        "\"am_mask\":%u,\"pm_modes\":%u,\"priority\":%u,\"max_frame\":%u,"
                        "\"latency_ms\":%u}",
                        i ? "," : "", (unsigned) i, b->name, (unsigned) b->fm_min,
                        (unsigned) b->fm_max, (unsigned) b->am_mask, (unsigned) b->pm_modes,
                        (unsigned) b->priority, (unsigned) b->max_frame, (unsigned) b->latency_ms);
    }
    zxvd_buf_raw(c->res, "]}", 2);
}

/* The channel: "band" by name; every other field defaults to the band's
 * widest setting at its lowest flip rate (32-bit words, static phase). */
static bool p_channel(mc_t *c, uint8_t key[EHOP_KEY_BYTES], ehop_cfg_t *cfg, uint32_t *chan,
                      uint32_t *epoch)
{
    char band[16];
    uint32_t kl, v[7];
    if (!p_hex(c, "key_hex", key, EHOP_KEY_BYTES, EHOP_KEY_BYTES, &kl) ||
        !p_str(c, "band", band, sizeof band) || !p_u32(c, "chan_id", chan, UINT32_MAX, true, 0) ||
        !p_u32(c, "epoch", epoch, UINT32_MAX, true, 0))
        return false;
    uint32_t bi = 0;
    while (bi < EHOP_BAND_COUNT && strcmp(ehop_band_info(bi)->name, band)) bi++;
    const ehop_band_info_t *b = ehop_band_info(bi); /* NULL past the plan */
    if (!b) {
        fail(c, E_PARAMS, "\"band\" must be control, settle, media, chat or bulk");
        return false;
    }
    if (!p_u32(c, "width", &v[0], 8, false, 4) ||
        !p_u32(c, "am_mask", &v[1], 255, false, b->am_mask) ||
        !p_u32(c, "pm_mode", &v[2], 255, false, EHOP_PM_STATIC) ||
        !p_u32(c, "fm_rate", &v[3], EHOP_RATE_MAX, false, b->fm_min) ||
        !p_u32(c, "fm_hop", &v[4], 65535, false, 0) ||
        !p_u32(c, "pm_phase", &v[5], 65535, false, 0) ||
        !p_u32(c, "pm_step", &v[6], 65535, false, 0))
        return false;
    *cfg = (ehop_cfg_t){(uint8_t) v[0],  (uint8_t) bi,    (uint8_t) v[1],  (uint8_t) v[2],
                        (uint16_t) v[3], (uint16_t) v[4], (uint16_t) v[5], (uint16_t) v[6]};
    if (ehop_cfg_check(cfg) != EHOP_OK) {
        fail(c, E_PARAMS,
             "channel outside band %s: width 2, 4 or 8; fm_rate %u..%u; am_mask a non-empty "
             "subset of %u; fm_hop, pm_phase and pm_step below fm_rate",
             b->name, (unsigned) b->fm_min, (unsigned) b->fm_max, (unsigned) b->am_mask);
        return false;
    }
    return true;
}

static ehop_channel_t E_CH;
static uint8_t E_PT[EHOP_MAX_PAYLOAD], E_FR[EHOP_MAX_FRAME];

static void m_ehop_seal(mc_t *c)
{
    uint8_t key[EHOP_KEY_BYTES];
    ehop_cfg_t cfg;
    uint32_t chan, epoch, sender, pl, fl = 0;
    uint64_t seq;
    if (p_channel(c, key, &cfg, &chan, &epoch) &&
        p_u32(c, "sender", &sender, UINT32_MAX, true, 0) &&
        p_u64(c, "seq", &seq, UINT64_MAX, true, 0) &&
        p_hex(c, "plaintext_hex", E_PT, sizeof E_PT, 0, &pl)) {
        int rc = ehop_channel_init(&E_CH, key, chan, epoch, &cfg, sender);
        if (rc == EHOP_OK) rc = ehop_channel_set_seq(&E_CH, seq);
        if (rc == EHOP_OK) rc = ehop_seal(&E_CH, E_PT, pl, E_FR, sizeof E_FR, &fl);
        if (rc != EHOP_OK) {
            fail(c, ZXVD_E_MODULE, "ehop: %s", ehop_why(rc));
        } else {
            zxvd_buf_printf(c->res,
                            "{\"band\":\"%s\",\"priority\":%u,\"sender\":%u,\"seq\":\"%llu\","
                            "\"frame_len\":%u",
                            ehop_band_info(cfg.band)->name,
                            (unsigned) ehop_band_info(cfg.band)->priority, (unsigned) sender,
                            (unsigned long long) seq, (unsigned) fl);
            put_key_hex(c->res, "frame_hex", E_FR, fl);
            zxvd_buf_raw(c->res, "}", 1);
        }
    }
    ehop_channel_wipe(&E_CH);
    wipe(key, sizeof key);
    wipe(E_PT, sizeof E_PT);
}

static void m_ehop_open(mc_t *c)
{
    uint8_t key[EHOP_KEY_BYTES];
    ehop_cfg_t cfg;
    uint32_t chan, epoch, fl, pl = 0;
    if (p_channel(c, key, &cfg, &chan, &epoch) &&
        p_hex(c, "frame_hex", E_FR, sizeof E_FR, 0, &fl)) {
        int rc = ehop_channel_init(&E_CH, key, chan, epoch, &cfg, 0);
        if (rc == EHOP_OK) rc = ehop_open(&E_CH, E_FR, fl, E_PT, sizeof E_PT, &pl);
        if (rc != EHOP_OK) {
            fail(c, ZXVD_E_MODULE, "ehop: %s", ehop_why(rc));
        } else { /* the clear header (ehop.h): le32 sender at 8, le64 seq at 12 */
            zxvd_buf_printf(c->res, "{\"band\":\"%s\",\"sender\":%u,\"seq\":\"%llu\"",
                            ehop_band_info(cfg.band)->name, (unsigned) w4_le32_get(E_FR + 8),
                            (unsigned long long) w4_le64_get(E_FR + 12));
            put_key_hex(c->res, "plaintext_hex", E_PT, pl);
            zxvd_buf_raw(c->res, "}", 1);
        }
    }
    ehop_channel_wipe(&E_CH);
    wipe(key, sizeof key);
    wipe(E_PT, sizeof E_PT);
}

/* ===================== web4 ===================== */

static uint8_t W_DATA[HEX_MAX];

static void m_web4_cid(mc_t *c)
{
    ipfsn_cid_t cid;
    bool by_data = param(c, "data_hex") >= 0;
    if (by_data == (param(c, "cid") >= 0))
        return fail(c, E_PARAMS, "give exactly one of \"data_hex\" and \"cid\"");
    if (by_data) {
        uint32_t len;
        if (!p_hex(c, "data_hex", W_DATA, sizeof W_DATA, 0, &len)) return;
        if (w4_cid_of(W_DATA, len, &cid) != W4_OK)
            return fail(c, ZXVD_E_MODULE, "web4: cannot hash this content");
    } else {
        char s[IPFSN_CID_STR_MAX + 8];
        if (!p_str(c, "cid", s, sizeof s)) return;
        bool uri = !strncmp(s, "ipfs://", 7);
        if ((uri ? w4_cid_uri_parse(s, (uint32_t) strlen(s), &cid)
                 : ipfsn_cid_parse(s, (uint32_t) strlen(s), &cid)) != 0)
            return fail(c, E_PARAMS, "\"cid\" is not a CID or an ipfs:// URI");
    }
    char str[IPFSN_CID_STR_MAX], uri[IPFSN_CID_STR_MAX + 8];
    uint8_t d32[32];
    uint32_t codec = 0;
    if (ipfsn_cid_to_string(&cid, str, sizeof str) < 0 || w4_cid_uri(&cid, uri, sizeof uri) < 0)
        return fail(c, ZXVD_E_MODULE, "web4: this CID has no string form here");
    zxvd_buf_raw(c->res, "{\"cid\":", 7);
    zxvd_buf_json_str(c->res, str);
    zxvd_buf_raw(c->res, ",\"uri\":", 7);
    zxvd_buf_json_str(c->res, uri);
    zxvd_buf_printf(c->res, ",\"version\":%u,\"codec\":%u", (unsigned) cid.version,
                    (unsigned) cid.codec);
    if (w4_cid_to_bytes32(&cid, d32, &codec) == W4_OK)
        put_key_hex(c->res, "bytes32_hex", d32, sizeof d32);
    else
        zxvd_buf_raw(c->res, ",\"bytes32_hex\":null", 19);
    zxvd_buf_raw(c->res, "}", 1);
}

static w4_agent_t W_AGENT;
static w4_card_t W_CARD;

static void put_card(zxvd_buf_t *b, const w4_card_t *k)
{
    zxvd_buf_raw(b, "{\"name\":", 8);
    zxvd_buf_json_str(b, k->name);
    zxvd_buf_printf(b, ",\"bound_ms\":\"%llu\",\"self_bound\":true",
                    (unsigned long long) k->bound_ms);
    put_key_hex(b, "agent_id", k->agent_id, W4_ID_LEN);
    put_key_hex(b, "peer_id", k->peer_id, W4_ID_LEN);
    put_key_hex(b, "pk_hex", k->pk, W4_PK_LEN);
    put_key_hex(b, "bind_sig_hex", k->bind_sig, W4_SIG_LEN);
}

static void m_web4_agent_card(mc_t *c)
{
    uint8_t seed[32];
    char name[W4_NAME_MAX];
    uint32_t sl;
    uint64_t ms;
    if (p_hex(c, "seed_hex", seed, sizeof seed, sizeof seed, &sl) &&
        p_str(c, "name", name, sizeof name) && p_u64(c, "bound_ms", &ms, UINT64_MAX, false, 0)) {
        int rc = w4_agent_create(&W_AGENT, seed, name, NULL, NULL, ms, NULL);
        if (rc == W4_ERR_ARG) {
            fail(c, E_PARAMS, "\"name\" must be 1 to 31 of a-z 0-9 - _ .");
        } else if (rc != W4_OK) {
            fail(c, ZXVD_E_MODULE, "web4: the new card does not verify (%d)", rc);
        } else {
            put_card(c->res, &W_AGENT.card);
            zxvd_buf_raw(c->res, "}", 1);
        }
    }
    wipe(&W_AGENT, sizeof W_AGENT); /* X4: the secret key never outlives the call */
    wipe(seed, sizeof seed);
}

static void m_web4_card_verify(mc_t *c)
{
    uint32_t pl, gl, il;
    uint64_t ms;
    uint8_t claimed[W4_ID_LEN];
    w4_card_t *k = &W_CARD;
    memset(k, 0, sizeof *k);
    if (!p_str(c, "name", k->name, sizeof k->name) ||
        !p_u64(c, "bound_ms", &ms, UINT64_MAX, true, 0) ||
        !p_hex(c, "pk_hex", k->pk, W4_PK_LEN, W4_PK_LEN, &pl) ||
        !p_hex(c, "bind_sig_hex", k->bind_sig, W4_SIG_LEN, W4_SIG_LEN, &gl))
        return;
    bool claim = param(c, "agent_id") >= 0;
    if (claim && !p_hex(c, "agent_id", claimed, sizeof claimed, sizeof claimed, &il)) return;
    /* A1: the id is the key's hash; a self-bound card's peer is itself */
    k->bound_ms = ms;
    k->self_bound = true;
    w4_key_id(k->pk, k->agent_id);
    memcpy(k->peer_id, k->agent_id, W4_ID_LEN);
    int rc = w4_card_verify(k);
    if (rc == W4_ERR_ARG) return fail(c, E_PARAMS, "\"name\" must be 1 to 31 of a-z 0-9 - _ .");
    const char *why = rc == W4_OK ? NULL : "the binding signature does not verify";
    if (claim && memcmp(claimed, k->agent_id, W4_ID_LEN)) why = "agent_id is not SHA3-256 of pk";
    zxvd_buf_printf(c->res, "{\"valid\":%s", why ? "false" : "true");
    put_key_hex(c->res, "agent_id", k->agent_id, W4_ID_LEN);
    if (why) {
        zxvd_buf_raw(c->res, ",\"reason\":", 10);
        zxvd_buf_json_str(c->res, why);
    }
    zxvd_buf_raw(c->res, "}", 1);
}

/* ===================== the table ===================== */

typedef struct {
    const char *name;
    void (*fn)(mc_t *);
} mmethod_t;

static const mmethod_t MM[] = {
    {"freight.pack", m_freight_pack},
    {"freight.unpack", m_freight_unpack},
    {"harmonic.encode", m_harmonic_encode},
    {"harmonic.decode", m_harmonic_decode},
    {"stream.plan", m_stream_plan},
    {"stream.segment", m_stream_segment},
    {"ehop.bands", m_ehop_bands},
    {"ehop.seal", m_ehop_seal},
    {"ehop.open", m_ehop_open},
    {"web4.cid", m_web4_cid},
    {"web4.agent_card", m_web4_agent_card},
    {"web4.card_verify", m_web4_card_verify},
};
#define N_MM ((int) (sizeof MM / sizeof MM[0]))

int zxvd_mod_find(const char *js, const w4_jtok_t *name)
{
    for (int i = 0; i < N_MM; i++)
        if (w4_json_str_eq(js, name, MM[i].name)) return i;
    return -1;
}

void zxvd_mod_names(zxvd_buf_t *out)
{
    for (int i = 0; i < N_MM; i++) {
        zxvd_buf_raw(out, ",", 1);
        zxvd_buf_json_str(out, MM[i].name);
    }
}

void zxvd_mod_call(int i, const char *js, const w4_jtok_t *t, int32_t n, int32_t params,
                   zxvd_buf_t *res, int *err, char *msg, size_t msg_cap)
{
    mc_t c = {js, t, n, params, res, err, msg, msg_cap};
    if (i >= 0 && i < N_MM) MM[i].fn(&c);
}

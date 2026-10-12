/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zaero_tm.c — telemetry packets (exact integer readings, CRC-32C) and their
 * carriage in UBH-168 frames (zxv_wire_pack / zxv_wire_unpack). Integer
 * code only (R11). */
#include <string.h>

#include "zaero.h"

uint32_t zao_crc32c(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0x82F63B78u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}
static void put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void put_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static uint16_t get_u16(const uint8_t *p) { return (uint16_t) (p[0] | (p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}
static uint64_t get_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

int32_t zao_tm_encode(const zao_tm *tm, uint8_t *buf, size_t cap, size_t *out_len) {
    size_t len, o = 0;
    if (!tm || !out_len) return ZXV_E_ARG;
    if (tm->n > ZAO_TM_VALUES) {
        zind_set_error("zao_tm_encode: at most %u readings", ZAO_TM_VALUES);
        return ZXV_E_ARG;
    }
    len = ZAO_TM_MIN_BYTES + 10u * tm->n;
    *out_len = len;
    if (!buf || cap < len) return ZXV_E_BUFFER;
    buf[o++] = 1;
    put_u16(buf + o, tm->scid);
    o += 2;
    buf[o++] = tm->vcid;
    put_u32(buf + o, tm->seq);
    o += 4;
    put_u64(buf + o, tm->time_ms);
    o += 8;
    buf[o++] = tm->n;
    for (uint32_t i = 0; i < tm->n; i++) {
        put_u16(buf + o, tm->v[i].channel);
        put_u64(buf + o + 2, (uint64_t) tm->v[i].value);
        o += 10;
    }
    put_u32(buf + o, zao_crc32c(buf, o));
    return ZXV_OK;
}

int32_t zao_tm_decode(const uint8_t *buf, size_t len, zao_tm *out) {
    size_t o = 0;
    uint8_t n;
    if (!buf || !out) return ZXV_E_ARG;
    if (len < ZAO_TM_MIN_BYTES || buf[0] != 1) return ZXV_E_FORMAT;
    n = buf[16];
    if (n > ZAO_TM_VALUES || len != ZAO_TM_MIN_BYTES + 10u * n) return ZXV_E_FORMAT;
    memset(out, 0, sizeof *out);
    o = 1;
    out->scid = get_u16(buf + o);
    o += 2;
    out->vcid = buf[o++];
    out->seq = get_u32(buf + o);
    o += 4;
    out->time_ms = get_u64(buf + o);
    o += 8;
    out->n = buf[o++];
    for (uint32_t i = 0; i < n; i++) {
        out->v[i].channel = get_u16(buf + o);
        out->v[i].value = (int64_t) get_u64(buf + o + 2);
        o += 10;
    }
    if (get_u32(buf + o) != zao_crc32c(buf, o)) {
        zind_set_error("zao_tm_decode: the on-board CRC-32C does not match");
        return ZAO_E_CRC;
    }
    return ZXV_OK;
}

/* ===== UBH-168 carriage ===== */

size_t zao_wire_frames(size_t len) { return 1 + (len + ZAO_WIRE_PAYLOAD - 1) / ZAO_WIRE_PAYLOAD; }

static uint32_t word_at(const uint8_t *b, size_t len, size_t at) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = (v << 8) | (at + (size_t) i < len ? b[at + (size_t) i] : 0u);
    return v;
}

int32_t zao_wire_encode(uint32_t trunk, const uint8_t *bytes, size_t len, uint8_t *frames,
                        size_t cap, size_t *out_nframes) {
    uint32_t sp[3], sm[2];
    uint8_t tag = 0, digest[32];
    size_t nf, ndata;
    int32_t st;
    if (!out_nframes || (len && !bytes) || trunk > 9) return ZXV_E_ARG;
    if (len == 0 || len > ZAO_WIRE_MAX_BYTES) {
        zind_set_error("zao_wire_encode: 1..%u bytes", ZAO_WIRE_MAX_BYTES);
        return ZXV_E_ARG;
    }
    nf = zao_wire_frames(len);
    ndata = nf - 1;
    *out_nframes = nf;
    if (!frames || cap < nf * ZXV_WIRE_FRAME_BYTES) return ZXV_E_BUFFER;
    st = zxv_sha3_256(bytes, len, digest);
    if (st != ZXV_OK) return st;
    st = zxv_wire_tag(trunk, 0, 1, &tag);
    if (st != ZXV_OK) return st;
    sp[0] = ZAO_WIRE_MAGIC;
    sp[1] = (uint32_t) len;
    sp[2] = (uint32_t) ndata;
    sm[0] = zao_crc32c(bytes, len);
    sm[1] = word_at(digest, 32, 0);
    st = zxv_wire_pack(tag, sp, sm, frames);
    for (size_t k = 0; st == ZXV_OK && k < ndata; k++) {
        size_t at = k * ZAO_WIRE_PAYLOAD;
        st = zxv_wire_tag(trunk, (uint32_t) (k % 10), 0, &tag);
        if (st != ZXV_OK) break;
        sp[0] = 0xA0000000u | (uint32_t) k;
        sm[0] = word_at(bytes, len, at);
        sp[1] = word_at(bytes, len, at + 4);
        sm[1] = word_at(bytes, len, at + 8);
        sp[2] = word_at(bytes, len, at + 12);
        st = zxv_wire_pack(tag, sp, sm, frames + (k + 1) * ZXV_WIRE_FRAME_BYTES);
    }
    return st;
}

static int32_t wire_bad(const char *why) {
    zind_set_error("zao_wire_decode: %s", why);
    return ZAO_E_WIRE;
}

int32_t zao_wire_decode(const uint8_t *frames, size_t nframes, uint8_t *bytes, size_t cap,
                        size_t *out_len, uint32_t *out_trunk) {
    uint32_t sp[3], sm[2], trunk, len, ndata, crc, dig;
    uint8_t tag = 0, digest[32];
    int32_t st;
    if (!frames || !out_len) return ZXV_E_ARG;
    if (nframes < 2) return wire_bad("fewer than two frames");
    st = zxv_wire_unpack(frames, &tag, sp, sm);
    if (st != ZXV_OK || tag >= 200) return wire_bad("the header frame does not unpack");
    trunk = tag % 10u;
    if (tag != 100u + trunk) return wire_bad("the first frame is not a header (sync 1, shell 0)");
    if (sp[0] != ZAO_WIRE_MAGIC) return wire_bad("the header magic is wrong");
    len = sp[1];
    ndata = sp[2];
    crc = sm[0];
    dig = sm[1];
    if (len == 0 || len > ZAO_WIRE_MAX_BYTES || ndata != (len + ZAO_WIRE_PAYLOAD - 1) / ZAO_WIRE_PAYLOAD)
        return wire_bad("the header length and frame count disagree");
    if (nframes != (size_t) ndata + 1) return wire_bad("missing or extra frames");
    *out_len = len;
    if (!bytes || cap < len) return ZXV_E_BUFFER;
    for (uint32_t k = 0; k < ndata; k++) {
        uint32_t w[4];
        st = zxv_wire_unpack(frames + (size_t) (k + 1) * ZXV_WIRE_FRAME_BYTES, &tag, sp, sm);
        if (st != ZXV_OK || tag != (uint8_t) (10u * (k % 10u) + trunk))
            return wire_bad("a data frame has the wrong tag (trunk, shell or sync)");
        if (sp[0] != (0xA0000000u | k)) return wire_bad("a data frame is out of order");
        w[0] = sm[0];
        w[1] = sp[1];
        w[2] = sm[1];
        w[3] = sp[2];
        for (uint32_t b = 0; b < 16; b++) {
            uint32_t at = k * 16u + b;
            uint8_t v = (uint8_t) (w[b / 4] >> (8 * (b % 4)));
            if (at < len) bytes[at] = v;
            else if (v != 0) return wire_bad("non-zero padding in the last frame");
        }
    }
    if (zao_crc32c(bytes, len) != crc) return wire_bad("the carrier CRC-32C does not match");
    st = zxv_sha3_256(bytes, len, digest);
    if (st != ZXV_OK) return st;
    if (word_at(digest, 32, 0) != dig) return wire_bad("the carrier digest does not match");
    if (out_trunk) *out_trunk = trunk;
    return ZXV_OK;
}

uint32_t zao_bit_diff(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen) {
    size_t n = alen < blen ? alen : blen;
    uint32_t bits = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t x = (uint8_t) (a[i] ^ b[i]);
        while (x) {
            bits += x & 1u;
            x >>= 1;
        }
    }
    bits += (uint32_t) (8 * (alen > blen ? alen - blen : blen - alen));
    return bits;
}

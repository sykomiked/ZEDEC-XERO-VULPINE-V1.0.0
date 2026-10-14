/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvband.c — v-band core: the append-only sensor log, LoRaWAN-sized
 * batches, UBH-168 carriage and the in-order gateway.
 *
 * MCU subset style: no heap, no stdio, no C library calls (byte loops in
 * place of memcpy), every buffer owned by the caller, integers only. The
 * only external calls are the pure libzxv functions zxv_sha3_256,
 * zxv_wire_*, zxv_hex_* and zxv_status_name, which need no context, no
 * capability and no heap (sdk/embedded/README.md, "pure"). R11. */
#include "zvband.h"

/* ===== small helpers (no libc) ===== */

static void cpy(uint8_t *d, const uint8_t *s, size_t n) {
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static int same(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t x = 0;
    for (size_t i = 0; i < n; i++) x |= (uint8_t) (a[i] ^ b[i]);
    return x == 0;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

static uint32_t zigzag(int32_t v) {
    return v < 0 ? (((uint32_t) (-(v + 1))) << 1) | 1u : ((uint32_t) v) << 1;
}

static int32_t unzigzag(uint32_t u) {
    return (u & 1u) ? -(int32_t) (u >> 1) - 1 : (int32_t) (u >> 1);
}

static size_t varint_len(uint32_t v) {
    size_t n = 1;
    while (v >= 0x80u) {
        v >>= 7;
        n++;
    }
    return n;
}

static size_t varint_put(uint8_t *p, uint32_t v) {
    size_t n = 0;
    while (v >= 0x80u) {
        p[n++] = (uint8_t) (v | 0x80u);
        v >>= 7;
    }
    p[n++] = (uint8_t) v;
    return n;
}

/* Canonical (shortest) LEB128 of a uint32 only. 0 on error. */
static size_t varint_get(const uint8_t *p, size_t avail, uint32_t *out) {
    uint32_t v = 0;
    for (size_t i = 0; i < 5 && i < avail; i++) {
        uint8_t b = p[i];
        if (i == 4 && b > 0x0Fu) return 0;
        v |= (uint32_t) (b & 0x7Fu) << (7 * i);
        if (!(b & 0x80u)) {
            if (i > 0 && b == 0) return 0; /* overlong */
            *out = v;
            return i + 1;
        }
    }
    return 0;
}

const char *zvb_status_name(int32_t status) {
    switch (status) {
    case ZVB_E_FULL: return "ZVB_E_FULL";
    case ZVB_E_DUPLICATE: return "ZVB_E_DUPLICATE";
    case ZVB_E_ORDER: return "ZVB_E_ORDER";
    case ZVB_E_CHECK: return "ZVB_E_CHECK";
    case ZVB_E_DR: return "ZVB_E_DR";
    case ZVB_E_DEVICE: return "ZVB_E_DEVICE";
    case ZVB_E_WIRE: return "ZVB_E_WIRE";
    case ZVB_E_EMPTY: return "ZVB_E_EMPTY";
    default: return zxv_status_name(status);
    }
}

/* ===== the log ===== */

int32_t zvb_log_init(zvb_log *log, zvb_record *storage, uint32_t cap, uint32_t device) {
    if (!log || !storage || cap == 0) return ZXV_E_ARG;
    for (size_t i = 0; i < sizeof *log; i++) ((uint8_t *) log)[i] = 0;
    log->rec = storage;
    log->cap = cap;
    log->device = device;
    log->next_seq = 1;
    log->next_batch = 1;
    return ZXV_OK;
}

static int32_t chain_step(zvb_log *log, const zvb_record *r) {
    uint8_t b[10 + 32 + 4 + 18];
    cpy(b, (const uint8_t *) "zvb/rec/v1", 10);
    cpy(b + 10, log->chain, 32);
    put32(b + 42, log->device);
    put32(b + 46, r->seq);
    put32(b + 50, r->time_s);
    b[54] = r->sensor;
    b[55] = r->lp;
    put32(b + 56, (uint32_t) r->value);
    put32(b + 60, (uint32_t) r->value2);
    return zxv_sha3_256(b, sizeof b, log->chain);
}

static void release_acked(zvb_log *log, uint32_t *released) {
    uint32_t first = log->next_seq - log->n, k = 0;
    while (log->n > 0 && first + k <= log->acked) {
        log->head = (log->head + 1) % log->cap;
        log->n--;
        k++;
    }
    if (released) *released = k;
}

static int32_t append(zvb_log *log, uint8_t sensor, uint32_t time_s, int32_t lp, int32_t v,
                      int32_t v2) {
    zvb_record *r;
    zvb_record nr;
    int32_t st;
    if (time_s < log->last_time) return ZXV_E_ARG;
    if (log->next_seq == UINT32_MAX) return ZXV_E_RANGE;
    if (log->n == log->cap) release_acked(log, (uint32_t *) 0);
    if (log->n == log->cap) return ZVB_E_FULL;
    nr.seq = log->next_seq;
    nr.time_s = time_s;
    nr.sensor = sensor;
    nr.lp = (uint8_t) lp;
    nr.value = v;
    nr.value2 = v2;
    st = chain_step(log, &nr);
    if (st != ZXV_OK) return st;
    r = &log->rec[(log->head + log->n) % log->cap];
    *r = nr;
    log->n++;
    log->next_seq++;
    log->last_time = time_s;
    return ZXV_OK;
}

int32_t zvb_log_append(zvb_log *log, uint8_t sensor, uint32_t time_s, int32_t lp, int32_t value) {
    if (!log) return ZXV_E_ARG;
    if (lp != ZXV_LP_TRUE && lp != ZXV_LP_FALSE && lp != ZXV_LP_NEITHER) return ZXV_E_ARG;
    if (lp == ZXV_LP_NEITHER && value != 0) return ZXV_E_ARG;
    return append(log, sensor, time_s, lp, value, 0);
}

int32_t zvb_log_append_pair(zvb_log *log, uint8_t sensor, uint32_t time_s, int32_t a, int32_t b,
                            uint32_t tolerance) {
    uint32_t diff;
    if (!log) return ZXV_E_ARG;
    diff = a >= b ? (uint32_t) a - (uint32_t) b : (uint32_t) b - (uint32_t) a;
    if (diff <= tolerance) return append(log, sensor, time_s, ZXV_LP_TRUE, a, 0);
    return append(log, sensor, time_s, ZXV_LP_BOTH, a, b);
}

int32_t zvb_log_at(const zvb_log *log, uint32_t i, zvb_record *out) {
    if (!log || !out) return ZXV_E_ARG;
    if (i >= log->n) return ZXV_E_NOT_FOUND;
    *out = log->rec[(log->head + i) % log->cap];
    return ZXV_OK;
}

int32_t zvb_log_ack(zvb_log *log, uint32_t next_expected) {
    if (!log) return ZXV_E_ARG;
    if (next_expected < log->acked + 1 || next_expected > log->sent_max + 1) return ZXV_E_ARG;
    log->acked = next_expected - 1;
    log->sent = log->acked;
    return ZXV_OK;
}

int32_t zvb_log_trim(zvb_log *log, uint32_t *out_released) {
    if (!log) return ZXV_E_ARG;
    release_acked(log, out_released);
    return ZXV_OK;
}

/* ===== payload limits ===== */

int32_t zvb_payload_max(int32_t region, uint8_t dr, uint32_t *out_bytes) {
    static const uint16_t eu868[8] = {51, 51, 51, 115, 222, 222, 222, 222};
    static const uint16_t us915[5] = {11, 53, 125, 242, 242};
    if (!out_bytes) return ZXV_E_ARG;
    if (region == ZVB_REGION_EU868 && dr < 8) *out_bytes = eu868[dr];
    else if (region == ZVB_REGION_US915 && dr < 5) *out_bytes = us915[dr];
    else if (region == ZVB_REGION_NBIOT) *out_bytes = ZVB_BATCH_MAX;
    else return ZVB_E_DR;
    return ZXV_OK;
}

/* ===== batches ===== */

static int32_t check4(const uint8_t *buf, size_t len, uint8_t out[4]) {
    uint8_t tmp[12 + ZVB_BATCH_MAX], h[32];
    int32_t st;
    if (len > ZVB_BATCH_MAX) return ZXV_E_ARG;
    cpy(tmp, (const uint8_t *) "zvb/batch/v1", 12);
    cpy(tmp + 12, buf, len);
    st = zxv_sha3_256(tmp, 12 + len, h);
    if (st == ZXV_OK) cpy(out, h, 4);
    return st;
}

static size_t rec_size(const zvb_record *r, uint32_t prev_time) {
    size_t n = 2 + varint_len(r->time_s - prev_time) + varint_len(zigzag(r->value));
    if (r->lp == ZXV_LP_BOTH) n += varint_len(zigzag(r->value2));
    return n;
}

int32_t zvb_batch_make(zvb_log *log, uint32_t max_bytes, uint8_t *out, size_t cap,
                       size_t *out_len, uint32_t *out_count) {
    uint32_t first_kept, idx, count = 0, prev;
    size_t o = ZVB_BATCH_HEADER;
    const zvb_record *r0;
    int32_t st;
    if (!log || !out || !out_len || !out_count) return ZXV_E_ARG;
    if (max_bytes > ZVB_BATCH_MAX) return ZXV_E_ARG;
    if (cap < max_bytes) max_bytes = (uint32_t) cap;
    if (log->sent + 1 >= log->next_seq) return ZVB_E_EMPTY;
    first_kept = log->next_seq - log->n;
    idx = log->sent + 1 - first_kept;
    r0 = &log->rec[(log->head + idx) % log->cap];
    prev = r0->time_s;
    while (idx + count < log->n && count < ZVB_BATCH_RECORDS) {
        const zvb_record *r = &log->rec[(log->head + idx + count) % log->cap];
        size_t sz = rec_size(r, prev);
        if (o + sz + 4 > max_bytes) break;
        out[o++] = r->sensor;
        out[o++] = (uint8_t) (r->lp | (r->lp == ZXV_LP_BOTH ? 4u : 0u));
        o += varint_put(out + o, r->time_s - prev);
        o += varint_put(out + o, zigzag(r->value));
        if (r->lp == ZXV_LP_BOTH) o += varint_put(out + o, zigzag(r->value2));
        prev = r->time_s;
        count++;
    }
    if (count == 0) return ZVB_E_DR;
    out[0] = 1;
    put32(out + 1, log->device);
    put32(out + 5, log->next_batch);
    put32(out + 9, r0->seq);
    out[13] = (uint8_t) count;
    put32(out + 14, r0->time_s);
    st = check4(out, o, out + o);
    if (st != ZXV_OK) return st;
    *out_len = o + 4;
    *out_count = count;
    log->sent += count;
    if (log->sent > log->sent_max) log->sent_max = log->sent;
    log->next_batch++;
    return ZXV_OK;
}

int32_t zvb_batch_decode(const uint8_t *buf, size_t len, zvb_record *out, uint32_t out_cap,
                         zvb_batch_info *info) {
    uint8_t chk[4];
    uint32_t count, seq, t;
    size_t o = ZVB_BATCH_HEADER, end;
    int32_t st, lp = ZXV_LP_TRUE;
    uint8_t base = 0;
    if (!buf || !info) return ZXV_E_ARG;
    if (len < ZVB_BATCH_MIN || len > ZVB_BATCH_MAX || buf[0] != 1) return ZXV_E_FORMAT;
    end = len - 4;
    st = check4(buf, end, chk);
    if (st != ZXV_OK) return st;
    if (!same(chk, buf + end, 4)) return ZVB_E_CHECK;
    for (size_t i = 0; i < sizeof *info; i++) ((uint8_t *) info)[i] = 0;
    info->device = get32(buf + 1);
    info->batch = get32(buf + 5);
    info->first_seq = seq = get32(buf + 9);
    info->count = count = buf[13];
    t = get32(buf + 14);
    if (count == 0 || seq == 0 || info->batch == 0 || seq > UINT32_MAX - count) return ZXV_E_FORMAT;
    if (out && out_cap < count) return ZXV_E_BUFFER;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t dt = 0, zv = 0, zv2 = 0;
        uint8_t sensor, flags;
        size_t k;
        if (end - o < 4) return ZXV_E_FORMAT;
        sensor = buf[o++];
        flags = buf[o++];
        if ((flags & ~7u) || ((flags & 4u) != 0) != ((flags & 3u) == ZXV_LP_BOTH))
            return ZXV_E_FORMAT;
        k = varint_get(buf + o, end - o, &dt);
        if (!k || (i == 0 && dt != 0) || t > UINT32_MAX - dt) return ZXV_E_FORMAT;
        o += k;
        t += dt;
        k = varint_get(buf + o, end - o, &zv);
        if (!k) return ZXV_E_FORMAT;
        o += k;
        if (flags & 4u) {
            k = varint_get(buf + o, end - o, &zv2);
            if (!k) return ZXV_E_FORMAT;
            o += k;
        }
        if ((flags & 3u) == ZXV_LP_NEITHER && zv != 0) return ZXV_E_FORMAT;
        info->n_lp[flags & 3u]++;
        if (out) {
            out[i].seq = seq + i;
            out[i].time_s = t;
            out[i].sensor = sensor;
            out[i].lp = (uint8_t) (flags & 3u);
            out[i].value = unzigzag(zv);
            out[i].value2 = unzigzag(zv2);
        }
    }
    if (o != end) return ZXV_E_FORMAT;
    if (info->n_lp[ZXV_LP_BOTH]) lp = ZXV_LP_BOTH;
    else if (info->n_lp[ZXV_LP_FALSE]) lp = ZXV_LP_FALSE;
    else if (info->n_lp[ZXV_LP_NEITHER]) lp = ZXV_LP_NEITHER;
    st = zxv_hex_from_lp(lp, &base);
    if (st == ZXV_OK) st = zxv_hex_pack(base, ZXV_HEX_POL_NEUTRAL, ZXV_HEX_PHASE_MANIFEST, &info->tag);
    return st;
}

/* ===== UBH-168 ===== */

uint32_t zvb_wire_frames(size_t len) { return 1u + (uint32_t) ((len + 15u) / 16u); }

static uint32_t word_at(const uint8_t *b, size_t len, size_t at) {
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = (v << 8) | (at + (size_t) i < len ? b[at + (size_t) i] : 0u);
    return v;
}

int32_t zvb_wire_encode(uint32_t trunk, const uint8_t *bytes, size_t len, uint8_t *frames,
                        size_t cap, size_t *out_nframes) {
    uint32_t sp[3], sm[2], nf;
    uint8_t tag = 0, h[32];
    int32_t st;
    if (!bytes || !out_nframes || trunk > 9 || len == 0 || len > ZVB_BATCH_MAX) return ZXV_E_ARG;
    nf = zvb_wire_frames(len);
    *out_nframes = nf;
    if (!frames || cap < (size_t) nf * ZXV_WIRE_FRAME_BYTES) return ZXV_E_BUFFER;
    st = zxv_sha3_256(bytes, len, h);
    if (st == ZXV_OK) st = zxv_wire_tag(trunk, 0, 1, &tag);
    if (st != ZXV_OK) return st;
    sp[0] = ZVB_WIRE_MAGIC;
    sp[1] = (uint32_t) len;
    sp[2] = nf - 1;
    sm[0] = word_at(h, 32, 0);
    sm[1] = word_at(h, 32, 4);
    st = zxv_wire_pack(tag, sp, sm, frames);
    for (uint32_t k = 0; st == ZXV_OK && k + 1 < nf; k++) {
        size_t at = (size_t) k * 16u;
        st = zxv_wire_tag(trunk, k % 10u, 0, &tag);
        if (st != ZXV_OK) break;
        sp[0] = 0xB0000000u | k;
        sm[0] = word_at(bytes, len, at);
        sp[1] = word_at(bytes, len, at + 4);
        sm[1] = word_at(bytes, len, at + 8);
        sp[2] = word_at(bytes, len, at + 12);
        st = zxv_wire_pack(tag, sp, sm, frames + (size_t) (k + 1) * ZXV_WIRE_FRAME_BYTES);
    }
    return st;
}

int32_t zvb_wire_decode(const uint8_t *frames, size_t nframes, uint8_t *bytes, size_t cap,
                        size_t *out_len) {
    uint32_t sp[3], sm[2], trunk, len, ndata, h0, h1;
    uint8_t tag = 0, h[32];
    int32_t st;
    if (!frames || !out_len) return ZXV_E_ARG;
    if (nframes < 2) return ZVB_E_WIRE;
    st = zxv_wire_unpack(frames, &tag, sp, sm);
    if (st != ZXV_OK || tag >= 200) return ZVB_E_WIRE;
    trunk = tag % 10u;
    if (tag != 100u + trunk || sp[0] != ZVB_WIRE_MAGIC) return ZVB_E_WIRE;
    len = sp[1];
    ndata = sp[2];
    h0 = sm[0];
    h1 = sm[1];
    if (len == 0 || len > ZVB_BATCH_MAX || ndata != (len + 15u) / 16u || nframes != ndata + 1u)
        return ZVB_E_WIRE;
    *out_len = len;
    if (!bytes || cap < len) return ZXV_E_BUFFER;
    for (uint32_t k = 0; k < ndata; k++) {
        uint32_t w[4];
        st = zxv_wire_unpack(frames + (size_t) (k + 1) * ZXV_WIRE_FRAME_BYTES, &tag, sp, sm);
        if (st != ZXV_OK || tag != (uint8_t) (10u * (k % 10u) + trunk) ||
            sp[0] != (0xB0000000u | k))
            return ZVB_E_WIRE;
        w[0] = sm[0];
        w[1] = sp[1];
        w[2] = sm[1];
        w[3] = sp[2];
        for (uint32_t b = 0; b < 16; b++) {
            uint32_t at = k * 16u + b;
            uint8_t v = (uint8_t) (w[b / 4] >> (8 * (b % 4)));
            if (at < len) bytes[at] = v;
            else if (v != 0) return ZVB_E_WIRE;
        }
    }
    st = zxv_sha3_256(bytes, len, h);
    if (st != ZXV_OK) return st;
    if (word_at(h, 32, 0) != h0 || word_at(h, 32, 4) != h1) return ZVB_E_WIRE;
    return ZXV_OK;
}

/* ===== the gateway ===== */

int32_t zvb_gw_init(zvb_gw *gw, zvb_gw_dev *storage, uint32_t cap, zvb_refuse_fn fn, void *user) {
    if (!gw || !storage || cap == 0) return ZXV_E_ARG;
    gw->dev = storage;
    gw->cap = cap;
    gw->n = 0;
    gw->on_refuse = fn;
    gw->user = user;
    return ZXV_OK;
}

static zvb_gw_dev *gw_find(const zvb_gw *gw, uint32_t device) {
    for (uint32_t i = 0; i < gw->n; i++)
        if (gw->dev[i].device == device) return &gw->dev[i];
    return (zvb_gw_dev *) 0;
}

int32_t zvb_gw_add(zvb_gw *gw, uint32_t device) {
    zvb_gw_dev *d;
    if (!gw) return ZXV_E_ARG;
    if (gw_find(gw, device)) return ZXV_E_DUPLICATE;
    if (gw->n >= gw->cap) return ZXV_E_CAPACITY;
    d = &gw->dev[gw->n++];
    for (size_t i = 0; i < sizeof *d; i++) ((uint8_t *) d)[i] = 0;
    d->device = device;
    d->next_seq = 1;
    return ZXV_OK;
}

static int32_t refuse(zvb_gw *gw, zvb_gw_dev *d, uint32_t device, int32_t st, uint32_t batch) {
    if (d) d->refused++;
    if (gw->on_refuse) gw->on_refuse(gw->user, device, st, batch);
    return st;
}

int32_t zvb_gw_accept(zvb_gw *gw, const uint8_t *buf, size_t len, zvb_record *out,
                      uint32_t out_cap, zvb_batch_info *info, uint32_t *out_next) {
    zvb_batch_info tmp;
    zvb_gw_dev *d;
    int32_t st;
    if (!gw || !buf) return ZXV_E_ARG;
    if (!info) info = &tmp;
    st = zvb_batch_decode(buf, len, (zvb_record *) 0, 0, info);
    if (st != ZXV_OK) {
        uint32_t dev = len >= 5 ? get32(buf + 1) : 0;
        d = gw_find(gw, dev);
        if (d && out_next) *out_next = d->next_seq;
        return refuse(gw, d, dev, st, len >= 9 ? get32(buf + 5) : 0);
    }
    d = gw_find(gw, info->device);
    if (!d) return refuse(gw, d, info->device, ZVB_E_DEVICE, info->batch);
    if (out_next) *out_next = d->next_seq;
    if (info->batch == d->last_batch || info->first_seq < d->next_seq)
        return refuse(gw, d, info->device, ZVB_E_DUPLICATE, info->batch);
    if (info->batch < d->last_batch || info->first_seq > d->next_seq)
        return refuse(gw, d, info->device, ZVB_E_ORDER, info->batch);
    if (!out || out_cap < info->count) return ZXV_E_BUFFER;
    st = zvb_batch_decode(buf, len, out, out_cap, info);
    if (st != ZXV_OK) return st;
    d->last_batch = info->batch;
    d->next_seq += info->count;
    cpy(d->last_check, buf + len - 4, 4);
    d->accepted++;
    if (out_next) *out_next = d->next_seq;
    return ZXV_OK;
}

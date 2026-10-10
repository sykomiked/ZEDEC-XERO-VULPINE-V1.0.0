/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_frame.c — UBH-168 framing over src/ubh/ubh.c. See vna_frame.h. */
#include "vna_frame.h"
#include "../ubh/ubh.h"

uint32_t vna_frame_len(uint32_t len)
{
    uint32_t frames = (len + (UBH_168_OCTETS - 1u)) / UBH_168_OCTETS; /* 32-bit division */
    return VNA_UBH_HDR + frames * UBH_168_OCTETS;
}

int32_t vna_frame_wrap(const vna_schema_t *s, const uint8_t *rec, uint32_t len, uint8_t *out,
                       uint32_t cap)
{
    if (!s || !rec || !out || len > 0x00FFFFFFu) return -1;
    uint32_t total = vna_frame_len(len);
    if (total > cap) return -1;
    ubh_168_header_t h;
    ubh_168_header_init(&h, (ubh_frame_class_t) s->ubh_class);
    h.source_format_id = (uint16_t) VNA_UBH_FORMAT;
    h.target_type = s->id;
    h.flags = (uint8_t) (h.flags | 0x01u); /* has_payload (is_canonical set by init) */
    h.payload_length = len;
    h.schema_id = VNA_UBH_SCHEMA_TAG;
    uint8_t d[32];
    vna_sha3(rec, len, d);
    h.integrity_ref[0] = d[0];
    h.integrity_ref[1] = d[1];
    h.integrity_ref[2] = d[2];
    ubh_168_header_pack(&h, out);
    vna_copy(out + VNA_UBH_HDR, rec, len);
    vna_zero(out + VNA_UBH_HDR + len, total - VNA_UBH_HDR - len);
    return (int32_t) total;
}

bool vna_frame_is_ubh(const uint8_t *buf, uint32_t len)
{
    return buf && len >= VNA_UBH_HDR && buf[0] == ((UBH_MAGIC >> 16) & 0xFFu) &&
           buf[1] == ((UBH_MAGIC >> 8) & 0xFFu) && buf[2] == (UBH_MAGIC & 0xFFu);
}

vna_status_t vna_frame_unwrap(const uint8_t *buf, uint32_t len, const uint8_t **rec,
                              uint32_t *rec_len, uint16_t *schema_id, uint8_t *frame_class)
{
    if (!vna_frame_is_ubh(buf, len)) return VNA_ERR_PARSE;
    ubh_168_header_t h;
    if (!ubh_168_header_unpack(&h, buf)) return VNA_ERR_PARSE;
    if (h.version != 1 || h.source_format_id != VNA_UBH_FORMAT ||
        h.schema_id != VNA_UBH_SCHEMA_TAG || (h.flags & 0x01u) == 0)
        return VNA_ERR_PARSE;
    if (h.payload_length > len - VNA_UBH_HDR) return VNA_ERR_PARSE;
    if (vna_frame_len(h.payload_length) != len) return VNA_ERR_PARSE;
    const uint8_t *p = buf + VNA_UBH_HDR;
    for (uint32_t i = h.payload_length; i < len - VNA_UBH_HDR; i++)
        if (p[i]) return VNA_ERR_PARSE; /* padding must be zero */
    uint8_t d[32];
    vna_sha3(p, h.payload_length, d);
    if (d[0] != h.integrity_ref[0] || d[1] != h.integrity_ref[1] || d[2] != h.integrity_ref[2])
        return VNA_ERR_PARSE;
    *rec = p;
    *rec_len = h.payload_length;
    if (schema_id) *schema_id = h.target_type;
    if (frame_class) *frame_class = h.frame_class;
    return VNA_OK;
}

vna_status_t vna_frame_accept(const vna_schema_t *s, const uint8_t *buf, uint32_t len,
                              const uint8_t **rec, uint32_t *rec_len, bool *was_ubh)
{
    if (!s || !buf || !rec || !rec_len) return VNA_ERR_ARG;
    if (vna_frame_is_ubh(buf, len)) {
        uint16_t sid;
        uint8_t cls;
        vna_status_t st = vna_frame_unwrap(buf, len, rec, rec_len, &sid, &cls);
        if (st != VNA_OK) return st;
        if (sid != s->id || cls != s->ubh_class) return VNA_ERR_PARSE;
        if (was_ubh) *was_ubh = true;
        return VNA_OK;
    }
    *rec = buf;
    *rec_len = len;
    if (was_ubh) *was_ubh = false;
    return VNA_OK;
}

int32_t vna_xform_apply(const vna_xform_t *x, bool seal, const uint8_t *addr, uint32_t addr_len,
                        uint8_t *buf, uint32_t len, uint32_t cap, uint8_t *scratch, uint32_t scap)
{
    vna_xform_fn fn = x ? (seal ? x->seal : x->open) : 0;
    if (!fn) return (int32_t) len;
    if (!buf || !scratch || len > cap) return -1;
    if (seal && x->overhead > VNA_XFORM_MAX_OVERHEAD) return -1;
    int32_t r = fn(x->ctx, addr, addr_len, buf, len, scratch, scap);
    if (r < 0 || (uint32_t) r > cap || (uint32_t) r > scap) return -1;
    if (seal && (uint32_t) r > len + x->overhead) return -1;
    vna_copy(buf, scratch, (uint32_t) r);
    return r;
}

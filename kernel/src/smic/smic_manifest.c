/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "smic_manifest.h"
#include "../ubh/ubh.h"
#include "../robin_debanks/sha256.h"

static void sm_copy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    while (n--) *d++ = *s++;
}

static void sm_zero(void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    while (n--) *d++ = 0;
}

static void put_be64(uint8_t *p, uint64_t v)
{
    uint32_t i;
    for (i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (56 - 8 * i));
}

static void put_be32(uint8_t *p, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (24 - 8 * i));
}

static void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static uint64_t get_be64(const uint8_t *p)
{
    uint64_t v = 0;
    uint32_t i;
    for (i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static uint32_t get_be32(const uint8_t *p)
{
    uint32_t v = 0;
    uint32_t i;
    for (i = 0; i < 4; i++) v = (v << 8) | p[i];
    return v;
}

static int smic_type_ok(uint16_t t)
{
    return t == SMIC_T_MANIFEST || t == SMIC_T_TELEMETRY || t == SMIC_T_RECEIPT;
}

int smic_manifest_pack(uint16_t type, const smic_hw_t *hw, uint8_t out[SMIC_ENV_BYTES])
{
    ubh_168_header_t h;
    uint8_t d[SHA256_DIGEST_LEN];
    uint8_t *pl;
    uint32_t i;

    if (!hw || !out || !smic_type_ok(type) || hw->region >= SMIC_REGIONS ||
        hw->arch == SMIC_ARCH_NONE || hw->lpx >= LPX_STATES)
        return SMIC_ERR_ARG;

    ubh_168_header_init(&h, UBH_FRAME_CONTENT_OBJECT);
    h.source_format_id = UBH_FMT_UBH_168;
    h.target_type = type;
    h.flags = 0x05u; /* has_payload | canonical */
    h.payload_length = SMIC_PAYLOAD;
    h.schema_id = SMIC_SCHEMA;
    for (i = 0; i < 3; i++) h.integrity_ref[i] = hw->node_id[i];
    ubh_168_header_pack(&h, out);

    pl = out + SMIC_FRAME;
    sm_zero(pl, SMIC_PAYLOAD);
    sm_copy(pl + 0, hw->node_id, 32);
    sm_copy(pl + 32, hw->hw_digest, 32);
    put_be64(pl + 64, hw->caps);
    put_be64(pl + 72, hw->provisioned_at);
    put_be64(pl + 80, hw->disk_mb);
    put_be16(pl + 88, hw->net_mbps);
    pl[90] = hw->region;
    pl[91] = hw->role;
    pl[92] = hw->octane;
    pl[93] = hw->lpx;
    pl[94] = hw->arch;
    pl[95] = hw->flags;
    put_be32(pl + 96, hw->fw_crc);

    /* integrity trailer: the 21-octet header repacked as an INTEGRITY
     * frame carrying the first 3 digest bytes over everything before it */
    sha256(out, SMIC_FRAME + SMIC_PAYLOAD, d);
    ubh_168_header_init(&h, UBH_FRAME_INTEGRITY);
    h.source_format_id = UBH_FMT_UBH_168;
    h.target_type = type;
    h.payload_length = SMIC_PAYLOAD;
    h.schema_id = SMIC_PAYLOAD / SMIC_FRAME; /* 6 frames */
    for (i = 0; i < 3; i++) h.integrity_ref[i] = d[i];
    ubh_168_header_pack(&h, out + SMIC_FRAME + SMIC_PAYLOAD);
    return SMIC_OK;
}

int smic_manifest_unpack(const uint8_t in[SMIC_ENV_BYTES], uint16_t *type, smic_hw_t *hw)
{
    ubh_168_header_t h, it;
    uint8_t d[SHA256_DIGEST_LEN];
    const uint8_t *pl;
    uint32_t i;

    if (!in || !hw) return SMIC_ERR_ARG;
    if (!ubh_168_header_unpack(&h, in)) return SMIC_ERR_MALFORMED;
    if (h.magic[0] != 0x5A || h.magic[1] != 0x58 || h.magic[2] != 0x56 ||
        h.frame_class != UBH_FRAME_CONTENT_OBJECT || !smic_type_ok(h.target_type) ||
        h.payload_length != SMIC_PAYLOAD || h.schema_id != SMIC_SCHEMA)
        return SMIC_ERR_MALFORMED;
    /* integrity trailer: same framing, INTEGRITY class, digest match */
    if (!ubh_168_header_unpack(&it, in + SMIC_FRAME + SMIC_PAYLOAD) ||
        it.frame_class != UBH_FRAME_INTEGRITY || it.target_type != h.target_type)
        return SMIC_ERR_MALFORMED;
    sha256(in, SMIC_FRAME + SMIC_PAYLOAD, d);
    for (i = 0; i < 3; i++)
        if (it.integrity_ref[i] != d[i]) return SMIC_ERR_MALFORMED;

    pl = in + SMIC_FRAME;
    sm_copy(hw->node_id, pl + 0, 32);
    sm_copy(hw->hw_digest, pl + 32, 32);
    hw->caps = get_be64(pl + 64);
    hw->provisioned_at = get_be64(pl + 72);
    hw->disk_mb = get_be64(pl + 80);
    hw->net_mbps = (uint16_t) ((pl[88] << 8) | pl[89]);
    hw->region = pl[90];
    hw->role = pl[91];
    hw->octane = pl[92];
    hw->lpx = pl[93];
    hw->arch = pl[94];
    hw->flags = pl[95];
    hw->fw_crc = get_be32(pl + 96);
    if (hw->region >= SMIC_REGIONS || hw->arch == SMIC_ARCH_NONE ||
        hw->lpx >= LPX_STATES || hw->role > SMIC_ROLE_WAREHOUSE)
        return SMIC_ERR_MALFORMED;
    if (type) *type = h.target_type;
    return SMIC_OK;
}

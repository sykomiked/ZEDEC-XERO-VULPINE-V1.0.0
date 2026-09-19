/* crit168_os.c — Canonical Representation, Integrity, Translation (O1)
 *
 * Implements canonical 168-bit frame serialization, CRC integrity,
 * endianness translation, and format registry.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "crit168_os.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ===== CRC-8 (poly 0x07, init 0x00) ===== */

uint8_t crit168_crc8(const uint8_t *data, uint32_t len) {
    if (!data) return 0;
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x07);
            else
                crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* ===== CRC-32 (poly 0xEDB88320, init 0xFFFFFFFF) ===== */

uint32_t crit168_crc32(const uint8_t *data, uint32_t len) {
    if (!data) return 0;
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* ===== Endianness ===== */

void crit168_swap16(uint16_t *val) {
    if (!val) return;
    uint8_t *b = (uint8_t *)val;
    uint8_t tmp = b[0];
    b[0] = b[1];
    b[1] = tmp;
}

void crit168_swap32(uint32_t *val) {
    if (!val) return;
    uint8_t *b = (uint8_t *)val;
    uint8_t tmp;
    tmp = b[0]; b[0] = b[3]; b[3] = tmp;
    tmp = b[1]; b[1] = b[2]; b[2] = tmp;
}

/* ===== Registry ===== */

void crit168_registry_init(crit168_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
}

int32_t crit168_register_format(crit168_registry_t *reg, uint16_t tag,
                                const char *name, uint8_t version,
                                crit168_endian_t endian, uint32_t max_frames) {
    if (!reg || !name) return -1;
    if (reg->format_count >= CRIT168_MAX_FORMATS) return -1;

    /* Check for duplicate tag */
    for (uint32_t i = 0; i < reg->format_count; i++) {
        if (reg->formats[i].tag == tag) return (int32_t)i;
    }

    crit168_format_t *f = &reg->formats[reg->format_count];
    ev_memset(f, 0, sizeof(*f));
    f->tag = tag;
    copy_str(f->name, name, CRIT168_MAX_NAME_LEN);
    f->version = version;
    f->endian = endian;
    f->max_frames = max_frames;
    f->active = true;
    return (int32_t)reg->format_count++;
}

crit168_format_t *crit168_get_format(crit168_registry_t *reg, uint16_t tag) {
    if (!reg) return NULL;
    for (uint32_t i = 0; i < reg->format_count; i++) {
        if (reg->formats[i].tag == tag) return &reg->formats[i];
    }
    return NULL;
}

/* ===== Serializer ===== */

void crit168_serializer_init(crit168_serializer_t *s, uint16_t format_tag,
                             uint8_t version, crit168_endian_t target_endian) {
    if (!s) return;
    ev_memset(s, 0, sizeof(*s));
    s->format_tag = format_tag;
    s->version = version;
    s->target_endian = target_endian;
    s->next_sequence = 0;
}

static void compute_frame_crc(crit168_frame_t *frame) {
    /* CRC over 19 bytes (version + type + format_tag + sequence + payload) */
    uint8_t buf[20];
    ev_memset(buf, 0, sizeof(buf));
    buf[0] = frame->version;
    buf[1] = frame->type;
    /* Serialize format_tag and sequence as-is (native) */
    ev_memcpy(&buf[2], &frame->format_tag, 2);
    ev_memcpy(&buf[4], &frame->sequence, 2);
    ev_memcpy(&buf[6], frame->payload, CRIT168_PAYLOAD_SIZE);
    frame->crc = crit168_crc8(buf, 20);
}

int32_t crit168_serialize_header(crit168_serializer_t *s, uint32_t total_data_len) {
    if (!s) return -1;
    if (s->frame_count >= CRIT168_MAX_FRAMES) return -1;

    crit168_frame_t *f = &s->frames[s->frame_count];
    ev_memset(f, 0, sizeof(*f));
    f->version = s->version;
    f->type = CRIT168_TYPE_HEADER;
    f->format_tag = s->format_tag;
    f->sequence = s->next_sequence++;
    /* Encode total data length in payload (4 bytes) + reserved */
    ev_memcpy(f->payload, &total_data_len, 4);
    compute_frame_crc(f);
    return (int32_t)s->frame_count++;
}

int32_t crit168_serialize_data(crit168_serializer_t *s, const uint8_t *data,
                               uint32_t data_len) {
    if (!s || !data) return -1;
    uint32_t offset = 0;
    int32_t first_idx = -1;

    while (offset < data_len) {
        if (s->frame_count >= CRIT168_MAX_FRAMES) return -1;

        crit168_frame_t *f = &s->frames[s->frame_count];
        ev_memset(f, 0, sizeof(*f));
        f->version = s->version;
        f->type = CRIT168_TYPE_DATA;
        f->format_tag = s->format_tag;
        f->sequence = s->next_sequence++;

        uint32_t chunk = data_len - offset;
        if (chunk > CRIT168_PAYLOAD_SIZE) chunk = CRIT168_PAYLOAD_SIZE;
        ev_memcpy(f->payload, data + offset, chunk);
        offset += chunk;

        compute_frame_crc(f);
        if (first_idx < 0) first_idx = (int32_t)s->frame_count;
        s->frame_count++;
    }
    return first_idx;
}

int32_t crit168_serialize_checksum(crit168_serializer_t *s) {
    if (!s) return -1;
    if (s->frame_count >= CRIT168_MAX_FRAMES) return -1;

    /* Compute CRC-32 over all frame payloads so far */
    uint32_t crc = 0;
    for (uint32_t i = 0; i < s->frame_count; i++) {
        crc = crit168_crc32(s->frames[i].payload, CRIT168_PAYLOAD_SIZE);
    }

    crit168_frame_t *f = &s->frames[s->frame_count];
    ev_memset(f, 0, sizeof(*f));
    f->version = s->version;
    f->type = CRIT168_TYPE_CHECKSUM;
    f->format_tag = s->format_tag;
    f->sequence = s->next_sequence++;
    ev_memcpy(f->payload, &crc, 4);
    compute_frame_crc(f);
    return (int32_t)s->frame_count++;
}

int32_t crit168_serialize_eof(crit168_serializer_t *s) {
    if (!s) return -1;
    if (s->frame_count >= CRIT168_MAX_FRAMES) return -1;

    crit168_frame_t *f = &s->frames[s->frame_count];
    ev_memset(f, 0, sizeof(*f));
    f->version = s->version;
    f->type = CRIT168_TYPE_EOF;
    f->format_tag = s->format_tag;
    f->sequence = s->next_sequence++;
    compute_frame_crc(f);
    return (int32_t)s->frame_count++;
}

/* ===== Deserialization ===== */

bool crit168_deserialize_frame(const crit168_frame_t *frame,
                               crit168_endian_t source_endian,
                               crit168_endian_t target_endian,
                               uint8_t *out_payload, uint32_t *out_len) {
    if (!frame) return false;

    /* Verify CRC first */
    if (!crit168_verify_frame(frame)) return false;

    /* Copy payload */
    if (out_payload) {
        ev_memcpy(out_payload, frame->payload, CRIT168_PAYLOAD_SIZE);
    }
    if (out_len) {
        *out_len = CRIT168_PAYLOAD_SIZE;
    }

    /* Endianness translation for multi-byte fields would happen here
     * for format-specific payloads. For the generic frame, we just
     * pass through the payload bytes. */
    if (source_endian != target_endian && source_endian != CRIT168_ENDIAN_NATIVE) {
        /* Swap 16-bit values in payload at known offsets if needed */
        /* This is format-specific; the generic layer just flags it */
    }

    return true;
}

/* ===== Integrity ===== */

bool crit168_verify_frame(const crit168_frame_t *frame) {
    if (!frame) return false;
    uint8_t buf[20];
    ev_memset(buf, 0, sizeof(buf));
    buf[0] = frame->version;
    buf[1] = frame->type;
    ev_memcpy(&buf[2], &frame->format_tag, 2);
    ev_memcpy(&buf[4], &frame->sequence, 2);
    ev_memcpy(&buf[6], frame->payload, CRIT168_PAYLOAD_SIZE);
    uint8_t expected = crit168_crc8(buf, 20);
    return expected == frame->crc;
}

bool crit168_verify_sequence(const crit168_serializer_t *s) {
    if (!s) return false;
    uint16_t expected = 0;
    for (uint32_t i = 0; i < s->frame_count; i++) {
        if (s->frames[i].sequence != expected) return false;
        if (!crit168_verify_frame(&s->frames[i])) return false;
        expected++;
    }
    return true;
}

/* ===== Endianness Translation ===== */

void crit168_translate_frame(crit168_frame_t *frame,
                             crit168_endian_t from, crit168_endian_t to) {
    if (!frame) return;
    if (from == to || from == CRIT168_ENDIAN_NATIVE || to == CRIT168_ENDIAN_NATIVE)
        return;

    /* Swap multi-byte header fields */
    crit168_swap16(&frame->format_tag);
    crit168_swap16(&frame->sequence);

    /* Recompute CRC after translation */
    compute_frame_crc(frame);
}

/* ===== Queries ===== */

uint32_t crit168_get_total_payload(const crit168_serializer_t *s) {
    if (!s) return 0;
    uint32_t total = 0;
    for (uint32_t i = 0; i < s->frame_count; i++) {
        if (s->frames[i].type == CRIT168_TYPE_DATA)
            total += CRIT168_PAYLOAD_SIZE;
    }
    return total;
}

uint32_t crit168_count_by_type(const crit168_serializer_t *s,
                               crit168_frame_type_t type) {
    if (!s) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->frame_count; i++) {
        if (s->frames[i].type == type) count++;
    }
    return count;
}

/* ===== Name Functions ===== */

const char *crit168_frame_type_name(crit168_frame_type_t type) {
    switch (type) {
        case CRIT168_TYPE_HEADER:   return "header";
        case CRIT168_TYPE_DATA:     return "data";
        case CRIT168_TYPE_CHECKSUM: return "checksum";
        case CRIT168_TYPE_PADDING:  return "padding";
        case CRIT168_TYPE_EOF:      return "eof";
        default:                     return "unknown";
    }
}

const char *crit168_endian_name(crit168_endian_t endian) {
    switch (endian) {
        case CRIT168_ENDIAN_LITTLE: return "little";
        case CRIT168_ENDIAN_BIG:    return "big";
        case CRIT168_ENDIAN_NATIVE: return "native";
        default:                     return "unknown";
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * The 168-bit frame serialiser. crit168_os.o's `nm -u` is empty.
 *
 * THIS DECLARATION IS SCOPED TO THIS FILE ON PURPOSE. The sibling
 * crit168/crit_168_word.c carries U __muldc3 -- libgcc's double-complex
 * multiply, from a `double complex` that has not been reformulated in Q32.32 /
 * zphi yet. Nothing here roots that: the bring-up touches only the CRC and
 * registry entry points defined in this file, so the complex-arithmetic word
 * stays out of the image until it is rewritten.
 */
#include "zxv_decl.h"
static int zxvd_crit168_bringup(void) {
    static crit168_registry_t reg;
    static const uint8_t probe[4] = { 0x01u, 0x02u, 0x03u, 0x04u };
    uint8_t  c8;
    uint32_t c32;

    crit168_registry_init(&reg);
    /* A checksum that ignores its input is the classic silent failure: verify
     * that two different messages do not produce the same value. */
    c8  = crit168_crc8(probe, sizeof probe);
    c32 = crit168_crc32(probe, sizeof probe);
    if (c8  == crit168_crc8(probe, sizeof probe - 1u))  return -1;
    if (c32 == crit168_crc32(probe, sizeof probe - 1u)) return -1;
    return 0;
}

ZXV_DECLARE(crit168_os,
    ZXV_PROVIDES(crit168_frames_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_crit168_bringup));

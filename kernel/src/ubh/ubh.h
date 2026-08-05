/* ubh.h — Universal Binary Harmonization Matrix
 *
 * UBH-168 canonical ZXV framed envelope and multi-format registry.
 *
 * UBH-168: 168 bits = 21 octets = 24 septets = 28 sextets.
 * The three views are reversible bit slicings only after bit order, unit order,
 * padding, canonicalization, and endianness are defined.
 *
 * UBH-7: experimental adversarial analysis profile — never privileged executable.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef UBH_H
#define UBH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== UBH-168 Constants ===== */

#define UBH_168_BITS      168
#define UBH_168_OCTETS    21    /* 168 / 8 */
#define UBH_168_SEPTETS   24    /* 168 / 7 */
#define UBH_168_SEXTETS   28    /* 168 / 6 */

#define UBH_MAGIC         0x5A5856  /* "ZXV" */

/* ===== UBH Frame Classes ===== */

typedef enum {
    UBH_FRAME_FORMAT_IDENTITY     = 0,
    UBH_FRAME_CAUSAL_EVENT        = 1,
    UBH_FRAME_SCHEMA_TYPE         = 2,
    UBH_FRAME_CONTENT_OBJECT      = 3,
    UBH_FRAME_CAPABILITY_POLICY   = 4,
    UBH_FRAME_TRANSLATION_PROV    = 5,
    UBH_FRAME_INTEGRITY           = 6,
    UBH_FRAME_FRAGMENT_COORD      = 7,
} ubh_frame_class_t;

/* ===== UBH Bit/Unit Order ===== */

typedef enum {
    UBH_ORDER_MSB_FIRST   = 0,  /* most significant bit first */
    UBH_ORDER_LSB_FIRST   = 1,  /* least significant bit first */
} ubh_bit_order_t;

typedef enum {
    UBH_ENDIAN_LITTLE  = 0,
    UBH_ENDIAN_BIG     = 1,
    UBH_ENDIAN_MIXED   = 2,
} ubh_endian_t;

/* ===== UBH-168 Frame Header ===== */
/* Compact header proposal — fits within the 21-octet envelope. */

typedef struct ubh_168_header {
    /* Octets 0-2: magic (3 bytes) */
    uint8_t magic[3];

    /* Octet 3: version + frame class */
    uint8_t version;         /* high nibble */
    uint8_t frame_class;     /* low nibble (ubh_frame_class_t) */

    /* Octets 4-5: source format ID (compact) */
    uint16_t source_format_id;

    /* Octets 6-7: target/content type */
    uint16_t target_type;

    /* Octet 8: byte/bit/unit order flags */
    uint8_t order_flags;     /* bits 0-1: endian, bit 2: bit_order, bits 3-4: unit_width */

    /* Octet 9: flags */
    uint8_t flags;           /* bit 0: has_payload, bit 1: is_fragment, bit 2: is_canonical */

    /* Octets 10-13: payload length or fragment metadata (32-bit) */
    uint32_t payload_length;

    /* Octets 14-17: schema/dictionary ID (32-bit) */
    uint32_t schema_id;

    /* Octets 18-20: integrity reference (3 bytes — not a full hash) */
    uint8_t integrity_ref[3];
} ubh_168_header_t;

/* Compile-time check: header must be exactly 21 octets */
/* magic(3) + version(1) + frame_class(1) + source(2) + target(2) +
   order(1) + flags(1) + payload(4) + schema(4) + integrity(3) = 22...
   We pack version+frame_class into one octet, so:
   magic(3) + ver_cls(1) + source(2) + target(2) + order(1) + flags(1) +
   payload(4) + schema(4) + integrity(3) = 21 */

/* ===== Format Plane Identity ===== */

#define UBH_MAX_FORMAT_NAME   32
#define UBH_MAX_FORMATS       64

typedef enum {
    UBH_FMT_UNKNOWN     = 0,
    UBH_FMT_UTF8        = 1,
    UBH_FMT_UTF16LE     = 2,
    UBH_FMT_UTF16BE     = 3,
    UBH_FMT_ASCII       = 4,
    UBH_FMT_ELF         = 5,
    UBH_FMT_PE_COFF     = 6,
    UBH_FMT_WASM        = 7,
    UBH_FMT_MACH_O      = 8,
    UBH_FMT_ZEDC        = 9,   /* ZXV positive container */
    UBH_FMT_CEDEZ       = 10,  /* ZXV negative container */
    UBH_FMT_CEDEC       = 11,  /* ZXV neutral container */
    UBH_FMT_ZIP         = 12,
    UBH_FMT_TAR         = 13,
    UBH_FMT_UBH_168     = 14,
    UBH_FMT_UBH_7       = 15,
    UBH_FMT_EBCDIC      = 16,
    UBH_FMT_UEFI_PE     = 17,
    UBH_FMT_DEVICE_TREE = 18,
    UBH_FMT_ACPI        = 19,
    UBH_FMT_FIRMWARE    = 20,
} ubh_format_id_t;

typedef enum {
    UBH_TRUST_UNKNOWN   = 0,
    UBH_TRUST_SIGNED    = 1,
    UBH_TRUST_REVIEWED  = 2,
    UBH_TRUST_PRODUCTION = 3,
    UBH_TRUST_EXPERIMENTAL = 4,
    UBH_TRUST_QUARANTINED = 5,
} ubh_trust_level_t;

typedef struct ubh_format_plane {
    uint16_t id;
    char name[UBH_MAX_FORMAT_NAME];
    uint16_t version;
    uint8_t architecture;     /* 0=generic, 1=x86_64, 2=arm64, 3=riscv, 4=wasm */
    ubh_endian_t endianness;
    uint8_t word_width;       /* 0=variable, 7, 8, 16, 32, 64, 128, 168 */
    ubh_trust_level_t trust;
    bool is_executable;
    bool is_text;
    bool is_container;
    bool active;
} ubh_format_plane_t;

/* ===== Format Registry ===== */

typedef struct ubh_format_registry {
    ubh_format_plane_t formats[UBH_MAX_FORMATS];
    uint32_t count;
} ubh_format_registry_t;

/* ===== UBH-7 Anomaly Operators ===== */

typedef enum {
    UBH7_OP_NONE                = 0,
    UBH7_OP_STRIP_HIGH_BIT      = 1,
    UBH7_OP_FLIP_SEPTET_ORDER   = 2,
    UBH7_OP_SHIFT_PACK_BOUNDARY = 3,
    UBH7_OP_INJECT_SENTINEL     = 4,
    UBH7_OP_TRUNCATE_FINAL      = 5,
    UBH7_OP_DUP_ALIAS           = 6,
    UBH7_OP_VERSION_DIFF        = 7,
    UBH7_OP_TAINT_AMBIGUOUS     = 8,
    UBH7_OP_CANARY_FRAME        = 9,
    UBH7_OP_RESYNCHRONIZE       = 10,
} ubh7_operator_t;

typedef enum {
    UBH7_PROFILE_DECODE_ONLY       = 0,
    UBH7_PROFILE_DIFFERENTIAL_LAB  = 1,
    UBH7_PROFILE_SYMBOLIC_LAB      = 2,
    UBH7_PROFILE_PRODUCTION        = 3,  /* prohibited until spec review */
} ubh7_profile_t;

/* ===== API ===== */

void ubh_registry_init(ubh_format_registry_t *reg);
int32_t ubh_registry_register(ubh_format_registry_t *reg,
                              const char *name, uint16_t version,
                              uint8_t arch, ubh_endian_t endian,
                              uint8_t word_width, ubh_trust_level_t trust,
                              bool is_exec, bool is_text, bool is_container);
ubh_format_plane_t *ubh_registry_get(ubh_format_registry_t *reg, uint16_t id);
ubh_format_plane_t *ubh_registry_find(ubh_format_registry_t *reg, const char *name);

/* Register launch formats */
void ubh_registry_register_launch(ubh_format_registry_t *reg);

/* ===== UBH-168 Frame Operations ===== */

void ubh_168_header_init(ubh_168_header_t *hdr, ubh_frame_class_t cls);
bool ubh_168_header_validate(const ubh_168_header_t *hdr);
void ubh_168_header_pack(const ubh_168_header_t *hdr, uint8_t *out21);
bool ubh_168_header_unpack(ubh_168_header_t *hdr, const uint8_t *in21);

/* ===== Bit Slicing ===== */
/* Convert between octet, septet, and sextet views of 168 bits. */

bool ubh_octets_to_septets(const uint8_t *octets21, uint8_t *septets24);
bool ubh_septets_to_octets(const uint8_t *septets24, uint8_t *octets21);
bool ubh_octets_to_sextets(const uint8_t *octets21, uint8_t *sextets28);
bool ubh_sextets_to_octets(const uint8_t *sextets28, uint8_t *octets21);

/* Round-trip verification */
bool ubh_168_roundtrip_test(const uint8_t *octets21);

/* ===== Format Detection ===== */
/* Detect format from magic bytes (not filename). Returns format_id or UNKNOWN. */

ubh_format_id_t ubh_detect_format(const uint8_t *data, uint32_t len);

/* ===== Name Functions ===== */

const char *ubh_frame_class_name(ubh_frame_class_t cls);
const char *ubh_format_name(ubh_format_id_t id);
const char *ubh_trust_name(ubh_trust_level_t trust);
const char *ubh7_operator_name(ubh7_operator_t op);
const char *ubh7_profile_name(ubh7_profile_t profile);

#endif /* UBH_H */

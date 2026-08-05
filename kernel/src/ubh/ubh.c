/* ubh.c — Universal Binary Harmonization Matrix
 *
 * Implements UBH-168 frame codec, bit slicing, format registry, and detection.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include "ubh.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static int str_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (a[i] != b[i]) return 0;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* ===== Format Registry ===== */

void ubh_registry_init(ubh_format_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
}

int32_t ubh_registry_register(ubh_format_registry_t *reg,
                              const char *name, uint16_t version,
                              uint8_t arch, ubh_endian_t endian,
                              uint8_t word_width, ubh_trust_level_t trust,
                              bool is_exec, bool is_text, bool is_container) {
    if (!reg || !name) return -1;
    if (reg->count >= UBH_MAX_FORMATS) return -1;
    ubh_format_plane_t *p = &reg->formats[reg->count];
    ev_memset(p, 0, sizeof(*p));
    p->id = (uint16_t)reg->count;
    copy_str(p->name, name, UBH_MAX_FORMAT_NAME);
    p->version = version;
    p->architecture = arch;
    p->endianness = endian;
    p->word_width = word_width;
    p->trust = trust;
    p->is_executable = is_exec;
    p->is_text = is_text;
    p->is_container = is_container;
    p->active = true;
    return (int32_t)reg->count++;
}

ubh_format_plane_t *ubh_registry_get(ubh_format_registry_t *reg, uint16_t id) {
    if (!reg || id >= reg->count) return NULL;
    return &reg->formats[id];
}

ubh_format_plane_t *ubh_registry_find(ubh_format_registry_t *reg, const char *name) {
    if (!reg || !name) return NULL;
    for (uint32_t i = 0; i < reg->count; i++) {
        if (reg->formats[i].active && str_eq(reg->formats[i].name, name))
            return &reg->formats[i];
    }
    return NULL;
}

void ubh_registry_register_launch(ubh_format_registry_t *reg) {
    if (!reg) return;
    /* Text encodings */
    ubh_registry_register(reg, "UTF-8", 1, 0, UBH_ENDIAN_BIG, 8,
                          UBH_TRUST_PRODUCTION, false, true, false);
    ubh_registry_register(reg, "UTF-16LE", 1, 0, UBH_ENDIAN_LITTLE, 16,
                          UBH_TRUST_PRODUCTION, false, true, false);
    ubh_registry_register(reg, "UTF-16BE", 1, 0, UBH_ENDIAN_BIG, 16,
                          UBH_TRUST_PRODUCTION, false, true, false);
    ubh_registry_register(reg, "ASCII", 1, 0, UBH_ENDIAN_BIG, 7,
                          UBH_TRUST_PRODUCTION, false, true, false);
    ubh_registry_register(reg, "EBCDIC", 1, 0, UBH_ENDIAN_BIG, 8,
                          UBH_TRUST_REVIEWED, false, true, false);
    /* Executable formats */
    ubh_registry_register(reg, "ELF", 1, 1, UBH_ENDIAN_LITTLE, 64,
                          UBH_TRUST_SIGNED, true, false, false);
    ubh_registry_register(reg, "PE-COFF", 1, 1, UBH_ENDIAN_LITTLE, 64,
                          UBH_TRUST_SIGNED, true, false, false);
    ubh_registry_register(reg, "WebAssembly", 1, 4, UBH_ENDIAN_LITTLE, 0,
                          UBH_TRUST_REVIEWED, true, false, false);
    /* ZXV containers */
    ubh_registry_register(reg, ".zedec", 1, 0, UBH_ENDIAN_BIG, 168,
                          UBH_TRUST_SIGNED, true, false, true);
    ubh_registry_register(reg, ".cedez", 1, 0, UBH_ENDIAN_BIG, 168,
                          UBH_TRUST_SIGNED, false, false, true);
    ubh_registry_register(reg, ".cedec", 1, 0, UBH_ENDIAN_BIG, 168,
                          UBH_TRUST_REVIEWED, false, false, true);
    /* Archives */
    ubh_registry_register(reg, "ZIP", 1, 0, UBH_ENDIAN_LITTLE, 0,
                          UBH_TRUST_REVIEWED, false, false, true);
    ubh_registry_register(reg, "TAR", 1, 0, UBH_ENDIAN_BIG, 0,
                          UBH_TRUST_REVIEWED, false, false, true);
    /* UBH */
    ubh_registry_register(reg, "UBH-168", 1, 0, UBH_ENDIAN_BIG, 168,
                          UBH_TRUST_PRODUCTION, false, false, false);
    ubh_registry_register(reg, "UBH-7", 0, 0, UBH_ENDIAN_BIG, 7,
                          UBH_TRUST_EXPERIMENTAL, false, false, false);
    /* Firmware */
    ubh_registry_register(reg, "UEFI-PE", 1, 1, UBH_ENDIAN_LITTLE, 64,
                          UBH_TRUST_SIGNED, true, false, false);
    ubh_registry_register(reg, "DeviceTree", 1, 2, UBH_ENDIAN_BIG, 32,
                          UBH_TRUST_REVIEWED, false, false, false);
    ubh_registry_register(reg, "ACPI", 1, 1, UBH_ENDIAN_LITTLE, 8,
                          UBH_TRUST_REVIEWED, false, false, false);
}

/* ===== UBH-168 Header Operations ===== */

void ubh_168_header_init(ubh_168_header_t *hdr, ubh_frame_class_t cls) {
    if (!hdr) return;
    ev_memset(hdr, 0, sizeof(*hdr));
    hdr->magic[0] = (UBH_MAGIC >> 16) & 0xFF;
    hdr->magic[1] = (UBH_MAGIC >> 8) & 0xFF;
    hdr->magic[2] = UBH_MAGIC & 0xFF;
    hdr->version = 1;
    hdr->frame_class = (uint8_t)cls;
    hdr->order_flags = (UBH_ENDIAN_BIG << 0) | (UBH_ORDER_MSB_FIRST << 2);
    hdr->flags = 0x04;  /* is_canonical */
}

bool ubh_168_header_validate(const ubh_168_header_t *hdr) {
    if (!hdr) return false;
    if (hdr->magic[0] != ((UBH_MAGIC >> 16) & 0xFF)) return false;
    if (hdr->magic[1] != ((UBH_MAGIC >> 8) & 0xFF)) return false;
    if (hdr->magic[2] != (UBH_MAGIC & 0xFF)) return false;
    if (hdr->version == 0) return false;
    if (hdr->frame_class > UBH_FRAME_FRAGMENT_COORD) return false;
    return true;
}

void ubh_168_header_pack(const ubh_168_header_t *hdr, uint8_t *out21) {
    if (!hdr || !out21) return;
    uint32_t pos = 0;
    /* Magic (3 bytes) */
    out21[pos++] = hdr->magic[0];
    out21[pos++] = hdr->magic[1];
    out21[pos++] = hdr->magic[2];
    /* Version + frame_class packed (1 byte) */
    out21[pos++] = (uint8_t)((hdr->version << 4) | (hdr->frame_class & 0x0F));
    /* Source format ID (2 bytes, big-endian) */
    out21[pos++] = (uint8_t)(hdr->source_format_id >> 8);
    out21[pos++] = (uint8_t)(hdr->source_format_id & 0xFF);
    /* Target type (2 bytes, big-endian) */
    out21[pos++] = (uint8_t)(hdr->target_type >> 8);
    out21[pos++] = (uint8_t)(hdr->target_type & 0xFF);
    /* Order flags (1 byte) */
    out21[pos++] = hdr->order_flags;
    /* Flags (1 byte) */
    out21[pos++] = hdr->flags;
    /* Payload length (4 bytes, big-endian) */
    out21[pos++] = (uint8_t)(hdr->payload_length >> 24);
    out21[pos++] = (uint8_t)(hdr->payload_length >> 16);
    out21[pos++] = (uint8_t)(hdr->payload_length >> 8);
    out21[pos++] = (uint8_t)(hdr->payload_length & 0xFF);
    /* Schema ID (4 bytes, big-endian) */
    out21[pos++] = (uint8_t)(hdr->schema_id >> 24);
    out21[pos++] = (uint8_t)(hdr->schema_id >> 16);
    out21[pos++] = (uint8_t)(hdr->schema_id >> 8);
    out21[pos++] = (uint8_t)(hdr->schema_id & 0xFF);
    /* Integrity ref (3 bytes) */
    out21[pos++] = hdr->integrity_ref[0];
    out21[pos++] = hdr->integrity_ref[1];
    out21[pos++] = hdr->integrity_ref[2];
    /* Total: 3+1+2+2+1+1+4+4+3 = 21 */
}

bool ubh_168_header_unpack(ubh_168_header_t *hdr, const uint8_t *in21) {
    if (!hdr || !in21) return false;
    uint32_t pos = 0;
    hdr->magic[0] = in21[pos++];
    hdr->magic[1] = in21[pos++];
    hdr->magic[2] = in21[pos++];
    uint8_t vc = in21[pos++];
    hdr->version = (vc >> 4) & 0x0F;
    hdr->frame_class = vc & 0x0F;
    uint8_t sf_hi = in21[pos++];
    uint8_t sf_lo = in21[pos++];
    hdr->source_format_id = ((uint16_t)sf_hi << 8) | sf_lo;
    uint8_t tt_hi = in21[pos++];
    uint8_t tt_lo = in21[pos++];
    hdr->target_type = ((uint16_t)tt_hi << 8) | tt_lo;
    hdr->order_flags = in21[pos++];
    hdr->flags = in21[pos++];
    uint8_t pl0 = in21[pos++];
    uint8_t pl1 = in21[pos++];
    uint8_t pl2 = in21[pos++];
    uint8_t pl3 = in21[pos++];
    hdr->payload_length = ((uint32_t)pl0 << 24) | ((uint32_t)pl1 << 16) |
                          ((uint32_t)pl2 << 8) | pl3;
    uint8_t sc0 = in21[pos++];
    uint8_t sc1 = in21[pos++];
    uint8_t sc2 = in21[pos++];
    uint8_t sc3 = in21[pos++];
    hdr->schema_id = ((uint32_t)sc0 << 24) | ((uint32_t)sc1 << 16) |
                     ((uint32_t)sc2 << 8) | sc3;
    hdr->integrity_ref[0] = in21[pos++];
    hdr->integrity_ref[1] = in21[pos++];
    hdr->integrity_ref[2] = in21[pos++];
    return ubh_168_header_validate(hdr);
}

/* ===== Bit Slicing ===== */
/* Pack 21 octets (168 bits) into 24 septets (7-bit units). */

bool ubh_octets_to_septets(const uint8_t *octets21, uint8_t *septets24) {
    if (!octets21 || !septets24) return false;

    /* Bit-by-bit extraction: collect 168 bits MSB-first, then repack into 7-bit units. */
    uint8_t bits[168];
    for (uint32_t i = 0; i < 21; i++) {
        for (uint32_t b = 0; b < 8; b++) {
            bits[i * 8 + b] = (octets21[i] >> (7 - b)) & 1;
        }
    }
    for (uint32_t i = 0; i < 24; i++) {
        uint8_t val = 0;
        for (uint32_t b = 0; b < 7; b++) {
            val = (val << 1) | bits[i * 7 + b];
        }
        septets24[i] = val;
    }
    return true;
}

bool ubh_septets_to_octets(const uint8_t *septets24, uint8_t *octets21) {
    if (!septets24 || !octets21) return false;

    uint8_t bits[168];
    for (uint32_t i = 0; i < 24; i++) {
        for (uint32_t b = 0; b < 7; b++) {
            bits[i * 7 + b] = (septets24[i] >> (6 - b)) & 1;
        }
    }
    for (uint32_t i = 0; i < 21; i++) {
        uint8_t val = 0;
        for (uint32_t b = 0; b < 8; b++) {
            val = (val << 1) | bits[i * 8 + b];
        }
        octets21[i] = val;
    }
    return true;
}

/* Pack 21 octets (168 bits) into 28 sextets (6-bit units). */

bool ubh_octets_to_sextets(const uint8_t *octets21, uint8_t *sextets28) {
    if (!octets21 || !sextets28) return false;

    uint8_t bits[168];
    for (uint32_t i = 0; i < 21; i++) {
        for (uint32_t b = 0; b < 8; b++) {
            bits[i * 8 + b] = (octets21[i] >> (7 - b)) & 1;
        }
    }
    for (uint32_t i = 0; i < 28; i++) {
        uint8_t val = 0;
        for (uint32_t b = 0; b < 6; b++) {
            val = (val << 1) | bits[i * 6 + b];
        }
        sextets28[i] = val;
    }
    return true;
}

bool ubh_sextets_to_octets(const uint8_t *sextets28, uint8_t *octets21) {
    if (!sextets28 || !octets21) return false;

    uint8_t bits[168];
    for (uint32_t i = 0; i < 28; i++) {
        for (uint32_t b = 0; b < 6; b++) {
            bits[i * 6 + b] = (sextets28[i] >> (5 - b)) & 1;
        }
    }
    for (uint32_t i = 0; i < 21; i++) {
        uint8_t val = 0;
        for (uint32_t b = 0; b < 8; b++) {
            val = (val << 1) | bits[i * 8 + b];
        }
        octets21[i] = val;
    }
    return true;
}

bool ubh_168_roundtrip_test(const uint8_t *octets21) {
    if (!octets21) return false;
    uint8_t septets[UBH_168_SEPTETS];
    uint8_t sextets[UBH_168_SEXTETS];
    uint8_t back[UBH_168_OCTETS];

    /* Octet → Septet → Octet */
    if (!ubh_octets_to_septets(octets21, septets)) return false;
    if (!ubh_septets_to_octets(septets, back)) return false;
    for (uint32_t i = 0; i < UBH_168_OCTETS; i++) {
        if (back[i] != octets21[i]) return false;
    }

    /* Octet → Sextet → Octet */
    if (!ubh_octets_to_sextets(octets21, sextets)) return false;
    if (!ubh_sextets_to_octets(sextets, back)) return false;
    for (uint32_t i = 0; i < UBH_168_OCTETS; i++) {
        if (back[i] != octets21[i]) return false;
    }

    return true;
}

/* ===== Format Detection ===== */

ubh_format_id_t ubh_detect_format(const uint8_t *data, uint32_t len) {
    if (!data || len < 4) return UBH_FMT_UNKNOWN;

    /* ELF: 0x7F 'E' 'L' 'F' */
    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F')
        return UBH_FMT_ELF;

    /* PE/COFF: "MZ" */
    if (data[0] == 'M' && data[1] == 'Z')
        return UBH_FMT_PE_COFF;

    /* WebAssembly: 0x00 0x61 0x73 0x6D ("\0asm") */
    if (data[0] == 0x00 && data[1] == 0x61 && data[2] == 0x73 && data[3] == 0x6D)
        return UBH_FMT_WASM;

    /* ZIP: PK\x03\x04 */
    if (data[0] == 'P' && data[1] == 'K' && data[2] == 0x03 && data[3] == 0x04)
        return UBH_FMT_ZIP;

    /* Mach-O: 0xFEEDFACE/0xFEEDFACF (32/64-bit BE) or 0xCEFAEDFE (LE) */
    if ((data[0] == 0xFE && data[1] == 0xED && data[2] == 0xFA &&
         (data[3] == 0xCE || data[3] == 0xCF)) ||
        (data[0] == 0xCE && data[1] == 0xFA && data[2] == 0xED && data[3] == 0xFE))
        return UBH_FMT_MACH_O;

    /* TAR: "ustar" at offset 257 */
    if (len >= 262 && data[257] == 'u' && data[258] == 's' &&
        data[259] == 't' && data[260] == 'a' && data[261] == 'r')
        return UBH_FMT_TAR;

    /* UBH-168: "ZXV" magic */
    if (data[0] == 'Z' && data[1] == 'X' && data[2] == 'V')
        return UBH_FMT_UBH_168;

    /* UEFI PE also starts with MZ — distinguished by PE signature at offset 60+ */
    /* This is a simplification; real detection checks the PE pointer */

    /* UTF-8 BOM: EF BB BF */
    if (data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        return UBH_FMT_UTF8;

    /* UTF-16LE BOM: FF FE */
    if (data[0] == 0xFF && data[1] == 0xFE)
        return UBH_FMT_UTF16LE;

    /* UTF-16BE BOM: FE FF */
    if (data[0] == 0xFE && data[1] == 0xFF)
        return UBH_FMT_UTF16BE;

    /* Device Tree Blob: 0xD00DFEED magic */
    if (len >= 4 && data[0] == 0xD0 && data[1] == 0x0D &&
        data[2] == 0xFE && data[3] == 0xED)
        return UBH_FMT_DEVICE_TREE;

    /* ASCII heuristic: all bytes < 0x80 in first 16 bytes */
    if (len >= 16) {
        bool ascii = true;
        for (uint32_t i = 0; i < 16; i++) {
            if (data[i] > 0x7F) { ascii = false; break; }
        }
        if (ascii) return UBH_FMT_ASCII;
    }

    return UBH_FMT_UNKNOWN;
}

/* ===== Name Functions ===== */

const char *ubh_frame_class_name(ubh_frame_class_t cls) {
    switch (cls) {
        case UBH_FRAME_FORMAT_IDENTITY:   return "format_identity";
        case UBH_FRAME_CAUSAL_EVENT:      return "causal_event";
        case UBH_FRAME_SCHEMA_TYPE:       return "schema_type";
        case UBH_FRAME_CONTENT_OBJECT:    return "content_object";
        case UBH_FRAME_CAPABILITY_POLICY: return "capability_policy";
        case UBH_FRAME_TRANSLATION_PROV:  return "translation_provenance";
        case UBH_FRAME_INTEGRITY:         return "integrity";
        case UBH_FRAME_FRAGMENT_COORD:    return "fragment_coordinate";
        default:                           return "unknown";
    }
}

const char *ubh_format_name(ubh_format_id_t id) {
    switch (id) {
        case UBH_FMT_UNKNOWN:     return "unknown";
        case UBH_FMT_UTF8:        return "UTF-8";
        case UBH_FMT_UTF16LE:     return "UTF-16LE";
        case UBH_FMT_UTF16BE:     return "UTF-16BE";
        case UBH_FMT_ASCII:       return "ASCII";
        case UBH_FMT_ELF:         return "ELF";
        case UBH_FMT_PE_COFF:     return "PE-COFF";
        case UBH_FMT_WASM:        return "WebAssembly";
        case UBH_FMT_MACH_O:      return "Mach-O";
        case UBH_FMT_ZEDC:        return ".zedec";
        case UBH_FMT_CEDEZ:       return ".cedez";
        case UBH_FMT_CEDEC:       return ".cedec";
        case UBH_FMT_ZIP:         return "ZIP";
        case UBH_FMT_TAR:         return "TAR";
        case UBH_FMT_UBH_168:     return "UBH-168";
        case UBH_FMT_UBH_7:       return "UBH-7";
        case UBH_FMT_EBCDIC:      return "EBCDIC";
        case UBH_FMT_UEFI_PE:     return "UEFI-PE";
        case UBH_FMT_DEVICE_TREE: return "DeviceTree";
        case UBH_FMT_ACPI:        return "ACPI";
        case UBH_FMT_FIRMWARE:    return "firmware";
        default:                   return "unknown";
    }
}

const char *ubh_trust_name(ubh_trust_level_t trust) {
    switch (trust) {
        case UBH_TRUST_UNKNOWN:      return "unknown";
        case UBH_TRUST_SIGNED:       return "signed";
        case UBH_TRUST_REVIEWED:     return "reviewed";
        case UBH_TRUST_PRODUCTION:   return "production";
        case UBH_TRUST_EXPERIMENTAL: return "experimental";
        case UBH_TRUST_QUARANTINED:  return "quarantined";
        default:                      return "unknown";
    }
}

const char *ubh7_operator_name(ubh7_operator_t op) {
    switch (op) {
        case UBH7_OP_NONE:                return "none";
        case UBH7_OP_STRIP_HIGH_BIT:      return "strip_high_bit";
        case UBH7_OP_FLIP_SEPTET_ORDER:   return "flip_septet_order";
        case UBH7_OP_SHIFT_PACK_BOUNDARY: return "shift_pack_boundary";
        case UBH7_OP_INJECT_SENTINEL:     return "inject_sentinel";
        case UBH7_OP_TRUNCATE_FINAL:      return "truncate_final";
        case UBH7_OP_DUP_ALIAS:           return "duplicate_alias";
        case UBH7_OP_VERSION_DIFF:        return "version_differential";
        case UBH7_OP_TAINT_AMBIGUOUS:     return "taint_ambiguous";
        case UBH7_OP_CANARY_FRAME:        return "canary_frame";
        case UBH7_OP_RESYNCHRONIZE:       return "resynchronize";
        default:                           return "unknown";
    }
}

const char *ubh7_profile_name(ubh7_profile_t profile) {
    switch (profile) {
        case UBH7_PROFILE_DECODE_ONLY:      return "decode_only";
        case UBH7_PROFILE_DIFFERENTIAL_LAB: return "differential_lab";
        case UBH7_PROFILE_SYMBOLIC_LAB:     return "symbolic_operator_lab";
        case UBH7_PROFILE_PRODUCTION:       return "production_prohibited";
        default:                             return "unknown";
    }
}

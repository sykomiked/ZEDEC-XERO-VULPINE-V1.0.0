/* test_ubh.c — Universal Binary Harmonization Matrix Tests
 *
 * Tests for UBH-168 frame codec, bit slicing, format registry, and detection.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ubh.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  [TEST] %s ... ", #name); \
    name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define PASS() return

/* ===== Registry Tests ===== */

TEST(registry_init_test) {
    ubh_format_registry_t reg;
    ubh_registry_init(&reg);
    ASSERT(reg.count == 0, "count is 0");
    PASS();
}

TEST(registry_register_test) {
    ubh_format_registry_t reg;
    ubh_registry_init(&reg);
    int32_t idx = ubh_registry_register(&reg, "test-fmt", 1, 0,
                                        UBH_ENDIAN_LITTLE, 32,
                                        UBH_TRUST_REVIEWED, true, false, false);
    ASSERT(idx >= 0, "format registered");
    ASSERT(idx == 0, "first format is index 0");

    ubh_format_plane_t *p = ubh_registry_get(&reg, 0);
    ASSERT(p != NULL, "format retrieved");
    ASSERT(strcmp(p->name, "test-fmt") == 0, "name matches");
    ASSERT(p->is_executable, "is executable");
    ASSERT(!p->is_text, "not text");
    ASSERT(p->active, "is active");
    PASS();
}

TEST(registry_launch_formats_test) {
    ubh_format_registry_t reg;
    ubh_registry_init(&reg);
    ubh_registry_register_launch(&reg);
    ASSERT(reg.count >= 15, "at least 15 launch formats registered");

    ubh_format_plane_t *utf8 = ubh_registry_find(&reg, "UTF-8");
    ASSERT(utf8 != NULL, "UTF-8 found");
    ASSERT(utf8->is_text, "UTF-8 is text");
    ASSERT(!utf8->is_executable, "UTF-8 is not executable");
    ASSERT(utf8->trust == UBH_TRUST_PRODUCTION, "UTF-8 is production trust");

    ubh_format_plane_t *elf = ubh_registry_find(&reg, "ELF");
    ASSERT(elf != NULL, "ELF found");
    ASSERT(elf->is_executable, "ELF is executable");
    ASSERT(elf->trust == UBH_TRUST_SIGNED, "ELF is signed trust");

    ubh_format_plane_t *ubh7 = ubh_registry_find(&reg, "UBH-7");
    ASSERT(ubh7 != NULL, "UBH-7 found");
    ASSERT(ubh7->trust == UBH_TRUST_EXPERIMENTAL, "UBH-7 is experimental");
    ASSERT(!ubh7->is_executable, "UBH-7 is not executable");

    ubh_format_plane_t *zedc = ubh_registry_find(&reg, ".zedec");
    ASSERT(zedc != NULL, ".zedec found");
    ASSERT(zedc->is_container, ".zedec is container");
    ASSERT(zedc->word_width == 168, ".zedec word width is 168");
    PASS();
}

TEST(registry_find_missing_test) {
    ubh_format_registry_t reg;
    ubh_registry_init(&reg);
    ubh_registry_register_launch(&reg);
    ubh_format_plane_t *p = ubh_registry_find(&reg, "nonexistent");
    ASSERT(p == NULL, "nonexistent format not found");
    PASS();
}

/* ===== UBH-168 Header Tests ===== */

TEST(header_init_test) {
    ubh_168_header_t hdr;
    ubh_168_header_init(&hdr, UBH_FRAME_FORMAT_IDENTITY);
    ASSERT(hdr.magic[0] == 'Z', "magic[0] is Z");
    ASSERT(hdr.magic[1] == 'X', "magic[1] is X");
    ASSERT(hdr.magic[2] == 'V', "magic[2] is V");
    ASSERT(hdr.version == 1, "version is 1");
    ASSERT(hdr.frame_class == UBH_FRAME_FORMAT_IDENTITY, "frame class is format_identity");
    ASSERT(hdr.flags & 0x04, "is_canonical flag set");
    PASS();
}

TEST(header_validate_test) {
    ubh_168_header_t hdr;
    ubh_168_header_init(&hdr, UBH_FRAME_CAUSAL_EVENT);
    ASSERT(ubh_168_header_validate(&hdr), "valid header passes");

    /* Corrupt magic */
    hdr.magic[0] = 'X';
    ASSERT(!ubh_168_header_validate(&hdr), "corrupted magic rejected");

    /* Restore and corrupt version */
    ubh_168_header_init(&hdr, UBH_FRAME_CAUSAL_EVENT);
    hdr.version = 0;
    ASSERT(!ubh_168_header_validate(&hdr), "zero version rejected");

    /* Restore and corrupt frame class */
    ubh_168_header_init(&hdr, UBH_FRAME_CAUSAL_EVENT);
    hdr.frame_class = 99;
    ASSERT(!ubh_168_header_validate(&hdr), "invalid frame class rejected");
    PASS();
}

TEST(header_pack_unpack_test) {
    ubh_168_header_t hdr;
    ubh_168_header_init(&hdr, UBH_FRAME_CONTENT_OBJECT);
    hdr.source_format_id = 0x1234;
    hdr.target_type = 0x5678;
    hdr.payload_length = 0xDEADBEEF;
    hdr.schema_id = 0xCAFEBABE;
    hdr.integrity_ref[0] = 0xAA;
    hdr.integrity_ref[1] = 0xBB;
    hdr.integrity_ref[2] = 0xCC;

    uint8_t buf[UBH_168_OCTETS];
    ubh_168_header_pack(&hdr, buf);

    ubh_168_header_t hdr2;
    ASSERT(ubh_168_header_unpack(&hdr2, buf), "unpack succeeds");
    ASSERT(hdr2.magic[0] == 'Z', "magic[0] matches");
    ASSERT(hdr2.version == 1, "version matches");
    ASSERT(hdr2.frame_class == UBH_FRAME_CONTENT_OBJECT, "frame class matches");
    ASSERT(hdr2.source_format_id == 0x1234, "source format matches");
    ASSERT(hdr2.target_type == 0x5678, "target type matches");
    ASSERT(hdr2.payload_length == 0xDEADBEEF, "payload length matches");
    ASSERT(hdr2.schema_id == 0xCAFEBABE, "schema id matches");
    ASSERT(hdr2.integrity_ref[0] == 0xAA, "integrity[0] matches");
    ASSERT(hdr2.integrity_ref[1] == 0xBB, "integrity[1] matches");
    ASSERT(hdr2.integrity_ref[2] == 0xCC, "integrity[2] matches");
    PASS();
}

/* ===== Bit Slicing Tests ===== */

TEST(octet_septet_roundtrip_test) {
    uint8_t octets[UBH_168_OCTETS];
    uint8_t septets[UBH_168_SEPTETS];
    uint8_t back[UBH_168_OCTETS];

    /* Fill with known pattern */
    for (uint32_t i = 0; i < UBH_168_OCTETS; i++)
        octets[i] = (uint8_t)(i * 11 + 3);

    ASSERT(ubh_octets_to_septets(octets, septets), "octet→septet succeeds");
    ASSERT(ubh_septets_to_octets(septets, back), "septet→octet succeeds");
    for (uint32_t i = 0; i < UBH_168_OCTETS; i++) {
        ASSERT(back[i] == octets[i], "septet roundtrip preserves data");
    }
    PASS();
}

TEST(octet_sextet_roundtrip_test) {
    uint8_t octets[UBH_168_OCTETS];
    uint8_t sextets[UBH_168_SEXTETS];
    uint8_t back[UBH_168_OCTETS];

    for (uint32_t i = 0; i < UBH_168_OCTETS; i++)
        octets[i] = (uint8_t)(0xAA ^ (i * 7));

    ASSERT(ubh_octets_to_sextets(octets, sextets), "octet→sextet succeeds");
    ASSERT(ubh_sextets_to_octets(sextets, back), "sextet→octet succeeds");
    for (uint32_t i = 0; i < UBH_168_OCTETS; i++) {
        ASSERT(back[i] == octets[i], "sextet roundtrip preserves data");
    }
    PASS();
}

TEST(full_roundtrip_test) {
    uint8_t octets[UBH_168_OCTETS];
    /* All 1s */
    memset(octets, 0xFF, UBH_168_OCTETS);
    ASSERT(ubh_168_roundtrip_test(octets), "all-1s roundtrip");

    /* All 0s */
    memset(octets, 0x00, UBH_168_OCTETS);
    ASSERT(ubh_168_roundtrip_test(octets), "all-0s roundtrip");

    /* Alternating */
    for (uint32_t i = 0; i < UBH_168_OCTETS; i++)
        octets[i] = (i % 2) ? 0xAA : 0x55;
    ASSERT(ubh_168_roundtrip_test(octets), "alternating pattern roundtrip");

    /* Sequential */
    for (uint32_t i = 0; i < UBH_168_OCTETS; i++)
        octets[i] = (uint8_t)i;
    ASSERT(ubh_168_roundtrip_test(octets), "sequential pattern roundtrip");
    PASS();
}

TEST(septet_count_test) {
    /* 168 bits / 7 = 24 septets exactly, no remainder */
    ASSERT(UBH_168_SEPTETS == 24, "24 septets");
    ASSERT(UBH_168_SEXTETS == 28, "28 sextets");
    ASSERT(UBH_168_OCTETS == 21, "21 octets");
    ASSERT(UBH_168_BITS == 168, "168 bits");
    ASSERT(UBH_168_OCTETS * 8 == UBH_168_BITS, "21*8=168");
    ASSERT(UBH_168_SEPTETS * 7 == UBH_168_BITS, "24*7=168");
    ASSERT(UBH_168_SEXTETS * 6 == UBH_168_BITS, "28*6=168");
    PASS();
}

/* ===== Format Detection Tests ===== */

TEST(detect_elf_test) {
    uint8_t data[] = {0x7F, 'E', 'L', 'F', 0x02, 0x01, 0x01, 0x00};
    ASSERT(ubh_detect_format(data, sizeof(data)) == UBH_FMT_ELF, "ELF detected");
    PASS();
}

TEST(detect_pe_test) {
    uint8_t data[] = {'M', 'Z', 0x90, 0x00, 0x03, 0x00, 0x00, 0x00};
    ASSERT(ubh_detect_format(data, sizeof(data)) == UBH_FMT_PE_COFF, "PE-COFF detected");
    PASS();
}

TEST(detect_wasm_test) {
    uint8_t data[] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    ASSERT(ubh_detect_format(data, sizeof(data)) == UBH_FMT_WASM, "WebAssembly detected");
    PASS();
}

TEST(detect_zip_test) {
    uint8_t data[] = {'P', 'K', 0x03, 0x04, 0x14, 0x00, 0x00, 0x00};
    ASSERT(ubh_detect_format(data, sizeof(data)) == UBH_FMT_ZIP, "ZIP detected");
    PASS();
}

TEST(detect_ubh168_test) {
    uint8_t data[] = {'Z', 'X', 'V', 0x01, 0x00, 0x00, 0x00, 0x00};
    ASSERT(ubh_detect_format(data, sizeof(data)) == UBH_FMT_UBH_168, "UBH-168 detected");
    PASS();
}

TEST(detect_utf8_bom_test) {
    uint8_t data[] = {0xEF, 0xBB, 0xBF, 0x23, 0x48, 0x65, 0x6C, 0x6C};
    ASSERT(ubh_detect_format(data, sizeof(data)) == UBH_FMT_UTF8, "UTF-8 BOM detected");
    PASS();
}

TEST(detect_utf16le_test) {
    uint8_t data[] = {0xFF, 0xFE, 0x41, 0x00, 0x42, 0x00, 0x43, 0x00};
    ASSERT(ubh_detect_format(data, sizeof(data)) == UBH_FMT_UTF16LE, "UTF-16LE detected");
    PASS();
}

TEST(detect_ascii_test) {
    uint8_t data[] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o',
                      'r', 'l', 'd', '!', '\n', '\t', ' ', ' '};
    ASSERT(ubh_detect_format(data, sizeof(data)) == UBH_FMT_ASCII, "ASCII detected");
    PASS();
}

TEST(detect_unknown_test) {
    uint8_t data[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    ASSERT(ubh_detect_format(data, sizeof(data)) == UBH_FMT_UNKNOWN, "PNG is unknown");
    PASS();
}

TEST(detect_short_data_test) {
    uint8_t data[] = {0x42};
    ASSERT(ubh_detect_format(data, 1) == UBH_FMT_UNKNOWN, "short data is unknown");
    ASSERT(ubh_detect_format(NULL, 0) == UBH_FMT_UNKNOWN, "null data is unknown");
    PASS();
}

/* ===== Name Function Tests ===== */

TEST(name_functions_test) {
    ASSERT(strcmp(ubh_frame_class_name(UBH_FRAME_FORMAT_IDENTITY), "format_identity") == 0, "frame class name");
    ASSERT(strcmp(ubh_format_name(UBH_FMT_ELF), "ELF") == 0, "format name");
    ASSERT(strcmp(ubh_trust_name(UBH_TRUST_PRODUCTION), "production") == 0, "trust name");
    ASSERT(strcmp(ubh7_operator_name(UBH7_OP_STRIP_HIGH_BIT), "strip_high_bit") == 0, "operator name");
    ASSERT(strcmp(ubh7_profile_name(UBH7_PROFILE_DECODE_ONLY), "decode_only") == 0, "profile name");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV UBH Matrix Tests ===\n\n");

    RUN(registry_init_test);
    RUN(registry_register_test);
    RUN(registry_launch_formats_test);
    RUN(registry_find_missing_test);
    RUN(header_init_test);
    RUN(header_validate_test);
    RUN(header_pack_unpack_test);
    RUN(octet_septet_roundtrip_test);
    RUN(octet_sextet_roundtrip_test);
    RUN(full_roundtrip_test);
    RUN(septet_count_test);
    RUN(detect_elf_test);
    RUN(detect_pe_test);
    RUN(detect_wasm_test);
    RUN(detect_zip_test);
    RUN(detect_ubh168_test);
    RUN(detect_utf8_bom_test);
    RUN(detect_utf16le_test);
    RUN(detect_ascii_test);
    RUN(detect_unknown_test);
    RUN(detect_short_data_test);
    RUN(name_functions_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}

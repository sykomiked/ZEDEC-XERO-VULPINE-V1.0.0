/* test_crit168_os.c — Canonical Representation, Integrity, Translation (O1) Tests
 *
 * Tests for 168-bit frame serialization, CRC integrity, endianness
 * translation, and format registry.
 *
 * Author: 36N9 Genetics, LLC
 * License: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "crit168_os.h"

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

/* ===== CRC Tests ===== */

TEST(crc8_test) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    uint8_t crc = crit168_crc8(data, 3);
    ASSERT(crc != 0, "CRC-8 is non-zero for non-zero input");

    /* Empty data */
    ASSERT(crit168_crc8(NULL, 0) == 0, "CRC-8 of null is 0");

    /* Known value: CRC-8 of "123456789" with poly 0x07 is 0xF4 */
    uint8_t test[] = {'1','2','3','4','5','6','7','8','9'};
    ASSERT(crit168_crc8(test, 9) == 0xF4, "CRC-8 check value is 0xF4");
    PASS();
}

TEST(crc32_test) {
    /* CRC-32 of "123456789" is 0xCBF43926 */
    uint8_t test[] = {'1','2','3','4','5','6','7','8','9'};
    uint32_t crc = crit168_crc32(test, 9);
    ASSERT(crc == 0xCBF43926, "CRC-32 check value is 0xCBF43926");
    PASS();
}

/* ===== Endianness Tests ===== */

TEST(swap16_test) {
    uint16_t val = 0x1234;
    crit168_swap16(&val);
    ASSERT(val == 0x3412, "swap16 works");
    crit168_swap16(&val);
    ASSERT(val == 0x1234, "double swap16 restores");
    PASS();
}

TEST(swap32_test) {
    uint32_t val = 0x12345678;
    crit168_swap32(&val);
    ASSERT(val == 0x78563412, "swap32 works");
    crit168_swap32(&val);
    ASSERT(val == 0x12345678, "double swap32 restores");
    PASS();
}

/* ===== Registry Tests ===== */

TEST(registry_init_test) {
    crit168_registry_t reg;
    crit168_registry_init(&reg);
    ASSERT(reg.format_count == 0, "no formats");
    PASS();
}

TEST(register_format_test) {
    crit168_registry_t reg;
    crit168_registry_init(&reg);
    int32_t idx = crit168_register_format(&reg, 0x0001, "storage.v1",
                                          1, CRIT168_ENDIAN_LITTLE, 256);
    ASSERT(idx >= 0, "format registered");
    ASSERT(idx == 0, "first format at index 0");

    crit168_format_t *f = crit168_get_format(&reg, 0x0001);
    ASSERT(f != NULL, "format found");
    ASSERT(strcmp(f->name, "storage.v1") == 0, "name matches");
    ASSERT(f->version == 1, "version is 1");
    ASSERT(f->endian == CRIT168_ENDIAN_LITTLE, "endian is little");
    ASSERT(f->max_frames == 256, "max_frames is 256");
    ASSERT(f->active, "is active");
    PASS();
}

TEST(duplicate_format_test) {
    crit168_registry_t reg;
    crit168_registry_init(&reg);
    int32_t idx1 = crit168_register_format(&reg, 0x0001, "a", 1, CRIT168_ENDIAN_LITTLE, 64);
    int32_t idx2 = crit168_register_format(&reg, 0x0001, "b", 2, CRIT168_ENDIAN_BIG, 128);
    ASSERT(idx1 == idx2, "duplicate tag returns same index");
    ASSERT(reg.format_count == 1, "count still 1");
    PASS();
}

TEST(get_nonexistent_format_test) {
    crit168_registry_t reg;
    crit168_registry_init(&reg);
    ASSERT(crit168_get_format(&reg, 0xFFFF) == NULL, "nonexistent returns NULL");
    PASS();
}

/* ===== Serializer Tests ===== */

TEST(serializer_init_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);
    ASSERT(s.frame_count == 0, "no frames");
    ASSERT(s.format_tag == 0x0001, "format tag set");
    ASSERT(s.version == 1, "version set");
    ASSERT(s.target_endian == CRIT168_ENDIAN_LITTLE, "endian set");
    ASSERT(s.next_sequence == 0, "sequence starts at 0");
    PASS();
}

TEST(serialize_header_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);
    int32_t idx = crit168_serialize_header(&s, 256);
    ASSERT(idx >= 0, "header serialized");
    ASSERT(s.frame_count == 1, "1 frame");

    crit168_frame_t *f = &s.frames[0];
    ASSERT(f->type == CRIT168_TYPE_HEADER, "type is header");
    ASSERT(f->version == 1, "version is 1");
    ASSERT(f->format_tag == 0x0001, "format tag set");
    ASSERT(f->sequence == 0, "sequence is 0");
    ASSERT(crit168_verify_frame(f), "frame CRC valid");
    PASS();
}

TEST(serialize_data_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);

    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    int32_t idx = crit168_serialize_data(&s, data, 6);
    ASSERT(idx >= 0, "data serialized");
    ASSERT(s.frame_count == 1, "1 frame for 6 bytes");

    crit168_frame_t *f = &s.frames[0];
    ASSERT(f->type == CRIT168_TYPE_DATA, "type is data");
    ASSERT(f->payload[0] == 0xDE, "payload[0] matches");
    ASSERT(f->payload[5] == 0xFE, "payload[5] matches");
    ASSERT(crit168_verify_frame(f), "frame CRC valid");
    PASS();
}

TEST(serialize_large_data_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);

    /* 26 bytes = 2 frames (13 + 13) */
    uint8_t data[26];
    for (int i = 0; i < 26; i++) data[i] = (uint8_t)(i + 1);
    int32_t idx = crit168_serialize_data(&s, data, 26);
    ASSERT(idx >= 0, "data serialized");
    ASSERT(s.frame_count == 2, "2 frames for 26 bytes");

    /* Verify first frame */
    ASSERT(s.frames[0].payload[0] == 1, "frame 0 payload[0] is 1");
    ASSERT(s.frames[0].payload[12] == 13, "frame 0 payload[12] is 13");

    /* Verify second frame */
    ASSERT(s.frames[1].payload[0] == 14, "frame 1 payload[0] is 14");
    ASSERT(s.frames[1].payload[12] == 26, "frame 1 payload[12] is 26");
    PASS();
}

TEST(serialize_checksum_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);
    crit168_serialize_header(&s, 100);

    uint8_t data[] = {0x01, 0x02, 0x03};
    crit168_serialize_data(&s, data, 3);

    int32_t idx = crit168_serialize_checksum(&s);
    ASSERT(idx >= 0, "checksum serialized");
    ASSERT(s.frames[idx].type == CRIT168_TYPE_CHECKSUM, "type is checksum");
    ASSERT(crit168_verify_frame(&s.frames[idx]), "checksum frame CRC valid");
    PASS();
}

TEST(serialize_eof_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);
    crit168_serialize_header(&s, 0);
    int32_t idx = crit168_serialize_eof(&s);
    ASSERT(idx >= 0, "EOF serialized");
    ASSERT(s.frames[idx].type == CRIT168_TYPE_EOF, "type is EOF");
    ASSERT(crit168_verify_frame(&s.frames[idx]), "EOF frame CRC valid");
    PASS();
}

TEST(full_serialization_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);

    /* Header */
    crit168_serialize_header(&s, 20);

    /* Data (20 bytes = 2 frames) */
    uint8_t data[20];
    for (int i = 0; i < 20; i++) data[i] = (uint8_t)(0xA0 + i);
    crit168_serialize_data(&s, data, 20);

    /* Checksum */
    crit168_serialize_checksum(&s);

    /* EOF */
    crit168_serialize_eof(&s);

    /* Should have 4 frames: header + 2 data + checksum + eof = 5 */
    ASSERT(s.frame_count == 5, "5 frames total");

    /* Verify all frames */
    ASSERT(crit168_verify_sequence(&s), "sequence valid");

    /* Verify frame types */
    ASSERT(s.frames[0].type == CRIT168_TYPE_HEADER, "frame 0 is header");
    ASSERT(s.frames[1].type == CRIT168_TYPE_DATA, "frame 1 is data");
    ASSERT(s.frames[2].type == CRIT168_TYPE_DATA, "frame 2 is data");
    ASSERT(s.frames[3].type == CRIT168_TYPE_CHECKSUM, "frame 3 is checksum");
    ASSERT(s.frames[4].type == CRIT168_TYPE_EOF, "frame 4 is EOF");

    /* Verify sequence numbers are contiguous */
    for (uint32_t i = 0; i < 5; i++) {
        ASSERT(s.frames[i].sequence == i, "contiguous sequence");
    }
    PASS();
}

/* ===== Integrity Tests ===== */

TEST(verify_frame_corrupt_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);
    crit168_serialize_header(&s, 100);

    /* Corrupt a payload byte */
    s.frames[0].payload[0] ^= 0xFF;
    ASSERT(!crit168_verify_frame(&s.frames[0]), "corrupted frame fails CRC");
    PASS();
}

TEST(verify_sequence_corrupt_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);
    crit168_serialize_header(&s, 100);
    crit168_serialize_eof(&s);

    /* Corrupt sequence number */
    s.frames[1].sequence = 99;
    ASSERT(!crit168_verify_sequence(&s), "corrupted sequence fails");
    PASS();
}

/* ===== Deserialization Tests ===== */

TEST(deserialize_frame_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);

    uint8_t data[] = {0xAA, 0xBB, 0xCC};
    crit168_serialize_data(&s, data, 3);

    uint8_t out_payload[CRIT168_PAYLOAD_SIZE];
    uint32_t out_len = 0;
    ASSERT(crit168_deserialize_frame(&s.frames[0], CRIT168_ENDIAN_NATIVE,
                                     CRIT168_ENDIAN_NATIVE,
                                     out_payload, &out_len),
           "deserialization succeeds");
    ASSERT(out_len == CRIT168_PAYLOAD_SIZE, "output length is 13");
    ASSERT(out_payload[0] == 0xAA, "payload[0] matches");
    ASSERT(out_payload[1] == 0xBB, "payload[1] matches");
    ASSERT(out_payload[2] == 0xCC, "payload[2] matches");
    PASS();
}

TEST(deserialize_corrupt_fails_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);
    crit168_serialize_header(&s, 100);

    /* Corrupt CRC */
    s.frames[0].crc ^= 0xFF;
    uint8_t out[CRIT168_PAYLOAD_SIZE];
    uint32_t len = 0;
    ASSERT(!crit168_deserialize_frame(&s.frames[0],
                                      CRIT168_ENDIAN_NATIVE,
                                      CRIT168_ENDIAN_NATIVE,
                                      out, &len),
           "corrupted frame deserialization fails");
    PASS();
}

/* ===== Endianness Translation Tests ===== */

TEST(translate_frame_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x1234, 1, CRIT168_ENDIAN_LITTLE);
    crit168_serialize_header(&s, 100);

    /* Save original values */
    uint16_t orig_tag = s.frames[0].format_tag;

    /* Translate little -> big */
    crit168_translate_frame(&s.frames[0], CRIT168_ENDIAN_LITTLE, CRIT168_ENDIAN_BIG);

    /* After translation, multi-byte fields should be swapped */
    uint16_t swapped_tag = orig_tag;
    crit168_swap16(&swapped_tag);
    ASSERT(s.frames[0].format_tag == swapped_tag, "format_tag swapped");

    /* Frame should still verify (CRC recomputed) */
    ASSERT(crit168_verify_frame(&s.frames[0]), "translated frame CRC valid");

    /* Translate back */
    crit168_translate_frame(&s.frames[0], CRIT168_ENDIAN_BIG, CRIT168_ENDIAN_LITTLE);
    ASSERT(s.frames[0].format_tag == orig_tag, "format_tag restored");
    ASSERT(crit168_verify_frame(&s.frames[0]), "restored frame CRC valid");
    PASS();
}

TEST(translate_native_noop_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_NATIVE);
    crit168_serialize_header(&s, 100);

    uint16_t orig_tag = s.frames[0].format_tag;
    crit168_translate_frame(&s.frames[0], CRIT168_ENDIAN_NATIVE, CRIT168_ENDIAN_BIG);
    ASSERT(s.frames[0].format_tag == orig_tag, "native translation is noop");
    PASS();
}

/* ===== Query Tests ===== */

TEST(get_total_payload_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);
    crit168_serialize_header(&s, 100);

    uint8_t data[26];
    crit168_serialize_data(&s, data, 26);

    /* 2 data frames * 13 bytes = 26 */
    ASSERT(crit168_get_total_payload(&s) == 26, "total payload is 26");
    PASS();
}

TEST(count_by_type_test) {
    crit168_serializer_t s;
    crit168_serializer_init(&s, 0x0001, 1, CRIT168_ENDIAN_LITTLE);
    crit168_serialize_header(&s, 20);
    uint8_t data[13];
    crit168_serialize_data(&s, data, 13);
    crit168_serialize_checksum(&s);
    crit168_serialize_eof(&s);

    ASSERT(crit168_count_by_type(&s, CRIT168_TYPE_HEADER) == 1, "1 header");
    ASSERT(crit168_count_by_type(&s, CRIT168_TYPE_DATA) == 1, "1 data");
    ASSERT(crit168_count_by_type(&s, CRIT168_TYPE_CHECKSUM) == 1, "1 checksum");
    ASSERT(crit168_count_by_type(&s, CRIT168_TYPE_EOF) == 1, "1 EOF");
    PASS();
}

/* ===== Name Function Tests ===== */

TEST(name_functions_test) {
    ASSERT(strcmp(crit168_frame_type_name(CRIT168_TYPE_HEADER), "header") == 0, "header name");
    ASSERT(strcmp(crit168_frame_type_name(CRIT168_TYPE_DATA), "data") == 0, "data name");
    ASSERT(strcmp(crit168_frame_type_name(CRIT168_TYPE_CHECKSUM), "checksum") == 0, "checksum name");
    ASSERT(strcmp(crit168_frame_type_name(CRIT168_TYPE_EOF), "eof") == 0, "eof name");
    ASSERT(strcmp(crit168_endian_name(CRIT168_ENDIAN_LITTLE), "little") == 0, "little name");
    ASSERT(strcmp(crit168_endian_name(CRIT168_ENDIAN_BIG), "big") == 0, "big name");
    ASSERT(strcmp(crit168_endian_name(CRIT168_ENDIAN_NATIVE), "native") == 0, "native name");
    PASS();
}

/* ===== Main ===== */

int main(void) {
    printf("\n=== ZXV CRIT_168 OS (O1) Tests ===\n\n");

    RUN(crc8_test);
    RUN(crc32_test);
    RUN(swap16_test);
    RUN(swap32_test);
    RUN(registry_init_test);
    RUN(register_format_test);
    RUN(duplicate_format_test);
    RUN(get_nonexistent_format_test);
    RUN(serializer_init_test);
    RUN(serialize_header_test);
    RUN(serialize_data_test);
    RUN(serialize_large_data_test);
    RUN(serialize_checksum_test);
    RUN(serialize_eof_test);
    RUN(full_serialization_test);
    RUN(verify_frame_corrupt_test);
    RUN(verify_sequence_corrupt_test);
    RUN(deserialize_frame_test);
    RUN(deserialize_corrupt_fails_test);
    RUN(translate_frame_test);
    RUN(translate_native_noop_test);
    RUN(get_total_payload_test);
    RUN(count_by_type_test);
    RUN(name_functions_test);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return tests_failed > 0 ? 1 : 0;
}

/* crit168_os.h — Canonical Representation, Integrity, Translation (O1)
 *
 * O1 CRIT_168 provides the OS-level canonical serialization, integrity
 * verification, and format translation layer for the ZXV kernel.
 *
 * Key responsibilities:
 *   - Canonical serialization of kernel data structures to UBH-168 frames
 *   - Integrity verification via CRC-32 and payload hashing
 *   - Endianness translation (x86_64 little-endian <-> ARM64 bi-endian)
 *   - Versioned format tags for backward compatibility
 *   - Bounded safe parsers with length checking
 *   - Translation registry for format conversion
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */
#ifndef CRIT168_OS_H
#define CRIT168_OS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== O1 Constants ===== */

#define CRIT168_FRAME_SIZE     168   /* canonical 168-bit (21-byte) frame */
#define CRIT168_MAX_FRAMES    1024   /* max frames per serialization */
#define CRIT168_MAX_FORMATS     32   /* max registered formats */
#define CRIT168_MAX_NAME_LEN    32
#define CRIT168_HEADER_SIZE      8   /* header bytes per frame */
#define CRIT168_PAYLOAD_SIZE    13   /* payload bytes per frame (21-8) */

/* ===== Endianness ===== */

typedef enum {
    CRIT168_ENDIAN_LITTLE = 0,   /* x86_64 */
    CRIT168_ENDIAN_BIG    = 1,   /* network, ARM64 default */
    CRIT168_ENDIAN_NATIVE = 2,   /* no translation needed */
} crit168_endian_t;

/* ===== Frame Type ===== */

typedef enum {
    CRIT168_TYPE_HEADER    = 0,  /* metadata frame */
    CRIT168_TYPE_DATA      = 1,  /* data frame */
    CRIT168_TYPE_CHECKSUM  = 2,  /* integrity frame */
    CRIT168_TYPE_PADDING   = 3,  /* alignment frame */
    CRIT168_TYPE_EOF       = 4,  /* end marker */
} crit168_frame_type_t;

/* ===== Canonical Frame (21 bytes = 168 bits) ===== */

typedef struct crit168_frame {
    uint8_t  version;              /* format version */
    uint8_t  type;                 /* crit168_frame_type_t */
    uint16_t format_tag;           /* identifies the data format */
    uint16_t sequence;             /* frame sequence number */
    uint8_t  payload[CRIT168_PAYLOAD_SIZE]; /* 13 bytes of data */
    uint8_t  crc;                  /* CRC-8 over header + payload */
} crit168_frame_t;

/* ===== Format Descriptor ===== */

typedef struct crit168_format {
    uint16_t tag;
    char name[CRIT168_MAX_NAME_LEN];
    uint8_t  version;
    crit168_endian_t endian;
    uint32_t max_frames;
    bool active;
} crit168_format_t;

/* ===== Serialization Context ===== */

typedef struct crit168_serializer {
    crit168_frame_t frames[CRIT168_MAX_FRAMES];
    uint32_t frame_count;
    uint16_t format_tag;
    uint8_t  version;
    crit168_endian_t target_endian;
    uint16_t next_sequence;
} crit168_serializer_t;

/* ===== Translation Registry ===== */

typedef struct crit168_registry {
    crit168_format_t formats[CRIT168_MAX_FORMATS];
    uint32_t format_count;
} crit168_registry_t;

/* ===== API ===== */

void crit168_registry_init(crit168_registry_t *reg);
int32_t crit168_register_format(crit168_registry_t *reg, uint16_t tag,
                                const char *name, uint8_t version,
                                crit168_endian_t endian, uint32_t max_frames);
crit168_format_t *crit168_get_format(crit168_registry_t *reg, uint16_t tag);

/* Serialization */
void crit168_serializer_init(crit168_serializer_t *s, uint16_t format_tag,
                             uint8_t version, crit168_endian_t target_endian);
int32_t crit168_serialize_data(crit168_serializer_t *s, const uint8_t *data,
                               uint32_t data_len);
int32_t crit168_serialize_header(crit168_serializer_t *s, uint32_t total_data_len);
int32_t crit168_serialize_checksum(crit168_serializer_t *s);
int32_t crit168_serialize_eof(crit168_serializer_t *s);

/* Deserialization */
bool crit168_deserialize_frame(const crit168_frame_t *frame,
                               crit168_endian_t source_endian,
                               crit168_endian_t target_endian,
                               uint8_t *out_payload, uint32_t *out_len);

/* Integrity */
uint8_t crit168_crc8(const uint8_t *data, uint32_t len);
uint32_t crit168_crc32(const uint8_t *data, uint32_t len);
bool crit168_verify_frame(const crit168_frame_t *frame);
bool crit168_verify_sequence(const crit168_serializer_t *s);

/* Endianness Translation */
void crit168_swap16(uint16_t *val);
void crit168_swap32(uint32_t *val);
void crit168_translate_frame(crit168_frame_t *frame,
                             crit168_endian_t from, crit168_endian_t to);

/* Queries */
uint32_t crit168_get_total_payload(const crit168_serializer_t *s);
uint32_t crit168_count_by_type(const crit168_serializer_t *s,
                               crit168_frame_type_t type);

/* Name Functions */
const char *crit168_frame_type_name(crit168_frame_type_t type);
const char *crit168_endian_name(crit168_endian_t endian);

#endif /* CRIT168_OS_H */

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zaero.h — aero-orbital: a ground-side analysis aid for SmallSat operators
 * and ground stations.
 *
 *   geodesy     WGS-84 geodetic <-> ECEF in integer fixed point. Angles in
 *               nano-degrees, lengths in millimetres (and finer variants in
 *               micrometres and pico-degrees). CORDIC on Q61 words with a
 *               portable 128-bit product; no float anywhere. Errors were
 *               measured against an 80-digit decimal reference (README).
 *   telemetry   a canonical telemetry packet of exact integer readings with
 *               the spacecraft's CRC-32C, carried over the ground network
 *               in UBH-168 frames (zxv_wire_*), 16 payload bytes per frame.
 *   SEU flags   a receiver that collects the redundant copies a spacecraft
 *               sends of each packet. Copies that disagree, or a copy whose
 *               on-board CRC does not match, make the packet BOTH: it is
 *               held and the quarantine log gets an entry. Nothing is ever
 *               corrected silently: the optional majority policy releases a
 *               strict-majority value only after a log entry and a signed
 *               record are written. An analyst (a person) resolves a held
 *               packet by signing a decision; decisions verify offline.
 *
 * Not flight software. No attitude or orbit control. No DO-178C, ECSS or
 * other certification claim (README "Limits"). Integer code only (R11).
 * Kit API 0.1 (preview). */
#ifndef ZAERO_H
#define ZAERO_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZAO_VERSION_MAJOR 0
#define ZAO_VERSION_MINOR 1

/* Status codes (-3800..-3899). */
#define ZAO_E_RANGE     (-3801) /* outside the supported geodetic / ECEF range          */
#define ZAO_E_CRC       (-3802) /* the packet's on-board CRC-32C does not match          */
#define ZAO_E_WIRE      (-3803) /* UBH-168 frames malformed, out of order or corrupt     */
#define ZAO_E_SEU       (-3804) /* copies disagree or a CRC failed: held as BOTH, logged */
#define ZAO_E_PACKET    (-3805) /* unknown packet                                        */
#define ZAO_E_ANALYST   (-3806) /* the key is not a registered analyst                   */
#define ZAO_E_CORRECTED (-3807) /* released by the logged majority policy (see README)   */
#define ZAO_E_PENDING   (-3808) /* still collecting copies                               */

const char *zao_status_name(int32_t status);

/* ===== Geodesy (WGS-84) =====
 * a = 6,378,137 m, 1/f = 298.257223563. Ranges:
 *   latitude   -90..+90 degrees      (+-90,000,000,000 nano-degrees)
 *   longitude  -180..+180 degrees    (output in (-180, +180])
 *   height     -10 km .. +100,000 km above the ellipsoid
 *   ECEF       each axis within +-110,000 km
 * Outside them: ZAO_E_RANGE. Results are rounded to the nearest unit. */
#define ZAO_LAT_MAX  INT64_C(90000000000)
#define ZAO_LON_MAX  INT64_C(180000000000)
#define ZAO_H_MIN    INT64_C(-10000000)
#define ZAO_H_MAX    INT64_C(100000000000)
#define ZAO_ECEF_MAX INT64_C(110000000000)

typedef struct {
    int64_t lat_nd; /* nano-degrees */
    int64_t lon_nd;
    int64_t h_mm;   /* millimetres above the ellipsoid */
} zao_geo;

typedef struct {
    int64_t x_mm, y_mm, z_mm;
} zao_ecef;

int32_t zao_geo_to_ecef(const zao_geo *g, zao_ecef *out);
int32_t zao_ecef_to_geo(const zao_ecef *e, zao_geo *out);

/* The same conversions at finer output resolution: ECEF in micrometres,
 * latitude/longitude in pico-degrees and height in micrometres. Inputs
 * are the same units as above. */
typedef struct {
    int64_t x_um, y_um, z_um;
} zao_ecef_um;
typedef struct {
    int64_t lat_pd, lon_pd; /* pico-degrees */
    int64_t h_um;
} zao_geo_fine;
int32_t zao_geo_to_ecef_um(const zao_geo *g, zao_ecef_um *out);
int32_t zao_ecef_to_geo_fine(const zao_ecef *e, zao_geo_fine *out);

/* ===== Telemetry packets =====
 * Canonical bytes (little-endian):
 *   u8 version (1) | u16 scid | u8 vcid | u32 seq | u64 time_ms | u8 n |
 *   n x (u16 channel | i64 value) | u32 CRC-32C of everything before it
 * Values are exact integers in the channel's own unit (mV, mK, counts). */
#define ZAO_TM_VALUES    32u
#define ZAO_TM_MIN_BYTES 21u
#define ZAO_TM_MAX_BYTES (ZAO_TM_MIN_BYTES + 10u * ZAO_TM_VALUES)

typedef struct {
    uint16_t channel;
    int64_t value;
} zao_reading;

typedef struct {
    uint16_t scid;    /* spacecraft id     */
    uint8_t vcid;     /* virtual channel   */
    uint32_t seq;     /* packet sequence   */
    uint64_t time_ms; /* spacecraft time   */
    uint8_t n;        /* readings, 0..32   */
    zao_reading v[ZAO_TM_VALUES];
} zao_tm;

/* CRC-32C (Castagnoli, reflected, init and xorout 0xFFFFFFFF). */
uint32_t zao_crc32c(const uint8_t *p, size_t n);
int32_t zao_tm_encode(const zao_tm *tm, uint8_t *buf, size_t cap, size_t *out_len);
/* ZXV_OK; ZAO_E_CRC when the structure parses but the CRC does not match
 * (*out holds the parsed, untrusted fields); ZXV_E_FORMAT otherwise. */
int32_t zao_tm_decode(const uint8_t *buf, size_t len, zao_tm *out);

/* ===== UBH-168 carriage =====
 * A byte string travels as one header frame and ceil(len/16) data frames,
 * each a zxv_wire_pack frame on one trunk (0..9):
 *   header  tag(trunk, shell 0, sync 1); S+ = {magic, len, data frames};
 *           S- = {CRC-32C(bytes), first 4 bytes of SHA3-256(bytes)}
 *   data k  tag(trunk, shell k mod 10, sync 0); S+0 = 0xA0000000 | k;
 *           the other four words carry 16 bytes (the last frame zero-padded)
 * The index word keeps a data frame from ever reading as the idle pilot. The
 * decoder refuses anything else: a wrong tag, a frame out of order, a
 * missing or extra frame, non-zero padding or a checksum mismatch. */
#define ZAO_WIRE_MAGIC     0xA0E0A168u
#define ZAO_WIRE_PAYLOAD   16u
#define ZAO_WIRE_MAX_BYTES 4096u

/* Frames needed for len bytes. */
size_t zao_wire_frames(size_t len);
int32_t zao_wire_encode(uint32_t trunk, const uint8_t *bytes, size_t len, uint8_t *frames,
                        size_t cap, size_t *out_nframes);
int32_t zao_wire_decode(const uint8_t *frames, size_t nframes, uint8_t *bytes, size_t cap,
                        size_t *out_len, uint32_t *out_trunk);

/* ===== SEU receiver ===== */
#define ZAO_MAX_COPIES 5u
#define ZAO_MAX_PACKETS 65536u
#define ZAO_MAX_ANALYSTS 16u

#define ZAO_POLICY_HOLD     0 /* any disagreement or CRC failure: held for an analyst */
#define ZAO_POLICY_MAJORITY 1 /* a strict majority of good identical copies is released
                                 after a log entry and a signed record; else held     */

#define ZAO_PK_COLLECTING 1 /* NEITHER, Emerging: waiting for copies                 */
#define ZAO_PK_RELEASED   2 /* TRUE, Manifest: every copy good and identical         */
#define ZAO_PK_CORRECTED  3 /* TRUE (negentropic): released by the logged majority  */
#define ZAO_PK_HELD       4 /* BOTH: frozen for an analyst                           */
#define ZAO_PK_ACCEPTED   5 /* TRUE: an analyst signed for one copy                  */
#define ZAO_PK_REJECTED   6 /* FALSE, Forbidden: an analyst rejected the packet      */

#define ZAO_ACCEPT 1
#define ZAO_REJECT 2

#define ZAO_REASON_SEU       (ZIND_REASON_KIT_BASE + 0) /* good copies disagree         */
#define ZAO_REASON_CRC       (ZIND_REASON_KIT_BASE + 1) /* an on-board CRC failed       */
#define ZAO_REASON_CORRECTED (ZIND_REASON_KIT_BASE + 2) /* released by majority          */
#define ZAO_REASON_LATE      (ZIND_REASON_KIT_BASE + 3) /* a differing copy after release*/
#define ZAO_REASON_UNPARSED  (ZIND_REASON_KIT_BASE + 4) /* bytes not a packet at all     */

#define ZAO_REVIEW_CTX "aero-orbital/seu"

typedef struct {
    uint16_t scid;
    uint8_t vcid;
    uint32_t seq;
} zao_key;

typedef struct {
    uint8_t copies;    /* copies the spacecraft sends of each packet, 1..5 */
    uint8_t policy;    /* ZAO_POLICY_*                                      */
    uint32_t capacity; /* packets kept, 1..ZAO_MAX_PACKETS                  */
} zao_rx_cfg;

typedef struct zao_rx zao_rx;

/* The cap needs CRYPTO_SIGN and ATTEST. qlog is required: a receiver that
 * cannot log cannot hold. The operator key signs the decision records.
 * Both are borrowed. */
int32_t zao_rx_create(const zxv_cap *cap, const zind_signer *operator_key, zxv_hexlog *qlog,
                      const zao_rx_cfg *cfg, zao_rx **out);
void zao_rx_destroy(zao_rx *rx);
int32_t zao_analyst_add(zao_rx *rx, const uint8_t pk[ZIND_PK_BYTES]);

/* One received copy, as UBH-168 frames. *out_key (may be NULL) names the
 * packet. A packet is decided when its last expected copy arrives (or at
 * zao_rx_close). Returns:
 *   ZXV_OK           recorded (still collecting, or released clean)
 *   ZAO_E_SEU        decided: held as BOTH and logged
 *   ZAO_E_CORRECTED  decided: released by the logged majority policy
 *   ZIND_E_REPLAY    an identical copy of an already decided packet; ignored
 *   ZIND_E_HELD      recorded as more evidence on a held packet
 *   ZIND_E_FROZEN    the packet was rejected; nothing recorded
 *   ZAO_E_WIRE       the frames do not decode; nothing recorded
 *   ZXV_E_FORMAT     the bytes are not a packet; logged, nothing recorded
 *   ZXV_E_CAPACITY   the table or the packet's copy slots are full
 * A copy that differs from a released packet re-opens it as held
 * (ZAO_E_SEU): consumers must re-check zao_rx_info. */
int32_t zao_rx_submit(zao_rx *rx, const uint8_t *frames, size_t nframes, zao_key *out_key);
/* Decide a packet now with the copies it has (end of a pass). */
int32_t zao_rx_close(zao_rx *rx, zao_key key);

typedef struct {
    int32_t state;      /* ZAO_PK_*                                    */
    uint8_t tag;        /* 64-state tag                                */
    uint8_t copies;     /* copies recorded                             */
    uint8_t crc_bad;    /* copies whose on-board CRC failed            */
    uint8_t distinct;   /* distinct byte strings among the good copies */
    uint32_t max_bit_diff; /* the most bits by which two copies differ */
    uint32_t holds;     /* times this packet was held                  */
} zao_pkt_info;
int32_t zao_rx_info(const zao_rx *rx, zao_key key, zao_pkt_info *out);
/* The decided value (RELEASED, CORRECTED, ACCEPTED). ZAO_E_PENDING while
 * collecting, ZIND_E_HELD when held, ZIND_E_FROZEN when rejected. */
int32_t zao_rx_value(const zao_rx *rx, zao_key key, zao_tm *out);
/* Copy i's bytes; *out_crc_ok = 1 when its on-board CRC matched. */
int32_t zao_rx_copy(const zao_rx *rx, zao_key key, uint32_t i, uint8_t *buf, size_t cap,
                    size_t *out_len, int32_t *out_crc_ok);
/* Count of decided, held and rejected packets. */
int32_t zao_rx_counts(const zao_rx *rx, uint32_t *out_packets, uint32_t *out_held,
                      uint32_t *out_logged);

/* The bytes an analyst signs (context ZAO_REVIEW_CTX): the key, the SHA3-256
 * of every copy recorded, the hold number, the decision and the copy chosen
 * (ACCEPT names a copy whose CRC matched; REJECT passes 0). */
int32_t zao_review_message(const zao_rx *rx, zao_key key, int32_t decision, uint32_t copy,
                           uint8_t *buf, size_t cap, size_t *out_len);
int32_t zao_review_resolve(zao_rx *rx, zao_key key, const uint8_t analyst_pk[ZIND_PK_BYTES],
                           int32_t decision, uint32_t copy, const uint8_t sig[ZIND_SIG_BYTES]);

/* The decision chain (majority corrections and analyst decisions), as
 * zind_chain_export bytes, and its offline check: every link and operator
 * signature, and every embedded analyst signature against the trusted
 * analyst keys. *out_reviews / *out_corrections count the records. */
int32_t zao_decisions_export(const zao_rx *rx, uint8_t *buf, size_t cap, size_t *out_len);
int32_t zao_decisions_verify(const zxv_cap *cap, const uint8_t *buf, size_t len,
                             const uint8_t *const *analyst_pks, size_t n_analysts,
                             const uint8_t *operator_pk, uint32_t *out_reviews,
                             uint32_t *out_corrections, uint32_t *out_bad);

/* Bits that differ between two byte strings (a longer one counts 8 bits per
 * extra byte). */
uint32_t zao_bit_diff(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen);

#ifdef __cplusplus
}
#endif

#endif /* ZAERO_H */

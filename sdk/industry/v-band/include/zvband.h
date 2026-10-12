/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvband.h — v-band: low-power state logging for LoRaWAN and NB-IoT
 * sensors.
 *
 *   the log      on the sensor: an append-only log of readings as exact
 *                integers, each with a four-valued state (TRUE a valid
 *                reading, FALSE a sensor fault, NEITHER not sampled, BOTH a
 *                redundant pair that disagrees, both values kept). The
 *                memory is the caller's fixed array; a full log refuses new
 *                readings and never overwrites one the gateway has not
 *                acknowledged. A SHA3-256 chain head covers every reading
 *                ever appended.
 *   batches      the unacknowledged readings, packed (varints, time deltas)
 *                into an uplink payload no larger than the LoRaWAN data
 *                rate allows (the table in README), with a 4-byte SHA3
 *                check value.
 *   UBH-168      a gateway forwards a payload to the local mesh as UBH-168
 *                frames (zxv_wire_pack), 16 payload bytes per frame.
 *   the gateway  per device, accepts batches strictly in order: a repeated
 *                batch or one whose readings it already holds is refused as
 *                a duplicate, a late batch or one that skips readings is
 *                refused as out of order, and a bad check value is refused.
 *                Every refusal is reported to a callback (for the host's
 *                quarantine log) and the reply names the next reading it
 *                expects, so the device can resend.
 *
 * MCU subset style: no heap, no stdio, no C library calls in the core (only
 * the pure libzxv functions zxv_sha3_256, zxv_wire_* and zxv_hex_*), fixed
 * caller-owned buffers, integers only. It is built for the host in the tests
 * and checked freestanding (make check-mcu). Not a LoRaWAN stack: it sits
 * above the operator's MAC (README "Limits"). Kit API 0.1 (preview). */
#ifndef ZVBAND_H
#define ZVBAND_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZVB_VERSION_MAJOR 0
#define ZVB_VERSION_MINOR 1

/* Status codes (-4000..-4099). */
#define ZVB_E_FULL      (-4001) /* the log is full of unacknowledged readings         */
#define ZVB_E_DUPLICATE (-4002) /* gateway: that batch, or its readings, already held  */
#define ZVB_E_ORDER     (-4003) /* gateway: a late batch, or one that skips readings   */
#define ZVB_E_CHECK     (-4004) /* the batch check value does not match                */
#define ZVB_E_DR        (-4005) /* unknown region or data rate, or too small a payload */
#define ZVB_E_DEVICE    (-4006) /* gateway: unknown device                             */
#define ZVB_E_WIRE      (-4007) /* UBH-168 frames malformed, out of order or corrupt   */
#define ZVB_E_EMPTY     (-4008) /* nothing to send                                     */

/* Static name of any status (falls back to zxv_status_name). */
const char *zvb_status_name(int32_t status);

/* ===== the sensor log ===== */
typedef struct {
    uint32_t seq;    /* 1, 2, 3, ... per device, no gaps        */
    uint32_t time_s; /* device seconds; never decreasing         */
    uint8_t sensor;
    uint8_t lp;      /* ZXV_LP_*                                 */
    int32_t value;   /* exact integer in the sensor's unit       */
    int32_t value2;  /* the second reading of a BOTH pair, else 0 */
} zvb_record;

typedef struct {
    zvb_record *rec; /* caller storage: cap records, used as a ring */
    uint32_t cap;
    uint32_t head;      /* index of the oldest kept record */
    uint32_t n;         /* records kept                    */
    uint32_t device;
    uint32_t next_seq;  /* seq of the next reading         */
    uint32_t acked;     /* highest seq the gateway holds   */
    uint32_t sent;      /* highest seq in the batch now in flight; readings
                           after it go in the next batch              */
    uint32_t sent_max;  /* highest seq ever put in a batch           */
    uint32_t next_batch;
    uint32_t last_time;
    uint8_t chain[32];  /* SHA3-256 chain head             */
} zvb_log;

int32_t zvb_log_init(zvb_log *log, zvb_record *storage, uint32_t cap, uint32_t device);
/* lp: TRUE (value is the reading), FALSE (value is the fault code) or
 * NEITHER (not sampled; value must be 0). BOTH comes only from a pair. */
int32_t zvb_log_append(zvb_log *log, uint8_t sensor, uint32_t time_s, int32_t lp, int32_t value);
/* Two redundant sensors: within `tolerance` of each other -> TRUE with the
 * first value; otherwise BOTH with both values. Neither is chosen silently. */
int32_t zvb_log_append_pair(zvb_log *log, uint8_t sensor, uint32_t time_s, int32_t a, int32_t b,
                            uint32_t tolerance);
/* Kept record i (0 = oldest). */
int32_t zvb_log_at(const zvb_log *log, uint32_t i, zvb_record *out);
/* The gateway's reply: the next seq it expects. Readings before it are
 * acknowledged; any sent after it are queued again for the next batch. It
 * may acknowledge readings carried by an earlier batch (a lost reply), but
 * never one that was never sent, and never goes backwards: ZXV_E_ARG. A
 * device that hears no reply calls zvb_log_ack(log, log->acked + 1) to
 * queue its unacknowledged readings again. */
int32_t zvb_log_ack(zvb_log *log, uint32_t next_expected);
/* Release acknowledged readings to make room (the chain head keeps them). */
int32_t zvb_log_trim(zvb_log *log, uint32_t *out_released);

/* ===== payload limits ===== */
#define ZVB_REGION_EU868 1 /* LoRaWAN RP002-1.0.4 EU863-870, DR0..DR7 */
#define ZVB_REGION_US915 2 /* LoRaWAN RP002-1.0.4 US902-928, DR0..DR4 uplink */
#define ZVB_REGION_NBIOT 3 /* this kit's assumption: 512 bytes (DR ignored) */
int32_t zvb_payload_max(int32_t region, uint8_t dr, uint32_t *out_bytes);

/* ===== batches =====
 *   u8 version 1 | u32 device | u32 batch | u32 first seq | u8 count |
 *   u32 base time | records | 4-byte check
 * record: u8 sensor | u8 flags (lp, bit 2: has value2) | varint time delta |
 *         zigzag varint value | [zigzag varint value2]
 * check:  the first 4 bytes of SHA3-256("zvb/batch/v1" || everything before).
 * The check catches corruption; it does not authenticate (that is the
 * LoRaWAN MIC's job, or NB-IoT's transport security). */
#define ZVB_BATCH_HEADER 18u
#define ZVB_BATCH_MIN    (ZVB_BATCH_HEADER + 4u + 4u) /* header, one record, check */
#define ZVB_BATCH_MAX    512u
#define ZVB_BATCH_RECORDS 255u

/* Pack the readings after the last one sent, as many as fit in max_bytes.
 * ZVB_E_EMPTY when none are waiting; ZVB_E_DR when max_bytes cannot hold a
 * single reading. */
int32_t zvb_batch_make(zvb_log *log, uint32_t max_bytes, uint8_t *out, size_t cap,
                       size_t *out_len, uint32_t *out_count);

typedef struct {
    uint32_t device, batch, first_seq, count;
    uint32_t n_lp[4];  /* readings by ZXV_LP_* value                       */
    uint8_t tag;       /* BOTH if any pair disagreed, else FALSE if any fault,
                          else NEITHER if any unsampled, else TRUE; Manifest */
} zvb_batch_info;

/* Decode and check a payload without gateway state. */
int32_t zvb_batch_decode(const uint8_t *buf, size_t len, zvb_record *out, uint32_t out_cap,
                         zvb_batch_info *info);

/* ===== UBH-168 carriage (as in aero-orbital, magic 0xA0E0B168, the header's
 * S- words are the first 8 bytes of SHA3-256(bytes)) ===== */
#define ZVB_WIRE_MAGIC    0xA0E0B168u
#define ZVB_WIRE_MAX_FRAMES (1u + (ZVB_BATCH_MAX + 15u) / 16u)
uint32_t zvb_wire_frames(size_t len);
int32_t zvb_wire_encode(uint32_t trunk, const uint8_t *bytes, size_t len, uint8_t *frames,
                        size_t cap, size_t *out_nframes);
int32_t zvb_wire_decode(const uint8_t *frames, size_t nframes, uint8_t *bytes, size_t cap,
                        size_t *out_len);

/* ===== the gateway ===== */
typedef struct {
    uint32_t device;
    uint32_t last_batch; /* 0 = none yet                 */
    uint32_t next_seq;   /* next reading expected (>= 1) */
    uint8_t last_check[4];
    uint32_t accepted, refused;
} zvb_gw_dev;

/* Called for every refusal: device (0 if unknown), status, batch number. */
typedef void (*zvb_refuse_fn)(void *user, uint32_t device, int32_t status, uint32_t batch);

typedef struct {
    zvb_gw_dev *dev; /* caller storage */
    uint32_t cap, n;
    zvb_refuse_fn on_refuse;
    void *user;
} zvb_gw;

int32_t zvb_gw_init(zvb_gw *gw, zvb_gw_dev *storage, uint32_t cap, zvb_refuse_fn fn, void *user);
int32_t zvb_gw_add(zvb_gw *gw, uint32_t device);
/* Accept one batch. *out_next (may be NULL) = the next seq expected from
 * that device, after this call (also on a refusal, for the reply). The
 * readings are copied to out (out_cap >= the batch's count, else
 * ZXV_E_BUFFER and nothing changes). */
int32_t zvb_gw_accept(zvb_gw *gw, const uint8_t *buf, size_t len, zvb_record *out,
                      uint32_t out_cap, zvb_batch_info *info, uint32_t *out_next);

#ifdef __cplusplus
}
#endif

#endif /* ZVBAND_H */

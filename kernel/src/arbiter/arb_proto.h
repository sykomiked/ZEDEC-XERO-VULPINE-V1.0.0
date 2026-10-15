/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* arb_proto.h — the arbiter mailbox: the wire format both ends share.
 *
 * Every institutional briefcase, every server and every handheld carries an
 * arbiter (hardware/docs/oem/OEM_INSTITUTIONAL.md section 2 and 6,
 * servers/SERVER_COMMON.md section 3 and 5, mobile/MOBILE_COMMON.md section 1):
 * an FPGA SoC that owns power, reset, the kill switches, the tamper sensors,
 * the key store and the seal display. OS_HARDWARE_INTERFACE.md section 3 row 4
 * leaves its host interface to this project: "doorbells, message rings, and
 * three linear framebuffers in a BAR". This file is that interface.
 *
 * LAYOUT of the shared window (the arbiter's PCIe BAR 0 on a briefcase or
 * server; on a handheld, the same layout in the arbiter's shared SRAM).
 * Little-endian, every field 32-bit aligned:
 *
 *   0x0000  header   magic "ZXVARBMB" (u64), version, ring capacity, message
 *                    size, framebuffer count/width/height/stride/offset
 *   0x0040  doorbell host -> arbiter (u32 counter, written by the host)
 *   0x0044  doorbell arbiter -> host (u32 counter, written by the arbiter)
 *   0x0100  ring H2A host -> arbiter: head (host writes), tail (arbiter
 *                    writes), then ARB_RING_MAX slots of ARB_MSG_SIZE bytes
 *   0x1140  ring A2H arbiter -> host: head (arbiter writes), tail (host)
 *   0x3000  framebuffers, ARB_FB_MAX of them, 32-bit XRGB, stride bytes/row
 *
 * The arbiter writes the header once, before it lets the host out of reset.
 * Each ring has exactly one producer and one consumer. head and tail are
 * free-running u32 counters; head - tail is the fill, and head - tail >=
 * capacity is FULL (backpressure, never an overwrite). A consumer that reads
 * a fill above capacity, or a message whose length overruns its slot, has a
 * peer that broke the protocol: it stops reading that ring (state B) and says
 * so, rather than guessing which half to believe.
 *
 * NEITHER side trusts the other's memory: every value read from the window is
 * bounded before it is used. The window is not secret (it is on PCIe inside
 * the vault, behind the host IOMMU on the arbiter side); a released key is in
 * it only until the host consumes the message, which zeroes the slot.
 */
#ifndef ZXV_ARB_PROTO_H
#define ZXV_ARB_PROTO_H

#include <stdint.h>

#define ARB_MAGIC   0x424D42524156585AULL /* "ZXVARBMB" little-endian */
#define ARB_VERSION 1u

#define ARB_MSG_SIZE    64u
#define ARB_MSG_PAYLOAD 56u
#define ARB_RING_MAX    64u /* slots reserved per ring; capacity <= this, power of two */
#define ARB_FB_MAX      3u  /* the briefcase's three panels */

#define ARB_OFF_MAGIC     0x0000u
#define ARB_OFF_VERSION   0x0008u
#define ARB_OFF_RING_CAP  0x000Cu
#define ARB_OFF_MSG_SIZE  0x0010u
#define ARB_OFF_FB_COUNT  0x0014u
#define ARB_OFF_FB_WIDTH  0x0018u
#define ARB_OFF_FB_HEIGHT 0x001Cu
#define ARB_OFF_FB_STRIDE 0x0020u
#define ARB_OFF_FB_OFFSET 0x0024u
#define ARB_OFF_DB_H2A    0x0040u
#define ARB_OFF_DB_A2H    0x0044u
#define ARB_OFF_RING_H2A  0x0100u
#define ARB_RING_BYTES    (0x40u + ARB_RING_MAX * ARB_MSG_SIZE) /* 0x1040 */
#define ARB_OFF_RING_A2H  (ARB_OFF_RING_H2A + ARB_RING_BYTES)   /* 0x1140 */
#define ARB_OFF_FB        0x3000u
#define ARB_WINDOW_MIN    ARB_OFF_FB /* a window with no framebuffers */

/* Ring header offsets inside a ring. */
#define ARB_RING_HEAD  0x00u
#define ARB_RING_TAIL  0x04u
#define ARB_RING_SLOTS 0x40u

/* ---- message types ---------------------------------------------------------
 * host -> arbiter */
#define ARB_M_HELLO   0x01 /* u32 version */
#define ARB_M_ATTEST  0x02 /* nonce[16], measurement[32] */
#define ARB_M_KEY_REQ 0x03 /* u32 slot */
#define ARB_M_SEAL_REQ                                                                             \
    0x04 /* u32 id, i64 amount (minor units), char ccy[4], u32 expiry, payee[32] */
#define ARB_M_ENTROPY_REQ 0x05 /* u32 bytes (1..48) */
#define ARB_M_FB_FLIP     0x06 /* u32 head, u32 generation */
#define ARB_M_STATE_REQ   0x07 /* none: ask for kill switches, interlock, tamper */
/* arbiter -> host */
#define ARB_M_HELLO_ACK     0x81 /* u32 version, nonce[16], u32 kill present, on, tamper */
#define ARB_M_ATTEST_RESULT 0x82 /* u32 verdict (ARB_ATTEST_*) */
#define ARB_M_KEY           0x83 /* u32 slot, u32 status (ARB_KEY_*), key[32] */
#define ARB_M_SEAL_RESULT   0x84 /* u32 id, u32 verdict (ARB_SEAL_*), payee[32] as displayed */
#define ARB_M_ENTROPY       0x85 /* u32 bytes, data[48] */
#define ARB_M_FB_DONE       0x86 /* u32 head, u32 generation */
#define ARB_M_KILL_STATE    0x87 /* u32 present, u32 on */
#define ARB_M_TAMPER        0x88 /* u32 cause (ARB_TAMPER_*), u32 kek_cleared */
#define ARB_M_TELEMETRY     0x89 /* u32 n (1..6), n x {u16 sensor, u16 0, i32 value} */
#define ARB_M_INTERLOCK     0x8A /* u32 permitted, u32 reason (ARB_HOLD_*) */

#define ARB_NONCE_LEN   16u
#define ARB_MEAS_LEN    32u
#define ARB_KEY_LEN     32u
#define ARB_PAYEE_LEN   32u
#define ARB_ENTROPY_MAX 48u
#define ARB_TELEM_MAX   6u

#define ARB_ATTEST_OK       1u
#define ARB_ATTEST_MISMATCH 2u /* measurement differs from the one the arbiter verified */
#define ARB_ATTEST_STALE    3u /* nonce is not the current one */
#define ARB_ATTEST_TAMPERED 4u

#define ARB_KEY_OK           1u
#define ARB_KEY_NOT_ATTESTED 2u
#define ARB_KEY_NO_SLOT      3u
#define ARB_KEY_ZEROISED     4u

#define ARB_SEAL_APPROVED 1u
#define ARB_SEAL_DENIED   2u /* the owner pressed deny, or let it lapse */
#define ARB_SEAL_EXPIRED  3u
#define ARB_SEAL_BUSY     4u /* another seal is on the display */
#define ARB_SEAL_REFUSED  5u /* not attested, or tampered */

/* Kill switches (mobile/MOBILE_COMMON.md section 1; briefcase radios). A set
 * bit in `on` means the device has power. Off means unpowered, not asked to
 * stop, so the host sees the device vanish from its bus and treats that as
 * normal (OS_HARDWARE_INTERFACE.md section 6). */
#define ARB_KILL_CELLULAR  0x01u
#define ARB_KILL_SATELLITE 0x02u
#define ARB_KILL_WIFI_BT   0x04u
#define ARB_KILL_CAMERA    0x08u
#define ARB_KILL_MIC       0x10u
#define ARB_KILL_LORA      0x20u
#define ARB_KILL_UWB_GNSS  0x40u
#define ARB_KILL_ALL       0x7Fu

/* Tamper causes (OEM_INSTITUTIONAL.md section 6.2, SERVER_COMMON.md 5). */
#define ARB_TAMPER_PRESSURE 1u /* vault / shell pressure or humidity step */
#define ARB_TAMPER_LIGHT    2u
#define ARB_TAMPER_SWITCH   3u /* lid, latch or door micro-switch */
#define ARB_TAMPER_MESH     4u /* conductive mesh broken */
#define ARB_TAMPER_GLITCH   5u /* arbiter temperature or voltage out of range */
#define ARB_TAMPER_MOTION   6u /* removed from its mount */

/* Why the start interlock holds the host (SERVER_COMMON.md 2.3, HARDENING 5). */
#define ARB_HOLD_NONE      0u
#define ARB_HOLD_COLD_HDD  1u
#define ARB_HOLD_COLD_SSD  2u
#define ARB_HOLD_DEW       3u
#define ARB_HOLD_COLD_CELL 4u
#define ARB_HOLD_DOOR      5u

/* Sensors the arbiter reports (OS_HARDWARE_INTERFACE.md section 5). Power in
 * milliwatts, temperature in milli-degrees C, pressure in pascals, humidity
 * in hundredths of a percent. */
#define ARB_S_PWR_CPU     0
#define ARB_S_PWR_NEURAL  1
#define ARB_S_PWR_DRAM    2
#define ARB_S_PWR_STORAGE 3
#define ARB_S_PWR_PROJ    4
#define ARB_S_PWR_BUS     5
#define ARB_S_T_CPU       6
#define ARB_S_T_NEURAL    7
#define ARB_S_T_ARBITER   8
#define ARB_S_T_INTAKE    9
#define ARB_S_T_EXHAUST   10
#define ARB_S_T_VAULT     11
#define ARB_S_PRESSURE    12
#define ARB_S_HUMIDITY    13
#define ARB_S_T_CELL      14
#define ARB_S_COUNT       15

typedef struct arb_msg {
    uint16_t type;
    uint16_t len; /* payload bytes used, <= ARB_MSG_PAYLOAD */
    uint32_t seq;
    uint8_t payload[ARB_MSG_PAYLOAD];
} arb_msg_t;

/* ---- window access ---------------------------------------------------------
 * The window is device memory: every access is one volatile 32-bit load or
 * store, and the producer publishes a slot before its head with a release
 * fence (the consumer pairs it with an acquire fence). */
static inline uint32_t arb_rd32(volatile uint8_t *w, uint32_t off)
{
    return *(volatile uint32_t *) (w + off);
}
static inline void arb_wr32(volatile uint8_t *w, uint32_t off, uint32_t v)
{
    *(volatile uint32_t *) (w + off) = v;
}
static inline uint64_t arb_rd64(volatile uint8_t *w, uint32_t off)
{
    return (uint64_t) arb_rd32(w, off) | ((uint64_t) arb_rd32(w, off + 4) << 32);
}

/* Validate a header a peer wrote: magic, version, a power-of-two capacity no
 * larger than ARB_RING_MAX, the message size, and framebuffers that fit
 * inside a window of `size` bytes. 0 = valid; else a negative reason. */
#define ARB_E_MAGIC   (-1)
#define ARB_E_VERSION (-2)
#define ARB_E_RING    (-3)
#define ARB_E_FB      (-4)
#define ARB_E_SIZE    (-5)
int arb_header_check(volatile uint8_t *w, uint64_t size);

/* Write a fresh header (arbiter side). fb_count 0..ARB_FB_MAX; returns 0, or
 * a negative reason if the requested layout does not fit `size`. */
int arb_header_write(volatile uint8_t *w, uint64_t size, uint32_t ring_cap, uint32_t fb_count,
                     uint32_t fb_width, uint32_t fb_height);

/* Producer: copy m into the next slot of the ring at ring_off. 0 = queued,
 * -1 = full (head - tail >= cap), -2 = payload length over ARB_MSG_PAYLOAD. */
int arb_ring_put(volatile uint8_t *w, uint32_t ring_off, uint32_t cap, const arb_msg_t *m);

/* Consumer: take the oldest message. 1 = m written (and the slot zeroed),
 * 0 = empty, -1 = the peer broke the ring (fill above cap, or a length over
 * the payload): nothing consumed. */
int arb_ring_get(volatile uint8_t *w, uint32_t ring_off, uint32_t cap, arb_msg_t *m);

/* Fill in the ring now (for tests and the full check). */
uint32_t arb_ring_fill(volatile uint8_t *w, uint32_t ring_off);

/* Little-endian payload packing, bounded by the caller's offsets. */
void arb_put32(uint8_t *p, uint32_t off, uint32_t v);
uint32_t arb_get32(const uint8_t *p, uint32_t off);
void arb_put64(uint8_t *p, uint32_t off, uint64_t v);
uint64_t arb_get64(const uint8_t *p, uint32_t off);

/* Byte helpers without libc. arb_ct_equal is constant time in n. */
void arb_copy(uint8_t *dst, const uint8_t *src, uint32_t n);
void arb_zero(volatile uint8_t *dst, uint32_t n);
int arb_ct_equal(const uint8_t *a, const uint8_t *b, uint32_t n);

#endif

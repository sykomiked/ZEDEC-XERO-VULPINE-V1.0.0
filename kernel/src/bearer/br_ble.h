/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_ble.h — Bluetooth LE and Wi-Fi Direct framing.
 *
 * The radios belong to the phone's OS (CoreBluetooth on iOS, the Android
 * Bluetooth and Wi-Fi P2P stacks). This file is the byte layer the apps
 * put on top of them; docs/retail/BEARERS.md lists the hook points.
 *
 * TWO WAYS TO CARRY DEVMESH FRAMES
 *   GATT  write-without-response / notifications on one characteristic.
 *         Each ATT value is at most ATT_MTU - 3 bytes (3 bytes are the ATT
 *         opcode and handle). ATT_MTU is 23 by default and at most 517
 *         (Core spec Vol 3 Part F 3.2.8/3.2.9: a value is at most 512
 *         bytes). Messages are cut with br_frag.h at that payload size.
 *   Stream  an L2CAP LE credit-based channel (Android
 *         BluetoothDevice.createL2capChannel, iOS CBL2CAPChannel) or a TCP
 *         socket over a Wi-Fi Direct group: a reliable byte stream. Each
 *         message is [length u32 big-endian][bytes] (br_lps_*).
 *
 * RULES
 *   L1  br_ble_payload_mtu() accepts ATT_MTU 23..517 and returns MTU - 3,
 *       capped at 512; anything else is BR_ERR_ARG.
 *   L2  The stream decoder refuses a length of 0 or above its maximum and
 *       then stays dead (BR_ERR_SIZE on every later byte) until
 *       br_lps_rx_reset(): a byte stream cannot be resynchronised safely,
 *       so the host closes and reopens the channel.
 *   L3  No encryption here: devmesh frames are already AEAD-protected or
 *       signed. BLE pairing / LE Secure Connections is the OS's business.
 *
 * HONEST LIMITS
 *   Not tested on real radios. Throughput depends on the phone's
 *   connection interval and data length extension; the 1 Mbit/s in
 *   br_caps(BR_BLE) is the LE 1M PHY symbol rate, not goodput.
 */
#ifndef ZXV_BR_BLE_H
#define ZXV_BR_BLE_H

#include "br_frag.h"

#define BR_ATT_MTU_MIN 23u
#define BR_ATT_MTU_MAX 517u
#define BR_ATT_VAL_MAX 512u

/* L1: ATT value payload for a negotiated ATT_MTU, or BR_ERR_ARG. */
int32_t br_ble_payload_mtu(uint32_t att_mtu);

/* Length-prefixed stream. */
#define BR_LPS_HDR 4u
int32_t br_lps_encode_hdr(uint32_t len, uint8_t hdr[BR_LPS_HDR]);

typedef struct {
    uint8_t *buf;
    uint32_t cap; /* largest message accepted */
    uint8_t hdr[BR_LPS_HDR];
    uint32_t hn, want, have;
    uint8_t dead;
} br_lps_rx_t;

br_status_t br_lps_rx_init(br_lps_rx_t *rx, uint8_t *buf, uint32_t cap);
void br_lps_rx_reset(br_lps_rx_t *rx);
/* Feed bytes. Stops after one complete message: returns 1 with the
 * message in rx->buf[0..*len) and *used = bytes consumed; 0 when all of
 * `n` was consumed without completing a message; <0 when dead (L2). */
int br_lps_rx_push(br_lps_rx_t *rx, const uint8_t *p, uint32_t n, uint32_t *used, uint32_t *len);

#endif /* ZXV_BR_BLE_H */

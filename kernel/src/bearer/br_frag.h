/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_frag.h — split a message into small link frames and put it back
 * together (used by the BLE ATT path and by LoRa).
 *
 * WIRE FORMAT (every fragment fits in `mtu` bytes)
 *   first   [0x80 | ctr] [idx = 0] [total length, 24-bit big-endian] [data]
 *   later   [ctr]        [idx mod 256]                               [data]
 *   ctr is a 7-bit message counter chosen by the sender (it changes per
 *   message); idx counts fragments from 0.
 *
 * RULES
 *   F1  mtu is at least BR_FRAG_MIN_MTU (6: a 5-byte first header and one
 *       data byte). A message is 1 .. BR_FRAG_MAX_MSG bytes.
 *   F2  Fragments are expected in order (a BLE connection delivers them in
 *       order; LoRa delivers them one by one). A fragment with the wrong
 *       counter or index, a later fragment with no first one, or data
 *       past the announced length drops the partial message and is counted
 *       in `drops`; the receiver then waits for the next first fragment.
 *       A new first fragment always restarts reassembly (resync).
 *   F3  A message longer than the receiver's buffer is refused at the first
 *       fragment, before any data is stored.
 *   F4  No integrity check here: devmesh frames carry their own AEAD or
 *       signatures, and the link layers below have CRCs (BLE CRC-24, LoRa
 *       payload CRC). A lost fragment loses the message; retry is the
 *       caller's job.
 */
#ifndef ZXV_BR_FRAG_H
#define ZXV_BR_FRAG_H

#include "bearer.h"

#define BR_FRAG_FIRST   0x80u
#define BR_FRAG_HDR1    5u
#define BR_FRAG_HDRN    2u
#define BR_FRAG_MIN_MTU 6u
#define BR_FRAG_MAX_MSG 0xFFFFFFu

/* Number of fragments for msg_len at mtu, or 0 if the arguments are bad. */
uint32_t br_frag_count(uint32_t msg_len, uint32_t mtu);

typedef struct {
    const uint8_t *msg;
    uint32_t len, off, idx;
    uint32_t mtu;
    uint8_t ctr;
} br_frag_tx_t;

br_status_t br_frag_tx_init(br_frag_tx_t *tx, const uint8_t *msg, uint32_t len, uint32_t mtu,
                            uint8_t ctr);
/* Write the next fragment. Returns its length, 0 when done, <0 on error. */
int32_t br_frag_tx_next(br_frag_tx_t *tx, uint8_t *out, uint32_t cap);

typedef struct {
    uint8_t *buf;
    uint32_t cap;
    uint32_t want, have, idx;
    uint8_t ctr, active;
    uint32_t drops;
} br_frag_rx_t;

br_status_t br_frag_rx_init(br_frag_rx_t *rx, uint8_t *buf, uint32_t cap);
/* Feed one fragment. 1 = message complete in rx->buf[0..*len), 0 = more
 * needed, <0 = this fragment was refused (partial message dropped). */
int br_frag_rx_push(br_frag_rx_t *rx, const uint8_t *frag, uint32_t n, uint32_t *len);

#endif /* ZXV_BR_FRAG_H */

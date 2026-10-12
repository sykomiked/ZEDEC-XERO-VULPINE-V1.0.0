/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* nicpeer.h — the other end of a fake NIC's cable, shared by the NIC tests.
 *
 * A fake NIC hands every frame it transmits to peer_on_tx(). The peer
 *   - answers a DHCPDISCOVER with a DHCPOFFER of 10.0.2.15 from 10.0.2.2
 *     (what QEMU user networking does), so hw_selftest_all's NIC path runs;
 *   - echoes any frame with EtherType 0x88B5 (IEEE local experimental) back
 *     to its sender (destination = the sender, source = the peer) with the
 *     payload byte for byte, so a test
 *     can check payloads end to end;
 *   - drops everything else.
 * Replies (and frames a test injects) wait in a FIFO the fake's receive
 * path drains into its ring. Header only: every NIC test includes it. */
#ifndef ZXV_NICPEER_H
#define ZXV_NICPEER_H

#include <string.h>
#include "hwfake.h"

#define PEER_Q     128
#define PEER_FRAME 2048

typedef struct peer_frame {
    uint32_t len;
    uint8_t err; /* fault knob: fake marks this frame bad on receive */
    uint8_t b[PEER_FRAME];
} peer_frame_t;

typedef struct peer {
    peer_frame_t q[PEER_Q];
    uint32_t head, count;
    int no_dhcp; /* knob: the DHCP server is down */
    uint32_t seen, discovers, offers, echoes, dropped;
    uint8_t last[PEER_FRAME];
    uint32_t last_len;
} peer_t;

static const uint8_t peer_mac[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};

static inline int peer_push(peer_t *p, const uint8_t *f, uint32_t len, uint8_t err)
{
    if (p->count == PEER_Q || len > PEER_FRAME) return -1;
    peer_frame_t *s = &p->q[(p->head + p->count) % PEER_Q];
    memcpy(s->b, f, len);
    s->len = len;
    s->err = err;
    p->count++;
    return 0;
}

static inline peer_frame_t *peer_front(peer_t *p)
{
    return p->count ? &p->q[p->head] : 0;
}

static inline void peer_pop(peer_t *p)
{
    if (!p->count) return;
    p->head = (p->head + 1) % PEER_Q;
    p->count--;
}

/* A test frame: dst, src, EtherType 0x88B5, then a counting payload seeded by
 * `seed`, total `len` bytes (len >= 14). */
static inline uint32_t peer_mk(uint8_t *f, const uint8_t dst[6], const uint8_t src[6], uint32_t len,
                               uint32_t seed)
{
    memcpy(f, dst, 6);
    memcpy(f + 6, src, 6);
    f[12] = 0x88;
    f[13] = 0xB5;
    for (uint32_t i = 14; i < len; i++) f[i] = (uint8_t) (seed * 31 + i * 7);
    return len;
}

static inline uint32_t peer_offer(uint8_t *f, const uint8_t *disc)
{
    const uint8_t *b = disc + 14 + 20 + 8;
    memset(f, 0, 342);
    memset(f, 0xFF, 6);
    memcpy(f + 6, peer_mac, 6);
    f[12] = 0x08;
    uint8_t *ip = f + 14;
    ip[0] = 0x45;
    ip[2] = (20 + 8 + 300) >> 8;
    ip[3] = (uint8_t) (20 + 8 + 300);
    ip[8] = 64;
    ip[9] = 17;
    ip[12] = 10, ip[13] = 0, ip[14] = 2, ip[15] = 2;
    memset(ip + 16, 0xFF, 4);
    uint8_t *udp = ip + 20;
    udp[1] = 67;
    udp[3] = 68;
    udp[4] = (8 + 300) >> 8;
    udp[5] = (uint8_t) (8 + 300);
    uint8_t *o = udp + 8;
    o[0] = 2; /* BOOTREPLY */
    o[1] = 1;
    o[2] = 6;
    memcpy(o + 4, b + 4, 4); /* xid */
    o[16] = 10, o[17] = 0, o[18] = 2, o[19] = 15;
    memcpy(o + 28, b + 28, 6);
    o[236] = 99, o[237] = 130, o[238] = 83, o[239] = 99;
    o[240] = 53, o[241] = 1, o[242] = 2;
    o[243] = 54, o[244] = 4, o[245] = 10, o[246] = 0, o[247] = 2, o[248] = 2;
    o[249] = 255;
    return 342;
}

static inline void peer_on_tx(peer_t *p, const uint8_t *f, uint32_t len)
{
    p->seen++;
    if (len <= PEER_FRAME) {
        memcpy(p->last, f, len);
        p->last_len = len;
    }
    uint8_t r[PEER_FRAME];
    if (len >= 14 + 20 + 8 + 244 && f[12] == 0x08 && f[13] == 0 && f[14 + 9] == 17 &&
        f[14 + 20 + 3] == 67 && f[14 + 28] == 1 && f[14 + 28 + 240] == 53 &&
        f[14 + 28 + 242] == 1) {
        p->discovers++;
        if (p->no_dhcp) return;
        p->offers++;
        peer_push(p, r, peer_offer(r, f), 0);
        return;
    }
    if (len >= 14 && f[12] == 0x88 && f[13] == 0xB5) {
        p->echoes++;
        memcpy(r, f, len);
        memcpy(r, f + 6, 6);
        memcpy(r + 6, peer_mac, 6);
        peer_push(p, r, len, 0);
        return;
    }
    p->dropped++;
}

#endif

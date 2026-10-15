/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* arb_proto.c — header check, rings and packing for the arbiter mailbox
 * (arb_proto.h has the layout and the rules). */
#include "arb_proto.h"

static inline void fence_release(void)
{
    __atomic_thread_fence(__ATOMIC_RELEASE);
}
static inline void fence_acquire(void)
{
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

static int pow2_cap(uint32_t cap)
{
    return cap >= 2 && cap <= ARB_RING_MAX && (cap & (cap - 1)) == 0;
}

/* Bytes the framebuffers need, or UINT64_MAX when a factor is out of range. */
static uint64_t fb_bytes(uint32_t count, uint32_t stride, uint32_t height)
{
    if (count > ARB_FB_MAX || stride > (1u << 16) || height > (1u << 14)) return UINT64_MAX;
    return (uint64_t) count * stride * height;
}

int arb_header_check(volatile uint8_t *w, uint64_t size)
{
    if (size < ARB_WINDOW_MIN) return ARB_E_SIZE;
    if (arb_rd64(w, ARB_OFF_MAGIC) != ARB_MAGIC) return ARB_E_MAGIC;
    if (arb_rd32(w, ARB_OFF_VERSION) != ARB_VERSION) return ARB_E_VERSION;
    if (!pow2_cap(arb_rd32(w, ARB_OFF_RING_CAP))) return ARB_E_RING;
    if (arb_rd32(w, ARB_OFF_MSG_SIZE) != ARB_MSG_SIZE) return ARB_E_RING;
    uint32_t n = arb_rd32(w, ARB_OFF_FB_COUNT);
    if (n == 0) return 0;
    uint32_t wd = arb_rd32(w, ARB_OFF_FB_WIDTH), ht = arb_rd32(w, ARB_OFF_FB_HEIGHT);
    uint32_t st = arb_rd32(w, ARB_OFF_FB_STRIDE), off = arb_rd32(w, ARB_OFF_FB_OFFSET);
    if (wd == 0 || ht == 0 || st < (uint64_t) wd * 4 || (st & 3) || off < ARB_OFF_FB ||
        (off & 0xFFF))
        return ARB_E_FB;
    uint64_t need = fb_bytes(n, st, ht);
    if (need == UINT64_MAX || (uint64_t) off + need > size) return ARB_E_FB;
    return 0;
}

int arb_header_write(volatile uint8_t *w, uint64_t size, uint32_t ring_cap, uint32_t fb_count,
                     uint32_t fb_width, uint32_t fb_height)
{
    if (size < ARB_WINDOW_MIN) return ARB_E_SIZE;
    if (!pow2_cap(ring_cap)) return ARB_E_RING;
    uint32_t stride = fb_width * 4;
    if (fb_count) {
        if (fb_width == 0 || fb_height == 0 || fb_width > (1u << 14)) return ARB_E_FB;
        uint64_t need = fb_bytes(fb_count, stride, fb_height);
        if (need == UINT64_MAX || (uint64_t) ARB_OFF_FB + need > size) return ARB_E_FB;
    } else {
        fb_width = fb_height = stride = 0;
    }
    /* Everything below the framebuffers starts at zero: doorbells, both rings
     * (head = tail = 0) and every slot. */
    arb_zero(w, ARB_OFF_FB);
    arb_wr32(w, ARB_OFF_VERSION, ARB_VERSION);
    arb_wr32(w, ARB_OFF_RING_CAP, ring_cap);
    arb_wr32(w, ARB_OFF_MSG_SIZE, ARB_MSG_SIZE);
    arb_wr32(w, ARB_OFF_FB_COUNT, fb_count);
    arb_wr32(w, ARB_OFF_FB_WIDTH, fb_width);
    arb_wr32(w, ARB_OFF_FB_HEIGHT, fb_height);
    arb_wr32(w, ARB_OFF_FB_STRIDE, stride);
    arb_wr32(w, ARB_OFF_FB_OFFSET, fb_count ? ARB_OFF_FB : 0);
    /* The magic goes last: a host that sees it sees a complete header. */
    fence_release();
    arb_wr32(w, ARB_OFF_MAGIC, (uint32_t) ARB_MAGIC);
    arb_wr32(w, ARB_OFF_MAGIC + 4, (uint32_t) (ARB_MAGIC >> 32));
    return 0;
}

uint32_t arb_ring_fill(volatile uint8_t *w, uint32_t ring_off)
{
    return arb_rd32(w, ring_off + ARB_RING_HEAD) - arb_rd32(w, ring_off + ARB_RING_TAIL);
}

int arb_ring_put(volatile uint8_t *w, uint32_t ring_off, uint32_t cap, const arb_msg_t *m)
{
    if (m->len > ARB_MSG_PAYLOAD) return -2;
    uint32_t head = arb_rd32(w, ring_off + ARB_RING_HEAD);
    uint32_t tail = arb_rd32(w, ring_off + ARB_RING_TAIL);
    fence_acquire();
    if (head - tail >= cap) return -1;
    uint32_t slot = ring_off + ARB_RING_SLOTS + (head & (cap - 1)) * ARB_MSG_SIZE;
    arb_wr32(w, slot, (uint32_t) m->type | ((uint32_t) m->len << 16));
    arb_wr32(w, slot + 4, m->seq);
    for (uint32_t i = 0; i < ARB_MSG_PAYLOAD; i += 4) {
        uint32_t v = 0;
        for (uint32_t b = 0; b < 4; b++)
            if (i + b < m->len) v |= (uint32_t) m->payload[i + b] << (8 * b);
        arb_wr32(w, slot + 8 + i, v);
    }
    fence_release();
    arb_wr32(w, ring_off + ARB_RING_HEAD, head + 1);
    return 0;
}

int arb_ring_get(volatile uint8_t *w, uint32_t ring_off, uint32_t cap, arb_msg_t *m)
{
    uint32_t head = arb_rd32(w, ring_off + ARB_RING_HEAD);
    uint32_t tail = arb_rd32(w, ring_off + ARB_RING_TAIL);
    fence_acquire();
    if (head == tail) return 0;
    if (head - tail > cap) return -1;
    uint32_t slot = ring_off + ARB_RING_SLOTS + (tail & (cap - 1)) * ARB_MSG_SIZE;
    uint32_t w0 = arb_rd32(w, slot);
    uint16_t len = (uint16_t) (w0 >> 16);
    if (len > ARB_MSG_PAYLOAD) return -1;
    m->type = (uint16_t) w0;
    m->len = len;
    m->seq = arb_rd32(w, slot + 4);
    for (uint32_t i = 0; i < ARB_MSG_PAYLOAD; i += 4) {
        uint32_t v = arb_rd32(w, slot + 8 + i);
        for (uint32_t b = 0; b < 4; b++)
            m->payload[i + b] = i + b < len ? (uint8_t) (v >> (8 * b)) : 0;
    }
    /* A consumed slot is wiped, so a released key does not linger in the
     * window after the host has read it. */
    arb_zero(w + slot, ARB_MSG_SIZE);
    fence_release();
    arb_wr32(w, ring_off + ARB_RING_TAIL, tail + 1);
    return 1;
}

void arb_put32(uint8_t *p, uint32_t off, uint32_t v)
{
    for (uint32_t b = 0; b < 4; b++) p[off + b] = (uint8_t) (v >> (8 * b));
}

uint32_t arb_get32(const uint8_t *p, uint32_t off)
{
    uint32_t v = 0;
    for (uint32_t b = 0; b < 4; b++) v |= (uint32_t) p[off + b] << (8 * b);
    return v;
}

void arb_put64(uint8_t *p, uint32_t off, uint64_t v)
{
    arb_put32(p, off, (uint32_t) v);
    arb_put32(p, off + 4, (uint32_t) (v >> 32));
}

uint64_t arb_get64(const uint8_t *p, uint32_t off)
{
    return (uint64_t) arb_get32(p, off) | ((uint64_t) arb_get32(p, off + 4) << 32);
}

void arb_copy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

void arb_zero(volatile uint8_t *dst, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) dst[i] = 0;
}

int arb_ct_equal(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < n; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

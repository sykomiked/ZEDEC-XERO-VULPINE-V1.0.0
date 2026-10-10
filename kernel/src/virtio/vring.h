/* vring.h — a portable split-virtqueue engine (VIRTIO 1.0)
 *
 * The descriptor-ring bookkeeping — the free list, chained descriptors, the
 * avail/used indices and their wrap-around — is the bug-prone heart of every
 * virtio driver, and it is identical for block, net, and console. So it lives
 * here, MMIO-free and host-testable against a mock device, instead of being
 * re-implemented (and re-broken) per driver. The virtio-net driver is then a
 * thin layer: negotiate, hand this engine the shared rings, and notify.
 *
 * SPLIT VIRTQUEUE, in three shared regions the device also sees:
 *   desc[]  — the descriptor table (addr/len/flags/next)
 *   avail   — the driver->device ring (we publish descriptor-chain heads)
 *   used    — the device->driver ring (the device publishes completions)
 *
 * The engine owns a software free list threaded through desc[].next, so
 * vring_add() allocates a chain and vring_get_used() frees it back — the
 * classic technique, with the ring wrap that trips people up handled once,
 * here, under test.
 *
 * Freestanding: integer only, no libc, no allocation. The caller supplies the
 * three regions (page-aligned, PA==VA, in the real driver) and performs the
 * device notify and any cache maintenance around vring_add/get_used.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV virtio slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_VRING_H
#define ZXV_VRING_H

#include <stdint.h>
#include <stdbool.h>

#define VRING_MAX_DEPTH   64u

/* descriptor flags */
#define VRING_DESC_F_NEXT   1u   /* chains to .next */
#define VRING_DESC_F_WRITE  2u   /* device WRITES this buffer (device-writable) */

typedef struct { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; } vring_desc_t;
typedef struct { uint16_t flags; uint16_t idx; uint16_t ring[VRING_MAX_DEPTH]; uint16_t used_event; } vring_avail_t;
typedef struct { uint32_t id; uint32_t len; } vring_used_elem_t;
typedef struct { uint16_t flags; uint16_t idx; vring_used_elem_t ring[VRING_MAX_DEPTH]; uint16_t avail_event; } vring_used_t;

/* One buffer of a chain to submit. */
typedef struct {
    void    *addr;
    uint32_t len;
    bool     device_writable;   /* true for RX buffers the device fills */
} vring_buf_t;

typedef struct {
    vring_desc_t  *desc;
    vring_avail_t *avail;
    vring_used_t  *used;
    uint16_t depth;             /* <= VRING_MAX_DEPTH; the device's QUEUE_NUM */
    uint16_t free_head;         /* head of the free descriptor list */
    uint16_t num_free;
    uint16_t last_used;         /* used entries consumed so far */
} vring_t;

/* Bind the engine to caller-owned rings and build the free list. Returns
 * false if depth is 0 or exceeds VRING_MAX_DEPTH. */
bool vring_init(vring_t *vr, vring_desc_t *desc, vring_avail_t *avail,
                vring_used_t *used, uint16_t depth);

/* Allocate and publish a descriptor chain for `n` buffers. Returns the head
 * descriptor id (>= 0), or -1 if fewer than `n` descriptors are free. After
 * this the caller must notify the device. */
int32_t vring_add(vring_t *vr, const vring_buf_t *bufs, uint16_t n);

/* Reap one completion: if the device has published a new used entry, return
 * its head descriptor id, write the device-reported length to *len_out, free
 * the whole chain back to the free list, and advance. Returns -1 if nothing
 * new. */
int32_t vring_get_used(vring_t *vr, uint32_t *len_out);

uint16_t vring_num_free(const vring_t *vr);

#endif /* ZXV_VRING_H */

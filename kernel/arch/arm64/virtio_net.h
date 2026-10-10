/* virtio_net.h — virtio-net (virtio-mmio) driver for ARM64
 *
 * A thin driver over the tested split-virtqueue engine (src/virtio/vring):
 * it negotiates the device, hands the engine two shared rings (RX queue 0,
 * TX queue 1), and does the MMIO notify. All ring bookkeeping is the engine's,
 * so it is proven on the host; only the register poking lives here.
 *
 * Poll-driven (no interrupts), matching virtio_blk.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include <stdint.h>
#include <stdbool.h>

#define VNET_MAC_LEN     6u
#define VNET_MAX_FRAME   1526u   /* 1514 ethernet + a little slack */

/* Probe the virtio-mmio transport for a network device and bring it up
 * (queues ready, RX pre-filled). Returns true on success. */
bool virtio_net_init(void);

/* True once a device is up. */
bool virtio_net_present(void);

/* The device MAC address (all-zero if no device). */
const uint8_t *virtio_net_mac(void);

/* Transmit one ethernet frame (`len` bytes, without the virtio-net header —
 * the driver prepends it). Returns 0 on success, negative on error/no-space. */
int virtio_net_tx(const uint8_t *frame, uint32_t len);

/* Poll for one received ethernet frame. Copies up to `max` bytes into `out`
 * and returns the frame length (>0), 0 if nothing is available, <0 on error.
 * The virtio-net header is stripped. */
int virtio_net_rx_poll(uint8_t *out, uint32_t max);

/* Self-check: device present, MAC read, queues ready. For a boot banner. */
bool virtio_net_selftest(void);

#endif /* VIRTIO_NET_H */

/* virtio_blk.h — virtio-blk (virtio-mmio) driver for ARM64
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include <stdint.h>
#include <stdbool.h>
#include "../include/blockdev.h"

/* Probe the virtio-mmio transport for a block device and populate
 * `dev` with sector read/write callbacks. Returns true on success. */
bool virtio_blk_init(block_device_t *dev);

/* Device capacity in 512-byte sectors (0 if no device). */
uint64_t virtio_blk_capacity_sectors(void);

#endif /* VIRTIO_BLK_H */

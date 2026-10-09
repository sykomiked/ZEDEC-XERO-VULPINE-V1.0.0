/* blockdev.h — Generic block device abstraction
 *
 * Abstracts ATA, RAM disk, VirtIO block, and other storage behind
 * a unified sector read/write interface for the filesystem layer.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include <stdint.h>
#include <stdbool.h>

#define BLOCKDEV_SECTOR_SIZE 512

typedef struct block_device {
    bool present;
    uint32_t total_sectors;
    char model[41];

    /* Driver-specific data pointer */
    void *driver_data;

    /* Sector read/write — driver fills these in */
    int (*read_sector)(struct block_device *dev, uint32_t lba, uint8_t *buffer);
    int (*write_sector)(struct block_device *dev, uint32_t lba, const uint8_t *buffer);
} block_device_t;

/* Initialize a block device with driver callbacks */
void blockdev_init(block_device_t *dev,
                    int (*read)(block_device_t *, uint32_t, uint8_t *),
                    int (*write)(block_device_t *, uint32_t, const uint8_t *),
                    uint32_t total_sectors, const char *model);

/* Convenience: read/write sector through the device's callbacks */
static inline int blockdev_read(block_device_t *dev, uint32_t lba, uint8_t *buf) {
    if (!dev || !dev->present || !dev->read_sector) return -1;
    return dev->read_sector(dev, lba, buf);
}

static inline int blockdev_write(block_device_t *dev, uint32_t lba, const uint8_t *buf) {
    if (!dev || !dev->present || !dev->write_sector) return -1;
    return dev->write_sector(dev, lba, buf);
}

#endif /* BLOCKDEV_H */

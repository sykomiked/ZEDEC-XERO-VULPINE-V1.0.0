/* blockdev.c — Generic block device implementation
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "../../include/blockdev.h"
#include <stddef.h>

void blockdev_init(block_device_t *dev,
                    int (*read)(block_device_t *, uint32_t, uint8_t *),
                    int (*write)(block_device_t *, uint32_t, const uint8_t *),
                    uint32_t total_sectors, const char *model) {
    dev->present = true;
    dev->total_sectors = total_sectors;
    dev->read_sector = read;
    dev->write_sector = write;
    dev->driver_data = 0;

    int i;
    for (i = 0; i < 40 && model[i]; i++)
        dev->model[i] = model[i];
    dev->model[i] = 0;
}

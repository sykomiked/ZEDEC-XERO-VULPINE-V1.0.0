/* ramdisk.h — RAM-based block device for ARM64
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef RAMDISK_H
#define RAMDISK_H

#include <stdint.h>
#include <stdbool.h>
#include "../include/blockdev.h"

void ramdisk_init(void);
bool ramdisk_is_present(void);
uint32_t ramdisk_total_sectors(void);
int ramdisk_read_sector(uint32_t lba, uint8_t *buffer);
int ramdisk_write_sector(uint32_t lba, const uint8_t *buffer);
uint8_t *ramdisk_get_buffer(void);
void ramdisk_format_fat32(void);
void ramdisk_create_blockdev(block_device_t *dev);

#endif /* RAMDISK_H */

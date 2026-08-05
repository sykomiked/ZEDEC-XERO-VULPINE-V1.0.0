/* ramdisk.h — RAM-based block device for ARM64
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
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

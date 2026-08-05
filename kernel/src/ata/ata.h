/* ata.h — ATA/IDE disk driver
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_PRIMARY_DATA     0x1F0
#define ATA_PRIMARY_ERROR    0x1F1
#define ATA_PRIMARY_COUNT    0x1F2
#define ATA_PRIMARY_LBA_LO   0x1F3
#define ATA_PRIMARY_LBA_MID  0x1F4
#define ATA_PRIMARY_LBA_HI   0x1F5
#define ATA_PRIMARY_DRIVE    0x1F6
#define ATA_PRIMARY_COMMAND  0x1F7
#define ATA_PRIMARY_CONTROL  0x3F6

#define ATA_SECONDARY_DATA     0x170
#define ATA_SECONDARY_CONTROL  0x376

#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC

#define ATA_SECTOR_SIZE 512

typedef struct ata_device {
    bool present;
    bool is_master;
    uint8_t bus;
    uint16_t data_port;
    uint16_t error_port;
    uint16_t count_port;
    uint16_t lba_lo_port;
    uint16_t lba_mid_port;
    uint16_t lba_hi_port;
    uint16_t drive_port;
    uint16_t command_port;
    uint16_t control_port;
    uint32_t total_sectors;
    char model[41];
} ata_device_t;

typedef struct ata_state {
    ata_device_t devices[4];
    uint32_t num_devices;
} ata_state_t;

void ata_init(ata_state_t *state);
int ata_read_sector(ata_device_t *dev, uint32_t lba, uint8_t *buffer);
int ata_write_sector(ata_device_t *dev, uint32_t lba, const uint8_t *buffer);
int ata_identify(ata_device_t *dev);

#endif

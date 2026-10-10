/* fat32.h — FAT32 filesystem driver
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include "../../include/blockdev.h"

#define FAT32_MAX_FILES 256
#define FAT32_MAX_PATH  256
#define FAT32_ENTRIES_PER_SECTOR 16
/* Largest cluster this driver mounts (8 sectors of 512 bytes): the cluster
 * buffers are on the stack. */
#define FAT32_CLUSTER_MAX 4096u

typedef struct fat32_bpb {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors16;
    uint8_t  media;
    uint16_t fat_size16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors32;
    uint32_t fat_size32;
    uint16_t flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct fat32_dirent {
    char     name[11];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_lo;
    uint32_t size;
} __attribute__((packed)) fat32_dirent_t;

typedef struct fat32_file {
    char name[13];
    uint32_t cluster;
    uint32_t size;
    uint8_t  attr;
    bool     is_dir;
    bool     valid;
} fat32_file_t;

typedef struct fat32_state {
    fat32_bpb_t bpb;
    block_device_t *dev;
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    uint32_t root_cluster;
    uint32_t bytes_per_cluster;
    uint32_t current_dir_cluster;
    fat32_file_t files[FAT32_MAX_FILES];
    uint32_t num_files;
    bool mounted;
} fat32_state_t;

int fat32_mount(fat32_state_t *fs, block_device_t *dev);
int fat32_read_cluster(fat32_state_t *fs, uint32_t cluster, uint8_t *buffer);
uint32_t fat32_next_cluster(fat32_state_t *fs, uint32_t cluster);
int fat32_read_dir(fat32_state_t *fs, uint32_t cluster);
/* Reads the whole file into buffer. Fails (-1) if file->size exceeds cap,
 * the chain is shorter than the size, or a cluster read fails. */
int fat32_read_file(fat32_state_t *fs, const fat32_file_t *file, uint8_t *buffer, uint32_t cap);
fat32_file_t *fat32_find_file(fat32_state_t *fs, const char *name);
int fat32_change_dir(fat32_state_t *fs, const char *name);

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LFN       0x0F

#endif

/* fat32.c — FAT32 filesystem implementation
 * Mount, read directory, read file, traverse clusters via FAT chain.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "fat32.h"
#include "../../include/freestanding.h"

static void fat32_name_to_83(const char *name, char *out) {
    int i = 0, j = 0;
    for (; i < 8 && name[j] && name[j] != '.'; i++, j++)
        out[i] = name[j];
    for (; i < 8; i++) out[i] = ' ';
    if (name[j] == '.') j++;
    for (; i < 11 && name[j]; i++, j++)
        out[i] = name[j];
    for (; i < 11; i++) out[i] = ' ';
}

static void fat32_83_to_name(const char *src, char *out) {
    int j = 0;
    for (int i = 0; i < 8; i++) {
        if (src[i] != ' ') out[j++] = src[i];
    }
    if (src[8] != ' ') {
        out[j++] = '.';
        for (int i = 8; i < 11; i++) {
            if (src[i] != ' ') out[j++] = src[i];
        }
    }
    out[j] = 0;
}

int fat32_mount(fat32_state_t *fs, block_device_t *dev) {
    fs->dev = dev;
    fs->mounted = false;
    if (!dev || !dev->present) return -1;

    uint8_t boot[512];
    if (blockdev_read(dev, 0, boot) != 0) return -1;

    fs_memcpy(&fs->bpb, boot, sizeof(fat32_bpb_t));

    if (fs->bpb.bytes_per_sector == 0 || fs->bpb.sectors_per_cluster == 0)
        return -1;

    fs->fat_start_sector = fs->bpb.reserved_sectors;
    fs->root_cluster = fs->bpb.root_cluster;
    fs->bytes_per_cluster = (uint32_t)fs->bpb.sectors_per_cluster * fs->bpb.bytes_per_sector;
    fs->data_start_sector = fs->bpb.reserved_sectors +
                            fs->bpb.num_fats * fs->bpb.fat_size32;
    fs->current_dir_cluster = fs->root_cluster;
    fs->num_files = 0;
    fs->mounted = true;
    return 0;
}

int fat32_read_cluster(fat32_state_t *fs, uint32_t cluster, uint8_t *buffer) {
    uint32_t sector = fs->data_start_sector +
                      (cluster - 2) * fs->bpb.sectors_per_cluster;
    for (uint32_t i = 0; i < fs->bpb.sectors_per_cluster; i++) {
        if (blockdev_read(fs->dev, sector + i, buffer + i * 512) != 0)
            return -1;
    }
    return 0;
}

uint32_t fat32_next_cluster(fat32_state_t *fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    uint8_t sector_buf[512];
    if (blockdev_read(fs->dev, fat_sector, sector_buf) != 0)
        return 0x0FFFFFFF;

    uint32_t next = *(uint32_t *)(sector_buf + entry_offset);
    return next & 0x0FFFFFFF;
}

int fat32_read_dir(fat32_state_t *fs, uint32_t cluster) {
    fs->num_files = 0;
    uint8_t cluster_buf[4096];

    while (cluster < 0x0FFFFFF8 && fs->num_files < FAT32_MAX_FILES) {
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) return -1;

        fat32_dirent_t *entries = (fat32_dirent_t *)cluster_buf;
        uint32_t num_entries = fs->bytes_per_cluster / sizeof(fat32_dirent_t);

        for (uint32_t i = 0; i < num_entries && fs->num_files < FAT32_MAX_FILES; i++) {
            if (entries[i].name[0] == 0x00) break;
            if (entries[i].name[0] == 0xE5) continue;
            if (entries[i].attr == FAT32_ATTR_LFN) continue;

            fat32_file_t *f = &fs->files[fs->num_files++];
            fat32_83_to_name(entries[i].name, f->name);
            f->cluster = ((uint32_t)entries[i].cluster_hi << 16) | entries[i].cluster_lo;
            f->size = entries[i].size;
            f->attr = entries[i].attr;
            f->is_dir = (entries[i].attr & FAT32_ATTR_DIRECTORY) != 0;
            f->valid = true;
        }

        cluster = fat32_next_cluster(fs, cluster);
    }
    return 0;
}

int fat32_read_file(fat32_state_t *fs, const fat32_file_t *file, uint8_t *buffer) {
    uint32_t cluster = file->cluster;
    uint32_t remaining = file->size;
    uint32_t offset = 0;

    while (cluster < 0x0FFFFFF8 && remaining > 0) {
        uint8_t cluster_buf[4096];
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) return -1;

        uint32_t to_copy = remaining < fs->bytes_per_cluster ? remaining : fs->bytes_per_cluster;
        fs_memcpy(buffer + offset, cluster_buf, to_copy);
        offset += to_copy;
        remaining -= to_copy;
        cluster = fat32_next_cluster(fs, cluster);
    }
    return 0;
}

fat32_file_t *fat32_find_file(fat32_state_t *fs, const char *name) {
    char name83[11];
    fat32_name_to_83(name, name83);

    for (uint32_t i = 0; i < fs->num_files; i++) {
        char file83[11];
        fat32_name_to_83(fs->files[i].name, file83);
        if (fs_strcmp(fs->files[i].name, name) == 0)
            return &fs->files[i];
    }
    return 0;
}

int fat32_change_dir(fat32_state_t *fs, const char *name) {
    fat32_file_t *f = fat32_find_file(fs, name);
    if (!f || !f->is_dir) return -1;
    fs->current_dir_cluster = f->cluster;
    return fat32_read_dir(fs, f->cluster);
}

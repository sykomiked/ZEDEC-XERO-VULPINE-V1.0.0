/* fat32.c — FAT32 filesystem implementation
 * Mount, read directory, read file, traverse clusters via FAT chain.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "fat32.h"
#include "../../include/freestanding.h"

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

    /* The boot sector is untrusted disk input. The cluster buffers below are
     * FAT32_CLUSTER_MAX bytes and the device reads 512-byte sectors, so a BPB
     * that claims anything else would overflow the stack (sectors_per_cluster
     * goes up to 255) or mis-address every cluster. */
    if (fs->bpb.bytes_per_sector != BLOCKDEV_SECTOR_SIZE) return -1;
    if (fs->bpb.sectors_per_cluster == 0 ||
        (fs->bpb.sectors_per_cluster & (fs->bpb.sectors_per_cluster - 1)) != 0)
        return -1;
    if ((uint32_t) fs->bpb.sectors_per_cluster * BLOCKDEV_SECTOR_SIZE > FAT32_CLUSTER_MAX)
        return -1;
    if (fs->bpb.num_fats == 0 || fs->bpb.fat_size32 == 0 || fs->bpb.reserved_sectors == 0)
        return -1;
    if (fs->bpb.root_cluster < 2) return -1;
    if ((uint64_t) fs->bpb.num_fats * fs->bpb.fat_size32 + fs->bpb.reserved_sectors > 0xFFFFFFFFu)
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

/* Number of entries the FAT can hold: the largest cluster number + 1. */
static uint32_t fat32_fat_entries(const fat32_state_t *fs)
{
    uint64_t n = (uint64_t) fs->bpb.fat_size32 * (BLOCKDEV_SECTOR_SIZE / 4u);
    return n > 0x0FFFFFF8u ? 0x0FFFFFF8u : (uint32_t) n;
}

int fat32_read_cluster(fat32_state_t *fs, uint32_t cluster, uint8_t *buffer) {
    /* Clusters 0 and 1 are reserved; (cluster - 2) would wrap. */
    if (!fs || !fs->mounted || cluster < 2 || cluster >= fat32_fat_entries(fs)) return -1;
    uint32_t sector = fs->data_start_sector +
                      (cluster - 2) * fs->bpb.sectors_per_cluster;
    for (uint32_t i = 0; i < fs->bpb.sectors_per_cluster; i++) {
        if (blockdev_read(fs->dev, sector + i, buffer + i * 512) != 0)
            return -1;
    }
    return 0;
}

uint32_t fat32_next_cluster(fat32_state_t *fs, uint32_t cluster) {
    if (!fs || cluster >= fat32_fat_entries(fs)) return 0x0FFFFFFF;
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    uint8_t sector_buf[512];
    if (blockdev_read(fs->dev, fat_sector, sector_buf) != 0)
        return 0x0FFFFFFF;

    uint32_t next = (uint32_t) sector_buf[entry_offset] |
                    ((uint32_t) sector_buf[entry_offset + 1] << 8) |
                    ((uint32_t) sector_buf[entry_offset + 2] << 16) |
                    ((uint32_t) sector_buf[entry_offset + 3] << 24);
    next &= 0x0FFFFFFF;
    /* Free (0) and reserved (1) entries in the middle of a chain are
     * corruption; end the chain rather than reading cluster 0/1. */
    if (next < 2) return 0x0FFFFFFF;
    return next;
}

int fat32_read_dir(fat32_state_t *fs, uint32_t cluster) {
    fs->num_files = 0;
    uint8_t cluster_buf[FAT32_CLUSTER_MAX];
    /* A corrupt FAT can link a chain into a loop. No chain is longer than
     * the FAT has entries, so stop there. */
    uint32_t hops = 0, max_hops = fat32_fat_entries(fs);

    while (cluster < 0x0FFFFFF8 && fs->num_files < FAT32_MAX_FILES) {
        if (hops++ >= max_hops) return -1;
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) return -1;

        fat32_dirent_t *entries = (fat32_dirent_t *)cluster_buf;
        uint32_t num_entries = fs->bytes_per_cluster / sizeof(fat32_dirent_t);

        for (uint32_t i = 0; i < num_entries && fs->num_files < FAT32_MAX_FILES; i++) {
            /* `name` is char[11], and plain char is SIGNED on x86/-m32 while it
             * is UNSIGNED on AArch64. Comparing it against 0xE5 (229) directly
             * is always false where char is signed — its range stops at 127 — so
             * the deleted-entry skip below silently vanished on x86 and every
             * tombstone was imported as a live file, leaking the cluster chains
             * of deleted files and burning FAT32_MAX_FILES slots. The same
             * source therefore behaved DIFFERENTLY per architecture. Read the
             * byte as a byte; do not rely on char's signedness. */
            uint8_t name0 = (uint8_t)entries[i].name[0];
            if (name0 == 0x00) break;      /* end of directory */
            if (name0 == 0xE5) continue;   /* deleted entry */
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

int fat32_read_file(fat32_state_t *fs, const fat32_file_t *file, uint8_t *buffer, uint32_t cap)
{
    if (!fs || !file || !buffer) return -1;
    uint32_t cluster = file->cluster;
    uint32_t remaining = file->size;
    uint32_t offset = 0;
    /* The size comes from the directory entry on disk: never trust it to
     * fit the caller's buffer. */
    if (remaining > cap) return -1;

    while (cluster < 0x0FFFFFF8 && remaining > 0) {
        uint8_t cluster_buf[FAT32_CLUSTER_MAX];
        if (fat32_read_cluster(fs, cluster, cluster_buf) != 0) return -1;

        uint32_t to_copy = remaining < fs->bytes_per_cluster ? remaining : fs->bytes_per_cluster;
        fs_memcpy(buffer + offset, cluster_buf, to_copy);
        offset += to_copy;
        remaining -= to_copy;
        cluster = fat32_next_cluster(fs, cluster);
    }
    /* A chain that ends before the recorded size is a truncated file. */
    return remaining == 0 ? 0 : -1;
}

fat32_file_t *fat32_find_file(fat32_state_t *fs, const char *name) {
    if (!fs || !name) return 0;
    for (uint32_t i = 0; i < fs->num_files; i++) {
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

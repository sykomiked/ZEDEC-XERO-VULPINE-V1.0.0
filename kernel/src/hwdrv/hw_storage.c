/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hw_storage.c — from a class-driver disk to files: GPT, then FAT32.
 *
 * Any hw_blk_t (NVMe, SATA, USB storage) becomes a block_device_t the existing
 * FAT32 driver (kernel/src/fat32) mounts. The GPT header and partition array
 * are checked by CRC32 before anything on them is believed; a disk whose GPT
 * fails its CRC is reported and left alone, never mounted. */
#include "hw_storage.h"

static const uint8_t ESP_GUID[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                                     0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};

uint32_t hw_crc32(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static uint32_t le32(const uint8_t *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) |
           ((uint32_t) b[3] << 24);
}

static uint64_t le64(const uint8_t *b)
{
    return (uint64_t) le32(b) | ((uint64_t) le32(b + 4) << 32);
}

int hw_gpt_find_esp(hw_blk_t *b, hw_part_t *out, uint8_t *scratch)
{
    uint32_t bs = b->block_size;
    if (bs < 512 || bs > 4096 || b->blocks < 3) return HW_GPT_NONE;
    if (b->read(b, 1, 1, scratch) != 0) return HW_GPT_IO;
    if (!hw_memeq(scratch, "EFI PART", 8)) return HW_GPT_NONE;
    uint32_t hsize = le32(scratch + 12);
    if (hsize < 92 || hsize > bs) return HW_GPT_BAD;
    uint32_t want = le32(scratch + 16);
    scratch[16] = scratch[17] = scratch[18] = scratch[19] = 0;
    if (hw_crc32(0, scratch, hsize) != want) return HW_GPT_BAD;
    uint64_t ent_lba = le64(scratch + 72);
    uint32_t nent = le32(scratch + 80), esz = le32(scratch + 84), ecrc = le32(scratch + 88);
    /* Entry size is a multiple of 128 (UEFI 2.10 5.3.2); cap the array at
     * 1 MB so a hostile header cannot make us read the whole disk. */
    if (esz < 128 || esz % 128 || esz > bs || nent == 0 || nent > (1u << 20) / esz)
        return HW_GPT_BAD;
    uint32_t total = nent * esz, per = bs / esz;
    uint64_t nblk = (total + bs - 1) / bs;
    if (ent_lba < 2 || ent_lba >= b->blocks || nblk > b->blocks - ent_lba) return HW_GPT_BAD;

    /* One pass: CRC every entry byte, remember the first ESP. */
    uint32_t crc = 0, seen = 0;
    int found = 0;
    for (uint64_t k = 0; k < nblk; k++) {
        if (b->read(b, ent_lba + k, 1, scratch) != 0) return HW_GPT_IO;
        for (uint32_t e = 0; e < per && seen < nent; e++, seen++) {
            const uint8_t *ent = scratch + e * esz;
            crc = hw_crc32(crc, ent, esz);
            if (!found && hw_memeq(ent, ESP_GUID, 16)) {
                out->index = seen + 1;
                out->first = le64(ent + 32);
                out->last = le64(ent + 40);
                found = 1;
            }
        }
    }
    if (crc != ecrc) return HW_GPT_BAD;
    if (!found) return HW_GPT_NO_ESP;
    if (out->first > out->last || out->last >= b->blocks) return HW_GPT_BAD;
    return HW_GPT_OK;
}

/* block_device_t callbacks: 512-byte sectors relative to the partition. */
static int bdev_read(block_device_t *dev, uint32_t lba, uint8_t *buf)
{
    hw_bdev_t *d = (hw_bdev_t *) dev->driver_data;
    if (lba >= d->count) return -1;
    return d->b->read(d->b, d->base + lba, 1, buf);
}

static int bdev_write(block_device_t *dev, uint32_t lba, const uint8_t *buf)
{
    hw_bdev_t *d = (hw_bdev_t *) dev->driver_data;
    if (lba >= d->count || !d->b->write) return -1;
    return d->b->write(d->b, d->base + lba, 1, buf);
}

int hw_bdev_init(hw_bdev_t *d, hw_blk_t *b, uint64_t base, uint64_t count)
{
    /* The FAT32 driver and block_device_t speak 512-byte sectors and 32-bit
     * sector numbers; a 4Kn disk or a partition past 2 TB needs zxvfs. */
    if (b->block_size != BLOCKDEV_SECTOR_SIZE || count == 0 || count > 0xFFFFFFFFu ||
        base >= b->blocks || count > b->blocks - base)
        return -1;
    d->b = b;
    d->base = base;
    d->count = count;
    hw_memset(&d->bd, 0, sizeof d->bd);
    d->bd.present = 1;
    d->bd.total_sectors = (uint32_t) count;
    d->bd.driver_data = d;
    d->bd.read_sector = bdev_read;
    d->bd.write_sector = bdev_write;
    for (uint32_t i = 0; i < sizeof b->name && b->name[i] && i < 40; i++)
        d->bd.model[i] = b->name[i];
    return 0;
}

static const char *gpt_why(int rc)
{
    switch (rc) {
    case HW_GPT_NONE:
        return "no GPT";
    case HW_GPT_IO:
        return "read error";
    case HW_GPT_BAD:
        return "GPT failed its CRC or bounds check, left alone";
    case HW_GPT_NO_ESP:
        return "GPT has no EFI system partition";
    }
    return "?";
}

uint32_t hw_storage_selftest(const hw_plat_t *p, hw_registry_t *r, const char *path)
{
    static uint8_t scratch[4096];
    static uint8_t file[4096];
    static fat32_state_t fs;
    static hw_bdev_t bdev;
    uint32_t ok = 0;
    for (uint32_t i = 0; i < r->n_blk; i++) {
        hw_blk_t *b = r->blk[i];
        hw_line_t l;
        hw_line_init(&l);
        hw_part_t part;
        int rc = hw_gpt_find_esp(b, &part, scratch);
        if (rc != HW_GPT_OK) {
            hw_line_s(&l, rc == HW_GPT_BAD || rc == HW_GPT_IO ? "  [STORAGE FAILED] "
                                                              : "  [STORAGE NONE] ");
            hw_line_s(&l, b->name);
            hw_line_c(&l, ' ');
            hw_line_s(&l, gpt_why(rc));
            p->log(l.b);
            continue;
        }
        hw_line_s(&l, b->name);
        hw_line_s(&l, " ESP (GPT partition ");
        hw_line_u(&l, part.index);
        hw_line_s(&l, ", LBA ");
        hw_line_u(&l, part.first);
        hw_line_s(&l, "..");
        hw_line_u(&l, part.last);
        hw_line_s(&l, ")");
        hw_line_t out;
        hw_line_init(&out);
        fat32_file_t *f = 0;
        int mounted = hw_bdev_init(&bdev, b, part.first, part.last - part.first + 1) == 0 &&
                      fat32_mount(&fs, &bdev.bd) == 0 && fat32_read_dir(&fs, fs.root_cluster) == 0;
        if (mounted) f = fat32_find_file(&fs, path);
        if (!mounted) {
            hw_line_s(&out, "  [STORAGE FAILED] ");
            hw_line_s(&out, l.b);
            hw_line_s(&out, " is not a FAT32 volume this driver can mount");
        } else if (!f || f->is_dir) {
            hw_line_s(&out, "  [STORAGE OK] ");
            hw_line_s(&out, l.b);
            hw_line_s(&out, " FAT32 mounted; /");
            hw_line_s(&out, path);
            hw_line_s(&out, " not on it");
            ok++;
        } else if (f->size > sizeof file || fat32_read_file(&fs, f, file, sizeof file) != 0) {
            hw_line_s(&out, "  [STORAGE FAILED] ");
            hw_line_s(&out, l.b);
            hw_line_s(&out, " FAT32 mounted; reading /");
            hw_line_s(&out, path);
            hw_line_s(&out, " failed");
        } else {
            hw_line_s(&out, "  [STORAGE OK] ");
            hw_line_s(&out, l.b);
            hw_line_s(&out, " FAT32 mounted; read /");
            hw_line_s(&out, path);
            hw_line_c(&out, ' ');
            hw_line_u(&out, f->size);
            hw_line_s(&out, " bytes, crc32 ");
            hw_line_x(&out, hw_crc32(0, file, f->size), 8);
            ok++;
        }
        p->log(out.b);
    }
    return ok;
}

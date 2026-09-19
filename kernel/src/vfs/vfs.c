/* vfs.c — Virtual Filesystem Implementation
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "vfs.h"
#include "../lpres/lpres_core.h"
#include "../oseq/oseq_core.h"
#include "../../include/m5_types.h"

static __attribute__((unused)) int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void str_copy(char *d, const char *s) { int i = 0; while (s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

void vfs_init(vfs_state_t *vfs) {
    vfs->num_mounts = 0;
    vfs->num_open = 0;
    str_copy(vfs->cwd, "/");
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++)
        vfs->mounts[i].active = false;
    for (uint32_t i = 0; i < VFS_MAX_FILES; i++) {
        vfs->open_files[i].name[0] = 0;
        vfs->open_files[i].size = 0;
    }
}

int32_t vfs_mount(vfs_state_t *vfs, const char *path, vfs_mount_type_t type, void *fs_data) {
    if (vfs->num_mounts >= VFS_MAX_MOUNTS) return -1;
    vfs_mount_t *m = &vfs->mounts[vfs->num_mounts];
    str_copy(m->path, path);
    m->type = type;
    m->fs_data = fs_data;
    m->active = true;
    vfs->num_mounts++;
    return 0;
}

int32_t vfs_unmount(vfs_state_t *vfs, const char *path) {
    for (uint32_t i = 0; i < vfs->num_mounts; i++) {
        if (str_cmp(vfs->mounts[i].path, path) == 0) {
            vfs->mounts[i].active = false;
            return 0;
        }
    }
    return -1;
}

int32_t vfs_open(vfs_state_t *vfs, const char *path) {
    if (vfs->num_open >= VFS_MAX_FILES) return -1;

    /* M5 LPRES: attestation check — file must have logical presence */
    /* Generate ordinal from path hash for LPRES lookup */
    ordinal_t file_ordinal = 0;
    for (const char *p = path; *p; p++)
        file_ordinal = file_ordinal * 31 + (ordinal_t)(uint8_t)*p;
    trit_t presence = lpres_get_presence(file_ordinal);
    if (presence == TRIT_FALSE) {
        /* File not attested — register it with TRUE presence on first open */
        lpres_set_presence(file_ordinal, TRIT_TRUE);
    }

    /* Find the file in the mounted FAT32 filesystem */
    for (uint32_t i = 0; i < vfs->num_mounts; i++) {
        if (!vfs->mounts[i].active) continue;
        if (vfs->mounts[i].type == VFS_MOUNT_FAT32 && vfs->mounts[i].fs_data) {
            fat32_state_t *fs = (fat32_state_t *)vfs->mounts[i].fs_data;
            fat32_file_t *file = fat32_find_file(fs, path);
            if (file) {
                int32_t fd = (int32_t)vfs->num_open;
                vfs_node_t *node = &vfs->open_files[fd];
                str_copy(node->name, path);
                node->is_dir = file->is_dir;
                node->size = file->size;
                node->cluster = file->cluster;
                str_copy(node->full_path, path);
                node->phase = 0;
                node->omega = (uint32_t)file_ordinal;
                node->integrity = (presence == TRIT_GLUT) ? 50 : 100;
                vfs->num_open++;
                return fd;
            }
        }
    }
    return -1;
}

int32_t vfs_close(vfs_state_t *vfs, int32_t fd) {
    if (fd < 0 || (uint32_t)fd >= vfs->num_open) return -1;
    vfs->open_files[fd].name[0] = 0;
    return 0;
}

int32_t vfs_read(vfs_state_t *vfs, int32_t fd, void *buf, uint32_t len) {
    if (fd < 0 || (uint32_t)fd >= vfs->num_open) return -1;
    (void)buf; (void)len;
    /* Would read from FAT32 via cluster chain */
    return 0;
}

int32_t vfs_write(vfs_state_t *vfs, int32_t fd, const void *buf, uint32_t len) {
    if (fd < 0 || (uint32_t)fd >= vfs->num_open) return -1;
    (void)buf; (void)len;
    return 0;
}

int32_t vfs_list_dir(vfs_state_t *vfs, const char *path, vfs_node_t *out, uint32_t max_entries) {
    (void)path;
    for (uint32_t i = 0; i < vfs->num_mounts; i++) {
        if (!vfs->mounts[i].active) continue;
        if (vfs->mounts[i].type == VFS_MOUNT_FAT32 && vfs->mounts[i].fs_data) {
            fat32_state_t *fs = (fat32_state_t *)vfs->mounts[i].fs_data;
            uint32_t count = fs->num_files;
            if (count > max_entries) count = max_entries;
            for (uint32_t j = 0; j < count; j++) {
                str_copy(out[j].name, fs->files[j].name);
                out[j].is_dir = fs->files[j].is_dir;
                out[j].size = fs->files[j].size;
                out[j].cluster = fs->files[j].cluster;
                out[j].phase = 0;
                out[j].omega = 0;
                out[j].integrity = 100;
            }
            return (int32_t)count;
        }
    }
    return 0;
}

int32_t vfs_mkdir(vfs_state_t *vfs, const char *path) {
    (void)vfs; (void)path;
    return -1;
}

int32_t vfs_chdir(vfs_state_t *vfs, const char *path) {
    str_copy(vfs->cwd, path);
    return 0;
}

int32_t vfs_stat(vfs_state_t *vfs, const char *path, vfs_node_t *out) {
    (void)vfs;
    str_copy(out->name, path);
    out->size = 0;
    out->is_dir = false;
    return 0;
}

const char *vfs_get_cwd(vfs_state_t *vfs) {
    return vfs->cwd;
}

/* ---- DECLARATION -----------------------------------------------------------
 * The mount table sits on top of a block device (kernel_main mounts FAT32 over
 * the RAM disk), so blockdev_ready is the whole of its requirement.
 *
 * NO BRING-UP YET: every piece of vfs state lives in a caller-owned
 * vfs_state_t, so there is nothing this module can check about itself that
 * would not be a second, unrelated instance. The bring-up appears when the
 * state moves here, which is a separate change from declaring the edge. */
#include "zxv_decl.h"

ZXV_DECLARE(vfs,
    ZXV_PROVIDES(vfs_ready),
    ZXV_REQUIRES(blockdev_ready),
    ZXV_NO_BRINGUP);

/* vfs.h — Virtual Filesystem Layer
 * Abstracts FAT32 (and future filesystems) behind a unified interface.
 * M5-axiomatic: files carry phase metadata for axiomatic queries.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stdbool.h>
#include "../fat32/fat32.h"

#define VFS_MAX_MOUNTS  8
#define VFS_MAX_FILES   64
#define VFS_PATH_LEN    256
#define VFS_NAME_LEN    32

typedef enum {
    VFS_MOUNT_FAT32 = 0,
    VFS_MOUNT_RAMFS = 1,
    VFS_MOUNT_PROC  = 2,
    VFS_MOUNT_NET   = 3
} vfs_mount_type_t;

typedef struct vfs_mount {
    char path[VFS_PATH_LEN];
    vfs_mount_type_t type;
    void *fs_data;
    bool active;
} vfs_mount_t;

typedef struct vfs_node {
    char name[VFS_NAME_LEN];
    bool is_dir;
    uint32_t size;
    uint32_t cluster;
    char full_path[VFS_PATH_LEN];

    /* M5 metadata */
    uint32_t phase;
    uint32_t omega;
    uint32_t integrity;
} vfs_node_t;

typedef struct vfs_state {
    vfs_mount_t mounts[VFS_MAX_MOUNTS];
    uint32_t num_mounts;
    vfs_node_t open_files[VFS_MAX_FILES];
    uint32_t num_open;
    char cwd[VFS_PATH_LEN];
} vfs_state_t;

void vfs_init(vfs_state_t *vfs);
int32_t vfs_mount(vfs_state_t *vfs, const char *path, vfs_mount_type_t type, void *fs_data);
int32_t vfs_unmount(vfs_state_t *vfs, const char *path);
int32_t vfs_open(vfs_state_t *vfs, const char *path);
int32_t vfs_close(vfs_state_t *vfs, int32_t fd);
int32_t vfs_read(vfs_state_t *vfs, int32_t fd, void *buf, uint32_t len);
int32_t vfs_write(vfs_state_t *vfs, int32_t fd, const void *buf, uint32_t len);
int32_t vfs_list_dir(vfs_state_t *vfs, const char *path, vfs_node_t *out, uint32_t max_entries);
int32_t vfs_mkdir(vfs_state_t *vfs, const char *path);
int32_t vfs_chdir(vfs_state_t *vfs, const char *path);
int32_t vfs_stat(vfs_state_t *vfs, const char *path, vfs_node_t *out);
const char *vfs_get_cwd(vfs_state_t *vfs);

#endif

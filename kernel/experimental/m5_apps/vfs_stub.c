/* Minimal in-memory single-file VFS stub for host-testing notes_app.c
 * without pulling in the real fat32/ata hardware chain (matching this
 * codebase's own R9 testing guideline: host tests use a stub platform). */
#include "vfs.h"
#include <string.h>

#define STUB_MAX_FILES 8
#define STUB_FILE_SIZE 8192

static char stub_names[STUB_MAX_FILES][64];
static char stub_data[STUB_MAX_FILES][STUB_FILE_SIZE];
static uint32_t stub_len[STUB_MAX_FILES];
static bool stub_used[STUB_MAX_FILES];
static bool stub_open[STUB_MAX_FILES];

void vfs_init(vfs_state_t *vfs) {
    (void)vfs;
    for (int i = 0; i < STUB_MAX_FILES; i++) { stub_used[i] = false; stub_open[i] = false; stub_len[i] = 0; }
}

int32_t vfs_open(vfs_state_t *vfs, const char *path) {
    (void)vfs;
    for (int i = 0; i < STUB_MAX_FILES; i++) {
        if (stub_used[i] && strcmp(stub_names[i], path) == 0) { stub_open[i] = true; return i; }
    }
    for (int i = 0; i < STUB_MAX_FILES; i++) {
        if (!stub_used[i]) {
            stub_used[i] = true; stub_open[i] = true; stub_len[i] = 0;
            strncpy(stub_names[i], path, sizeof(stub_names[i]) - 1);
            stub_names[i][sizeof(stub_names[i]) - 1] = '\0';
            return i;
        }
    }
    return -1;
}

int32_t vfs_read(vfs_state_t *vfs, int32_t fd, void *buf, uint32_t len) {
    (void)vfs;
    if (fd < 0 || fd >= STUB_MAX_FILES || !stub_open[fd]) return -1;
    uint32_t n = stub_len[fd] < len ? stub_len[fd] : len;
    memcpy(buf, stub_data[fd], n);
    return (int32_t)n;
}

int32_t vfs_write(vfs_state_t *vfs, int32_t fd, const void *buf, uint32_t len) {
    (void)vfs;
    if (fd < 0 || fd >= STUB_MAX_FILES || !stub_open[fd]) return -1;
    uint32_t n = len < STUB_FILE_SIZE ? len : STUB_FILE_SIZE;
    memcpy(stub_data[fd], buf, n);
    stub_len[fd] = n;
    return (int32_t)n;
}

int32_t vfs_close(vfs_state_t *vfs, int32_t fd) {
    (void)vfs;
    if (fd >= 0 && fd < STUB_MAX_FILES) { stub_open[fd] = false; return 0; }
    return -1;
}

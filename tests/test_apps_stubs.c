/* test_apps_stubs.c — Stubs for host testing of apps
 * Provides empty implementations of VFS and GUI functions
 * that have x86 or FAT32 dependencies.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* VFS stubs */
typedef struct vfs_state vfs_state_t;
typedef struct vfs_node vfs_node_t;

void vfs_init(void *vfs) { (void)vfs; }
int32_t vfs_mount(void *vfs, const char *path, uint32_t type, void *data) { (void)vfs;(void)path;(void)type;(void)data; return 0; }
int32_t vfs_unmount(void *vfs, const char *path) { (void)vfs;(void)path; return 0; }
int32_t vfs_open(void *vfs, const char *path) { (void)vfs;(void)path; return -1; }
int32_t vfs_close(void *vfs, int32_t fd) { (void)vfs;(void)fd; return 0; }
int32_t vfs_read(void *vfs, int32_t fd, void *buf, uint32_t len) { (void)vfs;(void)fd;(void)buf;(void)len; return 0; }
int32_t vfs_write(void *vfs, int32_t fd, const void *buf, uint32_t len) { (void)vfs;(void)fd;(void)buf;(void)len; return 0; }
int32_t vfs_list_dir(void *vfs, const char *path, void *out, uint32_t max) { (void)vfs;(void)path;(void)out;(void)max; return 0; }
int32_t vfs_mkdir(void *vfs, const char *path) { (void)vfs;(void)path; return 0; }
int32_t vfs_chdir(void *vfs, const char *path) { (void)vfs;(void)path; return 0; }
int32_t vfs_stat(void *vfs, const char *path, void *out) { (void)vfs;(void)path;(void)out; return -1; }
const char *vfs_get_cwd(void *vfs) { (void)vfs; return "/"; }

/* GUI stubs */
typedef struct gui_desktop gui_desktop_t;
void gui_init(void *gui, void *vbe) { (void)gui;(void)vbe; }
uint32_t gui_create_window(void *gui, const char *title, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t bg) {
    (void)gui;(void)title;(void)x;(void)y;(void)w;(void)h;(void)bg; return 0;
}
void gui_render(void *gui) { (void)gui; }
void gui_set_status(void *gui, const char *text) { (void)gui;(void)text; }
void gui_close_window(void *gui, uint32_t win) { (void)gui;(void)win; }
void gui_bring_to_front(void *gui, uint32_t win) { (void)gui;(void)win; }

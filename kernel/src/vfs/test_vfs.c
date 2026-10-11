/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vfs.c — known-answer tests for the virtual filesystem layer (vfs.h)
 * over a real fat32_state_t file table and the real LPRES presence lattice:
 * init defaults, the 8-mount and 64-open limits, bounded path/name copies,
 * mount/unmount lookup, open's path-hash ordinal (h = h*31 + byte), the
 * first-open presence attestation and the GLUT half-integrity rule, the
 * mount filter (inactive, non-FAT32, NULL fs_data), fd range checks on
 * close/read/write, list_dir clamping, and the mkdir/chdir/stat/cwd stubs. */
#include <stdio.h>
#include <string.h>
#include "vfs.h"
#include "../lpres/lpres_core.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

static vfs_state_t V;
static fat32_state_t FS, FS2;

static uint64_t path_hash(const char *p)
{
    uint64_t h = 0;
    for (; *p; p++) h = h * 31 + (uint8_t) *p;
    return h;
}

static void add_file(fat32_state_t *fs, const char *name, uint32_t cluster, uint32_t size, bool dir)
{
    fat32_file_t *f = &fs->files[fs->num_files++];
    memset(f, 0, sizeof *f);
    strncpy(f->name, name, sizeof f->name - 1);
    f->cluster = cluster;
    f->size = size;
    f->is_dir = dir;
    f->valid = true;
}

static void fresh(void)
{
    /* garbage everywhere, but every bool holds a valid value */
    memset(&V, 0x5A, sizeof V);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) V.mounts[i].active = true;
    for (int i = 0; i < VFS_MAX_FILES; i++) V.open_files[i].is_dir = true;
    vfs_init(&V);
}

static void test_init_mount(void)
{
    fresh();
    CHECK(V.num_mounts == 0 && V.num_open == 0, "init counts");
    CHECK(strcmp(V.cwd, "/") == 0, "init cwd");
    CHECK(strcmp(vfs_get_cwd(&V), "/") == 0, "get_cwd");
    CHECK(vfs_get_cwd(&V) == V.cwd, "get_cwd returns the state buffer");
    bool all_inactive = true;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) all_inactive = all_inactive && !V.mounts[i].active;
    CHECK(all_inactive, "all mounts inactive");
    bool all_clear = true;
    for (int i = 0; i < VFS_MAX_FILES; i++)
        all_clear = all_clear && V.open_files[i].name[0] == 0 && V.open_files[i].size == 0;
    CHECK(all_clear, "all open-file slots cleared");
    bool tail_untouched = true;
    for (int i = 2; i < VFS_PATH_LEN; i++) tail_untouched = tail_untouched && V.cwd[i] == 0x5A;
    CHECK(tail_untouched, "init writes nothing past the tables (cwd tail intact)");

    int tag;
    CHECK(vfs_mount(&V, "/mnt/a", VFS_MOUNT_RAMFS, &tag) == 0, "mount a");
    CHECK(V.num_mounts == 1, "one mount");
    CHECK(strcmp(V.mounts[0].path, "/mnt/a") == 0, "mount path");
    CHECK(V.mounts[0].type == VFS_MOUNT_RAMFS, "mount type");
    CHECK(V.mounts[0].fs_data == &tag, "mount data");
    CHECK(V.mounts[0].active, "mount active");

    char longp[400];
    memset(longp, 'p', sizeof longp);
    longp[399] = 0;
    CHECK(vfs_mount(&V, longp, VFS_MOUNT_PROC, NULL) == 0, "mount long path");
    CHECK(strlen(V.mounts[1].path) == VFS_PATH_LEN - 1, "mount path truncated to 255");
    CHECK(vfs_mount(&V, NULL, VFS_MOUNT_NET, NULL) == 0, "mount NULL path");
    CHECK(V.mounts[2].path[0] == 0, "NULL path is empty");

    while (V.num_mounts < VFS_MAX_MOUNTS - 1) vfs_mount(&V, "/x", VFS_MOUNT_NET, NULL);
    CHECK(vfs_mount(&V, "/last", VFS_MOUNT_NET, NULL) == 0, "8th mount fits");
    CHECK(V.num_mounts == 8, "eight mounts");
    CHECK(vfs_mount(&V, "/over", VFS_MOUNT_NET, NULL) == -1, "9th refused");
    CHECK(V.num_mounts == 8, "still eight");

    CHECK(vfs_unmount(&V, "/last") == 0, "unmount last slot");
    CHECK(!V.mounts[7].active && V.mounts[0].active, "only last deactivated");
    CHECK(vfs_unmount(&V, "/mnt/a") == 0, "unmount first");
    CHECK(!V.mounts[0].active, "first deactivated");
    CHECK(vfs_unmount(&V, "/mnt") == -1, "prefix is not a match");
    CHECK(vfs_unmount(&V, "/mnt/ab") == -1, "longer is not a match");
    CHECK(vfs_unmount(&V, "/nowhere") == -1, "unknown path");
    CHECK(V.num_mounts == 8, "unmount keeps count");

    /* only slots below num_mounts are searched */
    fresh();
    vfs_mount(&V, "/a", VFS_MOUNT_NET, NULL);
    strcpy(V.mounts[1].path, "/ghost");
    V.mounts[1].active = true;
    CHECK(vfs_unmount(&V, "/ghost") == -1, "slot past num_mounts not matched");
    CHECK(V.mounts[1].active, "ghost slot untouched");
}

static void test_open(void)
{
    lpres_init();
    memset(&FS, 0, sizeof FS);
    add_file(&FS, "HELLO.TXT", 7, 1234, false);
    add_file(&FS, "DOCS", 9, 0, true);
    add_file(&FS, "GLUT.BIN", 11, 42, false);

    fresh();
    /* no mounts: not found, but the path is still attested on first sight */
    uint64_t hh = path_hash("HELLO.TXT");
    CHECK(lpres_get_presence(hh) == TRIT_FALSE, "absent before open");
    CHECK(vfs_open(&V, "HELLO.TXT") == -1, "no mounts -> -1");
    CHECK(lpres_get_presence(hh) == TRIT_TRUE, "first open attests TRUE");
    CHECK(V.num_open == 0, "nothing opened");

    vfs_mount(&V, "/", VFS_MOUNT_FAT32, &FS);
    lpres_init();
    int32_t fd = vfs_open(&V, "HELLO.TXT");
    CHECK(fd == 0, "first fd 0");
    CHECK(V.num_open == 1, "one open");
    vfs_node_t *n = &V.open_files[0];
    CHECK(strcmp(n->name, "HELLO.TXT") == 0, "node name");
    CHECK(strcmp(n->full_path, "HELLO.TXT") == 0, "node path");
    CHECK(!n->is_dir && n->size == 1234 && n->cluster == 7, "node file fields");
    CHECK(n->phase == 0, "phase 0");
    CHECK(n->omega == (uint32_t) hh, "omega is the path hash");
    CHECK(n->omega == 0xED69A714u, "omega known answer");
    CHECK(n->integrity == 100, "absent-then-attested is full integrity");
    CHECK(lpres_get_presence(hh) == TRIT_TRUE, "attested");

    fd = vfs_open(&V, "HELLO.TXT");
    CHECK(fd == 1 && V.open_files[1].integrity == 100, "reopen: fd 1, TRUE presence -> 100");
    CHECK(lpres_get_presence(hh) == TRIT_TRUE, "still TRUE");

    fd = vfs_open(&V, "DOCS");
    CHECK(fd == 2 && V.open_files[2].is_dir && V.open_files[2].cluster == 9, "dir node");
    CHECK(V.open_files[2].size == 0, "dir size 0");

    uint64_t gh = path_hash("GLUT.BIN");
    lpres_set_presence(gh, TRIT_GLUT);
    fd = vfs_open(&V, "GLUT.BIN");
    CHECK(fd == 3, "glut fd");
    CHECK(V.open_files[3].integrity == 50, "GLUT presence halves integrity");
    CHECK(V.open_files[3].size == 42 && V.open_files[3].cluster == 11, "glut node");
    CHECK(lpres_get_presence(gh) == TRIT_GLUT, "GLUT is not overwritten by open");

    CHECK(vfs_open(&V, "MISSING") == -1, "missing file");
    CHECK(vfs_open(&V, "hello.txt") == -1, "lookup is exact");
    CHECK(V.num_open == 4, "failed opens do not consume an fd");
    CHECK(lpres_get_presence(path_hash("MISSING")) == TRIT_TRUE, "missing still attested");

    /* empty path hashes to 0 */
    CHECK(vfs_open(&V, "") == -1, "empty path");
    CHECK(lpres_get_presence(0) == TRIT_TRUE, "ordinal 0 attested");
}

static void test_mount_filter(void)
{
    lpres_init();
    memset(&FS, 0, sizeof FS);
    memset(&FS2, 0, sizeof FS2);
    add_file(&FS, "A", 100, 1, false);
    add_file(&FS2, "A", 200, 2, false);
    add_file(&FS2, "B", 300, 3, false);

    fresh();
    vfs_mount(&V, "/r", VFS_MOUNT_RAMFS, &FS);  /* not FAT32 */
    vfs_mount(&V, "/n", VFS_MOUNT_FAT32, NULL); /* no fs_data */
    vfs_mount(&V, "/i", VFS_MOUNT_FAT32, &FS);  /* will be inactive */
    vfs_mount(&V, "/b", VFS_MOUNT_FAT32, &FS2);
    vfs_unmount(&V, "/i");
    int32_t fd = vfs_open(&V, "A");
    CHECK(fd == 0, "found on the active FAT32 mount");
    CHECK(V.open_files[0].cluster == 200 && V.open_files[0].size == 2, "from FS2, not FS");

    vfs_node_t out[4];
    memset(out, 0x77, sizeof out);
    CHECK(vfs_list_dir(&V, "/", out, 4) == 2, "list FS2: 2 entries");
    CHECK(strcmp(out[0].name, "A") == 0 && strcmp(out[1].name, "B") == 0, "list names");
    CHECK(out[1].cluster == 300 && out[1].size == 3 && !out[1].is_dir, "list fields");
    CHECK(out[0].phase == 0 && out[0].omega == 0 && out[0].integrity == 100, "list m5");
    CHECK(out[2].cluster == 0x77777777u, "list writes no more than count");

    /* the first usable FAT32 mount wins */
    fresh();
    vfs_mount(&V, "/a", VFS_MOUNT_FAT32, &FS);
    vfs_mount(&V, "/b", VFS_MOUNT_FAT32, &FS2);
    CHECK(vfs_open(&V, "A") == 0 && V.open_files[0].cluster == 100, "first mount wins");
    CHECK(vfs_open(&V, "B") == 1 && V.open_files[1].cluster == 300, "falls through to second");
    CHECK(vfs_list_dir(&V, "/", out, 4) == 1, "list uses first mount only");

    /* list_dir clamps to max_entries */
    memset(out, 0x77, sizeof out);
    vfs_unmount(&V, "/a");
    CHECK(vfs_list_dir(&V, "/", out, 1) == 1, "clamped to 1");
    CHECK(strcmp(out[0].name, "A") == 0 && out[1].cluster == 0x77777777u, "only one written");
    CHECK(vfs_list_dir(&V, "/", out, 2) == 2, "exactly 2");
    CHECK(vfs_list_dir(&V, "/", out, 0) == 0, "max 0");

    /* no usable mounts */
    fresh();
    vfs_mount(&V, "/r", VFS_MOUNT_RAMFS, &FS2);
    vfs_mount(&V, "/n", VFS_MOUNT_FAT32, NULL);
    CHECK(vfs_list_dir(&V, "/", out, 4) == 0, "nothing to list");
    CHECK(vfs_open(&V, "B") == -1, "nothing to open");

    /* mount slots past num_mounts are not searched */
    fresh();
    V.mounts[0].type = VFS_MOUNT_FAT32;
    V.mounts[0].fs_data = &FS2;
    V.mounts[0].active = true;
    CHECK(vfs_open(&V, "B") == -1, "ghost mount not opened");
    CHECK(vfs_list_dir(&V, "/", out, 4) == 0, "ghost mount not listed");
}

static void test_fds(void)
{
    lpres_init();
    memset(&FS, 0, sizeof FS);
    add_file(&FS, "F", 5, 6, false);
    add_file(&FS, "X", 1, 1, false);
    fresh();
    vfs_mount(&V, "/", VFS_MOUNT_FAT32, &FS);

    uint8_t buf[8];
    CHECK(vfs_close(&V, 0) == -1, "close with nothing open");
    CHECK(vfs_read(&V, 0, buf, 8) == -1, "read with nothing open");
    CHECK(vfs_write(&V, 0, buf, 8) == -1, "write with nothing open");

    CHECK(vfs_open(&V, "F") == 0, "open F");
    CHECK(vfs_open(&V, "F") == 1, "open F again");
    CHECK(vfs_read(&V, 1, buf, 8) == 0, "read last fd");
    CHECK(vfs_write(&V, 1, buf, 8) == 0, "write last fd");
    CHECK(vfs_read(&V, 0, buf, 8) == 0, "read fd 0");
    CHECK(vfs_write(&V, 0, buf, 8) == 0, "write fd 0");
    CHECK(vfs_read(&V, 2, buf, 8) == -1, "read past num_open");
    CHECK(vfs_write(&V, 2, buf, 8) == -1, "write past num_open");
    CHECK(vfs_read(&V, -1, buf, 8) == -1, "read negative fd");
    CHECK(vfs_write(&V, -1, buf, 8) == -1, "write negative fd");
    CHECK(vfs_close(&V, -1) == -1, "close negative fd");
    CHECK(vfs_close(&V, 2) == -1, "close past num_open");
    CHECK(vfs_close(&V, 1) == 0, "close last fd");
    CHECK(V.open_files[1].name[0] == 0, "close clears the name");
    CHECK(strcmp(V.open_files[0].name, "F") == 0, "other fd untouched");
    CHECK(V.num_open == 2, "close does not shrink the table");
    CHECK(vfs_close(&V, 0) == 0, "close fd 0");
    CHECK(V.open_files[0].name[0] == 0, "fd 0 cleared");

    /* the table holds exactly 64 */
    while (V.num_open < VFS_MAX_FILES - 1) vfs_open(&V, "F");
    CHECK(vfs_open(&V, "X") == 63, "64th open fits");
    CHECK(V.open_files[63].cluster == 1, "64th node");
    CHECK(vfs_open(&V, "X") == -1, "65th refused");
    CHECK(V.num_open == 64, "count 64");
    CHECK(vfs_close(&V, 63) == 0 && vfs_close(&V, 64) == -1, "close boundary at 64");
}

static void test_names_and_stubs(void)
{
    /* a 59-char FAT name can't exist (13 bytes), so test truncation via stat/chdir */
    fresh();
    vfs_node_t st;
    memset(&st, 0x33, sizeof st);
    CHECK(vfs_stat(&V, "/etc/motd", &st) == 0, "stat ok");
    CHECK(strcmp(st.name, "/etc/motd") == 0, "stat name");
    CHECK(st.size == 0 && !st.is_dir, "stat zeroes");

    char longname[60];
    memset(longname, 'N', sizeof longname);
    longname[59] = 0;
    CHECK(vfs_stat(&V, longname, &st) == 0, "stat long");
    CHECK(strlen(st.name) == VFS_NAME_LEN - 1, "stat name truncated to 31");
    CHECK(memcmp(st.name, longname, VFS_NAME_LEN - 1) == 0, "stat name prefix");

    CHECK(vfs_chdir(&V, "/home") == 0, "chdir ok");
    CHECK(strcmp(vfs_get_cwd(&V), "/home") == 0, "cwd updated");
    char longp[300];
    memset(longp, 'c', sizeof longp);
    longp[299] = 0;
    CHECK(vfs_chdir(&V, longp) == 0, "chdir long");
    CHECK(strlen(V.cwd) == VFS_PATH_LEN - 1, "cwd truncated to 255");
    CHECK(vfs_chdir(&V, "") == 0 && V.cwd[0] == 0, "chdir empty");

    CHECK(vfs_mkdir(&V, "/new") == -1, "mkdir unsupported");

    /* open of a long-named FAT file truncates the 32-byte name, not full_path */
    lpres_init();
    memset(&FS, 0, sizeof FS);
    add_file(&FS, "ABCDEFGH.TXT", 3, 4, false);
    vfs_mount(&V, "/", VFS_MOUNT_FAT32, &FS);
    CHECK(vfs_open(&V, "ABCDEFGH.TXT") == 0, "open 12-char name");
    CHECK(strcmp(V.open_files[0].name, "ABCDEFGH.TXT") == 0, "short name intact");
}

int main(void)
{
    test_init_mount();
    test_open();
    test_mount_filter();
    test_fds();
    test_names_and_stubs();
    printf("test_vfs: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}

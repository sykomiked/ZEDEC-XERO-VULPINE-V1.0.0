/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_files.c — zxv_files.h F1-F6 on a scratch root (POSIX).
 *   cc -std=c11 -I. test_zxv_files.c zxv_files.c && ./a.out */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zxv_files.h"

static int fails, checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #c);                           \
        }                                                                                          \
    } while (0)

static char ROOTDIR[512];

static int put(const char *rel, const char *s)
{
    uint64_t sz;
    return zxf_write(rel, (const uint8_t *) s, (uint32_t) strlen(s), ZXF_W_REPLACE, false, &sz);
}

static int slurp(const char *rel, char *out, uint32_t cap)
{
    uint32_t got;
    uint64_t size;
    int rc = zxf_read(rel, 0, (uint8_t *) out, cap - 1, true, &got, &size);
    out[rc ? 0 : got] = 0;
    return rc;
}

static void names(void)
{
    CHECK(zxf_valid(""));
    CHECK(zxf_valid("a"));
    CHECK(zxf_valid("Notes/today.md"));
    CHECK(zxf_valid("caf\xc3\xa9 \xe2\x9c\x93.txt"));
    CHECK(zxf_valid(".Trash"));
    CHECK(zxf_valid(".Trash/x"));
    CHECK(!zxf_valid("a/.Trash"));
    CHECK(!zxf_valid(".hidden"));
    CHECK(!zxf_valid(".."));
    CHECK(!zxf_valid("."));
    CHECK(!zxf_valid("a/../b"));
    CHECK(!zxf_valid("/abs"));
    CHECK(!zxf_valid("a/"));
    CHECK(!zxf_valid("a//b"));
    CHECK(!zxf_valid("a\\b"));
    CHECK(!zxf_valid("c:x"));
    CHECK(!zxf_valid("tab\there"));
    CHECK(!zxf_valid("del\x7f"));
    CHECK(!zxf_valid("trail."));
    CHECK(!zxf_valid("trail "));
    CHECK(!zxf_valid("bad\xc3"));         /* cut UTF-8 */
    CHECK(!zxf_valid("bad\xc0\xaf"));     /* overlong '/' */
    CHECK(!zxf_valid("bad\xed\xa0\x80")); /* a surrogate */
    CHECK(!zxf_valid("bad\xf5\x80\x80\x80"));
    char lng[300];
    memset(lng, 'a', 255);
    lng[255] = 0;
    CHECK(zxf_valid(lng));
    lng[255] = 'a';
    lng[256] = 0;
    CHECK(!zxf_valid(lng));
}

static void basics(void)
{
    char buf[256], root[600];
    CHECK(zxf_root(root, sizeof root) == 0 && !strcmp(root, ROOTDIR));
    CHECK(put("hello.txt", "hi there\n") == 0);
    CHECK(slurp("hello.txt", buf, sizeof buf) == 0 && !strcmp(buf, "hi there\n"));
    /* exclusive refuses an existing file; replace overwrites */
    uint64_t sz = 0;
    CHECK(zxf_write("hello.txt", (const uint8_t *) "x", 1, ZXF_W_REPLACE, true, &sz) ==
          ZXF_E_EXISTS);
    CHECK(zxf_write("new.txt", (const uint8_t *) "x", 1, ZXF_W_REPLACE, true, &sz) == 0 && sz == 1);
    CHECK(put("hello.txt", "two") == 0);
    CHECK(slurp("hello.txt", buf, sizeof buf) == 0 && !strcmp(buf, "two"));
    /* folders */
    CHECK(zxf_mkdir("Docs") == 0);
    CHECK(zxf_mkdir("Docs") == ZXF_E_EXISTS);
    CHECK(zxf_mkdir("No/Deep") == ZXF_E_NOTFOUND);
    CHECK(put("No/file.txt", "x") == ZXF_E_NOTFOUND);
    CHECK(put("hello.txt/x", "x") == ZXF_E_NOTDIR);
    CHECK(put("Docs", "x") == ZXF_E_ISDIR);
    CHECK(put("", "x") == ZXF_E_PATH);
    CHECK(put("../escape", "x") == ZXF_E_PATH);
    CHECK(put("Docs/a.md", "# A") == 0);
    zxf_entry_t e;
    CHECK(zxf_stat("Docs/a.md", &e) == 0 && !strcmp(e.name, "a.md") && !e.dir && e.size == 3);
    CHECK(zxf_stat("Docs", &e) == 0 && e.dir && e.size == 0);
    CHECK(zxf_stat("missing", &e) == ZXF_E_NOTFOUND);
    /* listing: folders first, then names; hidden left out */
    zxf_entry_t *v;
    uint32_t n;
    CHECK(zxf_list("", &v, &n) == 0);
    CHECK(n == 3 && v[0].dir && !strcmp(v[0].name, "Docs") && !strcmp(v[1].name, "hello.txt") &&
          !strcmp(v[2].name, "new.txt"));
    free(v);
    CHECK(zxf_list("hello.txt", &v, &n) == ZXF_E_NOTDIR && v == NULL && n == 0);
    CHECK(zxf_list("nope", &v, &n) == ZXF_E_NOTFOUND);
    /* the staging folder is there but hidden */
    char p[700];
    struct stat st;
    snprintf(p, sizeof p, "%s/.zxv-staging", ROOTDIR);
    CHECK(stat(p, &st) == 0 && S_ISDIR(st.st_mode));
    /* rename */
    CHECK(zxf_rename("new.txt", "Docs/new.txt") == 0);
    CHECK(zxf_stat("new.txt", &e) == ZXF_E_NOTFOUND && zxf_stat("Docs/new.txt", &e) == 0);
    CHECK(zxf_rename("hello.txt", "Docs/a.md") == ZXF_E_EXISTS);
    CHECK(zxf_rename("ghost", "g2") == ZXF_E_NOTFOUND);
    CHECK(zxf_rename("Docs", "Docs/inner") == ZXF_E_PATH);
    CHECK(zxf_rename("hello.txt", "Nope/h.txt") == ZXF_E_NOTFOUND);
    CHECK(zxf_rename("hello.txt", ".Trash") == ZXF_E_PATH);
    CHECK(zxf_rename("hello.txt", ".Trash/h") == ZXF_E_PATH);
    CHECK(zxf_rename("", "x") == ZXF_E_PATH);
}

static void chunks(void)
{
    uint64_t sz = 9;
    /* add/end with no begin */
    CHECK(zxf_write("big.txt", (const uint8_t *) "x", 1, ZXF_W_ADD, false, &sz) == ZXF_E_STAGE);
    CHECK(zxf_write("big.txt", (const uint8_t *) "x", 1, ZXF_W_END, false, &sz) == ZXF_E_STAGE);
    CHECK(zxf_write("big.txt", (const uint8_t *) "ab", 2, ZXF_W_BEGIN, false, &sz) == 0 && sz == 2);
    zxf_entry_t e;
    CHECK(zxf_stat("big.txt", &e) == ZXF_E_NOTFOUND); /* nothing until "end" */
    CHECK(zxf_write("big.txt", (const uint8_t *) "cd", 2, ZXF_W_ADD, false, &sz) == 0 && sz == 4);
    CHECK(zxf_write("big.txt", (const uint8_t *) "e", 1, ZXF_W_END, false, &sz) == 0 && sz == 5);
    char buf[64];
    CHECK(slurp("big.txt", buf, sizeof buf) == 0 && !strcmp(buf, "abcde"));
    /* the parts are gone after "end" */
    CHECK(zxf_write("big.txt", (const uint8_t *) "x", 1, ZXF_W_ADD, false, &sz) == ZXF_E_STAGE);
    /* a new begin restarts */
    CHECK(zxf_write("big.txt", (const uint8_t *) "zz", 2, ZXF_W_BEGIN, false, &sz) == 0);
    CHECK(zxf_write("big.txt", (const uint8_t *) "y", 1, ZXF_W_BEGIN, false, &sz) == 0 && sz == 1);
    CHECK(zxf_write("big.txt", NULL, 0, ZXF_W_END, false, &sz) == 0 && sz == 1);
    CHECK(slurp("big.txt", buf, sizeof buf) == 0 && !strcmp(buf, "y"));
    /* exclusive at "end" */
    CHECK(zxf_write("big.txt", (const uint8_t *) "q", 1, ZXF_W_BEGIN, true, &sz) == ZXF_E_EXISTS);
}

static void reads(void)
{
    /* "é" split across a chunk boundary: the text read stops before it */
    CHECK(put("u.txt", "ab\xc3\xa9z") == 0);
    uint8_t b[16];
    uint32_t got;
    uint64_t size;
    CHECK(zxf_read("u.txt", 0, b, 3, true, &got, &size) == 0 && got == 2 && size == 5);
    CHECK(zxf_read("u.txt", 2, b, 3, true, &got, &size) == 0 && got == 3 &&
          !memcmp(b, "\xc3\xa9z", 3));
    CHECK(zxf_read("u.txt", 3, b, 3, true, &got, &size) == ZXF_E_BINARY); /* starts mid-char */
    CHECK(zxf_read("u.txt", 3, b, 3, false, &got, &size) == 0 && got == 2);
    CHECK(zxf_read("u.txt", 5, b, 3, true, &got, &size) == 0 && got == 0);
    CHECK(zxf_read("u.txt", 99, b, 3, true, &got, &size) == 0 && got == 0 && size == 5);
    /* a four-byte character cut after two bytes */
    CHECK(put("e.txt", "a\xf0\x9f\x98\x80") == 0);
    CHECK(zxf_read("e.txt", 0, b, 3, true, &got, &size) == 0 && got == 1);
    CHECK(zxf_read("e.txt", 0, b, 5, true, &got, &size) == 0 && got == 5);
    /* binary */
    uint64_t sz;
    CHECK(zxf_write("bin", (const uint8_t *) "a\0b", 3, ZXF_W_REPLACE, false, &sz) == 0);
    CHECK(zxf_read("bin", 0, b, 16, true, &got, &size) == ZXF_E_BINARY);
    CHECK(zxf_read("bin", 0, b, 16, false, &got, &size) == 0 && got == 3 && !memcmp(b, "a\0b", 3));
    CHECK(put("cut.txt", "a\xc3") == 0); /* invalid at the end of the file */
    CHECK(zxf_read("cut.txt", 0, b, 16, true, &got, &size) == ZXF_E_BINARY);
    CHECK(zxf_read("Docs", 0, b, 16, true, &got, &size) == ZXF_E_ISDIR);
    CHECK(zxf_read("zzz", 0, b, 16, true, &got, &size) == ZXF_E_NOTFOUND);
}

static void links(void)
{
    char p[700], q[700];
    snprintf(p, sizeof p, "%s/out", ROOTDIR);
    CHECK(symlink("/etc", p) == 0);
    snprintf(q, sizeof q, "%s/f.lnk", ROOTDIR);
    CHECK(symlink("/etc/hostname", q) == 0);
    char buf[64];
    zxf_entry_t e, *v;
    uint32_t n;
    CHECK(zxf_list("out", &v, &n) == ZXF_E_PATH);
    CHECK(slurp("out/passwd", buf, sizeof buf) == ZXF_E_PATH);
    CHECK(slurp("f.lnk", buf, sizeof buf) == ZXF_E_PATH);
    CHECK(put("f.lnk", "x") == ZXF_E_PATH);
    CHECK(zxf_stat("out", &e) == ZXF_E_PATH);
    CHECK(zxf_rename("out", "out2") == ZXF_E_PATH);
    CHECK(zxf_rename("hello.txt", "out/x") == ZXF_E_PATH);
    CHECK(zxf_mkdir("out/x") == ZXF_E_PATH);
    /* listings leave links out */
    CHECK(zxf_list("", &v, &n) == 0);
    for (uint32_t i = 0; i < n; i++) CHECK(strcmp(v[i].name, "out") && strcmp(v[i].name, "f.lnk"));
    free(v);
    unlink(p);
    unlink(q);
}

static void trash(void)
{
    char to[ZXF_PATH_MAX + 1];
    zxf_entry_t e, *v;
    uint32_t n;
    CHECK(zxf_trash("hello.txt", to, sizeof to) == 0 && !strcmp(to, ".Trash/hello.txt"));
    CHECK(zxf_stat("hello.txt", &e) == ZXF_E_NOTFOUND);
    CHECK(put("hello.txt", "again") == 0);
    CHECK(zxf_trash("hello.txt", to, sizeof to) == 0 && !strcmp(to, ".Trash/hello 2.txt"));
    CHECK(zxf_trash("Docs", to, sizeof to) == 0 && !strcmp(to, ".Trash/Docs"));
    CHECK(zxf_trash("ghost", to, sizeof to) == ZXF_E_NOTFOUND);
    CHECK(zxf_trash("", to, sizeof to) == ZXF_E_PATH);
    CHECK(zxf_trash(".Trash", to, sizeof to) == ZXF_E_PATH);
    CHECK(zxf_list(".Trash", &v, &n) == 0 && n == 3 && v[0].dir && !strcmp(v[0].name, "Docs"));
    free(v);
    /* no writes or new folders inside the Trash */
    CHECK(put(".Trash/new.txt", "x") == ZXF_E_PATH);
    CHECK(zxf_mkdir(".Trash/dir") == ZXF_E_PATH);
    /* restore */
    CHECK(zxf_rename(".Trash/hello 2.txt", "restored.txt") == 0);
    char buf[32];
    CHECK(slurp("restored.txt", buf, sizeof buf) == 0 && !strcmp(buf, "again"));
    /* reading inside the Trash works; trashing inside it removes for good */
    CHECK(slurp(".Trash/Docs/a.md", buf, sizeof buf) == 0 && !strcmp(buf, "# A"));
    CHECK(zxf_trash(".Trash/hello.txt", to, sizeof to) == 0 && to[0] == 0);
    CHECK(zxf_stat(".Trash/hello.txt", &e) == ZXF_E_NOTFOUND);
    /* emptying removes folders and all in them */
    uint32_t removed = 9;
    CHECK(zxf_empty_trash(&removed) == 0 && removed == 1);
    CHECK(zxf_list(".Trash", &v, &n) == 0 && n == 0);
    free(v);
    CHECK(zxf_empty_trash(&removed) == 0 && removed == 0);
    /* a link put in the Trash is removed, not followed */
    char p[700], keep[700];
    snprintf(keep, sizeof keep, "%s.keep", ROOTDIR);
    FILE *f = fopen(keep, "w");
    CHECK(f != NULL);
    if (f) fclose(f);
    snprintf(p, sizeof p, "%s/.Trash/l", ROOTDIR);
    CHECK(symlink(keep, p) == 0);
    CHECK(zxf_empty_trash(&removed) == 0 && removed == 1);
    struct stat st;
    CHECK(stat(keep, &st) == 0);
    unlink(keep);
}

static void roots(void)
{
    char r[600], p[700];
    snprintf(p, sizeof p, "%s/plain-file", ROOTDIR);
    FILE *f = fopen(p, "w");
    if (f) fclose(f);
    CHECK(zxf_set_root(p) == ZXF_E_ROOT);
    CHECK(zxf_root(r, sizeof r) == ZXF_E_ROOT);
    CHECK(zxf_set_root("") == ZXF_E_ROOT);
    /* a missing root is made */
    snprintf(p, sizeof p, "%s/sub-root", ROOTDIR);
    CHECK(zxf_set_root(p) == 0 && zxf_root(r, sizeof r) == 0 && !strcmp(r, p));
    CHECK(zxf_root(r, 4) == ZXF_E_ROOT);
    CHECK(strcmp(zxf_strerror(ZXF_E_BINARY), "unknown error") &&
          !strcmp(zxf_strerror(42), "unknown error"));
}

int main(void)
{
    const char *t = getenv("TMPDIR");
    snprintf(ROOTDIR, sizeof ROOTDIR, "%s/zxv-files-XXXXXX", t && t[0] ? t : "/tmp");
    if (!mkdtemp(ROOTDIR)) return 2;
    CHECK(zxf_set_root(ROOTDIR) == 0);
    names();
    basics();
    chunks();
    reads();
    links();
    trash();
    roots();
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", ROOTDIR);
    if (system(cmd)) fprintf(stderr, "cleanup failed\n");
    printf("%s test_zxv_files: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}

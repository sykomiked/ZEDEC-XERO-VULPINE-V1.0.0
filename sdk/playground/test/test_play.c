/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_play.c - every template and every test case against its golden.
 *
 *   test_play [--update] TEMPLATES GOLDEN CASES
 *
 * 1. Each built-in template runs as shipped; its result JSON must equal
 *    GOLDEN/<id>.json byte for byte, and the built-in copy must equal the
 *    files in TEMPLATES/<id>/ (the generated table is not stale).
 * 2. Each CASES/<name>/ holds case.txt ("template ID" and "expect VERDICT"
 *    lines) and files that replace or add to the template's. The verdict
 *    must match and the JSON must equal GOLDEN/case-<name>.json.
 * 3. Determinism: every run is repeated and must give the same bytes.
 * 4. Library checks: the catalogue, zp_run_one, refusals of bad manifests,
 *    the AI draft prompt.
 * --update rewrites the goldens instead of comparing.
 */
#include "zxvplay.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_fail, g_pass, g_update;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    char *b;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *) malloc((size_t) n + 1u);
    if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    if (b) {
        b[n] = '\0';
        if (len) *len = (size_t) n;
    }
    return b;
}

static void golden(const char *dir, const char *name, const char *json) {
    char path[1024];
    char *want;
    snprintf(path, sizeof path, "%s/%s.json", dir, name);
    if (g_update) {
        FILE *f = fopen(path, "wb");
        CHECK(f != NULL, "cannot write %s", path);
        if (f) {
            fputs(json, f);
            fclose(f);
        }
        return;
    }
    want = slurp(path, NULL);
    CHECK(want != NULL, "%s: no golden (run make golden)", path);
    if (want) {
        CHECK(strcmp(want, json) == 0, "%s: result differs from the golden:\n%s", path, json);
        free(want);
    }
}

static const char *verdict_of(const char *json) {
    static char v[16];
    const char *p = strstr(json, "\"verdict\":\"");
    v[0] = '\0';
    if (p) sscanf(p + 11, "%15[A-Z]", v);
    return v;
}

static void test_templates(const char *tdir, const char *gdir) {
    uint32_t n = zp_template_count();
    CHECK(n >= 6, "only %u templates", n);
    for (uint32_t i = 0; i < n; i++) {
        const zp_template *t = zp_template_at(i);
        char *a = NULL, *b = NULL;
        size_t al = 0, bl = 0;
        CHECK(zp_run(t, NULL, 0, &a, &al) == ZP_OK && a, "%s: run", t->id);
        CHECK(zp_run(t, NULL, 0, &b, &bl) == ZP_OK && b && al == bl && memcmp(a, b, al) == 0,
              "%s: not deterministic", t->id);
        if (a) {
            CHECK(strcmp(verdict_of(a), "FALSE") != 0, "%s: a shipped template must not be refused:\n%s", t->id, a);
            golden(gdir, t->id, a);
        }
        for (uint32_t k = 0; k < t->n_files; k++) {
            char path[1024];
            size_t fl = 0;
            char *disk;
            snprintf(path, sizeof path, "%s/%s/%s", tdir, t->id, t->files[k].name);
            disk = slurp(path, &fl);
            CHECK(disk && fl == t->files[k].len && memcmp(disk, t->files[k].text, fl) == 0,
                  "%s: built-in copy is stale (rebuild)", path);
            free(disk);
        }
        CHECK(zp_template_file(t, "README.md") != NULL, "%s: no README.md", t->id);
        free(a);
        free(b);
    }
}

static void test_case(const char *cdir, const char *gdir, const char *name) {
    char path[1024], tid[64] = "", expect[16] = "";
    char *ct;
    zp_file files[16];
    char *texts[16];
    uint32_t nf = 0;
    DIR *d;
    struct dirent *e;
    const zp_template *t;
    char *out = NULL;
    snprintf(path, sizeof path, "%s/%s/case.txt", cdir, name);
    ct = slurp(path, NULL);
    CHECK(ct != NULL, "%s: no case.txt", name);
    if (!ct) return;
    {
        const char *p = strstr(ct, "template ");
        const char *q = strstr(ct, "expect ");
        if (p) sscanf(p + 9, "%63s", tid);
        if (q) sscanf(q + 7, "%15s", expect);
    }
    free(ct);
    t = zp_template_find(tid);
    CHECK(t != NULL, "%s: unknown template %s", name, tid);
    if (!t) return;
    snprintf(path, sizeof path, "%s/%s", cdir, name);
    d = opendir(path);
    while (d && (e = readdir(d)) != NULL) {
        char fp[1400];
        size_t fl = 0;
        if (e->d_name[0] == '.' || strcmp(e->d_name, "case.txt") == 0 || nf >= 16) continue;
        snprintf(fp, sizeof fp, "%s/%s", path, e->d_name);
        texts[nf] = slurp(fp, &fl);
        if (!texts[nf]) continue;
        files[nf].name = strdup(e->d_name);
        files[nf].text = texts[nf];
        files[nf].len = fl;
        nf++;
    }
    if (d) closedir(d);
    CHECK(zp_run(t, files, nf, &out, NULL) == ZP_OK && out, "%s: run", name);
    if (out) {
        char gname[300];
        CHECK(strcmp(verdict_of(out), expect) == 0, "case %s: verdict %s, expected %s:\n%s", name, verdict_of(out),
              expect, out);
        snprintf(gname, sizeof gname, "case-%s", name);
        golden(gdir, gname, out);
    }
    free(out);
    for (uint32_t i = 0; i < nf; i++) {
        free((char *) files[i].name);
        free(texts[i]);
    }
}

static void test_cases(const char *cdir, const char *gdir) {
    struct dirent **list = NULL;
    int n = scandir(cdir, &list, NULL, alphasort);
    int count = 0;
    for (int i = 0; i < n; i++) {
        char p[1024];
        struct stat st;
        snprintf(p, sizeof p, "%s/%s", cdir, list[i]->d_name);
        if (list[i]->d_name[0] != '.' && stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            test_case(cdir, gdir, list[i]->d_name);
            count++;
        }
        free(list[i]);
    }
    free(list);
    CHECK(count >= 6, "only %d test cases", count);
}

static void test_library(void) {
    char *out = NULL;
    size_t len = 0;
    const zp_template *t;
    zp_file bad = {"template.zxv", "title x\n", 8};
    zp_file nostep = {"template.zxv", "id x\ntitle X\n", 13};
    CHECK(zp_catalogue_json(&out, &len) == ZP_OK && out && out[0] == '[', "catalogue");
    free(out);
    out = NULL;
    CHECK(zp_run_one("kron", "+/1r3 1r6", 9, NULL, 0, &out, NULL) == ZP_OK && out &&
              strstr(out, "\"value\":\"1r2\"") && strcmp(verdict_of(out), "TRUE") == 0,
          "run_one kron: %s", out ? out : "");
    free(out);
    out = NULL;
    CHECK(zp_run_one("kron", "x*2", 3, "x = 1 2 3\n", 10, &out, NULL) == ZP_OK && out &&
              strstr(out, "(vec 2 4 6)"),
          "run_one kron with inputs: %s", out ? out : "");
    free(out);
    out = NULL;
    CHECK(zp_run_one("kron", "x*2", 3, NULL, 0, &out, NULL) == ZP_OK && out && strcmp(verdict_of(out), "NEITHER") == 0,
          "missing input is NEITHER: %s", out ? out : "");
    free(out);
    out = NULL;
    CHECK(zp_run_one("kron", "1%0", 3, NULL, 0, &out, NULL) == ZP_OK && out && strcmp(verdict_of(out), "FALSE") == 0 &&
              strstr(out, "KR_E_DIVZERO"),
          "division by zero is refused: %s", out ? out : "");
    free(out);
    out = NULL;
    CHECK(zp_run_one("cobol", "x", 1, NULL, 0, &out, NULL) == ZP_OK && out && strcmp(verdict_of(out), "FALSE") == 0,
          "unknown language refused");
    free(out);
    out = NULL;
    CHECK(zp_run_one(NULL, "x", 1, NULL, 0, &out, NULL) == ZP_E_ARG, "NULL language");
    t = zp_template_at(0);
    CHECK(zp_run(t, &bad, 1, &out, NULL) == ZP_OK && out && strstr(out, "ZP_E_MANIFEST"), "manifest without id");
    free(out);
    out = NULL;
    CHECK(zp_run(t, &nostep, 1, &out, NULL) == ZP_OK && out && strstr(out, "no steps"), "manifest without steps");
    free(out);
    out = NULL;
    {
        zp_file evil = {"../x", "a", 1};
        CHECK(zp_run(t, &evil, 1, &out, NULL) == ZP_OK && out && strcmp(verdict_of(out), "FALSE") == 0,
              "a file name with a directory is refused");
        free(out);
        out = NULL;
    }
    CHECK(zp_template_find("no-such-template") == NULL, "find unknown");
    {
        uint32_t drafts = 0;
        for (uint32_t i = 0; i < zp_template_count(); i++) {
            int32_t st = zp_draft_prompt(zp_template_at(i), NULL, 0, "make it smaller", &out, NULL);
            if (st == ZP_OK) {
                drafts++;
                CHECK(out && strstr(out, "untrusted") && strstr(out, "make it smaller"), "draft prompt text");
            } else {
                CHECK(st == ZP_E_NOTFOUND, "draft prompt status %d", (int) st);
            }
            free(out);
            out = NULL;
        }
        CHECK(drafts >= 2, "at least two templates offer an AI draft (%u)", drafts);
    }
    for (uint32_t i = 0; i < zp_lang_count(); i++) CHECK(zp_lang_name(i) != NULL, "lang name");
    CHECK(zp_lang_name(zp_lang_count()) == NULL, "lang name past end");
}

int main(int argc, char **argv) {
    int a = 1;
    if (a < argc && strcmp(argv[a], "--update") == 0) {
        g_update = 1;
        a++;
    }
    if (argc - a != 3) {
        fprintf(stderr, "usage: test_play [--update] TEMPLATES GOLDEN CASES\n");
        return 2;
    }
    test_library();
    test_templates(argv[a], argv[a + 1]);
    test_cases(argv[a + 2], argv[a + 1]);
    printf("test_play: %d passed, %d failed%s\n", g_pass, g_fail, g_update ? " (goldens updated)" : "");
    return g_fail ? 1 : 0;
}

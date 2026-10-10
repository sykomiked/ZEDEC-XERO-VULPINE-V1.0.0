/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_model_host.c — map a GGUF model and join it to the tensor engine.
 * See zxv_model_host.h (M1-M4). */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/mman.h>
#    include <sys/stat.h>
#    include <dirent.h>
#    include <fcntl.h>
#    include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_model_host.h"

static struct {
    zxv_model_info_t info;
    const uint8_t *map;
    uint64_t size;
#if defined(_WIN32)
    HANDLE file, mapping;
#endif
    zt_gguf_t g;
    zt_tok_t tok;
    void *arena;
} M;

const zxv_model_info_t *zxv_model_info(void)
{
    return &M.info;
}

void zxv_model_close(void)
{
#if defined(ZXV_HAVE_ZT_GLUE)
    zxv_zt_release();
#endif
#if defined(_WIN32)
    if (M.map) UnmapViewOfFile(M.map);
    if (M.mapping) CloseHandle(M.mapping);
    if (M.file && M.file != INVALID_HANDLE_VALUE) CloseHandle(M.file);
#else
    if (M.map) munmap((void *) M.map, (size_t) M.size);
#endif
    free(M.arena);
    memset(&M, 0, sizeof M);
    snprintf(M.info.status, sizeof M.info.status,
             "No model installed: the swarm runs, but nothing writes answers yet.");
}

static int map_file(const char *path)
{
#if defined(_WIN32)
    M.file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (M.file == INVALID_HANDLE_VALUE) return -1;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(M.file, &sz) || sz.QuadPart <= 0) return -1;
    M.mapping = CreateFileMappingA(M.file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!M.mapping) return -1;
    M.map = (const uint8_t *) MapViewOfFile(M.mapping, FILE_MAP_READ, 0, 0, 0);
    if (!M.map) return -1;
    M.size = (uint64_t) sz.QuadPart;
    return 0;
#else
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        close(fd);
        return -1;
    }
    void *p = mmap(NULL, (size_t) st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); /* the mapping keeps the file */
    if (p == MAP_FAILED) return -1;
    M.map = (const uint8_t *) p;
    M.size = (uint64_t) st.st_size;
    return 0;
#endif
}

static void copy_str(char *dst, size_t cap, const zt_gguf_t *g, const char *key, const char *def)
{
    zt_gguf_val_t v;
    if (zt_gguf_find(g, key, &v) == ZT_GGUF_OK && v.type == ZT_GGUF_STRING) {
        size_t n = v.str.len < cap - 1 ? (size_t) v.str.len : cap - 1;
        for (size_t i = 0; i < n; i++) {
            unsigned char c = v.str.p[i];
            dst[i] = (c < 0x20 || c == 0x7f) ? ' ' : (char) c;
        }
        dst[n] = 0;
    } else {
        snprintf(dst, cap, "%s", def);
    }
}

static const char *base_name(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

int zxv_model_open(const char *path)
{
    zxv_model_close();
    zxv_model_info_t *I = &M.info;
    snprintf(I->path, sizeof I->path, "%s", path);
    if (map_file(path) != 0) {
        zxv_model_close();
        snprintf(M.info.status, sizeof M.info.status, "Could not open the model file %s.",
                 base_name(path));
        return -1;
    }
    int32_t r = zt_gguf_open(&M.g, M.map, M.size);
    if (r != ZT_GGUF_OK) {
        char name[128];
        snprintf(name, sizeof name, "%s", base_name(path));
        zxv_model_close();
        snprintf(M.info.status, sizeof M.info.status,
                 "%s is not a GGUF model this engine can read (error %d).", name, (int) r);
        return -1;
    }
    I->loaded = true;
    I->bytes = M.size;
    I->n_tensors = M.g.n_tensors;
    copy_str(I->arch, sizeof I->arch, &M.g, "general.architecture", "unknown");
    copy_str(I->name, sizeof I->name, &M.g, "general.name", base_name(path));
    copy_str(I->tokenizer, sizeof I->tokenizer, &M.g, "tokenizer.ggml.model", "none");
    char key[64];
    snprintf(key, sizeof key, "%s.block_count", I->arch);
    I->n_layers = zt_gguf_get_int(&M.g, key, 0);
    snprintf(key, sizeof key, "%s.context_length", I->arch);
    I->n_ctx = zt_gguf_get_int(&M.g, key, 0);
    snprintf(key, sizeof key, "%s.embedding_length", I->arch);
    I->n_embd = zt_gguf_get_int(&M.g, key, 0);

    uint64_t need = zt_tok_arena_bytes(&M.g);
    if (need > 0 && need < (1ull << 31) && (M.arena = malloc((size_t) need)) != NULL &&
        zt_tok_load(&M.tok, &M.g, M.arena, need) == ZT_GGUF_OK) {
        I->tok_ok = true;
        I->n_vocab = M.tok.n_vocab;
    }
    const char *no_gen = "tokenizer ready; this build has no forward pass yet";
#if defined(ZXV_HAVE_ZT_GLUE)
    I->can_generate = I->tok_ok && zxv_zt_can_run(&M.g);
    no_gen = "tokenizer ready; the engine cannot run these weights (see zt_model.h)";
#endif
    snprintf(I->status, sizeof I->status, "%s (%s, %llu MB): %s.", I->name, I->arch,
             (unsigned long long) (I->bytes >> 20),
             I->can_generate ? "ready"
             : I->tok_ok     ? no_gen
                             : "metadata only; its tokenizer is not supported yet");
    return 0;
}

static bool is_gguf_name(const char *n)
{
    size_t k = strlen(n);
    return k > 5 && n[0] != '.' && (!strcmp(n + k - 5, ".gguf") || !strcmp(n + k - 5, ".GGUF"));
}

int zxv_model_find_in_dir(const char *dir, char *out, size_t cap)
{
    char best[512] = "";
#if defined(_WIN32)
    char pat[1100];
    snprintf(pat, sizeof pat, "%s\\*.gguf", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (is_gguf_name(fd.cFileName) && (!best[0] || strcmp(fd.cFileName, best) < 0))
            snprintf(best, sizeof best, "%s", fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (is_gguf_name(e->d_name) && (!best[0] || strcmp(e->d_name, best) < 0))
            snprintf(best, sizeof best, "%s", e->d_name);
    closedir(d);
#endif
    if (!best[0]) return -1;
    int n = snprintf(out, cap, "%s/%s", dir, best);
    return (n > 0 && (size_t) n < cap) ? 0 : -1;
}

int zxv_model_answer(const char *prompt, char *out, size_t cap)
{
    out[0] = 0;
    if (!M.info.loaded) return 0;
    if (!M.info.tok_ok) {
        snprintf(out, cap, "Model %s is installed, but its tokenizer (%s) is not supported yet.",
                 M.info.name, M.info.tokenizer);
        return 0;
    }
    uint64_t len = strlen(prompt);
    uint64_t cap_ids = len + 2;
    uint64_t wb = zt_tok_work_bytes(len);
    int32_t *ids = malloc((size_t) cap_ids * sizeof *ids);
    void *work = malloc((size_t) (wb ? wb : 1));
    uint64_t n = 0;
    int32_t r = -1;
    if (ids && work) {
        uint64_t off = 0;
        if (M.tok.add_bos && M.tok.bos >= 0) ids[off++] = M.tok.bos;
        r = zt_tok_encode(&M.tok, (const uint8_t *) prompt, len, false, ids + off, cap_ids - off,
                          &n, work, wb);
        n += off;
    }
    int answered = 0;
    if (r != ZT_GGUF_OK) {
        snprintf(out, cap, "Model %s could not tokenize this message (error %d).", M.info.name,
                 (int) r);
#if defined(ZXV_HAVE_ZT_GLUE)
    } else if (M.info.can_generate) {
        char err[160] = "";
        int32_t g = zxv_zt_generate(&M.g, &M.tok, ids, n, out, cap, err, sizeof err);
        if (g >= 0)
            answered = 1;
        else
            snprintf(out, cap, "Model %s failed to generate: %s", M.info.name, err);
#endif
    } else {
        snprintf(out, cap,
                 "Model %s read your message as %llu tokens, but this build cannot run its "
                 "weights, so it cannot write the answer.",
                 M.info.name, (unsigned long long) n);
    }
    free(ids);
    free(work);
    return answered;
}

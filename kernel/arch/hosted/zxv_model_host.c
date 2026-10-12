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

/* A read-only mapping of one file (M1). */
typedef struct {
    const uint8_t *map;
    uint64_t size;
#if defined(_WIN32)
    HANDLE file, mapping;
#endif
} file_map_t;

static struct {
    zxv_model_info_t info;
    file_map_t f;
    zt_gguf_t g;
    zt_tok_t tok;
    void *arena;
} M;

const zxv_model_info_t *zxv_model_info(void)
{
    return &M.info;
}

static void unmap_file(file_map_t *f)
{
#if defined(_WIN32)
    if (f->map) UnmapViewOfFile(f->map);
    if (f->mapping) CloseHandle(f->mapping);
    if (f->file && f->file != INVALID_HANDLE_VALUE) CloseHandle(f->file);
#else
    if (f->map) munmap((void *) f->map, (size_t) f->size);
#endif
    memset(f, 0, sizeof *f);
}

void zxv_model_close(void)
{
#if defined(ZXV_HAVE_ZT_GLUE)
    zxv_zt_release();
#endif
    unmap_file(&M.f);
    free(M.arena);
    memset(&M, 0, sizeof M);
    snprintf(M.info.status, sizeof M.info.status,
             "No model installed: the swarm runs, but nothing writes answers yet.");
}

static int map_file(const char *path, file_map_t *f)
{
#if defined(_WIN32)
    f->file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (f->file == INVALID_HANDLE_VALUE) return -1;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f->file, &sz) || sz.QuadPart <= 0) return -1;
    f->mapping = CreateFileMappingA(f->file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!f->mapping) return -1;
    f->map = (const uint8_t *) MapViewOfFile(f->mapping, FILE_MAP_READ, 0, 0, 0);
    if (!f->map) return -1;
    f->size = (uint64_t) sz.QuadPart;
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
    f->map = (const uint8_t *) p;
    f->size = (uint64_t) st.st_size;
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

/* M1-M3 for one file: map it, read its header and tokenizer, fill I. On
 * failure everything is released and I->status says why. */
static int open_model_file(const char *path, file_map_t *f, zt_gguf_t *g, zt_tok_t *tok,
                           void **arena, zxv_model_info_t *I)
{
    memset(I, 0, sizeof *I);
    snprintf(I->path, sizeof I->path, "%s", path);
    if (map_file(path, f) != 0) {
        unmap_file(f);
        snprintf(I->status, sizeof I->status, "Could not open the model file %s.", base_name(path));
        return -1;
    }
    int32_t r = zt_gguf_open(g, f->map, f->size);
    if (r != ZT_GGUF_OK) {
        unmap_file(f);
        snprintf(I->status, sizeof I->status,
                 "%s is not a GGUF model this engine can read (error %d).", base_name(path),
                 (int) r);
        return -1;
    }
    I->loaded = true;
    I->bytes = f->size;
    I->n_tensors = g->n_tensors;
    copy_str(I->arch, sizeof I->arch, g, "general.architecture", "unknown");
    copy_str(I->name, sizeof I->name, g, "general.name", base_name(path));
    copy_str(I->tokenizer, sizeof I->tokenizer, g, "tokenizer.ggml.model", "none");
    char key[64];
    snprintf(key, sizeof key, "%s.block_count", I->arch);
    I->n_layers = zt_gguf_get_int(g, key, 0);
    snprintf(key, sizeof key, "%s.context_length", I->arch);
    I->n_ctx = zt_gguf_get_int(g, key, 0);
    snprintf(key, sizeof key, "%s.embedding_length", I->arch);
    I->n_embd = zt_gguf_get_int(g, key, 0);

    uint64_t need = zt_tok_arena_bytes(g);
    if (need > 0 && need < (1ull << 31) && (*arena = malloc((size_t) need)) != NULL &&
        zt_tok_load(tok, g, *arena, need) == ZT_GGUF_OK) {
        I->tok_ok = true;
        I->n_vocab = tok->n_vocab;
    }
    const char *no_gen = "tokenizer ready; this build has no forward pass yet";
#if defined(ZXV_HAVE_ZT_GLUE)
    I->can_generate = I->tok_ok && zxv_zt_can_run(g);
    no_gen = "tokenizer ready; the engine cannot run these weights (see zt_model.h)";
#endif
    snprintf(I->status, sizeof I->status, "%s (%s, %llu MB): %s.", I->name, I->arch,
             (unsigned long long) (I->bytes >> 20),
             I->can_generate ? "ready"
             : I->tok_ok     ? no_gen
                             : "metadata only; its tokenizer is not supported yet");
    return 0;
}

int zxv_model_open(const char *path)
{
    zxv_model_close();
    if (open_model_file(path, &M.f, &M.g, &M.tok, &M.arena, &M.info) != 0) {
        zxv_model_info_t keep = M.info;
        zxv_model_close();
        snprintf(M.info.status, sizeof M.info.status, "%s", keep.status);
        return -1;
    }
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

#if defined(ZXV_HAVE_ZT_GLUE)
typedef struct {
    const zt_gguf_t *g;    /* the single slot's file (agent null) */
    zxv_zt_agent_t *agent; /* or an agent (M6) */
    const zt_tok_t *tok;
    const int32_t *ids;
    uint64_t n;
    char *out;
    size_t cap;
    char err[160];
    bool called;
} gen_job_t;

static int32_t gen_job(void *ctx, uint32_t max_new)
{
    gen_job_t *j = (gen_job_t *) ctx;
    j->called = true;
    if (j->agent)
        return zxv_zt_agent_generate(j->agent, j->tok, j->ids, j->n, max_new, j->out, j->cap,
                                     j->err, sizeof j->err);
    return zxv_zt_generate_n(j->g, j->tok, j->ids, j->n, max_new, j->out, j->cap, j->err,
                             sizeof j->err);
}
#endif

/* Tokenize the prompt with tok and generate on g (agent null) or the agent. */
static int answer_with(const zxv_model_info_t *I, const zt_gguf_t *g, const zt_tok_t *tok,
                       void *agent, const char *prompt, zxv_gen_runner_fn run, void *rctx,
                       char *out, size_t cap)
{
    out[0] = 0;
    if (!I->loaded) return 0;
    if (!I->tok_ok) {
        snprintf(out, cap, "Model %s is installed, but its tokenizer (%s) is not supported yet.",
                 I->name, I->tokenizer);
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
        if (tok->add_bos && tok->bos >= 0) ids[off++] = tok->bos;
        r = zt_tok_encode(tok, (const uint8_t *) prompt, len, false, ids + off, cap_ids - off, &n,
                          work, wb);
        n += off;
    }
    int answered = 0;
    if (r != ZT_GGUF_OK) {
        snprintf(out, cap, "Model %s could not tokenize this message (error %d).", I->name,
                 (int) r);
#if defined(ZXV_HAVE_ZT_GLUE)
    } else if (I->can_generate) {
        gen_job_t j = {g, (zxv_zt_agent_t *) agent, tok, ids, n, out, cap, "", false};
        int32_t gr = run ? run(rctx, gen_job, &j) : gen_job(&j, ZXV_MODEL_MAX_NEW);
        if (gr >= 0 && j.called) {
            answered = 1;
        } else if (!j.called) {
            out[0] = 0;
            answered = ZXV_MODEL_BUDGET_EXHAUSTED;
        } else {
            snprintf(out, cap, "Model %s failed to generate: %s", I->name, j.err);
        }
#else
        (void) g;
        (void) agent;
        (void) run;
        (void) rctx;
#endif
    } else {
        snprintf(out, cap,
                 "Model %s read your message as %llu tokens, but this build cannot run its "
                 "weights, so it cannot write the answer.",
                 I->name, (unsigned long long) n);
    }
    free(ids);
    free(work);
    return answered;
}

int zxv_model_answer_ex(const char *prompt, zxv_gen_runner_fn run, void *rctx, char *out,
                        size_t cap)
{
    return answer_with(&M.info, &M.g, &M.tok, NULL, prompt, run, rctx, out, cap);
}

int zxv_model_answer(const char *prompt, char *out, size_t cap)
{
    return zxv_model_answer_ex(prompt, NULL, NULL, out, cap);
}

/* ---- M6: several models at once, a LoRA adapter per agent ---- */

static struct {
    zxv_model_info_t info;
    file_map_t f;
    zt_gguf_t g;
    zt_tok_t tok;
    void *arena;
#if defined(ZXV_HAVE_ZT_GLUE)
    zxv_zt_base_t *base;
#endif
} S[ZXV_SLOTS];

static struct {
    bool open;
    uint32_t slot;
    char lora_path[1024];
#if defined(ZXV_HAVE_ZT_GLUE)
    zxv_zt_agent_t *a;
#endif
} A[ZXV_AGENTS];

const zxv_model_info_t *zxv_slot_info(uint32_t slot)
{
    return slot < ZXV_SLOTS ? &S[slot].info : NULL;
}

void zxv_slot_close(uint32_t slot)
{
    if (slot >= ZXV_SLOTS) return;
    for (uint32_t i = 0; i < ZXV_AGENTS; i++)
        if (A[i].open && A[i].slot == slot) zxv_agent_close(i);
#if defined(ZXV_HAVE_ZT_GLUE)
    zxv_zt_base_free(S[slot].base);
#endif
    unmap_file(&S[slot].f);
    free(S[slot].arena);
    memset(&S[slot], 0, sizeof S[slot]);
    snprintf(S[slot].info.status, sizeof S[slot].info.status, "Slot %u is empty.", (unsigned) slot);
}

int zxv_slot_open(uint32_t slot, const char *path)
{
    if (slot >= ZXV_SLOTS || !path) return -1;
    zxv_slot_close(slot);
    zxv_model_info_t *I = &S[slot].info;
    if (open_model_file(path, &S[slot].f, &S[slot].g, &S[slot].tok, &S[slot].arena, I) != 0) {
        zxv_model_info_t keep = *I;
        zxv_slot_close(slot);
        snprintf(I->status, sizeof I->status, "%s", keep.status);
        return -1;
    }
#if defined(ZXV_HAVE_ZT_GLUE)
    if (I->can_generate) {
        char err[160] = "";
        if (zxv_zt_base_load(&S[slot].base, &S[slot].g, err, sizeof err) != 0) {
            char msg[sizeof I->status];
            I->can_generate = false;
            snprintf(msg, sizeof msg, "%s (%s): the weights did not load: %s.", I->name, I->arch,
                     err);
            memcpy(I->status, msg, sizeof msg);
        }
    }
#endif
    return 0;
}

int zxv_agent_open(uint32_t agent, uint32_t slot, const char *lora_path, uint32_t scale_q16,
                   char *err, size_t err_cap)
{
    if (err && err_cap) err[0] = 0;
    if (agent >= ZXV_AGENTS || slot >= ZXV_SLOTS || !S[slot].info.loaded) {
        if (err && err_cap) snprintf(err, err_cap, "no model in slot %u", (unsigned) slot);
        return -1;
    }
    zxv_agent_close(agent);
#if defined(ZXV_HAVE_ZT_GLUE)
    if (!S[slot].base) {
        if (err && err_cap)
            snprintf(err, err_cap, "the model in slot %u cannot generate", (unsigned) slot);
        return -1;
    }
    file_map_t lf;
    zt_gguf_t lg;
    memset(&lf, 0, sizeof lf);
    const zt_gguf_t *lp = NULL;
    if (lora_path && lora_path[0]) {
        if (map_file(lora_path, &lf) != 0 || zt_gguf_open(&lg, lf.map, lf.size) != ZT_GGUF_OK) {
            unmap_file(&lf);
            if (err && err_cap)
                snprintf(err, err_cap, "could not read the adapter file %s", base_name(lora_path));
            return -1;
        }
        lp = &lg;
    }
    int32_t r = zxv_zt_agent_new(&A[agent].a, S[slot].base, lp, scale_q16 ? scale_q16 : 65536u, err,
                                 err_cap);
    unmap_file(&lf); /* the adapter is copied into the agent's own arena */
    if (r != 0) return -1;
#else
    (void) scale_q16;
    if (lora_path && lora_path[0]) {
        if (err && err_cap) snprintf(err, err_cap, "this build has no forward pass");
        return -1;
    }
#endif
    A[agent].open = true;
    A[agent].slot = slot;
    snprintf(A[agent].lora_path, sizeof A[agent].lora_path, "%s", lora_path ? lora_path : "");
    return 0;
}

void zxv_agent_close(uint32_t agent)
{
    if (agent >= ZXV_AGENTS || !A[agent].open) return;
#if defined(ZXV_HAVE_ZT_GLUE)
    zxv_zt_agent_free(A[agent].a);
#endif
    memset(&A[agent], 0, sizeof A[agent]);
}

int zxv_agent_slot(uint32_t agent)
{
    return agent < ZXV_AGENTS && A[agent].open ? (int) A[agent].slot : -1;
}

int zxv_agent_answer_ex(uint32_t agent, const char *prompt, zxv_gen_runner_fn run, void *rctx,
                        char *out, size_t cap)
{
    out[0] = 0;
    if (agent >= ZXV_AGENTS || !A[agent].open) {
        snprintf(out, cap, "Agent %u has no model.", (unsigned) agent);
        return 0;
    }
    uint32_t s = A[agent].slot;
    void *a = NULL;
#if defined(ZXV_HAVE_ZT_GLUE)
    a = A[agent].a;
#endif
    return answer_with(&S[s].info, &S[s].g, &S[s].tok, a, prompt, run, rctx, out, cap);
}

int zxv_agent_answer(uint32_t agent, const char *prompt, char *out, size_t cap)
{
    return zxv_agent_answer_ex(agent, prompt, NULL, NULL, out, cap);
}

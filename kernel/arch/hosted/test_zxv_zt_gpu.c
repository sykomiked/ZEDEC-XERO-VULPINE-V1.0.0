/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_zt_gpu.c — the app's models on the GPU (zxv_zt_glue.c G7): a base
 * and its agents run on whatever GPU zt_accel_open finds, and every reply is
 * the one the CPU gives, bit for bit: the single slot, plain agents (more
 * than the GPU shares caches with, so some run product by product), a LoRA
 * agent, and agents made again on recycled caches. With no GPU the replies
 * still match (both runs are on the CPU) and the device line says why.
 *
 *   test_zxv_zt_gpu                 the tiny qwen2 fixture and its adapter
 *   test_zxv_zt_gpu MODEL.gguf      a real model with its tokenizer
 *   ZXV_GPU_REQUIRE=1               fail when no GPU was used
 *
 * kernel/arch/hosted/test_hosted.sh runs it; kernel/Makefile test-vk runs it
 * on lavapipe with the Vulkan kernels built. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L /* setenv */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_model_host.h"
#include "zt_choose.h"
#include "zt_model.h"
#include "test_model_fixture.h"
#include "test_lora_fixture.h"
#include "test_tok_fixture.h"
#include "test_fixture_rebuild.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        int ok_ = (c) ? 1 : 0;                                                                     \
        checks++;                                                                                  \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

#define N_GEN    12u
#define MAX_PLAN 20u

static uint8_t *read_all(const char *path, uint64_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *b = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0 && (b = malloc((size_t) n)) &&
            fread(b, 1, (size_t) n, f) != (size_t) n) {
            free(b);
            b = NULL;
        }
        *len = n > 0 ? (uint64_t) n : 0;
    }
    fclose(f);
    return b;
}

typedef struct {
    char text[4096];
    int32_t n;
} reply_t;

static void agent_reply(zxv_zt_agent_t *a, const zt_tok_t *tok, const int32_t *ids, uint64_t n,
                        reply_t *r)
{
    char err[200] = "";
    r->n = zxv_zt_agent_generate(a, tok, ids, n, N_GEN, r->text, sizeof r->text, err, sizeof err);
    if (r->n < 0) printf("  (agent: %s)\n", err);
}

static int same(const reply_t *a, const reply_t *b)
{
    return a->n >= 0 && a->n == b->n && !strcmp(a->text, b->text);
}

int main(int argc, char **argv)
{
    zt_gguf_t mg, tg, lg;
    zt_tok_t tok;
    uint8_t *file = NULL, *lfile = NULL;
    uint64_t flen = 0;
    bool fixture = argc < 2, have_lora = false;
    uint32_t n_agents = fixture ? 17u : 3u; /* past GLUE_POOL's 15 shared caches */
    if (fixture) {
        const mf_variant_t *v = &MF_VARIANTS[0]; /* qwen2 */
        const lf_variant_t *lv = &LF_VARIANTS[0];
        mf_variant_t la = *v;
        la.hdr = lv->hdr;
        la.hdr_len = lv->hdr_len;
        la.file_len = lv->file_len;
        la.gen = lv->gen;
        la.n_gen = lv->n_gen;
        file = rebuild(v);
        lfile = rebuild(&la);
        CHECK(file && zt_gguf_open(&mg, file, v->file_len) == 0, "qwen2 test model rebuilt");
        have_lora = lfile && zt_gguf_open(&lg, lfile, lv->file_len) == 0;
        CHECK(have_lora, "its rank-8 LoRA adapter rebuilt");
        zt_gguf_open(&tg, (const uint8_t *) TOK_GGUF, TOK_GGUF_LEN);
    } else {
        file = read_all(argv[1], &flen);
        CHECK(file && zt_gguf_open(&mg, file, flen) == 0, "%s read", argv[1]);
        if (!file) return 1;
        tg = mg;
    }
    uint64_t tb = zt_tok_arena_bytes(&tg);
    void *tar = malloc((size_t) (tb ? tb : 1));
    CHECK(tb && tar && zt_tok_load(&tok, &tg, tar, tb) == 0, "tokenizer loaded");
    const char *msg = "Integer math is exact.";
    int32_t ids[64];
    uint64_t n = 0, wb = zt_tok_work_bytes(strlen(msg));
    void *work = malloc((size_t) wb);
    zt_tok_encode(&tok, (const uint8_t *) msg, strlen(msg), false, ids, 64, &n, work, wb);
    free(work);
    char err[200] = "";

    /* the machine, as a swarm planner sees it before loading */
    zt_host_t host;
    char hl[512];
    zt_choose_host(&host, true);
    zt_host_line(&host, hl, sizeof hl);
    printf("[INFO] host: %s\n", hl);
    CHECK(host.ncpu >= 1 && host.n_sets >= 1 && !strcmp(host.sets[host.n_sets - 1], "c") &&
              !strncmp(hl, "cpus=", 5),
          "the host probe lists the CPUs, kernel sets (C reference last) and GPUs");

    /* the CPU's replies */
    setenv("ZT_GPU", "cpu", 1);
    zxv_zt_base_t *cb = NULL;
    zxv_zt_agent_t *ca = NULL, *cl = NULL;
    reply_t ref, ref_lora, single;
    CHECK(zxv_zt_base_load(&cb, &mg, err, sizeof err) == 0 &&
              strstr(zxv_zt_base_choice(cb), " gpu=none "),
          "ZT_GPU=cpu: the base runs on the CPU (%s)", cb ? zxv_zt_base_gpu(cb) : err);
    CHECK(zxv_zt_agent_new(&ca, cb, NULL, 0, err, sizeof err) == 0, "CPU agent");
    agent_reply(ca, &tok, ids, n, &ref);
    CHECK(ref.n > 0, "CPU reply: %d tokens", (int) ref.n);
    if (have_lora) {
        CHECK(zxv_zt_agent_new(&cl, cb, &lg, 65536, err, sizeof err) == 0, "CPU LoRA agent");
        agent_reply(cl, &tok, ids, n, &ref_lora);
    }
    zxv_zt_agent_free(ca);
    zxv_zt_agent_free(cl);
    CHECK(zxv_zt_base_free(cb) == 0, "CPU base freed");

    /* the measured choice: every engine timed, the fastest exact ones kept */
    unsetenv("ZT_GPU");
    zxv_zt_base_t *mb = NULL;
    zxv_zt_agent_t *ma = NULL;
    reply_t mr;
    char rec[1536] = "", head[256] = "", api[24] = "", cpu[48] = "";
    CHECK(zxv_zt_base_load(&mb, &mg, err, sizeof err) == 0 && mb, "base loaded, engines measured");
    if (!mb) return 1;
    snprintf(rec, sizeof rec, "%s", zxv_zt_base_choice(mb));
    printf("[INFO] choice: %s\n", rec);
    const char *bar = strstr(rec, " | ");
    CHECK(!strncmp(rec, "zt-choice/1 cpu=", 16) && bar && strstr(bar, "cpu:"),
          "the record names the choice, then every engine it measured");
    /* the CPU engine chosen is the one measured fastest at one-token steps */
    {
        char want[64];
        unsigned long long best = ~0ull, mine = ~0ull;
        if (sscanf(rec + 12, "cpu=%47s", want) == 1)
            for (const char *q = bar; q && (q = strstr(q, "cpu:")) != NULL; q += 4) {
                char tag[64];
                unsigned long long pu, su;
                if (sscanf(q + 4, "%63s prompt %llu us/token, step %llu us", tag, &pu, &su) != 3)
                    continue;
                if (su < best) best = su;
                if (!strcmp(tag, want)) mine = su;
            }
        CHECK(mine != ~0ull && mine <= best,
              "the CPU engine chosen (%s, %llu us a step) is the fastest measured (%llu us)", want,
              mine, best);
    }
    CHECK(zxv_zt_agent_new(&ma, mb, NULL, 0, err, sizeof err) == 0, "agent on the chosen engine");
    agent_reply(ma, &tok, ids, n, &mr);
    CHECK(same(&mr, &ref), "the measured choice replies exactly as the CPU does (%s)",
          zxv_zt_base_gpu(mb));
    zxv_zt_agent_free(ma);
    CHECK(zxv_zt_base_free(mb) == 0, "base freed");
    /* the choice's head ("cpu=SET/T gpu=..."), and a GPU that measured exact */
    const char *mo = strstr(rec, " model=");
    if (mo && mo > rec + 12) snprintf(head, sizeof head, "%.*s", (int) (mo - rec - 12), rec + 12);
    sscanf(rec + 12, "cpu=%47s", cpu);
    for (const char *q = bar; q && (q = strstr(q, "gpu:")) != NULL; q += 4) {
        char a[24];
        if (sscanf(q + 4, "%23[a-z] prompt", a) == 1 &&
            strstr(q, " prompt ") == q + 4 + strlen(a)) {
            snprintf(api, sizeof api, "%s", a);
            break;
        }
    }

    /* the record replays: the same choice, not measured again */
    setenv("ZT_CHOICE", head, 1);
    CHECK(zxv_zt_base_load(&mb, &mg, err, sizeof err) == 0 && mb, "base loaded from ZT_CHOICE");
    if (!mb) return 1;
    CHECK(!strncmp(zxv_zt_base_choice(mb), rec, (size_t) (mo ? mo - rec : 0)) &&
              strstr(zxv_zt_base_choice(mb), "replayed"),
          "ZT_CHOICE=\"%s\" rebuilds that choice without measuring", head);
    CHECK(zxv_zt_agent_new(&ma, mb, NULL, 0, err, sizeof err) == 0, "agent on the replayed engine");
    agent_reply(ma, &tok, ids, n, &mr);
    CHECK(same(&mr, &ref), "the replayed choice replies exactly as the CPU does");
    zxv_zt_agent_free(ma);
    zxv_zt_base_free(mb);

    /* every pass on the GPU that measured exact (whether or not it won) */
    bool on_gpu = api[0] != 0;
    char dev[192] = "no GPU";
    if (!on_gpu) {
        unsetenv("ZT_CHOICE");
        printf("[INFO] no GPU ran the model exactly here: the GPU-only checks are skipped\n");
    } else {
        char forced[128];
        snprintf(forced, sizeof forced, "cpu=%s gpu=%s:0-max", cpu, api);
        setenv("ZT_CHOICE", forced, 1);
    }
    zxv_zt_base_t *gb = NULL;
    CHECK(zxv_zt_base_load(&gb, &mg, err, sizeof err) == 0 && gb, "base loaded (%s)", err);
    if (!gb) return 1;
    snprintf(dev, sizeof dev, "%s", zxv_zt_base_gpu(gb)); /* the base's copy goes with it */
    printf("[INFO] device: %s\n", dev);
    zxv_zt_agent_t *ag[MAX_PLAN] = {0};
    bool all = true;
    for (uint32_t i = 0; i < n_agents; i++) {
        reply_t r;
        if (zxv_zt_agent_new(&ag[i], gb, NULL, 0, err, sizeof err)) {
            all = false;
            printf("  (agent %u: %s)\n", (unsigned) i, err);
            continue;
        }
        agent_reply(ag[i], &tok, ids, n, &r);
        if (!same(&r, &ref)) {
            all = false;
            printf("  (agent %u: %d tokens \"%s\")\n", (unsigned) i, (int) r.n, r.text);
        }
    }
    CHECK(all, "%u plain agents on one base reply exactly as the CPU does", (unsigned) n_agents);
    uint64_t passes = 0, products = 0;
    zxv_zt_base_gpu_work(gb, &passes, &products);
    if (on_gpu) /* at least one whole pass per agent whose cache the GPU shares */
        CHECK(passes >= (n_agents < 15u ? n_agents : 15u),
              "the GPU ran %llu whole passes and %llu products", (unsigned long long) passes,
              (unsigned long long) products);
    if (have_lora) {
        zxv_zt_agent_t *la = NULL;
        reply_t r;
        CHECK(zxv_zt_agent_new(&la, gb, &lg, 65536, err, sizeof err) == 0, "LoRA agent");
        agent_reply(la, &tok, ids, n, &r);
        CHECK(same(&r, &ref_lora) && strcmp(r.text, ref.text),
              "the LoRA agent replies exactly as on the CPU, and differently from the plain ones");
        zxv_zt_agent_free(la);
    }
    /* caches go back to the base and are handed out again */
    for (uint32_t i = 0; i < n_agents; i += 2) {
        zxv_zt_agent_free(ag[i]);
        ag[i] = NULL;
    }
    all = true;
    for (uint32_t i = 0; i < n_agents; i += 2) {
        reply_t r;
        if (zxv_zt_agent_new(&ag[i], gb, NULL, 0, err, sizeof err) == 0)
            agent_reply(ag[i], &tok, ids, n, &r);
        else
            r.n = -1;
        if (!same(&r, &ref)) all = false;
    }
    CHECK(all, "agents made again on recycled caches reply the same");
    CHECK(zxv_zt_base_free(gb) == -1, "a base with agents on it is not freed");
    for (uint32_t i = 0; i < n_agents; i++) zxv_zt_agent_free(ag[i]);
    CHECK(zxv_zt_base_free(gb) == 0, "base and its GPU freed once the agents are gone");

    /* the single slot (zxv_zt_generate_n) */
    single.n = zxv_zt_generate_n(&mg, &tok, ids, n, N_GEN, single.text, sizeof single.text, err,
                                 sizeof err);
    CHECK(same(&single, &ref) && (!on_gpu || !strcmp(zxv_zt_gpu(), dev)),
          "the single slot replies the same%s (%s)", on_gpu ? ", on the same device" : "",
          zxv_zt_gpu());
    zxv_zt_release();
    CHECK(zxv_zt_gpu()[0] == 0, "released: no device");

    unsetenv("ZT_CHOICE");
    const char *req = getenv("ZXV_GPU_REQUIRE");
    if (req && !strcmp(req, "1")) CHECK(on_gpu, "a GPU ran the model exactly (ZXV_GPU_REQUIRE=1)");
    free(tar);
    free(file);
    free(lfile);
    printf("%d/%d checks passed (%s)\n", checks - failures, checks, on_gpu ? dev : "no GPU");
    return failures ? 1 : 0;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_swarm_slots.c — several models at once (zxv_model_host.h M6,
 * zxv_zt_glue.c G6): agents on one shared base, each with its own KV cache
 * and its own LoRA adapter or none. Uses the tiny qwen2 test model and its
 * rank-8 adapter (kernel/src/tensor/test_model_fixture.h,
 * test_lora_fixture.h; the engine's own accuracy against float64 is
 * test_zt_lora's job) with the qwen2 tokenizer fixture.
 *
 *   sh kernel/arch/hosted/test_hosted.sh runs it (with ASan/UBSan where the
 *   compiler has them). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_model_host.h"
#include "zt_model.h"
#include "test_model_fixture.h"
#include "test_lora_fixture.h"
#include "test_tok_fixture.h"
#include "test_fixture_rebuild.h"
#include "test_model_file.h"

static int failures = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        int ok_ = (c) ? 1 : 0;                                                                     \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static int write_file(const char *path, const uint8_t *b, uint64_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(b, 1, (size_t) n, f);
    return fclose(f) == 0 && w == n ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp";
    const mf_variant_t *v = &MF_VARIANTS[0]; /* qwen2 */
    const lf_variant_t *lv = &LF_VARIANTS[0];
    mf_variant_t la = *v; /* the adapter, rebuilt with the same generator */
    la.hdr = lv->hdr;
    la.hdr_len = lv->hdr_len;
    la.file_len = lv->file_len;
    la.gen = lv->gen;
    la.n_gen = lv->n_gen;
    uint8_t *file = rebuild(v), *lfile = rebuild(&la);
    zt_gguf_t mg, tg, lg;
    zt_tok_t tok;
    CHECK(file && zt_gguf_open(&mg, file, v->file_len) == 0, "qwen2 test model rebuilt");
    CHECK(lfile && zt_gguf_open(&lg, lfile, lv->file_len) == 0, "its rank-8 LoRA adapter rebuilt");
    zt_gguf_open(&tg, (const uint8_t *) TOK_GGUF, TOK_GGUF_LEN);
    uint64_t tb = zt_tok_arena_bytes(&tg);
    void *tar = malloc((size_t) tb);
    zt_tok_load(&tok, &tg, tar, tb);

    const char *msg = "Hi there";
    int32_t ids[64];
    uint64_t n = 0, wb = zt_tok_work_bytes(strlen(msg));
    void *work = malloc((size_t) wb);
    zt_tok_encode(&tok, (const uint8_t *) msg, strlen(msg), false, ids, 64, &n, work, wb);
    free(work);

    /* G6: the glue */
    char single[4096], err[200] = "";
    int32_t g0 = zxv_zt_generate_n(&mg, &tok, ids, n, 12, single, sizeof single, err, sizeof err);
    zxv_zt_base_t *base = NULL;
    CHECK(zxv_zt_base_load(&base, &mg, err, sizeof err) == 0 && base, "base loaded once (%s)", err);
    zxv_zt_agent_t *plain = NULL, *tuned = NULL, *half = NULL, *bad = NULL;
    CHECK(zxv_zt_agent_new(&plain, base, NULL, 0, err, sizeof err) == 0 &&
              !zxv_zt_agent_has_lora(plain),
          "agent without an adapter");
    CHECK(zxv_zt_agent_new(&tuned, base, &lg, 65536, err, sizeof err) == 0 &&
              zxv_zt_agent_has_lora(tuned),
          "agent with the LoRA adapter (%s)", err);
    CHECK(zxv_zt_agent_new(&half, base, &lg, 32768, err, sizeof err) == 0,
          "agent with the same adapter at half scale");

    char a[4096], b[4096], c[4096], a2[4096], b2[4096];
    int32_t ra = zxv_zt_agent_generate(plain, &tok, ids, n, 12, a, sizeof a, err, sizeof err);
    int32_t rb = zxv_zt_agent_generate(tuned, &tok, ids, n, 12, b, sizeof b, err, sizeof err);
    int32_t rc = zxv_zt_agent_generate(half, &tok, ids, n, 12, c, sizeof c, err, sizeof err);
    CHECK(ra == g0 && !strcmp(a, single),
          "the plain agent replies exactly as the single slot does (%d tokens)", (int) ra);
    CHECK(rb > 0 && rc > 0 && strcmp(a, b) && strcmp(b, c),
          "the adapter changes the reply, and its scale matters (%d, %d tokens)", (int) rb,
          (int) rc);
    zxv_zt_verdict_t vd;
    zxv_zt_last_verdict(&vd);
    CHECK(vd.n_gen == (uint32_t) rc && !vd.forward_error, "the agent's request sets the verdict");
    int32_t rb2 = zxv_zt_agent_generate(tuned, &tok, ids, n, 12, b2, sizeof b2, err, sizeof err);
    int32_t ra2 = zxv_zt_agent_generate(plain, &tok, ids, n, 12, a2, sizeof a2, err, sizeof err);
    CHECK(rb2 == rb && ra2 == ra && !strcmp(a2, a) && !strcmp(b2, b),
          "agents taking turns on one base keep their own replies, bit for bit");

    /* an adapter of all zeros on the agent gives the plain reply */
    uint8_t *z = malloc(lv->file_len);
    memcpy(z, lfile, lv->file_len);
    zt_gguf_t zg;
    zt_gguf_open(&zg, z, lv->file_len);
    for (uint64_t i = 0; i < zg.n_tensors; i++) {
        zt_gguf_tensor_t t;
        if (zt_gguf_tensor(&zg, i, &t) == 0 && t.name.len > 6 && t.name.p[t.name.len - 1] == 'b')
            memset(z + (t.data - z), 0, (size_t) t.n_bytes);
    }
    zxv_zt_agent_t *zero = NULL;
    char zr[4096];
    CHECK(zxv_zt_agent_new(&zero, base, &zg, 65536, err, sizeof err) == 0 &&
              zxv_zt_agent_generate(zero, &tok, ids, n, 12, zr, sizeof zr, err, sizeof err) == ra &&
              !strcmp(zr, a),
          "an adapter with B = 0 gives the plain reply");
    memset(z, 0, lv->file_len); /* the agent copied what it needs */
    CHECK(zxv_zt_agent_generate(zero, &tok, ids, n, 12, zr, sizeof zr, err, sizeof err) == ra &&
              !strcmp(zr, a),
          "the agent keeps working after the adapter's buffer is gone");
    zxv_zt_agent_free(zero);
    free(z);

    /* refusals */
    zt_gguf_t qg;
    const mf_variant_t *q3 = &MF_VARIANTS[1];
    uint8_t *q3file = rebuild(q3);
    zt_gguf_open(&qg, q3file, q3->file_len);
    zxv_zt_base_t *b3 = NULL;
    zxv_zt_base_load(&b3, &qg, err, sizeof err);
    CHECK(zxv_zt_agent_new(&bad, b3, &lg, 65536, err, sizeof err) != 0 && !bad &&
              strstr(err, "architecture"),
          "a qwen2 adapter on a qwen3 base is refused: %s", err);
    CHECK(zxv_zt_agent_new(&bad, base, &lg, 0, err, sizeof err) != 0,
          "an adapter scale of 0 is refused at this level: %s", err);
    CHECK(zxv_zt_base_free(base) == -1, "a base with agents on it is not freed");
    zxv_zt_agent_free(plain);
    zxv_zt_agent_free(tuned);
    zxv_zt_agent_free(half);
    CHECK(zxv_zt_base_free(base) == 0 && zxv_zt_base_free(b3) == 0,
          "bases free once their agents are gone");
    free(q3file);

    /* M6: the host's slots and agents, from files */
    uint64_t clen = 0;
    uint8_t *combined = model_with_tokenizer(file, v->file_len, &clen);
    char mpath[1024], lpath[1024], npath[1024];
    snprintf(mpath, sizeof mpath, "%s/zxv_swarm_test_model.gguf", dir);
    snprintf(lpath, sizeof lpath, "%s/zxv_swarm_test_lora.gguf", dir);
    snprintf(npath, sizeof npath, "%s/zxv_swarm_test_missing.gguf", dir);
    CHECK(combined && write_file(mpath, combined, clen) == 0 &&
              write_file(lpath, lfile, lv->file_len) == 0,
          "test model (with tokenizer) and adapter written to %s", dir);
    CHECK(zxv_slot_open(0, mpath) == 0 && zxv_slot_info(0)->can_generate, "slot 0 opened: %s",
          zxv_slot_info(0)->status);
    CHECK(zxv_slot_open(9, mpath) == -1 && zxv_slot_info(9) == NULL, "slot 9 is out of range");
    CHECK(zxv_slot_open(1, npath) == -1 && !zxv_slot_info(1)->loaded,
          "a missing file leaves the slot empty: %s", zxv_slot_info(1)->status);
    CHECK(zxv_agent_open(0, 0, NULL, 0, err, sizeof err) == 0 &&
              zxv_agent_open(1, 0, lpath, 0, err, sizeof err) == 0 && zxv_agent_slot(1) == 0,
          "agents 0 (plain) and 1 (adapter) on slot 0 (%s)", err);
    CHECK(zxv_agent_open(2, 1, NULL, 0, err, sizeof err) == -1,
          "an agent on an empty slot is refused: %s", err);
    CHECK(zxv_agent_open(3, 0, npath, 0, err, sizeof err) == -1 && zxv_agent_slot(3) == -1,
          "a missing adapter file is refused: %s", err);
    char h0[4096], h1[4096];
    int r0 = zxv_agent_answer(0, msg, h0, sizeof h0);
    int r1 = zxv_agent_answer(1, msg, h1, sizeof h1);
    char want_plain[4096], want_tuned[4096];
    zxv_zt_base_t *chk = NULL;
    zxv_zt_agent_t *cp = NULL, *ct = NULL;
    zxv_zt_base_load(&chk, &mg, err, sizeof err);
    zxv_zt_agent_new(&cp, chk, NULL, 0, err, sizeof err);
    zxv_zt_agent_new(&ct, chk, &lg, 65536, err, sizeof err);
    uint64_t off = (tok.add_bos && tok.bos >= 0) ? 1 : 0;
    int32_t ids2[66];
    if (off) ids2[0] = tok.bos;
    memcpy(ids2 + off, ids, (size_t) n * sizeof *ids);
    zxv_zt_agent_generate(cp, &tok, ids2, n + off, ZXV_MODEL_MAX_NEW, want_plain, sizeof want_plain,
                          err, sizeof err);
    zxv_zt_agent_generate(ct, &tok, ids2, n + off, ZXV_MODEL_MAX_NEW, want_tuned, sizeof want_tuned,
                          err, sizeof err);
    CHECK(r0 == 1 && r1 == 1 && !strcmp(h0, want_plain) && !strcmp(h1, want_tuned) &&
              strcmp(h0, h1),
          "slot agents answer from the file, each with its own adapter");
    zxv_zt_agent_free(cp);
    zxv_zt_agent_free(ct);
    zxv_zt_base_free(chk);
    CHECK(zxv_agent_answer(5, msg, h0, sizeof h0) == 0 && strstr(h0, "no model"),
          "an agent that was never opened says so: %s", h0);
    zxv_slot_close(0);
    CHECK(zxv_agent_slot(0) == -1 && zxv_agent_slot(1) == -1 && !zxv_slot_info(0)->loaded,
          "closing the slot closes its agents");
    remove(mpath);
    remove(lpath);

    zxv_zt_release();
    free(combined);
    free(tar);
    free(file);
    free(lfile);
    printf(failures ? "swarm slots test FAILED (%d)\n" : "swarm slots test passed\n", failures);
    return failures ? 1 : 0;
}

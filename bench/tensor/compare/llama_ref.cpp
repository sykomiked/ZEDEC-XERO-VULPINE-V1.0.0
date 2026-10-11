// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
// Reference runner on llama.cpp for comparing against the ZXV engine.
//   llama_ref gen  MODEL PROMPT N THREADS          -> greedy ids, text, timings
//   llama_ref dump MODEL TEXTFILE NTOK IDS.bin LOGITS.f32 THREADS [UBATCH]
#include "llama.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
static double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
static std::vector<llama_token> tok(const llama_vocab *v, const std::string &s, bool special)
{
    std::vector<llama_token> t(s.size() + 16);
    int n = llama_tokenize(v, s.data(), (int) s.size(), t.data(), (int) t.size(), true, special);
    if (n < 0) {
        t.resize(-n);
        n = llama_tokenize(v, s.data(), (int) s.size(), t.data(), (int) t.size(), true, special);
    }
    t.resize(n);
    return t;
}
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    std::string mode = argv[1];
    llama_backend_init();
    auto mp = llama_model_default_params();
    mp.n_gpu_layers = 0;
    llama_model *model = llama_model_load_from_file(argv[2], mp);
    if (!model) return 3;
    const llama_vocab *vocab = llama_model_get_vocab(model);
    int nv = llama_vocab_n_tokens(vocab);
    auto cp = llama_context_default_params();
    if (mode == "gen") {
        std::string prompt = argv[3];
        int n = atoi(argv[4]);
        int th = atoi(argv[5]);
        auto ids = tok(vocab, prompt, true);
        cp.n_ctx = ids.size() + n + 8;
        cp.n_batch = cp.n_ctx;
        cp.n_threads = th;
        cp.n_threads_batch = th;
        llama_context *ctx = llama_init_from_model(model, cp);
        double t0 = now();
        if (llama_decode(ctx, llama_batch_get_one(ids.data(), ids.size()))) return 5;
        double t1 = now();
        std::vector<llama_token> out;
        std::string text;
        for (int i = 0; i < n; i++) {
            float *lg = llama_get_logits_ith(ctx, -1);
            int best = 0;
            for (int j = 1; j < nv; j++)
                if (lg[j] > lg[best]) best = j;
            if (llama_vocab_is_eog(vocab, best)) break;
            out.push_back(best);
            char buf[256];
            int k = llama_token_to_piece(vocab, best, buf, sizeof buf, 0, false);
            if (k > 0) text.append(buf, k);
            llama_token t = best;
            if (llama_decode(ctx, llama_batch_get_one(&t, 1))) return 5;
        }
        double t2 = now();
        printf("ref.n_prompt=%zu\nref.prefill_tps=%.3f\nref.n_gen=%zu\nref.gen_tps=%.3f\nref.ids=",
               ids.size(), ids.size() / (t1 - t0), out.size(), out.size() / (t2 - t1));
        for (size_t i = 0; i < out.size(); i++) printf(i ? ",%d" : "%d", out[i]);
        printf("\nref.prompt_ids=");
        for (size_t i = 0; i < ids.size(); i++) printf(i ? ",%d" : "%d", ids[i]);
        printf("\n");
        fprintf(stderr, "%s", text.c_str());
        llama_free(ctx);
    } else {
        FILE *f = fopen(argv[3], "rb");
        std::string s;
        char b[65536];
        size_t k;
        while ((k = fread(b, 1, sizeof b, f)) > 0) s.append(b, k);
        fclose(f);
        int ntok = atoi(argv[4]);
        int th = atoi(argv[7]);
        auto ids = tok(vocab, s, false);
        if ((int) ids.size() > ntok) ids.resize(ntok);
        cp.n_ctx = ids.size();
        cp.n_batch = ids.size();
        cp.n_ubatch = argc > 8 ? atoi(argv[8]) : 512;
        cp.n_threads = th;
        cp.n_threads_batch = th;
        llama_context *ctx = llama_init_from_model(model, cp);
        llama_batch bt = llama_batch_init(ids.size(), 0, 1);
        for (size_t i = 0; i < ids.size(); i++) {
            bt.token[i] = ids[i];
            bt.pos[i] = i;
            bt.n_seq_id[i] = 1;
            bt.seq_id[i][0] = 0;
            bt.logits[i] = 1;
        }
        bt.n_tokens = ids.size();
        double t0 = now();
        if (llama_decode(ctx, bt)) return 5;
        double t1 = now();
        FILE *fi = fopen(argv[5], "wb");
        fwrite(ids.data(), 4, ids.size(), fi);
        fclose(fi);
        FILE *fl = fopen(argv[6], "wb");
        for (size_t i = 0; i < ids.size(); i++) fwrite(llama_get_logits_ith(ctx, i), 4, nv, fl);
        fclose(fl);
        printf("ref.n=%zu ref.tps=%.3f\n", ids.size(), ids.size() / (t1 - t0));
    }
    return 0;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * zxva_bench.c — walk a corpus and vectorise it, measuring every conversion.
 *
 *   audio  WAV / AIFF / FLAC (kernel/src/media/zui_audio.c) -> ZXVA through
 *          the analyser (kernel/src/vaudio/zxva_analyse.c), once per backend:
 *          the CPU integer reference, then CUDA and Vulkan when a device is
 *          there. Every GPU result is compared byte for byte with the CPU's.
 *   data   every file (audio included, as a media reference) -> ZXVS
 *          (kernel/src/vshape/zxvs.c) with its curve errors.
 *
 * Output: one JSON object per line per conversion (--jsonl, default stdout)
 * and one summary object (--summary FILE, also printed to stderr). Nothing in
 * the corpus is changed; --out DIR writes <cid>.zxva / <cid>.zxvs beside each
 * other, named by the ORIGINAL's CID (the originals are kept, by CID).
 *
 * WHAT IS AND IS NOT IMPLEMENTED
 *   Done: recursive walk (regular files, symlinks not followed), per-backend
 *     timing (wall ms per analysis; GPU kernel ms from device timers), SNR,
 *     band error, residual exactness, GPU vs CPU byte identity, ZXVS kind /
 *     format / curve max and RMS error / node counts, a summary per backend
 *     and per data kind.
 *   Not done: MP3 / AAC / Opus / Vorbis (the decoder refuses them; they are
 *     listed with the reason), files of 4 GiB or more, Metal, parallel files.
 *   The vector forms scale cleanly; they do not add detail that the source
 *   did not have. The measured numbers here are what says how close they are.
 *
 * Build (from the repository root; hosted tool, -ldl for the GPU loaders):
 *   K=kernel; cc -std=c11 -O2 -Wall -Wextra -DTEST_HOST -I$K/include \
 *     -I$K/src/modbind -I$K/src/vaudio -I$K/src/media -I$K/src/vshape \
 *     tools/zxva/zxva_bench.c $K/src/vaudio/zxva_analyse.c $K/src/vaudio/zxva.c \
 *     $K/src/vaudio/zxva_cuda.c $K/src/vaudio/zxva_vk.c $K/src/media/zui_audio.c \
 *     $K/src/vshape/zxvs.c $K/src/robin_debanks/sha256.c -ldl -o zxva_bench
 *   For the real Vulkan backend add -DZXVA_VK_SPV_H='"/path/zxva_vk_spv.h"'
 *   (sh kernel/src/vaudio/gen_zxva_vk_spv.sh /path/zxva_vk_spv.h); without
 *   it zxva_vk.c is a stub and Vulkan reports "unavailable".
 *
 * Run:
 *   ./zxva_bench [--mode all|audio|data] [--backends cpu,cuda,vulkan]
 *                [--no-residual] [--tol Q16] [--max-mb N] [--out DIR]
 *                [--jsonl FILE] [--summary FILE] PATH...
 */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "zui_audio.h"
#include "zxva.h"
#include "zxva_analyse.h"
#include "zxva_cuda.h"
#include "zxva_vk.h"
#include "zxvs.h"

enum { B_CPU, B_CUDA, B_VK, NB };
static const char *bname[NB] = {"cpu", "cuda", "vulkan"};

typedef struct {
    bool want, up;
    char dev[320];
    zxva_backend_t be;
    /* summary */
    uint32_t files, fails, bitexact, mismatch, exact;
    double ms, kern_ms;
    uint64_t src_bytes, zxva_bytes;
    int64_t snr_sum;
    int32_t snr_min;
    uint32_t band_max;
} back_t;

typedef struct {
    uint32_t files, fails;
    uint64_t src_bytes, desc_bytes;
    uint32_t ncurves, nseg, npts, worst_max_q24, worst_rms_q24;
    double ms;
} kind_t;

static back_t B[NB];
static kind_t K[16];
static zxva_cuda_t *cuda;
static zxva_vk_t *vk;
static FILE *jl;
static const char *outdir;
static bool do_audio = true, do_data = true, residual = true;
static uint32_t tol_q16;
static uint64_t max_bytes = (uint64_t) 512 << 20;
static uint32_t n_files, n_skipped, n_refused, n_audio;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double) t.tv_sec * 1e3 + (double) t.tv_nsec / 1e6;
}

/* the CID as a CIDv1 string: multibase 'b' + RFC 4648 base32, lower case */
static void cid_str(const uint8_t *cid, char *s)
{
    static const char a[] = "abcdefghijklmnopqrstuvwxyz234567";
    uint32_t acc = 0, nb = 0, o = 0;
    s[o++] = 'b';
    for (int i = 0; i < ZXVA_CID_LEN; i++) {
        acc = (acc << 8) | cid[i];
        nb += 8;
        while (nb >= 5) {
            s[o++] = a[(acc >> (nb - 5)) & 31];
            nb -= 5;
        }
    }
    if (nb) s[o++] = a[(acc << (5 - nb)) & 31];
    s[o] = 0;
}

static void jstr(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char) *s;
        if (c == '"' || c == '\\')
            fprintf(f, "\\%c", c);
        else if (c < 0x20)
            fprintf(f, "\\u%04x", c);
        else
            fputc(c, f);
    }
    fputc('"', f);
}

static void save(const char *cid, const char *ext, const uint8_t *d, uint64_t n)
{
    if (!outdir) return;
    char p[4096];
    if (snprintf(p, sizeof p, "%s/%s.%s", outdir, cid, ext) >= (int) sizeof p) return;
    FILE *f = fopen(p, "wb");
    if (!f) {
        fprintf(stderr, "zxva_bench: cannot write %s: %s\n", p, strerror(errno));
        return;
    }
    if (fwrite(d, 1, n, f) != n) fprintf(stderr, "zxva_bench: short write %s\n", p);
    fclose(f);
}

static int run_one(back_t *bk, int bi, const zxva_pcm_t *pcm, zxva_an_cfg_t *cfg, uint8_t **outp,
                   zxva_an_report_t *rep, double *ms, double *kms)
{
    cfg->backend = bi == B_CPU ? NULL : &bk->be;
    uint64_t ws = zxva_analyse_work_size(pcm, cfg), ob = zxva_analyse_out_bound(pcm, cfg);
    if (!ws || !ob || ws > ((uint64_t) 16 << 30)) return ZXVA_AN_ECAP;
    void *w = malloc(ws);
    uint8_t *o = malloc(ob);
    if (!w || !o) {
        free(w), free(o);
        return ZXVA_AN_ECAP;
    }
    double k0 = bi == B_CUDA ? zxva_cuda_kernel_ms(cuda) : bi == B_VK ? zxva_vk_kernel_ms(vk) : 0;
    double t0 = now_ms();
    int64_t r = zxva_analyse(pcm, cfg, w, ws, o, ob, rep);
    *ms = now_ms() - t0;
    *kms = (bi == B_CUDA ? zxva_cuda_kernel_ms(cuda) : bi == B_VK ? zxva_vk_kernel_ms(vk) : 0) - k0;
    free(w);
    if (r < 0) {
        free(o);
        return (int) r;
    }
    *outp = o;
    return (int) r;
}

static void audio(const char *path, const uint8_t *d, uint64_t n, uint8_t vcid[ZXVA_CID_LEN],
                  int32_t *fid_cdb)
{
    zui_audio_info_t in;
    char cs[80];
    uint8_t cid[ZXVA_CID_LEN];
    zxva_cid(d, n, cid);
    cid_str(cid, cs);
    int pr = zui_audio_probe(d, (uint32_t) n, &in);
    if (pr < 0) {
        n_refused++;
        fprintf(jl, "{\"type\":\"audio\",\"file\":");
        jstr(jl, path);
        fprintf(jl, ",\"cid\":\"%s\",\"refused\":", cs);
        jstr(jl, zui_audio_reason(pr == ZUI_AUD_EUNSUP ? (int) zui_audio_sniff(d, (uint32_t) n) : pr));
        fprintf(jl, "}\n");
        return;
    }
    uint64_t frames = in.frames ? in.frames : (uint64_t) in.max_block * 65536;
    uint64_t cap = frames * in.nch;
    uint32_t ww = zui_audio_work_words(&in);
    int32_t *pcmb = cap && cap < ((uint64_t) 1 << 31) ? malloc(cap * 4) : NULL;
    int32_t *wk = ww ? malloc((size_t) ww * 4) : NULL;
    int64_t got = pcmb ? zui_audio_decode(d, (uint32_t) n, pcmb, cap, wk, ww, &in, ZUI_AUD_F_MD5) : -1;
    free(wk);
    if (got <= 0 || (uint64_t) got > frames) {
        n_refused++;
        fprintf(jl, "{\"type\":\"audio\",\"file\":");
        jstr(jl, path);
        fprintf(jl, ",\"cid\":\"%s\",\"refused\":", cs);
        jstr(jl, got < 0 ? zui_audio_reason((int) got) : "no frames or over the cap");
        fprintf(jl, "}\n");
        free(pcmb);
        return;
    }
    n_audio++;
    zxva_pcm_t pcm = {pcmb, (uint64_t) got, in.nch, in.rate, in.bits};
    zxva_an_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.residual = residual;
    cfg.cid = cid;
    cfg.src_bytes = n;
    cfg.src_kind = in.kind == ZUI_AUD_WAV ? ZXVA_SRC_WAV : in.kind == ZUI_AUD_AIFF ? ZXVA_SRC_AIFF
                 : in.kind == ZUI_AUD_FLAC ? ZXVA_SRC_FLAC : ZXVA_SRC_OTHER;
    uint8_t *ref = NULL;
    int64_t refn = -1;
    for (int bi = 0; bi < NB; bi++) {
        back_t *bk = &B[bi];
        if (!bk->want || !bk->up) continue;
        zxva_an_report_t rep;
        memset(&rep, 0, sizeof rep);
        uint8_t *o = NULL;
        double ms = 0, kms = 0;
        int r = run_one(bk, bi, &pcm, &cfg, &o, &rep, &ms, &kms);
        bk->files++;
        fprintf(jl, "{\"type\":\"audio\",\"file\":");
        jstr(jl, path);
        fprintf(jl, ",\"cid\":\"%s\",\"backend\":\"%s\",\"device\":", cs, bname[bi]);
        jstr(jl, bk->dev);
        fprintf(jl, ",\"rate\":%u,\"nch\":%u,\"bits\":%u,\"frames\":%llu,\"src_bytes\":%llu", in.rate, in.nch,
                in.bits, (unsigned long long) got, (unsigned long long) n);
        if (r < 0) {
            bk->fails++;
            fprintf(jl, ",\"error\":%d,\"ms\":%.3f}\n", r, ms);
            continue;
        }
        int bitexact = -1; /* -1: no CPU reference (it was not run or failed) */
        if (bi == B_CPU) {
            ref = o;
            refn = r;
            bitexact = 1;
        } else if (ref) {
            bitexact = refn == r && !memcmp(ref, o, (size_t) r);
        }
        if (bi != B_CPU) bitexact == 1 ? bk->bitexact++ : bitexact == 0 ? bk->mismatch++ : 0;
        if (bi == B_CPU) bk->bitexact++;
        bk->ms += ms;
        bk->kern_ms += kms;
        bk->src_bytes += n;
        bk->zxva_bytes += (uint64_t) r;
        bk->snr_sum += rep.snr_cdb;
        if (bk->files - bk->fails == 1 || rep.snr_cdb < bk->snr_min) bk->snr_min = rep.snr_cdb;
        if (rep.band_err_cdb > bk->band_max) bk->band_max = rep.band_err_cdb;
        if (rep.exact) bk->exact++;
        fprintf(jl,
                ",\"ms\":%.3f,\"kernel_ms\":%.3f,\"stft_frames\":%llu,\"zxva_bytes\":%d,\"param_bytes\":%u,"
                "\"tracks\":%u,\"snr_cdb\":%d,\"snr_tonal_cdb\":%d,\"band_err_cdb\":%u,\"residual\":%s,"
                "\"residual_exact\":%s,\"bit_exact_vs_cpu\":%s}\n",
                ms, kms, (unsigned long long) rep.stft_frames, r, rep.param_bytes, rep.ntracks, rep.snr_cdb,
                rep.snr_tonal_cdb, rep.band_err_cdb, residual ? "true" : "false", rep.exact ? "true" : "false",
                bitexact < 0 ? "null" : bitexact ? "true" : "false");
        if (bi == B_CPU || !ref) {
            save(cs, "zxva", o, (uint64_t) r);
            zxva_cid(o, (uint64_t) r, vcid);
            *fid_cdb = rep.snr_cdb;
        }
        if (o != ref) free(o);
    }
    free(ref);
    free(pcmb);
}

static void data(const char *path, const uint8_t *d, uint64_t n, const uint8_t *vcid, int32_t fid_cdb)
{
    zxvs_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.tol_q16 = tol_q16;
    if (vcid) {
        cfg.ref_cid = vcid;
        cfg.ref_kind = 2;
        cfg.ref_fid_known = true;
        cfg.ref_fid_cdb = fid_cdb;
    }
    uint64_t ws = zxvs_work_size(n, &cfg), ob = zxvs_out_bound(n, &cfg);
    void *w = ws ? malloc(ws) : NULL;
    uint8_t *o = ob ? malloc(ob) : NULL;
    zxvs_report_t rep;
    memset(&rep, 0, sizeof rep);
    double t0 = now_ms();
    int64_t r = w && o ? zxvs_describe(d, n, w, ws, o, ob, &cfg, &rep) : ZXVS_ECAP;
    double ms = now_ms() - t0;
    free(w);
    zxvs_info_t info;
    char cs[80] = "";
    if (r > 0 && zxvs_check(o, (uint32_t) r, &info) == 0) cid_str(info.cid, cs);
    else if (r > 0) r = ZXVS_EFORMAT; /* never expected: describe checks its own output */
    /* media: does the REF section link a vector rendition (has_vec)? */
    const uint8_t *rb = NULL;
    uint32_t rl = 0;
    bool vec = r > 0 && info.has_ref && !zxvs_section(o, (uint32_t) r, ZXVS_REF, 0, &rb, &rl) && rl >= 2 && rb[1];
    kind_t *k = &K[r > 0 ? info.kind & 15 : 0];
    fprintf(jl, "{\"type\":\"data\",\"file\":");
    jstr(jl, path);
    if (r < 0) {
        K[0].fails++;
        fprintf(jl, ",\"error\":%lld,\"src_bytes\":%llu}\n", (long long) r, (unsigned long long) n);
        free(o);
        return;
    }
    k->files++;
    k->ms += ms;
    k->src_bytes += n;
    k->desc_bytes += (uint64_t) r;
    k->ncurves += rep.ncurves;
    k->nseg += rep.nseg;
    k->npts += rep.npts;
    if (rep.curve_max_q24 > k->worst_max_q24) k->worst_max_q24 = rep.curve_max_q24;
    if (rep.curve_rms_q24 > k->worst_rms_q24) k->worst_rms_q24 = rep.curve_rms_q24;
    fprintf(jl,
            ",\"kind\":\"%s\",\"format\":\"%s\",\"cid\":\"%s\",\"ms\":%.3f,\"src_bytes\":%llu,\"desc_bytes\":%lld,"
            "\"curves\":%u,\"points\":%u,\"segments\":%u,\"curve_max_err_pct\":%.5f,\"curve_rms_err_pct\":%.5f,"
            "\"entropy_max_err_pct\":%.5f,\"nodes\":%u,\"total_nodes\":%u,\"hilbert_order\":%u,"
            "\"hilbert_edges\":%u,\"vector_rendition\":%s}\n",
            zxvs_kind_name(info.kind), zxvs_fmt_name(info.fmt), cs, ms, (unsigned long long) n, (long long) r,
            rep.ncurves, rep.npts, rep.nseg, rep.curve_max_q24 * 100.0 / 16777216.0,
            rep.curve_rms_q24 * 100.0 / 16777216.0, rep.ent_max_q24 * 100.0 / 16777216.0, rep.nodes,
            rep.total_nodes, rep.hilbert_order, rep.hilbert_edges, vec ? "true" : "false");
    save(cs, "zxvs", o, (uint64_t) r);
    free(o);
}

static void file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        n_skipped++;
        return;
    }
    struct stat st;
    if (fstat(fileno(f), &st) || (uint64_t) st.st_size > max_bytes || (uint64_t) st.st_size >= ((uint64_t) 1 << 32)) {
        fclose(f);
        n_skipped++;
        return;
    }
    uint64_t n = (uint64_t) st.st_size;
    uint8_t *d = malloc(n ? n : 1);
    if (!d || fread(d, 1, n, f) != n) {
        free(d);
        fclose(f);
        n_skipped++;
        return;
    }
    fclose(f);
    n_files++;
    uint32_t s = zui_audio_sniff(d, (uint32_t) n);
    uint8_t zc[ZXVA_CID_LEN];
    bool have = false;
    int32_t fid = 0;
    if (do_audio && s) {
        memset(zc, 0, sizeof zc);
        audio(path, d, n, zc, &fid);
        for (int i = 0; i < ZXVA_CID_LEN; i++) have |= zc[i] != 0;
    }
    if (do_data) data(path, d, n, have ? zc : NULL, fid);
    free(d);
}

static void walk(const char *path, int depth)
{
    struct stat st;
    if (lstat(path, &st)) return;
    if (S_ISREG(st.st_mode)) {
        file(path);
        return;
    }
    if (!S_ISDIR(st.st_mode) || depth > 64) return;
    DIR *dir = opendir(path);
    if (!dir) return;
    /* sorted, so runs are comparable */
    char **names = NULL;
    size_t nn = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || !strcmp(e->d_name, ".git")) continue;
        if (nn == cap) {
            cap = cap ? cap * 2 : 64;
            char **nw = realloc(names, cap * sizeof *names);
            if (!nw) break;
            names = nw;
        }
        names[nn++] = strdup(e->d_name);
    }
    closedir(dir);
    for (size_t i = 1; i < nn; i++)
        for (size_t j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char *t = names[j];
            names[j] = names[j - 1];
            names[j - 1] = t;
        }
    for (size_t i = 0; i < nn; i++) {
        char p[4096];
        if (names[i] && snprintf(p, sizeof p, "%s/%s", path, names[i]) < (int) sizeof p) walk(p, depth + 1);
        free(names[i]);
    }
    free(names);
}

static void summary(FILE *f)
{
    fprintf(f, "{\"type\":\"summary\",\"files\":%u,\"skipped\":%u,\"audio_files\":%u,\"audio_refused\":%u,", n_files,
            n_skipped, n_audio, n_refused);
    fprintf(f, "\"residual\":%s,\"tol_q16\":%u,\"backends\":[", residual ? "true" : "false", tol_q16 ? tol_q16 : 64);
    int first = 1;
    for (int bi = 0; bi < NB; bi++) {
        back_t *b = &B[bi];
        if (!b->want) continue;
        uint32_t ok = b->files - b->fails;
        fprintf(f, "%s{\"backend\":\"%s\",\"available\":%s,\"device\":", first ? "" : ",", bname[bi],
                b->up ? "true" : "false");
        jstr(f, b->dev);
        fprintf(f,
                ",\"ran\":%u,\"failed\":%u,\"bit_exact_vs_cpu\":%u,\"mismatch_vs_cpu\":%u,\"residual_exact\":%u,"
                "\"ms\":%.3f,\"kernel_ms\":%.3f,\"src_bytes\":%llu,\"zxva_bytes\":%llu,\"snr_mean_cdb\":%lld,"
                "\"snr_min_cdb\":%d,\"band_err_max_cdb\":%u}",
                b->files, b->fails, b->bitexact, b->mismatch, b->exact, b->ms, b->kern_ms,
                (unsigned long long) b->src_bytes, (unsigned long long) b->zxva_bytes,
                ok ? (long long) (b->snr_sum / ok) : 0, ok ? b->snr_min : 0, b->band_max);
        first = 0;
    }
    fprintf(f, "],\"data\":[");
    first = 1;
    for (int k = 1; k < 16; k++) {
        kind_t *q = &K[k];
        if (!q->files) continue;
        fprintf(f,
                "%s{\"kind\":\"%s\",\"files\":%u,\"ms\":%.3f,\"src_bytes\":%llu,\"desc_bytes\":%llu,\"curves\":%u,"
                "\"points\":%u,\"segments\":%u,\"curve_max_err_pct\":%.5f,\"curve_rms_err_pct\":%.5f}",
                first ? "" : ",", zxvs_kind_name((uint32_t) k), q->files, q->ms, (unsigned long long) q->src_bytes,
                (unsigned long long) q->desc_bytes, q->ncurves, q->npts, q->nseg, q->worst_max_q24 * 100.0 / 16777216.0,
                q->worst_rms_q24 * 100.0 / 16777216.0);
        first = 0;
    }
    fprintf(f, "],\"data_failed\":%u}\n", K[0].fails);
}

static int usage(void)
{
    fprintf(stderr, "usage: zxva_bench [--mode all|audio|data] [--backends cpu,cuda,vulkan] [--no-residual]\n"
                    "                  [--tol Q16] [--max-mb N] [--out DIR] [--jsonl FILE] [--summary FILE] PATH...\n");
    return 2;
}

int main(int argc, char **argv)
{
    const char *jpath = NULL, *spath = NULL, *backs = "cpu,cuda,vulkan";
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++) {
        const char *a = argv[i];
        bool hasv = i + 1 < argc;
        if (!strcmp(a, "--no-residual"))
            residual = false;
        else if (!strcmp(a, "--mode") && hasv) {
            const char *m = argv[++i];
            do_audio = !strcmp(m, "all") || !strcmp(m, "audio");
            do_data = !strcmp(m, "all") || !strcmp(m, "data");
            if (!do_audio && !do_data) return usage();
        } else if (!strcmp(a, "--backends") && hasv)
            backs = argv[++i];
        else if (!strcmp(a, "--tol") && hasv)
            tol_q16 = (uint32_t) strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--max-mb") && hasv)
            max_bytes = (uint64_t) strtoull(argv[++i], NULL, 0) << 20;
        else if (!strcmp(a, "--out") && hasv)
            outdir = argv[++i];
        else if (!strcmp(a, "--jsonl") && hasv)
            jpath = argv[++i];
        else if (!strcmp(a, "--summary") && hasv)
            spath = argv[++i];
        else
            return usage();
    }
    if (i >= argc) return usage();
    jl = jpath && strcmp(jpath, "-") ? fopen(jpath, "w") : stdout;
    if (!jl) {
        fprintf(stderr, "zxva_bench: %s: %s\n", jpath, strerror(errno));
        return 1;
    }
    if (outdir) mkdir(outdir, 0777);
    for (int bi = 0; bi < NB; bi++) {
        const char *p = strstr(backs, bname[bi]);
        size_t l = strlen(bname[bi]);
        B[bi].want = p && (p == backs || p[-1] == ',') && (p[l] == 0 || p[l] == ',');
    }
    if (B[B_CUDA].want || B[B_VK].want) B[B_CPU].want = true; /* the reference for bit-exactness */
    if (do_audio) {
        char err[256];
        zxva_backend_cpu(&B[B_CPU].be);
        B[B_CPU].up = true;
        snprintf(B[B_CPU].dev, sizeof B[B_CPU].dev, "C integer reference");
        if (B[B_CUDA].want) {
            err[0] = 0;
            cuda = zxva_cuda_new(-1, err, sizeof err);
            if (cuda) {
                zxva_cuda_backend(cuda, &B[B_CUDA].be);
                B[B_CUDA].up = true;
                snprintf(B[B_CUDA].dev, sizeof B[B_CUDA].dev, "%s", zxva_cuda_name(cuda));
            } else
                snprintf(B[B_CUDA].dev, sizeof B[B_CUDA].dev, "unavailable: %s", err);
        }
        if (B[B_VK].want) {
            err[0] = 0;
            vk = zxva_vk_new(-1, err, sizeof err);
            if (vk) {
                zxva_vk_backend(vk, &B[B_VK].be);
                B[B_VK].up = true;
                snprintf(B[B_VK].dev, sizeof B[B_VK].dev, "%s", zxva_vk_name(vk));
            } else
                snprintf(B[B_VK].dev, sizeof B[B_VK].dev, "unavailable: %s", err);
        }
        for (int bi = 0; bi < NB; bi++)
            if (B[bi].want) fprintf(stderr, "zxva_bench: %-6s %s\n", bname[bi], B[bi].dev);
    }
    for (; i < argc; i++) walk(argv[i], 0);
    if (jl != stdout) fclose(jl);
    summary(stderr);
    if (spath) {
        FILE *f = fopen(spath, "w");
        if (!f) {
            fprintf(stderr, "zxva_bench: %s: %s\n", spath, strerror(errno));
            return 1;
        }
        summary(f);
        fclose(f);
    }
    if (cuda) zxva_cuda_free(cuda);
    if (vk) zxva_vk_free(vk);
    uint32_t bad = K[0].fails;
    for (int bi = 0; bi < NB; bi++) bad += B[bi].fails + B[bi].mismatch;
    return bad ? 1 : 0;
}

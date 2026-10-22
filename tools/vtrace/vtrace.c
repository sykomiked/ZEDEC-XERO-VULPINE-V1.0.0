/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vtrace — any picture or film to vector (kernel/src/vtrace), from a shell.
 *
 *   vtrace [options] INPUT [-o OUT]
 *     INPUT  PNG, JPEG, GIF, BMP, ... (zx_image) or Y4M, AVI, MP4/MOV,
 *            animated GIF (zui_video)
 *     OUT    picture: OUT.zxvg and its provenance record OUT.zxvt
 *            film:    OUT.zxvm
 *            (default: INPUT with the extension replaced)
 *   --preset pixel|icon|illustration|photo   (default illustration)
 *   --backend cpu|cuda|vulkan|auto           (default cpu; auto: the first
 *                                             GPU that opens, else cpu)
 *   --render FILE.ppm  --scale N             the result drawn back at N x
 *   --gop N  --max-frames N                  films
 *   --json                                   the report as one JSON object
 *
 *   vtrace --bench DIR [--presets LIST|all] [--backends LIST|all]
 *          [--jsonl FILE] [--summary FILE]
 *     every picture and film under DIR (recursively), through every backend
 *     listed that opens (cpu always runs, as the reference), one JSON object
 *     per (file, backend, preset) to --jsonl (default stdout) and totals per
 *     backend to --summary. Fields: file kind cid backend preset ms bytes
 *     psnr_cdb ssim_x10000 exact bit_exact_vs_cpu (+ w h frames paths
 *     fallbacks error).
 *
 * Exit status: 0 done, 1 a file failed, 2 usage. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "zui_canvas.h"
#include "zx_image.h"
#include "zui_vec.h"
#include "zui_video.h"
#include "zui_vtrace.h"
#include "zui_vtrace_video.h"
#include "zvt_gpu.h"

/* ---- small helpers -------------------------------------------------------- */
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static uint8_t *slurp(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long len = ftell(f);
    if (len < 0 || len > 0x7FFFFFFFL) {
        fclose(f);
        return 0;
    }
    rewind(f);
    uint8_t *d = malloc((size_t) len + 1);
    if (d && fread(d, 1, (size_t) len, f) != (size_t) len) {
        free(d);
        d = 0;
    }
    fclose(f);
    *n = (uint32_t) len;
    return d;
}

static int spit(const char *path, const uint8_t *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(d, 1, n, f);
    return fclose(f) == 0 && w == n ? 0 : -1;
}

static void hex(const uint8_t *d, uint32_t n, char *out)
{
    static const char x[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n; i++) out[2 * i] = x[d[i] >> 4], out[2 * i + 1] = x[d[i] & 15];
    out[2 * n] = 0;
}

static void json_str(FILE *f, const char *s)
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

static int preset_of(const char *s)
{
    for (int p = 0; p < ZVT_PRESET_COUNT; p++)
        if (!strcmp(s, zvt_preset_name((zvt_preset_t) p))) return p;
    return -1;
}

/* ---- backends ------------------------------------------------------------ */
typedef struct {
    const char *name;
    zvt_backend_t be;
    bool ok;
    void *h;
    char why[256];
} bk_t;

enum { BK_CPU, BK_CUDA, BK_VK, BK_N };
static bk_t bks[BK_N];

static void open_backends(bool cuda, bool vk)
{
    bks[BK_CPU] = (bk_t){"cpu", zvt_cpu_backend, true, 0, ""};
    bks[BK_CUDA].name = "cuda";
    bks[BK_VK].name = "vulkan";
    if (cuda) {
        zvt_cuda_t *g = zvt_cuda_new(0, bks[BK_CUDA].why, sizeof bks[BK_CUDA].why);
        if (g) zvt_cuda_backend(g, &bks[BK_CUDA].be), bks[BK_CUDA].ok = true, bks[BK_CUDA].h = g;
    }
    if (vk) {
        zvt_vk_t *g = zvt_vk_new(-1, bks[BK_VK].why, sizeof bks[BK_VK].why);
        if (g) zvt_vk_backend(g, &bks[BK_VK].be), bks[BK_VK].ok = true, bks[BK_VK].h = g;
    }
}

static void close_backends(void)
{
    if (bks[BK_CUDA].h) zvt_cuda_free((zvt_cuda_t *) bks[BK_CUDA].h);
    if (bks[BK_VK].h) zvt_vk_free((zvt_vk_t *) bks[BK_VK].h);
}

static const char *device_name(int b)
{
    if (b == BK_CUDA && bks[b].h) return zvt_cuda_name((zvt_cuda_t *) bks[b].h);
    if (b == BK_VK && bks[b].h) return zvt_vk_name((zvt_vk_t *) bks[b].h);
    return "cpu";
}

/* ---- one job: a picture or a film through one backend and preset ---------- */
typedef struct {
    bool video;
    uint32_t w, h, frames, keyframes;
    uint8_t *out; /* ZXVG or ZXVM */
    int32_t len;
    uint8_t rec[ZVT_REC_LEN];
    zvt_result_t r; /* pictures */
    zvm_result_t m; /* films    */
    uint64_t ns;
    const char *err;
} job_t;

static bool is_film(const uint8_t *d, uint32_t n)
{
    zx_image_info_t ii;
    zui_video_info_t vi;
    if (zui_video_probe(d, n, 0, &vi) < 0) return false;
    if (vi.container == ZUI_VID_GIF) return zx_image_probe(d, n, &ii) == 0 && ii.frames > 1;
    return true;
}

static bool recognised(const uint8_t *d, uint32_t n)
{
    zx_image_info_t ii;
    zui_video_info_t vi;
    return zx_image_probe(d, n, &ii) == 0 || zui_video_probe(d, n, 0, &vi) >= 0;
}

static void run_job(const uint8_t *d, uint32_t n, zvt_preset_t pr, const zvt_backend_t *be,
                    const zvm_opts_t *o, job_t *j)
{
    memset(j, 0, sizeof *j);
    j->len = -1;
    zvt_params_t p;
    zvt_preset(&p, pr);
    if (is_film(d, n)) {
        j->video = true;
        uint64_t ws = zvm_trace_video_work(d, n, o);
        if (!ws) {
            j->err = "cannot open the film";
            return;
        }
        void *work = malloc(ws);
        uint32_t cap = n * 4u + (8u << 20);
        for (int tries = 0; tries < 6 && work; tries++, cap *= 2u) {
            free(j->out);
            j->out = malloc(cap);
            if (!j->out) break;
            uint64_t t0 = now_ns();
            j->len = zvm_trace_video(d, n, &p, o, be, work, ws, j->out, cap, &j->m);
            j->ns = now_ns() - t0;
            if (j->len != ZVT_E_SPACE || cap > 0x7FFFFFFFu / 2u) break;
        }
        free(work);
        if (j->len < 0) j->err = "the film did not trace";
        j->w = j->m.w, j->h = j->m.h, j->frames = j->m.frames, j->keyframes = j->m.keyframes;
        return;
    }
    zx_image_info_t ii;
    if (zx_image_probe(d, n, &ii) != 0) {
        j->err = "not a picture or film this build decodes";
        return;
    }
    void *iw = malloc((size_t) ii.work + 16);
    uint32_t *px = malloc(ii.px_bytes ? ii.px_bytes : 4);
    if (!iw || !px || zx_image_decode(d, n, iw, ii.work + 16, px, ii.w * ii.h, &ii) != 0) {
        free(iw), free(px);
        j->err = "the picture did not decode";
        return;
    }
    free(iw);
    j->w = ii.w, j->h = ii.h, j->frames = 1;
    uint64_t ws = zvt_work_size(ii.w, ii.h), os = zvt_out_max(ii.w, ii.h);
    void *work = ws ? malloc(ws) : 0;
    if (!work) {
        free(px);
        j->err = ws ? "out of memory" : "the picture is larger than the tracer takes";
        return;
    }
    for (int tries = 0; tries < 4 && os <= 0xFFFFFFFFull; tries++, os *= 2) {
        free(j->out);
        j->out = malloc(os);
        if (!j->out) break;
        uint64_t t0 = now_ns();
        j->len = zvt_trace(px, ii.w, ii.h, ii.w, d, n, &p, be, 0, work, ws, j->out, (uint32_t) os,
                           &j->r);
        j->ns = now_ns() - t0;
        if (j->len != ZVT_E_SPACE) break;
    }
    free(work), free(px);
    if (j->len < 0)
        j->err = "the picture did not trace";
    else
        zvt_record_write(&j->r, j->out, (uint32_t) j->len, j->rec);
}

static int32_t job_psnr(const job_t *j)
{
    return j->video ? j->m.psnr_avg : j->r.psnr_cdb;
}
static int32_t job_ssim(const job_t *j)
{
    return j->video ? j->m.ssim_avg : j->r.ssim;
}
static bool job_exact(const job_t *j)
{
    return j->video ? j->m.psnr_min == ZVT_PSNR_INF : j->r.exact;
}
static const uint8_t *job_cid(const job_t *j)
{
    return j->video ? j->m.cid : j->r.cid;
}

/* ---- drawing the result back --------------------------------------------- */
static int render_ppm(const job_t *j, uint32_t scale, const char *path)
{
    uint32_t w = j->w * scale, h = j->h * scale;
    if (!w || !h || w > 16384 || h > 16384) return -1;
    uint32_t *px = malloc((size_t) w * h * 4);
    zui_vec_work_t *vw = malloc(sizeof *vw);
    if (!px || !vw) return free(px), free(vw), -1;
    for (uint64_t i = 0; i < (uint64_t) w * h; i++) px[i] = 0xFFFFFFu;
    zui_surface_t s;
    zui_surface_init(&s, px, (int32_t) w, (int32_t) h, (int32_t) w);
    int rc = 0;
    if (j->video) { /* the first frame */
        zvm_info_t info;
        zvm_player_t pl;
        zvm_frame_t f;
        void *pw = 0;
        rc = zvm_probe(j->out, (uint32_t) j->len, &info);
        if (!rc) pw = malloc(info.work);
        if (!rc && (!pw || zvm_open(&pl, j->out, (uint32_t) j->len, pw, info.work) < 0 ||
                    zvm_next_frame(&pl, &s, (zui_rect_t){0, 0, (int32_t) w, (int32_t) h}, &f) != 1))
            rc = -1;
        free(pw);
    } else {
        rc = zui_vec_draw_zxvg(&s, vw, j->out, (uint32_t) j->len,
                               (zui_rect_t){0, 0, (int32_t) w, (int32_t) h}, 0);
    }
    FILE *f = rc ? 0 : fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        for (uint64_t i = 0; i < (uint64_t) w * h; i++) {
            uint8_t rgb[3] = {(uint8_t) (px[i] >> 16), (uint8_t) (px[i] >> 8), (uint8_t) px[i]};
            fwrite(rgb, 1, 3, f);
        }
        if (fclose(f) != 0) rc = -1;
    } else {
        rc = -1;
    }
    free(px), free(vw);
    return rc;
}

/* ---- the report ------------------------------------------------------------ */
static void report_json(FILE *f, const char *file, const char *backend, zvt_preset_t pr,
                        const job_t *j, int bitexact)
{
    char cid[2 * ZVT_CID_LEN + 1];
    hex(job_cid(j), ZVT_CID_LEN, cid);
    fprintf(f, "{\"file\":");
    json_str(f, file);
    fprintf(f, ",\"kind\":\"%s\",\"cid\":\"%s\",\"backend\":\"%s\",\"preset\":\"%s\"",
            j->video ? "video" : "image", j->len > 0 ? cid : "", backend, zvt_preset_name(pr));
    fprintf(f, ",\"ms\":%.3f,\"bytes\":%d,\"psnr_cdb\":%d,\"ssim_x10000\":%d,\"exact\":%s",
            (double) j->ns / 1e6, j->len > 0 ? j->len : 0, j->len > 0 ? job_psnr(j) : 0,
            j->len > 0 ? job_ssim(j) : 0, j->len > 0 && job_exact(j) ? "true" : "false");
    if (bitexact < 0)
        fprintf(f, ",\"bit_exact_vs_cpu\":null");
    else
        fprintf(f, ",\"bit_exact_vs_cpu\":%s", bitexact ? "true" : "false");
    fprintf(f, ",\"w\":%u,\"h\":%u,\"frames\":%u", j->w, j->h, j->frames);
    if (j->video)
        fprintf(f,
                ",\"keyframes\":%u,\"psnr_min_cdb\":%d,\"ssim_min_x10000\":%d,\"zxvg_bytes\":%llu,"
                "\"fallbacks\":%u",
                j->keyframes, j->m.psnr_min, j->m.ssim_min, (unsigned long long) j->m.zxvg_bytes,
                j->m.fallbacks);
    else
        fprintf(f, ",\"paths\":%u,\"colours\":%u,\"gradients\":%u,\"fallbacks\":%u", j->r.paths,
                j->r.colours, j->r.gradients, j->r.fallbacks);
    if (j->err) {
        fprintf(f, ",\"error\":");
        json_str(f, j->err);
        fprintf(f, ",\"code\":%d", j->len);
    }
    fprintf(f, "}\n");
}

static void report_text(const char *in, const char *backend, zvt_preset_t pr, const job_t *j,
                        uint32_t srclen)
{
    char cid[2 * ZVT_CID_LEN + 1];
    hex(job_cid(j), ZVT_CID_LEN, cid);
    int32_t ps = job_psnr(j);
    printf("%s: %ux%u %s, preset %s, backend %s, %.1f ms\n", in, j->w, j->h,
           j->video ? "film" : "picture", zvt_preset_name(pr), backend, (double) j->ns / 1e6);
    if (j->video) {
        printf("  %u frames (%u keyframes), %d bytes ZXVM (the frames as separate ZXVG: %llu), "
               "source %u bytes\n",
               j->frames, j->keyframes, j->len, (unsigned long long) j->m.zxvg_bytes, srclen);
        printf("  per frame PSNR min %d.%02d avg %d.%02d dB, SSIM min %.4f avg %.4f\n",
               j->m.psnr_min / 100, j->m.psnr_min % 100, ps / 100, ps % 100,
               j->m.ssim_min / 10000.0, j->m.ssim_avg / 10000.0);
    } else {
        printf("  %d bytes ZXVG (source %u), %u paths, %u colours, %u gradients, %u refits\n",
               j->len, srclen, j->r.paths, j->r.colours, j->r.gradients, j->r.refits);
        if (ps == ZVT_PSNR_INF)
            printf("  re-render identical: EXACT\n");
        else
            printf("  PSNR %d.%02d dB, SSIM %.4f: APPROXIMATE\n", ps / 100, ps % 100,
                   j->r.ssim / 10000.0);
    }
    printf("  source CID %s\n", cid);
}

/* ---- the benchmark ---------------------------------------------------------- */
typedef struct {
    uint32_t runs, ok, failed, exact, bitexact, compared;
    double ms, psnr_sum, ssim_sum;
    uint64_t bytes;
} tot_t;

static tot_t tot[BK_N][ZVT_PRESET_COUNT];
static FILE *jl;
static bool want_bk[BK_N], want_pr[ZVT_PRESET_COUNT];
static const zvm_opts_t *g_opts;
static int bench_fail;

static void bench_file(const char *path)
{
    uint32_t n = 0;
    uint8_t *d = slurp(path, &n);
    if (!d) return;
    if (!recognised(d, n)) {
        free(d);
        return;
    }
    for (int p = 0; p < ZVT_PRESET_COUNT; p++) {
        if (!want_pr[p]) continue;
        job_t ref;
        run_job(d, n, (zvt_preset_t) p, &bks[BK_CPU].be, g_opts, &ref);
        for (int b = 0; b < BK_N; b++) {
            if (!want_bk[b] || !bks[b].ok) continue;
            job_t jb, *j = &ref;
            if (b != BK_CPU) run_job(d, n, (zvt_preset_t) p, &bks[b].be, g_opts, &jb), j = &jb;
            int bx = -1;
            if (b != BK_CPU)
                bx = j->len > 0 && j->len == ref.len && !memcmp(j->out, ref.out, (size_t) j->len) &&
                     job_psnr(j) == job_psnr(&ref) && job_ssim(j) == job_ssim(&ref);
            report_json(jl, path, bks[b].name, (zvt_preset_t) p, j, bx);
            fflush(jl);
            tot_t *t = &tot[b][p];
            t->runs++;
            if (j->len > 0) {
                t->ok++;
                t->ms += (double) j->ns / 1e6;
                t->bytes += (uint64_t) j->len;
                int32_t ps = job_psnr(j);
                t->psnr_sum += ps > 9999 ? 9999 : ps;
                t->ssim_sum += job_ssim(j);
                t->exact += job_exact(j);
            } else {
                t->failed++, bench_fail = 1;
            }
            if (bx >= 0) t->compared++, t->bitexact += bx == 1;
            if (j != &ref) free(jb.out);
        }
        free(ref.out);
    }
    free(d);
}

static void walk_dir(const char *dir, int depth)
{
    if (depth > 8) return;
    DIR *dh = opendir(dir);
    if (!dh) return;
    struct dirent *e;
    /* sorted for a stable order: collect, then sort by name */
    char **names = 0;
    size_t nn = 0, cap = 0;
    while ((e = readdir(dh)) != 0) {
        if (e->d_name[0] == '.') continue;
        if (nn == cap) {
            cap = cap ? cap * 2 : 64;
            char **nx = realloc(names, cap * sizeof *names);
            if (!nx) break;
            names = nx;
        }
        size_t l = strlen(dir) + strlen(e->d_name) + 2;
        names[nn] = malloc(l);
        if (!names[nn]) break;
        snprintf(names[nn], l, "%s/%s", dir, e->d_name);
        nn++;
    }
    closedir(dh);
    for (size_t i = 1; i < nn; i++)
        for (size_t k = i; k > 0 && strcmp(names[k - 1], names[k]) > 0; k--) {
            char *t = names[k];
            names[k] = names[k - 1], names[k - 1] = t;
        }
    for (size_t i = 0; i < nn; i++) {
        struct stat st;
        if (stat(names[i], &st) == 0) {
            if (S_ISDIR(st.st_mode))
                walk_dir(names[i], depth + 1);
            else if (S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size < 0x7FFFFFFF)
                bench_file(names[i]);
        }
        free(names[i]);
    }
    free(names);
}

static void write_summary(const char *path, const char *dir)
{
    FILE *f = path ? fopen(path, "w") : 0;
    if (!f) return;
    fprintf(f, "{\"corpus\":");
    json_str(f, dir);
    fprintf(f, ",\"backends\":[");
    bool first = true;
    for (int b = 0; b < BK_N; b++) {
        if (!want_bk[b]) continue;
        fprintf(f, "%s{\"backend\":\"%s\",\"available\":%s,\"device\":", first ? "" : ",",
                bks[b].name, bks[b].ok ? "true" : "false");
        json_str(f, bks[b].ok ? device_name(b) : "");
        fprintf(f, ",\"why_not\":");
        json_str(f, bks[b].ok ? "" : bks[b].why);
        fprintf(f, "}");
        first = false;
    }
    fprintf(f, "],\"totals\":[");
    first = true;
    for (int b = 0; b < BK_N; b++)
        for (int p = 0; p < ZVT_PRESET_COUNT; p++) {
            const tot_t *t = &tot[b][p];
            if (!t->runs) continue;
            const tot_t *c = &tot[BK_CPU][p];
            fprintf(f,
                    "%s{\"backend\":\"%s\",\"preset\":\"%s\",\"runs\":%u,\"ok\":%u,\"failed\":%u,"
                    "\"ms_total\":%.3f,"
                    "\"bytes_total\":%llu,\"psnr_cdb_mean\":%.1f,\"ssim_x10000_mean\":%.1f,"
                    "\"exact\":%u",
                    first ? "" : ",", bks[b].name, zvt_preset_name((zvt_preset_t) p), t->runs,
                    t->ok, t->failed, t->ms, (unsigned long long) t->bytes,
                    t->ok ? t->psnr_sum / t->ok : 0.0, t->ok ? t->ssim_sum / t->ok : 0.0, t->exact);
            if (b != BK_CPU)
                fprintf(f, ",\"bit_exact_vs_cpu\":%u,\"compared\":%u,\"speedup_vs_cpu\":%.3f",
                        t->bitexact, t->compared, t->ms > 0 && c->ms > 0 ? c->ms / t->ms : 0.0);
            fprintf(f, "}");
            first = false;
        }
    fprintf(f, "]}\n");
    fclose(f);
}

static bool parse_list(const char *s, bool *want, int count, int (*lookup)(const char *))
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s", s);
    if (!strcmp(buf, "all")) {
        for (int i = 0; i < count; i++) want[i] = true;
        return true;
    }
    for (char *t = strtok(buf, ","); t; t = strtok(0, ",")) {
        int k = lookup(t);
        if (k < 0) return false;
        want[k] = true;
    }
    return true;
}

static int bk_of(const char *s)
{
    return !strcmp(s, "cpu")                          ? BK_CPU
           : !strcmp(s, "cuda")                       ? BK_CUDA
           : !strcmp(s, "vulkan") || !strcmp(s, "vk") ? BK_VK
                                                      : -1;
}

static int usage(void)
{
    fprintf(
        stderr,
        "usage: vtrace [--preset P] [--backend cpu|cuda|vulkan|auto] [--render F.ppm --scale N]\n"
        "              [--gop N] [--max-frames N] [--json] INPUT [-o OUT]\n"
        "       vtrace --bench DIR [--presets LIST|all] [--backends LIST|all] [--jsonl F] "
        "[--summary F]\n"
        "              [--gop N] [--max-frames N]\n"
        "  presets: pixel icon illustration photo\n");
    return 2;
}

int main(int argc, char **argv)
{
    const char *in = 0, *out = 0, *render = 0, *bench = 0, *jsonl = 0, *summary = 0;
    const char *backend = "cpu", *presets = "illustration", *backends = "all";
    int preset = ZVT_PRESET_ILLUSTRATION;
    uint32_t scale = 1;
    bool json = false;
    zvm_opts_t o;
    zvm_opts_default(&o);
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : 0;
        if (!strcmp(a, "--preset") && v) {
            if ((preset = preset_of(v)) < 0) return usage();
            i++;
        } else if (!strcmp(a, "--backend") && v)
            backend = v, i++;
        else if (!strcmp(a, "--render") && v)
            render = v, i++;
        else if (!strcmp(a, "--scale") && v)
            scale = (uint32_t) strtoul(v, 0, 10), i++;
        else if (!strcmp(a, "--gop") && v)
            o.gop = (uint32_t) strtoul(v, 0, 10), i++;
        else if (!strcmp(a, "--max-frames") && v)
            o.max_frames = (uint32_t) strtoul(v, 0, 10), i++;
        else if (!strcmp(a, "--json"))
            json = true;
        else if (!strcmp(a, "-o") && v)
            out = v, i++;
        else if (!strcmp(a, "--bench") && v)
            bench = v, i++;
        else if (!strcmp(a, "--presets") && v)
            presets = v, i++;
        else if (!strcmp(a, "--backends") && v)
            backends = v, i++;
        else if (!strcmp(a, "--jsonl") && v)
            jsonl = v, i++;
        else if (!strcmp(a, "--summary") && v)
            summary = v, i++;
        else if (a[0] != '-' && !in)
            in = a;
        else
            return usage();
    }
    if (scale < 1 || scale > 64) return usage();
    g_opts = &o;

    if (bench) {
        if (!parse_list(presets, want_pr, ZVT_PRESET_COUNT, preset_of) ||
            !parse_list(backends, want_bk, BK_N, bk_of))
            return usage();
        open_backends(want_bk[BK_CUDA], want_bk[BK_VK]);
        for (int b = 1; b < BK_N; b++)
            if (want_bk[b] && !bks[b].ok)
                fprintf(stderr, "vtrace: %s not available: %s\n", bks[b].name, bks[b].why);
        jl = jsonl ? fopen(jsonl, "w") : stdout;
        if (!jl) return usage();
        uint64_t t0 = now_ns();
        walk_dir(bench, 0);
        if (jl != stdout) fclose(jl);
        write_summary(summary, bench);
        fprintf(stderr, "vtrace: bench done in %.1f s\n", (double) (now_ns() - t0) / 1e9);
        close_backends();
        return bench_fail;
    }

    if (!in) return usage();
    int b = bk_of(backend);
    bool autob = !strcmp(backend, "auto");
    if (b < 0 && !autob) return usage();
    open_backends(autob || b == BK_CUDA, autob || b == BK_VK);
    if (autob) b = bks[BK_CUDA].ok ? BK_CUDA : bks[BK_VK].ok ? BK_VK : BK_CPU;
    if (!bks[b].ok) {
        fprintf(stderr, "vtrace: %s not available: %s\n", bks[b].name, bks[b].why);
        close_backends();
        return 1;
    }
    uint32_t n = 0;
    uint8_t *d = slurp(in, &n);
    if (!d) {
        fprintf(stderr, "vtrace: cannot read %s\n", in);
        close_backends();
        return 1;
    }
    job_t j;
    run_job(d, n, (zvt_preset_t) preset, &bks[b].be, &o, &j);
    if (j.len <= 0) {
        fprintf(stderr, "vtrace: %s: %s (%d)\n", in, j.err ? j.err : "failed", j.len);
        free(d), free(j.out);
        close_backends();
        return 1;
    }
    /* outputs */
    char base[4096];
    if (out) {
        snprintf(base, sizeof base, "%s", out);
    } else {
        snprintf(base, sizeof base, "%s", in);
        char *dot = strrchr(base, '.'), *sl = strrchr(base, '/');
        if (dot && (!sl || dot > sl)) *dot = 0;
    }
    size_t bl = strlen(base);
    const char *ext = j.video ? ".zxvm" : ".zxvg";
    if (bl > 5 && (!strcmp(base + bl - 5, ".zxvg") || !strcmp(base + bl - 5, ".zxvm")))
        base[bl - 5] = 0;
    char path[4200];
    int rc = 0;
    snprintf(path, sizeof path, "%s%s", base, ext);
    if (spit(path, j.out, (size_t) j.len))
        fprintf(stderr, "vtrace: cannot write %s\n", path), rc = 1;
    if (!j.video) {
        snprintf(path, sizeof path, "%s.zxvt", base);
        if (spit(path, j.rec, ZVT_REC_LEN))
            fprintf(stderr, "vtrace: cannot write %s\n", path), rc = 1;
    }
    if (render && render_ppm(&j, scale, render))
        fprintf(stderr, "vtrace: cannot render %s\n", render), rc = 1;
    if (json)
        report_json(stdout, in, bks[b].name, (zvt_preset_t) preset, &j, -1);
    else
        report_text(in, bks[b].name, (zvt_preset_t) preset, &j, n);
    free(d), free(j.out);
    close_backends();
    return rc;
}

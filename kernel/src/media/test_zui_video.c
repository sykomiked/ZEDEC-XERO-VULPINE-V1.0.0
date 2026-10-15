/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_video.c — Y4M, AVI, MP4/QuickTime, animated GIF and the
 * platform-decoder adapter (zui_video.h VD1..VD4).
 *
 * Fixtures: tools/media/make_fixtures.py (test/fx_video.inc). Y4M and raw
 * AVI frames must match the generator's integer BT.601 model exactly;
 * Motion-JPEG frames must match Pillow's libjpeg-turbo decode within 2 per
 * channel. Every container is cut at every length and has bytes flipped;
 * each copy is played to the end or to an error, never a crash. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_video.h"

typedef struct {
    const char *name;
    const uint8_t *d;
    uint32_t n, w, h;
    const uint32_t *ref;
    int tol, rc, fmt, flags;
} fx_t;

#include "test/fx_video.inc"
#include "test/fx_misc.inc" /* the animated GIF */

#ifndef FUZZ_ROUNDS
#    define FUZZ_ROUNDS 3 /* copies per cut length (1 cut + flipped); raise for a soak */
#endif

static int fails, checks, sec_checks, sec_fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        sec_checks++;                                                                              \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            sec_fails++;                                                                           \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static void section(const char *name)
{
    printf("%s %s (%d checks)\n", sec_fails ? "[FAIL]" : "[PASS]", name, sec_checks);
    sec_checks = sec_fails = 0;
}

#define WORK_BYTES (4u << 20)
static uint8_t WORK[WORK_BYTES];

static int maxdiff(const uint32_t *a, const uint32_t *b, uint32_t n)
{
    int m = 0;
    for (uint32_t i = 0; i < n; i++)
        for (int s = 0; s < 24; s += 8) {
            int d = (int) ((a[i] >> s) & 255) - (int) ((b[i] >> s) & 255);
            if (d < 0) d = -d;
            if (d > m) m = d;
        }
    return m;
}

static bool hasword(const char *s, const char *w)
{
    return s && strstr(s, w) != 0;
}

/* Play a file to the end; compare frames with refs (may be NULL). */
static void play(const char *name, const uint8_t *d, uint32_t n, uint32_t nf,
                 const uint32_t *const *ref, int tol, const uint64_t *pts, const uint64_t *dur)
{
    zui_video_t v;
    zui_video_frame_t f;
    int rc = zui_video_open(&v, d, n, WORK, WORK_BYTES, 0);
    CHECK(rc == 0, "%s opens (%d: %s)", name, rc, v.info.why);
    if (rc) return;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t k = 0;
        int worst = 0;
        bool times = true;
        while ((rc = zui_video_next_frame(&v, &f)) == 1) {
            if (ref && k < nf) {
                int m = maxdiff(f.px, ref[k], f.w * f.h);
                if (m > worst) worst = m;
            }
            if (pts && k < nf && (f.pts_us != pts[k] || f.dur_us != dur[k])) {
                times = false;
                printf("  %s frame %u: pts %llu dur %llu\n", name, k, (unsigned long long) f.pts_us,
                       (unsigned long long) f.dur_us);
            }
            if (f.index != k) times = false;
            k++;
        }
        CHECK(rc == 0 && k == nf, "%s pass %d: %u frames then the end (rc %d: %s)", name, pass, k,
              rc, v.info.why);
        if (ref)
            CHECK(worst <= tol, "%s pass %d: frames match the reference (max diff %d, allowed %d)",
                  name, pass, worst, tol);
        if (pts) CHECK(times, "%s pass %d: presentation times and durations", name, pass);
        zui_video_rewind(&v);
    }
}

static void test_y4m(void)
{
    zui_video_info_t i;
    CHECK(zui_video_probe(vfx_y4m_420, vfx_y4m_420_len, 0, &i) == 0 && i.container == ZUI_VID_Y4M &&
              i.codec == ZUI_VC_YUV && i.w == 16 && i.h == 12 && i.frames == 3 &&
              i.rate_num == 30000 && i.rate_den == 1001 && i.duration_us == 100100,
          "probe: Y4M 16x12, 3 frames at 30000/1001, %llu us", (unsigned long long) i.duration_us);
    static const uint64_t P30[3] = {0, 33366, 66733}, D30[3] = {33366, 33366, 33366};
    static const uint64_t P25[3] = {0, 40000, 80000}, D25[3] = {40000, 40000, 40000};
    play("y4m 4:2:0", vfx_y4m_420, vfx_y4m_420_len, 3, vfx_y4m_420_ref, 0, P30, D30);
    play("y4m 4:4:4", vfx_y4m_444, vfx_y4m_444_len, 3, vfx_y4m_444_ref, 0, P25, D25);
    play("y4m mono", vfx_y4m_mono, vfx_y4m_mono_len, 3, vfx_y4m_mono_ref, 0, P25, D25);
    play("y4m 4:2:2 odd width", vfx_y4m_422, vfx_y4m_422_len, 3, vfx_y4m_422_ref, 0, P25, D25);
    CHECK(zui_video_probe(vfx_y4m_10bit, vfx_y4m_10bit_len, 0, &i) == ZX_IMG_E_UNSUPPORTED &&
              hasword(i.why, "8-bit"),
          "10-bit Y4M is refused (%s)", i.why);
    /* a cut inside the last frame: two frames, then E_TRUNC */
    zui_video_t v;
    zui_video_frame_t f;
    uint32_t n = vfx_y4m_420_len - 7, k = 0;
    int rc;
    CHECK(zui_video_probe(vfx_y4m_420, n, 0, &i) == 0 && i.frames == 2,
          "a cut file reports its complete frames (%u)", i.frames);
    zui_video_open(&v, vfx_y4m_420, n, WORK, WORK_BYTES, 0);
    while ((rc = zui_video_next_frame(&v, &f)) == 1) k++;
    CHECK(k == 2 && rc == ZX_IMG_E_TRUNC, "VD3 cut Y4M plays 2 frames then E_TRUNC (%u, %d)", k,
          rc);
    section("Y4M");
}

static void test_avi(void)
{
    zui_video_info_t i;
    CHECK(zui_video_probe(vfx_avi_mjpeg, vfx_avi_mjpeg_len, 0, &i) == 0 &&
              i.container == ZUI_VID_AVI && i.codec == ZUI_VC_MJPEG && i.fourcc == 0x47504A4Du &&
              i.w == 32 && i.h == 24 && i.frames == 3 && i.rate_num == 25 && i.rate_den == 1 &&
              i.duration_us == 120000 && !i.hw,
          "probe: AVI Motion-JPEG 32x24, 3 frames at 25 fps");
    CHECK(i.work >= 32 * 24 * 4 + zui_jpeg_work_max(32, 24),
          "work covers the frame and the JPEG decoder (%u)", i.work);
    static const uint64_t P25[3] = {0, 40000, 80000}, D25[3] = {40000, 40000, 40000};
    static const uint64_t PN[3] = {0, 33366, 66733}, DN[3] = {33366, 33366, 33366};
    play("avi mjpeg (frame 1 without Huffman tables)", vfx_avi_mjpeg, vfx_avi_mjpeg_len, 3,
         vfx_mjpeg_ref, 2, P25, D25);
    play("avi mjpeg, no idx1, 29.97", vfx_avi_mjpeg_noidx, vfx_avi_mjpeg_noidx_len, 3,
         vfx_mjpeg_ref, 2, PN, DN);
    static const uint64_t P10[2] = {0, 100000}, D10[2] = {100000, 100000};
    play("avi raw 24-bit", vfx_avi_raw, vfx_avi_raw_len, 2, vfx_avi_raw_ref, 0, P10, D10);
    CHECK(zui_video_probe(vfx_avi_h264, vfx_avi_h264_len, 0, &i) == ZUI_VID_E_CODEC &&
              i.codec == ZUI_VC_H264 && hasword(i.why, "H.264"),
          "VD4 AVI H.264 without a platform decoder is refused (%s)", i.why);
    zui_video_t v;
    CHECK(zui_video_open(&v, vfx_avi_h264, vfx_avi_h264_len, WORK, WORK_BYTES, 0) ==
              ZUI_VID_E_CODEC,
          "and open refuses it too");
    CHECK(zui_video_probe(vfx_avi_mjpeg, vfx_avi_mjpeg_len, 0, &i) == 0 &&
              zui_video_open(&v, vfx_avi_mjpeg, vfx_avi_mjpeg_len, WORK, i.work - 1, 0) ==
                  ZX_IMG_E_SPACE,
          "one byte short of work is E_SPACE");
    section("AVI");
}

/* ---- a stand-in platform decoder ------------------------------------------ */
typedef struct {
    bool yes;
    int delay; /* pictures held back (B-frame style reordering) */
    int opened, closed, units, drains, outs;
    uint32_t sizes[16];
    uint64_t pts[16];
    uint8_t cfg0;
    uint32_t ncfg;
    zui_vid_codec_t codec;
} mock_t;

static bool m_supports(void *ctx, zui_vid_codec_t c, uint32_t w, uint32_t h)
{
    mock_t *m = ctx;
    return m->yes && (c == ZUI_VC_H264 || c == ZUI_VC_HEVC) && w <= 4096 && h <= 4096;
}
static int m_open(void *ctx, zui_vid_codec_t c, uint32_t w, uint32_t h, const uint8_t *cfg,
                  uint32_t n)
{
    mock_t *m = ctx;
    (void) w, (void) h;
    m->opened++;
    m->codec = c;
    m->ncfg = n;
    m->cfg0 = n ? cfg[0] : 0;
    return 0;
}
static int m_decode(void *ctx, const uint8_t *au, uint32_t n, uint64_t pts, uint32_t *px,
                    uint32_t stride, uint64_t *out)
{
    mock_t *m = ctx;
    if (!au) {
        m->drains++;
        if (m->outs >= m->units) return 0;
    } else {
        if (m->units < 16) m->sizes[m->units] = n, m->pts[m->units] = pts;
        m->units++;
        if (m->units - m->outs <= m->delay) return 0;
    }
    int k = m->outs++;
    for (uint32_t y = 0; y < 24; y++)
        for (uint32_t x = 0; x < 32; x++)
            px[y * stride + x] = 0xFF000000u | (uint32_t) (k * 0x111111);
    *out = m->pts[k < 16 ? k : 15];
    return 1;
}
static void m_close(void *ctx)
{
    ((mock_t *) ctx)->closed++;
}

static void test_mp4(void)
{
    zui_video_info_t i;
    CHECK(zui_video_probe(vfx_mov_mjpeg, vfx_mov_mjpeg_len, 0, &i) == 0 &&
              i.container == ZUI_VID_MP4 && i.codec == ZUI_VC_MJPEG && i.w == 32 && i.h == 24 &&
              i.frames == 3 && i.duration_us == 160000,
          "probe: QuickTime Motion-JPEG, 3 frames, %llu us", (unsigned long long) i.duration_us);
    static const uint64_t P[3] = {0, 40000, 120000}, D[3] = {40000, 80000, 40000};
    play("mov mjpeg (two chunks, variable durations)", vfx_mov_mjpeg, vfx_mov_mjpeg_len, 3,
         vfx_mjpeg_ref, 2, P, D);

    CHECK(zui_video_probe(vfx_mp4_avc1, vfx_mp4_avc1_len, 0, &i) == ZUI_VID_E_CODEC &&
              i.codec == ZUI_VC_H264 && i.container == ZUI_VID_MP4 && i.w == 32 && i.h == 24 &&
              i.frames == 4,
          "VD4 MP4 H.264 is identified (%ux%u, %u frames) and refused: %s", i.w, i.h, i.frames,
          i.why);
    CHECK(zui_video_probe(vfx_mp4_hev1, vfx_mp4_hev1_len, 0, &i) == ZUI_VID_E_CODEC &&
              i.codec == ZUI_VC_HEVC,
          "MP4 HEVC refused (%s)", i.why);
    CHECK(zui_video_probe(vfx_mp4_av01, vfx_mp4_av01_len, 0, &i) == ZUI_VID_E_CODEC &&
              i.codec == ZUI_VC_AV1 && hasword(i.why, "AV1"),
          "MP4 AV1 refused (%s)", i.why);
    CHECK(zui_video_probe(vfx_webm, vfx_webm_len, 0, &i) == ZX_IMG_E_UNSUPPORTED &&
              i.container == ZUI_VID_MKV,
          "WebM/Matroska is recognised and refused (%s)", i.why);

    /* with an adapter: units go through unchanged, with times and the avcC */
    for (int delay = 0; delay < 3; delay++) {
        mock_t m = {0};
        m.yes = true;
        m.delay = delay;
        zui_video_hw_t hw = {"mock", &m, m_supports, m_open, m_decode, m_close};
        zui_video_t v;
        zui_video_frame_t f;
        CHECK(zui_video_probe(vfx_mp4_avc1, vfx_mp4_avc1_len, &hw, &i) == 0 && i.hw,
              "delay %d: probe routes to the adapter", delay);
        CHECK(zui_video_open(&v, vfx_mp4_avc1, vfx_mp4_avc1_len, WORK, WORK_BYTES, &hw) == 0,
              "open");
        int k = 0, rc;
        bool ok = true;
        static const uint64_t P[4] = {0, 41666, 83333, 125000};
        while ((rc = zui_video_next_frame(&v, &f)) == 1) {
            ok &= f.pts_us == P[k] && f.px[0] == (0xFF000000u | (uint32_t) (k * 0x111111));
            k++;
        }
        CHECK(rc == 0 && k == 4 && ok, "delay %d: 4 pictures in order with their times (%d, rc %d)",
              delay, k, rc);
        bool sizes = m.units == 4;
        for (int u = 0; u < 4 && sizes; u++)
            sizes = m.sizes[u] == (uint32_t) (10 + u) && m.pts[u] == P[u];
        CHECK(sizes, "delay %d: each MP4 sample reached the adapter whole, with its time", delay);
        CHECK(m.opened == 1 && m.codec == ZUI_VC_H264 && m.ncfg == 19 && m.cfg0 == 1,
              "the adapter got the avcC record (%u bytes)", m.ncfg);
        CHECK(m.drains >= 1, "delay %d: end of stream drains the adapter", delay);
        zui_video_rewind(&v);
        CHECK(m.closed == 1, "rewind closes the adapter stream");
        zui_video_close(&v);
    }
    mock_t m = {0};
    zui_video_hw_t hw = {"mock", &m, m_supports, m_open, m_decode, m_close};
    CHECK(zui_video_probe(vfx_mp4_avc1, vfx_mp4_avc1_len, &hw, &i) == ZUI_VID_E_CODEC,
          "an adapter that says no is not used");
    m.yes = true;
    CHECK(zui_video_probe(vfx_mp4_av01, vfx_mp4_av01_len, &hw, &i) == ZUI_VID_E_CODEC,
          "nor for a codec it does not list");
    CHECK(zui_video_probe(vfx_avi_h264, vfx_avi_h264_len, &hw, &i) == 0 && i.hw,
          "AVI H.264 goes to the adapter too");
    section("MP4 / QuickTime and the adapter");
}

static void test_gif(void)
{
    zui_video_info_t i;
    const uint8_t *d = 0;
    uint32_t n = 0;
    for (int k = 0; k < FX_MISC_N; k++)
        if (!strcmp(FX_MISC[k].name, "gif_anim")) d = FX_MISC[k].d, n = FX_MISC[k].n;
    CHECK(zui_video_probe(d, n, 0, &i) == 0 && i.container == ZUI_VID_GIF &&
              i.frames == FX_GIF_NFRAMES,
          "probe: animated GIF, %u frames", i.frames);
    uint64_t pts[FX_GIF_NFRAMES], dur[FX_GIF_NFRAMES], t = 0;
    for (int k = 0; k < FX_GIF_NFRAMES; k++) {
        uint32_t cs = FX_GIF_DELAYS[k] < 2 ? 10 : FX_GIF_DELAYS[k];
        pts[k] = t, dur[k] = cs * 10000ull;
        t += dur[k];
    }
    play("animated gif", d, n, FX_GIF_NFRAMES, FX_GIF_FRAMES, 0, pts, dur);
    section("animated GIF");
}

#ifdef FX_HAVE_FF
static void test_ffmpeg(void)
{
    zui_video_info_t i;
    CHECK(zui_video_probe(vfx_ff_avi_mjpeg, vfx_ff_avi_mjpeg_len, 0, &i) == 0 &&
              i.codec == ZUI_VC_MJPEG && i.w == 32 && i.h == 24 && i.frames == 3,
          "ffmpeg's AVI muxer and MJPEG encoder: probe");
    play("ffmpeg avi mjpeg", vfx_ff_avi_mjpeg, vfx_ff_avi_mjpeg_len, 3, 0, 0, 0, 0);
#    ifdef FX_HAVE_FF_H264
    CHECK(zui_video_probe(vfx_ff_mp4_h264, vfx_ff_mp4_h264_len, 0, &i) == ZUI_VID_E_CODEC &&
              i.codec == ZUI_VC_H264 && i.w == 32 && i.h == 24 && i.frames == 3,
          "a real x264 MP4 is identified and refused (%s)", i.why);
    mock_t m = {0};
    m.yes = true;
    zui_video_hw_t hw = {"mock", &m, m_supports, m_open, m_decode, m_close};
    zui_video_t v;
    zui_video_frame_t f;
    int k = 0;
    zui_video_open(&v, vfx_ff_mp4_h264, vfx_ff_mp4_h264_len, WORK, WORK_BYTES, &hw);
    while (zui_video_next_frame(&v, &f) == 1) k++;
    CHECK(k == 3 && m.units == 3 && m.ncfg > 7 && m.cfg0 == 1,
          "its samples and avcC reach an adapter (%d, %d)", k, m.units);
#    endif
    section("ffmpeg-made files");
}
#endif

/* VD3: cut every container at every length, flip bytes, play to the end */
static void test_fuzz(void)
{
    struct {
        const uint8_t *d;
        uint32_t n;
    } F[] = {{vfx_y4m_420, vfx_y4m_420_len},     {vfx_y4m_422, vfx_y4m_422_len},
             {vfx_avi_mjpeg, vfx_avi_mjpeg_len}, {vfx_avi_raw, vfx_avi_raw_len},
             {vfx_mov_mjpeg, vfx_mov_mjpeg_len}, {vfx_mp4_avc1, vfx_mp4_avc1_len},
             {vfx_avi_h264, vfx_avi_h264_len}};
    mock_t m = {0};
    m.yes = true;
    zui_video_hw_t hw = {"mock", &m, m_supports, m_open, m_decode, m_close};
    uint32_t cuts = 0, flips = 0, odd = 0, rng = 99;
    for (unsigned k = 0; k < sizeof F / sizeof F[0]; k++) {
        for (uint32_t L = 0; L <= F[k].n; L++) {
            for (int r = 0; r < FUZZ_ROUNDS; r++) {
                uint8_t *cp = malloc(L ? L : 1);
                memcpy(cp, F[k].d, L);
                if (r && L) {
                    for (int j = 0; j < 1 + r % 4; j++) {
                        rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
                        cp[rng % L] ^= (uint8_t) (1 + (rng >> 8) % 255);
                    }
                    flips++;
                } else if (r) {
                    free(cp);
                    continue;
                } else {
                    cuts++;
                }
                zui_video_t v;
                zui_video_frame_t f;
                memset(&m, 0, sizeof m);
                m.yes = true;
                if (zui_video_open(&v, cp, L, WORK, WORK_BYTES, &hw) == 0) {
                    int rc, frames = 0;
                    while ((rc = zui_video_next_frame(&v, &f)) == 1 && frames < 1000) frames++;
                    if (rc > 0 || rc < ZUI_VID_E_CODEC || frames >= 1000) odd++;
                    zui_video_close(&v);
                }
                free(cp);
            }
        }
    }
    CHECK(odd == 0, "VD3 %u cut and %u flipped containers play to an end or a clean error (%u odd)",
          cuts, flips, odd);
    zui_video_info_t i;
    CHECK(zui_video_probe(0, 0, 0, &i) == ZX_IMG_E_ARG &&
              zui_video_probe(vfx_y4m_420, 3, 0, &i) == ZX_IMG_E_FORMAT,
          "null and tiny inputs");
    CHECK(!strcmp(zui_video_codec_name(ZUI_VC_HEVC), "H.265/HEVC"), "codec names");
    section("truncation and corruption");
}

int main(void)
{
    test_y4m();
    test_avi();
    test_mp4();
    test_gif();
#ifdef FX_HAVE_FF
    test_ffmpeg();
#endif
    test_fuzz();
    printf("%s test_zui_video: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_capture.c — screenshots and recording (zui_capture.h CP1..CP6).
 * Every PNG is read back by a small decoder written here (chunks, CRC-32,
 * zlib header, stored and fixed-Huffman inflate, Adler-32) and compared
 * pixel for pixel with the surface; recordings are parsed and replayed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_capture.h"
#include "zui_tokens.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define W 640
#define H 400

static uint32_t PX[W * H], SCR[W * H];
static zui_surface_t S, SCRATCH;
static zui_capture_t CAP;
static zui_wm_t WM;

/* ---- a sink for the bytes ------------------------------------------------ */

#define SINK_MAX (8u * 1024u * 1024u)
static uint8_t SINK[SINK_MAX];
static uint32_t nsink, sink_calls, fail_after = 0xFFFFFFFFu;

static bool sink(void *ctx, const uint8_t *p, uint32_t n)
{
    (void) ctx;
    sink_calls++;
    if (nsink + n > fail_after || nsink + n > SINK_MAX) return false;
    memcpy(SINK + nsink, p, n);
    nsink += n;
    return true;
}

static void sink_reset(void)
{
    nsink = sink_calls = 0;
    fail_after = 0xFFFFFFFFu;
}

/* ---- an independent reader ---------------------------------------------- */

static uint32_t ref_crc(const uint8_t *p, uint32_t n) /* bit at a time */
{
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t ref_adler(const uint8_t *p, uint32_t n)
{
    uint32_t a = 1, b = 0;
    for (uint32_t i = 0; i < n; i++) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static uint32_t le(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

typedef struct {
    const uint8_t *p;
    uint32_t n, at, bit; /* at: byte, bit: 0..7 */
    bool bad;
} bits_t;

static uint32_t getbit(bits_t *b)
{
    if (b->at >= b->n) {
        b->bad = true;
        return 0;
    }
    uint32_t v = (b->p[b->at] >> b->bit) & 1u;
    if (++b->bit == 8) {
        b->bit = 0;
        b->at++;
    }
    return v;
}

static uint32_t getbits(bits_t *b, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) v |= getbit(b) << i;
    return v;
}

static int fixed_sym(bits_t *b)
{
    uint32_t code = 0;
    for (uint32_t len = 1; len <= 9 && !b->bad; len++) {
        code = (code << 1) | getbit(b);
        if (len == 7 && code <= 0x17u) return (int) (256u + code);
        if (len == 8 && code >= 0x30u && code <= 0xBFu) return (int) (code - 0x30u);
        if (len == 8 && code >= 0xC0u && code <= 0xC7u) return (int) (280u + code - 0xC0u);
        if (len == 9 && code >= 0x190u && code <= 0x1FFu) return (int) (144u + code - 0x190u);
    }
    return -1;
}

static const uint16_t LB[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LX[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DB[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DX[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static uint8_t RAW[2u * 1024u * 1024u], ZDATA[4u * 1024u * 1024u];

typedef struct {
    int32_t w, h;
    uint32_t idats, stored_blocks, fixed_blocks, matches;
    bool ok;
    const char *why;
} png_info_t;

/* Inflate stored and fixed blocks into RAW; returns bytes or -1. */
static int64_t inflate(const uint8_t *z, uint32_t n, png_info_t *in)
{
    bits_t b = {z, n, 0, 0, false};
    uint32_t o = 0;
    for (;;) {
        uint32_t fin = getbit(&b), type = getbits(&b, 2);
        if (type == 0) {
            if (b.bit) {
                b.bit = 0;
                b.at++;
            }
            if (b.at + 4 > n) return -1;
            uint32_t len = z[b.at] | (uint32_t) z[b.at + 1] << 8;
            uint32_t nlen = z[b.at + 2] | (uint32_t) z[b.at + 3] << 8;
            if ((len ^ 0xFFFFu) != nlen) return -1;
            b.at += 4;
            if (b.at + len > n || o + len > sizeof RAW) return -1;
            memcpy(RAW + o, z + b.at, len);
            o += len;
            b.at += len;
            in->stored_blocks++;
        } else if (type == 1) {
            in->fixed_blocks++;
            for (;;) {
                int sym = fixed_sym(&b);
                if (sym < 0 || b.bad) return -1;
                if (sym < 256) {
                    if (o >= sizeof RAW) return -1;
                    RAW[o++] = (uint8_t) sym;
                    continue;
                }
                if (sym == 256) break;
                uint32_t li = (uint32_t) sym - 257u;
                if (li > 28) return -1;
                uint32_t len = LB[li] + getbits(&b, LX[li]);
                uint32_t dc = 0;
                for (int k = 0; k < 5; k++) dc = (dc << 1) | getbit(&b);
                if (dc > 29) return -1;
                uint32_t dist = DB[dc] + getbits(&b, DX[dc]);
                if (dist > o || o + len > sizeof RAW) return -1;
                for (uint32_t k = 0; k < len; k++, o++) RAW[o] = RAW[o - dist];
                in->matches++;
            }
        } else {
            return -1; /* the encoder never writes dynamic blocks */
        }
        if (b.bad) return -1;
        if (fin) break;
    }
    if (b.bit) b.at++;
    if (b.at + 4 != n) return -1; /* then the Adler-32, then nothing */
    if (rd32(z + b.at) != ref_adler(RAW, o)) {
        in->why = "adler";
        return -1;
    }
    return o;
}

/* Parse a PNG; on success the pixels are in OUTPX (w x h, XRGB). */
static uint32_t OUTPX[W * H * 2];

static png_info_t read_png(const uint8_t *p, uint32_t n)
{
    png_info_t in;
    memset(&in, 0, sizeof in);
    in.why = "header";
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n < 8 || memcmp(p, SIG, 8)) return in;
    uint32_t at = 8, nz = 0;
    bool ihdr = false, iend = false;
    while (at + 12 <= n && !iend) {
        uint32_t len = rd32(p + at);
        const uint8_t *type = p + at + 4;
        if (at + 12 + len > n) return in;
        if (rd32(p + at + 8 + len) != ref_crc(type, len + 4)) {
            in.why = "chunk crc";
            return in;
        }
        if (!memcmp(type, "IHDR", 4)) {
            if (len != 13 || ihdr) return in;
            in.w = (int32_t) rd32(type + 4);
            in.h = (int32_t) rd32(type + 8);
            if (type[12] != 8 || type[13] != 2 || type[14] || type[15] || type[16]) return in;
            ihdr = true;
        } else if (!memcmp(type, "IDAT", 4)) {
            if (!ihdr || nz + len > sizeof ZDATA) return in;
            memcpy(ZDATA + nz, type + 4, len);
            nz += len;
            in.idats++;
        } else if (!memcmp(type, "IEND", 4)) {
            iend = true;
        }
        at += 12 + len;
    }
    if (!iend || at != n || nz < 6) {
        in.why = "structure";
        return in;
    }
    if (ZDATA[0] != 0x78 || ((ZDATA[0] << 8) | ZDATA[1]) % 31 || (ZDATA[1] & 0x20)) {
        in.why = "zlib header";
        return in;
    }
    in.why = "inflate";
    int64_t raw = inflate(ZDATA + 2, nz - 2, &in);
    if (raw < 0) return in;
    uint32_t rowlen = 1u + 3u * (uint32_t) in.w;
    if ((uint64_t) raw != (uint64_t) rowlen * (uint32_t) in.h) {
        in.why = "size";
        return in;
    }
    if ((uint64_t) in.w * (uint64_t) in.h > sizeof OUTPX / 4) return in;
    for (int32_t y = 0; y < in.h; y++) {
        const uint8_t *r = RAW + (uint32_t) y * rowlen;
        if (r[0] != 0) {
            in.why = "filter";
            return in;
        }
        for (int32_t x = 0; x < in.w; x++)
            OUTPX[y * in.w + x] =
                (uint32_t) r[1 + 3 * x] << 16 | (uint32_t) r[2 + 3 * x] << 8 | r[3 + 3 * x];
    }
    in.ok = true;
    in.why = "";
    return in;
}

static uint32_t out_at(const png_info_t *in, int32_t x, int32_t y)
{
    return OUTPX[y * in->w + x];
}

/* The picture matches `s` over r (black outside the surface), except where
 * `blackout` says it should be black. */
static uint32_t mismatches(const png_info_t *in, const zui_surface_t *s, zui_rect_t r,
                           bool (*blackout)(int32_t x, int32_t y))
{
    uint32_t bad = 0;
    for (int32_t y = 0; y < r.h; y++)
        for (int32_t x = 0; x < r.w; x++) {
            int32_t sx = r.x + x, sy = r.y + y;
            uint32_t want = 0;
            if (sx >= 0 && sy >= 0 && sx < s->w && sy < s->h)
                want = s->px[sy * s->stride + sx] & 0xFFFFFFu;
            if (blackout && blackout(sx, sy)) want = 0;
            bad += out_at(in, x, y) != want;
        }
    return bad;
}

/* ---- pictures to take ---------------------------------------------------- */

static uint32_t seed = 12345u;
static uint32_t rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

static void paint_scene(void)
{
    zui_unclip(&S);
    zui_fill(&S, (zui_rect_t){0, 0, W, H}, ZUI_VOID);
    for (int32_t y = 0; y < 80; y++) /* a gradient band */
        for (int32_t x = 0; x < W; x++)
            PX[y * W + x] = (uint32_t) (x & 255) << 16 | (uint32_t) (y * 3) << 8 | 0x40u;
    zui_fill_rrect(&S, (zui_rect_t){40, 120, 200, 120}, ZUI_R_PANEL, ZUI_PANEL, 255);
    zui_engrave(&S, (zui_rect_t){40, 120, 200, 120}, ZUI_R_PANEL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI,
                90u);
    for (int i = 0; i < 400; i++) /* noise: literals */
        PX[(300 + (int32_t) (rnd() % 90u)) * W + 300 + (int32_t) (rnd() % 300u)] =
            rnd() | 0xFF000000u;
    PX[0] = 0xAB123456u; /* the X byte must not leak */
}

static void test_checksums(void)
{
    const uint8_t *nine = (const uint8_t *) "123456789";
    CHECK(zui_crc32(0, nine, 9) == 0xCBF43926u, "CRC-32 check value, got %08x",
          zui_crc32(0, nine, 9));
    CHECK(zui_crc32(0, nine, 0) == 0, "CRC-32 of nothing");
    CHECK(zui_crc32(zui_crc32(0, nine, 4), nine + 4, 5) == 0xCBF43926u, "CRC-32 continues");
    const uint8_t *wiki = (const uint8_t *) "Wikipedia";
    CHECK(zui_adler32(1, wiki, 9) == 0x11E60398u, "Adler-32 check value, got %08x",
          zui_adler32(1, wiki, 9));
    CHECK(zui_adler32(1, wiki, 0) == 1u, "Adler-32 of nothing");
    CHECK(zui_adler32(zui_adler32(1, wiki, 3), wiki + 3, 6) == 0x11E60398u, "Adler-32 continues");
    static uint8_t big[100000];
    for (uint32_t i = 0; i < sizeof big; i++) big[i] = (uint8_t) (0xFFu - (i % 7u));
    CHECK(zui_adler32(1, big, sizeof big) == ref_adler(big, sizeof big),
          "Adler-32 over the overflow batch");
    CHECK(zui_crc32(0, big, sizeof big) == ref_crc(big, sizeof big), "CRC-32 long run");
    uint8_t iend[4] = {'I', 'E', 'N', 'D'};
    CHECK(zui_crc32(0, iend, 4) == 0xAE426082u, "the IEND chunk's CRC");
}

static void test_png(void)
{
    paint_scene();
    zui_capture_init(&CAP, 0);
    png_info_t in;
    uint32_t fixed_len = 0, stored_len = 0;
    for (int m = 0; m < 2; m++) {
        CAP.mode = m ? ZUI_PNG_STORED : ZUI_PNG_FIXED;
        sink_reset();
        CHECK(zui_capture_surface(&CAP, &S, sink, 0) == ZUI_CAP_OK, "surface %d", m);
        in = read_png(SINK, nsink);
        CHECK(in.ok, "mode %d decodes (%s)", m, in.why);
        CHECK(in.w == W && in.h == H, "mode %d size %dx%d", m, in.w, in.h);
        CHECK(in.ok && mismatches(&in, &S, (zui_rect_t){0, 0, W, H}, 0) == 0,
              "mode %d pixels match", m);
        CHECK(in.ok && out_at(&in, 0, 0) == 0x123456u, "the X byte is dropped");
        if (m == 0) {
            fixed_len = nsink;
            CHECK(in.fixed_blocks == 1 && in.stored_blocks == 0 && in.matches > 100,
                  "one fixed block with matches (%u)", in.matches);
        } else {
            stored_len = nsink;
            CHECK(in.stored_blocks == (uint32_t) H && in.fixed_blocks == 0,
                  "a stored block per row");
        }
        CHECK(in.idats >= 2, "streamed in several IDAT chunks (%u)", in.idats);
    }
    CHECK(fixed_len * 4u < stored_len, "matches pay: %u vs %u", fixed_len, stored_len);
    uint32_t zlen = 2u + (uint32_t) H * (5u + 1u + 3u * W) + 4u;
    uint32_t want_stored =
        8u + 25u + 12u + zlen + 12u * ((zlen + ZUI_CAP_IDAT - 1u) / ZUI_CAP_IDAT);
    CHECK(stored_len == want_stored, "stored size is exact: %u vs %u", stored_len, want_stored);

    /* the reader is strict: a flipped bit anywhere is caught */
    sink_reset();
    CAP.mode = ZUI_PNG_FIXED;
    zui_capture_rect(&CAP, &S, (zui_rect_t){0, 0, 64, 64}, sink, 0);
    CHECK(read_png(SINK, nsink).ok, "a small picture reads");
    if (getenv("ZUI_CAPTURE_PNG")) { /* for a look with other tools */
        FILE *f = fopen(getenv("ZUI_CAPTURE_PNG"), "wb");
        if (f) {
            fwrite(SINK, 1, nsink, f);
            fclose(f);
        }
    }
    uint32_t bad = 0;
    for (uint32_t i = 8; i < nsink; i += 7) {
        SINK[i] ^= 0x10u;
        bad += !read_png(SINK, nsink).ok;
        SINK[i] ^= 0x10u;
    }
    CHECK(bad == (nsink - 8u + 6u) / 7u, "every corruption is refused (%u)", bad);
    /* CP6: the same surface gives the same bytes */
    CAP.mode = ZUI_PNG_FIXED;
    sink_reset();
    zui_capture_surface(&CAP, &S, sink, 0);
    static uint8_t first[SINK_MAX / 4];
    uint32_t n1 = nsink;
    memcpy(first, SINK, n1 < sizeof first ? n1 : sizeof first);
    sink_reset();
    zui_capture_surface(&CAP, &S, sink, 0);
    CHECK(n1 == nsink && !memcmp(first, SINK, nsink), "deterministic bytes");
    CHECK(zui_png_write(&CAP, &S, (zui_rect_t){0, 0, W, H}, 0, 0) == ZUI_CAP_OK &&
              CAP.png.bytes == n1,
          "a NULL writer counts the same bytes");

    /* sizes and edges */
    static const zui_rect_t RS[] = {{0, 0, 1, 1},      {5, 5, 3, 2},     {W - 2, H - 2, 6, 5},
                                    {-10, -4, 30, 20}, {100, 0, 259, 1}, {0, 300, W, 90},
                                    {300, 300, 1, 90}, {1, 1, 87, 61},   {W, H, 4, 4}};
    for (uint32_t k = 0; k < sizeof RS / sizeof RS[0]; k++)
        for (int m = 0; m < 2; m++) {
            CAP.mode = m ? ZUI_PNG_STORED : ZUI_PNG_FIXED;
            sink_reset();
            int rc = zui_capture_rect(&CAP, &S, RS[k], sink, 0);
            in = read_png(SINK, nsink);
            CHECK(rc == ZUI_CAP_OK && in.ok && in.w == RS[k].w && in.h == RS[k].h,
                  "rect %u mode %d (%s)", k, m, in.why);
            CHECK(in.ok && mismatches(&in, &S, RS[k], 0) == 0, "rect %u mode %d pixels", k, m);
        }
    /* a run longer than 258 and a row of one colour */
    zui_fill(&S, (zui_rect_t){0, 200, W, 3}, 0x0A0B0Cu);
    CAP.mode = ZUI_PNG_FIXED;
    sink_reset();
    zui_capture_rect(&CAP, &S, (zui_rect_t){0, 200, W, 3}, sink, 0);
    in = read_png(SINK, nsink);
    CHECK(in.ok && mismatches(&in, &S, (zui_rect_t){0, 200, W, 3}, 0) == 0 && nsink < 200,
          "flat rows compress to almost nothing (%u bytes)", nsink);

    /* errors */
    sink_reset();
    CHECK(zui_capture_rect(&CAP, &S, (zui_rect_t){0, 0, 0, 5}, sink, 0) == ZUI_CAP_E_ARG &&
              nsink == 0,
          "an empty rect is refused before writing");
    CHECK(zui_capture_rect(&CAP, &S, (zui_rect_t){0, 0, 5, -1}, sink, 0) == ZUI_CAP_E_ARG,
          "negative height");
    CHECK(zui_capture_rect(&CAP, &S, (zui_rect_t){0, 0, ZUI_CAP_MAX_W + 1, 1}, sink, 0) ==
              ZUI_CAP_E_SIZE,
          "too wide");
    CHECK(zui_capture_rect(&CAP, &S, (zui_rect_t){0, 0, ZUI_CAP_MAX_W, 1}, 0, 0) == ZUI_CAP_OK,
          "the widest row");
    sink_reset();
    fail_after = 100;
    uint32_t shots = CAP.shots;
    CHECK(zui_capture_surface(&CAP, &S, sink, 0) == ZUI_CAP_E_WRITE, "a refusing writer");
    uint32_t calls = sink_calls;
    CHECK(calls < 10 && CAP.shots == shots, "it stops writing at once (%u calls)", calls);
}

/* ---- windows and privacy ---------------------------------------------- */

typedef struct {
    uint32_t rgb;
    int paints;
} app_t;
static app_t A1 = {0x204060u, 0}, A2 = {0x406020u, 0}, A3 = {0x602040u, 0};

static void a_paint(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t tick)
{
    (void) tick;
    app_t *a = ctx;
    a->paints++;
    zui_fill(s, c, a->rgb);
    zui_fill(s, (zui_rect_t){c.x + 4, c.y + 4, 8, 8}, 0xFFFFFFu); /* a corner mark */
}

static uint32_t open_app(app_t *a, const char *name)
{
    zui_wm_client_t cl = {a_paint, 0, 0, a};
    uint32_t id = zui_wm_open(&WM, name, name, 0, &cl);
    zui_wm_progress(&WM, id, 256);
    zui_wm_tick(&WM, ZUI_WM_OPEN_MS);
    return id;
}

static int uses_on[2], uses_off[2], in_use_now;
static void on_use(void *ctx, zui_cap_use_t u, bool on)
{
    (void) ctx;
    if (on)
        uses_on[u]++;
    else
        uses_off[u]++;
    in_use_now += on ? 1 : -1;
}

static zui_rect_t BL[4];
static uint32_t nbl;
static bool in_bl(int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < nbl; i++)
        if (x >= BL[i].x && y >= BL[i].y && x < BL[i].x + BL[i].w && y < BL[i].y + BL[i].h)
            return true;
    return false;
}

static zui_rect_t CV[4];
static uint32_t ncv;
/* black where a private rect is, unless a window above covers it */
static bool private_visible(int32_t x, int32_t y)
{
    if (!in_bl(x, y)) return false;
    for (uint32_t i = 0; i < ncv; i++)
        if (x >= CV[i].x && y >= CV[i].y && x < CV[i].x + CV[i].w && y < CV[i].y + CV[i].h)
            return false;
    return true;
}

static void draw(void)
{
    zui_wm_draw(&WM, &S, 0);
}

static void test_windows(void)
{
    zui_wm_init(&WM, W, H, false);
    zui_capture_init(&CAP, &WM);
    CAP.in_use = on_use;
    uint32_t a = open_app(&A1, "Alpha"), b = open_app(&A2, "Beta");
    zui_wm_move(&WM, a, (zui_rect_t){20, 20, 360, 240});
    zui_wm_move(&WM, b, (zui_rect_t){200, 100, 360, 240}); /* over a's lower right */
    draw();
    zui_wm_window_t *wa = zui_wm_get(&WM, a), *wb = zui_wm_get(&WM, b);
    zui_rect_t ca = zui_wm_content(&WM, wa), cb = zui_wm_content(&WM, wb);
    png_info_t in;

    sink_reset();
    CHECK(zui_capture_window(&CAP, &S, a, false, 0, 0, sink, 0) == ZUI_CAP_OK, "window frame");
    in = read_png(SINK, nsink);
    CHECK(in.ok && in.w == wa->r.w && in.h == wa->r.h, "frame size %dx%d", in.w, in.h);
    CHECK(in.ok && mismatches(&in, &S, wa->r, 0) == 0, "frame pixels are the screen's");
    CHECK(uses_on[ZUI_CAP_SCREENSHOT] == 1 && uses_off[ZUI_CAP_SCREENSHOT] == 1 && in_use_now == 0,
          "CP3: a screenshot reports on then off");
    CHECK(CAP.shots == 1, "one shot counted");

    sink_reset();
    CHECK(zui_capture_window(&CAP, &S, b, true, 0, 0, sink, 0) == ZUI_CAP_OK, "content");
    in = read_png(SINK, nsink);
    CHECK(in.ok && in.w == cb.w && in.h == cb.h, "content size");
    CHECK(in.ok && out_at(&in, 20, 20) == A2.rgb && out_at(&in, 5, 5) == 0xFFFFFFu,
          "content is the app's paint");
    zui_rect_t r = zui_capture_window_rect(&CAP, b, true);
    CHECK(r.x == cb.x && r.y == cb.y && r.w == cb.w && r.h == cb.h, "window_rect content");
    r = zui_capture_window_rect(&CAP, a, false);
    CHECK(r.x == wa->r.x && r.w == wa->r.w, "window_rect frame");

    /* the covered window, painted off screen, comes out whole */
    zui_surface_init(&SCRATCH, SCR, W, H, W);
    sink_reset();
    int paints = A1.paints;
    CHECK(zui_capture_window(&CAP, &S, a, true, &SCRATCH, 7, sink, 0) == ZUI_CAP_OK, "offscreen");
    in = read_png(SINK, nsink);
    CHECK(A1.paints == paints + 1, "the client painted for the capture");
    CHECK(in.ok && in.w == ca.w && out_at(&in, ca.w - 3, ca.h - 3) == A1.rgb,
          "the part under Beta is Alpha's own");
    CHECK(in.ok && out_at(&in, 6, 6) == 0xFFFFFFu, "painted at the content origin");
    zui_surface_t tiny;
    zui_surface_init(&tiny, SCR, 10, 10, 10);
    CHECK(zui_capture_window(&CAP, &S, a, true, &tiny, 0, sink, 0) == ZUI_CAP_E_ARG,
          "scratch too small");

    /* CP2: a private window is black where it shows, not under the window above */
    CHECK(zui_capture_set_private(&CAP, a, true) && zui_capture_is_private(&CAP, a),
          "mark private");
    CHECK(zui_capture_set_private(&CAP, a, true) && CAP.npriv == 1, "marking twice keeps one");
    CHECK(!zui_capture_set_private(&CAP, 0, true), "id 0 is no window");
    sink_reset();
    zui_capture_surface(&CAP, &S, sink, 0);
    in = read_png(SINK, nsink);
    nbl = 1;
    BL[0] = wa->r;
    ncv = 1;
    CV[0] = wb->r;
    CHECK(in.ok && mismatches(&in, &S, (zui_rect_t){0, 0, W, H}, private_visible) == 0,
          "private window blacked, Beta above it kept");
    CHECK(in.ok && out_at(&in, ca.x + 30, ca.y + 30) == 0 &&
              out_at(&in, cb.x + 30, cb.y + 30) == A2.rgb,
          "black over Alpha, Beta as drawn");
    sink_reset();
    zui_capture_window(&CAP, &S, a, true, 0, 0, sink, 0);
    in = read_png(SINK, nsink);
    CHECK(in.ok && out_at(&in, 10, 10) == 0 && out_at(&in, ca.w - 2, ca.h - 2) == A2.rgb,
          "its own capture is black except where Beta covers it");
    sink_reset();
    zui_capture_window(&CAP, &S, a, true, &SCRATCH, 0, sink, 0);
    in = read_png(SINK, nsink);
    uint32_t lit = 0;
    for (int32_t i = 0; in.ok && i < in.w * in.h; i++) lit += OUTPX[i] != 0;
    CHECK(in.ok && lit == 0, "off screen it is all black (%u lit)", lit);

    /* raise it: now it covers Beta, and all of it is black */
    zui_wm_focus(&WM, a);
    draw();
    sink_reset();
    zui_capture_surface(&CAP, &S, sink, 0);
    in = read_png(SINK, nsink);
    ncv = 0;
    CHECK(in.ok && mismatches(&in, &S, (zui_rect_t){0, 0, W, H}, private_visible) == 0,
          "raised private window all black");
    CHECK(in.ok && out_at(&in, cb.x + cb.w - 5, cb.y + cb.h - 5) == A2.rgb,
          "Beta outside it still shows");

    /* minimised: not on screen, nothing black; capture by id is refused */
    zui_wm_minimise(&WM, a);
    draw();
    sink_reset();
    zui_capture_surface(&CAP, &S, sink, 0);
    in = read_png(SINK, nsink);
    CHECK(in.ok && mismatches(&in, &S, (zui_rect_t){0, 0, W, H}, 0) == 0,
          "a minimised private window hides nothing");
    CHECK(zui_capture_window(&CAP, &S, a, false, 0, 0, sink, 0) == ZUI_CAP_E_GONE,
          "minimised frame is gone");
    CHECK(zui_capture_window_rect(&CAP, a, false).w == 0, "and has no rect");
    CHECK(zui_capture_window(&CAP, &S, 999, false, 0, 0, sink, 0) == ZUI_CAP_E_GONE,
          "no such window");
    zui_wm_focus(&WM, a);
    CHECK(zui_capture_set_private(&CAP, a, false) && !zui_capture_is_private(&CAP, a), "unmark");

    /* a password field: a private rect relative to Beta's content moves with it */
    zui_wm_focus(&WM, b);
    draw();
    int32_t h1 = zui_capture_private_rect(&CAP, b, (zui_rect_t){40, 50, 120, 24});
    CHECK(h1 >= 0, "private rect handle");
    sink_reset();
    zui_capture_surface(&CAP, &S, sink, 0);
    in = read_png(SINK, nsink);
    nbl = 1;
    BL[0] = (zui_rect_t){cb.x + 40, cb.y + 50, 120, 24};
    ncv = 0;
    CHECK(in.ok && mismatches(&in, &S, (zui_rect_t){0, 0, W, H}, private_visible) == 0,
          "the field is black");
    zui_wm_move(&WM, b, (zui_rect_t){60, 60, 360, 240});
    draw();
    cb = zui_wm_content(&WM, wb);
    sink_reset();
    zui_capture_surface(&CAP, &S, sink, 0);
    in = read_png(SINK, nsink);
    BL[0] = (zui_rect_t){cb.x + 40, cb.y + 50, 120, 24};
    CHECK(in.ok && mismatches(&in, &S, (zui_rect_t){0, 0, W, H}, private_visible) == 0,
          "and moves with the window");
    sink_reset();
    zui_capture_window(&CAP, &S, b, true, &SCRATCH, 0, sink, 0);
    in = read_png(SINK, nsink);
    CHECK(in.ok && out_at(&in, 41, 51) == 0 && out_at(&in, 39, 51) == A2.rgb &&
              out_at(&in, 160, 74) == A2.rgb,
          "off screen, only the field is black");
    /* a field partly outside its content is clipped to it */
    int32_t h2 = zui_capture_private_rect(&CAP, b, (zui_rect_t){-50, -50, 70, 70});
    sink_reset();
    zui_capture_surface(&CAP, &S, sink, 0);
    in = read_png(SINK, nsink);
    nbl = 2;
    BL[1] = (zui_rect_t){cb.x, cb.y, 20, 20};
    CHECK(in.ok && mismatches(&in, &S, (zui_rect_t){0, 0, W, H}, private_visible) == 0,
          "clipped to the content (the title bar shows)");
    zui_capture_private_clear(&CAP, h2);
    /* a screen rect (the lock screen) covers everything, windows included */
    int32_t h3 = zui_capture_private_rect(&CAP, 0, (zui_rect_t){0, 0, W, H});
    sink_reset();
    zui_capture_surface(&CAP, &S, sink, 0);
    in = read_png(SINK, nsink);
    lit = 0;
    for (int32_t i = 0; in.ok && i < in.w * in.h; i++) lit += OUTPX[i] != 0;
    CHECK(in.ok && lit == 0, "a screen-wide private rect: all black");
    zui_capture_private_clear(&CAP, h3);
    zui_capture_private_clear(&CAP, h1);
    zui_capture_private_clear(&CAP, -1);
    zui_capture_private_clear(&CAP, 1000);
    for (uint32_t i = 0; i < ZUI_CAP_PRIVATE_RECTS; i++)
        CHECK(zui_capture_private_rect(&CAP, 0, (zui_rect_t){0, 0, 1, 1}) >= 0, "fill %u", i);
    CHECK(zui_capture_private_rect(&CAP, 0, (zui_rect_t){0, 0, 1, 1}) == -1, "full");
    for (int32_t i = 0; i < (int32_t) ZUI_CAP_PRIVATE_RECTS; i++)
        zui_capture_private_clear(&CAP, i);
    /* private window list fills */
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) zui_capture_set_private(&CAP, 100 + i, true);
    CHECK(!zui_capture_set_private(&CAP, 500, true), "private list full");
    for (uint32_t i = 0; i < ZUI_WM_MAX; i++) zui_capture_set_private(&CAP, 100 + i, false);
    CHECK(CAP.npriv == 0, "emptied");
    (void) A3;
}

/* ---- recording ------------------------------------------------------- */

static uint32_t FRAMES[12][120 * 80];
static uint32_t PREV[W * H];

typedef struct {
    uint32_t w, h, fps, enc, key_every;
    uint32_t nframes, nkey, ndelta;
    uint32_t t[64], idx[64];
    uint32_t end_frames, end_ms;
    bool ok;
    const char *why;
} rec_info_t;

static uint32_t REPLAY[64][120 * 80];

/* Parse the container and replay every frame into REPLAY. */
static rec_info_t read_rec(const uint8_t *p, uint32_t n)
{
    rec_info_t ri;
    memset(&ri, 0, sizeof ri);
    ri.why = "header";
    if (n < ZUI_REC_HEADER || memcmp(p, "ZXVR", 4) || p[4] != 1 || p[5] || p[6] != 32 || p[7])
        return ri;
    ri.w = le(p + 8);
    ri.h = le(p + 12);
    ri.fps = le(p + 16);
    ri.enc = le(p + 20);
    ri.key_every = le(p + 24);
    if (le(p + 28) || ri.w * ri.h > 120 * 80) return ri;
    uint32_t at = ZUI_REC_HEADER;
    static uint32_t cur[120 * 80];
    memset(cur, 0, sizeof cur);
    for (;;) {
        ri.why = "record";
        if (at + ZUI_REC_RECORD > n) return ri;
        uint8_t kind = p[at];
        if (p[at + 1] || p[at + 2] || p[at + 3]) return ri;
        uint32_t idx = le(p + at + 4), t = le(p + at + 8), len = le(p + at + 12);
        at += ZUI_REC_RECORD;
        if (at + len > n) return ri;
        if (kind == ZUI_REC_KIND_END) {
            ri.end_frames = idx;
            ri.end_ms = t;
            ri.ok = len == 0 && at == n;
            ri.why = ri.ok ? "" : "trailing";
            return ri;
        }
        if (ri.nframes >= 64 || idx != ri.nframes) return ri;
        if (kind == ZUI_REC_KIND_KEY) {
            png_info_t in = read_png(p + at, len);
            ri.why = "frame png";
            if (!in.ok || (uint32_t) in.w != ri.w || (uint32_t) in.h != ri.h) return ri;
            memcpy(cur, OUTPX, ri.w * ri.h * 4);
            ri.nkey++;
        } else if (kind == ZUI_REC_KIND_DELTA) {
            ri.why = "delta";
            if (!ri.nframes) return ri;
            uint32_t k = 0, q = at;
            while (k < ri.w * ri.h) {
                if (q + 8 > at + len) return ri;
                uint32_t skip = le(p + q), cnt = le(p + q + 4);
                q += 8;
                k += skip;
                if (k + cnt > ri.w * ri.h || q + 3 * cnt > at + len) return ri;
                for (uint32_t j = 0; j < cnt; j++, k++, q += 3)
                    cur[k] = (uint32_t) p[q] << 16 | (uint32_t) p[q + 1] << 8 | p[q + 2];
            }
            if (q != at + len) return ri;
            ri.ndelta++;
        } else {
            return ri;
        }
        ri.t[ri.nframes] = t;
        ri.idx[ri.nframes] = idx;
        memcpy(REPLAY[ri.nframes], cur, ri.w * ri.h * 4);
        ri.nframes++;
        at += len;
    }
}

/* scene k: the rect region shows a box at a place that depends on k */
static void scene(int k)
{
    zui_unclip(&S);
    zui_fill(&S, (zui_rect_t){0, 0, W, H}, ZUI_VOID);
    zui_fill(&S, (zui_rect_t){100 + 7 * k, 100 + 3 * k, 30, 20}, ZUI_GOLD);
    zui_fill(&S, (zui_rect_t){110, 150, 40 + k, 10}, 0x123456u + (uint32_t) k);
}

static void snap(uint32_t *dst, zui_rect_t r)
{
    for (int32_t y = 0; y < r.h; y++)
        for (int32_t x = 0; x < r.w; x++) {
            int32_t sx = r.x + x, sy = r.y + y;
            dst[y * r.w + x] =
                sx >= 0 && sy >= 0 && sx < W && sy < H ? PX[sy * W + sx] & 0xFFFFFFu : 0;
        }
}

static void test_record(void)
{
    zui_wm_init(&WM, W, H, false);
    zui_capture_init(&CAP, &WM);
    CAP.in_use = on_use;
    memset(uses_on, 0, sizeof uses_on);
    memset(uses_off, 0, sizeof uses_off);
    in_use_now = 0;
    zui_rec_t R;
    zui_rec_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.target = ZUI_REC_RECT;
    cfg.rect = (zui_rect_t){90, 90, 120, 80};
    cfg.fps = 10;
    cfg.write = sink;

    /* refusals */
    cfg.fps = 0;
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_E_ARG, "fps 0");
    cfg.fps = ZUI_REC_FPS_MAX + 1;
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_E_ARG, "fps too high");
    cfg.fps = 10;
    cfg.encoding = ZUI_REC_DELTA;
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_E_ARG, "delta needs a frame buffer");
    cfg.prev = PREV;
    cfg.prev_cap = 100;
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_E_ARG, "frame buffer too small");
    cfg.prev = 0;
    cfg.prev_cap = 0;
    cfg.encoding = ZUI_REC_PNG;
    cfg.target = ZUI_REC_WINDOW;
    cfg.win = 42;
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_E_GONE, "no such window");
    cfg.target = ZUI_REC_RECT;
    cfg.rect.w = 0;
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_E_ARG, "empty rect");
    cfg.rect.w = 120;
    CHECK(uses_on[ZUI_CAP_RECORDING] == 0 && !CAP.recording, "nothing reported for refusals");
    CHECK(zui_rec_present(&R, &S, 0, 0) == ZUI_CAP_E_STATE, "present when not recording");

    /* PNG every frame, at 10 per second, presents every 16 ms */
    sink_reset();
    scene(0);
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 5000) == ZUI_CAP_OK, "start");
    CHECK(uses_on[ZUI_CAP_RECORDING] == 1 && CAP.recording && in_use_now == 1,
          "CP3: recording reports on");
    zui_rec_t R2;
    CHECK(zui_rec_start(&R2, &CAP, &S, &cfg, 5000) == ZUI_CAP_E_STATE, "one recording at a time");
    int k = 0, written = 0, notdue = 0;
    uint32_t snaps = 0;
    for (uint32_t t = 5000; t < 6000; t += 16) {
        if ((t - 5000) / 250u != (uint32_t) k && k < 3) { /* the scene changes 4 times a second */
            k = (int) ((t - 5000) / 250u);
            scene(k);
        }
        int rc = zui_rec_present(&R, &S, 0, t);
        if (rc == 1) {
            snap(FRAMES[snaps < 12 ? snaps : 11], cfg.rect);
            snaps++;
            written++;
        } else if (rc == 0) {
            notdue++;
        }
    }
    CHECK(written == 4, "only changed frames are written: %d", written);
    CHECK(R.skipped == 6, "the unchanged slots are skipped: %u", R.skipped);
    CHECK(notdue == 63 - 4, "every other present wrote nothing (%d)", notdue);
    CHECK(zui_rec_stop(&R, 6000) == ZUI_CAP_OK, "stop");
    CHECK(uses_off[ZUI_CAP_RECORDING] == 1 && !CAP.recording && in_use_now == 0,
          "CP3: recording reports off");
    CHECK(zui_rec_stop(&R, 6001) == ZUI_CAP_E_STATE, "stop twice");
    CHECK(zui_rec_present(&R, &S, 0, 6100) == ZUI_CAP_E_STATE, "present after stop");
    rec_info_t ri = read_rec(SINK, nsink);
    CHECK(ri.ok, "container parses (%s)", ri.why);
    CHECK(ri.w == 120 && ri.h == 80 && ri.fps == 10 && ri.enc == ZUI_REC_PNG, "header fields");
    CHECK(ri.nframes == 4 && ri.nkey == 4 && ri.end_frames == 4 && ri.end_ms == 1000,
          "4 key frames, end record (%u, %u ms)", ri.nframes, ri.end_ms);
    CHECK(ri.t[0] == 0 && ri.t[1] >= 250 && ri.t[1] < 350 && ri.t[2] >= 500 && ri.t[3] >= 750,
          "timestamps in ms from the start: %u %u %u %u", ri.t[0], ri.t[1], ri.t[2], ri.t[3]);
    for (uint32_t f = 0; f < 4 && ri.ok; f++)
        CHECK(!memcmp(REPLAY[f], FRAMES[f], 120 * 80 * 4), "frame %u replays exactly", f);

    /* the schedule: frame k is due at k * 1000 / fps, late calls skip slots */
    cfg.fps = 3;
    sink_reset();
    zui_rec_start(&R, &CAP, &S, &cfg, 0);
    int due = 0;
    for (uint32_t t = 0; t < 1000; t++) {
        scene((int) (t % 7u)); /* always changing */
        due += zui_rec_present(&R, &S, 0, t) == 1;
    }
    CHECK(due == 3, "3 per second exactly: %d", due);
    scene(5);
    CHECK(zui_rec_present(&R, &S, 0, 5000) == 1, "a late call takes a frame");
    scene(6);
    CHECK(zui_rec_present(&R, &S, 0, 5100) == 0, "the next slot (5333) is not due at 5100");
    CHECK(zui_rec_present(&R, &S, 0, 5334) == 1, "due at 5334");
    zui_rec_stop(&R, 5400);
    ri = read_rec(SINK, nsink);
    CHECK(ri.ok && ri.nframes == 5 && ri.t[3] == 5000 && ri.t[4] == 5334, "late timestamps");

    /* damage: none in the target skips without reading pixels */
    cfg.fps = 60;
    sink_reset();
    scene(0);
    zui_rec_start(&R, &CAP, &S, &cfg, 0);
    zui_damage_t dmg;
    memset(&dmg, 0, sizeof dmg);
    CHECK(zui_rec_present(&R, &S, &dmg, 0) == 1, "the first frame is always written");
    scene(1);
    dmg.n = 1;
    dmg.r[0] = (zui_rect_t){500, 300, 10, 10}; /* elsewhere */
    CHECK(zui_rec_present(&R, &S, &dmg, 100) == 0 && R.skipped == 1,
          "damage outside the target: skipped (even though pixels changed)");
    dmg.r[0] = (zui_rect_t){95, 95, 10, 10};
    CHECK(zui_rec_present(&R, &S, &dmg, 200) == 1, "damage inside: written");
    dmg.n = 0;
    CHECK(zui_rec_present(&R, &S, &dmg, 300) == 0, "no damage: skipped");
    dmg.n = 1;
    CHECK(zui_rec_present(&R, &S, &dmg, 400) == 0 && R.skipped == 3,
          "damage but the same pixels: skipped");
    zui_rec_stop(&R, 500);

    /* DELTA: key frames and run-length deltas, replayed exactly */
    cfg.fps = 10;
    cfg.encoding = ZUI_REC_DELTA;
    cfg.prev = PREV;
    cfg.prev_cap = sizeof PREV / 4;
    cfg.key_every = 3;
    sink_reset();
    scene(0);
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_OK, "delta start");
    uint32_t t = 0;
    for (int f = 0; f < 7; f++) {
        scene(f);
        if (f == 4) zui_fill(&S, (zui_rect_t){0, 0, W, H}, 0x010203u); /* all changed */
        snap(FRAMES[f], cfg.rect);
        CHECK(zui_rec_present(&R, &S, 0, t) == 1, "delta frame %d written", f);
        t += 100;
    }
    CHECK(zui_rec_present(&R, &S, 0, t) == 0, "same pixels, exact compare: skipped");
    zui_rec_stop(&R, t);
    ri = read_rec(SINK, nsink);
    CHECK(ri.ok && ri.enc == ZUI_REC_DELTA && ri.key_every == 3, "delta container (%s)", ri.why);
    CHECK(ri.nframes == 7 && ri.nkey == 3 && ri.ndelta == 4, "keys at 0, 3, 6: %u keys %u deltas",
          ri.nkey, ri.ndelta);
    for (int f = 0; f < 7 && ri.ok; f++)
        CHECK(!memcmp(REPLAY[f], FRAMES[f], 120 * 80 * 4), "delta frame %d replays exactly", f);
    CHECK(nsink < 4u * 120u * 80u * 3u, "deltas are small (%u bytes)", nsink);

    /* a window target follows the window; a private window records black */
    zui_wm_init(&WM, W, H, false);
    zui_capture_init(&CAP, &WM);
    CAP.in_use = on_use;
    uint32_t a = open_app(&A1, "Alpha");
    zui_wm_move(&WM, a, (zui_rect_t){20, 20, 340, 220});
    draw();
    memset(&cfg, 0, sizeof cfg);
    cfg.target = ZUI_REC_WINDOW;
    cfg.win = a;
    cfg.content_only = true;
    cfg.fps = 30;
    cfg.write = sink;
    sink_reset();
    zui_rect_t c0 = zui_capture_window_rect(&CAP, a, true);
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_OK && R.w == c0.w && R.h == c0.h,
          "window recording at its content size");
    CHECK(zui_rec_present(&R, &S, 0, 0) == 1, "window frame 0");
    zui_wm_move(&WM, a, (zui_rect_t){120, 60, 340, 220});
    draw();
    CHECK(zui_rec_present(&R, &S, 0, 100) == 0, "moved but the content is the same: skipped");
    zui_capture_set_private(&CAP, a, true);
    CHECK(zui_rec_present(&R, &S, 0, 200) == 1, "marked private: a new frame");
    zui_wm_close(&WM, a);
    draw();
    CHECK(zui_rec_present(&R, &S, 0, 300) == 0, "closed: black again, unchanged");
    zui_rec_stop(&R, 400);
    /* the frames are bigger than REPLAY: read the PNGs by hand */
    {
        uint32_t at = ZUI_REC_HEADER, f = 0;
        bool good = true;
        uint32_t lit[2] = {0, 0};
        while (at + ZUI_REC_RECORD <= nsink && SINK[at] != ZUI_REC_KIND_END && f < 2) {
            uint32_t len = le(SINK + at + 12);
            png_info_t in = read_png(SINK + at + ZUI_REC_RECORD, len);
            good = good && in.ok && in.w == c0.w && in.h == c0.h;
            for (int32_t i = 0; in.ok && i < in.w * in.h; i++) lit[f] += OUTPX[i] != 0;
            at += ZUI_REC_RECORD + len;
            f++;
        }
        CHECK(good && f == 2, "two window frames decode");
        CHECK(lit[0] > 1000 && lit[1] == 0, "the private frame is black (%u, %u)", lit[0], lit[1]);
    }

    /* a writer that fails mid recording ends it and turns the indicator off */
    zui_capture_init(&CAP, 0);
    CAP.in_use = on_use;
    memset(&cfg, 0, sizeof cfg);
    cfg.target = ZUI_REC_SURFACE;
    cfg.fps = 10;
    cfg.write = sink;
    sink_reset();
    scene(0);
    int before = in_use_now;
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_OK && in_use_now == before + 1,
          "surface recording");
    fail_after = nsink + 50;
    CHECK(zui_rec_present(&R, &S, 0, 0) == ZUI_CAP_E_WRITE, "write failure surfaces");
    CHECK(!R.on && !CAP.recording && in_use_now == before, "and the indicator goes off");
    CHECK(zui_rec_stop(&R, 10) == ZUI_CAP_E_WRITE, "stop reports the error");
    CHECK(in_use_now == before, "off only once");
    sink_reset();
    fail_after = 10;
    CHECK(zui_rec_start(&R, &CAP, &S, &cfg, 0) == ZUI_CAP_E_WRITE && !CAP.recording &&
              in_use_now == before,
          "a header that cannot be written: not started");
}

int main(void)
{
    zui_surface_init(&S, PX, W, H, W);
    test_checksums();
    test_png();
    test_windows();
    test_record();
    printf("%s test_zui_capture: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}

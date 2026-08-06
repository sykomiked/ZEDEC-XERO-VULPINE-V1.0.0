/* test_video.c — the software rasteriser against literal pixel values.
 *
 * The anchors here are hand-derived, not read back out of the implementation:
 * the Bresenham run for (0,0)->(9,3) is stepped by hand below, the midpoint
 * circle for r=5 is enumerated by hand, the RGB565 encoding of 0xFFFF8040 is
 * 0xFC08 because 255>>3=31, 128>>2=32, 64>>3=8, and the 50% blend of red over
 * black is 0x80 because (255*128 + 0*127 + 127)/255 = 128. If the rasteriser
 * drifts from those numbers, something on a screen moved.
 *
 * Two categories get deliberate attention:
 *   1. The vbe.c font bug. That table is ASCII-32 indexed and vbe_draw_char()
 *      indexes it with the raw byte, so 'A' renders the glyph 32 rows late.
 *      test_font_is_ascii32_indexed() draws 'A' and asserts the 'A' bitmap,
 *      which is exactly the pattern the off-by-32 destroys.
 *   2. Anything that needs hardware. Every one of those is checked to FAIL
 *      with a specific error code when no backend is bound, and to succeed
 *      only once a fake backend acknowledges the work.
 */
#include <stdio.h>
#include <string.h>
#include "video.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c,m) do{ checks++; if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* ===== fixtures ===================================================== */

#define W 32
#define H 16
#define BG 0xFF000000u          /* a zeroed XRGB8888 buffer reads back as this */
#define RED   0xFFFF0000u
#define GREEN 0xFF00FF00u
#define BLUE  0xFF0000FFu
#define WHITE 0xFFFFFFFFu

static uint8_t g_fb[W * H * 4];
static video_device_t g_dev;

static void fixture_reset(void) {
    memset(g_fb, 0, sizeof g_fb);
    video_init(&g_dev, "test-video");
    uint32_t id = video_add_display(&g_dev, W, H, 32);
    (void)id;
    video_set_framebuffer(&g_dev, 1, g_fb, sizeof g_fb);
}

/* read a pixel of display 1 as canonical ARGB; 1 on failure so a broken read
 * never silently reads as the background colour */
static uint32_t P(int32_t x, int32_t y) {
    uint32_t v = 1;
    if (!video_read_pixel(&g_dev, 1, x, y, &v)) return 1;
    return v;
}

static int count_non_bg(void) {
    int n = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (P(x, y) != BG) n++;
    return n;
}

/* ===== fake hardware backend ======================================== */

typedef struct {
    int flip_calls, vsync_calls, mode_calls, probe_calls;
    int flip_rc, vsync_rc, mode_rc, probe_rc;
    void *last_front;
    uint32_t last_pitch;
    uint32_t probe_w, probe_h;
} fake_hw_t;

static int fake_flip(void *ctx, uint32_t id, void *front, uint32_t pitch) {
    fake_hw_t *hw = (fake_hw_t *)ctx; (void)id;
    hw->flip_calls++; hw->last_front = front; hw->last_pitch = pitch;
    return hw->flip_rc;
}
static int fake_vsync(void *ctx, uint32_t id) {
    fake_hw_t *hw = (fake_hw_t *)ctx; (void)id;
    hw->vsync_calls++; return hw->vsync_rc;
}
static int fake_setmode(void *ctx, uint32_t id, uint32_t w, uint32_t h,
                        uint32_t bpp, pixel_format_t fmt) {
    fake_hw_t *hw = (fake_hw_t *)ctx; (void)id;(void)w;(void)h;(void)bpp;(void)fmt;
    hw->mode_calls++; return hw->mode_rc;
}
static int fake_probe(void *ctx, uint32_t id, uint32_t *w, uint32_t *h) {
    fake_hw_t *hw = (fake_hw_t *)ctx; (void)id;
    hw->probe_calls++;
    if (hw->probe_rc != 0) return hw->probe_rc;
    *w = hw->probe_w; *h = hw->probe_h;
    return 0;
}

/* ===== tests ======================================================== */

static void test_pixel_formats(void) {
    printf("\n--- pixel formats: exact bytes on the wire ---\n");
    /* 0xFFFF8040 : R=255 G=128 B=64 */
    struct { uint32_t bpp; pixel_format_t fmt; uint32_t in;
             uint32_t nbytes; uint8_t raw[4]; uint32_t back; } tv[] = {
      { 8,  PIXEL_RGB332,   0xFFFF8040u, 1, {0xF1,0,0,0},          0xFFFF9255u },
      { 16, PIXEL_RGB565,   0xFFFF8040u, 2, {0x08,0xFC,0,0},       0xFFFF8242u },
      { 24, PIXEL_RGB888,   0xFFFF8040u, 3, {0x40,0x80,0xFF,0},    0xFFFF8040u },
      { 32, PIXEL_XRGB8888, 0xFFFF8040u, 4, {0x40,0x80,0xFF,0x00}, 0xFFFF8040u },
      { 32, PIXEL_ARGB8888, 0x80FF8040u, 4, {0x40,0x80,0xFF,0x80}, 0x80FF8040u },
    };
    static const char *nm[] = {"RGB332","RGB565","RGB888","XRGB8888","ARGB8888"};

    for (unsigned t = 0; t < sizeof tv / sizeof tv[0]; t++) {
        uint8_t buf[64];
        video_device_t d;
        memset(buf, 0, sizeof buf);
        video_init(&d, "fmt");
        uint32_t id = video_add_display(&d, 4, 2, tv[t].bpp);
        video_set_resolution(&d, id, 4, 2, tv[t].bpp, tv[t].fmt);
        video_set_framebuffer(&d, id, buf, 4 * 2 * tv[t].nbytes);
        video_put_pixel(&d, 1, 0, tv[t].in);

        uint32_t off = 1 * tv[t].nbytes;
        int bytes_ok = 1;
        for (uint32_t i = 0; i < tv[t].nbytes; i++)
            if (buf[off + i] != tv[t].raw[i]) bytes_ok = 0;
        char msg[128];
        snprintf(msg, sizeof msg, "%s stores the exact bytes the format defines", nm[t]);
        if (!bytes_ok)
            printf("        got %02X %02X %02X %02X\n",
                   buf[off], buf[off+1], buf[off+2], buf[off+3]);
        CHECK(bytes_ok, msg);

        uint32_t back = 0;
        CHECK(video_read_pixel(&d, id, 1, 0, &back) && back == tv[t].back,
              (snprintf(msg, sizeof msg,
                        "%s reads back 0x%08X (low bits replicated, not zero-filled)",
                        nm[t], tv[t].back), msg));

        /* Round-tripping the STORED value must be lossless, or a blit through
         * the canonical form would slowly bleach the screen. */
        video_put_pixel(&d, 2, 0, back);
        int stable = 1;
        for (uint32_t i = 0; i < tv[t].nbytes; i++)
            if (buf[2 * tv[t].nbytes + i] != tv[t].raw[i]) stable = 0;
        CHECK(stable, (snprintf(msg, sizeof msg,
                                "%s pack(unpack(raw)) == raw — a blit cannot drift", nm[t]), msg));
    }
}

static void test_put_pixel_and_bounds(void) {
    printf("\n--- put_pixel / bounds ---\n");
    fixture_reset();
    video_put_pixel(&g_dev, 3, 4, RED);
    CHECK(P(3,4) == RED, "put_pixel(3,4) sets exactly that pixel to 0xFFFF0000");
    CHECK(P(2,4) == BG && P(4,4) == BG && P(3,3) == BG && P(3,5) == BG,
          "and leaves all four neighbours untouched");
    CHECK(count_non_bg() == 1, "exactly one pixel in the whole surface changed");
    CHECK(g_dev.stat_pixels_written == 1, "stat_pixels_written == 1");

    uint64_t before = g_dev.stat_pixels_written;
    video_put_pixel(&g_dev, -1, 4, RED);
    video_put_pixel(&g_dev, W, 4, RED);
    video_put_pixel(&g_dev, 3, -1, RED);
    video_put_pixel(&g_dev, 3, H, RED);
    video_put_pixel(&g_dev, 2000000000, 2000000000, RED);
    CHECK(g_dev.stat_pixels_written == before,
          "five out-of-bounds writes store nothing");
    CHECK(g_dev.stat_pixels_clipped == 5, "and all five are counted as clipped");
    CHECK(count_non_bg() == 1, "the surface still holds exactly one set pixel");
}

static void test_fill_rect(void) {
    printf("\n--- fill_rect ---\n");
    fixture_reset();
    video_fill_rect(&g_dev, 5, 3, 7, 4, GREEN);
    CHECK(count_non_bg() == 7 * 4, "fill_rect(5,3,7,4) touches exactly 28 pixels");
    CHECK(g_dev.stat_pixels_written == 28, "stat_pixels_written == 28");
    CHECK(P(5,3) == GREEN && P(11,3) == GREEN && P(5,6) == GREEN && P(11,6) == GREEN,
          "all four corners of the rect are set");
    CHECK(P(4,3) == BG && P(12,3) == BG && P(5,2) == BG && P(5,7) == BG,
          "the pixels just outside each edge are NOT set (half-open extents)");

    fixture_reset();
    video_fill_rect(&g_dev, 5, 3, 0, 4, GREEN);
    video_fill_rect(&g_dev, 5, 3, 7, 0, GREEN);
    video_fill_rect(&g_dev, 5, 3, -7, -4, GREEN);
    CHECK(count_non_bg() == 0, "zero and negative extents draw nothing");

    /* a rect that starts off-surface still fills its visible part */
    fixture_reset();
    video_fill_rect(&g_dev, -3, -2, 6, 5, BLUE);
    CHECK(count_non_bg() == 3 * 3, "a rect straddling the top-left corner fills 3x3");
    CHECK(P(0,0) == BLUE && P(2,2) == BLUE && P(3,0) == BG,
          "and the visible part is in the right place");
    CHECK(g_dev.stat_pixels_clipped == 6 * 5 - 9,
          "the 21 pixels that fell off the surface are counted as clipped");
}

static void test_draw_line(void) {
    printf("\n--- draw_line: Bresenham, hand-stepped ---\n");
    /* (0,0)->(9,3): dx=9 dy=3 err=6. Stepping the standard integer form by
     * hand gives exactly these ten pixels, 2/3/3/2 per scanline. */
    static const int exp[10][2] = {
        {0,0},{1,0},{2,1},{3,1},{4,1},{5,2},{6,2},{7,2},{8,3},{9,3}
    };
    fixture_reset();
    video_draw_line(&g_dev, 0, 0, 9, 3, WHITE);
    CHECK(count_non_bg() == 10, "the line lays down exactly max(dx,dy)+1 = 10 pixels");
    int all = 1;
    for (int i = 0; i < 10; i++) if (P(exp[i][0], exp[i][1]) != WHITE) all = 0;
    CHECK(all, "every one of the ten hand-derived Bresenham pixels is set");
    CHECK(P(0,0) == WHITE && P(9,3) == WHITE, "both endpoints are set");
    CHECK(P(4,1) == WHITE, "the midpoint (4,1) is on the line");
    CHECK(P(4,2) == BG && P(5,1) == BG,
          "the two pixels flanking the midpoint are NOT on the line");

    /* symmetry: drawing it backwards must cover the same scanline spans */
    fixture_reset();
    video_draw_line(&g_dev, 9, 3, 0, 0, WHITE);
    CHECK(count_non_bg() == 10, "the reversed line is also exactly 10 pixels");
    CHECK(P(0,0) == WHITE && P(9,3) == WHITE, "reversed: both endpoints still set");

    /* A STEEP line with an EVEN minor axis. This is the one shape where the
     * error test must be strict (e2 > -dy, not >=): with dy even, 2*err can
     * land exactly on -dy, and the two variants then diverge. Correct gives
     * (0,1); the sloppy variant gives (1,1). */
    /* The SHALLOW mirror of the same tie, on the other comparison (e2 < dx).
     * dx=4 is even, so 2*err reaches dx exactly and a <= there would lift the
     * whole line one row early. */
    fixture_reset();
    video_draw_line(&g_dev, 0, 0, 4, 2, WHITE);
    CHECK(count_non_bg() == 5, "the shallow line (0,0)->(4,2) is exactly 5 pixels");
    CHECK(P(0,0) == WHITE && P(1,0) == WHITE && P(2,1) == WHITE &&
          P(3,1) == WHITE && P(4,2) == WHITE,
          "and they are (0,0),(1,0),(2,1),(3,1),(4,2)");
    CHECK(P(1,1) == BG && P(3,2) == BG,
          "the tie at 2*err == dx does not lift the line a row early");

    fixture_reset();
    video_draw_line(&g_dev, 0, 0, 2, 4, WHITE);
    CHECK(count_non_bg() == 5, "the steep line (0,0)->(2,4) is exactly 5 pixels");
    CHECK(P(0,0) == WHITE && P(0,1) == WHITE && P(1,2) == WHITE &&
          P(1,3) == WHITE && P(2,4) == WHITE,
          "and they are (0,0),(0,1),(1,2),(1,3),(2,4)");
    CHECK(P(1,1) == BG && P(2,3) == BG,
          "the tie at 2*err == -dy breaks toward the shallower pixel, not (1,1)");

    fixture_reset();
    video_draw_line(&g_dev, 6, 2, 6, 2, RED);
    CHECK(count_non_bg() == 1 && P(6,2) == RED, "a degenerate line is one pixel");

    fixture_reset();
    video_draw_line(&g_dev, 2, 5, 2, 12, RED);
    CHECK(count_non_bg() == 8, "a vertical line covers 8 pixels");
    CHECK(P(2,5) == RED && P(2,12) == RED && P(3,5) == BG, "and is one pixel wide");

    fixture_reset();
    video_draw_line(&g_dev, 4, 9, 20, 9, RED);
    CHECK(count_non_bg() == 17, "a horizontal line covers 17 pixels");

    fixture_reset();
    video_draw_line(&g_dev, 0, 0, 7, 7, RED);
    CHECK(count_non_bg() == 8 && P(3,3) == RED && P(3,4) == BG,
          "a 45-degree line is the diagonal, 8 pixels, nothing beside it");

    /* fully off-surface: trivially rejected, nothing written */
    fixture_reset();
    uint64_t w0 = g_dev.stat_pixels_written;
    video_draw_line(&g_dev, -50, -50, -10, -20, RED);
    CHECK(g_dev.stat_pixels_written == w0 && count_non_bg() == 0,
          "a line entirely off the surface writes nothing");
    CHECK(g_dev.stat_pixels_clipped == 41,
          "its 41 would-be pixels are all counted as clipped");
}

static void test_draw_circle(void) {
    printf("\n--- draw_circle: midpoint, hand-enumerated for r=5 ---\n");
    /* r=5 from (10,10). The decision variable runs
     *   (x,y) = (5,0) (5,1) (5,2) (4,3) and stops when x < y,
     * so the 8-way symmetry yields 4 axis pixels + 3*8 = 28 distinct pixels. */
    fixture_reset();
    video_draw_circle(&g_dev, 10, 10, 5, WHITE);
    CHECK(count_non_bg() == 28, "the r=5 circle is exactly 28 distinct pixels");
    CHECK(P(15,10) == WHITE && P(5,10) == WHITE && P(10,15) == WHITE && P(10,5) == WHITE,
          "the four axis points at distance 5 are set");
    CHECK(P(14,13) == WHITE && P(13,14) == WHITE && P(6,7) == WHITE && P(7,6) == WHITE,
          "the (4,3) octant points are set — 16+9 = 25, exactly on the circle");
    CHECK(P(15,15) == BG && P(5,5) == BG,
          "the bounding-box corners are NOT on the circle (distance 7.07)");
    CHECK(P(10,10) == BG, "the centre is not filled — this is an outline, not a disc");
    CHECK(P(13,13) == BG, "a point inside the circle is not set");

    /* r=4 is the smallest radius at which the decision update matters: the
     * true midpoint circle steps x down to 3 while y is 2, so (3,2)/(2,3) are
     * on it and (4,2)/(2,4) are not. round(sqrt(16-4)) = 3, not 4. */
    fixture_reset();
    video_draw_circle(&g_dev, 8, 8, 4, WHITE);
    CHECK(count_non_bg() == 24, "the r=4 circle is exactly 24 distinct pixels");
    CHECK(P(11,10) == WHITE && P(10,11) == WHITE,
          "(3,2) and (2,3) are on it — round(sqrt(16-4)) = 3");
    CHECK(P(12,10) == BG && P(10,12) == BG,
          "(4,2) and (2,4) are NOT — the decision variable must step x down here");
    CHECK(P(11,11) == WHITE && P(12,9) == WHITE && P(12,8) == WHITE,
          "the 45-degree point (3,3) and the (4,1),(4,0) points are on it");

    fixture_reset();
    video_draw_circle(&g_dev, 8, 8, 0, RED);
    CHECK(count_non_bg() == 1 && P(8,8) == RED, "r=0 is a single pixel at the centre");

    fixture_reset();
    video_draw_circle(&g_dev, 8, 8, 1, RED);
    CHECK(count_non_bg() == 4, "r=1 is the four axis neighbours");
    CHECK(P(9,8) == RED && P(7,8) == RED && P(8,9) == RED && P(8,7) == RED &&
          P(9,9) == BG, "and not the diagonals");

    fixture_reset();
    video_draw_circle(&g_dev, 8, 8, -3, RED);
    CHECK(count_non_bg() == 0, "a negative radius draws nothing");

    fixture_reset();
    video_draw_circle(&g_dev, 8, 8, VIDEO_MAX_RADIUS + 1, RED);
    CHECK(count_non_bg() == 0 && g_dev.stat_pixels_written == 0,
          "a radius past VIDEO_MAX_RADIUS is refused rather than looped");
}

static void test_clipping(void) {
    printf("\n--- clipping actually clips ---\n");
    fixture_reset();
    video_set_clip(&g_dev, 4, 4, 8, 8);
    video_fill_rect(&g_dev, 0, 0, W, H, RED);
    CHECK(count_non_bg() == 64, "a full-surface fill under an 8x8 clip sets 64 pixels");
    CHECK(g_dev.stat_pixels_written == 64, "stat_pixels_written == 64");
    CHECK(g_dev.stat_pixels_clipped == (uint64_t)(W * H - 64),
          "and the other 448 requested writes are counted as clipped");
    CHECK(P(4,4) == RED && P(11,11) == RED, "the clip box corners are inside");
    CHECK(P(3,4) == BG && P(12,11) == BG && P(4,3) == BG && P(11,12) == BG,
          "every pixel one step outside the clip box is untouched");

    /* clipping applies to lines too */
    fixture_reset();
    video_set_clip(&g_dev, 0, 0, 4, 4);
    video_draw_line(&g_dev, 0, 0, 9, 3, WHITE);
    CHECK(count_non_bg() == 4,
          "the same 10-pixel line under a 4x4 clip lands only 4 pixels");
    CHECK(P(0,0) == WHITE && P(1,0) == WHITE && P(2,1) == WHITE && P(3,1) == WHITE,
          "and they are exactly the four Bresenham pixels inside the box");
    CHECK(P(4,1) == BG, "the fifth, at x=4, was clipped");

    /* a degenerate clip must draw nothing at all */
    fixture_reset();
    video_set_clip(&g_dev, 4, 4, 0, 8);
    video_fill_rect(&g_dev, 0, 0, W, H, RED);
    CHECK(count_non_bg() == 0, "a zero-width clip box draws nothing");
    video_set_clip(&g_dev, 4, 4, -8, -8);
    video_fill_rect(&g_dev, 0, 0, W, H, RED);
    CHECK(count_non_bg() == 0, "a negative clip extent draws nothing");

    /* a clip larger than the surface is intersected with the surface */
    fixture_reset();
    video_set_clip(&g_dev, -100, -100, 1000, 1000);
    video_fill_rect(&g_dev, 0, 0, W, H, RED);
    CHECK(count_non_bg() == W * H,
          "an oversized clip is intersected with the surface, not trusted");
}

static void test_font_is_ascii32_indexed(void) {
    printf("\n--- font: the vbe.c off-by-32 must stay dead ---\n");
    /* 'A' = {00,3C,66,66,7E,66,66,66,...}, stored row r lands on cell row r+1.
     * Stored row 1 = 0x3C = columns 2,3,4,5.
     * If the table were indexed with the raw byte 65 instead of 65-32, 'A'
     * would render the glyph for 'a', whose stored row 1 is 0x00 — so the very
     * first assertion below is the off-by-32 detector. */
    fixture_reset();
    video_draw_text(&g_dev, 0, 0, "A", RED);
    CHECK(P(2,2) == RED && P(3,2) == RED && P(4,2) == RED && P(5,2) == RED,
          "'A' row 2 is 0x3C — columns 2..5 set (this is the off-by-32 detector)");
    CHECK(P(0,2) == BG && P(1,2) == BG && P(6,2) == BG && P(7,2) == BG,
          "'A' row 2 leaves columns 0,1,6,7 clear");
    CHECK(P(1,4) == RED && P(2,4) == RED && P(5,4) == RED && P(6,4) == RED &&
          P(3,4) == BG && P(4,4) == BG,
          "'A' row 4 is 0x66 — the two uprights with a gap between them");
    CHECK(P(1,5) == RED && P(2,5) == RED && P(3,5) == RED && P(4,5) == RED &&
          P(5,5) == RED && P(6,5) == RED,
          "'A' row 5 is 0x7E — the crossbar");
    CHECK(P(0,1) == BG && P(7,1) == BG, "'A' row 1 is blank");

    /* lowercase must be a real 'a', not the punctuation the donor table holds */
    fixture_reset();
    video_draw_text(&g_dev, 0, 0, "a", RED);
    CHECK(P(2,2) == BG && P(3,2) == BG,
          "'a' has NO ink on row 2 — it is an x-height glyph, unlike 'A'");
    CHECK(P(2,4) == RED && P(3,4) == RED && P(4,4) == RED && P(5,4) == RED,
          "'a' row 4 is 0x3C — the bowl starts here");
    CHECK(P(5,5) == RED && P(6,5) == RED && P(4,5) == BG && P(2,5) == BG,
          "'a' row 5 is 0x06 — only the right stem, columns 5 and 6");

    /* a control byte must fall back to .notdef, never index past the table */
    fixture_reset();
    video_draw_text(&g_dev, 0, 0, "\x01", RED);
    CHECK(P(1,2) == RED && P(2,2) == RED && P(6,2) == RED &&
          P(0,2) == BG && P(7,2) == BG,
          "a control byte renders the .notdef box top edge (0x7E)");
    CHECK(P(1,3) == RED && P(6,3) == RED && P(3,3) == BG,
          ".notdef row 3 is 0x42 — a hollow box, not a solid block");

    /* byte 127 and byte 255 are both outside 32..126 */
    fixture_reset();
    video_draw_text(&g_dev, 0, 0, "\x7F\xFF", RED);
    CHECK(P(1,2) == RED && P(9,2) == RED,
          "0x7F and 0xFF both render .notdef, at 8-pixel spacing");

    fixture_reset();
    video_draw_text(&g_dev, 0, 0, "~", RED);
    CHECK(P(2,5) == RED && P(3,5) == RED && P(4,5) == RED &&
          P(6,5) == RED && P(7,5) == RED && P(5,5) == BG,
          "'~' (the LAST glyph in the table, 126) renders 0x3B, not .notdef");
}

static void test_draw_text_layout(void) {
    printf("\n--- draw_text: layout and transparency ---\n");
    fixture_reset();
    video_fill_rect(&g_dev, 0, 0, W, H, BLUE);
    uint64_t before = g_dev.stat_pixels_written;
    video_draw_text(&g_dev, 0, 0, "T", RED);
    /* 'T' = 00,7E,18,18,18,18,18,18 -> 6 + 2*6 = 18 lit pixels */
    CHECK(g_dev.stat_pixels_written - before == 18,
          "'T' lights exactly 18 pixels (0x7E once, 0x18 six times)");
    CHECK(P(0,2) == BLUE && P(7,2) == BLUE,
          "the glyph background is left alone — text composites, it does not box");
    CHECK(P(3,3) == RED && P(4,3) == RED && P(2,3) == BLUE,
          "'T' stem is columns 3,4");

    fixture_reset();
    video_draw_text(&g_dev, 0, 0, "AA", RED);
    CHECK(P(2,2) == RED && P(5,2) == RED && P(10,2) == RED && P(13,2) == RED &&
          P(8,2) == BG && P(14,2) == BG,
          "the second 'A' is the first shifted exactly 8 pixels right (VIDEO_FONT_W)");

    /* the newline test needs a surface taller than one 16-row cell */
    {
        static uint8_t tall[16 * 40 * 4];
        video_device_t t;
        memset(tall, 0, sizeof tall);
        video_init(&t, "tall");
        uint32_t tid = video_add_display(&t, 16, 40, 32);
        video_set_framebuffer(&t, tid, tall, sizeof tall);
        video_draw_text(&t, 0, 0, "A\nA", RED);
        uint32_t v1 = 0, v2 = 0, v3 = 0;
        video_read_pixel(&t, tid, 2, 2, &v1);
        video_read_pixel(&t, tid, 2, 18, &v2);
        video_read_pixel(&t, tid, 10, 2, &v3);
        CHECK(v1 == RED && v2 == RED && v3 == BG,
              "'\\n' returns to the start column and drops VIDEO_FONT_H = 16 rows");
    }

    fixture_reset();
    uint64_t w0 = g_dev.stat_pixels_written;
    video_draw_text(&g_dev, 0, 0, "   ", RED);
    CHECK(g_dev.stat_pixels_written == w0, "spaces light no pixels");
    video_draw_text(&g_dev, 0, 0, NULL, RED);
    CHECK(g_dev.stat_pixels_written == w0, "a NULL string draws nothing and does not fault");

    /* text clipped by the clip box */
    fixture_reset();
    video_set_clip(&g_dev, 0, 0, 4, H);
    video_draw_text(&g_dev, 0, 0, "A", RED);
    CHECK(P(2,2) == RED && P(3,2) == RED && P(4,2) == BG,
          "text obeys the clip box mid-glyph");
}

static void test_blit(void) {
    printf("\n--- blit ---\n");
    /* --- COPY, non-overlapping --- */
    fixture_reset();
    video_fill_rect(&g_dev, 0, 0, 4, 4, RED);
    video_blit(&g_dev, 8, 0, 0, 0, 4, 4, BLIT_COPY);
    CHECK(P(8,0) == RED && P(11,3) == RED, "BLIT_COPY reproduces the source rect");
    CHECK(P(12,0) == BG && P(8,4) == BG, "and does not spill past w x h");
    CHECK(count_non_bg() == 32, "16 source + 16 destination pixels are set");

    /* --- COPY with horizontal overlap (the memmove case) --- */
    fixture_reset();
    video_put_pixel(&g_dev, 0, 0, RED);
    video_put_pixel(&g_dev, 1, 0, GREEN);
    video_put_pixel(&g_dev, 2, 0, BLUE);
    video_put_pixel(&g_dev, 3, 0, WHITE);
    video_blit(&g_dev, 1, 0, 0, 0, 4, 1, BLIT_COPY);
    CHECK(P(1,0) == RED && P(2,0) == GREEN && P(3,0) == BLUE && P(4,0) == WHITE,
          "an overlapping right-shift copies R,G,B,W — it does not smear the first pixel");

    /* --- COPY with vertical overlap --- */
    fixture_reset();
    video_put_pixel(&g_dev, 0, 0, RED);
    video_put_pixel(&g_dev, 0, 1, GREEN);
    video_put_pixel(&g_dev, 0, 2, BLUE);
    video_put_pixel(&g_dev, 0, 3, WHITE);
    video_blit(&g_dev, 0, 1, 0, 0, 1, 4, BLIT_COPY);
    CHECK(P(0,1) == RED && P(0,2) == GREEN && P(0,3) == BLUE && P(0,4) == WHITE,
          "an overlapping downward copy runs bottom-up and does not smear");

    /* --- FLIPPED --- */
    fixture_reset();
    video_put_pixel(&g_dev, 0, 0, RED);
    video_put_pixel(&g_dev, 1, 0, GREEN);
    video_put_pixel(&g_dev, 2, 0, BLUE);
    video_put_pixel(&g_dev, 3, 0, WHITE);
    video_blit(&g_dev, 8, 0, 0, 0, 4, 1, BLIT_FLIPPED);
    CHECK(P(8,0) == WHITE && P(9,0) == BLUE && P(10,0) == GREEN && P(11,0) == RED,
          "BLIT_FLIPPED mirrors the row horizontally");
    video_blit(&g_dev, 0, 0, 0, 0, 4, 1, BLIT_FLIPPED);
    CHECK(P(0,0) == WHITE && P(1,0) == BLUE && P(2,0) == GREEN && P(3,0) == RED,
          "an in-place flip works: the row buffer makes full overlap safe");

    /* --- SOLID_FILL --- */
    fixture_reset();
    video_set_color(&g_dev, GREEN, BG);
    video_blit(&g_dev, 20, 8, 0, 0, 3, 2, BLIT_SOLID_FILL);
    CHECK(count_non_bg() == 6, "BLIT_SOLID_FILL paints exactly w*h = 6 pixels");
    CHECK(P(20,8) == GREEN && P(22,9) == GREEN && P(23,8) == BG,
          "with ctx.fg_color, and reads no source at all");

    /* --- ROTATED 90 CW: a 2x3 source becomes a 3x2 destination --- */
    fixture_reset();
    video_put_pixel(&g_dev, 0, 0, RED);   video_put_pixel(&g_dev, 1, 0, GREEN);
    video_put_pixel(&g_dev, 0, 1, BLUE);  video_put_pixel(&g_dev, 1, 1, WHITE);
    video_put_pixel(&g_dev, 0, 2, 0xFF123456u); video_put_pixel(&g_dev, 1, 2, 0xFF654321u);
    video_blit(&g_dev, 10, 0, 0, 0, 2, 3, BLIT_ROTATED);
    CHECK(P(12,0) == RED && P(12,1) == GREEN,
          "rotate CW: the source top-left lands at the destination top-RIGHT");
    CHECK(P(11,0) == BLUE && P(11,1) == WHITE, "middle source row becomes the middle column");
    CHECK(P(10,0) == 0xFF123456u && P(10,1) == 0xFF654321u,
          "the source bottom row becomes the destination left column");

    /* overlap is refused, not silently corrupted */
    fixture_reset();
    video_fill_rect(&g_dev, 0, 0, 2, 3, RED);
    uint64_t w0 = g_dev.stat_pixels_written;
    video_blit(&g_dev, 1, 1, 0, 0, 2, 3, BLIT_ROTATED);
    CHECK(g_dev.stat_pixels_written == w0,
          "BLIT_ROTATED with overlapping rects writes nothing (it cannot be done safely)");

    /* --- SCALED 2x --- */
    fixture_reset();
    video_put_pixel(&g_dev, 0, 0, RED);   video_put_pixel(&g_dev, 1, 0, GREEN);
    video_put_pixel(&g_dev, 0, 1, BLUE);  video_put_pixel(&g_dev, 1, 1, WHITE);
    video_blit(&g_dev, 16, 0, 0, 0, 2, 2, BLIT_SCALED);
    CHECK(P(16,0) == RED && P(17,0) == RED && P(18,0) == GREEN && P(19,0) == GREEN,
          "BLIT_SCALED doubles each source pixel horizontally");
    CHECK(P(16,1) == RED && P(18,1) == GREEN, "and duplicates the row vertically");
    CHECK(P(16,2) == BLUE && P(17,3) == BLUE && P(19,3) == WHITE,
          "the second source row fills destination rows 2 and 3");
    CHECK(P(20,0) == BG && P(16,4) == BG, "the destination is exactly 2w x 2h");

    fixture_reset();
    video_fill_rect(&g_dev, 0, 0, 2, 2, RED);
    w0 = g_dev.stat_pixels_written;
    video_blit(&g_dev, 1, 1, 0, 0, 2, 2, BLIT_SCALED);
    CHECK(g_dev.stat_pixels_written == w0, "BLIT_SCALED with overlapping rects writes nothing");

    /* --- degenerate / hostile --- */
    fixture_reset();
    video_fill_rect(&g_dev, 0, 0, 4, 4, RED);
    w0 = g_dev.stat_pixels_written;
    video_blit(&g_dev, 8, 0, 0, 0, 0, 4, BLIT_COPY);
    video_blit(&g_dev, 8, 0, 0, 0, 4, -1, BLIT_COPY);
    video_blit(&g_dev, 8, 0, 0, 0, VIDEO_BLIT_MAX_W + 1, 1, BLIT_COPY);
    video_blit(&g_dev, 8, 0, 0, 0, 4, 4, (blit_op_t)99);
    CHECK(g_dev.stat_pixels_written == w0,
          "zero/negative/oversized extents and an unknown op all draw nothing");

    /* a source rect partly off the surface copies only the part that exists */
    fixture_reset();
    video_fill_rect(&g_dev, 0, 0, 2, 2, RED);
    video_blit(&g_dev, 10, 10, -2, 0, 4, 2, BLIT_COPY);
    CHECK(P(12,10) == RED && P(13,10) == RED && P(10,10) == BG && P(11,10) == BG,
          "source pixels outside the surface are skipped, not read as garbage");
}

static void test_alpha(void) {
    printf("\n--- alpha blending: exact arithmetic ---\n");
    /* (255*128 + 0*127 + 127)/255 = 32767/255 = 128 = 0x80 */
    fixture_reset();
    video_fill_rect(&g_dev, 0, 0, 2, 2, RED);
    g_dev.ctx.alpha = 128;
    video_blit(&g_dev, 8, 8, 0, 0, 2, 2, BLIT_ALPHA);
    CHECK(P(8,8) == 0xFF800000u,
          "red at 50% over black is 0xFF800000 — (255*128+0*127+127)/255 = 128");

    /* (0*64 + 255*191 + 127)/255 = 48832/255 = 191 = 0xBF */
    fixture_reset();
    video_fill_rect(&g_dev, 8, 8, 2, 2, WHITE);
    video_fill_rect(&g_dev, 0, 0, 2, 2, 0xFF000000u);
    g_dev.ctx.alpha = 64;
    video_blit(&g_dev, 8, 8, 0, 0, 2, 2, BLIT_ALPHA);
    CHECK(P(8,8) == 0xFFBFBFBFu,
          "black at 25% over white is 0xFFBFBFBF — (0*64+255*191+127)/255 = 191");

    /* The blend rounds to nearest rather than truncating: half of 3, 5, 7 is
     * 2, 3, 4 (1.51, 2.51, 3.51 all round up). A truncating blend would give
     * 1, 2, 3 and every composited UI would creep darker frame after frame. */
    fixture_reset();
    video_fill_rect(&g_dev, 8, 8, 2, 2, 0xFF000000u);
    video_fill_rect(&g_dev, 0, 0, 2, 2, 0xFF030507u);
    g_dev.ctx.alpha = 128;
    video_blit(&g_dev, 8, 8, 0, 0, 2, 2, BLIT_ALPHA);
    CHECK(P(8,8) == 0xFF020304u,
          "0xFF030507 at 50% over black is 0xFF020304 — rounded, not truncated");

    fixture_reset();
    video_fill_rect(&g_dev, 8, 8, 2, 2, WHITE);
    video_fill_rect(&g_dev, 0, 0, 2, 2, RED);
    g_dev.ctx.alpha = 0;
    video_blit(&g_dev, 8, 8, 0, 0, 2, 2, BLIT_ALPHA);
    CHECK(P(8,8) == WHITE, "alpha 0 leaves the destination bit-identical");
    g_dev.ctx.alpha = 255;
    video_blit(&g_dev, 8, 8, 0, 0, 2, 2, BLIT_ALPHA);
    CHECK(P(8,8) == RED, "alpha 255 replaces the destination exactly");

    /* ctx.alpha_blending makes every primitive blend */
    fixture_reset();
    video_fill_rect(&g_dev, 0, 0, 4, 4, WHITE);
    g_dev.ctx.alpha_blending = true;
    g_dev.ctx.alpha = 128;
    video_put_pixel(&g_dev, 0, 0, 0xFF000000u);
    CHECK(P(0,0) == 0xFF7F7F7Fu,
          "with ctx.alpha_blending, put_pixel blends: (0*128+255*127+127)/255 = 127");
    g_dev.ctx.alpha_blending = false;
    video_put_pixel(&g_dev, 1, 0, 0xFF000000u);
    CHECK(P(1,0) == 0xFF000000u, "with it off, put_pixel stores the colour verbatim");

    /* per-pixel source alpha, on the only format that carries one */
    {
        uint8_t buf[4 * 2 * 4];
        video_device_t d;
        memset(buf, 0, sizeof buf);
        video_init(&d, "argb");
        uint32_t id = video_add_display(&d, 4, 2, 32);
        CHECK(video_set_resolution(&d, id, 4, 2, 32, PIXEL_ARGB8888) == VIDEO_OK,
              "a display can be switched to PIXEL_ARGB8888");
        video_set_framebuffer(&d, id, buf, sizeof buf);
        video_put_pixel(&d, 0, 0, 0x80FF0000u);   /* half-opaque red */
        video_put_pixel(&d, 2, 0, 0xFF000000u);   /* opaque black destination */
        d.ctx.alpha = 255;
        video_blit(&d, 2, 0, 0, 0, 1, 1, BLIT_ALPHA);
        uint32_t got = 0;
        video_read_pixel(&d, id, 2, 0, &got);
        CHECK(got == 0xFF800000u,
              "ARGB8888 honours the SOURCE pixel's alpha: 0x80 red over black = 0xFF800000");
    }
}

static void test_multi_display(void) {
    printf("\n--- multi-display ---\n");
    static uint8_t fb1[8 * 4 * 4];
    static uint8_t fb2[4 * 4 * 2];
    video_device_t d;
    memset(fb1, 0, sizeof fb1); memset(fb2, 0, sizeof fb2);
    video_init(&d, "multi");

    uint32_t a = video_add_display(&d, 8, 4, 32);
    uint32_t b = video_add_display(&d, 4, 4, 16);
    CHECK(a == 1 && b == 2, "display ids are 1-based and sequential");
    CHECK(d.num_displays == 2, "both displays are recorded");
    CHECK(d.primary_display == 1 && d.ctx.display_id == 1,
          "the first display becomes primary and the 2D context target");
    CHECK(video_get_display(&d, 0) == NULL && video_get_display(&d, 3) == NULL,
          "id 0 and an unknown id both resolve to NULL");
    CHECK(d.displays[1].format == PIXEL_RGB565 && d.displays[1].pitch == 8,
          "a 16bpp display gets RGB565 and an 8-byte pitch");

    video_set_framebuffer(&d, a, fb1, sizeof fb1);
    video_set_framebuffer(&d, b, fb2, sizeof fb2);

    video_put_pixel(&d, 0, 0, RED);
    uint32_t v = 0;
    CHECK(video_read_pixel(&d, a, 0, 0, &v) && v == RED, "drawing hits display 1");
    v = 0;
    CHECK(video_read_pixel(&d, b, 0, 0, &v) && v == BG, "and not display 2");

    CHECK(video_set_primary(&d, b) == VIDEO_OK, "the primary can be switched");
    CHECK(d.ctx.display_id == 2 && d.ctx.clip_w == 4 && d.ctx.clip_h == 4,
          "the 2D context follows, with the clip reset to the new geometry");
    video_put_pixel(&d, 0, 0, RED);
    v = 0;
    /* RGB565 cannot hold 0xFFFF0000 exactly: 255>>3 = 31 -> back to 0xFF */
    CHECK(video_read_pixel(&d, b, 0, 0, &v) && v == 0xFFFF0000u,
          "now drawing hits display 2, through the RGB565 encoder");

    CHECK(video_set_primary(&d, 99) == VIDEO_ENODISPLAY,
          "setting a nonexistent display primary fails");
    CHECK(video_get_framebuffer(&d, 99) == NULL, "so does fetching its framebuffer");
    CHECK(video_get_framebuffer(&d, a) == fb1, "and a real one returns the front page");

    /* the device holds VIDEO_MAX_DISPLAYS and no more */
    video_add_display(&d, 4, 4, 32);
    video_add_display(&d, 4, 4, 32);
    CHECK(d.num_displays == VIDEO_MAX_DISPLAYS, "four displays fit");
    CHECK(video_add_display(&d, 4, 4, 32) == 0, "the fifth is refused");

    /* rejected geometry */
    video_device_t e;
    video_init(&e, "bad");
    CHECK(video_add_display(&e, 0, 4, 32) == 0, "zero width is refused");
    CHECK(video_add_display(&e, 4, 0, 32) == 0, "zero height is refused");
    CHECK(video_add_display(&e, 4, 4, 12) == 0, "an unsupported bpp is refused");
    CHECK(video_add_display(&e, VIDEO_DEFAULT_MAX_W + 1, 4, 32) == 0,
          "a width past max_width is refused");
    CHECK(e.num_displays == 0, "and none of those five were recorded");
}

static void test_geometry_and_buffers(void) {
    printf("\n--- resolution / framebuffer binding ---\n");
    static uint8_t buf[8 * 4 * 4];
    video_device_t d;
    memset(buf, 0, sizeof buf);
    video_init(&d, "geom");
    uint32_t id = video_add_display(&d, 8, 4, 32);
    CHECK(video_set_framebuffer(&d, id, buf, sizeof buf) == VIDEO_OK,
          "an exactly-page-sized buffer binds");
    CHECK(!d.displays[0].double_buffered && d.displays[0].backbuffer == NULL,
          "one page means single buffered");

    CHECK(video_set_framebuffer(&d, id, buf, 8 * 4 * 4 - 1) == VIDEO_ENOSPC,
          "a buffer one byte short of a page is refused");
    CHECK(video_set_framebuffer(&d, 99, buf, sizeof buf) == VIDEO_ENODISPLAY,
          "binding to an unknown display fails");

    video_set_framebuffer(&d, id, buf, sizeof buf);
    CHECK(video_set_resolution(&d, id, 16, 8, 32, PIXEL_XRGB8888) == VIDEO_ENOSPC,
          "growing the resolution past the bound buffer is refused");
    CHECK(d.displays[0].width == 8 && d.displays[0].height == 4,
          "and the old geometry survives the refusal");
    CHECK(video_set_resolution(&d, id, 8, 4, 32, PIXEL_RGB565) == VIDEO_EINVAL,
          "bpp 32 with an RGB565 format is refused (they must agree)");
    CHECK(video_set_resolution(&d, id, 0, 4, 32, PIXEL_XRGB8888) == VIDEO_EINVAL,
          "a zero dimension is refused");
    CHECK(video_set_resolution(&d, 99, 8, 4, 32, PIXEL_XRGB8888) == VIDEO_ENODISPLAY,
          "an unknown display is refused");
    CHECK(video_set_resolution(&d, id, 4, 2, 16, PIXEL_RGB565) == VIDEO_OK,
          "shrinking into the same buffer succeeds");
    CHECK(d.displays[0].pitch == 8 && d.displays[0].bpp == 16,
          "and the pitch follows the new format (4 px * 2 bytes)");
    CHECK(!d.displays[0].scanout_live,
          "VIDEO_OK from set_resolution never claims a live scanout");

    /* unbind */
    CHECK(video_set_framebuffer(&d, id, NULL, 0) == VIDEO_OK, "a framebuffer can be unbound");
    CHECK(video_get_framebuffer(&d, id) == NULL, "after which there is none");
    uint64_t w0 = d.stat_pixels_written;
    video_fill_rect(&d, 0, 0, 4, 2, RED);
    CHECK(d.stat_pixels_written == w0, "and drawing with no framebuffer writes nothing");
}

static void test_double_buffer_and_flip(void) {
    printf("\n--- double buffering / page flip ---\n");
    static uint8_t buf[8 * 4 * 4 * 2];      /* two full pages */
    const uint32_t page = 8 * 4 * 4;
    video_device_t d;
    fake_hw_t hw;
    memset(buf, 0, sizeof buf);
    memset(&hw, 0, sizeof hw);
    video_init(&d, "flip");
    uint32_t id = video_add_display(&d, 8, 4, 32);
    video_set_framebuffer(&d, id, buf, sizeof buf);

    CHECK(d.displays[0].double_buffered, "two pages auto-enable double buffering");
    CHECK(d.displays[0].framebuffer == buf &&
          d.displays[0].backbuffer == buf + page,
          "front is page 0, back is page 1");

    video_put_pixel(&d, 0, 0, RED);
    CHECK(buf[page + 0] == 0x00 && buf[page + 1] == 0x00 &&
          buf[page + 2] == 0xFF && buf[page + 3] == 0x00,
          "drawing lands in the BACK page (B,G,R,X = 00,00,FF,00)");
    CHECK(buf[0] == 0 && buf[2] == 0, "the front page is untouched");

    CHECK(video_flip(&d, id) == VIDEO_OK,
          "in HEADLESS mode a flip is complete without hardware — there is no panel");
    CHECK(d.displays[0].framebuffer == buf + page && d.displays[0].backbuffer == buf,
          "the pages swapped");
    CHECK(d.stat_page_flips == 1 && d.stat_scanout_flips == 0,
          "one page flip happened; NO scanout flip did, and the counter says so");
    CHECK(!d.displays[0].scanout_live, "scanout_live is still false — nothing drives a panel");

    CHECK(video_set_mode(&d, VIDEO_MODE_FRAMEBUFFER) == VIDEO_ENOSCANOUT,
          "leaving HEADLESS without a backend returns ENOSCANOUT, not success");
    CHECK(d.mode == VIDEO_MODE_FRAMEBUFFER, "though the software mode IS recorded");
    CHECK(video_flip(&d, id) == VIDEO_ENOSCANOUT,
          "and a flip now reports that nothing was presented");
    CHECK(d.stat_page_flips == 2,
          "while still counting the page swap that genuinely happened");

    /* --- with a backend --- */
    video_ops_t ops;
    memset(&ops, 0, sizeof ops);
    ops.flip = fake_flip; ops.vsync_wait = fake_vsync;
    ops.set_mode = fake_setmode; ops.probe = fake_probe;
    ops.ctx = &hw;
    CHECK(video_bind_ops(&d, &ops) == VIDEO_OK, "a real backend binds");
    CHECK(video_has_backend(&d), "and is reported bound");

    CHECK(video_flip(&d, id) == VIDEO_OK, "now a flip succeeds");
    CHECK(hw.flip_calls == 1 && hw.last_front == d.displays[0].framebuffer &&
          hw.last_pitch == 32,
          "the backend was handed the NEW front page and the pitch");
    CHECK(d.stat_scanout_flips == 1 && d.stat_page_flips == 3,
          "one scanout flip is now counted, out of three page flips");
    CHECK(d.displays[0].scanout_live, "and scanout_live is finally true");

    hw.flip_rc = -5;
    CHECK(video_flip(&d, id) == VIDEO_EBACKEND, "a backend failure is propagated");
    CHECK(d.stat_scanout_flips == 1, "and does NOT increment the scanout counter");
    CHECK(d.stat_page_flips == 4, "though the page swap still happened and is counted");
    CHECK(!d.displays[0].scanout_live, "scanout_live drops back to false");

    hw.flip_rc = 0;
    video_flip(&d, id);
    CHECK(d.displays[0].scanout_live, "a good flip re-establishes the scanout claim");
    video_unbind_ops(&d);
    CHECK(!video_has_backend(&d), "unbinding clears the backend");
    CHECK(!d.displays[0].scanout_live,
          "and withdraws every scanout claim — nothing drives the panel any more");
    CHECK(video_flip(&d, id) == VIDEO_ENOSCANOUT,
          "so flips go back to reporting that nothing was presented");

    /* single-buffered device cannot flip */
    static uint8_t single[8 * 4 * 4];
    video_device_t s;
    video_init(&s, "single");
    uint32_t sid = video_add_display(&s, 8, 4, 32);
    video_set_framebuffer(&s, sid, single, sizeof single);
    CHECK(video_flip(&s, sid) == VIDEO_ENOBUF, "a single-buffered display cannot flip");
    CHECK(s.stat_page_flips == 0, "and nothing is counted");
    CHECK(video_flip(&s, 99) == VIDEO_ENODISPLAY, "flipping an unknown display fails");
    CHECK(video_flip(NULL, 1) == VIDEO_EINVAL, "flipping a NULL device fails");
}

static void test_hardware_gating(void) {
    printf("\n--- the hardware boundary ---\n");
    video_device_t d;
    fake_hw_t hw;
    memset(&hw, 0, sizeof hw);
    video_init(&d, "hw");
    uint32_t id = video_add_display(&d, 8, 4, 32);

    CHECK(video_vsync_wait(&d, id) == VIDEO_ENODEV,
          "vsync_wait with no backend returns ENODEV — it never fakes a retrace");
    CHECK(d.stat_vsync_waits == 0, "and counts no wait");
    CHECK(video_vsync_wait(&d, 99) == VIDEO_ENODISPLAY, "unknown display fails first");

    video_ops_t empty;
    memset(&empty, 0, sizeof empty);
    empty.ctx = &hw;
    CHECK(video_bind_ops(&d, &empty) == VIDEO_EINVAL,
          "an ops struct with no callbacks is NOT a backend and is refused");
    CHECK(!video_has_backend(&d), "so nothing got bound");
    CHECK(video_bind_ops(&d, NULL) == VIDEO_EINVAL, "a NULL ops pointer is refused");
    CHECK(video_bind_ops(NULL, &empty) == VIDEO_EINVAL, "a NULL device is refused");
    CHECK(video_vsync_wait(&d, id) == VIDEO_ENODEV,
          "and vsync_wait still refuses after the failed bind");

    video_ops_t ops;
    memset(&ops, 0, sizeof ops);
    ops.vsync_wait = fake_vsync; ops.set_mode = fake_setmode; ops.ctx = &hw;
    CHECK(video_bind_ops(&d, &ops) == VIDEO_OK, "a partial backend (vsync + modeset) binds");
    CHECK(video_vsync_wait(&d, id) == VIDEO_OK, "vsync_wait now succeeds");
    CHECK(hw.vsync_calls == 1 && d.stat_vsync_waits == 1,
          "the backend was called once and one wait is counted");
    CHECK(d.displays[0].vsync, "and the display records the retrace");

    hw.vsync_rc = -1;
    CHECK(video_vsync_wait(&d, id) == VIDEO_EBACKEND, "a failing retrace is propagated");
    CHECK(d.stat_vsync_waits == 1, "and is NOT counted as a satisfied wait");

    /* this backend has no flip callback, so flips still cannot be presented */
    static uint8_t buf[8 * 4 * 4 * 2];
    video_set_framebuffer(&d, id, buf, sizeof buf);
    video_set_mode(&d, VIDEO_MODE_VBE);
    CHECK(video_flip(&d, id) == VIDEO_ENOSCANOUT,
          "a backend without a flip callback still cannot present a page");

    CHECK(video_set_mode(&d, VIDEO_MODE_VBE) == VIDEO_OK && hw.mode_calls == 2,
          "set_mode reaches the backend when one is bound");
    hw.mode_rc = -1;
    CHECK(video_set_mode(&d, VIDEO_MODE_VBE) == VIDEO_EBACKEND,
          "and a backend modeset failure is propagated");
    CHECK(!d.displays[0].scanout_live, "a failed modeset leaves scanout_live false");
    CHECK(video_set_mode(&d, (video_mode_t)77) == VIDEO_EINVAL, "an invalid mode is refused");
    CHECK(video_set_mode(NULL, VIDEO_MODE_VBE) == VIDEO_EINVAL, "a NULL device is refused");
}

static void test_text_mode_is_inert(void) {
    printf("\n--- text mode disables the rasteriser ---\n");
    fixture_reset();
    video_set_mode(&g_dev, VIDEO_MODE_TEXT_80x25);
    uint64_t w0 = g_dev.stat_pixels_written;
    video_put_pixel(&g_dev, 1, 1, RED);
    video_fill_rect(&g_dev, 0, 0, 4, 4, RED);
    video_draw_line(&g_dev, 0, 0, 9, 3, RED);
    video_draw_circle(&g_dev, 8, 8, 3, RED);
    video_draw_text(&g_dev, 0, 0, "A", RED);
    video_blit(&g_dev, 8, 0, 0, 0, 4, 4, BLIT_COPY);
    CHECK(g_dev.stat_pixels_written == w0,
          "in VIDEO_MODE_TEXT_80x25 every raster op writes zero pixels");
    int zero = 1;
    for (unsigned i = 0; i < sizeof g_fb; i++) if (g_fb[i]) zero = 0;
    CHECK(zero, "the character-cell buffer is left byte-for-byte untouched");

    /* Reading is inert too. A character-cell array has no pixels in it, so
     * reporting one would be fiction — the same fiction the writer refuses. */
    uint32_t v = 0xDEADBEEFu;
    CHECK(!video_read_pixel(&g_dev, 1, 1, 1, &v) && v == 0xDEADBEEFu,
          "video_read_pixel also refuses in text mode, and leaves *out alone");

    video_set_mode(&g_dev, VIDEO_MODE_HEADLESS);
    video_put_pixel(&g_dev, 1, 1, RED);
    CHECK(P(1,1) == RED, "leaving text mode restores drawing");
}

static void test_irq(void) {
    printf("\n--- IRQ handling counts only what happened ---\n");
    video_device_t d;
    fake_hw_t hw;
    memset(&hw, 0, sizeof hw);
    video_init(&d, "irq");
    uint32_t id = video_add_display(&d, 8, 4, 32);
    (void)id;

    video_handle_irq(&d);
    CHECK(d.stat_irq_vsync == 0 && d.stat_irq_flip == 0 && d.stat_irq_hotplug == 0,
          "an IRQ with no flags set increments nothing (a lie-free counter)");

    d.irq_vsync = true;
    video_handle_irq(&d);
    CHECK(d.stat_irq_vsync == 1 && !d.irq_vsync, "a vsync latch is consumed and counted");
    CHECK(d.displays[0].vsync, "and recorded on the primary display");

    d.irq_flip_done = true;
    video_handle_irq(&d);
    CHECK(d.stat_irq_flip == 1 && !d.irq_flip_done, "a flip-done latch is consumed");
    CHECK(!d.displays[0].scanout_live,
          "but with no backend bound it still does not claim a live scanout");

    /* hotplug with a probe backend */
    video_ops_t ops;
    memset(&ops, 0, sizeof ops);
    ops.probe = fake_probe; ops.ctx = &hw;
    hw.probe_w = 4; hw.probe_h = 2;
    video_bind_ops(&d, &ops);
    d.irq_display_change = true;
    video_handle_irq(&d);
    CHECK(d.stat_irq_hotplug == 1 && hw.probe_calls == 1, "a hotplug re-probes the connector");
    CHECK(d.displays[0].width == 4 && d.displays[0].height == 2 && d.displays[0].pitch == 16,
          "and the new geometry is adopted");

    hw.probe_rc = -1;
    d.irq_display_change = true;
    video_handle_irq(&d);
    CHECK(!d.displays[0].active,
          "a probe failure marks the connector gone rather than pretending it is there");
    uint64_t w0 = d.stat_pixels_written;
    video_put_pixel(&d, 0, 0, RED);
    CHECK(d.stat_pixels_written == w0, "and drawing to an inactive display writes nothing");

    /* video_handle_irq(NULL) must not fault AND must not touch a real device:
     * assert on state, not on having reached the next line. */
    uint64_t s_v = d.stat_irq_vsync, s_f = d.stat_irq_flip, s_h = d.stat_irq_hotplug;
    video_handle_irq(NULL);
    CHECK(d.stat_irq_vsync == s_v && d.stat_irq_flip == s_f &&
          d.stat_irq_hotplug == s_h,
          "video_handle_irq(NULL) is safe and advances no counter anywhere");
}

static void test_coverage_can_fail(void) {
    printf("\n--- coverage: a check that can actually fail ---\n");
    static uint8_t b1[8 * 4 * 4];
    static uint8_t b2[4 * 4 * 4];
    video_device_t d;
    video_init(&d, "cov");

    CHECK(!video_verify_coverage(&d),
          "FAIL 1: a device with no displays covers nothing");
    CHECK(d.coverage_r == 0.0, "coverage_r is 0.0, not a placeholder");

    uint32_t a = video_add_display(&d, 8, 4, 32);
    CHECK(!video_verify_coverage(&d),
          "FAIL 2: a display with no framebuffer bound does not pass");
    CHECK(d.coverage_r == 0.0, "coverage_r stays 0/1");

    video_set_framebuffer(&d, a, b1, sizeof b1);
    CHECK(video_verify_coverage(&d), "PASS: one fully configured display passes");
    CHECK(d.coverage_r == 1.0 && d.coverage_l == 1.0,
          "with r = 1.0 and l = 1.0 — both computed, neither assumed");

    uint32_t b = video_add_display(&d, 4, 4, 32);
    CHECK(!video_verify_coverage(&d),
          "FAIL 3: adding a second display with no buffer drops coverage");
    CHECK(d.coverage_r == 0.5, "coverage_r is exactly 1/2");

    video_set_framebuffer(&d, b, b2, sizeof b2);
    CHECK(video_verify_coverage(&d), "PASS again once the second buffer is bound");

    uint32_t saved_pitch = d.displays[0].pitch;
    d.displays[0].pitch = 1;                       /* a pitch that cannot hold a row */
    CHECK(!video_verify_coverage(&d),
          "FAIL 4: a pitch too small for one scanline is caught");
    d.displays[0].pitch = saved_pitch;

    uint32_t saved_size = d.displays[0].fb_size;
    d.displays[0].fb_size = saved_size - 1;        /* one byte short of a page */
    CHECK(!video_verify_coverage(&d),
          "FAIL 5: a framebuffer one byte short of a page is caught");
    d.displays[0].fb_size = saved_size;

    d.displays[0].double_buffered = true;          /* claimed, with no back page */
    d.displays[0].backbuffer = NULL;
    CHECK(!video_verify_coverage(&d),
          "FAIL 6: a display claiming double buffering with no back page is caught");
    d.displays[0].double_buffered = false;

    d.ctx.display_id = 77;                         /* context points nowhere */
    CHECK(!video_verify_coverage(&d),
          "FAIL 7: a 2D context aimed at a nonexistent display is caught");
    CHECK(d.coverage_l == 0.5, "coverage_l falls to exactly 1/2");
    d.ctx.display_id = a;

    CHECK(video_verify_coverage(&d), "PASS once more after every fault is repaired");
    CHECK(!video_verify_coverage(NULL), "and a NULL device never passes");
}

static void test_hostile_input(void) {
    printf("\n--- hostile input terminates and writes nothing wrong ---\n");
    fixture_reset();
    /* a line 4 billion pixels long must hit the step cap, not hang */
    video_draw_line(&g_dev, -2000000000, 5, 2000000000, 5, RED);
    CHECK(count_non_bg() == 0,
          "a 4-billion-step line off the surface terminates without drawing");

    fixture_reset();
    video_fill_rect(&g_dev, -2000000000, -2000000000, 2000000000, 2000000000, RED);
    CHECK(count_non_bg() == 0, "a rect whose extents overflow int32 draws nothing wrong");

    fixture_reset();
    video_fill_rect(&g_dev, 2000000000, 2000000000, 2000000000, 2000000000, RED);
    CHECK(count_non_bg() == 0, "and neither does one placed past the far corner");

    fixture_reset();
    video_draw_circle(&g_dev, 2000000000, 2000000000, 1000, RED);
    CHECK(count_non_bg() == 0, "a circle centred off in the distance draws nothing");

    fixture_reset();
    video_draw_text(&g_dev, 2000000000, 0, "AAAA", RED);
    CHECK(count_non_bg() == 0, "text placed past int32 range draws nothing");

    /* NULL device on every entry point */
    video_init(NULL, "nul");                 /* arch/arm32 calls this with 0 */
    video_set_color(NULL, 0, 0);
    video_set_clip(NULL, 0, 0, 1, 1);
    video_put_pixel(NULL, 0, 0, 0);
    video_fill_rect(NULL, 0, 0, 1, 1, 0);
    video_draw_line(NULL, 0, 0, 1, 1, 0);
    video_draw_circle(NULL, 0, 0, 1, 0);
    video_draw_text(NULL, 0, 0, "x", 0);
    video_blit(NULL, 0, 0, 0, 0, 1, 1, BLIT_COPY);
    video_unbind_ops(NULL);
    CHECK(video_add_display(NULL, 4, 4, 32) == 0, "video_add_display(NULL) returns 0");
    CHECK(video_get_display(NULL, 1) == NULL, "video_get_display(NULL) returns NULL");
    CHECK(video_get_framebuffer(NULL, 1) == NULL, "video_get_framebuffer(NULL) returns NULL");
    CHECK(video_set_framebuffer(NULL, 1, NULL, 0) == VIDEO_EINVAL, "set_framebuffer(NULL) fails");
    CHECK(video_set_resolution(NULL, 1, 4, 4, 32, PIXEL_XRGB8888) == VIDEO_EINVAL,
          "set_resolution(NULL) fails");
    CHECK(video_vsync_wait(NULL, 1) == VIDEO_EINVAL, "vsync_wait(NULL) fails");
    CHECK(!video_has_backend(NULL), "has_backend(NULL) is false");
    uint32_t junk = 0;
    CHECK(!video_read_pixel(NULL, 1, 0, 0, &junk), "read_pixel(NULL) is false");
    fixture_reset();
    CHECK(!video_read_pixel(&g_dev, 1, -1, 0, &junk) &&
          !video_read_pixel(&g_dev, 1, W, 0, &junk) &&
          !video_read_pixel(&g_dev, 1, 0, H, &junk),
          "read_pixel outside the surface is false, and leaves the output alone");
    CHECK(junk == 0, "the out parameter really was left alone");
}

/* =====================================================================
 * INDEPENDENT ORACLES
 *
 * Everything above asserts hand-derived anchors. The three suites below
 * assert PROPERTIES computed here, from the mathematical definition, with no
 * reference to the implementation's own output — the only way to check a
 * rasteriser over a range too wide to enumerate by hand. All the arithmetic
 * is integer, so the oracle cannot itself drift on a different FPU and the
 * test needs no libm.
 * ===================================================================== */

#define OW 64
#define OH 64
static uint8_t g_ofb[OW * OH * 4];
static video_device_t g_odev;

static void oracle_reset(void) {
    memset(g_ofb, 0, sizeof g_ofb);
    video_init(&g_odev, "oracle");
    video_add_display(&g_odev, OW, OH, 32);
    video_set_framebuffer(&g_odev, 1, g_ofb, sizeof g_ofb);
}
static int olit(int x, int y) {
    uint32_t v = 0;
    if (!video_read_pixel(&g_odev, 1, x, y, &v)) return 0;
    return v == WHITE;
}

/* round(sqrt(v)) with integers only: the unique k >= 0 with
 * (2k-1)^2 <= 4v < (2k+1)^2. */
static long iround_sqrt(long v) {
    long k = 0;
    while ((2 * k + 1) * (2 * k + 1) <= 4 * v) k++;
    return k;
}

static void test_line_oracle(void) {
    printf("\n--- draw_line vs an independently computed ideal segment ---\n");
    /* Every endpoint on a 41x41 lattice around the centre: 1681 segments.
     * Two properties, both from the definition of a rasterised segment:
     *   1. it lays down exactly max(|dx|,|dy|)+1 pixels — one per major step,
     *      no gaps and no doubles;
     *   2. every lit pixel is within half a pixel of the true segment. With
     *      cross = (x2-x1)(y1-y) - (x1-x)(y2-y1), the perpendicular distance
     *      is |cross|/L, so "<= 0.5" is exactly 4*cross^2 <= dx^2+dy^2 —
     *      integer arithmetic, no sqrt, no rounding to argue about. */
    int pairs = 0, bad_count = 0, bad_dist = 0, bad_box = 0;
    const int cx = 32, cy = 32;
    for (int ox = -20; ox <= 20; ox++)
    for (int oy = -20; oy <= 20; oy++) {
        oracle_reset();
        int x1 = cx, y1 = cy, x2 = cx + ox, y2 = cy + oy;
        video_draw_line(&g_odev, x1, y1, x2, y2, WHITE);
        pairs++;
        long dx = ox < 0 ? -ox : ox, dy = oy < 0 ? -oy : oy;
        long expect = (dx > dy ? dx : dy) + 1;
        long n = 0;
        int lo_x = x1 < x2 ? x1 : x2, hi_x = x1 < x2 ? x2 : x1;
        int lo_y = y1 < y2 ? y1 : y2, hi_y = y1 < y2 ? y2 : y1;
        for (int y = 0; y < OH; y++)
        for (int x = 0; x < OW; x++) {
            if (!olit(x, y)) continue;
            n++;
            if (x < lo_x || x > hi_x || y < lo_y || y > hi_y) bad_box++;
            long cross = (long)(x2 - x1) * (y1 - y) - (long)(x1 - x) * (y2 - y1);
            if (4 * cross * cross > dx * dx + dy * dy) bad_dist++;
        }
        if (n != expect) bad_count++;
    }
    CHECK(pairs == 1681, "1681 endpoint pairs were rasterised (41x41 lattice)");
    CHECK(bad_count == 0,
          "every one lays down exactly max(|dx|,|dy|)+1 pixels — no gaps, no doubles");
    CHECK(bad_dist == 0,
          "every lit pixel is within 0.5 of the ideal segment (4*cross^2 <= dx^2+dy^2)");
    CHECK(bad_box == 0, "and no lit pixel escapes the endpoints' bounding box");
}

static void test_circle_oracle(void) {
    printf("\n--- draw_circle vs round(sqrt(r^2 - y^2)), computed here ---\n");
    /* The midpoint circle's defining property, checked against an integer
     * round(sqrt()) written in this file: in the first octant the lit column
     * for row y is exactly round(sqrt(r^2 - y^2)). Plus the global property
     * that EVERY lit pixel sits within half a pixel of the true circle:
     *   |d - r| <= 0.5  <=>  (2r-1)^2 <= 4*(dx^2+dy^2) <= (2r+1)^2. */
    int missing = 0, offcircle = 0, samples = 0, drawn = 0;
    for (int r = 1; r <= 28; r++) {
        oracle_reset();
        video_draw_circle(&g_odev, 32, 32, r, WHITE);
        for (long y = 0; 2 * y * y <= (long)r * r; y++) {
            long ex = iround_sqrt((long)r * r - y * y);
            samples++;
            /* all eight symmetric images of the octant sample must be lit */
            if (!olit(32 + (int)ex, 32 + (int)y) || !olit(32 - (int)ex, 32 + (int)y) ||
                !olit(32 + (int)ex, 32 - (int)y) || !olit(32 - (int)ex, 32 - (int)y) ||
                !olit(32 + (int)y, 32 + (int)ex) || !olit(32 - (int)y, 32 + (int)ex) ||
                !olit(32 + (int)y, 32 - (int)ex) || !olit(32 - (int)y, 32 - (int)ex))
                missing++;
        }
        for (int y = 0; y < OH; y++)
        for (int x = 0; x < OW; x++) {
            if (!olit(x, y)) continue;
            drawn++;
            long dx = x - 32, dy = y - 32;
            long d4 = 4 * (dx * dx + dy * dy);
            if (d4 < (long)(2 * r - 1) * (2 * r - 1) ||
                d4 > (long)(2 * r + 1) * (2 * r + 1)) offcircle++;
        }
    }
    /* sum over r=1..28 of (floor(r/sqrt2) + 1) = 301 octant rows */
    CHECK(samples == 301 && drawn > 2000,
          "r = 1..28 produced 301 octant samples and over 2000 lit pixels");
    CHECK(missing == 0,
          "every octant sample and all eight of its mirrors are lit — the shape "
          "is round(sqrt(r^2-y^2)) with 8-way symmetry");
    CHECK(offcircle == 0,
          "and NO lit pixel is further than half a pixel from the true circle");
}

static void test_format_roundtrip_exhaustive(void) {
    printf("\n--- pack(unpack(raw)) == raw for EVERY raw value ---\n");
    /* The header claims this for all raw values, not for the one sample per
     * format tested above. A blit converts to canonical ARGB and back on
     * every pixel, so a single non-fixed raw value would make repeated blits
     * bleach the screen. Drive it through the public API only: poke raw bytes
     * into the framebuffer, read_pixel to canonicalise, put_pixel to re-pack,
     * compare the bytes. */
    struct { uint32_t bpp; pixel_format_t fmt; uint32_t nbytes; uint32_t count; }
    tv[] = {
        { 8,  PIXEL_RGB332,   1, 256u    },
        { 16, PIXEL_RGB565,   2, 65536u  },
        { 24, PIXEL_RGB888,   3, 262144u },   /* every 64th value of 2^24 */
        { 32, PIXEL_XRGB8888, 4, 262144u },
        { 32, PIXEL_ARGB8888, 4, 262144u },
    };
    static const char *nm[] = {"RGB332","RGB565","RGB888","XRGB8888","ARGB8888"};
    for (unsigned t = 0; t < sizeof tv / sizeof tv[0]; t++) {
        uint8_t buf[8];
        video_device_t d;
        video_init(&d, "rt");
        uint32_t id = video_add_display(&d, 2, 1, tv[t].bpp);
        video_set_resolution(&d, id, 2, 1, tv[t].bpp, tv[t].fmt);
        video_set_framebuffer(&d, id, buf, 2 * tv[t].nbytes);
        uint32_t stride = (tv[t].nbytes >= 3) ? 64u : 1u;   /* sample 24/32bpp */
        uint32_t bad = 0, seen = 0;
        for (uint32_t k = 0; k < tv[t].count; k++) {
            uint32_t raw = k * stride;
            memset(buf, 0, sizeof buf);
            for (uint32_t i = 0; i < tv[t].nbytes; i++)
                buf[i] = (uint8_t)(raw >> (8u * i));
            uint32_t canon = 0;
            if (!video_read_pixel(&d, id, 0, 0, &canon)) { bad++; continue; }
            video_put_pixel(&d, 1, 0, canon);
            for (uint32_t i = 0; i < tv[t].nbytes; i++)
                if (buf[tv[t].nbytes + i] != (uint8_t)(raw >> (8u * i))) { bad++; break; }
            seen++;
        }
        char msg[160];
        snprintf(msg, sizeof msg,
                 "%s: %u raw values round-trip byte-identically (%u drifted)",
                 nm[t], seen, bad);
        CHECK(bad == 0 && seen == tv[t].count, msg);
    }
}

static void test_blend_arithmetic_sweep(void) {
    printf("\n--- blend arithmetic across all 256 alphas ---\n");
    /* The formula, recomputed here: (s*a + d*(255-a) + 127) / 255, per
     * channel, with a == 0 keeping the destination and a == 255 replacing it.
     * Sweep every alpha against three channel pairs so a truncating blend, an
     * unrounded blend or a /256 blend all fail. */
    static const uint32_t src[3] = { 0xFF030507u, 0xFFFF0000u, 0xFF10D0A0u };
    static const uint32_t dst[3] = { 0xFF000000u, 0xFFFFFFFFu, 0xFF804020u };
    int mismatches = 0, cases = 0;
    for (int p = 0; p < 3; p++)
    for (int a = 0; a <= 255; a++) {
        fixture_reset();
        video_fill_rect(&g_dev, 0, 0, 1, 1, dst[p]);
        g_dev.ctx.alpha_blending = true;
        g_dev.ctx.alpha = (uint8_t)a;
        video_put_pixel(&g_dev, 0, 0, src[p]);
        uint32_t want;
        if (a == 0) want = dst[p];
        else if (a == 255) want = 0xFF000000u | (src[p] & 0x00FFFFFFu);
        else {
            uint32_t ia = 255u - (uint32_t)a, ch[3];
            for (int c = 0; c < 3; c++) {
                uint32_t sh = (uint32_t)(16 - 8 * c);
                uint32_t sc = (src[p] >> sh) & 0xFFu, dc = (dst[p] >> sh) & 0xFFu;
                ch[c] = (sc * (uint32_t)a + dc * ia + 127u) / 255u;
            }
            want = 0xFF000000u | (ch[0] << 16) | (ch[1] << 8) | ch[2];
        }
        cases++;
        if (P(0, 0) != want) mismatches++;
    }
    CHECK(cases == 768, "768 (src,dst,alpha) blend cases were exercised");
    CHECK(mismatches == 0,
          "every one matches (s*a + d*(255-a) + 127)/255 recomputed here — "
          "truncation, /256 or an unrounded divide would all fail");
}

static void test_font_table_integrity(void) {
    printf("\n--- font table: 95 glyphs, all distinct, none a stand-in ---\n");
    /* An off-by-N of ANY size (not just the vbe.c 32) collapses or permutes
     * the table. Rendering every printable byte and comparing bitmaps catches
     * every such shift: if two characters share a bitmap, one of them is
     * drawing the other's glyph. */
    static uint64_t bits[128];
    for (int c = 32; c <= 126; c++) {
        fixture_reset();
        char s[2]; s[0] = (char)c; s[1] = 0;
        video_draw_text(&g_dev, 0, 0, s, RED);
        uint64_t m = 0;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 8; x++)
                if (P(x, y) == RED) m ^= (0x9E3779B97F4A7C15ull * (uint64_t)(y * 8 + x + 1));
        bits[c] = m;
    }
    fixture_reset();
    video_draw_text(&g_dev, 0, 0, "\x01", RED);
    uint64_t notdef = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 8; x++)
            if (P(x, y) == RED) notdef ^= (0x9E3779B97F4A7C15ull * (uint64_t)(y * 8 + x + 1));

    int dup = 0, blank = 0, is_notdef = 0;
    for (int a = 32; a <= 126; a++) {
        if (bits[a] == 0) blank++;
        if (bits[a] == notdef) is_notdef++;
        for (int b = a + 1; b <= 126; b++) if (bits[a] == bits[b]) dup++;
    }
    CHECK(dup == 0,
          "all 95 printable glyphs render DIFFERENT bitmaps — no shift of any "
          "size can make two characters share ink");
    CHECK(blank == 1, "exactly one printable glyph is blank, and it is space");
    CHECK(bits[' '] == 0, "…confirmed: the blank one is ASCII 32");
    CHECK(is_notdef == 0 && notdef != 0,
          "the .notdef box is non-empty and no printable character renders it");
}

static void test_text_control_bytes(void) {
    printf("\n--- draw_text: control bytes, tabs, caps, every byte value ---\n");
    /* 'A' = 3C,66,66,7E,66,66,66 -> 4+4+4+6+4+4+4 = 30 lit pixels. Every
     * count below is that 30, multiplied by how many glyphs actually land. */
    fixture_reset();
    video_draw_text(&g_dev, 0, 0, "A", RED);
    CHECK(g_dev.stat_pixels_written == 30, "one 'A' lights exactly 30 pixels");

    fixture_reset();
    video_draw_text(&g_dev, 0, 0, "A\rA", RED);
    CHECK(P(2,2) == RED && P(10,2) == BG,
          "'\\r' returns to the start column: the second 'A' overprints the first");
    CHECK(g_dev.stat_pixels_written == 60,
          "…and both glyphs really were drawn — 60 writes into the same 30 pixels");

    {   /* \t needs a surface wider than four cells */
        static uint8_t wide[64 * 16 * 4];
        video_device_t t;
        memset(wide, 0, sizeof wide);
        video_init(&t, "tab");
        uint32_t tid = video_add_display(&t, 64, 16, 32);
        video_set_framebuffer(&t, tid, wide, sizeof wide);
        video_draw_text(&t, 0, 0, "\tA", RED);
        uint32_t at32 = 0, at0 = 0;
        video_read_pixel(&t, tid, 34, 2, &at32);
        video_read_pixel(&t, tid, 2, 2, &at0);
        CHECK(at32 == RED && at0 == BG,
              "'\\t' advances exactly 4 cells (32 px) before the glyph");
        CHECK(t.stat_pixels_written == 30, "and the tab itself lights nothing");
    }

    /* The VIDEO_TEXT_MAX_CHARS cap. Every character between the two 'A's is a
     * carriage return, so BOTH would land on the same 30 pixels: 30 writes if
     * the cap stops the walk at index 4095, 60 if it does not. Nothing else
     * in the fixture can produce that difference. */
    {
        static char longstr[VIDEO_TEXT_MAX_CHARS + 3];
        memset(longstr, '\r', sizeof longstr);
        longstr[0] = 'A';
        longstr[VIDEO_TEXT_MAX_CHARS + 1] = 'A';    /* index 4097, past the cap */
        longstr[VIDEO_TEXT_MAX_CHARS + 2] = 0;
        fixture_reset();
        video_draw_text(&g_dev, 0, 0, longstr, RED);
        CHECK(g_dev.stat_pixels_written == 30,
              "a 4098-character string stops at VIDEO_TEXT_MAX_CHARS: the "
              "second 'A' at index 4097 is never rasterised");
    }

    /* Every byte value 0..255 as a one-character string. Nothing outside
     * 32..126 may produce anything but the .notdef box, and nothing may index
     * past the 96-entry table (ASan is the second half of this assertion). */
    {
        int notdef_hits = 0, printable_hits = 0, movers = 0, empty = 0, other = 0;
        fixture_reset();
        video_draw_text(&g_dev, 0, 0, "\x01", RED);
        int notdef_count = count_non_bg();
        for (int c = 0; c < 256; c++) {
            char s[2]; s[0] = (char)c; s[1] = 0;
            fixture_reset();
            video_draw_text(&g_dev, 0, 0, s, RED);
            int n = count_non_bg();
            if (c == 0)                          { if (n == 0) empty++; else other++; }
            else if (c >= 32 && c <= 126)        { if (n >= 0) printable_hits++; }
            else if (c == '\n' || c == '\r' || c == '\t')
                                                 { if (n == 0) movers++; else other++; }
            else if (n == notdef_count)          notdef_hits++;
            else                                 other++;
        }
        CHECK(notdef_count == 22,
              ".notdef is a 22-pixel hollow box (7E + five 42s + 7E = 6+5*2+6)");
        CHECK(printable_hits == 95 && empty == 1 && movers == 3,
              "95 printable bytes, one empty string, three cursor movers");
        CHECK(notdef_hits == 157 && other == 0,
              "and all 157 remaining byte values render exactly .notdef — none "
              "indexes past the 96-entry table");
    }

    /* One string containing every byte value. On a 32-wide surface only the
     * first four cells land, and all four bytes here (7F,01,02,03) are
     * non-printable, so the total is exactly four .notdef boxes. */
    {
        static char every[257];
        for (int i = 0; i < 256; i++) every[i] = (char)(i ? i : 0x7F);
        every[256] = 0;
        fixture_reset();
        video_draw_text(&g_dev, 0, 0, every, RED);
        CHECK(count_non_bg() == 4 * 22,
              "a 256-byte string of every byte value draws exactly the four "
              ".notdef boxes that fit, and nothing outside the surface");
    }
}

/* =====================================================================
 * MEMORY SAFETY AND THE HARDWARE-HONESTY BOUNDARY, second pass
 * ===================================================================== */

static void test_untrusted_num_displays(void) {
    printf("\n--- dev->num_displays is untrusted by EVERY loop ---\n");
    /* num_displays is a plain public field. Two loops (video_unbind_ops and
     * video_set_mode) once walked it raw and wrote scanout_live past
     * displays[3]; ASan called both heap-buffer-overflows. The canary below
     * catches it even in a build without a sanitiser. */
    struct { video_device_t dev; uint8_t canary[256]; } box;
    memset(&box, 0, sizeof box);
    memset(box.canary, 0xAB, sizeof box.canary);
    video_init(&box.dev, "untrusted");
    uint32_t id = video_add_display(&box.dev, 8, 4, 32);
    static uint8_t buf[8 * 4 * 4];
    video_set_framebuffer(&box.dev, id, buf, sizeof buf);

    box.dev.num_displays = 4096;                 /* hostile / corrupt count */
    video_unbind_ops(&box.dev);
    video_set_mode(&box.dev, VIDEO_MODE_HEADLESS);
    video_handle_irq(&box.dev);
    video_verify_coverage(&box.dev);
    (void)video_get_display(&box.dev, 3);
    (void)video_add_display(&box.dev, 4, 4, 32); /* must refuse: count is full */

    int intact = 1;
    for (unsigned i = 0; i < sizeof box.canary; i++)
        if (box.canary[i] != 0xAB) intact = 0;
    CHECK(intact,
          "with num_displays = 4096, not one byte past the display array was "
          "written — every loop clamps to VIDEO_MAX_DISPLAYS");
    CHECK(video_add_display(&box.dev, 4, 4, 32) == 0,
          "and add_display refuses to grow a count that is already over the cap");
}

static void test_blit_extent_bounds(void) {
    printf("\n--- blit bounds BOTH extents, not just the width ---\n");
    fixture_reset();
    video_fill_rect(&g_dev, 0, 0, 2, 2, RED);
    uint64_t w0 = g_dev.stat_pixels_written;

    /* h was unbounded: BLIT_SCALED computed h*2 in int32, which UBSan reports
     * as signed overflow for h >= 0x40000000, and every op looped O(w*h) —
     * one call with h = INT32_MAX spun for hours. */
    video_blit(&g_dev, 8, 0, 0, 0, 2, 0x40000000, BLIT_SCALED);
    video_blit(&g_dev, 8, 0, 0, 0, 2, 0x7FFFFFFF, BLIT_COPY);
    video_blit(&g_dev, 8, 0, 0, 0, 2, 0x7FFFFFFF, BLIT_ROTATED);
    video_blit(&g_dev, 8, 0, 0, 0, 2, 0x7FFFFFFF, BLIT_ALPHA);
    video_blit(&g_dev, 8, 0, 0, 0, 2, VIDEO_BLIT_MAX_H + 1, BLIT_FLIPPED);
    CHECK(g_dev.stat_pixels_written == w0,
          "every op refuses h > VIDEO_BLIT_MAX_H outright, drawing nothing");

    /* the boundary values themselves must still work */
    video_blit(&g_dev, 8, 0, 0, 0, 2, VIDEO_BLIT_MAX_H, BLIT_COPY);
    CHECK(g_dev.stat_pixels_written > w0 && P(8,0) == RED && P(9,1) == RED,
          "h == VIDEO_BLIT_MAX_H is accepted and copies the visible rows");
    w0 = g_dev.stat_pixels_written;
    video_blit(&g_dev, 0, 8, 0, 0, VIDEO_BLIT_MAX_W, 2, BLIT_COPY);
    CHECK(g_dev.stat_pixels_written > w0,
          "w == VIDEO_BLIT_MAX_W is accepted (the row buffer is exactly that wide)");
}

static void test_hard_geometry_ceiling(void) {
    printf("\n--- the geometry ceiling a caller cannot raise ---\n");
    video_device_t d;
    video_init(&d, "ceiling");
    /* max_width/max_height are public fields; raising them must NOT let a
     * caller pick a width whose pitch*height overflows 32 bits. */
    d.max_width = 0xFFFFFFFFu;
    d.max_height = 0xFFFFFFFFu;
    CHECK(video_add_display(&d, VIDEO_HARD_MAX_DIM + 1, 4, 32) == 0,
          "add_display still refuses w > VIDEO_HARD_MAX_DIM after max_width is raised");
    CHECK(video_add_display(&d, 4, VIDEO_HARD_MAX_DIM + 1, 32) == 0,
          "and h > VIDEO_HARD_MAX_DIM");
    CHECK(video_add_display(&d, 0x40000000u, 0x40000000u, 32) == 0,
          "a 1G x 1G display, whose pitch alone would overflow, is refused");
    CHECK(d.num_displays == 0, "none of the three was recorded");

    uint32_t id = video_add_display(&d, 8, 4, 32);
    CHECK(id == 1, "a sane geometry still works with the ceiling raised");
    CHECK(video_set_resolution(&d, id, VIDEO_HARD_MAX_DIM + 1, 4, 32,
                               PIXEL_XRGB8888) == VIDEO_EINVAL,
          "set_resolution enforces the hard ceiling too");
    CHECK(d.displays[0].width == 8, "and the old geometry survives");
}

static void test_set_mode_needs_a_display(void) {
    printf("\n--- set_mode cannot succeed with nothing to program ---\n");
    fake_hw_t hw;
    memset(&hw, 0, sizeof hw);
    video_ops_t ops;
    memset(&ops, 0, sizeof ops);
    ops.set_mode = fake_setmode; ops.ctx = &hw;

    video_device_t d;
    video_init(&d, "nodisp");
    CHECK(video_set_mode(&d, VIDEO_MODE_VBE) == VIDEO_ENODISPLAY,
          "VBE with no displays and no backend is ENODISPLAY");
    CHECK(video_bind_ops(&d, &ops) == VIDEO_OK, "bind a real modesetting backend");
    CHECK(video_set_mode(&d, VIDEO_MODE_VBE) == VIDEO_ENODISPLAY,
          "and a BOUND backend does not turn 'nothing to program' into VIDEO_OK");
    CHECK(hw.mode_calls == 0, "the backend was never called — there was no display");
    CHECK(video_set_mode(&d, VIDEO_MODE_HEADLESS) == VIDEO_OK,
          "HEADLESS with no displays is genuinely complete, so it is VIDEO_OK");
    CHECK(d.mode == VIDEO_MODE_HEADLESS, "and the software mode is recorded");

    video_add_display(&d, 8, 4, 32);
    CHECK(video_set_mode(&d, VIDEO_MODE_VBE) == VIDEO_OK && hw.mode_calls == 1,
          "once a display exists the backend is called and VIDEO_OK is earned");
}

static void test_inactive_display_refusals(void) {
    printf("\n--- an inactive connector cannot be flipped, waited on or read ---\n");
    static uint8_t buf[8 * 4 * 4 * 2];
    fake_hw_t hw;
    video_device_t d;
    memset(buf, 0, sizeof buf);
    memset(&hw, 0, sizeof hw);
    video_init(&d, "gone");
    uint32_t id = video_add_display(&d, 8, 4, 32);
    video_set_framebuffer(&d, id, buf, sizeof buf);
    video_ops_t ops;
    memset(&ops, 0, sizeof ops);
    ops.flip = fake_flip; ops.vsync_wait = fake_vsync; ops.ctx = &hw;
    video_bind_ops(&d, &ops);

    CHECK(video_flip(&d, id) == VIDEO_OK && d.displays[0].scanout_live,
          "while the connector is present, a flip is acknowledged and live");

    d.displays[0].active = false;          /* as a failed probe would leave it */
    d.displays[0].scanout_live = false;
    CHECK(video_flip(&d, id) == VIDEO_ENODISPLAY,
          "once it is gone, flip refuses — a stale ack cannot re-claim scanout");
    CHECK(!d.displays[0].scanout_live && hw.flip_calls == 1,
          "the backend was not called again and scanout_live stays false");
    CHECK(d.stat_page_flips == 1, "and no phantom page flip was counted");
    CHECK(video_vsync_wait(&d, id) == VIDEO_ENODISPLAY,
          "vsync_wait refuses a gone connector before it reaches the backend");
    CHECK(hw.vsync_calls == 0 && d.stat_vsync_waits == 0, "so nothing is counted");
    uint32_t v = 0;
    CHECK(!video_read_pixel(&d, id, 0, 0, &v),
          "and its pixels cannot be read back either");
}

static void test_irq_flip_claim_needs_a_flip_path(void) {
    printf("\n--- irq_flip_done alone does not prove a scanout ---\n");
    static uint8_t buf[8 * 4 * 4 * 2];
    fake_hw_t hw;
    video_device_t d;
    memset(buf, 0, sizeof buf);
    memset(&hw, 0, sizeof hw);
    video_init(&d, "irqclaim");
    uint32_t id = video_add_display(&d, 8, 4, 32);
    video_set_framebuffer(&d, id, buf, sizeof buf);

    /* a backend that can probe but has NO flip path: this file has never
     * handed a page to anything, so a flip-done latch proves nothing */
    video_ops_t probe_only;
    memset(&probe_only, 0, sizeof probe_only);
    probe_only.probe = fake_probe; probe_only.ctx = &hw;
    CHECK(video_bind_ops(&d, &probe_only) == VIDEO_OK, "a probe-only backend binds");
    d.irq_flip_done = true;
    video_handle_irq(&d);
    CHECK(d.stat_irq_flip == 1, "the latch is still consumed and counted");
    CHECK(!d.displays[0].scanout_live,
          "but scanout_live stays FALSE — no flip callback ever presented a page");

    /* now with a real flip path */
    video_ops_t full;
    memset(&full, 0, sizeof full);
    full.flip = fake_flip; full.probe = fake_probe; full.ctx = &hw;
    video_bind_ops(&d, &full);
    d.irq_flip_done = true;
    video_handle_irq(&d);
    CHECK(d.displays[0].scanout_live, "with a flip path bound, the latch does claim it");

    d.displays[0].active = false;
    d.displays[0].scanout_live = false;
    d.irq_flip_done = true;
    video_handle_irq(&d);
    CHECK(d.stat_irq_flip == 3 && !d.displays[0].scanout_live,
          "and never for a connector the driver already reported gone");
}

static void test_counter_invariants(void) {
    printf("\n--- the counters keep their stated relationships ---\n");
    static uint8_t buf[8 * 4 * 4 * 2];
    fake_hw_t hw;
    video_device_t d;
    memset(buf, 0, sizeof buf);
    memset(&hw, 0, sizeof hw);
    video_init(&d, "counters");
    uint32_t id = video_add_display(&d, 8, 4, 32);
    video_set_framebuffer(&d, id, buf, sizeof buf);
    video_ops_t ops;
    memset(&ops, 0, sizeof ops);
    ops.flip = fake_flip; ops.vsync_wait = fake_vsync; ops.ctx = &hw;

    video_flip(&d, id);                       /* headless, no backend */
    video_flip(&d, id);
    video_bind_ops(&d, &ops);
    video_flip(&d, id);                       /* acked */
    hw.flip_rc = -1;
    video_flip(&d, id);                       /* swapped, not acked */
    hw.flip_rc = 0;
    video_flip(&d, id);                       /* acked */
    hw.vsync_rc = -1;
    video_vsync_wait(&d, id);
    hw.vsync_rc = 0;
    video_vsync_wait(&d, id);

    CHECK(d.stat_page_flips == 5, "five page swaps really happened");
    CHECK(d.stat_scanout_flips == 2, "exactly two were acknowledged by a backend");
    CHECK(d.stat_scanout_flips < d.stat_page_flips,
          "scanout flips are a STRICT subset of page flips, as the header says");
    CHECK((int)d.stat_scanout_flips == hw.flip_calls - 1,
          "…and equal the backend calls that returned success (3 calls, 1 failed)");
    CHECK(d.stat_vsync_waits == 1 && hw.vsync_calls == 2,
          "one of two retrace attempts succeeded, and only that one is counted");
}

static void test_coverage_format_mismatch(void) {
    printf("\n--- coverage catches a bpp that disagrees with the format ---\n");
    static uint8_t b[8 * 4 * 4];
    video_device_t d;
    video_init(&d, "cov2");
    uint32_t a = video_add_display(&d, 8, 4, 32);
    video_set_framebuffer(&d, a, b, sizeof b);
    CHECK(video_verify_coverage(&d), "the configured display passes");

    d.displays[0].bpp = 16;                    /* claims 16bpp, stores XRGB8888 */
    CHECK(!video_verify_coverage(&d),
          "FAIL 8: bpp 16 with an XRGB8888 format is caught");
    CHECK(d.coverage_r == 0.0, "coverage_r drops to 0/1");
    d.displays[0].bpp = 32;

    pixel_format_t saved = d.displays[0].format;
    d.displays[0].format = (pixel_format_t)99;
    CHECK(!video_verify_coverage(&d), "FAIL 9: an unknown pixel format is caught");
    d.displays[0].format = saved;

    uint32_t sw = d.displays[0].width;
    d.displays[0].width = d.max_width + 1;
    CHECK(!video_verify_coverage(&d),
          "FAIL 10: a width past the device's own ceiling is caught");
    d.displays[0].width = sw;
    CHECK(video_verify_coverage(&d), "and it passes again once repaired");
}

static void test_init_defaults(void) {
    printf("\n--- init: no capability is claimed that is not delivered ---\n");
    video_device_t d;
    video_init(&d, "zxv-video");
    CHECK(strcmp(d.name, "zxv-video") == 0, "the device name is copied");
    CHECK(d.mode == VIDEO_MODE_HEADLESS,
          "the default mode is HEADLESS — nothing has programmed a display");
    CHECK(d.supports_2d_accel, "2D acceleration is claimed: the software rasteriser");
    CHECK(!d.supports_3d, "3D is NOT claimed — there is no 3D pipeline in this file");
    CHECK(d.dma_buffer == NULL && d.dma_size == 0,
          "no DMA buffer is claimed — blits are CPU copies");
    CHECK(!d.ops_bound, "no hardware backend is bound at init");
    CHECK(d.num_displays == 0 && d.primary_display == 0, "no displays exist yet");
    CHECK(d.max_width == VIDEO_DEFAULT_MAX_W && d.max_height == VIDEO_DEFAULT_MAX_H,
          "the geometry ceiling is set");
    CHECK(d.stat_pixels_written == 0 && d.stat_page_flips == 0 &&
          d.stat_scanout_flips == 0 && d.stat_vsync_waits == 0,
          "every counter starts at zero");
    char big[300];
    memset(big, 'x', sizeof big); big[sizeof big - 1] = 0;
    video_init(&d, big);
    CHECK(strlen(d.name) == sizeof(d.name) - 1,
          "an over-long name is truncated to the field, not overflowed");
}

int main(void) {
    printf("=== ZXV video: software rasteriser, pixel-exact ===\n");
    test_init_defaults();
    test_pixel_formats();
    test_put_pixel_and_bounds();
    test_fill_rect();
    test_draw_line();
    test_draw_circle();
    test_clipping();
    test_font_is_ascii32_indexed();
    test_draw_text_layout();
    test_blit();
    test_alpha();
    test_multi_display();
    test_geometry_and_buffers();
    test_double_buffer_and_flip();
    test_hardware_gating();
    test_text_mode_is_inert();
    test_irq();
    test_coverage_can_fail();
    test_hostile_input();
    /* --- second pass: independent oracles and the audit's own findings --- */
    test_line_oracle();
    test_circle_oracle();
    test_format_roundtrip_exhaustive();
    test_blend_arithmetic_sweep();
    test_font_table_integrity();
    test_text_control_bytes();
    test_untrusted_num_displays();
    test_blit_extent_bounds();
    test_hard_geometry_ceiling();
    test_set_mode_needs_a_display();
    test_inactive_display_refusals();
    test_irq_flip_claim_needs_a_flip_path();
    test_counter_invariants();
    test_coverage_format_mismatch();
    printf("\n%s: %d check(s), %d failure(s)\n",
           failures ? "*** FAILED ***" : "ALL PASS", checks, failures);
    return failures ? 1 : 0;
}

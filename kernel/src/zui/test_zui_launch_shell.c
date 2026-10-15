/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_launch_shell.c — a whole launch, for real (LS1..LS5): the window
 * opens, the app's icon and the five logos are read from "disk" and drawn,
 * the begin, ticks and end are synthesised and reach the audio device as the
 * real load reaches them. */
#include <stdio.h>
#include <string.h>

#include "zui_launch_shell.h"
#include "zui_png_fixture.inc"

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

#define W 1280
#define H 800
static uint32_t PX[W * H];
static zui_surface_t S;
static zui_wm_t WM;
static zui_launch_theme_t LT;
static zui_launch_shell_t LS;
static audio_device_t DEV;
static uint8_t FILEBUF[1 << 16], WORK[1 << 18];

/* ---- the "disk" ------------------------------------------------------------ */
#define NF 16
static struct {
    char path[96];
    uint8_t data[4096];
    uint32_t n;
    int reads;
} disk[NF];
static int ndisk, misses;

static void put_file(const char *path, const uint8_t *d, uint32_t n)
{
    snprintf(disk[ndisk].path, sizeof disk[ndisk].path, "%s", path);
    memcpy(disk[ndisk].data, d, n);
    disk[ndisk].n = n;
    disk[ndisk].reads = 0;
    ndisk++;
}

static int reads_of(const char *path)
{
    for (int i = 0; i < ndisk; i++)
        if (!strcmp(disk[i].path, path)) return disk[i].reads;
    return -1;
}

static int32_t fs_read(void *ctx, const char *path, uint8_t *buf, uint32_t cap)
{
    (void) ctx;
    for (int i = 0; i < ndisk; i++)
        if (!strcmp(disk[i].path, path)) {
            disk[i].reads++;
            if (disk[i].n > cap) return -1;
            memcpy(buf, disk[i].data, disk[i].n);
            return (int32_t) disk[i].n;
        }
    misses++;
    return -1;
}

/* a solid RGBA picture, w x h, one stored deflate block */
static uint32_t solid_png(uint8_t *out, uint32_t w, uint32_t h, uint32_t rgba)
{
    uint32_t n = 0, raw = h * (1 + 4 * w);
#define B(v)   out[n++] = (uint8_t) (v)
#define U32(v) B((v) >> 24), B((v) >> 16), B((v) >> 8), B(v)
    static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    for (int i = 0; i < 8; i++) B(SIG[i]);
    U32(13u), B('I'), B('H'), B('D'), B('R'), U32(w), U32(h), B(8), B(6), B(0), B(0), B(0), U32(0u);
    U32(raw + 11u), B('I'), B('D'), B('A'), B('T'), B(0x78), B(0x01), B(0x01);
    B(raw), B(raw >> 8), B(~raw), B((~raw) >> 8);
    for (uint32_t y = 0; y < h; y++) {
        B(0);
        for (uint32_t x = 0; x < w; x++) B(rgba >> 24), B(rgba >> 16), B(rgba >> 8), B(rgba);
    }
    U32(0u), U32(0u), U32(0u), B('I'), B('E'), B('N'), B('D'), U32(0u);
#undef B
#undef U32
    return n;
}

static void make_disk(void)
{
    static uint8_t tmp[4096];
    ndisk = misses = 0;
    put_file("icons/001-burrow.png", PNG_RGB8_KEYED, sizeof PNG_RGB8_KEYED); /* gold on green */
    put_file("icons/l2/145-dragon-archivist.png", PNG_RGB8_KEYED, sizeof PNG_RGB8_KEYED);
    uint32_t n = solid_png(tmp, 20, 20, 0xD02020FFu); /* a person's own red icon */
    put_file("icons/mytool.png", tmp, n);
    static const char *L[5] = {"36n9", "acoto", "ascw", "smic", "cis"};
    for (int i = 0; i < 5; i++) {
        char p[64];
        snprintf(p, sizeof p, "logos/%s-gold.png", L[i]);
        n = solid_png(tmp, 12, 12, 0xF2C230FFu);
        put_file(p, tmp, n);
    }
}

/* ---- watching what is drawn ------------------------------------------------- */
static zui_lt_shell_t REAL;
static zui_rect_t icon_box;
static uint32_t icon_lit, logo_draws;
static void w_icon(void *ctx, const char *app, zxl_icon_ink_t ink, zui_surface_t *s, zui_rect_t box,
                   uint32_t lit)
{
    icon_box = box;
    icon_lit = lit;
    REAL.icon(ctx, app, ink, s, box, lit);
}
static void w_logo(void *ctx, uint32_t which, zui_surface_t *s, zui_rect_t box, uint32_t lit)
{
    logo_draws++;
    REAL.logo(ctx, which, s, box, lit);
}

/* ---- a stand-in vector drawer (LS6) ------------------------------------------
 * "VEC" + three colour bytes: fills the box's middle with that colour (or the
 * tint), at the strength given. "BAD": malformed. */
static uint8_t VSTORE[1 << 12];
static int vec_on, vec_calls, vec_bad_calls;
static uint32_t vec_last_tint, vec_last_strength;
static int fake_vec(void *ctx, zui_surface_t *s, const uint8_t *d, uint32_t n, zui_rect_t box,
                    uint32_t strength, uint32_t tint)
{
    (void) ctx;
    vec_calls++;
    if (n < 6 || memcmp(d, "VEC", 3)) {
        vec_bad_calls++;
        return -1;
    }
    if (box.x == icon_box.x && box.y == icon_box.y && box.w == icon_box.w) {
        vec_last_tint = tint;
        if (strength > vec_last_strength) vec_last_strength = strength;
    }
    uint32_t rgb = tint ? tint & 0xFFFFFFu : (uint32_t) d[3] << 16 | (uint32_t) d[4] << 8 | d[5];
    if (strength >= 128)
        zui_fill(s, (zui_rect_t){box.x + box.w / 4, box.y + box.h / 4, box.w / 2, box.h / 2}, rgb);
    return 0;
}

static void setup(audio_device_t *dev, const zxl_prefs_t *prefs)
{
    make_disk();
    zui_ls_config_t cfg = {fs_read,
                           0,
                           "icons",
                           "logos",
                           dev,
                           FILEBUF,
                           sizeof FILEBUF,
                           WORK,
                           sizeof WORK,
                           vec_on ? fake_vec : 0,
                           0,
                           VSTORE,
                           sizeof VSTORE};
    vec_calls = vec_bad_calls = 0;
    zui_ls_init(&LS, &cfg);
    REAL = zui_ls_shell(&LS);
    zui_lt_shell_t watched = REAL;
    watched.icon = w_icon;
    watched.logo = w_logo;
    zui_wm_init(&WM, W, H, false);
    zui_launch_theme_attach(&LT, &WM, &watched, prefs);
    logo_draws = 0;
}

/* how many pixels in the icon's box are close to a colour */
static uint32_t near(uint32_t rgb, int tol)
{
    uint32_t n = 0;
    for (int32_t y = icon_box.y; y < icon_box.y + icon_box.h; y++)
        for (int32_t x = icon_box.x; x < icon_box.x + icon_box.w; x++) {
            uint32_t p = PX[y * W + x];
            int dr = (int) ((p >> 16) & 255u) - (int) ((rgb >> 16) & 255u);
            int dg = (int) ((p >> 8) & 255u) - (int) ((rgb >> 8) & 255u);
            int db = (int) (p & 255u) - (int) (rgb & 255u);
            if (dr * dr + dg * dg + db * db <= tol * tol) n++;
        }
    return n;
}

/* one frame of the session: the load moves, the screen is drawn, the sound
 * is fed (800 frames = one 60 Hz frame of audio) */
static int16_t pcm[800 * 2];
static double frame(uint32_t id, uint32_t p, uint32_t tick)
{
    if (p) zui_wm_progress(&WM, id, p);
    zui_wm_draw(&WM, &S, tick);
    zui_ls_render(&LS, pcm, 800);
    double e = 0;
    for (int i = 0; i < 1600; i++) e += (double) pcm[i] * pcm[i];
    return e;
}

static void test_cold_launch(void)
{
    setup(0, 0);
    uint32_t id = zui_launch_theme_open(&LT, "Burrow", "Burrow", "", true, false, 0);
    CHECK(id != 0, "Burrow opens");
    CHECK(LS.player.age == 1, "LS4: its begin is sounding from the moment it opens");
    double e0 = frame(id, 0, 0);
    CHECK(e0 > 0, "and is heard in the first frame");
    CHECK(reads_of("icons/001-burrow.png") == 1, "LS1: its own icon read from disk");
    for (int i = 0; i < 5; i++) CHECK(disk[3 + i].reads == 1, "LS3: logo %d read once", i);
    CHECK(logo_draws >= 5, "the five logos drawn on their tiles");
    CHECK(icon_box.w > 0 && icon_lit < 256, "LS2: the icon waits, unlit");
    uint32_t dim = near(ZXL_GOLD_RGB, 40);
    CHECK(dim == 0, "unlit, it is a faint engraving, not yet gold (%u)", dim);

    /* the real load, a frame at a time: each tile's tick starts in the frame
     * its tile lights (the bar fills from both ends, so the outer tiles light
     * in pairs), never before, never in a burst */
    uint32_t cues = LS.player.age, ticks = 0, first = 0, last = 0;
    for (uint32_t p = 4; p <= 128; p += 4) {
        frame(id, p, p);
        if (LS.player.age != cues) {
            CHECK(LS.player.age - cues <= 2, "at most a pair at once (p=%u)", p);
            ticks += LS.player.age - cues;
            if (!first) first = p;
            last = p;
            cues = LS.player.age;
        }
    }
    CHECK(ticks == 5, "LS4: five ticks as five tiles lit (%u)", ticks);
    CHECK(first < last && last >= 120, "spread across the load, the centre last (%u..%u)", first,
          last);
    for (uint32_t p = 132; p < 256; p += 8) frame(id, p, p);
    CHECK(LS.player.age == cues, "nothing more until the app is ready");
    double before = frame(id, 0, 299);
    frame(id, 256, 300);
    CHECK(LS.player.age == cues + 1, "LS4: the end plays the moment the app is ready");
    CHECK(LS.player.cue[0].on + LS.player.cue[1].on + LS.player.cue[2].on + LS.player.cue[3].on +
                  LS.player.cue[4].on + LS.player.cue[5].on >=
              1,
          "and is sounding");
    double after = frame(id, 0, 301);
    CHECK(after > before, "louder than the ticks' tails: the end is heard");
    CHECK(icon_lit == 256, "the icon is fully lit");
    uint32_t gold = near(ZXL_GOLD_RGB, 40);
    CHECK(gold > 20, "LS2: a system app's icon in the palette's gold (%u px)", gold);
    for (int t = 0; t < 200; t++) frame(id, 0, 302 + t);
    CHECK(zxl_player_active(&LS.player) == 0, "every cue over a few seconds later");
}

static void test_own_colours_and_paths(void)
{
    setup(0, 0);
    uint32_t id = zui_launch_theme_open(&LT, "My Tool", "My Tool", "group3", false, false, 0);
    CHECK(id != 0, "a person's app opens");
    frame(id, 256, 1);
    frame(id, 0, 2);
    CHECK(reads_of("icons/mytool.png") >= 1, "LS1: its icon by its name (mytool.png)");
    uint32_t red = near(0xD02020u, 30), gold = near(ZXL_GOLD_RGB, 40);
    CHECK(red > 20 && gold == 0, "LS2: a person's app keeps its own colours (%u red, %u gold)", red,
          gold);
    CHECK(zui_launch_theme_of(&LT, id) == zxl_theme_pick("group3"), "its picked sound sequence");

    char p[ZUI_LS_PATH];
    CHECK(!zui_ls_icon_path("icons", "Cartulary", p, sizeof p) &&
              !strcmp(p, "icons/l2/145-dragon-archivist.png"),
          "a tier-2 app's icon from the catalogue (%s)", p);
    CHECK(!zui_ls_icon_path("/zxv/icons/", "Burrow", p, sizeof p) &&
              !strcmp(p, "/zxv/icons/001-burrow.png"),
          "one slash between folder and file (%s)", p);
    CHECK(!zui_ls_icon_path("i", "Night Owl 2!", p, sizeof p) && !strcmp(p, "i/nightowl2.png"),
          "a person's app: lower case, letters and digits (%s)", p);
    CHECK(zui_ls_icon_path("icons", "Burrow", p, 12) == -1, "too long for the room: refused");
    CHECK(zui_ls_icon_path("icons", "!!!", p, sizeof p) == -1, "no letters: no file");
}

static void test_missing_files(void)
{
    setup(0, 0);
    ndisk = 0; /* an empty disk */
    uint32_t id = zui_launch_theme_open(&LT, "Ghost", "Ghost", "", false, false, 0);
    double e = frame(id, 0, 0);
    CHECK(id != 0 && e > 0, "LS5: no pictures, the launch still runs and sounds");
    int m = misses;
    for (int t = 1; t < 10; t++) frame(id, 0, (uint32_t) t);
    CHECK(misses == m, "not read again every frame");
    zui_wm_close(&WM, id);
    id = zui_launch_theme_open(&LT, "Ghost", "Ghost", "", false, false, 0);
    frame(id, 0, 20);
    CHECK(misses > m, "LS5: tried again when it next opens");
}

static void test_icon_cache(void)
{
    setup(0, 0);
    static const char *APPS[5] = {"Burrow", "Cartulary", "My Tool", "Hollow", "Threshold"};
    for (int i = 0; i < 5; i++) {
        uint32_t id = zui_launch_theme_open(&LT, APPS[i], APPS[i], "", true, false, 0);
        frame(id, 0, (uint32_t) i);
        zui_wm_close(&WM, id);
    }
    CHECK(reads_of("icons/001-burrow.png") == 1, "Burrow read once so far");
    uint32_t id = zui_launch_theme_open(&LT, "Burrow", "Burrow", "", true, false, 0);
    frame(id, 0, 9);
    CHECK(reads_of("icons/001-burrow.png") == 2, "the oldest of %d kept icons gave way",
          ZUI_LS_ICONS);
    id = zui_launch_theme_open(&LT, "Burrow", "Burrow", "", true, false, 0);
    frame(id, 0, 10);
    CHECK(reads_of("icons/001-burrow.png") == 2, "a kept icon is not read again");
}

static void test_audio_device(void)
{
    audio_init(&DEV, AUDIO_CTRL_NONE, "launch");
    setup(&DEV, 0);
    CHECK(LS.stream != 0, "LS4: a 48 kHz stereo stream on the device");
    uint32_t id = zui_launch_theme_open(&LT, "Coffer", "Coffer", "", true, false, 0);
    CHECK(audio_stream_available(&DEV, LS.stream) > 0, "the begin reaches the device at once");
    uint32_t first = LS.frames_out;
    CHECK(zui_ls_pump(&LS) == 0, "a full ring takes nothing more");
    CHECK(first * 4u <= AUDIO_STREAM_BUF_SIZE, "never more than the ring holds");
    /* the device plays some out: the pump tops it up */
    for (int i = 0; i < 4; i++) audio_mixer_process(&DEV);
    uint32_t more = zui_ls_pump(&LS);
    CHECK(more > 0 || audio_stream_space(&DEV, LS.stream) < 4, "topped up as it drains (%u)", more);
    (void) id;
    /* muted: nothing reaches the device */
    zxl_prefs_t mute = zxl_prefs_default();
    mute.system_muted = 1;
    audio_init(&DEV, AUDIO_CTRL_NONE, "launch");
    setup(&DEV, &mute);
    zui_launch_theme_open(&LT, "Coffer", "Coffer", "", true, false, 0);
    CHECK(audio_stream_available(&DEV, LS.stream) == 0 && LS.player.age == 0, "muted: not a sound");
}

static void test_vectors_first(void)
{
    static const uint8_t RED[6] = {'V', 'E', 'C', 0xD0, 0x20, 0x20};
    static const uint8_t GOLD[6] = {'V', 'E', 'C', 0xF2, 0xC2, 0x30};
    static const uint8_t BAD[6] = {'B', 'A', 'D', 0, 0, 0};
    vec_on = 1;
    setup(0, 0);
    put_file("icons/001-burrow.zxvg", GOLD, 6);
    put_file("icons/mytool.zxvg", RED, 6);
    put_file("logos/ascw-gold.zxvg", RED, 6);
    put_file("logos/cis-gold.zxvg", BAD, 6);
    vec_last_strength = 0;
    uint32_t id = zui_launch_theme_open(&LT, "Burrow", "Burrow", "", true, false, 0);
    frame(id, 0, 0);
    CHECK(reads_of("icons/001-burrow.zxvg") == 1 && reads_of("icons/001-burrow.png") == 0,
          "LS6: the vector is read, not the PNG");
    CHECK(reads_of("logos/ascw-gold.zxvg") == 1 && reads_of("logos/ascw-gold.png") == 0,
          "LS6: a logo's vector is read, not its PNG");
    CHECK(reads_of("logos/36n9-gold.png") == 1, "a logo with no vector: its PNG");
    CHECK(reads_of("logos/cis-gold.zxvg") == 1 && reads_of("logos/cis-gold.png") == 1,
          "LS6: a malformed vector falls back to its PNG");
    CHECK(vec_bad_calls == 1, "and is not tried again (%d)", vec_bad_calls);
    CHECK(vec_last_strength > 0 && vec_last_strength < 128 && vec_last_tint == 0,
          "LS6: unlit, faint, as drawn (%u)", vec_last_strength);
    int calls = vec_calls;
    frame(id, 256, 1);
    frame(id, 0, 2);
    CHECK(vec_calls > calls, "LS6: drawn live every frame");
    uint32_t gold = near(ZXL_GOLD_RGB, 40), red = near(0xD02020u, 30);
    CHECK(gold > 20 && red == 0 && vec_last_tint == 0 && vec_last_strength == 256,
          "LS6: a system app's vector, lit, in its own gold (%u gold, %u red)", gold, red);
    CHECK(reads_of("icons/001-burrow.zxvg") == 1, "kept, not read again");
    zui_wm_close(&WM, id);

    id = zui_launch_theme_open(&LT, "My Tool", "My Tool", "", false, false, 0);
    frame(id, 256, 3);
    frame(id, 0, 4);
    gold = near(ZXL_GOLD_RGB, 40), red = near(0xD02020u, 30);
    CHECK(red > 20 && gold == 0 && vec_last_tint == 0,
          "LS2: a person's vector keeps its own colours (%u red, %u gold)", red, gold);
    zui_wm_close(&WM, id);

    put_file("icons/l2/145-dragon-archivist.zxvg", BAD, 6);
    id = zui_launch_theme_open(&LT, "Cartulary", "Cartulary", "", true, false, 0);
    frame(id, 256, 5);
    frame(id, 0, 6);
    CHECK(reads_of("icons/l2/145-dragon-archivist.png") == 1,
          "LS6: a malformed icon vector falls back to its PNG");
    calls = vec_bad_calls;
    frame(id, 0, 7);
    CHECK(vec_bad_calls == calls, "and its vector is not tried again");
    CHECK(near(ZXL_GOLD_RGB, 40) > 20, "its PNG drawn in gold");
    vec_on = 0;
}

int main(void)
{
    zui_surface_init(&S, PX, W, H, W);
    test_cold_launch();
    test_own_colours_and_paths();
    test_missing_files();
    test_icon_cache();
    test_audio_device();
    test_vectors_first();
    printf("test_zui_launch_shell: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}

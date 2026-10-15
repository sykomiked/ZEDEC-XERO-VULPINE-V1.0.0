/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_chgskin.c — the Chiglet's base schematic and skins (SK1-SK6). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zui_chgskin.h"
#include "zui_tokens.h"

static int checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define W 240
#define H 240
static uint32_t PIX[W * H];
static zui_surface_t S;

static const zui_chiglet_t WHO = {{1, 2, 3, 4, 5, 6, 7, 8}, 2, ZUI_CHG_DECIDED, 0};

static void clear(void)
{
    zui_surface_init(&S, PIX, W, H, W);
    zui_fill(&S, (zui_rect_t){0, 0, W, H}, ZUI_VOID);
}

static uint64_t hash(void)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < W * H; i++) {
        h ^= PIX[i];
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t draw(const zui_chg_skin_t *k, const zui_chiglet_t *c, const zui_chg_pose_t *p)
{
    clear();
    zui_chg_skin_draw(&S, k, c, p, W / 2, H / 2, 60);
    return hash();
}

static zui_chg_pose_t pose_of(swarm_emotion_t e)
{
    zui_chg_pose_t p;
    zui_chg_pose_from(&p, &WHO);
    swarm_feeling_t f = {e, 5};
    p.face = zui_face_feeling(f);
    return p;
}

/* pixels that are exactly this colour (no anti-aliasing) */
static uint32_t count(uint32_t rgb, int32_t *sumx)
{
    uint32_t n = 0;
    int64_t sx = 0;
    for (int32_t y = 0; y < H; y++)
        for (int32_t x = 0; x < W; x++)
            if ((PIX[y * W + x] & 0xFFFFFFu) == rgb) {
                n++;
                sx += x;
            }
    if (sumx) *sumx = n ? (int32_t) (sx / n) : -1;
    return n;
}

static uint64_t region(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint64_t h = 7;
    for (int32_t y = y0; y < y1; y++)
        for (int32_t x = x0; x < x1; x++) h = h * 31u + PIX[y * W + x];
    return h;
}

/* pixels brighter than 0xF0 in every channel */
static uint32_t bright(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < W * H; i++)
        n += (PIX[i] >> 16 & 0xFF) > 0xF0 && (PIX[i] >> 8 & 0xFF) > 0xF0 && (PIX[i] & 0xFF) > 0xF0;
    return n;
}

/* ---- SK1, SK2: the format, fail closed ------------------------------------ */

static uint8_t BUF[8192];
static uint32_t LEN;

static void fresh(void)
{
    const uint8_t *b = zui_chg_skin_anime_bytes(&LEN);
    memcpy(BUF, b, LEN);
}

static zui_skin_status_t parse(void)
{
    zui_chg_skin_t k;
    memset(&k, 0x5A, sizeof k);
    zui_skin_status_t st = zui_chg_skin_parse(&k, BUF, LEN);
    if (st != ZUI_SKIN_OK) {
        static const zui_chg_skin_t z;
        CHECK(memcmp(&k, &z, sizeof k) == 0, "a refused skin leaves nothing behind");
    }
    return st;
}

/* offset of the first shape record and of the first point */
static uint32_t shapes_at(void)
{
    uint32_t at = 10;
    for (int i = 0; i < 3; i++) at += 1u + BUF[at];
    return at;
}

static void test_format(void)
{
    uint32_t n = 0;
    const uint8_t *b = zui_chg_skin_anime_bytes(&n);
    const zui_chg_skin_t *a = zui_chg_skin_anime();
    CHECK(a != NULL, "the built-in skin parses");
    CHECK(n > 1000 && b && memcmp(b, "ZXCS", 4) == 0, "its bytes are a .zxcs (%u)", n);
    CHECK(a && strcmp(a->name, "Anime") == 0, "SK5 name");
    CHECK(a && strcmp(a->author, "36N9 GENETICS LLC") == 0, "SK5 author");
    CHECK(a && strcmp(a->licence, "Apache-2.0") == 0, "SK5 licence");
    CHECK(zui_chg_skin_expressive(a), "it draws its own face");
    CHECK(!zui_chg_skin_expressive(NULL), "classic has the face badge");
    CHECK(a && a->nshapes >= 40 && a->nshapes <= ZUI_SKIN_MAX_SHAPES, "%u shapes",
          a ? a->nshapes : 0);
    CHECK(zui_chg_skin_anime() == a, "parsed once");

    zui_chg_skin_t k;
    CHECK(zui_chg_skin_parse(NULL, b, n) == ZUI_SKIN_E_ARG, "NULL out");
    CHECK(zui_chg_skin_parse(&k, NULL, n) == ZUI_SKIN_E_ARG, "NULL bytes");

    fresh();
    CHECK(parse() == ZUI_SKIN_OK, "a copy parses");
    /* every shorter length is refused */
    uint32_t full = LEN, bad = 0;
    for (LEN = 0; LEN < full; LEN++)
        if (parse() == ZUI_SKIN_OK) bad++;
    LEN = full;
    CHECK(bad == 0, "every truncation refused (%u slipped)", bad);
    BUF[LEN] = 0;
    LEN++;
    CHECK(parse() == ZUI_SKIN_E_TRAILING, "a trailing byte is refused");

    fresh();
    BUF[0] = 'z';
    CHECK(parse() == ZUI_SKIN_E_MAGIC, "magic");
    fresh();
    BUF[4] = 2;
    CHECK(parse() == ZUI_SKIN_E_VERSION, "version");
    fresh();
    BUF[5] |= 2;
    CHECK(parse() == ZUI_SKIN_E_FLAGS, "unknown flag");
    fresh();
    BUF[6] = 0;
    BUF[7] = 0;
    CHECK(parse() == ZUI_SKIN_E_LIMIT, "no shapes");
    fresh();
    BUF[6] = 97;
    BUF[7] = 0;
    CHECK(parse() == ZUI_SKIN_E_LIMIT, "too many shapes");
    fresh();
    BUF[8] = 0x01;
    BUF[9] = 0x08; /* 2049 */
    CHECK(parse() == ZUI_SKIN_E_LIMIT, "too many points");
    fresh();
    BUF[10] = 0;
    CHECK(parse() == ZUI_SKIN_E_TEXT, "an empty name");
    fresh();
    BUF[11] = 0x07;
    CHECK(parse() == ZUI_SKIN_E_TEXT, "a control character in the name");
    fresh();
    {
        uint32_t lic = 10u + 1u + BUF[10];
        lic += 1u + BUF[lic];
        BUF[lic + 1] = '/';
        CHECK(parse() == ZUI_SKIN_E_TEXT, "a licence that is not SPDX");
    }

    uint32_t sa = shapes_at();
    uint32_t pa = sa + (uint32_t) (BUF[6] | BUF[7] << 8) * ZUI_SKIN_SHAPE_B;
    fresh();
    BUF[sa + 0] = ZUI_SKG_COUNT;
    CHECK(parse() == ZUI_SKIN_E_ENUM, "group");
    fresh();
    BUF[sa + 1] = ZUI_SKR_COUNT;
    CHECK(parse() == ZUI_SKIN_E_ENUM, "reaction");
    fresh();
    BUF[sa + 2] = ZUI_SKP_COUNT;
    CHECK(parse() == ZUI_SKIN_E_ENUM, "paint");
    fresh();
    BUF[sa + 7] = 1;
    CHECK(parse() == ZUI_SKIN_E_ENUM, "the pad byte");
    fresh();
    BUF[sa + 8] = 0x01;
    BUF[sa + 9] = 0x08;
    CHECK(parse() == ZUI_SKIN_E_COORD, "a pivot outside the box");
    fresh();
    BUF[sa + 14] = 2;
    BUF[sa + 15] = 0;
    CHECK(parse() == ZUI_SKIN_E_LIMIT, "a shape of two points");
    fresh();
    BUF[sa + 14] = 0x01;
    BUF[sa + 15] = 0x02; /* 513 */
    CHECK(parse() == ZUI_SKIN_E_LIMIT, "a shape over 512 points");
    fresh();
    {
        uint32_t last = sa + (uint32_t) ((BUF[6] | BUF[7] << 8) - 1) * ZUI_SKIN_SHAPE_B;
        BUF[last + 14]--;
        CHECK(parse() == ZUI_SKIN_E_LIMIT, "point counts that do not add up");
    }
    fresh();
    BUF[pa + 0] = 0x01;
    BUF[pa + 1] = 0x08;
    CHECK(parse() == ZUI_SKIN_E_COORD, "a point outside the box");
    fresh();
    BUF[pa + 3] = 0xF7; /* y = -2049 */
    BUF[pa + 2] = 0xFF;
    CHECK(parse() == ZUI_SKIN_E_COORD, "a point outside the box, negative");
    fresh();
    BUF[pa + 4] = 2;
    CHECK(parse() == ZUI_SKIN_E_OUTLINE, "an unknown point flag");
    fresh();
    BUF[pa + 4] = 0;
    CHECK(parse() == ZUI_SKIN_E_OUTLINE, "a shape starting on a control point");
    fresh();
    /* the first shape is an ellipse: on, off, on, off ... make two offs */
    BUF[pa + 2 * ZUI_SKIN_POINT_B + 4] = 0;
    CHECK(parse() == ZUI_SKIN_E_OUTLINE, "two control points in a row");

    /* any single byte, any value: refused or drawn, never a crash */
    fresh();
    uint32_t ok = 0, refused = 0;
    static const uint8_t vals[] = {0x00, 0x01, 0x7F, 0x80, 0xFF};
    for (uint32_t i = 0; i < LEN; i += 3) {
        for (uint32_t v = 0; v < sizeof vals; v++) {
            uint8_t was = BUF[i];
            BUF[i] = vals[v];
            zui_chg_skin_t k2;
            if (zui_chg_skin_parse(&k2, BUF, LEN) == ZUI_SKIN_OK) {
                ok++;
                clear();
                zui_chg_pose_t p;
                zui_chg_pose_from(&p, &WHO);
                zui_chg_skin_draw(&S, &k2, &WHO, &p, W / 2, H / 2, 60);
            } else {
                refused++;
            }
            BUF[i] = was;
        }
    }
    CHECK(ok > 0 && refused > 0, "mutations: %u drawn, %u refused", ok, refused);
}

/* ---- SK3, SK6: genes show, same in -> same out ---------------------------- */

static void test_genes(void)
{
    const zui_chg_skin_t *a = zui_chg_skin_anime();
    zui_chg_pose_t p;
    zui_chg_pose_from(&p, &WHO);
    uint64_t h1 = draw(a, &WHO, &p), h2 = draw(a, &WHO, &p);
    CHECK(h1 == h2, "SK6 deterministic");
    uint64_t classic = draw(NULL, &WHO, &p);
    CHECK(classic != h1, "a skin changes the look");
    clear();
    zui_chiglet_draw(&S, &WHO, W / 2, H / 2, 60, 0);
    CHECK(hash() == classic, "NULL is exactly the classic look");

    uint32_t distinct = 0;
    uint64_t seen[8];
    for (uint32_t i = 0; i < 8; i++) {
        zui_chiglet_t c = WHO;
        c.genes[4] = (uint8_t) i; /* warmth: hair colour and body tint */
        seen[i] = draw(a, &c, &p);
        bool dup = false;
        for (uint32_t j = 0; j < i; j++) dup = dup || seen[j] == seen[i];
        distinct += !dup;
    }
    CHECK(distinct == 8, "SK3 eight warmth genes, eight looks (%u)", distinct);

    /* curiosity: stray strands above the head */
    uint64_t top[4];
    for (uint32_t i = 0; i < 4; i++) {
        zui_chiglet_t c = WHO;
        c.genes[2] = (uint8_t) i;
        c.genes[3] = 0; /* keep the eye colour still */
        draw(a, &c, &p);
        top[i] = region(0, 0, W, H / 2 - 50);
    }
    CHECK(top[0] != top[1] && top[1] != top[2] && top[2] != top[3], "SK3 curiosity strands");

    /* specialty: the chest emblem, four marks */
    uint64_t chest[4];
    for (uint32_t i = 0; i < 4; i++) {
        zui_chiglet_t c = WHO;
        c.genes[7] = (uint8_t) (i + 4u * 3u); /* % 4 == i; tone shifts with it too */
        c.genes[1] = (uint8_t) (6u - i);      /* hold the skin tone index still */
        draw(a, &c, &p);
        chest[i] = region(W / 2 - 10, H / 2 + 40, W / 2 + 10, H / 2 + 62);
    }
    CHECK(chest[0] != chest[1] && chest[0] != chest[2] && chest[0] != chest[3] &&
              chest[1] != chest[2] && chest[1] != chest[3] && chest[2] != chest[3],
          "SK3 four emblems");

    /* the halo is the schematic's: more time together, more rings */
    zui_chiglet_t g0 = WHO, g4 = WHO;
    g0.together = 0;
    g4.together = 4181; /* all four rings (zui_chiglet_rings) */
    CHECK(draw(a, &g0, &p) != draw(a, &g4, &p), "the halo shows its time together");
}

/* ---- SK4: it reacts ------------------------------------------------------ */

static void test_react(void)
{
    const zui_chg_skin_t *a = zui_chg_skin_anime();
    zui_chg_pose_t p = pose_of(SWARM_EMO_NEUTRAL);
    draw(a, &WHO, &p);
    uint32_t white_open = bright();
    CHECK(white_open > 60, "eyes open (%u white)", white_open);
    p.blink = 256;
    draw(a, &WHO, &p);
    uint32_t white_shut = bright();
    CHECK(white_shut * 8 < white_open, "a blink closes them (%u)", white_shut);

    zui_chg_pose_t joy = pose_of(SWARM_EMO_JOY);
    draw(a, &WHO, &joy);
    CHECK(count(0x5A1F2Bu, NULL) > 20, "joy opens the mouth");
    CHECK(bright() < white_open / 4, "joy squints: happy arcs, no whites");

    zui_chg_pose_t sad = pose_of(SWARM_EMO_SADNESS), calm = pose_of(SWARM_EMO_NEUTRAL);
    draw(a, &WHO, &calm);
    CHECK(count(0x8FD0FFu, NULL) == 0, "no tear when it is fine");
    CHECK(count(0x5A1F2Bu, NULL) == 0, "mouth closed at rest");
    draw(a, &WHO, &sad);
    uint64_t sadh = hash();
    CHECK(sadh != draw(a, &WHO, &calm), "sadness shows");
    {
        /* the tear is drawn at alpha 220, so look for its blend, not its colour */
        draw(a, &WHO, &sad);
        uint32_t blue = 0;
        for (uint32_t i = 0; i < W * H; i++) {
            uint32_t v = PIX[i];
            if ((v & 0xFF) > 0xC0 && ((v >> 16) & 0xFF) < 0xA0) blue++;
        }
        CHECK(blue > 10, "a tear (%u)", blue);
    }

    /* speaking: the level opens the mouth whatever the face */
    zui_chg_pose_t sp = pose_of(SWARM_EMO_NEUTRAL);
    sp.mouth = 255;
    draw(a, &WHO, &sp);
    uint32_t wide = count(0x5A1F2Bu, NULL);
    sp.mouth = 128;
    draw(a, &WHO, &sp);
    uint32_t half = count(0x5A1F2Bu, NULL);
    sp.mouth = 5;
    draw(a, &WHO, &sp);
    uint32_t shut = count(0x5A1F2Bu, NULL);
    CHECK(wide > half && half > 0 && shut == 0, "mouth by level: %u > %u > %u", wide, half, shut);

    /* the gaze: the pupils move toward what it looks at */
    zui_chg_pose_t l = pose_of(SWARM_EMO_NEUTRAL), r = l;
    l.gaze_x = -256;
    r.gaze_x = 256;
    int32_t xl = 0, xr = 0;
    uint32_t pupil = zui_mix(0x2BB3A3u, 0, 205); /* WHO's eye colour, darkened */
    draw(a, &WHO, &l);
    CHECK(count(pupil, &xl) > 20, "pupils drawn");
    draw(a, &WHO, &r);
    count(pupil, &xr);
    CHECK(xr > xl + 2, "looks right (%d) of where it looked left (%d)", xr, xl);

    /* brows */
    zui_chg_pose_t cur = pose_of(SWARM_EMO_CURIOUS), ang = pose_of(SWARM_EMO_FRUSTRATION);
    uint64_t hc = draw(a, &WHO, &cur), ha = draw(a, &WHO, &ang);
    CHECK(hc != ha && hc != draw(a, &WHO, &calm), "brows move with the feeling");

    /* blush fades in with love */
    zui_chg_pose_t love = pose_of(SWARM_EMO_LOVE), love0 = love;
    love0.face.blush = 0;
    CHECK(draw(a, &WHO, &love) != draw(a, &WHO, &love0), "blush");

    /* breathing: only on a running clock */
    zui_chg_pose_t t0 = pose_of(SWARM_EMO_NEUTRAL), t4 = t0;
    t4.tick = 4;
    CHECK(draw(a, &WHO, &t0) != draw(a, &WHO, &t4), "it breathes");
}

static void test_pose(void)
{
    zui_chg_pose_t p;
    zui_chiglet_t c = WHO;
    zui_chg_pose_from(&p, &c);
    CHECK(p.mouth == 0 && p.face.brow_raise == 0 && p.blink == 0 && p.tick == 0, "still pose");
    c.mood = ZUI_CHG_UNCERTAIN;
    zui_chg_pose_from(&p, &c);
    CHECK(p.face.brow_raise == 256, "unsure raises a brow");
    c.mood = ZUI_CHG_SPEAKING;
    zui_chg_pose_from(&p, &c);
    CHECK(p.mouth == 128, "speaking holds the mouth half open");
    c.mouth = 200;
    zui_chg_pose_from(&p, &c);
    CHECK(p.mouth == 200, "speech level");
    zui_chg_pose_from(NULL, &c);
    zui_chg_pose_from(&p, NULL);
    CHECK(p.mouth == 0, "no Chiglet: still");

    /* blinks: some, short, none with reduce motion */
    uint32_t shut = 0, maxb = 0;
    for (uint32_t t = 0; t < 20000; t += 10) {
        uint16_t b = zui_chg_blink(&WHO, t, false);
        shut += b > 0;
        if (b > maxb) maxb = b;
        CHECK(zui_chg_blink(&WHO, t, true) == 0, "no blink with reduce motion");
        if (fails > 50) break;
    }
    CHECK(shut > 0 && shut < 2000 / 15, "blinks are short and rare (%u of 2000)", shut);
    CHECK(maxb >= 240 && maxb <= 256, "a blink shuts the eyes (%u)", maxb);
    zui_chiglet_t other = WHO;
    other.genes[1] = 9;
    uint32_t diff = 0;
    for (uint32_t t = 0; t < 20000; t += 10)
        diff += zui_chg_blink(&WHO, t, false) != zui_chg_blink(&other, t, false);
    CHECK(diff > 0, "each Chiglet blinks on its own rhythm");
    CHECK(zui_chg_blink(NULL, 0, false) == 0, "NULL");

    int16_t gx, gy;
    zui_chg_gaze(100, 100, 50, 100, 100, &gx, &gy);
    CHECK(gx == 0 && gy == 0, "at the centre: straight ahead");
    zui_chg_gaze(100, 100, 50, 125, 100, &gx, &gy);
    CHECK(gx == 128 && gy == 0, "half way right");
    zui_chg_gaze(100, 100, 50, 1000, 100, &gx, &gy);
    CHECK(gx == 256 && gy == 0, "far right");
    zui_chg_gaze(100, 100, 50, 100 - 900, 100 - 450, &gx, &gy);
    CHECK(gx == -256 && gy == -128, "far up-left");
    zui_chg_gaze(0, 0, 0, 5, 5, &gx, &gy);
    CHECK(gx == 256 && gy == 256, "r 0 is safe");
    zui_chg_gaze(0, 0, 10, 3, 0, NULL, NULL);
}

/* ---- safety --------------------------------------------------------------- */

static void test_safety(void)
{
    const zui_chg_skin_t *a = zui_chg_skin_anime();
    zui_chg_pose_t p = pose_of(SWARM_EMO_JOY);
    clear();
    uint64_t before = hash();
    zui_chg_skin_draw(&S, a, &WHO, &p, W / 2, H / 2, 7);
    CHECK(hash() == before, "too small: nothing");
    zui_chg_skin_draw(NULL, a, &WHO, &p, 0, 0, 60);
    zui_chg_skin_draw(&S, a, NULL, &p, 0, 0, 60);
    CHECK(hash() == before, "NULL surface or Chiglet: nothing");
    zui_chg_skin_draw(&S, a, &WHO, NULL, W / 2, H / 2, 60);
    CHECK(hash() != before, "NULL pose: its still pose");
    clear();
    zui_chg_skin_draw(&S, a, &WHO, &p, -5000, 9000, 60);
    CHECK(hash() == before, "far off the surface: nothing");
    zui_chg_skin_draw(&S, a, &WHO, &p, 0, 0, 60);
    zui_chg_skin_draw(&S, a, &WHO, &p, W, H, 60);
    zui_chg_skin_draw(&S, a, &WHO, &p, W / 2, H / 2, 6000);
    zui_chg_skin_draw(&S, a, &WHO, &p, W / 2, H / 2, 100000);
    CHECK(1, "edges and huge sizes clip");

    /* a clip rectangle is honoured */
    clear();
    zui_clip(&S, (zui_rect_t){0, 0, W / 2, H});
    zui_chg_skin_draw(&S, a, &WHO, &p, W / 2, H / 2, 60);
    zui_unclip(&S);
    uint32_t right = 0;
    for (int32_t y = 0; y < H; y++)
        for (int32_t x = W / 2; x < W; x++) right += (PIX[y * W + x] & 0xFFFFFFu) != ZUI_VOID;
    CHECK(right == 0, "nothing past the clip (%u)", right);

    /* out-of-range pose values are clamped */
    zui_chg_pose_t wild = p;
    wild.gaze_x = 30000;
    wild.gaze_y = -30000;
    wild.blink = 9999;
    wild.face.eye_open = 30000;
    wild.face.mouth_curve = -30000;
    wild.face.brow_raise = 30000;
    wild.face.blush = -5;
    wild.face.tear = 30000;
    zui_chg_pose_t sane = wild;
    sane.gaze_x = 256;
    sane.gaze_y = -256;
    sane.blink = 256;
    sane.face.eye_open = 256;
    sane.face.mouth_curve = -256;
    sane.face.brow_raise = 256;
    sane.face.blush = 0;
    sane.face.tear = 256;
    CHECK(draw(a, &WHO, &wild) == draw(a, &WHO, &sane), "wild pose = its clamped pose");
}

int main(void)
{
    test_format();
    test_genes();
    test_react();
    test_pose();
    test_safety();
    const char *out = getenv("ZXV_SKIN_PPM");
    if (out) { /* a row of Chiglets for people to look at */
        static uint32_t big[960 * 260];
        zui_surface_t s;
        zui_surface_init(&s, big, 960, 260, 960);
        zui_fill(&s, (zui_rect_t){0, 0, 960, 260}, ZUI_VOID);
        static const swarm_emotion_t e[4] = {SWARM_EMO_NEUTRAL, SWARM_EMO_JOY, SWARM_EMO_CURIOUS,
                                             SWARM_EMO_SADNESS};
        for (int i = 0; i < 4; i++) {
            zui_chiglet_t c = WHO;
            c.genes[4] = (uint8_t) (i * 3);
            zui_chg_pose_t p = pose_of(e[i]);
            zui_chg_skin_draw(&s, zui_chg_skin_anime(), &c, &p, 120 + 240 * i, 130, 70);
        }
        FILE *f = fopen(out, "wb");
        if (f) {
            fprintf(f, "P6\n960 260\n255\n");
            for (int i = 0; i < 960 * 260; i++) {
                unsigned char px[3] = {(unsigned char) (big[i] >> 16),
                                       (unsigned char) (big[i] >> 8), (unsigned char) big[i]};
                fwrite(px, 1, 3, f);
            }
            fclose(f);
        }
    }
    printf("[%s] test_zui_chgskin: %d/%d checks\n", fails ? "FAIL" : "PASS", checks - fails,
           checks);
    return fails ? 1 : 0;
}

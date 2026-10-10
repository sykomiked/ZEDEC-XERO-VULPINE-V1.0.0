/* test_truetype.c — the TrueType rasteriser, known-answer + hostile input.
 *
 * MINI_TTF is a hand-built minimal, valid TrueType font whose only real glyph
 * is a SQUARE spanning font units x[100,900] y[0,800] — 0.64 of the em box in
 * each axis. So rendered into a size x size bitmap it must ink ~0.64 of the
 * pixels, its centre must be solid, and its margins (outside the square) must
 * be empty. That is a geometry we can check exactly, not against our own
 * output. Then: the parser must survive malformed and truncated fonts.
 */
#include <stdio.h>
#include <string.h>
#include "truetype.h"
#include "font.h"
#include "ttf_fixture.inc" /* MINI_TTF[] */

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

int main(void)
{
    printf("=== TrueType rasteriser (known-answer + hostile input) ===\n");
    ttf_font_t f;

    /* ---- parse ---- */
    CHECK(ttf_parse(&f, MINI_TTF, sizeof MINI_TTF), "the minimal TTF parses");
    CHECK(f.units_per_em == 1000, "unitsPerEm read from head (1000)");
    CHECK(f.num_glyphs == 2, "numGlyphs read from maxp (2)");

    /* ---- cmap ---- */
    CHECK(ttf_glyph_index(&f, 'A') == 1, "cmap maps 'A' -> glyph 1");
    CHECK(ttf_glyph_index(&f, 'B') == 0, "an unmapped codepoint -> .notdef (0)");
    CHECK(ttf_glyph_index(&f, 0x4E2D) == 0, "a CJK codepoint not in this font -> 0");

    /* ---- render: the known square ---- */
    {
        const uint32_t N = 40;
        static uint8_t bmp[40 * 40];
        CHECK(ttf_render(&f, 1, N, bmp, N, N), "glyph 1 renders");

        uint32_t ink = 0;
        for (uint32_t i = 0; i < N * N; i++)
            if (bmp[i] > 127) ink++;
        printf("       inked %u / %u pixels (expect ~0.64 = ~1024)\n", ink, N * N);
        CHECK(ink > 950 && ink < 1100,
              "the square inks ~64% of the box — the exact area of a font-unit "
              "square x[100,900] y[0,800] in a 1000-unit em");

        CHECK(bmp[20 * N + 20] == 255, "the centre of the square is solid");
        /* outside the square: top-left corner maps to font (~x62, ~y950) — the
         * y is above the square's top (800) */
        CHECK(bmp[1 * N + 20] == 0, "a pixel above the square's top edge is empty");
        /* left of the square: col 1 maps to font x ~37 < 100 */
        CHECK(bmp[20 * N + 1] == 0, "a pixel left of the square's left edge is empty");
        CHECK(bmp[1 * N + 1] == 0, "the top-left corner is empty");

        /* the ink is a connected block, not scattered: the row through the
         * middle is inked across the interior and clear at the far margins */
        CHECK(bmp[20 * N + 5] > 127 && bmp[20 * N + 34] > 127,
              "the middle row is inked across the square's interior");
    }

    /* ---- empty glyph (like a space) renders as false, not a box ---- */
    {
        static uint8_t bmp[16 * 16];
        CHECK(!ttf_render(&f, 0, 16, bmp, 16, 16),
              "the empty .notdef glyph reports 'no outline', not a fabricated box");
    }

    /* ---- codepoint convenience path matches the glyph path ---- */
    {
        const uint32_t N = 32;
        static uint8_t a[32 * 32], b[32 * 32];
        ttf_render_cp(&f, 'A', N, a, N, N);
        ttf_render(&f, 1, N, b, N, N);
        CHECK(memcmp(a, b, N * N) == 0, "render_cp('A') == render(glyph 1)");
    }

    /* ================= hostile / malformed input ================= */
    {
        ttf_font_t g;
        CHECK(!ttf_parse(&g, (const uint8_t *) "not a font at all!!", 19),
              "garbage is rejected, not parsed");
        CHECK(!ttf_parse(&g, MINI_TTF, 8), "a truncated header is rejected");
        /* every prefix of a valid font must parse-or-reject without crashing */
        for (uint32_t cut = 0; cut < sizeof MINI_TTF; cut++) (void) ttf_parse(&g, MINI_TTF, cut);
        CHECK(1, "every truncation of the font is handled without overrun");

        /* a valid font, but ask for an out-of-range glyph */
        static uint8_t bmp[16 * 16];
        CHECK(!ttf_render(&f, 9999, 16, bmp, 16, 16), "an out-of-range glyph id renders nothing");
        /* rendering into a 1x1 buffer must not overrun */
        static uint8_t one[1];
        (void) ttf_render(&f, 1, 1, one, 1, 1);
        CHECK(1, "rendering into a 1x1 buffer does not overrun");

        /* randomised fonts must never crash the parser or rasteriser */
        static uint8_t junk[300];
        for (uint32_t s = 0; s < 2000; s++) {
            uint32_t x = s * 2654435761u;
            for (uint32_t i = 0; i < sizeof junk; i++) {
                x = x * 1103515245u + 12345u;
                junk[i] = (uint8_t) (x >> 16);
            }
            ttf_font_t j;
            if (ttf_parse(&j, junk, sizeof junk)) {
                static uint8_t b2[24 * 24];
                for (uint32_t gid = 0; gid < 4; gid++) (void) ttf_render(&j, gid, 24, b2, 24, 24);
                (void) ttf_glyph_index(&j, 'A');
            }
        }
        CHECK(1, "2000 randomised fonts parse+render without crashing (ASan gate)");
    }

    /* ================= full stack: registry -> TTF backend -> pixels ========= */
    {
        static font_registry_t reg;
        font_registry_init(&reg);
        int32_t latin =
            font_register(&reg, "Mini", SCRIPT_LATIN, FONT_STYLE_REGULAR, "/fonts/mini.ttf");
        (void) latin;
        /* before binding a rasteriser: 'A' is not in the 8x8 builtin set */
        static uint8_t bmp[24 * 24];
        CHECK(!font_glyph(&reg, latin, 'A', bmp, 24, 24),
              "with no glyph backend, the registry cannot draw 'A' at 24px");

        /* bind the parsed TrueType font as the glyph backend */
        font_bind_ttf(&reg, &f);
        CHECK(font_glyph(&reg, latin, 'A', bmp, 24, 24),
              "with the TTF backend bound, the registry renders 'A' from the outline");
        uint32_t ink = 0;
        for (uint32_t i = 0; i < 24 * 24; i++)
            if (bmp[i] > 127) ink++;
        CHECK(ink > 300 && ink < 400 && bmp[12 * 24 + 12] == 255,
              "the glyph the FONT STACK drew is the square (centre solid, ~64% inked)");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}

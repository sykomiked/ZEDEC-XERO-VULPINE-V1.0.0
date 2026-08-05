/* test_font.c — the font registry: resolution and the honest glyph boundary. */
#include <stdio.h>
#include <string.h>
#include "font.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* a stub rasterizer: "renders" any codepoint by filling coverage 0x7F, and
 * records the last request, so we can prove the ops path is taken */
static uint32_t g_last_cp = 0; static const font_face_t *g_last_face = 0;
static int stub_raster(const font_face_t *f, uint32_t cp, uint8_t *b, uint32_t w, uint32_t h, void *ctx) {
    (void)ctx; g_last_cp = cp; g_last_face = f;
    for (uint32_t i = 0; i < w*h; i++) b[i] = 0x7F;
    return 0;
}

int main(void) {
    printf("=== font registry: script->face resolution + glyph boundary ===\n");
    static font_registry_t r;
    font_registry_init(&r);

    /* install a representative slice of the ASCW set */
    int32_t latin  = font_register(&r, "Golden", SCRIPT_LATIN,  FONT_STYLE_REGULAR, "/fonts/ASCW-Golden-Latin.ttf");
    int32_t arabic = font_register(&r, "Golden", SCRIPT_ARABIC, FONT_STYLE_REGULAR, "/fonts/ASCW-Golden-Arabic.ttf");
    int32_t han    = font_register(&r, "Golden", SCRIPT_HAN,    FONT_STYLE_REGULAR, "/fonts/ASCW-Golden-Han.ttf");
    int32_t dragon = font_register(&r, "Dragon", SCRIPT_LATIN,  FONT_STYLE_DISPLAY, "/fonts/ASCW-Dragon-Display.ttf");
    CHECK(latin>=0 && arabic>=0 && han>=0 && dragon>=0, "four faces install");

    /* ---- resolution: the heart of multi-language rendering ---- */
    CHECK(font_resolve(&r, SCRIPT_ARABIC, FONT_STYLE_REGULAR) == arabic,
          "an Arabic run resolves to the Arabic face");
    CHECK(font_resolve(&r, SCRIPT_HAN, FONT_STYLE_REGULAR) == han,
          "a Han run resolves to the Han face");
    CHECK(font_resolve(&r, SCRIPT_LATIN, FONT_STYLE_DISPLAY) == dragon,
          "a Latin DISPLAY run resolves to the Dragon face (style preference honoured)");
    CHECK(font_resolve(&r, SCRIPT_LATIN, FONT_STYLE_REGULAR) == latin,
          "a Latin regular run resolves to the regular Latin face");

    /* a script we never installed falls back to Latin, not to nothing */
    CHECK(font_resolve(&r, SCRIPT_CHEROKEE, FONT_STYLE_REGULAR) == latin,
          "an un-installed script falls back to the global fallback (Latin)");
    CHECK(font_resolve(&r, SCRIPT_COMMON, FONT_STYLE_REGULAR) == latin,
          "COMMON resolves to the fallback (it is drawn by its neighbour's run)");

    /* default override */
    int32_t arabic2 = font_register(&r, "Naskh", SCRIPT_ARABIC, FONT_STYLE_REGULAR, "/fonts/ASCW-Naskh.ttf");
    CHECK(font_set_default(&r, SCRIPT_ARABIC, arabic2), "the Arabic default can be overridden");
    CHECK(font_resolve(&r, SCRIPT_ARABIC, FONT_STYLE_MONO) == arabic2,
          "with no mono Arabic face, resolution uses the (now overridden) default");

    const font_face_t *f = font_get(&r, arabic);
    CHECK(f && strcmp(f->family,"Golden")==0 && f->script==SCRIPT_ARABIC,
          "a face carries its family and script");

    /* ---- the builtin diagnostic glyphs (before the TTF rasterizer) ---- */
    {
        uint8_t bmp[64];
        CHECK(font_builtin_has('Z') && font_builtin_has('0') && font_builtin_has(' '),
              "the builtin set covers Z, 0 and space");
        CHECK(!font_builtin_has(0x0627),
              "it does NOT cover Arabic — that needs the real rasterizer");

        memset(bmp,0,64);
        CHECK(font_glyph(&r, latin, 'Z', bmp, 8, 8), "'Z' renders from the builtin");
        /* top row of 'Z' is 0xFF -> all 8 pixels set */
        int top=0; for (int x=0;x<8;x++) if (bmp[x]==0xFF) top++;
        CHECK(top==8, "the top row of 'Z' is a full bar (0xFF)");
        /* second row 0x02 -> only the second-from-right pixel set */
        CHECK(bmp[8+6]==0xFF && bmp[8+0]==0x00, "row 1 of 'Z' has a single pixel (0x02)");

        memset(bmp,0xAA,64);
        CHECK(font_glyph(&r, latin, ' ', bmp, 8, 8), "space renders");
        int any=0; for (int i=0;i<64;i++) if (bmp[i]) any++;
        CHECK(any==0, "space is blank");

        /* a codepoint with no builtin and no rasterizer -> NO glyph, not tofu */
        CHECK(!font_glyph(&r, arabic, 0x0627, bmp, 8, 8),
              "an Arabic codepoint with no rasterizer returns NO glyph — the "
              "system never fabricates one");
    }

    /* ---- with a rasterizer bound, real faces render ---- */
    {
        font_glyph_ops_t ops = { stub_raster, 0 };
        font_set_glyph_ops(&r, &ops);
        uint8_t bmp[16*16];
        CHECK(font_glyph(&r, arabic, 0x0627, bmp, 16, 16),
              "with a rasterizer bound, the Arabic codepoint renders");
        CHECK(g_last_cp == 0x0627 && g_last_face == font_get(&r, arabic),
              "the rasterizer was called with the right codepoint and face");
        CHECK(bmp[0]==0x7F, "and it filled the coverage bitmap");
    }

    /* ---- bounds ---- */
    CHECK(font_get(&r, 9999) == 0, "an out-of-range face index returns NULL");
    CHECK(font_resolve(&r, SCRIPT_LATIN, FONT_STYLE_REGULAR) >= 0, "resolution still works");
    { font_registry_t empty; font_registry_init(&empty);
      CHECK(font_resolve(&empty, SCRIPT_LATIN, FONT_STYLE_REGULAR) == -1,
            "an empty registry resolves to -1 (nothing installed) rather than lying"); }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}

/* font.c — the font registry. See font.h. */
#include "font.h"

static void scpy(char *d, const char *s, uint32_t cap) {
    uint32_t i = 0; if (s) while (s[i] && i + 1u < cap) { d[i] = s[i]; i++; } d[i] = 0;
}

void font_registry_init(font_registry_t *r) {
    if (!r) return;
    for (uint32_t i = 0; i < sizeof(*r); i++) ((uint8_t *)r)[i] = 0;
    for (uint32_t i = 0; i < SCRIPT__COUNT; i++) r->def_for[i] = -1;
    r->fallback = -1;
}

void font_set_glyph_ops(font_registry_t *r, const font_glyph_ops_t *ops) {
    if (!r) return;
    if (ops) r->ops = *ops; else { r->ops.raster = 0; r->ops.ctx = 0; }
}

static int32_t reg(font_registry_t *r, const char *family, font_script_t sc,
                   font_style_t style, const char *asset,
                   const uint8_t *bytes, uint32_t len) {
    if (!r || (uint32_t)sc >= SCRIPT__COUNT) return -1;
    if (r->count >= FONT_MAX_FACES) return -1;
    int32_t idx = (int32_t)r->count++;
    font_face_t *f = &r->face[idx];
    f->in_use = true;
    scpy(f->family, family, FONT_FAMILY_LEN);
    f->script = sc; f->style = style;
    scpy(f->asset, asset, FONT_ASSET_LEN);
    f->embedded = bytes; f->embedded_len = len;
    /* first regular face for a script becomes its default */
    if (style == FONT_STYLE_REGULAR && r->def_for[sc] < 0) r->def_for[sc] = (int16_t)idx;
    /* first Latin face becomes the global fallback */
    if (sc == SCRIPT_LATIN && r->fallback < 0) r->fallback = (int16_t)idx;
    return idx;
}

int32_t font_register(font_registry_t *r, const char *family, font_script_t sc,
                      font_style_t style, const char *asset) {
    return reg(r, family, sc, style, asset, 0, 0);
}
int32_t font_register_embedded(font_registry_t *r, const char *family, font_script_t sc,
                               font_style_t style, const uint8_t *bytes, uint32_t len) {
    return reg(r, family, sc, style, "", bytes, len);
}

bool font_set_default(font_registry_t *r, font_script_t sc, int32_t face) {
    if (!r || (uint32_t)sc >= SCRIPT__COUNT) return false;
    if (face < 0 || (uint32_t)face >= r->count || !r->face[face].in_use) return false;
    r->def_for[sc] = (int16_t)face; return true;
}
bool font_set_fallback(font_registry_t *r, int32_t face) {
    if (!r || face < 0 || (uint32_t)face >= r->count || !r->face[face].in_use) return false;
    r->fallback = (int16_t)face; return true;
}

int32_t font_resolve(const font_registry_t *r, font_script_t sc, font_style_t pref) {
    if (!r || (uint32_t)sc >= SCRIPT__COUNT) return -1;
    /* COMMON has no font of its own — it is drawn by whatever run it joined;
     * resolve it to the fallback so a stray call still returns something. */
    if (sc == SCRIPT_COMMON || sc == SCRIPT_UNKNOWN) return r->fallback;

    /* 1: a face of the requested style for this script */
    for (uint32_t i = 0; i < r->count; i++)
        if (r->face[i].in_use && r->face[i].script == sc && r->face[i].style == pref)
            return (int32_t)i;
    /* 2: the script's default (first regular face) */
    if (r->def_for[sc] >= 0) return r->def_for[sc];
    /* 3: ANY face for this script */
    for (uint32_t i = 0; i < r->count; i++)
        if (r->face[i].in_use && r->face[i].script == sc) return (int32_t)i;
    /* 4: the global fallback */
    return r->fallback;
}

const font_face_t *font_get(const font_registry_t *r, int32_t face) {
    if (!r || face < 0 || (uint32_t)face >= r->count || !r->face[face].in_use) return 0;
    return &r->face[face];
}

/* ---- builtin 8x8 diagnostic glyphs (NOT a general font) ----
 * Just enough to render "ZXV" and a version string before the TrueType
 * rasterizer exists. MSB is the leftmost pixel of each row. */
struct g8 { uint32_t cp; uint8_t rows[8]; };
static const struct g8 BUILTIN[] = {
    { 0x20, {0,0,0,0,0,0,0,0} },                                  /* space */
    { 0x2E, {0,0,0,0,0,0x18,0x18,0} },                            /* .     */
    { 0x3A, {0,0x18,0x18,0,0,0x18,0x18,0} },                      /* :     */
    { 0x30, {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0} },             /* 0     */
    { 0x31, {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0} },             /* 1     */
    { 0x56, {0xC3,0xC3,0xC3,0x66,0x66,0x3C,0x3C,0x18} },          /* V     */
    { 0x58, {0xC3,0x66,0x3C,0x18,0x18,0x3C,0x66,0xC3} },          /* X     */
    { 0x5A, {0xFF,0x02,0x04,0x08,0x10,0x20,0x40,0xFF} },          /* Z     */
};
#define NBUILTIN (sizeof BUILTIN / sizeof BUILTIN[0])

static const struct g8 *builtin_find(uint32_t cp) {
    for (uint32_t i = 0; i < NBUILTIN; i++) if (BUILTIN[i].cp == cp) return &BUILTIN[i];
    return 0;
}
bool font_builtin_has(uint32_t cp) { return builtin_find(cp) != 0; }

bool font_glyph(const font_registry_t *r, int32_t face, uint32_t cp,
                uint8_t *bitmap, uint32_t w, uint32_t h) {
    if (!r || !bitmap || w == 0 || h == 0) return false;

    /* a real rasterizer, when bound, gets first refusal */
    if (r->ops.raster) {
        const font_face_t *f = font_get(r, face);
        if (f && r->ops.raster(f, cp, bitmap, w, h, r->ops.ctx) == 0) return true;
        /* fall through to the builtin only for the ASCII diagnostic case */
    }

    if (w == 8 && h == 8) {
        const struct g8 *g = builtin_find(cp);
        if (g) {
            for (uint32_t y = 0; y < 8; y++)
                for (uint32_t x = 0; x < 8; x++)
                    bitmap[y * 8 + x] = (g->rows[y] & (uint8_t)(0x80u >> x)) ? 0xFF : 0x00;
            return true;
        }
    }
    return false;   /* no glyph available — never fabricate one */
}

/* ---- TrueType glyph backend binding ---- */
#include "truetype.h"
static int font_ttf_raster(const font_face_t *face, uint32_t cp,
                           uint8_t *bitmap, uint32_t w, uint32_t h, void *ctx) {
    (void)face;
    ttf_font_t *ttf = (ttf_font_t *)ctx;
    if (!ttf) return -1;
    /* the glyph height is the box height; the rasteriser fits the em to it */
    return ttf_render_cp(ttf, cp, h, bitmap, w, h) ? 0 : -1;
}
void font_bind_ttf(font_registry_t *r, void *ttf_font) {
    if (!r) return;
    font_glyph_ops_t ops = { font_ttf_raster, ttf_font };
    font_set_glyph_ops(r, &ops);
}

/* refinery.c — the Magitech Refinery. See refinery.h. */
#include "refinery.h"
#include "../robin_debanks/sha256.h"

/* ------------------------------------------------------------- palette */
/* Full colour spectrum by root: the nine chakra-aligned hues, one per
 * semantic domain, replacing the fixed three-ink scheme of the printed
 * deck. Values are chosen to sit correctly on both the parchment day
 * field and the #002911 midnight-emerald night field. */
static const uint32_t ROOT_HUE[10] = {
    0x7A1C14u,          /* 0: unreachable — the old rubric, kept as fallback */
    0xC0392Bu,          /* 1 Monadic     — root red        */
    0xE67E22u,          /* 2 Dyadic      — sacral orange   */
    0xF1C40Fu,          /* 3 Triadic     — solar gold      */
    0x27AE60u,          /* 4 Tetradic    — heart green     */
    0x1ABC9Cu,          /* 5 Pentadic    — turquoise       */
    0x2E86DEu,          /* 6 Hexadic     — throat blue     */
    0x5B2C91u,          /* 7 Heptadic    — ajna indigo     */
    0x8E44ADu,          /* 8 Octadic     — crown violet    */
    0x66023Cu           /* 9 Enneadic    — tyrian purple, the house colour */
};

/* Lighten an RGB888 colour toward white by pct/100 — night traces keep
 * their root hue but must stay legible on the midnight-emerald field. */
static uint32_t lighten(uint32_t rgb, uint32_t pct) {
    uint32_t r = (rgb >> 16) & 0xFFu, g = (rgb >> 8) & 0xFFu, b = rgb & 0xFFu;
    r += (255u - r) * pct / 100u;
    g += (255u - g) * pct / 100u;
    b += (255u - b) * pct / 100u;
    return (r << 16) | (g << 8) | b;
}

static ref_palette_t palette_for(uint32_t root, bool night) {
    ref_palette_t p;
    p.bg     = night ? 0x002911u : 0xEFE3C2u;
    p.ink    = night ? 0xD4AF37u : 0x0A0A0Au;   /* gold fabric at night */
    p.trace  = ROOT_HUE[(root <= 9u) ? root : 0u];
    /* accent = the mirror root's hue: the grammar's reflective pair made
     * visible. Root 9 mirrors itself and accents in gold. */
    uint32_t m = (root >= 1u && root <= 9u) ? ((root == 9u) ? 9u : 9u - root) : 0u;
    p.accent = (root == 9u) ? 0xD4AF37u : ROOT_HUE[m];
    if (night) {                      /* keep hue identity, gain contrast */
        p.trace  = lighten(p.trace, 35u);
        p.accent = lighten(p.accent, 20u);
    }
    return p;
}

/* ------------------------------------------------------------ the forge */
ref_status_t ref_forge(const char *text, uint32_t len, eno_voice_t voice,
                       ref_card_t *out) {
    if (!text || !out) return REF_ERR_ARG;
    if (len == 0 || len > REF_TEXT_MAX) return REF_ERR_TEXT;

    for (uint32_t i = 0; i < REF_TEXT_MAX; i++) out->text[i] = 0;
    for (uint32_t i = 0; i < len; i++) out->text[i] = text[i];
    out->text_len = (uint8_t)len;
    out->voice    = (uint8_t)voice;

    /* language layer: gematria and root over the folded letters */
    out->gematria = eno_gematria(text, len);
    out->root     = (uint8_t)eno_root(out->gematria);

    /* the circuit: SHA-256 of the text bytes, exactly as the deck's
     * generator hashed the Enochian word */
    sha256((const uint8_t *)text, len, out->digest);

    /* first occurrence of each hex-digit value, in stream order, mapped
     * v -> (v%5, v/5). The %25 is kept for byte parity with the
     * generator even though a hex digit cannot exceed 15. */
    sig_init(&out->sigil, out->gematria);
    out->path_len = 0;
    bool seen[25];
    for (uint32_t i = 0; i < 25u; i++) seen[i] = false;
    for (uint32_t i = 0; i < 64u; i++) {
        uint8_t byte = out->digest[i / 2u];
        uint8_t dig  = (i & 1u) ? (byte & 0x0Fu) : (byte >> 4);
        uint8_t v    = dig % 25u;
        if (seen[v]) continue;
        seen[v] = true;
        uint8_t col = v % REF_KAMEA, row = v / REF_KAMEA;
        if (!sig_add_node(&out->sigil, col, row)) break;
        int32_t idx = sig_find_node(&out->sigil, col, row);
        if (out->path_len && idx >= 0)
            sig_add_edge(&out->sigil, out->path[out->path_len - 1u], (uint8_t)idx);
        if (idx >= 0 && out->path_len < 16u)
            out->path[out->path_len++] = (uint8_t)idx;
    }

    /* the fabric: N = root+3, k = N/2 - 1 — the generator's exact rule */
    out->sigil.fab_n = (uint8_t)(out->root + 3u);
    out->sigil.fab_k = (uint8_t)((out->sigil.fab_n / 2u) - 1u);
    out->sigil.pitch = 49u;               /* the shipped deck's lattice pitch */

    out->pal = palette_for(out->root, false);

    /* the effect: run the card through the activation VM so a forged card
     * IS an equipable Chiglet module with no second path to maintain */
    zca_attrs_t at;
    /* Synthetic card id from the digest, NOT from gematria*100+root: many
     * texts share a gematria and root, and a colliding id would make
     * loadout_equip refuse the second of two genuinely different presets
     * as a "duplicate physical card". Digest bytes are unique per text. */
    at.index      = ((uint32_t)out->digest[3] << 24) |
                    ((uint32_t)out->digest[4] << 16) |
                    ((uint32_t)out->digest[5] << 8)  |
                     (uint32_t)out->digest[6];
    at.discipline = (uint8_t)(out->digest[0] % 38u);  /* hash-chosen school */
    at.set        = (uint8_t)(1u + (out->digest[1] % 3u));
    at.gematria   = (uint16_t)(out->gematria > 65535u ? 65535u : out->gematria);
    at.root       = out->root;
    at.tarot      = out->digest[2];
    zca_program_t prog;
    if (zca_derive(&at, &prog) == ZCA_OK) zca_exec(&prog, &out->fx);
    else {
        for (uint32_t d = 0; d < CHG_DIM; d++) out->fx.evidence[d] = SR_ZERO;
        out->fx.grants = 0;
    }
    return REF_OK;
}

/* ----------------------------------------------------------- the renderer */
/* Integer sin/cos in milli-units on a 96-step circle: exact symmetry, no
 * float, no libm. Covers every N=4..12 vertex angle the fabric can need
 * (all divide 96 except 7 and 11, which land within one table step —
 * 3.75deg — of true; the seal reads identically at card scale). */
static const int16_t SIN96[25] = {      /* first quadrant, milli */
      0,  65, 131, 195, 259, 321, 383, 442, 500, 556,
    609, 659, 707, 752, 793, 831, 866, 897, 924, 946,
    966, 981, 991, 998, 1000
};
static int32_t msin(uint32_t step96) {
    step96 %= 96u;
    if (step96 <= 24u)  return  SIN96[step96];
    if (step96 <= 48u)  return  SIN96[48u - step96];
    if (step96 <= 72u)  return -SIN96[step96 - 48u];
    return -SIN96[96u - step96];
}
static int32_t mcos(uint32_t step96) { return msin(step96 + 24u); }

static uint32_t put_stroke(ref_stroke_t *out, uint32_t cap, uint32_t n,
                           ref_skind_t k, uint8_t layer,
                           int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                           uint32_t rgb, uint16_t w) {
    if (n >= cap) return n;
    out[n].kind = (uint8_t)k; out[n].layer = layer;
    out[n].x1 = (uint16_t)x1; out[n].y1 = (uint16_t)y1;
    out[n].x2 = (uint16_t)x2; out[n].y2 = (uint16_t)y2;
    out[n].rgb = rgb; out[n].width_milli = w;
    return n + 1u;
}

uint32_t ref_render(const ref_card_t *c, bool night,
                    ref_stroke_t *out, uint32_t cap) {
    if (!c || !out || !cap) return 0;
    ref_palette_t pal = palette_for(c->root, night);
    uint32_t n = 0;

    /* card frame */
    n = put_stroke(out, cap, n, REF_S_LINE, 0,  40,  40, 960,  40, pal.accent, 10);
    n = put_stroke(out, cap, n, REF_S_LINE, 0, 960,  40, 960,1360, pal.accent, 10);
    n = put_stroke(out, cap, n, REF_S_LINE, 0, 960,1360,  40,1360, pal.accent, 10);
    n = put_stroke(out, cap, n, REF_S_LINE, 0,  40,1360,  40,  40, pal.accent, 10);

    /* seal rings, concentric — centre (500,430), R 330, as the deck */
    const int32_t cx = 500, cy = 430, R = 330;
    n = put_stroke(out, cap, n, REF_S_CIRCLE, 1, cx, cy, R,           0, pal.ink, 6);
    n = put_stroke(out, cap, n, REF_S_CIRCLE, 1, cx, cy, R * 94/100,  0, pal.ink, 2);
    n = put_stroke(out, cap, n, REF_S_CIRCLE, 1, cx, cy, R * 85/100,  0, pal.ink, 4);

    /* the star fabric {N/k}: ONE orbit, +k from vertex 0 until it closes,
     * exactly as the generator inked it. Vertex 0 sits at 90deg (top). */
    uint32_t N = c->sigil.fab_n ? c->sigil.fab_n : 12u;
    uint32_t K = c->sigil.fab_k ? c->sigil.fab_k : 5u;
    int32_t sr = R * 75 / 100;
    uint32_t cur = 0;
    for (uint32_t guard = 0; guard <= N; guard++) {
        uint32_t nxt = (cur + K) % N;
        /* step 24 = 90deg; each vertex advances 96/N of the circle */
        uint32_t a1 = 24u + cur * 96u / N, a2 = 24u + nxt * 96u / N;
        n = put_stroke(out, cap, n, REF_S_LINE, 1,
                       cx + sr * mcos(a1) / 1000, cy - sr * msin(a1) / 1000,
                       cx + sr * mcos(a2) / 1000, cy - sr * msin(a2) / 1000,
                       pal.ink, 6);
        cur = nxt;
        if (cur == 0) break;
    }

    /* the circuit: kamea grid path in the root's spectrum hue */
    if (c->path_len) {
        int32_t grid = R * 12 / 10, cell = grid / (int32_t)REF_KAMEA;
        int32_t gx = cx - grid / 2, gy = cy - grid / 2;
        int32_t px = 0, py = 0;
        for (uint32_t i = 0; i < c->path_len; i++) {
            const sig_pt_t *p = &c->sigil.node[c->path[i]];
            int32_t x = gx + ((int32_t)p->col * 2 + 1) * cell / 2;
            int32_t y = gy + ((int32_t)p->row * 2 + 1) * cell / 2;
            if (i) n = put_stroke(out, cap, n, REF_S_LINE, 2, px, py, x, y,
                                  pal.trace, 4);
            px = x; py = y;
        }
        /* start = filled dot, end = open circle: the trace's direction
         * marks, so stroke ORDER survives rendering */
        const sig_pt_t *s0 = &c->sigil.node[c->path[0]];
        const sig_pt_t *s1 = &c->sigil.node[c->path[c->path_len - 1u]];
        int32_t x0 = gx + ((int32_t)s0->col * 2 + 1) * cell / 2;
        int32_t y0 = gy + ((int32_t)s0->row * 2 + 1) * cell / 2;
        int32_t x1 = gx + ((int32_t)s1->col * 2 + 1) * cell / 2;
        int32_t y1 = gy + ((int32_t)s1->row * 2 + 1) * cell / 2;
        n = put_stroke(out, cap, n, REF_S_DOT,    3, x0, y0, 10, 0, pal.ink,    0);
        n = put_stroke(out, cap, n, REF_S_CIRCLE, 3, x1, y1, 15, 0, pal.accent, 5);
    }

    /* centre boss */
    n = put_stroke(out, cap, n, REF_S_CIRCLE, 1, cx, cy, R / 4, 0, pal.ink, 4);
    /* the intent line anchor (text layer renders the actual glyphs) */
    n = put_stroke(out, cap, n, REF_S_TEXT, 3, 500, 1100, 0, 0, pal.trace, 0);
    return n;
}

/* ----------------------------------------------------- activation line */
static uint32_t sput(char *o, uint32_t cap, uint32_t at, const char *s) {
    while (*s && at + 1u < cap) o[at++] = *s++;
    return at;
}
static uint32_t sputu(char *o, uint32_t cap, uint32_t at, uint32_t v) {
    char t[12]; uint32_t k = 0;
    if (!v) t[k++] = '0';
    while (v) { t[k++] = (char)('0' + v % 10u); v /= 10u; }
    while (k-- && at + 1u < cap) o[at++] = t[k];
    return at;
}

uint32_t ref_activation_line(const ref_card_t *c, char *out, uint32_t cap) {
    if (!c || !out || cap < 8u) return 0;
    eno_voice_t v = (eno_voice_t)c->voice;
    uint32_t at = 0;
    at = sput(out, cap, at, eno_voice_verb(v));
    at = sput(out, cap, at, " the seal of \"");
    for (uint32_t i = 0; i < c->text_len && at + 1u < cap; i++) out[at++] = c->text[i];
    at = sput(out, cap, at, "\" (");
    at = sput(out, cap, at, eno_root_domain(c->root));
    at = sput(out, cap, at, ", gematria ");
    at = sputu(out, cap, at, c->gematria);
    at = sput(out, cap, at, "). ");
    at = sput(out, cap, at, eno_voice_manner(v));
    at = sput(out, cap, at, ".");
    out[at] = 0;
    return at;
}

/* -------------------------------------------------- the shareable preset */
static uint16_t crc16(uint16_t crc, uint8_t byte) {
    crc ^= (uint16_t)byte << 8;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                              : (uint16_t)(crc << 1);
    return crc;
}

uint32_t ref_preset_pack(const ref_card_t *c, uint8_t *out, uint32_t cap) {
    if (!c || !out) return 0;
    uint32_t need = 4u + 1u + 1u + 2u + 4u + 1u + (uint32_t)c->text_len + 32u + 2u;
    if (cap < need) return 0;
    uint32_t at = 0;
    out[at++] = (uint8_t)(REF_PRESET_MAGIC);
    out[at++] = (uint8_t)(REF_PRESET_MAGIC >> 8);
    out[at++] = (uint8_t)(REF_PRESET_MAGIC >> 16);
    out[at++] = (uint8_t)(REF_PRESET_MAGIC >> 24);
    out[at++] = c->voice;
    out[at++] = c->root;
    out[at++] = (uint8_t)(c->gematria);
    out[at++] = (uint8_t)(c->gematria >> 8);
    out[at++] = (uint8_t)(c->gematria >> 16);
    out[at++] = (uint8_t)(c->gematria >> 24);
    out[at++] = 0;                                   /* reserved */
    out[at++] = 0;
    out[at++] = c->text_len;
    for (uint32_t i = 0; i < c->text_len; i++) out[at++] = (uint8_t)c->text[i];
    for (uint32_t i = 0; i < 32u; i++) out[at++] = c->digest[i];
    uint16_t s = 0xFFFFu;
    for (uint32_t i = 0; i < at; i++) s = crc16(s, out[i]);
    out[at++] = (uint8_t)(s >> 8);
    out[at++] = (uint8_t)(s);
    return at;
}

ref_status_t ref_preset_unpack(const uint8_t *blob, uint32_t len,
                               ref_card_t *out) {
    if (!blob || !out) return REF_ERR_ARG;
    if (len < 4u + 9u + 32u + 2u) return REF_ERR_SEAL;
    uint32_t magic = (uint32_t)blob[0] | ((uint32_t)blob[1] << 8) |
                     ((uint32_t)blob[2] << 16) | ((uint32_t)blob[3] << 24);
    if (magic != REF_PRESET_MAGIC) return REF_ERR_SEAL;

    /* seal first: refuse a damaged blob before reading anything from it */
    uint16_t s = 0xFFFFu;
    for (uint32_t i = 0; i + 2u < len; i++) s = crc16(s, blob[i]);
    uint16_t claimed = (uint16_t)(((uint16_t)blob[len - 2u] << 8) | blob[len - 1u]);
    if (s != claimed) return REF_ERR_SEAL;

    uint8_t  voice = blob[4];
    uint8_t  root  = blob[5];
    uint32_t gem   = (uint32_t)blob[6] | ((uint32_t)blob[7] << 8) |
                     ((uint32_t)blob[8] << 16) | ((uint32_t)blob[9] << 24);
    uint8_t  tlen  = blob[12];
    if (tlen == 0 || tlen > REF_TEXT_MAX) return REF_ERR_SEAL;
    if (len != 4u + 9u + (uint32_t)tlen + 32u + 2u) return REF_ERR_SEAL;
    const char *text = (const char *)&blob[13];
    const uint8_t *digest = &blob[13u + tlen];

    /* THE VERIFICATION: re-forge from the text and compare every claim.
     * The preset does not carry authority — the derivation does. A blob
     * whose stated root or digest disagrees with what the text derives
     * is a forgery and is refused whole. */
    ref_status_t st = ref_forge(text, tlen, (eno_voice_t)(voice % 3u), out);
    if (st != REF_OK) return st;
    if (out->gematria != gem || out->root != root) return REF_ERR_FORGERY;
    for (uint32_t i = 0; i < 32u; i++)
        if (out->digest[i] != digest[i]) return REF_ERR_FORGERY;
    return REF_OK;
}

const char *ref_status_name(ref_status_t s) {
    switch (s) {
    case REF_OK:          return "OK";
    case REF_ERR_ARG:     return "ERR_ARG";
    case REF_ERR_TEXT:    return "ERR_TEXT";
    case REF_ERR_SEAL:    return "ERR_SEAL";
    case REF_ERR_FORGERY: return "ERR_FORGERY";
    default:              return "?";
    }
}

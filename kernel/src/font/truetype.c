/* truetype.c — a TrueType outline rasteriser. See truetype.h. */
#include "truetype.h"

/* ---- bounds-checked big-endian readers (the file is untrusted) ---- */
static uint32_t rd8(const uint8_t *d, uint32_t len, uint32_t o) {
    return o < len ? d[o] : 0;
}
static uint32_t rd16(const uint8_t *d, uint32_t len, uint32_t o) {
    if (o + 2u > len) return 0;
    return ((uint32_t)d[o] << 8) | d[o+1];
}
static int32_t rd16s(const uint8_t *d, uint32_t len, uint32_t o) {
    return (int32_t)(int16_t)(uint16_t)rd16(d, len, o);
}
static uint32_t rd32(const uint8_t *d, uint32_t len, uint32_t o) {
    if (o + 4u > len) return 0;
    return ((uint32_t)d[o]<<24)|((uint32_t)d[o+1]<<16)|((uint32_t)d[o+2]<<8)|d[o+3];
}
static uint32_t tag4(char a, char b, char c, char e) {
    return ((uint32_t)(uint8_t)a<<24)|((uint32_t)(uint8_t)b<<16)|((uint32_t)(uint8_t)c<<8)|(uint8_t)e;
}

bool ttf_parse(ttf_font_t *f, const uint8_t *data, uint32_t len) {
    if (!f || !data) return false;
    for (uint32_t i = 0; i < sizeof(*f); i++) ((uint8_t*)f)[i] = 0;
    f->data = data; f->len = len;
    if (len < 12) return false;

    uint32_t num_tables = rd16(data, len, 4);
    if (num_tables > 64u) return false;             /* absurd */
    uint32_t head=0, maxp=0, cmap=0, hlen_head=0, hlen_maxp=0, hlen_cmap=0;

    for (uint32_t i = 0; i < num_tables; i++) {
        uint32_t rec = 12u + i * 16u;
        if (rec + 16u > len) return false;
        uint32_t tg = rd32(data, len, rec);
        uint32_t off = rd32(data, len, rec + 8);
        uint32_t tl  = rd32(data, len, rec + 12);
        if (off > len || tl > len - off) continue;   /* skip a lying record */
        if      (tg == tag4('h','e','a','d')) { head = off; hlen_head = tl; }
        else if (tg == tag4('m','a','x','p')) { maxp = off; hlen_maxp = tl; }
        else if (tg == tag4('c','m','a','p')) { cmap = off; hlen_cmap = tl; }
        else if (tg == tag4('l','o','c','a')) { f->loca_off = off; f->loca_len = tl; }
        else if (tg == tag4('g','l','y','f')) { f->glyf_off = off; f->glyf_len = tl; }
    }
    if (!head || !maxp || !cmap || !f->loca_off || !f->glyf_off) return false;
    if (hlen_head < 54u || hlen_maxp < 6u) return false;

    f->units_per_em = rd16(data, len, head + 18);
    if (f->units_per_em == 0) return false;
    f->loca_long = rd16s(data, len, head + 50) != 0;
    f->num_glyphs = rd16(data, len, maxp + 4);
    if (f->num_glyphs == 0) return false;

    /* pick a Unicode format-4 cmap subtable */
    uint32_t nsub = rd16(data, len, cmap + 2);
    (void)hlen_cmap;
    uint32_t best = 0;
    for (uint32_t i = 0; i < nsub && i < 64u; i++) {
        uint32_t rec = cmap + 4u + i * 8u;
        uint32_t plat = rd16(data, len, rec);
        uint32_t enc  = rd16(data, len, rec + 2);
        uint32_t soff = rd32(data, len, rec + 4);
        uint32_t sub  = cmap + soff;
        if (sub + 4u > len) continue;
        if (rd16(data, len, sub) != 4u) continue;    /* format 4 only */
        bool unicode = (plat == 3u && (enc == 1u || enc == 10u)) || plat == 0u;
        if (unicode && !best) best = sub;
        else if (!best) best = sub;                   /* any format-4 as fallback */
    }
    if (!best) return false;
    f->cmap_sub_off = best;
    f->valid = true;
    return true;
}

uint32_t ttf_glyph_index(const ttf_font_t *f, uint32_t cp) {
    if (!f || !f->valid || cp > 0xFFFFu) return 0;   /* format 4 is BMP */
    const uint8_t *d = f->data; uint32_t len = f->len, s = f->cmap_sub_off;
    uint32_t segX2 = rd16(d, len, s + 6);
    uint32_t segs = segX2 / 2u;
    if (segs == 0 || segs > 0x4000u) return 0;
    uint32_t endO   = s + 14u;
    uint32_t startO = endO + segX2 + 2u;
    uint32_t deltaO = startO + segX2;
    uint32_t rangeO = deltaO + segX2;
    if (rangeO + segX2 > len) return 0;

    for (uint32_t i = 0; i < segs; i++) {
        uint32_t end = rd16(d, len, endO + i*2u);
        if (cp <= end) {
            uint32_t start = rd16(d, len, startO + i*2u);
            if (cp < start) return 0;
            int32_t delta = rd16s(d, len, deltaO + i*2u);
            uint32_t ro = rd16(d, len, rangeO + i*2u);
            if (ro == 0) return (uint32_t)((cp + (uint32_t)delta) & 0xFFFFu);
            /* glyphId at rangeO[i] + ro + 2*(cp-start) */
            uint32_t gaddr = rangeO + i*2u + ro + 2u*(cp - start);
            uint32_t gid = rd16(d, len, gaddr);
            if (gid == 0) return 0;
            return (uint32_t)((gid + (uint32_t)delta) & 0xFFFFu);
        }
    }
    return 0;
}

/* glyph byte range from loca */
static bool glyph_range(const ttf_font_t *f, uint32_t gid, uint32_t *off, uint32_t *end) {
    if (gid + 1u > f->num_glyphs) return false;
    if (f->loca_long) {
        *off = rd32(f->data, f->len, f->loca_off + gid*4u);
        *end = rd32(f->data, f->len, f->loca_off + (gid+1u)*4u);
    } else {
        *off = rd16(f->data, f->len, f->loca_off + gid*2u) * 2u;
        *end = rd16(f->data, f->len, f->loca_off + (gid+1u)*2u) * 2u;
    }
    return *end >= *off && f->glyf_off + *end <= f->len;
}

#define TT_MAX_PTS  1024u
#define TT_MAX_SEG  4096u
#define TT_QUAD_STEP 6u

typedef struct { int32_t x, y; uint8_t on; } tt_pt;

/* parse a SIMPLE glyph into points + contour end indices. Returns numPoints,
 * or 0 for empty/composite/malformed. */
static uint32_t parse_simple(const ttf_font_t *f, uint32_t goff, uint32_t glen,
                             tt_pt *pts, uint16_t *ends, uint32_t *nc) {
    const uint8_t *d = f->data; uint32_t len = f->len;
    uint32_t g = f->glyf_off + goff;
    if (glen < 10u) return 0;
    int32_t ncont = rd16s(d, len, g);
    if (ncont <= 0) return 0;                        /* empty or composite */
    if ((uint32_t)ncont > 64u) return 0;
    uint32_t at = g + 10u;
    uint32_t npts = 0;
    for (int32_t c = 0; c < ncont; c++) {
        uint32_t e = rd16(d, len, at); at += 2u;
        ends[c] = (uint16_t)e;
        npts = e + 1u;
    }
    *nc = (uint32_t)ncont;
    if (npts == 0 || npts > TT_MAX_PTS) return 0;
    uint32_t ilen = rd16(d, len, at); at += 2u + ilen;   /* skip hinting */

    /* flags with repeat */
    uint8_t flags[TT_MAX_PTS];
    for (uint32_t i = 0; i < npts; ) {
        if (at >= len) return 0;
        uint8_t fl = (uint8_t)rd8(d, len, at++);
        flags[i++] = fl;
        if (fl & 0x08u) {                            /* repeat */
            uint32_t rep = rd8(d, len, at++);
            while (rep-- && i < npts) flags[i++] = fl;
        }
    }
    /* x coords */
    int32_t x = 0;
    for (uint32_t i = 0; i < npts; i++) {
        uint8_t fl = flags[i];
        if (fl & 0x02u) { uint32_t dx = rd8(d, len, at++); x += (fl & 0x10u) ? (int32_t)dx : -(int32_t)dx; }
        else if (!(fl & 0x10u)) { x += rd16s(d, len, at); at += 2u; }
        pts[i].x = x; pts[i].on = (fl & 0x01u) ? 1u : 0u;
    }
    /* y coords */
    int32_t y = 0;
    for (uint32_t i = 0; i < npts; i++) {
        uint8_t fl = flags[i];
        if (fl & 0x04u) { uint32_t dy = rd8(d, len, at++); y += (fl & 0x20u) ? (int32_t)dy : -(int32_t)dy; }
        else if (!(fl & 0x20u)) { y += rd16s(d, len, at); at += 2u; }
        pts[i].y = y;
    }
    return npts;
}

/* emit a line segment into the edge list */
static void seg_add(int32_t *sx, int32_t *sy, int32_t *ex, int32_t *ey, uint32_t *ns,
                    int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    if (*ns >= TT_MAX_SEG) return;
    if (ay == by) return;                            /* horizontal: no crossing */
    sx[*ns]=ax; sy[*ns]=ay; ex[*ns]=bx; ey[*ns]=by; (*ns)++;
}

/* flatten a quadratic bezier (a -> ctrl -> b) into line segments */
static void quad_flatten(int32_t *sx,int32_t *sy,int32_t *ex,int32_t *ey,uint32_t *ns,
                         int32_t ax,int32_t ay,int32_t cx,int32_t cy,int32_t bx,int32_t by) {
    int32_t px = ax, py = ay;
    for (uint32_t s = 1; s <= TT_QUAD_STEP; s++) {
        int32_t t = (int32_t)((s * 1024u) / TT_QUAD_STEP);   /* t in 0..1024 */
        int32_t mt = 1024 - t;
        /* B(t) = mt^2 A + 2 mt t C + t^2 B, in 1024 fixed */
        int64_t qx = ((int64_t)mt*mt*ax + 2LL*mt*t*cx + (int64_t)t*t*bx) / (1024*1024);
        int64_t qy = ((int64_t)mt*mt*ay + 2LL*mt*t*cy + (int64_t)t*t*by) / (1024*1024);
        seg_add(sx,sy,ex,ey,ns, px,py,(int32_t)qx,(int32_t)qy);
        px = (int32_t)qx; py = (int32_t)qy;
    }
}

bool ttf_render(const ttf_font_t *f, uint32_t gid, uint32_t size,
                uint8_t *bitmap, uint32_t w, uint32_t h) {
    if (!f || !f->valid || !bitmap || w == 0 || h == 0 || size == 0) return false;
    for (uint32_t i = 0; i < w*h; i++) bitmap[i] = 0;

    uint32_t goff, gend;
    if (!glyph_range(f, gid, &goff, &gend)) return false;
    if (gend == goff) return false;                  /* empty glyph (e.g. space) */

    static tt_pt pts[TT_MAX_PTS];
    static uint16_t ends[64];
    uint32_t nc = 0;
    uint32_t npts = parse_simple(f, goff, gend - goff, pts, ends, &nc);
    if (npts == 0) return false;                      /* composite / malformed */

    /* build edge list in FONT UNITS by flattening each contour */
    static int32_t sx[TT_MAX_SEG], sy[TT_MAX_SEG], ex[TT_MAX_SEG], ey[TT_MAX_SEG];
    uint32_t ns = 0;
    uint32_t start = 0;
    for (uint32_t c = 0; c < nc; c++) {
        uint32_t last = ends[c];
        if (last < start || last >= npts) break;
        uint32_t n = last - start + 1u;
        if (n < 2u) { start = last + 1u; continue; }

        /* find a starting on-curve point (synthesise one if none) */
        tt_pt cur;
        uint32_t i0;
        if (pts[start].on) { cur = pts[start]; i0 = start + 1u; }
        else if (pts[last].on) { cur = pts[last]; i0 = start; }
        else { /* both off: midpoint */
            cur.x = (pts[start].x + pts[last].x)/2; cur.y = (pts[start].y + pts[last].y)/2; cur.on = 1;
            i0 = start;
        }
        tt_pt begin = cur;
        int32_t ctrlx = 0, ctrly = 0; bool have_ctrl = false;
        for (uint32_t k = 0; k <= n; k++) {
            uint32_t idx = start + ((i0 - start) + k) % n;
            tt_pt p = pts[idx];
            if (k == n) p = begin;                    /* close the contour */
            if (p.on) {
                if (have_ctrl) { quad_flatten(sx,sy,ex,ey,&ns, cur.x,cur.y, ctrlx,ctrly, p.x,p.y); have_ctrl=false; }
                else seg_add(sx,sy,ex,ey,&ns, cur.x,cur.y, p.x,p.y);
                cur = p;
            } else {
                if (have_ctrl) { /* two off-curve: implied on-curve midpoint */
                    int32_t mx=(ctrlx+p.x)/2, my=(ctrly+p.y)/2;
                    quad_flatten(sx,sy,ex,ey,&ns, cur.x,cur.y, ctrlx,ctrly, mx,my);
                    cur.x=mx; cur.y=my; cur.on=1;
                }
                ctrlx = p.x; ctrly = p.y; have_ctrl = true;
            }
        }
        start = last + 1u;
    }
    if (ns == 0) return false;

    /* supersampled non-zero-winding fill. Map each sub-sample pixel back to
     * font coords and ray-cast against the edge list. y is flipped (font y is
     * up); the glyph is scaled so `size` font-em-units map to `size` pixels and
     * the baseline sits near the bottom of the box. */
    const uint32_t S = 3u;                            /* 3x3 supersampling */
    uint32_t E = f->units_per_em;
    for (uint32_t oy = 0; oy < h; oy++) {
        for (uint32_t ox = 0; ox < w; ox++) {
            uint32_t inside = 0;
            for (uint32_t syi = 0; syi < S; syi++) {
                for (uint32_t sxi = 0; sxi < S; sxi++) {
                    /* sub-pixel centre in bitmap space */
                    int64_t bxp = (int64_t)ox*2*S + sxi*2 + 1;   /* *2S scale */
                    int64_t byp = (int64_t)oy*2*S + syi*2 + 1;
                    /* -> font units: fx = bxp/(2S) * E/size ; fy flipped */
                    int64_t fx = bxp * (int64_t)E / (2LL*S*size);
                    int64_t fy = ((int64_t)h*2*S - byp) * (int64_t)E / (2LL*S*size);
                    int32_t wind = 0;
                    for (uint32_t e = 0; e < ns; e++) {
                        int32_t ay=sy[e], by=ey[e];
                        bool up = by > ay;
                        int32_t lo = up?ay:by, hi = up?by:ay;
                        if (fy < lo || fy >= hi) continue;         /* half-open */
                        int64_t xint = (int64_t)sx[e] +
                            (int64_t)(fy - ay) * (ex[e]-sx[e]) / (by - ay);
                        if (xint > fx) wind += up ? 1 : -1;
                    }
                    if (wind != 0) inside++;
                }
            }
            bitmap[oy*w + ox] = (uint8_t)((inside * 255u) / (S*S));
        }
    }
    return true;
}

bool ttf_render_cp(const ttf_font_t *f, uint32_t cp, uint32_t size,
                   uint8_t *bitmap, uint32_t w, uint32_t h) {
    return ttf_render(f, ttf_glyph_index(f, cp), size, bitmap, w, h);
}

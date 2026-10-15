/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_image.c — format sniffing, the small decoders (GIF, BMP, netpbm, QOI,
 * TGA), orientation and the scaler. PNG is zui_png.c, JPEG zui_jpeg.c. */
#include "zx_image_int.h"

/* ---- shared helpers ----------------------------------------------------- */
int zi_dims(zx_image_info_t *info, uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) return zi_fail(info, ZX_IMG_E_DATA, "zero width or height");
    if (w > ZX_IMG_MAX_DIM || h > ZX_IMG_MAX_DIM || (uint64_t) w * h > ZX_IMG_MAX_PIXELS)
        return zi_fail(info, ZX_IMG_E_SIZE, "larger than the image caps");
    return ZX_IMG_OK;
}

void zi_row(const zi_out_t *o, uint32_t y, int64_t *base, int64_t *step)
{
    int64_t sw = o->sw, sh = o->sh, Y = y;
    switch (o->orient) {
    case 2:
        *base = Y * sw + sw - 1, *step = -1;
        break;
    case 3:
        *base = (sh - 1 - Y) * sw + sw - 1, *step = -1;
        break;
    case 4:
        *base = (sh - 1 - Y) * sw, *step = 1;
        break;
    case 5:
        *base = Y, *step = sh;
        break;
    case 6:
        *base = sh - 1 - Y, *step = sh;
        break;
    case 7:
        *base = (sw - 1) * sh + sh - 1 - Y, *step = -sh;
        break;
    case 8:
        *base = (sw - 1) * sh + Y, *step = -sh;
        break;
    default:
        *base = Y * sw, *step = 1;
    }
}

uint8_t zi_exif_orientation(const uint8_t *t, uint32_t n)
{
    if (n < 8) return 1;
    bool le = t[0] == 'I' && t[1] == 'I';
    if (!le && !(t[0] == 'M' && t[1] == 'M')) return 1;
#define U16(p) (le ? zi_le16(p) : zi_be16(p))
#define U32(p) (le ? zi_le32(p) : zi_be32(p))
    if (U16(t + 2) != 42) return 1;
    uint32_t ifd = U32(t + 4);
    if (ifd > n - 2) return 1;
    uint32_t cnt = U16(t + ifd);
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t e = ifd + 2 + 12 * i;
        if (e > n - 12) return 1;
        if (U16(t + e) == 0x0112 && U16(t + e + 2) == 3 && U32(t + e + 4) == 1) {
            uint32_t v = U16(t + e + 8);
            return v >= 1 && v <= 8 ? (uint8_t) v : 1;
        }
    }
#undef U16
#undef U32
    return 1;
}

/* ---- GIF ---------------------------------------------------------------- */
typedef struct {
    uint16_t prefix[4096];
    uint8_t suffix[4096];
    uint8_t stack[4097];
} lzw_t;

typedef struct {
    const uint8_t *d;
    uint32_t n, pos, left;
    bool end, trunc;
} sub_t;

static int sub_byte(sub_t *s)
{
    if (s->end) return -1;
    if (!s->left) {
        if (s->pos >= s->n) {
            s->trunc = s->end = true;
            return -1;
        }
        s->left = s->d[s->pos++];
        if (!s->left) {
            s->end = true;
            return -1;
        }
    }
    if (s->pos >= s->n) {
        s->trunc = s->end = true;
        return -1;
    }
    s->left--;
    return s->d[s->pos++];
}

/* Skip a sub-block chain at *pos; false when it runs off the end. */
static bool skip_subs(const uint8_t *d, uint32_t n, uint32_t *pos)
{
    for (;;) {
        if (*pos >= n) return false;
        uint32_t l = d[(*pos)++];
        if (!l) return true;
        if (n - *pos < l) return false;
        *pos += l;
    }
}

/* Walk the whole block structure: frames counted, trailer required. */
static int gif_walk(const uint8_t *d, uint32_t n, zx_image_info_t *info, uint32_t *start)
{
    if (n < 6) return zi_fail(info, ZX_IMG_E_TRUNC, "GIF: header cut short");
    if (d[0] != 'G' || d[1] != 'I' || d[2] != 'F' || d[3] != '8' || (d[4] != '7' && d[4] != '9') ||
        d[5] != 'a')
        return zi_fail(info, ZX_IMG_E_FORMAT, "not a GIF");
    if (n < 13) return zi_fail(info, ZX_IMG_E_TRUNC, "GIF: header cut short");
    uint32_t w = zi_le16(d + 6), h = zi_le16(d + 8), pos = 13;
    int rc = zi_dims(info, w, h);
    if (rc) return rc;
    if (d[10] & 0x80) pos += 3u << ((d[10] & 7) + 1);
    if (pos > n) return zi_fail(info, ZX_IMG_E_TRUNC, "GIF: palette cut short");
    *start = pos;
    uint32_t frames = 0;
    for (;;) {
        if (pos >= n) return zi_fail(info, ZX_IMG_E_TRUNC, "GIF: no trailer");
        uint8_t b = d[pos++];
        if (b == 0x3B) break;
        if (b == 0x21) {
            if (pos >= n) return zi_fail(info, ZX_IMG_E_TRUNC, "GIF: extension cut short");
            pos++;
            if (!skip_subs(d, n, &pos))
                return zi_fail(info, ZX_IMG_E_TRUNC, "GIF: extension cut short");
        } else if (b == 0x2C) {
            if (n - pos < 10) return zi_fail(info, ZX_IMG_E_TRUNC, "GIF: image cut short");
            uint8_t pk = d[pos + 8];
            if (pk & 0x40) info->progressive = true;
            pos += 9;
            if (pk & 0x80) pos += 3u << ((pk & 7) + 1);
            if (pos >= n) return zi_fail(info, ZX_IMG_E_TRUNC, "GIF: image cut short");
            if (d[pos] < 2 || d[pos] > 8)
                return zi_fail(info, ZX_IMG_E_DATA, "GIF: bad LZW code size");
            pos++;
            if (!skip_subs(d, n, &pos))
                return zi_fail(info, ZX_IMG_E_TRUNC, "GIF: image data cut short");
            if (++frames > ZUI_GIF_MAX_FRAMES)
                return zi_fail(info, ZX_IMG_E_DATA, "GIF: too many frames");
        } else {
            return zi_fail(info, ZX_IMG_E_DATA, "GIF: bad block");
        }
    }
    if (!frames) return zi_fail(info, ZX_IMG_E_DATA, "GIF: no image");
    info->format = ZX_IMG_GIF;
    info->w = w;
    info->h = h;
    info->frames = frames;
    info->alpha = true;
    info->work = ZUI_GIF_WORK;
    return ZX_IMG_OK;
}

int zui_gif_open(zui_gif_t *g, const uint8_t *d, uint32_t n, uint32_t *canvas, uint32_t *prev,
                 void *work, uint32_t worklen)
{
    zx_image_info_t info;
    uint32_t start;
    if (!g || !d || !canvas || !work) return ZX_IMG_E_ARG;
    zi_zero(&info, sizeof info);
    int rc = gif_walk(d, n, &info, &start);
    if (rc) return rc;
    if (worklen < sizeof(lzw_t)) return ZX_IMG_E_SPACE;
    zi_zero(g, sizeof *g);
    g->d = d, g->n = n, g->w = info.w, g->h = info.h, g->pos = start;
    g->canvas = canvas, g->prev = prev, g->work = work, g->worklen = worklen;
    zui_gif_rewind(g);
    return ZX_IMG_OK;
}

void zui_gif_rewind(zui_gif_t *g)
{
    g->pos = 13;
    if (g->d[10] & 0x80) g->pos += 3u << ((g->d[10] & 7) + 1);
    g->frame = 0;
    g->dispose = 0;
    g->restore = false;
    for (uint32_t i = 0; i < g->w * g->h; i++) g->canvas[i] = 0;
}

static int gif_image(zui_gif_t *g, int trans, uint8_t dispose)
{
    const uint8_t *d = g->d;
    uint32_t pos = g->pos, n = g->n;
    uint32_t fx = zi_le16(d + pos), fy = zi_le16(d + pos + 2), fw = zi_le16(d + pos + 4),
             fh = zi_le16(d + pos + 6);
    uint8_t pk = d[pos + 8];
    pos += 9;
    const uint8_t *pal = 0;
    uint32_t npal = 0;
    if (pk & 0x80) {
        pal = d + pos, npal = 2u << (pk & 7);
        pos += 3 * npal;
    } else if (d[10] & 0x80) {
        pal = d + 13, npal = 2u << (d[10] & 7);
    }
    if (!pal) return ZX_IMG_E_DATA;
    if (pos >= n || d[pos] < 2 || d[pos] > 8) return ZX_IMG_E_TRUNC; /* gif_walk proved it */
    /* disposal of the previous frame, then save for this one's (3) */
    uint32_t W = g->w, H = g->h, *cv = g->canvas;
    if (g->frame) {
        if (g->dispose == 2 || (g->dispose == 3 && !g->restore)) {
            for (uint32_t y = g->fy; y < g->fy + g->fh && y < H; y++)
                for (uint32_t x = g->fx; x < g->fx + g->fw && x < W; x++) cv[y * W + x] = 0;
        } else if (g->dispose == 3) {
            for (uint32_t i = 0; i < W * H; i++) cv[i] = g->prev[i];
        }
    }
    g->restore = dispose == 3 && g->prev;
    if (g->restore)
        for (uint32_t i = 0; i < W * H; i++) g->prev[i] = cv[i];
    g->fx = fx, g->fy = fy, g->fw = fw, g->fh = fh, g->dispose = dispose;

    uint32_t mcs = d[pos++];
    lzw_t *z = (lzw_t *) g->work;
    sub_t s = {d, n, pos, 0, false, false};
    uint32_t clear = 1u << mcs, eoi = clear + 1, next = clear + 2, size = mcs + 1;
    uint32_t acc = 0, nacc = 0, total = fw * fh, done = 0;
    uint32_t x = 0, y = 0, pass = 0;
    static const uint8_t ISTART[4] = {0, 4, 2, 1}, ISTEP[4] = {8, 8, 4, 2};
    bool inter = (pk & 0x40) != 0;
    int old = -1;
    uint8_t first = 0;
    for (uint32_t i = 0; i < clear; i++) z->prefix[i] = 0xFFFF, z->suffix[i] = (uint8_t) i;
    while (done < total) {
        while (nacc < size) {
            int b = sub_byte(&s);
            if (b < 0) break;
            acc |= (uint32_t) b << nacc;
            nacc += 8;
        }
        if (nacc < size) break; /* data ended before EOI: keep what we have */
        uint32_t code = acc & ((1u << size) - 1);
        acc >>= size;
        nacc -= size;
        if (code == clear) {
            next = clear + 2, size = mcs + 1, old = -1;
            continue;
        }
        if (code == eoi) break;
        uint32_t sp = 0, c = code;
        if (old < 0) {
            if (code >= clear) return ZX_IMG_E_DATA;
            first = (uint8_t) code;
            z->stack[sp++] = first;
        } else {
            if (code > next) return ZX_IMG_E_DATA;
            if (code == next) {
                if (next >= 4096) return ZX_IMG_E_DATA;
                z->stack[sp++] = first;
                c = (uint32_t) old;
            }
            while (c >= clear) {
                if (sp >= 4096) return ZX_IMG_E_DATA;
                z->stack[sp++] = z->suffix[c];
                c = z->prefix[c];
            }
            z->stack[sp++] = (uint8_t) c;
            first = (uint8_t) c;
            if (next < 4096) {
                z->prefix[next] = (uint16_t) old;
                z->suffix[next] = first;
                next++;
                if (next == (1u << size) && size < 12) size++;
            }
        }
        old = (int) code;
        while (sp && done < total) {
            uint32_t idx = z->stack[--sp];
            uint32_t X = fx + x, Y = fy + y;
            if ((int) idx != trans && X < W && Y < H) {
                const uint8_t *p = pal + 3 * idx;
                cv[Y * W + X] = idx < npal ? zi_argb(255, p[0], p[1], p[2]) : 0xFF000000u;
            }
            done++;
            if (++x == fw) {
                x = 0;
                if (!inter) {
                    y++;
                } else {
                    y += ISTEP[pass];
                    while (y >= fh && pass < 3) y = ISTART[++pass];
                }
            }
        }
    }
    s.end = false;
    while (sub_byte(&s) >= 0) {
    } /* the rest of the chain */
    if (s.trunc) return ZX_IMG_E_TRUNC;
    g->pos = s.pos;
    return ZX_IMG_OK;
}

int zui_gif_next(zui_gif_t *g, uint32_t *delay_cs)
{
    int trans = -1;
    uint8_t dispose = 0;
    uint32_t delay = 0;
    const uint8_t *d = g->d;
    uint32_t n = g->n;
    for (;;) {
        if (g->pos >= n) return ZX_IMG_E_TRUNC;
        uint8_t b = d[g->pos++];
        if (b == 0x3B) {
            g->pos--;
            return 0;
        }
        if (b == 0x21) {
            if (g->pos >= n) return ZX_IMG_E_TRUNC;
            uint8_t label = d[g->pos++];
            uint32_t p = g->pos;
            if (label == 0xF9 && p + 5 < n && d[p] == 4) {
                dispose = (d[p + 1] >> 2) & 7;
                delay = zi_le16(d + p + 2);
                trans = (d[p + 1] & 1) ? d[p + 4] : -1;
            } else if (label == 0xFF && p + 16 < n && d[p] == 11 && d[p + 12] == 3 &&
                       d[p + 13] == 1) {
                g->loops = zi_le16(d + p + 14);
            }
            if (!skip_subs(d, n, &g->pos)) return ZX_IMG_E_TRUNC;
        } else if (b == 0x2C) {
            if (n - g->pos < 10) return ZX_IMG_E_TRUNC;
            if (dispose > 3) dispose = 0;
            int rc = gif_image(g, trans, dispose);
            if (rc) return rc;
            g->frame++;
            if (delay_cs) *delay_cs = delay;
            return 1;
        } else {
            return ZX_IMG_E_DATA;
        }
    }
}

static int gif_decode(const uint8_t *d, uint32_t n, uint8_t *work, uint32_t worklen, uint32_t *px,
                      zx_image_info_t *info)
{
    zui_gif_t g;
    int rc = zui_gif_open(&g, d, n, px, 0, work, worklen);
    if (rc == 0) rc = zui_gif_next(&g, 0);
    if (rc == 1) return ZX_IMG_OK;
    return zi_fail(info, rc ? rc : ZX_IMG_E_DATA, "GIF: bad image data");
}

/* ---- BMP ---------------------------------------------------------------- */
typedef struct {
    uint32_t w, h, bpp, comp, off, pal, npal, palsz;
    bool top;
    uint32_t mask[4];
} bmp_t;

static int bmp_head(const uint8_t *d, uint32_t n, bmp_t *b, zx_image_info_t *info)
{
    if (n < 2 || d[0] != 'B' || d[1] != 'M') return zi_fail(info, ZX_IMG_E_FORMAT, "not a BMP");
    if (n < 26) return zi_fail(info, ZX_IMG_E_TRUNC, "BMP: header cut short");
    zi_zero(b, sizeof *b);
    uint32_t dib = zi_le32(d + 14);
    b->off = zi_le32(d + 10);
    if (dib == 12) {
        b->w = zi_le16(d + 18);
        b->h = zi_le16(d + 20);
        b->bpp = zi_le16(d + 24);
        b->palsz = 3;
        if (zi_le16(d + 22) != 1) return zi_fail(info, ZX_IMG_E_DATA, "BMP: bad planes");
    } else if (dib == 40 || dib == 52 || dib == 56 || dib == 108 || dib == 124) {
        if (n < 14 + dib) return zi_fail(info, ZX_IMG_E_TRUNC, "BMP: header cut short");
        int32_t w = (int32_t) zi_le32(d + 18), h = (int32_t) zi_le32(d + 22);
        if (w <= 0 || h == 0 || h == INT32_MIN)
            return zi_fail(info, ZX_IMG_E_DATA, "BMP: bad size");
        b->w = (uint32_t) w;
        b->top = h < 0;
        b->h = (uint32_t) (h < 0 ? -h : h);
        if (zi_le16(d + 26) != 1) return zi_fail(info, ZX_IMG_E_DATA, "BMP: bad planes");
        b->bpp = zi_le16(d + 28);
        b->comp = zi_le32(d + 30);
        b->npal = zi_le32(d + 46);
        b->palsz = 4;
        if (b->comp == 3 || b->comp == 6) {
            /* masks sit at DIB offset 40: inside a V2+ header, just after a V1 one */
            uint32_t m = 14 + 40, nm = b->comp == 6 || dib >= 56 ? 4 : 3;
            if (n < m + 4 * nm) return zi_fail(info, ZX_IMG_E_TRUNC, "BMP: masks cut short");
            for (uint32_t i = 0; i < nm; i++) b->mask[i] = zi_le32(d + m + 4 * i);
            if (dib == 40) dib += 4 * nm; /* the palette (if any) follows the masks */
        }
    } else {
        return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "BMP: unsupported header version");
    }
    if (b->comp == 4 || b->comp == 5)
        return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "BMP: embedded JPEG/PNG");
    uint32_t bpp = b->bpp, c = b->comp;
    bool ok =
        (c == 0 && (bpp == 1 || bpp == 4 || bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32)) ||
        (c == 1 && bpp == 8) || (c == 2 && bpp == 4) ||
        ((c == 3 || c == 6) && (bpp == 16 || bpp == 32));
    if (!ok) return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "BMP: unsupported depth / compression");
    if ((c == 1 || c == 2) && b->top) return zi_fail(info, ZX_IMG_E_DATA, "BMP: top-down RLE");
    if (bpp <= 8) {
        if (b->npal == 0 || b->npal > (1u << bpp)) b->npal = 1u << bpp;
        b->pal = 14 + dib;
        if (b->pal > n || (n - b->pal) / b->palsz < b->npal)
            return zi_fail(info, ZX_IMG_E_TRUNC, "BMP: palette cut short");
    }
    if (c == 0 && bpp == 16) b->mask[0] = 0x7C00, b->mask[1] = 0x03E0, b->mask[2] = 0x001F;
    if (c == 0 && bpp == 32) b->mask[0] = 0xFF0000, b->mask[1] = 0xFF00, b->mask[2] = 0xFF;
    int rc = zi_dims(info, b->w, b->h);
    if (rc) return rc;
    if (b->off > n) return zi_fail(info, ZX_IMG_E_TRUNC, "BMP: pixel data missing");
    if (c != 1 && c != 2) {
        uint64_t stride = ((uint64_t) b->w * bpp + 31) / 32 * 4;
        if ((uint64_t) (n - b->off) < stride * b->h)
            return zi_fail(info, ZX_IMG_E_TRUNC, "BMP: pixel data cut short");
    }
    info->format = ZX_IMG_BMP;
    info->w = b->w;
    info->h = b->h;
    info->alpha = b->mask[3] != 0 || c == 1 || c == 2;
    return ZX_IMG_OK;
}

typedef struct {
    uint32_t shift, max;
} chan_t;

static bool chan(uint32_t m, chan_t *c)
{
    c->shift = c->max = 0;
    if (!m) return true;
    while (!((m >> c->shift) & 1)) c->shift++;
    uint64_t v = (uint64_t) (m >> c->shift);
    if (v & (v + 1)) return false; /* not contiguous */
    c->max = (uint32_t) v;
    return true;
}

static uint32_t chan_get(uint32_t p, const chan_t *c, uint32_t none)
{
    if (!c->max) return none;
    uint64_t v = (p >> c->shift) & c->max;
    return (uint32_t) ((v * 255 + c->max / 2) / c->max);
}

static int bmp_decode(const uint8_t *d, uint32_t n, uint32_t *px, zx_image_info_t *info)
{
    bmp_t b;
    int rc = bmp_head(d, n, &b, info);
    if (rc) return rc;
    uint32_t pal[256];
    for (uint32_t i = 0; i < 256; i++) pal[i] = 0xFF000000u;
    if (b.bpp <= 8)
        for (uint32_t i = 0; i < b.npal; i++) {
            const uint8_t *p = d + b.pal + i * b.palsz;
            pal[i] = zi_argb(255, p[2], p[1], p[0]);
        }
    const uint8_t *s = d + b.off;
    uint32_t len = n - b.off, W = b.w, H = b.h;
    if (b.comp == 1 || b.comp == 2) { /* RLE: unwritten pixels stay transparent */
        for (uint64_t i = 0; i < (uint64_t) W * H; i++) px[i] = 0;
        bool r4 = b.comp == 2;
        uint32_t x = 0, row = 0, p = 0;
        for (;;) {
            if (len - p < 2) return zi_fail(info, ZX_IMG_E_TRUNC, "BMP: RLE cut short");
            uint32_t a = s[p], v = s[p + 1];
            p += 2;
            if (a) {
                for (uint32_t i = 0; i < a; i++, x++)
                    if (x < W && row < H)
                        px[(H - 1 - row) * W + x] = pal[r4 ? (i & 1 ? v & 15 : v >> 4) : v];
            } else if (v == 0) {
                x = 0, row++;
            } else if (v == 1) {
                break;
            } else if (v == 2) {
                if (len - p < 2) return zi_fail(info, ZX_IMG_E_TRUNC, "BMP: RLE cut short");
                x += s[p], row += s[p + 1];
                p += 2;
            } else {
                uint32_t bytes = r4 ? (v + 1) / 2 : v;
                if (len - p < bytes + (bytes & 1))
                    return zi_fail(info, ZX_IMG_E_TRUNC, "BMP: RLE cut short");
                for (uint32_t i = 0; i < v; i++, x++) {
                    uint32_t q = r4 ? (i & 1 ? s[p + i / 2] & 15 : s[p + i / 2] >> 4) : s[p + i];
                    if (x < W && row < H) px[(H - 1 - row) * W + x] = pal[q];
                }
                p += bytes + (bytes & 1);
            }
            if (row > H) row = H; /* keep counters bounded; writes are already clipped */
            if (x > W) x = W;
        }
        return ZX_IMG_OK;
    }
    chan_t ch[4];
    for (int i = 0; i < 4; i++)
        if (!chan(b.mask[i], &ch[i])) return zi_fail(info, ZX_IMG_E_DATA, "BMP: bad bit mask");
    uint32_t stride = (W * b.bpp + 31) / 32 * 4;
    for (uint32_t y = 0; y < H; y++) {
        const uint8_t *r = s + (size_t) (b.top ? y : H - 1 - y) * stride;
        uint32_t *o = px + (size_t) y * W;
        for (uint32_t x = 0; x < W; x++) {
            switch (b.bpp) {
            case 1:
                o[x] = pal[(r[x >> 3] >> (7 - (x & 7))) & 1];
                break;
            case 4:
                o[x] = pal[x & 1 ? r[x >> 1] & 15 : r[x >> 1] >> 4];
                break;
            case 8:
                o[x] = pal[r[x]];
                break;
            case 24:
                o[x] = zi_argb(255, r[3 * x + 2], r[3 * x + 1], r[3 * x]);
                break;
            default: {
                uint32_t v = b.bpp == 16 ? zi_le16(r + 2 * x) : zi_le32(r + 4 * x);
                o[x] = zi_argb(chan_get(v, &ch[3], 255), chan_get(v, &ch[0], 0),
                               chan_get(v, &ch[1], 0), chan_get(v, &ch[2], 0));
            }
            }
        }
    }
    return ZX_IMG_OK;
}

/* ---- netpbm ------------------------------------------------------------- */
typedef struct {
    const uint8_t *d;
    uint32_t n, pos;
} rd_t;

static bool ws(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static void skip_ws(rd_t *r)
{
    while (r->pos < r->n) {
        if (r->d[r->pos] == '#') {
            while (r->pos < r->n && r->d[r->pos] != '\n' && r->d[r->pos] != '\r') r->pos++;
        } else if (ws(r->d[r->pos])) {
            r->pos++;
        } else {
            break;
        }
    }
}

/* A decimal number; 0 ok, else an error code. */
static int num(rd_t *r, uint32_t *v)
{
    skip_ws(r);
    if (r->pos >= r->n) return ZX_IMG_E_TRUNC;
    if (r->d[r->pos] < '0' || r->d[r->pos] > '9') return ZX_IMG_E_DATA;
    uint32_t x = 0;
    while (r->pos < r->n && r->d[r->pos] >= '0' && r->d[r->pos] <= '9') {
        x = x * 10 + (uint32_t) (r->d[r->pos++] - '0');
        if (x > 0x7FFFFFFu) return ZX_IMG_E_DATA;
    }
    *v = x;
    return 0;
}

typedef struct {
    uint32_t kind, w, h, max, data;
} pnm_t;

static int pnm_head(const uint8_t *d, uint32_t n, pnm_t *p, zx_image_info_t *info)
{
    if (n < 2 || d[0] != 'P' || d[1] < '1' || d[1] > '7')
        return zi_fail(info, ZX_IMG_E_FORMAT, "not a netpbm file");
    if (d[1] == '7') return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "PAM (P7) not supported");
    rd_t r = {d, n, 2};
    p->kind = (uint32_t) (d[1] - '0');
    p->max = 1;
    int rc = num(&r, &p->w);
    if (!rc) rc = num(&r, &p->h);
    if (!rc && p->kind != 1 && p->kind != 4) rc = num(&r, &p->max);
    if (rc) return zi_fail(info, rc, "PNM: bad header");
    if (p->max < 1 || p->max > 65535) return zi_fail(info, ZX_IMG_E_DATA, "PNM: bad maxval");
    if ((rc = zi_dims(info, p->w, p->h))) return rc;
    if (p->kind >= 4) { /* binary: exactly one whitespace byte, then the raster */
        if (r.pos >= n) return zi_fail(info, ZX_IMG_E_TRUNC, "PNM: raster missing");
        if (!ws(d[r.pos])) return zi_fail(info, ZX_IMG_E_DATA, "PNM: bad header");
        r.pos++;
        uint64_t spp = p->kind == 6 ? 3 : 1, bps = p->max > 255 ? 2 : 1;
        uint64_t need =
            p->kind == 4 ? (uint64_t) (p->w + 7) / 8 * p->h : (uint64_t) p->w * p->h * spp * bps;
        if (n - r.pos < need) return zi_fail(info, ZX_IMG_E_TRUNC, "PNM: raster cut short");
    }
    p->data = r.pos;
    info->format = ZX_IMG_PNM;
    info->w = p->w;
    info->h = p->h;
    return ZX_IMG_OK;
}

static int pnm_decode(const uint8_t *d, uint32_t n, uint32_t *px, zx_image_info_t *info)
{
    pnm_t p;
    int rc = pnm_head(d, n, &p, info);
    if (rc) return rc;
    rd_t r = {d, n, p.data};
    uint32_t spp = p.kind == 3 || p.kind == 6 ? 3 : 1, mx = p.max;
    for (uint32_t y = 0; y < p.h; y++)
        for (uint32_t x = 0; x < p.w; x++) {
            uint32_t v[3] = {0, 0, 0};
            if (p.kind == 1) {
                skip_ws(&r);
                if (r.pos >= n) return zi_fail(info, ZX_IMG_E_TRUNC, "PNM: raster cut short");
                uint8_t c = d[r.pos++];
                if (c != '0' && c != '1') return zi_fail(info, ZX_IMG_E_DATA, "PNM: bad bit");
                v[0] = c == '1' ? 0 : 1; /* 1 is black */
            } else if (p.kind == 4) {
                v[0] = !((d[r.pos + y * ((p.w + 7) / 8) + x / 8] >> (7 - (x & 7))) & 1);
            } else {
                for (uint32_t i = 0; i < spp; i++) {
                    if (p.kind <= 3) {
                        if ((rc = num(&r, &v[i]))) return zi_fail(info, rc, "PNM: bad sample");
                    } else if (mx > 255) {
                        v[i] = zi_be16(d + r.pos);
                        r.pos += 2;
                    } else {
                        v[i] = d[r.pos++];
                    }
                    if (v[i] > mx) return zi_fail(info, ZX_IMG_E_DATA, "PNM: sample above maxval");
                }
            }
            uint32_t c[3];
            for (uint32_t i = 0; i < 3; i++) c[i] = (v[spp == 3 ? i : 0] * 255 + mx / 2) / mx;
            px[(size_t) y * p.w + x] = zi_argb(255, c[0], c[1], c[2]);
        }
    return ZX_IMG_OK;
}

/* ---- QOI ---------------------------------------------------------------- */
static int qoi_head(const uint8_t *d, uint32_t n, zx_image_info_t *info)
{
    if (n < 4 || d[0] != 'q' || d[1] != 'o' || d[2] != 'i' || d[3] != 'f')
        return zi_fail(info, ZX_IMG_E_FORMAT, "not a QOI");
    if (n < 14) return zi_fail(info, ZX_IMG_E_TRUNC, "QOI: header cut short");
    if ((d[12] != 3 && d[12] != 4) || d[13] > 1)
        return zi_fail(info, ZX_IMG_E_DATA, "QOI: bad header");
    int rc = zi_dims(info, zi_be32(d + 4), zi_be32(d + 8));
    if (rc) return rc;
    info->format = ZX_IMG_QOI;
    info->w = zi_be32(d + 4);
    info->h = zi_be32(d + 8);
    info->alpha = d[12] == 4;
    return ZX_IMG_OK;
}

static int qoi_decode(const uint8_t *d, uint32_t n, uint32_t *px, zx_image_info_t *info)
{
    int rc = qoi_head(d, n, info);
    if (rc) return rc;
    uint32_t idx[64], c = 0xFF000000u, total = info->w * info->h, pos = 14, run = 0;
    zi_zero(idx, sizeof idx);
    for (uint32_t i = 0; i < total; i++) {
        if (run) {
            run--;
        } else {
            if (pos >= n) return zi_fail(info, ZX_IMG_E_TRUNC, "QOI: cut short");
            uint32_t b = d[pos++];
            uint32_t a = c >> 24, r = (c >> 16) & 255, g = (c >> 8) & 255, bl = c & 255;
            if (b == 0xFE || b == 0xFF) {
                uint32_t k = b == 0xFE ? 3 : 4;
                if (n - pos < k) return zi_fail(info, ZX_IMG_E_TRUNC, "QOI: cut short");
                r = d[pos], g = d[pos + 1], bl = d[pos + 2];
                if (k == 4) a = d[pos + 3];
                pos += k;
            } else if ((b >> 6) == 0) {
                c = idx[b];
                a = c >> 24, r = (c >> 16) & 255, g = (c >> 8) & 255, bl = c & 255;
            } else if ((b >> 6) == 1) {
                r = (r + ((b >> 4) & 3) - 2) & 255;
                g = (g + ((b >> 2) & 3) - 2) & 255;
                bl = (bl + (b & 3) - 2) & 255;
            } else if ((b >> 6) == 2) {
                if (pos >= n) return zi_fail(info, ZX_IMG_E_TRUNC, "QOI: cut short");
                uint32_t b2 = d[pos++], dg = (b & 63) - 32;
                r = (r + dg + (b2 >> 4) - 8) & 255;
                g = (g + dg) & 255;
                bl = (bl + dg + (b2 & 15) - 8) & 255;
            } else {
                run = b & 63;
                if (run > total - i - 1)
                    return zi_fail(info, ZX_IMG_E_DATA, "QOI: run past the end");
            }
            c = zi_argb(a, r, g, bl);
            idx[(r * 3 + g * 5 + bl * 7 + a * 11) & 63] = c;
        }
        px[i] = c;
    }
    static const uint8_t END[8] = {0, 0, 0, 0, 0, 0, 0, 1};
    if (n - pos < 8) return zi_fail(info, ZX_IMG_E_TRUNC, "QOI: end marker missing");
    for (int i = 0; i < 8; i++)
        if (d[pos + i] != END[i]) return zi_fail(info, ZX_IMG_E_DATA, "QOI: bad end marker");
    return ZX_IMG_OK;
}

/* ---- TGA ---------------------------------------------------------------- */
typedef struct {
    uint32_t type, bpp, w, h, cmfirst, cmlen, cmbits, cmoff, data, abits;
    bool top, right;
} tga_t;

static int tga_head(const uint8_t *d, uint32_t n, tga_t *t, zx_image_info_t *info)
{
    if (n < 18) return zi_fail(info, ZX_IMG_E_FORMAT, "not a TGA");
    t->type = d[2];
    uint32_t cmt = d[1];
    t->cmfirst = zi_le16(d + 3), t->cmlen = zi_le16(d + 5), t->cmbits = d[7];
    t->w = zi_le16(d + 12), t->h = zi_le16(d + 14), t->bpp = d[16];
    t->abits = d[17] & 15, t->right = (d[17] & 0x10) != 0, t->top = (d[17] & 0x20) != 0;
    uint32_t base = t->type & 7, b = t->bpp;
    bool ok = cmt <= 1 && (d[17] & 0xC0) == 0 && t->w && t->h &&
              (t->type == 1 || t->type == 2 || t->type == 3 || t->type == 9 || t->type == 10 ||
               t->type == 11);
    if (ok && base == 1)
        ok = cmt == 1 && b == 8 && t->cmlen && t->cmlen <= 256 &&
             (t->cmbits == 15 || t->cmbits == 16 || t->cmbits == 24 || t->cmbits == 32);
    if (ok && base == 2) ok = b == 15 || b == 16 || b == 24 || b == 32;
    if (ok && base == 3) ok = b == 8 || b == 16;
    if (ok && cmt == 0) ok = t->cmlen == 0;
    if (ok && cmt == 1)
        ok = t->cmbits == 15 || t->cmbits == 16 || t->cmbits == 24 || t->cmbits == 32;
    if (!ok) return zi_fail(info, ZX_IMG_E_FORMAT, "not a recognisable image");
    t->cmoff = 18 + d[0];
    t->data = t->cmoff + (cmt ? t->cmlen * ((t->cmbits + 7) / 8) : 0);
    int rc = zi_dims(info, t->w, t->h);
    if (rc) return rc;
    if (t->data > n) return zi_fail(info, ZX_IMG_E_TRUNC, "TGA: header cut short");
    if (t->type < 9 && (uint64_t) (n - t->data) < (uint64_t) t->w * t->h * ((b + 7) / 8))
        return zi_fail(info, ZX_IMG_E_TRUNC, "TGA: pixel data cut short");
    info->format = ZX_IMG_TGA;
    info->w = t->w;
    info->h = t->h;
    info->alpha = (b == 32 && t->abits == 8) || (base == 3 && b == 16) ||
                  ((b == 16 || b == 15) && t->abits == 1);
    return ZX_IMG_OK;
}

static uint32_t tga_px(const uint8_t *p, uint32_t bytes, uint32_t abits, bool grey)
{
    if (grey) return bytes == 2 ? zi_argb(p[1], p[0], p[0], p[0]) : zi_argb(255, p[0], p[0], p[0]);
    if (bytes == 2) {
        uint32_t v = zi_le16(p), r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
        uint32_t a = abits == 1 ? ((v >> 15) ? 255 : 0) : 255;
        return zi_argb(a, (r * 255 + 15) / 31, (g * 255 + 15) / 31, (b * 255 + 15) / 31);
    }
    return zi_argb(bytes == 4 && abits == 8 ? p[3] : 255, p[2], p[1], p[0]);
}

static int tga_decode(const uint8_t *d, uint32_t n, uint32_t *px, zx_image_info_t *info)
{
    tga_t t;
    int rc = tga_head(d, n, &t, info);
    if (rc) return rc;
    uint32_t pal[256], bytes = (t.bpp + 7) / 8, base = t.type & 7;
    if (base == 1) {
        uint32_t cb = (t.cmbits + 7) / 8;
        for (uint32_t i = 0; i < t.cmlen; i++)
            pal[i] = tga_px(d + t.cmoff + i * cb, cb,
                            t.cmbits == 32   ? 8
                            : t.cmbits == 16 ? 1
                                             : 0,
                            false);
    }
    uint32_t total = t.w * t.h, pos = t.data, i = 0;
    while (i < total) {
        uint32_t cnt = 1;
        bool rle = false;
        if (t.type >= 9) {
            if (pos >= n) return zi_fail(info, ZX_IMG_E_TRUNC, "TGA: RLE cut short");
            cnt = (d[pos] & 127u) + 1;
            rle = (d[pos++] & 128u) != 0;
            if (cnt > total - i)
                return zi_fail(info, ZX_IMG_E_DATA, "TGA: RLE packet past the end");
        }
        for (uint32_t k = 0; k < cnt; k++, i++) {
            if (!rle || k == 0) {
                if (n - pos < bytes)
                    return zi_fail(info, ZX_IMG_E_TRUNC, "TGA: pixel data cut short");
                pos += bytes;
            }
            const uint8_t *p = d + pos - bytes;
            uint32_t c;
            if (base == 1) {
                uint32_t idx = p[0];
                if (idx < t.cmfirst || idx - t.cmfirst >= t.cmlen)
                    return zi_fail(info, ZX_IMG_E_DATA, "TGA: colour index out of range");
                c = pal[idx - t.cmfirst];
            } else {
                c = tga_px(p, bytes, t.abits, base == 3);
            }
            uint32_t x = i % t.w, y = i / t.w;
            if (t.right) x = t.w - 1 - x;
            if (!t.top) y = t.h - 1 - y;
            px[(size_t) y * t.w + x] = c;
        }
    }
    return ZX_IMG_OK;
}

/* ---- the front end ------------------------------------------------------ */
static zui_img_format_t sniff(const uint8_t *d, uint32_t n)
{
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return ZX_IMG_PNG;
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return ZX_IMG_JPEG;
    if (n >= 4 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8') return ZX_IMG_GIF;
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return ZX_IMG_BMP;
    if (n >= 4 && d[0] == 'q' && d[1] == 'o' && d[2] == 'i' && d[3] == 'f') return ZX_IMG_QOI;
    if (n >= 3 && d[0] == 'P' && d[1] >= '1' && d[1] <= '7' && ws(d[2])) return ZX_IMG_PNM;
    return ZX_IMG_TGA; /* the last resort: tga_head refuses anything implausible */
}

int zx_image_probe(const uint8_t *d, uint32_t n, zx_image_info_t *info)
{
    if (!info) return ZX_IMG_E_ARG;
    zi_zero(info, sizeof *info);
    info->orientation = 1;
    info->frames = 1;
    info->why = "";
    if (!d) return zi_fail(info, ZX_IMG_E_ARG, "no data");
    int rc;
    bmp_t b;
    pnm_t p;
    tga_t t;
    uint32_t start;
    switch (sniff(d, n)) {
    case ZX_IMG_PNG:
        rc = zi_png_probe(d, n, info);
        break;
    case ZX_IMG_JPEG:
        rc = zi_jpeg_probe(d, n, info);
        break;
    case ZX_IMG_GIF:
        rc = gif_walk(d, n, info, &start);
        break;
    case ZX_IMG_BMP:
        rc = bmp_head(d, n, &b, info);
        break;
    case ZX_IMG_QOI:
        rc = qoi_head(d, n, info);
        break;
    case ZX_IMG_PNM:
        rc = pnm_head(d, n, &p, info);
        break;
    default:
        rc = tga_head(d, n, &t, info);
    }
    if (rc) return rc;
    info->px_bytes = info->w * info->h * 4;
    if (info->work) info->work += 8; /* alignment slack */
    return ZX_IMG_OK;
}

int zx_image_decode(const uint8_t *d, uint32_t n, void *work, uint32_t worklen, uint32_t *px,
                    uint32_t pxcap, zx_image_info_t *info)
{
    zx_image_info_t local;
    if (!info) info = &local;
    int rc = zx_image_probe(d, n, info);
    if (rc) return rc;
    if (!px) return zi_fail(info, ZX_IMG_E_ARG, "no output buffer");
    if (pxcap < info->w * info->h) return zi_fail(info, ZX_IMG_E_SPACE, "output buffer too small");
    if (worklen < info->work || (info->work && !work))
        return zi_fail(info, ZX_IMG_E_SPACE, "work too small");
    uint8_t *w = (uint8_t *) work;
    uint32_t adj = w ? (uint32_t) ((8 - ((uintptr_t) w & 7)) & 7) : 0;
    w += adj;
    worklen = worklen >= adj ? worklen - adj : 0;
    switch (info->format) {
    case ZX_IMG_PNG:
        return zi_png_decode(d, n, w, worklen, px, info);
    case ZX_IMG_JPEG:
        return zi_jpeg_decode(d, n, w, worklen, px, info);
    case ZX_IMG_GIF:
        return gif_decode(d, n, w, worklen, px, info);
    case ZX_IMG_BMP:
        return bmp_decode(d, n, px, info);
    case ZX_IMG_PNM:
        return pnm_decode(d, n, px, info);
    case ZX_IMG_QOI:
        return qoi_decode(d, n, px, info);
    case ZX_IMG_TGA:
        return tga_decode(d, n, px, info);
    default:
        return zi_fail(info, ZX_IMG_E_FORMAT, "unknown format");
    }
}

/* ---- drawing (IM5) ------------------------------------------------------ */
zui_rect_t zx_image_place(uint32_t w, uint32_t h, zui_rect_t box, zui_img_fit_t fit)
{
    int64_t bw = box.w > 0 ? box.w : 0, bh = box.h > 0 ? box.h : 0, dw = w, dh = h;
    if (!w || !h || !bw || !bh) return (zui_rect_t){box.x, box.y, 0, 0};
    if (fit == ZX_IMG_FIT_STRETCH) return box;
    if (fit != ZX_IMG_FIT_NONE) {
        bool wide = (int64_t) w * bh > (int64_t) h * bw; /* relatively wider than the box */
        if (wide == (fit == ZX_IMG_FIT_CONTAIN)) {
            dw = bw;
            dh = ((int64_t) h * bw * 2 + w) / (2 * (int64_t) w);
        } else {
            dh = bh;
            dw = ((int64_t) w * bh * 2 + h) / (2 * (int64_t) h);
        }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        if (dw > 0x3FFFFFFF || dh > 0x3FFFFFFF) return (zui_rect_t){box.x, box.y, 0, 0};
    }
    return (zui_rect_t){(int32_t) (box.x + (bw - dw) / 2), (int32_t) (box.y + (bh - dh) / 2),
                        (int32_t) dw, (int32_t) dh};
}

/* The source span for output index i of an axis of dst pixels over src:
 * [*j0, *j1] with weights from tap(); total weight in *tot. */
typedef struct {
    int64_t src, dst;
    int64_t j0, j1, f; /* bilinear: j0, j1 and the fraction of j1 out of 2*dst */
    bool area;
} axis_t;

static void axis(axis_t *a, int64_t i)
{
    if (a->dst < a->src) { /* shrink: exact area */
        a->area = true;
        a->j0 = i * a->src / a->dst;
        a->j1 = ((i + 1) * a->src - 1) / a->dst;
    } else { /* grow: bilinear between pixel centres */
        a->area = false;
        int64_t p = (2 * i + 1) * a->src - a->dst;
        if (p < 0) p = 0;
        a->j0 = p / (2 * a->dst);
        a->f = p % (2 * a->dst);
        a->j1 = a->j0 + 1 < a->src ? a->j0 + 1 : a->j0;
        if (a->j1 == a->j0) a->f = 0;
    }
}

static int64_t tap(const axis_t *a, int64_t i, int64_t j)
{
    if (!a->area) return j == a->j0 ? 2 * a->dst - a->f : a->f;
    int64_t lo = j * a->dst > i * a->src ? j * a->dst : i * a->src;
    int64_t hi = (j + 1) * a->dst < (i + 1) * a->src ? (j + 1) * a->dst : (i + 1) * a->src;
    return hi > lo ? hi - lo : 0;
}

static int64_t total(const axis_t *a)
{
    return a->area ? a->src : 2 * a->dst;
}

int zx_image_blit(zui_surface_t *s, zui_rect_t box, const uint32_t *px, uint32_t w, uint32_t h,
                  uint32_t stride, zui_img_fit_t fit, uint32_t opacity)
{
    if (!s || !px || !w || !h || stride < w) return ZX_IMG_E_ARG;
    if (opacity > 255) opacity = 255;
    zui_rect_t D = zx_image_place(w, h, box, fit);
    if (D.w <= 0 || D.h <= 0) return ZX_IMG_OK;
    int64_t x0 = D.x, y0 = D.y, x1 = (int64_t) D.x + D.w, y1 = (int64_t) D.y + D.h;
    zui_rect_t c[2] = {box, s->clip};
    for (int k = 0; k < 2; k++) {
        if (c[k].x > x0) x0 = c[k].x;
        if (c[k].y > y0) y0 = c[k].y;
        if ((int64_t) c[k].x + c[k].w < x1) x1 = (int64_t) c[k].x + c[k].w;
        if ((int64_t) c[k].y + c[k].h < y1) y1 = (int64_t) c[k].y + c[k].h;
    }
    axis_t ax = {w, D.w, 0, 0, 0, false}, ay = {h, D.h, 0, 0, 0, false};
    for (int64_t Y = y0; Y < y1; Y++) {
        int64_t iy = Y - D.y;
        axis(&ay, iy);
        for (int64_t X = x0; X < x1; X++) {
            int64_t ix = X - D.x;
            axis(&ax, ix);
            uint64_t sa = 0, sr = 0, sg = 0, sb = 0;
            for (int64_t j = ay.j0; j <= ay.j1; j++) {
                int64_t wy = tap(&ay, iy, j);
                if (!wy) continue;
                const uint32_t *row = px + (size_t) j * stride;
                for (int64_t i = ax.j0; i <= ax.j1; i++) {
                    uint64_t wgt = (uint64_t) (wy * tap(&ax, ix, i));
                    uint32_t p = row[i], a = p >> 24;
                    uint64_t aw = a * wgt;
                    sa += aw;
                    sr += ((p >> 16) & 255) * aw;
                    sg += ((p >> 8) & 255) * aw;
                    sb += (p & 255) * aw;
                }
            }
            if (!sa) continue;
            uint64_t T = (uint64_t) (total(&ax) * total(&ay));
            uint32_t A = (uint32_t) ((sa + T / 2) / T);
            uint32_t rgb = (uint32_t) ((sr + sa / 2) / sa) << 16 |
                           (uint32_t) ((sg + sa / 2) / sa) << 8 | (uint32_t) ((sb + sa / 2) / sa);
            zui_blend_px(s, (int32_t) X, (int32_t) Y, rgb, (A * opacity + 127) / 255);
        }
    }
    return ZX_IMG_OK;
}

uint32_t zx_image_draw_work(const zx_image_info_t *info)
{
    uint64_t t = (uint64_t) info->px_bytes + info->work + 8;
    return t > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) t;
}

int zx_image_draw(zui_surface_t *s, zui_rect_t box, const uint8_t *d, uint32_t n, void *work,
                  uint32_t worklen, zui_img_fit_t fit, uint32_t opacity)
{
    zx_image_info_t info;
    int rc = zx_image_probe(d, n, &info);
    if (rc) return rc;
    if (!s || !work) return ZX_IMG_E_ARG;
    if (worklen < zx_image_draw_work(&info)) return ZX_IMG_E_SPACE;
    uint8_t *w = (uint8_t *) work;
    uint32_t adj = (uint32_t) ((8 - ((uintptr_t) w & 7)) & 7);
    uint32_t *px = (uint32_t *) (void *) (w + adj);
    uint8_t *scratch = w + adj + info.px_bytes;
    rc = zx_image_decode(d, n, scratch, worklen - adj - info.px_bytes, px, info.w * info.h, &info);
    if (rc) return rc;
    return zx_image_blit(s, box, px, info.w, info.h, info.w, fit, opacity);
}

const char *zx_image_format_name(zui_img_format_t f)
{
    static const char *const N[] = {"none", "PNG", "JPEG", "GIF", "BMP", "PNM", "QOI", "TGA"};
    return (unsigned) f < 8 ? N[f] : "none";
}

const char *zx_image_error(int rc)
{
    switch (rc) {
    case ZX_IMG_OK:
        return "ok";
    case ZX_IMG_E_ARG:
        return "bad argument";
    case ZX_IMG_E_FORMAT:
        return "unknown format";
    case ZX_IMG_E_UNSUPPORTED:
        return "unsupported feature";
    case ZX_IMG_E_DATA:
        return "malformed data";
    case ZX_IMG_E_TRUNC:
        return "truncated";
    case ZX_IMG_E_SIZE:
        return "too large";
    case ZX_IMG_E_SPACE:
        return "buffer too small";
    case ZX_IMG_E_CHECK:
        return "checksum mismatch";
    default:
        return "error";
    }
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_png.c — the PNG decoder behind zx_image.h. */
#include "zx_image_int.h"
#include "zx_inflate.h"
#include "zui_capture.h" /* zui_crc32 */

static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
static const uint8_t A7X[7] = {0, 4, 0, 2, 0, 1, 0}, A7Y[7] = {0, 0, 4, 0, 2, 0, 1};
static const uint8_t A7DX[7] = {8, 8, 4, 4, 2, 2, 1}, A7DY[7] = {8, 8, 8, 4, 4, 2, 2};

typedef struct {
    uint32_t w, h, depth, ctype, interlace, ch, bits; /* bits per pixel */
    uint64_t idat, raw;
} png_t;

static bool is_type(const uint8_t *t, const char *s)
{
    return t[0] == s[0] && t[1] == s[1] && t[2] == s[2] && t[3] == s[3];
}

static uint32_t pass_dim(uint32_t n, uint32_t s, uint32_t d)
{
    return n > s ? (n - s + d - 1) / d : 0;
}

static uint64_t rowbytes(const png_t *p, uint32_t w)
{
    return ((uint64_t) w * p->bits + 7) / 8;
}

/* Walk the chunk list (no CRCs here); fills p and info. */
static int walk(const uint8_t *d, uint32_t n, png_t *p, zx_image_info_t *info)
{
    if (n < 8) return zi_fail(info, ZX_IMG_E_TRUNC, "PNG: no signature");
    for (int i = 0; i < 8; i++)
        if (d[i] != SIG[i]) return zi_fail(info, ZX_IMG_E_FORMAT, "not a PNG");
    uint32_t pos = 8;
    bool first = true, plte = false, end = false;
    zi_zero(p, sizeof *p);
    while (!end) {
        if (n - pos < 12) return zi_fail(info, ZX_IMG_E_TRUNC, "PNG: ends before IEND");
        uint32_t len = zi_be32(d + pos);
        const uint8_t *t = d + pos + 4, *c = d + pos + 8;
        if (len > 0x7FFFFFFFu) return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad chunk length");
        if (n - pos - 12 < len) return zi_fail(info, ZX_IMG_E_TRUNC, "PNG: chunk cut short");
        for (int i = 0; i < 4; i++)
            if (!((t[i] >= 'A' && t[i] <= 'Z') || (t[i] >= 'a' && t[i] <= 'z')))
                return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad chunk type");
        if (first != is_type(t, "IHDR")) return zi_fail(info, ZX_IMG_E_DATA, "PNG: IHDR not first");
        if (is_type(t, "IHDR")) {
            if (!first || len != 13) return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad IHDR");
            p->w = zi_be32(c);
            p->h = zi_be32(c + 4);
            p->depth = c[8];
            p->ctype = c[9];
            p->interlace = c[12];
            if (c[10] != 0 || c[11] != 0 || c[12] > 1)
                return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad compression, filter or interlace");
            uint32_t dp = p->depth;
            bool ok;
            switch (p->ctype) {
            case 0:
                ok = dp == 1 || dp == 2 || dp == 4 || dp == 8 || dp == 16;
                p->ch = 1;
                break;
            case 2:
                ok = dp == 8 || dp == 16;
                p->ch = 3;
                break;
            case 3:
                ok = dp == 1 || dp == 2 || dp == 4 || dp == 8;
                p->ch = 1;
                break;
            case 4:
                ok = dp == 8 || dp == 16;
                p->ch = 2;
                break;
            case 6:
                ok = dp == 8 || dp == 16;
                p->ch = 4;
                break;
            default:
                ok = false;
            }
            if (!ok) return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad colour type / bit depth");
            p->bits = p->ch * dp;
            int rc = zi_dims(info, p->w, p->h);
            if (rc) return rc;
        } else if (is_type(t, "IDAT")) {
            p->idat += len;
        } else if (is_type(t, "PLTE")) {
            plte = true;
        } else if (is_type(t, "tRNS")) {
            info->alpha = true;
        } else if (is_type(t, "eXIf")) {
            info->orientation = zi_exif_orientation(c, len);
        } else if (is_type(t, "IEND")) {
            end = true;
        } else if (!(t[0] & 0x20)) {
            return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "PNG: unknown critical chunk");
        }
        first = false;
        pos += 12 + len;
    }
    if (!p->idat) return zi_fail(info, ZX_IMG_E_DATA, "PNG: no IDAT");
    if (p->ctype == 3 && !plte) return zi_fail(info, ZX_IMG_E_DATA, "PNG: palette missing");
    if (p->interlace) {
        for (int k = 0; k < 7; k++) {
            uint32_t pw = pass_dim(p->w, A7X[k], A7DX[k]), ph = pass_dim(p->h, A7Y[k], A7DY[k]);
            if (pw && ph) p->raw += (uint64_t) ph * (1 + rowbytes(p, pw));
        }
    } else {
        p->raw = (uint64_t) p->h * (1 + rowbytes(p, p->w));
    }
    if (p->idat + p->raw > 0xFFFFF000u) return zi_fail(info, ZX_IMG_E_SIZE, "PNG: too large");
    info->alpha = info->alpha || p->ctype == 4 || p->ctype == 6;
    info->progressive = p->interlace != 0;
    return ZX_IMG_OK;
}

int zi_png_probe(const uint8_t *d, uint32_t n, zx_image_info_t *info)
{
    png_t p;
    int rc = walk(d, n, &p, info);
    if (rc) return rc;
    info->format = ZX_IMG_PNG;
    bool swap = info->orientation >= 5;
    info->w = swap ? p.h : p.w;
    info->h = swap ? p.w : p.h;
    info->work = (uint32_t) (p.idat + p.raw);
    return ZX_IMG_OK;
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c)
{
    int p = (int) a + b - c, pa = p - a, pb = p - b, pc = p - c;
    if (pa < 0) pa = -pa;
    if (pb < 0) pb = -pb;
    if (pc < 0) pc = -pc;
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static int unfilter(uint8_t *r, const uint8_t *prev, uint32_t rb, uint32_t bpp)
{
    uint8_t f = r[-1];
    switch (f) {
    case 0:
        break;
    case 1:
        for (uint32_t i = bpp; i < rb; i++) r[i] = (uint8_t) (r[i] + r[i - bpp]);
        break;
    case 2:
        if (prev)
            for (uint32_t i = 0; i < rb; i++) r[i] = (uint8_t) (r[i] + prev[i]);
        break;
    case 3:
        for (uint32_t i = 0; i < rb; i++) {
            uint32_t a = i >= bpp ? r[i - bpp] : 0, b = prev ? prev[i] : 0;
            r[i] = (uint8_t) (r[i] + ((a + b) >> 1));
        }
        break;
    case 4:
        for (uint32_t i = 0; i < rb; i++) {
            uint8_t a = i >= bpp ? r[i - bpp] : 0, b = prev ? prev[i] : 0;
            uint8_t c = prev && i >= bpp ? prev[i - bpp] : 0;
            r[i] = (uint8_t) (r[i] + paeth(a, b, c));
        }
        break;
    default:
        return -1;
    }
    return 0;
}

typedef struct {
    uint32_t pal[256], npal;
    uint32_t key[3]; /* tRNS colour key at native depth */
    bool haskey;
} pal_t;

static uint32_t sample(const uint8_t *r, uint32_t i, uint32_t depth) /* i-th sample of the row */
{
    if (depth == 16) return zi_be16(r + 2 * i);
    if (depth == 8) return r[i];
    uint32_t bit = i * depth;
    return (uint32_t) (r[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1u);
}

static uint32_t to8(uint32_t v, uint32_t depth)
{
    switch (depth) {
    case 1:
        return v * 255;
    case 2:
        return v * 85;
    case 4:
        return v * 17;
    case 16:
        return v >> 8;
    default:
        return v;
    }
}

static int emit_row(const png_t *p, const pal_t *pl, const uint8_t *r, uint32_t pw,
                    const zi_out_t *o, uint32_t y, uint32_t sx, uint32_t dx)
{
    int64_t base, step;
    zi_row(o, y, &base, &step);
    uint32_t dp = p->depth;
    for (uint32_t x = 0; x < pw; x++) {
        uint32_t c, X = sx + x * dx;
        switch (p->ctype) {
        case 0: {
            uint32_t v = sample(r, x, dp), g = to8(v, dp);
            c = zi_argb(pl->haskey && v == pl->key[0] ? 0 : 255, g, g, g);
            break;
        }
        case 2: {
            uint32_t R = sample(r, 3 * x, dp), G = sample(r, 3 * x + 1, dp),
                     B = sample(r, 3 * x + 2, dp);
            bool k = pl->haskey && R == pl->key[0] && G == pl->key[1] && B == pl->key[2];
            c = zi_argb(k ? 0 : 255, to8(R, dp), to8(G, dp), to8(B, dp));
            break;
        }
        case 3: {
            uint32_t i = sample(r, x, dp);
            if (i >= pl->npal) return -1;
            c = pl->pal[i];
            break;
        }
        case 4: {
            uint32_t g = to8(sample(r, 2 * x, dp), dp);
            c = zi_argb(to8(sample(r, 2 * x + 1, dp), dp), g, g, g);
            break;
        }
        default:
            c = zi_argb(to8(sample(r, 4 * x + 3, dp), dp), to8(sample(r, 4 * x, dp), dp),
                        to8(sample(r, 4 * x + 1, dp), dp), to8(sample(r, 4 * x + 2, dp), dp));
        }
        o->px[base + (int64_t) X * step] = c;
    }
    return 0;
}

int zi_png_decode(const uint8_t *d, uint32_t n, uint8_t *work, uint32_t worklen, uint32_t *px,
                  zx_image_info_t *info)
{
    png_t p;
    int rc = walk(d, n, &p, info);
    if (rc) return rc;
    if (worklen < p.idat + p.raw) return zi_fail(info, ZX_IMG_E_SPACE, "PNG: work too small");
    pal_t pl;
    zi_zero(&pl, sizeof pl);
    uint8_t trns[256];
    uint32_t ntrns = 0;
    uint32_t idat = 0;
    for (uint32_t pos = 8;;) { /* walk() proved the structure; now the contents */
        uint32_t len = zi_be32(d + pos);
        const uint8_t *t = d + pos + 4, *c = d + pos + 8;
        if (zui_crc32(0, t, len + 4) != zi_be32(c + len))
            return zi_fail(info, ZX_IMG_E_CHECK, "PNG: chunk CRC mismatch");
        if (is_type(t, "IDAT")) {
            for (uint32_t i = 0; i < len; i++) work[idat + i] = c[i];
            idat += len;
        } else if (is_type(t, "PLTE")) {
            if (len % 3 || len == 0 || len > 768 || pl.npal)
                return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad PLTE");
            pl.npal = len / 3;
            for (uint32_t i = 0; i < pl.npal; i++)
                pl.pal[i] = zi_argb(255, c[3 * i], c[3 * i + 1], c[3 * i + 2]);
        } else if (is_type(t, "tRNS")) {
            if (p.ctype == 3) {
                if (len > 256) return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad tRNS");
                for (uint32_t i = 0; i < len; i++) trns[i] = c[i];
                ntrns = len;
            } else if (p.ctype == 0 || p.ctype == 2) {
                if (len != (p.ctype ? 6u : 2u))
                    return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad tRNS");
                for (uint32_t i = 0; i < len / 2; i++) pl.key[i] = zi_be16(c + 2 * i);
                pl.haskey = true;
            }
        } else if (is_type(t, "IEND")) {
            break;
        }
        pos += 12 + len;
    }
    if (ntrns > pl.npal && p.ctype == 3)
        return zi_fail(info, ZX_IMG_E_DATA, "PNG: tRNS longer than PLTE");
    for (uint32_t i = 0; i < ntrns; i++)
        pl.pal[i] = (pl.pal[i] & 0xFFFFFFu) | (uint32_t) trns[i] << 24;

    uint8_t *raw = work + idat;
    uint32_t got = 0;
    rc = zx_inflate_zlib(work, idat, raw, (uint32_t) p.raw, &got);
    if (rc == ZX_INF_E_SPACE)
        return zi_fail(info, ZX_IMG_E_DATA, "PNG: more image data than the header allows");
    if (rc == ZX_INF_E_CHECK) return zi_fail(info, ZX_IMG_E_CHECK, "PNG: zlib check failed");
    if (rc == ZX_INF_E_TRUNC) return zi_fail(info, ZX_IMG_E_TRUNC, "PNG: zlib stream cut short");
    if (rc) return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad zlib stream");
    if (got != p.raw)
        return zi_fail(info, ZX_IMG_E_DATA, "PNG: less image data than the header says");

    zi_out_t o = {px, p.w, p.h, info->orientation};
    uint32_t bpp = p.bits >= 8 ? p.bits / 8 : 1;
    uint8_t *row = raw;
    for (int k = 0; k < 7; k++) {
        uint32_t sx = 0, sy = 0, dx = 1, dy = 1, pw = p.w, ph = p.h;
        if (p.interlace) {
            sx = A7X[k], sy = A7Y[k], dx = A7DX[k], dy = A7DY[k];
            pw = pass_dim(p.w, sx, dx);
            ph = pass_dim(p.h, sy, dy);
            if (!pw || !ph) continue;
        }
        uint32_t rb = (uint32_t) rowbytes(&p, pw);
        const uint8_t *prev = 0;
        for (uint32_t y = 0; y < ph; y++) {
            if (unfilter(row + 1, prev, rb, bpp))
                return zi_fail(info, ZX_IMG_E_DATA, "PNG: bad filter type");
            if (emit_row(&p, &pl, row + 1, pw, &o, sy + y * dy, sx, dx))
                return zi_fail(info, ZX_IMG_E_DATA, "PNG: palette index out of range");
            prev = row + 1;
            row += 1 + rb;
        }
        if (!p.interlace) break;
    }
    return ZX_IMG_OK;
}

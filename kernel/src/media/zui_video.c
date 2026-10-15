/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_video.c — Y4M, AVI and MP4/QuickTime demuxing, Motion-JPEG and raw
 * frames, animated GIF, and the platform-decoder adapter. See zui_video.h. */
#include "zui_video.h"
#include "zx_image_int.h"

#define FCC(a, b, c, d)                                                                            \
    ((uint32_t) (a) | (uint32_t) (b) << 8 | (uint32_t) (c) << 16 | (uint32_t) (d) << 24)
#define MAX_HDR 1024u /* longest Y4M stream or frame header line */

static int vfail(zui_video_info_t *i, int rc, const char *why)
{
    i->why = why;
    return rc;
}

static int dims_ok(zui_video_info_t *i, uint32_t w, uint32_t h)
{
    if (!w || !h) return vfail(i, ZX_IMG_E_DATA, "zero width or height");
    if (w > ZX_IMG_MAX_DIM || h > ZX_IMG_MAX_DIM || (uint64_t) w * h > ZX_IMG_MAX_PIXELS)
        return vfail(i, ZX_IMG_E_SIZE, "larger than the image caps");
    return 0;
}

static uint64_t ticks_us(uint64_t t, uint64_t num,
                         uint64_t den) /* t * num * 1e6 / den, no overflow */
{
    if (!den) return 0;
    uint64_t a = t * num;
    return a / den * 1000000u + a % den * 1000000u / den;
}

static int64_t asr(int64_t v, int s)
{
    return v >= 0 ? v >> s : ~(~v >> s);
}

static uint8_t c8(int64_t v)
{
    return (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : v);
}

static zui_vid_codec_t codec_of(uint32_t f)
{
    uint32_t l = f | 0x20202020u; /* lower-case letters; digits and space keep */
    static const struct {
        uint32_t f;
        zui_vid_codec_t c;
    } T[] = {
        {FCC('m', 'j', 'p', 'g'), ZUI_VC_MJPEG}, {FCC('a', 'v', 'r', 'n'), ZUI_VC_MJPEG},
        {FCC('d', 'm', 'b', '1'), ZUI_VC_MJPEG}, {FCC('j', 'p', 'e', 'g'), ZUI_VC_MJPEG},
        {FCC('m', 'j', 'p', 'a'), ZUI_VC_MJPEG}, {FCC('h', '2', '6', '4'), ZUI_VC_H264},
        {FCC('x', '2', '6', '4'), ZUI_VC_H264},  {FCC('a', 'v', 'c', '1'), ZUI_VC_H264},
        {FCC('a', 'v', 'c', '3'), ZUI_VC_H264},  {FCC('d', 'a', 'v', 'c'), ZUI_VC_H264},
        {FCC('h', 'e', 'v', 'c'), ZUI_VC_HEVC},  {FCC('h', '2', '6', '5'), ZUI_VC_HEVC},
        {FCC('h', 'v', 'c', '1'), ZUI_VC_HEVC},  {FCC('h', 'e', 'v', '1'), ZUI_VC_HEVC},
        {FCC('x', '2', '6', '5'), ZUI_VC_HEVC},  {FCC('a', 'v', '0', '1'), ZUI_VC_AV1},
        {FCC('v', 'p', '9', '0'), ZUI_VC_VP9},   {FCC('v', 'p', '0', '9'), ZUI_VC_VP9},
        {FCC('v', 'p', '8', '0'), ZUI_VC_VP8},   {FCC('v', 'p', '0', '8'), ZUI_VC_VP8},
        {FCC('x', 'v', 'i', 'd'), ZUI_VC_MPEG4}, {FCC('d', 'i', 'v', 'x'), ZUI_VC_MPEG4},
        {FCC('d', 'x', '5', '0'), ZUI_VC_MPEG4}, {FCC('f', 'm', 'p', '4'), ZUI_VC_MPEG4},
        {FCC('m', 'p', '4', 'v'), ZUI_VC_MPEG4},
    };
    for (uint32_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (T[i].f == l) return T[i].c;
    return ZUI_VC_OTHER;
}

const char *zui_video_codec_name(zui_vid_codec_t c)
{
    static const char *const N[] = {"none", "raw YUV", "raw RGB",       "Motion-JPEG",
                                    "GIF",  "H.264",   "H.265/HEVC",    "AV1",
                                    "VP9",  "VP8",     "MPEG-4 Part 2", "other"};
    return (unsigned) c <= ZUI_VC_OTHER ? N[c] : "other";
}

/* VD4: decide who decodes a demuxed codec. */
static int route(zui_video_info_t *i, const zui_video_hw_t *hw)
{
    zui_vid_codec_t c = i->codec;
    if (c == ZUI_VC_MJPEG || c == ZUI_VC_RGB || c == ZUI_VC_YUV || c == ZUI_VC_GIF) return 0;
    if (hw && hw->supports && hw->decode && hw->supports(hw->ctx, c, i->w, i->h)) {
        i->hw = true;
        return 0;
    }
    switch (c) {
    case ZUI_VC_H264:
        return vfail(i, ZUI_VID_E_CODEC, "H.264 needs a platform decoder (none registered)");
    case ZUI_VC_HEVC:
        return vfail(i, ZUI_VID_E_CODEC, "HEVC needs a platform decoder (none registered)");
    case ZUI_VC_AV1:
        return vfail(i, ZUI_VID_E_CODEC, "AV1 needs a platform decoder (none registered)");
    case ZUI_VC_VP9:
        return vfail(i, ZUI_VID_E_CODEC, "VP9 needs a platform decoder (none registered)");
    case ZUI_VC_VP8:
        return vfail(i, ZUI_VID_E_CODEC, "VP8 needs a platform decoder (none registered)");
    case ZUI_VC_MPEG4:
        return vfail(i, ZUI_VID_E_CODEC, "MPEG-4 Part 2 needs a platform decoder");
    default:
        return vfail(i, ZUI_VID_E_CODEC, "unknown video codec");
    }
}

/* ---- Y4M ----------------------------------------------------------------- */
typedef struct {
    uint32_t hdr_end, cs, cw, ch, fsize;
    bool full;
} y4m_t;

static uint32_t line_end(const uint8_t *d, uint32_t n, uint32_t p) /* index of '\n', or 0 */
{
    for (uint32_t i = p; i < n && i - p < MAX_HDR; i++)
        if (d[i] == '\n') return i;
    return 0;
}

static uint32_t dec(const uint8_t *d, uint32_t *p, uint32_t e)
{
    uint32_t v = 0;
    while (*p < e && d[*p] >= '0' && d[*p] <= '9' && v < 100000000u)
        v = v * 10 + (uint32_t) (d[(*p)++] - '0');
    return v;
}

static bool tok(const uint8_t *d, uint32_t p, uint32_t e, const char *s)
{
    for (; *s; s++, p++)
        if (p >= e || d[p] != (uint8_t) *s) return false;
    return p == e || d[p] == ' ';
}

static int y4m_head(const uint8_t *d, uint32_t n, zui_video_info_t *i, y4m_t *y)
{
    uint32_t e = line_end(d, n, 0);
    if (!e) return vfail(i, ZX_IMG_E_TRUNC, "Y4M: header line missing");
    uint32_t w = 0, h = 0, fn = 25, fd = 1;
    y->cs = 0;
    y->full = false;
    for (uint32_t p = 10; p < e;) {
        uint8_t t = d[p++];
        uint32_t q = p;
        if (t == 'W')
            w = dec(d, &q, e);
        else if (t == 'H')
            h = dec(d, &q, e);
        else if (t == 'F') {
            fn = dec(d, &q, e);
            if (q < e && d[q] == ':') q++;
            fd = dec(d, &q, e);
        } else if (t == 'C') {
            if (tok(d, p, e, "420jpeg") || tok(d, p, e, "420mpeg2") || tok(d, p, e, "420paldv") ||
                tok(d, p, e, "420"))
                y->cs = 0;
            else if (tok(d, p, e, "422"))
                y->cs = 1;
            else if (tok(d, p, e, "444"))
                y->cs = 2;
            else if (tok(d, p, e, "mono"))
                y->cs = 3;
            else
                return vfail(i, ZX_IMG_E_UNSUPPORTED,
                             "Y4M: colour space not supported (8-bit 420/422/444/mono only)");
        } else if (t == 'X' && tok(d, p, e, "COLORRANGE=FULL")) {
            y->full = true;
        }
        while (p < e && d[p] != ' ') p++;
        while (p < e && d[p] == ' ') p++;
    }
    int rc = dims_ok(i, w, h);
    if (rc) return rc;
    if (!fn || !fd) return vfail(i, ZX_IMG_E_DATA, "Y4M: bad frame rate");
    y->cw = y->cs == 2 ? w : y->cs == 3 ? 0 : (w + 1) / 2;
    y->ch = y->cs == 0 ? (h + 1) / 2 : y->cs == 3 ? 0 : h;
    y->fsize = w * h + 2 * y->cw * y->ch;
    y->hdr_end = e + 1;
    i->container = ZUI_VID_Y4M;
    i->codec = ZUI_VC_YUV;
    i->w = w, i->h = h, i->rate_num = fn, i->rate_den = fd;
    /* count the complete frames */
    uint32_t frames = 0;
    for (uint32_t p = y->hdr_end; p < n;) {
        if (n - p < 5 || d[p] != 'F' || d[p + 1] != 'R' || d[p + 2] != 'A' || d[p + 3] != 'M' ||
            d[p + 4] != 'E')
            break;
        uint32_t le = line_end(d, n, p);
        if (!le || n - (le + 1) < y->fsize) break;
        p = le + 1 + y->fsize;
        frames++;
    }
    i->frames = frames;
    i->duration_us = ticks_us(frames, fd, fn);
    return 0;
}

static int y4m_next(zui_video_t *v, zui_video_frame_t *f)
{
    const uint8_t *d = v->d;
    uint32_t n = v->n, p = v->pos, w = v->info.w, h = v->info.h;
    if (p >= n) return 0;
    if (n - p < 5 || d[p] != 'F' || d[p + 1] != 'R' || d[p + 2] != 'A' || d[p + 3] != 'M' ||
        d[p + 4] != 'E')
        return vfail(&v->info, n - p < 5 ? ZX_IMG_E_TRUNC : ZX_IMG_E_DATA, "Y4M: FRAME expected");
    uint32_t le = line_end(d, n, p);
    if (!le) return vfail(&v->info, ZX_IMG_E_TRUNC, "Y4M: frame header cut short");
    p = le + 1;
    if (n - p < w * h + 2 * v->y4m_cw * v->y4m_ch)
        return vfail(&v->info, ZX_IMG_E_TRUNC, "Y4M: frame cut short");
    const uint8_t *Y = d + p, *U = Y + w * h, *V = U + v->y4m_cw * v->y4m_ch;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            int64_t yy = Y[y * w + x], cb = 128, cr = 128;
            if (v->y4m_cs != 3) {
                uint32_t ci =
                    (v->y4m_cs == 0 ? y / 2 : y) * v->y4m_cw + (v->y4m_cs == 2 ? x : x / 2);
                cb = U[ci], cr = V[ci];
            }
            int64_t R, G, B;
            if (v->y4m_full) { /* JFIF / full range, as zui_jpeg */
                R = yy + asr(91881 * (cr - 128) + 32768, 16);
                G = yy + asr(-22554 * (cb - 128) + 32768 - 46802 * (cr - 128), 16);
                B = yy + asr(116130 * (cb - 128) + 32768, 16);
            } else { /* BT.601 video range */
                int64_t c = yy - 16, dd = cb - 128, e = cr - 128;
                R = asr(298 * c + 409 * e + 128, 8);
                G = asr(298 * c - 100 * dd - 208 * e + 128, 8);
                B = asr(298 * c + 516 * dd + 128, 8);
            }
            v->frame[y * w + x] = zi_argb(255, c8(R), c8(G), c8(B));
        }
    v->pos = p + w * h + 2 * v->y4m_cw * v->y4m_ch;
    f->pts_us = ticks_us(v->index, v->info.rate_den, v->info.rate_num);
    f->dur_us = ticks_us(1, v->info.rate_den, v->info.rate_num);
    return 1;
}

/* ---- AVI ----------------------------------------------------------------- */
static int avi_head(const uint8_t *d, uint32_t n, zui_video_info_t *i, zui_video_t *v)
{
    uint32_t end = 8 + zi_le32(d + 4);
    if (end > n || end < 12) end = n;
    bool hdrl = false;
    int32_t bw = 0, bh = 0;
    uint32_t comp = 0, bpp = 0, handler = 0, nstrl = 0;
    v->stream = 0xFFFFFFFFu;
    for (uint32_t p = 12; p + 8 <= end;) {
        uint32_t id = zi_le32(d + p), sz = zi_le32(d + p + 4);
        uint32_t body = p + 8, next = sz > end - body ? end : body + sz + (sz & 1);
        uint32_t kind =
            id == FCC('L', 'I', 'S', 'T') && sz >= 4 && end - body >= 4 ? zi_le32(d + body) : 0;
        if (kind == FCC('h', 'd', 'r', 'l')) {
            if (sz > n - body) return vfail(i, ZX_IMG_E_TRUNC, "AVI: header cut short");
            hdrl = true;
            for (uint32_t q = body + 4; q + 8 <= body + sz;) {
                uint32_t qid = zi_le32(d + q), qsz = zi_le32(d + q + 4), qb = q + 8;
                if (qsz > body + sz - qb) return vfail(i, ZX_IMG_E_DATA, "AVI: bad header chunk");
                if (qid == FCC('a', 'v', 'i', 'h') && qsz >= 40 && !i->frames)
                    i->frames = zi_le32(d + qb + 16);
                if (qid == FCC('L', 'I', 'S', 'T') && qsz >= 4 &&
                    zi_le32(d + qb) == FCC('s', 't', 'r', 'l')) {
                    uint32_t sh = 0, sf = 0, shn = 0, sfn = 0;
                    for (uint32_t r = qb + 4; r + 8 <= qb + qsz;) {
                        uint32_t rid = zi_le32(d + r), rsz = zi_le32(d + r + 4);
                        if (rsz > qb + qsz - (r + 8))
                            return vfail(i, ZX_IMG_E_DATA, "AVI: bad stream chunk");
                        if (rid == FCC('s', 't', 'r', 'h')) sh = r + 8, shn = rsz;
                        if (rid == FCC('s', 't', 'r', 'f')) sf = r + 8, sfn = rsz;
                        r += 8 + rsz + (rsz & 1);
                    }
                    if (sh && shn >= 36 && zi_le32(d + sh) == FCC('v', 'i', 'd', 's') &&
                        v->stream == 0xFFFFFFFFu) {
                        if (!sf || sfn < 40)
                            return vfail(i, ZX_IMG_E_DATA, "AVI: video format missing");
                        v->stream = nstrl;
                        handler = zi_le32(d + sh + 4);
                        v->scale = zi_le32(d + sh + 20);
                        v->rate = zi_le32(d + sh + 24);
                        if (zi_le32(d + sh + 32)) i->frames = zi_le32(d + sh + 32);
                        bw = (int32_t) zi_le32(d + sf + 4);
                        bh = (int32_t) zi_le32(d + sf + 8);
                        bpp = zi_le16(d + sf + 14);
                        comp = zi_le32(d + sf + 16);
                    }
                    nstrl++;
                }
                q = qb + qsz + (qsz & 1);
            }
        } else if (kind == FCC('m', 'o', 'v', 'i')) {
            v->pos = v->first = body + 4;
            v->end = sz > n - body ? n : body + sz;
            break;
        }
        p = next;
    }
    if (!hdrl || v->stream == 0xFFFFFFFFu) return vfail(i, ZX_IMG_E_DATA, "AVI: no video stream");
    if (!v->end) return vfail(i, ZX_IMG_E_TRUNC, "AVI: no movi list");
    if (bw <= 0 || bh == 0 || bh == INT32_MIN)
        return vfail(i, ZX_IMG_E_DATA, "AVI: bad frame size");
    if (!v->scale || !v->rate) return vfail(i, ZX_IMG_E_DATA, "AVI: bad frame rate");
    int rc = dims_ok(i, (uint32_t) bw, (uint32_t) (bh < 0 ? -bh : bh));
    if (rc) return rc;
    i->container = ZUI_VID_AVI;
    i->w = (uint32_t) bw, i->h = (uint32_t) (bh < 0 ? -bh : bh);
    i->rate_num = v->rate, i->rate_den = v->scale;
    i->duration_us = ticks_us(i->frames, v->scale, v->rate);
    v->top_down = bh < 0;
    v->bpp = bpp;
    if (comp == 0) {
        i->codec = ZUI_VC_RGB;
        i->fourcc = 0;
        if (bpp != 24 && bpp != 32)
            return vfail(i, ZX_IMG_E_UNSUPPORTED, "AVI: raw RGB must be 24 or 32 bits");
    } else {
        i->fourcc = comp;
        i->codec = codec_of(comp);
        if (i->codec == ZUI_VC_OTHER) i->codec = codec_of(handler);
    }
    return 0;
}

/* The next access unit of the video stream (AVI chunk / MP4 sample). */
static int next_au(zui_video_t *v, const uint8_t **au, uint32_t *len, uint64_t *pts, uint64_t *dur)
{
    const uint8_t *d = v->d;
    if (v->info.container == ZUI_VID_AVI) {
        uint32_t want_dc = FCC('0' + v->stream / 10 % 10, '0' + v->stream % 10, 'd', 'c');
        uint32_t want_db = FCC('0' + v->stream / 10 % 10, '0' + v->stream % 10, 'd', 'b');
        for (;;) {
            uint32_t p = v->pos;
            if (p >= v->end) return 0;
            if (v->end - p < 8) return vfail(&v->info, ZX_IMG_E_TRUNC, "AVI: chunk cut short");
            uint32_t id = zi_le32(d + p), sz = zi_le32(d + p + 4);
            if (id == FCC('L', 'I', 'S', 'T')) { /* 'rec ' groups: step inside */
                v->pos = p + 12;
                continue;
            }
            if (sz > v->n - (p + 8)) return vfail(&v->info, ZX_IMG_E_TRUNC, "AVI: frame cut short");
            v->pos = p + 8 + sz + (sz & 1);
            if (id == want_dc || id == want_db) {
                *au = d + p + 8;
                *len = sz;
                *pts = ticks_us(v->index, v->scale, v->rate);
                *dur = ticks_us(1, v->scale, v->rate);
                return 1;
            }
        }
    }
    /* MP4 */
    if (v->sample >= v->nsamples) return 0;
    if (!v->chunk_left) { /* the next chunk (stsc runs give samples per chunk, all >= 1) */
        if (++v->chunk >= v->nchunks)
            return vfail(&v->info, ZX_IMG_E_DATA, "MP4: more samples than chunks");
        if (v->stsc_i + 1 < v->nstsc && v->chunk + 1 >= zi_be32(d + v->stsc + 12 * (v->stsc_i + 1)))
            v->stsc_i++;
        v->chunk_left = zi_be32(d + v->stsc + 12 * v->stsc_i + 4);
        v->chunk_off = v->co64 ? (uint64_t) zi_be32(d + v->stco + 8 * v->chunk) << 32 |
                                     zi_be32(d + v->stco + 8 * v->chunk + 4)
                               : zi_be32(d + v->stco + 4 * v->chunk);
    }
    uint32_t sz = v->stsz_const ? v->stsz_const : zi_be32(d + v->stsz + 4 * v->sample);
    uint64_t off = v->chunk_off;
    if (off > v->n || sz > v->n - off)
        return vfail(&v->info, ZX_IMG_E_TRUNC, "MP4: sample outside the file");
    v->chunk_off += sz;
    v->chunk_left--;
    uint32_t delta = 0;
    while (v->stts_i < v->nstts && !v->stts_left) {
        v->stts_left = zi_be32(d + v->stts + 8 * v->stts_i);
        if (!v->stts_left) v->stts_i++;
    }
    if (v->stts_left) {
        delta = zi_be32(d + v->stts + 8 * v->stts_i + 4);
        if (!--v->stts_left) v->stts_i++;
    }
    *au = d + off;
    *len = sz;
    *pts = ticks_us(v->dts, 1, v->timescale);
    *dur = ticks_us(delta, 1, v->timescale);
    v->dts += delta;
    v->sample++;
    return 1;
}

/* ---- MP4 / QuickTime ------------------------------------------------------ */
/* One box at p within [.., lim): type, body and end. False when it does not
 * fit (rc says why). */
static bool box(const uint8_t *d, uint32_t lim, uint32_t p, uint32_t *type, uint32_t *body,
                uint32_t *end)
{
    if (p > lim || lim - p < 8) return false;
    uint64_t sz = zi_be32(d + p);
    *type = zi_le32(d + p + 4);
    *body = p + 8;
    if (sz == 1) {
        if (lim - p < 16) return false;
        sz = (uint64_t) zi_be32(d + p + 8) << 32 | zi_be32(d + p + 12);
        *body = p + 16;
    } else if (sz == 0) {
        sz = lim - p;
    }
    if (sz < *body - p || sz > lim - p) return false;
    *end = p + (uint32_t) sz;
    return true;
}

static bool child(const uint8_t *d, uint32_t b, uint32_t e, uint32_t want, uint32_t *cb,
                  uint32_t *ce)
{
    uint32_t t, body, end;
    for (uint32_t p = b; box(d, e, p, &t, &body, &end); p = end)
        if (t == want) {
            *cb = body, *ce = end;
            return true;
        }
    return false;
}

static int mp4_head(const uint8_t *d, uint32_t n, zui_video_info_t *i, zui_video_t *v)
{
    uint32_t t, body, end, mb = 0, me = 0;
    for (uint32_t p = 0; p < n; p = end) {
        if (!box(d, n, p, &t, &body, &end)) {
            if (n - p >= 8 && zi_le32(d + p + 4) == FCC('m', 'd', 'a', 't') && mb)
                break; /* cut inside mdat */
            return vfail(i, ZX_IMG_E_TRUNC, "MP4: box cut short");
        }
        if (t == FCC('m', 'o', 'o', 'v')) mb = body, me = end;
        if (t == FCC('m', 'o', 'o', 'f'))
            return vfail(i, ZX_IMG_E_UNSUPPORTED, "MP4: fragmented files not supported");
    }
    if (!mb) return vfail(i, ZX_IMG_E_TRUNC, "MP4: no moov box");
    i->container = ZUI_VID_MP4;
    uint32_t tb, te;
    for (uint32_t p = mb; box(d, me, p, &t, &body, &end); p = end) {
        if (t != FCC('t', 'r', 'a', 'k')) continue;
        uint32_t ab, ae, hb, he, db, de, nb, ne, sb, se;
        if (!child(d, body, end, FCC('m', 'd', 'i', 'a'), &ab, &ae)) continue;
        if (!child(d, ab, ae, FCC('h', 'd', 'l', 'r'), &hb, &he) || he - hb < 12) continue;
        if (zi_le32(d + hb + 8) != FCC('v', 'i', 'd', 'e')) continue;
        if (!child(d, ab, ae, FCC('m', 'd', 'h', 'd'), &db, &de) || de - db < 24)
            return vfail(i, ZX_IMG_E_DATA, "MP4: no mdhd");
        if (d[db] == 1) {
            if (de - db < 32) return vfail(i, ZX_IMG_E_DATA, "MP4: bad mdhd");
            v->timescale = zi_be32(d + db + 20);
        } else {
            v->timescale = zi_be32(d + db + 12);
        }
        if (!child(d, ab, ae, FCC('m', 'i', 'n', 'f'), &nb, &ne) ||
            !child(d, nb, ne, FCC('s', 't', 'b', 'l'), &sb, &se))
            return vfail(i, ZX_IMG_E_DATA, "MP4: no sample table");
        tb = sb, te = se;
        goto found;
    }
    return vfail(i, ZX_IMG_E_DATA, "MP4: no video track");
found:
    if (!v->timescale) return vfail(i, ZX_IMG_E_DATA, "MP4: zero timescale");
    uint32_t b, e;
    /* stsd: the first sample entry names the codec and the size */
    if (!child(d, tb, te, FCC('s', 't', 's', 'd'), &b, &e) || e - b < 8 + 86 ||
        zi_be32(d + b + 4) < 1)
        return vfail(i, ZX_IMG_E_DATA, "MP4: bad stsd");
    uint32_t et, eb, ee;
    if (!box(d, e, b + 8, &et, &eb, &ee) || ee - eb < 78)
        return vfail(i, ZX_IMG_E_DATA, "MP4: bad sample entry");
    i->fourcc = et;
    i->codec = codec_of(et);
    uint32_t w = zi_be16(d + eb + 24), h = zi_be16(d + eb + 26);
    static const uint32_t CFG[4] = {FCC('a', 'v', 'c', 'C'), FCC('h', 'v', 'c', 'C'),
                                    FCC('a', 'v', '1', 'C'), FCC('v', 'p', 'c', 'C')};
    for (int k = 0; k < 4 && !v->config; k++) {
        uint32_t cb, ce;
        if (child(d, eb + 78, ee, CFG[k], &cb, &ce)) v->config = cb, v->nconfig = ce - cb;
    }
    int rc = dims_ok(i, w, h);
    if (rc) return rc;
    i->w = w, i->h = h;
    /* the sample tables */
    if (child(d, tb, te, FCC('s', 't', 'z', '2'), &b, &e))
        return vfail(i, ZX_IMG_E_UNSUPPORTED, "MP4: stz2 not supported");
    if (!child(d, tb, te, FCC('s', 't', 's', 'z'), &b, &e) || e - b < 12)
        return vfail(i, ZX_IMG_E_DATA, "MP4: no stsz");
    v->stsz_const = zi_be32(d + b + 4);
    v->nsamples = zi_be32(d + b + 8);
    v->stsz = b + 12;
    if (!v->stsz_const && (e - b - 12) / 4 < v->nsamples)
        return vfail(i, ZX_IMG_E_DATA, "MP4: stsz too short");
    if (child(d, tb, te, FCC('s', 't', 'c', 'o'), &b, &e)) {
        v->co64 = false;
    } else if (child(d, tb, te, FCC('c', 'o', '6', '4'), &b, &e)) {
        v->co64 = true;
    } else {
        return vfail(i, ZX_IMG_E_DATA, "MP4: no chunk offsets");
    }
    if (e - b < 8) return vfail(i, ZX_IMG_E_DATA, "MP4: bad chunk offsets");
    v->nchunks = zi_be32(d + b + 4);
    v->stco = b + 8;
    if ((e - b - 8) / (v->co64 ? 8u : 4u) < v->nchunks)
        return vfail(i, ZX_IMG_E_DATA, "MP4: chunk table too short");
    if (!child(d, tb, te, FCC('s', 't', 's', 'c'), &b, &e) || e - b < 8)
        return vfail(i, ZX_IMG_E_DATA, "MP4: no stsc");
    v->nstsc = zi_be32(d + b + 4);
    v->stsc = b + 8;
    if ((e - b - 8) / 12 < v->nstsc || !v->nstsc || zi_be32(d + v->stsc) != 1)
        return vfail(i, ZX_IMG_E_DATA, "MP4: bad stsc");
    for (uint32_t k = 0; k < v->nstsc; k++) {
        uint32_t fc = zi_be32(d + v->stsc + 12 * k), spc = zi_be32(d + v->stsc + 12 * k + 4);
        if (!spc || (k && fc <= zi_be32(d + v->stsc + 12 * (k - 1))))
            return vfail(i, ZX_IMG_E_DATA, "MP4: bad stsc");
    }
    if (!child(d, tb, te, FCC('s', 't', 't', 's'), &b, &e) || e - b < 8)
        return vfail(i, ZX_IMG_E_DATA, "MP4: no stts");
    v->nstts = zi_be32(d + b + 4);
    v->stts = b + 8;
    if ((e - b - 8) / 8 < v->nstts) return vfail(i, ZX_IMG_E_DATA, "MP4: stts too short");
    uint64_t total = 0;
    for (uint32_t k = 0; k < v->nstts; k++)
        total += (uint64_t) zi_be32(d + v->stts + 8 * k) * zi_be32(d + v->stts + 8 * k + 4);
    if (!v->nsamples || !v->nchunks) return vfail(i, ZX_IMG_E_DATA, "MP4: no samples");
    i->frames = v->nsamples;
    i->duration_us = ticks_us(total, 1, v->timescale);
    if (v->nstts == 1 && zi_be32(d + v->stts + 4)) /* constant rate; else 0/0, variable */
        i->rate_num = v->timescale, i->rate_den = zi_be32(d + v->stts + 4);
    return 0;
}

static void mp4_reset(zui_video_t *v)
{
    v->sample = 0;
    v->chunk = 0;
    v->stsc_i = 0;
    v->stts_i = 0;
    v->stts_left = 0;
    v->dts = 0;
    v->chunk_left = zi_be32(v->d + v->stsc + 4);
    v->chunk_off = v->co64 ? (uint64_t) zi_be32(v->d + v->stco) << 32 | zi_be32(v->d + v->stco + 4)
                           : zi_be32(v->d + v->stco);
}

/* ---- the front end -------------------------------------------------------- */
static zui_vid_container_t sniff(const uint8_t *d, uint32_t n)
{
    if (n >= 10 && tok(d, 0, 9, "YUV4MPEG2") && d[9] == ' ') return ZUI_VID_Y4M;
    if (n >= 12 && zi_le32(d) == FCC('R', 'I', 'F', 'F') &&
        zi_le32(d + 8) == FCC('A', 'V', 'I', ' '))
        return ZUI_VID_AVI;
    if (n >= 4 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8') return ZUI_VID_GIF;
    if (n >= 4 && zi_be32(d) == 0x1A45DFA3u) return ZUI_VID_MKV;
    if (n >= 4 && zi_le32(d) == FCC('O', 'g', 'g', 'S')) return ZUI_VID_OGG;
    if (n >= 189 && d[0] == 0x47 && d[188] == 0x47) return ZUI_VID_TS;
    if (n >= 8) {
        uint32_t t = zi_le32(d + 4);
        if (t == FCC('f', 't', 'y', 'p') || t == FCC('m', 'o', 'o', 'v') ||
            t == FCC('m', 'd', 'a', 't') || t == FCC('f', 'r', 'e', 'e') ||
            t == FCC('w', 'i', 'd', 'e') || t == FCC('s', 'k', 'i', 'p'))
            return ZUI_VID_MP4;
    }
    return ZUI_VID_NONE;
}

static int head(const uint8_t *d, uint32_t n, zui_video_info_t *i, zui_video_t *v, y4m_t *y)
{
    zx_image_info_t ii;
    int rc;
    switch (sniff(d, n)) {
    case ZUI_VID_Y4M:
        return y4m_head(d, n, i, y);
    case ZUI_VID_AVI:
        return avi_head(d, n, i, v);
    case ZUI_VID_MP4:
        return mp4_head(d, n, i, v);
    case ZUI_VID_GIF:
        rc = zx_image_probe(d, n, &ii);
        if (rc) return vfail(i, rc, ii.why);
        i->container = ZUI_VID_GIF, i->codec = ZUI_VC_GIF, i->w = ii.w, i->h = ii.h,
        i->frames = ii.frames;
        return 0;
    case ZUI_VID_MKV:
        i->container = ZUI_VID_MKV;
        return vfail(i, ZX_IMG_E_UNSUPPORTED, "Matroska/WebM demux not implemented");
    case ZUI_VID_OGG:
        i->container = ZUI_VID_OGG;
        return vfail(i, ZX_IMG_E_UNSUPPORTED, "Ogg demux not implemented");
    case ZUI_VID_TS:
        i->container = ZUI_VID_TS;
        return vfail(i, ZX_IMG_E_UNSUPPORTED, "MPEG-TS demux not implemented");
    default:
        return vfail(i, ZX_IMG_E_FORMAT, "not a video format we know");
    }
}

static uint32_t work_for(const zui_video_info_t *i)
{
    uint64_t t = (uint64_t) i->w * i->h * 4 + 16;
    if (i->codec == ZUI_VC_MJPEG && !i->hw) t += zui_jpeg_work_max(i->w, i->h);
    if (i->codec == ZUI_VC_GIF) t += (uint64_t) i->w * i->h * 4 + ZUI_GIF_WORK;
    return t > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) t;
}

int zui_video_probe(const uint8_t *d, uint32_t n, const zui_video_hw_t *hw, zui_video_info_t *info)
{
    if (!info) return ZX_IMG_E_ARG;
    zui_video_t v;
    y4m_t y;
    zi_zero(&v, sizeof v);
    zi_zero(info, sizeof *info);
    info->why = "";
    if (!d) return vfail(info, ZX_IMG_E_ARG, "no data");
    int rc = head(d, n, info, &v, &y);
    if (!rc) rc = route(info, hw);
    if (!rc) info->work = work_for(info);
    return rc;
}

int zui_video_open(zui_video_t *v, const uint8_t *d, uint32_t n, void *work, uint32_t worklen,
                   const zui_video_hw_t *hw)
{
    if (!v || !d || !work) return ZX_IMG_E_ARG;
    zi_zero(v, sizeof *v);
    zui_video_info_t *i = &v->info;
    i->why = "";
    y4m_t y;
    zi_zero(&y, sizeof y);
    int rc = head(d, n, i, v, &y);
    if (!rc) rc = route(i, hw);
    if (rc) return rc;
    i->work = work_for(i);
    if (worklen < i->work) return vfail(i, ZX_IMG_E_SPACE, "work too small");
    uint8_t *w = (uint8_t *) work;
    w += (8 - ((uintptr_t) w & 7)) & 7;
    v->d = d, v->n = n, v->hw = i->hw ? hw : 0;
    v->frame = (uint32_t *) (void *) w;
    v->scratch = w + (size_t) i->w * i->h * 4;
    v->scratchlen = worklen - (uint32_t) (v->scratch - (uint8_t *) work);
    for (uint32_t k = 0; k < i->w * i->h; k++) v->frame[k] = 0xFF000000u;
    if (i->container == ZUI_VID_Y4M) {
        v->first = v->pos = y.hdr_end;
        v->y4m_cw = y.cw, v->y4m_ch = y.ch, v->y4m_cs = (uint8_t) y.cs, v->y4m_full = y.full;
    } else if (i->container == ZUI_VID_MP4) {
        mp4_reset(v);
    } else if (i->container == ZUI_VID_GIF) {
        uint32_t *prev = (uint32_t *) (void *) v->scratch;
        rc = zui_gif_open(&v->gif, d, n, v->frame, prev, v->scratch + (size_t) i->w * i->h * 4,
                          ZUI_GIF_WORK);
        if (rc) return vfail(i, rc, "GIF: cannot open");
    }
    return 0;
}

static int show(zui_video_t *v, const uint8_t *au, uint32_t len)
{
    zui_video_info_t *i = &v->info;
    uint32_t w = i->w, h = i->h;
    if (i->codec == ZUI_VC_RGB) {
        uint32_t bpp = v->bpp / 8, stride = (w * v->bpp + 31) / 32 * 4;
        if ((uint64_t) stride * h > len) return vfail(i, ZX_IMG_E_DATA, "AVI: raw frame too short");
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *r = au + (size_t) (v->top_down ? y : h - 1 - y) * stride;
            for (uint32_t x = 0; x < w; x++)
                v->frame[y * w + x] = zi_argb(255, r[bpp * x + 2], r[bpp * x + 1], r[bpp * x]);
        }
        return 0;
    }
    /* Motion-JPEG: a JPEG of exactly the stream's size, nothing else */
    zx_image_info_t ii;
    int rc = zx_image_probe(au, len, &ii);
    if (rc) return vfail(i, rc, "Motion-JPEG: bad frame");
    if (ii.format != ZX_IMG_JPEG)
        return vfail(i, ZX_IMG_E_DATA, "Motion-JPEG: frame is not a JPEG");
    if (ii.w != w || ii.h != h)
        return vfail(i, ii.w == w && ii.h * 2 == h ? ZX_IMG_E_UNSUPPORTED : ZX_IMG_E_DATA,
                     ii.h * 2 == h ? "Motion-JPEG: two-field (interlaced) frames not supported"
                                   : "Motion-JPEG: frame size differs from the stream's");
    rc = zx_image_decode(au, len, v->scratch, v->scratchlen, v->frame, w * h, &ii);
    return rc ? vfail(i, rc, ii.why) : 0;
}

int zui_video_next_frame(zui_video_t *v, zui_video_frame_t *f)
{
    if (!v || !f || !v->d) return ZX_IMG_E_ARG;
    zui_video_info_t *i = &v->info;
    int rc;
    f->px = v->frame, f->w = i->w, f->h = i->h, f->index = v->index;
    if (i->container == ZUI_VID_Y4M) {
        rc = y4m_next(v, f);
    } else if (i->container == ZUI_VID_GIF) {
        uint32_t cs = 0;
        rc = zui_gif_next(&v->gif, &cs);
        if (rc < 0) return vfail(i, rc, "GIF: bad frame");
        if (cs < 2) cs = 10; /* browsers play 0 and 1 cs as 10 */
        f->pts_us = v->t_us;
        f->dur_us = (uint64_t) cs * 10000u;
        v->t_us += f->dur_us;
    } else if (v->hw) {
        const zui_video_hw_t *hw = v->hw;
        if (!v->hw_open) {
            const uint8_t *cfg = v->nconfig ? v->d + v->config : 0;
            if (hw->open && hw->open(hw->ctx, i->codec, i->w, i->h, cfg, v->nconfig) < 0)
                return vfail(i, ZUI_VID_E_CODEC, "the platform decoder refused the stream");
            v->hw_open = true;
        }
        for (;;) {
            const uint8_t *au = 0;
            uint32_t len = 0;
            uint64_t pts = 0, dur = 0, out = 0;
            if (!v->eos) {
                rc = next_au(v, &au, &len, &pts, &dur);
                if (rc < 0) return rc;
                if (rc == 0) v->eos = true, au = 0, len = 0;
            }
            rc = hw->decode(hw->ctx, au, len, pts, v->frame, i->w, &out);
            if (rc < 0) return vfail(i, ZX_IMG_E_DATA, "the platform decoder failed");
            if (rc == 1) {
                f->pts_us = out;
                f->dur_us = dur;
                break;
            }
            if (v->eos) return 0;
        }
        rc = 1;
    } else {
        const uint8_t *au;
        uint32_t len;
        uint64_t pts, dur;
        rc = next_au(v, &au, &len, &pts, &dur);
        if (rc <= 0) return rc;
        if (len) { /* an empty chunk is a dropped frame: the last picture stays */
            rc = show(v, au, len);
            if (rc) return rc;
        }
        f->pts_us = pts, f->dur_us = dur;
        rc = 1;
    }
    if (rc == 1) v->index++;
    return rc;
}

void zui_video_rewind(zui_video_t *v)
{
    if (!v || !v->d) return;
    v->index = 0;
    v->t_us = 0;
    v->eos = false;
    v->pos = v->first;
    if (v->info.container == ZUI_VID_MP4) mp4_reset(v);
    if (v->info.container == ZUI_VID_GIF) zui_gif_rewind(&v->gif);
    if (v->hw_open) {
        if (v->hw->close) v->hw->close(v->hw->ctx);
        v->hw_open = false;
    }
}

void zui_video_close(zui_video_t *v)
{
    if (v && v->hw_open && v->hw->close) v->hw->close(v->hw->ctx);
    if (v) v->hw_open = false;
}

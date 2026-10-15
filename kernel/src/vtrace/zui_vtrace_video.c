/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_vtrace_video.c — films to ZXVM and back. See zui_vtrace_video.h. */
#include "zui_vtrace_video.h"

#include "sha256.h"
#include "zui_vec.h"
#include "zui_video.h"

#define ISZ   (2u * ZVM_GOP_CAP) /* hash index slots (a power of two) */
#define LIM24 (1 << 24)

/* ---- little helpers ------------------------------------------------------- */
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v, p[1] = (uint8_t) (v >> 8), p[2] = (uint8_t) (v >> 16),
    p[3] = (uint8_t) (v >> 24);
}
static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t) v), put32(p + 4, (uint32_t) (v >> 32));
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static uint64_t get64(const uint8_t *p)
{
    return (uint64_t) get32(p) | (uint64_t) get32(p + 4) << 32;
}
static void bzero8(void *p, uint64_t n)
{
    uint8_t *b = (uint8_t *) p;
    while (n--) *b++ = 0;
}
static bool beq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}
static uint64_t fnv(const uint8_t *p, uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

/* a bounded byte writer (uv / zz as in ZXVG) */
typedef struct {
    uint8_t *p;
    uint32_t n, cap;
    bool full;
} bw_t;
static void b_u8(bw_t *w, uint32_t v)
{
    if (w->n >= w->cap) {
        w->full = true;
        return;
    }
    w->p[w->n++] = (uint8_t) v;
}
static void b_uv(bw_t *w, uint32_t v)
{
    while (v >= 0x80u) {
        b_u8(w, (v & 0x7Fu) | 0x80u);
        v >>= 7;
    }
    b_u8(w, v);
}
static void b_zz(bw_t *w, int32_t s)
{
    b_uv(w, s >= 0 ? (uint32_t) s << 1 : ((uint32_t) (-(s + 1)) << 1) | 1u);
}
static void b_bytes(bw_t *w, const uint8_t *s, uint32_t n)
{
    if (w->cap - w->n < n || w->n > w->cap) {
        w->full = true;
        return;
    }
    for (uint32_t i = 0; i < n; i++) w->p[w->n + i] = s[i];
    w->n += n;
}

/* a bounded reader */
typedef struct {
    const uint8_t *p;
    uint32_t i, end;
    bool bad;
} br_t;
static uint32_t r_u8(br_t *r)
{
    if (r->i >= r->end) {
        r->bad = true;
        return 0;
    }
    return r->p[r->i++];
}
static uint32_t r_uv(br_t *r)
{
    uint64_t v = 0;
    for (int k = 0; k < 5; k++) {
        uint32_t b = r_u8(r);
        v |= (uint64_t) (b & 0x7Fu) << (7 * k);
        if (!(b & 0x80u)) {
            if (v > 0xFFFFFFFFull) r->bad = true;
            return (uint32_t) v;
        }
    }
    r->bad = true;
    return 0;
}
static int32_t r_zz(br_t *r) /* limited to |v| <= 2^24 */
{
    uint32_t u = r_uv(r);
    int64_t v = (u & 1u) ? -(int64_t) (u >> 1) - 1 : (int64_t) (u >> 1);
    if (v > LIM24 || v < -LIM24) {
        r->bad = true;
        return 0;
    }
    return (int32_t) v;
}

static uint64_t al8(uint64_t v)
{
    return (v + 7u) & ~(uint64_t) 7u;
}
static void *take(uint8_t **at, uint8_t *end, uint64_t bytes, bool *bad)
{
    uint8_t *p = (uint8_t *) (((uintptr_t) *at + 7u) & ~(uintptr_t) 7u);
    if (p > end || (uint64_t) (end - p) < bytes) {
        *bad = true;
        return 0;
    }
    *at = p + bytes;
    return p;
}

void zvm_opts_default(zvm_opts_t *o)
{
    o->gop = 60, o->scene_thr = 40, o->scene_pct = 30, o->max_frames = 0;
}

/* the ZXVG header of a frame of a GOP (zui_vtrace.c's emit_head, no gradients) */
static void frame_head(bw_t *w, uint32_t fw, uint32_t fh, const uint8_t *rgb, const uint32_t *pal,
                       uint32_t npal)
{
    b_u8(w, 'Z'), b_u8(w, 'X'), b_u8(w, 'V'), b_u8(w, 'G'), b_u8(w, 1), b_u8(w, 0);
    b_zz(w, 0), b_zz(w, 0), b_uv(w, fw * 16u), b_uv(w, fh * 16u);
    b_uv(w, npal);
    for (uint32_t i = 0; i < npal; i++) {
        if (rgb)
            b_u8(w, rgb[3 * i]), b_u8(w, rgb[3 * i + 1]), b_u8(w, rgb[3 * i + 2]);
        else
            b_u8(w, (pal[i] >> 16) & 255u), b_u8(w, (pal[i] >> 8) & 255u), b_u8(w, pal[i] & 255u);
        b_u8(w, 255);
    }
    b_uv(w, 0), b_uv(w, 0);
}

/* ---- writing --------------------------------------------------------------- */
static uint32_t shape_cap(uint32_t n)
{
    return n / 2u + 64u;
}

uint64_t zvm_writer_work_size(uint32_t w, uint32_t h, uint32_t max_frames)
{
    uint64_t tw = zvt_work_size(w, h), om = zvt_out_max(w, h);
    if (!tw || !max_frames || om > 0xFFFFFFFFull) return 0;
    uint64_t n = (uint64_t) w * h;
    return al8(tw) + al8(om) + al8(shape_cap((uint32_t) n) * (uint64_t) sizeof(zvt_shape_t)) +
           al8(n * 4u) + al8(n) + al8((uint64_t) h * 4u) +
           al8((uint64_t) ZVM_GOP_CAP * sizeof(zvm_tab_t)) + al8((uint64_t) ISZ * 4u) +
           al8((uint64_t) max_frames * ZVM_ENTRY_LEN) + 64u;
}

int zvm_writer_begin(zvm_writer_t *wr, uint32_t w, uint32_t h, uint32_t max_frames,
                     uint32_t rate_num, uint32_t rate_den, const uint8_t *cid,
                     const zvt_params_t *p, const zvm_opts_t *o, const zvt_backend_t *be,
                     void *work, uint64_t worklen, uint8_t *out, uint32_t outcap)
{
    if (!wr || !p || !work || !out) return ZVT_E_ARG;
    bzero8(wr, sizeof *wr);
    if (!w || !h || w > ZVT_MAX_DIM || h > ZVT_MAX_DIM || (uint64_t) w * h > ZVT_MAX_PIXELS)
        return ZVT_E_SIZE;
    if (!max_frames || max_frames > (0xFFFFFFFFu - ZVM_HEAD_LEN) / ZVM_ENTRY_LEN) return ZVT_E_ARG;
    uint64_t need = zvm_writer_work_size(w, h, max_frames);
    if (!need) return ZVT_E_SIZE;
    if (worklen < need) return ZVT_E_SPACE;
    if (outcap < ZVM_HEAD_LEN + 32u) return ZVT_E_SPACE;
    wr->w = w, wr->h = h, wr->n = w * h, wr->max_frames = max_frames;
    wr->p = *p;
    wr->p.gradients = false;
    if (o)
        wr->o = *o;
    else
        zvm_opts_default(&wr->o);
    if (wr->o.gop < 1) wr->o.gop = 1;
    if (wr->o.gop > 100000u) wr->o.gop = 100000u;
    if (wr->o.scene_pct > 100u) wr->o.scene_pct = 100u;
    wr->be = be ? be : &zvt_cpu_backend;
    wr->out = out, wr->cap = outcap;
    wr->rate_num = rate_num, wr->rate_den = rate_den;
    uint8_t *at = (uint8_t *) work, *end = at + worklen;
    bool bad = false;
    wr->twlen = zvt_work_size(w, h);
    wr->tw = (uint8_t *) take(&at, end, wr->twlen, &bad);
    wr->zxvg_cap = (uint32_t) zvt_out_max(w, h);
    wr->zxvg = (uint8_t *) take(&at, end, wr->zxvg_cap, &bad);
    wr->shape_cap = shape_cap(wr->n);
    wr->shapes =
        (zvt_shape_t *) take(&at, end, (uint64_t) wr->shape_cap * sizeof(zvt_shape_t), &bad);
    wr->prev = (uint32_t *) take(&at, end, (uint64_t) wr->n * 4u, &bad);
    wr->mask = (uint8_t *) take(&at, end, wr->n, &bad);
    wr->rows = (uint32_t *) take(&at, end, (uint64_t) h * 4u, &bad);
    wr->tab = (zvm_tab_t *) take(&at, end, (uint64_t) ZVM_GOP_CAP * sizeof(zvm_tab_t), &bad);
    wr->index = (uint32_t *) take(&at, end, (uint64_t) ISZ * 4u, &bad);
    wr->table = (uint8_t *) take(&at, end, (uint64_t) max_frames * ZVM_ENTRY_LEN, &bad);
    if (bad) return ZVT_E_SPACE;
    bzero8(out, ZVM_HEAD_LEN);
    wr->len = ZVM_HEAD_LEN;
    if (cid) {
        for (uint32_t i = 0; i < ZVT_CID_LEN; i++) wr->r.cid[i] = cid[i];
    } else {
        wr->cid_of_pixels = true;
    }
    wr->r.w = w, wr->r.h = h;
    wr->r.psnr_min = ZVT_PSNR_INF, wr->r.ssim_min = 10000;
    return 0;
}

static void o_u8(zvm_writer_t *wr, uint32_t v)
{
    bw_t b = {wr->out, wr->len, wr->cap, false};
    b_u8(&b, v);
    wr->len = b.n, wr->full |= b.full;
}
static void o_uv(zvm_writer_t *wr, uint32_t v)
{
    bw_t b = {wr->out, wr->len, wr->cap, false};
    b_uv(&b, v);
    wr->len = b.n, wr->full |= b.full;
}
static void o_zz(zvm_writer_t *wr, int32_t v)
{
    bw_t b = {wr->out, wr->len, wr->cap, false};
    b_zz(&b, v);
    wr->len = b.n, wr->full |= b.full;
}

static bool in24(int32_t v)
{
    return v <= LIM24 && v >= -LIM24;
}

int zvm_writer_frame(zvm_writer_t *wr, const uint32_t *px, uint64_t pts_us, uint64_t dur_us)
{
    if (!wr || !wr->out || !px) return ZVT_E_ARG;
    if (wr->r.frames >= wr->max_frames) return ZVT_E_LIMIT;
    uint32_t fi = wr->r.frames;
    if (fi) { /* presentation times strictly increase */
        uint64_t prev = get64(wr->table + (uint64_t) (fi - 1u) * ZVM_ENTRY_LEN);
        if (pts_us <= prev) return ZVT_E_ARG;
    }
    if (dur_us > 0xFFFFFFFFull) dur_us = 0xFFFFFFFFull;
    bool key = fi == 0 || wr->since_key >= wr->o.gop;
    if (!key) { /* a scene change? */
        const zvt_backend_t *be = wr->be;
        if (!be->diff ||
            be->diff(be->ctx, wr->prev, px, wr->w, wr->h, wr->o.scene_thr, wr->mask, wr->rows)) {
            if (be != &zvt_cpu_backend) wr->r.fallbacks++;
            zvt_cpu_diff(wr->prev, px, wr->w, wr->h, wr->o.scene_thr, wr->mask, wr->rows);
        }
        uint64_t changed = 0;
        for (uint32_t y = 0; y < wr->h; y++) changed += wr->rows[y];
        if (changed * 100u > (uint64_t) wr->o.scene_pct * wr->n) key = true;
    }
    int32_t len;
    for (;;) {
        zvt_extra_t ex = {key ? 0 : wr->pal, key ? 0 : wr->npal, wr->shapes, wr->shape_cap};
        len = zvt_trace(px, wr->w, wr->h, wr->w, 0, 0, &wr->p, wr->be, &ex, wr->tw, wr->twlen,
                        wr->zxvg, wr->zxvg_cap, &wr->last);
        if (len < 0) return len;
        if (wr->last.nshapes != wr->last.paths) return ZVT_E_LIMIT; /* shape table too small */
        if (!key && wr->ntab + wr->last.nshapes > ZVM_GOP_CAP) {
            key = true; /* the GOP's table is full: start a new GOP */
            continue;
        }
        if (key && wr->last.nshapes > ZVM_GOP_CAP) return ZVT_E_LIMIT;
        break;
    }
    wr->r.fallbacks += wr->last.fallbacks;
    wr->zxvg_len = (uint32_t) len;
    wr->last_key = key;
    uint32_t rec = wr->len;
    if (key) {
        wr->ntab = 0;
        bzero8(wr->index, (uint64_t) ISZ * 4u);
        wr->npal = wr->last.npal;
        for (uint32_t i = 0; i < wr->npal; i++) wr->pal[i] = wr->last.pal[i];
        wr->since_key = 0;
        wr->r.keyframes++;
        o_u8(wr, 0);
        o_uv(wr, wr->npal);
        for (uint32_t i = 0; i < wr->npal; i++) {
            o_u8(wr, (wr->pal[i] >> 16) & 255u);
            o_u8(wr, (wr->pal[i] >> 8) & 255u);
            o_u8(wr, wr->pal[i] & 255u);
        }
    } else {
        o_u8(wr, 1);
    }
    o_uv(wr, wr->last.nshapes);
    for (uint32_t s = 0; s < wr->last.nshapes; s++) {
        const zvt_shape_t *sh = &wr->shapes[s];
        if (sh->len < 1 || sh->off > (uint32_t) len || sh->len > (uint32_t) len - sh->off)
            return ZVT_E_CHECK;
        int32_t dex = sh->ex - sh->x0, dey = sh->ey - sh->y0;
        if (!in24(sh->x0) || !in24(sh->y0) || !in24(dex) || !in24(dey)) return ZVT_E_LIMIT;
        const uint8_t *body = wr->zxvg + sh->off;
        uint64_t hv = fnv(body, sh->len);
        uint32_t slot = (uint32_t) (hv ^ (hv >> 29)) & (ISZ - 1u), found = 0;
        while (wr->index[slot]) {
            const zvm_tab_t *t = &wr->tab[wr->index[slot] - 1u];
            if (t->hash == hv && t->len == sh->len && t->dex == dex && t->dey == dey &&
                beq(wr->out + t->off, body, sh->len)) {
                found = wr->index[slot];
                break;
            }
            slot = (slot + 1u) & (ISZ - 1u);
        }
        if (found) {
            o_uv(wr, ((found - 1u) << 1) | 1u);
            o_zz(wr, sh->x0), o_zz(wr, sh->y0);
            wr->r.shapes_ref++;
        } else {
            o_uv(wr, sh->len << 1);
            o_zz(wr, sh->x0), o_zz(wr, sh->y0), o_zz(wr, dex), o_zz(wr, dey);
            uint32_t at = wr->len;
            bw_t b = {wr->out, wr->len, wr->cap, false};
            b_bytes(&b, body, sh->len);
            wr->len = b.n, wr->full |= b.full;
            if (wr->full) return ZVT_E_SPACE;
            zvm_tab_t *t = &wr->tab[wr->ntab];
            t->off = at, t->len = sh->len, t->dex = dex, t->dey = dey, t->hash = hv;
            wr->index[slot] = ++wr->ntab;
            wr->r.shapes_new++;
        }
        if (wr->full) return ZVT_E_SPACE;
    }
    if (wr->full) return ZVT_E_SPACE;
    if (wr->ntab > wr->gop_max) wr->gop_max = wr->ntab;
    uint8_t *e = wr->table + (uint64_t) fi * ZVM_ENTRY_LEN;
    bzero8(e, ZVM_ENTRY_LEN);
    put64(e, pts_us);
    put32(e + 8, (uint32_t) dur_us);
    put32(e + 12, rec);
    put32(e + 16, wr->len - rec);
    put32(e + 20, (uint32_t) wr->last.psnr_cdb);
    put32(e + 24, (uint32_t) wr->last.ssim);
    e[28] = key ? 0 : 1;
    for (uint32_t i = 0; i < wr->n; i++) wr->prev[i] = px[i];
    wr->since_key++;
    wr->r.frames++;
    wr->r.zxvg_bytes += (uint32_t) len;
    if ((uint32_t) len > wr->r.bytes) wr->r.bytes = (uint32_t) len; /* frame_max until the end */
    int32_t ps = wr->last.psnr_cdb > 9999 ? 9999 : wr->last.psnr_cdb;
    wr->psnr_sum += ps, wr->ssim_sum += wr->last.ssim;
    if (wr->last.psnr_cdb < wr->r.psnr_min) wr->r.psnr_min = wr->last.psnr_cdb;
    if (wr->last.ssim < wr->r.ssim_min) wr->r.ssim_min = wr->last.ssim;
    wr->r.duration_us = pts_us + dur_us;
    return 0;
}

int32_t zvm_writer_end(zvm_writer_t *wr, zvm_result_t *res)
{
    if (!wr || !wr->out || !wr->r.frames) return ZVT_E_ARG;
    uint32_t table_off = wr->len, frames = wr->r.frames;
    uint64_t total = (uint64_t) table_off + (uint64_t) frames * ZVM_ENTRY_LEN + 32u;
    if (total > wr->cap || wr->full) return ZVT_E_SPACE;
    for (uint64_t i = 0; i < (uint64_t) frames * ZVM_ENTRY_LEN; i++)
        wr->out[table_off + i] = wr->table[i];
    uint32_t frame_max = wr->r.bytes;
    uint8_t *hd = wr->out;
    hd[0] = 'Z', hd[1] = 'X', hd[2] = 'V', hd[3] = 'M', hd[4] = 1;
    hd[5] = wr->cid_of_pixels ? 1 : 0, hd[6] = (uint8_t) wr->p.preset, hd[7] = 0;
    put32(hd + 8, wr->w), put32(hd + 12, wr->h), put32(hd + 16, frames),
        put32(hd + 20, wr->r.keyframes);
    put32(hd + 24, wr->rate_num), put32(hd + 28, wr->rate_den);
    put64(hd + 32, wr->r.duration_us);
    for (uint32_t i = 0; i < ZVT_CID_LEN; i++) hd[40 + i] = wr->r.cid[i];
    put32(hd + 76, table_off), put32(hd + 80, (uint32_t) total);
    put32(hd + 84, wr->gop_max), put32(hd + 88, frame_max), put32(hd + 92, 0);
    sha256(wr->out, (size_t) (total - 32u), wr->out + total - 32u);
    wr->len = (uint32_t) total;
    wr->r.bytes = (uint32_t) total;
    wr->r.psnr_avg = (int32_t) (wr->psnr_sum / frames);
    wr->r.ssim_avg = (int32_t) (wr->ssim_sum / frames);
    if (res) *res = wr->r;
    return (int32_t) total;
}

/* ---- the whole film ------------------------------------------------------------ */
static uint32_t film_frames(const zui_video_info_t *vi, const zvm_opts_t *o)
{
    uint32_t m = o && o->max_frames ? o->max_frames : vi->frames ? vi->frames : 65536u;
    if (o && o->max_frames && vi->frames && vi->frames < m) m = vi->frames;
    return m;
}

uint64_t zvm_trace_video_work(const uint8_t *file, uint32_t n, const zvm_opts_t *o)
{
    zui_video_info_t vi;
    if (!file || zui_video_probe(file, n, 0, &vi) < 0) return 0;
    uint64_t ww = zvm_writer_work_size(vi.w, vi.h, film_frames(&vi, o));
    if (!ww) return 0;
    return al8(sizeof(zui_video_t)) + al8(vi.work) + ww + 64u;
}

int32_t zvm_trace_video(const uint8_t *file, uint32_t n, const zvt_params_t *p, const zvm_opts_t *o,
                        const zvt_backend_t *be, void *work, uint64_t worklen, uint8_t *out,
                        uint32_t outcap, zvm_result_t *res)
{
    if (!file || !p || !work || !out) return ZVT_E_ARG;
    zui_video_info_t vi;
    if (zui_video_probe(file, n, 0, &vi) < 0) return ZVT_E_DATA;
    uint32_t mf = film_frames(&vi, o);
    if (worklen < zvm_trace_video_work(file, n, o)) return ZVT_E_SPACE;
    uint8_t *at = (uint8_t *) work, *end = at + worklen;
    bool bad = false;
    zui_video_t *v = (zui_video_t *) take(&at, end, sizeof(zui_video_t), &bad);
    void *vw = take(&at, end, vi.work, &bad);
    uint8_t *rest = (uint8_t *) take(&at, end, 0, &bad);
    if (bad) return ZVT_E_SPACE;
    if (zui_video_open(v, file, n, vw, vi.work, 0) < 0) return ZVT_E_DATA;
    uint8_t cid[ZVT_CID_LEN];
    zvt_cid(file, n, cid);
    zvm_writer_t wr;
    int rc = zvm_writer_begin(&wr, vi.w, vi.h, mf, vi.rate_num, vi.rate_den, cid, p, o, be, rest,
                              (uint64_t) (end - rest), out, outcap);
    if (rc < 0) {
        zui_video_close(v);
        return rc;
    }
    int vrc = 0;
    for (uint32_t i = 0; i < mf; i++) {
        zui_video_frame_t f;
        int r = zui_video_next_frame(v, &f);
        if (r <= 0) {
            vrc = r;
            break;
        }
        if (f.w != vi.w || f.h != vi.h) {
            vrc = ZVT_E_DATA;
            break;
        }
        if ((rc = zvm_writer_frame(&wr, f.px, f.pts_us, f.dur_us)) < 0) {
            zui_video_close(v);
            return rc;
        }
    }
    zui_video_close(v);
    if (!wr.r.frames) return ZVT_E_DATA;
    int32_t len = zvm_writer_end(&wr, res);
    if (len > 0 && res) res->video_rc = vrc;
    return len;
}

/* ---- reading ----------------------------------------------------------------- */
typedef struct {
    const uint8_t *d;
    uint32_t w, h;
    zvm_tab_t *tab; /* NULL: count only (probe) */
    uint32_t ntab, cap;
    uint32_t pal_off, npal;
    uint8_t *fr; /* NULL: no frame */
    uint32_t frcap, frlen;
} walk_t;

/* One record: the GOP table brought up to date and, when fr is set, the
 * frame's ZXVG rebuilt. 0, or ZVT_E_DATA. */
static int walk(walk_t *k, uint32_t off, uint32_t len, bool key)
{
    br_t r = {k->d, off, off + len, false};
    uint32_t kind = r_u8(&r);
    if (kind != (key ? 0u : 1u)) return ZVT_E_DATA;
    if (key) {
        k->ntab = 0;
        k->npal = r_uv(&r);
        if (k->npal < 1 || k->npal > 256u) return ZVT_E_DATA;
        k->pal_off = r.i;
        if (r.end - r.i < 3u * k->npal) return ZVT_E_DATA;
        r.i += 3u * k->npal;
    } else if (!k->npal) {
        return ZVT_E_DATA;
    }
    uint32_t ns = r_uv(&r);
    if (r.bad || ns > len / 3u) return ZVT_E_DATA;
    bw_t b = {k->fr, 0, k->frcap, false};
    if (k->fr) frame_head(&b, k->w, k->h, k->d + k->pal_off, 0, k->npal);
    int64_t cx = 0, cy = 0;
    for (uint32_t s = 0; s < ns; s++) {
        uint32_t tag = r_uv(&r);
        int32_t x0 = r_zz(&r), y0 = r_zz(&r);
        if (r.bad) return ZVT_E_DATA;
        uint32_t id;
        if (tag & 1u) {
            id = tag >> 1;
            if (id >= k->ntab) return ZVT_E_DATA;
        } else {
            uint32_t bl = tag >> 1;
            int32_t dex = r_zz(&r), dey = r_zz(&r);
            if (r.bad || bl < 1 || r.end - r.i < bl || k->ntab >= k->cap) return ZVT_E_DATA;
            id = k->ntab++;
            if (k->tab) {
                zvm_tab_t *t = &k->tab[id];
                t->off = r.i, t->len = bl, t->dex = dex, t->dey = dey, t->hash = 0;
            }
            r.i += bl;
        }
        if (k->fr) {
            const zvm_tab_t *t = &k->tab[id];
            b_u8(&b, 0x07), b_u8(&b, 0x01);
            b_zz(&b, (int32_t) (x0 - cx)), b_zz(&b, (int32_t) (y0 - cy));
            b_bytes(&b, k->d + t->off, t->len);
            cx = (int64_t) x0 + t->dex, cy = (int64_t) y0 + t->dey;
            if (b.full) return ZVT_E_DATA;
        }
    }
    if (r.bad || r.i != r.end) return ZVT_E_DATA;
    if (k->fr) {
        b_u8(&b, 0x00);
        if (b.full) return ZVT_E_DATA;
        k->frlen = b.n;
    }
    return 0;
}

int zvm_probe(const uint8_t *d, uint32_t n, zvm_info_t *info)
{
    if (!d || !info) return ZVT_E_ARG;
    bzero8(info, sizeof *info);
    if (n < ZVM_HEAD_LEN + ZVM_ENTRY_LEN + 32u) return ZVT_E_DATA;
    if (d[0] != 'Z' || d[1] != 'X' || d[2] != 'V' || d[3] != 'M' || d[4] != 1 || d[5] > 1 ||
        d[6] >= ZVT_PRESET_COUNT || d[7])
        return ZVT_E_DATA;
    uint32_t w = get32(d + 8), h = get32(d + 12), frames = get32(d + 16), keys = get32(d + 20);
    uint32_t table_off = get32(d + 76), total = get32(d + 80), gop = get32(d + 84),
             fmax = get32(d + 88);
    if (!w || !h || w > ZVT_MAX_DIM || h > ZVT_MAX_DIM || (uint64_t) w * h > ZVT_MAX_PIXELS)
        return ZVT_E_DATA;
    if (total != n || get32(d + 92) || !frames || !keys || keys > frames) return ZVT_E_DATA;
    if (table_off < ZVM_HEAD_LEN ||
        (uint64_t) table_off + (uint64_t) frames * ZVM_ENTRY_LEN + 32u != n)
        return ZVT_E_DATA;
    if (gop < 1 || gop > ZVM_GOP_CAP || fmax < 16u || fmax > zvt_out_max(w, h)) return ZVT_E_DATA;
    uint8_t dg[32];
    sha256(d, (size_t) n - 32u, dg);
    if (!beq(dg, d + n - 32u, 32)) return ZVT_E_DATA;
    walk_t k = {d, w, h, 0, 0, ZVM_GOP_CAP, 0, 0, 0, 0, 0};
    uint32_t expect = ZVM_HEAD_LEN, nkeys = 0, gmax = 0;
    uint64_t prev_pts = 0, dur_end = 0;
    for (uint32_t i = 0; i < frames; i++) {
        const uint8_t *e = d + table_off + (uint64_t) i * ZVM_ENTRY_LEN;
        uint64_t pts = get64(e);
        uint32_t dur = get32(e + 8), ro = get32(e + 12), rl = get32(e + 16);
        int32_t ps = (int32_t) get32(e + 20), ss = (int32_t) get32(e + 24);
        if (i && pts <= prev_pts) return ZVT_E_DATA;
        if (ro != expect || rl < 2u || rl > table_off - ro) return ZVT_E_DATA;
        if (e[28] > 1 || (i == 0 && e[28] != 0) || e[29] || e[30] || e[31]) return ZVT_E_DATA;
        if (ps < -100000 || ps > ZVT_PSNR_INF || ss < -10000 || ss > 10000) return ZVT_E_DATA;
        if (walk(&k, ro, rl, e[28] == 0) < 0) return ZVT_E_DATA;
        if (k.ntab > gmax) gmax = k.ntab;
        nkeys += e[28] == 0;
        expect = ro + rl, prev_pts = pts, dur_end = pts + dur;
    }
    if (expect != table_off || nkeys != keys || gmax != gop || get64(d + 32) != dur_end)
        return ZVT_E_DATA;
    info->w = w, info->h = h, info->frames = frames, info->keyframes = keys;
    info->rate_num = get32(d + 24), info->rate_den = get32(d + 28);
    info->duration_us = dur_end;
    info->preset = (zvt_preset_t) d[6];
    info->cid_of_pixels = d[5] & 1u;
    for (uint32_t i = 0; i < ZVT_CID_LEN; i++) info->cid[i] = d[40 + i];
    info->gop_shapes = gop, info->frame_max = fmax;
    info->work =
        al8((uint64_t) gop * sizeof(zvm_tab_t)) + al8(fmax) + al8(sizeof(zui_vec_work_t)) + 32u;
    return 0;
}

static walk_t pl_walk(zvm_player_t *pl, bool frame)
{
    walk_t k = {pl->d,
                pl->info.w,
                pl->info.h,
                pl->tab,
                pl->ntab,
                pl->info.gop_shapes,
                pl->pal_off,
                pl->npal,
                frame ? pl->frame : 0,
                pl->info.frame_max,
                0};
    return k;
}

static int pl_step(zvm_player_t *pl, uint32_t i, bool frame, uint32_t *frlen)
{
    const uint8_t *e = pl->d + get32(pl->d + 76) + (uint64_t) i * ZVM_ENTRY_LEN;
    walk_t k = pl_walk(pl, frame);
    if (walk(&k, get32(e + 12), get32(e + 16), e[28] == 0) < 0) return ZVT_E_DATA;
    pl->ntab = k.ntab, pl->pal_off = k.pal_off, pl->npal = k.npal;
    if (frlen) *frlen = k.frlen;
    return 0;
}

int zvm_open(zvm_player_t *pl, const uint8_t *d, uint32_t n, void *work, uint64_t worklen)
{
    if (!pl || !d || !work) return ZVT_E_ARG;
    bzero8(pl, sizeof *pl);
    int rc = zvm_probe(d, n, &pl->info);
    if (rc < 0) return rc;
    if (worklen < pl->info.work) return ZVT_E_SPACE;
    uint8_t *at = (uint8_t *) work, *end = at + worklen;
    bool bad = false;
    pl->tab =
        (zvm_tab_t *) take(&at, end, (uint64_t) pl->info.gop_shapes * sizeof(zvm_tab_t), &bad);
    pl->frame = (uint8_t *) take(&at, end, pl->info.frame_max, &bad);
    pl->vw = take(&at, end, sizeof(zui_vec_work_t), &bad);
    if (bad) return ZVT_E_SPACE;
    pl->d = d, pl->n = n;
    uint32_t longest = 0;
    for (uint32_t i = 0; i < pl->info.frames; i++) {
        uint32_t fl = 0;
        if (pl_step(pl, i, true, &fl) < 0 || zui_zxvg_check(pl->frame, fl) < 0) {
            pl->d = 0;
            return ZVT_E_DATA;
        }
        if (fl > longest) longest = fl;
    }
    if (longest != pl->info.frame_max) {
        pl->d = 0;
        return ZVT_E_DATA;
    }
    zvm_rewind(pl);
    return 0;
}

void zvm_rewind(zvm_player_t *pl)
{
    if (pl) pl->next = 0, pl->ntab = 0, pl->npal = 0, pl->pal_off = 0;
}

int zvm_seek(zvm_player_t *pl, uint32_t i)
{
    if (!pl || !pl->d || i >= pl->info.frames) return ZVT_E_ARG;
    const uint8_t *tb = pl->d + get32(pl->d + 76);
    uint32_t k = i;
    while (k > 0 && tb[(uint64_t) k * ZVM_ENTRY_LEN + 28] != 0) k--;
    zvm_rewind(pl);
    for (uint32_t j = k; j < i; j++)
        if (pl_step(pl, j, false, 0) < 0) return ZVT_E_DATA;
    pl->next = i;
    return 0;
}

int zvm_next_frame(zvm_player_t *pl, zui_surface_t *s, zui_rect_t box, zvm_frame_t *f)
{
    if (!pl || !pl->d) return ZVT_E_ARG;
    if (pl->next >= pl->info.frames) return 0;
    uint32_t i = pl->next, fl = 0;
    if (pl_step(pl, i, true, &fl) < 0) return ZVT_E_DATA;
    if (s && zui_vec_draw_zxvg(s, (zui_vec_work_t *) pl->vw, pl->frame, fl, box, 0) < 0)
        return ZVT_E_DATA;
    const uint8_t *e = pl->d + get32(pl->d + 76) + (uint64_t) i * ZVM_ENTRY_LEN;
    if (f) {
        f->index = i;
        f->pts_us = get64(e), f->dur_us = get32(e + 8);
        f->psnr_cdb = (int32_t) get32(e + 20), f->ssim = (int32_t) get32(e + 24);
        f->key = e[28] == 0;
        f->zxvg = pl->frame, f->zxvg_len = fl;
    }
    pl->next++;
    return 1;
}

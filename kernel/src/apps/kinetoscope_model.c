/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kinetoscope_model.c — Kinetoscope's model (kinetoscope_model.h). */
#include <string.h>

#include "kinetoscope_model.h"

#define KS_US      INT64_C(1000000)
#define KS_U32_MAX INT64_C(4294967295)
/* a segment's bytes as base64 (files.read) and as hex (stream.segment) */
#define KS_B64_MAX (4u * ((KINETOSCOPE_SEG_BYTES + 2u) / 3u))
#define KS_HEX_MAX (2u * KINETOSCOPE_SEG_BYTES)
/* the units of one stream.plan request as JSON: "[len,pts,false]," each */
#define KS_UNITS_JSON (16u + KINETOSCOPE_UNITS * 48u)

/* ---- small helpers, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static void say(kinetoscope_t *k, const char *a)
{
    uint32_t n = len_of(a, KINETOSCOPE_NOTICE_MAX);
    memcpy(k->notice, a, n);
    k->notice[n] = 0;
}

static int32_t b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Padded base64 into out; the bytes decoded, or -1 (not base64, or no room). */
static int32_t b64_dec(const char *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t k = 0;
    if (n % 4u) return -1;
    for (uint32_t i = 0; i < n; i += 4u) {
        int32_t v[4];
        uint32_t pad = 0;
        for (uint32_t j = 0; j < 4u; j++) {
            if (i + 4u == n && j >= 2u && in[i + j] == '=') {
                if (j == 2u && in[i + 3u] != '=') return -1;
                pad++;
                v[j] = 0;
                continue;
            }
            v[j] = b64_val(in[i + j]);
            if (v[j] < 0) return -1;
        }
        uint32_t q =
            (uint32_t) v[0] << 18 | (uint32_t) v[1] << 12 | (uint32_t) v[2] << 6 | (uint32_t) v[3];
        uint32_t got = 3u - pad;
        if (k + got > cap) return -1;
        out[k++] = (uint8_t) (q >> 16);
        if (got > 1u) out[k++] = (uint8_t) (q >> 8);
        if (got > 2u) out[k++] = (uint8_t) q;
    }
    return (int32_t) k;
}

static uint32_t le16(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t) le32(p) | (uint64_t) le32(p + 4) << 32;
}

/* the digits of v at out; their count, or 0 when they do not fit */
static uint32_t put_u64(char *out, uint32_t cap, uint64_t v)
{
    char d[20];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    if (n > cap) return 0;
    for (uint32_t i = 0; i < n; i++) out[i] = d[n - 1u - i];
    return n;
}

static bool put_text(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = len_of(s, cap);
    if (*at + n > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    return true;
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, uint64_t v)
{
    uint32_t n = put_u64(out + *at, cap - *at, v);
    *at += n;
    return n > 0;
}

/* a decimal string zxvd gives for a wide integer (t_start_us) */
static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[24];
    int64_t x = 0;
    if (zxm_get_str(l, obj, key, s, sizeof s) || !s[0]) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9' || x > (INT64_MAX - 9) / 10) return ZXM_E_PROTOCOL;
        x = x * 10 + (s[i] - '0');
    }
    *v = x;
    return ZXM_OK;
}

/* up to max bytes of the open video from offset, decoded into out */
static int read_bytes(kinetoscope_t *k, const char *path, int64_t offset, uint32_t max,
                      uint8_t *out, uint32_t cap, int32_t *got, int64_t *size)
{
    static char enc[KS_B64_MAX + 8u];
    zxm_link_t *l = k->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "offset", (uint64_t) offset);
    zxm_p_u64(l, "max", max);
    zxm_p_str(l, "encoding", "base64");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "base64", enc, sizeof enc)) return ZXM_E_PROTOCOL;
    if (size && zxm_get_i64(l, l->result, "size", size)) return ZXM_E_PROTOCOL;
    *got = b64_dec(enc, len_of(enc, sizeof enc), out, cap);
    return *got < 0 || (uint32_t) *got > max ? ZXM_E_PROTOCOL : ZXM_OK;
}

/* KM2: is a frame whose data starts with b a keyframe? */
static bool keyframe(int32_t codec, uint8_t b)
{
    if (codec == KINETOSCOPE_VP8) return (b & 1u) == 0;
    if (b >> 6 != 2u) return false; /* VP9's frame marker */
    uint32_t profile = (uint32_t) (b >> 5 & 1u) | (uint32_t) (b >> 4 & 1u) << 1;
    uint32_t at = profile == 3u ? 2u : 3u; /* profile 3 has a reserved bit first */
    if (profile == 3u && (b >> 3 & 1u)) return false;
    if (b >> at & 1u) return false; /* shown again, no new picture */
    return (b >> (at - 1u) & 1u) == 0;
}

/* ---- KM1: a video ---- */

void kinetoscope_init(kinetoscope_t *k, zxm_link_t *link)
{
    memset(k, 0, sizeof *k);
    k->link = link;
}

int kinetoscope_open(kinetoscope_t *k, const char *path)
{
    uint8_t h[KINETOSCOPE_IVF_HDR];
    int32_t got = 0;
    int64_t size = 0;
    uint32_t n = len_of(path, KINETOSCOPE_PATH_MAX + 1u);
    if (!path || n == 0 || n > KINETOSCOPE_PATH_MAX) return ZXM_E_ARG;
    int rc = read_bytes(k, path, 0, KINETOSCOPE_IVF_HDR, h, sizeof h, &got, &size);
    if (rc) return rc;
    int32_t codec =
        got == (int32_t) KINETOSCOPE_IVF_HDR && !memcmp(h + 8, "VP80", 4)   ? KINETOSCOPE_VP8
        : got == (int32_t) KINETOSCOPE_IVF_HDR && !memcmp(h + 8, "VP90", 4) ? KINETOSCOPE_VP9
                                                                            : 0;
    int64_t w = le16(h + 12), ht = le16(h + 14), rate = le32(h + 16), scale = le32(h + 20),
            frames = le32(h + 24);
    if (!codec || memcmp(h, "DKIF", 4) || le16(h + 4) != 0 || le16(h + 6) != KINETOSCOPE_IVF_HDR ||
        !kinetoscope_fit_domain(w, ht, 0, 0) || rate < 1 || rate > KINETOSCOPE_FRATE_MAX ||
        scale < 1 || scale > KINETOSCOPE_FRATE_MAX || scale * KS_US / rate > KS_U32_MAX) {
        say(k, "Not a video Kinetoscope plays: an IVF file of VP8 or VP9 is");
        return ZXM_E_STATE;
    }
    memset(k->unit, 0, sizeof k->unit);
    memset(k->seg, 0, sizeof k->seg);
    memcpy(k->path, path, n + 1u);
    k->codec = codec;
    k->width = w;
    k->height = ht;
    k->fnum = rate;
    k->fden = scale;
    k->frames = frames;
    k->length_ms = frames * scale * 1000 / rate;
    k->size = size;
    k->next_off = KINETOSCOPE_IVF_HDR;
    k->first_frame = k->read_frames = 0;
    k->at_end = size <= (int64_t) KINETOSCOPE_IVF_HDR;
    k->nunits = k->nsegs = 0;
    k->seek_points = 0;
    k->notice[0] = 0;
    k->open = true;
    return ZXM_OK;
}

/* ---- KM2: the index ---- */

int kinetoscope_index(kinetoscope_t *k)
{
    static kinetoscope_unit_t got[KINETOSCOPE_UNITS];
    uint8_t h[KINETOSCOPE_FRAME_HDR + 1u];
    if (!k->open) return ZXM_E_STATE;
    if (k->at_end) {
        say(k, "The end of the video");
        return ZXM_E_STATE;
    }
    int64_t off = k->next_off, last = k->nunits ? k->unit[k->nunits - 1u].pts_us : 0;
    uint32_t n = 0;
    while (n < KINETOSCOPE_UNITS && off < k->size) {
        int32_t g = 0;
        int rc = read_bytes(k, k->path, off, sizeof h, h, sizeof h, &g, NULL);
        if (rc) return rc;
        int64_t len = le32(h);
        uint64_t ts = le64(h + 4);
        if (g != (int32_t) sizeof h || len < 1 || len > (int64_t) KINETOSCOPE_SEG_BYTES ||
            off + (int64_t) KINETOSCOPE_FRAME_HDR + len > k->size ||
            ts > (uint64_t) (INT64_MAX / KS_US / k->fden)) {
            say(k, "A frame is cut short, empty, too large or out of time");
            return ZXM_E_STATE;
        }
        int64_t pts = (int64_t) ts * k->fden * KS_US / k->fnum;
        if (pts < last) {
            say(k, "A frame's time goes backwards");
            return ZXM_E_STATE;
        }
        got[n].file_off = off + (int64_t) KINETOSCOPE_FRAME_HDR;
        got[n].len = len;
        got[n].pts_us = pts;
        got[n].key = keyframe(k->codec, h[KINETOSCOPE_FRAME_HDR]);
        last = pts;
        off += (int64_t) KINETOSCOPE_FRAME_HDR + len;
        n++;
        zxm_progress(k->link, (uint64_t) off, (uint64_t) k->size);
    }
    memcpy(k->unit, got, n * sizeof got[0]);
    k->first_frame = k->read_frames;
    k->read_frames += n;
    k->nunits = n;
    k->next_off = off;
    k->at_end = off >= k->size;
    k->nsegs = 0;
    k->seek_points = 0;
    return ZXM_OK;
}

/* ---- KM3: the plan ---- */

int kinetoscope_plan(kinetoscope_t *k, int64_t target_us, int64_t max_us)
{
    static char units[KS_UNITS_JSON];
    static kinetoscope_seg_t got[KINETOSCOPE_UNITS];
    int64_t key[KINETOSCOPE_UNITS];
    uint32_t at = 0;
    if (target_us < 1 || target_us > KS_U32_MAX || max_us < target_us || max_us > KS_U32_MAX)
        return ZXM_E_ARG;
    if (!k->open || k->nunits == 0) return ZXM_E_STATE;
    bool ok = put_text(units, sizeof units, &at, "{\"units\":[");
    for (uint32_t i = 0; ok && i < k->nunits; i++)
        ok = put_text(units, sizeof units, &at, i ? ",[" : "[") &&
             put_num(units, sizeof units, &at, (uint64_t) k->unit[i].len) &&
             put_text(units, sizeof units, &at, ",") &&
             put_num(units, sizeof units, &at, (uint64_t) k->unit[i].pts_us) &&
             put_text(units, sizeof units, &at, k->unit[i].key ? ",true]" : ",false]");
    if (!ok || !put_text(units, sizeof units, &at, "]}")) return ZXM_E_TOOBIG;
    zxm_link_t *l = k->link;
    zxm_begin(l, "stream.plan");
    zxm_p_raw(l, units, at);
    zxm_p_i64(l, "target_us", target_us);
    zxm_p_i64(l, "max_us", max_us);
    zxm_p_u64(l, "max_bytes", KINETOSCOPE_SEG_BYTES);
    zxm_p_i64(l, "tail_us", k->fden * KS_US / k->fnum);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "segments");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t ns = zxm_len(l, arr), next = 0;
    if (ns == 0 || ns > KINETOSCOPE_UNITS) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < ns; i++) {
        uint64_t first = 0, nu = 0;
        int32_t o = zxm_at(l, arr, i);
        memset(&got[i], 0, sizeof got[i]);
        if (o < 0 || zxm_get_u64(l, o, "first_unit", &first) || zxm_get_u64(l, o, "nunits", &nu) ||
            zxm_get_i64(l, o, "offset", &got[i].offset) || zxm_get_i64(l, o, "len", &got[i].len) ||
            get_dec(l, o, "t_start_us", &got[i].t_start_us) ||
            zxm_get_i64(l, o, "dur_us", &got[i].dur_us) ||
            zxm_get_bool(l, o, "key_start", &got[i].key_start))
            return ZXM_E_PROTOCOL;
        /* the segments follow one another and cover the window */
        if (first != next || nu == 0 || first + nu > k->nunits || got[i].len < 1 ||
            got[i].len > (int64_t) KINETOSCOPE_SEG_BYTES ||
            got[i].t_start_us != k->unit[first].pts_us || got[i].key_start != k->unit[first].key)
            return ZXM_E_PROTOCOL;
        got[i].first_unit = (uint32_t) first;
        got[i].nunits = (uint32_t) nu;
        key[i] = got[i].key_start ? 1 : 0;
        next = (uint32_t) (first + nu);
    }
    if (next != k->nunits) return ZXM_E_PROTOCOL;
    memcpy(k->seg, got, ns * sizeof got[0]);
    k->nsegs = ns;
    k->seek_points = kinetoscope_seek_points(key, ns);
    return ZXM_OK;
}

/* ---- KM4: a segment described ---- */

int kinetoscope_segment(kinetoscope_t *k, uint32_t seg, uint64_t stream_id, uint32_t seq)
{
    static uint8_t data[KINETOSCOPE_SEG_BYTES];
    static char hex[KS_HEX_MAX + 1u];
    static const char digit[] = "0123456789abcdef";
    if (!k->open) return ZXM_E_STATE;
    if (seg >= k->nsegs) return ZXM_E_ARG;
    const kinetoscope_seg_t *s = &k->seg[seg];
    uint32_t at = 0;
    for (uint32_t u = s->first_unit; u < s->first_unit + s->nunits; u++) {
        const kinetoscope_unit_t *x = &k->unit[u];
        int32_t g = 0;
        if (at + (uint32_t) x->len > sizeof data) return ZXM_E_PROTOCOL;
        int rc = read_bytes(k, k->path, x->file_off, (uint32_t) x->len, data + at,
                            (uint32_t) sizeof data - at, &g, NULL);
        if (rc) return rc;
        if (g != (int32_t) x->len) {
            say(k, "The video changed since it was indexed");
            return ZXM_E_STATE;
        }
        at += (uint32_t) g;
    }
    if ((int64_t) at != s->len) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < at; i++) {
        hex[2u * i] = digit[data[i] >> 4];
        hex[2u * i + 1u] = digit[data[i] & 15u];
    }
    hex[2u * at] = 0;
    zxm_link_t *l = k->link;
    zxm_begin(l, "stream.segment");
    zxm_p_str(l, "data_hex", hex);
    zxm_p_u64(l, "stream_id", stream_id);
    zxm_p_u64(l, "seq", seq);
    int rc = zxm_call(l);
    if (rc) return rc;
    char cid[KINETOSCOPE_SEGCID_MAX + 1u];
    int64_t len = 0, nf = 0;
    if (zxm_get_str(l, l->result, "cid", cid, sizeof cid) ||
        zxm_get_i64(l, l->result, "data_len", &len) ||
        zxm_get_i64(l, l->result, "nfreights", &nf) || len != s->len || nf < 1 || cid[0] != 'b')
        return ZXM_E_PROTOCOL;
    memcpy(k->seg[seg].cid, cid, sizeof cid);
    k->seg[seg].nfreights = nf;
    k->seg[seg].described = true;
    return ZXM_OK;
}

/* ---- KM5, KM6: the fit and the frame ---- */

int kinetoscope_fit(const kinetoscope_t *k, int64_t winw, int64_t winh, kinetoscope_rect_t *r)
{
    if (!r) return ZXM_E_ARG;
    if (!k->open) return ZXM_E_STATE;
    int64_t w = kinetoscope_fit_w(k->width, k->height, winw, winh);
    int64_t h = kinetoscope_fit_h(k->width, k->height, winw, winh);
    if (w < 0 || h < 0) return ZXM_E_ARG;
    r->w = w;
    r->h = h;
    r->x = (winw - w) / 2;
    r->y = (winh - h) / 2;
    return ZXM_OK;
}

int64_t kinetoscope_frame(const kinetoscope_t *k, int64_t pos_ms)
{
    if (!k->open) return KINETOSCOPE_REFUSED;
    return kinetoscope_frame_index(pos_ms, k->fnum, k->fden);
}

int64_t kinetoscope_hall_frame(const kinetoscope_t *k)
{
    if (!k->hall.set) return KINETOSCOPE_REFUSED;
    return kinetoscope_frame(k, k->hall.pos_ms);
}

int kinetoscope_time_text(int64_t ms, char *out, uint32_t cap)
{
    char d[24];
    if (!out || cap == 0) return ZXM_E_ARG;
    int64_t hms = kinetoscope_hms(ms);
    if (hms < 0) return ZXM_E_ARG;
    int64_t h = hms / 10000, m = hms / 100 % 100, s = hms % 100;
    uint32_t i = sizeof d;
    d[--i] = 0;
    d[--i] = (char) ('0' + s % 10);
    d[--i] = (char) ('0' + s / 10);
    d[--i] = ':';
    d[--i] = (char) ('0' + m % 10);
    d[--i] = (char) ('0' + m / 10);
    d[--i] = ':';
    do {
        d[--i] = (char) ('0' + h % 10);
        h /= 10;
    } while (h);
    uint32_t n = (uint32_t) sizeof d - i;
    if (n > cap) return ZXM_E_TOOBIG;
    memcpy(out, d + i, n);
    return ZXM_OK;
}

/* ---- KM7: the watch hall ---- */

/* 'b' then base32 lower case, n bytes, 2..max */
static bool cid_ok(const char *s, uint32_t max)
{
    uint32_t n = len_of(s, max + 1u);
    if (n < 2u || n > max || s[0] != 'b') return false;
    for (uint32_t i = 1; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '2' && s[i] <= '7'))) return false;
    return true;
}

int kinetoscope_hall_set(kinetoscope_t *k, const char *cid)
{
    if (!cid_ok(cid, KINETOSCOPE_CID_MAX)) return ZXM_E_ARG;
    memcpy(k->hall_cid, cid, len_of(cid, KINETOSCOPE_CID_MAX) + 1u);
    memset(&k->hall, 0, sizeof k->hall);
    return ZXM_OK;
}

/* the hall.status object of the last answer into k->hall */
static int keep_hall(kinetoscope_t *k)
{
    zxm_link_t *l = k->link;
    kinetoscope_hall_t h;
    char state[8];
    memset(&h, 0, sizeof h);
    if (zxm_get_bool(l, l->result, "set", &h.set)) return ZXM_E_PROTOCOL;
    if (h.set) {
        if (zxm_get_str(l, l->result, "state", state, sizeof state) ||
            zxm_get_str(l, l->result, "media", h.media, sizeof h.media) ||
            zxm_get_i64(l, l->result, "pos_ms", &h.pos_ms) ||
            zxm_get_i64(l, l->result, "rate_q16", &h.rate_q16))
            return ZXM_E_PROTOCOL;
        h.state = !strcmp(state, "play")    ? KINETOSCOPE_HALL_PLAY
                  : !strcmp(state, "pause") ? KINETOSCOPE_HALL_PAUSE
                  : !strcmp(state, "stop")  ? KINETOSCOPE_HALL_STOP
                                            : KINETOSCOPE_HALL_NONE;
        if (h.state == KINETOSCOPE_HALL_NONE || h.pos_ms < 0 ||
            !kinetoscope_hall_rate_ok(h.rate_q16))
            return ZXM_E_PROTOCOL;
    }
    if (zxm_get(l, l->result, "viewers") >= 0 && zxm_get_i64(l, l->result, "viewers", &h.viewers))
        return ZXM_E_PROTOCOL;
    if (zxm_get(l, l->result, "may_control") >= 0 &&
        zxm_get_bool(l, l->result, "may_control", &h.may_control))
        return ZXM_E_PROTOCOL;
    k->hall = h;
    return ZXM_OK;
}

/* arguments first, then the hall, then the owner's word (when the call
 * changes what every viewer sees); ZXM_OK when the call may be sent */
static int hall_gate(kinetoscope_t *k, int64_t pos_ms, int64_t rate_q16, bool changes,
                     bool owner_confirmed)
{
    if (pos_ms < -1 || (rate_q16 != 0 && !kinetoscope_hall_rate_ok(rate_q16))) return ZXM_E_ARG;
    if (!k->hall_cid[0]) return ZXM_E_STATE;
    if (changes && !owner_confirmed) {
        say(k, "Only the owner's confirmed word changes what the hall shows");
        return ZXM_E_STATE;
    }
    return ZXM_OK;
}

/* one hall.* call, its gate already passed */
static int hall_call(kinetoscope_t *k, const char *method, const char *media, int64_t pos_ms,
                     int64_t rate_q16)
{
    zxm_link_t *l = k->link;
    zxm_begin(l, method);
    zxm_p_str(l, "cid", k->hall_cid);
    if (media) zxm_p_str(l, "media", media);
    if (pos_ms >= 0) zxm_p_i64(l, "pos_ms", pos_ms);
    if (rate_q16 > 0) zxm_p_i64(l, "rate_q16", rate_q16);
    int rc = zxm_call(l);
    if (rc) return rc;
    return keep_hall(k);
}

int kinetoscope_hall_load(kinetoscope_t *k, const char *media, int64_t pos_ms, bool owner_confirmed)
{
    if (!cid_ok(media, KINETOSCOPE_MEDIA_MAX)) return ZXM_E_ARG;
    int rc = hall_gate(k, pos_ms, 0, true, owner_confirmed);
    if (rc) return rc;
    return hall_call(k, "hall.load", media, pos_ms, 0);
}

int kinetoscope_hall_play(kinetoscope_t *k, int64_t pos_ms, int64_t rate_q16, bool owner_confirmed)
{
    int rc = hall_gate(k, pos_ms, rate_q16, true, owner_confirmed);
    if (rc) return rc;
    return hall_call(k, "hall.play", NULL, pos_ms, rate_q16);
}

int kinetoscope_hall_pause(kinetoscope_t *k, int64_t pos_ms, bool owner_confirmed)
{
    int rc = hall_gate(k, pos_ms, 0, true, owner_confirmed);
    if (rc) return rc;
    return hall_call(k, "hall.pause", NULL, pos_ms, 0);
}

int kinetoscope_hall_seek(kinetoscope_t *k, int64_t pos_ms, bool owner_confirmed)
{
    if (pos_ms < 0) return ZXM_E_ARG; /* hall.seek needs a position */
    int rc = hall_gate(k, pos_ms, 0, true, owner_confirmed);
    if (rc) return rc;
    return hall_call(k, "hall.seek", NULL, pos_ms, 0);
}

int kinetoscope_hall_status(kinetoscope_t *k)
{
    int rc = hall_gate(k, -1, 0, false, false);
    if (rc) return rc;
    return hall_call(k, "hall.status", NULL, -1, 0);
}

/* ---- KM8: resume ---- */

static int32_t resume_slot(const kinetoscope_t *k)
{
    for (uint32_t i = 0; i < KINETOSCOPE_RESUME; i++)
        if (k->resume[i].used && !strcmp(k->resume[i].path, k->path)) return (int32_t) i;
    return -1;
}

int kinetoscope_resume_keep(kinetoscope_t *k, int64_t pos_ms, int64_t stamp)
{
    if (!k->open) return ZXM_E_STATE;
    int32_t at = resume_slot(k);
    for (uint32_t i = 0; at < 0 && i < KINETOSCOPE_RESUME; i++)
        if (!k->resume[i].used) at = (int32_t) i;
    if (at < 0) {
        say(k, "The resume table is full: forget a video first");
        return ZXM_E_STATE;
    }
    if (pos_ms < 0 || pos_ms > k->length_ms || stamp < 0 ||
        !kinetoscope_resume_row_ok(at, pos_ms, k->length_ms, stamp))
        return ZXM_E_ARG;
    kinetoscope_resume_t *r = &k->resume[at];
    memcpy(r->path, k->path, sizeof r->path);
    r->pos_ms = pos_ms;
    r->length_ms = k->length_ms;
    r->stamp = stamp;
    r->used = true;
    return ZXM_OK;
}

int kinetoscope_resume_find(const kinetoscope_t *k, int64_t *pos_ms)
{
    if (!pos_ms) return ZXM_E_ARG;
    if (!k->open) return ZXM_E_STATE;
    int32_t at = resume_slot(k);
    if (at < 0) return ZXM_E_STATE;
    *pos_ms = k->resume[at].pos_ms;
    return ZXM_OK;
}

int kinetoscope_resume_forget(kinetoscope_t *k)
{
    if (!k->open) return ZXM_E_STATE;
    int32_t at = resume_slot(k);
    if (at < 0) return ZXM_E_STATE;
    memset(&k->resume[at], 0, sizeof k->resume[at]);
    return ZXM_OK;
}

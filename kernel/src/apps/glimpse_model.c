/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* glimpse_model.c — Glimpse's model (glimpse_model.h). */
#include <string.h>

#include "glimpse_model.h"

#define GLIMPSE_E_FILES (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define GLIMPSE_PREFIX  "Screenshot "
#define GLIMPSE_SUFFIX  ".png"
/* the units of one stream.plan request as JSON: "[len,pts,false]," each */
#define GLIMPSE_UNITS_JSON (16u + GLIMPSE_UNITS * 48u)

/* ---- small text helpers, bounded ---- */

/* Append s to out (cap bytes) at *at; false when it does not fit. */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    size_t n = strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n + 1u);
    *at += (uint32_t) n;
    return true;
}

/* v in decimal, a '-' first when below 0. */
static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char d[24];
    uint32_t k = sizeof d - 1u;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    d[k] = 0;
    do {
        d[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) d[--k] = '-';
    return put(out, cap, at, d + k);
}

static void say(glimpse_t *g, const char *a, int64_t v, const char *b)
{
    uint32_t at = 0;
    g->notice[0] = 0;
    if (!put(g->notice, sizeof g->notice, &at, a)) return;
    if (b && (!put_i64(g->notice, sizeof g->notice, &at, v) ||
              !put(g->notice, sizeof g->notice, &at, b)))
        return;
}

/* "Screenshot N.png" with N 1..GLIMPSE_NUM_MAX written plainly (no sign,
 * no leading 0): N, else 0. */
static int64_t shot_number(const char *name)
{
    size_t lp = sizeof GLIMPSE_PREFIX - 1u, ls = sizeof GLIMPSE_SUFFIX - 1u, n = strlen(name);
    if (n <= lp + ls || n > lp + ls + 9u || memcmp(name, GLIMPSE_PREFIX, lp) ||
        memcmp(name + n - ls, GLIMPSE_SUFFIX, ls) || name[lp] == '0')
        return 0;
    int64_t v = 0;
    for (size_t i = lp; i < n - ls; i++) {
        if (name[i] < '0' || name[i] > '9') return 0;
        v = v * 10 + (name[i] - '0');
    }
    return v;
}

static bool path_of(int64_t n, char *out, uint32_t cap)
{
    uint32_t at = 0;
    return put(out, cap, &at, GLIMPSE_DIR "/" GLIMPSE_PREFIX) && put_i64(out, cap, &at, n) &&
           put(out, cap, &at, GLIMPSE_SUFFIX);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

/* ---- GM1, GM2: the screen and the region ---- */

void glimpse_init(glimpse_t *g, zxm_link_t *link)
{
    memset(g, 0, sizeof *g);
    g->link = link;
}

int glimpse_set_screen(glimpse_t *g, int64_t width, int64_t height)
{
    if (width < 1 || width > GLIMPSE_SIDE_MAX || height < 1 || height > GLIMPSE_SIDE_MAX)
        return ZXM_E_ARG;
    if (g->recording) return ZXM_E_STATE;
    g->screen_w = width;
    g->screen_h = height;
    g->region = false;
    g->x = g->y = g->w = g->h = 0;
    g->shot_len = 0;
    return ZXM_OK;
}

int glimpse_select(glimpse_t *g, int64_t x, int64_t y, int64_t w, int64_t h)
{
    if (x < -GLIMPSE_POS_MAX || x > GLIMPSE_POS_MAX || y < -GLIMPSE_POS_MAX ||
        y > GLIMPSE_POS_MAX || w < 0 || w > GLIMPSE_POS_MAX || h < 0 || h > GLIMPSE_POS_MAX)
        return ZXM_E_ARG;
    if (!g->screen_w || g->recording) return ZXM_E_STATE;
    int64_t cx = glimpse_capture_origin(x, g->screen_w),
            cy = glimpse_capture_origin(y, g->screen_h);
    int64_t cw = glimpse_capture_extent(x, w, g->screen_w);
    int64_t ch = glimpse_capture_extent(y, h, g->screen_h);
    if (cx < 0 || cy < 0 || cw < 0 || ch < 0) return ZXM_E_ARG;
    if (cw == 0 || ch == 0) {
        say(g, "Nothing of that region is on the screen.", 0, NULL);
        return ZXM_E_ARG;
    }
    g->x = cx, g->y = cy, g->w = cw, g->h = ch;
    g->region = true;
    g->shot_len = 0;
    g->notice[0] = 0;
    return ZXM_OK;
}

int glimpse_select_screen(glimpse_t *g)
{
    if (!g->screen_w) return ZXM_E_STATE;
    return glimpse_select(g, 0, 0, g->screen_w, g->screen_h);
}

int glimpse_take(glimpse_t *g, const uint8_t *png, uint32_t n)
{
    static const uint8_t sig[8] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    static const uint8_t iend[8] = {0, 0, 0, 0, 'I', 'E', 'N', 'D'};
    if (!g->region) return ZXM_E_STATE;
    /* signature, IHDR (length 13) first, IEND (length 0, then its CRC) last */
    if (!png || n < 8u + 25u + 12u || n > GLIMPSE_SHOT_MAX || memcmp(png, sig, sizeof sig) ||
        be32(png + 8) != 13u || memcmp(png + 12, "IHDR", 4) ||
        memcmp(png + n - 12u, iend, sizeof iend))
        return ZXM_E_ARG;
    if ((int64_t) be32(png + 16) != g->w || (int64_t) be32(png + 20) != g->h) {
        say(g, "The picture is not the size of the region.", 0, NULL);
        return ZXM_E_ARG;
    }
    memcpy(g->shot, png, n);
    g->shot_len = n;
    return ZXM_OK;
}

/* ---- GM3: room, numbers, saving ---- */

int glimpse_set_floor(glimpse_t *g, int64_t floor)
{
    if (floor < 0 || floor > GLIMPSE_ROOM_MAX) return ZXM_E_ARG;
    g->floor = floor;
    return ZXM_OK;
}

int glimpse_room(glimpse_t *g)
{
    zxm_link_t *l = g->link;
    int64_t total, free, avail;
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "total", &total) || zxm_get_i64(l, l->result, "free", &free) ||
        zxm_get_i64(l, l->result, "avail", &avail) || total < 0 || free < 0 || avail < 0)
        return ZXM_E_PROTOCOL;
    g->total = total;
    g->free = free;
    g->avail = avail > GLIMPSE_ROOM_MAX ? GLIMPSE_ROOM_MAX : avail;
    return ZXM_OK;
}

int glimpse_next_number(glimpse_t *g, int64_t *n)
{
    static int64_t got[GLIMPSE_USED_MAX];
    static char name[256];
    zxm_link_t *l = g->link;
    if (!n) return ZXM_E_ARG;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", GLIMPSE_DIR);
    int rc = zxm_call(l);
    uint32_t k = 0;
    if (rc == ZXM_E_REMOTE && l->err_code == GLIMPSE_E_FILES && strstr(l->err, "no such")) {
        g->folder = false; /* nothing saved yet */
    } else {
        if (rc) return rc;
        int32_t arr = zxm_get(l, l->result, "entries");
        if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
        uint32_t ne = zxm_len(l, arr);
        for (uint32_t i = 0; i < ne; i++) {
            int32_t e = zxm_at(l, arr, i);
            bool dir = false;
            if (e < 0 || zxm_get_bool(l, e, "dir", &dir)) return ZXM_E_PROTOCOL;
            if (zxm_get_str(l, e, "name", name, sizeof name)) return ZXM_E_PROTOCOL;
            int64_t v = dir ? 0 : shot_number(name);
            if (!v) continue;
            if (k >= GLIMPSE_USED_MAX) return ZXM_E_TOOBIG;
            got[k++] = v;
        }
        g->folder = true;
    }
    if (k == 0) got[k++] = 0; /* GR4: a 0 holds no place: an empty folder */
    int64_t free_n = glimpse_first_free(got, k);
    if (free_n < 1 || free_n > GLIMPSE_NUM_MAX) return ZXM_E_TOOBIG;
    memcpy(g->used, got, (size_t) k * sizeof got[0]);
    g->nused = k;
    *n = free_n;
    return ZXM_OK;
}

/* Bytes in one request: a multiple of 3, so every piece is whole base64. */
#define PIECE 9000u

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* RFC 4648, padded; out holds 4 * ceil(n / 3) bytes. Its length. */
static uint32_t b64_enc(const uint8_t *in, uint32_t n, char *out)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t) in[i] << 16;
        if (i + 1 < n) v |= (uint32_t) in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[k++] = B64[(v >> 18) & 63u];
        out[k++] = B64[(v >> 12) & 63u];
        out[k++] = i + 1 < n ? B64[(v >> 6) & 63u] : '=';
        out[k++] = i + 2 < n ? B64[v & 63u] : '=';
    }
    return k;
}

static int write_shot(glimpse_t *g, const char *path)
{
    static char enc[(PIECE / 3u) * 4u];
    zxm_link_t *l = g->link;
    uint32_t len = g->shot_len, at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < PIECE ? len - at : PIECE;
        bool last = at + n >= len;
        uint32_t k = b64_enc(g->shot + at, n, enc);
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "base64", enc, k);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        if (first) zxm_p_bool(l, "exclusive", true);
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
        zxm_progress(l, at, len);
    } while (at < len);
    return ZXM_OK;
}

static int make_folder(glimpse_t *g)
{
    zxm_link_t *l = g->link;
    if (g->folder) return ZXM_OK;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", GLIMPSE_DIR);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == GLIMPSE_E_FILES && strstr(l->err, "already exists"))
        rc = ZXM_OK;
    if (!rc) g->folder = true;
    return rc;
}

int glimpse_save(glimpse_t *g, bool owner_confirmed)
{
    if (!g->shot_len) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(g, "Not saved: the owner has not confirmed it.", 0, NULL);
        return ZXM_E_STATE;
    }
    g->notice[0] = 0;
    /* GM3: room first (GR5) */
    int rc = glimpse_room(g);
    if (rc) return rc;
    int64_t need = g->shot_len;
    if (!glimpse_shot_fits(need, g->avail, g->floor)) {
        say(g, "Not saved: the screenshot is ", need + g->floor - g->avail,
            " bytes more than the room left.");
        return ZXM_E_TOOBIG;
    }
    /* the first free number (GR4), then the file */
    int64_t n = 0;
    char path[GLIMPSE_PATH_MAX];
    rc = glimpse_next_number(g, &n);
    if (rc) return rc;
    if (!path_of(n, path, sizeof path)) return ZXM_E_TOOBIG;
    rc = make_folder(g);
    if (!rc) rc = write_shot(g, path);
    if (rc) return rc;
    g->last_n = n;
    memcpy(g->last_path, path, sizeof path);
    g->shot_len = 0;
    return ZXM_OK;
}

/* ---- GM4: recording ---- */

int glimpse_record_start(glimpse_t *g, int64_t bpp, int64_t fps, int64_t secs, bool owner_confirmed)
{
    if (glimpse_recording_bytes(1, 1, bpp, secs, fps, GLIMPSE_ROOM_MAX) < 0) return ZXM_E_ARG;
    if (!g->region || g->recording) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(g, "Not recording: the owner has not confirmed it.", 0, NULL);
        return ZXM_E_STATE;
    }
    g->notice[0] = 0;
    int rc = glimpse_room(g);
    if (rc) return rc;
    /* the exact size at most the room sys.disk reports, the floor kept (GR3) */
    int64_t room = g->avail - g->floor;
    int64_t need = glimpse_recording_bytes(g->w, g->h, bpp, secs, fps, GLIMPSE_ROOM_MAX);
    int64_t size =
        room < 0 ? GLIMPSE_REFUSED : glimpse_recording_bytes(g->w, g->h, bpp, secs, fps, room);
    if (need < 0) return ZXM_E_ARG;
    if (size < 0) {
        say(g, "Not recording: the recording is ", need - room, " bytes more than the room left.");
        return ZXM_E_TOOBIG;
    }
    g->bpp = bpp, g->fps = fps, g->secs = secs;
    g->rec_bytes = size;
    g->frames = 0;
    g->frames_max = secs * fps;
    g->nsegs = 0;
    g->recording = true;
    memcpy(g->indicator, GLIMPSE_REC_TEXT, sizeof GLIMPSE_REC_TEXT);
    return ZXM_OK;
}

void glimpse_record_stop(glimpse_t *g)
{
    g->recording = false;
    g->indicator[0] = 0;
}

int glimpse_record_frame(glimpse_t *g)
{
    if (!g->recording) return ZXM_E_STATE;
    g->frames++;
    if (g->frames >= g->frames_max) {
        glimpse_record_stop(g);
        say(g, "Recording stopped at its last frame, ", g->frames, " frames.");
    }
    return ZXM_OK;
}

/* ---- GM5: the cut ---- */

/* An unsigned decimal string field (stream.plan's t_start_us) into *v. */
static int get_dec(const zxm_link_t *l, int32_t o, const char *key, int64_t *v)
{
    char s[24];
    if (zxm_get_str(l, o, key, s, sizeof s) || !s[0] || strlen(s) > 18u) return -1;
    int64_t x = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return -1;
        x = x * 10 + (*p - '0');
    }
    *v = x;
    return 0;
}

int glimpse_cut(glimpse_t *g, const glimpse_unit_t *u, uint32_t n, int64_t target_us,
                int64_t max_us)
{
    static char units[GLIMPSE_UNITS_JSON];
    static glimpse_seg_t got[GLIMPSE_UNITS];
    if (!u || n < 1u || n > GLIMPSE_UNITS || target_us < 1 || target_us > GLIMPSE_US_MAX ||
        max_us < target_us || max_us > GLIMPSE_US_MAX)
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (u[i].len < 0 || u[i].len > (int64_t) GLIMPSE_SEG_BYTES || u[i].pts_us < 0 ||
            u[i].pts_us > GLIMPSE_SECS_MAX * 1000000LL || (i && u[i].pts_us < u[i - 1].pts_us))
            return ZXM_E_ARG;
    if (g->recording) return ZXM_E_STATE;
    uint32_t at = 0;
    bool ok = put(units, sizeof units, &at, "{\"units\":[");
    for (uint32_t i = 0; ok && i < n; i++)
        ok = put(units, sizeof units, &at, i ? ",[" : "[") &&
             put_i64(units, sizeof units, &at, u[i].len) && put(units, sizeof units, &at, ",") &&
             put_i64(units, sizeof units, &at, u[i].pts_us) &&
             put(units, sizeof units, &at, u[i].key ? ",true]" : ",false]");
    if (!ok || !put(units, sizeof units, &at, "]}")) return ZXM_E_TOOBIG;
    zxm_link_t *l = g->link;
    zxm_begin(l, "stream.plan");
    zxm_p_raw(l, units, at);
    zxm_p_i64(l, "target_us", target_us);
    zxm_p_i64(l, "max_us", max_us);
    zxm_p_u64(l, "max_bytes", GLIMPSE_SEG_BYTES);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "segments");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t ns = zxm_len(l, arr), next = 0;
    int64_t off = 0;
    if (ns == 0 || ns > n) return ZXM_E_PROTOCOL;
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
        /* the segments follow one another, byte for byte, and cover the units */
        if (first != next || nu == 0 || first + nu > n || got[i].offset != off || got[i].len < 0 ||
            got[i].len > (int64_t) GLIMPSE_SEG_BYTES || got[i].t_start_us != u[first].pts_us ||
            got[i].key_start != u[first].key)
            return ZXM_E_PROTOCOL;
        int64_t sum = 0;
        for (uint64_t k = first; k < first + nu; k++) sum += u[k].len;
        if (sum != got[i].len) return ZXM_E_PROTOCOL;
        got[i].first_unit = (uint32_t) first;
        got[i].nunits = (uint32_t) nu;
        next = (uint32_t) (first + nu);
        off += got[i].len;
    }
    if (next != n) return ZXM_E_PROTOCOL;
    memcpy(g->seg, got, ns * sizeof got[0]);
    g->nsegs = ns;
    return ZXM_OK;
}

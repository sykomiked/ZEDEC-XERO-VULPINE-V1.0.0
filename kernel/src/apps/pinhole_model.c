/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pinhole_model.c — Pinhole's model (pinhole_model.h). */
#include <string.h>

#include "pinhole_model.h"

#define PINHOLE_E_FILES   (-32014)          /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define PINHOLE_STAMP_MAX 999999999999999LL /* PH7: 15 digits */
#define PINHOLE_PHOTO_MAX 999999999LL       /* PH7: 9 digits */
#define PINHOLE_LINE_MAX  128u              /* one index line */

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

static void say(pinhole_t *p, const char *a, int64_t v, const char *b)
{
    uint32_t at = 0;
    p->notice[0] = 0;
    if (!put(p->notice, sizeof p->notice, &at, a)) return;
    if (b && (!put_i64(p->notice, sizeof p->notice, &at, v) ||
              !put(p->notice, sizeof p->notice, &at, b)))
        return;
}

/* One signed decimal from s[*at..n), then one separator `end`; false when
 * there is none, it has more than 18 digits, or the separator is missing. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = false;
    int64_t x = 0;
    if (i < n && s[i] == '-') neg = true, i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (s[i] - '0');
        i++;
    }
    if (!digits || i >= n || s[i] != end) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

static bool path_of(int64_t album, int64_t photo, char *out, uint32_t cap)
{
    uint32_t at = 0;
    return put(out, cap, &at, PINHOLE_DIR "/a") && put_i64(out, cap, &at, album) &&
           put(out, cap, &at, "-p") && put_i64(out, cap, &at, photo) && put(out, cap, &at, ".jpg");
}

/* ---- PM1-PM3: the camera ---- */

void pinhole_init(pinhole_t *p, zxm_link_t *link)
{
    memset(p, 0, sizeof *p);
    p->link = link;
    p->iso = 100;
    p->shutter = 8000; /* 1/125 s */
    p->floor = PINHOLE_FLOOR;
}

int pinhole_camera_on(pinhole_t *p, int64_t sensor, int64_t mode)
{
    if (!pinhole_flag(sensor) || !pinhole_flag(mode)) return ZXM_E_ARG;
    if (!pinhole_camera_ready(sensor, mode, 1)) { /* no frame yet: the buffer is free */
        say(p, sensor ? "The camera does not support that mode." : "No camera sensor is present.",
            0, NULL);
        return ZXM_E_STATE;
    }
    p->sensor = sensor;
    p->mode = mode;
    p->on = true;
    memcpy(p->indicator, PINHOLE_ON_TEXT, sizeof PINHOLE_ON_TEXT);
    return ZXM_OK;
}

void pinhole_camera_off(pinhole_t *p)
{
    p->on = false;
    p->indicator[0] = 0;
    p->sensor = p->mode = 0;
}

int pinhole_set_exposure(pinhole_t *p, int64_t iso, int64_t shutter, int64_t comp)
{
    if (!pinhole_exposure_ok(iso, shutter, comp)) return ZXM_E_ARG;
    p->iso = iso;
    p->shutter = shutter;
    p->comp = comp;
    return ZXM_OK;
}

int pinhole_take(pinhole_t *p, const uint8_t *jpeg, uint32_t n, int64_t width, int64_t height)
{
    if (!p->on) return ZXM_E_STATE;
    int64_t buffer = n >= 4u && n <= PINHOLE_FRAME_MAX ? 1 : 0;
    if (!jpeg || !pinhole_camera_ready(p->sensor, p->mode, buffer)) return ZXM_E_ARG;
    /* a JPEG starts with SOI (FF D8) and ends with EOI (FF D9) */
    if (jpeg[0] != 0xFFu || jpeg[1] != 0xD8u || jpeg[n - 2u] != 0xFFu || jpeg[n - 1u] != 0xD9u)
        return ZXM_E_ARG;
    if (width < 1 || width > PINHOLE_SIDE_MAX || height < 1 || height > PINHOLE_SIDE_MAX)
        return ZXM_E_ARG;
    memcpy(p->frame, jpeg, n);
    p->frame_len = n;
    p->width = width;
    p->height = height;
    return ZXM_OK;
}

/* ---- PM4: room ---- */

int pinhole_set_floor(pinhole_t *p, int64_t floor)
{
    if (floor < 0 || floor > LIFEBOAT_BYTES_MAX) return ZXM_E_ARG;
    p->floor = floor;
    return ZXM_OK;
}

int pinhole_room(pinhole_t *p)
{
    zxm_link_t *l = p->link;
    int64_t total, free, avail;
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "total", &total) || zxm_get_i64(l, l->result, "free", &free) ||
        zxm_get_i64(l, l->result, "avail", &avail) || total < 0 || free < 0 || avail < 0)
        return ZXM_E_PROTOCOL;
    p->total = total;
    p->free = free;
    p->avail = avail;
    return ZXM_OK;
}

/* ---- PM6: the index ---- */

static int32_t compose_index(const pinhole_t *p, char *out, uint32_t cap)
{
    uint32_t at = 0;
    out[0] = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        const pinhole_photo_t *f = &p->photo[i];
        const int64_t v[9] = {f->photo,  f->album, f->bytes, f->width, f->height,
                              f->placed, f->lat,   f->lon,   f->stamp};
        for (uint32_t k = 0; k < 9u; k++)
            if (!put_i64(out, cap, &at, v[k]) || !put(out, cap, &at, k < 8u ? " " : "\n"))
                return -1;
    }
    return (int32_t) at;
}

/* One index line into *f; false when it is not one (PH7 among the checks). */
static bool parse_line(const char *s, uint32_t n, uint32_t *at, pinhole_photo_t *f)
{
    int64_t v[9];
    for (uint32_t k = 0; k < 9u; k++)
        if (!get_num(s, n, at, k < 8u ? ' ' : '\n', &v[k])) return false;
    if (v[0] < 1 || v[1] < 0 || v[1] > PINHOLE_ALBUM_MAX || v[2] < 1 || v[3] < 1 || v[4] < 1 ||
        !pinhole_flag(v[5]) || v[6] < -90000000 || v[6] > 90000000 || v[7] < -180000000 ||
        v[7] > 180000000 || v[8] < 0 ||
        !pinhole_record_ok(v[0], v[1], v[2], v[3], v[4], v[5], v[8]))
        return false;
    memset(f, 0, sizeof *f);
    f->photo = v[0], f->album = v[1], f->bytes = v[2], f->width = v[3], f->height = v[4];
    f->placed = v[5], f->lat = (int32_t) v[6], f->lon = (int32_t) v[7], f->stamp = v[8];
    return path_of(f->album, f->photo, f->path, sizeof f->path);
}

int pinhole_load(pinhole_t *p)
{
    static char text[PINHOLE_PHOTOS * PINHOLE_LINE_MAX];
    static pinhole_photo_t next[PINHOLE_PHOTOS];
    zxm_link_t *l = p->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", PINHOLE_INDEX);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == PINHOLE_E_FILES && strstr(l->err, "no such")) {
        p->n = 0, p->last = 0, p->folder = false, p->loaded = true; /* nothing taken yet */
        return ZXM_OK;
    }
    if (rc) return rc;
    bool eof = false;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t len = w4_json_str(l->resp, &l->tok[t], text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    uint32_t at = 0, n = 0;
    int64_t last = 0;
    while (at < (uint32_t) len) {
        if (n >= PINHOLE_PHOTOS || !parse_line(text, (uint32_t) len, &at, &next[n]))
            return ZXM_E_PROTOCOL;
        if (next[n].photo <= last) return ZXM_E_PROTOCOL; /* in the order taken */
        last = next[n].photo;
        n++;
    }
    memcpy(p->photo, next, (size_t) n * sizeof next[0]);
    p->n = n;
    p->last = last;
    p->folder = true;
    p->loaded = true;
    return ZXM_OK;
}

/* ---- PM5: the place ---- */

/* place.ask under the owner's answer: 1 and *lat, *lon when a tag may be
 * added, 0 when not (p->notice says why), or a negative ZXM_ error. */
static int ask_place(pinhole_t *p, int32_t *lat, int32_t *lon)
{
    zxm_link_t *l = p->link;
    zxm_begin(l, "place.ask");
    zxm_p_str(l, "app", PINHOLE_APP);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "answer", p->answer, sizeof p->answer)) return ZXM_E_PROTOCOL;
    int64_t granted = !strcmp(p->answer, "allowed") ? 1 : 0;
    int64_t revoked = !strcmp(p->answer, "never") ? 1 : 0;
    if (!pinhole_place_ok(1, granted, revoked)) {
        say(p,
            revoked ? "Saved without a place: the owner said Pinhole may never know the location."
            : !strcmp(p->answer, "unknown")
                ? "Saved without a place: no location is set."
                : "Saved without a place: the owner has not answered yet (Waymark).",
            0, NULL);
        return 0;
    }
    int64_t a, o;
    if (zxm_get_i64(l, l->result, "lat", &a) || zxm_get_i64(l, l->result, "lon", &o) ||
        a < -90000000 || a > 90000000 || o < -180000000 || o > 180000000)
        return ZXM_E_PROTOCOL;
    *lat = (int32_t) a;
    *lon = (int32_t) o;
    return 1;
}

/* ---- PM4-PM6: saving ---- */

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

static int write_frame(pinhole_t *p, const char *path)
{
    static char enc[(PIECE / 3u) * 4u];
    zxm_link_t *l = p->link;
    uint32_t len = p->frame_len, at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < PIECE ? len - at : PIECE;
        bool last = at + n >= len;
        uint32_t k = b64_enc(p->frame + at, n, enc);
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

static int make_folder(pinhole_t *p)
{
    zxm_link_t *l = p->link;
    if (p->folder) return ZXM_OK;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", PINHOLE_DIR);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == PINHOLE_E_FILES && strstr(l->err, "already exists"))
        rc = ZXM_OK;
    if (!rc) p->folder = true;
    return rc;
}

static int write_index(pinhole_t *p)
{
    static char text[PINHOLE_PHOTOS * PINHOLE_LINE_MAX];
    zxm_link_t *l = p->link;
    int32_t len = compose_index(p, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", PINHOLE_INDEX);
    zxm_p_strn(l, "text", text, (uint32_t) len);
    return zxm_call(l);
}

int pinhole_save(pinhole_t *p, int64_t album, int64_t stamp, bool place)
{
    if (album < 0 || album > PINHOLE_ALBUM_MAX || stamp < 0 || stamp > PINHOLE_STAMP_MAX)
        return ZXM_E_ARG;
    if (!p->loaded || !p->frame_len) return ZXM_E_STATE;
    if (p->n >= PINHOLE_PHOTOS || p->last >= PINHOLE_PHOTO_MAX) return ZXM_E_TOOBIG;
    p->notice[0] = 0;
    /* PM4: room first */
    int rc = pinhole_room(p);
    if (rc) return rc;
    int64_t need = p->frame_len;
    if (!pinhole_photo_fits(need, p->avail, p->floor)) {
        int64_t short_by = need + p->floor - p->avail;
        say(p, "Not saved: the photo is ", short_by, " bytes more than the room left.");
        return ZXM_E_TOOBIG;
    }
    /* PM5: the place only with the owner's permit */
    pinhole_photo_t f = {p->last + 1, album, need, p->width, p->height, 0, 0, 0, stamp, {0}};
    if (place) {
        rc = ask_place(p, &f.lat, &f.lon);
        if (rc < 0) return rc;
        f.placed = rc;
    }
    if (!pinhole_record_ok(f.photo, f.album, f.bytes, f.width, f.height, f.placed, f.stamp) ||
        !path_of(album, f.photo, f.path, sizeof f.path))
        return ZXM_E_ARG;
    /* PM6: the photo, then its record */
    rc = make_folder(p);
    if (!rc) rc = write_frame(p, f.path);
    if (rc) return rc;
    p->photo[p->n++] = f;
    rc = write_index(p);
    if (rc) {
        p->n--; /* the index on disk does not hold it */
        return rc;
    }
    p->last = f.photo;
    p->frame_len = 0;
    return ZXM_OK;
}

int pinhole_album_bytes(const pinhole_t *p, int64_t album, int64_t *total)
{
    static int64_t albums[PINHOLE_PHOTOS], bytes[PINHOLE_PHOTOS];
    for (uint32_t i = 0; i < p->n; i++) {
        albums[i] = p->photo[i].album;
        bytes[i] = p->photo[i].bytes;
    }
    return pinhole_album_total(albums, bytes, p->n, album, total);
}

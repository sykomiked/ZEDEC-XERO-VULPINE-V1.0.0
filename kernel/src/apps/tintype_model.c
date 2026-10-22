/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tintype_model.c — Tintype's model (tintype_model.h). */
#include <string.h>

#include "sha256.h"
#include "tintype_model.h"

#define TINTYPE_E_FILES (-32014)      /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define PIECE           9000u         /* bytes in one write: a multiple of 3, whole base64 */
#define READ_B64        (66u * 1024u) /* base64 of one 48 KiB read */
#define NAME_MAX_       256u

static uint8_t FILEBUF[TINTYPE_SHOT_MAX]; /* a photo read back (TM7) */
static char CATBUF[TINTYPE_CAT_MAX];      /* the catalogue (TM8) */

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

static void say(tintype_t *t, const char *a, int64_t v, const char *b)
{
    uint32_t at = 0;
    t->notice[0] = 0;
    if (!put(t->notice, sizeof t->notice, &at, a)) return;
    if (b && (!put_i64(t->notice, sizeof t->notice, &at, v) ||
              !put(t->notice, sizeof t->notice, &at, b)))
        return;
}

static uint32_t be16(const uint8_t *p)
{
    return (uint32_t) p[0] << 8 | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

/* ---- base64 (RFC 4648, padded) ---- */

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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

static int32_t b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* The bytes decoded into out, or -1 (not base64, or no room). */
static int32_t b64_dec(const char *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t k = 0;
    if (n % 4u) return -1;
    for (uint32_t i = 0; i < n; i += 4) {
        int32_t v[4];
        uint32_t pad = 0;
        for (uint32_t j = 0; j < 4; j++) {
            if (i + 4 == n && j >= 2 && in[i + j] == '=') {
                if (j == 2 && in[i + 3] != '=') return -1;
                pad++;
                v[j] = 0;
                continue;
            }
            v[j] = b64_val(in[i + j]);
            if (v[j] < 0) return -1;
        }
        uint32_t w =
            (uint32_t) v[0] << 18 | (uint32_t) v[1] << 12 | (uint32_t) v[2] << 6 | (uint32_t) v[3];
        uint32_t got = 3u - pad;
        if (k + got > cap) return -1;
        out[k++] = (uint8_t) (w >> 16);
        if (got > 1) out[k++] = (uint8_t) (w >> 8);
        if (got > 2) out[k++] = (uint8_t) w;
    }
    return (int32_t) k;
}

/* ---- an image's header ---- */

/* Days since 1970-01-01 of a civil date (proleptic Gregorian). */
static int64_t days_from_civil(int64_t y, int64_t m, int64_t d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static bool digits_at(const uint8_t *p, uint32_t n, int64_t *v)
{
    int64_t x = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return false;
        x = x * 10 + (p[i] - '0');
    }
    *v = x;
    return true;
}

/* "YYYY:MM:DD HH:MM:SS" and its NUL at p: seconds since 1970 in *s */
static bool exif_time(const uint8_t *p, int64_t *s)
{
    int64_t y, mo, d, h, mi, se;
    if (p[4] != ':' || p[7] != ':' || p[10] != ' ' || p[13] != ':' || p[16] != ':' || p[19] != 0)
        return false;
    if (!digits_at(p, 4, &y) || !digits_at(p + 5, 2, &mo) || !digits_at(p + 8, 2, &d) ||
        !digits_at(p + 11, 2, &h) || !digits_at(p + 14, 2, &mi) || !digits_at(p + 17, 2, &se))
        return false;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59)
        return false;
    *s = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se;
    return true;
}

/* A JPEG's frame size (SOF) and, when its EXIF segment holds one, the first
 * date and time the camera wrote there. */
static bool jpeg_head(const uint8_t *p, uint32_t n, int64_t *w, int64_t *h, int64_t *stamp,
                      bool *tknown)
{
    bool sized = false;
    *tknown = false;
    if (n < 4u || p[0] != 0xFFu || p[1] != 0xD8u) return false;
    uint32_t i = 2;
    while (i + 4u <= n) {
        if (p[i] != 0xFFu) return false;
        uint32_t m = p[i + 1];
        if (m == 0xFFu) { /* fill */
            i++;
            continue;
        }
        if (m == 0xD9u || m == 0xDAu) break; /* EOI, or the scan: the header is over */
        if ((m >= 0xD0u && m <= 0xD7u) || m == 0x01u) {
            i += 2;
            continue;
        }
        uint32_t len = be16(p + i + 2);
        if (len < 2u || i + 2u + len > n) return false;
        const uint8_t *seg = p + i + 4;
        uint32_t sl = len - 2u;
        if (m == 0xE1u && sl >= 6u && !memcmp(seg, "Exif\0\0", 6) && !*tknown)
            for (uint32_t k = 6; k + 20u <= sl && !*tknown; k++)
                *tknown = exif_time(seg + k, stamp);
        if (m >= 0xC0u && m <= 0xCFu && m != 0xC4u && m != 0xC8u && m != 0xCCu) {
            if (sl < 5u) return false;
            *h = be16(seg + 1);
            *w = be16(seg + 3);
            sized = true;
        }
        i += 2u + len;
    }
    return sized && *w > 0 && *h > 0;
}

/* A PNG's size, from its IHDR. */
static bool png_head(const uint8_t *p, uint32_t n, int64_t *w, int64_t *h)
{
    static const uint8_t sig[8] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    if (n < 8u + 25u || memcmp(p, sig, sizeof sig) || be32(p + 8) != 13u ||
        memcmp(p + 12, "IHDR", 4))
        return false;
    *w = be32(p + 16);
    *h = be32(p + 20);
    return *w > 0 && *h > 0;
}

/* The SHA-256 of the bytes, in lower-case hex. */
static void content_id(const uint8_t *p, uint32_t n, char *hex)
{
    static const char D[] = "0123456789abcdef";
    uint8_t md[SHA256_DIGEST_LEN];
    sha256(p, n, md);
    for (uint32_t i = 0; i < SHA256_DIGEST_LEN; i++) {
        hex[2 * i] = D[md[i] >> 4];
        hex[2 * i + 1] = D[md[i] & 15u];
    }
    hex[2 * SHA256_DIGEST_LEN] = 0;
}

/* TR2 for bytes of a kind against a record: the gaps, and the joined state */
typedef struct {
    int64_t wgap, hgap, tgap, cidsame;
    bool tknown, decoded;
    lpres_state_t said;
} tintype_judged_t;

static tintype_judged_t judge(const uint8_t *p, uint32_t n, int64_t kind, int64_t width,
                              int64_t height, int64_t stamp, const char *cid)
{
    tintype_judged_t j;
    int64_t w = 0, h = 0, s = 0;
    bool tk = false;
    char hex[TINTYPE_CID_HEX + 1];
    j.decoded =
        kind == TINTYPE_KIND_PHOTO ? jpeg_head(p, n, &w, &h, &s, &tk) : png_head(p, n, &w, &h);
    /* a file that no longer decodes contradicts every size it recorded */
    j.wgap = j.decoded ? width - w : width;
    j.hgap = j.decoded ? height - h : height;
    j.tknown = j.decoded && tk;
    j.tgap = j.tknown ? stamp - s : 0;
    j.cidsame = 1;
    if (cid) {
        content_id(p, n, hex);
        j.cidsame = !strcmp(hex, cid) ? 1 : 0;
    }
    j.said = tintype_integrity(j.wgap, j.hgap, j.tgap, j.tknown, j.cidsame);
    return j;
}

/* ---- TM1 ---- */

void tintype_init(tintype_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->floor = TINTYPE_FLOOR;
    t->shot_said = LPRES_STATE_NEITHER;
}

tintype_album_t *tintype_album(tintype_t *t, int64_t album)
{
    for (uint32_t i = 0; i < t->nalbums; i++)
        if (t->album[i].album == album) return &t->album[i];
    return NULL;
}

int tintype_album_add(tintype_t *t, int64_t album, int64_t parent, int64_t declared)
{
    if (album < 1 || album > TINTYPE_ALBUM_MAX || parent < 0 || parent > TINTYPE_ALBUM_MAX ||
        parent == album || declared < 1 || declared > TINTYPE_BYTES_MAX ||
        !tintype_album_node_ok(parent, album, declared, 0, 0))
        return ZXM_E_ARG;
    if (tintype_album(t, album) || (parent && !tintype_album(t, parent)) ||
        t->nalbums >= TINTYPE_ALBUMS)
        return ZXM_E_STATE;
    tintype_album_t *a = &t->album[t->nalbums++];
    memset(a, 0, sizeof *a);
    a->album = album;
    a->parent = parent;
    a->declared = declared;
    t->dirty = true;
    return ZXM_OK;
}

/* ---- TM2, TM3 ---- */

static void hold(tintype_t *t, const uint8_t *p, uint32_t n, int64_t kind, int64_t w, int64_t h,
                 int64_t stamp, int64_t ev)
{
    tintype_judged_t j = judge(p, n, kind, w, h, stamp, NULL);
    memcpy(t->shot, p, n);
    t->shot_len = n;
    t->shot_kind = kind;
    t->shot_width = w;
    t->shot_height = h;
    t->shot_stamp = stamp;
    t->shot_ev = ev;
    t->shot_tknown = j.tknown;
    t->shot_said = j.said;
    if (j.said == LPRES_STATE_BOTH)
        say(t, "Possibly altered: the picture does not decode to what was reported.", 0, NULL);
    else
        t->notice[0] = 0;
}

int tintype_take_photo(tintype_t *t, int64_t sensor, int64_t mode, const uint8_t *jpeg, uint32_t n,
                       int64_t width, int64_t height, int64_t aperture, int64_t shutter,
                       int64_t iso, int64_t stamp)
{
    int64_t w = 0, h = 0, s = 0;
    bool tk = false;
    if (!jpeg || n == 0u || width < 1 || width > TINTYPE_SIDE_MAX || height < 1 ||
        height > TINTYPE_SIDE_MAX || stamp < 0 || stamp > TINTYPE_STAMP_MAX)
        return ZXM_E_ARG;
    if (!pinhole_camera_ready(sensor, mode, n <= TINTYPE_SHOT_MAX ? 1 : 0)) {
        say(t, "The camera is not ready.", 0, NULL);
        return ZXM_E_STATE;
    }
    int64_t ev = tintype_ev_thirds(aperture, shutter, iso);
    if (!pinhole_exposure_ok(iso, shutter, 0) || ev == TINTYPE_EV_REFUSED) return ZXM_E_ARG;
    if (!jpeg_head(jpeg, n, &w, &h, &s, &tk)) return ZXM_E_ARG; /* no JPEG we can read */
    hold(t, jpeg, n, TINTYPE_KIND_PHOTO, width, height, stamp, ev);
    return ZXM_OK;
}

int tintype_take_screen(tintype_t *t, int64_t screen_w, int64_t screen_h, int64_t x, int64_t y,
                        int64_t w, int64_t h, const uint8_t *png, uint32_t n, int64_t stamp)
{
    int64_t pw = 0, ph = 0;
    if (!png || n == 0u || n > TINTYPE_SHOT_MAX || w < 0 || w > GLIMPSE_POS_MAX || h < 0 ||
        h > GLIMPSE_POS_MAX || stamp < 0 || stamp > TINTYPE_STAMP_MAX)
        return ZXM_E_ARG;
    int64_t cx = glimpse_capture_origin(x, screen_w), cy = glimpse_capture_origin(y, screen_h);
    if (cx < 0 || cy < 0) return ZXM_E_ARG; /* a screen or a corner outside its range */
    int64_t cw = glimpse_capture_extent(x, w, screen_w),
            ch = glimpse_capture_extent(y, h, screen_h);
    if (cw <= 0 || ch <= 0) {
        say(t, "Nothing of that region is on the screen.", 0, NULL);
        return ZXM_E_ARG;
    }
    if (!png_head(png, n, &pw, &ph)) return ZXM_E_ARG;
    hold(t, png, n, TINTYPE_KIND_SCREEN, cw, ch, stamp, TINTYPE_EV_REFUSED);
    return ZXM_OK;
}

/* ---- TM4 ---- */

int tintype_develop(const uint8_t *in, uint32_t n, const int64_t *curve, int64_t delta,
                    int64_t gain, uint8_t *out)
{
    if (!in || !out || !curve || gesso_bright_px(0, delta) == GESSO_REFUSED ||
        gesso_contrast_px(0, gain) == GESSO_REFUSED ||
        tintype_curve_px(0, curve) == TINTYPE_REFUSED)
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        int64_t v = tintype_curve_px(in[i], curve);
        v = gesso_bright_px(v, delta);
        v = gesso_contrast_px(v, gain);
        out[i] = (uint8_t) v; /* each step holds 0..255 */
    }
    return ZXM_OK;
}

int tintype_histogram(const uint8_t *px, uint32_t n, int64_t width, int64_t *bins, uint32_t *nbins)
{
    static int64_t row[1024];
    if (!px || !bins || !nbins || width < 1 || width > (int64_t) TINTYPE_BINS_MAX) return ZXM_E_ARG;
    uint32_t nb = (uint32_t) ((TINTYPE_BINS_MAX + (uint64_t) width - 1u) / (uint64_t) width);
    for (uint32_t b = 0; b < nb; b++) bins[b] = 0;
    for (uint32_t at = 0; at < n; at += 1024u) {
        uint32_t k = n - at < 1024u ? n - at : 1024u;
        for (uint32_t i = 0; i < k; i++) row[i] = px[at + i];
        for (uint32_t b = 0; b < nb; b++)
            bins[b] +=
                gesso_range_count(row, k, (int64_t) b * width, (int64_t) b * width + width - 1);
    }
    *nbins = nb;
    return ZXM_OK;
}

/* ---- TM5 ---- */

int tintype_set_floor(tintype_t *t, int64_t floor)
{
    if (floor < 0 || floor > LIFEBOAT_BYTES_MAX) return ZXM_E_ARG;
    t->floor = floor;
    return ZXM_OK;
}

int tintype_room(tintype_t *t)
{
    zxm_link_t *l = t->link;
    int64_t total, free, avail;
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "total", &total) || zxm_get_i64(l, l->result, "free", &free) ||
        zxm_get_i64(l, l->result, "avail", &avail) || total < 0 || free < 0 || avail < 0)
        return ZXM_E_PROTOCOL;
    t->total = total;
    t->free = free;
    t->avail = avail > LIFEBOAT_BYTES_MAX ? LIFEBOAT_BYTES_MAX : avail;
    return ZXM_OK;
}

int tintype_recording_fits(tintype_t *t, int64_t w, int64_t h, int64_t bpp, int64_t secs,
                           int64_t fps, int64_t *bytes)
{
    if (!bytes) return ZXM_E_ARG;
    int64_t need = glimpse_recording_bytes(w, h, bpp, secs, fps, GLIMPSE_ROOM_MAX);
    if (need < 0) return ZXM_E_ARG;
    int rc = tintype_room(t);
    if (rc) return rc;
    int64_t room = t->avail - t->floor;
    if (room < 0 || glimpse_recording_bytes(w, h, bpp, secs, fps, room) < 0) {
        say(t, "The recording is ", need - (room < 0 ? 0 : room),
            " bytes more than the room left.");
        return ZXM_E_TOOBIG;
    }
    *bytes = need;
    return ZXM_OK;
}

/* ---- TM6 ---- */

static const char *ext_of(int64_t kind)
{
    return kind == TINTYPE_KIND_SCREEN ? ".png" : ".jpg";
}

static bool album_dir(int64_t album, char *out, uint32_t cap)
{
    uint32_t at = 0;
    return put(out, cap, &at, TINTYPE_DIR "/Album ") && put_i64(out, cap, &at, album);
}

bool tintype_path(const tintype_photo_t *p, char *out, uint32_t cap)
{
    uint32_t at;
    if (!p || !album_dir(p->album, out, cap)) return false;
    at = (uint32_t) strlen(out);
    return put(out, cap, &at, "/Photo ") && put_i64(out, cap, &at, p->num) &&
           put(out, cap, &at, ext_of(p->kind));
}

tintype_photo_t *tintype_find(tintype_t *t, int64_t id)
{
    for (uint32_t i = 0; i < t->nphotos; i++)
        if (t->photo[i].id == id) return &t->photo[i];
    return NULL;
}

/* "Photo N.jpg" or "Photo N.png", N 1..TINTYPE_NUM_MAX written plainly: N, else 0 */
static int64_t photo_number(const char *name)
{
    size_t n = strlen(name);
    if (n < 11u || n > 15u || memcmp(name, "Photo ", 6) || name[6] == '0' ||
        (strcmp(name + n - 4u, ".jpg") && strcmp(name + n - 4u, ".png")))
        return 0;
    int64_t v = 0;
    for (size_t i = 6; i < n - 4u; i++) {
        if (name[i] < '0' || name[i] > '9') return 0;
        v = v * 10 + (name[i] - '0');
    }
    return v;
}

/* The first number free in the album's folder (glimpse.first_free_n). */
static int next_number(tintype_t *t, const char *dir, int64_t *n)
{
    static int64_t got[GLIMPSE_USED_MAX];
    static char name[NAME_MAX_];
    zxm_link_t *l = t->link;
    uint32_t k = 0;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", dir);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == TINTYPE_E_FILES && strstr(l->err, "no such")) {
        rc = ZXM_OK; /* nothing kept there yet */
    } else {
        if (rc) return rc;
        int32_t arr = zxm_get(l, l->result, "entries");
        if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
        uint32_t ne = zxm_len(l, arr);
        for (uint32_t i = 0; i < ne; i++) {
            int32_t e = zxm_at(l, arr, i);
            bool isdir = false;
            if (e < 0 || zxm_get_bool(l, e, "dir", &isdir) ||
                zxm_get_str(l, e, "name", name, sizeof name))
                return ZXM_E_PROTOCOL;
            int64_t v = isdir ? 0 : photo_number(name);
            if (!v) continue;
            if (k >= GLIMPSE_USED_MAX) return ZXM_E_TOOBIG;
            got[k++] = v;
        }
    }
    if (k == 0) got[k++] = 0; /* a 0 holds no place: an empty album */
    int64_t free_n = glimpse_first_free(got, k);
    if (free_n < 1 || free_n > TINTYPE_NUM_MAX) return ZXM_E_TOOBIG;
    *n = free_n;
    return ZXM_OK;
}

static int make_dir(tintype_t *t, const char *path)
{
    zxm_link_t *l = t->link;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == TINTYPE_E_FILES && strstr(l->err, "already exists"))
        rc = ZXM_OK;
    return rc;
}

/* Write n bytes whole: a new file (exclusive), or replacing one. */
static int write_file(tintype_t *t, const char *path, const uint8_t *p, uint32_t len, bool fresh)
{
    static char enc[(PIECE / 3u) * 4u];
    zxm_link_t *l = t->link;
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < PIECE ? len - at : PIECE;
        bool last = at + n >= len;
        uint32_t k = b64_enc(p + at, n, enc);
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "base64", enc, k);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        if (first && fresh) zxm_p_bool(l, "exclusive", true);
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
        zxm_progress(l, at, len);
    } while (at < len);
    return ZXM_OK;
}

/* files.stat's size of a file */
static int stat_size(tintype_t *t, const char *path, int64_t *size)
{
    zxm_link_t *l = t->link;
    bool isdir = true;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "size", size) || zxm_get_bool(l, l->result, "dir", &isdir) ||
        isdir || *size < 0)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* A file read whole into buf (cap bytes); its length in *len. */
static int read_file(tintype_t *t, const char *path, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    static char chunk[READ_B64];
    zxm_link_t *l = t->link;
    uint64_t off = 0;
    *len = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        zxm_p_str(l, "encoding", "base64");
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t size, next;
        bool eof;
        int32_t tk = zxm_get(l, l->result, "base64");
        if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_bool(l, l->result, "eof", &eof) || tk < 0)
            return ZXM_E_PROTOCOL;
        if (size > cap) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[tk], chunk, sizeof chunk);
        if (n < 0) return ZXM_E_TOOBIG;
        int32_t got = b64_dec(chunk, (uint32_t) n, buf + *len, cap - *len);
        if (got < 0) return ZXM_E_PROTOCOL;
        *len += (uint32_t) got;
        zxm_progress(l, *len, size);
        if (eof) break;
        if (next <= off) return ZXM_E_PROTOCOL;
        off = next;
    }
    return ZXM_OK;
}

int tintype_keep(tintype_t *t, int64_t album, int64_t owner, int64_t licence, int64_t release,
                 bool owner_confirmed, int64_t *id)
{
    char dir[TINTYPE_PATH_MAX], path[TINTYPE_PATH_MAX];
    /* check: the rights row (TR3, for the id it will take), the album, the
     * capture, the album's limit */
    if (!tintype_rights_ok(1, owner, licence, release)) return ZXM_E_ARG;
    tintype_album_t *a = tintype_album(t, album);
    if (!a || !t->shot_len || t->nphotos >= TINTYPE_PHOTOS || t->last >= TINTYPE_ID_MAX)
        return ZXM_E_STATE;
    int64_t bytes = t->shot_len;
    int64_t room = tintype_album_room(a->declared, a->used, bytes);
    if (!tintype_storage_ok(room, a->declared) ||
        !tintype_album_node_ok(a->parent, a->album, a->declared, a->used + bytes, a->photos + 1)) {
        say(t, "Not kept: the album would be ", room < 0 ? -room : 0,
            " bytes past what was declared for it.");
        return ZXM_E_TOOBIG;
    }
    if (!owner_confirmed) {
        say(t, "Not kept: the owner has not confirmed it.", 0, NULL);
        return ZXM_E_STATE;
    }
    /* reserve: room on the disk ("photo fits"), then the album's next number */
    int rc = tintype_room(t);
    if (rc) return rc;
    if (!lifeboat_restore_fits(bytes, t->avail, t->floor)) {
        say(t, "Not kept: the photo is ", bytes + t->floor - t->avail,
            " bytes more than the room left.");
        return ZXM_E_TOOBIG;
    }
    int64_t num = 0;
    if (!album_dir(album, dir, sizeof dir)) return ZXM_E_TOOBIG;
    rc = next_number(t, dir, &num);
    if (rc) return rc;
    /* compute: the record, the catalogue item and the content id */
    tintype_photo_t p;
    memset(&p, 0, sizeof p);
    p.id = t->last + 1;
    p.num = num;
    p.album = album;
    p.kind = t->shot_kind;
    p.bytes = bytes;
    p.width = t->shot_width;
    p.height = t->shot_height;
    p.stamp = t->shot_stamp;
    p.tknown = t->shot_tknown;
    p.ev = t->shot_ev;
    p.owner = owner;
    p.licence = licence;
    p.release = release;
    p.said = t->shot_said;
    if (!pinhole_record_ok(p.id, album, bytes, p.width, p.height, 0, p.stamp * 1000) ||
        !tintype_item_fits(album, num, p.kind, bytes) || !tintype_path(&p, path, sizeof path))
        return ZXM_E_ARG;
    content_id(t->shot, t->shot_len, p.cid);
    /* write, a new file, and its size confirmed */
    rc = make_dir(t, TINTYPE_DIR);
    if (!rc) rc = make_dir(t, dir);
    if (!rc) rc = write_file(t, path, t->shot, t->shot_len, true);
    int64_t size = 0;
    if (!rc) rc = stat_size(t, path, &size);
    if (rc) return rc;
    if (size != bytes) return ZXM_E_PROTOCOL;
    /* mutate and commit */
    t->photo[t->nphotos++] = p;
    t->last = p.id;
    a->used += bytes;
    a->photos++;
    t->shot_len = 0;
    t->dirty = true;
    if (p.said == LPRES_STATE_BOTH)
        say(t, "Kept, flagged possibly altered: photo ", p.id, ".");
    else
        t->notice[0] = 0;
    if (id) *id = p.id;
    return ZXM_OK;
}

/* ---- TM7 ---- */

int tintype_verify(tintype_t *t, int64_t id)
{
    char path[TINTYPE_PATH_MAX];
    uint32_t len = 0;
    int64_t size = 0;
    tintype_photo_t *p = tintype_find(t, id);
    if (!p) return ZXM_E_STATE;
    if (!tintype_path(p, path, sizeof path)) return ZXM_E_TOOBIG;
    int rc = stat_size(t, path, &size);
    if (!rc) rc = read_file(t, path, FILEBUF, sizeof FILEBUF, &len);
    if (rc) return rc;
    if ((int64_t) len != size) return ZXM_E_PROTOCOL;
    tintype_judged_t j = judge(FILEBUF, len, p->kind, p->width, p->height, p->stamp, p->cid);
    /* the time is judged only where the record had one to judge */
    if (!p->tknown) j.said = tintype_integrity(j.wgap, j.hgap, 0, false, j.cidsame);
    p->said = j.said;
    if (j.said == LPRES_STATE_BOTH)
        say(t, "Possibly altered: photo ", id, " does not decode to its record.");
    else
        t->notice[0] = 0;
    return ZXM_OK;
}

/* ---- TM8 ---- */

static bool put_row(char *out, uint32_t cap, uint32_t *at, const int64_t *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!put(out, cap, at, " ") || !put_i64(out, cap, at, v[i])) return false;
    return true;
}

int tintype_save(tintype_t *t, bool owner_confirmed)
{
    uint32_t at = 0;
    bool ok = put(CATBUF, sizeof CATBUF, &at, "tintype 1\n");
    for (uint32_t i = 0; ok && i < t->nalbums; i++) {
        const tintype_album_t *a = &t->album[i];
        int64_t v[5] = {a->album, a->parent, a->declared, a->used, a->photos};
        ok = put(CATBUF, sizeof CATBUF, &at, "A") && put_row(CATBUF, sizeof CATBUF, &at, v, 5) &&
             put(CATBUF, sizeof CATBUF, &at, "\n");
    }
    for (uint32_t i = 0; ok && i < t->nphotos; i++) {
        const tintype_photo_t *p = &t->photo[i];
        int64_t v[14] = {p->id,    p->num,     p->album,   p->kind,           p->bytes,
                         p->width, p->height,  p->stamp,   p->tknown ? 1 : 0, p->ev,
                         p->owner, p->licence, p->release, (int64_t) p->said};
        ok = put(CATBUF, sizeof CATBUF, &at, "P") && put_row(CATBUF, sizeof CATBUF, &at, v, 14) &&
             put(CATBUF, sizeof CATBUF, &at, " ") && put(CATBUF, sizeof CATBUF, &at, p->cid) &&
             put(CATBUF, sizeof CATBUF, &at, "\n");
    }
    if (!ok) return ZXM_E_TOOBIG;
    if (!owner_confirmed) {
        say(t, "Not saved: the owner has not confirmed it.", 0, NULL);
        return ZXM_E_STATE;
    }
    int rc = make_dir(t, TINTYPE_DIR);
    if (!rc) rc = write_file(t, TINTYPE_CATALOGUE, (const uint8_t *) CATBUF, at, false);
    if (rc) return rc;
    t->dirty = false;
    t->notice[0] = 0;
    return ZXM_OK;
}

/* One integer of a line at *at, past one space; false at the end or on junk. */
static bool get_i64(const char *s, uint32_t *at, int64_t *v)
{
    uint32_t i = *at;
    bool neg = false;
    int64_t x = 0;
    if (s[i] != ' ') return false;
    i++;
    if (s[i] == '-') neg = true, i++;
    if (s[i] < '0' || s[i] > '9') return false;
    for (uint32_t k = 0; s[i] >= '0' && s[i] <= '9'; i++, k++) {
        if (k >= 18u) return false;
        x = x * 10 + (s[i] - '0');
    }
    *v = neg ? -x : x;
    *at = i;
    return true;
}

static bool get_row(const char *s, uint32_t *at, int64_t *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!get_i64(s, at, &v[i])) return false;
    return true;
}

static bool hex_cid(const char *s, uint32_t *at, char *cid)
{
    uint32_t i = *at;
    if (s[i] != ' ') return false;
    i++;
    for (uint32_t k = 0; k < TINTYPE_CID_HEX; k++) {
        char c = s[i + k];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        cid[k] = c;
    }
    cid[TINTYPE_CID_HEX] = 0;
    *at = i + TINTYPE_CID_HEX;
    return true;
}

int tintype_load(tintype_t *t)
{
    static tintype_album_t al[TINTYPE_ALBUMS];
    static tintype_photo_t ph[TINTYPE_PHOTOS];
    uint32_t len = 0, na = 0, np = 0;
    int64_t last = 0;
    int rc = read_file(t, TINTYPE_CATALOGUE, (uint8_t *) CATBUF, sizeof CATBUF - 1u, &len);
    if (rc == ZXM_E_REMOTE && t->link->err_code == TINTYPE_E_FILES &&
        strstr(t->link->err, "no such")) {
        len = 0; /* nothing saved yet: an empty catalogue */
    } else if (rc) {
        return rc;
    } else if (len < 10u || memcmp(CATBUF, "tintype 1\n", 10)) {
        return ZXM_E_PROTOCOL;
    }
    CATBUF[len] = 0;
    for (uint32_t at = len ? 10u : 0u; at < len;) {
        int64_t v[14];
        char c = CATBUF[at++];
        if (c == 'A') {
            if (na >= TINTYPE_ALBUMS || !get_row(CATBUF, &at, v, 5)) return ZXM_E_PROTOCOL;
            tintype_album_t *a = &al[na];
            a->album = v[0], a->parent = v[1], a->declared = v[2], a->used = v[3], a->photos = v[4];
            bool parent_ok = !a->parent;
            for (uint32_t k = 0; k < na; k++) {
                parent_ok = parent_ok || al[k].album == a->parent;
                if (al[k].album == a->album) return ZXM_E_PROTOCOL;
            }
            if (a->album < 1 || a->album > TINTYPE_ALBUM_MAX || a->parent == a->album ||
                !parent_ok ||
                !tintype_album_node_ok(a->parent, a->album, a->declared, a->used, a->photos) ||
                !tintype_storage_ok(a->declared - a->used, a->declared))
                return ZXM_E_PROTOCOL;
            na++;
        } else if (c == 'P') {
            if (np >= TINTYPE_PHOTOS || !get_row(CATBUF, &at, v, 14)) return ZXM_E_PROTOCOL;
            tintype_photo_t *p = &ph[np];
            memset(p, 0, sizeof *p);
            p->id = v[0], p->num = v[1], p->album = v[2], p->kind = v[3], p->bytes = v[4];
            p->width = v[5], p->height = v[6], p->stamp = v[7], p->tknown = v[8] == 1;
            p->ev = v[9], p->owner = v[10], p->licence = v[11], p->release = v[12];
            if (!hex_cid(CATBUF, &at, p->cid) || (v[8] != 0 && v[8] != 1) ||
                (v[13] != LPRES_STATE_TRUE && v[13] != LPRES_STATE_BOTH) ||
                (p->kind != TINTYPE_KIND_PHOTO && p->kind != TINTYPE_KIND_SCREEN) || p->num < 1 ||
                p->num > TINTYPE_NUM_MAX || p->id <= last || p->stamp < 0 ||
                p->stamp > TINTYPE_STAMP_MAX ||
                !tintype_rights_ok(p->id, p->owner, p->licence, p->release) ||
                !pinhole_record_ok(p->id, p->album, p->bytes, p->width, p->height, 0,
                                   p->stamp * 1000) ||
                !tintype_item_fits(p->album, p->num, p->kind, p->bytes))
                return ZXM_E_PROTOCOL;
            p->said = (lpres_state_t) v[13];
            last = p->id;
            np++;
        } else {
            return ZXM_E_PROTOCOL;
        }
        if (CATBUF[at] != '\n') return ZXM_E_PROTOCOL;
        at++;
    }
    for (uint32_t i = 0; i < np; i++) { /* every photo in an album filed */
        bool found = false;
        for (uint32_t k = 0; k < na && !found; k++) found = al[k].album == ph[i].album;
        if (!found) return ZXM_E_PROTOCOL;
    }
    memcpy(t->album, al, na * sizeof al[0]);
    t->nalbums = na;
    memcpy(t->photo, ph, np * sizeof ph[0]);
    t->nphotos = np;
    t->last = last;
    t->dirty = false;
    return ZXM_OK;
}

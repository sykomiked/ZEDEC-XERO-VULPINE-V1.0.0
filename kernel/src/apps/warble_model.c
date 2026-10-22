/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* warble_model.c — Warble's model (warble_model.h). */
#include <string.h>

#include "warble_model.h"

#define WARBLE_E_FILES (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define WARBLE_ASK     100u     /* hits asked of search.query */
#define WARBLE_WAV_HDR 44u      /* WM2: the canonical header */

static const char *const AUDIO[] = {".wav", ".flac", ".mp3", ".ogg", ".opus", ".m4a", ".aac"};

/* ---- small helpers, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static void say(warble_t *w, const char *a, const char *b)
{
    uint32_t na = len_of(a, WARBLE_NOTICE_MAX), nb = len_of(b, WARBLE_NOTICE_MAX - na);
    memcpy(w->notice, a, na);
    memcpy(w->notice + na, b, nb);
    w->notice[na + nb] = 0;
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

/* WM1: a name ending in a known audio extension, any case */
static bool audio_name(const char *name, uint32_t n)
{
    for (uint32_t e = 0; e < sizeof AUDIO / sizeof AUDIO[0]; e++) {
        uint32_t k = len_of(AUDIO[e], 8u), i = 0;
        if (n <= k) continue;
        while (i < k && lower(name[n - k + i]) == AUDIO[e][i]) i++;
        if (i == k) return true;
    }
    return false;
}

static bool wav_name(const char *name, uint32_t n)
{
    return n > 4u && lower(name[n - 4u]) == '.' && lower(name[n - 3u]) == 'w' &&
           lower(name[n - 2u]) == 'a' && lower(name[n - 1u]) == 'v';
}

static bool path_of(const char *name, char *out, uint32_t cap)
{
    uint32_t n = len_of(name, WARBLE_NAME_MAX + 1u), d = (uint32_t) sizeof WARBLE_DIR - 1u;
    if (n == 0 || n > WARBLE_NAME_MAX || d + 1u + n + 1u > cap) return false;
    memcpy(out, WARBLE_DIR, d);
    out[d] = '/';
    memcpy(out + d + 1u, name, n + 1u);
    return true;
}

static bool no_such(const zxm_link_t *l, int rc)
{
    return rc == ZXM_E_REMOTE && l->err_code == WARBLE_E_FILES && strstr(l->err, "no such");
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

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* ---- WM1, WM2: the library ---- */

/* WM2: a WAV track's length in ms from its header, 0 when not known */
static int wav_ms(warble_t *w, const char *name, int64_t *ms)
{
    zxm_link_t *l = w->link;
    char path[WARBLE_PATH_MAX + 1u], enc[64];
    uint8_t h[WARBLE_WAV_HDR];
    *ms = 0;
    if (!wav_name(name, len_of(name, WARBLE_NAME_MAX + 1u))) return ZXM_OK;
    if (!path_of(name, path, sizeof path)) return ZXM_E_ARG;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", WARBLE_WAV_HDR);
    zxm_p_str(l, "encoding", "base64");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t t = zxm_get(l, l->result, "base64");
    if (t < 0) return ZXM_E_PROTOCOL;
    int32_t n = w4_json_str(l->resp, &l->tok[t], enc, sizeof enc);
    if (n < 0) return ZXM_OK; /* more than a header: not what was asked, not known */
    int32_t got = b64_dec(enc, (uint32_t) n, h, sizeof h);
    if (got != (int32_t) WARBLE_WAV_HDR || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4) ||
        memcmp(h + 12, "fmt ", 4) || memcmp(h + 36, "data", 4))
        return ZXM_OK;
    uint32_t rate = le32(h + 28), size = le32(h + 40);
    if (rate == 0) return ZXM_OK;
    int64_t v = (int64_t) size * 1000 / (int64_t) rate;
    if (v <= WARBLE_TRACK_MS_MAX) *ms = v;
    return ZXM_OK;
}

void warble_init(warble_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
    w->volume = WARBLE_VOL_START;
    w->cap = WARBLE_CAP_START;
}

int warble_load(warble_t *w)
{
    static warble_track_t got[WARBLE_TRACKS];
    zxm_link_t *l = w->link;
    uint32_t n = 0, skipped = 0;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", WARBLE_DIR);
    int rc = zxm_call(l);
    if (no_such(l, rc)) {
        w->n = w->nlist = w->nhits = w->skipped = 0;
        w->shuffled = false;
        w->loaded = true;
        return ZXM_OK;
    }
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t count = zxm_len(l, arr);
    for (uint32_t i = 0; i < count; i++) {
        char name[WARBLE_NAME_MAX + 2u];
        bool dir = false;
        uint64_t size = 0;
        int64_t mtime = 0;
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_bool(l, o, "dir", &dir) || zxm_get_u64(l, o, "size", &size) ||
            zxm_get_i64(l, o, "mtime", &mtime))
            return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "name", name, sizeof name)) {
            skipped++;
            continue;
        }
        uint32_t nn = len_of(name, sizeof name);
        if (dir || nn > WARBLE_NAME_MAX || !audio_name(name, nn) || size > (uint64_t) INT64_MAX ||
            n >= WARBLE_TRACKS || !warble_track_record_ok((int64_t) n, 0, mtime)) {
            skipped++;
            continue;
        }
        memcpy(got[n].name, name, nn + 1u);
        got[n].bytes = (int64_t) size;
        got[n].mtime = mtime;
        got[n].ms = 0;
        n++;
    }
    /* the listing's names first, then the lengths: each read reuses the link */
    for (uint32_t i = 0; i < n; i++) {
        rc = wav_ms(w, got[i].name, &got[i].ms);
        if (rc) return rc;
        if (!warble_track_record_ok((int64_t) i, got[i].ms, got[i].mtime)) got[i].ms = 0;
        zxm_progress(l, i + 1u, n);
    }
    memcpy(w->track, got, n * sizeof got[0]);
    w->n = n;
    w->skipped = skipped;
    w->nlist = w->nhits = 0;
    w->shuffled = false;
    w->place = 0;
    w->loaded = true;
    return ZXM_OK;
}

int warble_restat(warble_t *w, uint32_t track)
{
    zxm_link_t *l = w->link;
    char path[WARBLE_PATH_MAX + 1u];
    if (!w->loaded) return ZXM_E_STATE;
    if (track >= w->n || !path_of(w->track[track].name, path, sizeof path)) return ZXM_E_ARG;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool dir = false;
    uint64_t size = 0;
    int64_t mtime = 0, ms = 0;
    if (zxm_get_bool(l, l->result, "dir", &dir) || zxm_get_u64(l, l->result, "size", &size) ||
        zxm_get_i64(l, l->result, "mtime", &mtime))
        return ZXM_E_PROTOCOL;
    if (dir || size > (uint64_t) INT64_MAX || !warble_track_record_ok((int64_t) track, 0, mtime))
        return ZXM_E_PROTOCOL;
    rc = wav_ms(w, w->track[track].name, &ms);
    if (rc) return rc;
    if (!warble_track_record_ok((int64_t) track, ms, mtime)) ms = 0;
    w->track[track].bytes = (int64_t) size;
    w->track[track].mtime = mtime;
    w->track[track].ms = ms;
    return ZXM_OK;
}

int32_t warble_find(const warble_t *w, const char *name)
{
    if (!name) return -1;
    for (uint32_t i = 0; i < w->n; i++)
        if (!strcmp(w->track[i].name, name)) return (int32_t) i;
    return -1;
}

/* ---- WM3: finding ---- */

int warble_search(warble_t *w, const char *query)
{
    static uint32_t got[WARBLE_HITS];
    uint32_t n = len_of(query, 257u), kept = 0, d = (uint32_t) sizeof WARBLE_DIR - 1u;
    bool word = false;
    if (!query || n == 0 || n > 256u) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        if ((unsigned char) query[i] < 0x20u) return ZXM_E_ARG;
        word = word || query[i] != ' ';
    }
    if (!word) return ZXM_E_ARG;
    if (!w->loaded) return ZXM_E_STATE;
    zxm_link_t *l = w->link;
    zxm_begin(l, "search.query");
    zxm_p_str(l, "q", query);
    zxm_p_u64(l, "max", WARBLE_ASK);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "hits");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > WARBLE_ASK)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, arr) && kept < WARBLE_HITS; i++) {
        char path[WARBLE_PATH_MAX + 1u];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "path", path, sizeof path)) continue; /* longer than any track's */
        if (strncmp(path, WARBLE_DIR "/", d + 1u)) continue;
        int32_t at = warble_find(w, path + d + 1u);
        if (at >= 0) got[kept++] = (uint32_t) at;
    }
    memcpy(w->hit, got, kept * sizeof got[0]);
    w->nhits = kept;
    return ZXM_OK;
}

/* ---- WM4: the playlist ---- */

int warble_queue(warble_t *w, uint32_t track)
{
    if (!w->loaded) return ZXM_E_STATE;
    if (track >= w->n) return ZXM_E_ARG;
    if (w->nlist >= WARBLE_PLAYLIST_MAX) {
        say(w, "The playlist is full", "");
        return ZXM_E_STATE;
    }
    w->list[w->nlist++] = track;
    w->shuffled = false;
    return ZXM_OK;
}

int warble_unqueue(warble_t *w, uint32_t place)
{
    if (place >= w->nlist) return ZXM_E_ARG;
    memmove(w->list + place, w->list + place + 1u, (w->nlist - place - 1u) * sizeof w->list[0]);
    w->nlist--;
    w->shuffled = false;
    if (w->place >= w->nlist) w->place = 0;
    return ZXM_OK;
}

int warble_length(const warble_t *w, int64_t *ms, uint32_t *unknown)
{
    int64_t len[WARBLE_PLAYLIST_MAX];
    uint32_t none = 0;
    if (!ms || !unknown) return ZXM_E_ARG;
    for (uint32_t i = 0; i < w->nlist; i++) {
        len[i] = w->track[w->list[i]].ms;
        if (len[i] == 0) none++;
    }
    int64_t total = warble_playlist_ms(len, w->nlist);
    if (total < 0) return ZXM_E_STATE;
    *ms = total;
    *unknown = none;
    return ZXM_OK;
}

int warble_length_text(const warble_t *w, char *out, uint32_t cap)
{
    int64_t ms = 0;
    uint32_t unknown = 0, at = 0;
    char d[24];
    if (!out || cap == 0) return ZXM_E_ARG;
    int rc = warble_length(w, &ms, &unknown);
    if (rc) return rc;
    int64_t hms = warble_length_hms(ms);
    if (hms < 0) return ZXM_E_STATE;
    int64_t h = hms / 10000, m = hms / 100 % 100, s = hms % 100;
    uint32_t k = sizeof d;
    d[--k] = 0;
    d[--k] = (char) ('0' + s % 10);
    d[--k] = (char) ('0' + s / 10);
    d[--k] = ':';
    d[--k] = (char) ('0' + m % 10);
    d[--k] = (char) ('0' + m / 10);
    d[--k] = ':';
    do {
        d[--k] = (char) ('0' + h % 10);
        h /= 10;
    } while (h);
    at = (uint32_t) sizeof d - k;
    if (at > cap) return ZXM_E_TOOBIG;
    memcpy(out, d + k, at);
    return ZXM_OK;
}

/* ---- WM5: the shuffle ---- */

int32_t warble_at(const warble_t *w, uint32_t place)
{
    if (place >= w->nlist) return -1;
    if (!w->shuffled) return (int32_t) w->list[place];
    int64_t i = warble_shuffle_at(w->seed, (int64_t) w->nlist, (int64_t) place);
    return i < 0 ? -1 : (int32_t) w->list[i];
}

int warble_shuffle(warble_t *w, int64_t seed)
{
    if (seed < 0 || seed > WARBLE_SEED_MAX) return ZXM_E_ARG;
    if (w->nlist == 0) return ZXM_E_STATE;
    w->seed = seed;
    w->shuffled = true;
    w->place = 0;
    return ZXM_OK;
}

void warble_unshuffle(warble_t *w)
{
    w->shuffled = false;
    w->place = 0;
}

int32_t warble_next(warble_t *w)
{
    if (w->place + 1u >= w->nlist) return -1;
    w->place++;
    return warble_at(w, w->place);
}

/* ---- WM6: the volume ---- */

int warble_set_cap(warble_t *w, int64_t cap, bool owner_confirmed)
{
    if (!warble_volume_cap_ok(0, cap)) return ZXM_E_ARG;
    if (!owner_confirmed) {
        say(w, "Only the owner's confirmed word moves the volume cap", "");
        return ZXM_E_STATE;
    }
    w->cap = cap;
    if (w->volume > cap) {
        w->volume = cap;
        say(w, "The volume was brought down to the new cap", "");
    }
    return ZXM_OK;
}

int warble_set_volume(warble_t *w, int64_t volume)
{
    if (!warble_volume_cap_ok(volume, w->cap)) {
        say(w, "That volume is past its limits or the owner's cap", "");
        return ZXM_E_ARG;
    }
    w->volume = volume;
    return ZXM_OK;
}

/* ---- WM7: the watch hall ---- */

/* 'b' then base32 lower case, n bytes, 2..max */
static bool cid_ok(const char *s, uint32_t max)
{
    uint32_t n = len_of(s, max + 1u);
    if (n < 2u || n > max || s[0] != 'b') return false;
    for (uint32_t i = 1; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '2' && s[i] <= '7'))) return false;
    return true;
}

int warble_hall_set(warble_t *w, const char *cid)
{
    if (!cid_ok(cid, WARBLE_CID_MAX)) return ZXM_E_ARG;
    memcpy(w->hall_cid, cid, len_of(cid, WARBLE_CID_MAX) + 1u);
    memset(&w->hall, 0, sizeof w->hall);
    return ZXM_OK;
}

/* the hall.status object of the last answer into w->hall */
static int keep_hall(warble_t *w)
{
    zxm_link_t *l = w->link;
    warble_hall_t h;
    char state[8];
    memset(&h, 0, sizeof h);
    if (zxm_get_bool(l, l->result, "set", &h.set)) return ZXM_E_PROTOCOL;
    if (h.set) {
        if (zxm_get_str(l, l->result, "state", state, sizeof state) ||
            zxm_get_str(l, l->result, "media", h.media, sizeof h.media) ||
            zxm_get_i64(l, l->result, "pos_ms", &h.pos_ms) ||
            zxm_get_i64(l, l->result, "rate_q16", &h.rate_q16))
            return ZXM_E_PROTOCOL;
        h.state = !strcmp(state, "play")    ? WARBLE_HALL_PLAY
                  : !strcmp(state, "pause") ? WARBLE_HALL_PAUSE
                  : !strcmp(state, "stop")  ? WARBLE_HALL_STOP
                                            : WARBLE_HALL_NONE;
        if (h.state == WARBLE_HALL_NONE || h.pos_ms < 0 || h.rate_q16 < 1 ||
            h.rate_q16 > WARBLE_RATE_MAX)
            return ZXM_E_PROTOCOL;
    }
    if (zxm_get(l, l->result, "viewers") >= 0 && zxm_get_i64(l, l->result, "viewers", &h.viewers))
        return ZXM_E_PROTOCOL;
    if (zxm_get(l, l->result, "may_control") >= 0 &&
        zxm_get_bool(l, l->result, "may_control", &h.may_control))
        return ZXM_E_PROTOCOL;
    w->hall = h;
    return ZXM_OK;
}

static int hall_call(warble_t *w, const char *method, const char *media, int64_t pos_ms,
                     int64_t rate_q16)
{
    zxm_link_t *l = w->link;
    if (!w->hall_cid[0]) return ZXM_E_STATE;
    if (pos_ms < -1 || rate_q16 < 0 || rate_q16 > WARBLE_RATE_MAX) return ZXM_E_ARG;
    zxm_begin(l, method);
    zxm_p_str(l, "cid", w->hall_cid);
    if (media) zxm_p_str(l, "media", media);
    if (pos_ms >= 0) zxm_p_i64(l, "pos_ms", pos_ms);
    if (rate_q16 > 0) zxm_p_i64(l, "rate_q16", rate_q16);
    int rc = zxm_call(l);
    if (rc) return rc;
    return keep_hall(w);
}

int warble_hall_load(warble_t *w, const char *media, int64_t pos_ms)
{
    if (!cid_ok(media, WARBLE_MEDIA_MAX)) return ZXM_E_ARG;
    return hall_call(w, "hall.load", media, pos_ms, 0);
}

int warble_hall_play(warble_t *w, int64_t pos_ms, int64_t rate_q16)
{
    return hall_call(w, "hall.play", NULL, pos_ms, rate_q16);
}

int warble_hall_pause(warble_t *w, int64_t pos_ms)
{
    return hall_call(w, "hall.pause", NULL, pos_ms, 0);
}

int warble_hall_status(warble_t *w)
{
    return hall_call(w, "hall.status", NULL, -1, 0);
}

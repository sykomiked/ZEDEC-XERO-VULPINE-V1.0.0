/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* birdsong_model.c — Birdsong's model (birdsong_model.h). */
#include <string.h>

#include "birdsong_model.h"

/* ---- the rules ---- */

/* floor(sample * level / 10000), toward minus infinity, before the clip */
static int64_t scaled(int64_t sample, int64_t level)
{
    int64_t p = sample * level;
    int64_t q = p / BIRDSONG_UNITY;
    if (p % BIRDSONG_UNITY != 0 && p < 0) q -= 1;
    return q;
}

static bool in_domain(int64_t sample, int64_t level)
{
    return sample >= -32768 && sample <= 32767 && level >= 0 && level <= BIRDSONG_LEVEL_MAX;
}

int64_t birdsong_gain(int64_t sample, int64_t level)
{
    if (!in_domain(sample, level)) return BIRDSONG_REFUSED;
    int64_t q = scaled(sample, level);
    if (q > 32767) return 32767;
    if (q < -32768) return -32768;
    return q;
}

static int64_t gcd64(int64_t a, int64_t b) /* a, b >= 0 */
{
    while (b) {
        int64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

int birdsong_clip_ms(int64_t samples, int64_t rate, int64_t *num, int64_t *den)
{
    if (!num || !den || samples < 0 || rate <= 0 || samples > INT64_MAX / 1000) return ZXM_E_ARG;
    int64_t n = samples * 1000;
    int64_t g = gcd64(n, rate); /* at least 1: rate > 0 */
    *num = n / g;
    *den = rate / g;
    return ZXM_OK;
}

bool birdsong_rec_ok(int64_t rate, int64_t channels, int64_t frames)
{
    return rate >= BIRDSONG_RATE_MIN && rate <= BIRDSONG_RATE_MAX && channels >= 1 &&
           channels <= BIRDSONG_CHANNELS_MAX && frames >= 0 && frames <= BIRDSONG_FRAMES_MAX;
}

/* |v| has at most `digits` decimal digits (MYRA's iN) */
static bool fits_digits(int64_t v, uint32_t digits)
{
    int64_t top = 1;
    for (uint32_t i = 0; i < digits; i++) top *= 10;
    return v > -top && v < top;
}

bool birdsong_memo_ok(int64_t memo, int64_t rate, int64_t channels, int64_t frames, int64_t mtime)
{
    return fits_digits(memo, 9) && fits_digits(rate, 5) && fits_digits(channels, 1) &&
           fits_digits(frames, 9) && fits_digits(mtime, 12);
}

/* ---- the memo ---- */

void birdsong_init(birdsong_t *b, zxm_link_t *link)
{
    memset(b, 0, sizeof *b);
    b->link = link;
}

int birdsong_take(birdsong_t *b, int32_t rate, int32_t channels, const int16_t *pcm, uint32_t n)
{
    if (!pcm && n) return ZXM_E_ARG;
    if (channels < 1 || channels > BIRDSONG_CHANNELS_MAX || n % (uint32_t) channels)
        return ZXM_E_ARG;
    uint32_t frames = n / (uint32_t) channels;
    if (!birdsong_rec_ok(rate, channels, frames)) return ZXM_E_ARG;
    if (n) memcpy(b->pcm, pcm, (size_t) n * sizeof b->pcm[0]);
    b->rate = rate;
    b->channels = channels;
    b->frames = frames;
    b->space[0] = 0;
    b->path[0] = 0;
    b->dirty = true;
    b->clipped = 0;
    return ZXM_OK;
}

int birdsong_apply_gain(birdsong_t *b, int64_t level)
{
    if (!b->rate) return ZXM_E_STATE;
    if (!in_domain(0, level)) return ZXM_E_ARG;
    uint32_t n = b->frames * (uint32_t) b->channels, clipped = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t q = scaled(b->pcm[i], level);
        clipped += q > 32767 || q < -32768;
        b->pcm[i] = (int16_t) birdsong_gain(b->pcm[i], level);
    }
    b->clipped = clipped;
    if (level != BIRDSONG_UNITY && n) b->dirty = true;
    return ZXM_OK;
}

int birdsong_trim(birdsong_t *b, uint32_t from, uint32_t to)
{
    if (!b->rate) return ZXM_E_STATE;
    if (from > to || to > b->frames) return ZXM_E_ARG;
    uint32_t ch = (uint32_t) b->channels;
    if (from || to != b->frames) {
        memmove(b->pcm, b->pcm + (size_t) from * ch, (size_t) (to - from) * ch * sizeof b->pcm[0]);
        b->frames = to - from;
        b->dirty = true;
    }
    return ZXM_OK;
}

int birdsong_length_ms(const birdsong_t *b, int64_t *num, int64_t *den)
{
    if (!b->rate) return ZXM_E_STATE;
    return birdsong_clip_ms(b->frames, b->rate, num, den);
}

/* ---- BS1: the WAV file, as base64 ---- */

static uint8_t FILEBUF[BIRDSONG_FILE_MAX];

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
    for (uint32_t i = 0; i < n; i += 4) {
        int32_t v[4];
        uint32_t pad = 0;
        for (uint32_t j = 0; j < 4; j++) {
            bool tail = i + 4 == n && j >= 2 && in[i + j] == '=';
            if (tail) {
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

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xFFFFu);
    put16(p + 2, v >> 16);
}

static uint32_t get16(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8;
}

static uint32_t get32(const uint8_t *p)
{
    return get16(p) | get16(p + 2) << 16;
}

/* The memo as a canonical 44-byte-header PCM WAV in FILEBUF; its length. */
static uint32_t wav_encode(const birdsong_t *b)
{
    uint32_t n = b->frames * (uint32_t) b->channels, data = 2u * n;
    uint32_t block = 2u * (uint32_t) b->channels;
    memcpy(FILEBUF, "RIFF", 4);
    put32(FILEBUF + 4, 36u + data);
    memcpy(FILEBUF + 8, "WAVEfmt ", 8);
    put32(FILEBUF + 16, 16);
    put16(FILEBUF + 20, 1); /* PCM */
    put16(FILEBUF + 22, (uint32_t) b->channels);
    put32(FILEBUF + 24, (uint32_t) b->rate);
    put32(FILEBUF + 28, (uint32_t) b->rate * block);
    put16(FILEBUF + 32, block);
    put16(FILEBUF + 34, 16);
    memcpy(FILEBUF + 36, "data", 4);
    put32(FILEBUF + 40, data);
    for (uint32_t i = 0; i < n; i++)
        put16(FILEBUF + BIRDSONG_WAV_HEAD + 2u * i, (uint16_t) b->pcm[i]);
    return BIRDSONG_WAV_HEAD + data;
}

/* A 16-bit PCM WAV of len bytes in FILEBUF: its format and where its
 * samples are. Other chunks (LIST, ...) are skipped; ZXM_E_ARG when it is
 * not one, or breaks BS3. */
static int wav_decode(uint32_t len, int32_t *rate, int32_t *channels, uint32_t *frames,
                      uint32_t *at)
{
    bool fmt = false;
    if (len < 12 || memcmp(FILEBUF, "RIFF", 4) || memcmp(FILEBUF + 8, "WAVE", 4)) return ZXM_E_ARG;
    for (uint32_t p = 12; p + 8 <= len;) {
        uint32_t size = get32(FILEBUF + p + 4), body = p + 8;
        if (size > len - body) return ZXM_E_ARG;
        if (!memcmp(FILEBUF + p, "fmt ", 4)) {
            if (size < 16) return ZXM_E_ARG;
            uint32_t ch = get16(FILEBUF + body + 2), hz = get32(FILEBUF + body + 4);
            if (get16(FILEBUF + body) != 1 || get16(FILEBUF + body + 14) != 16 ||
                !birdsong_rec_ok(hz, ch, 0) || get16(FILEBUF + body + 12) != 2u * ch ||
                get32(FILEBUF + body + 8) != hz * 2u * ch)
                return ZXM_E_ARG;
            *rate = (int32_t) hz;
            *channels = (int32_t) ch;
            fmt = true;
        } else if (!memcmp(FILEBUF + p, "data", 4)) {
            if (!fmt || size % (2u * (uint32_t) *channels)) return ZXM_E_ARG;
            *frames = size / (2u * (uint32_t) *channels);
            *at = body;
            return birdsong_rec_ok(*rate, *channels, *frames) ? ZXM_OK : ZXM_E_ARG;
        }
        p = body + size + (size & 1u); /* chunks are padded to even sizes */
    }
    return ZXM_E_ARG; /* no data chunk */
}

static bool where_ok(const char *space, const char *path)
{
    size_t n = path ? strlen(path) : 0;
    return n && n <= BIRDSONG_PATH_MAX && space && strlen(space) <= ZXM_SPACE_MAX;
}

static void set_where(birdsong_t *b, const char *space, const char *path)
{
    memcpy(b->space, space, strlen(space) + 1);
    if (!strcmp(b->space, "home")) b->space[0] = 0;
    memcpy(b->path, path, strlen(path) + 1);
}

/* Bytes in one request: a multiple of 3, so every piece is whole base64. */
#define PIECE 9000u

static int write_all(birdsong_t *b, const char *space, const char *path)
{
    static char enc[(PIECE / 3u) * 4u];
    zxm_link_t *l = b->link;
    uint32_t len = wav_encode(b), at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < PIECE ? len - at : PIECE;
        bool last = at + n >= len;
        uint32_t k = b64_enc(FILEBUF + at, n, enc);
        zxm_begin(l, "files.write");
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "base64", enc, k);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
        zxm_progress(l, at, len);
    } while (at < len);
    return ZXM_OK;
}

int birdsong_save(birdsong_t *b)
{
    if (!b->rate || !b->path[0]) return ZXM_E_STATE;
    int rc = write_all(b, b->space, b->path);
    if (!rc) b->dirty = false;
    return rc;
}

int birdsong_save_as(birdsong_t *b, const char *space, const char *path)
{
    if (!b->rate) return ZXM_E_STATE;
    if (!where_ok(space, path)) return ZXM_E_ARG;
    int rc = write_all(b, space, path);
    if (rc) return rc;
    set_where(b, space, path);
    b->dirty = false;
    return ZXM_OK;
}

int birdsong_open(birdsong_t *b, const char *space, const char *path)
{
    static char chunk[66u * 1024u]; /* base64 of one 48 KiB read */
    zxm_link_t *l = b->link;
    uint32_t len = 0;
    uint64_t off = 0;
    if (!where_ok(space, path)) return ZXM_E_ARG;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        zxm_p_str(l, "encoding", "base64");
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t size, next;
        bool eof;
        int32_t t = zxm_get(l, l->result, "base64");
        if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size > BIRDSONG_FILE_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0) return ZXM_E_TOOBIG;
        int32_t got = b64_dec(chunk, (uint32_t) n, FILEBUF + len, BIRDSONG_FILE_MAX - len);
        if (got < 0) return ZXM_E_PROTOCOL;
        len += (uint32_t) got;
        zxm_progress(l, len, size);
        if (eof) break;
        if (next <= off) return ZXM_E_PROTOCOL;
        off = next;
    }
    /* checked apart, so a file that is not a memo leaves the memo as it was */
    int32_t rate = 0, channels = 0;
    uint32_t frames = 0, at = 0;
    int rc = wav_decode(len, &rate, &channels, &frames, &at);
    if (rc) return rc;
    uint32_t n = frames * (uint32_t) channels;
    for (uint32_t i = 0; i < n; i++) {
        int32_t v = (int32_t) get16(FILEBUF + at + 2u * i);
        b->pcm[i] = (int16_t) (v > 32767 ? v - 65536 : v);
    }
    b->rate = rate;
    b->channels = channels;
    b->frames = frames;
    b->clipped = 0;
    b->dirty = false;
    set_where(b, space, path);
    return ZXM_OK;
}

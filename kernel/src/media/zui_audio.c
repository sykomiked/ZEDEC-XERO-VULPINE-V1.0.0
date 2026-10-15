/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_audio.c — WAV, AIFF/AIFC and FLAC decoders. See zui_audio.h. */
#include "zui_audio.h"

#include <stddef.h>

/* ---- little helpers ---------------------------------------------------- */
static uint32_t le16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}
static uint32_t le32(const uint8_t *p)
{
    return le16(p) | (le16(p + 2) << 16);
}
static uint32_t be16(const uint8_t *p)
{
    return ((uint32_t) p[0] << 8) | p[1];
}
static uint32_t be32(const uint8_t *p)
{
    return (be16(p) << 16) | be16(p + 2);
}
static bool tag4(const uint8_t *p, const char *t)
{
    return p[0] == (uint8_t) t[0] && p[1] == (uint8_t) t[1] && p[2] == (uint8_t) t[2] &&
           p[3] == (uint8_t) t[3];
}

/* ---- float to int (AU1) ------------------------------------------------ */
/* M * 2^sh (M < 2^54), signed, rounded half to even, saturated to int32 */
static int32_t scale_round(int neg, uint64_t M, int sh)
{
    uint64_t q;
    if (M == 0) return 0;
    if (sh >= 0) {
        if (sh > 31 || (M << sh) >> sh != M || (M << sh) > ((uint64_t) 1 << 31))
            return neg ? INT32_MIN : INT32_MAX;
        q = M << sh;
    } else {
        int r = -sh;
        if (r > 60) return 0;
        q = M >> r;
        uint64_t rem = M & (((uint64_t) 1 << r) - 1), half = (uint64_t) 1 << (r - 1);
        if (rem > half || (rem == half && (q & 1u))) q++;
    }
    if (neg) return q >= ((uint64_t) 1 << 31) ? INT32_MIN : -(int32_t) q;
    return q >= ((uint64_t) 1 << 31) ? INT32_MAX : (int32_t) q;
}
int32_t zui_audio_f32_to_i32(uint32_t b)
{
    int neg = (int) (b >> 31);
    uint32_t e = (b >> 23) & 0xFFu, m = b & 0x7FFFFFu;
    if (e == 255) return m ? 0 : (neg ? INT32_MIN : INT32_MAX); /* NaN -> 0, inf saturates */
    if (e == 0) return scale_round(neg, m, -149 + 31);          /* subnormal */
    return scale_round(neg, m | 0x800000u, (int) e - 127 - 23 + 31);
}
int32_t zui_audio_f64_to_i32(uint64_t b)
{
    int neg = (int) (b >> 63);
    uint32_t e = (uint32_t) (b >> 52) & 0x7FFu;
    uint64_t m = b & 0xFFFFFFFFFFFFFull;
    if (e == 2047) return m ? 0 : (neg ? INT32_MIN : INT32_MAX);
    if (e == 0) return 0; /* subnormal: far below one LSB */
    return scale_round(neg, m | ((uint64_t) 1 << 52), (int) e - 1023 - 52 + 31);
}

/* ---- MD5 (RFC 1321) ---------------------------------------------------- */
static const uint32_t MD5K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
static const uint8_t MD5S[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};
static void md5_block(uint32_t h[4], const uint8_t *p)
{
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], x[16];
    for (int i = 0; i < 16; i++) x[i] = le32(p + 4 * i);
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) & 15u;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 15u;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) & 15u;
        }
        uint32_t s = MD5S[(i >> 4) * 4 + (i & 3u)], t = a + f + MD5K[i] + x[g];
        a = d;
        d = c;
        c = b;
        b = b + ((t << s) | (t >> (32 - s)));
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
}
void zui_md5_init(zui_md5_t *m)
{
    m->h[0] = 0x67452301u;
    m->h[1] = 0xefcdab89u;
    m->h[2] = 0x98badcfeu;
    m->h[3] = 0x10325476u;
    m->len = 0;
    m->nb = 0;
}
void zui_md5_update(zui_md5_t *m, const uint8_t *p, uint32_t n)
{
    m->len += n;
    while (n) {
        m->buf[m->nb++] = *p++;
        n--;
        if (m->nb == 64) {
            md5_block(m->h, m->buf);
            m->nb = 0;
        }
    }
}
void zui_md5_final(zui_md5_t *m, uint8_t out[16])
{
    uint64_t bits = m->len * 8u;
    uint8_t pad = 0x80;
    zui_md5_update(m, &pad, 1);
    pad = 0;
    while (m->nb != 56) zui_md5_update(m, &pad, 1);
    for (int i = 0; i < 8; i++) {
        uint8_t b = (uint8_t) (bits >> (8 * i));
        zui_md5_update(m, &b, 1);
    }
    for (int i = 0; i < 16; i++) out[i] = (uint8_t) (m->h[i / 4] >> (8 * (i % 4)));
}

/* ---- sniffing ---------------------------------------------------------- */
uint32_t zui_audio_sniff(const uint8_t *d, uint32_t n)
{
    if (!d || n < 4) return ZUI_AUD_UNKNOWN;
    if (n >= 12 && tag4(d, "RIFF") && tag4(d + 8, "WAVE")) return ZUI_AUD_WAV;
    if (n >= 12 && (tag4(d, "RF64") || tag4(d, "BW64"))) return ZUI_AUD_WAV;
    if (n >= 12 && tag4(d, "FORM") && (tag4(d + 8, "AIFF") || tag4(d + 8, "AIFC")))
        return ZUI_AUD_AIFF;
    if (tag4(d, "fLaC")) return ZUI_AUD_FLAC;
    if (tag4(d, "OggS")) return ZUI_AUD_OGG;
    if (n >= 12 && tag4(d + 4, "ftyp")) return ZUI_AUD_MP4;
    if (d[0] == 'I' && d[1] == 'D' && d[2] == '3') return ZUI_AUD_MP3;
    if (d[0] == 0xFF && (d[1] & 0xF6u) == 0xF0u) return ZUI_AUD_AAC; /* ADTS: layer 00 */
    if (d[0] == 0xFF && (d[1] & 0xE0u) == 0xE0u && (d[1] & 0x06u)) return ZUI_AUD_MP3;
    return ZUI_AUD_UNKNOWN;
}

const char *zui_audio_reason(int c)
{
    switch (c) {
    case ZUI_AUD_EFORMAT:
        return "malformed, or not a recording this decoder knows";
    case ZUI_AUD_ETRUNC:
        return "the file stops before its header says it ends";
    case ZUI_AUD_EUNSUP:
        return "a codec or variant this decoder refuses";
    case ZUI_AUD_ECAP:
        return "valid, but over this build's limits";
    case ZUI_AUD_EARG:
        return "bad argument";
    case ZUI_AUD_ECRC:
        return "a FLAC frame failed its CRC";
    case ZUI_AUD_EMD5:
        return "the decoded audio does not match the FLAC MD5";
    case ZUI_AUD_WAV:
        return "WAV";
    case ZUI_AUD_AIFF:
        return "AIFF";
    case ZUI_AUD_FLAC:
        return "FLAC";
    case ZUI_AUD_MP3:
        return "MP3: not decoded here. The patents have expired; an integer decoder is a future "
               "option";
    case ZUI_AUD_AAC:
        return "AAC: not decoded here. AAC is still under a licensing pool";
    case ZUI_AUD_OGG:
        return "Ogg (Opus, Vorbis or Ogg FLAC): not decoded here. Opus and Vorbis are "
               "royalty-free but large codecs, a future option";
    case ZUI_AUD_MP4:
        return "MP4/M4A (usually AAC): not decoded here";
    default:
        return "unknown";
    }
}

/* ---- WAV --------------------------------------------------------------- */
typedef struct {
    const uint8_t *data;
    uint32_t len, fmt, bytes; /* fmt 1 PCM, 3 float; bytes per sample */
    bool be, aiff_float;
    uint32_t shift; /* AIFF: left-justified samples, bits below */
} pcm_src_t;

static const uint8_t KSUB[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80,
                                 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

static int wav_probe(const uint8_t *d, uint32_t n, zui_audio_info_t *in, pcm_src_t *s)
{
    if (tag4(d, "RF64") || tag4(d, "BW64")) return ZUI_AUD_EUNSUP;
    uint32_t riff = le32(d + 4);
    if (riff < 4) return ZUI_AUD_EFORMAT;
    uint64_t end = (uint64_t) riff + 8;
    if (end > n) end = n; /* a short RIFF size field is common; data is checked below */
    uint32_t p = 12;
    const uint8_t *fmt = 0;
    uint32_t fmtlen = 0;
    bool havedata = false;
    while ((uint64_t) p + 8 <= end) {
        uint32_t len = le32(d + p + 4);
        const uint8_t *body = d + p + 8;
        uint64_t left = end - p - 8;
        if (tag4(d + p, "fmt ")) {
            if (fmt || len < 16 || len > left) return ZUI_AUD_EFORMAT;
            fmt = body;
            fmtlen = len;
        } else if (tag4(d + p, "data")) {
            if (!fmt || havedata) return ZUI_AUD_EFORMAT;
            if (len > left) return ZUI_AUD_ETRUNC;
            s->data = body;
            s->len = len;
            havedata = true;
        } else if (len > left) {
            return ZUI_AUD_ETRUNC;
        }
        p += 8;
        if ((uint64_t) p + len + (len & 1u) > end) {
            p = (uint32_t) end;
            break;
        }
        p += len + (len & 1u);
    }
    if (!fmt || !havedata) return fmt ? ZUI_AUD_ETRUNC : ZUI_AUD_EFORMAT;
    uint32_t tag = le16(fmt), nch = le16(fmt + 2), rate = le32(fmt + 4), align = le16(fmt + 12);
    uint32_t bits = le16(fmt + 14), valid = bits;
    if (tag == 0xFFFE) {
        if (fmtlen < 40 || le16(fmt + 16) < 22) return ZUI_AUD_EFORMAT;
        valid = le16(fmt + 18);
        tag = le16(fmt + 24);
        for (int i = 0; i < 14; i++)
            if (fmt[26 + i] != KSUB[i]) return ZUI_AUD_EUNSUP;
        if (valid == 0) valid = bits;
        if (valid > bits) return ZUI_AUD_EFORMAT;
    }
    if (nch == 0 || rate == 0) return ZUI_AUD_EFORMAT;
    if (nch > ZUI_AUD_MAX_CH) return ZUI_AUD_ECAP;
    if (tag == 1) {
        if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return ZUI_AUD_EUNSUP;
    } else if (tag == 3) {
        if (bits != 32 && bits != 64) return ZUI_AUD_EUNSUP;
    } else {
        return ZUI_AUD_EUNSUP; /* A-law, mu-law, ADPCM, MP3-in-WAV, ... */
    }
    if (align != nch * (bits / 8)) return ZUI_AUD_EFORMAT;
    s->fmt = tag;
    s->bytes = bits / 8;
    s->be = false;
    s->aiff_float = false;
    s->shift = 0;
    in->kind = ZUI_AUD_WAV;
    in->rate = rate;
    in->nch = nch;
    in->is_float = tag == 3;
    in->bits = tag == 3 ? 32 : bits;
    in->valid_bits = tag == 3 ? 32 : valid;
    in->frames = s->len / align;
    return 0;
}

/* ---- AIFF -------------------------------------------------------------- */
/* 80-bit extended to a whole number of Hz; 0 if not a positive integer < 2^32 */
static uint32_t ext80(const uint8_t *p)
{
    uint32_t se = be16(p);
    uint64_t m = ((uint64_t) be32(p + 2) << 32) | be32(p + 6);
    if ((se & 0x8000u) || !m) return 0;
    int sh = (int) (se & 0x7FFFu) - 16383 - 63;
    if (sh >= 0 || sh < -63) return 0;
    if (m & (((uint64_t) 1 << -sh) - 1)) return 0; /* fractional Hz */
    uint64_t v = m >> -sh;
    return v >> 32 ? 0 : (uint32_t) v;
}
static int aiff_probe(const uint8_t *d, uint32_t n, zui_audio_info_t *in, pcm_src_t *s)
{
    bool aifc = tag4(d + 8, "AIFC");
    uint64_t end = (uint64_t) be32(d + 4) + 8;
    if (end > n) return ZUI_AUD_ETRUNC;
    uint32_t p = 12, nch = 0, bits = 0, rate = 0;
    uint64_t frames = 0;
    bool comm = false, ssnd = false, sowt = false, fl = false;
    while ((uint64_t) p + 8 <= end) {
        uint32_t len = be32(d + p + 4);
        const uint8_t *b = d + p + 8;
        if ((uint64_t) p + 8 + len > end) return ZUI_AUD_ETRUNC;
        if (tag4(d + p, "COMM")) {
            if (comm || len < 18 || (aifc && len < 22)) return ZUI_AUD_EFORMAT;
            nch = be16(b);
            frames = be32(b + 2);
            bits = be16(b + 6);
            rate = ext80(b + 8);
            if (aifc) {
                if (tag4(b + 18, "NONE") || tag4(b + 18, "twos")) {
                } else if (tag4(b + 18, "sowt")) {
                    sowt = true;
                } else if (tag4(b + 18, "fl32") || tag4(b + 18, "FL32")) {
                    fl = true;
                } else {
                    return ZUI_AUD_EUNSUP;
                }
            }
            comm = true;
        } else if (tag4(d + p, "SSND")) {
            if (ssnd || len < 8) return ZUI_AUD_EFORMAT;
            uint32_t off = be32(b);
            if (off > len - 8) return ZUI_AUD_EFORMAT;
            s->data = b + 8 + off;
            s->len = len - 8 - off;
            ssnd = true;
        }
        p += 8 + len + (len & 1u);
    }
    if (!comm) return ZUI_AUD_EFORMAT;
    if (nch == 0 || rate == 0) return ZUI_AUD_EFORMAT;
    if (nch > ZUI_AUD_MAX_CH) return ZUI_AUD_ECAP;
    if (fl) {
        if (bits != 32) return ZUI_AUD_EFORMAT;
    } else if (bits < 1 || bits > 32) {
        return ZUI_AUD_EFORMAT;
    }
    if (!ssnd) {
        if (frames) return ZUI_AUD_ETRUNC;
        s->data = d;
        s->len = 0;
    }
    uint32_t bytes = (bits + 7) / 8;
    if (frames * nch * bytes > s->len) return ZUI_AUD_ETRUNC;
    s->fmt = fl ? 3 : 1;
    s->bytes = bytes;
    s->be = !sowt;
    s->aiff_float = fl;
    s->shift = fl ? 0 : bytes * 8 - bits;
    in->kind = ZUI_AUD_AIFF;
    in->rate = rate;
    in->nch = nch;
    in->bits = fl ? 32 : bits;
    in->valid_bits = in->bits;
    in->is_float = fl;
    in->frames = frames;
    return 0;
}

static int32_t pcm_sample(const pcm_src_t *s, const uint8_t *p)
{
    uint32_t b = s->bytes;
    if (s->fmt == 3) {
        if (b == 8) {
            uint64_t v = 0;
            for (uint32_t i = 0; i < 8; i++) v |= (uint64_t) p[i] << (8 * i);
            return zui_audio_f64_to_i32(v);
        }
        return zui_audio_f32_to_i32(s->be ? be32(p) : le32(p));
    }
    if (b == 1 && !s->be && s->shift == 0) return (int32_t) p[0] - 128; /* WAV 8-bit unsigned */
    uint32_t v = 0;
    for (uint32_t i = 0; i < b; i++) v |= (uint32_t) p[s->be ? b - 1 - i : i] << (8 * i);
    v <<= 32 - 8 * b; /* to the top of 32 bits, then an arithmetic shift down */
    int32_t x = (int32_t) v;
    uint32_t down = 32 - 8 * b + s->shift;
    return down ? (x < 0 ? ~(~x >> down) : x >> down) : x;
}

/* ---- FLAC -------------------------------------------------------------- */
typedef struct {
    const uint8_t *p, *e;
    uint32_t bit;
    bool bad;
} br_t;
static uint32_t br_bit(br_t *b)
{
    if (b->p >= b->e) {
        b->bad = true;
        return 0;
    }
    uint32_t v = (*b->p >> (7 - b->bit)) & 1u;
    if (++b->bit == 8) {
        b->bit = 0;
        b->p++;
    }
    return v;
}
static uint32_t br_get(br_t *b, uint32_t n) /* n <= 32 */
{
    uint32_t v = 0;
    while (n && b->bit == 0 && n >= 8 && b->p < b->e) {
        v = (v << 8) | *b->p++;
        n -= 8;
    }
    while (n--) v = (v << 1) | br_bit(b);
    return v;
}
static int32_t br_sget(br_t *b, uint32_t n) /* signed, 1 <= n <= 32 */
{
    uint32_t v = br_get(b, n);
    if (n < 32 && (v >> (n - 1)) & 1u) v |= ~(uint32_t) 0 << n;
    return (int32_t) v;
}
static uint32_t br_unary(br_t *b) /* zeros, then a one */
{
    uint32_t q = 0;
    while (!b->bad && !br_bit(b)) {
        if (++q > 0x7FFFFFFFu) b->bad = true;
    }
    return q;
}

static uint8_t crc8(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c & 0x80u) ? ((c << 1) ^ 0x07u) & 0xFFu : (c << 1) & 0xFFu;
    }
    return (uint8_t) c;
}
static uint32_t crc16(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0;
    while (n--) {
        c ^= (uint32_t) *p++ << 8;
        for (int k = 0; k < 8; k++)
            c = (c & 0x8000u) ? ((c << 1) ^ 0x8005u) & 0xFFFFu : (c << 1) & 0xFFFFu;
    }
    return c;
}

typedef struct {
    uint32_t minbs, maxbs, rate, nch, bps;
    uint64_t total;
    uint8_t md5[16];
    const uint8_t *frames; /* first frame */
} flac_si_t;

static int flac_probe(const uint8_t *d, uint32_t n, zui_audio_info_t *in, flac_si_t *si)
{
    uint32_t p = 4;
    bool first = true, last = false;
    while (!last) {
        if (p + 4 > n) return ZUI_AUD_ETRUNC;
        uint32_t type = d[p] & 0x7Fu, len = ((uint32_t) d[p + 1] << 16) | be16(d + p + 2);
        last = (d[p] & 0x80u) != 0;
        if (type == 127) return ZUI_AUD_EFORMAT;
        if (first != (type == 0)) return ZUI_AUD_EFORMAT; /* STREAMINFO first, once */
        if ((uint64_t) p + 4 + len > n) return ZUI_AUD_ETRUNC;
        if (type == 0) {
            const uint8_t *b = d + p + 4;
            if (len != 34) return ZUI_AUD_EFORMAT;
            si->minbs = be16(b);
            si->maxbs = be16(b + 2);
            si->rate = (be32(b + 10) >> 12) & 0xFFFFFu;
            si->nch = ((b[12] >> 1) & 7u) + 1;
            si->bps = (((uint32_t) (b[12] & 1u) << 4) | (b[13] >> 4)) + 1;
            si->total = ((uint64_t) (b[13] & 0xFu) << 32) | be32(b + 14);
            for (int i = 0; i < 16; i++) si->md5[i] = b[18 + i];
            if (si->minbs < 16 || si->maxbs < si->minbs || si->rate == 0 || si->bps < 4)
                return ZUI_AUD_EFORMAT;
        }
        first = false;
        p += 4 + len;
    }
    si->frames = d + p;
    in->kind = ZUI_AUD_FLAC;
    in->rate = si->rate;
    in->nch = si->nch;
    in->bits = si->bps;
    in->valid_bits = si->bps;
    in->is_float = false;
    in->frames = si->total;
    in->max_block = si->maxbs;
    in->md5_present = false;
    for (int i = 0; i < 16; i++)
        if (si->md5[i]) in->md5_present = true;
    return 0;
}

/* residual of one subframe into s[i * st] for i in [order, bs) */
static bool flac_residual(br_t *b, int32_t *s, uint32_t st, uint32_t bs, uint32_t order)
{
    uint32_t method = br_get(b, 2);
    if (method > 1) return false;
    uint32_t pbits = method ? 5 : 4, esc = method ? 31 : 15, po = br_get(b, 4);
    uint32_t parts = 1u << po;
    if (b->bad || (bs & (parts - 1)) || (bs >> po) < order) return false;
    uint32_t i = order;
    for (uint32_t pt = 0; pt < parts; pt++) {
        uint32_t cnt = (bs >> po) - (pt == 0 ? order : 0), k = br_get(b, pbits);
        if (b->bad) return false;
        if (k == esc) {
            uint32_t nb = br_get(b, 5);
            for (uint32_t j = 0; j < cnt; j++, i++) s[i * st] = nb ? br_sget(b, nb) : 0;
        } else {
            for (uint32_t j = 0; j < cnt && !b->bad; j++, i++) {
                uint32_t q = br_unary(b);
                if (q > (0xFFFFFFFFu >> k)) return false;
                uint32_t u = (q << k) | br_get(b, k);
                s[i * st] = (int32_t) (u >> 1) ^ -(int32_t) (u & 1u);
            }
        }
        if (b->bad) return false;
    }
    return true;
}

static int64_t clamp_ok(int64_t v, uint32_t bits, bool *ok)
{
    int64_t lim = (int64_t) 1 << (bits - 1);
    if (v < -lim || v >= lim) *ok = false;
    return v;
}

/* one subframe of `bits` bits into s[i * st], i < bs */
static bool flac_subframe(br_t *b, int32_t *s, uint32_t st, uint32_t bs, uint32_t bits)
{
    if (br_bit(b)) return false; /* zero pad */
    uint32_t type = br_get(b, 6), wasted = 0;
    if (br_bit(b)) {
        wasted = br_unary(b) + 1;
        if (wasted >= bits) return false;
    }
    if (b->bad) return false;
    uint32_t eb = bits - wasted; /* effective bits */
    bool ok = true;
    if (type == 0) {
        int32_t v = br_sget(b, eb);
        for (uint32_t i = 0; i < bs; i++) s[i * st] = v;
    } else if (type == 1) {
        for (uint32_t i = 0; i < bs; i++) s[i * st] = br_sget(b, eb);
    } else if (type >= 8 && type <= 12) {
        uint32_t order = type - 8;
        if (order > bs) return false;
        for (uint32_t i = 0; i < order; i++) s[i * st] = br_sget(b, eb);
        if (!flac_residual(b, s, st, bs, order)) return false;
        for (uint32_t i = order; i < bs; i++) {
            int64_t r = s[i * st], p = 0;
            const int32_t *h = s + (size_t) i * st;
            switch (order) {
            case 1:
                p = h[-(int64_t) st];
                break;
            case 2:
                p = 2 * (int64_t) h[-(int64_t) st] - h[-2 * (int64_t) st];
                break;
            case 3:
                p = 3 * (int64_t) h[-(int64_t) st] - 3 * (int64_t) h[-2 * (int64_t) st] +
                    h[-3 * (int64_t) st];
                break;
            case 4:
                p = 4 * (int64_t) h[-(int64_t) st] - 6 * (int64_t) h[-2 * (int64_t) st] +
                    4 * (int64_t) h[-3 * (int64_t) st] - h[-4 * (int64_t) st];
                break;
            default:
                break;
            }
            int64_t v = clamp_ok(p + r, eb, &ok);
            if (!ok) return false;
            s[i * st] = (int32_t) v;
        }
    } else if (type >= 32) {
        uint32_t order = type - 31;
        if (order > bs) return false;
        for (uint32_t i = 0; i < order; i++) s[i * st] = br_sget(b, eb);
        uint32_t prec = br_get(b, 4) + 1;
        if (prec == 16) return false;
        int32_t shift = br_sget(b, 5);
        if (shift < 0) return false; /* negative shifts are reserved */
        int32_t c[32];
        for (uint32_t j = 0; j < order; j++) c[j] = br_sget(b, prec);
        if (b->bad || !flac_residual(b, s, st, bs, order)) return false;
        for (uint32_t i = order; i < bs; i++) {
            int64_t acc = 0;
            for (uint32_t j = 0; j < order; j++) acc += (int64_t) c[j] * s[(i - 1 - j) * st];
            int64_t pr = acc >= 0 ? acc >> shift : ~(~acc >> shift);
            int64_t v = clamp_ok(pr + s[i * st], eb, &ok);
            if (!ok) return false;
            s[i * st] = (int32_t) v;
        }
    } else {
        return false; /* reserved types */
    }
    if (b->bad) return false;
    if (wasted)
        for (uint32_t i = 0; i < bs; i++) s[i * st] = (int32_t) ((uint32_t) s[i * st] << wasted);
    return true;
}

/* UTF-8-like coded number (up to 36 bits) */
static bool flac_utf8(br_t *b, uint64_t *v)
{
    uint32_t c = br_get(b, 8), extra;
    if (!(c & 0x80u)) {
        *v = c;
        return !b->bad;
    }
    if ((c & 0xE0u) == 0xC0u) {
        extra = 1;
        *v = c & 0x1Fu;
    } else if ((c & 0xF0u) == 0xE0u) {
        extra = 2;
        *v = c & 0x0Fu;
    } else if ((c & 0xF8u) == 0xF0u) {
        extra = 3;
        *v = c & 0x07u;
    } else if ((c & 0xFCu) == 0xF8u) {
        extra = 4;
        *v = c & 0x03u;
    } else if ((c & 0xFEu) == 0xFCu) {
        extra = 5;
        *v = c & 0x01u;
    } else if (c == 0xFEu) {
        extra = 6;
        *v = 0;
    } else {
        return false;
    }
    while (extra--) {
        uint32_t x = br_get(b, 8);
        if ((x & 0xC0u) != 0x80u) return false;
        *v = (*v << 6) | (x & 0x3Fu);
    }
    return !b->bad;
}

static const uint32_t RATES[12] = {0,     88200, 176400, 192000, 8000,  16000,
                                   22050, 24000, 32000,  44100,  48000, 96000};

static void md5_frame(zui_md5_t *m, const int32_t *s, uint32_t cnt, uint32_t bps)
{
    uint32_t nb = (bps + 7) / 8;
    for (uint32_t i = 0; i < cnt; i++) {
        uint8_t t[4];
        for (uint32_t k = 0; k < nb; k++) t[k] = (uint8_t) ((uint32_t) s[i] >> (8 * k));
        zui_md5_update(m, t, nb);
    }
}

static int64_t flac_decode(const uint8_t *d, uint32_t n, const flac_si_t *si, int32_t *out,
                           uint64_t cap, int32_t *tmp, uint32_t tmp_words, zui_audio_info_t *in,
                           uint32_t flags)
{
    const uint8_t *p = si->frames, *e = d + n;
    uint64_t total = 0, fno = 0;
    int strategy = -1;
    zui_md5_t md5;
    bool want_md5 = (flags & ZUI_AUD_F_MD5) && in->md5_present;
    if (want_md5) zui_md5_init(&md5);
    uint32_t nframes = 0;
    while (p < e) {
        br_t b = {p, e, 0, false};
        uint32_t sync = br_get(&b, 15);
        if (b.bad) return ZUI_AUD_ETRUNC;
        if (sync != 0x7FFCu) return ZUI_AUD_EFORMAT; /* 1111 1111 1111 100 */
        uint32_t strat = br_bit(&b);
        if (strategy < 0) strategy = (int) strat;
        if ((int) strat != strategy) return ZUI_AUD_EFORMAT;
        uint32_t bsc = br_get(&b, 4), rc = br_get(&b, 4), ca = br_get(&b, 4), ss = br_get(&b, 3);
        if (br_bit(&b)) return ZUI_AUD_EFORMAT;
        uint64_t num;
        if (!flac_utf8(&b, &num)) return b.bad ? ZUI_AUD_ETRUNC : ZUI_AUD_EFORMAT;
        uint32_t bs;
        if (bsc == 0)
            return ZUI_AUD_EFORMAT;
        else if (bsc == 1)
            bs = 192;
        else if (bsc <= 5)
            bs = 576u << (bsc - 2);
        else if (bsc == 6)
            bs = br_get(&b, 8) + 1;
        else if (bsc == 7)
            bs = br_get(&b, 16) + 1;
        else
            bs = 256u << (bsc - 8);
        uint32_t rate;
        if (rc == 0)
            rate = si->rate;
        else if (rc < 12)
            rate = RATES[rc];
        else if (rc == 12)
            rate = br_get(&b, 8) * 1000;
        else if (rc == 13)
            rate = br_get(&b, 16);
        else if (rc == 14)
            rate = br_get(&b, 16) * 10;
        else
            return ZUI_AUD_EFORMAT;
        static const uint32_t SS[8] = {0, 8, 12, 0, 16, 20, 24, 32};
        if (ss == 3) return ZUI_AUD_EFORMAT;
        uint32_t bps = ss ? SS[ss] : si->bps;
        if (b.bad) return ZUI_AUD_ETRUNC;
        if (b.p >= e) return ZUI_AUD_ETRUNC;
        if (crc8(p, (uint32_t) (b.p - p)) != *b.p) return ZUI_AUD_ECRC;
        b.p++;
        if (rate != si->rate || bps != si->bps) return ZUI_AUD_EFORMAT;
        uint32_t nch = ca < 8 ? ca + 1 : 2;
        if (ca > 10) return ZUI_AUD_EFORMAT;
        if (nch != si->nch) return ZUI_AUD_EFORMAT;
        if (bs > si->maxbs) return ZUI_AUD_EFORMAT;
        if (strategy == 0 ? num != fno : num != total) return ZUI_AUD_EFORMAT;
        if (ca >= 8 && bps >= 32) return ZUI_AUD_ECAP; /* side channel would need 33 bits */
        /* where this block's samples go */
        int32_t *dst;
        uint32_t st = nch;
        bool scratch = !out || total + bs > cap;
        if (scratch) {
            if (!tmp || (uint64_t) bs * nch > tmp_words) return ZUI_AUD_EARG; /* see work_words */
            dst = tmp;
        } else {
            dst = out + total * nch;
        }
        for (uint32_t c = 0; c < nch; c++) {
            uint32_t sb = bps;
            if ((ca == 8 && c == 1) || (ca == 9 && c == 0) || (ca == 10 && c == 1)) sb++;
            if (!flac_subframe(&b, dst + c, st, bs, sb))
                return b.bad ? ZUI_AUD_ETRUNC : ZUI_AUD_EFORMAT;
        }
        bool ok = true;
        for (uint32_t i = 0; i < bs && ca >= 8; i++) {
            int64_t l, r, a = dst[i * st], c2 = dst[i * st + 1];
            if (ca == 8) {
                l = a;
                r = a - c2;
            } else if (ca == 9) {
                r = c2;
                l = a + c2;
            } else {
                int64_t mid = a * 2 + (c2 & 1);
                l = (mid + c2) >> 1;
                r = (mid - c2) >> 1;
            }
            clamp_ok(l, bps, &ok);
            clamp_ok(r, bps, &ok);
            dst[i * st] = (int32_t) l;
            dst[i * st + 1] = (int32_t) r;
        }
        if (!ok) return ZUI_AUD_EFORMAT;
        /* byte align, then CRC-16 over the whole frame */
        if (b.bit) {
            if (*b.p & (0xFFu >> b.bit)) return ZUI_AUD_EFORMAT;
            b.p++;
            b.bit = 0;
        }
        if (b.p + 2 > e) return ZUI_AUD_ETRUNC;
        if (crc16(p, (uint32_t) (b.p - p)) != be16(b.p)) return ZUI_AUD_ECRC;
        b.p += 2;
        if (want_md5) md5_frame(&md5, dst, bs * nch, bps);
        total += bs;
        fno++;
        nframes++;
        p = b.p;
    }
    if (si->total && total != si->total) return ZUI_AUD_ETRUNC;
    in->frames = total;
    in->flac_frames = nframes;
    if (want_md5) {
        uint8_t h[16];
        zui_md5_final(&md5, h);
        for (int i = 0; i < 16; i++)
            if (h[i] != si->md5[i]) return ZUI_AUD_EMD5;
        in->md5_checked = true;
    }
    return (int64_t) total;
}

/* ---- front end --------------------------------------------------------- */
static int probe2(const uint8_t *d, uint32_t n, zui_audio_info_t *in, pcm_src_t *s, flac_si_t *si)
{
    zui_audio_info_t z = {0};
    if (!d || !in) return ZUI_AUD_EARG;
    uint32_t k = zui_audio_sniff(d, n);
    int rc;
    switch (k) {
    case ZUI_AUD_WAV:
        rc = wav_probe(d, n, &z, s);
        break;
    case ZUI_AUD_AIFF:
        rc = aiff_probe(d, n, &z, s);
        break;
    case ZUI_AUD_FLAC:
        rc = flac_probe(d, n, &z, si);
        break;
    case ZUI_AUD_UNKNOWN:
        return ZUI_AUD_EFORMAT;
    default:
        return ZUI_AUD_EUNSUP;
    }
    if (rc < 0) return rc;
    if (z.nch > ZUI_AUD_MAX_CH) return ZUI_AUD_ECAP;
    *in = z;
    return 0;
}

int zui_audio_probe(const uint8_t *d, uint32_t n, zui_audio_info_t *info)
{
    pcm_src_t s;
    flac_si_t si;
    return probe2(d, n, info, &s, &si);
}

uint32_t zui_audio_work_words(const zui_audio_info_t *in)
{
    return in && in->kind == ZUI_AUD_FLAC ? in->max_block * in->nch : 0;
}

int64_t zui_audio_decode(const uint8_t *d, uint32_t n, int32_t *out, uint64_t cap, int32_t *work,
                         uint32_t work_words, zui_audio_info_t *info, uint32_t flags)
{
    pcm_src_t s;
    flac_si_t si;
    zui_audio_info_t z;
    int rc = probe2(d, n, &z, &s, &si);
    if (rc < 0) return rc;
    int64_t r;
    if (z.kind == ZUI_AUD_FLAC) {
        r = flac_decode(d, n, &si, out, cap, work, work_words, &z, flags);
    } else {
        uint64_t frames = z.frames, w = out ? (frames < cap ? frames : cap) : 0;
        uint32_t fb = s.bytes * z.nch;
        for (uint64_t f = 0; f < w; f++)
            for (uint32_t c = 0; c < z.nch; c++)
                out[f * z.nch + c] = pcm_sample(&s, s.data + f * fb + (uint64_t) c * s.bytes);
        r = (int64_t) frames;
    }
    if (r >= 0 && info) *info = z;
    return r;
}

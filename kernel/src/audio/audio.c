/* audio.c — ZEDEC XERO pqOS audio stream engine + software mixer.
 *
 * See audio.h for the contract and, more importantly, for the LIMITATIONS
 * block. Everything in this file that is not explicitly gated behind
 * audio_ops_t is fully implemented and is asserted sample-by-sample in
 * test_audio.c.
 *
 * WHY THE MIXER IS SHAPED THIS WAY
 * --------------------------------
 * Summing audio is where naive kernels corrupt sound: two streams at 30000
 * added in an int16_t wrap to -5536, which is a full-scale sign inversion —
 * an audible crack, and on some codecs a speaker-killing transient. So every
 * sum here happens in a 32-bit accumulator that cannot overflow for the
 * bounded number of streams this device supports (16 streams x a downmix
 * coefficient sum of at most 3.0 x 32768 = 1.57M, which is 0.07% of INT32_MAX),
 * and the single conversion back to 16 bits goes through audio_saturate_s16().
 * Saturation happens exactly once, at the end, so the mix is also independent
 * of the order the streams happen to be scanned in.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 */

#ifdef TEST_HOST
#include <stdio.h>
#else
#include "freestanding.h"
#endif

#include "audio.h"

/* ===================== tiny freestanding helpers ===================== */

static void au_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)v;
}

static void au_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (max == 0) return;
    for (; i + 1u < max && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int au_strcmp(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        i++;
    }
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

/* Deterministic square root. We do NOT call the toolchain's sqrt(): the host
 * test and the freestanding target must agree bit for bit, and the target's
 * softfloat path is a different implementation. Newton-Raphson on IEEE double
 * converges to the correctly-rounded root for the small, well-conditioned
 * arguments the distance model produces. */
static double au_sqrt(double x) {
    if (!(x > 0.0)) return 0.0;             /* also catches NaN */
    double r = (x > 1.0) ? x : 1.0;
    for (int i = 0; i < 64; i++) {
        double nr = 0.5 * (r + x / r);
        if (nr >= r) break;                 /* converged or oscillating */
        r = nr;
    }
    /* one guarded refinement so perfect squares land exactly */
    double nr = 0.5 * (r + x / r);
    if (nr * nr >= x && nr < r) r = nr;
    return r;
}

/* Round half away from zero, with the range clamped so the cast is always
 * defined. NaN maps to 0 rather than to undefined behaviour. */
static int32_t au_round_i32(double v) {
    if (v != v) return 0;
    if (v >= 2000000000.0) return 2000000000;
    if (v <= -2000000000.0) return -2000000000;
    return (int32_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
}

static bool in01(double v)     { return v >= 0.0 && v <= 1.0; }
static bool in_pm1(double v)   { return v >= -1.0 && v <= 1.0; }
static bool chan_ok(uint8_t c) { return c == 1 || c == 2 || c == 6 || c == 8; }

/* ===================== pure exported helpers ===================== */

uint32_t audio_format_bytes(audio_format_t fmt) {
    switch (fmt) {
        case AUDIO_FMT_PCM_U8:
        case AUDIO_FMT_PCM_S8:      return 1;
        case AUDIO_FMT_PCM_S16LE:
        case AUDIO_FMT_PCM_S16BE:   return 2;
        case AUDIO_FMT_PCM_S24LE:   return 3;
        case AUDIO_FMT_PCM_S32LE:
        case AUDIO_FMT_FLOAT32:     return 4;
        default:                    return 0;
    }
}

int16_t audio_saturate_s16(int32_t v) {
    if (v > 32767)  return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

bool audio_rate_supported(uint32_t hz) {
    switch (hz) {
        case 8000: case 11025: case 16000: case 22050: case 32000:
        case 44100: case 48000: case 96000: case 192000: return true;
        default: return false;
    }
}

/* ===================== static ring-buffer pool ===================== */
/* No malloc anywhere in this kernel. Stream rings come from here. A slot is
 * claimed by audio_create_stream and released only when the owning device is
 * re-initialised — see LIMITATION 6. */

static uint8_t g_pool[AUDIO_POOL_SLOTS][AUDIO_STREAM_BUF_SIZE];
static const audio_device_t *g_pool_owner[AUDIO_POOL_SLOTS];

uint32_t audio_pool_slots_free(void) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < AUDIO_POOL_SLOTS; i++)
        if (g_pool_owner[i] == 0) n++;
    return n;
}

static uint8_t *pool_claim(const audio_device_t *dev) {
    for (uint32_t i = 0; i < AUDIO_POOL_SLOTS; i++) {
        if (g_pool_owner[i] == 0) {
            g_pool_owner[i] = dev;
            au_memset(g_pool[i], 0, AUDIO_STREAM_BUF_SIZE);
            return g_pool[i];
        }
    }
    return 0;
}

static void pool_release_all(const audio_device_t *dev) {
    for (uint32_t i = 0; i < AUDIO_POOL_SLOTS; i++)
        if (g_pool_owner[i] == dev) g_pool_owner[i] = 0;
}

/* ===================== ring arithmetic =====================
 * Reserved-slot convention: head == tail means empty, so capacity is
 * size - 1 bytes. No separate count field is needed, which matters because
 * audio_stream_t is part of the published ABI. */

static uint32_t rb_used(uint32_t head, uint32_t tail, uint32_t size) {
    return (head >= tail) ? (head - tail) : (size - tail + head);
}
static uint32_t rb_free(uint32_t head, uint32_t tail, uint32_t size) {
    return size - 1u - rb_used(head, tail, size);
}

static uint32_t st_used(const audio_stream_t *s) {
    return rb_used(s->buffer_head, s->buffer_tail, s->buffer_size);
}
static uint32_t st_free(const audio_stream_t *s) {
    return rb_free(s->buffer_head, s->buffer_tail, s->buffer_size);
}

/* Read `off` bytes past the stream's read cursor. Callers only ever ask for
 * offsets inside the used region, but the bound is enforced here anyway:
 * every index derived from caller data is range-checked before use. */
static uint8_t st_rd(const audio_stream_t *s, uint32_t off) {
    if (!s->buffer || s->buffer_size == 0 || off >= s->buffer_size) return 0;
    uint32_t i = s->buffer_tail + off;
    if (i >= s->buffer_size) i -= s->buffer_size;
    return s->buffer[i];
}

/* Write `off` bytes past the stream's write cursor. */
static void st_wr(audio_stream_t *s, uint32_t off, uint8_t b) {
    if (!s->buffer || s->buffer_size == 0 || off >= s->buffer_size) return;
    uint32_t i = s->buffer_head + off;
    if (i >= s->buffer_size) i -= s->buffer_size;
    s->buffer[i] = b;
}

static void st_advance_head(audio_stream_t *s, uint32_t n) {
    s->buffer_head = (s->buffer_head + n) % s->buffer_size;
}
static void st_advance_tail(audio_stream_t *s, uint32_t n) {
    s->buffer_tail = (s->buffer_tail + n) % s->buffer_size;
}

static uint32_t frame_bytes(const audio_stream_t *s) {
    return audio_format_bytes(s->format) * (uint32_t)s->channels;
}

/* ===================== stream lookup ===================== */

static audio_stream_t *find_stream(audio_device_t *dev, uint32_t id) {
    if (!dev || id == 0) return 0;
    for (uint32_t i = 0; i < dev->num_streams && i < AUDIO_MAX_STREAMS; i++)
        if (dev->streams[i].stream_id == id) return &dev->streams[i];
    return 0;
}

static const audio_stream_t *find_stream_c(const audio_device_t *dev, uint32_t id) {
    if (!dev || id == 0) return 0;
    for (uint32_t i = 0; i < dev->num_streams && i < AUDIO_MAX_STREAMS; i++)
        if (dev->streams[i].stream_id == id) return &dev->streams[i];
    return 0;
}

/* ===================== sample decode / encode =====================
 * Everything is converted into the signed-16-bit domain (-32768..32767) for
 * mixing. S24/S32 lose their low bits here; that is LIMITATION 2, stated in
 * the header rather than hidden. All multi-byte access is done byte-wise, so
 * the code is endian-independent on both host and target. */

static double f32_from_bits(uint32_t bits) {
    union { uint32_t u; float f; } cvt;
    cvt.u = bits;
    return (double)cvt.f;
}

static uint32_t bits_from_f32(double v) {
    union { uint32_t u; float f; } cvt;
    cvt.f = (float)v;
    return cvt.u;
}

static int32_t decode_sample(const audio_stream_t *s, uint32_t off) {
    switch (s->format) {
        case AUDIO_FMT_PCM_U8:
            return ((int32_t)st_rd(s, off) - 128) * 256;
        case AUDIO_FMT_PCM_S8:
            return (int32_t)(int8_t)st_rd(s, off) * 256;
        /* Sign extension is written as "mask, then subtract the sign weight"
         * rather than as a cast of an out-of-range unsigned value, which is
         * only implementation-defined. This form is fully defined in C11. */
        case AUDIO_FMT_PCM_S16LE: {
            uint32_t v = (uint32_t)st_rd(s, off) | ((uint32_t)st_rd(s, off + 1) << 8);
            return (int32_t)(v & 0x7FFFu) - ((v & 0x8000u) ? 32768 : 0);
        }
        case AUDIO_FMT_PCM_S16BE: {
            uint32_t v = ((uint32_t)st_rd(s, off) << 8) | (uint32_t)st_rd(s, off + 1);
            return (int32_t)(v & 0x7FFFu) - ((v & 0x8000u) ? 32768 : 0);
        }
        case AUDIO_FMT_PCM_S24LE: {
            uint32_t v = (uint32_t)st_rd(s, off)
                       | ((uint32_t)st_rd(s, off + 1) << 8)
                       | ((uint32_t)st_rd(s, off + 2) << 16);
            int32_t sv = (int32_t)(v & 0x7FFFFFu) - ((v & 0x800000u) ? 0x800000 : 0);
            return sv >> 8;
        }
        case AUDIO_FMT_PCM_S32LE: {
            uint32_t v = (uint32_t)st_rd(s, off)
                       | ((uint32_t)st_rd(s, off + 1) << 8)
                       | ((uint32_t)st_rd(s, off + 2) << 16)
                       | ((uint32_t)st_rd(s, off + 3) << 24);
            int32_t sv = (int32_t)(v & 0x7FFFFFFFu);
            if (v & 0x80000000u) { sv -= 0x40000000; sv -= 0x40000000; }
            return sv >> 16;
        }
        case AUDIO_FMT_FLOAT32: {
            uint32_t v = (uint32_t)st_rd(s, off)
                       | ((uint32_t)st_rd(s, off + 1) << 8)
                       | ((uint32_t)st_rd(s, off + 2) << 16)
                       | ((uint32_t)st_rd(s, off + 3) << 24);
            double f = f32_from_bits(v);
            if (f != f) return 0;               /* a NaN sample is silence */
            if (f > 1.0) f = 1.0;
            if (f < -1.0) f = -1.0;
            return au_round_i32(f * 32767.0);
        }
        default:
            return 0;
    }
}

/* Encode one S16-domain sample into the stream's format at `off` bytes past
 * the write cursor. The inverse of decode_sample, to the precision the format
 * allows. */
static void encode_sample(audio_stream_t *s, uint32_t off, int32_t v) {
    int16_t q = audio_saturate_s16(v);
    switch (s->format) {
        case AUDIO_FMT_PCM_U8:
            st_wr(s, off, (uint8_t)((q >> 8) + 128));
            break;
        case AUDIO_FMT_PCM_S8:
            st_wr(s, off, (uint8_t)(int8_t)(q >> 8));
            break;
        case AUDIO_FMT_PCM_S16LE:
            st_wr(s, off,     (uint8_t)((uint16_t)q & 0xFFu));
            st_wr(s, off + 1, (uint8_t)(((uint16_t)q >> 8) & 0xFFu));
            break;
        case AUDIO_FMT_PCM_S16BE:
            st_wr(s, off,     (uint8_t)(((uint16_t)q >> 8) & 0xFFu));
            st_wr(s, off + 1, (uint8_t)((uint16_t)q & 0xFFu));
            break;
        case AUDIO_FMT_PCM_S24LE: {
            uint32_t u = ((uint32_t)(uint16_t)q << 8) & 0xFFFFFFu;
            st_wr(s, off,     (uint8_t)(u & 0xFFu));
            st_wr(s, off + 1, (uint8_t)((u >> 8) & 0xFFu));
            st_wr(s, off + 2, (uint8_t)((u >> 16) & 0xFFu));
            break;
        }
        case AUDIO_FMT_PCM_S32LE: {
            uint32_t u = (uint32_t)(uint16_t)q << 16;
            st_wr(s, off,     (uint8_t)(u & 0xFFu));
            st_wr(s, off + 1, (uint8_t)((u >> 8) & 0xFFu));
            st_wr(s, off + 2, (uint8_t)((u >> 16) & 0xFFu));
            st_wr(s, off + 3, (uint8_t)((u >> 24) & 0xFFu));
            break;
        }
        case AUDIO_FMT_FLOAT32: {
            uint32_t u = bits_from_f32((double)q / 32767.0);
            st_wr(s, off,     (uint8_t)(u & 0xFFu));
            st_wr(s, off + 1, (uint8_t)((u >> 8) & 0xFFu));
            st_wr(s, off + 2, (uint8_t)((u >> 16) & 0xFFu));
            st_wr(s, off + 3, (uint8_t)((u >> 24) & 0xFFu));
            break;
        }
        default:
            break;
    }
}

/* Decode frame `fi` (counted from the read cursor) and fold it to stereo.
 * Fold-down coefficients are 1.0 for the matching front channel and 0.5
 * (integer halving, truncating toward zero) for everything else — see
 * LIMITATION 4. Channel order is the WAVE_FORMAT_EXTENSIBLE order:
 *   5.1: FL FR FC LFE SL SR
 *   7.1: FL FR FC LFE RL RR SL SR                                      */
static void decode_frame(const audio_stream_t *s, uint32_t fi, int32_t *l, int32_t *r) {
    uint32_t bps = audio_format_bytes(s->format);
    uint32_t base = fi * (uint32_t)s->channels * bps;
    int32_t c[8] = {0,0,0,0,0,0,0,0};
    uint32_t nch = s->channels;
    if (nch > 8) nch = 8;
    for (uint32_t i = 0; i < nch; i++) c[i] = decode_sample(s, base + i * bps);

    switch (s->channels) {
        case 1: *l = c[0]; *r = c[0]; break;
        case 2: *l = c[0]; *r = c[1]; break;
        case 6:
            *l = c[0] + c[2] / 2 + c[3] / 2 + c[4] / 2;
            *r = c[1] + c[2] / 2 + c[3] / 2 + c[5] / 2;
            break;
        case 8:
            *l = c[0] + c[2] / 2 + c[3] / 2 + c[4] / 2 + c[6] / 2;
            *r = c[1] + c[2] / 2 + c[3] / 2 + c[5] / 2 + c[7] / 2;
            break;
        default: *l = 0; *r = 0; break;
    }
}

/* ===================== DMA staging rings ===================== */

static uint32_t tx_used(const audio_device_t *d) {
    return rb_used(d->tx_head, d->tx_tail, AUDIO_BUFFER_SIZE);
}
static uint32_t tx_free(const audio_device_t *d) {
    return rb_free(d->tx_head, d->tx_tail, AUDIO_BUFFER_SIZE);
}
static uint32_t rx_used(const audio_device_t *d) {
    return rb_used(d->rx_head, d->rx_tail, AUDIO_BUFFER_SIZE);
}
static uint32_t rx_free(const audio_device_t *d) {
    return rb_free(d->rx_head, d->rx_tail, AUDIO_BUFFER_SIZE);
}

static void tx_push_s16(audio_device_t *d, int16_t v) {
    d->tx_dma[d->tx_head] = (uint8_t)((uint16_t)v & 0xFFu);
    d->tx_head = (d->tx_head + 1u) % AUDIO_BUFFER_SIZE;
    d->tx_dma[d->tx_head] = (uint8_t)(((uint16_t)v >> 8) & 0xFFu);
    d->tx_head = (d->tx_head + 1u) % AUDIO_BUFFER_SIZE;
}

static int32_t rx_read_s16(const audio_device_t *d, uint32_t off) {
    uint32_t i = (d->rx_tail + off) % AUDIO_BUFFER_SIZE;
    uint32_t j = (i + 1u) % AUDIO_BUFFER_SIZE;
    uint32_t v = (uint32_t)d->rx_dma[i] | ((uint32_t)d->rx_dma[j] << 8);
    return (int32_t)(int16_t)(uint16_t)v;
}

uint32_t audio_tx_pending(const audio_device_t *dev) {
    return dev ? tx_used(dev) : 0;
}

/* ===================== mixer routing ===================== */

static const audio_mixer_ch_t *channel_for(const audio_device_t *dev,
                                           const audio_stream_t *s) {
    uint32_t n = dev->num_mixer_channels;
    if (n > AUDIO_MAX_MIXER_CH) n = AUDIO_MAX_MIXER_CH;
    for (uint32_t i = 0; i < n; i++)
        if (dev->mixer[i].source_stream == s->stream_id) return &dev->mixer[i];
    uint32_t def = s->is_capture ? AUDIO_CH_MIC : AUDIO_CH_PCM;
    if (def < n) return &dev->mixer[def];
    return 0;
}

/* Per-stream left/right gain, everything except the master fader.
 *
 *   base = stream volume * mixer-channel volume * distance attenuation
 *   pan  = balance + (spatial ? x/d : 0), clamped to [-1,1]
 *   gl   = base * (pan <= 0 ? 1 : 1 - pan)
 *   gr   = base * (pan >= 0 ? 1 : 1 + pan)
 *
 * The pan law is deliberately linear rather than the usual -3 dB constant-
 * power curve: it is exactly representable, so the mixer's output can be
 * asserted as an exact integer in the tests instead of "within some epsilon".
 * Centre is unity on both sides (no centre dip). */
static void stream_gains(const audio_device_t *dev, const audio_stream_t *s,
                         double *gl, double *gr) {
    double base = s->volume;
    const audio_mixer_ch_t *c = channel_for(dev, s);
    if (!c || c->muted) { *gl = 0.0; *gr = 0.0; return; }
    base *= c->volume;

    double pan = s->balance;
    if (s->spatial && dev->supports_3d) {
        double d = au_sqrt(s->pos_x * s->pos_x + s->pos_y * s->pos_y + s->pos_z * s->pos_z);
        base *= 1.0 / (1.0 + d);
        if (d > 0.0) pan += s->pos_x / d;
    }
    if (pan > 1.0) pan = 1.0;
    if (pan < -1.0) pan = -1.0;

    *gl = base * ((pan <= 0.0) ? 1.0 : (1.0 - pan));
    *gr = base * ((pan >= 0.0) ? 1.0 : (1.0 + pan));
}

/* ===================== lifecycle ===================== */

static void apply_caps(audio_device_t *dev, audio_ctrl_type_t type) {
    switch (type) {
        case AUDIO_CTRL_AC97:
            dev->max_sample_rate = 48000; dev->max_channels = 2;
            dev->supports_capture = true; dev->supports_3d = false; break;
        case AUDIO_CTRL_HDA:
            dev->max_sample_rate = 192000; dev->max_channels = 8;
            dev->supports_capture = true; dev->supports_3d = true; break;
        case AUDIO_CTRL_USB_AUDIO:
            dev->max_sample_rate = 96000; dev->max_channels = 2;
            dev->supports_capture = true; dev->supports_3d = false; break;
        case AUDIO_CTRL_I2S:
            dev->max_sample_rate = 96000; dev->max_channels = 2;
            dev->supports_capture = true; dev->supports_3d = false; break;
        case AUDIO_CTRL_NONE:
        default:
            dev->max_sample_rate = 48000; dev->max_channels = 2;
            dev->supports_capture = false; dev->supports_3d = false; break;
    }
}

static void add_channel(audio_device_t *dev, const char *name, bool capture) {
    if (dev->num_mixer_channels >= AUDIO_MAX_MIXER_CH) return;
    audio_mixer_ch_t *c = &dev->mixer[dev->num_mixer_channels++];
    au_strcpy(c->name, name, sizeof(c->name));
    c->volume = 1.0;
    c->muted = false;
    c->is_capture = capture;
    c->source_stream = 0;
}

void audio_init(audio_device_t *dev, audio_ctrl_type_t type, const char *name) {
    /* The ARM32 boot path calls audio_init(0, ...). Do not fault. */
    if (!dev) return;

    pool_release_all(dev);
    au_memset(dev, 0, sizeof(*dev));

    dev->device_id = 1;
    dev->ctrl_type = type;
    au_strcpy(dev->name, name ? name : "audio", sizeof(dev->name));
    apply_caps(dev, type);

    dev->reg_sample_rate = (dev->max_sample_rate >= 48000) ? 48000 : dev->max_sample_rate;
    dev->reg_format = (uint32_t)AUDIO_FMT_PCM_S16LE;   /* the mixer output */
    dev->reg_volume = 255;
    dev->reg_command = 0;
    dev->reg_status = 0;

    dev->master_volume = 1.0;
    dev->master_muted = false;

    add_channel(dev, "Master", false);
    add_channel(dev, "PCM",    false);
    add_channel(dev, "Line-In", true);
    add_channel(dev, "Mic",    true);

    dev->m5.omega = 0;
    dev->m5.r   = SR_FROM_FLOAT(1.0);
    dev->m5.ell = SR_FROM_FLOAT(0.0);
    dev->m5.phi = SR_ZERO;
    dev->m5.chi = 0;
    dev->coverage_r = 1.0;
    dev->coverage_l = 0.0;
}

uint32_t audio_create_stream(audio_device_t *dev, bool capture, audio_format_t fmt,
                             uint32_t rate, uint8_t channels) {
    if (!dev) return 0;
    if (dev->num_streams >= AUDIO_MAX_STREAMS) return 0;
    if ((int)fmt < 0 || (int)fmt >= (int)AUDIO_FMT__COUNT) return 0;
    if (audio_format_bytes(fmt) == 0) return 0;
    if (!audio_rate_supported(rate) || rate > dev->max_sample_rate) return 0;
    if (!chan_ok(channels) || channels > dev->max_channels) return 0;
    if (capture && !dev->supports_capture) return 0;

    uint8_t *buf = pool_claim(dev);
    if (!buf) return 0;                       /* pool exhausted — say so */

    uint32_t idx = dev->num_streams;
    audio_stream_t *s = &dev->streams[idx];
    au_memset(s, 0, sizeof(*s));
    s->stream_id   = idx + 1;
    s->active      = true;
    s->is_capture  = capture;
    s->format      = fmt;
    s->sample_rate = rate;
    s->channels    = channels;
    s->buffer      = buf;
    s->buffer_size = AUDIO_STREAM_BUF_SIZE;
    s->volume      = 1.0;
    s->balance     = 0.0;
    s->spatial     = false;
    s->rs_phase    = 0;
    dev->num_streams++;

    dev->reg_command |= AUDIO_CMD_RUN;
    return s->stream_id;
}

/* ===================== data path ===================== */

int audio_write(audio_device_t *dev, uint32_t stream_id, const void *data, uint32_t len) {
    if (!dev || !data) return AUDIO_EINVAL;
    audio_stream_t *s = find_stream(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    if (s->is_capture) return AUDIO_EDIR;
    if (len == 0) return 0;

    uint32_t space = st_free(s);
    uint32_t n = (len < space) ? len : space;
    /* A full playback ring is backpressure, not a fault: report 0 bytes
     * accepted and raise nothing. Counting this as an overrun would be a
     * statistic for an event that did not happen. */
    if (n == 0) return 0;
    const uint8_t *p = (const uint8_t *)data;
    for (uint32_t i = 0; i < n; i++) st_wr(s, i, p[i]);
    st_advance_head(s, n);
    dev->stats.bytes_written += n;
    return (int)n;
}

int audio_read(audio_device_t *dev, uint32_t stream_id, void *data, uint32_t len) {
    if (!dev || !data) return AUDIO_EINVAL;
    audio_stream_t *s = find_stream(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    if (!s->is_capture) return AUDIO_EDIR;
    if (len == 0) return 0;

    uint32_t have = st_used(s);
    uint32_t n = (len < have) ? len : have;
    if (n == 0) return 0;
    uint8_t *p = (uint8_t *)data;
    for (uint32_t i = 0; i < n; i++) p[i] = st_rd(s, i);
    st_advance_tail(s, n);
    dev->stats.bytes_read += n;
    return (int)n;
}

int audio_set_volume(audio_device_t *dev, uint32_t stream_id, double vol) {
    if (!dev) return AUDIO_EINVAL;
    if (!in01(vol)) return AUDIO_EINVAL;       /* rejects NaN too */
    audio_stream_t *s = find_stream(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    s->volume = vol;
    return AUDIO_OK;
}

int audio_set_balance(audio_device_t *dev, uint32_t stream_id, double bal) {
    if (!dev) return AUDIO_EINVAL;
    if (!in_pm1(bal)) return AUDIO_EINVAL;
    audio_stream_t *s = find_stream(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    s->balance = bal;
    return AUDIO_OK;
}

int audio_set_3d_position(audio_device_t *dev, uint32_t stream_id,
                          double x, double y, double z) {
    if (!dev) return AUDIO_EINVAL;
    if (!dev->supports_3d) return AUDIO_ENOSUP;
    if (x != x || y != y || z != z) return AUDIO_EINVAL;
    if (x > 1e9 || x < -1e9 || y > 1e9 || y < -1e9 || z > 1e9 || z < -1e9)
        return AUDIO_EINVAL;
    audio_stream_t *s = find_stream(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    if (s->is_capture) return AUDIO_EDIR;      /* a microphone has no position */
    s->pos_x = x; s->pos_y = y; s->pos_z = z;
    s->spatial = true;
    return AUDIO_OK;
}

static void refresh_run_bit(audio_device_t *dev) {
    bool any = false;
    for (uint32_t i = 0; i < dev->num_streams && i < AUDIO_MAX_STREAMS; i++)
        if (dev->streams[i].stream_id && dev->streams[i].active) { any = true; break; }
    if (any) dev->reg_command |= AUDIO_CMD_RUN;
    else     dev->reg_command &= ~AUDIO_CMD_RUN;
}

int audio_pause(audio_device_t *dev, uint32_t stream_id) {
    if (!dev) return AUDIO_EINVAL;
    audio_stream_t *s = find_stream(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    s->active = false;
    refresh_run_bit(dev);
    return AUDIO_OK;
}

int audio_resume(audio_device_t *dev, uint32_t stream_id) {
    if (!dev) return AUDIO_EINVAL;
    audio_stream_t *s = find_stream(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    s->active = true;
    refresh_run_bit(dev);
    return AUDIO_OK;
}

int audio_stop(audio_device_t *dev, uint32_t stream_id) {
    if (!dev) return AUDIO_EINVAL;
    audio_stream_t *s = find_stream(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    s->active = false;
    s->buffer_head = 0;
    s->buffer_tail = 0;
    s->rs_phase = 0;
    dev->irq_stream_done = true;
    refresh_run_bit(dev);
    return AUDIO_OK;
}

/* ===================== mixer control ===================== */

int audio_mixer_set_channel(audio_device_t *dev, uint32_t ch, double vol, bool mute) {
    if (!dev) return AUDIO_EINVAL;
    if (ch >= dev->num_mixer_channels || ch >= AUDIO_MAX_MIXER_CH) return AUDIO_EINVAL;
    if (!in01(vol)) return AUDIO_EINVAL;
    dev->mixer[ch].volume = vol;
    dev->mixer[ch].muted = mute;
    if (ch == AUDIO_CH_MASTER) {
        dev->master_volume = vol;
        dev->master_muted = mute;
        dev->reg_volume = (uint32_t)au_round_i32(vol * 255.0);
    }
    return AUDIO_OK;
}

int audio_mixer_set_master(audio_device_t *dev, double vol, bool mute) {
    if (!dev) return AUDIO_EINVAL;
    if (!in01(vol)) return AUDIO_EINVAL;
    dev->master_volume = vol;
    dev->master_muted = mute;
    dev->reg_volume = (uint32_t)au_round_i32(vol * 255.0);
    if (AUDIO_CH_MASTER < dev->num_mixer_channels) {
        dev->mixer[AUDIO_CH_MASTER].volume = vol;
        dev->mixer[AUDIO_CH_MASTER].muted = mute;
    }
    /* Software state is applied either way; a codec that refuses the write is
     * reported, not swallowed. */
    if (dev->ops && dev->ops->set_volume) {
        uint8_t lvl = mute ? 0u : (uint8_t)dev->reg_volume;
        if (dev->ops->set_volume(dev->ops->ctx, lvl) != 0) return AUDIO_EIO;
    }
    return AUDIO_OK;
}

int audio_mixer_bind_stream(audio_device_t *dev, uint32_t ch, uint32_t stream_id) {
    if (!dev) return AUDIO_EINVAL;
    if (ch >= dev->num_mixer_channels || ch >= AUDIO_MAX_MIXER_CH) return AUDIO_EINVAL;
    if (stream_id == 0) { dev->mixer[ch].source_stream = 0; return AUDIO_OK; }
    audio_stream_t *s = find_stream(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    if (dev->mixer[ch].is_capture != s->is_capture) return AUDIO_EDIR;
    dev->mixer[ch].source_stream = stream_id;
    return AUDIO_OK;
}

int audio_mixer_find_channel(const audio_device_t *dev, const char *name) {
    if (!dev || !name) return AUDIO_EINVAL;
    uint32_t n = dev->num_mixer_channels;
    if (n > AUDIO_MAX_MIXER_CH) n = AUDIO_MAX_MIXER_CH;
    for (uint32_t i = 0; i < n; i++)
        if (au_strcmp(dev->mixer[i].name, name) == 0) return (int)i;
    return AUDIO_ENOSTREAM;
}

/* ===================== the mixer ===================== */

typedef struct {
    uint32_t si;         /* index into dev->streams                     */
    double   gl, gr;
    uint64_t step;       /* Q32.32 input frames consumed per output frame */
    uint64_t phase;
    uint32_t avail;      /* whole input frames sitting in the ring       */
    uint32_t k;          /* output frames this stream can supply         */
} mixin_t;

int audio_mixer_process_n(audio_device_t *dev, uint32_t max_frames) {
    if (!dev) return AUDIO_EINVAL;
    if (dev->reg_sample_rate == 0) return AUDIO_EFORMAT;
    if (max_frames == 0 || max_frames > AUDIO_MIX_MAX_FRAMES)
        max_frames = AUDIO_MIX_MAX_FRAMES;

    uint32_t nf = tx_free(dev) / 4u;           /* stereo S16 = 4 bytes/frame */
    if (nf > max_frames) nf = max_frames;
    if (nf == 0) { dev->reg_status &= ~AUDIO_ST_RUNNING; return 0; }

    mixin_t m[AUDIO_MAX_STREAMS];
    uint32_t nm = 0;
    uint32_t best = 0;
    uint32_t ns = dev->num_streams;
    if (ns > AUDIO_MAX_STREAMS) ns = AUDIO_MAX_STREAMS;

    for (uint32_t i = 0; i < ns; i++) {
        audio_stream_t *s = &dev->streams[i];
        if (s->stream_id == 0 || s->is_capture || !s->active || !s->buffer) continue;
        uint32_t fb = frame_bytes(s);
        if (fb == 0 || s->buffer_size == 0) continue;
        if (s->sample_rate == 0) continue;

        mixin_t *e = &m[nm];
        e->si    = i;
        e->avail = st_used(s) / fb;
        e->phase = s->rs_phase;
        e->step  = ((uint64_t)s->sample_rate << 32) / (uint64_t)dev->reg_sample_rate;
        if (e->step == 0) e->step = 1;
        stream_gains(dev, s, &e->gl, &e->gr);

        uint64_t limit = (uint64_t)e->avail << 32;
        if (limit > e->phase) {
            uint64_t kk = ((limit - 1u - e->phase) / e->step) + 1u;
            e->k = (kk > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)kk;
        } else {
            e->k = 0;
        }
        if (e->k > best) best = e->k;
        nm++;
    }

    if (nm == 0 || best == 0) {
        dev->reg_status &= ~AUDIO_ST_RUNNING;
        return 0;                              /* nothing had data to mix */
    }
    if (nf > best) nf = best;

    for (uint32_t f = 0; f < nf; f++) {
        int32_t accL = 0, accR = 0;
        for (uint32_t j = 0; j < nm; j++) {
            uint32_t fi = (uint32_t)(m[j].phase >> 32);
            if (fi >= m[j].avail) continue;    /* ran dry mid-block: silence */
            int32_t l = 0, r = 0;
            decode_frame(&dev->streams[m[j].si], fi, &l, &r);
            accL += au_round_i32((double)l * m[j].gl);
            accR += au_round_i32((double)r * m[j].gr);
            m[j].phase += m[j].step;
        }
        if (dev->master_muted) {
            accL = 0; accR = 0;
        } else if (dev->master_volume != 1.0) {
            accL = au_round_i32((double)accL * dev->master_volume);
            accR = au_round_i32((double)accR * dev->master_volume);
        }
        /* the one and only conversion back to 16 bits */
        tx_push_s16(dev, audio_saturate_s16(accL));
        tx_push_s16(dev, audio_saturate_s16(accR));
    }

    for (uint32_t j = 0; j < nm; j++) {
        audio_stream_t *s = &dev->streams[m[j].si];
        uint32_t fb = frame_bytes(s);
        uint32_t consumed = (uint32_t)(m[j].phase >> 32);
        if (consumed > m[j].avail) consumed = m[j].avail;
        if (consumed) st_advance_tail(s, consumed * fb);
        s->rs_phase = m[j].phase & 0xFFFFFFFFull;
        if (m[j].k < nf) {
            s->underruns++;
            dev->stats.underruns++;
            dev->irq_buffer_underrun = true;
        }
    }

    dev->stats.frames_mixed += nf;
    dev->reg_status |= AUDIO_ST_RUNNING;
    return (int)nf;
}

void audio_mixer_process(audio_device_t *dev) {
    (void)audio_mixer_process_n(dev, AUDIO_MIX_MAX_FRAMES);
}

/* ===================== capture fan-out ===================== */

int audio_capture_dispatch(audio_device_t *dev) {
    if (!dev) return AUDIO_EINVAL;
    if (dev->reg_sample_rate == 0) return AUDIO_EFORMAT;

    uint32_t n = rx_used(dev) / 4u;            /* device frames available */
    if (n == 0) return 0;

    uint32_t ns = dev->num_streams;
    if (ns > AUDIO_MAX_STREAMS) ns = AUDIO_MAX_STREAMS;

    for (uint32_t i = 0; i < ns; i++) {
        audio_stream_t *s = &dev->streams[i];
        if (s->stream_id == 0 || !s->is_capture || !s->active || !s->buffer) continue;
        uint32_t fb = frame_bytes(s);
        if (fb == 0 || s->sample_rate == 0) continue;

        double gl, gr;
        stream_gains(dev, s, &gl, &gr);
        uint64_t step = ((uint64_t)dev->reg_sample_rate << 32) / (uint64_t)s->sample_rate;
        if (step == 0) step = 1;
        uint64_t phase = s->rs_phase;

        while ((phase >> 32) < (uint64_t)n && st_free(s) >= fb) {
            uint32_t fi = (uint32_t)(phase >> 32);
            int32_t L = rx_read_s16(dev, fi * 4u);
            int32_t R = rx_read_s16(dev, fi * 4u + 2u);
            L = au_round_i32((double)L * gl);
            R = au_round_i32((double)R * gr);

            uint32_t bps = audio_format_bytes(s->format);
            uint32_t off = 0;
            if (s->channels == 1) {
                encode_sample(s, off, (L + R) / 2);
            } else {
                encode_sample(s, off, L);
                encode_sample(s, off + bps, R);
                for (uint32_t c = 2; c < s->channels; c++)
                    encode_sample(s, off + c * bps, 0);
            }
            st_advance_head(s, fb);
            phase += step;
        }

        uint64_t span = (uint64_t)n << 32;
        if (phase >= span) {
            s->rs_phase = phase - span;        /* carries decimation debt */
        } else {
            /* the ring filled before we consumed the block: frames are lost */
            s->overruns++;
            dev->stats.overruns++;
            dev->irq_buffer_overrun = true;
            s->rs_phase = phase & 0xFFFFFFFFull;
        }
    }

    dev->rx_tail = (dev->rx_tail + n * 4u) % AUDIO_BUFFER_SIZE;
    return (int)n;
}

int audio_rx_inject(audio_device_t *dev, const uint8_t *pcm, uint32_t n) {
    if (!dev || !pcm) return AUDIO_EINVAL;
    if (n % 4u != 0u) return AUDIO_EINVAL;     /* S16LE stereo frames only */
    if (n == 0) return 0;

    uint32_t space = rx_free(dev);
    uint32_t take = (n < space) ? n : space;
    take -= take % 4u;
    if (take < n) {
        dev->irq_buffer_overrun = true;
        dev->stats.overruns++;
    }
    for (uint32_t i = 0; i < take; i++) {
        dev->rx_dma[dev->rx_head] = pcm[i];
        dev->rx_head = (dev->rx_head + 1u) % AUDIO_BUFFER_SIZE;
    }
    uint32_t frames = take / 4u;
    dev->stats.frames_captured += frames;

    (void)audio_capture_dispatch(dev);
    return (int)frames;
}

/* ===================== hardware-gated operations ===================== */

int audio_bind_ops(audio_device_t *dev, const audio_ops_t *ops) {
    if (!dev) return AUDIO_EINVAL;
    dev->ops = ops;
    if (ops) dev->reg_status |= AUDIO_ST_BOUND;
    else     dev->reg_status &= ~AUDIO_ST_BOUND;
    return AUDIO_OK;
}

bool audio_has_backend(const audio_device_t *dev) {
    return dev && dev->ops;
}

int audio_dma_flush(audio_device_t *dev) {
    if (!dev) return AUDIO_EINVAL;
    if (!dev->ops) return AUDIO_ENODEV;        /* no silicon: no pretending */
    if (!dev->ops->tx_submit) return AUDIO_ENOSUP;

    uint32_t pending = tx_used(dev);
    if (pending == 0) return 0;
    /* One contiguous run per call; call again to drain a wrapped ring. */
    uint32_t run = AUDIO_BUFFER_SIZE - dev->tx_tail;
    if (run > pending) run = pending;

    int took = dev->ops->tx_submit(dev->ops->ctx, dev->tx_dma + dev->tx_tail, run);
    if (took < 0) return AUDIO_EIO;
    if ((uint32_t)took > run) return AUDIO_EIO;   /* backend lied about size */
    if (took == 0) return 0;

    dev->tx_tail = (dev->tx_tail + (uint32_t)took) % AUDIO_BUFFER_SIZE;
    dev->stats.dma_bytes_out += (uint64_t)(uint32_t)took;
    return took;
}

int audio_capture_poll(audio_device_t *dev) {
    if (!dev) return AUDIO_EINVAL;
    if (!dev->supports_capture) return AUDIO_ENOSUP;
    if (!dev->ops) return AUDIO_ENODEV;
    if (!dev->ops->rx_poll) return AUDIO_ENOSUP;

    uint8_t chunk[AUDIO_RX_CHUNK];
    int got = dev->ops->rx_poll(dev->ops->ctx, chunk, AUDIO_RX_CHUNK);
    if (got < 0) return AUDIO_EIO;
    if ((uint32_t)got > AUDIO_RX_CHUNK) return AUDIO_EIO;
    got -= got % 4;
    if (got == 0) return 0;

    dev->stats.dma_bytes_in += (uint64_t)(uint32_t)got;
    int fr = audio_rx_inject(dev, chunk, (uint32_t)got);
    if (fr < 0) return fr;
    return got;
}

int audio_set_output_rate(audio_device_t *dev, uint32_t hz) {
    if (!dev) return AUDIO_EINVAL;
    if (!audio_rate_supported(hz) || hz > dev->max_sample_rate) return AUDIO_EFORMAT;
    uint32_t prev = dev->reg_sample_rate;
    if (dev->ops && dev->ops->set_rate) {
        if (dev->ops->set_rate(dev->ops->ctx, hz) != 0) {
            dev->reg_sample_rate = prev;       /* keep software and codec in step */
            return AUDIO_EIO;
        }
    }
    dev->reg_sample_rate = hz;
    return AUDIO_OK;
}

/* ===================== IRQ ===================== */

void audio_handle_irq(audio_device_t *dev) {
    if (!dev) return;
    bool work = false;

    if (dev->irq_buffer_underrun) {
        dev->reg_status |= AUDIO_ST_UNDERRUN;
        dev->irq_buffer_underrun = false;
        work = true;
    }
    if (dev->irq_buffer_overrun) {
        dev->reg_status |= AUDIO_ST_OVERRUN;
        dev->irq_buffer_overrun = false;
        work = true;
    }
    if (dev->irq_stream_done) {
        dev->irq_stream_done = false;
        work = true;
        (void)audio_mixer_process_n(dev, AUDIO_MIX_MAX_FRAMES);
        (void)audio_dma_flush(dev);   /* AUDIO_ENODEV when unbound; counts nothing */
    }
    if (work) dev->stats.irqs_handled++;
}

void audio_clear_status(audio_device_t *dev) {
    if (!dev) return;
    dev->reg_status &= ~(AUDIO_ST_UNDERRUN | AUDIO_ST_OVERRUN);
}

/* ===================== introspection ===================== */

int32_t audio_stream_available(const audio_device_t *dev, uint32_t stream_id) {
    const audio_stream_t *s = find_stream_c(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    return (int32_t)st_used(s);
}

int32_t audio_stream_space(const audio_device_t *dev, uint32_t stream_id) {
    const audio_stream_t *s = find_stream_c(dev, stream_id);
    if (!s) return AUDIO_ENOSTREAM;
    return (int32_t)st_free(s);
}

/* ===================== coverage ===================== */

static bool name_terminated(const char *p, uint32_t max) {
    for (uint32_t i = 0; i < max; i++) if (p[i] == '\0') return true;
    return false;
}

bool audio_verify_coverage(audio_device_t *dev) {
    if (!dev) return false;

    /* ---- structural invariants. Any violation is a hard false. ---- */
    if (dev->num_streams > AUDIO_MAX_STREAMS) return false;
    if (dev->num_mixer_channels > AUDIO_MAX_MIXER_CH) return false;
    if (!in01(dev->master_volume)) return false;
    if (!audio_rate_supported(dev->reg_sample_rate)) return false;
    if (dev->reg_sample_rate > dev->max_sample_rate) return false;
    if (dev->tx_head >= AUDIO_BUFFER_SIZE || dev->tx_tail >= AUDIO_BUFFER_SIZE) return false;
    if (dev->rx_head >= AUDIO_BUFFER_SIZE || dev->rx_tail >= AUDIO_BUFFER_SIZE) return false;
    if (dev->reg_format != (uint32_t)AUDIO_FMT_PCM_S16LE) return false;

    uint32_t routable = 0;
    for (uint32_t i = 0; i < dev->num_streams; i++) {
        const audio_stream_t *s = &dev->streams[i];
        if (s->stream_id == 0) return false;             /* num_streams lied */
        if (!s->buffer) return false;
        if (s->buffer_size == 0 || s->buffer_size > AUDIO_STREAM_BUF_SIZE) return false;
        if (s->buffer_head >= s->buffer_size) return false;
        if (s->buffer_tail >= s->buffer_size) return false;
        if (!in01(s->volume)) return false;
        if (!in_pm1(s->balance)) return false;
        if ((int)s->format < 0 || (int)s->format >= (int)AUDIO_FMT__COUNT) return false;
        if (!chan_ok(s->channels) || s->channels > dev->max_channels) return false;
        if (!audio_rate_supported(s->sample_rate)) return false;
        if (s->rs_phase >= ((uint64_t)1 << 48)) return false;
        if (s->is_capture && !dev->supports_capture) return false;
        if (s->spatial && !dev->supports_3d) return false;

        /* ---- can this stream actually put sound through right now? ---- */
        const audio_mixer_ch_t *c = channel_for(dev, s);
        bool live = s->active && s->volume > 0.0;
        if (!c || c->muted || !(c->volume > 0.0)) live = false;
        if (!s->is_capture && (dev->master_muted || !(dev->master_volume > 0.0)))
            live = false;
        if (live) routable++;
    }

    uint32_t engaged = 0;
    for (uint32_t i = 0; i < dev->num_mixer_channels; i++) {
        const audio_mixer_ch_t *c = &dev->mixer[i];
        if (!in01(c->volume)) return false;
        if (!name_terminated(c->name, (uint32_t)sizeof(c->name))) return false;
        if (c->source_stream != 0 && !find_stream_c(dev, c->source_stream)) return false;
        if (c->name[0] != '\0' && !c->muted && c->volume > 0.0) engaged++;
    }

    /* ---- coverage ---- */
    dev->coverage_r = (dev->num_streams == 0)
                    ? (dev->master_muted ? 0.0 : 1.0)
                    : (double)routable / (double)dev->num_streams;
    dev->coverage_l = (dev->num_mixer_channels == 0)
                    ? 0.0
                    : (double)engaged / (double)dev->num_mixer_channels;

    dev->m5.omega = dev->num_streams;
    dev->m5.r     = SR_FROM_FLOAT(dev->coverage_r);
    dev->m5.ell   = SR_FROM_FLOAT(dev->coverage_l);

    /* A device with no streams AND no mixer channels has nothing to cover. */
    if (dev->num_streams == 0 && dev->num_mixer_channels == 0) return true;

    return (dev->coverage_r * dev->coverage_l) >= AUDIO_COVERAGE_FLOOR;
}

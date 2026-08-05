/* test_audio.c — the audio mixer against known samples.
 *
 * The anchors here are NUMBERS, not control flow. "audio_mixer_process
 * returned ok" proves nothing about sound; "stream A at -8000 plus stream B
 * at -4000 came out of the DMA ring as exactly -12000" proves the summing
 * path. Every mixer case below states the arithmetic it expects and then
 * reads the bytes back out of tx_dma.
 *
 * The clipping case is the one that matters most: 30000 + 30000 in an int16
 * wraps to -5536, a full-scale sign inversion. We assert +32767.
 *
 * The hardware boundary is tested from both sides: with no ops bound every
 * silicon operation must return AUDIO_ENODEV and leave dma_bytes_out at zero,
 * and with a fake codec bound the same calls must actually move bytes.
 *
 * audio_verify_coverage() is tested in the FAILING direction as well as the
 * passing one — a verifier that cannot fail verifies nothing.
 */
#include <stdio.h>
#include "audio.h"

static int failures = 0;
static int checks   = 0;
#define CHECK(c,m) do{ checks++; if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* Read stereo frame `f` back out of the device's TX DMA ring (S16LE). */
static void tx_frame(const audio_device_t *d, uint32_t f, int32_t *l, int32_t *r) {
    uint32_t o = (d->tx_tail + f * 4u) % AUDIO_BUFFER_SIZE;
    uint32_t a = o, b = (o + 1u) % AUDIO_BUFFER_SIZE;
    uint32_t c = (o + 2u) % AUDIO_BUFFER_SIZE, e = (o + 3u) % AUDIO_BUFFER_SIZE;
    uint32_t vl = (uint32_t)d->tx_dma[a] | ((uint32_t)d->tx_dma[b] << 8);
    uint32_t vr = (uint32_t)d->tx_dma[c] | ((uint32_t)d->tx_dma[e] << 8);
    *l = (int32_t)(vl & 0x7FFFu) - ((vl & 0x8000u) ? 32768 : 0);
    *r = (int32_t)(vr & 0x7FFFu) - ((vr & 0x8000u) ? 32768 : 0);
}

/* Build a little-endian S16 buffer of `n` samples all equal to `v`. */
static void fill_s16(uint8_t *out, uint32_t n, int32_t v) {
    for (uint32_t i = 0; i < n; i++) {
        out[i * 2u]      = (uint8_t)((uint32_t)v & 0xFFu);
        out[i * 2u + 1u] = (uint8_t)(((uint32_t)v >> 8) & 0xFFu);
    }
}

/* ===================== a fake codec ===================== */
typedef struct {
    uint8_t  sink[4096];
    uint32_t sink_len;
    uint32_t max_accept;    /* short-write emulation */
    uint8_t  src[4096];
    uint32_t src_len;
    uint32_t rate_set;
    uint8_t  vol_set;
    bool     fail_rate;
    bool     fail_vol;
    bool     fail_tx;
    int      tx_calls, rx_calls;
} fake_codec_t;

static int fc_tx(void *ctx, const uint8_t *pcm, uint32_t n) {
    fake_codec_t *f = (fake_codec_t *)ctx;
    f->tx_calls++;
    if (f->fail_tx) return AUDIO_EIO;
    uint32_t take = n;
    if (f->max_accept && take > f->max_accept) take = f->max_accept;
    if (take > sizeof(f->sink) - f->sink_len) take = (uint32_t)sizeof(f->sink) - f->sink_len;
    for (uint32_t i = 0; i < take; i++) f->sink[f->sink_len + i] = pcm[i];
    f->sink_len += take;
    return (int)take;
}
static int fc_rx(void *ctx, uint8_t *pcm, uint32_t cap) {
    fake_codec_t *f = (fake_codec_t *)ctx;
    f->rx_calls++;
    uint32_t n = f->src_len < cap ? f->src_len : cap;
    for (uint32_t i = 0; i < n; i++) pcm[i] = f->src[i];
    f->src_len = 0;
    return (int)n;
}
static int fc_rate(void *ctx, uint32_t hz) {
    fake_codec_t *f = (fake_codec_t *)ctx;
    if (f->fail_rate) return -1;
    f->rate_set = hz;
    return 0;
}
static int fc_vol(void *ctx, uint8_t lvl) {
    fake_codec_t *f = (fake_codec_t *)ctx;
    if (f->fail_vol) return -1;
    f->vol_set = lvl;
    return 0;
}

/* audio_device_t carries two 64 KiB DMA rings; keep them off the stack. */
static audio_device_t dev;
static audio_device_t dev2;
static fake_codec_t   codec;
static uint8_t        scratch[8192];

int main(void) {
    int32_t l, r;

    printf("=== ZXV audio: stream engine + software mixer ===\n");

    /* ---------------- pure helpers ---------------- */
    CHECK(audio_saturate_s16(60000) == 32767,
          "saturate(60000) == 32767 — the sum of two loud streams CLIPS");
    CHECK(audio_saturate_s16(-60000) == -32768, "saturate(-60000) == -32768");
    CHECK(audio_saturate_s16(32767) == 32767 && audio_saturate_s16(-32768) == -32768,
          "saturate is the identity inside the 16-bit range");
    CHECK((int16_t)(int32_t)(30000 + 30000) != audio_saturate_s16(60000),
          "a plain int16 truncation of 60000 is NOT what we produce (it wraps)");
    CHECK(audio_format_bytes(AUDIO_FMT_PCM_U8) == 1 &&
          audio_format_bytes(AUDIO_FMT_PCM_S16LE) == 2 &&
          audio_format_bytes(AUDIO_FMT_PCM_S24LE) == 3 &&
          audio_format_bytes(AUDIO_FMT_FLOAT32) == 4, "format sizes 1/2/3/4");
    CHECK(audio_format_bytes((audio_format_t)99) == 0, "an unknown format has size 0");
    CHECK(audio_rate_supported(44100) && audio_rate_supported(8000),
          "44100 and 8000 are supported rates");
    CHECK(!audio_rate_supported(44101) && !audio_rate_supported(0),
          "44101 and 0 are NOT supported rates");

    /* ---------------- init ---------------- */
    audio_init(0, AUDIO_CTRL_HDA, "null");     /* the ARM32 boot path does this */
    CHECK(1, "audio_init(NULL, ...) does not fault");

    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    CHECK(dev.ctrl_type == AUDIO_CTRL_HDA && dev.max_channels == 8 &&
          dev.max_sample_rate == 192000 && dev.supports_3d,
          "HDA profile: 8ch, 192 kHz, 3D offered");
    CHECK(dev.reg_sample_rate == 48000 && dev.reg_format == (uint32_t)AUDIO_FMT_PCM_S16LE,
          "output defaults to 48 kHz S16LE");
    CHECK(dev.num_mixer_channels == AUDIO_NUM_DEFAULT_CH,
          "four default mixer channels exist");
    CHECK(audio_mixer_find_channel(&dev, "PCM") == (int)AUDIO_CH_PCM &&
          audio_mixer_find_channel(&dev, "Mic") == (int)AUDIO_CH_MIC,
          "channels are findable by name");
    CHECK(audio_mixer_find_channel(&dev, "Aux") == AUDIO_ENOSTREAM,
          "a channel that does not exist is not invented");
    CHECK(dev.stats.dma_bytes_out == 0 && !audio_has_backend(&dev),
          "a fresh device has NO codec bound and has moved zero bytes");

    {
        audio_device_t *ac = &dev2;
        audio_init(ac, AUDIO_CTRL_AC97, "AC97");
        CHECK(ac->max_channels == 2 && ac->max_sample_rate == 48000 && !ac->supports_3d,
              "AC97 profile: 2ch, 48 kHz, no 3D");
        CHECK(audio_create_stream(ac, false, AUDIO_FMT_PCM_S16LE, 48000, 6) == 0,
              "AC97 refuses a 5.1 stream — it only has two channels");
        CHECK(audio_create_stream(ac, false, AUDIO_FMT_PCM_S16LE, 96000, 2) == 0,
              "AC97 refuses 96 kHz — above its max rate");
        CHECK(audio_set_3d_position(ac, 1, 1.0, 0.0, 0.0) == AUDIO_ENOSUP,
              "3D on a controller without 3D returns ENOSUP, not success");
        audio_init(ac, AUDIO_CTRL_NONE, "none");   /* release its pool slots */
    }

    /* ---------------- stream creation and refusal ---------------- */
    uint32_t sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    uint32_t sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    CHECK(sa == 1 && sb == 2, "two playback streams get ids 1 and 2");
    CHECK(dev.num_streams == 2, "num_streams tracks them");
    CHECK((dev.reg_command & AUDIO_CMD_RUN) != 0, "the RUN bit is set once a stream exists");
    CHECK(audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 44101, 2) == 0,
          "an unsupported rate is refused (id 0)");
    CHECK(audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 3) == 0,
          "3 channels is refused — not a layout we downmix");
    CHECK(audio_create_stream(&dev, false, (audio_format_t)42, 48000, 2) == 0,
          "an out-of-range format enum is refused");
    CHECK(audio_stream_available(&dev, 99) == AUDIO_ENOSTREAM,
          "an unknown stream id is ENOSTREAM, not 0 bytes");

    /* ---------------- ring buffer ---------------- */
    CHECK(audio_stream_space(&dev, sa) == (int32_t)AUDIO_STREAM_BUF_SIZE - 1,
          "a new ring has size-1 bytes free (one slot is reserved)");
    fill_s16(scratch, 4, 1000);
    CHECK(audio_write(&dev, sa, scratch, 8) == 8, "8 bytes accepted");
    CHECK(audio_stream_available(&dev, sa) == 8, "8 bytes are queued");
    CHECK(audio_read(&dev, sa, scratch, 8) == AUDIO_EDIR,
          "reading a PLAYBACK stream is refused (EDIR), not silently empty");
    {
        /* fill it to the brim and prove the short write is honest */
        uint32_t space = (uint32_t)audio_stream_space(&dev, sa);
        for (uint32_t i = 0; i < sizeof(scratch); i++) scratch[i] = 0;
        uint32_t done = 0;
        while (done < space) {
            uint32_t chunk = space - done;
            if (chunk > sizeof(scratch)) chunk = (uint32_t)sizeof(scratch);
            int w = audio_write(&dev, sa, scratch, chunk);
            if (w <= 0) break;
            done += (uint32_t)w;
        }
        CHECK(done == space, "the ring accepts exactly size-1 bytes total");
        CHECK(audio_write(&dev, sa, scratch, 16) == 0,
              "a full ring accepts 0 bytes — it does not claim to have taken 16");
        CHECK(dev.stats.bytes_written == 8 + space,
              "bytes_written counts only bytes that were actually stored");
    }
    audio_stop(&dev, sa);
    CHECK(audio_stream_available(&dev, sa) == 0, "stop() discards what was buffered");
    audio_resume(&dev, sa);

    /* ================================================================
     * THE MIXER. Exact expected values, computed by hand.
     * ================================================================ */

    /* ---- 1. two streams sum ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, -8000);  audio_write(&dev, sa, scratch, 16);  /* 4 frames */
    fill_s16(scratch, 8, -4000);  audio_write(&dev, sb, scratch, 16);
    CHECK(audio_mixer_process_n(&dev, 4) == 4, "four stereo frames were produced");
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == -12000 && r == -12000,
          "-8000 + -4000 mixes to exactly -12000 on both channels");
    tx_frame(&dev, 3, &l, &r);
    CHECK(l == -12000 && r == -12000, "and the fourth frame too");
    CHECK(dev.stats.frames_mixed == 4, "frames_mixed == 4");
    CHECK(audio_tx_pending(&dev) == 16, "16 bytes of stereo S16 are staged for DMA");
    CHECK(audio_stream_available(&dev, sa) == 0, "the mixer consumed stream A's frames");

    /* ---- 2. CLIPPING: two loud streams must saturate, never wrap ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 4, 30000); audio_write(&dev, sa, scratch, 8);
    fill_s16(scratch, 4, 30000); audio_write(&dev, sb, scratch, 8);
    CHECK(audio_mixer_process_n(&dev, 2) == 2, "two frames of 30000 + 30000");
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 32767, "30000 + 30000 == 32767 (CLIPPED)");
    CHECK(l != -5536, "and specifically NOT -5536, which is what a wrapping "
                      "int16 add would have produced");
    CHECK(l > 0, "the clipped sample kept its sign");
    CHECK(r == 32767, "the right channel clipped identically");

    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 4, -30000); audio_write(&dev, sa, scratch, 8);
    fill_s16(scratch, 4, -30000); audio_write(&dev, sb, scratch, 8);
    audio_mixer_process_n(&dev, 2);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == -32768 && r == -32768, "-30000 + -30000 == -32768 (CLIPPED negative)");
    CHECK(l < 0, "the negative clip did not wrap to a positive sample");

    /* ---- 3. three streams: sum is order-independent and does not clip early ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    {
        uint32_t s1 = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        uint32_t s2 = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        uint32_t s3 = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        fill_s16(scratch, 2,  30000); audio_write(&dev, s1, scratch, 4);
        fill_s16(scratch, 2,  30000); audio_write(&dev, s2, scratch, 4);
        fill_s16(scratch, 2, -30000); audio_write(&dev, s3, scratch, 4);
        audio_mixer_process_n(&dev, 1);
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 30000,
              "30000 + 30000 - 30000 == 30000: the accumulator is wide, so an "
              "intermediate over-range does not poison the result");
    }

    /* ---- 4. per-stream volume ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 4, 20000); audio_write(&dev, sa, scratch, 8);
    CHECK(audio_set_volume(&dev, sa, 0.5) == AUDIO_OK, "volume 0.5 accepted");
    CHECK(audio_set_volume(&dev, sa, 1.5) == AUDIO_EINVAL, "volume 1.5 refused");
    CHECK(audio_set_volume(&dev, sa, -0.1) == AUDIO_EINVAL, "volume -0.1 refused");
    CHECK(audio_set_volume(&dev, 77, 0.5) == AUDIO_ENOSTREAM, "volume on a ghost stream refused");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 10000 && r == 10000, "20000 at volume 0.5 is exactly 10000");

    /* ---- 5. balance ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 4, 16000); audio_write(&dev, sa, scratch, 8);
    CHECK(audio_set_balance(&dev, sa, 1.0) == AUDIO_OK, "balance +1.0 accepted");
    CHECK(audio_set_balance(&dev, sa, 1.5) == AUDIO_EINVAL, "balance 1.5 refused");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 0 && r == 16000, "balance hard right: left 0, right 16000");

    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 4, 16000); audio_write(&dev, sa, scratch, 8);
    audio_set_balance(&dev, sa, -0.5);
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 16000 && r == 8000, "balance -0.5: left 16000, right 8000");

    /* ---- 6. master volume and master mute ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 10000); audio_write(&dev, sa, scratch, 16);
    CHECK(audio_mixer_set_master(&dev, 0.25, false) == AUDIO_OK, "master 0.25 accepted");
    CHECK(audio_mixer_set_master(&dev, 2.0, false) == AUDIO_EINVAL, "master 2.0 refused");
    CHECK(dev.reg_volume == 64, "reg_volume mirrors master 0.25 as 64/255");
    CHECK(dev.mixer[AUDIO_CH_MASTER].volume == 0.25,
          "the Master mixer channel mirrors the master fader");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 2500 && r == 2500, "10000 at master 0.25 is exactly 2500");

    audio_mixer_set_master(&dev, 1.0, true);
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 1, &l, &r);
    CHECK(l == 0 && r == 0, "master mute produces true silence, not a small value");
    /* 16 bytes were queued = 4 stereo frames; two mixer calls of one frame
     * each have consumed two of them, mute or no mute. */
    CHECK(audio_stream_available(&dev, sa) == 8,
          "a muted device still CONSUMES the stream — time does not stop");

    /* ---- 7. mixer channel gain and mute, and per-stream routing ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 8000); audio_write(&dev, sa, scratch, 16);
    fill_s16(scratch, 8, 8000); audio_write(&dev, sb, scratch, 16);
    CHECK(audio_mixer_set_channel(&dev, AUDIO_CH_PCM, 0.5, false) == AUDIO_OK,
          "PCM channel set to 0.5");
    CHECK(audio_mixer_set_channel(&dev, 99, 0.5, false) == AUDIO_EINVAL,
          "an out-of-range mixer channel index is refused");
    CHECK(audio_mixer_set_channel(&dev, AUDIO_CH_PCM, 3.0, false) == AUDIO_EINVAL,
          "a mixer gain above 1.0 is refused");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 8000, "both streams via PCM at 0.5: 4000 + 4000 == 8000");

    /* route stream B to its own channel and mute that channel */
    {
        int ch = audio_mixer_find_channel(&dev, "Line-In");
        CHECK(audio_mixer_bind_stream(&dev, (uint32_t)ch, sb) == AUDIO_EDIR,
              "a PLAYBACK stream cannot be routed to a CAPTURE channel");
        CHECK(audio_mixer_bind_stream(&dev, AUDIO_CH_MASTER, sb) == AUDIO_OK,
              "routing stream B to the Master channel is allowed");
        CHECK(audio_mixer_bind_stream(&dev, AUDIO_CH_MASTER, 999) == AUDIO_ENOSTREAM,
              "routing a ghost stream is refused");
        audio_mixer_set_channel(&dev, AUDIO_CH_MASTER, 1.0, true);   /* mute B's channel */
        audio_mixer_process_n(&dev, 1);
        tx_frame(&dev, 1, &l, &r);
        CHECK(l == 0,
              "muting the channel B is routed to silences the WHOLE mix here, "
              "because that channel is also the master");
        audio_mixer_set_channel(&dev, AUDIO_CH_MASTER, 1.0, false);
        audio_mixer_bind_stream(&dev, AUDIO_CH_MASTER, 0);
        CHECK(dev.mixer[AUDIO_CH_MASTER].source_stream == 0, "routing can be cleared");
    }

    /* ---- 8. every sample format decodes to the same S16 value ---- */
    {
        struct { audio_format_t f; const char *n; uint8_t b[4]; uint32_t nb; int32_t want; } cases[] = {
            { AUDIO_FMT_PCM_U8,    "U8   0xC0",     {0xC0,0,0,0},          1, 16384 },
            { AUDIO_FMT_PCM_S8,    "S8   0x40",     {0x40,0,0,0},          1, 16384 },
            { AUDIO_FMT_PCM_S16LE, "S16LE 16384",   {0x00,0x40,0,0},       2, 16384 },
            { AUDIO_FMT_PCM_S16BE, "S16BE 16384",   {0x40,0x00,0,0},       2, 16384 },
            { AUDIO_FMT_PCM_S24LE, "S24LE 4194304", {0x00,0x00,0x40,0},    3, 16384 },
            { AUDIO_FMT_PCM_S32LE, "S32LE 2^30",    {0x00,0x00,0x00,0x40}, 4, 16384 },
        };
        for (uint32_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
            audio_init(&dev, AUDIO_CTRL_HDA, "fmt");
            uint32_t s = audio_create_stream(&dev, false, cases[i].f, 48000, 1);
            audio_write(&dev, s, cases[i].b, cases[i].nb);
            audio_mixer_process_n(&dev, 1);
            tx_frame(&dev, 0, &l, &r);
            char msg[96];
            snprintf(msg, sizeof msg, "%s decodes to %d (mono -> both channels)",
                     cases[i].n, cases[i].want);
            CHECK(l == cases[i].want && r == cases[i].want, msg);
        }
        /* negative S24 sign extension */
        audio_init(&dev, AUDIO_CTRL_HDA, "fmt");
        uint32_t s = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S24LE, 48000, 1);
        uint8_t neg[3] = {0x00, 0x00, 0xC0};      /* -4194304 in 24-bit */
        audio_write(&dev, s, neg, 3);
        audio_mixer_process_n(&dev, 1);
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == -16384, "S24LE 0xC00000 sign-extends to -16384, not a huge positive");

        /* float32 1.0 -> full scale, -1.0 -> -32767, and NaN -> silence */
        audio_init(&dev, AUDIO_CTRL_HDA, "fmt");
        s = audio_create_stream(&dev, false, AUDIO_FMT_FLOAT32, 48000, 1);
        uint8_t fbuf[12] = {0x00,0x00,0x80,0x3F,   /*  1.0f */
                            0x00,0x00,0x80,0xBF,   /* -1.0f */
                            0x00,0x00,0xC0,0x7F};  /*  NaN  */
        audio_write(&dev, s, fbuf, 12);
        audio_mixer_process_n(&dev, 3);
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 32767, "float32 +1.0 becomes 32767");
        tx_frame(&dev, 1, &l, &r);
        CHECK(l == -32767, "float32 -1.0 becomes -32767");
        tx_frame(&dev, 2, &l, &r);
        CHECK(l == 0, "a NaN float sample becomes silence, not garbage");
    }

    /* ---- 9. multichannel downmix ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "5.1");
    {
        /* FL FR FC LFE SL SR = 1000 2000 400 200 600 800
         * L = 1000 + 200 + 100 + 300 = 1600
         * R = 2000 + 200 + 100 + 400 = 2700 */
        int16_t six[6] = {1000, 2000, 400, 200, 600, 800};
        uint8_t buf[12];
        for (int i = 0; i < 6; i++) {
            buf[i*2]   = (uint8_t)((uint16_t)six[i] & 0xFF);
            buf[i*2+1] = (uint8_t)(((uint16_t)six[i] >> 8) & 0xFF);
        }
        uint32_t s = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 6);
        CHECK(s != 0, "HDA accepts a 5.1 stream");
        audio_write(&dev, s, buf, 12);
        audio_mixer_process_n(&dev, 1);
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 1600 && r == 2700, "5.1 folds down to L=1600 R=2700 exactly");
    }

    /* ---- 10. zero-order-hold rate conversion ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "rate");
    CHECK(audio_set_output_rate(&dev, 16000) == AUDIO_OK, "output rate set to 16 kHz");
    CHECK(audio_set_output_rate(&dev, 44101) == AUDIO_EFORMAT, "a bogus rate is refused");
    {
        uint32_t s = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 8000, 1);
        int16_t seq[3] = {100, 200, 300};
        uint8_t buf[6];
        for (int i = 0; i < 3; i++) {
            buf[i*2]   = (uint8_t)((uint16_t)seq[i] & 0xFF);
            buf[i*2+1] = (uint8_t)(((uint16_t)seq[i] >> 8) & 0xFF);
        }
        audio_write(&dev, s, buf, 6);
        CHECK(audio_mixer_process_n(&dev, 6) == 6,
              "8 kHz into a 16 kHz device yields twice as many frames");
        int32_t got[6];
        for (uint32_t i = 0; i < 6; i++) { tx_frame(&dev, i, &l, &r); got[i] = l; }
        CHECK(got[0]==100 && got[1]==100 && got[2]==200 &&
              got[3]==200 && got[4]==300 && got[5]==300,
              "each 8 kHz sample is held for exactly two 16 kHz frames");
    }
    audio_init(&dev, AUDIO_CTRL_HDA, "rate");
    audio_set_output_rate(&dev, 8000);
    {
        uint32_t s = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 16000, 1);
        int16_t seq[4] = {10, 20, 30, 40};
        uint8_t buf[8];
        for (int i = 0; i < 4; i++) {
            buf[i*2]   = (uint8_t)((uint16_t)seq[i] & 0xFF);
            buf[i*2+1] = (uint8_t)(((uint16_t)seq[i] >> 8) & 0xFF);
        }
        audio_write(&dev, s, buf, 8);
        CHECK(audio_mixer_process_n(&dev, 8) == 2, "16 kHz into 8 kHz halves the frame count");
        tx_frame(&dev, 0, &l, &r); CHECK(l == 10, "decimation keeps sample 0");
        tx_frame(&dev, 1, &l, &r); CHECK(l == 30, "and sample 2, dropping 1 and 3");
    }

    /* ---- 11. underrun accounting ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "under");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 5000); audio_write(&dev, sa, scratch, 16);  /* 4 frames */
    fill_s16(scratch, 2, 7000); audio_write(&dev, sb, scratch, 4);   /* 1 frame  */
    CHECK(audio_mixer_process_n(&dev, 4) == 4, "the longer stream sets the block length");
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 12000, "frame 0 has both streams: 5000 + 7000");
    tx_frame(&dev, 1, &l, &r);
    CHECK(l == 5000, "frame 1 has only stream A — B ran dry and went silent");
    CHECK(dev.stats.underruns == 1 && dev.streams[1].underruns == 1,
          "exactly ONE underrun was recorded, against the stream that ran dry");
    CHECK(dev.streams[0].underruns == 0, "the stream that had data was not blamed");
    CHECK(dev.irq_buffer_underrun, "the underrun IRQ latch is set");

    /* ---- 12. paused streams contribute nothing and are not consumed ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "pause");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 4, 1000); audio_write(&dev, sa, scratch, 8);
    fill_s16(scratch, 4, 2000); audio_write(&dev, sb, scratch, 8);
    CHECK(audio_pause(&dev, sb) == AUDIO_OK, "stream B paused");
    audio_mixer_process_n(&dev, 2);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 1000, "a paused stream contributes nothing to the mix");
    CHECK(audio_stream_available(&dev, sb) == 8, "and its buffered audio is untouched");
    CHECK(audio_resume(&dev, sb) == AUDIO_OK, "stream B resumed");
    CHECK(audio_pause(&dev, 404) == AUDIO_ENOSTREAM, "pausing a ghost stream is refused");

    /* ---- 13. mixing when nothing has data ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "idle");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    CHECK(audio_mixer_process_n(&dev, 8) == 0,
          "an empty stream produces 0 frames — the mixer does not emit silence forever");
    CHECK(dev.stats.frames_mixed == 0, "and frames_mixed stays at 0");
    CHECK(dev.stats.underruns == 0, "no underrun is charged for an idle device");
    CHECK((dev.reg_status & AUDIO_ST_RUNNING) == 0, "the RUNNING status bit is clear");

    /* ---- 14. 3D positioning ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "3d");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 20000); audio_write(&dev, sa, scratch, 16);
    CHECK(audio_set_3d_position(&dev, sa, 1.0, 0.0, 0.0) == AUDIO_OK,
          "a source one unit to the right is accepted");
    CHECK(dev.streams[0].spatial, "the stream is flagged spatial");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 0 && r == 10000,
          "at distance 1 hard right: gain 1/(1+1)=0.5, pan +1 -> L=0, R=10000");
    {
        double nan_v = 0.0;
        nan_v = nan_v / (nan_v == 0.0 ? 1.0 : 1.0);   /* keep it 0.0, quiet */
        CHECK(audio_set_3d_position(&dev, sa, 1e30, 0.0, 0.0) == AUDIO_EINVAL,
              "an absurd coordinate is refused rather than producing inf gain");
        (void)nan_v;
    }

    /* ---- 15. capture path (no hardware needed) ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "cap");
    {
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 48000, 2);
        CHECK(cs != 0, "a capture stream is created on a device that supports capture");
        CHECK(audio_write(&dev, cs, scratch, 4) == AUDIO_EDIR,
              "writing a CAPTURE stream is refused");
        uint8_t frames[16];
        for (int i = 0; i < 4; i++) {
            int16_t lv = (int16_t)(1000 + i * 100), rv = (int16_t)(-1000 - i * 100);
            frames[i*4+0] = (uint8_t)((uint16_t)lv & 0xFF);
            frames[i*4+1] = (uint8_t)(((uint16_t)lv >> 8) & 0xFF);
            frames[i*4+2] = (uint8_t)((uint16_t)rv & 0xFF);
            frames[i*4+3] = (uint8_t)(((uint16_t)rv >> 8) & 0xFF);
        }
        CHECK(audio_rx_inject(&dev, frames, 16) == 4, "4 captured frames injected");
        CHECK(audio_rx_inject(&dev, frames, 15) == AUDIO_EINVAL,
              "a partial stereo frame is refused, not silently truncated");
        CHECK(audio_stream_available(&dev, cs) == 16,
              "16 bytes landed in the capture stream's ring");
        uint8_t out[16];
        CHECK(audio_read(&dev, cs, out, 16) == 16, "16 bytes read back");
        int32_t g0 = (int32_t)((uint32_t)out[0] | ((uint32_t)out[1] << 8));
        g0 = (g0 & 0x7FFF) - ((g0 & 0x8000) ? 32768 : 0);
        int32_t g3r = (int32_t)((uint32_t)out[14] | ((uint32_t)out[15] << 8));
        g3r = (g3r & 0x7FFF) - ((g3r & 0x8000) ? 32768 : 0);
        CHECK(g0 == 1000, "captured frame 0 left channel is exactly 1000");
        CHECK(g3r == -1300, "captured frame 3 right channel is exactly -1300");
        CHECK(dev.stats.frames_captured == 4, "frames_captured == 4");
    }
    /* mono capture stream averages the two channels */
    audio_init(&dev, AUDIO_CTRL_HDA, "cap-mono");
    {
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 48000, 1);
        uint8_t f1[4] = {0xE8,0x03, 0xD0,0x07};   /* L=1000, R=2000 */
        audio_rx_inject(&dev, f1, 4);
        uint8_t out[2];
        CHECK(audio_read(&dev, cs, out, 2) == 2, "one mono sample recorded");
        int32_t v = (int32_t)((uint32_t)out[0] | ((uint32_t)out[1] << 8));
        v = (v & 0x7FFF) - ((v & 0x8000) ? 32768 : 0);
        CHECK(v == 1500, "mono capture is (1000 + 2000)/2 == 1500");
    }
    /* a capture stream on a device with no capture support is refused */
    {
        audio_init(&dev2, AUDIO_CTRL_NONE, "silent");
        CHECK(audio_create_stream(&dev2, true, AUDIO_FMT_PCM_S16LE, 48000, 2) == 0,
              "a controller without capture refuses a capture stream");
        CHECK(audio_capture_poll(&dev2) == AUDIO_ENOSUP,
              "and capture_poll on it returns ENOSUP");
    }

    /* ================================================================
     * THE HARDWARE BOUNDARY
     * ================================================================ */
    audio_init(&dev, AUDIO_CTRL_HDA, "hw");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 4000); audio_write(&dev, sa, scratch, 16);
    audio_mixer_process_n(&dev, 4);
    CHECK(audio_tx_pending(&dev) == 16, "16 bytes are staged");
    CHECK(audio_dma_flush(&dev) == AUDIO_ENODEV,
          "with no codec bound, dma_flush returns ENODEV — it does NOT report success");
    CHECK(audio_capture_poll(&dev) == AUDIO_ENODEV, "capture_poll returns ENODEV too");
    CHECK(dev.stats.dma_bytes_out == 0 && dev.stats.dma_bytes_in == 0,
          "and NOTHING was counted as transferred");
    CHECK(audio_tx_pending(&dev) == 16, "the staged bytes were not consumed");
    CHECK((dev.reg_status & AUDIO_ST_BOUND) == 0, "the BOUND status bit is clear");

    {
        for (uint32_t i = 0; i < sizeof(codec); i++) ((uint8_t*)&codec)[i] = 0;
        audio_ops_t ops = { fc_tx, fc_rx, fc_rate, fc_vol, &codec };
        CHECK(audio_bind_ops(&dev, &ops) == AUDIO_OK, "a codec backend binds");
        CHECK(audio_has_backend(&dev) && (dev.reg_status & AUDIO_ST_BOUND),
              "the device now reports a backend");
        CHECK(audio_dma_flush(&dev) == 16, "16 bytes went to the codec");
        CHECK(codec.sink_len == 16 && codec.tx_calls == 1, "the codec really received them");
        CHECK(dev.stats.dma_bytes_out == 16, "dma_bytes_out == 16 — counted only now");
        CHECK(audio_tx_pending(&dev) == 0, "the staging ring drained");
        {
            int32_t sl = (int32_t)((uint32_t)codec.sink[0] | ((uint32_t)codec.sink[1] << 8));
            sl = (sl & 0x7FFF) - ((sl & 0x8000) ? 32768 : 0);
            CHECK(sl == 4000, "and the first sample the codec saw is exactly 4000");
        }

        /* short writes */
        fill_s16(scratch, 8, 1000); audio_write(&dev, sa, scratch, 16);
        audio_mixer_process_n(&dev, 4);
        codec.max_accept = 4;
        CHECK(audio_dma_flush(&dev) == 4, "a codec that accepts only 4 bytes gets 4");
        CHECK(audio_tx_pending(&dev) == 12, "the remaining 12 bytes stay staged");
        CHECK(dev.stats.dma_bytes_out == 20, "dma_bytes_out counts 16 + 4, not 16 + 16");
        codec.max_accept = 0;

        /* a failing codec is reported, not swallowed */
        codec.fail_tx = true;
        CHECK(audio_dma_flush(&dev) == AUDIO_EIO, "a codec error surfaces as EIO");
        CHECK(dev.stats.dma_bytes_out == 20, "and nothing extra is counted");
        codec.fail_tx = false;
        CHECK(audio_dma_flush(&dev) == 12, "after the fault clears, the 12 bytes go out");

        /* rate and volume reach the codec */
        CHECK(audio_set_output_rate(&dev, 96000) == AUDIO_OK, "96 kHz programmed");
        CHECK(codec.rate_set == 96000, "the codec was told 96000");
        codec.fail_rate = true;
        CHECK(audio_set_output_rate(&dev, 44100) == AUDIO_EIO,
              "a codec that refuses the rate makes the call fail");
        CHECK(dev.reg_sample_rate == 96000,
              "and the software rate stays at 96000 — it does not drift from the codec");
        codec.fail_rate = false;

        CHECK(audio_mixer_set_master(&dev, 1.0, false) == AUDIO_OK, "master 1.0");
        CHECK(codec.vol_set == 255, "the codec got attenuation 255");
        codec.fail_vol = true;
        CHECK(audio_mixer_set_master(&dev, 0.5, false) == AUDIO_EIO,
              "a codec that refuses the volume write is reported");
        CHECK(dev.master_volume == 0.5, "while the software fader still moved");
        codec.fail_vol = false;

        /* capture through the backend */
        audio_init(&dev, AUDIO_CTRL_HDA, "hw2");
        audio_bind_ops(&dev, &ops);
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 48000, 2);
        codec.src[0]=0x10; codec.src[1]=0x27; codec.src[2]=0x10; codec.src[3]=0x27;
        codec.src_len = 4;                                  /* one frame, 10000/10000 */
        CHECK(audio_capture_poll(&dev) == 4, "4 bytes pulled from the codec");
        CHECK(dev.stats.dma_bytes_in == 4, "dma_bytes_in == 4");
        CHECK(audio_stream_available(&dev, cs) == 4, "and they reached the capture stream");
        uint8_t o[4];
        audio_read(&dev, cs, o, 4);
        int32_t cv = (int32_t)((uint32_t)o[0] | ((uint32_t)o[1] << 8));
        cv = (cv & 0x7FFF) - ((cv & 0x8000) ? 32768 : 0);
        CHECK(cv == 10000, "the captured sample is exactly 10000");

        /* a backend with no rx_poll must not pretend */
        audio_ops_t txonly = { fc_tx, 0, 0, 0, &codec };
        audio_bind_ops(&dev, &txonly);
        CHECK(audio_capture_poll(&dev) == AUDIO_ENOSUP,
              "a play-only backend returns ENOSUP for capture");
        CHECK(audio_set_output_rate(&dev, 44100) == AUDIO_OK,
              "a backend with no set_rate still lets the software rate change");

        audio_bind_ops(&dev, 0);
        CHECK(!audio_has_backend(&dev) && audio_dma_flush(&dev) == AUDIO_ENODEV,
              "unbinding puts the device back to ENODEV");
    }

    /* ---- IRQ handling ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "irq");
    {
        for (uint32_t i = 0; i < sizeof(codec); i++) ((uint8_t*)&codec)[i] = 0;
        audio_ops_t ops = { fc_tx, fc_rx, fc_rate, fc_vol, &codec };
        audio_bind_ops(&dev, &ops);
        sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        fill_s16(scratch, 8, 2500); audio_write(&dev, sa, scratch, 16);

        audio_handle_irq(&dev);
        CHECK(dev.stats.irqs_handled == 0,
              "an IRQ with no latch set does not count as handled");

        dev.irq_stream_done = true;
        audio_handle_irq(&dev);
        CHECK(dev.stats.irqs_handled == 1, "a stream-done IRQ is counted once");
        CHECK(!dev.irq_stream_done, "the latch was cleared");
        CHECK(codec.sink_len == 16, "and the IRQ actually mixed and pushed 16 bytes");
        {
            int32_t sv = (int32_t)((uint32_t)codec.sink[0] | ((uint32_t)codec.sink[1] << 8));
            sv = (sv & 0x7FFF) - ((sv & 0x8000) ? 32768 : 0);
            CHECK(sv == 2500, "the bytes the IRQ pushed are the real mixed samples");
        }
        dev.irq_buffer_underrun = true;
        dev.irq_buffer_overrun = true;
        audio_handle_irq(&dev);
        CHECK((dev.reg_status & AUDIO_ST_UNDERRUN) && (dev.reg_status & AUDIO_ST_OVERRUN),
              "underrun and overrun are latched into reg_status");
        audio_clear_status(&dev);
        CHECK((dev.reg_status & (AUDIO_ST_UNDERRUN | AUDIO_ST_OVERRUN)) == 0,
              "and can be cleared");
        audio_bind_ops(&dev, 0);
    }

    /* ================================================================
     * COVERAGE — including the cases that MUST return false
     * ================================================================ */
    audio_init(&dev, AUDIO_CTRL_HDA, "cov");
    CHECK(audio_verify_coverage(&dev),
          "a fresh device with four live channels passes coverage");
    CHECK(dev.coverage_l == 1.0, "coverage_l is 1.0: every channel is engaged");
    CHECK(!audio_verify_coverage(0), "a NULL device fails coverage");

    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    CHECK(audio_verify_coverage(&dev) && dev.coverage_r == 1.0,
          "two live streams: coverage_r is 1.0");

    audio_pause(&dev, sb);
    CHECK(audio_verify_coverage(&dev) && dev.coverage_r == 0.5,
          "one of two paused: coverage_r drops to 0.5 and still meets the floor");
    audio_pause(&dev, sa);
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: with every stream paused, nothing is covered");
    CHECK(dev.coverage_r == 0.0, "coverage_r really is 0.0");
    audio_resume(&dev, sa); audio_resume(&dev, sb);
    CHECK(audio_verify_coverage(&dev), "resuming restores coverage");

    audio_mixer_set_master(&dev, 1.0, true);
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: a muted master covers nothing, however healthy the streams are");
    audio_mixer_set_master(&dev, 1.0, false);
    CHECK(audio_verify_coverage(&dev), "unmuting restores it");

    audio_mixer_set_channel(&dev, AUDIO_CH_PCM, 0.0, false);
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: a zero-gain PCM channel means no playback stream is routable");
    audio_mixer_set_channel(&dev, AUDIO_CH_PCM, 1.0, false);

    /* structural corruption must be caught */
    dev.streams[0].buffer_head = dev.streams[0].buffer_size;
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: a ring cursor at buffer_size is out of range");
    dev.streams[0].buffer_head = 0;
    CHECK(audio_verify_coverage(&dev), "and repairing it passes again");

    dev.streams[0].volume = 2.0;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a stream gain of 2.0 is impossible");
    dev.streams[0].volume = 1.0;

    dev.streams[1].sample_rate = 12345;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: 12345 Hz is not a rate we support");
    dev.streams[1].sample_rate = 48000;

    {
        uint8_t *saved = dev.streams[0].buffer;
        dev.streams[0].buffer = 0;
        CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a stream with no ring buffer");
        dev.streams[0].buffer = saved;
    }
    dev.num_streams = AUDIO_MAX_STREAMS + 1;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: num_streams beyond the table");
    dev.num_streams = 2;
    dev.mixer[AUDIO_CH_PCM].source_stream = 4242;
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: a mixer channel routed to a stream that does not exist");
    dev.mixer[AUDIO_CH_PCM].source_stream = 0;
    dev.tx_head = AUDIO_BUFFER_SIZE;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: tx_head outside the DMA ring");
    dev.tx_head = 0;
    dev.reg_sample_rate = 3;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: an impossible device output rate");
    dev.reg_sample_rate = 48000;
    CHECK(audio_verify_coverage(&dev), "with every field repaired, coverage passes");

    /* ---- pool exhaustion is reported, not faked ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "pool");
    audio_init(&dev2, AUDIO_CTRL_NONE, "pool2");
    CHECK(audio_pool_slots_free() == AUDIO_POOL_SLOTS,
          "re-initialising both devices returns every pool slot");
    {
        uint32_t made = 0;
        for (uint32_t i = 0; i < AUDIO_MAX_STREAMS; i++)
            if (audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2)) made++;
        CHECK(made == AUDIO_MAX_STREAMS, "all 16 stream slots can be filled");
        CHECK(audio_pool_slots_free() == 0, "the ring-buffer pool is now empty");
        CHECK(audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2) == 0,
              "the 17th stream is refused with id 0 — no buffer is invented");
        audio_init(&dev, AUDIO_CTRL_HDA, "pool");
        CHECK(audio_pool_slots_free() == AUDIO_POOL_SLOTS, "re-init frees the pool");
    }

    /* ---- a long soak: the rings must not drift ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "soak");
    {
        uint32_t s1 = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        uint32_t s2 = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 44100, 2);
        uint64_t pushed = 0;
        int bad = 0;
        for (uint32_t it = 0; it < 20000; it++) {
            fill_s16(scratch, 64, 100);
            int w1 = audio_write(&dev, s1, scratch, 128);
            int w2 = audio_write(&dev, s2, scratch, 128);
            if (w1 < 0 || w2 < 0) { bad++; break; }
            int n = audio_mixer_process_n(&dev, 16);
            if (n < 0) { bad++; break; }
            pushed += (uint64_t)n;
            /* drain the staging ring so it never wedges */
            dev.tx_tail = dev.tx_head;
            if (dev.streams[0].buffer_head >= dev.streams[0].buffer_size ||
                dev.streams[0].buffer_tail >= dev.streams[0].buffer_size ||
                dev.streams[1].buffer_head >= dev.streams[1].buffer_size ||
                dev.streams[1].buffer_tail >= dev.streams[1].buffer_size) { bad++; break; }
        }
        CHECK(bad == 0, "20000 write/mix/drain iterations with no error and no cursor drift");
        CHECK(pushed > 100000, "the soak actually produced audio (frames > 100000)");
        CHECK(dev.stats.frames_mixed == pushed, "frames_mixed agrees with what we counted");
        CHECK(audio_verify_coverage(&dev), "the device is still structurally sound after the soak");
    }

    printf("\n%s: %d check(s), %d failure(s)\n",
           failures ? "*** FAILED ***" : "ALL PASS", checks, failures);
    return failures ? 1 : 0;
}

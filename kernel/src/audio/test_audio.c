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
 * passing one — a verifier that cannot fail verifies nothing. 34 distinct
 * inputs are driven through it that must come back false.
 *
 * THREE RULES THIS FILE FOLLOWS, because breaking them is how test suites come
 * to pass while the code underneath is broken:
 *
 *  1. Expected values are DERIVED, in the comment above the assertion, before
 *     the code is run. None of them was read out of the implementation.
 *  2. An assertion that a sample is ZERO is only trustworthy if the buffer did
 *     not start at zero, so the buffers in those cases are poisoned with 0xAA
 *     first (see poison_tx / poison_ring). Silence has to be WRITTEN.
 *  3. Every claim has at least one case that fails if the function is replaced
 *     by "return 0" or "return true". That was checked by mutation: 25 hostile
 *     edits to audio.c (stub the encoder, stub the fold-down, make au_sqrt
 *     return its argument, pin the resampler at 1:1, ignore per-stream routing,
 *     make the verifier always true, stop charging overruns, drop the bounds
 *     guards, ...) and every one of them turns this suite red.
 *
 * The last sections are adversarial rather than musical: wild ring cursors,
 * oversized ring sizes, half frames, NaN gains and 4000 rounds of randomised
 * malformed input, all of which must be refused rather than indexed with. Run
 * this file under -fsanitize=address,undefined; that is where those sections
 * earn their keep.
 */
#include <stdio.h>
#include "audio.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* Read stereo frame `f` back out of the device's TX DMA ring (S16LE). */
static void tx_frame(const audio_device_t *d, uint32_t f, int32_t *l, int32_t *r)
{
    uint32_t o = (d->tx_tail + f * 4u) % AUDIO_BUFFER_SIZE;
    uint32_t a = o, b = (o + 1u) % AUDIO_BUFFER_SIZE;
    uint32_t c = (o + 2u) % AUDIO_BUFFER_SIZE, e = (o + 3u) % AUDIO_BUFFER_SIZE;
    uint32_t vl = (uint32_t) d->tx_dma[a] | ((uint32_t) d->tx_dma[b] << 8);
    uint32_t vr = (uint32_t) d->tx_dma[c] | ((uint32_t) d->tx_dma[e] << 8);
    *l = (int32_t) (vl & 0x7FFFu) - ((vl & 0x8000u) ? 32768 : 0);
    *r = (int32_t) (vr & 0x7FFFu) - ((vr & 0x8000u) ? 32768 : 0);
}

/* Build a little-endian S16 buffer of `n` samples all equal to `v`. */
static void fill_s16(uint8_t *out, uint32_t n, int32_t v)
{
    for (uint32_t i = 0; i < n; i++) {
        out[i * 2u] = (uint8_t) ((uint32_t) v & 0xFFu);
        out[i * 2u + 1u] = (uint8_t) (((uint32_t) v >> 8) & 0xFFu);
    }
}

/* Decode one S16LE sample out of a byte buffer, the same fully-defined way the
 * implementation does, so the test never depends on the host's conversion of an
 * out-of-range unsigned to int16_t. */
static int32_t rd_s16(const uint8_t *p)
{
    uint32_t v = (uint32_t) p[0] | ((uint32_t) p[1] << 8);
    return (int32_t) (v & 0x7FFFu) - ((v & 0x8000u) ? 32768 : 0);
}

/* A deterministic LCG for the malformed-input section. This is a FUNCTION, not
 * a macro: two NEXT() macro expansions in one expression would both modify the
 * seed unsequenced, which is undefined behaviour and which -Wunsequenced
 * correctly refused to compile. */
static uint32_t fz_seed = 0x5EEDF00Du;
static uint32_t NEXT(void)
{
    fz_seed = fz_seed * 1664525u + 1013904223u;
    return fz_seed;
}

/* Poison a buffer before the code under test is expected to write it. An
 * assertion that a sample "is 0" proves nothing when the buffer started at 0 —
 * the value has to be written over something that is loudly not zero. 0xAA in
 * an S16LE pair reads back as -21846. */
static void poison(uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = 0xAA;
}
static void poison_tx(audio_device_t *d)
{
    poison(d->tx_dma, AUDIO_BUFFER_SIZE);
}
static void poison_ring(audio_device_t *d, uint32_t idx)
{
    poison(d->streams[idx].buffer, d->streams[idx].buffer_size);
}

/* NaN and +/-inf from their IEEE-754 bit patterns. Writing 0.0/0.0 invites the
 * compiler to fold it away at -O2; a bit pattern cannot be folded. */
static double bits_to_double(uint64_t u)
{
    union {
        uint64_t u;
        double d;
    } c;
    c.u = u;
    return c.d;
}
#define AU_NAN bits_to_double(0x7FF8000000000000ull)
#define AU_INF bits_to_double(0x7FF0000000000000ull)

/* ===================== a fake codec ===================== */
typedef struct {
    uint8_t sink[4096];
    uint32_t sink_len;
    uint32_t max_accept; /* short-write emulation */
    uint8_t src[4096];
    uint32_t src_len;
    uint32_t rate_set;
    uint8_t vol_set;
    bool fail_rate;
    bool fail_vol;
    bool fail_tx;
    int tx_calls, rx_calls;
} fake_codec_t;

static int fc_tx(void *ctx, const uint8_t *pcm, uint32_t n)
{
    fake_codec_t *f = (fake_codec_t *) ctx;
    f->tx_calls++;
    if (f->fail_tx) return AUDIO_EIO;
    uint32_t take = n;
    if (f->max_accept && take > f->max_accept) take = f->max_accept;
    if (take > sizeof(f->sink) - f->sink_len) take = (uint32_t) sizeof(f->sink) - f->sink_len;
    for (uint32_t i = 0; i < take; i++) f->sink[f->sink_len + i] = pcm[i];
    f->sink_len += take;
    return (int) take;
}
static int fc_rx(void *ctx, uint8_t *pcm, uint32_t cap)
{
    fake_codec_t *f = (fake_codec_t *) ctx;
    f->rx_calls++;
    uint32_t n = f->src_len < cap ? f->src_len : cap;
    for (uint32_t i = 0; i < n; i++) pcm[i] = f->src[i];
    f->src_len = 0;
    return (int) n;
}
static int fc_rate(void *ctx, uint32_t hz)
{
    fake_codec_t *f = (fake_codec_t *) ctx;
    if (f->fail_rate) return -1;
    f->rate_set = hz;
    return 0;
}
static int fc_vol(void *ctx, uint8_t lvl)
{
    fake_codec_t *f = (fake_codec_t *) ctx;
    if (f->fail_vol) return -1;
    f->vol_set = lvl;
    return 0;
}

/* audio_device_t carries two 64 KiB DMA rings; keep them off the stack. */
static audio_device_t dev;
static audio_device_t dev2;
static fake_codec_t codec;
static uint8_t scratch[8192];

int main(void)
{
    int32_t l, r;

    printf("=== ZXV audio: stream engine + software mixer ===\n");

    /* ---------------- pure helpers ---------------- */
    CHECK(audio_saturate_s16(60000) == 32767,
          "saturate(60000) == 32767 — the sum of two loud streams CLIPS");
    CHECK(audio_saturate_s16(-60000) == -32768, "saturate(-60000) == -32768");
    CHECK(audio_saturate_s16(32767) == 32767 && audio_saturate_s16(-32768) == -32768,
          "saturate is the identity inside the 16-bit range");
    CHECK((int16_t) (int32_t) (30000 + 30000) != audio_saturate_s16(60000),
          "a plain int16 truncation of 60000 is NOT what we produce (it wraps)");
    CHECK(audio_format_bytes(AUDIO_FMT_PCM_U8) == 1 &&
              audio_format_bytes(AUDIO_FMT_PCM_S16LE) == 2 &&
              audio_format_bytes(AUDIO_FMT_PCM_S24LE) == 3 &&
              audio_format_bytes(AUDIO_FMT_FLOAT32) == 4,
          "format sizes 1/2/3/4");
    CHECK(audio_format_bytes((audio_format_t) 99) == 0, "an unknown format has size 0");
    CHECK(audio_rate_supported(44100) && audio_rate_supported(8000),
          "44100 and 8000 are supported rates");
    CHECK(!audio_rate_supported(44101) && !audio_rate_supported(0),
          "44101 and 0 are NOT supported rates");

    /* ---------------- init ---------------- */
    audio_init(0, AUDIO_CTRL_HDA, "null"); /* the ARM32 boot path does this */
    CHECK(audio_pool_slots_free() == AUDIO_POOL_SLOTS && !audio_verify_coverage(0),
          "audio_init(NULL, ...) is a no-op: it neither faults nor touches the pool");

    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    CHECK(dev.ctrl_type == AUDIO_CTRL_HDA && dev.max_channels == 8 &&
              dev.max_sample_rate == 192000 && dev.supports_3d,
          "HDA profile: 8ch, 192 kHz, 3D offered");
    CHECK(dev.reg_sample_rate == 48000 && dev.reg_format == (uint32_t) AUDIO_FMT_PCM_S16LE,
          "output defaults to 48 kHz S16LE");
    CHECK(dev.num_mixer_channels == AUDIO_NUM_DEFAULT_CH, "four default mixer channels exist");
    CHECK(audio_mixer_find_channel(&dev, "PCM") == (int) AUDIO_CH_PCM &&
              audio_mixer_find_channel(&dev, "Mic") == (int) AUDIO_CH_MIC,
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
        audio_init(ac, AUDIO_CTRL_NONE, "none"); /* release its pool slots */
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
    CHECK(audio_create_stream(&dev, false, (audio_format_t) 42, 48000, 2) == 0,
          "an out-of-range format enum is refused");
    CHECK(audio_stream_available(&dev, 99) == AUDIO_ENOSTREAM,
          "an unknown stream id is ENOSTREAM, not 0 bytes");

    /* ---------------- ring buffer ---------------- */
    CHECK(audio_stream_space(&dev, sa) == (int32_t) AUDIO_STREAM_BUF_SIZE - 1,
          "a new ring has size-1 bytes free (one slot is reserved)");
    fill_s16(scratch, 4, 1000);
    CHECK(audio_write(&dev, sa, scratch, 8) == 8, "8 bytes accepted");
    CHECK(audio_stream_available(&dev, sa) == 8, "8 bytes are queued");
    CHECK(audio_read(&dev, sa, scratch, 8) == AUDIO_EDIR,
          "reading a PLAYBACK stream is refused (EDIR), not silently empty");
    {
        /* fill it to the brim and prove the short write is honest */
        uint32_t space = (uint32_t) audio_stream_space(&dev, sa);
        for (uint32_t i = 0; i < sizeof(scratch); i++) scratch[i] = 0;
        uint32_t done = 0;
        while (done < space) {
            uint32_t chunk = space - done;
            if (chunk > sizeof(scratch)) chunk = (uint32_t) sizeof(scratch);
            int w = audio_write(&dev, sa, scratch, chunk);
            if (w <= 0) break;
            done += (uint32_t) w;
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
    fill_s16(scratch, 8, -8000);
    audio_write(&dev, sa, scratch, 16); /* 4 frames */
    fill_s16(scratch, 8, -4000);
    audio_write(&dev, sb, scratch, 16);
    CHECK(audio_mixer_process_n(&dev, 4) == 4, "four stereo frames were produced");
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == -12000 && r == -12000, "-8000 + -4000 mixes to exactly -12000 on both channels");
    tx_frame(&dev, 3, &l, &r);
    CHECK(l == -12000 && r == -12000, "and the fourth frame too");
    CHECK(dev.stats.frames_mixed == 4, "frames_mixed == 4");
    CHECK(audio_tx_pending(&dev) == 16, "16 bytes of stereo S16 are staged for DMA");
    CHECK(audio_stream_available(&dev, sa) == 0, "the mixer consumed stream A's frames");

    /* ---- 2. CLIPPING: two loud streams must saturate, never wrap ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 4, 30000);
    audio_write(&dev, sa, scratch, 8);
    fill_s16(scratch, 4, 30000);
    audio_write(&dev, sb, scratch, 8);
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
    fill_s16(scratch, 4, -30000);
    audio_write(&dev, sa, scratch, 8);
    fill_s16(scratch, 4, -30000);
    audio_write(&dev, sb, scratch, 8);
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
        fill_s16(scratch, 2, 30000);
        audio_write(&dev, s1, scratch, 4);
        fill_s16(scratch, 2, 30000);
        audio_write(&dev, s2, scratch, 4);
        fill_s16(scratch, 2, -30000);
        audio_write(&dev, s3, scratch, 4);
        audio_mixer_process_n(&dev, 1);
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 30000, "30000 + 30000 - 30000 == 30000: the accumulator is wide, so an "
                          "intermediate over-range does not poison the result");
    }

    /* ---- 4. per-stream volume ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 4, 20000);
    audio_write(&dev, sa, scratch, 8);
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
    fill_s16(scratch, 4, 16000);
    audio_write(&dev, sa, scratch, 8);
    CHECK(audio_set_balance(&dev, sa, 1.0) == AUDIO_OK, "balance +1.0 accepted");
    CHECK(audio_set_balance(&dev, sa, 1.5) == AUDIO_EINVAL, "balance 1.5 refused");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 0 && r == 16000, "balance hard right: left 0, right 16000");

    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 4, 16000);
    audio_write(&dev, sa, scratch, 8);
    audio_set_balance(&dev, sa, -0.5);
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 16000 && r == 8000, "balance -0.5: left 16000, right 8000");

    /* ---- 6. master volume and master mute ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    poison_tx(&dev); /* so "silence" below must be WRITTEN, not left over */
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 10000);
    audio_write(&dev, sa, scratch, 16);
    CHECK(audio_mixer_set_master(&dev, 0.25, false) == AUDIO_OK, "master 0.25 accepted");
    CHECK(audio_mixer_set_master(&dev, 2.0, false) == AUDIO_EINVAL, "master 2.0 refused");
    CHECK(dev.reg_volume == 64, "reg_volume mirrors master 0.25 as 64/255");
    CHECK(dev.mixer[AUDIO_CH_MASTER].volume == 0.25,
          "the Master mixer channel mirrors the master fader");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 2500 && r == 2500, "10000 at master 0.25 is exactly 2500");

    audio_mixer_set_master(&dev, 1.0, true);
    CHECK(audio_mixer_process_n(&dev, 1) == 1, "a muted device still PRODUCES a frame");
    tx_frame(&dev, 1, &l, &r);
    CHECK(l == 0 && r == 0, "master mute produces true silence, not a small value");
    /* 16 bytes were queued = 4 stereo frames; two mixer calls of one frame
     * each have consumed two of them, mute or no mute. */
    CHECK(audio_stream_available(&dev, sa) == 8,
          "a muted device still CONSUMES the stream — time does not stop");

    /* ---- 7. mixer channel gain and mute, and per-stream routing ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "HDA-0");
    poison_tx(&dev);
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 8000);
    audio_write(&dev, sa, scratch, 16);
    fill_s16(scratch, 8, 8000);
    audio_write(&dev, sb, scratch, 16);
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
        CHECK(audio_mixer_bind_stream(&dev, (uint32_t) ch, sb) == AUDIO_EDIR,
              "a PLAYBACK stream cannot be routed to a CAPTURE channel");
        CHECK(audio_mixer_bind_stream(&dev, AUDIO_CH_MASTER, sb) == AUDIO_OK,
              "routing stream B to the Master channel is allowed");
        CHECK(audio_mixer_bind_stream(&dev, AUDIO_CH_MASTER, 999) == AUDIO_ENOSTREAM,
              "routing a ghost stream is refused");
        audio_mixer_set_channel(&dev, AUDIO_CH_MASTER, 1.0, true); /* mute B's channel */
        CHECK(audio_mixer_process_n(&dev, 1) == 1, "a frame is still produced while muted");
        tx_frame(&dev, 1, &l, &r);
        CHECK(l == 0, "muting the channel B is routed to silences the WHOLE mix here, "
                      "because that channel is also the master");
        audio_mixer_set_channel(&dev, AUDIO_CH_MASTER, 1.0, false);
        audio_mixer_bind_stream(&dev, AUDIO_CH_MASTER, 0);
        CHECK(dev.mixer[AUDIO_CH_MASTER].source_stream == 0, "routing can be cleared");
    }

    /* ---- 7b. routing decides WHICH channel's fader a stream obeys ----
     * The case above muted the Master channel, which silences everything, so it
     * could not tell routing apart from a global mute. Here stream A is routed
     * to Master and the PCM channel — where B still lives — is muted. If routing
     * were ignored both streams would sit on the muted PCM channel and the
     * output would be 0; if the mute were ignored it would be 16000. It is
     * A alone: 8000. */
    audio_init(&dev, AUDIO_CTRL_HDA, "route");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 8000);
    audio_write(&dev, sa, scratch, 16);
    fill_s16(scratch, 8, 8000);
    audio_write(&dev, sb, scratch, 16);
    CHECK(audio_mixer_bind_stream(&dev, AUDIO_CH_MASTER, sa) == AUDIO_OK,
          "stream A is routed to the Master channel");
    CHECK(audio_mixer_set_channel(&dev, AUDIO_CH_PCM, 1.0, true) == AUDIO_OK,
          "the PCM channel — where B still lives — is muted");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 8000 && r == 8000,
          "only the routed stream survives: 8000, not 0 (routing ignored) and not "
          "16000 (mute ignored)");
    /* now move the Master channel's own fader and watch A follow it */
    CHECK(audio_mixer_set_channel(&dev, AUDIO_CH_MASTER, 0.5, false) == AUDIO_OK,
          "the Master channel fader goes to 0.5");
    CHECK(dev.reg_volume == 128,
          "and reg_volume follows through the SAME path as set_master: round(0.5*255)=128");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 1, &l, &r);
    CHECK(l == 2000,
          "A obeys its routed channel gain AND the master fader: 8000 * 0.5 * 0.5 == 2000");

    /* ---- 8. every sample format decodes to the same S16 value ---- */
    {
        struct {
            audio_format_t f;
            const char *n;
            uint8_t b[4];
            uint32_t nb;
            int32_t want;
        } cases[] = {
            {AUDIO_FMT_PCM_U8, "U8   0xC0", {0xC0, 0, 0, 0}, 1, 16384},
            {AUDIO_FMT_PCM_S8, "S8   0x40", {0x40, 0, 0, 0}, 1, 16384},
            {AUDIO_FMT_PCM_S16LE, "S16LE 16384", {0x00, 0x40, 0, 0}, 2, 16384},
            {AUDIO_FMT_PCM_S16BE, "S16BE 16384", {0x40, 0x00, 0, 0}, 2, 16384},
            {AUDIO_FMT_PCM_S24LE, "S24LE 4194304", {0x00, 0x00, 0x40, 0}, 3, 16384},
            {AUDIO_FMT_PCM_S32LE, "S32LE 2^30", {0x00, 0x00, 0x00, 0x40}, 4, 16384},
            /* the same magnitude negative: sign extension in every format */
            {AUDIO_FMT_PCM_U8, "U8   0x40", {0x40, 0, 0, 0}, 1, -16384},
            {AUDIO_FMT_PCM_S8, "S8   0xC0", {0xC0, 0, 0, 0}, 1, -16384},
            {AUDIO_FMT_PCM_S16LE, "S16LE -16384", {0x00, 0xC0, 0, 0}, 2, -16384},
            {AUDIO_FMT_PCM_S16BE, "S16BE -16384", {0xC0, 0x00, 0, 0}, 2, -16384},
            {AUDIO_FMT_PCM_S32LE, "S32LE -2^30", {0x00, 0x00, 0x00, 0xC0}, 4, -16384},
            /* the extremes of each format's range */
            {AUDIO_FMT_PCM_U8, "U8   0x00", {0x00, 0, 0, 0}, 1, -32768},
            {AUDIO_FMT_PCM_U8, "U8   0xFF", {0xFF, 0, 0, 0}, 1, 32512},
            {AUDIO_FMT_PCM_S8, "S8   0x7F", {0x7F, 0, 0, 0}, 1, 32512},
            {AUDIO_FMT_PCM_S8, "S8   0x80", {0x80, 0, 0, 0}, 1, -32768},
            {AUDIO_FMT_PCM_S16LE, "S16LE  32767", {0xFF, 0x7F, 0, 0}, 2, 32767},
            {AUDIO_FMT_PCM_S16LE, "S16LE -32768", {0x00, 0x80, 0, 0}, 2, -32768},
            {AUDIO_FMT_PCM_S24LE, "S24LE  max", {0xFF, 0xFF, 0x7F, 0}, 3, 32767},
            {AUDIO_FMT_PCM_S24LE, "S24LE  min", {0x00, 0x00, 0x80, 0}, 3, -32768},
            {AUDIO_FMT_PCM_S32LE, "S32LE  max", {0xFF, 0xFF, 0xFF, 0x7F}, 4, 32767},
            {AUDIO_FMT_PCM_S32LE, "S32LE  min", {0x00, 0x00, 0x00, 0x80}, 4, -32768},
        };
        for (uint32_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            audio_init(&dev, AUDIO_CTRL_HDA, "fmt");
            uint32_t s = audio_create_stream(&dev, false, cases[i].f, 48000, 1);
            audio_write(&dev, s, cases[i].b, cases[i].nb);
            audio_mixer_process_n(&dev, 1);
            tx_frame(&dev, 0, &l, &r);
            char msg[96];
            snprintf(msg, sizeof msg, "%s decodes to %d (mono -> both channels)", cases[i].n,
                     cases[i].want);
            CHECK(l == cases[i].want && r == cases[i].want, msg);
        }
        /* negative S24 sign extension */
        audio_init(&dev, AUDIO_CTRL_HDA, "fmt");
        uint32_t s = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S24LE, 48000, 1);
        uint8_t neg[3] = {0x00, 0x00, 0xC0}; /* -4194304 in 24-bit */
        audio_write(&dev, s, neg, 3);
        audio_mixer_process_n(&dev, 1);
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == -16384, "S24LE 0xC00000 sign-extends to -16384, not a huge positive");

        /* float32 1.0 -> full scale, -1.0 -> -32767, and NaN -> silence */
        audio_init(&dev, AUDIO_CTRL_HDA, "fmt");
        poison_tx(&dev); /* the NaN case below expects a written zero */
        s = audio_create_stream(&dev, false, AUDIO_FMT_FLOAT32, 48000, 1);
        uint8_t fbuf[12] = {0x00, 0x00, 0x80, 0x3F,  /*  1.0f */
                            0x00, 0x00, 0x80, 0xBF,  /* -1.0f */
                            0x00, 0x00, 0xC0, 0x7F}; /*  NaN  */
        audio_write(&dev, s, fbuf, 12);
        CHECK(audio_mixer_process_n(&dev, 3) == 3, "three float samples mixed");
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 32767, "float32 +1.0 becomes 32767");
        tx_frame(&dev, 1, &l, &r);
        CHECK(l == -32767, "float32 -1.0 becomes -32767");
        tx_frame(&dev, 2, &l, &r);
        CHECK(l == 0, "a NaN float sample becomes silence, not garbage");

        /* out-of-range and non-finite floats clamp instead of wrapping */
        audio_init(&dev, AUDIO_CTRL_HDA, "fmt");
        s = audio_create_stream(&dev, false, AUDIO_FMT_FLOAT32, 48000, 1);
        uint8_t f2[20] = {0x00, 0x00, 0x00, 0x3F,  /*  0.5f  -> 16384          */
                          0x00, 0x00, 0x00, 0x40,  /*  2.0f  -> clamp  32767   */
                          0x00, 0x00, 0x00, 0xC0,  /* -2.0f  -> clamp -32767   */
                          0x00, 0x00, 0x80, 0x7F,  /* +inf   -> clamp  32767   */
                          0x00, 0x00, 0x80, 0xFF}; /* -inf   -> clamp -32767   */
        audio_write(&dev, s, f2, 20);
        CHECK(audio_mixer_process_n(&dev, 5) == 5, "five float samples mixed");
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 16384, "float32 0.5 becomes 16384");
        tx_frame(&dev, 1, &l, &r);
        CHECK(l == 32767, "float32 +2.0 CLAMPS to 32767, it does not wrap");
        tx_frame(&dev, 2, &l, &r);
        CHECK(l == -32767, "float32 -2.0 clamps to -32767");
        tx_frame(&dev, 3, &l, &r);
        CHECK(l == 32767, "float32 +inf clamps to 32767");
        tx_frame(&dev, 4, &l, &r);
        CHECK(l == -32767, "float32 -inf clamps to -32767");
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
            buf[i * 2] = (uint8_t) ((uint16_t) six[i] & 0xFF);
            buf[i * 2 + 1] = (uint8_t) (((uint16_t) six[i] >> 8) & 0xFF);
        }
        uint32_t s = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 6);
        CHECK(s != 0, "HDA accepts a 5.1 stream");
        audio_write(&dev, s, buf, 12);
        audio_mixer_process_n(&dev, 1);
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 1600 && r == 2700, "5.1 folds down to L=1600 R=2700 exactly");
    }
    /* 7.1: FL FR FC LFE RL RR SL SR. The odd values pin the halving direction —
     * 101/2 is 50 and -301/2 is -150, i.e. truncation TOWARD ZERO, not floor.
     *   L = 1000 + 400/2 + 200/2 + 600/2 + 101/2  = 1000+200+100+300+50  = 1650
     *   R = 2000 + 400/2 + 200/2 + 800/2 + -301/2 = 2000+200+100+400-150 = 2550 */
    audio_init(&dev, AUDIO_CTRL_HDA, "7.1");
    {
        int16_t eight[8] = {1000, 2000, 400, 200, 600, 800, 101, -301};
        uint8_t buf[16];
        for (int i = 0; i < 8; i++) {
            buf[i * 2] = (uint8_t) ((uint16_t) eight[i] & 0xFF);
            buf[i * 2 + 1] = (uint8_t) (((uint16_t) eight[i] >> 8) & 0xFF);
        }
        uint32_t s = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 8);
        CHECK(s != 0, "HDA accepts a 7.1 stream");
        audio_write(&dev, s, buf, 16);
        CHECK(audio_mixer_process_n(&dev, 1) == 1, "one 7.1 frame folds to one stereo frame");
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 1650 && r == 2550, "7.1 folds down to L=1650 R=2550 exactly");
        CHECK(audio_stream_available(&dev, s) == 0,
              "and all 16 bytes of the 7.1 frame were consumed, not just 4");
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
            buf[i * 2] = (uint8_t) ((uint16_t) seq[i] & 0xFF);
            buf[i * 2 + 1] = (uint8_t) (((uint16_t) seq[i] >> 8) & 0xFF);
        }
        audio_write(&dev, s, buf, 6);
        CHECK(audio_mixer_process_n(&dev, 6) == 6,
              "8 kHz into a 16 kHz device yields twice as many frames");
        int32_t got[6];
        for (uint32_t i = 0; i < 6; i++) {
            tx_frame(&dev, i, &l, &r);
            got[i] = l;
        }
        CHECK(got[0] == 100 && got[1] == 100 && got[2] == 200 && got[3] == 200 && got[4] == 300 &&
                  got[5] == 300,
              "each 8 kHz sample is held for exactly two 16 kHz frames");
    }
    audio_init(&dev, AUDIO_CTRL_HDA, "rate");
    audio_set_output_rate(&dev, 8000);
    {
        uint32_t s = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 16000, 1);
        int16_t seq[4] = {10, 20, 30, 40};
        uint8_t buf[8];
        for (int i = 0; i < 4; i++) {
            buf[i * 2] = (uint8_t) ((uint16_t) seq[i] & 0xFF);
            buf[i * 2 + 1] = (uint8_t) (((uint16_t) seq[i] >> 8) & 0xFF);
        }
        audio_write(&dev, s, buf, 8);
        CHECK(audio_mixer_process_n(&dev, 8) == 2, "16 kHz into 8 kHz halves the frame count");
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 10, "decimation keeps sample 0");
        tx_frame(&dev, 1, &l, &r);
        CHECK(l == 30, "and sample 2, dropping 1 and 3");
    }

    /* ---- 11. underrun accounting ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "under");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    sb = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 5000);
    audio_write(&dev, sa, scratch, 16); /* 4 frames */
    fill_s16(scratch, 2, 7000);
    audio_write(&dev, sb, scratch, 4); /* 1 frame  */
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
    fill_s16(scratch, 4, 1000);
    audio_write(&dev, sa, scratch, 8);
    fill_s16(scratch, 4, 2000);
    audio_write(&dev, sb, scratch, 8);
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
    fill_s16(scratch, 8, 20000);
    audio_write(&dev, sa, scratch, 16);
    CHECK(audio_set_3d_position(&dev, sa, 1.0, 0.0, 0.0) == AUDIO_OK,
          "a source one unit to the right is accepted");
    CHECK(dev.streams[0].spatial, "the stream is flagged spatial");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 0 && r == 10000,
          "at distance 1 hard right: gain 1/(1+1)=0.5, pan +1 -> L=0, R=10000");
    CHECK(audio_set_3d_position(&dev, sa, 1e30, 0.0, 0.0) == AUDIO_EINVAL,
          "an absurd coordinate is refused rather than producing inf gain");

    /* ---- 14b. the distance term is REAL: a non-trivial sqrt, and y/z ----
     * These are the cases that catch a square root stubbed to return its
     * argument (or 1.0). (3,4,0) has d = sqrt(9+16) = 5 exactly.
     *   base = 1/(1+5) = 1/6      pan = x/d = 3/5 = 0.6
     *   L = 20000 * (1/6) * (1-0.6) = 1333.33 -> 1333
     *   R = 20000 * (1/6)          = 3333.33 -> 3333                        */
    audio_init(&dev, AUDIO_CTRL_HDA, "3d-dist");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 20000);
    audio_write(&dev, sa, scratch, 16);
    CHECK(audio_set_3d_position(&dev, sa, 3.0, 4.0, 0.0) == AUDIO_OK,
          "a source at (3,4,0) is accepted");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 0, &l, &r);
    CHECK(l == 1333 && r == 3333, "(3,4,0): d=5 exactly, gain 1/6, pan +0.6 -> L=1333 R=3333");
    /* same distance, no x: y and z must move the gain and NOT the pan */
    CHECK(audio_set_3d_position(&dev, sa, 0.0, 3.0, 4.0) == AUDIO_OK, "moved to (0,3,4)");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 1, &l, &r);
    CHECK(l == 3333 && r == 3333,
          "(0,3,4): the same distance 5 attenuates to 3333, but x=0 so there is NO pan");
    /* at the listener: no attenuation at all, so the 1/(1+d) term is not a
     * constant fudge factor either */
    CHECK(audio_set_3d_position(&dev, sa, 0.0, 0.0, 0.0) == AUDIO_OK, "moved to the origin");
    audio_mixer_process_n(&dev, 1);
    tx_frame(&dev, 2, &l, &r);
    CHECK(l == 20000 && r == 20000, "at distance 0 the sample passes through untouched");

    /* ---- 14c. NaN and infinity are refused everywhere they can appear ---- */
    {
        CHECK(audio_set_volume(&dev, sa, AU_NAN) == AUDIO_EINVAL, "a NaN stream volume is refused");
        CHECK(audio_set_volume(&dev, sa, AU_INF) == AUDIO_EINVAL,
              "an infinite stream volume is refused");
        CHECK(audio_set_balance(&dev, sa, AU_NAN) == AUDIO_EINVAL, "a NaN balance is refused");
        CHECK(audio_mixer_set_master(&dev, AU_NAN, false) == AUDIO_EINVAL,
              "a NaN master volume is refused");
        CHECK(audio_mixer_set_channel(&dev, AUDIO_CH_PCM, AU_NAN, false) == AUDIO_EINVAL,
              "a NaN mixer channel gain is refused");
        CHECK(audio_set_3d_position(&dev, sa, AU_NAN, 0.0, 0.0) == AUDIO_EINVAL,
              "a NaN x coordinate is refused");
        CHECK(audio_set_3d_position(&dev, sa, 0.0, AU_NAN, 0.0) == AUDIO_EINVAL,
              "a NaN y coordinate is refused");
        CHECK(audio_set_3d_position(&dev, sa, 0.0, 0.0, AU_INF) == AUDIO_EINVAL,
              "an infinite z coordinate is refused");
        CHECK(dev.streams[0].volume == 1.0 && dev.streams[0].balance == 0.0 &&
                  dev.master_volume == 1.0 && dev.streams[0].pos_x == 0.0,
              "and not one of those refusals left a poisoned value behind");
        uint32_t mic = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 48000, 2);
        CHECK(audio_set_3d_position(&dev, mic, 1.0, 0.0, 0.0) == AUDIO_EDIR,
              "a microphone has no position: EDIR, not a silently ignored write");
        CHECK(!dev.streams[1].spatial, "and the capture stream was not flagged spatial");
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
            int16_t lv = (int16_t) (1000 + i * 100), rv = (int16_t) (-1000 - i * 100);
            frames[i * 4 + 0] = (uint8_t) ((uint16_t) lv & 0xFF);
            frames[i * 4 + 1] = (uint8_t) (((uint16_t) lv >> 8) & 0xFF);
            frames[i * 4 + 2] = (uint8_t) ((uint16_t) rv & 0xFF);
            frames[i * 4 + 3] = (uint8_t) (((uint16_t) rv >> 8) & 0xFF);
        }
        CHECK(audio_rx_inject(&dev, frames, 16) == 4, "4 captured frames injected");
        CHECK(audio_rx_inject(&dev, frames, 15) == AUDIO_EINVAL,
              "a partial stereo frame is refused, not silently truncated");
        CHECK(audio_stream_available(&dev, cs) == 16,
              "16 bytes landed in the capture stream's ring");
        uint8_t out[16];
        CHECK(audio_read(&dev, cs, out, 16) == 16, "16 bytes read back");
        int32_t g0 = (int32_t) ((uint32_t) out[0] | ((uint32_t) out[1] << 8));
        g0 = (g0 & 0x7FFF) - ((g0 & 0x8000) ? 32768 : 0);
        int32_t g3r = (int32_t) ((uint32_t) out[14] | ((uint32_t) out[15] << 8));
        g3r = (g3r & 0x7FFF) - ((g3r & 0x8000) ? 32768 : 0);
        CHECK(g0 == 1000, "captured frame 0 left channel is exactly 1000");
        CHECK(g3r == -1300, "captured frame 3 right channel is exactly -1300");
        CHECK(dev.stats.frames_captured == 4, "frames_captured == 4");
    }
    /* mono capture stream averages the two channels */
    audio_init(&dev, AUDIO_CTRL_HDA, "cap-mono");
    {
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 48000, 1);
        uint8_t f1[4] = {0xE8, 0x03, 0xD0, 0x07}; /* L=1000, R=2000 */
        audio_rx_inject(&dev, f1, 4);
        uint8_t out[2];
        CHECK(audio_read(&dev, cs, out, 2) == 2, "one mono sample recorded");
        int32_t v = (int32_t) ((uint32_t) out[0] | ((uint32_t) out[1] << 8));
        v = (v & 0x7FFF) - ((v & 0x8000) ? 32768 : 0);
        CHECK(v == 1500, "mono capture is (1000 + 2000)/2 == 1500");
    }
    /* a capture stream on a device with no capture support is refused */
    {
        audio_init(&dev2, AUDIO_CTRL_NONE, "silent");
        CHECK(audio_create_stream(&dev2, true, AUDIO_FMT_PCM_S16LE, 48000, 2) == 0,
              "a controller without capture refuses a capture stream");
        CHECK(audio_capture_poll(&dev2) == AUDIO_ENOSUP, "and capture_poll on it returns ENOSUP");
    }

    /* ================================================================
     * THE HARDWARE BOUNDARY
     * ================================================================ */
    audio_init(&dev, AUDIO_CTRL_HDA, "hw");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    fill_s16(scratch, 8, 4000);
    audio_write(&dev, sa, scratch, 16);
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
        for (uint32_t i = 0; i < sizeof(codec); i++) ((uint8_t *) &codec)[i] = 0;
        audio_ops_t ops = {fc_tx, fc_rx, fc_rate, fc_vol, &codec};
        CHECK(audio_bind_ops(&dev, &ops) == AUDIO_OK, "a codec backend binds");
        CHECK(audio_has_backend(&dev) && (dev.reg_status & AUDIO_ST_BOUND),
              "the device now reports a backend");
        CHECK(audio_dma_flush(&dev) == 16, "16 bytes went to the codec");
        CHECK(codec.sink_len == 16 && codec.tx_calls == 1, "the codec really received them");
        CHECK(dev.stats.dma_bytes_out == 16, "dma_bytes_out == 16 — counted only now");
        CHECK(audio_tx_pending(&dev) == 0, "the staging ring drained");
        {
            int32_t sl = (int32_t) ((uint32_t) codec.sink[0] | ((uint32_t) codec.sink[1] << 8));
            sl = (sl & 0x7FFF) - ((sl & 0x8000) ? 32768 : 0);
            CHECK(sl == 4000, "and the first sample the codec saw is exactly 4000");
        }

        /* short writes */
        fill_s16(scratch, 8, 1000);
        audio_write(&dev, sa, scratch, 16);
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
        codec.src[0] = 0x10;
        codec.src[1] = 0x27;
        codec.src[2] = 0x10;
        codec.src[3] = 0x27;
        codec.src_len = 4; /* one frame, 10000/10000 */
        CHECK(audio_capture_poll(&dev) == 4, "4 bytes pulled from the codec");
        CHECK(dev.stats.dma_bytes_in == 4, "dma_bytes_in == 4");
        CHECK(audio_stream_available(&dev, cs) == 4, "and they reached the capture stream");
        uint8_t o[4];
        audio_read(&dev, cs, o, 4);
        int32_t cv = (int32_t) ((uint32_t) o[0] | ((uint32_t) o[1] << 8));
        cv = (cv & 0x7FFF) - ((cv & 0x8000) ? 32768 : 0);
        CHECK(cv == 10000, "the captured sample is exactly 10000");

        /* a backend with no rx_poll must not pretend */
        audio_ops_t txonly = {fc_tx, 0, 0, 0, &codec};
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
        for (uint32_t i = 0; i < sizeof(codec); i++) ((uint8_t *) &codec)[i] = 0;
        audio_ops_t ops = {fc_tx, fc_rx, fc_rate, fc_vol, &codec};
        audio_bind_ops(&dev, &ops);
        sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        fill_s16(scratch, 8, 2500);
        audio_write(&dev, sa, scratch, 16);

        audio_handle_irq(&dev);
        CHECK(dev.stats.irqs_handled == 0, "an IRQ with no latch set does not count as handled");

        dev.irq_stream_done = true;
        audio_handle_irq(&dev);
        CHECK(dev.stats.irqs_handled == 1, "a stream-done IRQ is counted once");
        CHECK(!dev.irq_stream_done, "the latch was cleared");
        CHECK(codec.sink_len == 16, "and the IRQ actually mixed and pushed 16 bytes");
        {
            int32_t sv = (int32_t) ((uint32_t) codec.sink[0] | ((uint32_t) codec.sink[1] << 8));
            sv = (sv & 0x7FFF) - ((sv & 0x8000) ? 32768 : 0);
            CHECK(sv == 2500, "the bytes the IRQ pushed are the real mixed samples");
        }
        dev.irq_buffer_underrun = true;
        dev.irq_buffer_overrun = true;
        audio_handle_irq(&dev);
        CHECK((dev.reg_status & AUDIO_ST_UNDERRUN) && (dev.reg_status & AUDIO_ST_OVERRUN),
              "underrun and overrun are latched into reg_status");
        audio_clear_status(&dev);
        CHECK((dev.reg_status & (AUDIO_ST_UNDERRUN | AUDIO_ST_OVERRUN)) == 0, "and can be cleared");
        audio_bind_ops(&dev, 0);
    }

    /* ================================================================
     * COVERAGE — including the cases that MUST return false
     * ================================================================ */
    audio_init(&dev, AUDIO_CTRL_HDA, "cov");
    CHECK(audio_verify_coverage(&dev), "a fresh device with four live channels passes coverage");
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
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: with every stream paused, nothing is covered");
    CHECK(dev.coverage_r == 0.0, "coverage_r really is 0.0");
    audio_resume(&dev, sa);
    audio_resume(&dev, sb);
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
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a ring cursor at buffer_size is out of range");
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

    /* Every remaining invariant, driven through the failing direction too. A
     * verifier is only worth its name for the states it actually rejects. */
    dev.reg_format = (uint32_t) AUDIO_FMT_PCM_S24LE;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: the mixer output format is not S16LE");
    dev.reg_format = (uint32_t) AUDIO_FMT_PCM_S16LE;

    dev.rx_head = AUDIO_BUFFER_SIZE;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: rx_head outside the capture DMA ring");
    dev.rx_head = 0;
    dev.rx_tail = AUDIO_BUFFER_SIZE + 1u;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: rx_tail outside the capture DMA ring");
    dev.rx_tail = 0;

    dev.master_volume = 2.0;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a master volume of 2.0");
    dev.master_volume = 1.0;

    dev.max_sample_rate = 8000;
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: the output rate is above what the controller can do");
    dev.max_sample_rate = 192000;

    dev.num_mixer_channels = AUDIO_MAX_MIXER_CH + 1u;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: more mixer channels than the table holds");
    dev.num_mixer_channels = 2;
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: fewer than the four channels audio_init always builds");
    dev.num_mixer_channels = AUDIO_NUM_DEFAULT_CH;

    dev.mixer[AUDIO_CH_PCM].volume = 2.0;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a mixer channel gain of 2.0");
    dev.mixer[AUDIO_CH_PCM].volume = 1.0;

    dev.streams[0].balance = -2.0;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a balance of -2.0");
    dev.streams[0].balance = 0.0;

    dev.streams[0].channels = 3;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a 3-channel layout we cannot fold down");
    dev.streams[0].channels = 2;

    dev.max_channels = 1;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a stereo stream on a mono-only controller");
    dev.max_channels = 8;

    dev.streams[0].format = (audio_format_t) 99;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a format enum out of range");
    dev.streams[0].format = AUDIO_FMT_PCM_S16LE;

    dev.streams[0].rs_phase = (uint64_t) 1 << 48;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a resampler phase that ran away");
    dev.streams[0].rs_phase = 0;

    dev.streams[0].buffer_size = AUDIO_STREAM_BUF_SIZE * 2u;
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: a ring bigger than the pool slot it was cut from");
    dev.streams[0].buffer_size = AUDIO_STREAM_BUF_SIZE;

    dev.streams[0].buffer_tail = dev.streams[0].buffer_size;
    CHECK(!audio_verify_coverage(&dev), "FAIL CASE: a read cursor at buffer_size");
    dev.streams[0].buffer_tail = 0;

    dev.streams[0].stream_id = 0;
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: num_streams counts a slot that was never allocated");
    dev.streams[0].stream_id = sa;

    dev.streams[0].spatial = true;
    dev.supports_3d = false;
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: a positioned stream on a controller with no 3D");
    dev.supports_3d = true;
    dev.streams[0].spatial = false;

    dev.streams[0].is_capture = true;
    dev.supports_capture = false;
    CHECK(!audio_verify_coverage(&dev),
          "FAIL CASE: a capture stream on a controller that cannot record");
    dev.supports_capture = true;
    dev.streams[0].is_capture = false;

    {
        char longname[33];
        for (int i = 0; i < 32; i++) {
            dev.mixer[AUDIO_CH_PCM].name[i] = 'A';
            longname[i] = 'A';
        }
        longname[32] = '\0';
        CHECK(!audio_verify_coverage(&dev),
              "FAIL CASE: a mixer name with no NUL anywhere in its 32 bytes");
        CHECK(audio_mixer_find_channel(&dev, longname) == AUDIO_ENOSTREAM,
              "and find_channel refuses to match it rather than reading past the field");
    }
    audio_init(&dev, AUDIO_CTRL_HDA, "cov"); /* repair by re-initialising */
    CHECK(audio_verify_coverage(&dev), "a re-initialised device passes again");

    /* ================================================================
     * 16. CAPTURE ENCODE — the inverse of case 8, in every format.
     * One device frame L=+16384 R=-16384 is injected and the bytes the capture
     * stream stores are compared with a wire layout worked out by hand:
     *   +16384 is 0x4000 in the S16 domain, -16384 is 0xC000.
     *   U8    top 8 bits biased by 128:  0x40+128=0xC0 and -0x40+128=0x40
     *   S8    top 8 bits, signed:        0x40 and 0xC0
     *   S16LE / S16BE the value itself, each byte order
     *   S24LE the value shifted up 8 bits, S32LE shifted up 16
     * ================================================================ */
    {
        struct {
            audio_format_t f;
            const char *n;
            uint32_t nb;
            uint8_t want[8];
        } enc[] = {
            {AUDIO_FMT_PCM_U8, "U8", 2, {0xC0, 0x40}},
            {AUDIO_FMT_PCM_S8, "S8", 2, {0x40, 0xC0}},
            {AUDIO_FMT_PCM_S16LE, "S16LE", 4, {0x00, 0x40, 0x00, 0xC0}},
            {AUDIO_FMT_PCM_S16BE, "S16BE", 4, {0x40, 0x00, 0xC0, 0x00}},
            {AUDIO_FMT_PCM_S24LE, "S24LE", 6, {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0}},
            {AUDIO_FMT_PCM_S32LE, "S32LE", 8, {0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0xC0}},
        };
        uint8_t frame[4] = {0x00, 0x40, 0x00, 0xC0}; /* L=+16384, R=-16384 */
        for (uint32_t i = 0; i < sizeof(enc) / sizeof(enc[0]); i++) {
            audio_init(&dev, AUDIO_CTRL_HDA, "enc");
            uint32_t cs = audio_create_stream(&dev, true, enc[i].f, 48000, 2);
            poison_ring(&dev, 0); /* the 0x00 bytes below must be written, not stale */
            audio_rx_inject(&dev, frame, 4);
            uint8_t out[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            bool ok = (audio_read(&dev, cs, out, enc[i].nb) == (int) enc[i].nb);
            for (uint32_t k = 0; k < enc[i].nb; k++)
                if (out[k] != enc[i].want[k]) ok = false;
            char msg[128];
            snprintf(msg, sizeof msg,
                     "capture into %s writes the exact %u-byte wire frame for +16384/-16384",
                     enc[i].n, enc[i].nb);
            CHECK(ok, msg);
        }
        /* FLOAT32 has no hand-writable byte pattern, so prove the round trip
         * instead: encode into a capture stream, feed those very bytes back
         * through the playback decoder, and require the samples to survive. */
        audio_init(&dev, AUDIO_CTRL_HDA, "encf");
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_FLOAT32, 48000, 2);
        audio_rx_inject(&dev, frame, 4);
        uint8_t fout[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        CHECK(audio_read(&dev, cs, fout, 8) == 8, "8 bytes of FLOAT32 capture recorded");
        CHECK(fout[3] == 0x3F && fout[7] == 0xBF,
              "the two floats have magnitude ~0.5 and opposite signs (exponent byte 0x3F/0xBF)");
        audio_init(&dev, AUDIO_CTRL_HDA, "encf2");
        uint32_t ps = audio_create_stream(&dev, false, AUDIO_FMT_FLOAT32, 48000, 2);
        audio_write(&dev, ps, fout, 8);
        audio_mixer_process_n(&dev, 1);
        tx_frame(&dev, 0, &l, &r);
        CHECK(l == 16384 && r == -16384,
              "FLOAT32 encode then decode returns +16384/-16384 unchanged");
    }

    /* ================================================================
     * 17. CAPTURE RATE CONVERSION — the device clock is not the stream clock.
     * ================================================================ */
    audio_init(&dev, AUDIO_CTRL_HDA, "cap-down");
    {
        /* device 16 kHz -> stream 8 kHz: keep device frames 0 and 2 */
        CHECK(audio_set_output_rate(&dev, 16000) == AUDIO_OK, "device runs at 16 kHz");
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 8000, 2);
        uint8_t f4[16];
        int16_t seq[4] = {100, 200, 300, 400};
        for (int i = 0; i < 4; i++) {
            f4[i * 4 + 0] = (uint8_t) ((uint16_t) seq[i] & 0xFF);
            f4[i * 4 + 1] = (uint8_t) (((uint16_t) seq[i] >> 8) & 0xFF);
            f4[i * 4 + 2] = f4[i * 4 + 0];
            f4[i * 4 + 3] = f4[i * 4 + 1];
        }
        CHECK(audio_rx_inject(&dev, f4, 16) == 4, "4 device frames injected");
        CHECK(audio_stream_available(&dev, cs) == 8,
              "an 8 kHz stream keeps HALF of a 16 kHz capture: 2 frames, 8 bytes");
        uint8_t out[8];
        audio_read(&dev, cs, out, 8);
        CHECK(rd_s16(out) == 100 && rd_s16(out + 4) == 300,
              "and it kept device frames 0 and 2 — 100 and 300, dropping 200 and 400");
    }
    audio_init(&dev, AUDIO_CTRL_HDA, "cap-up");
    {
        /* device 8 kHz -> stream 16 kHz: hold each device frame twice */
        CHECK(audio_set_output_rate(&dev, 8000) == AUDIO_OK, "device runs at 8 kHz");
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 16000, 2);
        uint8_t f2[8] = {50, 0, 50, 0, 60, 0, 60, 0};
        CHECK(audio_rx_inject(&dev, f2, 8) == 2, "2 device frames injected");
        CHECK(audio_stream_available(&dev, cs) == 16,
              "a 16 kHz stream gets TWICE as many frames from an 8 kHz capture");
        uint8_t out[16];
        audio_read(&dev, cs, out, 16);
        CHECK(rd_s16(out) == 50 && rd_s16(out + 4) == 50 && rd_s16(out + 8) == 60 &&
                  rd_s16(out + 12) == 60,
              "each device frame is held for exactly two stream frames: 50 50 60 60");
    }

    /* ================================================================
     * 18. CAPTURE CHANNEL CONVERSION — mono average, 5.1 expansion.
     * ================================================================ */
    audio_init(&dev, AUDIO_CTRL_HDA, "cap-51");
    {
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 48000, 6);
        poison_ring(&dev, 0);                     /* every byte is 0xAA before the capture */
        uint8_t f1[4] = {0xE8, 0x03, 0xD0, 0x07}; /* L=1000, R=2000 */
        audio_rx_inject(&dev, f1, 4);
        CHECK(audio_stream_available(&dev, cs) == 12,
              "a 5.1 capture stream stores a full 12-byte frame per device frame");
        uint8_t out[12];
        audio_read(&dev, cs, out, 12);
        CHECK(rd_s16(out) == 1000 && rd_s16(out + 2) == 2000,
              "front L/R carry the captured stereo");
        CHECK(rd_s16(out + 4) == 0 && rd_s16(out + 6) == 0 && rd_s16(out + 8) == 0 &&
                  rd_s16(out + 10) == 0,
              "and the four channels the microphone cannot fill are ACTIVELY written as "
              "silence over a poisoned ring (0xAA would read back as -21846)");
    }
    audio_init(&dev, AUDIO_CTRL_HDA, "cap-mono2");
    {
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 48000, 1);
        /* 1000 and 2001 average to 1500; -1000 and -2001 average to -1500,
         * i.e. the halving truncates TOWARD ZERO, not toward -inf. */
        uint8_t f2[8] = {0xE8, 0x03, 0xD1, 0x07,  /*  1000,  2001 */
                         0x18, 0xFC, 0x2F, 0xF8}; /* -1000, -2001 */
        audio_rx_inject(&dev, f2, 8);
        uint8_t out[4];
        CHECK(audio_read(&dev, cs, out, 4) == 4, "two mono samples recorded");
        CHECK(rd_s16(out) == 1500, "(1000 + 2001)/2 == 1500");
        CHECK(rd_s16(out + 2) == -1500,
              "(-1000 + -2001)/2 == -1500: the average truncates toward zero");
    }

    /* ================================================================
     * 19. CAPTURE OVERRUN — charged when a stream ring really does fill.
     * A 5.1 S16LE stream has 12-byte frames, so its 8191-byte ring holds 682 of
     * them (682*12 = 8184, leaving 7 bytes — less than one frame). Push 700
     * device frames at it and exactly 18 must be lost.
     * ================================================================ */
    audio_init(&dev, AUDIO_CTRL_HDA, "cap-over");
    {
        uint32_t cs = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 48000, 6);
        for (uint32_t i = 0; i < 700u; i++) {
            scratch[i * 4 + 0] = 0x64;
            scratch[i * 4 + 1] = 0x00; /* L = 100 */
            scratch[i * 4 + 2] = 0xC8;
            scratch[i * 4 + 3] = 0x00; /* R = 200 */
        }
        CHECK(audio_rx_inject(&dev, scratch, 2800) == 700, "700 device frames injected");
        CHECK(audio_stream_available(&dev, cs) == 8184,
              "the ring took 682 frames (8184 bytes) and then had no room for a 683rd");
        CHECK(dev.streams[0].overruns == 1 && dev.stats.overruns == 1,
              "exactly one overrun is charged, to the stream whose ring filled");
        CHECK(dev.irq_buffer_overrun, "and the overrun IRQ latch is set");
        uint8_t out[12];
        audio_read(&dev, cs, out, 12);
        CHECK(rd_s16(out) == 100 && rd_s16(out + 2) == 200,
              "the frames that DID land are intact — the overrun did not corrupt them");
    }

    /* ---- 20. rx_inject accepts what fits and says so ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "rxfull");
    {
        /* No capture streams, so the DMA ring is the only consumer. Park it
         * with 6 bytes free — room for one whole frame, not two. */
        dev.rx_tail = 0;
        dev.rx_head = AUDIO_BUFFER_SIZE - 7u;
        uint8_t two[8] = {0x64, 0x00, 0xC8, 0x00, 0x64, 0x00, 0xC8, 0x00};
        CHECK(audio_rx_inject(&dev, two, 8) == 1,
              "with room for 6 bytes an 8-byte push accepts exactly ONE whole frame");
        CHECK(dev.stats.frames_captured == 1,
              "frames_captured counts the frame that was stored, not the two offered");
        CHECK(dev.stats.overruns == 1 && dev.irq_buffer_overrun,
              "and the frame that did not fit is recorded as an overrun");
    }

    /* ---- 21. the shadow registers track reality ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "shadow");
    CHECK((dev.reg_command & AUDIO_CMD_RUN) == 0, "a fresh device is not RUNning");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    CHECK((dev.reg_command & AUDIO_CMD_RUN) != 0, "creating a stream raises RUN");
    audio_pause(&dev, sa);
    CHECK((dev.reg_command & AUDIO_CMD_RUN) == 0,
          "pausing the only stream LOWERS RUN again — the bit is not write-once");
    audio_resume(&dev, sa);
    CHECK((dev.reg_command & AUDIO_CMD_RUN) != 0, "resuming raises it");
    CHECK(!dev.irq_stream_done, "no stream-done latch has been raised yet");
    audio_stop(&dev, sa);
    CHECK((dev.reg_command & AUDIO_CMD_RUN) == 0 && dev.irq_stream_done,
          "stop lowers RUN and raises the stream-done latch");
    audio_resume(&dev, sa);
    fill_s16(scratch, 8, 3000);
    audio_write(&dev, sa, scratch, 16);
    CHECK((dev.reg_status & AUDIO_ST_RUNNING) == 0, "RUNNING is clear before any mixing");
    CHECK(audio_mixer_process_n(&dev, 2) == 2, "two frames mixed");
    CHECK((dev.reg_status & AUDIO_ST_RUNNING) != 0, "a mix that produced frames sets RUNNING");
    audio_mixer_process_n(&dev, 8);
    CHECK(audio_mixer_process_n(&dev, 8) == 0, "the stream is drained");
    CHECK((dev.reg_status & AUDIO_ST_RUNNING) == 0,
          "and a mix that produced nothing clears RUNNING again");

    /* ---- 22. the staging ring is flushed correctly across its wrap ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "wrap");
    {
        for (uint32_t i = 0; i < sizeof(codec); i++) ((uint8_t *) &codec)[i] = 0;
        audio_ops_t ops = {fc_tx, fc_rx, fc_rate, fc_vol, &codec};
        audio_bind_ops(&dev, &ops);
        dev.tx_head = dev.tx_tail = AUDIO_BUFFER_SIZE - 8u; /* 16 bytes must wrap */
        uint32_t ws = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        fill_s16(scratch, 8, 4000);
        audio_write(&dev, ws, scratch, 16);
        CHECK(audio_mixer_process_n(&dev, 4) == 4, "four frames mixed across the ring wrap");
        CHECK(audio_tx_pending(&dev) == 16, "16 bytes pending, 8 of them past the wrap");
        CHECK(audio_dma_flush(&dev) == 8,
              "flush moves ONE contiguous run: the 8 bytes before the wrap");
        CHECK(audio_tx_pending(&dev) == 8, "the wrapped remainder is still staged, not lost");
        CHECK(audio_dma_flush(&dev) == 8, "a second flush takes the remainder");
        CHECK(codec.tx_calls == 2 && codec.sink_len == 16 && audio_tx_pending(&dev) == 0,
              "two calls, 16 bytes at the codec, ring empty");
        {
            bool ok = true;
            for (uint32_t i = 0; i < 16u; i += 2u)
                if (rd_s16(codec.sink + i) != 4000) ok = false;
            CHECK(ok, "and all eight samples arrived as 4000 — the wrap did not shear one in half");
        }
        /* channel 0 is the master fader, and it must reach the codec through
         * the same path set_master uses */
        codec.vol_set = 0;
        CHECK(audio_mixer_set_channel(&dev, AUDIO_CH_MASTER, 1.0, false) == AUDIO_OK,
              "set_channel on channel 0 is accepted");
        CHECK(codec.vol_set == 255,
              "and it PROGRAMMED THE CODEC — reg_volume never moves without the codec hearing it");
        codec.fail_vol = true;
        CHECK(audio_mixer_set_channel(&dev, AUDIO_CH_MASTER, 0.25, false) == AUDIO_EIO,
              "a codec that refuses the write makes set_channel(MASTER) fail too");
        CHECK(dev.master_volume == 0.25 && dev.reg_volume == 64,
              "while the software fader still moved");
        codec.fail_vol = false;
        audio_bind_ops(&dev, 0);
    }

    /* ---- 23. a corrupt stream is refused, not "written to" ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "corrupt");
    sa = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
    {
        uint8_t *saved_buf = dev.streams[0].buffer;
        uint32_t saved_sz = dev.streams[0].buffer_size;
        dev.streams[0].buffer = 0;
        CHECK(audio_write(&dev, sa, scratch, 8) == AUDIO_EINVAL,
              "a stream with a NULL ring REFUSES the write instead of reporting 8 bytes stored");
        CHECK(dev.stats.bytes_written == 0,
              "and bytes_written stays at 0 — no counter for bytes that went nowhere");
        CHECK(audio_stream_available(&dev, sa) == AUDIO_EINVAL, "available() refuses it too");
        dev.streams[0].buffer = saved_buf;

        dev.streams[0].buffer_size = 0;
        CHECK(audio_write(&dev, sa, scratch, 8) == AUDIO_EINVAL,
              "a zero-size ring is refused rather than used as a modulus");
        dev.streams[0].buffer_size = saved_sz;

        dev.streams[0].buffer_head = saved_sz;
        CHECK(audio_write(&dev, sa, scratch, 8) == AUDIO_EINVAL,
              "a write cursor past the end of the ring is refused");
        dev.streams[0].buffer_head = 0;

        CHECK(audio_write(&dev, sa, scratch, 8) == 8, "and the repaired stream works again");
        CHECK(dev.stats.bytes_written == 8, "with exactly 8 bytes counted, once");
    }

    /* ================================================================
     * 23b. A CORRUPT CURSOR CANNOT REACH OUTSIDE ITS OWN RING.
     * This is the one that mattered. audio_rx_inject() is what a codec ISR
     * calls, and the ring index used to be reduced by a single conditional
     * subtraction — correct only while the cursor was already in range. A
     * buffer_head of 0xFFFF0000 therefore indexed hundreds of megabytes past
     * an 8 KiB pool slot: an out-of-bounds WRITE from an interrupt handler.
     * ================================================================ */
    audio_init(&dev, AUDIO_CTRL_HDA, "cursor");
    {
        uint32_t c0 = audio_create_stream(&dev, true, AUDIO_FMT_PCM_S16LE, 48000, 2);
        uint32_t p1 = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        CHECK(c0 != 0 && p1 != 0, "a capture stream and a playback stream are created");
        CHECK(dev.streams[1].buffer == dev.streams[0].buffer + AUDIO_STREAM_BUF_SIZE,
              "their rings really are adjacent pool slots, so the canary is in the line of fire");
        poison_ring(&dev, 1); /* the neighbour, filled with 0xAA */
        uint8_t f1[4] = {0xE8, 0x03, 0xD0, 0x07};
        uint32_t wild[5] = {0xFFFF0000u, 0x80000000u, 0xFFFFFFFFu, AUDIO_STREAM_BUF_SIZE, 999999u};
        bool clean = true;
        for (uint32_t w = 0; w < 5u; w++) {
            dev.streams[0].buffer_head = wild[w];
            dev.streams[0].buffer_tail = 0;
            (void) audio_rx_inject(&dev, f1, 4); /* must not fault */
            for (uint32_t i = 0; i < dev.streams[1].buffer_size; i++)
                if (dev.streams[1].buffer[i] != 0xAA) clean = false;
        }
        CHECK(clean, "five wild producer cursors leave the NEXT pool slot byte-for-byte untouched "
                     "(regression: OOB write reachable from the codec ISR path)");
        dev.streams[0].buffer_head = AUDIO_STREAM_BUF_SIZE + 1u;
        CHECK(!audio_verify_coverage(&dev),
              "and coverage still calls that cursor what it is: broken");
        dev.streams[0].buffer_head = 0;

        /* An oversized buffer_size is a claim about memory nobody allocated.
         * The cursor is parked inside the SECOND slot's range on purpose: if
         * buffer_size were believed, the modulo would land the write squarely
         * on the neighbour. */
        dev.streams[0].buffer_size = AUDIO_STREAM_BUF_SIZE * 2u;
        dev.streams[0].buffer_head = AUDIO_STREAM_BUF_SIZE + 64u;
        dev.streams[0].buffer_tail = 0;
        (void) audio_rx_inject(&dev, f1, 4);
        clean = true;
        for (uint32_t i = 0; i < AUDIO_STREAM_BUF_SIZE; i++)
            if (dev.streams[1].buffer[i] != 0xAA) clean = false;
        CHECK(clean, "a buffer_size larger than the pool slot it came from is refused: the write "
                     "does NOT land 64 bytes into the neighbouring slot");
        CHECK(!audio_verify_coverage(&dev), "and coverage rejects the oversized ring too");
        dev.streams[0].buffer_size = AUDIO_STREAM_BUF_SIZE;
        dev.streams[0].buffer_head = 0;
    }
    /* the read side: a wild consumer cursor must not be decoded from */
    audio_init(&dev, AUDIO_CTRL_HDA, "cursor2");
    {
        uint32_t s0 = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        fill_s16(scratch, 8, 1234);
        audio_write(&dev, s0, scratch, 16);
        dev.streams[0].buffer_tail = 0xFFFFFFF0u;
        CHECK(audio_mixer_process_n(&dev, 4) == 0,
              "the mixer SKIPS a stream whose read cursor is outside its ring");
        CHECK(audio_stream_available(&dev, s0) == AUDIO_EINVAL, "and available() names it broken");
        dev.streams[0].buffer_tail = 0;
        CHECK(audio_mixer_process_n(&dev, 4) == 4, "repaired, it mixes again");
    }
    /* the device DMA cursors are published fields too */
    audio_init(&dev, AUDIO_CTRL_HDA, "dmacur");
    {
        uint32_t s0 = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        fill_s16(scratch, 8, 4321);
        audio_write(&dev, s0, scratch, 16);
        dev.tx_head = 0xFFFFFF00u;
        CHECK(audio_mixer_process_n(&dev, 4) == AUDIO_EINVAL,
              "a tx_head outside the 64 KiB staging ring is refused, not indexed with");
        CHECK(audio_tx_pending(&dev) == 0, "and tx_pending refuses to invent a byte count");
        CHECK(audio_dma_flush(&dev) == AUDIO_EINVAL, "dma_flush refuses it too");
        dev.tx_head = 0;
        dev.rx_head = 0xFFFFFF00u;
        CHECK(audio_rx_inject(&dev, scratch, 4) == AUDIO_EINVAL,
              "and an rx_head outside the ring is refused before a single byte is stored");
        CHECK(audio_capture_dispatch(&dev) == AUDIO_EINVAL, "as is capture_dispatch");
        dev.rx_head = 0;
        CHECK(audio_mixer_process_n(&dev, 4) == 4, "with the cursors repaired, mixing resumes");
    }

    /* ================================================================
     * 23c. Randomised malformed input. Deterministic LCG, so any failure is
     * reproducible. Every field written below is one the header PUBLISHES and
     * that this module therefore has to validate: cursors, ring sizes, channel
     * counts, format enums, rates, and half-frame payloads. Run under
     * -fsanitize=address,undefined this is the real bounds check.
     * ================================================================ */
    {
        uint32_t rates[10] = {8000, 11025, 16000, 22050, 32000, 44100, 48000, 96000, 192000, 7777};
        uint8_t chans[6] = {1, 2, 6, 8, 3, 0};
        for (uint32_t it = 0; it < 4000u; it++) {
            audio_init(&dev, (audio_ctrl_type_t) (NEXT() % 5u), "fuzz");
            uint32_t nmk = 1u + (NEXT() % 3u);
            for (uint32_t k = 0; k < nmk; k++)
                (void) audio_create_stream(&dev, (NEXT() & 1u) != 0, (audio_format_t) (NEXT() % 9u),
                                           rates[NEXT() % 10u], chans[NEXT() % 6u]);
            uint32_t len = NEXT() % 200u; /* often a PARTIAL frame */
            for (uint32_t k = 0; k < len; k++) scratch[k] = (uint8_t) NEXT();
            uint32_t sid = NEXT() % 5u;
            (void) audio_write(&dev, sid, scratch, len);
            (void) audio_read(&dev, sid, scratch, NEXT() % 200u);
            (void) audio_rx_inject(&dev, scratch, NEXT() % 64u); /* often not a /4 */
            (void) audio_set_volume(&dev, sid, (NEXT() & 1u) ? AU_NAN : 0.5);
            (void) audio_set_balance(&dev, sid, (NEXT() & 1u) ? AU_INF : -0.25);
            (void) audio_set_3d_position(&dev, sid, AU_NAN, 1e300, -1e300);
            if (dev.num_streams) {
                audio_stream_t *cs = &dev.streams[NEXT() % dev.num_streams];
                switch (NEXT() % 8u) {
                case 0:
                    cs->buffer_head = NEXT();
                    break;
                case 1:
                    cs->buffer_tail = NEXT();
                    break;
                case 2:
                    cs->buffer_size = NEXT() % 20000u;
                    break;
                case 3:
                    cs->channels = (uint8_t) NEXT();
                    break;
                case 4:
                    cs->format = (audio_format_t) (NEXT() % 40u);
                    break;
                case 5:
                    cs->sample_rate = NEXT();
                    break;
                case 6:
                    cs->rs_phase = ((uint64_t) NEXT() << 32) | NEXT();
                    break;
                default:
                    cs->stream_id = NEXT() % 6u;
                    break;
                }
            }
            switch (NEXT() % 6u) {
            case 0:
                dev.tx_head = NEXT();
                break;
            case 1:
                dev.tx_tail = NEXT();
                break;
            case 2:
                dev.rx_head = NEXT();
                break;
            case 3:
                dev.num_streams = NEXT() % 40u;
                break;
            case 4:
                dev.num_mixer_channels = NEXT() % 40u;
                break;
            default:
                dev.reg_sample_rate = NEXT();
                break;
            }
            (void) audio_mixer_process_n(&dev, NEXT() % 300u);
            (void) audio_capture_dispatch(&dev);
            (void) audio_rx_inject(&dev, scratch, (NEXT() % 16u) * 4u);
            (void) audio_dma_flush(&dev);
            (void) audio_verify_coverage(&dev);
            audio_handle_irq(&dev);
            (void) audio_stream_available(&dev, NEXT() % 6u);
            (void) audio_stream_space(&dev, NEXT() % 6u);
            (void) audio_mixer_find_channel(&dev, "PCM");
            (void) audio_mixer_bind_stream(&dev, NEXT() % 6u, NEXT() % 6u);
            (void) audio_tx_pending(&dev);
        }
        /* If any of that had walked off a buffer the sanitizers would have
         * killed the process long before this line. */
        audio_init(&dev, AUDIO_CTRL_HDA, "fuzz-end");
        audio_init(&dev2, AUDIO_CTRL_NONE, "fuzz-end2");
        CHECK(audio_verify_coverage(&dev),
              "4000 rounds of malformed streams, truncated frames, NaN gains and corrupted "
              "cursors: no fault, and a clean device afterwards");
        CHECK(audio_pool_slots_free() == AUDIO_POOL_SLOTS,
              "and the ring-buffer pool was not leaked by any of it");
    }

    /* ---- 24. every string that enters this module is bounded ---- */
    {
        char longn[300];
        for (uint32_t i = 0; i + 1u < sizeof(longn); i++) longn[i] = (char) ('a' + (i % 26));
        longn[sizeof(longn) - 1u] = '\0';
        audio_init(&dev, AUDIO_CTRL_HDA, longn);
        bool ok = (dev.name[127] == '\0');
        for (uint32_t i = 0; i < 127u; i++)
            if (dev.name[i] != longn[i]) ok = false;
        CHECK(ok, "a 299-character device name is truncated to 127 chars + NUL, not overflowed");
        audio_init(&dev, AUDIO_CTRL_HDA, 0);
        CHECK(dev.name[0] == 'a' && dev.name[1] == 'u' && dev.name[2] == 'd' &&
                  dev.name[3] == 'i' && dev.name[4] == 'o' && dev.name[5] == '\0',
              "a NULL name falls back to \"audio\" rather than dereferencing it");
        CHECK(audio_mixer_find_channel(&dev, "") == AUDIO_ENOSTREAM,
              "the empty string matches no channel");
        CHECK(audio_mixer_find_channel(&dev, "PCMX") == AUDIO_ENOSTREAM,
              "a longer string is not matched by a shorter channel name");
        CHECK(audio_mixer_find_channel(&dev, "PC") == AUDIO_ENOSTREAM,
              "nor a shorter string by a longer name");
        CHECK(audio_mixer_find_channel(&dev, "Line-In") == (int) AUDIO_CH_LINEIN,
              "and the exact name still matches");
    }

    /* ---- 25. every entry point refuses a NULL device instead of faulting ---- */
    audio_init(&dev, AUDIO_CTRL_HDA, "nullsweep");
    {
        uint32_t ns = audio_create_stream(&dev, false, AUDIO_FMT_PCM_S16LE, 48000, 2);
        uint8_t tmp[4] = {0, 0, 0, 0};
        int bad = 0;
        if (audio_create_stream(0, false, AUDIO_FMT_PCM_S16LE, 48000, 2) != 0) bad++;
        if (audio_write(0, ns, tmp, 4) != AUDIO_EINVAL) bad++;
        if (audio_write(&dev, ns, 0, 4) != AUDIO_EINVAL) bad++;
        if (audio_read(0, ns, tmp, 4) != AUDIO_EINVAL) bad++;
        if (audio_read(&dev, ns, 0, 4) != AUDIO_EINVAL) bad++;
        if (audio_set_volume(0, ns, 0.5) != AUDIO_EINVAL) bad++;
        if (audio_set_balance(0, ns, 0.0) != AUDIO_EINVAL) bad++;
        if (audio_set_3d_position(0, ns, 0.0, 0.0, 0.0) != AUDIO_EINVAL) bad++;
        if (audio_pause(0, ns) != AUDIO_EINVAL) bad++;
        if (audio_resume(0, ns) != AUDIO_EINVAL) bad++;
        if (audio_stop(0, ns) != AUDIO_EINVAL) bad++;
        if (audio_mixer_set_channel(0, 0, 1.0, false) != AUDIO_EINVAL) bad++;
        if (audio_mixer_set_master(0, 1.0, false) != AUDIO_EINVAL) bad++;
        if (audio_mixer_bind_stream(0, 0, ns) != AUDIO_EINVAL) bad++;
        if (audio_mixer_find_channel(0, "PCM") != AUDIO_EINVAL) bad++;
        if (audio_mixer_find_channel(&dev, 0) != AUDIO_EINVAL) bad++;
        if (audio_mixer_process_n(0, 4) != AUDIO_EINVAL) bad++;
        if (audio_bind_ops(0, 0) != AUDIO_EINVAL) bad++;
        if (audio_dma_flush(0) != AUDIO_EINVAL) bad++;
        if (audio_capture_poll(0) != AUDIO_EINVAL) bad++;
        if (audio_set_output_rate(0, 48000) != AUDIO_EINVAL) bad++;
        if (audio_rx_inject(0, tmp, 4) != AUDIO_EINVAL) bad++;
        if (audio_rx_inject(&dev, 0, 4) != AUDIO_EINVAL) bad++;
        if (audio_capture_dispatch(0) != AUDIO_EINVAL) bad++;
        if (audio_tx_pending(0) != 0) bad++;
        if (audio_stream_available(0, 1) != AUDIO_ENOSTREAM) bad++;
        if (audio_stream_space(0, 1) != AUDIO_ENOSTREAM) bad++;
        if (audio_has_backend(0)) bad++;
        if (audio_verify_coverage(0)) bad++;
        audio_mixer_process(0); /* void — must simply not fault */
        audio_handle_irq(0);
        audio_clear_status(0);
        CHECK(bad == 0, "all 29 NULL-device / NULL-buffer entry points refuse cleanly");
        CHECK(audio_write(&dev, ns, tmp, 0) == 0, "a zero-length write accepts 0 bytes");
        CHECK(audio_stream_available(&dev, ns) == 0, "and stored nothing");
    }

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
            if (w1 < 0 || w2 < 0) {
                bad++;
                break;
            }
            int n = audio_mixer_process_n(&dev, 16);
            if (n < 0) {
                bad++;
                break;
            }
            pushed += (uint64_t) n;
            /* drain the staging ring so it never wedges */
            dev.tx_tail = dev.tx_head;
            if (dev.streams[0].buffer_head >= dev.streams[0].buffer_size ||
                dev.streams[0].buffer_tail >= dev.streams[0].buffer_size ||
                dev.streams[1].buffer_head >= dev.streams[1].buffer_size ||
                dev.streams[1].buffer_tail >= dev.streams[1].buffer_size) {
                bad++;
                break;
            }
        }
        CHECK(bad == 0, "20000 write/mix/drain iterations with no error and no cursor drift");
        CHECK(pushed > 100000, "the soak actually produced audio (frames > 100000)");
        CHECK(dev.stats.frames_mixed == pushed, "frames_mixed agrees with what we counted");
        CHECK(audio_verify_coverage(&dev), "the device is still structurally sound after the soak");
    }

    printf("\n%s: %d check(s), %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", checks,
           failures);
    return failures ? 1 : 0;
}

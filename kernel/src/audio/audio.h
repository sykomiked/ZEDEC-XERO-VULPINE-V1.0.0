/* audio.h — ZEDEC XERO pqOS Audio Stream + Software Mixer
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 * 36N9 Genetics, LLC
 *
 * WHAT THIS IS
 * ------------
 * A real, portable software audio path: per-stream byte ring buffers, sample
 * decoding for seven PCM formats, per-stream gain / balance / distance
 * panning, a named mixer-channel matrix, zero-order-hold rate conversion, and
 * an N-into-2 mixer that sums in a wide accumulator and SATURATES once at the
 * output. Two loud streams clip at +/-32767; they never wrap to the opposite
 * sign. Every one of those steps is implemented here and is exercised by
 * test_audio.c with exact expected sample values.
 *
 * The only thing this file cannot do is move bytes to a codec. That is gated
 * behind audio_ops_t (see THE HARDWARE BOUNDARY below).
 *
 * ============================ LIMITATIONS ============================
 * Read this before believing anything the names above imply.
 *
 *  1. NO DRIVER. The controller-type enum (AC97 / HDA / USB / I2S) selects
 *     nothing but a capability profile (max rate, max channels, whether
 *     capture and 3D are offered). There is no AC97 register programming, no
 *     HDA CORB/RIRB, no USB audio class driver, no I2S clock setup anywhere in
 *     this tree. A real controller must be supplied through audio_bind_ops().
 *     Until it is, audio_dma_flush() and audio_capture_poll() return
 *     AUDIO_ENODEV and dev->stats.dma_bytes_out stays at zero. They do NOT
 *     return success.
 *
 *  2. MIXING IS DONE IN THE SIGNED 16-BIT DOMAIN. S24LE and S32LE inputs are
 *     right-shifted to 16 bits before mixing, so the bottom 8 / 16 bits of
 *     those formats are discarded. FLOAT32 is quantised to 16 bits. The mixer
 *     output is always S16LE stereo. This is a real precision loss, not a
 *     rounding detail.
 *
 *  3. RATE CONVERSION IS ZERO-ORDER HOLD (sample-and-hold on upsample, plain
 *     decimation on downsample). There is no anti-aliasing filter, so
 *     downsampling aliases audibly. It is exact and predictable — 8 kHz into a
 *     16 kHz device duplicates every sample exactly twice — and it is fine for
 *     system sounds. It is not a hi-fi resampler.
 *
 *  4. MULTICHANNEL INPUT IS DOWNMIXED TO STEREO with fixed 1.0 / 0.5
 *     coefficients (see audio_mixer_process). There is no ITU-R BS.775
 *     -3 dB centre attenuation and no surround decode. 5.1 and 7.1 streams
 *     play, but they play as a stereo fold-down.
 *
 *  5. 3D POSITIONING IS DISTANCE + LEFT/RIGHT PAN ONLY: gain 1/(1+d) and pan
 *     x/d. There is no HRTF, no ITD/IID, no elevation cue, no Doppler and no
 *     reverb. y and z affect only the distance term. Calling this "3D" is
 *     generous; it is what the code does.
 *
 *  6. THE IN-TREE SQUARE ROOT IS NOT CORRECTLY ROUNDED. It exists so host and
 *     target agree bit for bit without libm. It is exact on perfect squares and
 *     within 1 ULP everywhere else; roughly a quarter of arguments differ from
 *     libm's sqrt in the last bit. That cannot move a 16-bit sample, but do not
 *     lift it out of this file for anything that needs a real sqrt.
 *
 *  7. NO DYNAMIC ALLOCATION. Stream ring buffers come from a fixed static pool
 *     of AUDIO_POOL_SLOTS x AUDIO_STREAM_BUF_SIZE bytes shared by every
 *     audio_device_t in the system. When the pool is empty audio_create_stream
 *     returns 0. A slot is released only by audio_init() on its owning device;
 *     there is no per-stream destroy in this API.
 *
 *  8. THE MIXER CHANNEL SET IS FIXED. audio_init() creates exactly the four
 *     channels below and there is no API to add, rename or remove one. Only two
 *     of them (Master, PCM) are playback channels, so "route a stream to its own
 *     channel" means one of those two. AUDIO_MAX_MIXER_CH is headroom, not a
 *     feature.
 *
 *  9. NO MIDI, NO SYNTHESIS, NO CODEC (MP3/AAC/Vorbis), NO SRC DITHER, NO
 *     EQUALISER, NO COMPRESSOR. Nothing in this header claims those; this note
 *     exists so nobody infers them from the word "audio".
 * =====================================================================
 *
 * THE HARDWARE BOUNDARY
 * ---------------------
 * Same split as the virtio-net driver in this tree: all portable logic lives
 * here and is tested to the sample; the silicon-specific part is an ops struct
 * of function pointers a future driver fills in. When no ops are bound every
 * operation that would need silicon returns AUDIO_ENODEV, and no statistic is
 * incremented for work that did not happen.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "edp_risk.h"     /* m5_coords_t, surplus_real_t */

/* ===== Return codes. Zero or positive = real work; negative = refusal. ===== */
#define AUDIO_OK          0
#define AUDIO_EINVAL     (-1)   /* NULL pointer, out-of-range argument      */
#define AUDIO_ENOSTREAM  (-2)   /* no stream with that id on this device    */
#define AUDIO_ENODEV     (-3)   /* no hardware backend bound (audio_bind_ops)*/
#define AUDIO_EFORMAT    (-4)   /* format / rate / channel count unsupported */
#define AUDIO_EDIR       (-5)   /* wrong direction: read on playback, etc.  */
#define AUDIO_ENOSPC     (-6)   /* ring full, nothing accepted              */
#define AUDIO_EIO        (-7)   /* the bound backend reported a failure     */
#define AUDIO_ENOSUP     (-8)   /* device does not offer this capability    */

/* ===== Audio controller types (capability profile only — see LIMITATION 1) */
typedef enum {
    AUDIO_CTRL_AC97 = 0,
    AUDIO_CTRL_HDA,
    AUDIO_CTRL_USB_AUDIO,
    AUDIO_CTRL_I2S,
    AUDIO_CTRL_NONE,
} audio_ctrl_type_t;

/* ===== Sample formats ===== */
typedef enum {
    AUDIO_FMT_PCM_U8 = 0,
    AUDIO_FMT_PCM_S8,
    AUDIO_FMT_PCM_S16LE,
    AUDIO_FMT_PCM_S16BE,
    AUDIO_FMT_PCM_S24LE,
    AUDIO_FMT_PCM_S32LE,
    AUDIO_FMT_FLOAT32,
    AUDIO_FMT__COUNT
} audio_format_t;

/* ===== Audio stream ===== */
typedef struct {
    uint32_t stream_id;      /* 0 = slot never allocated                    */
    bool active;             /* true = playing/recording, false = paused    */
    bool is_capture;
    audio_format_t format;
    uint32_t sample_rate;    /* 8000, 11025, 16000, 22050, 32000, 44100,
                              * 48000, 96000, 192000                        */
    uint8_t channels;        /* 1=mono, 2=stereo, 6=5.1, 8=7.1              */
    uint8_t *buffer;         /* borrowed from the static pool; never freed  */
    uint32_t buffer_size;
    uint32_t buffer_head;    /* producer cursor (bytes)                     */
    uint32_t buffer_tail;    /* consumer cursor (bytes)                     */
    double volume;           /* 0.0 to 1.0                                  */
    double balance;          /* -1.0 (left) to 1.0 (right)                  */
    /* 3D positioning — distance + pan only, see LIMITATION 5 */
    double pos_x, pos_y, pos_z;
    bool spatial;

    /* --- added by the implementation --- */
    uint64_t rs_phase;       /* Q32.32 rate-conversion phase. Playback keeps
                              * only the fraction (< 2^32); capture may carry
                              * whole-frame decimation debt. Bounded < 2^48. */
    uint32_t underruns;      /* times the mixer wanted a frame and had none */
    uint32_t overruns;       /* capture frames dropped: this ring was full  */
} audio_stream_t;

#define AUDIO_MAX_STREAMS    16
#define AUDIO_BUFFER_SIZE    65536
#define AUDIO_MAX_MIXER_CH   32

/* One ring buffer per pool slot. AUDIO_POOL_SLOTS * AUDIO_STREAM_BUF_SIZE
 * bytes of .bss, shared by every device (LIMITATION 7). */
#define AUDIO_STREAM_BUF_SIZE 8192u
#define AUDIO_POOL_SLOTS      AUDIO_MAX_STREAMS

/* Largest block audio_mixer_process() will produce in one call, in stereo
 * frames. Bounds the stack scratch and the work done inside an IRQ. */
#define AUDIO_MIX_MAX_FRAMES  256u

/* Bytes the capture path pulls from the backend per audio_capture_poll(). */
#define AUDIO_RX_CHUNK        512u

/* Coverage floor for audio_verify_coverage(). See the comment on that
 * function: it is a liveness test and it is meant to be failable. */
#define AUDIO_COVERAGE_FLOOR  0.5

/* reg_status bits (sticky until audio_init or audio_clear_status) */
#define AUDIO_ST_RUNNING   0x1u
#define AUDIO_ST_UNDERRUN  0x2u
#define AUDIO_ST_OVERRUN   0x4u
#define AUDIO_ST_BOUND     0x8u
/* reg_command bits */
#define AUDIO_CMD_RUN      0x1u

/* ===== Mixer channel ===== */
typedef struct {
    char name[32];
    double volume;
    bool muted;
    bool is_capture;
    uint32_t source_stream;  /* 0 = applies to every stream of its direction
                              * that has no channel of its own              */
} audio_mixer_ch_t;

/* Fixed channel indices created by audio_init(). */
#define AUDIO_CH_MASTER  0u
#define AUDIO_CH_PCM     1u
#define AUDIO_CH_LINEIN  2u
#define AUDIO_CH_MIC     3u
#define AUDIO_NUM_DEFAULT_CH 4u

/* ===== Statistics — every counter here records work that ACTUALLY happened.
 * Nothing increments on a refused or hardware-less call. ===== */
typedef struct {
    uint64_t bytes_written;   /* payload bytes audio_write() accepted       */
    uint64_t bytes_read;      /* payload bytes audio_read() handed out      */
    uint64_t frames_mixed;    /* stereo frames audio_mixer_process produced */
    uint64_t frames_captured; /* stereo frames pushed in by audio_rx_inject */
    uint64_t dma_bytes_out;   /* bytes the BACKEND accepted (0 if unbound)  */
    uint64_t dma_bytes_in;    /* bytes the BACKEND delivered (0 if unbound) */
    uint32_t underruns;
    uint32_t overruns;
    uint32_t irqs_handled;    /* audio_handle_irq calls that had work to do */
} audio_stats_t;

/* ===== Hardware backend. NULL until audio_bind_ops(). =====
 * A real AC97/HDA/I2S driver implements these four and nothing else changes
 * in this file. Every callback may be NULL: a backend that can play but not
 * record leaves rx_poll NULL, and audio_capture_poll() then returns
 * AUDIO_ENOSUP rather than pretending. */
typedef struct audio_ops {
    /* Hand mixed S16LE stereo PCM at the device rate to the codec.
     * Returns bytes accepted (0..n, short writes are normal) or a negative
     * AUDIO_E* code. */
    int (*tx_submit)(void *ctx, const uint8_t *pcm, uint32_t n);
    /* Fetch captured S16LE stereo PCM at the device rate.
     * Returns bytes produced (0..cap) or a negative AUDIO_E* code. */
    int (*rx_poll)(void *ctx, uint8_t *pcm, uint32_t cap);
    /* Program the codec sample rate. 0 on success, negative on failure. */
    int (*set_rate)(void *ctx, uint32_t hz);
    /* Program codec master attenuation, 0..255 (255 = full scale). */
    int (*set_volume)(void *ctx, uint8_t level);
    void *ctx;
} audio_ops_t;

/* ===== Audio device ===== */
typedef struct {
    uint32_t device_id;
    char name[128];
    audio_ctrl_type_t ctrl_type;

    /* Shadow registers. These are kept truthful by the code below; they are
     * not decoration. reg_sample_rate IS the mixer output rate. */
    uint32_t reg_command;
    uint32_t reg_status;
    uint32_t reg_sample_rate;
    uint32_t reg_format;     /* always AUDIO_FMT_PCM_S16LE: the mixer output */
    uint32_t reg_volume;     /* master, 0..255                               */

    /* DMA staging rings (byte rings, reserved-slot convention) */
    uint8_t tx_dma[AUDIO_BUFFER_SIZE];
    uint8_t rx_dma[AUDIO_BUFFER_SIZE];
    uint32_t tx_head, tx_tail;
    uint32_t rx_head, rx_tail;

    /* IRQ latches */
    bool irq_buffer_underrun;
    bool irq_buffer_overrun;
    bool irq_stream_done;

    /* Streams */
    audio_stream_t streams[AUDIO_MAX_STREAMS];
    uint32_t num_streams;

    /* Mixer */
    audio_mixer_ch_t mixer[AUDIO_MAX_MIXER_CH];
    uint32_t num_mixer_channels;
    double master_volume;
    bool master_muted;

    /* Capabilities (derived from ctrl_type by audio_init) */
    uint32_t max_sample_rate;
    uint8_t max_channels;
    bool supports_capture;
    bool supports_3d;

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;

    /* --- added by the implementation --- */
    const audio_ops_t *ops;  /* NULL => no silicon; see LIMITATION 1 */
    audio_stats_t stats;
} audio_device_t;

/* ===== Lifecycle ===== */

/* Zeroes *dev, applies the capability profile for `type`, creates the four
 * default mixer channels and releases any pool slots this device held.
 * NULL-safe: audio_init(NULL, ...) is a no-op (the ARM32 boot path calls it
 * that way). */
void audio_init(audio_device_t *dev, audio_ctrl_type_t type, const char *name);

/* Returns a stream id >= 1, or 0 on refusal (bad format/rate/channels, no
 * capture support, stream table full, or pool exhausted). */
uint32_t audio_create_stream(audio_device_t *dev, bool capture, audio_format_t fmt,
                             uint32_t rate, uint8_t channels);

/* ===== Data path (no hardware needed) ===== */

/* Append up to `len` bytes to a PLAYBACK stream's ring. Returns the number of
 * bytes actually accepted, which may be less than len and may be 0 when the
 * ring is full. Negative on refusal. Short writes are not an error.
 * AUDIO_EINVAL if the stream's ring fields are inconsistent (NULL buffer, zero
 * size, cursor out of range) — it will not "accept" bytes into nowhere. */
int audio_write(audio_device_t *dev, uint32_t stream_id, const void *data, uint32_t len);

/* Drain up to `len` bytes from a CAPTURE stream's ring. Returns bytes copied
 * (may be 0), negative on refusal. Same AUDIO_EINVAL rule as audio_write. */
int audio_read(audio_device_t *dev, uint32_t stream_id, void *data, uint32_t len);

/* Both reject NaN and infinity: the range tests are written so that any
 * unordered comparison falls through to AUDIO_EINVAL. */
int audio_set_volume(audio_device_t *dev, uint32_t stream_id, double vol);
int audio_set_balance(audio_device_t *dev, uint32_t stream_id, double bal);
/* Requires dev->supports_3d, else AUDIO_ENOSUP. Rejects NaN and |coord| > 1e9.
 * Refuses a CAPTURE stream with AUDIO_EDIR — a microphone has no position. */
int audio_set_3d_position(audio_device_t *dev, uint32_t stream_id, double x, double y, double z);
int audio_pause(audio_device_t *dev, uint32_t stream_id);
int audio_resume(audio_device_t *dev, uint32_t stream_id);
/* Stops AND discards whatever is still buffered for that stream. The stream
 * id stays valid (there is no destroy in this API — LIMITATION 7). */
int audio_stop(audio_device_t *dev, uint32_t stream_id);

/* ===== Mixer ===== */
/* Set one mixer channel's gain and mute. ch == AUDIO_CH_MASTER is delegated
 * verbatim to audio_mixer_set_master() — channel 0 IS the master fader, and
 * there is exactly one path that moves it, so reg_volume can never claim an
 * attenuation the codec was never told about. That also means this call can
 * return AUDIO_EIO for ch 0 when a bound backend refuses the write. */
int audio_mixer_set_channel(audio_device_t *dev, uint32_t ch, double vol, bool mute);
/* Also mirrors into mixer[AUDIO_CH_MASTER] and reg_volume, and forwards to
 * ops->set_volume when a backend is bound (AUDIO_EIO if the backend refuses;
 * the software state is applied either way). */
int audio_mixer_set_master(audio_device_t *dev, double vol, bool mute);
/* Route one stream to one mixer channel. stream_id 0 clears the routing. */
int audio_mixer_bind_stream(audio_device_t *dev, uint32_t ch, uint32_t stream_id);
/* Index of the channel with this name, or AUDIO_ENOSTREAM if absent. */
int audio_mixer_find_channel(const audio_device_t *dev, const char *name);

/* Mix every runnable playback stream into tx_dma as S16LE stereo at
 * reg_sample_rate. Produces at most AUDIO_MIX_MAX_FRAMES frames. */
void audio_mixer_process(audio_device_t *dev);
/* Same, bounded: returns the number of stereo frames produced (0 is normal
 * and means "nothing had data"), or a negative AUDIO_E* code. max_frames of 0
 * or anything above AUDIO_MIX_MAX_FRAMES means AUDIO_MIX_MAX_FRAMES. */
int audio_mixer_process_n(audio_device_t *dev, uint32_t max_frames);

/* ===== Hardware-gated. Every one of these returns AUDIO_ENODEV when no ops
 * are bound; none of them fakes success. ===== */
int  audio_bind_ops(audio_device_t *dev, const audio_ops_t *ops);
bool audio_has_backend(const audio_device_t *dev);
/* Push pending tx_dma bytes to the codec. Returns bytes the backend took. */
int  audio_dma_flush(audio_device_t *dev);
/* Pull up to AUDIO_RX_CHUNK bytes from the codec into rx_dma and fan them out
 * to the capture streams. Returns bytes obtained. */
int  audio_capture_poll(audio_device_t *dev);
/* Program the device output rate (and the codec, if bound). */
int  audio_set_output_rate(audio_device_t *dev, uint32_t hz);

/* ===== Capture fan-out (no hardware needed) =====
 * audio_rx_inject is the entry point the codec ISR — or a test, or a virtual
 * loopback device — uses to hand the system captured S16LE stereo frames at
 * the device rate. `n` must be a multiple of 4. Returns frames accepted. */
int audio_rx_inject(audio_device_t *dev, const uint8_t *pcm, uint32_t n);
/* Convert and copy rx_dma frames into every active capture stream. Returns
 * the number of source frames consumed. */
int audio_capture_dispatch(audio_device_t *dev);

/* ===== IRQ ===== */
/* Services the three latches: stream_done tops the DMA back up, underrun and
 * overrun are recorded into reg_status. Increments stats.irqs_handled only
 * when a latch was actually set. */
void audio_handle_irq(audio_device_t *dev);
void audio_clear_status(audio_device_t *dev);

/* ===== Introspection / small pure helpers (all exported so they can be
 * tested directly rather than inferred from behaviour) ===== */
uint32_t audio_format_bytes(audio_format_t fmt);
/* Clamp to the 16-bit range. THIS is the saturation the mixer relies on:
 * audio_saturate_s16(60000) == 32767, never -5536. */
int16_t  audio_saturate_s16(int32_t v);
bool     audio_rate_supported(uint32_t hz);
/* Bytes queued / bytes free. AUDIO_ENOSTREAM if there is no such stream,
 * AUDIO_EINVAL if the stream's ring fields are inconsistent. */
int32_t  audio_stream_available(const audio_device_t *dev, uint32_t stream_id);
int32_t  audio_stream_space(const audio_device_t *dev, uint32_t stream_id);
uint32_t audio_tx_pending(const audio_device_t *dev);
uint32_t audio_pool_slots_free(void);

/* ===== Coverage =====
 * This is a LIVENESS check, not a config validator, and it is designed to be
 * able to fail — test_audio.c drives 34 distinct inputs through it that must
 * return false. There is no "nothing to check, so pass" branch; every exit is
 * either a named broken invariant or the ratio test.
 *
 * It returns false when (a) any of these structural invariants is broken:
 *      device: num_streams <= 16, 4 <= num_mixer_channels <= 32,
 *              master_volume in [0,1], reg_sample_rate is a supported rate and
 *              <= max_sample_rate, tx/rx head and tail inside AUDIO_BUFFER_SIZE,
 *              reg_format == S16LE
 *      stream: stream_id != 0, buffer != NULL, 0 < buffer_size <= 8192,
 *              head and tail < buffer_size, volume in [0,1],
 *              balance in [-1,1], format in range, channels in {1,2,6,8} and
 *              <= max_channels, sample_rate supported, rs_phase < 2^48,
 *              capture only on a capture-capable device, spatial only on a
 *              3D-capable device
 *      mixer:  volume in [0,1], name NUL-terminated inside its 32 bytes,
 *              source_stream either 0 or an existing stream
 * or (b) coverage_r * coverage_l falls below AUDIO_COVERAGE_FLOOR, where
 *   coverage_r = fraction of allocated streams that could actually put sound
 *                through the mixer right now, and
 *   coverage_l = fraction of mixer channels that are named, unmuted and
 *                above zero gain.
 * A muted master therefore fails, on purpose: nothing is being covered. */
bool audio_verify_coverage(audio_device_t *dev);

#endif /* AUDIO_H */

/* audio.h — ZEDEC XERO pqOS Audio Driver Subsystem
 *
 * Supports: AC97, Intel HDA, USB Audio, I2S (ARM/RISC-V)
 * Features: Playback, capture, mixer, multiple streams, 3D positioning
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 * 36N9 Genetics, LLC
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"

/* ===== Audio controller types ===== */
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
} audio_format_t;

/* ===== Audio stream ===== */
typedef struct {
    uint32_t stream_id;
    bool active;
    bool is_capture;
    audio_format_t format;
    uint32_t sample_rate;    /* 8000, 16000, 22050, 44100, 48000, 96000, 192000 */
    uint8_t channels;        /* 1=mono, 2=stereo, 6=5.1, 8=7.1 */
    uint8_t *buffer;
    uint32_t buffer_size;
    uint32_t buffer_head;
    uint32_t buffer_tail;
    double volume;           /* 0.0 to 1.0 */
    double balance;          /* -1.0 (left) to 1.0 (right) */
    /* 3D positioning */
    double pos_x, pos_y, pos_z;
    bool spatial;
} audio_stream_t;

#define AUDIO_MAX_STREAMS    16
#define AUDIO_BUFFER_SIZE    65536
#define AUDIO_MAX_MIXER_CH   32

/* ===== Mixer channel ===== */
typedef struct {
    char name[32];
    double volume;
    bool muted;
    bool is_capture;
    uint32_t source_stream;
} audio_mixer_ch_t;

/* ===== Audio device (hardware-as-code) ===== */
typedef struct {
    uint32_t device_id;
    char name[128];
    audio_ctrl_type_t ctrl_type;

    /* Registers */
    uint32_t reg_command;
    uint32_t reg_status;
    uint32_t reg_sample_rate;
    uint32_t reg_format;
    uint32_t reg_volume;

    /* DMA */
    uint8_t tx_dma[AUDIO_BUFFER_SIZE];
    uint8_t rx_dma[AUDIO_BUFFER_SIZE];
    uint32_t tx_head, tx_tail;
    uint32_t rx_head, rx_tail;

    /* IRQ */
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

    /* Capabilities */
    uint32_t max_sample_rate;
    uint8_t max_channels;
    bool supports_capture;
    bool supports_3d;

    /* M5 coordinates */
    m5_coords_t m5;
    double coverage_r;
    double coverage_l;
} audio_device_t;

/* ===== API ===== */
void audio_init(audio_device_t *dev, audio_ctrl_type_t type, const char *name);
uint32_t audio_create_stream(audio_device_t *dev, bool capture, audio_format_t fmt,
                             uint32_t rate, uint8_t channels);
int audio_write(audio_device_t *dev, uint32_t stream_id, const void *data, uint32_t len);
int audio_read(audio_device_t *dev, uint32_t stream_id, void *data, uint32_t len);
int audio_set_volume(audio_device_t *dev, uint32_t stream_id, double vol);
int audio_set_balance(audio_device_t *dev, uint32_t stream_id, double bal);
int audio_set_3d_position(audio_device_t *dev, uint32_t stream_id, double x, double y, double z);
int audio_pause(audio_device_t *dev, uint32_t stream_id);
int audio_resume(audio_device_t *dev, uint32_t stream_id);
int audio_stop(audio_device_t *dev, uint32_t stream_id);

/* Mixer */
int audio_mixer_set_channel(audio_device_t *dev, uint32_t ch, double vol, bool mute);
int audio_mixer_set_master(audio_device_t *dev, double vol, bool mute);
void audio_mixer_process(audio_device_t *dev);

/* IRQ */
void audio_handle_irq(audio_device_t *dev);

/* Coverage */
bool audio_verify_coverage(audio_device_t *dev);

#endif /* AUDIO_H */

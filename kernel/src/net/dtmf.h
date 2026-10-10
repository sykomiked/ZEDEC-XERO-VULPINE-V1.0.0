/* dtmf.h — DTMF Tone Generation and Detection + Legacy Telephony
 * Omni-compatible: translates DTMF to M5 axiomatic protocol.
 * Also includes Morse code, RTTY, and other legacy signaling.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef DTMF_H
#define DTMF_H

#include <stdint.h>
#include <stdbool.h>

#define DTMF_SAMPLE_RATE 8000  /* 8kHz telephony standard */
#define DTMF_TONE_DURATION_MS 100
#define DTMF_GAP_DURATION_MS  50
#define DTMF_MAX_DIGITS 256

/* DTMF frequency pairs (row, column) */
typedef struct dtmf_freq_pair {
    uint16_t low;   /* Row frequency */
    uint16_t high;  /* Column frequency */
} dtmf_freq_pair_t;

/* Morse code */
#define MORSE_MAX_LEN 256

typedef struct morse_symbol {
    char character;
    const char *code;  /* "." and "-" string */
} morse_symbol_t;

/* RTTY (Radio Teletype) */
#define RTTY_BAUD_RATE 45.45  /* Baudot standard */
#define RTTY_MARK_FREQ  2125   /* Mark frequency (Hz) */
#define RTTY_SPACE_FREQ 2295   /* Space frequency (Hz) */

/* SSTV (Slow Scan TV) */
#define SSTV_VIS_CODES 16

/* PSK31 */
#define PSK31_BAUD_RATE 31.25

/* DTMF functions */
void dtmf_init(void);
const dtmf_freq_pair_t *dtmf_get_freqs(char digit);
int32_t dtmf_generate(char digit, int16_t *samples, uint32_t max_samples);
int32_t dtmf_generate_string(const char *digits, int16_t *samples, uint32_t max_samples);
char dtmf_detect(const int16_t *samples, uint32_t num_samples);
int32_t dtmf_detect_string(const int16_t *samples, uint32_t num_samples, char *out, uint32_t max_out);

/* Morse code functions */
const char *morse_encode_char(char c);
int32_t morse_encode(const char *text, char *out, uint32_t max_out);
char morse_decode_symbol(const char *code);
int32_t morse_decode(const char *morse, char *out, uint32_t max_out);

/* RTTY functions */
int32_t rtty_encode(const char *text, int16_t *samples, uint32_t max_samples);
int32_t rtty_decode(const int16_t *samples, uint32_t num_samples, char *out, uint32_t max_out);

/* SSTV functions */
int32_t sstv_encode_header(uint8_t mode, int16_t *samples, uint32_t max_samples);
int32_t sstv_decode_header(const int16_t *samples, uint32_t num_samples, uint8_t *mode_out);

/* PSK31 functions */
int32_t psk31_encode(const char *text, int16_t *samples, uint32_t max_samples);
int32_t psk31_decode(const int16_t *samples, uint32_t num_samples, char *out, uint32_t max_out);

/* AX.25 (Ham radio packet) */
#define AX25_MAX_FRAME 256
#define AX25_MAX_ADDR  7

typedef struct ax25_frame {
    uint8_t dest_call[AX25_MAX_ADDR];
    uint8_t src_call[AX25_MAX_ADDR];
    uint8_t control;
    uint8_t pid;
    uint8_t info[AX25_MAX_FRAME];
    uint16_t info_len;
} ax25_frame_t;

int32_t ax25_encode(const char *dest_call, const char *src_call,
                     const void *info, uint16_t info_len,
                     uint8_t *out, uint32_t max_out);
int32_t ax25_decode(const uint8_t *data, uint32_t len, ax25_frame_t *frame);

/* APRS (Automatic Packet Reporting System) */
/* latitude / longitude in micro-degrees (integer; no floating point) */
int32_t aprs_encode_position(const char *callsign, int32_t lat_udeg, int32_t lon_udeg, char *out,
                             uint32_t max_out);
int32_t aprs_encode_message(const char *callsign, const char *dest,
                             const char *text, char *out, uint32_t max_out);

/* Goertzel algorithm for DTMF detection */
int32_t goertzel_magnitude(const int16_t *samples, uint32_t n, uint16_t target_freq, uint32_t sample_rate);

#endif

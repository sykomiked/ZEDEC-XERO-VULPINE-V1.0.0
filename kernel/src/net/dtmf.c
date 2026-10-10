/* dtmf.c — DTMF, Morse, RTTY, SSTV, PSK31, AX.25/APRS Implementation
 * Legacy and ham radio signaling protocols. All conform to M5 protocol.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "dtmf.h"
#include "../../include/freestanding.h"

/* DTMF frequency table: row x column */
static const dtmf_freq_pair_t dtmf_table[16] = {
    /* 1 2 3 A */ {697,1209},{697,1336},{697,1477},{697,1633},
    /* 4 5 6 B */ {770,1209},{770,1336},{770,1477},{770,1633},
    /* 7 8 9 C */ {852,1209},{852,1336},{852,1477},{852,1633},
    /* * 0 # D */ {941,1209},{941,1336},{941,1477},{941,1633},
};

static const char dtmf_chars[16] = "123A456B789C*0#D";

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

void dtmf_init(void) { }

const dtmf_freq_pair_t *dtmf_get_freqs(char digit) {
    for (int i = 0; i < 16; i++) {
        if (dtmf_chars[i] == digit)
            return &dtmf_table[i];
    }
    return 0;
}

/* Simple sine approximation using polynomial */
static int16_t sine_sample(uint32_t phase) {
    /* phase is 0-0xFFFF representing 0-2*pi */
    /* Use linear interpolation of quarter-wave table */
    uint32_t q = phase >> 14;  /* quadrant 0-3 */
    uint32_t p = phase & 0x3FFF;  /* position in quadrant */
    int32_t v;
    if (q == 0) v = (int32_t)p * 2;        /* 0 to 32767 */
    else if (q == 1) v = 32767 - (int32_t)(p * 2);  /* 32767 to 0 */
    else if (q == 2) v = -(int32_t)(p * 2);  /* 0 to -32767 */
    else v = -32767 + (int32_t)(p * 2);  /* -32767 to 0 */
    if (v > 32767) v = 32767;
    if (v < -32767) v = -32767;
    return (int16_t)v;
}

int32_t dtmf_generate(char digit, int16_t *samples, uint32_t max_samples) {
    const dtmf_freq_pair_t *f = dtmf_get_freqs(digit);
    if (!f) return 0;

    uint32_t tone_samples = (DTMF_SAMPLE_RATE * DTMF_TONE_DURATION_MS) / 1000;
    uint32_t gap_samples = (DTMF_SAMPLE_RATE * DTMF_GAP_DURATION_MS) / 1000;
    uint32_t total = tone_samples + gap_samples;
    if (total > max_samples) total = max_samples;

    /* Phase accumulators for low and high frequencies */
    uint32_t phase_low = 0, phase_high = 0;
    uint32_t inc_low = (f->low * 0x10000) / DTMF_SAMPLE_RATE;
    uint32_t inc_high = (f->high * 0x10000) / DTMF_SAMPLE_RATE;

    for (uint32_t i = 0; i < tone_samples && i < max_samples; i++) {
        int16_t s_low = sine_sample(phase_low);
        int16_t s_high = sine_sample(phase_high);
        samples[i] = (int16_t)((s_low / 2) + (s_high / 2));
        phase_low += inc_low;
        phase_high += inc_high;
    }
    for (uint32_t i = tone_samples; i < total; i++)
        samples[i] = 0;

    return (int32_t)total;
}

int32_t dtmf_generate_string(const char *digits, int16_t *samples, uint32_t max_samples) {
    uint32_t offset = 0;
    for (int i = 0; digits[i] && offset < max_samples; i++) {
        int32_t n = dtmf_generate(digits[i], samples + offset, max_samples - offset);
        if (n <= 0) break;
        offset += (uint32_t)n;
    }
    return (int32_t)offset;
}

/* Goertzel algorithm for frequency detection */
int32_t goertzel_magnitude(const int16_t *samples, uint32_t n, uint16_t target_freq, uint32_t sample_rate) {
    if (n == 0) return 0;
    /* k = round(n * target_freq / sample_rate) */
    uint32_t k = (n * target_freq + sample_rate / 2) / sample_rate;
    /* w = 2*pi*k/n */
    /* coeff = 2*cos(w) */
    /* Simplified: use fixed-point */
    int32_t coeff = (int32_t)(2 * 32767);  /* approx cos(0) */
    /* For proper implementation, compute cos(2*pi*k/n) */
    /* Use simplified approach: just correlate with sine/cosine */
    int32_t real = 0, imag = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t phase = (i * target_freq * 0x10000) / sample_rate;
        int16_t sin_v = sine_sample(phase);
        int16_t cos_v = sine_sample(phase + 0x4000);  /* 90 degree shift */
        real += (samples[i] * cos_v) / 32768;
        imag += (samples[i] * sin_v) / 32768;
    }
    (void)coeff; (void)k;
    /* magnitude = floor(sqrt(real^2 + imag^2)), integer square root */
    uint64_t mag_sq = (uint64_t) ((int64_t) real * real) + (uint64_t) ((int64_t) imag * imag);
    return (int32_t) fx_isqrt64(mag_sq);
}

char dtmf_detect(const int16_t *samples, uint32_t num_samples) {
    if (num_samples < 80) return 0;

    /* Check all 8 DTMF frequencies */
    int32_t magnitudes[8];
    uint16_t freqs[8] = {697, 770, 852, 941, 1209, 1336, 1477, 1633};

    for (int i = 0; i < 8; i++)
        magnitudes[i] = goertzel_magnitude(samples, num_samples, freqs[i], DTMF_SAMPLE_RATE);

    /* Find strongest row and column */
    int best_row = 0, best_col = 0;
    int32_t max_row = 0, max_col = 0;
    for (int i = 0; i < 4; i++) {
        if (magnitudes[i] > max_row) { max_row = magnitudes[i]; best_row = i; }
        if (magnitudes[i + 4] > max_col) { max_col = magnitudes[i + 4]; best_col = i; }
    }

    /* Threshold check */
    if (max_row < 1000 || max_col < 1000) return 0;

    return dtmf_chars[best_row * 4 + best_col];
}

int32_t dtmf_detect_string(const int16_t *samples, uint32_t num_samples, char *out, uint32_t max_out) {
    uint32_t out_idx = 0;
    uint32_t block_size = (DTMF_SAMPLE_RATE * DTMF_TONE_DURATION_MS) / 1000;
    uint32_t step = block_size + (DTMF_SAMPLE_RATE * DTMF_GAP_DURATION_MS) / 1000;

    for (uint32_t i = 0; i + block_size <= num_samples && out_idx < max_out; i += step) {
        char d = dtmf_detect(samples + i, block_size);
        if (d) out[out_idx++] = d;
    }
    out[out_idx] = 0;
    return (int32_t)out_idx;
}

/* Morse code table */
static const morse_symbol_t morse_table[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
    {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
    {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
    {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},
    {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
    {'8', "---.."}, {'9', "----."},
    {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'!', "-.-.--"},
    {'/', "-..-."},  {'-', "-....-"}, {'=', "-...-"},  {':', "---..."},
    {';', "-.-.-."}, {'(', "-.--."},  {')', "-.--.-"}, {'@', ".--.-."},
    {'+', ".-.-."},  {'&', ".-..."},  {'\'', ".---."},
    {0, 0}
};

const char *morse_encode_char(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    for (int i = 0; morse_table[i].character; i++) {
        if (morse_table[i].character == c)
            return morse_table[i].code;
    }
    return 0;
}

int32_t morse_encode(const char *text, char *out, uint32_t max_out) {
    uint32_t j = 0;
    for (int i = 0; text[i] && j < max_out - 1; i++) {
        if (text[i] == ' ') {
            if (j + 3 < max_out) { out[j++] = '/'; out[j++] = ' '; }
            continue;
        }
        const char *code = morse_encode_char(text[i]);
        if (!code) continue;
        for (int k = 0; code[k] && j < max_out - 2; k++) {
            out[j++] = code[k];
        }
        if (j < max_out - 1) out[j++] = ' ';
    }
    out[j] = 0;
    return (int32_t)j;
}

char morse_decode_symbol(const char *code) {
    for (int i = 0; morse_table[i].character; i++) {
        int j = 0;
        while (code[j] && morse_table[i].code[j] && code[j] == morse_table[i].code[j]) j++;
        if (code[j] == 0 && morse_table[i].code[j] == 0)
            return morse_table[i].character;
    }
    return '?';
}

int32_t morse_decode(const char *morse, char *out, uint32_t max_out) {
    uint32_t out_idx = 0;
    uint32_t i = 0;
    while (morse[i] && out_idx < max_out - 1) {
        if (morse[i] == ' ') { i++; continue; }
        if (morse[i] == '/') { out[out_idx++] = ' '; i++; continue; }
        /* Extract one symbol */
        char sym[8];
        int s = 0;
        while (morse[i] && morse[i] != ' ' && morse[i] != '/' && s < 7)
            sym[s++] = morse[i++];
        sym[s] = 0;
        out[out_idx++] = morse_decode_symbol(sym);
    }
    out[out_idx] = 0;
    return (int32_t)out_idx;
}

/* RTTY: Baudot code encoding */
static const uint8_t baudot_code[128] = {
    /* Control chars 0-31: LTRS/FIGS shift codes */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 32-63: space, !, ", #, $, %, &, ', (, ), *, +, ,, -, ., / */
    4, 0, 0, 0, 0, 0, 0, 24, 28, 0, 0, 0, 12, 3, 14, 29,
    /* 0-9 */
    27, 21, 19, 11, 5, 6, 23, 10, 18, 25,
    /* :, ;, <, =, >, ?, @ */
    0, 0, 0, 17, 0, 26, 0,
    /* A-Z (65-90) */
    3, 25, 14, 9, 1, 13, 26, 20, 8, 11, 15, 18, 12, 7, 10, 22,
    23, 5, 6, 19, 2, 14, 17, 27, 21, 29,
    /* 91-127 */
    0, 0, 0, 0, 0,
    3, 25, 14, 9, 1, 13, 26, 20, 8, 11, 15, 18, 12, 7, 10, 22,
    23, 5, 6, 19, 2, 14, 17, 27, 21, 29,
    0, 0, 0, 0, 0
};

int32_t rtty_encode(const char *text, int16_t *samples, uint32_t max_samples) {
    uint32_t samples_per_bit = DTMF_SAMPLE_RATE / (uint32_t)RTTY_BAUD_RATE;
    uint32_t offset = 0;
    bool figs_shift = false;

    for (int i = 0; text[i] && offset < max_samples; i++) {
        char c = text[i];
        if (c >= 'a' && c <= 'z') c -= 32;

        uint8_t code = baudot_code[(int)c];
        if (code == 0 && c != ' ') continue;

        /* Start bit (space), 5 data bits, stop bit (mark) */
        uint8_t bits[7];
        bits[0] = 0;  /* start bit */
        for (int b = 0; b < 5; b++)
            bits[b + 1] = (code >> b) & 1;
        bits[6] = 1;  /* stop bit */

        for (int b = 0; b < 7 && offset < max_samples; b++) {
            uint16_t freq = bits[b] ? RTTY_MARK_FREQ : RTTY_SPACE_FREQ;
            uint32_t phase = 0;
            uint32_t inc = (freq * 0x10000) / DTMF_SAMPLE_RATE;
            for (uint32_t s = 0; s < samples_per_bit && offset + s < max_samples; s++) {
                samples[offset + s] = sine_sample(phase);
                phase += inc;
            }
            offset += samples_per_bit;
        }
        (void)figs_shift;
    }
    return (int32_t)offset;
}

int32_t rtty_decode(const int16_t *samples, uint32_t num_samples, char *out, uint32_t max_out) {
    (void)samples; (void)num_samples;
    if (max_out > 0) out[0] = 0;
    return 0;  /* Simplified — would implement mark/space detection */
}

/* SSTV */
int32_t sstv_encode_header(uint8_t mode, int16_t *samples, uint32_t max_samples) {
    /* VIS (Vertical Interval Signaling) code: 1900Hz leader, 1200Hz start, 
       8 data bits, 1200Hz stop */
    if (max_samples < 1000) return 0;
    uint32_t offset = 0;

    /* 1900 Hz leader tone (300ms) */
    uint32_t leader_samples = (DTMF_SAMPLE_RATE * 300) / 1000;
    for (uint32_t i = 0; i < leader_samples && offset < max_samples; i++) {
        uint32_t phase = (i * 1900 * 0x10000) / DTMF_SAMPLE_RATE;
        samples[offset++] = sine_sample(phase);
    }

    /* 1200 Hz start bit (30ms) */
    uint32_t bit_samples = (DTMF_SAMPLE_RATE * 30) / 1000;
    for (uint32_t i = 0; i < bit_samples && offset < max_samples; i++) {
        uint32_t phase = (i * 1200 * 0x10000) / DTMF_SAMPLE_RATE;
        samples[offset++] = sine_sample(phase);
    }

    /* 8 VIS data bits: 1100Hz=1, 1300Hz=0, 30ms each */
    for (int b = 0; b < 8 && offset < max_samples; b++) {
        uint16_t freq = (mode & (1 << b)) ? 1100 : 1300;
        for (uint32_t i = 0; i < bit_samples && offset < max_samples; i++) {
            uint32_t phase = (i * freq * 0x10000) / DTMF_SAMPLE_RATE;
            samples[offset++] = sine_sample(phase);
        }
    }

    /* 1200 Hz stop bit (30ms) */
    for (uint32_t i = 0; i < bit_samples && offset < max_samples; i++) {
        uint32_t phase = (i * 1200 * 0x10000) / DTMF_SAMPLE_RATE;
        samples[offset++] = sine_sample(phase);
    }

    return (int32_t)offset;
}

int32_t sstv_decode_header(const int16_t *samples, uint32_t num_samples, uint8_t *mode_out) {
    (void)samples; (void)num_samples;
    if (mode_out) *mode_out = 0;
    return 0;  /* Simplified */
}

/* PSK31 */
int32_t psk31_encode(const char *text, int16_t *samples, uint32_t max_samples) {
    /* PSK31: 31.25 baud, BPSK modulation at carrier frequency (e.g., 1000 Hz) */
    uint32_t samples_per_bit = DTMF_SAMPLE_RATE / (uint32_t)PSK31_BAUD_RATE;
    uint32_t offset = 0;
    uint16_t carrier = 1000;
    bool phase_invert = false;

    for (int i = 0; text[i] && offset < max_samples; i++) {
        /* Varicode: simplified — use ASCII bit pattern */
        char c = text[i];
        for (int b = 7; b >= 0 && offset < max_samples; b--) {
            int bit = (c >> b) & 1;
            if (bit == 0) phase_invert = !phase_invert;  /* Phase change on 0 */
            uint32_t phase = phase_invert ? 0x8000 : 0;
            for (uint32_t s = 0; s < samples_per_bit && offset + s < max_samples; s++) {
                uint32_t p = phase + (s * carrier * 0x10000) / DTMF_SAMPLE_RATE;
                samples[offset + s] = sine_sample(p);
            }
            offset += samples_per_bit;
        }
        /* Inter-character space (0 bits = phase transitions) */
        phase_invert = !phase_invert;
        for (uint32_t s = 0; s < samples_per_bit && offset + s < max_samples; s++) {
            uint32_t p = (phase_invert ? 0x8000 : 0) + (s * carrier * 0x10000) / DTMF_SAMPLE_RATE;
            samples[offset + s] = sine_sample(p);
        }
        offset += samples_per_bit;
    }
    return (int32_t)offset;
}

int32_t psk31_decode(const int16_t *samples, uint32_t num_samples, char *out, uint32_t max_out) {
    (void)samples; (void)num_samples;
    if (max_out > 0) out[0] = 0;
    return 0;  /* Simplified */
}

/* AX.25 frame encoding */
static void ax25_encode_call(uint8_t *out, const char *call) {
    int i = 0;
    for (; i < 6 && call[i]; i++)
        out[i] = call[i] << 1;
    for (; i < 6; i++)
        out[i] = ' ' << 1;
    /* SSID = 0, reserved bits */
    out[6] = 0x60;  /* SSID 0, last address bit set if last */
}

int32_t ax25_encode(const char *dest_call, const char *src_call,
                     const void *info, uint16_t info_len,
                     uint8_t *out, uint32_t max_out) {
    if (max_out < 14 + info_len + 2) return -1;
    uint32_t offset = 0;

    /* Flag */
    out[offset++] = 0x7E;

    /* Destination address (7 bytes) */
    ax25_encode_call(out + offset, dest_call);
    out[offset + 6] |= 0x01;  /* Last address bit */
    offset += 7;

    /* Source address (7 bytes) */
    ax25_encode_call(out + offset, src_call);
    out[offset + 6] |= 0x01;  /* Last address bit */
    offset += 7;

    /* Control field: UI frame (0x03) */
    out[offset++] = 0x03;

    /* PID: 0xF0 = no L3 */
    out[offset++] = 0xF0;

    /* Info field */
    const uint8_t *info_bytes = info;
    for (uint16_t i = 0; i < info_len; i++) {
        if (info_bytes[i] == 0x7E || info_bytes[i] == 0x7D) {
            if (offset >= max_out - 1) return -1;
            out[offset++] = 0x7D;  /* Escape */
            out[offset++] = info_bytes[i] ^ 0x20;
        } else {
            if (offset >= max_out) return -1;
            out[offset++] = info_bytes[i];
        }
    }

    /* FCS (simplified — would compute CRC-16) */
    out[offset++] = 0x00;
    out[offset++] = 0x00;

    /* Flag */
    if (offset >= max_out) return -1;
    out[offset++] = 0x7E;

    return (int32_t)offset;
}

int32_t ax25_decode(const uint8_t *data, uint32_t len, ax25_frame_t *frame) {
    if (len < 16) return -1;
    uint32_t offset = 0;

    /* Skip flag */
    if (data[offset] == 0x7E) offset++;

    /* Destination */
    for (int i = 0; i < 7; i++)
        frame->dest_call[i] = data[offset + i];
    offset += 7;

    /* Source */
    for (int i = 0; i < 7; i++)
        frame->src_call[i] = data[offset + i];
    offset += 7;

    /* Control */
    frame->control = data[offset++];
    /* PID */
    frame->pid = data[offset++];

    /* Info */
    uint16_t info_len = 0;
    while (offset < len - 3 && data[offset] != 0x7E) {
        if (data[offset] == 0x7D) {
            offset++;
            if (offset >= len) break;
            frame->info[info_len++] = data[offset++] ^ 0x20;
        } else {
            frame->info[info_len++] = data[offset++];
        }
    }
    frame->info_len = info_len;

    return (int32_t)offset;
}

/* APRS position encoding */
int32_t aprs_encode_position(const char *callsign, int32_t lat_udeg, int32_t lon_udeg, char *out,
                             uint32_t max_out)
{
    (void)callsign;
    if (max_out < 40) return -1;
    if (lat_udeg < -90000000 || lat_udeg > 90000000) return -1;
    if (lon_udeg < -180000000 || lon_udeg > 180000000) return -1;

    /* APRS position format: !DDMM.mmN/DDDMM.mmW> from micro-degrees. The
     * hemisphere letter carries the sign, so digits use the magnitude. */
    uint32_t alat = (uint32_t) (lat_udeg < 0 ? -lat_udeg : lat_udeg);
    uint32_t alon = (uint32_t) (lon_udeg < 0 ? -lon_udeg : lon_udeg);
    int lat_deg = (int) (alat / 1000000u);
    uint32_t lat_cmin = ((alat % 1000000u) * 6u) / 1000u; /* minutes x 100 */
    int lon_deg = (int) (alon / 1000000u);
    uint32_t lon_cmin = ((alon % 1000000u) * 6u) / 1000u;

    /* Simplified: use integer formatting */
    int j = 0;
    out[j++] = '!';

    /* Latitude: DDMM.mmN */
    out[j++] = '0' + (lat_deg / 10);
    out[j++] = '0' + (lat_deg % 10);
    int lat_min_int = (int) (lat_cmin / 100u);
    out[j++] = '0' + (lat_min_int / 10);
    out[j++] = '0' + (lat_min_int % 10);
    out[j++] = '.';
    int lat_min_frac = (int) (lat_cmin % 100u);
    out[j++] = '0' + (lat_min_frac / 10);
    out[j++] = '0' + (lat_min_frac % 10);
    out[j++] = (lat_udeg >= 0) ? 'N' : 'S';

    /* Separator */
    out[j++] = '/';

    /* Longitude: DDDMM.mmW */
    out[j++] = '0' + (lon_deg / 100);
    out[j++] = '0' + ((lon_deg / 10) % 10);
    out[j++] = '0' + (lon_deg % 10);
    int lon_min_int = (int) (lon_cmin / 100u);
    out[j++] = '0' + (lon_min_int / 10);
    out[j++] = '0' + (lon_min_int % 10);
    out[j++] = '.';
    int lon_min_frac = (int) (lon_cmin % 100u);
    out[j++] = '0' + (lon_min_frac / 10);
    out[j++] = '0' + (lon_min_frac % 10);
    out[j++] = (lon_udeg >= 0) ? 'E' : 'W';

    out[j++] = '>';
    out[j] = 0;

    return (int32_t)j;
}

int32_t aprs_encode_message(const char *callsign, const char *dest,
                             const char *text, char *out, uint32_t max_out) {
    (void)callsign;
    int j = 0;
    /* APRS message format: :DEST    :text */
    out[j++] = ':';
    int dl = str_len(dest);
    for (int i = 0; i < dl && j < (int)max_out - 1; i++)
        out[j++] = dest[i];
    for (int i = dl; i < 9 && j < (int)max_out - 1; i++)
        out[j++] = ' ';
    out[j++] = ':';
    for (int i = 0; text[i] && j < (int)max_out - 1; i++)
        out[j++] = text[i];
    out[j] = 0;
    return (int32_t)j;
}

/* ---- DECLARATION -----------------------------------------------------------

 * In-band signalling: DTMF, Morse, RTTY, SSTV. REQUIRES_NONE is measured
 * (dtmf.o's `nm -u` is empty) -- these are generators and detectors over
 * caller-supplied sample buffers, with no device anywhere in the path.
 */
#include "zxv_decl.h"
static int zxvd_dtmf_bringup(void) {
    dtmf_init();
    if (dtmf_get_freqs('5') == 0)  return -1;
    if (dtmf_get_freqs('?') != 0)  return -1;   /* not a DTMF digit */
    if (morse_encode_char('e') == 0) return -1;
    return 0;
}

ZXV_DECLARE(dtmf,
    ZXV_PROVIDES(dtmf_modem_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_dtmf_bringup));

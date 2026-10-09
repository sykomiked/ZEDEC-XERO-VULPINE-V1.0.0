/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_trunk_bank.h — ten-trunk frequency-division bank: integer Goertzel
 * receiver, line states, and pi/4-DQPSK frames on each carrier.
 *
 *   B1  CARRIERS.  111, 222, ..., 999 Hz (trunks 0..8, axis) and 1111 Hz
 *       (trunk 9, expansion) at fs = 8000 Hz. All ten are harmonics of 111 Hz
 *       except 1111 Hz, which sits 1 Hz above the 10th harmonic (1110 Hz).
 *       So any nonlinearity (clipping, folding, saturation) on one trunk puts
 *       energy on other trunks: the 3rd harmonic of 111 Hz is trunk 2 (333),
 *       the 2nd harmonic of 555 Hz (1110 Hz) lands 1 Hz from trunk 9. Keep
 *       the composite linear and below full scale.
 *   B2  BLOCK AND WINDOW.  The receiver analyses non-overlapping blocks of
 *       ZT_TRUNK_BLOCK_SAMPLES = 320 samples (40 ms, 25 Hz bins) through a
 *       4-term Blackman-Harris window (sidelobes below -92 dB, main lobe +-4
 *       bins = +-100 Hz, inside the 111 Hz spacing). The spec's 160-sample
 *       rectangular block has 50 Hz bins and leaks a 555+666 Hz pair into
 *       444/777 Hz at about -22 dB; this window and length give about -96 dB.
 *       ZT_TRUNK_WINDOW_SAMPLES (160, 20 ms) is kept as the reporting cadence:
 *       line states change every second report.
 *   B3  GOERTZEL.  Per sample: xw = (x * window[n]) >> 15, then for each trunk
 *       q0 = xw + ((coef_q14 * q1) >> 14) - q2, in int64 (the spec's int32
 *       product overflows: q1 reaches about 2^26). Q14 coefficients and every
 *       other constant come from gen_harmonic_tables.py.
 *   B4  LINE STATE, at each block end:
 *         power < ZT_TRUNK_NOISE_FLOOR (45 dB below full scale)   -> DEAD
 *         power above floor, phase steady since the last block     -> ON_HOOK
 *         power above floor, phase moved >= 22.5 degrees            -> OFF_HOOK
 *       "Steady" means the block's complex output equals the previous one
 *       rotated by the carrier's own advance w * 320. A steady tone burst is
 *       therefore ON_HOOK; OFF_HOOK needs phase modulation (B5).
 *   B5  FRAMES.  pi/4-DQPSK, one symbol per block, 2 bits per symbol, Gray
 *       coded: 00 +45, 01 +135, 11 -135, 10 -45 degrees. Every symbol moves
 *       the phase, so a data burst is always OFF_HOOK. A frame is one
 *       reference block plus 84 symbol blocks (168 bits = the 21 octets, octet
 *       0 first, most significant bit first): 85 blocks = 27,200 samples =
 *       3.4 s. Rate: 50 bit/s per trunk, 500 bit/s for all ten.
 *   B6  SYNCHRONOUS.  Transmitter and receiver share the block clock: a frame
 *       starts on a block boundary and all trunks' symbols are aligned (one
 *       composite transmitter, zt_trunk_mux). There is no clock recovery.
 */
#ifndef ZT_TRUNK_BANK_H
#define ZT_TRUNK_BANK_H

#include "zt_harmonic_wire.h"

#define ZT_NUM_TRUNKS           10u
#define ZT_TRUNK_SAMPLE_RATE    8000u
#define ZT_TRUNK_WINDOW_SAMPLES 160u /* reporting cadence (20 ms), B2 */
#define ZT_TRUNK_BLOCK          320u /* analysis block = symbol (40 ms), B2 */
#define ZT_TRUNK_FRAME_SYMBOLS  84u  /* 168 bits / 2 */
#define ZT_TRUNK_FRAME_BLOCKS   85u  /* reference + symbols */
#define ZT_TRUNK_FRAME_SAMPLES  (ZT_TRUNK_FRAME_BLOCKS * ZT_TRUNK_BLOCK)
#define ZT_TRUNK_DEFAULT_AMP    8000

extern const uint16_t ZT_TRUNK_FREQS[ZT_NUM_TRUNKS];

typedef enum {
    ZT_TRK_SUPERVISOR = 0, /* 111 Hz */
    ZT_TRK_MEMORY = 1,     /* 222 Hz */
    ZT_TRK_LEDGER = 2,     /* 333 Hz */
    ZT_TRK_PARLEY = 3,     /* 444 Hz */
    ZT_TRK_PULSE = 4,      /* 555 Hz: swarm_harmonic metronome (zt_metronome.h) */
    ZT_TRK_BRIG = 5,       /* 666 Hz */
    ZT_TRK_MARKET = 6,     /* 777 Hz */
    ZT_TRK_PARADOX = 7,    /* 888 Hz */
    ZT_TRK_SEAL = 8,       /* 999 Hz */
    ZT_TRK_FLEET = 9       /* 1111 Hz: expansion trunk */
} zt_trunk_id_t;

/* Receiver state. */
typedef struct {
    int32_t coef_q14[ZT_NUM_TRUNKS];
    int64_t q1[ZT_NUM_TRUNKS];
    int64_t q2[ZT_NUM_TRUNKS];
    uint16_t sample_acc; /* position inside the current block */
    union {
        zt_line_status_t line_state[ZT_NUM_TRUNKS];
        zt_line_status_t status[ZT_NUM_TRUNKS]; /* signal-data spelling */
    };
    int64_t power[ZT_NUM_TRUNKS];      /* last block's power */
    int64_t peak_power[ZT_NUM_TRUNKS]; /* largest block power since init */
    int32_t prev_re[ZT_NUM_TRUNKS];    /* last block's output, normalised */
    int32_t prev_im[ZT_NUM_TRUNKS];
    bool prev_valid[ZT_NUM_TRUNKS];
    uint32_t blocks;                        /* blocks completed */
    uint32_t offhook_blocks[ZT_NUM_TRUNKS]; /* blocks reported OFF_HOOK */
    uint32_t onhook_blocks[ZT_NUM_TRUNKS];  /* blocks reported ON_HOOK */
} zt_trunk_demux_bank_t;

/* Transmitter (synthesiser) state. Phases are 32-bit binary angles. */
typedef struct {
    uint32_t phase_acc[ZT_NUM_TRUNKS];
    uint32_t phase_step[ZT_NUM_TRUNKS];
    uint32_t phase_offset[ZT_NUM_TRUNKS]; /* DQPSK symbol phase */
    int16_t amplitude[ZT_NUM_TRUNKS];
    bool line_active[ZT_NUM_TRUNKS];
} zt_trunk_mux_bank_t;

void zt_trunk_bank_init(zt_trunk_demux_bank_t *bank);
/* B3/B4: stream intake; any count, blocks complete across calls. */
void zt_trunk_bank_process_pcm(zt_trunk_demux_bank_t *bank, const int16_t *pcm_in, size_t count);
zt_line_status_t zt_trunk_query_line(const zt_trunk_demux_bank_t *bank, zt_trunk_id_t trunk_id);

/* B5: demodulate one frame from count >= ZT_TRUNK_FRAME_SAMPLES block-aligned
 * samples. True iff every block was above the floor and every symbol was a
 * valid pi/4 step. Sets the trunk's line state to OFF_HOOK on success. */
bool zt_trunk_read_frame(zt_trunk_demux_bank_t *bank, zt_trunk_id_t trunk_id, const int16_t *pcm_in,
                         size_t count, zt_ubh168_frame_t *dst_frame);

void zt_trunk_mux_init(zt_trunk_mux_bank_t *mux);
/* Add a steady carrier (ON_HOOK) to pcm_out, saturating. */
void zt_trunk_mux_tone(zt_trunk_mux_bank_t *mux, zt_trunk_id_t trunk_id, int16_t *pcm_out,
                       size_t count);
/* B5: add one modulated frame (ZT_TRUNK_FRAME_SAMPLES samples) to pcm_out,
 * saturating. False if count is too small or the trunk is invalid. */
bool zt_trunk_mux_transmit_frame(zt_trunk_mux_bank_t *mux, zt_trunk_id_t trunk_id,
                                 const zt_ubh168_frame_t *frame, int16_t *pcm_out, size_t count);

/* Block power relative to a full-scale tone, in tenths of a dB (<= 0 for
 * powers below full scale); -1000 for zero power. Integer log2. */
int32_t zt_trunk_power_db10(int64_t power);
int64_t zt_trunk_noise_floor(void);

/* Q15 sine of a 32-bit binary angle (256-step quarter wave, interpolated). */
int32_t zt_trunk_sin_q15(uint32_t phase);

#endif /* ZT_TRUNK_BANK_H */

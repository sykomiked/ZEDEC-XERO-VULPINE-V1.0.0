/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_adapter_core.h — the adapter house: converting host data ("DC": plain
 * byte arrays, PCM, pixels, weights, text) to streams of canonical frames
 * ("AC": the L-B-L-B-L frames of zt_harmonic_wire.h) and back.
 *
 * The spec's electrical words are names for two data conversions:
 *   inverter  = pump_in:   host buffer -> header frame + data frames
 *   rectifier = drain_out: frames + a truth state -> host buffer
 *
 *   A1  STREAM.  Every adapter emits one header frame, then data frames.
 *       Header: tag sync=1 shell=0; S+ W0 = payload length (bytes, or
 *       elements for the tensor adapter), W2 = 'Z' 'X' 'A' kind, W4 = number
 *       of data frames; S- W1 = FNV-1a-32 of the payload bytes, W3 = ~W0 (a
 *       witness of the length). Data frames: tag sync=0, shell = index mod 10.
 *   A2  BYTE PATH.  The plain byte adapter puts 20 payload bytes in each data
 *       frame (word k = bytes 4k..4k+3 read little-endian), so the round trip
 *       pump_in -> drain_out(TRUE) is bit-exact on any host. The graphics,
 *       audio and text adapters are also lossless on their own data. The
 *       tensor adapter is NOT: FP32/BF16 -> Q16.16 rounds to 2^-16 and
 *       saturates at +-32768, and phi scale snapping moves INT8 block scales
 *       by up to ~24% (zt_phi.h P2).
 *   A3  TRUTH AT THE RECTIFIER.  drain_out writes according to the truth:
 *         TRUE, NEUTRAL   the payload
 *         FALSE           the retraction (bytes: nothing; graphics: the
 *                         previous image from the residual; audio: silence;
 *                         text: nothing)
 *         GLUT, PARADOX   both claims, never an average (bytes: refused with
 *                         ZT_ADAPTER_EHELD since a byte stream cannot hold two
 *                         values; graphics: checkerboard of new and previous;
 *                         audio: the side channel; text: tagged "[GLUT] ")
 *         UNKNOWN         nothing
 *   A4  CLAIMS.  A claim frame (zt_adapter_claim_frame) carries evidence for
 *       one proposition: S+ W0 = evidence (Q16.16), W2 = proposition id,
 *       W4 = ZT_ADAPTER_CLAIM_MAGIC, S- repeats W0 and W2; tag shell 9 sync 1.
 *       evaluate_interference(a, b) with two claims on the same proposition
 *       is the wire's evidence resolver (W6: a = for, b = against) plus the
 *       adapter's persistence counter (W7: three consecutive GLUTs ->
 *       PARADOX). For two data frames the default reads b's S+ as the witness
 *       of a's S+ (the W5 correlation resolver); the audio adapter replaces
 *       this with a cancellation detector.
 *   Never panics, never allocates; every error is a negative return.
 */
#ifndef ZT_ADAPTER_CORE_H
#define ZT_ADAPTER_CORE_H

#include "zt_harmonic_wire.h"

typedef enum {
    ZT_ENDIAN_DC_LITTLE = 0,
    ZT_ENDIAN_DC_BIG = 1,
    ZT_ENDIAN_AC_MIXED = 2 /* L-B-L-B-L, the canonical frame */
} zt_endian_mode_t;

typedef enum {
    ZT_SPACE_S_PLUS = 0,  /* direct effect */
    ZT_SPACE_S_MINUS = 1, /* inverse witness / exact residual */
    ZT_SPACE_S_ZERO = 2   /* held contradiction */
} zt_space_pole_t;

typedef enum {
    ZT_ADAPTER_KIND_BYTES = 1,
    ZT_ADAPTER_KIND_TENSOR = 2,
    ZT_ADAPTER_KIND_GRAPHICS = 3,
    ZT_ADAPTER_KIND_AUDIO = 4,
    ZT_ADAPTER_KIND_TEXT = 5
} zt_adapter_kind_t;

#define ZT_ADAPTER_EARG    (-1) /* NULL or invalid argument */
#define ZT_ADAPTER_ESPACE  (-2) /* destination too small */
#define ZT_ADAPTER_EFORMAT (-3) /* bad header, checksum, witness or order */
#define ZT_ADAPTER_EHELD   (-4) /* GLUT/PARADOX on a path that cannot hold two values */
#define ZT_ADAPTER_EDOMAIN (-5) /* input outside the adapter's domain */

#define ZT_ADAPTER_PAYLOAD      20u         /* bytes per data frame on the byte path */
#define ZT_ADAPTER_CLAIM_MAGIC  0x434C4D21u /* "CLM!" */
#define ZT_ADAPTER_THRESHOLD    0x00000100  /* default evidence threshold, 1/256 */
#define ZT_ADAPTER_KIND_WORD(k) (0x5A584100u | (uint32_t) (k)) /* "ZXA" + kind */

typedef struct zt_adapter zt_adapter_t;

struct zt_adapter {
    const char *name;
    void *state;
    zt_adapter_kind_t kind;
    uint8_t trunk;         /* trunk written in every tag */
    uint8_t conflict_run;  /* W7 counter for evaluate_interference */
    int32_t threshold_q16; /* evidence / correlation threshold */
    /* inverter. Returns frames written (>= 1), or a negative error. The spec's
     * signature had no destination size; max_frames is added. */
    int (*pump_in)(zt_adapter_t *self, const void *dc_src, size_t len, zt_ubh168_frame_t *ac_dst,
                   size_t max_frames);
    /* rectifier. Returns bytes written (>= 0), or a negative error. The spec's
     * signature had no frame count; n_frames is added. */
    int (*drain_out)(zt_adapter_t *self, const zt_ubh168_frame_t *ac_src, size_t n_frames,
                     zt_truth_state_t truth, void *dc_dst, size_t max_len);
    zt_truth_state_t (*evaluate_interference)(zt_adapter_t *self, const zt_ubh168_frame_t *s_plus,
                                              const zt_ubh168_frame_t *s_minus);
};

/* The plain byte adapter (A2). */
void zt_adapter_bytes_init(zt_adapter_t *a, uint8_t trunk);

/* Frames needed for len payload bytes on the byte path (header included). */
size_t zt_adapter_frames_for(size_t len);

/* A1 helpers shared by the sub-adapters. */
uint32_t zt_adapter_fnv1a(const uint8_t *p, size_t n, uint32_t h);
void zt_adapter_header(zt_adapter_kind_t kind, uint8_t trunk, uint32_t length, uint32_t frames,
                       uint32_t checksum, zt_ubh168_frame_t *dst);
/* Validates a header; fills length, data-frame count and checksum. */
int zt_adapter_read_header(const zt_ubh168_frame_t *f, zt_adapter_kind_t kind, size_t n_frames,
                           uint32_t *length, uint32_t *frames, uint32_t *checksum);
uint8_t zt_adapter_data_tag(uint8_t trunk, uint32_t index);

/* A4. */
void zt_adapter_claim_frame(uint8_t trunk, uint32_t proposition, int32_t evidence_q16,
                            zt_ubh168_frame_t *dst);
bool zt_adapter_is_claim(const zt_ubh168_frame_t *f);
/* The default evaluate_interference (claims, else W5 correlation). */
zt_truth_state_t zt_adapter_evaluate_default(zt_adapter_t *self, const zt_ubh168_frame_t *a,
                                             const zt_ubh168_frame_t *b);

/* Little-endian byte helpers (host-independent). */
uint32_t zt_ld_le32(const uint8_t *p);
void zt_st_le32(uint8_t *p, uint32_t v);

#endif /* ZT_ADAPTER_CORE_H */

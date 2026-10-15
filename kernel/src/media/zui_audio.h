/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_audio.h — opening recordings: WAV, AIFF/AIFC and FLAC to integer PCM.
 *
 * The system's own sounds are vector (ZXVA, kernel/src/vaudio). People have
 * recordings in the formats everyone else uses, and "vectorise everything"
 * starts by reading them exactly. This is the decoder front end: sniff the
 * format from the bytes, report rate, channels, depth and length, and decode
 * into interleaved int32 samples, right-justified at the source depth. The
 * vector analyser (vaudio/zxva_analyse.h) takes that PCM.
 *
 * Rules:
 *   AU1 Freestanding: no libc, no allocation, no floating point. Float WAV
 *       and AIFC samples are converted to 32-bit integers bit by bit
 *       (sign, exponent, mantissa), rounding half to even and saturating.
 *   AU2 Fail closed. Every read is bounds checked; a header that does not
 *       add up, a chunk that runs past the end, a FLAC frame whose CRC-8 or
 *       CRC-16 fails, a frame number out of sequence, a sample out of its
 *       declared range, or an MD5 mismatch (when asked to check) is an
 *       error, never a partial decode. Output is written only up to the
 *       caller's cap, and decoding with out == NULL validates and counts.
 *   AU3 Exact: integer PCM comes out bit for bit as stored (a FLAC stream is
 *       reproduced exactly, MD5-checked by the test against the reference
 *       encoder's input).
 *
 * WHAT IS AND IS NOT IMPLEMENTED (do not overstate this)
 *   WAV   RIFF/WAVE: PCM 8 (unsigned), 16, 24, 32 bit; IEEE float 32 and 64
 *         bit (to int32, bits = 32, is_float = 1); WAVE_FORMAT_EXTENSIBLE
 *         with the PCM or float sub-format GUID (the container depth is
 *         used; "valid bits" is reported, the low bits are kept as stored).
 *         1..8 channels. Not done: RF64/BW64 (> 4 GB), A-law/mu-law, ADPCM,
 *         any compressed WAV (refused with a reason).
 *   AIFF  AIFF and AIFC with compression NONE, sowt (little endian) and
 *         fl32/FL32 (float), 1..32 bits, 1..8 channels, the 80-bit extended
 *         sample rate decoded in integers (must be a whole number of Hz).
 *         Other AIFC compressions are refused.
 *   FLAC  native FLAC streams, complete: every subframe type (CONSTANT,
 *         VERBATIM, FIXED orders 0..4, LPC orders 1..32), wasted bits, Rice
 *         partitions with 4- and 5-bit parameters and escape codes, all
 *         channel assignments (independent 1..8, left/side, right/side,
 *         mid/side), fixed and variable block sizes, every header code for
 *         block size and rate, 4..32 bits per sample, CRC-8 and CRC-16 on
 *         every frame, optional MD5 of the decoded audio. Limit: a stereo
 *         side channel of a 32-bit stream needs 33 bits and is refused
 *         (ZUI_AUD_ECAP). Ogg-encapsulated FLAC is refused.
 *   Refused with a reason (zui_audio_reason), not decoded:
 *     MP3     patents have expired; an integer decoder is a future option.
 *     AAC     (ADTS, or MP4/M4A) still under a licensing pool.
 *     Opus, Vorbis (Ogg) royalty-free, but large codecs: a future option.
 */
#ifndef ZXV_ZUI_AUDIO_H
#define ZXV_ZUI_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define ZUI_AUD_MAX_CH 8u

/* what zui_audio_sniff saw */
enum {
    ZUI_AUD_UNKNOWN = 0,
    ZUI_AUD_WAV = 1,
    ZUI_AUD_AIFF = 2,
    ZUI_AUD_FLAC = 3,
    /* recognised and refused */
    ZUI_AUD_MP3 = 16,
    ZUI_AUD_AAC = 17,
    ZUI_AUD_OGG = 18, /* Opus, Vorbis, or Ogg FLAC */
    ZUI_AUD_MP4 = 19
};

/* errors */
#define ZUI_AUD_EFORMAT (-1) /* malformed or not audio                     */
#define ZUI_AUD_ETRUNC  (-2) /* the data stops before the header says       */
#define ZUI_AUD_EUNSUP  (-3) /* a codec or variant this decoder refuses     */
#define ZUI_AUD_ECAP    (-4) /* valid but over this build's caps            */
#define ZUI_AUD_EARG    (-5) /* bad argument                                */
#define ZUI_AUD_ECRC    (-6) /* a FLAC frame CRC failed                     */
#define ZUI_AUD_EMD5    (-7) /* the FLAC MD5 of the decoded audio differs   */

#define ZUI_AUD_F_MD5 1u /* zui_audio_decode: verify the FLAC MD5           */

typedef struct {
    uint32_t kind;        /* ZUI_AUD_*                                       */
    uint32_t rate;        /* Hz                                              */
    uint32_t nch;         /* 1..8                                            */
    uint32_t bits;        /* sample depth of the decoded int32 values        */
    uint32_t valid_bits;  /* WAVE_FORMAT_EXTENSIBLE's valid bits, else bits  */
    bool is_float;        /* the source was float (converted to int32)       */
    uint64_t frames;      /* frames (0 for a FLAC stream that does not say,
                             until zui_audio_decode has counted them)         */
    bool md5_present;     /* FLAC: STREAMINFO has a non-zero MD5             */
    bool md5_checked;     /* set by zui_audio_decode with ZUI_AUD_F_MD5      */
    uint32_t flac_frames; /* FLAC frames decoded                            */
    uint32_t max_block;   /* FLAC: STREAMINFO's largest block, frames        */
} zui_audio_info_t;

/* The container or codec in d (refused codecs included). */
uint32_t zui_audio_sniff(const uint8_t *d, uint32_t n);

/* A one-line reason for an error code or a refused kind, for people. */
const char *zui_audio_reason(int code_or_kind);

/* Parse the header: 0 and info filled, or < 0 (EUNSUP for refused codecs). */
int zui_audio_probe(const uint8_t *d, uint32_t n, zui_audio_info_t *info);

/* Scratch a FLAC decode needs when blocks do not land in `out` (out NULL,
 * or past cap): max_block x nch int32 words. 0 for WAV and AIFF. */
uint32_t zui_audio_work_words(const zui_audio_info_t *info);

/* Decode the whole file into out (interleaved, info.nch per frame, values
 * right-justified at info.bits) up to cap frames. Returns the number of
 * frames (which may exceed cap: only whole FLAC blocks that fit were
 * written), or < 0. With out == NULL nothing is written and the whole file
 * is still validated (probe for size). `work` (zui_audio_work_words) is
 * needed for FLAC blocks that do not fit in out; else it may be NULL. */
int64_t zui_audio_decode(const uint8_t *d, uint32_t n, int32_t *out, uint64_t cap, int32_t *work,
                         uint32_t work_words, zui_audio_info_t *info, uint32_t flags);

/* IEEE 754 single / double bit patterns to a saturated int32 of x * 2^31,
 * rounded half to even (the conversion float sources go through). */
int32_t zui_audio_f32_to_i32(uint32_t bits);
int32_t zui_audio_f64_to_i32(uint64_t bits);

/* MD5 (RFC 1321), exported for the FLAC check and the tests. */
typedef struct {
    uint32_t h[4];
    uint64_t len;
    uint8_t buf[64];
    uint32_t nb;
} zui_md5_t;
void zui_md5_init(zui_md5_t *m);
void zui_md5_update(zui_md5_t *m, const uint8_t *p, uint32_t n);
void zui_md5_final(zui_md5_t *m, uint8_t out[16]);

#endif

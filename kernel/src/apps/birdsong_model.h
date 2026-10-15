/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* birdsong_model.h — Birdsong, voice memos and audio editing, as a model
 * (zxm.h): one memo of 16-bit PCM samples (one or two channels,
 * interleaved), its gain, its length and where it is kept in the Hoard.
 * No drawing, no allocation, integers only.
 *
 *   BS1 MEMOS ARE FILES. A memo is a 16-bit PCM WAV file in the Hoard,
 *       written with files.write (base64, begin/add/end when it does not
 *       fit one request) and read back with files.read (base64). Nothing
 *       appears until the whole file is written (F4).
 *   BS2 NO RECORDING METHOD. zxvd has no method that captures sound from
 *       the microphone (api/zxvd.openrpc.json lists none), so this model
 *       records nothing itself: the view hands it the samples it captured
 *       (birdsong_take), and the model keeps, edits and files them. The
 *       manifest still asks for "mic", the capability such a capture needs.
 *   BS3 LIMITS. A memo's rate is 8000..48000 Hz, its channels 1..2 and its
 *       length 0..BIRDSONG_FRAMES_MAX frames (blocks/birdsong/limits.eve,
 *       Eve). A memo outside them is refused (ZXM_E_ARG), never cut.
 *   BS4 GAIN AND CLIP. A level is in hundredths of a percent (10000 is
 *       unity, at most BIRDSONG_LEVEL_MAX). Each sample becomes
 *       floor(sample * level / 10000), the floor toward minus infinity,
 *       held inside -32768..32767 (blocks/birdsong/gain.kron and gain.cpp,
 *       witnessed). A sample or level outside that domain is
 *       BIRDSONG_REFUSED, as the C++ block answers.
 *   BS5 EXACT LENGTH. A length in milliseconds is samples * 1000 / rate as
 *       a reduced fraction (blocks/birdsong/clip_ms.tb, Trebago), never
 *       rounded. */
#ifndef BIRDSONG_MODEL_H
#define BIRDSONG_MODEL_H

#include "zxm.h"

#define BIRDSONG_RATE_MIN     8000
#define BIRDSONG_RATE_MAX     48000
#define BIRDSONG_CHANNELS_MAX 2
#define BIRDSONG_FRAMES_MAX   480000u
#define BIRDSONG_SAMPLES_MAX  (BIRDSONG_FRAMES_MAX * BIRDSONG_CHANNELS_MAX)
#define BIRDSONG_UNITY        10000    /* a level of 100.00% */
#define BIRDSONG_LEVEL_MAX    1000000  /* 100 times */
#define BIRDSONG_REFUSED      (-32769) /* BS4: below every 16-bit sample */
#define BIRDSONG_WAV_HEAD     44u      /* the header birdsong_save writes */
#define BIRDSONG_FILE_MAX     (BIRDSONG_WAV_HEAD + 2u * BIRDSONG_SAMPLES_MAX + 4096u)
#define BIRDSONG_PATH_MAX     1024u

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1];    /* where path is; "" = home */
    char path[BIRDSONG_PATH_MAX + 1]; /* "" = not saved yet */
    int32_t rate;                     /* Hz; 0 = no memo yet */
    int32_t channels;
    uint32_t frames;
    int16_t pcm[BIRDSONG_SAMPLES_MAX]; /* frames * channels, interleaved */
    bool dirty;                        /* changed since it was opened or saved */
    uint32_t clipped;                  /* samples the last gain held at the edge */
} birdsong_t;

/* The rules, as the blocks run them (each the reference for its entry). */
int64_t birdsong_gain(int64_t sample, int64_t level); /* BS4, one sample */
/* BS5: ZXM_OK and *num / *den in lowest terms (den > 0), or ZXM_E_ARG for
 * a negative count, a rate of 0 or less, or a product too big for int64. */
int birdsong_clip_ms(int64_t samples, int64_t rate, int64_t *num, int64_t *den);
bool birdsong_rec_ok(int64_t rate, int64_t channels, int64_t frames); /* BS3 */
/* Whether a memo's numbers fit its record (blocks/birdsong/memo.myra):
 * memo and frames 9 digits, rate 5, channels 1, mtime 12, either sign. */
bool birdsong_memo_ok(int64_t memo, int64_t rate, int64_t channels, int64_t frames, int64_t mtime);

void birdsong_init(birdsong_t *b, zxm_link_t *link);
/* BS2: take n interleaved samples the view captured, at rate and channels.
 * ZXM_E_ARG when they break BS3 or n is not whole frames. */
int birdsong_take(birdsong_t *b, int32_t rate, int32_t channels, const int16_t *pcm, uint32_t n);
/* BS4 over the whole memo; b->clipped counts the samples held at an edge. */
int birdsong_apply_gain(birdsong_t *b, int64_t level);
/* Keep only frames [from, to): the edit that trims a memo's ends. */
int birdsong_trim(birdsong_t *b, uint32_t from, uint32_t to);
/* BS5 for the whole memo. ZXM_E_STATE when there is none. */
int birdsong_length_ms(const birdsong_t *b, int64_t *num, int64_t *den);

/* BS1. space: a space id from spaces.list, "" for home. */
int birdsong_save(birdsong_t *b); /* to b->path; ZXM_E_STATE when there is none */
int birdsong_save_as(birdsong_t *b, const char *space, const char *path);
int birdsong_open(birdsong_t *b, const char *space, const char *path);

#endif

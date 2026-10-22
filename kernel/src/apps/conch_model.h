/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* conch_model.h — Conch, hearing devices and sound alerts, as a model
 * (zxm.h): the hearing devices paired, each band's gain under the owner's
 * cap, the balance and mono, and the notices zxvd keeps (notes.list) shown
 * as a screen flash and vibration, timed by zxvd's clock (sys.info). The
 * rules are conch_rules.h (CN1-CN4), the same ones the app's Eve, KRON and
 * MYRA blocks run. No drawing, no allocation, integers only. Advisory
 * only: not a clinical fitting; the cap is a safety ceiling the owner sets.
 *
 *   CM1 DEVICES. zxvd has no audio-device method, so the pairing is kept
 *       here: at most CONCH_DEVICES, each a name of 1..CONCH_NAME
 *       printable ASCII bytes, one name once, on the left, the right or
 *       both ears.
 *   CM2 LEVELS. Each of CONCH_BANDS bands has a gain (tenths of a dB). A
 *       band's level is the loudest input the owner expects (tenths of a
 *       dB SPL, CONCH_LOUDEST_START to begin with) plus its gain, and it
 *       must stay under the cap (CONCH_CAP_START to begin with) by CN1. A
 *       gain, cap or loudest input that would put any band past the cap
 *       is refused and nothing changes: the owner lowers a gain first.
 *   CM3 PLAYING. conch_band_level is what a band plays for an input
 *       level: input + gain, or the cap when that would pass it (the
 *       safety ceiling), with *limited saying so, so the view shows it.
 *   CM4 ALERTS. Each sound kind (CN3) has a flash and vibrate pattern. A
 *       refresh reads sys.info and notes.list; a notice is unread and
 *       fresh when its seq is new or its count went up since the refresh
 *       before. The flash and vibrate to show now are the largest
 *       patterns among the fresh notices; a notice already read is never
 *       flashed.
 *   CM5 A refresh checks every value before it changes anything: a reply
 *       out of bounds, or zxvd's clock going back, leaves what was shown
 *       as it was.
 *   CM6 Advisory and owner-confirmed: every call that changes a setting
 *       takes owner_confirmed, checked after its arguments and before
 *       anything changes; without it ZXM_E_STATE and nothing changes.
 *   CM7 DISMISS. Once the owner has seen the alerts, notes.mark_read marks
 *       zxvd's notices read, on the owner's confirmed word only (nothing
 *       sent without it), and nothing is flashed any more. */
#ifndef CONCH_MODEL_H
#define CONCH_MODEL_H

#include "conch_rules.h"
#include "zxm.h"

#define CONCH_DEVICES       4u
#define CONCH_NAME          31u
#define CONCH_BANDS         8u  /* octave bands, 125 Hz .. 16 kHz */
#define CONCH_NOTES         20u /* notes.list answers the newest 20 */
#define CONCH_KIND          15u
#define CONCH_SOURCE        23u                      /* zx_notify.h ZXN_SOURCE_MAX - 1 */
#define CONCH_TITLE         63u                      /* ZXN_TITLE_MAX - 1 */
#define CONCH_PRI_URGENT    3u                       /* zx_notify.h ZXN_PRI_URGENT */
#define CONCH_CAP_START     INT64_C(900)             /* 90 dB SPL */
#define CONCH_LOUDEST_START INT64_C(650)             /* 65 dB SPL, speech */
#define CONCH_MS_MAX        INT64_C(253402300799999) /* 9999-12-31 */

typedef struct {
    char name[CONCH_NAME + 1];
    uint8_t side; /* CONCH_LEFT, CONCH_RIGHT or CONCH_BOTH_EARS */
} conch_device_t;

typedef struct {
    uint8_t flash, vibrate;
} conch_pattern_t;

typedef struct {
    uint32_t seq, count;
    uint8_t pri, sound, flash, vibrate;
    bool read, fresh;
    int64_t seen_ms; /* zxvd's clock when the newest of its count was first seen */
    char kind[CONCH_KIND + 1];
    char source[CONCH_SOURCE + 1];
    char title[CONCH_TITLE + 1];
} conch_alert_t;

typedef struct {
    zxm_link_t *link;
    conch_device_t dev[CONCH_DEVICES];
    uint32_t ndev;
    int64_t gain[CONCH_BANDS];
    int64_t cap, loudest, balance;
    bool mono;
    conch_pattern_t map[CONCH_SOUNDS + 1]; /* by sound kind; 0 unused */
    bool seen;                             /* a refresh has run */
    int64_t now_ms;
    conch_alert_t alert[CONCH_NOTES];
    uint32_t nalert, unread, fresh;
    uint8_t flash_now, vibrate_now;
} conch_t;

void conch_init(conch_t *c, zxm_link_t *link);
/* CM1 */
int conch_pair(conch_t *c, const char *name, uint32_t side, bool owner_confirmed);
int conch_unpair(conch_t *c, const char *name, bool owner_confirmed);
const conch_device_t *conch_device(const conch_t *c, const char *name);
/* CM2 */
int conch_set_gain(conch_t *c, uint32_t band, int64_t gain, bool owner_confirmed);
int conch_set_cap(conch_t *c, int64_t cap, bool owner_confirmed);
int conch_set_loudest(conch_t *c, int64_t loudest, bool owner_confirmed);
/* CM3: input in tenths of a dB SPL, 0..CONCH_SPL_MAX. */
int conch_band_level(const conch_t *c, uint32_t band, int64_t input, int64_t *out, bool *limited);
/* CN2, CN4 */
int conch_set_balance(conch_t *c, int64_t balance, bool mono, bool owner_confirmed);
int64_t conch_ear(const conch_t *c, uint32_t side);
/* CM4 */
int conch_set_alert(conch_t *c, int64_t sound, int64_t flash, int64_t vibrate,
                    bool owner_confirmed);
uint8_t conch_sound_of(const char *kind, uint32_t pri);
int conch_refresh(conch_t *c);
/* CM7: notes.mark_read on the owner's word; *marked is how many zxvd marked. */
int conch_dismiss(conch_t *c, uint32_t *marked, bool owner_confirmed);
const conch_alert_t *conch_alert(const conch_t *c, uint32_t seq);

#endif

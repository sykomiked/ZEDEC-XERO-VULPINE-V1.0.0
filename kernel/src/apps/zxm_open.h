/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxm_open.h — opening an app from its recipe, the launch screen following
 * the real load.
 *
 * An app install is a public fabrication recipe (docs/spec/FABRICATION.md,
 * section 5). Opening one that is not on disk yet means receiving it in
 * steps from zxvd (zxvd_fab.h B13), and every answer goes to the app's
 * launch screen, so its bars and its sounds move only as fast as the work.
 * The desktop opens the app's window first (zui_launch_theme_open, not
 * warm), then drives one zxm_open_t, calling zxm_open_pump once a frame;
 * its load function is a three-line adapter onto zui_launch_theme_load for
 * that window (the phases below have zui_launch_theme.h's LT8 values).
 *
 *   O1 START. zxm_open_start asks fab.receive.start {recipe, out, trust,
 *      airgap}: the recipe's signature is checked, and its sender must be on
 *      the trust list when one is given (ntrust 0: any signer, the signature
 *      still checked). The answer's target CID, length and block total are
 *      kept, and the screen gets FETCH with 0 of the total.
 *   O2 STEP. Each zxm_open_pump asks one fab.receive.step {job, blocks} and
 *      hands the screen the blocks verified so far. A step answering phase
 *      "done" finishes the open only when its CID and length are the ones
 *      the start announced; the screen then gets DONE, which alone may open
 *      its doors.
 *   O3 REFUSE. An error answer, a broken one, counts that go backwards or
 *      past the total, a total that changes, or a "done" for some other
 *      target ends the open as FAILED: the screen gets FAILED (it never
 *      ends), zxvd is asked to drop the job when it may still hold it, and
 *      why says what went wrong (zxvd's own message for an error answer).
 *      zxvd writes `out` before it answers "done", so after a "done" for
 *      another target the file at out is not the app: never run it.
 *   O4 CANCEL. zxm_open_cancel ends a running open the same way, asking
 *      fab.receive.cancel.
 *
 * No allocation; the caller owns the struct and the link. */
#ifndef ZXM_OPEN_H
#define ZXM_OPEN_H

#include <stdbool.h>
#include <stdint.h>

#include "zxm.h"

/* What the screen is told (zui_launch_theme.h LT8: ZUI_LT_FETCH, _DONE, _FAILED). */
enum { ZXM_OPEN_FETCH = 0, ZXM_OPEN_DONE = 1, ZXM_OPEN_FAILED = 2 };
/* Where an open stands. */
enum { ZXM_OPEN_IDLE = 0, ZXM_OPEN_RUNNING, ZXM_OPEN_FINISHED, ZXM_OPEN_STOPPED };

#define ZXM_OPEN_CID_MAX  96u /* a CIDv1 string */
#define ZXM_OPEN_TRUST    16u /* sender ids on one trust list */
#define ZXM_OPEN_STEP     16u /* blocks asked per step (zxvd's default) */
#define ZXM_OPEN_STEP_MAX 4096u

typedef void (*zxm_open_load_fn)(void *ctx, uint32_t phase, uint32_t blocks_done,
                                 uint32_t blocks_total);

typedef struct {
    zxm_link_t *link;
    zxm_open_load_fn load;
    void *ctx;
    uint32_t state; /* ZXM_OPEN_IDLE.. */
    uint64_t job;
    uint32_t done, total; /* blocks verified so far, of all */
    uint32_t step;        /* blocks asked per step */
    uint64_t len;         /* the app's length in bytes */
    char cid[ZXM_OPEN_CID_MAX];
    char why[ZXM_ERR_MAX];
} zxm_open_t;

/* O1. trust: ntrust sender ids (64 hex digits each, the SHA-256 of the
 * sender's ML-DSA-87 key), at most ZXM_OPEN_TRUST. step: blocks per pump,
 * 1..ZXM_OPEN_STEP_MAX (0: ZXM_OPEN_STEP). Returns ZXM_OK with the open
 * RUNNING, ZXM_E_ARG for bad arguments (nothing is asked, the screen is not
 * told), or the call's error with the open STOPPED and FAILED told. */
int zxm_open_start(zxm_open_t *o, zxm_link_t *link, const char *recipe, const char *out,
                   const char *const *trust, uint32_t ntrust, bool airgap, uint32_t step,
                   zxm_open_load_fn load, void *ctx);

/* O2/O3: one step. Returns the state after it (RUNNING, FINISHED or
 * STOPPED); an open that is not running is left as it is. */
uint32_t zxm_open_pump(zxm_open_t *o);

/* O4. Returns ZXM_OK, or ZXM_E_STATE when the open is not running. */
int zxm_open_cancel(zxm_open_t *o);

#endif /* ZXM_OPEN_H */

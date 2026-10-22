/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_boot.h — the OS boot screen, drawn and sounded live, at the pace of
 * the real load, on every target.
 *
 * The owner (2026-10-15): no video. The intro and the boot sounds are made
 * at boot, as the boot screen itself, and they keep pace with the system
 * loading. Booting in a browser is still booting ("the space between
 * spaces"): the same screen, the same sounds, the same vault, wherever it
 * boots. So this module knows nothing of where it runs. Its host (the
 * browser page, the UEFI loader, the Mac app) tells it how much of the
 * system has really loaded, hands it the logos as they arrive, and pulls
 * frames and sound from it.
 *
 *   B1 THE SCREEN. All gold on #002911: the fox large at the top, the five
 *      logos on carved tiles below (36N9, ACOTO, the ASCW crest in the
 *      centre, SMIC, CIS), the gathering bar under them, the release bar
 *      under the fox, the bolts across the centre seam. The fox is built
 *      in (zui/brand/fox.zxvg); each logo is ZXVG bytes the host hands over
 *      once read from the sealed assets (zui_boot_logo). A logo not handed
 *      over leaves its tile carved and empty: nothing is drawn in its place.
 *   B2 REAL PACE. zui_boot_load(done, total) is the only thing that moves
 *      the bars: units of verified work (bytes checked against their hash,
 *      modules loaded). The tiles light outer pair first, the crest last,
 *      as the gathering bar passes them; then the fox lights and the bar
 *      under it fills with the rest. A total of 0 means the remaining work
 *      is not known yet: the heads breathe at the centre, no number shown.
 *      Counts that go backwards, past the total, or a total that changes
 *      are refused (-1, nothing changes).
 *   B3 SOUND AT THE SAME PACE. The chime sounds as the screen first draws,
 *      a coin as each tile's gold actually comes up on screen (the draw
 *      reports it, so sound and light happen together), and the gong only
 *      at zui_boot_ready. The sound setting and mute are zxl_prefs_t's.
 *      Nothing is recorded: zxl_synth makes every sample as it is pulled.
 *   B4 READY. zui_boot_ready: everything loaded. The bar fills, the gong
 *      sounds, and the vault opens (bolts throw back, the screen splits
 *      and slides to the edges) over ZUI_BOOT_OPEN_MS onto whatever the host
 *      draws behind (the login screen). Only the doors are timed: they are
 *      the reveal, not progress. Reduced motion opens them at once.
 *   B5 FAILED. zui_boot_fail: the screen stays where the load stopped and
 *      the gong never sounds; the host shows why.
 *
 * Integer only, no allocation, deterministic: the same calls give the same
 * pixels and the same samples on every target. */
#ifndef ZXV_ZUI_BOOT_H
#define ZXV_ZUI_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_progress.h"
#include "zui_vec.h"
#include "zxl_launch.h"
#include "zxl_synth.h"

#define ZUI_BOOT_LOGOS   5
#define ZUI_BOOT_OPEN_MS 1600u /* the doors, once ready */

/* the five tiles, left to right */
enum { ZUI_BOOT_36N9 = 0, ZUI_BOOT_ACOTO, ZUI_BOOT_ASCW, ZUI_BOOT_SMIC, ZUI_BOOT_CIS };

typedef enum {
    ZUI_BOOT_LOADING = 0,
    ZUI_BOOT_OPENING,
    ZUI_BOOT_DONE,
    ZUI_BOOT_FAILED
} zui_boot_phase_t;

typedef struct zui_boot zui_boot_t;

typedef struct {
    zui_boot_t *owner;
    uint32_t part; /* 0..4 a logo, 5 the fox */
} zui_boot_ref_t;

struct zui_boot {
    zxl_launch_t launch;
    zxl_player_t player;
    zxl_prefs_t prefs;
    zui_vec_work_t work;
    const uint8_t *logo[ZUI_BOOT_LOGOS];
    uint32_t logo_n[ZUI_BOOT_LOGOS];
    zui_boot_ref_t ref[ZUI_BOOT_LOGOS + 1];
    uint64_t done, total; /* the real load, as the host counts it */
    uint32_t p;           /* the screen's progress 0..256 */
    uint32_t open_ms;     /* the doors, once ready */
    uint32_t tick;        /* animation clock (breathing heads), ms */
    uint8_t phase;        /* zui_boot_phase_t */
    bool started, reduce_motion;
};

/* B1. prefs NULL: the defaults (sound on). */
void zui_boot_init(zui_boot_t *b, const zxl_prefs_t *prefs, bool reduce_motion);

/* B1. Hand over logo `which` (ZUI_BOOT_*): ZXVG bytes, kept by reference
 * (the host keeps them alive). 0, or -1 for a bad index or a malformed file
 * (the tile stays empty). */
int zui_boot_logo(zui_boot_t *b, uint32_t which, const uint8_t *zxvg, uint32_t n);

/* B2. 0, or -1 when refused. */
int zui_boot_load(zui_boot_t *b, uint64_t done, uint64_t total);

/* B4 / B5. */
void zui_boot_ready(zui_boot_t *b);
void zui_boot_fail(zui_boot_t *b);

/* Time passes (the doors, the breathing heads). */
void zui_boot_tick(zui_boot_t *b, uint32_t ms);

/* Draw a frame. face and behind are scratch surfaces the size of s (face
 * gets the screen while the doors open; behind is what the host drew behind
 * them, the login screen; NULL draws the ground). */
void zui_boot_draw(zui_boot_t *b, zui_surface_t *s, zui_surface_t *face,
                   const zui_surface_t *behind);

/* B3. Pull frames of sound: 48 kHz stereo interleaved int16. Returns frames
 * written (always `frames`; silence when nothing sounds). */
uint32_t zui_boot_audio(zui_boot_t *b, int16_t *lr, uint32_t frames);

/* The screen's progress 0..256, and where the boot stands. */
static inline uint32_t zui_boot_progress(const zui_boot_t *b)
{
    return b->p;
}
static inline zui_boot_phase_t zui_boot_phase(const zui_boot_t *b)
{
    return (zui_boot_phase_t) b->phase;
}

#endif /* ZXV_ZUI_BOOT_H */

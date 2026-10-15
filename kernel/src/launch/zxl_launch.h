/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxl_launch.h — app launch screens: one sequence, a theme per app.
 *
 * The owner asked (2026-10-12) for every app to open with the boot screen
 * itself, played inside the window the person opened for the app: the big
 * top-centre logo becomes the app's icon instead of the OS fox, the boot's
 * same five logos stay on the tiles underneath, and the vault opens inside the
 * window onto the app. Each app keeps its own beginning, middle and end sound.
 * It is an OS feature, not per-app work: an app maker picks a sound sequence
 * (zxl_theme_pick) and gives an icon; everything else is the system's.
 * System apps' icons are held to the palette (gold on #002911); an app a
 * person makes may colour its icon however it likes (zxl_icon_ink).
 * This is the logic behind that screen. Drawing lives in zui
 * (zui_launch_draw, zui_vault_draw) inside the app's window; this file decides
 * WHAT happens and WHEN, from the app's real loading, so the screen can never
 * run ahead of the work.
 *
 * The sequence (same grammar as the boot):
 *   BEGIN    the window opens on the screen: the app's icon dim at the top,
 *            its load parts on carved tiles; the begin cue plays.
 *   TICK     a tile lights when its part of the real load is done AND the
 *            gathering bar's head has reached it (the five tiles are the
 *            app's load in five parts; ZXL_TILES). Each lit part plays one pitched tick, the
 *            k-th part the k-th step up the app's scale, the centre the top
 *            step: the middle cue IS the progress, never a loop.
 *   LIGHT    every part is done: the icon lights and warms (silent); the bar
 *            under it grows with the rest of the load while the vault's bolts
 *            lock across the seam.
 *   END      the rest of the load is done: the end cue plays, the bolts throw
 *            back and the window's face splits onto the app. Never before.
 *
 * Unknown remaining time never fakes a number: the bar breathes and no END
 * fires until the app says it is ready.
 *
 * What a theme carries: the app's key, a timbre family and a faint ambient
 * motion behind the tiles (the screen and the vault are the boot's). Twelve
 * group defaults cover all 144 apps; a few apps have their own entry. The OS
 * boot keeps E and the gong; group defaults never use E, so the boot stays
 * the biggest moment (Murmuration, the swarm itself, is the one app in E).
 *
 * Sound preferences: launch sounds are ON by default with a mute setting
 * (Keystone > Sound > "App launch sounds"). They also stay silent when the
 * system is muted or a focus mode is on. With a screen reader running the
 * ticks are dropped and begin/end are ducked so they never talk over speech.
 * Reduce motion swaps the vault for a short cross-fade.
 *
 * Integer only, freestanding (no libc), deterministic.
 */
#ifndef ZXV_ZXL_LAUNCH_H
#define ZXV_ZXL_LAUNCH_H

#include <stdint.h>

/* ---- themes ------------------------------------------------------------- */
typedef enum {
    ZXL_REVEAL_VAULT = 0, /* the boot's: bolts throw back, the window's face splits */
    ZXL_REVEAL_FADE,      /* reduce motion: a short cross-fade                      */
    ZXL_REVEAL_COUNT
} zxl_reveal_t;

typedef enum {
    ZXL_AMB_NONE = 0,
    ZXL_AMB_ARCHES,
    ZXL_AMB_SPARKS,
    ZXL_AMB_FLOCK,
    ZXL_AMB_DUST,
    ZXL_AMB_WATER,
    ZXL_AMB_RINGS,
    ZXL_AMB_COUNT
} zxl_ambient_t;

typedef enum {
    ZXL_TIMBRE_GLASS = 0,
    ZXL_TIMBRE_MARIMBA,
    ZXL_TIMBRE_CHIP,
    ZXL_TIMBRE_FLUTE,
    ZXL_TIMBRE_BELL,
    ZXL_TIMBRE_COIN,
    ZXL_TIMBRE_PLUCK,
    ZXL_TIMBRE_STRINGS,
    ZXL_TIMBRE_COUNT
} zxl_timbre_t;

/* Pitch classes, C = 0. */
enum {
    ZXL_C = 0,
    ZXL_DB,
    ZXL_D,
    ZXL_EB,
    ZXL_E,
    ZXL_F,
    ZXL_FS,
    ZXL_G,
    ZXL_AB,
    ZXL_A,
    ZXL_BB,
    ZXL_B
};
#define ZXL_BOOT_KEY ZXL_E /* the OS boot's key: chime, coins, gong */
#define ZXL_GROUPS   12    /* the app list's twelve groups, 1..12   */

typedef struct {
    const char *app; /* NULL for a group default                         */
    uint8_t group;   /* 1..12                                            */
    uint8_t key;     /* pitch class                                      */
    uint8_t octave;  /* octave of the tick scale's first step            */
    uint8_t timbre;  /* zxl_timbre_t                                     */
    uint8_t reveal;  /* zxl_reveal_t                                     */
    uint8_t ambient; /* zxl_ambient_t                                    */
    uint8_t motif;   /* 0..ZXL_MOTIFS-1: the shape of its begin and end   */
} zxl_theme_t;

#define ZXL_MOTIFS 12  /* begin/end phrase shapes (tools/launch/launch_sounds.py) */
#define ZXL_APPS   407 /* every app in the catalogue has its own entry   */
#define ZXL_TIER1  144 /* the first tier: twelve groups of twelve        */

/* The theme for an app: its own entry (every app in the 407-app catalogue has one,
 * each with its own sound sequence; case-sensitive name), otherwise its
 * group's default (an app a person made that picked none). A group outside 1..12 gets group 1's.
 * Never returns NULL. */
const zxl_theme_t *zxl_theme_for(const char *app, uint32_t group);

/* The sound sequence an app maker picked, by name: a curated app's name
 * any app on the list ("Banker", "Flint", ...; case-insensitive) or a group's,
 * "group1".."group12".
 * NULL for anything else; the caller then uses zxl_theme_for. */
const zxl_theme_t *zxl_theme_pick(const char *id);

/* The group default for 1..12, or NULL. */
const zxl_theme_t *zxl_group_theme(uint32_t group);

/* Number of apps with their own entry (ZXL_APPS), and the i-th of them in
 * list order (NULL past the end). */
uint32_t zxl_curated_count(void);
const zxl_theme_t *zxl_curated(uint32_t i);

/* The tick for scale step `step` (0 = first part lit, ZXL_TOP_STEP = the
 * centre): a MIDI note number on the app's major pentatonic. */
#define ZXL_TOP_STEP 6
uint32_t zxl_tick_note(const zxl_theme_t *t, uint32_t step);

/* ---- the icon at the top ------------------------------------------------- */
typedef enum {
    ZXL_ICON_PALETTE = 0, /* system app: recoloured to gold on the ZXV green  */
    ZXL_ICON_OWN          /* an app a person made: drawn in its own colours   */
} zxl_icon_ink_t;

#define ZXL_GROUND_RGB 0x002911u /* the boot's ground  */
#define ZXL_GOLD_RGB   0xF2C230u /* the boot's gold    */

zxl_icon_ink_t zxl_icon_ink(int system_app);

/* The app's icon file in the catalogue, relative to the icons folder
 * ("001-burrow.png", "l2/145-dragon-archivist.png", ...), or NULL for an app
 * not in it. Only the names are built in; the pictures are read from disk. */
const char *zxl_icon_file(const char *app);

/* ---- preferences -------------------------------------------------------- */
typedef struct {
    uint8_t sounds;        /* the setting: 1 = app launch sounds on (default) */
    uint8_t system_muted;  /* output muted                                    */
    uint8_t focus_quiet;   /* a focus / do-not-disturb mode is on             */
    uint8_t screen_reader; /* a screen reader is speaking                     */
    uint8_t reduce_motion; /* reduce motion                                   */
    uint16_t volume_q8;    /* launch sound volume, 0..256 (256 = full)        */
} zxl_prefs_t;

#define ZXL_SETTING_KEY "sound.app_launch" /* Keystone's key for the setting */
#define ZXL_DUCK_Q8     96                 /* begin/end gain under a screen reader */

zxl_prefs_t zxl_prefs_default(void);

/* The reveal to draw after preferences: FADE under reduce motion. */
zxl_reveal_t zxl_effective_reveal(const zxl_theme_t *t, const zxl_prefs_t *p);

/* ---- one launch --------------------------------------------------------- */
#define ZXL_TILES     5 /* the boot's five logo tiles: every launch screen has them */
#define ZXL_MAX_PARTS 7
#define ZXL_FULL      256u
#define ZXL_UNKNOWN   0xFFFFFFFFu

typedef enum { ZXL_EV_BEGIN = 1, ZXL_EV_TICK, ZXL_EV_LIGHT, ZXL_EV_END } zxl_event_kind_t;

typedef struct {
    uint8_t kind;     /* zxl_event_kind_t                                    */
    uint8_t part;     /* TICK: which part lit                                */
    uint8_t step;     /* TICK: scale step (0.., ZXL_TOP_STEP for the centre) */
    uint8_t audible;  /* 1 when the cue should sound                         */
    uint16_t gain_q8; /* cue gain 0..256 when audible                        */
    uint16_t note;    /* TICK: MIDI note                                     */
} zxl_event_t;

#define ZXL_QUEUE 16

typedef enum { ZXL_PH_GATHER = 0, ZXL_PH_RELEASE, ZXL_PH_OPEN } zxl_phase_t;

typedef struct {
    const zxl_theme_t *theme;
    zxl_prefs_t prefs;
    uint32_t n;                     /* parts, 1..ZXL_MAX_PARTS                */
    uint32_t part_p[ZXL_MAX_PARTS]; /* each part's real progress 0..256       */
    uint32_t lit;                   /* bit i: part i lit                      */
    uint32_t nlit;
    uint32_t rest; /* release progress 0..256 or ZXL_UNKNOWN */
    uint8_t phase; /* zxl_phase_t                            */
    uint8_t warm;  /* opened already loaded: END only        */
    zxl_event_t q[ZXL_QUEUE];
    uint32_t qh, qn;
} zxl_launch_t;

/* Start a launch with n parts (clamped to 1..ZXL_MAX_PARTS). A warm launch
 * (the app is already loaded) skips the gathering: no BEGIN, no ticks, END at
 * once. Returns 0, or -1 on NULL arguments. */
int zxl_launch_init(zxl_launch_t *l, const zxl_theme_t *t, uint32_t n, const zxl_prefs_t *prefs,
                    int warm);

/* Report a part's real progress (0..256, larger values clamp). Progress never
 * goes backwards: a lower value than already reported is ignored. Returns 0,
 * or -1 for a bad index. */
int zxl_launch_part(zxl_launch_t *l, uint32_t part, uint32_t p);

/* Report the rest of the load (the release bar): 0..256 or ZXL_UNKNOWN. */
void zxl_launch_rest(zxl_launch_t *l, uint32_t p);

/* The gathering bar's progress 0..256: the mean of the parts' progress. */
uint32_t zxl_gather_p(const zxl_launch_t *l);

/* The screen lit tile `part` itself (when the drawing decides a tile lights,
 * as zui's launch screen does from the gathering bar's heads): its tick plays
 * now, the k-th tile lit the k-th step, the centre the top step. Only while
 * gathering. Returns 0, or -1 for a bad index, a tile already lit, or a launch
 * past gathering. */
int zxl_launch_lit(zxl_launch_t *l, uint32_t part);

/* The app is ready. Any tile not lit yet lights without a tick (a window that
 * was not on screen while it gathered), the mark lights if it has not, and
 * END follows. Nothing happens once END has been queued. */
void zxl_launch_finish(zxl_launch_t *l);

/* Take the next event; returns 1 and fills *ev, or 0 when none is waiting. */
int zxl_launch_next(zxl_launch_t *l, zxl_event_t *ev);

#endif /* ZXV_ZXL_LAUNCH_H */

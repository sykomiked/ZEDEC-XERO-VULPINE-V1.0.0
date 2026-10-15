/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_dock.h — the astrolabe dock: the apps, open and pinned, kept in a
 * corner seal that blooms into rings.
 *
 * Neither a bar along an edge nor a row along the bottom: at rest the dock
 * is one small engraved seal in a screen corner, so windows keep the whole
 * screen but that corner. Asked for, it opens like an astrolabe: quarter
 * rings around the seal carry the apps, and a ring that holds more than it
 * shows turns, like the rete over the plate, to bring the rest round.
 *
 *   DK1 THE SEAL. At rest the dock is a quarter disc of radius `seal`
 *       (ZUI_DOCK_SEAL by default, ZUI_DOCK_SEAL_MIN..ZUI_DOCK_SEAL_MAX)
 *       centred on a corner of the screen, bottom left by default, any
 *       corner by zui_dock_place. It carries the fox's mark (the icon
 *       callback with no item) inside an engraved limb with graduations.
 *       zui_dock_keepout is its square: the only part of the screen the
 *       resting dock takes. zui_dock_corner_for_edge maps the panel edge of
 *       zui_workspace.h WS4 to a corner (the corner that edge starts from,
 *       going anticlockwise: bottom - bottom left, left - top left, right -
 *       bottom right, top - top right) and panel.size is the seal's radius.
 *       Auto-hide draws the seal faint until the pointer reaches it or an
 *       app needs input.
 *   DK2 THE BLOOM. Moving the pointer onto the seal, pressing or tapping it,
 *       or the shortcut (zui_dock_t.chord_key with chord_mods, Cmd-Shift-D
 *       by default) opens the rings. Ring 0, nearest the seal, holds the open
 *       apps, most recently used first; ring 1 holds the pinned apps that
 *       are not open, in the order pinned; ring 2, drawn faint, appears only
 *       when rings 0 and 1 cannot show everything, and takes what is left
 *       (open apps first). A ring shows as many slots as its arc allows, so
 *       icons never overlap and stay ZUI_DOCK_EDGE clear of the screen
 *       edges; a ring with more apps than slots turns (DK4). On a screen too
 *       small for ring 2, rings 0 and 1 keep all their apps and turn. Slots
 *       run left to right on the screen, centred on the ring's arc.
 *   DK3 SIZES. Icons are ZUI_DOCK_ICON across on pointer screens and
 *       ZUI_DOCK_ICON_T (at least ZUI_TOUCH_MIN) on touch screens; each
 *       slot's target is the icon and ZUI_DOCK_PAD round it. Rings are
 *       icon + 14 apart; the first is two icons and 22 out from the seal.
 *   DK4 TURNING. A scroll (wheel or two-finger swipe: dx or dy, one step per
 *       ZUI_DOCK_SCROLL px) turns the ring under the pointer, or the
 *       outermost ring that turns. Arrow keys past the end of a turning ring
 *       turn it. Turning takes ZUI_DOCK_SPIN_MS, the apps sliding one slot
 *       along the arc; the one leaving fades out at one end as the next
 *       fades in at the other. A ring that turns shows three fading marks
 *       past each end of its arc.
 *   DK5 CLOSING. The bloom closes ZUI_DOCK_LEAVE_MS after the pointer leaves
 *       it (coming back in time keeps it open), on Esc, on a press outside
 *       it, on pressing the seal again, and after choosing an app. Opened
 *       by keyboard or touch it stays until one of the others. Opening and
 *       closing take ZUI_DOCK_BLOOM_MS of zui_motion time (zui_motion.h MO4:
 *       instant with reduced motion); the rings grow out from the seal.
 *   DK6 CHOOSING. Pressing a slot and letting go on it, Enter or Space on
 *       the focused slot, or pressing the seal and letting go on a slot
 *       (one gesture: press, sweep, release) chooses that app:
 *       zui_dock_event returns ZUI_DOCK_CHOSE and zui_dock_chosen names it.
 *       What choosing does is the owner's (zui_wm.h WM9: launch, raise, or
 *       minimise the app in front).
 *   DK7 ACTIVITY. Every slot has a pip for its app's activity: STOPPED (a
 *       pinned app that is not running: a still, hollow ring), IDLE (open,
 *       waiting: a muted dot that breathes slowly, ZUI_DOCK_SLOW_MS), BUSY
 *       (running a process: a green dot at a medium pulse,
 *       ZUI_DOCK_MEDIUM_MS), ASK (needs the person: a gold dot with a halo
 *       at a fast pulse, ZUI_DOCK_FAST_MS, under 1.4 a second). Each state differs in shape and
 * colour as well as speed, so with reduced motion (no pulse) they still read. While any app asks,
 * the resting seal carries a small gold tick on its rim. A launching app shows its gathering bar
 * (zui_progress.h) under its icon. DK8 KEYBOARD. Opened by the shortcut, the first slot has focus.
 * Left and Right move along a ring (turning it at the ends), Up and Down move to the next ring out
 * or in (Up is outward for a bottom corner), to the slot nearest the one left; Tab and Shift-Tab
 * step through every slot; Home and End go to a ring's ends; Enter or Space chooses; Esc closes. A
 * bloom opened by the pointer takes no keys. DK9 SPOKEN. Each slot is a button named "<title>,
 * <state>" (not running, open, running, needs input; then ", minimised" or ", in front"):
 * zui_dock_say. zui_dock_a11y writes the dock as the reader sees it, in zui_a11y_text's form;
 * on_move is told when focus or the pointer moves to another slot, so a reader can speak it. DK10
 * LOOK. Drawn on the ground (#002911) as carving: the seal and the rings are engraved, the open
 * bloom a carved panel step. The one gold element is the ring round the active slot (focused, else
 * under the pointer, else the app in front); a needs-input pip and the seal's tick are gold as
 * attention, never as decoration. Icons come from a callback (zui_vec art plugs in there); without
 * one each app is a carved disc with its initial. The phone (compact) form is a half seal at the
 * bottom centre with half rings rising from it. DK11 Integer only (a quarter-wave sine table;
 * angles in 1/4096 turn), no allocation, no libc, deterministic: the same events and ticks of
 *       elapsed ms give the same pixels. Time comes only from the caller. */
#ifndef ZXV_ZUI_DOCK_H
#define ZXV_ZUI_DOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_input.h"

#define ZUI_DOCK_ITEMS     32u /* open and pinned apps it holds          */
#define ZUI_DOCK_PINNED    16u /* pinned apps                            */
#define ZUI_DOCK_RINGS     3u
#define ZUI_DOCK_SLOTS     24u /* slots one ring shows at most           */
#define ZUI_DOCK_NAME      48u
#define ZUI_DOCK_SEAL      56 /* DK1: the seal's radius                */
#define ZUI_DOCK_SEAL_MIN  44
#define ZUI_DOCK_SEAL_MAX  128
#define ZUI_DOCK_ICON      36 /* DK3: pointer screens                  */
#define ZUI_DOCK_ICON_T    48 /* touch screens                         */
#define ZUI_DOCK_PAD       4  /* target margin round an icon           */
#define ZUI_DOCK_EDGE      6  /* icons stay this clear of the edges    */
#define ZUI_DOCK_BLOOM_MS  160
#define ZUI_DOCK_SPIN_MS   140
#define ZUI_DOCK_LEAVE_MS  450
#define ZUI_DOCK_SCROLL    40    /* px of scroll per step of turning      */
#define ZUI_DOCK_TURN      4096  /* angle units in a full turn          */
#define ZUI_DOCK_SLOW_MS   4000u /* DK7: pip pulse periods              */
#define ZUI_DOCK_MEDIUM_MS 1600u
#define ZUI_DOCK_FAST_MS   750u

typedef enum {
    ZUI_DOCK_BOTTOM_LEFT = 0,
    ZUI_DOCK_BOTTOM_RIGHT,
    ZUI_DOCK_TOP_LEFT,
    ZUI_DOCK_TOP_RIGHT
} zui_dock_corner_t;

/* DK7: the same values and tempos as zui_kinetic_activity_t (NOT_RUNNING,
 * OPEN_IDLE, RUNNING, NEEDS_INPUT), so an icon callback can hand an item's
 * activity straight to the kinetic engine. */
typedef enum { ZUI_DOCK_STOPPED = 0, ZUI_DOCK_IDLE, ZUI_DOCK_BUSY, ZUI_DOCK_ASK } zui_dock_act_t;

typedef struct {
    char app[ZUI_DOCK_NAME];
    char title[ZUI_DOCK_NAME];
    uint32_t win;      /* the owner's window id; 0 = not open (pinned only) */
    uint8_t activity;  /* zui_dock_act_t */
    uint32_t progress; /* 1..255 while launching: the gathering bar; else 0 */
    bool pinned, front, minimised;
} zui_dock_item_t;

/* One visible slot (DK2). */
typedef struct {
    uint8_t ring, slot;
    uint16_t item; /* index into zui_dock_t.item */
    int32_t cx, cy;
} zui_dock_slot_t;

typedef struct zui_dock zui_dock_t;

/* DK10: draw an app's icon in box at lit 0..256 (256 full; less is dimmer,
 * also used to fade a slot turning in or out). it NULL: the seal's mark. */
typedef void (*zui_dock_icon_fn)(zui_surface_t *s, zui_rect_t box, const zui_dock_item_t *it,
                                 uint32_t lit, void *ctx);

typedef struct {
    int32_t r;            /* radius when open                     */
    int32_t lo, hi;       /* the arc's usable angles (DK2)        */
    int32_t step;         /* angle between neighbouring slots     */
    uint16_t cap, n, rot; /* slots it can show, apps on it, turn  */
    int32_t spin;         /* DK4: ms of turning left              */
    int8_t spin_dir;      /* +1: rot went up                      */
    uint16_t item[ZUI_DOCK_ITEMS];
} zui_dock_ring_t;

enum { ZUI_DOCK_IGNORED = 0, ZUI_DOCK_USED, ZUI_DOCK_CHOSE }; /* zui_dock_event */
enum { ZUI_DOCK_HIT_NONE = 0, ZUI_DOCK_HIT_SEAL, ZUI_DOCK_HIT_PANEL, ZUI_DOCK_HIT_SLOT };
enum { ZUI_DOCK_REST = 0, ZUI_DOCK_BLOOMING, ZUI_DOCK_OPEN, ZUI_DOCK_CLOSING }; /* phase */

struct zui_dock {
    int32_t w, h;
    bool compact, touch, autohide;
    zui_dock_corner_t corner;
    int32_t seal, icon;
    /* the apps: open ones first (most recently used first), then pinned */
    zui_dock_item_t item[ZUI_DOCK_ITEMS];
    uint32_t nitem, nopen;
    char pinned[ZUI_DOCK_PINNED][ZUI_DOCK_NAME], pinned_title[ZUI_DOCK_PINNED][ZUI_DOCK_NAME];
    uint32_t npinned;
    zui_dock_ring_t ring[ZUI_DOCK_RINGS];
    uint32_t nring; /* rings in use */
    /* the bloom (DK2, DK5) */
    uint8_t phase;
    uint32_t t;       /* motion ms into the bloom, 0..ZUI_DOCK_BLOOM_MS */
    bool kbd, sticky; /* opened by keyboard; stays until closed (DK5)   */
    int32_t leave;    /* ms left before closing; < 0 not counting       */
    int32_t focus;    /* keyboard focus, a slot index, or -1            */
    int32_t hot;      /* the slot under the pointer, or -1              */
    int32_t press;    /* -2 none, -1 the seal, else the slot pressed    */
    bool near;        /* the pointer is on the seal (auto-hide)         */
    bool toggle;      /* letting go on the seal closes the bloom        */
    int32_t scroll;   /* DK4: px of scroll not yet turned               */
    int32_t chosen;   /* DK6: the item chosen, or -1                    */
    uint32_t clock;   /* ms, for the pips' pulses                       */
    uint8_t tev;      /* the last touch event, to drop its pointer twin */
    int32_t tx, ty;
    uint32_t tms;
    uint16_t chord_key; /* DK2 */
    uint8_t chord_mods;
    zui_dock_icon_fn icon_fn;
    void *icon_ctx;
    const zui_text_t *text; /* labels and initials; NULL draws none */
    void (*on_move)(zui_dock_t *d, int32_t slot, void *ctx); /* DK9 */
    void *on_move_ctx;
};

void zui_dock_init(zui_dock_t *d, int32_t w, int32_t h, bool touch);
void zui_dock_resize(zui_dock_t *d, int32_t w, int32_t h, bool compact);
/* DK1: seal is clamped to ZUI_DOCK_SEAL_MIN..MAX. */
void zui_dock_place(zui_dock_t *d, zui_dock_corner_t corner, int32_t seal, bool autohide);
zui_dock_corner_t zui_dock_corner_for_edge(int panel_pos); /* zui_panel_pos_t */
zui_rect_t zui_dock_keepout(const zui_dock_t *d);

/* The open apps, most recently used first (at most ZUI_DOCK_ITEMS). */
void zui_dock_set_open(zui_dock_t *d, const zui_dock_item_t *open, uint32_t n);
/* Pinned apps, in order; false when ZUI_DOCK_PINNED are pinned. Pinning one
 * already pinned keeps its place. */
bool zui_dock_pin(zui_dock_t *d, const char *app, const char *title);
bool zui_dock_unpin(zui_dock_t *d, const char *app);

/* DK2: the visible slots, ring by ring, left to right; returns how many. */
uint32_t zui_dock_slots(const zui_dock_t *d, zui_dock_slot_t *out, uint32_t cap);
bool zui_dock_slot(const zui_dock_t *d, int32_t slot, zui_dock_slot_t *out);
/* What is at (x, y): ZUI_DOCK_HIT_*, and the slot when HIT_SLOT. */
int zui_dock_hit(const zui_dock_t *d, int32_t x, int32_t y, int32_t *slot);
bool zui_dock_is_open(const zui_dock_t *d); /* blooming or open */
void zui_dock_bloom(zui_dock_t *d, bool open, bool by_keyboard);
bool zui_dock_turn(zui_dock_t *d, uint32_t ring, int32_t steps); /* DK4 */

int zui_dock_event(zui_dock_t *d, const zui_event_t *e); /* ZUI_DOCK_IGNORED / USED / CHOSE */
const zui_dock_item_t *zui_dock_chosen(const zui_dock_t *d);
void zui_dock_tick(zui_dock_t *d, uint32_t ms); /* elapsed ms */
void zui_dock_draw(const zui_dock_t *d, zui_surface_t *s);

/* DK9: the slot's spoken name; returns its length. */
uint32_t zui_dock_say(const zui_dock_t *d, int32_t slot, char *out, uint32_t cap);
uint32_t zui_dock_a11y(const zui_dock_t *d, char *out, uint32_t cap);

/* DK11: sine and cosine of an angle in 1/4096 turn, times 16384. */
int32_t zui_dock_sin(int32_t a);
int32_t zui_dock_cos(int32_t a);

#endif /* ZXV_ZUI_DOCK_H */

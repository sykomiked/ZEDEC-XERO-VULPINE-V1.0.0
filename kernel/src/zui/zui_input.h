/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_input.h — keyboard, pointer and touch, as one event stream.
 *
 * Every present layer (Metal, Vulkan, Direct3D, X11, the bare-metal
 * framebuffer with its PS/2 or USB HID driver) turns what its platform
 * delivers into zui_event_t and pushes it here. Widgets only ever see these
 * events, so a button behaves the same under a trackpad, a finger, a TV
 * remote and a serial console.
 *
 *   I1 ONE STREAM. A bounded ring (no allocation). A full ring drops the
 *      oldest pointer MOVE first, never a key or a press, so typing is never
 *      lost to a flood of motion.
 *   I2 TOUCH IS A POINTER. A touch with id 0 also arrives as pointer down /
 *      move / up at the same place, so widgets need no touch code. A second
 *      finger is kept as touch only (pinch and two-finger scroll).
 *   I3 GESTURES ARE DERIVED. Tap, double tap and long press come from the
 *      timing of down/up pairs (zui_gesture_feed), in milliseconds the
 *      platform supplies, not from wall-clock reads here. A long press is the
 *      touch way to open the Ask palette or a context menu.
 *   I4 TEXT IS SEPARATE FROM KEYS. ZUI_EV_KEY is a physical key with its
 *      modifiers (shortcuts, navigation); ZUI_EV_TEXT is committed UTF-8 from
 *      the platform's input method; ZUI_EV_PREEDIT is the composition still
 *      being typed (shown underlined at the caret, not yet in the document).
 *
 * Coordinates are device pixels in the window's surface. Integer only,
 * freestanding.
 */
#ifndef ZXV_ZUI_INPUT_H
#define ZXV_ZUI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ZUI_EV_NONE = 0,
    ZUI_EV_KEY_DOWN,    /* key, mods, repeat                          */
    ZUI_EV_KEY_UP,      /* key, mods                                  */
    ZUI_EV_TEXT,        /* text: committed UTF-8                      */
    ZUI_EV_PREEDIT,     /* text: composition in progress ("" ends it) */
    ZUI_EV_PTR_DOWN,    /* x, y, button, mods                         */
    ZUI_EV_PTR_MOVE,    /* x, y, buttons held in `button` as a mask   */
    ZUI_EV_PTR_UP,      /* x, y, button                               */
    ZUI_EV_SCROLL,      /* x, y, dx, dy in pixels (+dy = content up)  */
    ZUI_EV_TOUCH_BEGIN, /* touch_id, x, y                           */
    ZUI_EV_TOUCH_MOVE,
    ZUI_EV_TOUCH_END,
    ZUI_EV_TOUCH_CANCEL,
    ZUI_EV_RESIZE,   /* x = new width, y = new height (pixels)      */
    ZUI_EV_SCALE,    /* x = new scale_q8                            */
    ZUI_EV_FOCUS_IN, /* the window gained keyboard focus            */
    ZUI_EV_FOCUS_OUT,
    ZUI_EV_CLOSE, /* the person asked to close the window        */
    ZUI_EV_TICK   /* time passed with no input (animation)       */
} zui_ev_type_t;

/* Keys that are not text. Printable keys use their lower-case ASCII value
 * ('a'..'z', '0'..'9', ' ', and punctuation) so shortcuts read naturally. */
enum {
    ZUI_KEY_NONE = 0,
    ZUI_KEY_BACKSPACE = 8,
    ZUI_KEY_TAB = 9,
    ZUI_KEY_ENTER = 13,
    ZUI_KEY_ESCAPE = 27,
    ZUI_KEY_SPACE = 32,
    ZUI_KEY_DELETE = 127,
    ZUI_KEY_LEFT = 0x100,
    ZUI_KEY_RIGHT,
    ZUI_KEY_UP,
    ZUI_KEY_DOWN,
    ZUI_KEY_HOME,
    ZUI_KEY_END,
    ZUI_KEY_PAGE_UP,
    ZUI_KEY_PAGE_DOWN,
    ZUI_KEY_INSERT,
    ZUI_KEY_F1,
    ZUI_KEY_F12 = ZUI_KEY_F1 + 11,
    ZUI_KEY_MENU, /* the context-menu key, or a remote's menu button */
    ZUI_KEY_BACK  /* a remote's or phone's back button               */
};

enum {
    ZUI_MOD_SHIFT = 1u << 0,
    ZUI_MOD_CTRL = 1u << 1,
    ZUI_MOD_ALT = 1u << 2,  /* Option on a Mac                     */
    ZUI_MOD_META = 1u << 3, /* Command on a Mac, Windows key       */
    /* The platform's shortcut key: Command on a Mac, Ctrl elsewhere. The
     * present layer sets it alongside the real one, so Cmd-K and Ctrl-K are
     * both ZUI_MOD_CMD | 'k'. */
    ZUI_MOD_CMD = 1u << 4
};

enum { ZUI_BTN_LEFT = 1, ZUI_BTN_RIGHT = 2, ZUI_BTN_MIDDLE = 4 };

#define ZUI_TEXT_EV_MAX 32 /* bytes of UTF-8 in one TEXT / PREEDIT event */

typedef struct {
    uint8_t type; /* zui_ev_type_t */
    uint8_t button;
    uint8_t mods;
    uint8_t repeat; /* a held key's auto-repeat                 */
    uint16_t key;   /* ZUI_KEY_* or ASCII                        */
    uint16_t touch_id;
    int32_t x, y;
    int32_t dx, dy;
    uint32_t ms; /* the platform's monotonic time, ms         */
    char text[ZUI_TEXT_EV_MAX + 1];
} zui_event_t;

#ifndef ZUI_INPUT_RING
#    define ZUI_INPUT_RING 256u /* a power of two */
#endif

typedef struct {
    zui_event_t ev[ZUI_INPUT_RING];
    uint32_t head, tail; /* free-running; head - tail = queued */
    uint32_t dropped;    /* events lost to a full ring           */
    uint8_t buttons;     /* buttons held, from the stream        */
    uint8_t mods;        /* modifiers held, from the stream      */
    int32_t px, py;      /* the last pointer position            */
} zui_queue_t;

void zui_queue_init(zui_queue_t *q);
uint32_t zui_queue_len(const zui_queue_t *q);
/* I1, I2: push one event (a primary touch is also pushed as a pointer). */
void zui_push(zui_queue_t *q, const zui_event_t *e);
bool zui_pop(zui_queue_t *q, zui_event_t *out); /* false when empty */

/* Builders for present layers. text is cut on a UTF-8 boundary to fit. */
void zui_push_key(zui_queue_t *q, bool down, uint16_t key, uint8_t mods, bool repeat, uint32_t ms);
void zui_push_text(zui_queue_t *q, uint8_t type, const char *utf8, uint32_t ms);
void zui_push_ptr(zui_queue_t *q, uint8_t type, int32_t x, int32_t y, uint8_t button, uint32_t ms);
void zui_push_scroll(zui_queue_t *q, int32_t x, int32_t y, int32_t dx, int32_t dy, uint32_t ms);
void zui_push_touch(zui_queue_t *q, uint8_t type, uint16_t id, int32_t x, int32_t y, uint32_t ms);

/* ---- I3: gestures -------------------------------------------------------- */
#define ZUI_TAP_SLOP_PX  10  /* movement allowed in a tap (scaled by caller)  */
#define ZUI_DOUBLE_MS    350 /* second tap within this is a double tap        */
#define ZUI_LONGPRESS_MS 500 /* held this long without moving: long press     */

typedef enum {
    ZUI_GESTURE_NONE = 0,
    ZUI_GESTURE_TAP,
    ZUI_GESTURE_DOUBLE_TAP,
    ZUI_GESTURE_LONG_PRESS, /* fires once while still held */
    ZUI_GESTURE_DRAG        /* moved past the slop: a drag or scroll */
} zui_gesture_t;

typedef struct {
    bool down, moved, long_fired;
    int32_t x0, y0;
    uint32_t t0;
    uint32_t last_tap_ms;
    int32_t last_tap_x, last_tap_y;
    bool have_last_tap;
    int32_t slop_px;
} zui_gesture_state_t;

void zui_gesture_init(zui_gesture_state_t *g, int32_t slop_px);
/* Feed every pointer event (and TICKs, for long press); returns the gesture
 * it completes, if any. */
zui_gesture_t zui_gesture_feed(zui_gesture_state_t *g, const zui_event_t *e);

/* UTF-8 helpers shared by the text field and the queue. */
uint32_t zui_utf8_prev(const char *s, uint32_t at);             /* start of the char before at */
uint32_t zui_utf8_next(const char *s, uint32_t n, uint32_t at); /* end of the char at at   */
uint32_t zui_utf8_fit(const char *s, uint32_t n, uint32_t cap); /* longest whole-char prefix */

#endif /* ZXV_ZUI_INPUT_H */

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_login.h — the login screen the boot vault opens onto.
 *
 * Owner, 2026-10-12: after the boot sequence, a login screen; as many people
 * as the owner wants on one device; a password is each person's choice.
 * Accounts and checking live in kernel/src/seats (Argon2id, back-off); this
 * file only draws and hit-tests, so the same screen works on the bare-metal
 * splash, the Mac app and the desktop shell.
 *
 * Layout, top to bottom on the ground:
 *   the OS mark, small, and the time
 *   the chosen person: a large gold ring with their initials (or their
 *     Chiglet, through `portrait`), their name, then what their lock needs:
 *     nothing (an Enter button), a passphrase field (dots, never the text),
 *     or a hint to use the device's own unlock
 *   a row of everyone on the device, centred when it fits; with more people
 *     than fit it scrolls to keep the chosen person in the middle, so any
 *     number works; "Add person" sits at the end
 *
 * A failed try shakes the field once (shake_q8) and shows the wait, if any,
 * in plain words. Integer only, deterministic.
 */
#ifndef ZXV_ZUI_LOGIN_H
#define ZXV_ZUI_LOGIN_H

#include <stdint.h>
#include <stdbool.h>
#include "zui_canvas.h"
#include "zui_depth.h"

typedef enum { ZUI_LOCK_NONE = 0, ZUI_LOCK_PASS, ZUI_LOCK_PASSKEY } zui_lock_t; /* = seat_lock_t */

typedef struct {
    const char *name;
    uint32_t color; /* the person's accent; 0 = gold */
    uint8_t lock;   /* zui_lock_t */
    bool guest;
} zui_login_person_t;

typedef void (*zui_portrait_fn)(zui_surface_t *s, zui_rect_t box, uint32_t index, void *ctx);

typedef struct {
    const zui_login_person_t *people;
    uint32_t n;
    uint32_t chosen;          /* index into people                          */
    uint32_t typed;           /* characters typed in the passphrase field   */
    uint32_t wait_s;          /* back-off left, 0 = none                    */
    bool wrong;               /* the last try failed                        */
    bool can_add;             /* show "Add person" (an owner is present)    */
    uint32_t shake_q8;        /* 0..256 progress of the wrong-try shake      */
    const char *clock;        /* e.g. "06:12"; may be NULL                   */
    zui_portrait_fn portrait; /* draws a person's picture; NULL = initials  */
    void *portrait_ctx;
    void (*mark)(zui_surface_t *s, zui_rect_t box, void *ctx); /* OS mark; may be NULL */
    void *mark_ctx;
    const zui_depth_t *depth; /* FOIL: the ring, field and row shimmer; may be NULL */
} zui_login_t;

/* Fonts: big for the name, small for everything else, and optionally larger
 * faces for the initials in the big ring and in the row (fall back to big and
 * small). Any may be NULL: shapes are still drawn, e.g. in tests. */
typedef struct {
    const zui_text_t *big, *small, *initials, *row_initials;
} zui_login_fonts_t;

/* What a point on the screen is. */
typedef enum {
    ZUI_LOGIN_NOTHING = 0,
    ZUI_LOGIN_PERSON, /* *index = which person in the row */
    ZUI_LOGIN_FIELD,  /* the passphrase field             */
    ZUI_LOGIN_ENTER,  /* the Enter button                 */
    ZUI_LOGIN_ADD     /* Add person                       */
} zui_login_hit_t;

void zui_login_draw(zui_surface_t *s, zui_rect_t area, int32_t scale_q8, const zui_login_t *l,
                    const zui_login_fonts_t *f, uint32_t tick);
zui_login_hit_t zui_login_hit(zui_rect_t area, int32_t scale_q8, const zui_login_t *l, int32_t x,
                              int32_t y, uint32_t *index);

/* Up to two initials from a name ("Ana Maria" -> "AM"), ASCII letters and
 * digits only, upper-cased; out must hold 3 bytes. */
void zui_initials(const char *name, char out[3]);

#endif /* ZXV_ZUI_LOGIN_H */

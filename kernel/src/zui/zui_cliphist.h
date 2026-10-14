/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_cliphist.h — clipboard history: the last things copied, to paste
 * again, with secrets kept out.
 *
 * The history sits between the toolkit and the system clipboard: plugged
 * into zui_ui_t's clip_set / clip_get (zui_cliphist_attach), every copy and
 * cut in a zui view lands here as well as on the clipboard.
 *
 *   CH1 A RING of the most recent clips, newest first, up to ZUI_CLIP_MAX,
 *       each text with a type tag (plain, link, path, code, number). A
 *       clip longer than ZUI_CLIP_TEXT - 1 bytes is not kept in the
 *       history (it is still copied).
 *   CH2 DEDUPE. Copying text that is already in the history (same bytes,
 *       same type) moves that clip to the front instead of adding another;
 *       a pinned clip stays pinned.
 *   CH3 PIN, DELETE, CLEAR. A pinned clip is never evicted; clear removes
 *       every unpinned clip (and the pinned ones too when asked). Removed
 *       text is wiped, not just forgotten.
 *   CH4 A BYTE CAP. The kept text never totals more than `cap_bytes`
 *       (ZUI_CLIP_CAP unless set): the oldest unpinned clips go first. A
 *       new clip that cannot fit even then is not kept.
 *   CH5 SECRETS ARE NEVER STORED. A clip the source marks sensitive
 *       (ZUI_CLIP_SENSITIVE: a password, a one-time code, a key) and any
 *       copy made while a secret field has the focus are never written to
 *       the history. A sensitive clip is still on the clipboard to paste
 *       once, held in one buffer that the next copy, a clear, or
 *       zui_cliphist_forget overwrites with zeros.
 *   CH6 THE TOOLKIT HOOK. zui_cliphist_attach keeps the view's existing
 *       clip_set / clip_get (the platform clipboard, when the present
 *       layer supplied one) and calls them after the history has seen the
 *       copy, so the system clipboard keeps working. A paste reads the
 *       platform clipboard when there is one, else the last copy.
 *   CH7 Integer only, no allocation, deterministic.
 */
#ifndef ZXV_ZUI_CLIPHIST_H
#define ZXV_ZUI_CLIPHIST_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_widget.h"

#define ZUI_CLIP_MAX  32u
#define ZUI_CLIP_TEXT 1024u /* bytes per clip with the NUL (the toolkit's own limit) */
#define ZUI_CLIP_CAP  (16u * 1024u)

typedef enum {
    ZUI_CLIP_PLAIN = 0,
    ZUI_CLIP_LINK,   /* scheme://... */
    ZUI_CLIP_PATH,   /* /a/file */
    ZUI_CLIP_CODE,   /* the source said it is code */
    ZUI_CLIP_NUMBER, /* digits, a sign, a point or a comma */
    ZUI_CLIP_TYPES
} zui_clip_type_t;

#define ZUI_CLIP_SENSITIVE 1u /* CH5: never stored */

enum {
    ZUI_CLIP_ADDED = 1,    /* a new clip at the front         */
    ZUI_CLIP_MOVED = 2,    /* CH2: it was there; now in front  */
    ZUI_CLIP_SKIPPED = 0,  /* CH5: a secret, not stored        */
    ZUI_CLIP_TOO_BIG = -1, /* CH1, CH4: does not fit           */
    ZUI_CLIP_BAD = -2      /* NULL or empty                    */
};

typedef struct {
    uint32_t seq; /* when it was copied (or last moved to the front) */
    uint32_t len;
    uint8_t type; /* zui_clip_type_t */
    bool pinned;
    char text[ZUI_CLIP_TEXT];
} zui_clip_t;

typedef struct {
    zui_clip_t clip[ZUI_CLIP_MAX]; /* clip[0] is the newest; [0, n) used */
    uint32_t n;
    uint32_t bytes;     /* CH4: text kept */
    uint32_t cap_bytes; /* CH4 */
    uint32_t next_seq;
    uint32_t skipped, evicted;
    /* the clipboard's current contents (CH5: may be a secret) */
    char cur[ZUI_CLIP_TEXT];
    uint32_t cur_len;
    bool cur_sensitive;
    /* CH6: the view this history is attached to, and what it had before */
    const zui_ui_t *ui;
    bool (*next_set)(void *ctx, const char *s, uint32_t n);
    uint32_t (*next_get)(void *ctx, char *out, uint32_t cap);
    void *next_ctx;
} zui_cliphist_t;

void zui_cliphist_init(zui_cliphist_t *h);
/* CH1..CH5: copy n bytes of text. flags: ZUI_CLIP_SENSITIVE. Returns
 * ZUI_CLIP_ADDED, MOVED, SKIPPED, TOO_BIG or BAD. The text also becomes
 * the current clipboard (up to ZUI_CLIP_TEXT - 1 bytes, cut on a UTF-8
 * boundary) unless it is BAD. */
int zui_cliphist_add(zui_cliphist_t *h, const char *s, uint32_t n, zui_clip_type_t type,
                     uint32_t flags);
/* A type for text with none given: link, path, number or plain. */
zui_clip_type_t zui_clip_guess(const char *s, uint32_t n);

const zui_clip_t *zui_cliphist_get(const zui_cliphist_t *h, uint32_t i); /* 0 = newest */
uint32_t zui_cliphist_count(const zui_cliphist_t *h);
bool zui_cliphist_pin(zui_cliphist_t *h, uint32_t i, bool pinned);
bool zui_cliphist_delete(zui_cliphist_t *h, uint32_t i);
/* CH3: returns how many were removed. */
uint32_t zui_cliphist_clear(zui_cliphist_t *h, bool also_pinned);
/* Make clip i the clipboard again: moved to the front and handed to the
 * platform clipboard when attached. */
bool zui_cliphist_use(zui_cliphist_t *h, uint32_t i);
/* CH4: a new cap; evicts at once. */
void zui_cliphist_set_cap(zui_cliphist_t *h, uint32_t cap_bytes);
/* CH5: wipe the current clipboard buffer. */
void zui_cliphist_forget(zui_cliphist_t *h);
/* The current clipboard: copies up to cap bytes, returns how many. */
uint32_t zui_cliphist_current(const zui_cliphist_t *h, char *out, uint32_t cap);

/* CH6: plug into a view's copy and paste. */
void zui_cliphist_attach(zui_cliphist_t *h, zui_ui_t *ui);
void zui_cliphist_detach(zui_cliphist_t *h, zui_ui_t *ui);
/* The hook functions themselves (ctx is the zui_cliphist_t). */
bool zui_cliphist_clip_set(void *ctx, const char *s, uint32_t n);
uint32_t zui_cliphist_clip_get(void *ctx, char *out, uint32_t cap);

#endif /* ZXV_ZUI_CLIPHIST_H */

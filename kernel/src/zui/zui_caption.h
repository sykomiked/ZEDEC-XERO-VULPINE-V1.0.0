/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_caption.h — live captions: words heard from any audio source, shown
 * on screen as they are spoken, and kept as a transcript.
 *
 * The recognizer lives elsewhere (on the device or behind the speech
 * service). It hands this model timed text segments; the model decides what
 * is on screen, breaks it into lines that fit, and draws it.
 *
 *   CA1 SEGMENTS. A segment is UTF-8 text with a start and end time in ms
 *       (the audio's own clock), a speaker number and whether it is FINAL.
 *       A PARTIAL is the recognizer's current guess and will change.
 *   CA2 PARTIALS ARE REPLACED. A segment from the same speaker with the same
 *       start replaces the partial in place (another partial, or the final).
 *       A new partial from a speaker drops that speaker's older open
 *       partial; a final drops that speaker's open partials that start
 *       inside it; an empty partial withdraws one. Finals never change on
 *       screen, except by a final for the same speaker and start (a
 *       correction), which also corrects the transcript.
 *   CA3 ORDER AND SPEAKERS. On screen, segments are in start order. A
 *       change of speaker starts a new line, led by the speaker's name
 *       ("Ana: ") or ">> " when they have none; the first speaker is named
 *       only when a name is known.
 *   CA4 LINES THAT FIT. Lines break at spaces to the width, measured with
 *       the zui text measurement (zui_text_width_q8) at the style's size.
 *       Text without spaces (Chinese, Japanese) breaks between ideographs
 *       and kana, never before closing punctuation; a word longer than the
 *       line breaks between characters, never inside one. With no font the
 *       measure is half the size per character, so it is still testable.
 *   CA5 AT MOST N LINES. Only the last max_lines lines show (1..8); older
 *       text scrolls off the top. A final leaves the screen hold_ms after it
 *       ends (zui_caption_tick); a stale partial leaves after twice that.
 *   CA6 STYLE. Text size in pixels (8..128), the ground's opacity as an
 *       integer 0..256, top or bottom, the width share (20..100 %), and high
 *       contrast (the zui_palette.h high-contrast ground and text, the
 *       ground forced opaque). The box is the #002911 ground (ZUI_VOID) with
 *       an engraved edge and ZUI_TEXT lines; reduce transparency
 *       (zui_motion.h) makes it opaque.
 *   CA7 TRANSCRIPT. Every final goes into a ring of the last
 *       ZUI_CAP_TRANSCRIPT finals (the oldest is dropped and counted).
 *       zui_caption_transcript_text writes it as "[m:ss] Name: text" lines.
 *
 * Integer only, no allocation, deterministic. Right-to-left text is kept in
 * logical order; reordering for display belongs to the text layer.
 */
#ifndef ZXV_ZUI_CAPTION_H
#define ZXV_ZUI_CAPTION_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_canvas.h"
#include "zui_input.h"

#define ZUI_CAP_SEG_MAX    256u /* bytes of one segment's text, with the NUL  */
#define ZUI_CAP_LIVE       16u  /* segments that can be on screen at once     */
#define ZUI_CAP_LINES_MAX  8u
#define ZUI_CAP_LINE_MAX   256u /* bytes of one shown line, with the NUL      */
#define ZUI_CAP_TRANSCRIPT 64u
#define ZUI_CAP_SPEAKERS   16u
#define ZUI_CAP_NAME_MAX   32u
#define ZUI_CAP_HOLD_MS    4000u
#define ZUI_CAP_SIZE_MIN   8
#define ZUI_CAP_SIZE_MAX   128

typedef enum { ZUI_CAP_BOTTOM = 0, ZUI_CAP_TOP } zui_cap_pos_t;

typedef struct {
    int32_t size_px;    /* CA6: 8..128                         */
    uint32_t bg_q8;     /* ground opacity 0..256               */
    uint8_t position;   /* zui_cap_pos_t                       */
    bool high_contrast; /* CA6                                 */
    uint8_t max_lines;  /* CA5: 1..8                           */
    uint8_t width_pct;  /* 20..100 of the area                 */
    int32_t margin_px;  /* from the area's edge                */
    int32_t pad_px;     /* inside the box                      */
} zui_cap_style_t;

typedef struct {
    uint32_t start_ms, end_ms;
    uint16_t speaker;
    bool final;
    char text[ZUI_CAP_SEG_MAX];
} zui_cap_seg_t;

typedef enum {
    ZUI_CAP_IGNORED = 0, /* bad input, or an empty segment with nothing to withdraw */
    ZUI_CAP_ADDED,       /* a new segment                                           */
    ZUI_CAP_REPLACED,    /* CA2: replaced a partial (or corrected a final)          */
    ZUI_CAP_WITHDRAWN    /* CA2: an empty partial removed one                       */
} zui_cap_result_t;

typedef struct {
    zui_cap_style_t style;
    const zui_text_t *text; /* measuring and drawing; NULL or no font: CA4 fallback */
    uint32_t hold_ms, now_ms;
    zui_cap_seg_t live[ZUI_CAP_LIVE];
    uint32_t n_live;
    uint32_t live_dropped;
    zui_cap_seg_t ring[ZUI_CAP_TRANSCRIPT]; /* CA7 */
    uint32_t ring_n, ring_head;             /* entries held; next slot to write   */
    uint32_t ring_dropped;
    char speaker[ZUI_CAP_SPEAKERS][ZUI_CAP_NAME_MAX];
    /* the laid-out lines (CA4, CA5) */
    char line[ZUI_CAP_LINES_MAX][ZUI_CAP_LINE_MAX];
    int32_t line_w[ZUI_CAP_LINES_MAX];
    uint32_t n_lines;
    uint32_t total_lines; /* all lines the live text made, before keeping the last */
    int32_t width_px;     /* what the lines were broken to                         */
    bool dirty;
    char scratch[ZUI_CAP_LIVE * ZUI_CAP_SEG_MAX + ZUI_CAP_NAME_MAX * 2]; /* one speaker's run */
} zui_caption_t;

void zui_caption_init(zui_caption_t *c, const zui_text_t *text);
void zui_cap_style_default(zui_cap_style_t *st); /* 24 px, 192/256, bottom, 2 lines, 80 % */
void zui_caption_set_style(zui_caption_t *c, const zui_cap_style_t *st); /* clamped */
bool zui_caption_set_speaker(zui_caption_t *c, uint16_t speaker, const char *name);

/* CA1, CA2 */
zui_cap_result_t zui_caption_feed(zui_caption_t *c, uint32_t start_ms, uint32_t end_ms,
                                  uint16_t speaker, bool final, const char *utf8);
void zui_caption_tick(zui_caption_t *c, uint32_t now_ms); /* CA5: the audio clock now */
void zui_caption_clear(zui_caption_t *c);                 /* the screen; not the transcript */

/* CA4, CA5: break the live text to width_px; returns the lines kept. */
uint32_t zui_caption_layout(zui_caption_t *c, int32_t width_px);
const char *zui_caption_line(const zui_caption_t *c, uint32_t i); /* "" past the end */
/* The width of UTF-8 bytes s[0..n) at the style's size, px (CA4). */
int32_t zui_caption_measure(const zui_caption_t *c, const char *s, uint32_t n);
int32_t zui_caption_line_h(const zui_caption_t *c);

/* CA6: lay out for `area` and return the box it would draw (w == 0: nothing). */
zui_rect_t zui_caption_box(zui_caption_t *c, zui_rect_t area);
void zui_caption_draw(zui_caption_t *c, zui_surface_t *s, zui_rect_t area);

/* CA7: i = 0 is the oldest kept final. */
uint32_t zui_caption_transcript_count(const zui_caption_t *c);
const zui_cap_seg_t *zui_caption_transcript_get(const zui_caption_t *c, uint32_t i);
uint32_t zui_caption_transcript_text(const zui_caption_t *c, char *out, uint32_t cap);

#endif /* ZXV_ZUI_CAPTION_H */

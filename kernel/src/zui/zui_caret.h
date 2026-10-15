/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_caret.h — the caret of a zui text field in right-to-left and mixed
 * text, through the Unicode Bidirectional Algorithm (zui_bidi.h).
 *
 * A text field keeps its text in logical order (the order it is typed and
 * stored). What the person sees is the visual order UAX #9 gives each line.
 * zui_widget.c asks a zui_caret_fn for a line's visual layout (zui_widget.h
 * W14); this module is that function, kept apart so the widget set links
 * without the bidi tables when an app only shows left-to-right text.
 *
 *   CA1 PARAGRAPHS. The paragraph a line belongs to (up to the newlines
 *       around it) is resolved with its own direction (P2, P3: the first
 *       strong character), then the line is reordered (L1, L2). A
 *       right-to-left paragraph's lines align right.
 *   CA2 CARET. The caret at a logical position stands on the trailing side
 *       of the character before it (the left side of a right-to-left one),
 *       or, at a line's start, on the leading side of the first character.
 *       Left and Right move to the nearest caret position on screen in that
 *       direction; a click goes to the nearest boundary of the character
 *       under it. Selections stay logical ranges and light every character
 *       inside them wherever it is drawn.
 *   CA3 LIMITS. A paragraph past ZUI_BIDI_MAX code points, or a line past
 *       ZUI_CARET_MAX characters, falls back to left to right. Arabic is not
 *       shaped here (the field draws each character on its own); mirrored
 *       brackets are drawn mirrored.
 *
 * Integer only, no allocation; one static zui_bidi_t (not thread safe, like
 * the rest of zui).
 */
#ifndef ZXV_ZUI_CARET_H
#define ZXV_ZUI_CARET_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_widget.h"

/* CA1..CA3: a zui_caret_fn (zui_widget.h W14). */
bool zui_caret_bidi(const zui_text_t *t, zui_measure_fn measure, const char *s, uint32_t len,
                    uint32_t a, uint32_t b, zui_caret_line_t *out);
/* Turn the visual caret on (or off: left to right only) for every field of ui. */
void zui_ui_use_bidi(zui_ui_t *ui, bool on);

#endif /* ZXV_ZUI_CARET_H */

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_logic.h — the paraconsistent engine decides how things look.
 *
 * Every unit of work on screen (a message, a figure, an agent, a peer, an AI
 * suggestion) already carries a 64-state hexagram tag from the logic engine
 * (lpres/lpres_hex.h: base x polarity x phase). zui_look() turns that tag
 * into depth, tint, shade and colour/anti-colour, so a person reads the
 * logical state of a thing off the screen before reading a word of it.
 *
 * BASE -> where it sits and which way its tone moves
 *   TRUE     S+  comes forward, TINTED (lifted toward light): attested.
 *   FALSE    S-  recedes, SHADED (sunk toward the void): refused, withdrawn.
 *   NEITHER  S0  sits exactly at the glass, untouched: unknown, held.
 *   BOTH     glut: at the glass, drawn as a COLOUR / ANTI-COLOUR pair, the
 *            colour and its complement side by side, because two pieces of
 *            evidence disagree and neither may be hidden.
 * POLARITY -> how firmly
 *   negentropic (converging) full strength; neutral two thirds; entropic
 *   (diverging, stale) one third, low contrast, with a slow colour/anti-colour
 *   shimmer: the eye is told the thing is weakening. FORBIDDEN is never given
 *   depth or motion: flat, alert outline, nothing to admire.
 * PHASE -> how far along the depth it has travelled
 *   potential 1/4, emerging 1/2, manifest full, dissolving 1/2: the Gray
 *   cycle becomes a smooth rise and fall.
 *
 * These are real perceptual cues, not a hologram: aerial perspective (near
 * things have more contrast against the background, far things less),
 * tint/shade, and simultaneous contrast of complementary colours. The depth
 * offset z feeds zui_depth.h, which shows it only in a depth mode.
 *
 * CLARITY FLOOR. Shading must never cost legibility. zui_look_text() shades
 * text like its surface but stops before contrast against the background
 * falls under the WCAG AA ratio of 4.5:1, and never shimmers or splits text.
 *
 * Integer only, freestanding. Uses the tag accessors in lpres_hex.h (inline).
 */
#ifndef ZXV_ZUI_LOGIC_H
#define ZXV_ZUI_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "lpres_hex.h"

typedef struct {
    int32_t z_pt;     /* depth in points, + toward the viewer           */
    int32_t tone;     /* -256..256: + tint toward light, - shade to void */
    int32_t contrast; /* 0..256 of full contrast against the background  */
    int32_t shimmer;  /* 0..: colour/anti-colour swing amplitude          */
    bool split;       /* BOTH: draw colour + anti-colour pair             */
    bool forbidden;   /* flat, alert outline, no motion                   */
} zui_look_t;

/* The look for a tag. Pure; the same tag always gives the same look. */
zui_look_t zui_look(lpx_t tag);

/* The complement (anti-colour) of an RGB colour. */
static inline uint32_t zui_anti(uint32_t rgb)
{
    return (~rgb) & 0xFFFFFFu;
}

/* Apply a look to a surface/edge colour over background bg. phase_tick drives
 * the shimmer (any counter; only its low bits matter). */
uint32_t zui_look_color(const zui_look_t *l, uint32_t rgb, uint32_t bg, uint32_t phase_tick);

/* Same for text: tone and contrast only, clamped so the result keeps at least
 * 4.5:1 contrast against bg whenever the unshaded colour had it. */
uint32_t zui_look_text(const zui_look_t *l, uint32_t rgb, uint32_t bg);

/* WCAG contrast ratio of two colours, x100 (e.g. 450 = 4.5:1). */
uint32_t zui_contrast_x100(uint32_t a, uint32_t b);

#endif /* ZXV_ZUI_LOGIC_H */

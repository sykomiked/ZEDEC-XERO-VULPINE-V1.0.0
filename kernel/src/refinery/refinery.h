/* refinery.h — the Magitech Refinery: text in, sigil-card out
 *
 * WHAT THIS IS
 * ------------
 * The engine that made the 52,095-card deck, rebuilt native to ZXV. A
 * person types intent in ANY language; the Refinery compiles it into a
 * sigil-card:
 *
 *   text --fold--> Enochian letters --sum--> gematria --digital--> root
 *   text --SHA-256--------------------------------> kamea circuit path
 *   root --{root+3 / k}--------------------------------> star fabric
 *   root --spectrum---------------------------------------> palette
 *
 * Every arrow is deterministic, so the same intent forges the same card
 * on every device — which is what makes a card SHAREABLE: it is an AI
 * preset for nonlinear computing that any peer can re-derive and verify
 * from the text alone. No server holds anything.
 *
 * PARITY WITH THE SHIPPED DECK
 * ----------------------------
 * The algorithm is taken from the deck's own generator (generate_seals.py)
 * and is byte-checked against card 24525 in the tests:
 *
 *   * circuit: SHA-256 hex digits, first occurrence of each value, mapped
 *     v -> (v%5, v/5) on the 5x5 kamea. Hex spans 0..15, so at most 16 of
 *     the 25 cells are reachable — the shipped cards' 5x4-ish traces are
 *     this property, observed.
 *   * star: N = root+3, k = N/2-1. Root 9 -> {12/5}, as measured on the
 *     physical card. The star is drawn as ONE orbit (walk +k from vertex 0
 *     until it closes), exactly as the generator drew it; when gcd(N,k)>1
 *     the other orbits exist in the fabric but are not inked — the drawn
 *     figure shows one lane of a multi-lane fabric.
 *
 * A NOTE ON WHAT A PIXEL SCAN CAN SEE
 * -----------------------------------
 * Extracting seal_24525's trace from its raster found 14 of these nodes;
 * the true path has 16 — two sit under heavy star ink. That is why this
 * engine derives the circuit from the TEXT and uses imagery only to
 * verify: generation is exact where extraction is merely honest.
 *
 * THE CARD AS AN AI PRESET
 * ------------------------
 * ref_preset_pack() serializes a card to a small sealed blob. Receiving
 * one, ref_preset_unpack() re-forges the card from the embedded text and
 * REFUSES the blob if anything disagrees (gematria, root, digest, seal).
 * A preset can therefore travel peer-to-peer, and equipping it hands the
 * Chiglet its evidence direction through the existing card/ZCA machinery.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Magitech Refinery slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_REFINERY_H
#define ZXV_REFINERY_H

#include <stdint.h>
#include <stdbool.h>
#include "enochian.h"
#include "../cards/sigil.h"
#include "../cards/zca.h"

#define REF_TEXT_MAX     120u    /* the intent, as typed */
#define REF_KAMEA        5u      /* the 5x5 grid of the generator */
#define REF_MAX_STROKES  160u

/* Full-spectrum palette, chosen by root. RGB888. */
typedef struct {
    uint32_t bg;        /* field   — day: parchment; night: midnight emerald */
    uint32_t ink;       /* the star fabric */
    uint32_t trace;     /* the circuit — the root's spectrum hue */
    uint32_t accent;    /* markers and rim — complement of trace */
} ref_palette_t;

typedef struct {
    char        text[REF_TEXT_MAX];   /* the intent, any language */
    uint8_t     text_len;
    uint8_t     digest[32];           /* SHA-256 of the text bytes */
    uint32_t    gematria;
    uint8_t     root;                 /* 1..9 */
    uint8_t     voice;                /* eno_voice_t */
    sigil_t     sigil;                /* fabric {root+3/k} + kamea circuit */
    uint8_t     path_len;             /* nodes in trace order */
    uint8_t     path[16];             /* node indices, the recovered ORDER */
    ref_palette_t pal;
    card_effect_t fx;                 /* the ZCA-derived Chiglet effect */
} ref_card_t;

/* Stroke list for the renderer — resolution-independent millicoordinate
 * space, x in [0,1000], y in [0,1400] (the 2.5x3.5 card). The visual
 * layer (framebuffer, shimmer, SVG export) consumes these. */
typedef enum { REF_S_LINE = 0, REF_S_CIRCLE, REF_S_DOT, REF_S_TEXT } ref_skind_t;
typedef struct {
    uint8_t  kind;
    uint8_t  layer;        /* 0 bg, 1 fabric, 2 circuit, 3 markers */
    uint16_t x1, y1;       /* LINE: start; CIRCLE/DOT: centre; TEXT: anchor */
    uint16_t x2, y2;       /* LINE: end;   CIRCLE/DOT: (radius, 0)          */
    uint32_t rgb;
    uint16_t width_milli;  /* stroke width */
} ref_stroke_t;

typedef enum {
    REF_OK = 0,
    REF_ERR_ARG,
    REF_ERR_TEXT,          /* empty or oversized intent */
    REF_ERR_SEAL,          /* preset blob failed verification */
    REF_ERR_FORGERY        /* blob's claims disagree with re-derivation */
} ref_status_t;

/* Forge a card from intent text. Deterministic: same text + voice, same
 * card, every device. */
ref_status_t ref_forge(const char *text, uint32_t len, eno_voice_t voice,
                       ref_card_t *out);

/* Emit the card's seal as strokes. Returns the count, 0 on error.
 * night=true swaps the field to the midnight-emerald house style. */
uint32_t ref_render(const ref_card_t *c, bool night,
                    ref_stroke_t *out, uint32_t cap);

/* Compose the activation line in the card's voice into `out` (NUL-
 * terminated, truncated to cap). Returns length written. */
uint32_t ref_activation_line(const ref_card_t *c, char *out, uint32_t cap);

/* ---- the shareable AI preset ---- */
#define REF_PRESET_MAGIC 0x3152505Au   /* "ZPR1" little-endian */
#define REF_PRESET_MAX   (16u + REF_TEXT_MAX + 32u + 8u)

/* Pack a card into a sealed preset blob. Returns bytes written, 0 on
 * error. The blob contains the text and claims (gematria/root/voice), a
 * digest, and a CRC16 seal. */
uint32_t ref_preset_pack(const ref_card_t *c, uint8_t *out, uint32_t cap);

/* Unpack and VERIFY a preset: the card is re-forged from the embedded
 * text and every claim is checked against the derivation. A tampered or
 * corrupted blob is refused, never partially applied. */
ref_status_t ref_preset_unpack(const uint8_t *blob, uint32_t len,
                               ref_card_t *out);

const char *ref_status_name(ref_status_t s);

#endif /* ZXV_REFINERY_H */

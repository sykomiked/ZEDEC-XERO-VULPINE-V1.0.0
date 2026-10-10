/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dtmf.h — integer DTMF: a fixed-point Goertzel detector for the 16 tones on
 * 8 kHz 16-bit mono PCM with twist and timing checks (ITU-T Q.24 style
 * thresholds), a table-based integer tone generator (no float), RFC 4733
 * telephone-event RTP payload encode/decode, and SIP INFO
 * application/dtmf-relay body parse/build. Plus a small menu driver so a
 * phone caller can navigate the assistant or confirm a payment.
 *
 * The detector uses a 205-sample block (25.6 ms at 8 kHz) and the Goertzel
 * algorithm in Q14 fixed point with 64-bit accumulators (no 64-bit division).
 * It applies a forward/reverse twist limit and requires the detected row/col
 * energies to dominate the other six frequencies.
 *
 * HONEST LIMITS. These thresholds are Q.24-style, not a certified Q.24
 * compliance test; real telephony front-ends also need echo control, AGC and
 * a Type 1/Type 2 receiver conformance suite we do not provide. The generator
 * is a clean two-tone synthesizer, not a line-level signalling source.
 *
 * SECURITY. DTMF carries no authentication. A digit confirming a payment is a
 * UI gesture only; the platform's configured strong authentication MUST still
 * run. See the menu driver's confirmation flag and docs/LEGACY_BRIDGE.md.
 */
#ifndef ZXV_LEGACY_DTMF_H
#define ZXV_LEGACY_DTMF_H

#include <stdbool.h>
#include <stdint.h>

#define DTMF_BLOCK 205 /* samples per detection block at 8 kHz */

/* The 16 symbols in row-major order:
 * 1 2 3 A / 4 5 6 B / 7 8 9 C / * 0 # D */
extern const char DTMF_SYMBOLS[16];

/* ===== detector ===== */
typedef struct {
    /* twist limits in tenths of a dB equivalent as a ratio in Q8; defaults set
     * by dtmf_det_init to ITU-T Q.24-style 8 dB forward / 4 dB reverse. */
    uint32_t fwd_twist_q8;     /* max col/row energy ratio (Q8) */
    uint32_t rev_twist_q8;     /* max row/col energy ratio (Q8) */
    uint32_t rel_threshold_q8; /* required dominance over off-tones (Q8) */
} dtmf_detector;

void dtmf_det_init(dtmf_detector *d);

/* Analyse exactly DTMF_BLOCK samples. Returns the detected symbol character,
 * or 0 if none (silence, noise, single tone, or twist/dominance failure). */
char dtmf_detect_block(const dtmf_detector *d, const int16_t *pcm);

/* ===== generator ===== */
/* Generate n samples of the tone for `symbol` into pcm[n]. amp is the peak
 * amplitude of each of the two tones (0..16000 sensible; sum stays in range).
 * phase_row/phase_col hold Q16 phase across calls (init to 0). Returns true if
 * the symbol is a valid DTMF symbol. */
bool dtmf_generate(char symbol, int16_t *pcm, uint32_t n, uint16_t amp, uint32_t *phase_row,
                   uint32_t *phase_col);

/* ===== RFC 4733 telephone-event ===== */
typedef struct {
    uint8_t event;     /* 0-15 DTMF event code */
    bool end;          /* E bit */
    uint8_t volume;    /* 0-63, dBm0 magnitude */
    uint16_t duration; /* in timestamp units */
} dtmf_rtp_event;

/* Encode a 4-byte RFC 4733 telephone-event payload. Returns 4 or 0. */
uint32_t dtmf_rtp_encode(const dtmf_rtp_event *e, uint8_t *out, uint32_t cap);
bool dtmf_rtp_decode(const uint8_t *in, uint32_t len, dtmf_rtp_event *e);
/* Map a DTMF symbol char to its RFC 4733 event code (0-15), or -1. */
int dtmf_symbol_to_event(char symbol);
char dtmf_event_to_symbol(uint8_t event);

/* ===== SIP INFO application/dtmf-relay body ===== */
/* Parse "Signal=5\r\nDuration=160\r\n". Returns true; fills symbol/duration. */
bool dtmf_info_parse(const uint8_t *body, uint32_t len, char *symbol, uint32_t *duration_ms);
/* Build the body. Returns byte count or 0. */
uint32_t dtmf_info_build(char symbol, uint32_t duration_ms, uint8_t *out, uint32_t cap);

/* ===== menu driver ===== */
/* A bounded IVR-style menu. The host supplies a current node; a pressed digit
 * selects a transition. A node may be marked as requiring payment
 * confirmation, which sets needs_strong_auth so the caller knows DTMF alone
 * must not authorize the action. */
#define DTMF_MENU_MAX_NODES 32
#define DTMF_MENU_MAX_EDGES 8

typedef struct {
    char digit;      /* key that triggers this edge */
    uint8_t target;  /* index of target node */
    bool is_confirm; /* this edge confirms a payment / sensitive action */
} dtmf_menu_edge;

typedef struct {
    uint8_t id;
    dtmf_menu_edge edges[DTMF_MENU_MAX_EDGES];
    uint8_t nedges;
} dtmf_menu_node;

typedef struct {
    const dtmf_menu_node *nodes;
    uint8_t nnodes;
    uint8_t current;
    bool needs_strong_auth; /* set true when the last transition was a confirm */
} dtmf_menu;

void dtmf_menu_init(dtmf_menu *m, const dtmf_menu_node *nodes, uint8_t nnodes, uint8_t start);
/* Feed a digit. Returns true if a transition occurred. On a confirm edge,
 * needs_strong_auth is set; the host must run its configured authentication
 * before acting — a true return is never by itself authorization. */
bool dtmf_menu_press(dtmf_menu *m, char digit);

#endif /* ZXV_LEGACY_DTMF_H */

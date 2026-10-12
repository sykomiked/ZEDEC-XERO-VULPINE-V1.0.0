/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qr.h — a small QR Code encoder (ISO/IEC 18004), byte mode, level M.
 *
 * WHAT IT IS
 * ----------
 * Enough of QR Code to put a device-claim payload (ztp.h) on a screen or a
 * sticker: 8-bit byte mode only, error-correction level M only (about 15%
 * of the symbol can be damaged and still read), versions 1 to 10 (21x21 to
 * 57x57 modules, up to 213 bytes). The result is a module matrix; helpers
 * render it as text or as SVG for a screen, label printer or web page.
 *
 * RULES
 *   Q1 Version: the smallest of 1..10 that holds the data, or the caller's
 *      minimum if larger. Too much data for version 10 is QR_E_TOO_LONG.
 *   Q2 Encoding follows the standard: mode 0100, an 8-bit count (16-bit at
 *      version 10), the bytes, up to four zero terminator bits, zero bits to
 *      a byte boundary, then pad bytes 0xEC 0x11 alternating.
 *   Q3 Reed-Solomon over GF(256) with polynomial 0x11D, blocks split and
 *      interleaved as the standard's table for level M says.
 *   Q4 Mask: the caller's choice (0..7) or, with QR_MASK_AUTO, the mask with
 *      the lowest standard penalty score (rules N1-N4); ties go to the lower
 *      mask number.
 *   Q5 No allocation, no libc, no float. A qr_code_t is about 6.5 KB.
 *
 * TESTS
 *   test_qr.c compares whole symbols, module for module, with reference
 *   symbols on which two independent encoders (python-qrcode and Project
 *   Nayuki's qrcodegen) agree and which the zxing-cpp decoder reads back
 *   (gen_qr_fixtures.py), covering versions 1..10 and all eight masks. The
 *   automatic mask choice matches qrcodegen's. (segno 1.6.6 was not usable
 *   as a byte-exact reference: it adds one extra 0x00 codeword after the
 *   terminator; see gen_qr_fixtures.py.)
 *
 * HONEST LIMITS
 *   - No numeric, alphanumeric, Kanji, ECI or structured-append modes, no
 *     other error-correction levels, no Micro QR. Byte mode is all a claim
 *     payload needs; it makes symbols somewhat larger than mixed modes.
 *   - Rendering is plain text or SVG; there is no raster image output.
 *   - Scanning reliability depends on print size and contrast. Keep the
 *     quiet zone (4 modules) the renderers add by default.
 */
#ifndef ZXV_QR_H
#define ZXV_QR_H

#include <stddef.h>
#include <stdint.h>

#define QR_VERSION_MAX 10
#define QR_SIZE_MAX    (17 + 4 * QR_VERSION_MAX) /* 57 */
#define QR_MASK_AUTO   (-1)
#define QR_QUIET       4u

typedef enum {
    QR_OK = 0,
    QR_E_ARG,
    QR_E_TOO_LONG,
    QR_E_VERSION,
    QR_E_MASK,
} qr_err_t;

typedef struct {
    uint8_t version;                        /* 1..10 */
    uint8_t size;                           /* 17 + 4*version */
    uint8_t mask;                           /* 0..7 */
    uint8_t mod[QR_SIZE_MAX * QR_SIZE_MAX]; /* 1 = dark, row-major */
    uint8_t fn[QR_SIZE_MAX * QR_SIZE_MAX];  /* 1 = function pattern */
} qr_code_t;

/* Data capacity in bytes at level M for a version (0 for an invalid one). */
unsigned qr_capacity(int version);

/* Encode len bytes. min_version 0 means 1. mask is 0..7 or QR_MASK_AUTO. */
qr_err_t qr_encode_bytes(qr_code_t *qr, const uint8_t *data, size_t len, int min_version, int mask);

/* 1 if the module at column x, row y is dark; 0 if light or outside. */
int qr_module(const qr_code_t *qr, int x, int y);

/* Text rendering: each module is two characters ("##" dark, "  " light),
 * each row ends with '\n', with `quiet` light modules on every side.
 * Returns the length the text needs (without the NUL). Writes text and a NUL
 * only when cap is larger than that; otherwise writes nothing. */
size_t qr_render_text(const qr_code_t *qr, unsigned quiet, char *out, size_t cap);

/* SVG rendering: one <path> of dark runs on a white background, viewBox in
 * module units, width/height = (size + 2*quiet) * scale pixels. Same return
 * and write rules as qr_render_text. */
size_t qr_render_svg(const qr_code_t *qr, unsigned quiet, unsigned scale, char *out, size_t cap);

#endif /* ZXV_QR_H */

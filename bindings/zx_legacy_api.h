/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_legacy_api.h — stable C ABI for the ZEDEC legacy bridge.
 *
 * This is the one entry point legacy languages link against. It wraps the
 * freestanding bridge modules in kernel/src/legacy with a plain C calling
 * convention: fixed-width integers, caller-owned buffers with explicit
 * capacities, and an int return code (ZX_OK or a negative ZX_ERR_*). No
 * structs cross the boundary by value, no callbacks, no global state, so the
 * same symbols bind cleanly from GnuCOBOL, Fortran ISO_C_BINDING, Ada
 * Interfaces.C, Free Pascal, and any FFI that speaks the C ABI.
 *
 * ABI STABILITY. Function signatures here are append-only: new calls are added
 * at the end, existing ones never change shape. zx_legacy_abi_version()
 * returns the integer contract version a caller can check at startup.
 *
 * HONEST LIMITS. This exposes data conversion and message framing only. It
 * opens no sockets, places no calls, and authorizes no payment. A confirmation
 * digit is a UI event; the platform's configured strong authentication still
 * governs any money movement. See docs/LEGACY_BRIDGE.md.
 */
#ifndef ZX_LEGACY_API_H
#define ZX_LEGACY_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZX_ABI_VERSION 1

/* return codes */
#define ZX_OK           0
#define ZX_ERR_INVALID  (-1) /* bad argument / malformed input */
#define ZX_ERR_OVERFLOW (-2) /* output buffer too small / value too large */
#define ZX_ERR_RANGE    (-3) /* value out of representable range */

/* EBCDIC code page selectors (match ebcdic_cp order) */
#define ZX_CP037  0
#define ZX_CP500  1
#define ZX_CP1047 2

/* ---- version ---- */
int32_t zx_legacy_abi_version(void);

/* ---- COBOL numeric codecs (exact integer amounts) ----
 * Values are scaled integers: the caller tracks the implied decimal point. */
int32_t zx_comp3_decode(const uint8_t *in, int32_t nbytes, int64_t *out);
int32_t zx_comp3_encode(int64_t value, uint8_t *out, int32_t nbytes);
int32_t zx_zoned_decode(const uint8_t *in, int32_t ndigits, int64_t *out);
int32_t zx_zoned_encode(int64_t value, uint8_t *out, int32_t ndigits, int32_t is_signed);
int32_t zx_comp_decode(const uint8_t *in, int32_t size, int32_t is_signed, int64_t *out);
int32_t zx_comp_encode(int64_t value, uint8_t *out, int32_t size, int32_t is_signed);

/* ---- EBCDIC <-> UTF-8 ---- returns byte count written, or negative ZX_ERR_* */
int32_t zx_ebcdic_to_utf8(int32_t cp, const uint8_t *in, int32_t n, uint8_t *out, int32_t cap);
int32_t zx_utf8_to_ebcdic(int32_t cp, const uint8_t *in, int32_t n, uint8_t *out, int32_t cap,
                          uint8_t sub);

/* ---- IBM HFP <-> IEEE 754 bit patterns (integer in, integer out) ---- */
uint32_t zx_hfp32_to_ieee32(uint32_t hfp);
uint32_t zx_ieee32_to_hfp32(uint32_t ieee);
uint64_t zx_hfp64_to_ieee64(uint64_t hfp);
uint64_t zx_ieee64_to_hfp64(uint64_t ieee);

/* ---- USSD menu carriage (TCAP Begin + MAP Invoke + USSD-Arg) ----
 * Builds the component-portion bytes for a processUnstructuredSS-Request from
 * an ASCII USSD string (GSM 7-bit packed inside). Returns byte count or error.
 * Parse extracts the ASCII USSD string back out. */
int32_t zx_ussd_build_request(uint8_t invoke_id, const uint8_t *ascii, int32_t n, uint8_t *out,
                              int32_t cap);
int32_t zx_ussd_parse_request(const uint8_t *comp, int32_t len, uint8_t *ascii_out, int32_t cap,
                              int32_t *out_n);

/* ---- DTMF ---- */
/* Generate `nsamples` of 8kHz 16-bit PCM for a symbol into out. phase_* hold
 * continuation phase (pass pointers to zero-initialised values). */
int32_t zx_dtmf_generate(uint8_t symbol, int16_t *out, int32_t nsamples, uint16_t amp,
                         uint32_t *phase_row, uint32_t *phase_col);
/* Detect one 205-sample block; returns the symbol char (>0) or 0 if none. */
int32_t zx_dtmf_detect(const int16_t *pcm205);

/* ---- ISUP <-> SIP ---- returns SIP status (for a status mapping) or 0. */
int32_t zx_q850_to_sip_status(uint8_t cause);
int32_t zx_sip_status_to_q850(uint16_t status);

#ifdef __cplusplus
}
#endif

#endif /* ZX_LEGACY_API_H */

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ti_frame.h — the Tri-Isolate wire format, shared by the host library and
 * the worker. Pure functions over byte buffers: no I/O, no allocation in
 * the decoders, no floats.
 *
 * A frame is
 *
 *   u32le length   number of bytes that follow (1 .. the receiver's limit)
 *   u8    type     TI_MSG_*
 *   ...   body     length - 1 bytes, laid out per type (little-endian)
 *
 * Every decoder is strict: each field is range-checked, the body must be
 * consumed exactly (no trailing bytes), and anything else is ZXV_E_FORMAT.
 * The layouts are listed in README.md, "Wire format". */
#ifndef TI_FRAME_H
#define TI_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define TI_PROTO_VERSION 1u

#define TI_MSG_HELLO  1u /* worker -> host, once, at start                 */
#define TI_MSG_LOAD   2u /* host -> worker, once                            */
#define TI_MSG_LOADED 3u /* worker -> host                                  */
#define TI_MSG_CALL   4u /* host -> worker                                  */
#define TI_MSG_RESULT 5u /* worker -> host                                  */
#define TI_MSG_QUIT   6u /* host -> worker                                  */

#define TI_HELLO_SECCOMP 0x1u /* HELLO flag: the seccomp filter is installed */

#define TI_WASM_MAX     (4u << 20)            /* module bytes in a LOAD     */
#define TI_REQ_MAX      (TI_WASM_MAX + 4096u) /* host -> worker frame limit */
#define TI_REPLY_MAX    (16u << 10)           /* worker -> host frame limit */
#define TI_NAME_MAX     31u
#define TI_EXPORT_MAX   128u
#define TI_ARGS_MAX     8u
#define TI_PROPS_MAX    64u /* proposals per call (Z-Wasm's stage limit)   */
#define TI_LABEL_MAX    64u
#define TI_MSGTXT_MAX   512u
#define TI_LIMITS_COUNT 15u /* ZW_LIM_MODULE_BYTES .. ZW_LIM_ELEM_SEGMENTS  */

typedef struct {
    uint16_t version;
    uint16_t flags;
} ti_hello;

typedef struct {
    uint32_t grants;
    uint64_t limits[TI_LIMITS_COUNT]; /* limits[i] is ZW_LIM (i + 1)        */
    uint32_t name_len;
    char name[TI_NAME_MAX + 1u];      /* NUL-terminated                     */
    const uint8_t *wasm;              /* points into the decoded body       */
    size_t wasm_len;
} ti_load;

typedef struct {
    int32_t status;
    uint32_t msg_len;
    char msg[TI_MSGTXT_MAX + 1u]; /* printable ASCII, NUL-terminated        */
} ti_loaded;

typedef struct {
    uint64_t fuel;
    uint32_t export_len;
    char export_name[TI_EXPORT_MAX + 1u]; /* no NUL inside; NUL-terminated  */
    uint32_t nargs;
    int64_t args[TI_ARGS_MAX];
} ti_call;

typedef struct {
    uint32_t kind;
    int64_t num, den; /* normalised: den > 0, gcd(|num|, den) = 1          */
    uint32_t label_len;
    uint8_t label[TI_LABEL_MAX];
} ti_prop;

typedef struct {
    int32_t status;
    int32_t has_result;
    int64_t result;
    uint64_t fuel_used;
    uint32_t nprops;
    ti_prop props[TI_PROPS_MAX];
    uint32_t msg_len;
    char msg[TI_MSGTXT_MAX + 1u];
} ti_result;

/* A growable output buffer (encoders append one whole frame). */
typedef struct {
    uint8_t *b;
    size_t n, cap;
} ti_buf;

void ti_buf_free(ti_buf *b);

/* Encoders: append one frame to `out`. ZXV_E_ARG for a value the decoder
 * would refuse, ZXV_E_NOMEM. */
int32_t ti_enc_hello(ti_buf *out, const ti_hello *m);
int32_t ti_enc_load(ti_buf *out, const ti_load *m);
int32_t ti_enc_loaded(ti_buf *out, const ti_loaded *m);
int32_t ti_enc_call(ti_buf *out, const ti_call *m);
int32_t ti_enc_result(ti_buf *out, const ti_result *m);
int32_t ti_enc_quit(ti_buf *out);

/* The frame header: length and type from the first 5 bytes. ZXV_E_FORMAT
 * if the length is 0 or above `max_len`. */
int32_t ti_frame_header(const uint8_t hdr[4], uint32_t max_len, uint32_t *out_len);

/* Decoders: the body only (after the type byte). */
int32_t ti_dec_hello(const uint8_t *body, size_t len, ti_hello *m);
int32_t ti_dec_load(const uint8_t *body, size_t len, ti_load *m);
int32_t ti_dec_loaded(const uint8_t *body, size_t len, ti_loaded *m);
int32_t ti_dec_call(const uint8_t *body, size_t len, ti_call *m);
int32_t ti_dec_result(const uint8_t *body, size_t len, ti_result *m);
int32_t ti_dec_quit(const uint8_t *body, size_t len);

/* Statuses a worker may report. */
int32_t ti_loaded_status_ok(int32_t status);
int32_t ti_result_status_ok(int32_t status);

/* Copy `src` into a message field: printable ASCII kept, every other byte
 * becomes '?', at most TI_MSGTXT_MAX bytes. Returns the length. */
uint32_t ti_msg_clean(char dst[TI_MSGTXT_MAX + 1u], const char *src);

#endif /* TI_FRAME_H */

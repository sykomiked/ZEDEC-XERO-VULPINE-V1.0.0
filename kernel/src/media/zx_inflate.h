/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zx_inflate.h — a DEFLATE (RFC 1951) and zlib (RFC 1950) decoder.
 *
 * PNG pictures are zlib streams, so opening one needs a real inflater, not
 * the stored-block shortcut our own PNG writer (zui_capture.h CP1) takes.
 * This is that inflater, written for hostile input.
 *
 *   IF1 Complete DEFLATE: stored blocks, fixed Huffman and dynamic Huffman
 *       blocks, the full 258-byte match length and 32 KiB distance range.
 *   IF2 Bounded: the caller gives the output buffer and its size; output
 *       never runs past it (ZX_INF_E_SPACE instead), input is never read past
 *       its length (ZX_INF_E_TRUNC instead), and a match may only reach back
 *       into bytes this call already wrote. A stream that wants more output
 *       than the caller expected is how a decompression bomb shows itself,
 *       so the caller sizes the buffer from what it expects, never from the
 *       stream.
 *   IF3 Codes are checked the way zlib checks them: an over-subscribed code
 *       is refused, an incomplete one is refused unless it is the single
 *       one-bit code the format allows, and the reserved symbols (length
 *       286/287, distance 30/31) and block type 3 are refused.
 *   IF4 zlib: the header (CM 8, window <= 32 KiB, FCHECK) is verified, a
 *       preset dictionary is refused, and the Adler-32 trailer must match.
 *   IF5 No allocation, no libc, integer only. State lives on the stack
 *       (about 3 KiB of code tables). Huffman decoding uses a 9-bit lookup
 *       table with a bit-at-a-time canonical fallback for longer codes.
 *
 * WHAT IS AND IS NOT IMPLEMENTED
 *   Implemented: all of RFC 1951 decoding; the RFC 1950 wrapper.
 *   Not implemented: compression (zui_capture.c has our only encoder),
 *   gzip (RFC 1952) headers, preset dictionaries, and streaming: the whole
 *   input is one buffer and the whole output is one buffer.
 */
#ifndef ZXV_ZX_INFLATE_H
#define ZXV_ZX_INFLATE_H

#include <stdint.h>

enum {
    ZX_INF_OK = 0,
    ZX_INF_E_DATA = -1,  /* malformed stream (bad code, bad distance ...)   */
    ZX_INF_E_TRUNC = -2, /* the input ended inside the stream               */
    ZX_INF_E_SPACE = -3, /* the output would exceed the caller's buffer     */
    ZX_INF_E_CHECK = -4, /* zlib header check or Adler-32 mismatch          */
    ZX_INF_E_ARG = -5    /* NULL pointers                                   */
};

/* Raw DEFLATE. *outlen receives the bytes written and, when used is not
 * NULL, *used the input bytes consumed (rounded up to whole bytes). */
int zx_inflate_raw(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t cap, uint32_t *outlen,
                   uint32_t *used);

/* zlib: header, DEFLATE body, Adler-32 trailer. Bytes after the trailer are
 * ignored. */
int zx_inflate_zlib(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t cap,
                    uint32_t *outlen);

#endif /* ZXV_ZX_INFLATE_H */

/* zab.c — ZXV Artifact Bytecode capability verifier. See zab.h.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV capability-verifier slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "zab.h"

/* The opcode -> capability table. This array IS the security boundary: it is
 * the one place that decides what an instruction is allowed to imply, so it is
 * kept flat, total, and exhaustively tested (test_zab.c walks 0..255 and
 * requires every defined opcode to appear here and every undefined one to fall
 * through to ZAB_CAP_ALL). */
uint32_t zab_op_capability(uint8_t op) {
    switch ((zab_op_t)op) {
        case ZAB_OP_NOP:       return ZAB_CAP_NONE;
        case ZAB_OP_OBSERVE:   return ZAB_CAP_OBSERVE;
        case ZAB_OP_READ:      return ZAB_CAP_READ_STATE;
        case ZAB_OP_WRITE:     return ZAB_CAP_WRITE_STATE;
        case ZAB_OP_FS_READ:   return ZAB_CAP_FS_READ;
        case ZAB_OP_FS_WRITE:  return ZAB_CAP_FS_WRITE;
        case ZAB_OP_POST:      return ZAB_CAP_LEDGER;
        case ZAB_OP_SEND:      return ZAB_CAP_NET;
        case ZAB_OP_SPAWN:     return ZAB_CAP_SPAWN;
        case ZAB_OP_EMIT:      return ZAB_CAP_EMIT;
        case ZAB_OP_END:       return ZAB_CAP_NONE;
        case ZAB_OP__MAX:      break;
        default:               break;
    }
    /* Unknown opcode: assume it could do ANYTHING. Callers reject outright;
     * this value exists so that a caller which forgets to check still errs
     * toward "too powerful" rather than "harmless". */
    return ZAB_CAP_ALL;
}

static int op_is_defined(uint8_t op) {
    return op < (uint8_t)ZAB_OP__MAX;
}

/* FNV-1a over the header (with seal zeroed) and the instruction stream. Field
 * by field rather than over raw memory, so struct padding cannot change the
 * seal between compilers or architectures. */
uint32_t zab_compute_seal(const zab_header_t *h, const zab_ins_t *ins,
                          uint16_t count) {
    uint32_t s = 2166136261u;
    #define FNV_B(byte) do { s ^= (uint8_t)(byte); s *= 16777619u; } while (0)
    #define FNV_32(v) do { FNV_B((v)); FNV_B((v) >> 8); FNV_B((v) >> 16); FNV_B((v) >> 24); } while (0)
    #define FNV_16(v) do { FNV_B((v)); FNV_B((v) >> 8); } while (0)
    FNV_32(h->magic);
    FNV_16(h->version);
    FNV_16(h->count);
    for (uint16_t i = 0; i < count; i++) {
        FNV_B(ins[i].op);
        FNV_B(ins[i].a);
        FNV_16(ins[i].b);
    }
    #undef FNV_B
    #undef FNV_32
    #undef FNV_16
    return s ? s : 1u;    /* never 0: a zeroed buffer must not look sealed */
}

/* Read the little-endian scalars out of the byte stream explicitly, so the
 * on-disk format does not depend on host struct layout or alignment. */
static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define ZAB_HDR_BYTES 12u    /* magic4 + version2 + count2 + seal4 */
#define ZAB_INS_BYTES 4u

zab_result_t zab_derive_capabilities(const uint8_t *buf, uint32_t len,
                                     uint32_t *out_caps) {
    if (!buf || len < ZAB_HDR_BYTES) return ZAB_ERR_TRUNCATED;
    if (rd32(buf) != ZAB_MAGIC)      return ZAB_ERR_NOT_PROGRAM;

    uint16_t version = rd16(buf + 4);
    uint16_t count   = rd16(buf + 6);
    uint32_t seal    = rd32(buf + 8);
    if (version != ZAB_VERSION)  return ZAB_ERR_VERSION;
    if (count > ZAB_MAX_INS)     return ZAB_ERR_TOO_LONG;

    /* The body must be entirely present before anything is analysed — a
     * truncated program must never be read as "the part I can see". */
    uint32_t need = ZAB_HDR_BYTES + (uint32_t)count * ZAB_INS_BYTES;
    if (len < need) return ZAB_ERR_TRUNCATED;

    /* Reject unknown opcodes BEFORE unioning: an instruction we cannot
     * classify could imply anything, so it invalidates the whole analysis. */
    const uint8_t *p = buf + ZAB_HDR_BYTES;
    for (uint16_t i = 0; i < count; i++)
        if (!op_is_defined(p[i * ZAB_INS_BYTES])) return ZAB_ERR_BAD_OPCODE;

    /* Verify the seal over the exact bytes present. */
    static zab_ins_t ins[ZAB_MAX_INS];
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *q = p + (uint32_t)i * ZAB_INS_BYTES;
        ins[i].op = q[0]; ins[i].a = q[1]; ins[i].b = rd16(q + 2);
    }
    zab_header_t h; h.magic = ZAB_MAGIC; h.version = version;
    h.count = count; h.seal = 0;
    if (zab_compute_seal(&h, ins, count) != seal) return ZAB_ERR_SEAL;

    /* A program must terminate with END, so a stream truncated at a sector
     * boundary that still happens to checksum cannot pass as complete. */
    if (count == 0 || ins[count - 1].op != (uint8_t)ZAB_OP_END)
        return ZAB_ERR_NO_END;

    uint32_t caps = ZAB_CAP_NONE;
    for (uint16_t i = 0; i < count; i++)
        caps |= zab_op_capability(ins[i].op);

    if (out_caps) *out_caps = caps;
    return ZAB_OK;
}

bool zab_artifact_capabilities(const uint8_t *buf, uint32_t len,
                               uint32_t *out_caps, zab_result_t *why) {
    zab_result_t r = zab_derive_capabilities(buf, len, out_caps);
    if (why) *why = r;
    if (r == ZAB_OK) return true;
    if (r == ZAB_ERR_NOT_PROGRAM) {
        /* Data, not code. Data cannot act, so its capability set is empty.
         * This is what keeps the rule total — there is no artifact for which
         * we simply have no answer and therefore trust the author. */
        if (out_caps) *out_caps = ZAB_CAP_NONE;
        return true;
    }
    /* Malformed program: we could not establish what it does. Refuse. */
    return false;
}

const char *zab_op_name(uint8_t op) {
    switch ((zab_op_t)op) {
        case ZAB_OP_NOP:      return "NOP";
        case ZAB_OP_OBSERVE:  return "OBSERVE";
        case ZAB_OP_READ:     return "READ";
        case ZAB_OP_WRITE:    return "WRITE";
        case ZAB_OP_FS_READ:  return "FS_READ";
        case ZAB_OP_FS_WRITE: return "FS_WRITE";
        case ZAB_OP_POST:     return "POST";
        case ZAB_OP_SEND:     return "SEND";
        case ZAB_OP_SPAWN:    return "SPAWN";
        case ZAB_OP_EMIT:     return "EMIT";
        case ZAB_OP_END:      return "END";
        default:              return "?";
    }
}

const char *zab_result_name(zab_result_t r) {
    switch (r) {
        case ZAB_OK:                return "ok";
        case ZAB_ERR_NOT_PROGRAM:   return "not a program (data)";
        case ZAB_ERR_TRUNCATED:     return "truncated";
        case ZAB_ERR_VERSION:       return "bad version";
        case ZAB_ERR_TOO_LONG:      return "too many instructions";
        case ZAB_ERR_BAD_OPCODE:    return "unknown opcode";
        case ZAB_ERR_SEAL:          return "seal mismatch";
        case ZAB_ERR_NO_END:        return "no END terminator";
        default:                    return "?";
    }
}

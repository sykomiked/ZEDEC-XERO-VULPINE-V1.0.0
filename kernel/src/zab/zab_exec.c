/* zab_exec.c — the ZAB virtual machine. See zab_exec.h.
 *
 * Freestanding: integer only, no libc, no allocation, no float.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV artifact-VM slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "zab_exec.h"

#define ZAB_HDR_BYTES 12u
#define ZAB_INS_BYTES 4u

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* Dispatch one effect. Returns the host's verdict, or a negative VM error.
 * The handler lookup and the capability check are deliberately separate: a
 * capability may be granted while the host supplies nothing for it, and that
 * must fail loudly rather than pass as a no-op. */
static zab_exec_result_t dispatch(const zab_host_t *h, uint8_t op,
                                  uint8_t a, uint16_t b, int *host_rc) {
    int (*fn)(void *, uint8_t, uint16_t) = 0;
    switch ((zab_op_t)op) {
        case ZAB_OP_OBSERVE:  fn = h->observe;     break;
        case ZAB_OP_READ:     fn = h->read_state;  break;
        case ZAB_OP_WRITE:    fn = h->write_state; break;
        case ZAB_OP_FS_READ:  fn = h->fs_read;     break;
        case ZAB_OP_FS_WRITE: fn = h->fs_write;    break;
        case ZAB_OP_POST:     fn = h->ledger_post; break;
        case ZAB_OP_SEND:     fn = h->net_send;    break;
        case ZAB_OP_SPAWN:    fn = h->spawn;       break;
        case ZAB_OP_EMIT:     fn = h->emit;        break;
        default:              return ZABX_ERR_ARGS;   /* not an effect opcode */
    }
    if (!fn) return ZABX_ERR_NO_HOST;
    *host_rc = fn(h->ctx, a, b);
    return ZABX_OK;
}

zab_exec_result_t zab_execute(const uint8_t *prog, uint32_t len,
                              uint32_t granted, const zab_host_t *host,
                              zab_exec_t *out) {
    zab_exec_t st;
    for (unsigned i = 0; i < sizeof(st); i++) ((uint8_t *)&st)[i] = 0;

    if (!prog || !host) {
        st.result = ZABX_ERR_ARGS;
        if (out) *out = st;
        return st.result;
    }

    /* Re-verify the exact bytes we are about to execute. Trusting a capability
     * set derived at some earlier moment is precisely the gap an attacker
     * swaps an artifact into. */
    uint32_t declared_caps = 0;
    if (zab_derive_capabilities(prog, len, &declared_caps) != ZAB_OK) {
        st.result = ZABX_ERR_VERIFY;
        if (out) *out = st;
        return st.result;
    }

    uint16_t count = rd16(prog + 6);
    const uint8_t *p = prog + ZAB_HDR_BYTES;

    for (uint16_t pc = 0; pc < count; pc++) {
        if (st.steps >= ZAB_MAX_STEPS) {          /* unreachable while ZAB is
                                                   * straight-line; kept as a
                                                   * bound if that changes */
            st.result = ZABX_ERR_BUDGET;
            st.fault_pc = pc;
            break;
        }
        const uint8_t *q = p + (uint32_t)pc * ZAB_INS_BYTES;
        uint8_t  op = q[0], a = q[1];
        uint16_t b  = rd16(q + 2);
        st.steps++;

        if (op == (uint8_t)ZAB_OP_END) break;
        if (op == (uint8_t)ZAB_OP_NOP) continue;

        /* (a) permission. The capability this instruction needs must have been
         * granted by the caller — being present in the program is not consent. */
        uint32_t need = zab_op_capability(op);
        if ((need & ~granted) != 0) {
            st.result = ZABX_ERR_CAP_DENIED;
            st.fault_pc = pc; st.fault_op = op;
            if (out) *out = st;
            return st.result;
        }

        /* (b) availability, then the effect itself. */
        int host_rc = 0;
        zab_exec_result_t d = dispatch(host, op, a, b, &host_rc);
        if (d != ZABX_OK) {
            st.result = d;
            st.fault_pc = pc; st.fault_op = op;
            if (out) *out = st;
            return st.result;
        }
        if (host_rc != 0) {
            st.result = ZABX_ERR_HOST_FAILED;
            st.fault_pc = pc; st.fault_op = op;
            if (out) *out = st;
            return st.result;
        }

        /* It happened. Record it — the log is what a caller needs to reason
         * about a partially-applied run. */
        st.effects++;
        st.caps_used |= need;
        if (st.log_count < ZAB_MAX_LOG) st.log_op[st.log_count++] = op;
    }

    if (st.result == ZABX_OK) st.result = ZABX_OK;
    if (out) *out = st;
    return st.result;
}

const char *zab_exec_result_name(zab_exec_result_t r) {
    switch (r) {
        case ZABX_OK:              return "ok";
        case ZABX_ERR_VERIFY:      return "program does not verify";
        case ZABX_ERR_CAP_DENIED:  return "capability not granted";
        case ZABX_ERR_NO_HOST:     return "host provides no handler";
        case ZABX_ERR_HOST_FAILED: return "host rejected the effect";
        case ZABX_ERR_BUDGET:      return "step budget exceeded";
        case ZABX_ERR_ARGS:        return "bad arguments";
        default:                   return "?";
    }
}

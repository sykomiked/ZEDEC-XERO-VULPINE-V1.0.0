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

/* ---- scope: the OBJECT half of a capability ----------------------------- */

/* The kind -> verbs table. Read it as the mirror of zab_op_capability(): there
 * an unknown opcode returns ZAB_CAP_ALL because it could do anything and must
 * be refused; here an unknown kind returns ZAB_CAP_NONE because it can carry
 * nothing and must be refused. Both defaults point at refusal. */
uint32_t zab_scope_kind_caps(uint8_t kind) {
    switch ((zab_scope_kind_t)kind) {
        case ZAB_SCOPE_FILE:
            return ZAB_CAP_OBSERVE | ZAB_CAP_FS_READ | ZAB_CAP_FS_WRITE;
        case ZAB_SCOPE_STATE:
            return ZAB_CAP_OBSERVE | ZAB_CAP_READ_STATE | ZAB_CAP_WRITE_STATE;
        case ZAB_SCOPE_LEDGER:
            return ZAB_CAP_OBSERVE | ZAB_CAP_LEDGER;
        case ZAB_SCOPE_PEER:
            return ZAB_CAP_OBSERVE | ZAB_CAP_NET;
        case ZAB_SCOPE_EVENT:
            return ZAB_CAP_OBSERVE | ZAB_CAP_EMIT;
        case ZAB_SCOPE_PROCESS:
            return ZAB_CAP_OBSERVE | ZAB_CAP_SPAWN;
        case ZAB_SCOPE_CONTRACT:
            /* a contract acts on the runtime that deployed it: it may look,
             * read shared state, and post to the ledger. It may not write the
             * filesystem, spawn, or reach the network — those are not a
             * contract's business and never were. */
            return ZAB_CAP_OBSERVE | ZAB_CAP_READ_STATE | ZAB_CAP_LEDGER;
        case ZAB_SCOPE_TRIAD:
            /* A triad acting on ITSELF is the one kind with no verb ceiling of
             * its own, and that is not a hole: the ceiling is the capability
             * set recorded for the role, which zab.c DERIVED from the role's
             * own bytecode rather than reading a declaration. Narrowing here
             * as well would be a second, weaker copy of a check that is
             * already exact. */
            return ZAB_CAP_ALL;
        case ZAB_SCOPE_NONE:
        case ZAB_SCOPE__MAX:  break;
        default:              break;
    }
    return ZAB_CAP_NONE;
}

bool zab_scope_valid(const zab_scope_t *sc) {
    if (!sc) return false;
    if (sc->kind == (uint8_t)ZAB_SCOPE_NONE) return false;
    if (sc->kind >= (uint8_t)ZAB_SCOPE__MAX) return false;

    /* Reserved bytes must be zero. An entry written by something that knows
     * more than this VM does is refused, not partially understood. */
    if (sc->reserved[0] || sc->reserved[1] || sc->reserved[2]) return false;

    /* The name must be non-empty and NUL-terminated INSIDE the field. An
     * unterminated name is not a name: nothing downstream could compare it
     * without running off the end. */
    bool terminated = false;
    for (uint32_t i = 0; i < ZAB_SCOPE_NAME_LEN; i++)
        if (sc->name[i] == '\0') { terminated = true; break; }
    if (!terminated) return false;
    if (sc->name[0] == '\0') return false;

    /* No undefined capability bits: a bit this build cannot name could mean
     * anything in the build that wrote it. */
    if (sc->caps == ZAB_CAP_NONE) return false;
    if ((sc->caps & ~(uint32_t)ZAB_CAP_ALL) != 0) return false;

    /* A zero-length extent covers nothing, and a wrapping one covers
     * everything by accident — the exact shape of an always-true predicate. */
    if (sc->span == 0) return false;
    if (sc->base + sc->span < sc->base) return false;
    return true;
}

bool zab_scope_permits(const zab_scope_t *sc, uint32_t need, uint16_t off) {
    if (!zab_scope_valid(sc)) return false;
    if (need == ZAB_CAP_NONE) return false;   /* an effect with no verb is a
                                               * table bug; refuse it rather
                                               * than let it pass trivially */
    uint32_t allowed = sc->caps & zab_scope_kind_caps(sc->kind);
    if ((need & ~allowed) != 0) return false;
    if ((uint32_t)off >= sc->span) return false;
    return true;
}

const zab_scope_t *zab_scope_resolve(const zab_host_t *host, uint8_t slot) {
    if (!host || !host->scopes) return 0;
    if (host->reserved != 0) return 0;
    if (host->n_scopes == 0) return 0;
    if ((uint32_t)host->n_scopes > ZAB_MAX_SCOPES) return 0;
    if ((uint32_t)slot >= (uint32_t)host->n_scopes) return 0;
    return &host->scopes[slot];
}

bool zab_scope_set(zab_scope_t *sc, const char *name, uint8_t kind,
                   uint32_t caps, uint32_t base, uint32_t span) {
    if (!sc) return false;
    for (unsigned i = 0; i < sizeof(*sc); i++) ((uint8_t *)sc)[i] = 0;
    if (!name || name[0] == '\0') return false;

    /* The name must fit WITH its terminator. Truncating would silently rename
     * the object, and an extent that names the wrong object is worse than no
     * extent at all — so this refuses instead. */
    uint32_t n = 0;
    while (name[n] != '\0') {
        if (n >= ZAB_SCOPE_NAME_LEN - 1u) return false;
        n++;
    }
    for (uint32_t i = 0; i < n; i++) sc->name[i] = name[i];
    sc->kind = kind;
    sc->caps = caps;
    sc->base = base;
    sc->span = span;
    if (!zab_scope_valid(sc)) {
        for (unsigned i = 0; i < sizeof(*sc); i++) ((uint8_t *)sc)[i] = 0;
        return false;               /* zeroed == denies, so a caller that
                                     * ignores the result still fails closed */
    }
    /* The kind must be able to carry every verb asked of it: a host that puts
     * FS_WRITE on a network peer has made a mistake, and obeying it would be
     * the mistake this whole change exists to stop. */
    if ((caps & ~zab_scope_kind_caps(kind)) != 0) {
        for (unsigned i = 0; i < sizeof(*sc); i++) ((uint8_t *)sc)[i] = 0;
        return false;
    }
    return true;
}

/* ---- dispatch ----------------------------------------------------------- */

/* The handler for an opcode, or NULL if the host supplies none. Looked up
 * SEPARATELY from calling it, because "granted but unhosted" must fail loudly
 * (ZABX_ERR_NO_HOST) rather than pass as a no-op, and because the scope check
 * has to sit between availability and effect. */
static int (*host_fn(const zab_host_t *h, uint8_t op))(void *, uint8_t, uint16_t) {
    switch ((zab_op_t)op) {
        case ZAB_OP_OBSERVE:  return h->observe;
        case ZAB_OP_READ:     return h->read_state;
        case ZAB_OP_WRITE:    return h->write_state;
        case ZAB_OP_FS_READ:  return h->fs_read;
        case ZAB_OP_FS_WRITE: return h->fs_write;
        case ZAB_OP_POST:     return h->ledger_post;
        case ZAB_OP_SEND:     return h->net_send;
        case ZAB_OP_SPAWN:    return h->spawn;
        case ZAB_OP_EMIT:     return h->emit;
        default:              return 0;
    }
}

/* Is this an effect opcode at all? Distinguishes "no handler bound" from "not
 * an effect", which host_fn() alone cannot. */
static bool op_is_effect(uint8_t op) {
    switch ((zab_op_t)op) {
        case ZAB_OP_OBSERVE: case ZAB_OP_READ:     case ZAB_OP_WRITE:
        case ZAB_OP_FS_READ: case ZAB_OP_FS_WRITE: case ZAB_OP_POST:
        case ZAB_OP_SEND:    case ZAB_OP_SPAWN:    case ZAB_OP_EMIT:
            return true;
        default:
            return false;
    }
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

        /* (b) availability. Checked BEFORE scope so that "granted but the host
         * provides nothing" keeps reporting itself as exactly that — a host's
         * shape is a different fact from an object's extent, and collapsing
         * the two would make an unimplemented capability indistinguishable
         * from an out-of-scope one. */
        if (!op_is_effect(op)) {
            st.result = ZABX_ERR_ARGS;            /* not an effect opcode */
            st.fault_pc = pc; st.fault_op = op; st.fault_slot = a;
            if (out) *out = st;
            return st.result;
        }
        int (*fn)(void *, uint8_t, uint16_t) = host_fn(host, op);
        if (!fn) {
            st.result = ZABX_ERR_NO_HOST;
            st.fault_pc = pc; st.fault_op = op; st.fault_slot = a;
            if (out) *out = st;
            return st.result;
        }

        /* (c) EXTENT. The verb is granted and the host can perform it — but on
         * WHICH object? `a` selects an extent from a table only the host
         * writes, so the program picks among what it was offered and can never
         * name something it was not. No table, a bad slot, a malformed entry,
         * a verb the extent does not carry, or an offset outside it: all deny.
         * This is the check that turns "may write the filesystem" back into
         * "may write THIS file". */
        const zab_scope_t *sc = zab_scope_resolve(host, a);
        if (!zab_scope_permits(sc, need, b)) {
            st.result = ZABX_ERR_SCOPE;
            st.fault_pc = pc; st.fault_op = op; st.fault_slot = a;
            if (out) *out = st;
            return st.result;
        }

        /* (d) the effect itself. */
        int host_rc = fn(host->ctx, a, b);
        if (host_rc != 0) {
            st.result = ZABX_ERR_HOST_FAILED;
            st.fault_pc = pc; st.fault_op = op; st.fault_slot = a;
            if (out) *out = st;
            return st.result;
        }

        /* It happened. Record it — the log is what a caller needs to reason
         * about a partially-applied run, and it records the OBJECT as well as
         * the verb, because "it wrote something" is not an audit trail. */
        st.effects++;
        st.caps_used |= need;
        if (st.log_count < ZAB_MAX_LOG) {
            st.log_slot[st.log_count] = a;
            st.log_op[st.log_count++] = op;
        }
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
        case ZABX_ERR_SCOPE:       return "effect outside the granted scope";
        default:                   return "?";
    }
}

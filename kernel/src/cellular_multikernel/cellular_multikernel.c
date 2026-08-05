/* cellular_multikernel.c — ZXV Cellular Multikernel Fabric (CELL-001)
 *
 * Core implementation of the cell contract, control-plane admission,
 * data-plane bounded queues, tri-space commit tracking, and CRIT-168
 * value handling.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#include "cellular_multikernel.h"
#include <string.h>
#include <stdio.h>

/* ---- Local helpers ---- */

/* Compare fixed-size cell IDs; both are zero-padded in the contract. */
static bool cell_id_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    for (int i = 0; i < CELL_NAME_LEN; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == '\0') return true;
    }
    return true;
}

static cell_t *find_cell(cell_fabric_t *fabric, const char *cell_id) {
    if (!fabric || !cell_id) return NULL;
    for (uint8_t i = 0; i < fabric->cell_count; i++) {
        if (cell_id_eq(fabric->cells[i].cell_id, cell_id))
            return &fabric->cells[i];
    }
    return NULL;
}

static void cell_stop_execution(cell_fabric_t *fabric, cell_t *cell);
static void cell_reset_runtime_state(cell_t *cell);

/* ---- CRIT-168 ---- */

void crit168_zero(crit168_value_t *v) {
    if (!v) return;
    memset(v->b, 0, CRIT168_BYTE_COUNT);
}

bool crit168_eq(const crit168_value_t *a, const crit168_value_t *b) {
    if (!a || !b) return false;
    for (int i = 0; i < CRIT168_BYTE_COUNT; i++)
        if (a->b[i] != b->b[i]) return false;
    return true;
}

void crit168_from_digest(crit168_value_t *v, const uint8_t *data, size_t len) {
    if (!v || !data) { if (v) crit168_zero(v); return; }
    /* Placeholder digest: fold the input into 168 bits.
     * In production this is a proper cryptographic digest truncated to
     * 21 bytes, but the value semantics are independent of the hash. */
    crit168_zero(v);
    for (size_t i = 0; i < len; i++)
        v->b[i % CRIT168_BYTE_COUNT] ^= data[i];
}

/* ---- Cell contract ---- */

void cell_contract_zero(cell_t *cell) {
    if (!cell) return;
    memset(cell, 0, sizeof(*cell));
    cell->state = CELL_STATE_DISCOVERED;
    cell->revoked_at_ordinal = 0;
    cell->entry_point = NULL;
    cell->entry_arg = NULL;
    cell->task_id = 0;
    cell->is_executing = false;
    cell->health = CELL_HEALTH_OK;
}

bool cell_set_digest(cell_t *cell, const uint8_t digest[CELL_MAX_DIGEST]) {
    if (!cell || !digest) return false;
    memcpy(cell->firmware_or_kernel_digest, digest, CELL_MAX_DIGEST);
    return true;
}

/* ---- Fabric control plane ---- */

void cell_fabric_init(cell_fabric_t *fabric) {
    if (!fabric) return;
    memset(fabric, 0, sizeof(*fabric));
    fabric->next_ordinal = 1;
    fabric->control_plane_incarnation = 1;
    fabric->has_local_replica = true;  /* consumer profile */
    fabric->execution = NULL;
}

void cell_fabric_set_backend(cell_fabric_t *fabric,
                             const cell_execution_backend_t *backend) {
    if (!fabric) return;
    fabric->execution = backend;
}

bool cell_fabric_set_entry(cell_t *cell, void (*entry)(void *arg), void *arg) {
    if (!cell) return false;
    cell->entry_point = entry;
    cell->entry_arg = arg;
    return true;
}

bool cell_fabric_discover(cell_fabric_t *fabric, const cell_t *candidate) {
    if (!fabric || !candidate)
        return false;

    /* Reject empty cell IDs. */
    if (candidate->cell_id[0] == '\0') return false;

    /* A higher-incarnation contract may replace a failed / quarantined /
     * recovering / degraded cell.  This is the control-plane recovery path
     * for CELL-001: the old cell record is superseded, routes are dropped,
     * and any active execution context is terminated before the new contract
     * is adopted. */
    cell_t *existing = find_cell(fabric, candidate->cell_id);
    if (existing) {
        if (existing->incarnation >= candidate->incarnation)
            return false;                       /* not a newer incarnation */
        if (existing->state != CELL_STATE_FAILED &&
            existing->state != CELL_STATE_REVOKED &&
            existing->state != CELL_STATE_RECOVERING &&
            existing->state != CELL_STATE_DEGRADED &&
            existing->state != CELL_STATE_QUARANTINED)
            return false;                       /* healthy cell cannot be replaced */

        cell_stop_execution(fabric, existing);

        for (uint8_t i = 0; i < fabric->route_count; i++) {
            if (cell_id_eq(fabric->routes[i].target_cell, candidate->cell_id))
                fabric->routes[i].target_cell[0] = '\0';
        }

        cell_t *slot = existing;
        memcpy(slot, candidate, sizeof(*slot));
        cell_reset_runtime_state(slot);

        if (slot->incarnation > fabric->control_plane_incarnation)
            fabric->control_plane_incarnation = slot->incarnation;

        for (uint8_t i = 0; i < slot->transport_count; i++) {
            if (slot->transports[i].incarnation == 0)
                slot->transports[i].incarnation = slot->incarnation;
        }

        return true;
    }

    if (fabric->cell_count >= CELL_MAX_CELLS)
        return false;

    /* Clone the candidate contract for a brand-new cell. */
    cell_t *slot = &fabric->cells[fabric->cell_count];
    memcpy(slot, candidate, sizeof(*slot));
    cell_reset_runtime_state(slot);

    if (slot->incarnation > fabric->control_plane_incarnation)
        fabric->control_plane_incarnation = slot->incarnation;

    for (uint8_t i = 0; i < slot->transport_count; i++) {
        if (slot->transports[i].incarnation == 0)
            slot->transports[i].incarnation = slot->incarnation;
    }

    fabric->cell_count++;
    return true;
}

bool cell_fabric_authenticate(cell_fabric_t *fabric, const char *cell_id,
                              const uint8_t expected_digest[CELL_MAX_DIGEST]) {
    cell_t *cell = find_cell(fabric, cell_id);
    if (!cell || !expected_digest) return false;

    if (memcmp(cell->firmware_or_kernel_digest, expected_digest,
               CELL_MAX_DIGEST) != 0) {
        cell->state = CELL_STATE_QUARANTINED;
        cell->health = CELL_HEALTH_DEGRADED;
        return false;
    }

    cell->state = CELL_STATE_AUTHENTICATED;
    return true;
}

cell_state_t cell_fabric_admit(cell_fabric_t *fabric, const char *cell_id) {
    cell_t *cell = find_cell(fabric, cell_id);
    if (!cell) return CELL_STATE_FAILED;

    if (cell->state != CELL_STATE_AUTHENTICATED) {
        cell->state = CELL_STATE_QUARANTINED;
        return cell->state;
    }

    /* Admission assigns an ordinal and marks the cell active */
    cell->admitted_at_ordinal = fabric->next_ordinal++;
    cell->state = CELL_STATE_ADMITTED;
    /* Active is a separate transition after it has booted */
    return cell->state;
}

bool cell_fabric_activate(cell_fabric_t *fabric, const char *cell_id) {
    cell_t *cell = find_cell(fabric, cell_id);
    if (!cell || cell->state != CELL_STATE_ADMITTED) return false;
    cell->state = CELL_STATE_ACTIVE;
    cell->health = CELL_HEALTH_OK;
    return true;
}

static void cell_stop_execution(cell_fabric_t *fabric, cell_t *cell) {
    if (!cell || !cell->is_executing) return;
    if (fabric && fabric->execution && fabric->execution->terminate) {
        fabric->execution->terminate(cell->task_id);
    }
    cell->is_executing = false;
    cell->task_id = 0;
}

/* Reset all runtime/lifecycle state before adopting a contract.
 * This keeps stale execution context and ordinal metadata from
 * leaking into a newly discovered or replacement incarnation. */
static void cell_reset_runtime_state(cell_t *cell) {
    if (!cell) return;
    cell->state = CELL_STATE_DISCOVERED;
    cell->health = CELL_HEALTH_OK;
    cell->is_executing = false;
    cell->task_id = 0;
    cell->admitted_at_ordinal = 0;
    cell->revoked_at_ordinal = 0;
    cell->entry_point = NULL;
    cell->entry_arg = NULL;
}

cell_state_t cell_fabric_execute(cell_fabric_t *fabric, const char *cell_id,
                                 void (*entry)(void *arg), void *arg) {
    cell_t *cell = find_cell(fabric, cell_id);
    if (!cell) return CELL_STATE_FAILED;

    /* Only admitted or already active cells may be executed. */
    if (cell->state != CELL_STATE_ADMITTED && cell->state != CELL_STATE_ACTIVE) {
        cell->state = CELL_STATE_QUARANTINED;
        return cell->state;
    }

    if (entry) {
        cell->entry_point = entry;
        cell->entry_arg = arg;
    }

    if (!cell->entry_point) {
        cell->state = CELL_STATE_QUARANTINED;
        return cell->state;
    }

    /* If a backend is registered, create a separate execution context. */
    if (fabric && fabric->execution && fabric->execution->create) {
        uint32_t task_id = 0;
        int32_t rc = fabric->execution->create(cell->cell_id, cell->entry_point,
                                                cell->entry_arg, &task_id);
        if (rc < 0) {
            cell->state = CELL_STATE_DEGRADED;
            return cell->state;
        }
        cell->task_id = task_id;
        cell->is_executing = true;
    }

    cell->state = CELL_STATE_ACTIVE;
    return cell->state;
}

bool cell_fabric_terminate(cell_fabric_t *fabric, const char *cell_id) {
    cell_t *cell = find_cell(fabric, cell_id);
    if (!cell) return false;

    cell_stop_execution(fabric, cell);
    cell->state = CELL_STATE_REVOKED;
    cell->health = CELL_HEALTH_REVOKED;
    cell->revoked_at_ordinal = fabric->next_ordinal++;

    /* Remove routes owned by this cell */
    for (uint8_t i = 0; i < fabric->route_count; i++) {
        if (cell_id_eq(fabric->routes[i].target_cell, cell_id)) {
            fabric->routes[i].target_cell[0] = '\0';
        }
    }
    return true;
}

bool cell_fabric_revoke(cell_fabric_t *fabric, const char *cell_id) {
    return cell_fabric_terminate(fabric, cell_id);
}

bool cell_fabric_handle_fault(cell_fabric_t *fabric, const char *cell_id,
                              cell_health_t new_health) {
    cell_t *cell = find_cell(fabric, cell_id);
    if (!cell) return false;

    cell->health = new_health;
    if (new_health == CELL_HEALTH_FAIL_STOPPED ||
        new_health == CELL_HEALTH_REVOKED) {
        cell->state = CELL_STATE_FAILED;
        cell_stop_execution(fabric, cell);
        return cell_fabric_revoke(fabric, cell_id);
    }
    if (new_health == CELL_HEALTH_DEGRADED) {
        cell->state = CELL_STATE_DEGRADED;
    }
    if (new_health == CELL_HEALTH_RECOVERING) {
        cell->state = CELL_STATE_RECOVERING;
        cell_stop_execution(fabric, cell);
    }
    return true;
}

/* ---- Routing ---- */

bool cell_fabric_add_route(cell_fabric_t *fabric, const cell_route_t *route) {
    if (!fabric || !route || fabric->route_count >= CELL_MAX_ROUTES)
        return false;
    if (route->target_cell[0] == '\0') return false;

    memcpy(&fabric->routes[fabric->route_count], route, sizeof(*route));
    fabric->route_count++;
    return true;
}

const char *cell_fabric_route_lookup(cell_fabric_t *fabric, uint32_t phase_id,
                                      uint32_t phase_version) {
    if (!fabric) return NULL;
    for (uint8_t i = 0; i < fabric->route_count; i++) {
        if (fabric->routes[i].phase_id == phase_id &&
            fabric->routes[i].phase_version == phase_version &&
            fabric->routes[i].target_cell[0] != '\0') {
            return fabric->routes[i].target_cell;
        }
    }
    return NULL;
}

/* ---- Data plane (bounded queue, no dynamic allocation) ---- */

bool cell_transport_send(cell_transport_endpoint_t *ep, const fabric_event_t *ev) {
    if (!ep || !ev || ep->capacity == 0) return false;

    uint32_t next = (ep->tail + 1) % ep->capacity;
    if (next == ep->head) return false;  /* queue full: backpressure */

    /* Transport the event into a typed slot. In a real implementation this
     * writes to a shared ring; in the emulated model we use the endpoint's
     * own embedded ring slot. */
    uint64_t *ring = (uint64_t *)ep->local_addr; /* model */
    if (ring) {
        ring[ep->tail * 4 + 0] = ev->event_id;
        ring[ep->tail * 4 + 1] = ev->ordinal;
        ring[ep->tail * 4 + 2] = ev->triad_id;
        ring[ep->tail * 4 + 3] = (uint64_t)ep->sequence++;
    }

    ep->tail = next;
    return true;
}

bool cell_transport_receive(cell_transport_endpoint_t *ep, fabric_event_t *ev) {
    if (!ep || !ev || ep->capacity == 0) return false;
    if (ep->head == ep->tail) return false;  /* empty */

    ep->head = (ep->head + 1) % ep->capacity;
    ev->event_id = 0; /* slot would be copied from ring in real impl */
    return true;
}

/* ---- Tri-space commit protocol ---- */

void triad_commit_reset(triad_commit_state_t *st, uint64_t triad_id,
                        uint64_t veto_deadline_ordinal) {
    if (!st) return;
    st->triad_id = triad_id;
    st->s_plus_ok = false;
    st->s_minus_ok = false;
    st->s_zero_ok = false;
    st->s_zero_resolved = false;
    st->lane_received[0] = false;
    st->lane_received[1] = false;
    st->lane_received[2] = false;
    st->veto_deadline_ordinal = veto_deadline_ordinal;
    st->commit_decision = 0;
}

bool triad_commit_update(triad_commit_state_t *st, const fabric_event_t *ev) {
    if (!st || !ev || ev->triad_id != st->triad_id) return false;

    /* Stale incarnation / replay check: event must be at or past deadline */
    if (ev->ordinal > st->veto_deadline_ordinal) {
        return false; /* deadline exceeded, reject further input */
    }

    if (ev->lane > 2) return false;

    /* A lane cannot deliver more than one vote per triad */
    if (st->lane_received[ev->lane]) return false;
    st->lane_received[ev->lane] = true;

    bool lane_ok = (ev->decision != 2); /* not a reject */
    switch (ev->lane) {
    case 0:
        st->s_plus_ok = lane_ok;
        break;
    case 1:
        st->s_minus_ok = lane_ok;
        break;
    case 2:
        st->s_zero_ok = lane_ok;
        st->s_zero_resolved = true;
        break;
    default:
        return false;
    }

    /* Reject immediately if any lane explicitly vetoes */
    if (!lane_ok) {
        st->commit_decision = 2; /* reject */
        return true;
    }

    /* Commit token issued only when S+ and S- are received and OK,
     * and S0 is resolved and also OK. */
    if (st->lane_received[0] && st->lane_received[1] && st->s_zero_resolved) {
        if (st->s_plus_ok && st->s_minus_ok && st->s_zero_ok) {
            st->commit_decision = 1; /* commit */
        } else {
            st->commit_decision = 2; /* reject */
        }
    } else {
        st->commit_decision = 0; /* undecided */
    }

    return true;
}

/* ---- Diagnostics ---- */

void cell_fabric_health_summary(const cell_fabric_t *fabric, char *out, size_t out_len) {
    if (!fabric || !out || out_len == 0) return;
    size_t pos = 0;
    int n = snprintf(out, out_len, "cells=%u routes=%u ordinal=%llu\n",
                     fabric->cell_count, fabric->route_count,
                     (unsigned long long)fabric->next_ordinal);
    if (n > 0) pos = (size_t)n;

    for (uint8_t i = 0; i < fabric->cell_count && pos < out_len - 1; i++) {
        const cell_t *c = &fabric->cells[i];
        const char *state_str = "unknown";
        switch (c->state) {
        case CELL_STATE_DISCOVERED:  state_str = "discovered"; break;
        case CELL_STATE_AUTHENTICATED: state_str = "authenticated"; break;
        case CELL_STATE_QUARANTINED: state_str = "quarantined"; break;
        case CELL_STATE_ADMITTED:    state_str = "admitted"; break;
        case CELL_STATE_ACTIVE:      state_str = "active"; break;
        case CELL_STATE_DEGRADED:    state_str = "degraded"; break;
        case CELL_STATE_FAILED:      state_str = "failed"; break;
        case CELL_STATE_RECOVERING:  state_str = "recovering"; break;
        case CELL_STATE_REVOKED:     state_str = "revoked"; break;
        }
        n = snprintf(out + pos, out_len - pos, "  %s: %s\n", c->cell_id, state_str);
        if (n > 0) pos += (size_t)n;
    }
}


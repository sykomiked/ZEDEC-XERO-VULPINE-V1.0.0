/* yantra_fabric.c — ZXV Software-Defined Hardware Fabric (Yantra Fabric)
 *
 * Implements device registry, capability state transitions, state machine
 * management, digital twin assertions, and contradiction detection.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */

#include "yantra_fabric.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static bool str_eq(const char *a, const char *b) {
    uint32_t i;
    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (a[i] != b[i]) return false;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* ===== Capability Names ===== */

static const char *capability_names[] = {
    "MODEL_ONLY", "EMULATED", "RTL_SIMULATED", "SYNTHESIZED",
    "FPGA_VERIFIED", "DEVICE_CONNECTED", "DEVICE_MEASURED",
    "QUALIFIED", "CERTIFIED"
};

const char *yf_capability_name(yf_capability_state_t state) {
    if (state > YF_CERTIFIED) return "UNKNOWN";
    return capability_names[state];
}

/* ===== Initialization ===== */

void yf_init(yf_fabric_t *yf) {
    if (!yf) return;
    ev_memset(yf, 0, sizeof(*yf));
    yf->next_device_id = 1;
}

/* ===== Device Management ===== */

int32_t yf_register_device(yf_fabric_t *yf, const char *name,
                            const char *device_class,
                            yf_capability_state_t initial_capability) {
    if (!yf || !name) return -1;

    /* Check for duplicate name */
    if (yf_get_device_by_name(yf, name)) return -1;

    for (uint32_t i = 0; i < YF_MAX_DEVICES; i++) {
        if (!yf->devices[i].registered) {
            ev_memset(&yf->devices[i], 0, sizeof(yf->devices[i]));
            yf->devices[i].id = yf->next_device_id++;
            copy_str(yf->devices[i].name, name, YF_MAX_NAME_LEN);
            if (device_class)
                copy_str(yf->devices[i].device_class, device_class, YF_MAX_NAME_LEN);
            yf->devices[i].capability = initial_capability;
            yf->devices[i].current_state = 0;
            yf->devices[i].registered = true;
            yf->num_devices++;
            yf->total_devices_registered++;
            return (int32_t)i;
        }
    }
    return -1;
}

yf_device_t *yf_get_device(yf_fabric_t *yf, uint32_t idx) {
    if (!yf || idx >= YF_MAX_DEVICES) return NULL;
    if (!yf->devices[idx].registered) return NULL;
    return &yf->devices[idx];
}

yf_device_t *yf_get_device_by_name(yf_fabric_t *yf, const char *name) {
    if (!yf || !name) return NULL;
    for (uint32_t i = 0; i < YF_MAX_DEVICES; i++) {
        if (yf->devices[i].registered && str_eq(yf->devices[i].name, name))
            return &yf->devices[i];
    }
    return NULL;
}

/* ===== Capability State Transitions ===== */

bool yf_set_capability(yf_fabric_t *yf, uint32_t device_idx,
                        yf_capability_state_t new_state) {
    if (!yf) return false;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return false;
    if (new_state > YF_CERTIFIED) return false;

    if (new_state > dev->capability) {
        yf->total_capability_upgrades++;
    } else if (new_state < dev->capability) {
        yf->total_capability_downgrades++;
    }

    dev->capability = new_state;
    return true;
}

/* ===== Register Management ===== */

bool yf_device_add_register(yf_fabric_t *yf, uint32_t device_idx,
                             const char *name, uint32_t offset,
                             uint32_t width, bool readable, bool writable,
                             uint32_t reset_value) {
    if (!yf || !name) return false;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return false;
    if (dev->num_registers >= YF_MAX_REGISTERS) return false;

    yf_register_t *reg = &dev->registers[dev->num_registers++];
    copy_str(reg->name, name, YF_MAX_NAME_LEN);
    reg->offset = offset;
    reg->width = width;
    reg->readable = readable;
    reg->writable = writable;
    reg->reset_value = reset_value;
    return true;
}

/* ===== State Machine Management ===== */

bool yf_device_add_state(yf_fabric_t *yf, uint32_t device_idx,
                          const char *name, bool is_safe_state) {
    if (!yf || !name) return false;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return false;
    if (dev->num_states >= YF_MAX_STATES) return false;

    yf_state_t *s = &dev->states[dev->num_states];
    copy_str(s->name, name, YF_MAX_NAME_LEN);
    s->id = dev->num_states;
    s->is_safe_state = is_safe_state;
    dev->num_states++;
    return true;
}

bool yf_device_set_state(yf_fabric_t *yf, uint32_t device_idx,
                          uint32_t state_id) {
    if (!yf) return false;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return false;
    if (state_id >= dev->num_states) return false;

    /* If entering a safe state, count it */
    if (dev->states[state_id].is_safe_state &&
        dev->current_state != state_id) {
        dev->total_safe_state_transitions++;
        yf->total_safe_state_events++;
    }

    dev->current_state = state_id;
    return true;
}

/* ===== Event Management ===== */

bool yf_device_add_event(yf_fabric_t *yf, uint32_t device_idx,
                          const char *name, const char *schema,
                          uint32_t from_state, uint32_t to_state) {
    if (!yf || !name || !schema) return false;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return false;
    if (dev->num_events >= YF_MAX_EVENTS) return false;
    if (to_state >= dev->num_states) return false;

    yf_device_event_t *e = &dev->events[dev->num_events++];
    copy_str(e->name, name, YF_MAX_NAME_LEN);
    copy_str(e->schema, schema, EV_SCHEMA_LEN);
    e->from_state = from_state;
    e->to_state = to_state;
    return true;
}

bool yf_device_emit_event(yf_fabric_t *yf, uint32_t device_idx,
                           uint32_t event_idx, char *out_schema,
                           uint32_t schema_max) {
    if (!yf) return false;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return false;
    if (event_idx >= dev->num_events) return false;

    yf_device_event_t *e = &dev->events[event_idx];

    /* Check if we're in the correct source state */
    if (e->from_state != UINT32_MAX && e->from_state != dev->current_state) {
        return false;
    }

    /* Transition to target state */
    dev->current_state = e->to_state;
    dev->total_events_emitted++;

    /* Copy schema to output */
    if (out_schema && schema_max > 0) {
        copy_str(out_schema, e->schema, schema_max);
    }

    return true;
}

/* ===== Digital Twin ===== */

bool yf_twin_assert(yf_fabric_t *yf, uint32_t device_idx,
                     const char *predicted, const char *measured,
                     uint64_t sequence) {
    if (!yf || !predicted || !measured) return false;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return false;
    if (dev->num_twin_assertions >= YF_MAX_TWIN_ASSERTIONS) {
        /* Overwrite oldest assertion */
        dev->num_twin_assertions = 0;
    }

    yf_twin_assertion_t *a = &dev->twin_assertions[dev->num_twin_assertions++];
    copy_str(a->predicted_event, predicted, YF_MAX_NAME_LEN);
    copy_str(a->measured_event, measured, YF_MAX_NAME_LEN);
    a->sequence = sequence;

    if (str_eq(predicted, measured)) {
        a->status = YF_TWIN_CONSISTENT;
    } else {
        a->status = YF_TWIN_CONTRADICTORY;
        dev->total_contradictions++;
        yf->total_contradictions++;
    }

    return true;
}

uint32_t yf_twin_check_contradictions(yf_fabric_t *yf, uint32_t device_idx) {
    if (!yf) return 0;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return 0;

    uint32_t contradictions = 0;
    for (uint32_t i = 0; i < dev->num_twin_assertions; i++) {
        if (dev->twin_assertions[i].status == YF_TWIN_CONTRADICTORY) {
            contradictions++;
        }
    }
    return contradictions;
}

bool yf_twin_enter_safe_state(yf_fabric_t *yf, uint32_t device_idx) {
    if (!yf) return false;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return false;

    /* Find a safe state */
    for (uint32_t i = 0; i < dev->num_states; i++) {
        if (dev->states[i].is_safe_state) {
            return yf_device_set_state(yf, device_idx, i);
        }
    }
    return false;
}

/* ===== Utility Functions ===== */

uint32_t yf_get_devices_at_capability(yf_fabric_t *yf,
                                       yf_capability_state_t cap,
                                       uint32_t *out_indices,
                                       uint32_t max_out) {
    if (!yf || !out_indices) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < YF_MAX_DEVICES && count < max_out; i++) {
        if (yf->devices[i].registered && yf->devices[i].capability == cap) {
            out_indices[count++] = i;
        }
    }
    return count;
}

bool yf_device_ready_for(yf_fabric_t *yf, uint32_t device_idx,
                          yf_capability_state_t min_required) {
    if (!yf) return false;
    yf_device_t *dev = yf_get_device(yf, device_idx);
    if (!dev) return false;
    return dev->capability >= min_required;
}

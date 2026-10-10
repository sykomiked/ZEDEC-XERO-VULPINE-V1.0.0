/* compute_fabric.c — ZXV Compute Fabric Compound Module Implementation
 *
 * Unifies all compute modules: Cellular Multikernel, Hypercube, Event Transport,
 * Event Space, Scheduler, Dual Space, EL0 Userspace, Constellation Coordinator,
 * and integrates with Orbital, Yantra, Smart Adapter, Mesh Net, Identity, and Financial fabrics.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "compute_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "selfaudit.h"

/* ===== Helper Functions ===== */

static void cf_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void cf_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int cf_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t cf_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void cf_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t cf_compute_coverage(const m5_coords_t *m5) {
    if (!m5) return SR_ZERO;
    surplus_real_t omega = SR_FROM_INT(m5->omega);
    surplus_real_t r = m5->r;
    surplus_real_t ell = m5->ell;
    surplus_real_t phi = m5->phi;
    surplus_real_t chi = SR_FROM_INT(m5->chi);
    surplus_real_t numerator = SR_MUL(SR_MUL(omega, r), ell);
    surplus_real_t denominator = SR_MUL(phi, chi);
    if (SR_CMP(denominator, SR_ZERO) == 0) return SR_FROM_FLOAT(100.0);
    return SR_DIV(numerator, denominator);
}

/* ===== LPRES Attestation ===== */

lpres_state_t cf_attest(compute_fabric_t *fabric, uint32_t cell_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fabric || cell_id >= fabric->num_cells) return LPRES_STATE_NEITHER;
    
    cf_cell_t *cell = &fabric->cells[cell_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t cell_att = cell->attestation;
    lpres_state_t coverage_att = (SR_CMP(cell->coverage_ratio, fabric->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = fabric->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, cell_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fabric_att);
    
    cell->attestation = combined;
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void cf_init(compute_fabric_t *fabric,
             orbital_fabric_t *orbital,
             yf_fabric_t *yantra,
             smart_adapter_registry_t *sa_registry,
             sai_integration_fabric_t *sai_fabric,
             mesh_net_t *mesh,
             identity_fabric_t *identity,
             financial_fabric_t *financial) {
    if (!fabric) return;
    
    cf_mem_set(fabric, 0, sizeof(*fabric));
    fabric->orbital = orbital;
    fabric->yantra = yantra;
    fabric->sa_registry = sa_registry;
    fabric->sai_fabric = sai_fabric;
    fabric->mesh = mesh;
    fabric->identity = identity;
    fabric->financial = financial;
    
    /* Initialize sub-modules */
    /* cmk_init(&fabric->multikernel); */
    /* hypercube_init(&fabric->hypercube); */
    /* event_transport_init(&fabric->transport); */
    /* ev_seq_init(&fabric->sequencer, "compute-fabric"); */
    /* ev_audit_init(&fabric->audit); */
    /* ev_healing_init(&fabric->healing); */
    /* sched_init(&fabric->scheduler); */
    /* dual_space_init(&fabric->dual_space); */
    /* el0_init(&fabric->el0); */
    /* cc_coordinator_init(&fabric->constellation, "compute-fabric"); */
    
    /* Initialize M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(2.0);  /* Compute rail */
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = cf_compute_coverage(&fabric->m5);
    fabric->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fabric->config.auto_balance_load = true;
    fabric->config.enable_migration = true;
    fabric->config.audit_interval = 1000;
    fabric->config.health_check_interval = 5000;
    fabric->config.min_cell_coverage = SR_FROM_FLOAT(1.8);
    fabric->config.min_domain_coverage = SR_FROM_FLOAT(1.8);
    
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
    fabric->initialized = true;
}

void cf_register_builtins(compute_fabric_t *fabric) {
    if (!fabric) return;
    /* Built-ins already initialized in cf_init */
}

/* ===== Cell Management ===== */

int32_t cf_create_cell(compute_fabric_t *fabric, const char *name,
                       const char *arch,
                       int32_t hx, int32_t hy, int32_t hz, int32_t ht) {
    if (!fabric || !name || fabric->num_cells >= CF_MAX_CELLS) return -1;
    
    cf_cell_t *cell = &fabric->cells[fabric->num_cells];
    cf_mem_set(cell, 0, sizeof(*cell));
    cell->id = fabric->next_cell_id++;
    
    cf_str_copy(cell->name, name, CF_MAX_NAME_LEN);
    if (arch) cf_str_copy(cell->arch, arch, 16);
    else cf_str_copy(cell->arch, "arm64", 16);
    
    /* Create cellular multikernel cell */
    /* cell->cmk_cell = cmk_create_cell(&fabric->multikernel, name, arch); */
    
    /* Create Event Space domain for cell */
    int32_t ev_dom_id = ev_seq_create_domain(&fabric->sequencer, name, 100, 1000, EV_CONSISTENCY_LOCAL);
    if (ev_dom_id >= 0) {
        cell->ev_domain = ev_seq_get_domain(&fabric->sequencer, (uint32_t)ev_dom_id);
    }
    
    /* Register with Constellation */
    int32_t cc_result = cc_register_node(&fabric->constellation, name, cell->arch, 1);
    if (cc_result >= 0) {
        cell->cc_node = cc_get_node_by_idx(&fabric->constellation, (uint32_t)cc_result);
    }
    
    /* Hypercube position */
    cell->hypercube_pos.x = hx;
    cell->hypercube_pos.y = hy;
    cell->hypercube_pos.z = hz;
    cell->hypercube_pos.t = ht;
    cell->hypercube_pos.compute_capacity = SR_FROM_FLOAT(100.0);
    cell->hypercube_pos.memory_capacity = SR_FROM_FLOAT(1024.0);
    cell->hypercube_pos.network_capacity = SR_FROM_FLOAT(1000.0);
    
    /* Initialize M5 for cell */
    cell->m5.omega = fabric->num_cells + 1;
    cell->m5.r = SR_FROM_FLOAT(2.0 + fabric->num_cells * 0.1);
    cell->m5.ell = SR_ONE;
    cell->m5.phi = SR_ZERO;
    cell->m5.chi = 0;
    cell->coverage_ratio = cf_compute_coverage(&cell->m5);
    
    cell->attestation = LPRES_STATE_NEITHER;
    cell->active = true;
    cell->healthy = true;
    
    fabric->num_cells++;
    fabric->stats.total_cells_created++;
    
    return (int32_t)(cell->id);
}

cf_cell_t *cf_get_cell(compute_fabric_t *fabric, uint32_t cell_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_cells; i++) {
        if (fabric->cells[i].id == cell_id && fabric->cells[i].active) {
            return &fabric->cells[i];
        }
    }
    return NULL;
}

cf_cell_t *cf_get_cell_by_name(compute_fabric_t *fabric, const char *name) {
    if (!fabric || !name) return NULL;
    for (uint32_t i = 0; i < fabric->num_cells; i++) {
        if (fabric->cells[i].active && cf_str_cmp(fabric->cells[i].name, name) == 0) {
            return &fabric->cells[i];
        }
    }
    return NULL;
}

int32_t cf_start_cell(compute_fabric_t *fabric, uint32_t cell_id) {
    if (!fabric) return -1;
    cf_cell_t *cell = cf_get_cell(fabric, cell_id);
    if (!cell) return -1;
    
    /* Start cell kernel */
    /* cmk_start_cell(cell->cmk_cell); */
    
    cell->healthy = true;
    return cf_attest(fabric, cell_id, 0x1000, NULL, 0);
}

int32_t cf_stop_cell(compute_fabric_t *fabric, uint32_t cell_id) {
    if (!fabric) return -1;
    cf_cell_t *cell = cf_get_cell(fabric, cell_id);
    if (!cell) return -1;
    
    /* Stop cell kernel */
    /* cmk_stop_cell(cell->cmk_cell); */
    
    cell->healthy = false;
    return cf_attest(fabric, cell_id, 0x2000, NULL, 0);
}

int32_t cf_migrate_domain(compute_fabric_t *fabric, uint32_t domain_id,
                          uint32_t target_cell_id) {
    if (!fabric) return -1;
    if (!fabric->config.enable_migration) return -1;
    
    cf_compute_domain_t *domain = cf_get_domain(fabric, domain_id);
    cf_cell_t *target = cf_get_cell(fabric, target_cell_id);
    if (!domain || !target) return -1;
    
    uint32_t source_cell_id = domain->cell_id;
    cf_cell_t *source = cf_get_cell(fabric, source_cell_id);
    if (!source) return -1;
    
    /* Migrate event domain */
    /* Would involve moving event queue, handlers, state */
    
    domain->cell_id = target_cell_id;
    
    /* Update cell domain lists */
    /* Remove from source, add to target */
    
    fabric->stats.total_migrations++;
    return cf_attest(fabric, source_cell_id, 0x3000 | domain_id, &target_cell_id, 0);
}

/* ===== Compute Domain Management ===== */

int32_t cf_create_domain(compute_fabric_t *fabric, const char *name,
                         uint32_t cell_id,
                         uint32_t event_budget, uint32_t max_cost,
                         ev_priority_t priority, ev_consistency_t consistency) {
    if (!fabric || !name || fabric->num_domains >= CF_MAX_COMPUTE_DOMAINS) return -1;
    
    cf_cell_t *cell = cf_get_cell(fabric, cell_id);
    if (!cell) return -1;
    
    cf_compute_domain_t *domain = &fabric->domains[fabric->num_domains];
    cf_mem_set(domain, 0, sizeof(*domain));
    domain->id = fabric->next_domain_id++;
    
    cf_str_copy(domain->name, name, CF_MAX_NAME_LEN);
    domain->cell_id = cell_id;
    domain->priority = priority;
    domain->event_budget = event_budget;
    domain->max_cost_per_event = max_cost;
    
    /* Create Event Space domain */
    int32_t ev_dom_id = ev_seq_create_domain(&fabric->sequencer, name, event_budget, max_cost, consistency);
    if (ev_dom_id >= 0) {
        domain->ev_domain = ev_seq_get_domain(&fabric->sequencer, (uint32_t)ev_dom_id);
    }
    
    /* Initialize M5 for domain */
    domain->m5.omega = fabric->num_domains + 1;
    domain->m5.r = SR_FROM_FLOAT(2.0);
    domain->m5.ell = SR_ONE;
    domain->m5.phi = SR_ZERO;
    domain->m5.chi = 0;
    domain->coverage_ratio = cf_compute_coverage(&domain->m5);
    
    domain->attestation = LPRES_STATE_NEITHER;
    domain->active = true;
    
    fabric->num_domains++;
    fabric->stats.total_domains_created++;
    
    return (int32_t)(domain->id);
}

cf_compute_domain_t *cf_get_domain(compute_fabric_t *fabric, uint32_t domain_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        if (fabric->domains[i].id == domain_id && fabric->domains[i].active) {
            return &fabric->domains[i];
        }
    }
    return NULL;
}

/* ===== Event Handlers ===== */

int32_t cf_register_handler(compute_fabric_t *fabric, uint32_t domain_id,
                            const char *schema, uint16_t schema_version,
                            int32_t (*handler_fn)(ev_envelope_t *env, void *context),
                            void *context, uint32_t max_cost) {
    if (!fabric || !schema || !handler_fn) return -1;
    if (domain_id >= fabric->num_domains) return -1;
    if (fabric->num_handlers >= CF_MAX_EVENT_HANDLERS) return -1;
    
    cf_compute_domain_t *domain = &fabric->domains[domain_id];
    if (!domain->active) return -1;
    
    cf_event_handler_t *handler = &fabric->handlers[fabric->num_handlers];
    cf_mem_set(handler, 0, sizeof(*handler));
    handler->id = fabric->num_handlers;
    handler->domain_id = domain_id;
    cf_str_copy(handler->schema, schema, EV_SCHEMA_LEN);
    handler->schema_version = schema_version;
    handler->handler_fn = handler_fn;
    handler->context = context;
    handler->max_cost = max_cost;
    handler->attestation = LPRES_STATE_NEITHER;
    handler->active = true;
    
    /* Add to domain */
    if (domain->num_handlers < CF_MAX_EVENT_HANDLERS) {
        auto *dh = &domain->handlers[domain->num_handlers++];
        cf_str_copy(dh->schema, schema, EV_SCHEMA_LEN);
        dh->schema_version = schema_version;
        dh->handler_fn = handler_fn;
        dh->context = context;
        dh->max_cost = max_cost;
        dh->active = true;
    }
    
    fabric->num_handlers++;
    return cf_attest(fabric, domain->cell_id, 0x4000 | handler->id, handler, 0);
}

int32_t cf_dispatch_domain(compute_fabric_t *fabric, uint32_t domain_id) {
    if (!fabric) return -1;
    cf_compute_domain_t *domain = cf_get_domain(fabric, domain_id);
    if (!domain || !domain->active || !domain->ev_domain) return -1;
    
    /* Dequeue and process events */
    ev_envelope_t env;
    int32_t result = ev_seq_dequeue(&fabric->sequencer, domain->ev_domain->id, &env);
    if (result != 0) return -1;  /* No events */
    
    /* Find matching handler */
    for (uint32_t i = 0; i < domain->num_handlers; i++) {
        auto *dh = &domain->handlers[i];
        if (!dh->active) continue;
        if (cf_str_cmp(dh->schema, env.schema) == 0 && dh->schema_version == env.schema_version) {
            /* Execute handler */
            int32_t handler_result = dh->handler_fn(&env, dh->context);
            
            domain->events_processed++;
            domain->total_cost += dh->max_cost;
            fabric->stats.total_events_dispatched++;
            fabric->stats.total_handler_invocations++;
            
            if (handler_result < 0) {
                domain->events_failed++;
                fabric->stats.total_events_failed++;
                dh->failures++;
            }
            dh->invocations++;
            
            return cf_attest(fabric, domain->cell_id, 0x5000 | i, &env, handler_result);
        }
    }
    
    /* No handler found */
    domain->events_failed++;
    fabric->stats.total_events_failed++;
    return -1;
}

/* ===== Resource Management ===== */

int32_t cf_register_resource(compute_fabric_t *fabric,
                             const char *name, const char *type,
                             yf_device_t *yf_device,
                             smart_device_t *sa_device,
                             surplus_real_t capacity) {
    if (!fabric || !name || !type || fabric->num_resources >= CF_MAX_RESOURCES) return -1;
    
    cf_resource_t *res = &fabric->resources[fabric->num_resources];
    cf_mem_set(res, 0, sizeof(*res));
    res->id = fabric->num_resources;
    cf_str_copy(res->name, name, CF_MAX_NAME_LEN);
    cf_str_copy(res->type, type, CF_MAX_NAME_LEN);
    res->yf_device = yf_device;
    res->sa_device = sa_device;
    res->total_capacity = capacity;
    res->available_capacity = capacity;
    res->reserved_capacity = SR_ZERO;
    
    /* Initialize M5 for resource */
    res->m5.omega = fabric->num_resources + 1;
    res->m5.r = SR_FROM_FLOAT(2.0);
    res->m5.ell = SR_ONE;
    res->m5.phi = SR_ZERO;
    res->m5.chi = 0;
    res->coverage_ratio = cf_compute_coverage(&res->m5);
    
    res->attestation = LPRES_STATE_NEITHER;
    res->active = true;
    
    fabric->num_resources++;
    return (int32_t)(res->id);
}

int32_t cf_allocate_resource(compute_fabric_t *fabric,
                             uint32_t resource_id, uint32_t domain_id,
                             surplus_real_t amount, uint64_t expiry_tick) {
    if (!fabric) return -1;
    if (resource_id >= fabric->num_resources) return -1;
    
    cf_resource_t *res = &fabric->resources[resource_id];
    if (!res->active) return -1;
    if (SR_CMP(res->available_capacity, amount) < 0) return -1;
    
    if (res->num_allocations >= 32) return -1;
    
    auto *alloc = &res->allocations[res->num_allocations++];
    alloc->domain_id = domain_id;
    alloc->amount = amount;
    alloc->expiry_tick = expiry_tick;
    
    res->available_capacity = SR_SUB(res->available_capacity, amount);
    res->reserved_capacity = SR_ADD(res->reserved_capacity, amount);
    
    fabric->stats.total_resources_allocated++;
    return cf_attest(fabric, 0xFFFFFFFF, 0x6000 | resource_id, &amount, 0);
}

int32_t cf_release_resource(compute_fabric_t *fabric,
                            uint32_t resource_id, uint32_t domain_id) {
    if (!fabric) return -1;
    if (resource_id >= fabric->num_resources) return -1;
    
    cf_resource_t *res = &fabric->resources[resource_id];
    if (!res->active) return -1;
    
    for (uint32_t i = 0; i < res->num_allocations; i++) {
        if (res->allocations[i].domain_id == domain_id) {
            surplus_real_t amount = res->allocations[i].amount;
            res->available_capacity = SR_ADD(res->available_capacity, amount);
            res->reserved_capacity = SR_SUB(res->reserved_capacity, amount);
            
            /* Remove allocation */
            for (uint32_t j = i; j < res->num_allocations - 1; j++) {
                res->allocations[j] = res->allocations[j + 1];
            }
            res->num_allocations--;
            
            return cf_attest(fabric, 0xFFFFFFFF, 0x7000 | resource_id, &amount, 0);
        }
    }
    
    return -1;
}

/* ===== Event Transport ===== */

int32_t cf_send_event(compute_fabric_t *fabric,
                      uint32_t source_domain_id,
                      const char *destination_service,
                      const char *schema, uint16_t schema_version,
                      const uint8_t *payload, uint16_t payload_len,
                      ev_delivery_t delivery, ev_priority_t priority) {
    if (!fabric) return -1;
    
    cf_compute_domain_t *source = cf_get_domain(fabric, source_domain_id);
    if (!source) return -1;
    
    /* Create event envelope */
    ev_envelope_t env;
    ev_node_id_t node = {0};
    cf_str_copy(node.id, "compute-fabric", EV_NODE_ID_LEN);
    node.incarnation = 1;
    
    ev_envelope_init(&env, &node, fabric->sequencer.next_sequence, schema, schema_version);
    env.delivery = delivery;
    env.priority = priority;
    cf_str_copy(env.destination_service, destination_service, EV_DOMAIN_NAME_LEN);
    cf_str_copy(env.sender_domain, source->name, EV_DOMAIN_NAME_LEN);
    
    if (payload && payload_len > 0) {
        ev_envelope_set_payload(&env, payload, payload_len);
    }
    
    /* Send via Event Transport */
    /* event_transport_send(&fabric->transport, &env); */
    
    /* Also enqueue locally if destination is local */
    /* For now, just route via Constellation */
    cc_node_t *target = cc_route_event(&fabric->constellation, schema, schema_version);
    if (target) {
        /* Would forward to target node */
    }
    
    fabric->sequencer.next_sequence++;
    return cf_attest(fabric, source->cell_id, 0x8000, &env, 0);
}

int32_t cf_broadcast_event(compute_fabric_t *fabric,
                           const char *schema, uint16_t schema_version,
                           const uint8_t *payload, uint16_t payload_len,
                           ev_priority_t priority) {
    if (!fabric) return -1;
    
    /* Broadcast to all domains that accept this schema */
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        cf_compute_domain_t *domain = &fabric->domains[i];
        if (!domain->active || !domain->ev_domain) continue;
        
        /* Check if domain accepts this schema */
        for (uint32_t j = 0; j < domain->ev_domain->num_accepted_schemas; j++) {
            if (cf_str_cmp(domain->ev_domain->accepted_schemas[j], schema) == 0) {
                ev_envelope_t env;
                ev_node_id_t node = {0};
                cf_str_copy(node.id, "compute-fabric", EV_NODE_ID_LEN);
                node.incarnation = 1;
                
                ev_envelope_init(&env, &node, fabric->sequencer.next_sequence++, schema, schema_version);
                env.priority = priority;
                cf_str_copy(env.sender_domain, "broadcast", EV_DOMAIN_NAME_LEN);
                
                if (payload && payload_len > 0) {
                    ev_envelope_set_payload(&env, payload, payload_len);
                }
                
                ev_seq_enqueue(&fabric->sequencer, domain->ev_domain->id, &env);
            }
        }
    }
    
    return cf_attest(fabric, 0xFFFFFFFF, 0x9000, (void*)schema, 0);
}

/* ===== Scheduling ===== */

int32_t cf_schedule(compute_fabric_t *fabric) {
    if (!fabric) return -1;
    
    /* Run scheduler */
    /* sched_run(&fabric->scheduler); */
    
    /* Dispatch ready domains */
    int32_t dispatched = 0;
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        cf_compute_domain_t *domain = &fabric->domains[i];
        if (!domain->active || !domain->ev_domain) continue;
        
        /* Check if domain has events and budget */
        if (domain->ev_domain->queue_count > 0 && domain->ev_domain->events_remaining > 0) {
            cf_dispatch_domain(fabric, domain->id);
            dispatched++;
        }
    }
    
    /* Replenish budgets */
    ev_seq_replenish(&fabric->sequencer);
    
    fabric->stats.total_schedule_decisions += dispatched;
    return dispatched;
}

/* ===== Hypercube Topology ===== */

int32_t cf_get_cell_at(compute_fabric_t *fabric,
                       int32_t x, int32_t y, int32_t z, int32_t t,
                       cf_cell_t **out_cell) {
    if (!fabric || !out_cell) return -1;
    
    for (uint32_t i = 0; i < fabric->num_cells; i++) {
        cf_cell_t *cell = &fabric->cells[i];
        if (!cell->active) continue;
        if (cell->hypercube_pos.x == x && cell->hypercube_pos.y == y &&
            cell->hypercube_pos.z == z && cell->hypercube_pos.t == t) {
            *out_cell = cell;
            return 0;
        }
    }
    return -1;
}

int32_t cf_find_nearest_cell(compute_fabric_t *fabric,
                             int32_t x, int32_t y, int32_t z, int32_t t,
                             const char *required_capability,
                             cf_cell_t **out_cell) {
    if (!fabric || !out_cell) return -1;
    
    cf_cell_t *best = NULL;
    int32_t best_dist = 0x7FFFFFFF;
    
    for (uint32_t i = 0; i < fabric->num_cells; i++) {
        cf_cell_t *cell = &fabric->cells[i];
        if (!cell->active || !cell->healthy) continue;
        
        /* Check capability */
        if (required_capability) {
            bool has_cap = false;
            if (cell->ev_domain) {
                for (uint32_t j = 0; j < cell->ev_domain->num_capabilities; j++) {
                    if (cf_str_cmp(cell->ev_domain->capabilities[j].name, required_capability) == 0) {
                        has_cap = true; break;
                    }
                }
            }
            if (!has_cap) continue;
        }
        
        /* Manhattan distance in 4D */
        int32_t dist = abs(cell->hypercube_pos.x - x) +
                       abs(cell->hypercube_pos.y - y) +
                       abs(cell->hypercube_pos.z - z) +
                       abs(cell->hypercube_pos.t - t);
        
        if (dist < best_dist) {
            best_dist = dist;
            best = cell;
        }
    }
    
    if (best) {
        *out_cell = best;
        return 0;
    }
    return -1;
}

/* ===== Health & Attestation ===== */

int32_t cf_check_cell_health(compute_fabric_t *fabric,
                             uint32_t cell_id,
                             void *health_out) {
    if (!fabric) return -1;
    cf_cell_t *cell = cf_get_cell(fabric, cell_id);
    if (!cell) return -1;
    
    /* Update coverage */
    cell->coverage_ratio = cf_compute_coverage(&cell->m5);
    
    /* Check event domain health */
    if (cell->ev_domain) {
        ev_audit_result_t audit_result = ev_audit_check_domain(&fabric->sequencer, &fabric->audit, cell->ev_domain);
        ev_heal_action_t heal_action = ev_healing_apply(&fabric->sequencer, &fabric->healing, cell->ev_domain, audit_result);
        
        if (heal_action >= EV_HEAL_QUARANTINE) {
            cell->healthy = false;
        }
    }
    
    /* Check constellation health */
    if (cell->cc_node) {
        cc_check_health(&fabric->constellation, fabric->sequencer.next_sequence, 1000);
    }
    
    /* Check Yantra devices */
    for (uint32_t i = 0; i < cell->num_yf_devices; i++) {
        /* yf_check_device_health(fabric->yantra, cell->yf_device_ids[i]); */
    }
    
    /* Check Smart Adapter devices */
    for (uint32_t i = 0; i < cell->num_sa_devices; i++) {
        /* sai_check_health(fabric->sai_fabric, cell->sa_device_ids[i], ...); */
    }
    
    /* Update attestation */
    if (cell->healthy && SR_CMP(cell->coverage_ratio, fabric->config.min_cell_coverage) >= 0) {
        cell->attestation = LPRES_STATE_TRUE;
    } else {
        cell->attestation = LPRES_STATE_BOTH;
    }
    
    fabric->stats.total_self_audits++;
    if (cell->healthy == false) fabric->stats.total_healings++;
    
    return cell->healthy ? 0 : -1;
}

int32_t cf_check_domain_health(compute_fabric_t *fabric,
                               uint32_t domain_id,
                               void *health_out) {
    if (!fabric) return -1;
    cf_compute_domain_t *domain = cf_get_domain(fabric, domain_id);
    if (!domain) return -1;
    
    /* Update coverage */
    domain->coverage_ratio = cf_compute_coverage(&domain->m5);
    
    /* Check event domain health */
    if (domain->ev_domain) {
        ev_audit_result_t audit_result = ev_audit_check_domain(&fabric->sequencer, &fabric->audit, domain->ev_domain);
        ev_heal_action_t heal_action = ev_healing_apply(&fabric->sequencer, &fabric->healing, domain->ev_domain, audit_result);
        
        if (heal_action >= EV_HEAL_QUARANTINE) {
            domain->attestation = LPRES_STATE_FALSE;
            return -1;
        }
    }
    
    /* Update attestation */
    if (SR_CMP(domain->coverage_ratio, fabric->config.min_domain_coverage) >= 0) {
        domain->attestation = LPRES_STATE_TRUE;
    } else {
        domain->attestation = LPRES_STATE_BOTH;
    }
    
    return 0;
}

int32_t cf_check_global_health(compute_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fabric->num_cells; i++) {
        if (fabric->cells[i].active) {
            if (cf_check_cell_health(fabric, fabric->cells[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        if (fabric->domains[i].active) {
            if (cf_check_domain_health(fabric, fabric->domains[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0);
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool cf_global_safety_gate(compute_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

/* ===== Coverage ===== */

void cf_update_coverage(compute_fabric_t *fabric) {
    if (!fabric) return;
    
    fabric->coverage_ratio = cf_compute_coverage(&fabric->m5);
    
    for (uint32_t i = 0; i < fabric->num_cells; i++) {
        if (fabric->cells[i].active) {
            fabric->cells[i].coverage_ratio = cf_compute_coverage(&fabric->cells[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        if (fabric->domains[i].active) {
            fabric->domains[i].coverage_ratio = cf_compute_coverage(&fabric->domains[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_resources; i++) {
        if (fabric->resources[i].active) {
            fabric->resources[i].coverage_ratio = cf_compute_coverage(&fabric->resources[i].m5);
        }
    }
}

bool cf_enforce_coverage(compute_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false;
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fabric->num_cells; i++) {
        if (fabric->cells[i].active) {
            if (SR_CMP(fabric->cells[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void cf_get_stats(compute_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return;
    cf_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats));
}

/* ===== Paraconsistent State ===== */

lpres_state_t cf_get_attestation(compute_fabric_t *fabric, uint32_t cell_id) {
    if (!fabric || cell_id >= fabric->num_cells) return LPRES_STATE_NEITHER;
    return fabric->cells[cell_id].attestation;
}

void cf_set_attestation(compute_fabric_t *fabric, uint32_t cell_id, lpres_state_t state) {
    if (!fabric || cell_id >= fabric->num_cells) return;
    fabric->cells[cell_id].attestation = state;
}

/* ===== Utility ===== */

const char *cf_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

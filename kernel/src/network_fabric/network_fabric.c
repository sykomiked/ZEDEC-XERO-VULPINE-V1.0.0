/* network_fabric.c — ZXV Network Fabric Compound Module Implementation
 *
 * Unifies all network modules: Mesh Net, JDR PirateNet, Radio, Bluetooth,
 * WiFi, Event Transport, Smart Adapter, Firmware Adapters, with Orbital,
 * Identity, Financial, and Compute fabric integration.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "network_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "selfaudit.h"

/* ===== Helper Functions ===== */

static void nf_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void nf_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int nf_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t nf_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void nf_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t nf_compute_coverage(const m5_coords_t *m5) {
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

lpres_state_t nf_attest(network_fabric_t *fabric, uint32_t interface_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fabric || interface_id >= fabric->num_interfaces) return LPRES_STATE_NEITHER;
    
    nf_interface_t *iface = &fabric->interfaces[interface_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t iface_att = iface->attestation;
    lpres_state_t coverage_att = (SR_CMP(iface->coverage_ratio, fabric->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = fabric->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, iface_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fabric_att);
    
    iface->attestation = combined;
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void nf_init(network_fabric_t *fabric,
             orbital_fabric_t *orbital,
             identity_fabric_t *identity,
             financial_fabric_t *financial,
             compute_fabric_t *compute) {
    if (!fabric) return;
    
    nf_mem_set(fabric, 0, sizeof(*fabric));
    fabric->orbital = orbital;
    fabric->identity = identity;
    fabric->financial = financial;
    fabric->compute = compute;
    
    /* Initialize sub-modules */
    /* mn_init(&fabric->mesh, 0, "network-fabric", NULL); */
    /* jdr_network_init(&fabric->jdr_network); */
    /* jdr_adapter_registry_init(&fabric->jdr_registry); */
    /* radio_init(&fabric->radio); */
    /* bluetooth_init(&fabric->bluetooth); */
    /* wifi_init(&fabric->wifi); */
    /* event_transport_init(&fabric->transport); */
    /* smart_adapter_registry_init(&fabric->sa_registry); */
    /* sai_init(&fabric->sai_fabric, &fabric->sa_registry, &fabric->orbital->elevator, &fabric->orbital->yantra); */
    
    /* Register JDR firmware adapters */
    /* jdr_adapter_register(&fabric->jdr_registry, jdr_adapter_create_ad9361()); */
    /* jdr_adapter_register(&fabric->jdr_registry, jdr_adapter_create_hackrf_one()); */
    /* jdr_adapter_register(&fabric->jdr_registry, jdr_adapter_create_rtl_sdr()); */
    /* ... more adapters ... */
    
    /* Initialize M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(1.0);  /* Network rail */
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = nf_compute_coverage(&fabric->m5);
    fabric->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fabric->config.auto_fhss = true;
    fabric->config.auto_power_control = true;
    fabric->config.require_porter_house = true;
    fabric->config.min_link_snr = SR_FROM_FLOAT(10.0);  /* 10 dB */
    fabric->config.max_hop_count = 8;
    fabric->config.enable_mesh_token_settlement = true;
    
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
    fabric->initialized = true;
}

void nf_register_builtins(network_fabric_t *fabric) {
    if (!fabric) return;
}

/* ===== Interface Management ===== */

int32_t nf_create_interface(network_fabric_t *fabric,
                            const char *name, nf_iface_type_t type,
                            smart_device_t *sa_device) {
    if (!fabric || !name || fabric->num_interfaces >= NF_MAX_INTERFACES) return -1;
    
    nf_interface_t *iface = &fabric->interfaces[fabric->num_interfaces];
    nf_mem_set(iface, 0, sizeof(*iface));
    iface->id = fabric->next_interface_id++;
    
    nf_str_copy(iface->name, name, NF_MAX_NAME_LEN);
    iface->type = type;
    iface->sa_device = sa_device;
    
    /* Set defaults based on type */
    switch (type) {
        case NF_IFACE_JDR:
            iface->frequency_min = 70000000;      /* 70 MHz */
            iface->frequency_max = 6000000000;    /* 6 GHz */
            iface->max_bandwidth = 56000000;      /* 56 MHz */
            iface->supported_modulations = 0xFFFFFFFF;
            iface->tx_power_max = 30;
            iface->rx_sensitivity = -120;
            break;
        case NF_IFACE_RADIO:
            iface->frequency_min = 600000000;     /* 600 MHz */
            iface->frequency_max = 2700000000;    /* 2.7 GHz */
            iface->max_bandwidth = 20000000;      /* 20 MHz */
            iface->supported_modulations = 0x0000FFFF;
            iface->tx_power_max = 23;
            iface->rx_sensitivity = -110;
            break;
        case NF_IFACE_WIFI:
            iface->frequency_min = 2400000000;    /* 2.4 GHz */
            iface->frequency_max = 5800000000;    /* 5.8 GHz */
            iface->max_bandwidth = 160000000;     /* 160 MHz */
            iface->supported_modulations = 0x000000FF;
            iface->tx_power_max = 20;
            iface->rx_sensitivity = -95;
            break;
        case NF_IFACE_LORA:
            iface->frequency_min = 860000000;     /* 860 MHz */
            iface->frequency_max = 930000000;     /* 930 MHz */
            iface->max_bandwidth = 500000;        /* 500 kHz */
            iface->supported_modulations = 0x00000001;
            iface->tx_power_max = 20;
            iface->rx_sensitivity = -140;
            break;
        default:
            iface->frequency_min = 0;
            iface->frequency_max = 0xFFFFFFFF;
            iface->max_bandwidth = 0;
            iface->supported_modulations = 0;
            iface->tx_power_max = 0;
            iface->rx_sensitivity = 0;
            break;
    }
    
    /* Initialize M5 for interface */
    iface->m5.omega = fabric->num_interfaces + 1;
    iface->m5.r = SR_FROM_FLOAT(1.0 + fabric->num_interfaces * 0.01);
    iface->m5.ell = SR_ONE;
    iface->m5.phi = SR_ZERO;
    iface->m5.chi = 0;
    iface->coverage_ratio = nf_compute_coverage(&iface->m5);
    
    iface->attestation = LPRES_STATE_NEITHER;
    iface->active = true;
    
    fabric->num_interfaces++;
    fabric->stats.total_interfaces_created++;
    
    return (int32_t)(iface->id);
}

nf_interface_t *nf_get_interface(network_fabric_t *fabric, uint32_t interface_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_interfaces; i++) {
        if (fabric->interfaces[i].id == interface_id && fabric->interfaces[i].active) {
            return &fabric->interfaces[i];
        }
    }
    return NULL;
}

nf_interface_t *nf_get_interface_by_name(network_fabric_t *fabric, const char *name) {
    if (!fabric || !name) return NULL;
    for (uint32_t i = 0; i < fabric->num_interfaces; i++) {
        if (fabric->interfaces[i].active && nf_str_cmp(fabric->interfaces[i].name, name) == 0) {
            return &fabric->interfaces[i];
        }
    }
    return NULL;
}

int32_t nf_interface_up(network_fabric_t *fabric, uint32_t interface_id) {
    if (!fabric) return -1;
    nf_interface_t *iface = nf_get_interface(fabric, interface_id);
    if (!iface) return -1;
    
    iface->up = true;
    return nf_attest(fabric, interface_id, 0x1000, NULL, 0);
}

int32_t nf_interface_down(network_fabric_t *fabric, uint32_t interface_id) {
    if (!fabric) return -1;
    nf_interface_t *iface = nf_get_interface(fabric, interface_id);
    if (!iface) return -1;
    
    iface->up = false;
    return nf_attest(fabric, interface_id, 0x2000, NULL, 0);
}

int32_t nf_interface_set_freq(network_fabric_t *fabric, uint32_t interface_id,
                              uint64_t freq_hz, uint32_t bandwidth_hz) {
    if (!fabric) return -1;
    nf_interface_t *iface = nf_get_interface(fabric, interface_id);
    if (!iface) return -1;
    
    if (freq_hz < iface->frequency_min || freq_hz > iface->frequency_max) return -1;
    if (bandwidth_hz > iface->max_bandwidth) return -1;
    
    /* Configure via Smart Adapter or JDR */
    if (iface->sa_device) {
        /* smart_adapter_set_freq(iface->sa_device, freq_hz, bandwidth_hz); */
    }
    if (iface->jdr_transceiver) {
        /* jdr_transceiver_set_freq(iface->jdr_transceiver, freq_hz, bandwidth_hz); */
    }
    
    return nf_attest(fabric, interface_id, 0x3000, &freq_hz, 0);
}

int32_t nf_interface_set_modulation(network_fabric_t *fabric, uint32_t interface_id,
                                    uint32_t modulation_mask) {
    if (!fabric) return -1;
    nf_interface_t *iface = nf_get_interface(fabric, interface_id);
    if (!iface) return -1;
    
    if ((modulation_mask & iface->supported_modulations) != modulation_mask) return -1;
    
    return nf_attest(fabric, interface_id, 0x4000, &modulation_mask, 0);
}

int32_t nf_interface_set_tx_power(network_fabric_t *fabric, uint32_t interface_id,
                                  int32_t power_dbm) {
    if (!fabric) return -1;
    nf_interface_t *iface = nf_get_interface(fabric, interface_id);
    if (!iface) return -1;
    
    if (power_dbm > iface->tx_power_max) return -1;
    
    return nf_attest(fabric, interface_id, 0x5000, &power_dbm, 0);
}

/* ===== Mesh Network ===== */

int32_t nf_create_mesh_network(network_fabric_t *fabric,
                               const char *name, mn_net_access_t access,
                               const word168_t *creator_id, uint32_t creator_trust,
                               const uint32_t *interface_ids, uint32_t num_interfaces) {
    if (!fabric || !name || fabric->num_mesh_networks >= NF_MAX_MESH_NETWORKS) return -1;
    
    /* Create base mesh network */
    word168_t cid = creator_id ? *creator_id : (word168_t){0};
    int32_t mn_result = mn_create_network(&fabric->mesh, name, access, &cid, creator_trust);
    if (mn_result < 0) return -1;
    
    nf_mesh_network_t *mesh = &fabric->mesh_networks[fabric->num_mesh_networks];
    nf_mem_set(mesh, 0, sizeof(*mesh));
    mesh->base = *mn_get_network(&fabric->mesh, (uint32_t)mn_result);
    
    /* Assign interfaces */
    for (uint32_t i = 0; i < num_interfaces && i < 8; i++) {
        nf_interface_t *iface = nf_get_interface(fabric, interface_ids[i]);
        if (iface) {
            mesh->interface_ids[mesh->num_interfaces++] = interface_ids[i];
            iface->mesh_network_id = mesh->base.id;
        }
    }
    
    /* Initialize spectrum */
    mesh->spectrum.center_freq = 2400000000;  /* 2.4 GHz default */
    mesh->spectrum.bandwidth = 20000000;      /* 20 MHz */
    mesh->spectrum.fhss_enabled = fabric->config.auto_fhss;
    
    mesh->mesh_attestation = LPRES_STATE_NEITHER;
    mesh->active = true;
    
    fabric->num_mesh_networks++;
    fabric->stats.total_mesh_networks++;
    
    return (int32_t)(mesh->base.id);
}

nf_mesh_network_t *nf_get_mesh_network(network_fabric_t *fabric, uint32_t network_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_mesh_networks; i++) {
        if (fabric->mesh_networks[i].base.id == network_id && fabric->mesh_networks[i].active) {
            return &fabric->mesh_networks[i];
        }
    }
    return NULL;
}

/* ===== JDR Transceiver ===== */

int32_t nf_create_jdr_transceiver(network_fabric_t *fabric,
                                  uint32_t interface_id,
                                  uint32_t mesh_network_id,
                                  jdr_execution_mode_t mode) {
    if (!fabric) return -1;
    
    nf_interface_t *iface = nf_get_interface(fabric, interface_id);
    nf_mesh_network_t *mesh = nf_get_mesh_network(fabric, mesh_network_id);
    if (!iface || !mesh) return -1;
    
    if (fabric->num_jdr_transceivers >= NF_MAX_JDR_TRANSCEIVERS) return -1;
    
    /* Create JDR transceiver */
    jdr_transceiver_t *jdr = jdr_transceiver_create(&fabric->jdr_network, mode, &fabric->jdr_registry);
    if (!jdr) return -1;
    
    nf_jdr_transceiver_t *xdr = &fabric->jdr_transceivers[fabric->num_jdr_transceivers];
    nf_mem_set(xdr, 0, sizeof(*xdr));
    xdr->base = *jdr;
    xdr->interface_id = interface_id;
    xdr->mesh_network_id = mesh_network_id;
    
    /* SDR config */
    xdr->sdr_config.sample_rate = 56000000;
    xdr->sdr_config.fft_size = 2048;
    xdr->sdr_config.num_subcarriers = 1024;
    xdr->sdr_config.ofdm_enabled = true;
    xdr->sdr_config.fhss_enabled = fabric->config.auto_fhss;
    xdr->sdr_config.hop_rate = 1000;
    
    /* Link to interface */
    iface->jdr_transceiver = jdr;
    mesh->jdr_transceiver_ids[mesh->num_jdr_transceivers++] = fabric->num_jdr_transceivers;
    
    xdr->transceiver_attestation = LPRES_STATE_NEITHER;
    xdr->active = true;
    
    fabric->num_jdr_transceivers++;
    fabric->stats.total_jdr_transceivers++;
    
    return nf_attest(fabric, interface_id, 0x6000 | fabric->num_jdr_transceivers, xdr, 0);
}

nf_jdr_transceiver_t *nf_get_jdr_transceiver(network_fabric_t *fabric, uint32_t transceiver_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_jdr_transceivers; i++) {
        if (fabric->jdr_transceivers[i].base.device_id == transceiver_id && fabric->jdr_transceivers[i].active) {
            return &fabric->jdr_transceivers[i];
        }
    }
    return NULL;
}

int32_t nf_jdr_transmit(network_fabric_t *fabric, uint32_t transceiver_id,
                        const uint8_t *data, uint32_t len,
                        uint64_t freq_hz, uint32_t modulation) {
    if (!fabric) return -1;
    
    nf_jdr_transceiver_t *xdr = nf_get_jdr_transceiver(fabric, transceiver_id);
    if (!xdr) return -1;
    
    /* Transmit via JDR */
    /* jdr_transceiver_transmit(&xdr->base, data, len, freq_hz, modulation); */
    
    fabric->stats.total_bytes_tx += len;
    fabric->stats.total_packets_tx++;
    
    return nf_attest(fabric, xdr->interface_id, 0x7000 | transceiver_id, (void*)data, 0);
}

int32_t nf_jdr_receive(network_fabric_t *fabric, uint32_t transceiver_id,
                       uint8_t *buffer, uint32_t max_len,
                       uint32_t timeout_ms) {
    if (!fabric) return -1;
    
    nf_jdr_transceiver_t *xdr = nf_get_jdr_transceiver(fabric, transceiver_id);
    if (!xdr) return -1;
    
    /* Receive via JDR */
    /* int32_t len = jdr_transceiver_receive(&xdr->base, buffer, max_len, timeout_ms); */
    int32_t len = 0;  /* Placeholder */
    
    if (len > 0) {
        fabric->stats.total_bytes_rx += len;
        fabric->stats.total_packets_rx++;
    }
    
    return len;
}

/* ===== Routing ===== */

int32_t nf_create_route(network_fabric_t *fabric,
                        uint32_t mesh_network_id,
                        mn_route_type_t type,
                        const word168_t *source,
                        const word168_t *destination,
                        uint64_t price_per_unit, uint64_t capacity,
                        uint64_t current_cycle) {
    if (!fabric) return -1;
    
    nf_mesh_network_t *mesh = nf_get_mesh_network(fabric, mesh_network_id);
    if (!mesh) return -1;
    
    /* Create base mesh route */
    int32_t mn_result = mn_create_route(&fabric->mesh, mesh_network_id, type,
                                         source, destination, price_per_unit, capacity, current_cycle);
    if (mn_result < 0) return -1;
    
    if (fabric->num_routes >= NF_MAX_ROUTES) return -1;
    
    nf_route_t *route = &fabric->routes[fabric->num_routes];
    nf_mem_set(route, 0, sizeof(*route));
    route->base = *mn_get_route(&fabric->mesh, (uint32_t)mn_result);
    
    /* Assign egress interface */
    if (mesh->num_interfaces > 0) {
        route->interface_id = mesh->interface_ids[0];
    }
    
    /* Initialize link budget */
    route->link_budget.tx_power_dbm = 20;
    route->link_budget.rx_sensitivity_dbm = -100;
    route->link_budget.path_loss_db = 80;
    route->link_budget.fade_margin_db = 20;
    route->link_budget.snr_db = 20;
    
    route->latency_ms = SR_FROM_FLOAT(10.0);
    route->jitter_ms = SR_FROM_FLOAT(1.0);
    route->packet_loss_rate = SR_FROM_FLOAT(0.001);
    route->throughput_mbps = SR_FROM_FLOAT(10.0);
    
    route->route_attestation = LPRES_STATE_NEITHER;
    route->active = true;
    
    fabric->num_routes++;
    fabric->stats.total_routes_created++;
    
    return (int32_t)(route->base.id);
}

int32_t nf_find_route(network_fabric_t *fabric,
                      const word168_t *source,
                      const word168_t *destination,
                      nf_route_t **out_route) {
    if (!fabric || !source || !destination || !out_route) return -1;
    
    for (uint32_t i = 0; i < fabric->num_routes; i++) {
        nf_route_t *route = &fabric->routes[i];
        if (!route->active) continue;
        
        bool src_match = true, dst_match = true;
        for (int j = 0; j < 168/64; j++) {
            if (route->base.source.words[j] != source->words[j]) src_match = false;
            if (route->base.destination.words[j] != destination->words[j]) dst_match = false;
        }
        
        if (src_match && dst_match) {
            *out_route = route;
            return 0;
        }
    }
    
    return -1;
}

/* ===== Peers ===== */

int32_t nf_discover_peers(network_fabric_t *fabric, uint32_t interface_id) {
    if (!fabric) return -1;
    nf_interface_t *iface = nf_get_interface(fabric, interface_id);
    if (!iface) return -1;
    
    /* Scan for peers via JDR or radio */
    /* Would implement actual discovery */
    
    return nf_attest(fabric, interface_id, 0x8000, NULL, 0);
}

int32_t nf_connect_peer(network_fabric_t *fabric,
                        uint32_t mesh_network_id,
                        const word168_t *peer_id, uint32_t trust_weight,
                        uint64_t bandwidth) {
    if (!fabric) return -1;
    
    nf_mesh_network_t *mesh = nf_get_mesh_network(fabric, mesh_network_id);
    if (!mesh) return -1;
    
    /* Join peer to mesh */
    int32_t result = mn_join_network(&fabric->mesh, mesh_network_id, peer_id, trust_weight, bandwidth);
    if (result < 0) return -1;
    
    if (fabric->num_peers >= NF_MAX_PEERS) return -1;
    
    nf_peer_t *peer = &fabric->peers[fabric->num_peers];
    nf_mem_set(peer, 0, sizeof(*peer));
    peer->base = *mn_get_peer(&fabric->mesh, peer_id);  /* Would need this function */
    peer->base.peer_id = *peer_id;
    peer->base.trust_weight = trust_weight;
    peer->base.bandwidth_avail = bandwidth;
    
    peer->peer_attestation = LPRES_STATE_NEITHER;
    peer->active = true;
    
    fabric->num_peers++;
    fabric->stats.total_peers_connected++;
    
    return nf_attest(fabric, 0xFFFFFFFF, 0x9000 | fabric->num_peers, peer, 0);
}

/* ===== Health & Attestation ===== */

int32_t nf_check_interface_health(network_fabric_t *fabric,
                                  uint32_t interface_id,
                                  void *health_out) {
    if (!fabric) return -1;
    nf_interface_t *iface = nf_get_interface(fabric, interface_id);
    if (!iface) return -1;
    
    /* Update coverage */
    iface->coverage_ratio = nf_compute_coverage(&iface->m5);
    
    /* Check Smart Adapter health */
    if (iface->sa_device) {
        /* smart_adapter_health_check(iface->sa_device); */
    }
    
    /* Check JDR transceiver */
    if (iface->jdr_transceiver) {
        /* jdr_transceiver_health_check(iface->jdr_transceiver); */
    }
    
    /* Update attestation */
    if (SR_CMP(iface->coverage_ratio, fabric->min_coverage_ratio) >= 0) {
        iface->attestation = LPRES_STATE_TRUE;
    } else {
        iface->attestation = LPRES_STATE_BOTH;
    }
    
    return iface->attestation == LPRES_STATE_TRUE ? 0 : -1;
}

int32_t nf_check_mesh_health(network_fabric_t *fabric,
                             uint32_t mesh_network_id,
                             void *health_out) {
    if (!fabric) return -1;
    nf_mesh_network_t *mesh = nf_get_mesh_network(fabric, mesh_network_id);
    if (!mesh) return -1;
    
    /* Check mesh health */
    uint32_t expired = mn_check_expired(&fabric->mesh, 0);
    if (expired > 0) {
        mesh->mesh_attestation = LPRES_STATE_BOTH;
    } else {
        mesh->mesh_attestation = LPRES_STATE_TRUE;
    }
    
    /* Check interfaces */
    for (uint32_t i = 0; i < mesh->num_interfaces; i++) {
        nf_check_interface_health(fabric, mesh->interface_ids[i], NULL);
    }
    
    return mesh->mesh_attestation == LPRES_STATE_TRUE ? 0 : -1;
}

int32_t nf_check_global_health(network_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fabric->num_interfaces; i++) {
        if (fabric->interfaces[i].active) {
            if (nf_check_interface_health(fabric, fabric->interfaces[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_mesh_networks; i++) {
        if (fabric->mesh_networks[i].active) {
            if (nf_check_mesh_health(fabric, fabric->mesh_networks[i].base.id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0);
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool nf_global_safety_gate(network_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

/* ===== Coverage ===== */

void nf_update_coverage(network_fabric_t *fabric) {
    if (!fabric) return;
    
    fabric->coverage_ratio = nf_compute_coverage(&fabric->m5);
    
    for (uint32_t i = 0; i < fabric->num_interfaces; i++) {
        if (fabric->interfaces[i].active) {
            fabric->interfaces[i].coverage_ratio = nf_compute_coverage(&fabric->interfaces[i].m5);
        }
    }
}

bool nf_enforce_coverage(network_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false;
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fabric->num_interfaces; i++) {
        if (fabric->interfaces[i].active) {
            if (SR_CMP(fabric->interfaces[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void nf_get_stats(network_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return;
    nf_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats));
}

/* ===== Paraconsistent State ===== */

lpres_state_t nf_get_attestation(network_fabric_t *fabric, uint32_t interface_id) {
    if (!fabric || interface_id >= fabric->num_interfaces) return LPRES_STATE_NEITHER;
    return fabric->interfaces[interface_id].attestation;
}

void nf_set_attestation(network_fabric_t *fabric, uint32_t interface_id, lpres_state_t state) {
    if (!fabric || interface_id >= fabric->num_interfaces) return;
    fabric->interfaces[interface_id].attestation = state;
}

/* ===== Utility ===== */

const char *nf_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

const char *nf_iface_type_name(nf_iface_type_t type) {
    static const char *names[] = {
        "UNUSED", "JDR", "MESH", "RADIO", "BLUETOOTH", "WIFI", "LORA", "ETHERNET", "VIRTUAL"
    };
    if (type <= NF_IFACE_VIRTUAL) return names[type];
    return "UNKNOWN";
}

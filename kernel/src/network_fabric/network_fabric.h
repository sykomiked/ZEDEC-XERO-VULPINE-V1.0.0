/* network_fabric.h — ZXV Network Fabric Compound Module
 *
 * The Network Fabric unifies all networking, radio, and transport modules
 * into a single coherent fabric for multi-layer, multi-protocol, mesh-native
 * communication with post-quantum security.
 *
 * Sub-modules integrated:
 *   1. Mesh Net — P2P mesh networking with trade routes
 *   2. JDR PirateNet — Virtual SDR transceiver (all bands, all modulations)
 *   3. Radio — Cellular & satellite modem abstraction
 *   4. Bluetooth — Bluetooth LE / Classic stack
 *   5. WiFi — 802.11 stack with mesh extensions
 *   6. Cellular — LTE/5G/NR modem control
 *   7. LoRa — LoRaWAN stack
 *   8. Event Transport — Reliable event delivery
 *   9. Smart Adapter — Generic HW/FW radio adapters
 *   10. Firmware Adapters — 78+ radio hardware devices
 *   11. Orbital Fabric — Schema translation for network events
 *   12. Identity Fabric — Porter House trust-gated access
 *
 * Design principles:
 * - All radio is software-defined via JDR PirateNet
 * - Mesh is native, not overlay
 * - Trade routes settle economically (Mesh Token)
 * - All traffic is post-quantum signed & encrypted
 * - Spectrum is shared via FHSS/DSSS/hopping
 * - Hardware abstracted via Smart Adapter + Firmware Adapters
 * - Paraconsistent logic (LPRES) for all attestations
 * - M5 coverage hyperbola enforcement on all network operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef NETWORK_FABRIC_H
#define NETWORK_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "mesh_net.h"
#include "jdr_piratenet.h"
#include "radio.h"
#include "bluetooth.h"
#include "wifi.h"
#include "event_transport.h"
#include "smart_adapter.h"
#include "smart_adapter_integration.h"
#include "firmware_adapters.h"
#include "orbital_fabric.h"
/* Forward declarations for optional fabrics */
struct identity_fabric;
struct financial_fabric;
struct compute_fabric;
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Constants ===== */

#define NF_MAX_INTERFACES        64
#define NF_MAX_MESH_NETWORKS     32
#define NF_MAX_JDR_TRANSCEIVERS  16
#define NF_MAX_RADIO_MODEMS      8
#define NF_MAX_BT_DEVICES        32
#define NF_MAX_WIFI_INTERFACES   16
#define NF_MAX_LORA_DEVICES      16
#define NF_MAX_ROUTES            256
#define NF_MAX_PEERS             512
#define NF_MAX_NAME_LEN          64

/* ===== Network Interface ===== */

typedef enum {
    NF_IFACE_UNUSED    = 0,
    NF_IFACE_JDR       = 1,   /* JDR PirateNet virtual SDR */
    NF_IFACE_MESH      = 2,   /* Mesh Net interface */
    NF_IFACE_RADIO     = 3,   /* Cellular/Satellite modem */
    NF_IFACE_BLUETOOTH = 4,   /* Bluetooth LE/Classic */
    NF_IFACE_WIFI      = 5,   /* 802.11 */
    NF_IFACE_LORA      = 6,   /* LoRaWAN */
    NF_IFACE_ETHERNET  = 7,   /* Wired Ethernet */
    NF_IFACE_VIRTUAL   = 8    /* Virtual/tunnel interface */
} nf_iface_type_t;

typedef struct nf_interface {
    uint32_t id;
    char name[NF_MAX_NAME_LEN];
    nf_iface_type_t type;
    
    /* Hardware backing */
    smart_device_t *sa_device;           /* Smart Adapter device */
    jdr_adapter_t *jdr_adapter;          /* JDR firmware adapter */
    jdr_transceiver_t *jdr_transceiver;  /* JDR transceiver */
    
    /* Radio modem */
    radio_modem_t *radio_modem;
    
    /* Bluetooth */
    bt_device_t *bt_device;
    
    /* WiFi */
    wifi_interface_t *wifi_iface;
    
    /* LoRa */
    lora_device_t *lora_device;
    
    /* Mesh network */
    uint32_t mesh_network_id;
    
    /* Capabilities */
    uint64_t frequency_min;      /* Hz */
    uint64_t frequency_max;      /* Hz */
    uint32_t max_bandwidth;      /* Hz */
    uint32_t supported_modulations;  /* Bitmask */
    uint32_t tx_power_max;       /* dBm */
    uint32_t rx_sensitivity;     /* dBm */
    
    /* State */
    bool up;
    bool promiscuous;
    bool monitor_mode;
    
    /* Statistics */
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_errors;
    uint64_t rx_errors;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} nf_interface_t;

/* ===== Mesh Network (extends Mesh Net) ===== */

typedef struct nf_mesh_network {
    mn_network_t base;               /* Base mesh network */
    
    /* Network Fabric extensions */
    uint32_t interface_ids[8];       /* Physical interfaces */
    uint32_t num_interfaces;
    
    /* JDR transceivers for this mesh */
    uint32_t jdr_transceiver_ids[4];
    uint32_t num_jdr_transceivers;
    
    /* Spectrum management */
    struct {
        uint64_t center_freq;
        uint32_t bandwidth;
        uint32_t hop_pattern_id;
        bool fhss_enabled;
    } spectrum;
    
    /* Porter House trust */
    porter_house_t *porter;
    uint32_t min_trust_weight;
    
    /* LPRES attestation */
    lpres_state_t mesh_attestation;
    
    bool active;
} nf_mesh_network_t;

/* ===== JDR Transceiver (extends JDR PirateNet) ===== */

typedef struct nf_jdr_transceiver {
    jdr_transceiver_t base;          /* Base JDR transceiver */
    
    /* Network Fabric extensions */
    uint32_t interface_id;           /* Physical interface */
    uint32_t mesh_network_id;        /* Associated mesh */
    
    /* Virtual SDR state */
    struct {
        uint64_t sample_rate;
        uint32_t fft_size;
        uint32_t num_subcarriers;
        bool ofdm_enabled;
        bool fhss_enabled;
        uint32_t hop_rate;
    } sdr_config;
    
    /* Smart Adapter integration */
    smart_device_t *sa_device;
    sai_integrated_device_t *sai_device;
    
    /* LPRES attestation */
    lpres_state_t transceiver_attestation;
    
    bool active;
} nf_jdr_transceiver_t;

/* ===== Route (extends Mesh Net route) ===== */

typedef struct nf_route {
    mn_route_t base;                 /* Base mesh route */
    
    /* Network Fabric extensions */
    uint32_t interface_id;           /* Egress interface */
    uint32_t next_hop_interface_id;  /* Next hop interface */
    
    /* Radio link budget */
    struct {
        int32_t tx_power_dbm;
        int32_t rx_sensitivity_dbm;
        int32_t path_loss_db;
        int32_t fade_margin_db;
        int32_t snr_db;
    } link_budget;
    
    /* Quality metrics */
    surplus_real_t latency_ms;
    surplus_real_t jitter_ms;
    surplus_real_t packet_loss_rate;
    surplus_real_t throughput_mbps;
    
    /* LPRES attestation */
    lpres_state_t route_attestation;
    
    bool active;
} nf_route_t;

/* ===== Peer ===== */

typedef struct nf_peer {
    mn_peer_t base;                  /* Base mesh peer */
    
    /* Network Fabric extensions */
    uint32_t interface_ids[4];       /* Interfaces to reach peer */
    uint32_t num_interfaces;
    
    /* Radio metrics per interface */
    struct {
        uint32_t interface_id;
        int32_t rssi_dbm;
        int32_t snr_db;
        uint64_t last_heard_tick;
    } radio_metrics[4];
    
    /* Identity verification */
    bool identity_verified;
    uint8_t peer_certificate[256];
    uint32_t cert_len;
    
    /* LPRES attestation */
    lpres_state_t peer_attestation;
    
    bool active;
} nf_peer_t;

/* ===== Network Fabric ===== */

typedef struct network_fabric {
    /* Core sub-modules */
    mesh_net_t mesh;                 /* Mesh Net */
    jdr_network_t jdr_network;       /* JDR PirateNet */
    jdr_adapter_registry_t jdr_registry; /* JDR Adapter Registry */
    radio_subsystem_t radio;         /* Radio Subsystem */
    bluetooth_stack_t bluetooth;     /* Bluetooth Stack */
    wifi_stack_t wifi;               /* WiFi Stack */
    event_transport_t transport;     /* Event Transport */
    smart_adapter_registry_t sa_registry; /* Smart Adapter Registry */
    sai_integration_fabric_t sai_fabric;  /* SA Integration */
    
    /* Integration references */
    orbital_fabric_t *orbital;       /* Orbital Fabric */
    struct identity_fabric *identity;     /* Identity Fabric */
    struct financial_fabric *financial;   /* Financial Fabric */
    struct compute_fabric *compute;       /* Compute Fabric */
    
    /* Fabric-level state */
    nf_interface_t interfaces[NF_MAX_INTERFACES];
    uint32_t num_interfaces;
    uint32_t next_interface_id;
    
    nf_mesh_network_t mesh_networks[NF_MAX_MESH_NETWORKS];
    uint32_t num_mesh_networks;
    
    nf_jdr_transceiver_t jdr_transceivers[NF_MAX_JDR_TRANSCEIVERS];
    uint32_t num_jdr_transceivers;
    
    nf_route_t routes[NF_MAX_ROUTES];
    uint32_t num_routes;
    
    nf_peer_t peers[NF_MAX_PEERS];
    uint32_t num_peers;
    
    /* Global statistics */
    struct {
        uint64_t total_interfaces_created;
        uint64_t total_mesh_networks;
        uint64_t total_jdr_transceivers;
        uint64_t total_bytes_tx;
        uint64_t total_bytes_rx;
        uint64_t total_packets_tx;
        uint64_t total_packets_rx;
        uint64_t total_routes_created;
        uint64_t total_peers_connected;
        uint64_t total_spectrum_hops;
        uint64_t total_mesh_revenue;
    } stats;
    
    /* Paraconsistent global state */
    lpres_state_t global_attestation;
    bool global_safety_gate;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;
    
    /* Configuration */
    struct {
        bool auto_fhss;
        bool auto_power_control;
        bool require_porter_house;
        surplus_real_t min_link_snr;
        uint32_t max_hop_count;
        bool enable_mesh_token_settlement;
    } config;
    
    bool initialized;
} network_fabric_t;

/* ===== API ===== */

/* Initialize the Network Fabric */
void nf_init(network_fabric_t *fabric,
             orbital_fabric_t *orbital,
             struct identity_fabric *identity,
             struct financial_fabric *financial,
             struct compute_fabric *compute);

/* Register built-in network modules */
void nf_register_builtins(network_fabric_t *fabric);

/* ===== Interface Management ===== */

int32_t nf_create_interface(network_fabric_t *fabric,
                            const char *name, nf_iface_type_t type,
                            smart_device_t *sa_device);

nf_interface_t *nf_get_interface(network_fabric_t *fabric, uint32_t interface_id);
nf_interface_t *nf_get_interface_by_name(network_fabric_t *fabric, const char *name);

int32_t nf_interface_up(network_fabric_t *fabric, uint32_t interface_id);
int32_t nf_interface_down(network_fabric_t *fabric, uint32_t interface_id);
int32_t nf_interface_set_freq(network_fabric_t *fabric, uint32_t interface_id,
                              uint64_t freq_hz, uint32_t bandwidth_hz);
int32_t nf_interface_set_modulation(network_fabric_t *fabric, uint32_t interface_id,
                                    uint32_t modulation_mask);
int32_t nf_interface_set_tx_power(network_fabric_t *fabric, uint32_t interface_id,
                                  int32_t power_dbm);

/* ===== Mesh Network ===== */

int32_t nf_create_mesh_network(network_fabric_t *fabric,
                               const char *name, mn_net_access_t access,
                               const word168_t *creator_id, uint32_t creator_trust,
                               const uint32_t *interface_ids, uint32_t num_interfaces);

nf_mesh_network_t *nf_get_mesh_network(network_fabric_t *fabric, uint32_t network_id);

/* ===== JDR Transceiver ===== */

int32_t nf_create_jdr_transceiver(network_fabric_t *fabric,
                                  uint32_t interface_id,
                                  uint32_t mesh_network_id,
                                  jdr_execution_mode_t mode);

nf_jdr_transceiver_t *nf_get_jdr_transceiver(network_fabric_t *fabric, uint32_t transceiver_id);

int32_t nf_jdr_transmit(network_fabric_t *fabric, uint32_t transceiver_id,
                        const uint8_t *data, uint32_t len,
                        uint64_t freq_hz, uint32_t modulation);

int32_t nf_jdr_receive(network_fabric_t *fabric, uint32_t transceiver_id,
                       uint8_t *buffer, uint32_t max_len,
                       uint32_t timeout_ms);

/* ===== Routing ===== */

int32_t nf_create_route(network_fabric_t *fabric,
                        uint32_t mesh_network_id,
                        mn_route_type_t type,
                        const word168_t *source,
                        const word168_t *destination,
                        uint64_t price_per_unit, uint64_t capacity,
                        uint64_t current_cycle);

int32_t nf_find_route(network_fabric_t *fabric,
                      const word168_t *source,
                      const word168_t *destination,
                      nf_route_t **out_route);

/* ===== Peers ===== */

int32_t nf_discover_peers(network_fabric_t *fabric, uint32_t interface_id);

int32_t nf_connect_peer(network_fabric_t *fabric,
                        uint32_t mesh_network_id,
                        const word168_t *peer_id, uint32_t trust_weight,
                        uint64_t bandwidth);

/* ===== Health & Attestation ===== */

int32_t nf_check_interface_health(network_fabric_t *fabric,
                                  uint32_t interface_id,
                                  void *health_out);

int32_t nf_check_mesh_health(network_fabric_t *fabric,
                             uint32_t mesh_network_id,
                             void *health_out);

int32_t nf_check_global_health(network_fabric_t *fabric);

bool nf_global_safety_gate(network_fabric_t *fabric);

lpres_state_t nf_attest(network_fabric_t *fabric, uint32_t interface_id,
                        uint32_t op_id, void *args, int32_t result);

/* Coverage enforcement */
void nf_update_coverage(network_fabric_t *fabric);
bool nf_enforce_coverage(network_fabric_t *fabric, surplus_real_t min_ratio);

/* Statistics */
void nf_get_stats(network_fabric_t *fabric, void *stats_out);

/* Paraconsistent state */
lpres_state_t nf_get_attestation(network_fabric_t *fabric, uint32_t interface_id);
void nf_set_attestation(network_fabric_t *fabric, uint32_t interface_id, lpres_state_t state);

/* Utility */
const char *nf_lpres_state_name(lpres_state_t state);
const char *nf_iface_type_name(nf_iface_type_t type);

#endif /* NETWORK_FABRIC_H */

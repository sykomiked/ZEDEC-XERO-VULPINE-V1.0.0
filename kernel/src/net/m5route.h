/* m5route.h — M5 Axiomatic Omni-Router
 * ZEDEC pqOS native routing protocol. All external communication
 * infrastructures (TCP/IP, cellular, satellite, radio, ham, telephony)
 * are adapter layers that conform to M5 — not the other way around.
 *
 * Core concept: Every addressable endpoint in the universe (phone number,
 * IP address, callsign, satellite ID, cell tower ID, frequency) maps to
 * an M5 Axiomatic Address (MAA). The router routes between MAAs using
 * the M5 phase/omega/collapse protocol, and adapters translate at the
 * boundary.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef M5ROUTE_H
#define M5ROUTE_H

#include <stdint.h>
#include <stdbool.h>
#include "net.h"

/* M5 Axiomatic Address — universal endpoint identifier */
#define MAA_MAX_LEN 64

typedef enum {
    M5_PROTO_NATIVE    = 0,  /* M5 axiomatic (native ZEDEC pqOS) */
    M5_PROTO_IPV4      = 1,  /* IPv4 via ENUM/DNS */
    M5_PROTO_IPV6      = 2,  /* IPv6 via ENUM/DNS */
    M5_PROTO_CELLULAR  = 3,  /* 3G/4G/5G/LTE */
    M5_PROTO_SATELLITE = 4,  /* GEO satellite */
    M5_PROTO_LEO       = 5,  /* LEO constellation (Starlink, Iridium) */
    M5_PROTO_AM_RADIO  = 6,  /* AM broadcast */
    M5_PROTO_FM_RADIO  = 7,  /* FM broadcast */
    M5_PROTO_HAM_VHF   = 8,  /* Ham radio VHF/UHF (AX.25/APRS) */
    M5_PROTO_HAM_HF    = 9,  /* Ham radio HF (PSK31, RTTY, SSTV) */
    M5_PROTO_DTMF      = 10, /* DTMF telephony tones */
    M5_PROTO_PSTN      = 11, /* Legacy landline telephone */
    M5_PROTO_LORA      = 12, /* LoRaWAN IoT */
    M5_PROTO_LASER     = 13, /* Free-space optical/laser */
    M5_PROTO_QUANTUM   = 14, /* Quantum entanglement (futuristic) */
    M5_PROTO_NEUTRINO  = 15, /* Neutrino communication (futuristic) */
    M5_PROTO_DRAGON    = 16, /* Dragon harmonic substrate */
    M5_PROTO_NFC       = 17, /* NFC near-field */
    M5_PROTO_BLUETOOTH = 18, /* Bluetooth */
    M5_PROTO_WIFI      = 19, /* WiFi direct */
    M5_PROTO_RADAR     = 20, /* Radar (maritime/aviation/military) */
    M5_PROTO_LIDAR     = 21, /* LIDAR (light detection and ranging) */
    M5_PROTO_ULF       = 22, /* Ultra-Low/ELF/VLF submarine comms */
    M5_PROTO_TETRA     = 23, /* TETRA/TETRA2 (terrestrial trunked radio) */
    M5_PROTO_DECT      = 24, /* DECT/DECT 6.0 (cordless telephony) */
    M5_PROTO_ZIGBEE    = 25, /* Zigbee (802.15.4 mesh) */
    M5_PROTO_ZWAVE     = 26, /* Z-Wave (home automation) */
    M5_PROTO_THREAD    = 27, /* Thread (6LoWPAN mesh) */
    M5_PROTO_WEIGHTLESS= 28, /* Weightless-N/P/W (IoT) */
    M5_PROTO_SIGFOX    = 29, /* Sigfox (UNB IoT) */
    M5_PROTO_NBIOT     = 30, /* NB-IoT (3GPP LPWAN) */
    M5_PROTO_LTEM      = 31, /* LTE-M (eMTC LPWAN) */
    M5_PROTO_ECGSM     = 32, /* EC-GSM-IoT (extended coverage GSM) */
    M5_PROTO_HALOW     = 33, /* Wi-Fi HaLow (802.11ah sub-1GHz) */
    M5_PROTO_WIMAX     = 34, /* WiMAX (802.16) */
    M5_PROTO_WIGIG     = 35, /* WiGig (802.11ad 60GHz) */
    M5_PROTO_DSRC      = 36, /* DSRC/C-V2X (vehicular) */
    M5_PROTO_INMARSAT  = 37, /* Inmarsat (L-band GEO) */
    M5_PROTO_THURAYA   = 38, /* Thuraya (L-band GEO) */
    M5_PROTO_IRIDIUM_SBD=39, /* Iridium SBD (LEO short burst data) */
    M5_PROTO_MICROWAVE = 40, /* Microwave relay (licensed links) */
    M5_PROTO_ACOUSTIC  = 41, /* Acoustic/ultrasonic (underwater/air) */
    M5_PROTO_VISIBLE_LIGHT = 42, /* Li-Fi / visible light communication */
    M5_PROTO_JDR_PIRATENET = 43, /* JDR PirateNet harmonic hum carrier */
    M5_PROTO_MAX       = 44
} m5_proto_t;

typedef struct m5_address {
    m5_proto_t proto;
    char addr[MAA_MAX_LEN];     /* Protocol-native address string */
    uint8_t raw[16];            /* Binary address (IP, MAC, etc.) */
    uint8_t raw_len;

    /* M5 axiomatic coordinates */
    uint32_t lattice_node;      /* Originating lattice node ID */
    uint32_t phase;             /* Current phase */
    uint32_t omega;             /* Phase omega */
    uint32_t integrity;         /* Integrity score (0-100) */
} m5_address_t;

typedef struct m5_route_entry {
    m5_address_t dest;
    m5_address_t gateway;
    m5_proto_t   link_proto;    /* Which physical link to use */
    uint32_t     metric;        /* Route cost */
    uint32_t     latency_ms;    /* Estimated latency */
    uint32_t     bandwidth_kbps;/* Estimated bandwidth */
    bool         active;
    uint32_t     last_seen;     /* Tick count of last contact */
} m5_route_entry_t;

typedef struct m5_adapter {
    m5_proto_t proto;
    char name[16];
    bool (*send)(const m5_address_t *dest, const void *data, uint32_t len,
                 const m5_net_header_t *m5_meta);
    uint32_t (*poll)(void *buf, uint32_t max_len, m5_address_t *src);
    bool (*connect)(const m5_address_t *dest);
    bool (*disconnect)(const m5_address_t *dest);
    bool active;
} m5_adapter_t;

typedef struct m5_router {
    m5_route_entry_t routes[256];
    uint32_t num_routes;
    m5_adapter_t adapters[M5_PROTO_MAX];
    m5_address_t local_addr;

    /* Phone number → M5 address mapping (ENUM-style) */
    struct {
        char phone[20];
        m5_address_t maa;
    } phone_map[128];
    uint32_t phone_map_count;

    /* IPv4 ↔ IPv6 bridge state */
    struct {
        uint8_t ipv4[4];
        uint8_t ipv6[16];
        bool active;
    } v4v6_bridge[64];
    uint32_t v4v6_bridge_count;

    /* Stats */
    uint32_t packets_routed;
    uint32_t packets_dropped;
    uint32_t adapter_sends;
    uint32_t adapter_recvs;
} m5_router_t;

void m5_router_init(m5_router_t *r, m5_address_t *local);
int32_t m5_router_register_adapter(m5_router_t *r, m5_proto_t proto, const char *name,
                                    bool (*send)(const m5_address_t *, const void *, uint32_t, const m5_net_header_t *),
                                    uint32_t (*poll)(void *, uint32_t, m5_address_t *),
                                    bool (*connect)(const m5_address_t *),
                                    bool (*disconnect)(const m5_address_t *));
int32_t m5_router_add_route(m5_router_t *r, const m5_address_t *dest,
                             const m5_address_t *gateway, m5_proto_t link,
                             uint32_t metric, uint32_t latency, uint32_t bandwidth);
int32_t m5_router_remove_route(m5_router_t *r, const m5_address_t *dest);
m5_route_entry_t *m5_router_lookup(m5_router_t *r, const m5_address_t *dest);
int32_t m5_router_send(m5_router_t *r, const m5_address_t *dest,
                        const void *data, uint32_t len, const m5_net_header_t *m5_meta);
int32_t m5_router_broadcast(m5_router_t *r, const void *data, uint32_t len,
                             const m5_net_header_t *m5_meta);
void m5_router_poll(m5_router_t *r, void (*on_message)(const m5_address_t *, const void *, uint32_t));

/* Phone number ↔ M5 address mapping */
int32_t m5_phone_register(m5_router_t *r, const char *phone, const m5_address_t *maa);
int32_t m5_phone_lookup(m5_router_t *r, const char *phone, m5_address_t *out);
int32_t m5_phone_to_maa(m5_router_t *r, const char *phone, m5_address_t *out);

/* IPv4 ↔ IPv6 bridge */
int32_t m5_v4v6_bridge_add(m5_router_t *r, const uint8_t ipv4[4], const uint8_t ipv6[16]);
int32_t m5_v4v6_bridge_lookup_v4(m5_router_t *r, const uint8_t ipv4[4], uint8_t ipv6_out[16]);
int32_t m5_v4v6_bridge_lookup_v6(m5_router_t *r, const uint8_t ipv6[16], uint8_t ipv4_out[4]);

/* Address conversion utilities */
void m5_addr_from_ipv4(m5_address_t *maa, const uint8_t ip[4], uint16_t port);
void m5_addr_from_ipv6(m5_address_t *maa, const uint8_t ip[16], uint16_t port);
void m5_addr_from_phone(m5_address_t *maa, const char *phone);
void m5_addr_from_callsign(m5_address_t *maa, const char *callsign, m5_proto_t band);
void m5_addr_from_satellite(m5_address_t *maa, uint32_t sat_id, uint32_t beam_id);
void m5_addr_from_cell(m5_address_t *maa, uint32_t mcc, uint32_t mnc, uint32_t cell_id);
void m5_addr_from_frequency(m5_address_t *maa, uint32_t freq_hz, m5_proto_t modulation);

/* Protocol name strings */
const char *m5_proto_name(m5_proto_t proto);

#endif

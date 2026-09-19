/* m5route.c — M5 Axiomatic Omni-Router Implementation
 * ZEDEC pqOS native routing. All external protocols are adapters.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "m5route.h"

static __attribute__((unused)) int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void str_copy(char *d, const char *s) { int i = 0; while (s[i]) { d[i] = s[i]; i++; } d[i] = 0; }
static void mem_copy(void *d, const void *s, uint32_t n) {
    uint8_t *dst = d; const uint8_t *src = s;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}
static void mem_set(void *d, int c, uint32_t n) {
    uint8_t *dst = d; for (uint32_t i = 0; i < n; i++) dst[i] = (uint8_t)c;
}

static const char *proto_names[M5_PROTO_MAX] = {
    "M5-Native", "IPv4", "IPv6", "Cellular", "Satellite-GEO",
    "Satellite-LEO", "AM-Radio", "FM-Radio", "Ham-VHF", "Ham-HF",
    "DTMF", "PSTN", "LoRa", "Laser", "Quantum", "Neutrino",
    "Dragon", "NFC", "Bluetooth", "WiFi",
    "Radar", "LIDAR", "ULF-Submarine",
    "TETRA", "DECT", "Zigbee", "Z-Wave", "Thread",
    "Weightless", "Sigfox", "NB-IoT", "LTE-M", "EC-GSM",
    "Wi-Fi-HaLow", "WiMAX", "WiGig", "DSRC-CV2X",
    "Inmarsat", "Thuraya", "Iridium-SBD",
    "Microwave", "Acoustic", "Visible-Light", "JDR-PirateNet"
};

const char *m5_proto_name(m5_proto_t proto) {
    if (proto < M5_PROTO_MAX) return proto_names[proto];
    return "Unknown";
}

void m5_router_init(m5_router_t *r, m5_address_t *local) {
    r->num_routes = 0;
    r->phone_map_count = 0;
    r->v4v6_bridge_count = 0;
    r->packets_routed = 0;
    r->packets_dropped = 0;
    r->adapter_sends = 0;
    r->adapter_recvs = 0;

    for (uint32_t i = 0; i < M5_PROTO_MAX; i++) {
        r->adapters[i].active = false;
        r->adapters[i].proto = (m5_proto_t)i;
        r->adapters[i].send = 0;
        r->adapters[i].poll = 0;
        r->adapters[i].connect = 0;
        r->adapters[i].disconnect = 0;
    }

    for (uint32_t i = 0; i < 256; i++)
        r->routes[i].active = false;

    if (local) {
        mem_copy(&r->local_addr, local, sizeof(m5_address_t));
    } else {
        mem_set(&r->local_addr, 0, sizeof(m5_address_t));
        r->local_addr.proto = M5_PROTO_NATIVE;
    }
}

int32_t m5_router_register_adapter(m5_router_t *r, m5_proto_t proto, const char *name,
                                    bool (*send)(const m5_address_t *, const void *, uint32_t, const m5_net_header_t *),
                                    uint32_t (*poll)(void *, uint32_t, m5_address_t *),
                                    bool (*connect)(const m5_address_t *),
                                    bool (*disconnect)(const m5_address_t *)) {
    if (proto >= M5_PROTO_MAX) return -1;
    m5_adapter_t *a = &r->adapters[proto];
    a->proto = proto;
    a->send = send;
    a->poll = poll;
    a->connect = connect;
    a->disconnect = disconnect;
    a->active = true;
    int j = 0;
    for (; name[j] && j < 15; j++) a->name[j] = name[j];
    a->name[j] = 0;
    return 0;
}

int32_t m5_router_add_route(m5_router_t *r, const m5_address_t *dest,
                             const m5_address_t *gateway, m5_proto_t link,
                             uint32_t metric, uint32_t latency, uint32_t bandwidth) {
    /* Check if route already exists — update if so */
    for (uint32_t i = 0; i < r->num_routes; i++) {
        if (r->routes[i].active && str_cmp(r->routes[i].dest.addr, dest->addr) == 0) {
            if (gateway) mem_copy(&r->routes[i].gateway, gateway, sizeof(m5_address_t));
            r->routes[i].link_proto = link;
            r->routes[i].metric = metric;
            r->routes[i].latency_ms = latency;
            r->routes[i].bandwidth_kbps = bandwidth;
            return (int32_t)i;
        }
    }

    if (r->num_routes >= 256) return -1;
    m5_route_entry_t *e = &r->routes[r->num_routes];
    mem_copy(&e->dest, dest, sizeof(m5_address_t));
    if (gateway) mem_copy(&e->gateway, gateway, sizeof(m5_address_t));
    e->link_proto = link;
    e->metric = metric;
    e->latency_ms = latency;
    e->bandwidth_kbps = bandwidth;
    e->active = true;
    e->last_seen = 0;
    return (int32_t)r->num_routes++;
}

int32_t m5_router_remove_route(m5_router_t *r, const m5_address_t *dest) {
    for (uint32_t i = 0; i < r->num_routes; i++) {
        if (r->routes[i].active && str_cmp(r->routes[i].dest.addr, dest->addr) == 0) {
            r->routes[i].active = false;
            return 0;
        }
    }
    return -1;
}

m5_route_entry_t *m5_router_lookup(m5_router_t *r, const m5_address_t *dest) {
    /* Exact match first */
    for (uint32_t i = 0; i < r->num_routes; i++) {
        if (r->routes[i].active && str_cmp(r->routes[i].dest.addr, dest->addr) == 0)
            return &r->routes[i];
    }
    /* Protocol match (same proto, any address) */
    for (uint32_t i = 0; i < r->num_routes; i++) {
        if (r->routes[i].active && r->routes[i].dest.proto == dest->proto)
            return &r->routes[i];
    }
    /* Default route */
    for (uint32_t i = 0; i < r->num_routes; i++) {
        if (r->routes[i].active && r->routes[i].dest.addr[0] == 0)
            return &r->routes[i];
    }
    return 0;
}

int32_t m5_router_send(m5_router_t *r, const m5_address_t *dest,
                        const void *data, uint32_t len, const m5_net_header_t *m5_meta) {
    m5_route_entry_t *route = m5_router_lookup(r, dest);
    if (!route) {
        r->packets_dropped++;
        return -1;
    }

    m5_proto_t link = route->link_proto;
    if (link >= M5_PROTO_MAX || !r->adapters[link].active || !r->adapters[link].send) {
        r->packets_dropped++;
        return -1;
    }

    bool ok = r->adapters[link].send(dest, data, len, m5_meta);
    if (ok) {
        r->packets_routed++;
        r->adapter_sends++;
        return (int32_t)len;
    }
    r->packets_dropped++;
    return -1;
}

int32_t m5_router_broadcast(m5_router_t *r, const void *data, uint32_t len,
                             const m5_net_header_t *m5_meta) {
    int32_t sent = 0;
    for (uint32_t i = 0; i < M5_PROTO_MAX; i++) {
        if (r->adapters[i].active && r->adapters[i].send) {
            m5_address_t broadcast;
            mem_set(&broadcast, 0, sizeof(broadcast));
            broadcast.proto = (m5_proto_t)i;
            if (r->adapters[i].send(&broadcast, data, len, m5_meta)) {
                sent++;
                r->adapter_sends++;
            }
        }
    }
    r->packets_routed += (uint32_t)sent;
    return sent;
}

void m5_router_poll(m5_router_t *r, void (*on_message)(const m5_address_t *, const void *, uint32_t)) {
    for (uint32_t i = 0; i < M5_PROTO_MAX; i++) {
        if (!r->adapters[i].active || !r->adapters[i].poll) continue;
        uint8_t buf[NET_RX_BUFFER_SIZE];
        m5_address_t src;
        mem_set(&src, 0, sizeof(src));
        src.proto = (m5_proto_t)i;
        uint32_t got = r->adapters[i].poll(buf, sizeof(buf), &src);
        if (got > 0 && on_message) {
            r->adapter_recvs++;
            on_message(&src, buf, got);
        }
    }
}

/* Phone number ↔ M5 address mapping */
int32_t m5_phone_register(m5_router_t *r, const char *phone, const m5_address_t *maa) {
    if (r->phone_map_count >= 128) return -1;
    str_copy(r->phone_map[r->phone_map_count].phone, phone);
    mem_copy(&r->phone_map[r->phone_map_count].maa, maa, sizeof(m5_address_t));
    r->phone_map_count++;
    return 0;
}

int32_t m5_phone_lookup(m5_router_t *r, const char *phone, m5_address_t *out) {
    for (uint32_t i = 0; i < r->phone_map_count; i++) {
        if (str_cmp(r->phone_map[i].phone, phone) == 0) {
            mem_copy(out, &r->phone_map[i].maa, sizeof(m5_address_t));
            return 0;
        }
    }
    return -1;
}

int32_t m5_phone_to_maa(m5_router_t *r, const char *phone, m5_address_t *out) {
    /* First check local mapping */
    if (m5_phone_lookup(r, phone, out) == 0) return 0;

    /* ENUM-style: convert phone to M5 address */
    /* Strip non-digits, prepend + */
    int j = 0;
    out->addr[j++] = '+';
    for (int i = 0; phone[i] && j < MAA_MAX_LEN - 1; i++) {
        if (phone[i] >= '0' && phone[i] <= '9')
            out->addr[j++] = phone[i];
    }
    out->addr[j] = 0;
    out->proto = M5_PROTO_PSTN;
    out->raw_len = 0;
    out->lattice_node = 0;
    out->phase = 0;
    out->omega = 0;
    out->integrity = 100;
    return 0;
}

/* IPv4 ↔ IPv6 bridge */
int32_t m5_v4v6_bridge_add(m5_router_t *r, const uint8_t ipv4[4], const uint8_t ipv6[16]) {
    if (r->v4v6_bridge_count >= 64) return -1;
    mem_copy(r->v4v6_bridge[r->v4v6_bridge_count].ipv4, ipv4, 4);
    mem_copy(r->v4v6_bridge[r->v4v6_bridge_count].ipv6, ipv6, 16);
    r->v4v6_bridge[r->v4v6_bridge_count].active = true;
    r->v4v6_bridge_count++;
    return 0;
}

int32_t m5_v4v6_bridge_lookup_v4(m5_router_t *r, const uint8_t ipv4[4], uint8_t ipv6_out[16]) {
    for (uint32_t i = 0; i < r->v4v6_bridge_count; i++) {
        if (!r->v4v6_bridge[i].active) continue;
        bool match = true;
        for (int j = 0; j < 4; j++) {
            if (r->v4v6_bridge[i].ipv4[j] != ipv4[j]) { match = false; break; }
        }
        if (match) {
            mem_copy(ipv6_out, r->v4v6_bridge[i].ipv6, 16);
            return 0;
        }
    }
    /* NAT64 prefix: ::ffff:a.b.c.d */
    mem_set(ipv6_out, 0, 16);
    ipv6_out[10] = 0xFF; ipv6_out[11] = 0xFF;
    mem_copy(ipv6_out + 12, ipv4, 4);
    return 0;
}

int32_t m5_v4v6_bridge_lookup_v6(m5_router_t *r, const uint8_t ipv6[16], uint8_t ipv4_out[4]) {
    /* Check for ::ffff:a.b.c.d mapped addresses */
    bool is_mapped = true;
    for (int i = 0; i < 10; i++) {
        if (ipv6[i] != 0) { is_mapped = false; break; }
    }
    if (is_mapped && ipv6[10] == 0xFF && ipv6[11] == 0xFF) {
        mem_copy(ipv4_out, ipv6 + 12, 4);
        return 0;
    }
    /* Check bridge table */
    for (uint32_t i = 0; i < r->v4v6_bridge_count; i++) {
        if (!r->v4v6_bridge[i].active) continue;
        bool match = true;
        for (int j = 0; j < 16; j++) {
            if (r->v4v6_bridge[i].ipv6[j] != ipv6[j]) { match = false; break; }
        }
        if (match) {
            mem_copy(ipv4_out, r->v4v6_bridge[i].ipv4, 4);
            return 0;
        }
    }
    return -1;
}

/* Address conversion utilities */
void m5_addr_from_ipv4(m5_address_t *maa, const uint8_t ip[4], uint16_t port) {
    mem_set(maa, 0, sizeof(*maa));
    maa->proto = M5_PROTO_IPV4;
    /* Format: "ipv4:a.b.c.d:port" */
    int j = 0;
    const char *pfx = "ipv4:";
    for (int i = 0; pfx[i]; i++) maa->addr[j++] = pfx[i];
    for (int i = 0; i < 4; i++) {
        if (i > 0) maa->addr[j++] = '.';
        uint8_t v = ip[i];
        if (v >= 100) { maa->addr[j++] = '0' + v / 100; v %= 100; }
        if (v >= 10) { maa->addr[j++] = '0' + v / 10; v %= 10; }
        maa->addr[j++] = '0' + v;
    }
    maa->addr[j++] = ':';
    if (port >= 10000) { maa->addr[j++] = '0' + port / 10000; port %= 10000; }
    if (port >= 1000) { maa->addr[j++] = '0' + port / 1000; port %= 1000; }
    if (port >= 100) { maa->addr[j++] = '0' + port / 100; port %= 100; }
    if (port >= 10) { maa->addr[j++] = '0' + port / 10; port %= 10; }
    maa->addr[j++] = '0' + port;
    maa->addr[j] = 0;
    mem_copy(maa->raw, ip, 4);
    maa->raw_len = 4;
    maa->integrity = 100;
}

void m5_addr_from_ipv6(m5_address_t *maa, const uint8_t ip[16], uint16_t port) {
    (void)port;
    mem_set(maa, 0, sizeof(*maa));
    maa->proto = M5_PROTO_IPV6;
    int j = 0;
    const char *pfx = "ipv6:";
    for (int i = 0; pfx[i]; i++) maa->addr[j++] = pfx[i];
    for (int i = 0; i < 16; i++) {
        uint8_t hi = ip[i] >> 4, lo = ip[i] & 0xF;
        maa->addr[j++] = (hi < 10) ? ('0' + hi) : ('a' + hi - 10);
        maa->addr[j++] = (lo < 10) ? ('0' + lo) : ('a' + lo - 10);
        if (i % 2 == 1 && i < 15) maa->addr[j++] = ':';
    }
    maa->addr[j] = 0;
    mem_copy(maa->raw, ip, 16);
    maa->raw_len = 16;
    maa->integrity = 100;
}

void m5_addr_from_phone(m5_address_t *maa, const char *phone) {
    mem_set(maa, 0, sizeof(*maa));
    maa->proto = M5_PROTO_PSTN;
    str_copy(maa->addr, phone);
    maa->integrity = 100;
}

void m5_addr_from_callsign(m5_address_t *maa, const char *callsign, m5_proto_t band) {
    mem_set(maa, 0, sizeof(*maa));
    maa->proto = band;
    int j = 0;
    const char *pfx = "ham:";
    for (int i = 0; pfx[i]; i++) maa->addr[j++] = pfx[i];
    str_copy(maa->addr + j, callsign);
    maa->integrity = 100;
}

void m5_addr_from_satellite(m5_address_t *maa, uint32_t sat_id, uint32_t beam_id) {
    mem_set(maa, 0, sizeof(*maa));
    maa->proto = M5_PROTO_SATELLITE;
    /* Format: "sat:id:beam" */
    int j = 0;
    const char *pfx = "sat:";
    for (int i = 0; pfx[i]; i++) maa->addr[j++] = pfx[i];
    /* Simple uint to string */
    uint32_t v = sat_id;
    char tmp[12]; int ti = 0;
    if (v == 0) tmp[ti++] = '0';
    while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
    while (ti > 0) maa->addr[j++] = tmp[--ti];
    maa->addr[j++] = ':';
    v = beam_id; ti = 0;
    if (v == 0) tmp[ti++] = '0';
    while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
    while (ti > 0) maa->addr[j++] = tmp[--ti];
    maa->addr[j] = 0;
    maa->lattice_node = sat_id;
    maa->integrity = 100;
}

void m5_addr_from_cell(m5_address_t *maa, uint32_t mcc, uint32_t mnc, uint32_t cell_id) {
    mem_set(maa, 0, sizeof(*maa));
    maa->proto = M5_PROTO_CELLULAR;
    int j = 0;
    const char *pfx = "cell:";
    for (int i = 0; pfx[i]; i++) maa->addr[j++] = pfx[i];
    uint32_t vals[3] = { mcc, mnc, cell_id };
    for (int k = 0; k < 3; k++) {
        uint32_t v = vals[k];
        char tmp[12]; int ti = 0;
        if (v == 0) tmp[ti++] = '0';
        while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
        while (ti > 0) maa->addr[j++] = tmp[--ti];
        if (k < 2) maa->addr[j++] = ':';
    }
    maa->addr[j] = 0;
    maa->integrity = 100;
}

void m5_addr_from_frequency(m5_address_t *maa, uint32_t freq_hz, m5_proto_t modulation) {
    mem_set(maa, 0, sizeof(*maa));
    maa->proto = modulation;
    int j = 0;
    const char *pfx = "freq:";
    for (int i = 0; pfx[i]; i++) maa->addr[j++] = pfx[i];
    uint32_t v = freq_hz;
    char tmp[12]; int ti = 0;
    if (v == 0) tmp[ti++] = '0';
    while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
    while (ti > 0) maa->addr[j++] = tmp[--ti];
    maa->addr[j] = 0;
    /* Store frequency in raw */
    maa->raw[0] = (freq_hz >> 24) & 0xFF;
    maa->raw[1] = (freq_hz >> 16) & 0xFF;
    maa->raw[2] = (freq_hz >> 8) & 0xFF;
    maa->raw[3] = freq_hz & 0xFF;
    maa->raw_len = 4;
    maa->integrity = 100;
}

/* ---- DECLARATION -----------------------------------------------------------

 * The M-five router. net/radio.o's only undefined symbol is
 * m5_router_register_adapter, defined here. m5route.o's own `nm -u` is empty.
 *
 * THE NAME IS NOT THE EVIDENCE: this file was found by looking up the symbol
 * radio.o actually needs across every object in the build, not by guessing
 * that a directory called net/ owns routing.
 */
#include "zxv_decl.h"
ZXV_DECLARE(m5route,
    ZXV_PROVIDES(m5_router_ready),
    ZXV_REQUIRES_NONE,
    ZXV_NO_BRINGUP);

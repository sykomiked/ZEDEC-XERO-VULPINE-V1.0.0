/* net.c — Network Stack Implementation
 * Ethernet/ARP/IP/ICMP/TCP/UDP with M5 axiomatic extension headers.
 * Cross-compatible with conventional TCP/IP while carrying M5 metadata.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "net.h"
#include "../iphase/iphase_core.h"
#include "../../include/m5_types.h"

static __attribute__((unused)) int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static __attribute__((unused)) int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void mem_copy(void *d, const void *s, uint32_t n) {
    uint8_t *dst = (uint8_t *)d; const uint8_t *src = (const uint8_t *)s;
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}
static void mem_set(void *d, int c, uint32_t n) {
    uint8_t *dst = (uint8_t *)d;
    for (uint32_t i = 0; i < n; i++) dst[i] = (uint8_t)c;
}

void net_init(net_state_t *net) {
    net->num_interfaces = 0;
    net->num_sockets = 0;
    net->arp_cache_count = 0;
    net->rx_packets = 0;
    net->tx_packets = 0;
    net->rx_bytes = 0;
    net->tx_bytes = 0;

    for (uint32_t i = 0; i < NET_MAX_INTERFACES; i++) {
        net->interfaces[i].up = false;
        net->interfaces[i].type = NET_IF_UNUSED;
    }
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        net->sockets[i].active = false;
        net->sockets[i].type = SOCK_UNUSED;
        net->sockets[i].tcp_state = TCP_CLOSED;
        net->sockets[i].rx_ready = false;
        net->sockets[i].rx_len = 0;
    }
}

int32_t net_register_interface(net_state_t *net, const char *name, net_if_type_t type,
                                const uint8_t *mac, const uint8_t *ip,
                                const uint8_t *gateway, const uint8_t *netmask,
                                void (*tx_cb)(const uint8_t *, uint32_t),
                                void (*poll_cb)(void)) {
    if (net->num_interfaces >= NET_MAX_INTERFACES) return -1;
    net_interface_t *iface = &net->interfaces[net->num_interfaces];
    iface->type = type;
    iface->up = true;

    for (int i = 0; i < NET_MAC_LEN; i++) iface->mac[i] = mac ? mac[i] : 0;
    for (int i = 0; i < NET_IP_LEN; i++) {
        iface->ip[i] = ip ? ip[i] : 0;
        iface->gateway[i] = gateway ? gateway[i] : 0;
        iface->netmask[i] = netmask ? netmask[i] : 0xFF;
    }

    int j = 0;
    for (; name[j] && j < 15; j++) iface->name[j] = name[j];
    iface->name[j] = 0;

    iface->rx_ready = false;
    iface->rx_len = 0;
    iface->tx_callback = tx_cb;
    iface->poll_callback = poll_cb;

    return (int32_t)net->num_interfaces++;
}

void net_poll(net_state_t *net) {
    for (uint32_t i = 0; i < net->num_interfaces; i++) {
        if (net->interfaces[i].up && net->interfaces[i].poll_callback)
            net->interfaces[i].poll_callback();
        if (net->interfaces[i].rx_ready) {
            net_handle_eth(net, &net->interfaces[i],
                           net->interfaces[i].rx_buf,
                           net->interfaces[i].rx_len);
            net->interfaces[i].rx_ready = false;
        }
    }
}

void net_rx_packet(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len) {
    if (len > NET_RX_BUFFER_SIZE) return;
    mem_copy(iface->rx_buf, data, len);
    iface->rx_len = len;
    iface->rx_ready = true;
    net->rx_packets++;
    net->rx_bytes += len;
}

uint16_t net_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t w = (uint16_t)data[i] << 8;
        if (i + 1 < len) w |= data[i + 1];
        sum += w;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

uint16_t net_ip_checksum(const ip_header_t *ip) {
    return net_checksum((const uint8_t *)ip, sizeof(ip_header_t));
}

void net_build_eth(eth_header_t *eth, const uint8_t *dst, const uint8_t *src, uint16_t type) {
    mem_copy(eth->dst_mac, dst, NET_MAC_LEN);
    mem_copy(eth->src_mac, src, NET_MAC_LEN);
    eth->eth_type = (type >> 8) | (type << 8); /* network byte order */
}

void net_build_ip(ip_header_t *ip, const uint8_t *src, const uint8_t *dst, uint8_t proto, uint16_t len) {
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = (len >> 8) | (len << 8);
    ip->id = 0;
    ip->flags_frag = 0x0040; /* Don't Fragment */
    ip->ttl = 64;
    ip->protocol = proto;
    ip->checksum = 0;
    mem_copy(ip->src_ip, src, NET_IP_LEN);
    mem_copy(ip->dst_ip, dst, NET_IP_LEN);
    ip->checksum = net_ip_checksum(ip);
}

void net_build_tcp(tcp_header_t *tcp, uint16_t src_port, uint16_t dst_port,
                    uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window) {
    tcp->src_port = (src_port >> 8) | (src_port << 8);
    tcp->dst_port = (dst_port >> 8) | (dst_port << 8);
    tcp->seq_num = seq;
    tcp->ack_num = ack;
    tcp->data_offset_flags = (uint16_t)((5 << 12) | flags);
    tcp->window = (window >> 8) | (window << 8);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;
}

void net_build_udp(udp_header_t *udp, uint16_t src_port, uint16_t dst_port, uint16_t len) {
    udp->src_port = (src_port >> 8) | (src_port << 8);
    udp->dst_port = (dst_port >> 8) | (dst_port << 8);
    udp->length = (len >> 8) | (len << 8);
    udp->checksum = 0;
}

/* ARP */
void net_arp_request(net_state_t *net, net_interface_t *iface, const uint8_t *ip) {
    if (!iface->tx_callback) return;
    uint8_t pkt[42];
    eth_header_t *eth = (eth_header_t *)pkt;
    arp_packet_t *arp = (arp_packet_t *)(pkt + sizeof(eth_header_t));

    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    net_build_eth(eth, broadcast, iface->mac, ETH_TYPE_ARP);

    arp->hw_type = 0x0100;
    arp->proto_type = 0x0008;
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->opcode = 0x0100; /* request */
    mem_copy(arp->sender_mac, iface->mac, NET_MAC_LEN);
    mem_copy(arp->sender_ip, iface->ip, NET_IP_LEN);
    mem_set(arp->target_mac, 0, NET_MAC_LEN);
    mem_copy(arp->target_ip, ip, NET_IP_LEN);

    iface->tx_callback(pkt, sizeof(pkt));
    net->tx_packets++;
    net->tx_bytes += sizeof(pkt);
}

bool net_arp_lookup(net_state_t *net, const uint8_t *ip, uint8_t *mac_out) {
    for (uint32_t i = 0; i < net->arp_cache_count; i++) {
        bool match = true;
        for (int j = 0; j < NET_IP_LEN; j++) {
            if (net->arp_cache_ip[i][j] != ip[j]) { match = false; break; }
        }
        if (match) {
            mem_copy(mac_out, net->arp_cache_mac[i], NET_MAC_LEN);
            return true;
        }
    }
    return false;
}

void net_arp_add(net_state_t *net, const uint8_t *ip, const uint8_t *mac) {
    if (net->arp_cache_count >= 16) return;
    mem_copy(net->arp_cache_ip[net->arp_cache_count], ip, NET_IP_LEN);
    mem_copy(net->arp_cache_mac[net->arp_cache_count], mac, NET_MAC_LEN);
    net->arp_cache_count++;
}

/* ICMP */
void net_icmp_echo(net_state_t *net, net_interface_t *iface, const uint8_t *dst_ip,
                    const void *data, uint32_t len) {
    if (!iface->tx_callback) return;
    uint32_t total = sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t) + len;
    if (total > NET_TX_BUFFER_SIZE) return;

    uint8_t pkt[NET_TX_BUFFER_SIZE];
    eth_header_t *eth = (eth_header_t *)pkt;
    ip_header_t *ip = (ip_header_t *)(pkt + sizeof(eth_header_t));
    icmp_header_t *icmp = (icmp_header_t *)((uint8_t *)ip + sizeof(ip_header_t));

    uint8_t dst_mac[6];
    if (!net_arp_lookup(net, dst_ip, dst_mac)) {
        net_arp_request(net, iface, dst_ip);
        return;
    }

    net_build_eth(eth, dst_mac, iface->mac, ETH_TYPE_IP);
    net_build_ip(ip, iface->ip, dst_ip, IP_PROTO_ICMP,
                 (uint16_t)(sizeof(ip_header_t) + sizeof(icmp_header_t) + len));
    icmp->type = 8; /* Echo Request */
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->rest = 0;
    if (data && len > 0)
        mem_copy(pkt + sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(icmp_header_t), data, len);
    icmp->checksum = net_checksum((uint8_t *)icmp, sizeof(icmp_header_t) + len);

    iface->tx_callback(pkt, total);
    net->tx_packets++;
    net->tx_bytes += total;
}

/* Protocol handlers */
void net_handle_eth(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len) {
    if (len < sizeof(eth_header_t)) return;
    eth_header_t *eth = (eth_header_t *)data;
    uint16_t eth_type = (eth->eth_type >> 8) | (eth->eth_type << 8);

    switch (eth_type) {
        case ETH_TYPE_ARP: net_handle_arp(net, iface, data + sizeof(eth_header_t), len - sizeof(eth_header_t)); break;
        case ETH_TYPE_IP:  net_handle_ip(net, iface, data + sizeof(eth_header_t), len - sizeof(eth_header_t)); break;
        case ETH_TYPE_M5:  net_handle_ip(net, iface, data + sizeof(eth_header_t), len - sizeof(eth_header_t)); break;
        default: break;
    }
}

void net_handle_arp(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len) {
    if (len < sizeof(arp_packet_t)) return;
    arp_packet_t *arp = (arp_packet_t *)data;
    uint16_t op = (arp->opcode >> 8) | (arp->opcode << 8);

    if (op == 2) { /* Reply */
        net_arp_add(net, arp->sender_ip, arp->sender_mac);
    } else if (op == 1) { /* Request */
        /* Check if target IP matches our interface */
        bool match = true;
        for (int i = 0; i < NET_IP_LEN; i++) {
            if (arp->target_ip[i] != iface->ip[i]) { match = false; break; }
        }
        if (match && iface->tx_callback) {
            uint8_t pkt[42];
            eth_header_t *resp_eth = (eth_header_t *)pkt;
            arp_packet_t *resp_arp = (arp_packet_t *)(pkt + sizeof(eth_header_t));

            net_build_eth(resp_eth, arp->sender_mac, iface->mac, ETH_TYPE_ARP);
            mem_copy((uint8_t*)&resp_arp->hw_type, (uint8_t*)&arp->hw_type, 6);
            resp_arp->hw_len = 6;
            resp_arp->proto_len = 4;
            resp_arp->opcode = 0x0200; /* reply */
            mem_copy(resp_arp->sender_mac, iface->mac, NET_MAC_LEN);
            mem_copy(resp_arp->sender_ip, iface->ip, NET_IP_LEN);
            mem_copy(resp_arp->target_mac, arp->sender_mac, NET_MAC_LEN);
            mem_copy(resp_arp->target_ip, arp->sender_ip, NET_IP_LEN);

            iface->tx_callback(pkt, 42);
            net->tx_packets++;
        }
    }
}

void net_handle_ip(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len) {
    if (len < sizeof(ip_header_t)) return;
    ip_header_t *ip = (ip_header_t *)data;
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
    if (ihl < 20 || len < ihl) return;

    const uint8_t *payload = data + ihl;
    uint32_t payload_len = len - ihl;

    switch (ip->protocol) {
        case IP_PROTO_ICMP: net_handle_icmp(net, iface, ip, payload, payload_len); break;
        case IP_PROTO_TCP:  net_handle_tcp(net, iface, ip, payload, payload_len); break;
        case IP_PROTO_UDP:  net_handle_udp(net, iface, ip, payload, payload_len); break;
        case IP_PROTO_M5:   net_handle_tcp(net, iface, ip, payload, payload_len); break; /* M5 uses TCP-like */
        default: break;
    }
}

void net_handle_icmp(net_state_t *net, net_interface_t *iface, const ip_header_t *ip,
                      const uint8_t *data, uint32_t len) {
    if (len < sizeof(icmp_header_t)) return;
    icmp_header_t *icmp = (icmp_header_t *)data;

    if (icmp->type == 8) { /* Echo Request -> Reply */
        if (iface->tx_callback) {
            uint32_t total = sizeof(eth_header_t) + sizeof(ip_header_t) + len;
            uint8_t pkt[NET_TX_BUFFER_SIZE];
            if (total > NET_TX_BUFFER_SIZE) return;

            eth_header_t *eth = (eth_header_t *)pkt;
            ip_header_t *resp_ip = (ip_header_t *)(pkt + sizeof(eth_header_t));
            icmp_header_t *resp_icmp = (icmp_header_t *)((uint8_t *)resp_ip + sizeof(ip_header_t));

            uint8_t dst_mac[6];
            if (!net_arp_lookup(net, ip->src_ip, dst_mac)) return;

            net_build_eth(eth, dst_mac, iface->mac, ETH_TYPE_IP);
            net_build_ip(resp_ip, iface->ip, ip->src_ip, IP_PROTO_ICMP, (uint16_t)(sizeof(ip_header_t) + len));
            mem_copy((uint8_t *)resp_icmp, data, len);
            resp_icmp->type = 0; /* Echo Reply */
            resp_icmp->checksum = 0;
            resp_icmp->checksum = net_checksum((uint8_t *)resp_icmp, len);

            iface->tx_callback(pkt, total);
            net->tx_packets++;
            net->tx_bytes += total;
        }
    }
}

void net_handle_tcp(net_state_t *net, net_interface_t *iface, const ip_header_t *ip,
                     const uint8_t *data, uint32_t len) {
    (void)iface;
    if (len < sizeof(tcp_header_t)) return;
    tcp_header_t *tcp = (tcp_header_t *)data;
    uint16_t dst_port = (tcp->dst_port >> 8) | (tcp->dst_port << 8);
    uint16_t src_port = (tcp->src_port >> 8) | (tcp->src_port << 8);
    uint16_t flags = tcp->data_offset_flags & 0x3F;

    /* Find matching socket */
    for (uint32_t i = 0; i < net->num_sockets; i++) {
        socket_t *s = &net->sockets[i];
        if (!s->active || s->type != SOCK_TCP) continue;
        if (s->local_port != dst_port) continue;

        if (flags & TCP_FLAG_SYN) {
            if (s->tcp_state == TCP_LISTEN) {
                s->tcp_state = TCP_SYN_RCVD;
                s->remote_port = src_port;
                mem_copy(s->remote_ip, ip->src_ip, NET_IP_LEN);
                s->seq_num = 1000;
                s->ack_num = tcp->seq_num + 1;
            }
        } else if (flags & TCP_FLAG_ACK) {
            if (s->tcp_state == TCP_SYN_RCVD) {
                s->tcp_state = TCP_ESTABLISHED;
            }
        } else if (flags & TCP_FLAG_FIN) {
            s->tcp_state = TCP_CLOSE_WAIT;
        }

        if (len > sizeof(tcp_header_t) && s->tcp_state == TCP_ESTABLISHED) {
            uint32_t data_len = len - sizeof(tcp_header_t);
            if (data_len > 0 && data_len <= NET_RX_BUFFER_SIZE) {
                mem_copy(s->rx_buf, data + sizeof(tcp_header_t), data_len);
                s->rx_len = data_len;
                s->rx_ready = true;
            }
        }
        break;
    }
}

void net_handle_udp(net_state_t *net, net_interface_t *iface, const ip_header_t *ip,
                     const uint8_t *data, uint32_t len) {
    (void)iface;
    if (len < sizeof(udp_header_t)) return;
    udp_header_t *udp = (udp_header_t *)data;
    uint16_t dst_port = (udp->dst_port >> 8) | (udp->dst_port << 8);
    uint16_t src_port = (udp->src_port >> 8) | (udp->src_port << 8);

    for (uint32_t i = 0; i < net->num_sockets; i++) {
        socket_t *s = &net->sockets[i];
        if (!s->active || s->type != SOCK_UDP) continue;
        if (s->local_port != dst_port) continue;

        uint32_t data_len = len - sizeof(udp_header_t);
        if (data_len > 0 && data_len <= NET_RX_BUFFER_SIZE) {
            mem_copy(s->rx_buf, data + sizeof(udp_header_t), data_len);
            s->rx_len = data_len;
            s->rx_ready = true;
            s->remote_port = src_port;
            mem_copy(s->remote_ip, ip->src_ip, NET_IP_LEN);
        }
        break;
    }
}

/* Socket API */
int32_t net_socket(net_state_t *net, socket_type_t type) {
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!net->sockets[i].active) {
            net->sockets[i].active = true;
            net->sockets[i].type = type;
            net->sockets[i].tcp_state = TCP_CLOSED;
            net->sockets[i].rx_ready = false;
            net->sockets[i].rx_len = 0;
            net->sockets[i].local_port = 0;
            net->sockets[i].remote_port = 0;
            net->sockets[i].seq_num = 0;
            net->sockets[i].ack_num = 0;
            net->sockets[i].omega = 0;
            net->sockets[i].phase = 0;
            if (i + 1 > net->num_sockets) net->num_sockets = i + 1;
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t net_connect(net_state_t *net, int32_t sock, const uint8_t *ip, uint16_t port) {
    if (sock < 0 || (uint32_t)sock >= NET_MAX_SOCKETS) return -1;
    socket_t *s = &net->sockets[sock];
    if (!s->active) return -1;

    /* M5 IPHASE: compute phase vector for routing decision */
    lattice_node_id_t src_node = { 0, 0, 0 };
    lattice_node_id_t dst_node = { 0, 0, 0 };
    for (uint32_t i = 0; i < NET_IP_LEN; i++) {
        dst_node.node_id = (dst_node.node_id << 8) | ip[i];
    }
    double complex phase = iphase_route(src_node, dst_node);
    s->omega = (uint32_t)(creal(phase) * 1000);
    s->phase = (uint32_t)(cimag(phase) * 1000);

    mem_copy(s->remote_ip, ip, NET_IP_LEN);
    s->remote_port = port;
    if (s->type == SOCK_TCP) {
        s->tcp_state = TCP_SYN_SENT;
        /* Would send SYN packet here */
    }
    return 0;
}

int32_t net_bind(net_state_t *net, int32_t sock, uint16_t port) {
    if (sock < 0 || (uint32_t)sock >= NET_MAX_SOCKETS) return -1;
    net->sockets[sock].local_port = port;
    return 0;
}

int32_t net_listen(net_state_t *net, int32_t sock, uint32_t backlog) {
    (void)backlog;
    if (sock < 0 || (uint32_t)sock >= NET_MAX_SOCKETS) return -1;
    if (net->sockets[sock].type != SOCK_TCP) return -1;
    net->sockets[sock].tcp_state = TCP_LISTEN;
    return 0;
}

int32_t net_accept(net_state_t *net, int32_t sock) {
    (void)net; (void)sock;
    return -1; /* Not implemented in this iteration */
}

int32_t net_send(net_state_t *net, int32_t sock, const void *data, uint32_t len) {
    if (sock < 0 || (uint32_t)sock >= NET_MAX_SOCKETS) return -1;
    socket_t *s = &net->sockets[sock];
    if (!s->active) return -1;

    /* Find interface */
    net_interface_t *iface = 0;
    for (uint32_t i = 0; i < net->num_interfaces; i++) {
        if (net->interfaces[i].up && net->interfaces[i].type == NET_IF_ETHERNET) {
            iface = &net->interfaces[i];
            break;
        }
    }
    if (!iface || !iface->tx_callback) return -1;

    uint8_t dst_mac[6];
    if (!net_arp_lookup(net, s->remote_ip, dst_mac)) {
        net_arp_request(net, iface, s->remote_ip);
        return -1;
    }

    uint32_t total;
    if (s->type == SOCK_TCP) {
        total = sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(tcp_header_t) + len;
        if (total > NET_TX_BUFFER_SIZE) return -1;
        uint8_t pkt[NET_TX_BUFFER_SIZE];
        eth_header_t *eth = (eth_header_t *)pkt;
        ip_header_t *ip = (ip_header_t *)(pkt + sizeof(eth_header_t));
        tcp_header_t *tcp = (tcp_header_t *)((uint8_t *)ip + sizeof(ip_header_t));

        net_build_eth(eth, dst_mac, iface->mac, ETH_TYPE_IP);
        net_build_ip(ip, iface->ip, s->remote_ip, IP_PROTO_TCP,
                     (uint16_t)(sizeof(ip_header_t) + sizeof(tcp_header_t) + len));
        net_build_tcp(tcp, s->local_port, s->remote_port, s->seq_num, s->ack_num,
                      TCP_FLAG_ACK | TCP_FLAG_PSH, 4096);
        if (data && len > 0)
            mem_copy(pkt + total - len, data, len);

        iface->tx_callback(pkt, total);
    } else {
        total = sizeof(eth_header_t) + sizeof(ip_header_t) + sizeof(udp_header_t) + len;
        if (total > NET_TX_BUFFER_SIZE) return -1;
        uint8_t pkt[NET_TX_BUFFER_SIZE];
        eth_header_t *eth = (eth_header_t *)pkt;
        ip_header_t *ip = (ip_header_t *)(pkt + sizeof(eth_header_t));
        udp_header_t *udp = (udp_header_t *)((uint8_t *)ip + sizeof(ip_header_t));

        net_build_eth(eth, dst_mac, iface->mac, ETH_TYPE_IP);
        net_build_ip(ip, iface->ip, s->remote_ip, IP_PROTO_UDP,
                     (uint16_t)(sizeof(ip_header_t) + sizeof(udp_header_t) + len));
        net_build_udp(udp, s->local_port, s->remote_port,
                      (uint16_t)(sizeof(udp_header_t) + len));
        if (data && len > 0)
            mem_copy(pkt + total - len, data, len);

        iface->tx_callback(pkt, total);
    }

    net->tx_packets++;
    net->tx_bytes += total;
    s->seq_num += len;
    return (int32_t)len;
}

int32_t net_recv(net_state_t *net, int32_t sock, void *data, uint32_t max_len) {
    if (sock < 0 || (uint32_t)sock >= NET_MAX_SOCKETS) return -1;
    socket_t *s = &net->sockets[sock];
    if (!s->active || !s->rx_ready) return -1;

    uint32_t copy_len = s->rx_len;
    if (copy_len > max_len) copy_len = max_len;
    mem_copy(data, s->rx_buf, copy_len);
    s->rx_ready = false;
    s->rx_len = 0;
    return (int32_t)copy_len;
}

int32_t net_close(net_state_t *net, int32_t sock) {
    if (sock < 0 || (uint32_t)sock >= NET_MAX_SOCKETS) return -1;
    net->sockets[sock].active = false;
    net->sockets[sock].tcp_state = TCP_CLOSED;
    return 0;
}

/* M5 axiomatic send/recv */
int32_t net_m5_send(net_state_t *net, int32_t sock, const void *data, uint32_t len,
                     const m5_net_header_t *m5) {
    if (!m5) return net_send(net, sock, data, len);

    /* Prepend M5 header to data */
    uint8_t buf[NET_TX_BUFFER_SIZE];
    if (len + sizeof(m5_net_header_t) > sizeof(buf)) return -1;
    mem_copy(buf, m5, sizeof(m5_net_header_t));
    mem_copy(buf + sizeof(m5_net_header_t), data, len);
    return net_send(net, sock, buf, len + sizeof(m5_net_header_t));
}

int32_t net_m5_recv(net_state_t *net, int32_t sock, void *data, uint32_t max_len,
                     m5_net_header_t *m5_out) {
    uint8_t buf[NET_RX_BUFFER_SIZE];
    int32_t got = net_recv(net, sock, buf, sizeof(buf));
    if (got < (int32_t)sizeof(m5_net_header_t)) return -1;

    mem_copy(m5_out, buf, sizeof(m5_net_header_t));
    if (m5_out->magic != 0x5841354D) return -1; /* "M5AX" */

    uint32_t data_len = (uint32_t)got - sizeof(m5_net_header_t);
    if (data_len > max_len) data_len = max_len;
    mem_copy(data, buf + sizeof(m5_net_header_t), data_len);
    return (int32_t)data_len;
}

/* DHCP */
void net_dhcp_discover(net_state_t *net, net_interface_t *iface) {
    (void)net; (void)iface;
    /* Would build and send DHCP DISCOVER packet via UDP port 67 */
}

void net_dhcp_handle(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len) {
    (void)net; (void)iface; (void)data; (void)len;
    /* Would parse DHCP OFFER/ACK and configure interface */
}

/* DNS */
void net_dns_resolve(net_state_t *net, net_interface_t *iface, const char *hostname,
                      uint8_t *ip_out) {
    (void)net; (void)iface; (void)hostname;
    if (ip_out) { ip_out[0] = 0; ip_out[1] = 0; ip_out[2] = 0; ip_out[3] = 0; }
}

void net_dns_handle(net_state_t *net, const uint8_t *data, uint32_t len) {
    (void)net; (void)data; (void)len;
}

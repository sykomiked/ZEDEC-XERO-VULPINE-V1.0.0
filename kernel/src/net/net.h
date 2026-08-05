/* net.h — Network Stack: Ethernet, ARP, IP, ICMP, TCP, UDP
 * Cross-compatible with conventional TCP/IP while carrying M5 axiomatic
 * metadata in optional extension headers.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>
#include "dhcp.h"

#define NET_MAX_INTERFACES  4
#define NET_MAX_SOCKETS     64
#define NET_MAX_CONNECTIONS 32
#define NET_RX_BUFFER_SIZE  2048
#define NET_TX_BUFFER_SIZE  2048
#define NET_MAC_LEN         6
#define NET_IP_LEN          4

#define ETH_TYPE_ARP   0x0806
#define ETH_TYPE_IP    0x0800
#define ETH_TYPE_M5    0x0805  /* M5 axiomatic extension */

#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17
#define IP_PROTO_M5    250  /* M5 axiomatic transport */

#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN = 1,
    TCP_SYN_SENT = 2,
    TCP_SYN_RCVD = 3,
    TCP_ESTABLISHED = 4,
    TCP_FIN_WAIT_1 = 5,
    TCP_FIN_WAIT_2 = 6,
    TCP_CLOSE_WAIT = 7,
    TCP_CLOSING = 8,
    TCP_LAST_ACK = 9,
    TCP_TIME_WAIT = 10
} tcp_state_t;

typedef enum {
    NET_IF_UNUSED = 0,
    NET_IF_ETHERNET = 1,
    NET_IF_LOOPBACK = 2
} net_if_type_t;

typedef enum {
    SOCK_UNUSED = 0,
    SOCK_TCP = 1,
    SOCK_UDP = 2,
    SOCK_M5 = 3
} socket_type_t;

typedef struct net_interface {
    net_if_type_t type;
    uint8_t mac[NET_MAC_LEN];
    uint8_t ip[NET_IP_LEN];
    uint8_t gateway[NET_IP_LEN];
    uint8_t netmask[NET_IP_LEN];
    bool up;
    char name[16];

    /* RX/TX ring buffers */
    uint8_t rx_buf[NET_RX_BUFFER_SIZE];
    uint32_t rx_len;
    bool rx_ready;

    /* Driver callbacks */
    void (*tx_callback)(const uint8_t *data, uint32_t len);
    void (*poll_callback)(void);
} net_interface_t;

typedef struct eth_header {
    uint8_t dst_mac[NET_MAC_LEN];
    uint8_t src_mac[NET_MAC_LEN];
    uint16_t eth_type;
} __attribute__((packed)) eth_header_t;

typedef struct arp_packet {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[NET_MAC_LEN];
    uint8_t  sender_ip[NET_IP_LEN];
    uint8_t  target_mac[NET_MAC_LEN];
    uint8_t  target_ip[NET_IP_LEN];
} __attribute__((packed)) arp_packet_t;

typedef struct ip_header {
    uint8_t  version_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src_ip[NET_IP_LEN];
    uint8_t  dst_ip[NET_IP_LEN];
} __attribute__((packed)) ip_header_t;

typedef struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t data_offset_flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

typedef struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

typedef struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t rest;
} __attribute__((packed)) icmp_header_t;

/* M5 axiomatic extension header (optional, after IP header) */
typedef struct m5_net_header {
    uint32_t magic;       /* 0x4D354158 = "M5AX" */
    uint32_t omega;       /* Phase omega */
    uint32_t phase;       /* Current phase */
    uint16_t collapse;    /* Collapse count */
    uint16_t integrity;   /* Integrity score */
    uint32_t origin_id;   /* Originating lattice node */
} __attribute__((packed)) m5_net_header_t;

typedef struct socket {
    socket_type_t type;
    tcp_state_t tcp_state;
    uint8_t local_ip[NET_IP_LEN];
    uint16_t local_port;
    uint8_t remote_ip[NET_IP_LEN];
    uint16_t remote_port;
    uint32_t seq_num;
    uint32_t ack_num;

    uint8_t rx_buf[NET_RX_BUFFER_SIZE];
    uint32_t rx_len;
    bool rx_ready;

    /* M5 metadata */
    uint32_t omega;
    uint32_t phase;

    bool active;
} socket_t;

typedef struct net_state {
    net_interface_t interfaces[NET_MAX_INTERFACES];
    uint32_t num_interfaces;
    socket_t sockets[NET_MAX_SOCKETS];
    uint32_t num_sockets;

    /* ARP cache */
    uint8_t arp_cache_ip[16][NET_IP_LEN];
    uint8_t arp_cache_mac[16][NET_MAC_LEN];
    uint32_t arp_cache_count;

    /* DHCP bootstrap state — one exchange at a time */
    dhcp_client_t dhcp;

    /* Stats */
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
} net_state_t;

void net_init(net_state_t *net);
int32_t net_register_interface(net_state_t *net, const char *name, net_if_type_t type,
                                const uint8_t *mac, const uint8_t *ip,
                                const uint8_t *gateway, const uint8_t *netmask,
                                void (*tx_cb)(const uint8_t *, uint32_t),
                                void (*poll_cb)(void));
void net_poll(net_state_t *net);
void net_rx_packet(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len);

int32_t net_socket(net_state_t *net, socket_type_t type);
int32_t net_connect(net_state_t *net, int32_t sock, const uint8_t *ip, uint16_t port);
int32_t net_bind(net_state_t *net, int32_t sock, uint16_t port);
int32_t net_listen(net_state_t *net, int32_t sock, uint32_t backlog);
int32_t net_accept(net_state_t *net, int32_t sock);
int32_t net_send(net_state_t *net, int32_t sock, const void *data, uint32_t len);
int32_t net_recv(net_state_t *net, int32_t sock, void *data, uint32_t max_len);
int32_t net_close(net_state_t *net, int32_t sock);

/* M5 axiomatic send/recv with metadata */
int32_t net_m5_send(net_state_t *net, int32_t sock, const void *data, uint32_t len,
                     const m5_net_header_t *m5);
int32_t net_m5_recv(net_state_t *net, int32_t sock, void *data, uint32_t max_len,
                     m5_net_header_t *m5_out);

/* Protocol handlers */
void net_handle_eth(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len);
void net_handle_arp(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len);
void net_handle_ip(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len);
void net_handle_icmp(net_state_t *net, net_interface_t *iface, const ip_header_t *ip,
                      const uint8_t *data, uint32_t len);
void net_handle_tcp(net_state_t *net, net_interface_t *iface, const ip_header_t *ip,
                     const uint8_t *data, uint32_t len);
void net_handle_udp(net_state_t *net, net_interface_t *iface, const ip_header_t *ip,
                     const uint8_t *data, uint32_t len);

/* Utility functions */
uint16_t net_checksum(const uint8_t *data, uint32_t len);
uint16_t net_ip_checksum(const ip_header_t *ip);

/* Transport checksum (RFC 768 / RFC 793): the one's-complement sum spans a
 * 12-byte pseudo-header — source, destination, a zero byte, the protocol and
 * the transport length — as well as the transport header and payload.
 * Returns a HOST-order value; use net_udp_finish/net_tcp_finish to store it. */
uint16_t net_l4_checksum(const uint8_t *src_ip, const uint8_t *dst_ip,
                         uint8_t proto, const uint8_t *l4, uint32_t l4_len);

/* Fill in the checksum field after the payload is in place. `l4_len` counts
 * the transport header plus payload. */
void net_udp_finish(udp_header_t *udp, const uint8_t *src_ip, const uint8_t *dst_ip,
                    uint32_t l4_len);
void net_tcp_finish(tcp_header_t *tcp, const uint8_t *src_ip, const uint8_t *dst_ip,
                    uint32_t l4_len);
void net_build_eth(eth_header_t *eth, const uint8_t *dst, const uint8_t *src, uint16_t type);
void net_build_ip(ip_header_t *ip, const uint8_t *src, const uint8_t *dst, uint8_t proto, uint16_t len);
void net_build_tcp(tcp_header_t *tcp, uint16_t src_port, uint16_t dst_port,
                    uint32_t seq, uint32_t ack, uint8_t flags, uint16_t window);
void net_build_udp(udp_header_t *udp, uint16_t src_port, uint16_t dst_port, uint16_t len);

/* ARP */
void net_arp_request(net_state_t *net, net_interface_t *iface, const uint8_t *ip);
bool net_arp_lookup(net_state_t *net, const uint8_t *ip, uint8_t *mac_out);
void net_arp_add(net_state_t *net, const uint8_t *ip, const uint8_t *mac);

/* ICMP */
void net_icmp_echo(net_state_t *net, net_interface_t *iface, const uint8_t *dst_ip,
                    const void *data, uint32_t len);

/* DHCP */
void net_dhcp_discover(net_state_t *net, net_interface_t *iface);
void net_dhcp_handle(net_state_t *net, net_interface_t *iface, const uint8_t *data, uint32_t len);

/* DNS */
void net_dns_resolve(net_state_t *net, net_interface_t *iface, const char *hostname,
                      uint8_t *ip_out);
void net_dns_handle(net_state_t *net, const uint8_t *data, uint32_t len);

#endif

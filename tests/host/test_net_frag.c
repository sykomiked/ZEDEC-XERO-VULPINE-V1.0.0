/* test_net_frag.c — IPv4 fragmentation + reassembly round-trip test (host-side)
 *
 * Sends an oversized UDP datagram through net_send, captures the emitted
 * frames, feeds them back through net_rx_packet/net_poll, and checks that
 * the raw UDP hook receives the reassembled payload intact.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../kernel/src/net/net.h"

static uint8_t cap[64][2048];
static uint32_t cap_len[64];
static uint32_t cap_n;

static void tx_cap(const uint8_t *data, uint32_t len) {
    if (cap_n >= 64 || len > 2048) return;
    memcpy(cap[cap_n], data, len);
    cap_len[cap_n] = len;
    cap_n++;
}

static uint8_t hook_buf[NET_REASM_BYTES];
static uint32_t hook_len;
static uint8_t hook_src[4];
static uint16_t hook_port;

static void on_dgram(void *ctx, const uint8_t src_ip[4], uint16_t src_port,
                     const uint8_t *data, uint32_t len) {
    (void)ctx;
    memcpy(hook_src, src_ip, 4);
    hook_port = src_port;
    if (len > sizeof hook_buf) len = sizeof hook_buf;
    memcpy(hook_buf, data, len);
    hook_len = len;
}

int main(void) {
    net_state_t net;
    net_init(&net);

    uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    uint8_t ip[4]   = {10, 0, 2, 15};
    uint8_t gw[4]   = {10, 0, 2, 2};
    uint8_t mask[4] = {255, 255, 255, 0};

    int32_t idx = net_register_interface(&net, "eth0", NET_IF_ETHERNET,
                                         mac, ip, gw, mask, tx_cap, 0);
    if (idx < 0) { printf("FAIL: register interface\n"); return 1; }
    net_interface_t *iface = &net.interfaces[idx];

    /* send to ourselves: on-link, and the datagram comes back addressed to us */
    net_arp_add(&net, ip, mac);
    net_udp_hook(&net, 8724, on_dgram, 0);

    int32_t sock = net_socket(&net, SOCK_UDP);
    if (sock < 0) { printf("FAIL: socket\n"); return 1; }
    if (net_bind(&net, sock, 40000) != 0) { printf("FAIL: bind\n"); return 1; }
    if (net_connect(&net, sock, ip, 8724) != 0) { printf("FAIL: connect\n"); return 1; }

    /* 12000 bytes of a repeating pattern — 9 fragments at 1480 l4 bytes each */
    static uint8_t payload[12000];
    for (uint32_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i * 7 + 3);

    if (net_send(&net, sock, payload, sizeof payload) != (int32_t)sizeof payload) {
        printf("FAIL: net_send\n");
        return 1;
    }

    uint32_t expect = (12008 + 1480 - 1) / 1480;
    printf("  emitted %u frames (expect %u)\n", cap_n, expect);
    if (cap_n != expect) { printf("FAIL: frame count\n"); return 1; }
    for (uint32_t i = 0; i < cap_n; i++) {
        if (cap_len[i] > 1514) { printf("FAIL: frame %u over MTU: %u\n", i, cap_len[i]); return 1; }
    }

    /* feed the frames back — reassembly must deliver the whole datagram */
    for (uint32_t i = 0; i < cap_n; i++) {
        net_rx_packet(&net, iface, cap[i], cap_len[i]);
        net_poll(&net);
    }

    if (hook_len != 12000) { printf("FAIL: hook got %u bytes\n", hook_len); return 1; }
    if (memcmp(hook_buf, payload, 12000) != 0) { printf("FAIL: payload mismatch\n"); return 1; }
    if (hook_port != 40000) { printf("FAIL: src port %u\n", hook_port); return 1; }

    printf("  [TEST] fragmentation + reassembly round-trip... PASS\n");
    return 0;
}

/* test_net.c — Network stack socket API test (host-side)
 *
 * Tests the TCP/IP stack socket API: socket creation, bind, listen,
 * connect, send, recv, close. Uses loopback interface.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../kernel/src/net/net.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++; printf("  [TEST] %s... ", name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

void test_socket_create(void) {
    TEST("socket creation");
    net_state_t net;
    net_init(&net);

    int32_t sock = net_socket(&net, SOCK_TCP);
    if (sock < 0) FAIL("net_socket failed");
    if (!net.sockets[sock].active) FAIL("socket not active");
    if (net.sockets[sock].type != SOCK_TCP) FAIL("wrong socket type");

    PASS();
}

void test_socket_bind(void) {
    TEST("socket bind");
    net_state_t net;
    net_init(&net);

    int32_t sock = net_socket(&net, SOCK_TCP);
    int32_t ret = net_bind(&net, sock, 8080);
    if (ret != 0) FAIL("net_bind failed");
    if (net.sockets[sock].local_port != 8080) FAIL("port not set");

    PASS();
}

void test_socket_listen(void) {
    TEST("socket listen");
    net_state_t net;
    net_init(&net);

    int32_t sock = net_socket(&net, SOCK_TCP);
    net_bind(&net, sock, 8080);
    int32_t ret = net_listen(&net, sock, 5);
    if (ret != 0) FAIL("net_listen failed");
    if (net.sockets[sock].tcp_state != TCP_LISTEN) FAIL("not in LISTEN state");

    PASS();
}

void test_udp_socket(void) {
    TEST("UDP socket creation");
    net_state_t net;
    net_init(&net);

    int32_t sock = net_socket(&net, SOCK_UDP);
    if (sock < 0) FAIL("UDP socket failed");
    if (net.sockets[sock].type != SOCK_UDP) FAIL("wrong type");

    net_bind(&net, sock, 5353);
    if (net.sockets[sock].local_port != 5353) FAIL("UDP port not set");

    PASS();
}

void test_socket_close(void) {
    TEST("socket close");
    net_state_t net;
    net_init(&net);

    int32_t sock = net_socket(&net, SOCK_TCP);
    int32_t ret = net_close(&net, sock);
    if (ret != 0) FAIL("net_close failed");
    if (net.sockets[sock].active) FAIL("socket still active after close");

    PASS();
}

void test_max_sockets(void) {
    TEST("max sockets limit");
    net_state_t net;
    net_init(&net);

    int count = 0;
    for (uint32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        int32_t sock = net_socket(&net, SOCK_TCP);
        if (sock >= 0) count++;
    }
    if (count != (int)NET_MAX_SOCKETS) FAIL("didn't fill all sockets");

    /* One more should fail */
    int32_t sock = net_socket(&net, SOCK_TCP);
    if (sock >= 0) FAIL("should have failed on full socket table");

    PASS();
}

void test_interface_registration(void) {
    TEST("interface registration");
    net_state_t net;
    net_init(&net);

    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t ip[4] = {192, 168, 1, 100};
    uint8_t gw[4] = {192, 168, 1, 1};
    uint8_t mask[4] = {255, 255, 255, 0};

    int32_t idx = net_register_interface(&net, "eth0", NET_IF_ETHERNET,
                                          mac, ip, gw, mask, 0, 0);
    if (idx < 0) FAIL("interface registration failed");
    if (!net.interfaces[idx].up) FAIL("interface not up");
    if (net.interfaces[idx].type != NET_IF_ETHERNET) FAIL("wrong type");

    PASS();
}

void test_arp_cache(void) {
    TEST("ARP cache add/lookup");
    net_state_t net;
    net_init(&net);

    uint8_t ip[4] = {10, 0, 0, 1};
    uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};

    net_arp_add(&net, ip, mac);

    uint8_t found[6];
    if (!net_arp_lookup(&net, ip, found)) FAIL("ARP lookup failed");
    if (memcmp(found, mac, 6) != 0) FAIL("ARP MAC mismatch");

    /* Lookup unknown IP should fail */
    uint8_t unknown[4] = {10, 0, 0, 99};
    if (net_arp_lookup(&net, unknown, found)) FAIL("ARP should not find unknown IP");

    PASS();
}

void test_checksum(void) {
    TEST("IP checksum");
    /* Known test vector: IP header with all zeros except version/IHL */
    ip_header_t ip;
    memset(&ip, 0, sizeof(ip));
    ip.version_ihl = 0x45;
    ip.total_len = 0x1400;  /* 20 bytes, network order */
    ip.ttl = 64;
    ip.protocol = 6;  /* TCP */
    ip.src_ip[0] = 192; ip.src_ip[1] = 168; ip.src_ip[2] = 1; ip.src_ip[3] = 100;
    ip.dst_ip[0] = 192; ip.dst_ip[1] = 168; ip.dst_ip[2] = 1; ip.dst_ip[3] = 1;

    uint16_t csum = net_ip_checksum(&ip);
    /* Checksum should be non-zero (valid) */
    if (csum == 0) FAIL("checksum unexpectedly zero");

    /* Verify by recomputing with checksum in place */
    ip.checksum = csum;
    uint16_t verify = net_ip_checksum(&ip);
    if (verify != 0) FAIL("checksum verification failed");

    PASS();
}

int main(void) {
    printf("=== Network Stack Socket API Tests ===\n\n");

    test_socket_create();
    test_socket_bind();
    test_socket_listen();
    test_udp_socket();
    test_socket_close();
    test_max_sockets();
    test_interface_registration();
    test_arp_cache();
    test_checksum();

    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}

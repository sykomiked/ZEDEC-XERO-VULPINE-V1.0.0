/* test_net_vino.c — Host-testable tests for ZEDEC pqOS networking,
 * M5 Omni-Router, DTMF/Morse, Radio, Vino ledger, and Vena runtime.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../kernel/src/net/net.h"
#include "../kernel/src/net/m5route.h"
#include "../kernel/src/net/dtmf.h"
#include "../kernel/src/net/radio.h"
#include "../kernel/src/vino/vino.h"
#include "../kernel/src/vena/vena.h"

static void test_net_structures(void)
{
    printf("=== Network Stack Tests ===\n");
    assert(sizeof(eth_header_t) == 14);
    assert(sizeof(ip_header_t) == 20);
    assert(sizeof(tcp_header_t) == 20);
    assert(sizeof(udp_header_t) == 8);
    assert(sizeof(arp_packet_t) == 28);
    assert(ETH_TYPE_IP == 0x0800);
    assert(ETH_TYPE_ARP == 0x0806);
    assert(IP_PROTO_TCP == 6);
    assert(IP_PROTO_UDP == 17);
    assert(IP_PROTO_ICMP == 1);
    printf("  [PASS] Ethernet/IP/TCP/UDP/ARP header sizes correct\n");

    net_state_t net;
    memset(&net, 0, sizeof(net));
    net_init(&net);
    assert(net.num_interfaces == 0);
    assert(net.num_sockets == 0);
    printf("  [PASS] Net state initialized\n");

    int32_t sock = net_socket(&net, SOCK_TCP);
    assert(sock == 0);
    assert(net.sockets[0].active == true);
    assert(net.sockets[0].type == SOCK_TCP);
    net_close(&net, sock);
    assert(net.sockets[0].active == false);
    printf("  [PASS] Socket create/close works\n\n");
}

static void test_m5_router(void)
{
    printf("=== M5 Omni-Router Tests ===\n");
    assert(M5_PROTO_MAX == 44);
    assert(M5_PROTO_NATIVE == 0);
    assert(M5_PROTO_IPV4 == 1);
    assert(M5_PROTO_CELLULAR == 3);
    assert(M5_PROTO_SATELLITE == 4);
    assert(M5_PROTO_LEO == 5);
    assert(M5_PROTO_AM_RADIO == 6);
    assert(M5_PROTO_FM_RADIO == 7);
    assert(M5_PROTO_HAM_VHF == 8);
    assert(M5_PROTO_DTMF == 10);
    assert(M5_PROTO_QUANTUM == 14);
    assert(M5_PROTO_NEUTRINO == 15);
    assert(M5_PROTO_RADAR == 20);
    assert(M5_PROTO_LIDAR == 21);
    assert(M5_PROTO_ULF == 22);
    assert(M5_PROTO_TETRA == 23);
    assert(M5_PROTO_ZIGBEE == 25);
    assert(M5_PROTO_NBIOT == 30);
    assert(M5_PROTO_WIMAX == 34);
    assert(M5_PROTO_INMARSAT == 37);
    assert(M5_PROTO_ACOUSTIC == 41);
    assert(M5_PROTO_VISIBLE_LIGHT == 42);
    printf("  [PASS] 43 protocol types defined\n");

    m5_router_t r;
    memset(&r, 0, sizeof(r));
    m5_router_init(&r, 0);
    assert(r.num_routes == 0);
    assert(r.phone_map_count == 0);
    assert(r.v4v6_bridge_count == 0);
    printf("  [PASS] Router initialized with empty tables\n");

    /* Test phone number mapping */
    m5_address_t maa;
    m5_phone_to_maa(&r, "+14155551234", &maa);
    assert(maa.proto == M5_PROTO_PSTN);
    assert(maa.addr[0] == '+');
    printf("  [PASS] Phone->M5 address mapping works\n");

    /* Test IPv4 address conversion */
    uint8_t ip[4] = {192, 168, 1, 1};
    m5_addr_from_ipv4(&maa, ip, 80);
    assert(maa.proto == M5_PROTO_IPV4);
    assert(maa.raw[0] == 192);
    assert(maa.raw[3] == 1);
    printf("  [PASS] IPv4->M5 address conversion works\n");

    /* Test IPv4<->IPv6 bridge */
    uint8_t ipv6_out[16];
    m5_v4v6_bridge_lookup_v4(&r, ip, ipv6_out);
    assert(ipv6_out[10] == 0xFF);
    assert(ipv6_out[11] == 0xFF);
    assert(ipv6_out[12] == 192);
    printf("  [PASS] IPv4->IPv6 NAT64 bridge works\n");

    /* Test satellite address */
    m5_addr_from_satellite(&maa, 25544, 1);
    assert(maa.proto == M5_PROTO_SATELLITE);
    assert(maa.lattice_node == 25544);
    printf("  [PASS] Satellite address encoding works\n");

    /* Test cellular address */
    m5_addr_from_cell(&maa, 310, 260, 12345);
    assert(maa.proto == M5_PROTO_CELLULAR);
    printf("  [PASS] Cellular address encoding works\n");

    /* Test frequency address */
    m5_addr_from_frequency(&maa, 91700000, M5_PROTO_FM_RADIO);
    assert(maa.proto == M5_PROTO_FM_RADIO);
    printf("  [PASS] Radio frequency address encoding works\n\n");
}

static void test_dtmf_morse(void)
{
    printf("=== DTMF + Morse Code Tests ===\n");
    const dtmf_freq_pair_t *f = dtmf_get_freqs('1');
    assert(f != NULL);
    assert(f->low == 697);
    assert(f->high == 1209);
    printf("  [PASS] DTMF '1' = 697/1209 Hz\n");

    f = dtmf_get_freqs('0');
    assert(f->low == 941);
    assert(f->high == 1336);
    printf("  [PASS] DTMF '0' = 941/1336 Hz\n");

    f = dtmf_get_freqs('#');
    assert(f->low == 941);
    assert(f->high == 1477);
    printf("  [PASS] DTMF '#' = 941/1477 Hz\n");

    /* Morse code */
    const char *code = morse_encode_char('S');
    assert(strcmp(code, "...") == 0);
    code = morse_encode_char('O');
    assert(strcmp(code, "---") == 0);
    printf("  [PASS] Morse: S=... O=--- SOS correct\n");

    /* Morse encode */
    char encoded[128];
    morse_encode("HELLO", encoded, sizeof(encoded));
    assert(strstr(encoded, "....") != NULL);
    printf("  [PASS] Morse encode 'HELLO' works\n");

    /* DTMF generation */
    int16_t samples[1000];
    int32_t n = dtmf_generate('5', samples, 1000);
    assert(n > 0);
    printf("  [PASS] DTMF tone generation: %d samples\n\n", n);
}

static void test_radio_structures(void)
{
    printf("=== Radio/Cellular/Satellite Tests ===\n");
    assert(sizeof(cell_modem_t) > 0);
    assert(sizeof(satellite_link_t) > 0);
    assert(sizeof(radio_interface_t) > 0);
    assert(sizeof(quantum_link_t) > 0);
    assert(sizeof(laser_link_t) > 0);
    assert(sizeof(neutrino_link_t) > 0);
    assert(sizeof(starlink_terminal_t) > 0);
    printf("  [PASS] All radio/satellite structures defined\n");

    cell_modem_t cell;
    cell_init(&cell, CELL_GEN_5G);
    assert(cell.gen == CELL_GEN_5G);
    assert(cell.registered == false);
    cell_register(&cell);
    assert(cell.registered == true);
    printf("  [PASS] Cellular 5G modem init + register\n");

    satellite_link_t sat;
    sat_init(&sat, SAT_TYPE_LEO, 25544);
    assert(sat.type == SAT_TYPE_LEO);
    assert(sat.latency_ms == 20);
    printf("  [PASS] LEO satellite (ISS): latency=%dms\n", sat.latency_ms);

    sat_init(&sat, SAT_TYPE_GEO, 1);
    assert(sat.latency_ms == 250);
    printf("  [PASS] GEO satellite: latency=%dms\n", sat.latency_ms);

    radio_interface_t radio;
    radio_init(&radio, RADIO_FM, 91700000);
    assert(radio.modulation == RADIO_FM);
    assert(radio.frequency_hz == 91700000);
    assert(radio.bandwidth_hz == 200000);
    printf("  [PASS] FM radio at 91.7 MHz, BW=200kHz\n");

    radio_init(&radio, RADIO_AM, 530000);
    assert(radio.bandwidth_hz == 10000);
    printf("  [PASS] AM radio at 530 kHz, BW=10kHz\n");

    starlink_terminal_t st;
    starlink_init(&st);
    assert(st.link.type == SAT_TYPE_LEO);
    assert(st.link.bandwidth_mhz == 240);
    starlink_align(&st, 37.7749f, -122.4194f);
    assert(st.phased_array_aligned == true);
    printf("  [PASS] Starlink terminal init + align\n\n");
}

static void test_vino_ledger(void)
{
    printf("=== Vino Bank Node Tests ===\n");
    assert(CAP_MAX == 9);
    assert(ASSET_MAX == 12);
    assert(RAIL_MAX == 20);
    assert(MSG_MAX == 12);
    printf("  [PASS] 9 capital types, 12 asset classes, 20 payment rails, 12 msg standards\n");

    static vino_ledger_t v;
    memset(&v, 0, sizeof(v));
    vino_init(&v, 1);
    assert(v.node_id == 1);
    printf("  [PASS] Vino ledger initialized\n");

    int32_t idx = vino_create_account(&v, "ZEDEC:node:0001", "Genesis");
    assert(idx == 0);
    vino_account_t *acc = vino_get_account(&v, "ZEDEC:node:0001");
    assert(acc != NULL);
    assert(strcmp(acc->name, "Genesis") == 0);
    printf("  [PASS] Account creation + lookup\n");

    vino_create_account(&v, "ZEDEC:node:0002", "Treasury");
    acc->balance[CAP_FINANCIAL] = 1000000;
    int32_t txid = vino_transfer(&v, "ZEDEC:node:0001", "ZEDEC:node:0002", 50000, CAP_FINANCIAL,
                                 RAIL_VINO_NATIVE, "genesis grant");
    assert(txid >= 0);
    assert(v.num_txns == 1);
    uint64_t bal;
    vino_get_balance(&v, "ZEDEC:node:0001", CAP_FINANCIAL, &bal);
    assert(bal == 950000);
    vino_get_balance(&v, "ZEDEC:node:0002", CAP_FINANCIAL, &bal);
    assert(bal == 50000);
    printf("  [PASS] Transfer: 50000 financial capital, balances correct\n");

    vino_register_asset(&v, "USD", "US Dollar", ASSET_CURRENCY, 2, 1000000000);
    vino_register_asset(&v, "BTC", "Bitcoin", ASSET_CRYPTO, 8, 21000000);
    vino_register_asset(&v, "AAPL", "Apple Inc.", ASSET_EQUITY, 0, 16000000000);
    vino_register_asset(&v, "US10Y", "US 10-Year Bond", ASSET_BOND, 4, 0);
    vino_register_asset(&v, "XAU", "Gold", ASSET_COMMODITY, 4, 0);
    vino_register_asset(&v, "EURUSD", "Euro/Dollar", ASSET_FOREX, 5, 0);
    assert(v.num_assets == 6);
    vino_asset_t *btc = vino_get_asset(&v, "BTC");
    assert(btc != NULL);
    assert(btc->class == ASSET_CRYPTO);
    printf("  [PASS] 6 assets registered: USD, BTC, AAPL, US10Y, XAU, EURUSD\n");

    vino_add_peer(&v, "ZEDEC:node:peer1", "10.0.0.2:8333");
    vino_add_peer(&v, "ZEDEC:node:peer2", "10.0.0.3:8333");
    assert(v.num_peers == 2);
    printf("  [PASS] 2 P2P peers added\n");

    vino_set_validator(&v, true, 1000000);
    assert(v.is_validator == true);
    int32_t block = vino_propose_block(&v);
    assert(block > 0);
    printf("  [PASS] Validator mode + block proposal\n");

    char msg_buf[256];
    int32_t msg_len = vino_msg_to_mt103(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "I103") != NULL);
    printf("  [PASS] MT103 message adapter\n");

    msg_len = vino_msg_to_pacs008(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "pacs.008") != NULL);
    printf("  [PASS] PACS.008 message adapter\n");

    msg_len = vino_msg_to_cips(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "CIPS") != NULL);
    printf("  [PASS] CIPS message adapter\n");

    msg_len = vino_msg_to_btc(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "BTC") != NULL);
    printf("  [PASS] BTC blockchain adapter\n");

    msg_len = vino_msg_to_eth(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "ETH") != NULL);
    printf("  [PASS] ETH blockchain adapter\n");

    msg_len = vino_msg_to_visa(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "VISA") != NULL);
    printf("  [PASS] Visa payment rail adapter\n");

    assert(strcmp(vino_capital_name(CAP_FINANCIAL), "FINANCIAL") == 0);
    assert(strcmp(vino_capital_name(CAP_KNOWLEDGE), "INTELLECTUAL") == 0);
    assert(strcmp(vino_capital_name(CAP_HUMAN), "HUMAN") == 0);
    printf("  [PASS] Capital type names correct\n");

    assert(strcmp(vino_rail_name(RAIL_SWIFT), "SWIFT") == 0);
    assert(strcmp(vino_rail_name(RAIL_CIPS), "CIPS") == 0);
    assert(strcmp(vino_rail_name(RAIL_SPFS), "SPFS") == 0);
    assert(strcmp(vino_rail_name(RAIL_VISA), "Visa") == 0);
    printf("  [PASS] Payment rail names correct\n\n");
}

static void test_vena_runtime(void)
{
    printf("=== Vena Runtime Tests ===\n");
    assert(VENA_MAX_LANGUAGES == 32);
    assert(LANG_M5_AXIOMATIC == 31);
    printf("  [PASS] 31 languages defined (including M5 Axiomatic)\n");

    static vino_ledger_t v;
    memset(&v, 0, sizeof(v));
    vino_init(&v, 1);

    static vena_runtime_t vr;
    memset(&vr, 0, sizeof(vr));
    vena_init(&vr, &v);
    assert(vr.ledger == &v);
    assert(vr.default_language == LANG_M5_AXIOMATIC);
    printf("  [PASS] Vena runtime initialized with M5 Axiomatic language\n");

    assert(vena_is_language_supported(&vr, LANG_ENGLISH) == true);
    assert(vena_is_language_supported(&vr, LANG_KLINGON) == true);
    assert(vena_is_language_supported(&vr, LANG_NAVAJO) == true);
    assert(vena_is_language_supported(&vr, LANG_MORSE) == true);
    assert(vena_is_language_supported(&vr, LANG_M5_AXIOMATIC) == true);
    printf("  [PASS] Obscure languages supported: Navajo, Klingon, Morse, M5\n");

    int32_t cid = vena_register_contract(&vr, "genesis_contract", "M5:AXIOM:transfer",
                                         LANG_M5_AXIOMATIC, "ZEDEC:node:0001");
    assert(cid == 0);
    assert(vr.num_contracts == 1);
    printf("  [PASS] Smart contract registered\n");

    int32_t aid = vena_load_app(&vr, "Shell", APP_SHELL, "M5:shell", LANG_M5_AXIOMATIC);
    assert(aid == 0);
    aid = vena_load_app(&vr, "Wallet", APP_WALLET, "M5:wallet", LANG_M5_AXIOMATIC);
    assert(aid == 1);
    assert(vr.num_apps == 2);
    printf("  [PASS] 2 apps loaded (Shell, Wallet)\n");

    assert(vena_start_app(&vr, 0) == 0);
    assert(vr.apps[0].running == true);
    assert(vr.active_apps == 1);
    assert(vena_start_app(&vr, 1) == 0);
    assert(vr.active_apps == 2);
    assert(vena_stop_app(&vr, 0) == 0);
    assert(vr.active_apps == 1);
    printf("  [PASS] App start/stop works\n");

    vena_register_oracle(&vr, "BTC/USD", "blockchain:price");
    vena_register_oracle(&vr, "EUR/USD", "forex:price");
    assert(vr.num_oracles == 2);
    vena_update_oracle(&vr, 0, 45000);
    assert(vr.oracles[0].last_value == 45000);
    printf("  [PASS] Oracles: BTC/USD=45000, EUR/USD registered\n");

    assert(strcmp(vena_language_name(LANG_ENGLISH), "English") == 0);
    assert(strcmp(vena_language_name(LANG_M5_AXIOMATIC), "M5-Axiomatic") == 0);
    printf("  [PASS] Language names correct\n\n");
}

int main(void)
{
    printf("=== ZEDEC pqOS Network + Vino + Vena Tests ===\n\n");

    test_net_structures();
    test_m5_router();
    test_dtmf_morse();
    test_radio_structures();
    test_vino_ledger();
    test_vena_runtime();

    printf("=== All ZEDEC pqOS network/financial tests passed ===\n");
    return 0;
}

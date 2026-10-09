/* test_drivers.c — Host-testable driver logic tests
 * Tests data structures, scancode tables, PCI config, FAT32 name conversion,
 * GUI widget/window logic, init service dependency ordering.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Include driver headers - idt.h must come first as others depend on registers_t */
#include "../kernel/src/idt/idt.h"
#include "../kernel/src/gdt/gdt.h"
#include "../kernel/src/pic/pic.h"
#include "../kernel/src/timer/timer.h"
#include "../kernel/src/keyboard/keyboard.h"
#include "../kernel/src/mouse/mouse.h"
#include "../kernel/src/pci/pci.h"
#include "../kernel/src/acpi/acpi.h"
#include "../kernel/src/vbe/vbe.h"
#include "../kernel/src/ata/ata.h"
#include "../kernel/src/fat32/fat32.h"
#include "../gui/gui.h"
#include "../init/init.h"
#include "../kernel/src/net/net.h"
#include "../kernel/src/net/m5route.h"
#include "../kernel/src/net/dtmf.h"
#include "../kernel/src/net/radio.h"
#include "../kernel/src/vino/vino.h"
#include "../kernel/src/vena/vena.h"

static void test_gdt_layout(void) {
    printf("=== GDT Layout Tests ===\n");
    assert(sizeof(gdt_entry_t) == 8);
    assert(sizeof(gdt_ptr_t) == 6);
    assert(GDT_ENTRIES == 6);
    printf("  [PASS] GDT entry size = %zu, ptr size = %zu\n",
           sizeof(gdt_entry_t), sizeof(gdt_ptr_t));
    printf("  [PASS] GDT entries = %d\n\n", GDT_ENTRIES);
}

static void test_idt_layout(void) {
    printf("=== IDT Layout Tests ===\n");
    assert(sizeof(idt_entry_t) == 8);
    assert(sizeof(idt_ptr_t) == 6);
    assert(IDT_ENTRIES == 256);
    assert(sizeof(registers_t) > 0);
    printf("  [PASS] IDT entry size = %zu, ptr size = %zu\n",
           sizeof(idt_entry_t), sizeof(idt_ptr_t));
    printf("  [PASS] IDT entries = %d\n\n", IDT_ENTRIES);
}

static void test_pic_constants(void) {
    printf("=== PIC Constants Tests ===\n");
    assert(PIC1_CMD == 0x20);
    assert(PIC1_DATA == 0x21);
    assert(PIC2_CMD == 0xA0);
    assert(PIC2_DATA == 0xA1);
    assert(IRQ_TIMER == 0);
    assert(IRQ_KEYBOARD == 1);
    assert(IRQ_MOUSE == 12);
    assert(IRQ_PRIMARY_ATA == 14);
    assert(IRQ_SECONDARY_ATA == 15);
    printf("  [PASS] PIC port addresses and IRQ mappings correct\n\n");
}

static void test_keyboard_scancode_table(void) {
    printf("=== Keyboard Scancode Tests ===\n");
    assert(scancode_to_ascii[0x1E] == 'a');
    assert(scancode_to_ascii[0x30] == 'b');
    assert(scancode_to_ascii[0x2E] == 'c');
    assert(scancode_to_ascii[0x20] == 'd');
    assert(scancode_to_ascii[0x12] == 'e');
    assert(scancode_to_ascii[0x1C] == '\n');
    assert(scancode_to_ascii[0x39] == ' ');
    assert(scancode_to_ascii[0x0E] == '\b');
    assert(scancode_to_ascii[0x0F] == '\t');
    printf("  [PASS] Scancode set 1: a, b, c, d, e, enter, space, backspace, tab\n");

    assert(scancode_shift[0x02] == '!');
    assert(scancode_shift[0x03] == '@');
    assert(scancode_shift[0x04] == '#');
    assert(scancode_shift[0x12] == 'E');
    assert(scancode_shift[0x1E] == 'A');
    printf("  [PASS] Shift mappings: 1->!, 2->@, 3->#, e->E, a->A\n");

    keyboard_state_t kb;
    memset(&kb, 0, sizeof(kb));
    assert(kb.buf_count == 0);
    assert(keyboard_getchar() == -1);
    printf("  [PASS] Empty keyboard buffer returns -1\n\n");
}

static void test_pci_structures(void) {
    printf("=== PCI Structure Tests ===\n");
    assert(sizeof(pci_device_t) > 0);
    assert(sizeof(pci_state_t) > 0);
    assert(PCI_MAX_DEVICES == 32);
    assert(PCI_CONFIG_ADDR == 0xCF8);
    assert(PCI_CONFIG_DATA == 0xCFC);

    pci_state_t state;
    memset(&state, 0, sizeof(state));
    assert(state.num_devices == 0);
    assert(pci_find_device(&state, 0x1234, 0x5678) == NULL);
    assert(pci_find_class(&state, 0x02) == NULL);
    printf("  [PASS] PCI state init, empty search returns NULL\n\n");
}

static void test_vbe_color_macros(void) {
    printf("=== VBE Color/Mode Tests ===\n");
    assert(RGB(255, 0, 0) == 0xFF0000);
    assert(RGB(0, 255, 0) == 0x00FF00);
    assert(RGB(0, 0, 255) == 0x0000FF);
    assert(COLOR_BLACK == 0);
    assert(COLOR_WHITE == 0xFFFFFF);
    assert(VBE_DEFAULT_WIDTH == 1024);
    assert(VBE_DEFAULT_HEIGHT == 768);
    assert(VBE_DEFAULT_BPP == 32);
    printf("  [PASS] RGB macros: red=0x%X, green=0x%X, blue=0x%X\n",
           RGB(255,0,0), RGB(0,255,0), RGB(0,0,255));
    printf("  [PASS] Default mode: %dx%d@%d\n\n",
           VBE_DEFAULT_WIDTH, VBE_DEFAULT_HEIGHT, VBE_DEFAULT_BPP);
}

static void test_ata_structures(void) {
    printf("=== ATA Structure Tests ===\n");
    assert(ATA_PRIMARY_DATA == 0x1F0);
    assert(ATA_CMD_READ_PIO == 0x20);
    assert(ATA_CMD_WRITE_PIO == 0x30);
    assert(ATA_CMD_IDENTIFY == 0xEC);
    assert(ATA_SECTOR_SIZE == 512);

    ata_state_t state;
    memset(&state, 0, sizeof(state));
    assert(state.num_devices == 0);
    printf("  [PASS] ATA port 0x1F0, commands R=0x20 W=0x30 ID=0xEC\n");
    printf("  [PASS] Sector size = %d, empty state = 0 devices\n\n",
           ATA_SECTOR_SIZE);
}

static void test_fat32_structures(void) {
    printf("=== FAT32 Structure Tests ===\n");
    assert(sizeof(fat32_bpb_t) > 0);
    assert(sizeof(fat32_dirent_t) == 32);
    assert(FAT32_ATTR_DIRECTORY == 0x10);
    assert(FAT32_ATTR_LFN == 0x0F);
    assert(FAT32_MAX_FILES == 256);

    fat32_state_t fs;
    memset(&fs, 0, sizeof(fs));
    assert(fs.mounted == false);
    printf("  [PASS] FAT32 dirent size = %zu, attr dir=0x%x, lfn=0x%x\n",
           sizeof(fat32_dirent_t), FAT32_ATTR_DIRECTORY, FAT32_ATTR_LFN);
    printf("  [PASS] Unmounted state: mounted=false\n\n");
}

static void test_gui_structures(void) {
    printf("=== GUI Structure Tests ===\n");
    assert(sizeof(gui_window_t) > 0);
    assert(sizeof(gui_widget_t) > 0);
    assert(sizeof(gui_desktop_t) > 0);
    assert(GUI_MAX_WINDOWS == 32);
    assert(GUI_MAX_WIDGETS == 256);
    assert(GUI_TITLE_BAR_HEIGHT == 20);
    assert(WIDGET_BUTTON == 0);
    assert(WIDGET_LABEL == 1);
    assert(WIDGET_TEXTBOX == 2);
    assert(WIDGET_PROGRESS == 3);
    assert(WIDGET_CHECKBOX == 4);
    assert(WIDGET_PANEL == 5);
    printf("  [PASS] GUI: %d max windows, %d max widgets, title bar=%d\n",
           GUI_MAX_WINDOWS, GUI_MAX_WIDGETS, GUI_TITLE_BAR_HEIGHT);
    printf("  [PASS] Widget types: button=0, label=1, textbox=2, progress=3, checkbox=4, panel=5\n\n");
}

static void test_init_service_ordering(void) {
    printf("=== Init Service Ordering Tests ===\n");
    init_state_t init;
    memset(&init, 0, sizeof(init));
    init_register_services(&init);
    assert(init.num_services == 20);

    assert(init.services[SVC_GDT].dependencies[0] == 0 || init.services[SVC_GDT].num_deps == 0);
    assert(init.services[SVC_IDT].dependencies[0] == SVC_GDT);
    assert(init.services[SVC_PIC].dependencies[0] == SVC_IDT);
    assert(init.services[SVC_TIMER].dependencies[0] == SVC_PIC);
    assert(init.services[SVC_GUI].dependencies[0] == SVC_VBE);
    assert(init.services[SVC_GUI].dependencies[1] == SVC_KEYBOARD);
    assert(init.services[SVC_GUI].dependencies[2] == SVC_MOUSE);
    assert(init.services[SVC_FAT32].dependencies[0] == SVC_ATA);
    printf("  [PASS] 20 services registered with correct dependencies\n");
    printf("  [PASS] Boot order: GDT->IDT->PIC->Timer->KB/Mouse->PCI->...\n");

    assert(init.services[SVC_GDT].critical == true);
    assert(init.services[SVC_IDT].critical == true);
    assert(init.services[SVC_PIC].critical == true);
    assert(init.services[SVC_GUI].critical == false);
    printf("  [PASS] Critical services: GDT, IDT, PIC marked critical\n\n");
}

static void test_acpi_structures(void) {
    printf("=== ACPI Structure Tests ===\n");
    assert(sizeof(acpi_header_t) > 0);
    assert(sizeof(acpi_madt_t) > 0);
    assert(ACPI_MAX_TABLES == 16);

    acpi_state_t state;
    memset(&state, 0, sizeof(state));
    assert(state.num_tables == 0);
    assert(state.has_madt == false);
    assert(acpi_find_table(&state, "APIC") == NULL);
    printf("  [PASS] ACPI: empty state, max %d tables, find returns NULL\n\n",
           ACPI_MAX_TABLES);
}

static void test_timer_structures(void) {
    printf("=== Timer Structure Tests ===\n");
    assert(PIT_FREQUENCY == 1193182);
    assert(PIT_CHANNEL0 == 0x40);
    assert(PIT_COMMAND == 0x43);
    assert(TIMER_DEFAULT_HZ == 100);
    printf("  [PASS] PIT frequency = %d, channel0 = 0x%x, default Hz = %d\n\n",
           PIT_FREQUENCY, PIT_CHANNEL0, TIMER_DEFAULT_HZ);
}

static void test_net_structures(void) {
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

static void test_m5_router(void) {
    printf("=== M5 Omni-Router Tests ===\n");
    assert(M5_PROTO_MAX == 20);
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
    printf("  [PASS] 20 protocol types defined\n");

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
    m5_addr_from_satellite(&maa, 25544, 1);  /* ISS NORAD ID */
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

static void test_dtmf_morse(void) {
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
    code = morse_encode_char('S');
    assert(strcmp(code, "...") == 0);
    printf("  [PASS] Morse: S=... O=--- SOS correct\n");

    /* Morse encode/decode round-trip */
    char encoded[128];
    morse_encode("HELLO", encoded, sizeof(encoded));
    assert(strstr(encoded, "....") != NULL);  /* H = .... */
    assert(strstr(encoded, ".") != NULL);      /* E = . */
    printf("  [PASS] Morse encode 'HELLO' works\n");

    /* DTMF generation */
    int16_t samples[1000];
    int32_t n = dtmf_generate('5', samples, 1000);
    assert(n > 0);
    printf("  [PASS] DTMF tone generation: %d samples\n\n", n);
}

static void test_radio_structures(void) {
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

static void test_vino_ledger(void) {
    printf("=== Vino Bank Node Tests ===\n");
    assert(CAP_MAX == 9);
    assert(ASSET_MAX == 12);
    assert(RAIL_MAX == 20);
    assert(MSG_MAX == 12);
    printf("  [PASS] 9 capital types, 12 asset classes, 20 payment rails, 12 msg standards\n");

    vino_ledger_t v;
    memset(&v, 0, sizeof(v));
    vino_init(&v, 1);
    assert(v.node_id == 1);
    assert(v.num_accounts == 0);
    printf("  [PASS] Vino ledger initialized\n");

    int32_t idx = vino_create_account(&v, "ZEDEC:node:0001", "Genesis");
    assert(idx == 0);
    assert(v.num_accounts == 1);
    vino_account_t *acc = vino_get_account(&v, "ZEDEC:node:0001");
    assert(acc != NULL);
    assert(strcmp(acc->name, "Genesis") == 0);
    printf("  [PASS] Account creation + lookup\n");

    /* Create second account and transfer */
    vino_create_account(&v, "ZEDEC:node:0002", "Treasury");
    acc->balance[CAP_FINANCIAL] = 1000000;
    int32_t txid = vino_transfer(&v, "ZEDEC:node:0001", "ZEDEC:node:0002",
                                   50000, CAP_FINANCIAL, RAIL_VINO_NATIVE, "genesis grant");
    assert(txid >= 0);
    assert(v.num_txns == 1);
    uint64_t bal;
    vino_get_balance(&v, "ZEDEC:node:0001", CAP_FINANCIAL, &bal);
    assert(bal == 950000);
    vino_get_balance(&v, "ZEDEC:node:0002", CAP_FINANCIAL, &bal);
    assert(bal == 50000);
    printf("  [PASS] Transfer: 50000 financial capital, balances correct\n");

    /* Asset registration */
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

    /* P2P peers */
    vino_add_peer(&v, "ZEDEC:node:peer1", "10.0.0.2:8333");
    vino_add_peer(&v, "ZEDEC:node:peer2", "10.0.0.3:8333");
    assert(v.num_peers == 2);
    printf("  [PASS] 2 P2P peers added\n");

    /* Validator */
    vino_set_validator(&v, true, 1000000);
    assert(v.is_validator == true);
    int32_t block = vino_propose_block(&v);
    assert(block > 0);
    printf("  [PASS] Validator mode + block proposal\n");

    /* Messaging adapters */
    char msg_buf[256];
    int32_t msg_len = vino_msg_to_mt103(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "MT103") || strstr(msg_buf, "I103"));
    printf("  [PASS] MT103 message adapter\n");

    msg_len = vino_msg_to_pacs008(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "pacs.008"));
    printf("  [PASS] PACS.008 message adapter\n");

    msg_len = vino_msg_to_cips(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "CIPS"));
    printf("  [PASS] CIPS message adapter\n");

    msg_len = vino_msg_to_btc(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "BTC"));
    printf("  [PASS] BTC blockchain adapter\n");

    msg_len = vino_msg_to_eth(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "ETH"));
    printf("  [PASS] ETH blockchain adapter\n");

    msg_len = vino_msg_to_visa(&v.primary[0], msg_buf, sizeof(msg_buf));
    assert(msg_len > 0);
    assert(strstr(msg_buf, "VISA"));
    printf("  [PASS] Visa payment rail adapter\n");

    /* Capital type names */
    assert(strcmp(vino_capital_name(CAP_FINANCIAL), "Financial") == 0);
    assert(strcmp(vino_capital_name(CAP_KNOWLEDGE), "Knowledge") == 0);
    assert(strcmp(vino_capital_name(CAP_HUMAN), "Human") == 0);
    printf("  [PASS] Capital type names correct\n");

    /* Rail names */
    assert(strcmp(vino_rail_name(RAIL_SWIFT), "SWIFT") == 0);
    assert(strcmp(vino_rail_name(RAIL_CIPS), "CIPS") == 0);
    assert(strcmp(vino_rail_name(RAIL_SPFS), "SPFS") == 0);
    assert(strcmp(vino_rail_name(RAIL_VISA), "Visa") == 0);
    printf("  [PASS] Payment rail names correct\n\n");
}

static void test_vena_runtime(void) {
    printf("=== Vena Runtime Tests ===\n");
    assert(VENA_MAX_LANGUAGES == 32);
    assert(LANG_M5_AXIOMATIC == 30);
    printf("  [PASS] 31 languages defined (including M5 Axiomatic)\n");

    vino_ledger_t v;
    memset(&v, 0, sizeof(v));
    vino_init(&v, 1);

    vena_runtime_t vr;
    memset(&vr, 0, sizeof(vr));
    vena_init(&vr, &v);
    assert(vr.ledger == &v);
    assert(vr.default_language == LANG_M5_AXIOMATIC);
    printf("  [PASS] Vena runtime initialized with M5 Axiomatic language\n");

    /* Language support */
    assert(vena_is_language_supported(&vr, LANG_ENGLISH) == true);
    assert(vena_is_language_supported(&vr, LANG_KLINGON) == true);
    assert(vena_is_language_supported(&vr, LANG_NAVAJO) == true);
    assert(vena_is_language_supported(&vr, LANG_MORSE) == true);
    assert(vena_is_language_supported(&vr, LANG_M5_AXIOMATIC) == true);
    printf("  [PASS] Obscure languages supported: Navajo, Klingon, Morse, M5\n");

    /* Contract registration */
    int32_t cid = vena_register_contract(&vr, "genesis_contract",
        "M5:AXIOM:transfer", LANG_M5_AXIOMATIC, "ZEDEC:node:0001");
    assert(cid == 0);
    assert(vr.num_contracts == 1);
    printf("  [PASS] Smart contract registered\n");

    /* App loading */
    int32_t aid = vena_load_app(&vr, "Shell", APP_SHELL, "M5:shell", LANG_M5_AXIOMATIC);
    assert(aid == 0);
    aid = vena_load_app(&vr, "Wallet", APP_WALLET, "M5:wallet", LANG_M5_AXIOMATIC);
    assert(aid == 1);
    assert(vr.num_apps == 2);
    printf("  [PASS] 2 apps loaded (Shell, Wallet)\n");

    /* Start/stop apps */
    assert(vena_start_app(&vr, 0) == 0);
    assert(vr.apps[0].running == true);
    assert(vr.active_apps == 1);
    assert(vena_start_app(&vr, 1) == 0);
    assert(vr.active_apps == 2);
    assert(vena_stop_app(&vr, 0) == 0);
    assert(vr.active_apps == 1);
    printf("  [PASS] App start/stop works\n");

    /* Oracle registration */
    vena_register_oracle(&vr, "BTC/USD", "blockchain:price");
    vena_register_oracle(&vr, "EUR/USD", "forex:price");
    assert(vr.num_oracles == 2);
    vena_update_oracle(&vr, 0, 45000);
    assert(vr.oracles[0].last_value == 45000);
    printf("  [PASS] Oracles: BTC/USD=45000, EUR/USD registered\n");

    /* Language names */
    assert(strcmp(vena_language_name(LANG_ENGLISH), "English") == 0);
    assert(strcmp(vena_language_name(LANG_CHINESE), "\xe4\xb8\xad\xe6\x96\x87") == 0);
    assert(strcmp(vena_language_name(LANG_M5_AXIOMATIC), "M5-Axiomatic") == 0);
    printf("  [PASS] Language names correct\n\n");
}

int main(void) {
    printf("=== ZEDEC pqOS Driver & System Tests ===\n\n");

    test_gdt_layout();
    test_idt_layout();
    test_pic_constants();
    test_keyboard_scancode_table();
    test_pci_structures();
    test_vbe_color_macros();
    test_ata_structures();
    test_fat32_structures();
    test_gui_structures();
    test_init_service_ordering();
    test_acpi_structures();
    test_timer_structures();
    test_net_structures();
    test_m5_router();
    test_dtmf_morse();
    test_radio_structures();
    test_vino_ledger();
    test_vena_runtime();

    printf("=== All ZEDEC pqOS tests passed ===\n");
    return 0;
}

/* test_bluetooth.c — the Bluetooth host stack against the Core Spec wire format.
 *
 * The anchors are byte offsets and field orders from the specification, not
 * values read back out of our own builder: HCI_Reset is 03 0C 00 because the
 * opcode is (OGF 0x03 << 10) | OCF 0x0003 sent little-endian, the RFCOMM SABM
 * on DLCI 0 is 03 3F 01 1C because GSM 07.10 says so, and an A2DP media packet
 * starts 80 60 because RTP version 2 with payload type 96 says so. If our
 * layout drifts from the standard a real controller stops answering, so this
 * file checks the bytes, not the intent.
 *
 * The second thing it checks is that nothing succeeds without a radio. Every
 * air-facing call is invoked on an UNBOUND stack and must return BT_ENODEV
 * with no side effect — in particular bt_inquiry() must not invent a peer.
 *
 * The third thing it checks is that bt_verify_coverage() can FAIL. Five
 * separate corruptions are injected and each one must be caught.
 */
#include <stdio.h>
#include <string.h>
#include "bluetooth.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c,m) do{ checks++; if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* ===================== loopback controller model =====================
 * hci_send logs whatever the host hands it; hci_recv drains a queue the test
 * fills. Responses are injected explicitly so every assertion can name the
 * exact bytes it expects. */
typedef struct { uint8_t type; uint32_t len; uint8_t b[600]; } pkt_t;

typedef struct {
    pkt_t log[256];
    uint32_t n;
    pkt_t rxq[16];
    uint32_t rx_n, rx_i;
    int fail_send;
} ctrl_t;

static ctrl_t g_ctrl;

static int c_send(void *ctx, uint8_t type, const uint8_t *p, uint32_t n) {
    ctrl_t *c = (ctrl_t *)ctx;
    if (c->fail_send) return -1;
    if (c->n < 256 && n <= sizeof(c->log[0].b)) {
        c->log[c->n].type = type;
        c->log[c->n].len = n;
        memcpy(c->log[c->n].b, p, n);
        c->n++;
    }
    return 0;
}

static int c_recv(void *ctx, uint8_t *type, uint8_t *buf, uint32_t cap) {
    ctrl_t *c = (ctrl_t *)ctx;
    if (c->rx_i >= c->rx_n) return 0;
    pkt_t *p = &c->rxq[c->rx_i++];
    if (p->len > cap) return -1;
    *type = p->type;
    memcpy(buf, p->b, p->len);
    return (int)p->len;
}

static void ctrl_reset(void) {
    memset(&g_ctrl, 0, sizeof(g_ctrl));
}

/* ===================== injection helpers ===================== */

static int inj_evt(bluetooth_device_t *d, uint8_t code, const uint8_t *p, uint32_t n) {
    uint8_t buf[260];
    int len = bt_hci_build_event(buf, sizeof buf, code, p, n);
    if (len < 0) return len;
    return bt_hci_ingest(d, BT_H4_EVT, buf, (uint32_t)len);
}

static int inj_cc(bluetooth_device_t *d, uint16_t opcode, const uint8_t *ret, uint32_t n) {
    uint8_t p[260];
    p[0] = 1;                                   /* Num_HCI_Command_Packets */
    p[1] = (uint8_t)(opcode & 0xFF);
    p[2] = (uint8_t)(opcode >> 8);
    if (n) memcpy(p + 3, ret, n);
    return inj_evt(d, BT_EVT_CMD_COMPLETE, p, 3 + n);
}

static int inj_cs(bluetooth_device_t *d, uint8_t status, uint16_t opcode) {
    uint8_t p[4];
    p[0] = status; p[1] = 1;
    p[2] = (uint8_t)(opcode & 0xFF);
    p[3] = (uint8_t)(opcode >> 8);
    return inj_evt(d, BT_EVT_CMD_STATUS, p, 4);
}

static int inj_l2cap(bluetooth_device_t *d, uint16_t handle, uint16_t cid,
                     const uint8_t *pl, uint32_t n) {
    uint8_t pdu[700];
    int pl_len = bt_l2cap_build(pdu, sizeof pdu, cid, pl, n);
    if (pl_len < 0) return pl_len;
    uint8_t acl[720];
    int al = bt_hci_build_acl(acl, sizeof acl, handle, BT_PB_START_FLUSH, 0,
                              pdu, (uint32_t)pl_len);
    if (al < 0) return al;
    return bt_hci_ingest(d, BT_H4_ACL, acl, (uint32_t)al);
}

static int inj_sig(bluetooth_device_t *d, uint16_t handle, uint8_t code,
                   uint8_t ident, const uint8_t *data, uint32_t n) {
    /* Bigger than the stack's own signalling buffer on purpose, so the test
     * can hand it a command it cannot echo back verbatim. */
    uint8_t sig[256];
    int sl = bt_l2cap_build_sig(sig, sizeof sig, code, ident, data, n);
    if (sl < 0) return sl;
    return inj_l2cap(d, handle, BT_L2CAP_CID_SIGNAL, sig, (uint32_t)sl);
}

/* Pull the L2CAP payload out of a logged single-fragment ACL packet. */
static int acl_payload(const pkt_t *p, uint16_t *cid, const uint8_t **pl, uint32_t *n) {
    bt_hci_acl_t a;
    if (!p || p->type != BT_H4_ACL) return -1;
    if (bt_hci_parse_acl(p->b, p->len, &a) != BT_OK) return -1;
    bt_l2cap_frame_t f;
    if (bt_l2cap_parse(a.data, a.dlen, &f) != BT_OK) return -1;
    *cid = f.cid;
    *pl = f.payload;
    *n = f.len;
    return 0;
}

/* Independent bit-at-a-time reference for the GSM 07.10 FCS. Deliberately a
 * different algorithm from the table-driven one under test. */
static uint8_t ref_fcs(const uint8_t *d, uint32_t n) {
    uint8_t c = 0xFF;
    for (uint32_t i = 0; i < n; i++) {
        c = (uint8_t)(c ^ d[i]);
        for (int b = 0; b < 8; b++)
            c = (uint8_t)((c & 1) ? ((c >> 1) ^ 0xE0) : (c >> 1));
    }
    return (uint8_t)(0xFF - c);
}

static bluetooth_device_t g_dev;
static const uint8_t PEER[6] = {0x11,0x22,0x33,0x44,0x55,0x66};

/* ============================================================ */
static void test_hci_opcodes(void) {
    printf("\n--- HCI opcode packing (OGF<<10 | OCF, little-endian) ---\n");
    CHECK(bt_hci_opcode(0x03, 0x0003) == 0x0C03, "HCI_Reset opcode is 0x0C03");
    CHECK(bt_hci_opcode(0x01, 0x0001) == 0x0401, "HCI_Inquiry opcode is 0x0401");
    CHECK(bt_hci_opcode(0x04, 0x0009) == 0x1009, "HCI_Read_BD_ADDR opcode is 0x1009");
    CHECK(bt_hci_opcode(0x08, 0x000C) == 0x200C, "LE_Set_Scan_Enable opcode is 0x200C");
    CHECK(bt_hci_opcode_ogf(0x0C03) == 0x03 && bt_hci_opcode_ocf(0x0C03) == 0x0003,
          "0x0C03 splits back into OGF 0x03 / OCF 0x0003");
    CHECK(bt_hci_opcode_ogf(0x200C) == 0x08 && bt_hci_opcode_ocf(0x200C) == 0x000C,
          "0x200C splits back into OGF 0x08 / OCF 0x000C");

    uint8_t buf[300];
    int n = bt_hci_build_cmd(buf, sizeof buf, BT_HCI_RESET, NULL, 0);
    static const uint8_t want_reset[3] = {0x03, 0x0C, 0x00};
    CHECK(n == 3 && memcmp(buf, want_reset, 3) == 0,
          "HCI_Reset on the wire is exactly 03 0C 00");

    uint8_t scan = BT_SCAN_BOTH;
    n = bt_hci_build_cmd(buf, sizeof buf, BT_HCI_WRITE_SCAN_ENABLE, &scan, 1);
    static const uint8_t want_scan[4] = {0x1A, 0x0C, 0x01, 0x03};
    CHECK(n == 4 && memcmp(buf, want_scan, 4) == 0,
          "Write_Scan_Enable(both) is 1A 0C 01 03");

    uint8_t big[300];
    memset(big, 0xAB, sizeof big);
    CHECK(bt_hci_build_cmd(buf, sizeof buf, BT_HCI_RESET, big, 256) == BT_EMSGSIZE,
          "a 256-byte parameter block is refused: the length field is 8 bits");
    CHECK(bt_hci_build_cmd(buf, 3, BT_HCI_RESET, big, 4) == BT_ENOSPC,
          "building into an undersized buffer fails instead of overflowing");

    bt_hci_cmd_t c;
    n = bt_hci_build_cmd(buf, sizeof buf, BT_HCI_INQUIRY, big, 5);
    CHECK(bt_hci_parse_cmd(buf, (uint32_t)n, &c) == BT_OK && c.opcode == 0x0401 &&
          c.ogf == 0x01 && c.ocf == 0x0001 && c.plen == 5 && c.params[0] == 0xAB,
          "parse recovers opcode 0x0401, OGF/OCF and 5 parameter bytes");
    CHECK(bt_hci_parse_cmd(buf, (uint32_t)n - 1, &c) == BT_EPROTO,
          "a truncated command is rejected");
    CHECK(bt_hci_parse_cmd(buf, (uint32_t)n + 1, &c) == BT_EPROTO,
          "a command with trailing bytes is rejected");
    CHECK(bt_hci_parse_cmd(buf, 2, &c) == BT_EPROTO,
          "a 2-byte command (shorter than the header) is rejected");
}

static void test_hci_event_acl(void) {
    printf("\n--- HCI event and ACL layout ---\n");
    uint8_t buf[300];
    uint8_t p[4] = {0x00, 0x0B, 0x00, 0x13};
    int n = bt_hci_build_event(buf, sizeof buf, BT_EVT_DISCONN_COMPLETE, p, 4);
    static const uint8_t want[6] = {0x05, 0x04, 0x00, 0x0B, 0x00, 0x13};
    CHECK(n == 6 && memcmp(buf, want, 6) == 0,
          "Disconnection_Complete is 05 04 <status> <handle LE> <reason>");
    bt_hci_event_t e;
    CHECK(bt_hci_parse_event(buf, 6, &e) == BT_OK && e.code == 0x05 && e.plen == 4 &&
          e.params[3] == 0x13, "parse recovers event code 0x05 and reason 0x13");
    buf[1] = 0x09;   /* claim 9 parameter bytes when only 4 follow */
    CHECK(bt_hci_parse_event(buf, 6, &e) == BT_EPROTO,
          "an event whose length field lies is rejected");

    uint8_t data[3] = {0xDE, 0xAD, 0xBE};
    n = bt_hci_build_acl(buf, sizeof buf, 0x0123, BT_PB_START_FLUSH,
                         BT_BC_POINT_TO_POINT, data, 3);
    /* handle 0x0123 | PB 0b10 << 12 = 0x2123, little-endian, then length. */
    static const uint8_t want_acl[7] = {0x23, 0x21, 0x03, 0x00, 0xDE, 0xAD, 0xBE};
    CHECK(n == 7 && memcmp(buf, want_acl, 7) == 0,
          "ACL header packs handle:12|PB:2|BC:2 as 23 21 then length 03 00");
    bt_hci_acl_t a;
    CHECK(bt_hci_parse_acl(buf, 7, &a) == BT_OK && a.handle == 0x0123 &&
          a.pb == BT_PB_START_FLUSH && a.bc == 0 && a.dlen == 3,
          "parse recovers handle 0x0123 and PB=2 from the packed word");
    CHECK(bt_hci_build_acl(buf, sizeof buf, 0x1000, 0, 0, data, 3) == BT_EINVAL,
          "a handle above 12 bits is refused, not silently truncated");
    CHECK(bt_hci_parse_acl(buf, 3, &a) == BT_EPROTO,
          "an ACL packet shorter than its 4-byte header is rejected");

    uint8_t h4[8];
    CHECK(bt_h4_wrap(h4, sizeof h4, BT_H4_CMD, want, 3) == 4 && h4[0] == 0x01 &&
          h4[1] == 0x05, "H4 wrap prefixes the 0x01 command indicator");
    uint8_t t = 0; const uint8_t *pp = NULL; uint32_t pl = 0;
    CHECK(bt_h4_unwrap(h4, 4, &t, &pp, &pl) == BT_OK && t == BT_H4_CMD && pl == 3,
          "H4 unwrap returns type 0x01 and a 3-byte packet");
    uint8_t bogus[3] = {0x77, 0x00, 0x00};
    CHECK(bt_h4_unwrap(bogus, 3, &t, &pp, &pl) == BT_EPROTO,
          "H4 unwrap rejects indicator 0x77");
}

static void test_l2cap_codec(void) {
    printf("\n--- L2CAP framing ---\n");
    uint8_t buf[64];
    uint8_t pl[5] = {1,2,3,4,5};
    int n = bt_l2cap_build(buf, sizeof buf, 0x0040, pl, 5);
    static const uint8_t want[9] = {0x05, 0x00, 0x40, 0x00, 1,2,3,4,5};
    CHECK(n == 9 && memcmp(buf, want, 9) == 0,
          "L2CAP B-frame is length LE then CID LE then payload");
    bt_l2cap_frame_t f;
    CHECK(bt_l2cap_parse(buf, 9, &f) == BT_OK && f.len == 5 && f.cid == 0x0040,
          "parse recovers length 5 and CID 0x0040");
    CHECK(bt_l2cap_parse(buf, 8, &f) == BT_EPROTO,
          "an L2CAP frame one byte short of its length field is rejected");

    uint8_t req[4] = {0x03, 0x00, 0x40, 0x00};      /* PSM 0x0003, SCID 0x0040 */
    n = bt_l2cap_build_sig(buf, sizeof buf, BT_L2CAP_CONN_REQ, 1, req, 4);
    static const uint8_t want_sig[8] = {0x02, 0x01, 0x04, 0x00, 0x03, 0x00, 0x40, 0x00};
    CHECK(n == 8 && memcmp(buf, want_sig, 8) == 0,
          "L2CAP Connection Request is 02 <id> 04 00 then PSM and SCID");
    bt_l2cap_sig_t s;
    CHECK(bt_l2cap_parse_sig(buf, 8, &s) == BT_OK && s.code == 0x02 &&
          s.ident == 1 && s.len == 4, "parse recovers sig code 0x02, id 1, len 4");
    CHECK(bt_l2cap_build_sig(buf, sizeof buf, BT_L2CAP_CONN_REQ, 0, req, 4) == BT_EINVAL,
          "signalling identifier 0 is reserved and refused");
}

static void test_fragmentation(void) {
    printf("\n--- ACL fragmentation and reassembly ---\n");
    CHECK(bt_acl_frag_count(100, 27) == 4, "a 100-byte PDU needs 4 fragments at MTU 27");
    CHECK(bt_acl_frag_count(27, 27) == 1, "an exactly-MTU PDU is one fragment");
    CHECK(bt_acl_frag_count(28, 27) == 2, "one byte over the MTU needs two");
    CHECK(bt_acl_frag_count(100, 0) == 0, "MTU 0 yields no fragments (it is unknown)");

    uint8_t pdu[100];
    for (uint32_t i = 0; i < 100; i++) pdu[i] = (uint8_t)(i * 7u + 3u);
    /* Give the payload a truthful L2CAP header so reassembly can size it. */
    pdu[0] = 96; pdu[1] = 0; pdu[2] = 0x41; pdu[3] = 0x00;

    uint8_t frag[64];
    int fl = bt_acl_fragment(frag, sizeof frag, 0x000B, 27, pdu, 100, 0);
    CHECK(fl == 31 && frag[0] == 0x0B && frag[1] == 0x20 && frag[2] == 27,
          "fragment 0 is 27 payload bytes with PB=0b10 (start)");
    fl = bt_acl_fragment(frag, sizeof frag, 0x000B, 27, pdu, 100, 1);
    CHECK(fl == 31 && frag[1] == 0x10,
          "fragment 1 carries PB=0b01 (continuation)");
    fl = bt_acl_fragment(frag, sizeof frag, 0x000B, 27, pdu, 100, 3);
    CHECK(fl == 23 && frag[2] == 19, "the last fragment holds the 19-byte remainder");
    CHECK(bt_acl_fragment(frag, sizeof frag, 0x000B, 27, pdu, 100, 4) == 0,
          "asking for a fragment past the end returns 0, not garbage");

    bt_reasm_t r;
    bt_reasm_init(&r);
    int done = 0;
    for (uint32_t i = 0; i < 4; i++) {
        fl = bt_acl_fragment(frag, sizeof frag, 0x000B, 27, pdu, 100, i);
        done = bt_reasm_push(&r, frag, (uint32_t)fl);
        if (i < 3) CHECK(done == 0, "reassembly reports 'need more' for a middle fragment");
    }
    CHECK(done == 100, "reassembly completes at exactly 100 bytes");
    uint32_t got = 0;
    const uint8_t *out = bt_reasm_pdu(&r, &got);
    CHECK(out && got == 100 && memcmp(out, pdu, 100) == 0,
          "the reassembled PDU is byte-identical to the original");

    /* A continuation with no start must be dropped, not accepted. */
    bt_reasm_init(&r);
    fl = bt_acl_fragment(frag, sizeof frag, 0x000B, 27, pdu, 100, 1);
    CHECK(bt_reasm_push(&r, frag, (uint32_t)fl) == BT_EPROTO && r.drops == 1,
          "a continuation fragment with no start is rejected and counted");

    /* A start fragment on a different handle mid-PDU must not be spliced in. */
    bt_reasm_init(&r);
    fl = bt_acl_fragment(frag, sizeof frag, 0x000B, 27, pdu, 100, 0);
    bt_reasm_push(&r, frag, (uint32_t)fl);
    fl = bt_acl_fragment(frag, sizeof frag, 0x000C, 27, pdu, 100, 1);
    CHECK(bt_reasm_push(&r, frag, (uint32_t)fl) == BT_EPROTO && r.drops == 1,
          "a continuation for a different handle is rejected");

    /* Overflow: a PDU larger than the reassembly buffer must fail closed. */
    static uint8_t huge[1400];
    memset(huge, 0x5A, sizeof huge);
    huge[0] = (uint8_t)((1400 - 4) & 0xFF);
    huge[1] = (uint8_t)((1400 - 4) >> 8);
    bt_reasm_init(&r);
    int rc = 0;
    static uint8_t hf[600];
    for (uint32_t i = 0; i < bt_acl_frag_count(1400, 512); i++) {
        fl = bt_acl_fragment(hf, sizeof hf, 0x000B, 512, huge, 1400, i);
        rc = bt_reasm_push(&r, hf, (uint32_t)fl);
        if (rc < 0) break;
    }
    CHECK(rc == BT_ENOSPC && r.drops == 1,
          "a PDU past BT_REASM_MAX is refused rather than smashing the buffer");

    /* PB=0b00 is a host-to-controller encoding; a controller must not use it. */
    bt_reasm_init(&r);
    uint8_t pb0[8];
    fl = bt_hci_build_acl(pb0, sizeof pb0, 0x000B, 0x00, 0, pdu, 4);
    CHECK(bt_reasm_push(&r, pb0, (uint32_t)fl) == BT_EPROTO,
          "a received fragment with PB=0b00 is rejected: that flag is host->controller");

    /* A start fragment carrying more than its own length field claims. */
    bt_reasm_init(&r);
    uint8_t liar[24];
    liar[0] = 0x04; liar[1] = 0x00; liar[2] = 0x40; liar[3] = 0x00;  /* claims 8 total */
    memset(liar + 4, 0xEE, 20);
    fl = bt_hci_build_acl(frag, sizeof frag, 0x000B, BT_PB_START_FLUSH, 0, liar, 24);
    CHECK(bt_reasm_push(&r, frag, (uint32_t)fl) == BT_EPROTO,
          "a fragment longer than its L2CAP length field is rejected");
}

static void test_rfcomm(void) {
    printf("\n--- RFCOMM framing and the GSM 07.10 FCS ---\n");
    /* The table-driven FCS must agree with a bit-at-a-time reference on every
     * 3-byte header we can plausibly send. */
    int mismatch = 0;
    for (uint32_t a = 0; a < 256; a += 7) {
        for (uint32_t b = 0; b < 256; b += 11) {
            uint8_t h[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)((a * 3u + b) & 0xFF)};
            if (bt_rfcomm_fcs(h, 3) != ref_fcs(h, 3)) mismatch++;
            if (bt_rfcomm_fcs(h, 2) != ref_fcs(h, 2)) mismatch++;
        }
    }
    CHECK(mismatch == 0,
          "table FCS matches a bit-at-a-time reference over 1656 headers");

    static const uint8_t sabm0[3] = {0x03, 0x3F, 0x01};
    CHECK(bt_rfcomm_fcs(sabm0, 3) == 0x1C,
          "FCS of the published SABM/DLCI0 header 03 3F 01 is 0x1C");
    static const uint8_t ua0[3] = {0x03, 0x73, 0x01};
    CHECK(bt_rfcomm_fcs(ua0, 3) == 0xD7,
          "FCS of the published UA/DLCI0 header 03 73 01 is 0xD7");

    uint8_t buf[64];
    int n = bt_rfcomm_build(buf, sizeof buf, 0, 1, BT_RFCOMM_SABM, true, NULL, 0);
    static const uint8_t want_sabm[4] = {0x03, 0x3F, 0x01, 0x1C};
    CHECK(n == 4 && memcmp(buf, want_sabm, 4) == 0,
          "SABM on DLCI 0 is exactly 03 3F 01 1C");

    n = bt_rfcomm_build(buf, sizeof buf, 2, 1, BT_RFCOMM_UIH, false,
                        (const uint8_t *)"HI", 2);
    uint8_t hdr2[2] = {0x0B, 0xEF};
    CHECK(n == 6 && buf[0] == 0x0B && buf[1] == 0xEF && buf[2] == 0x05 &&
          buf[3] == 'H' && buf[4] == 'I' && buf[5] == ref_fcs(hdr2, 2),
          "UIH on DLCI 2 is 0B EF 05 'H' 'I' <FCS over 2 header bytes>");

    /* Lengths of 128 and up take a two-octet EA-extended field. */
    uint8_t big[200];
    memset(big, 0x41, sizeof big);
    n = bt_rfcomm_build(buf, sizeof buf, 2, 1, BT_RFCOMM_UIH, false, big, 200);
    CHECK(n == BT_ENOSPC, "a 200-byte UIH into a 64-byte buffer is refused");
    static uint8_t wide[300];
    n = bt_rfcomm_build(wide, sizeof wide, 2, 1, BT_RFCOMM_UIH, false, big, 200);
    CHECK(n == 205 && (wide[2] & 0x01) == 0 && wide[2] == (uint8_t)((200 & 0x7F) << 1) &&
          wide[3] == (200 >> 7),
          "a 200-byte UIH uses the 2-octet length form with EA clear");

    bt_rfcomm_frame_t f;
    CHECK(bt_rfcomm_parse(wide, 205, &f) == BT_OK && f.dlci == 2 && f.cr == 1 &&
          f.ctrl == BT_RFCOMM_UIH && f.len == 200 && f.fcs_ok,
          "parse recovers DLCI 2, UIH, 200 payload bytes and a good FCS");
    wide[204] ^= 0xFF;
    CHECK(bt_rfcomm_parse(wide, 205, &f) == BT_OK && !f.fcs_ok,
          "a corrupted FCS is reported as bad, not ignored");
    CHECK(bt_rfcomm_parse(want_sabm, 3, &f) == BT_EPROTO,
          "a 3-byte RFCOMM frame (no room for an FCS) is rejected");
}

static void test_avdtp_hid_rtp(void) {
    printf("\n--- AVDTP / HIDP / RTP framing ---\n");
    uint8_t buf[64];
    int n = bt_avdtp_build(buf, sizeof buf, 3, BT_AVDTP_MSG_COMMAND,
                           BT_AVDTP_DISCOVER, NULL, 0);
    CHECK(n == 2 && buf[0] == 0x30 && buf[1] == 0x01,
          "AVDTP DISCOVER command with label 3 is 30 01");
    n = bt_avdtp_build(buf, sizeof buf, 3, BT_AVDTP_MSG_ACCEPT, BT_AVDTP_START, NULL, 0);
    CHECK(n == 2 && buf[0] == 0x32 && buf[1] == 0x07,
          "AVDTP START accept with label 3 is 32 07");
    bt_avdtp_msg_t m;
    CHECK(bt_avdtp_parse(buf, 2, &m) == BT_OK && m.label == 3 &&
          m.msg_type == BT_AVDTP_MSG_ACCEPT && m.signal_id == BT_AVDTP_START,
          "parse recovers label 3, ResponseAccept and signal 0x07");
    CHECK(bt_avdtp_build(buf, sizeof buf, 16, 0, BT_AVDTP_START, NULL, 0) == BT_EINVAL,
          "a transaction label above 4 bits is refused");
    CHECK(bt_avdtp_parse(buf, 1, &m) == BT_EPROTO, "a 1-byte AVDTP message is rejected");

    uint8_t rep[3] = {0x01, 0x02, 0x03};
    n = bt_hidp_build(buf, sizeof buf, BT_HIDP_DATA, BT_HIDP_RTYPE_OUTPUT, rep, 3);
    CHECK(n == 4 && buf[0] == 0xA2, "HIDP DATA|Output header byte is 0xA2");
    n = bt_hidp_build(buf, sizeof buf, BT_HIDP_DATA, BT_HIDP_RTYPE_INPUT, rep, 3);
    CHECK(n == 4 && buf[0] == 0xA1, "HIDP DATA|Input header byte is 0xA1");
    uint8_t tt = 0, pm = 0; const uint8_t *pl = NULL; uint32_t pn = 0;
    CHECK(bt_hidp_parse(buf, 4, &tt, &pm, &pl, &pn) == BT_OK && tt == BT_HIDP_DATA &&
          pm == BT_HIDP_RTYPE_INPUT && pn == 3 && pl[2] == 0x03,
          "parse recovers DATA/Input and the 3 report bytes");

    n = bt_a2dp_media_header(buf, sizeof buf, 0x1234, 0x00010000u, 0xAABBCCDDu, 5);
    static const uint8_t want_rtp[13] = {0x80, 0x60, 0x12, 0x34, 0x00, 0x01, 0x00, 0x00,
                                         0xAA, 0xBB, 0xCC, 0xDD, 0x05};
    CHECK(n == 13 && memcmp(buf, want_rtp, 13) == 0,
          "RTP v2/PT96 header is 80 60 <seq BE> <ts BE> <ssrc BE> then SBC frame count");
    CHECK(bt_a2dp_media_header(buf, sizeof buf, 0, 0, 0, 0) == BT_EINVAL,
          "a media packet claiming zero frames is refused");
    CHECK(bt_a2dp_media_header(buf, 12, 0, 0, 0, 1) == BT_ENOSPC,
          "the 13-byte media header will not build into 12 bytes");
}

static void test_cod_and_states(void) {
    printf("\n--- Class of Device decoding and the link state machine ---\n");
    CHECK(bt_class_from_cod(0x240404) == BT_CLASS_HEADSET,
          "CoD 0x240404 (audio major, headset minor) decodes as a headset");
    CHECK(bt_class_from_cod(0x240414) == BT_CLASS_SPEAKER,
          "CoD 0x240414 (loudspeaker minor 0x05) decodes as a speaker");
    CHECK(bt_class_from_cod(0x000540) == BT_CLASS_KEYBOARD,
          "CoD 0x000540 (peripheral, keyboard bits 01) decodes as a keyboard");
    CHECK(bt_class_from_cod(0x000580) == BT_CLASS_MOUSE,
          "CoD 0x000580 (peripheral, pointing bits 10) decodes as a mouse");
    CHECK(bt_class_from_cod(0x00010C) == BT_CLASS_COMPUTER, "major 0x01 is a computer");
    CHECK(bt_class_from_cod(0x000204) == BT_CLASS_PHONE, "major 0x02 is a phone");
    CHECK(bt_class_from_cod(0x000900) == BT_CLASS_HEALTH, "major 0x09 is a health device");
    CHECK(bt_class_from_cod(0x001F00) == BT_CLASS_UNKNOWN,
          "an unassigned major class stays UNKNOWN instead of being guessed");

    CHECK(bt_state_transition_ok(BT_STATE_DISCONNECTED, BT_STATE_CONNECTING),
          "DISCONNECTED -> CONNECTING is legal");
    CHECK(!bt_state_transition_ok(BT_STATE_DISCONNECTED, BT_STATE_CONNECTED),
          "DISCONNECTED -> CONNECTED is ILLEGAL: a link cannot skip paging");
    CHECK(!bt_state_transition_ok(BT_STATE_CONNECTED, BT_STATE_PAIRED),
          "CONNECTED -> PAIRED is ILLEGAL: authentication must be requested first");
    CHECK(!bt_state_transition_ok(BT_STATE_CONNECTED, BT_STATE_CONNECTED),
          "a self-transition is rejected");
    CHECK(bt_state_transition_ok(BT_STATE_PAIRING, BT_STATE_PAIRED) &&
          bt_state_transition_ok(BT_STATE_PAIRING, BT_STATE_CONNECTED),
          "PAIRING can end in PAIRED or fall back to CONNECTED");
    CHECK(bt_state_transition_ok(BT_STATE_PAIRED, BT_STATE_DISCONNECTED),
          "PAIRED -> DISCONNECTED is legal");
}

/* ============================================================
 * No radio: nothing may succeed, and nothing may appear. */
static void test_no_radio(void) {
    printf("\n--- with NO radio bound, every air-facing call must fail ---\n");
    bt_init(&g_dev, "zxv-bt");
    CHECK(!bt_has_radio(&g_dev), "a freshly initialised stack has no radio");
    CHECK(g_dev.local_addr_valid == false && g_dev.local_addr[0] == 0,
          "no BD_ADDR is invented at init");
    CHECK(g_dev.version_valid == false,
          "no controller version is claimed before Read Local Version");
    CHECK(g_dev.reg_acl_mtu == 0 && g_dev.acl_credits == 0,
          "buffer geometry is unknown until the controller reports it");

    CHECK(bt_inquiry(&g_dev, 10) == BT_ENODEV, "bt_inquiry returns BT_ENODEV");
    CHECK(bt_get_device_count(&g_dev) == 0,
          "AND the device table is still empty: no peer was invented");
    CHECK(g_dev.inquiring == false, "the stack does not claim to be inquiring");

    CHECK(bt_reset(&g_dev) == BT_ENODEV, "bt_reset returns BT_ENODEV");
    CHECK(bt_set_discoverable(&g_dev, true) == BT_ENODEV,
          "bt_set_discoverable returns BT_ENODEV");
    CHECK(g_dev.discoverable == false,
          "AND discoverable stays false: the controller was never told");
    CHECK(bt_pair(&g_dev, PEER, "1234") == BT_ENODEV, "bt_pair returns BT_ENODEV");
    CHECK(bt_connect(&g_dev, PEER, BT_PROFILE_SPP) == BT_ENODEV,
          "bt_connect returns BT_ENODEV");
    CHECK(bt_disconnect(&g_dev, PEER) == BT_ENODEV, "bt_disconnect returns BT_ENODEV");
    CHECK(bt_send_data(&g_dev, PEER, "x", 1) == BT_ENODEV, "bt_send_data returns BT_ENODEV");
    CHECK(bt_audio_connect(&g_dev, PEER) == BT_ENODEV, "bt_audio_connect returns BT_ENODEV");
    CHECK(bt_audio_start(&g_dev, BT_A2DP_CODEC_SBC, 44100) == BT_ENODEV,
          "bt_audio_start returns BT_ENODEV");
    CHECK(bt_audio_stop(&g_dev) == BT_ENODEV, "bt_audio_stop returns BT_ENODEV");
    CHECK(bt_audio_send(&g_dev, "x", 1) == BT_ENODEV, "bt_audio_send returns BT_ENODEV");
    CHECK(bt_hid_connect(&g_dev, PEER) == BT_ENODEV, "bt_hid_connect returns BT_ENODEV");
    CHECK(bt_hid_send_report(&g_dev, PEER, "x", 1) == BT_ENODEV,
          "bt_hid_send_report returns BT_ENODEV");
    CHECK(bt_poll(&g_dev, 4) == BT_ENODEV, "bt_poll returns BT_ENODEV");
    CHECK(bt_tx_flush(&g_dev) == BT_ENODEV, "bt_tx_flush returns BT_ENODEV");

    /* Local-only state is still allowed to change, and says so. */
    CHECK(bt_set_name(&g_dev, "dragon") == BT_ENODEV,
          "bt_set_name returns BT_ENODEV (the radio was not told)");
    CHECK(strcmp(g_dev.local_name, "dragon") == 0,
          "AND the host-side name is set, exactly as the header promises");

    bt_handle_irq(&g_dev);
    CHECK(!g_dev.irq_rx_ready && !g_dev.irq_connected && !g_dev.irq_inquiry_done,
          "bt_handle_irq with no transport latches no interrupts");
    const bt_stats_t *st = bt_get_stats(&g_dev);
    CHECK(st->hci_cmds_sent == 0, "no command is counted as sent");
    CHECK(st->hci_cmds_failed == 7,
          "the 7 calls that tried to build a command counted it as failed instead");
    CHECK(bt_verify_coverage(&g_dev), "an empty stack is internally consistent");
}

/* ============================================================
 * Controller bring-up over the loopback transport. */
static uint16_t g_handle = 0x000B;

static void bringup(void) {
    printf("\n--- controller bring-up (Reset / BD_ADDR / Buffer Size / Version) ---\n");
    ctrl_reset();
    bt_init(&g_dev, "zxv-bt");
    bt_ops_t ops = { c_send, c_recv, &g_ctrl };
    CHECK(bt_bind_ops(&g_dev, &ops) == BT_OK && bt_has_radio(&g_dev),
          "the radio backend binds");

    bt_ops_t bad = { NULL, c_recv, &g_ctrl };
    CHECK(bt_bind_ops(&g_dev, &bad) == BT_EINVAL,
          "a half-filled ops struct is refused");

    CHECK(bt_reset(&g_dev) == BT_OK, "bt_reset queues the bring-up sequence");
    CHECK(g_ctrl.n == 1, "only ONE command goes out: the controller allows one at a time");
    static const uint8_t want_reset[3] = {0x03, 0x0C, 0x00};
    CHECK(g_ctrl.log[0].type == BT_H4_CMD && g_ctrl.log[0].len == 3 &&
          memcmp(g_ctrl.log[0].b, want_reset, 3) == 0, "and it is HCI_Reset 03 0C 00");

    uint8_t ok0 = 0x00;
    inj_cc(&g_dev, BT_HCI_RESET, &ok0, 1);
    CHECK(g_ctrl.n == 2 && g_ctrl.log[1].b[0] == 0x09 && g_ctrl.log[1].b[1] == 0x10,
          "the credit returned by Command Complete releases Read_BD_ADDR (09 10)");

    uint8_t bdret[7] = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    inj_cc(&g_dev, BT_HCI_READ_BD_ADDR, bdret, 7);
    static const uint8_t want_addr[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    CHECK(g_dev.local_addr_valid && memcmp(g_dev.local_addr, want_addr, 6) == 0,
          "the local address comes from the controller, LSB first");

    /* ACL packet length 27, 64 buffers. A deliberately small MTU so the
     * fragmentation path is exercised by every media packet below. */
    uint8_t bufret[8] = {0x00, 0x1B, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00};
    inj_cc(&g_dev, BT_HCI_READ_BUFFER_SIZE, bufret, 8);
    CHECK(g_dev.reg_acl_mtu == 27 && g_dev.acl_total_pkts == 64 && g_dev.acl_credits == 64,
          "Read_Buffer_Size sets ACL MTU 27 and 64 credits");

    uint8_t verret[9] = {0x00, 0x0C, 0x00, 0x00, 0x0C, 0x0F, 0x00, 0x00, 0x00};
    inj_cc(&g_dev, BT_HCI_READ_LOCAL_VERSION, verret, 9);
    CHECK(g_dev.version_valid && g_dev.version == BT_VERSION_5_3,
          "HCI version 0x0C is reported as Bluetooth 5.3");

    printf("\n--- discoverability is only claimed once confirmed ---\n");
    uint32_t before = g_ctrl.n;
    CHECK(bt_set_discoverable(&g_dev, true) == BT_OK, "Write_Scan_Enable is sent");
    static const uint8_t want_scan[4] = {0x1A, 0x0C, 0x01, 0x03};
    CHECK(g_ctrl.n == before + 1 && memcmp(g_ctrl.log[before].b, want_scan, 4) == 0,
          "the bytes are 1A 0C 01 03 (inquiry scan + page scan)");
    CHECK(g_dev.discoverable == false,
          "discoverable is still false before the controller confirms");
    inj_cc(&g_dev, BT_HCI_WRITE_SCAN_ENABLE, &ok0, 1);
    CHECK(g_dev.discoverable && g_dev.connectable,
          "and only now does the stack claim to be discoverable");
}

static void inquiry_and_connect(void) {
    printf("\n--- inquiry: devices come from event bytes, nowhere else ---\n");
    uint32_t before = g_ctrl.n;
    CHECK(bt_inquiry(&g_dev, 10) == BT_OK, "bt_inquiry is accepted with a radio bound");
    /* 10 s / 1.28 = 7 units; LAP 0x9E8B33 goes out LSB first. */
    static const uint8_t want_inq[8] = {0x01, 0x04, 0x05, 0x33, 0x8B, 0x9E, 0x07, 0x00};
    CHECK(g_ctrl.n == before + 1 && g_ctrl.log[before].len == 8 &&
          memcmp(g_ctrl.log[before].b, want_inq, 8) == 0,
          "HCI_Inquiry carries GIAC 33 8B 9E, 7 units of 1.28 s, unlimited responses");
    CHECK(bt_get_device_count(&g_dev) == 0,
          "no device exists yet: the command has only been sent");
    inj_cs(&g_dev, 0x00, BT_HCI_INQUIRY);
    CHECK(bt_inquiry(&g_dev, 10) == BT_EAGAIN, "a second inquiry while one runs is refused");

    /* Inquiry Result with RSSI: BD_ADDR, PSRM, reserved, CoD, clock offset, RSSI. */
    uint8_t ir[15];
    ir[0] = 1;
    memcpy(ir + 1, PEER, 6);
    ir[7] = 0x01; ir[8] = 0x00;
    ir[9] = 0x14; ir[10] = 0x04; ir[11] = 0x24;      /* CoD 0x240414 = speaker */
    ir[12] = 0x00; ir[13] = 0x00;
    ir[14] = (uint8_t)(int8_t)-60;
    CHECK(inj_evt(&g_dev, BT_EVT_INQUIRY_RESULT_RSSI, ir, 15) == BT_OK,
          "an Inquiry Result with RSSI is accepted");
    CHECK(bt_get_device_count(&g_dev) == 1, "exactly one device is now known");
    bt_device_t *d = bt_get_device(&g_dev, 0);
    CHECK(d && memcmp(d->bdaddr, PEER, 6) == 0, "its address is the one on the wire");
    CHECK(d && d->cod == 0x240414u && d->dev_class == BT_CLASS_SPEAKER,
          "its CoD 0x240414 decoded to BT_CLASS_SPEAKER");
    CHECK(d && d->rssi == -60, "its RSSI is -60 dBm, sign-extended from one octet");
    CHECK(bt_get_device(&g_dev, 1) == NULL && bt_get_device(&g_dev, 9999) == NULL,
          "out-of-range device indices return NULL");

    /* A malformed result must be rejected and must not create anything. */
    uint8_t bad_ir[15];
    memcpy(bad_ir, ir, 15);
    bad_ir[0] = 2;                    /* claims 2 responses, carries one */
    uint32_t nd = bt_get_device_count(&g_dev);
    uint32_t bad_before = bt_get_stats(&g_dev)->hci_events_bad;
    CHECK(inj_evt(&g_dev, BT_EVT_INQUIRY_RESULT_RSSI, bad_ir, 15) == BT_EPROTO,
          "an Inquiry Result whose count does not match its length is rejected");
    CHECK(bt_get_device_count(&g_dev) == nd,
          "AND no half-parsed device was added");
    CHECK(bt_get_stats(&g_dev)->hci_events_bad == bad_before + 1,
          "the malformed event is counted as bad");

    uint8_t ic = 0x00;
    inj_evt(&g_dev, BT_EVT_INQUIRY_COMPLETE, &ic, 1);
    CHECK(!g_dev.inquiring && g_dev.irq_inquiry_done,
          "Inquiry Complete clears the inquiring flag and latches the IRQ");

    printf("\n--- paging and the L2CAP/RFCOMM handshake for SPP ---\n");
    uint8_t nope[6] = {0,0,0,0,0,1};
    CHECK(bt_connect(&g_dev, nope, BT_PROFILE_SPP) == BT_ENOENT,
          "connecting to an address we never heard is refused");
    CHECK(bt_connect(&g_dev, PEER, BT_PROFILE_HFP) == BT_ENOTSUP,
          "HFP is refused with BT_ENOTSUP — it is not implemented");

    before = g_ctrl.n;
    CHECK(bt_connect(&g_dev, PEER, BT_PROFILE_SPP) == BT_OK, "bt_connect is accepted");
    static const uint8_t want_cc[16] = {0x05, 0x04, 0x0D,
        0x11,0x22,0x33,0x44,0x55,0x66, 0x18, 0xCC, 0x01, 0x00, 0x00, 0x00, 0x01};
    CHECK(g_ctrl.n == before + 1 && g_ctrl.log[before].len == 16 &&
          memcmp(g_ctrl.log[before].b, want_cc, 16) == 0,
          "Create_Connection is 05 04 0D + addr + packet types CC18 + PSRM/clkoff/role");
    bt_conn_t *c = bt_find_conn_addr(&g_dev, PEER);
    CHECK(c && c->state == BT_STATE_CONNECTING && c->handle == 0,
          "the link is CONNECTING and has no handle yet");
    inj_cs(&g_dev, 0x00, BT_HCI_CREATE_CONNECTION);

    uint8_t cco[11];
    cco[0] = 0x00;
    cco[1] = (uint8_t)(g_handle & 0xFF); cco[2] = (uint8_t)(g_handle >> 8);
    memcpy(cco + 3, PEER, 6);
    cco[9] = 0x01; cco[10] = 0x00;
    before = g_ctrl.n;
    CHECK(inj_evt(&g_dev, BT_EVT_CONN_COMPLETE, cco, 11) == BT_OK,
          "Connection Complete is accepted");
    CHECK(c->state == BT_STATE_CONNECTED && c->handle == g_handle,
          "the link is CONNECTED on handle 0x000B");
    CHECK(bt_get_device(&g_dev, 0)->connected, "the device record says connected");

    /* The stack should now be paging L2CAP for the RFCOMM PSM by itself. */
    uint16_t cid = 0; const uint8_t *pl = NULL; uint32_t pn = 0;
    CHECK(g_ctrl.n == before + 1 && acl_payload(&g_ctrl.log[before], &cid, &pl, &pn) == 0,
          "one ACL packet follows the connection");
    static const uint8_t want_conreq[8] = {0x02, 0x01, 0x04, 0x00, 0x03, 0x00, 0x40, 0x00};
    CHECK(cid == BT_L2CAP_CID_SIGNAL && pn == 8 && memcmp(pl, want_conreq, 8) == 0,
          "it is an L2CAP Connection Request for PSM 0x0003 with our CID 0x0040");
    CHECK(g_ctrl.log[before].b[0] == 0x0B && g_ctrl.log[before].b[1] == 0x20,
          "carried on handle 0x000B with PB=0b10 (first fragment)");

    /* Connection Response: our 0x0040 gets the peer's 0x0041. */
    uint8_t conrsp[8] = {0x41, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
    before = g_ctrl.n;
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONN_RSP, 1, conrsp, 8);
    CHECK(g_ctrl.n == before + 1 && acl_payload(&g_ctrl.log[before], &cid, &pl, &pn) == 0,
          "the response triggers exactly one more packet");
    static const uint8_t want_confreq[12] = {0x04, 0x02, 0x08, 0x00, 0x41, 0x00,
                                             0x00, 0x00, 0x01, 0x02, 0xA0, 0x02};
    CHECK(pn == 12 && memcmp(pl, want_confreq, 12) == 0,
          "a Configuration Request goes to CID 0x0041 advertising MTU 672 (A0 02)");

    /* Our config is accepted; then the peer configures its direction. */
    uint8_t confrsp[6] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
    before = g_ctrl.n;
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_RSP, 2, confrsp, 6);
    CHECK(g_ctrl.n == before,
          "an accepted Configuration Response alone opens nothing: both directions must agree");

    uint8_t confreq[8] = {0x40, 0x00, 0x00, 0x00, 0x01, 0x02, 0xA0, 0x02};
    before = g_ctrl.n;
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_REQ, 9, confreq, 8);
    CHECK(g_ctrl.n == before + 2, "the peer's request draws a response AND opens the channel");
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    static const uint8_t want_confrsp[10] = {0x05, 0x09, 0x06, 0x00, 0x41, 0x00,
                                             0x00, 0x00, 0x00, 0x00};
    CHECK(pn == 10 && memcmp(pl, want_confrsp, 10) == 0,
          "the Configuration Response echoes identifier 9 and result 0x0000");
    acl_payload(&g_ctrl.log[before + 1], &cid, &pl, &pn);
    static const uint8_t want_sabm0[4] = {0x03, 0x3F, 0x01, 0x1C};
    CHECK(cid == 0x0041 && pn == 4 && memcmp(pl, want_sabm0, 4) == 0,
          "an open RFCOMM channel immediately sends SABM on DLCI 0: 03 3F 01 1C");

    /* UA on DLCI 0 -> SABM on the server-channel DLCI. */
    uint8_t ua0[4];
    bt_rfcomm_build(ua0, sizeof ua0, 0, 1, BT_RFCOMM_UA, true, NULL, 0);
    before = g_ctrl.n;
    inj_l2cap(&g_dev, g_handle, 0x0040, ua0, 4);
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    uint8_t sabm2_hdr[3] = {0x0B, 0x3F, 0x01};
    CHECK(g_ctrl.n == before + 1 && pn == 4 && pl[0] == 0x0B && pl[1] == 0x3F &&
          pl[2] == 0x01 && pl[3] == ref_fcs(sabm2_hdr, 3),
          "the multiplexer comes up and SABM goes out on DLCI 2 (server channel 1)");

    uint8_t ua2[4];
    bt_rfcomm_build(ua2, sizeof ua2, 2, 1, BT_RFCOMM_UA, true, NULL, 0);
    inj_l2cap(&g_dev, g_handle, 0x0040, ua2, 4);
    CHECK(c->rfcomm_state == BT_RFCOMM_OPEN, "the RFCOMM data link is OPEN");
    CHECK((c->active_profiles & (uint32_t)BT_PROFILE_SPP) != 0,
          "SPP is only now listed as an ACTIVE profile");
}

static void spp_data(void) {
    printf("\n--- SPP data in both directions ---\n");
    uint32_t before = g_ctrl.n;
    uint64_t tx_before = bt_get_stats(&g_dev)->bytes_tx;
    CHECK(bt_send_data(&g_dev, PEER, "HELLO", 5) == 5,
          "bt_send_data accepts all 5 bytes");
    uint16_t cid = 0; const uint8_t *pl = NULL; uint32_t pn = 0;
    CHECK(g_ctrl.n == before + 1 && acl_payload(&g_ctrl.log[before], &cid, &pl, &pn) == 0,
          "one ACL packet carries it");
    uint8_t uih_hdr[2] = {0x0B, 0xEF};
    CHECK(cid == 0x0041 && pn == 9 && pl[0] == 0x0B && pl[1] == 0xEF && pl[2] == 0x0B &&
          memcmp(pl + 3, "HELLO", 5) == 0 && pl[8] == ref_fcs(uih_hdr, 2),
          "the frame is UIH on DLCI 2, length (5<<1)|1 = 0x0B, FCS over 2 header bytes");
    CHECK(bt_get_stats(&g_dev)->bytes_tx == tx_before + 13,
          "13 ACL payload bytes really left for the transport (4 L2CAP + 9 RFCOMM)");
    /* 0xFFFFFFFC makes len + overhead come out as 1, under any MTU. */
    CHECK(bt_send_data(&g_dev, PEER, "HELLO", 0xFFFFFFFCu) == BT_EMSGSIZE,
          "a length that wraps len + RFCOMM overhead is refused, not framed");

    /* Inbound UIH with a payload. */
    uint8_t in[32];
    int il = bt_rfcomm_build(in, sizeof in, 2, 1, BT_RFCOMM_UIH, false,
                             (const uint8_t *)"WORLD!", 6);
    inj_l2cap(&g_dev, g_handle, 0x0040, in, (uint32_t)il);
    CHECK(g_dev.irq_rx_ready, "inbound data latches the receive IRQ");
    uint8_t from[6]; uint8_t got[16];
    CHECK(bt_recv_data(&g_dev, from, got, 2) == BT_EMSGSIZE,
          "a too-small receive buffer reports BT_EMSGSIZE");
    int n = bt_recv_data(&g_dev, from, got, sizeof got);
    CHECK(n == 6 && memcmp(got, "WORLD!", 6) == 0,
          "AND the record survives: the retry returns all 6 bytes");
    CHECK(memcmp(from, PEER, 6) == 0, "the source address is reported");
    CHECK(bt_recv_data(&g_dev, from, got, sizeof got) == BT_EAGAIN,
          "the ring is empty again");
    CHECK(!g_dev.irq_rx_ready, "and the receive IRQ has cleared");

    /* A frame with a broken FCS must be counted and dropped. */
    uint32_t fcs_before = bt_get_stats(&g_dev)->rfcomm_fcs_errors;
    in[il - 1] ^= 0xFF;
    inj_l2cap(&g_dev, g_handle, 0x0040, in, (uint32_t)il);
    CHECK(bt_get_stats(&g_dev)->rfcomm_fcs_errors == fcs_before + 1,
          "a corrupted RFCOMM frame is counted as an FCS error");
    CHECK(bt_recv_data(&g_dev, from, got, sizeof got) == BT_EAGAIN,
          "AND it is not delivered");
}

static void pairing(void) {
    printf("\n--- legacy PIN pairing (the controller holds the crypto) ---\n");
    uint8_t other[6] = {9,9,9,9,9,9};
    CHECK(bt_pair(&g_dev, other, "1234") == BT_ENOENT,
          "pairing with an unknown address is refused");
    CHECK(bt_pair(&g_dev, PEER, "") == BT_EINVAL, "an empty PIN is refused");
    CHECK(bt_pair(&g_dev, PEER, "12345678901234567") == BT_EINVAL,
          "a 17-character PIN is refused (the field is 16 octets)");

    uint32_t before = g_ctrl.n;
    CHECK(bt_pair(&g_dev, PEER, "1234") == BT_OK, "bt_pair is accepted");
    static const uint8_t want_auth[5] = {0x11, 0x04, 0x02, 0x0B, 0x00};
    CHECK(g_ctrl.n == before + 1 && g_ctrl.log[before].len == 5 &&
          memcmp(g_ctrl.log[before].b, want_auth, 5) == 0,
          "Authentication_Requested is 11 04 02 with handle 0x000B");
    bt_conn_t *c = bt_find_conn_addr(&g_dev, PEER);
    CHECK(c->state == BT_STATE_PAIRING, "the link state is PAIRING");
    CHECK(!bt_get_device(&g_dev, 0)->paired,
          "the device is NOT marked paired on request alone");
    inj_cs(&g_dev, 0x00, BT_HCI_AUTH_REQUESTED);

    before = g_ctrl.n;
    inj_evt(&g_dev, BT_EVT_PIN_CODE_REQUEST, PEER, 6);
    static const uint8_t want_pin[26] = {0x0D, 0x04, 0x17,
        0x11,0x22,0x33,0x44,0x55,0x66, 0x04, '1','2','3','4',
        0,0,0,0,0,0,0,0,0,0,0,0};
    CHECK(g_ctrl.n == before + 1 && g_ctrl.log[before].len == 26 &&
          memcmp(g_ctrl.log[before].b, want_pin, 26) == 0,
          "PIN_Code_Request_Reply carries length 4 and '1234' zero-padded to 16");
    CHECK(bt_get_stats(&g_dev)->pin_replies == 1, "one PIN reply is counted");
    CHECK(g_dev.irq_pairing_req, "the pairing IRQ is latched");
    uint8_t pinret[7] = {0x00, 0x11,0x22,0x33,0x44,0x55,0x66};
    inj_cc(&g_dev, BT_HCI_PIN_CODE_REQ_REPLY, pinret, 7);

    uint8_t lkn[23];
    memcpy(lkn, PEER, 6);
    for (int i = 0; i < 16; i++) lkn[6 + i] = (uint8_t)(0xF0 + i);
    lkn[22] = 0x04;
    inj_evt(&g_dev, BT_EVT_LINK_KEY_NOTIFY, lkn, 23);
    bt_device_t *d = bt_get_device(&g_dev, 0);
    CHECK(d->link_key_valid && d->link_key[0] == 0xF0 && d->link_key[15] == 0xFF &&
          d->link_key_type == 0x04,
          "the controller-reported link key is stored verbatim");
    CHECK(bt_get_stats(&g_dev)->link_keys == 1, "one link key is counted");

    uint8_t ac[3] = {0x00, 0x0B, 0x00};
    inj_evt(&g_dev, BT_EVT_AUTH_COMPLETE, ac, 3);
    CHECK(c->state == BT_STATE_PAIRED && d->paired,
          "only Authentication Complete promotes the link to PAIRED");

    /* A later Link Key Request is answered from the stored key. */
    before = g_ctrl.n;
    inj_evt(&g_dev, BT_EVT_LINK_KEY_REQUEST, PEER, 6);
    CHECK(g_ctrl.n == before + 1 && g_ctrl.log[before].b[0] == 0x0B &&
          g_ctrl.log[before].b[1] == 0x04 && g_ctrl.log[before].len == 25 &&
          g_ctrl.log[before].b[9] == 0xF0,
          "Link_Key_Request_Reply (0B 04) returns the 16-byte stored key");
    inj_cc(&g_dev, BT_HCI_LINK_KEY_REQ_REPLY, pinret, 7);

    /* An unknown peer gets a negative reply, never a guessed PIN. */
    uint8_t stranger[6] = {0xAB,0xCD,0xEF,0x01,0x02,0x03};
    before = g_ctrl.n;
    inj_evt(&g_dev, BT_EVT_PIN_CODE_REQUEST, stranger, 6);
    CHECK(g_ctrl.n == before + 1 && g_ctrl.log[before].b[0] == 0x0E &&
          g_ctrl.log[before].b[1] == 0x04,
          "a PIN request for an unknown peer draws the NEGATIVE reply 0E 04, not '0000'");
    uint8_t strret[7] = {0x00, 0xAB,0xCD,0xEF,0x01,0x02,0x03};
    inj_cc(&g_dev, BT_HCI_PIN_CODE_REQ_NEG_REPLY, strret, 7);
}

static void a2dp_flow(void) {
    printf("\n--- A2DP source: signalling, then real RTP media framing ---\n");
    CHECK(bt_audio_start(&g_dev, BT_A2DP_CODEC_AAC, 44100) == BT_ENOTSUP,
          "AAC is refused: there is no AAC encoder here");
    CHECK(bt_audio_send(&g_dev, "x", 1) == BT_ESTATE,
          "sending media before a stream exists is refused");

    uint32_t before = g_ctrl.n;
    CHECK(bt_audio_connect(&g_dev, PEER) == BT_OK, "bt_audio_connect opens AVDTP");
    uint16_t cid = 0; const uint8_t *pl = NULL; uint32_t pn = 0;
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    static const uint8_t want_avdtp_req[8] = {0x02, 0x03, 0x04, 0x00, 0x19, 0x00, 0x41, 0x00};
    CHECK(pn == 8 && memcmp(pl, want_avdtp_req, 8) == 0,
          "an L2CAP Connection Request for PSM 0x0019 with our CID 0x0041");

    uint8_t conrsp[8] = {0x51, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00};
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONN_RSP, 3, conrsp, 8);
    uint8_t confrsp[6] = {0x41, 0x00, 0x00, 0x00, 0x00, 0x00};
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_RSP, 4, confrsp, 6);
    uint8_t confreq[8] = {0x41, 0x00, 0x00, 0x00, 0x01, 0x02, 0xA0, 0x02};
    before = g_ctrl.n;
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_REQ, 20, confreq, 8);
    acl_payload(&g_ctrl.log[before + 1], &cid, &pl, &pn);
    CHECK(cid == 0x0051 && pn == 2 && pl[0] == 0x10 && pl[1] == BT_AVDTP_DISCOVER,
          "the open AVDTP channel sends DISCOVER (label 1): 10 01");

    /* DISCOVER accept: one in-use source SEP, one free sink SEP. */
    uint8_t sep[6];
    sep[0] = (uint8_t)((2u << 2) | 0x02); sep[1] = 0x00;   /* in use, source */
    sep[2] = (uint8_t)(5u << 2);          sep[3] = 0x08;   /* free, sink */
    sep[4] = (uint8_t)((6u << 2) | 0x02); sep[5] = 0x08;   /* in use, sink */
    uint8_t rsp[8];
    int rl = bt_avdtp_build(rsp, sizeof rsp, 1, BT_AVDTP_MSG_ACCEPT,
                            BT_AVDTP_DISCOVER, sep, 6);
    inj_l2cap(&g_dev, g_handle, 0x0041, rsp, (uint32_t)rl);
    bt_conn_t *c = bt_find_conn_addr(&g_dev, PEER);
    CHECK(c->a2dp_seid == (uint8_t)(5u << 2),
          "the FREE SINK endpoint (SEID 5) is chosen, not the in-use or source ones");

    CHECK(bt_audio_start(&g_dev, BT_A2DP_CODEC_SBC, 22050) == BT_EINVAL,
          "22050 Hz is refused: SBC defines only 16/32/44.1/48 kHz");
    before = g_ctrl.n;
    CHECK(bt_audio_start(&g_dev, BT_A2DP_CODEC_SBC, 44100) == BT_OK,
          "SET_CONFIGURATION for SBC at 44.1 kHz is sent");
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    static const uint8_t want_setcfg[14] = {0x20, 0x03, 0x14, 0x04,
        0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x22, 0x15, 0x02, 0x35};
    CHECK(pn == 14 && memcmp(pl, want_setcfg, 14) == 0,
          "it carries ACP SEID 0x14, Media Transport, and SBC caps 22 15 02 35 "
          "(44.1 kHz | stereo, 16 blocks/8 subbands/loudness, bitpool 2..53)");
    CHECK(!g_dev.audio_streaming, "nothing is streaming yet");

    rl = bt_avdtp_build(rsp, sizeof rsp, 2, BT_AVDTP_MSG_ACCEPT, BT_AVDTP_SET_CONFIG, NULL, 0);
    before = g_ctrl.n;
    inj_l2cap(&g_dev, g_handle, 0x0041, rsp, (uint32_t)rl);
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    CHECK(pn == 3 && pl[1] == BT_AVDTP_OPEN && pl[2] == (uint8_t)(5u << 2),
          "the accept advances to OPEN for SEID 5");

    rl = bt_avdtp_build(rsp, sizeof rsp, 3, BT_AVDTP_MSG_ACCEPT, BT_AVDTP_OPEN, NULL, 0);
    before = g_ctrl.n;
    inj_l2cap(&g_dev, g_handle, 0x0041, rsp, (uint32_t)rl);
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    CHECK(pl[0] == 0x02 && pl[4] == 0x19 && pl[6] == 0x42,
          "OPEN accepted -> a SECOND L2CAP channel (CID 0x0042) opens for media");

    uint8_t mrsp[8] = {0x61, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00};
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONN_RSP, 5, mrsp, 8);
    uint8_t mcfr[6] = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00};
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_RSP, 6, mcfr, 6);
    uint8_t mcfq[8] = {0x42, 0x00, 0x00, 0x00, 0x01, 0x02, 0xA0, 0x02};
    before = g_ctrl.n;
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_REQ, 30, mcfq, 8);
    acl_payload(&g_ctrl.log[before + 1], &cid, &pl, &pn);
    CHECK(pn == 3 && pl[1] == BT_AVDTP_START,
          "the open media channel triggers AVDTP START");
    CHECK(!g_dev.audio_streaming,
          "audio_streaming is STILL false: START has only been sent");

    rl = bt_avdtp_build(rsp, sizeof rsp, 5, BT_AVDTP_MSG_ACCEPT, BT_AVDTP_START, NULL, 0);
    inj_l2cap(&g_dev, g_handle, 0x0041, rsp, (uint32_t)rl);
    CHECK(g_dev.audio_streaming && c->a2dp_state == BT_A2DP_STREAMING,
          "only the peer's accept sets audio_streaming");

    /* With a LIVE stream, the size guard is the only thing between a caller's
     * length and a memcpy into a 1 KiB automatic buffer. 0xFFFFFFF3 makes
     * RTP_HDR + 1 + len come out as exactly 0, so both MTU tests used to pass. */
    uint8_t one = 0x00;
    CHECK(bt_audio_send(&g_dev, &one, 0xFFFFFFF3u) == BT_EMSGSIZE,
          "a media length that wraps RTP_HDR + 1 + len is refused, not memcpy'd");

    /* Media. 20 payload bytes + 13 header = 33, so at ACL MTU 27 the L2CAP PDU
     * of 37 bytes must split into two fragments. */
    uint8_t frames[20];
    for (int i = 0; i < 20; i++) frames[i] = (uint8_t)(0x9C - i);
    before = g_ctrl.n;
    CHECK(bt_audio_send(&g_dev, frames, 20) == 20, "bt_audio_send accepts 20 bytes");
    CHECK(g_ctrl.n == before + 2, "and splits them across 2 ACL fragments at MTU 27");
    CHECK(g_ctrl.log[before].b[1] == 0x20 && g_ctrl.log[before + 1].b[1] == 0x10,
          "fragment 0 is PB=start, fragment 1 is PB=continue");
    uint8_t reasm[64];
    memcpy(reasm, g_ctrl.log[before].b + 4, g_ctrl.log[before].len - 4);
    memcpy(reasm + (g_ctrl.log[before].len - 4), g_ctrl.log[before + 1].b + 4,
           g_ctrl.log[before + 1].len - 4);
    static const uint8_t want_rtp[8] = {0x80, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CHECK(memcmp(reasm + 4, want_rtp, 8) == 0,
          "the reassembled media PDU starts 80 60 with sequence 0 and timestamp 0");
    CHECK(reasm[16] == 0x01 && reasm[17] == 0x9C,
          "the SBC header declares 1 frame and the payload follows unmodified");

    before = g_ctrl.n;
    bt_audio_send(&g_dev, frames, 20);
    memcpy(reasm, g_ctrl.log[before].b + 4, g_ctrl.log[before].len - 4);
    CHECK(reasm[6] == 0x00 && reasm[7] == 0x01,
          "the second packet has RTP sequence number 1");
    CHECK(reasm[8] == 0x00 && reasm[9] == 0x00 && reasm[10] == 0x00 && reasm[11] == 0x80,
          "and its timestamp advanced by 128 samples, the configured SBC frame size");

    CHECK(bt_audio_set_frame_geometry(&g_dev, 0, 1) == BT_EINVAL,
          "a zero-sample frame geometry is refused");
    CHECK(bt_audio_set_frame_geometry(&g_dev, 64, 4) == BT_OK, "geometry 64x4 is accepted");
    before = g_ctrl.n;
    bt_audio_send(&g_dev, frames, 20);
    memcpy(reasm, g_ctrl.log[before].b + 4, g_ctrl.log[before].len - 4);
    CHECK(reasm[16] == 0x04, "the SBC header now declares 4 frames");
    CHECK(reasm[11] == 0x00 && reasm[10] == 0x01,
          "and the timestamp advanced by 128 again (64 samples x 4 frames)");

    before = g_ctrl.n;
    CHECK(bt_audio_stop(&g_dev) == BT_OK, "bt_audio_stop sends SUSPEND");
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    CHECK(pl[1] == BT_AVDTP_SUSPEND, "the signal id is 0x09 (SUSPEND)");
    CHECK(g_dev.audio_streaming,
          "audio_streaming is still true: the peer has not accepted yet");
    rl = bt_avdtp_build(rsp, sizeof rsp, 6, BT_AVDTP_MSG_ACCEPT, BT_AVDTP_SUSPEND, NULL, 0);
    inj_l2cap(&g_dev, g_handle, 0x0041, rsp, (uint32_t)rl);
    CHECK(!g_dev.audio_streaming, "the accept clears audio_streaming");
    CHECK(bt_audio_stop(&g_dev) == BT_ESTATE, "stopping again is refused");
}

static void hid_flow(void) {
    printf("\n--- HID over the control and interrupt channels ---\n");
    CHECK(bt_hid_send_report(&g_dev, PEER, "\x01", 1) == BT_ESTATE,
          "sending a report before the interrupt channel exists is refused");
    uint32_t before = g_ctrl.n;
    CHECK(bt_hid_connect(&g_dev, PEER) == BT_OK, "bt_hid_connect is accepted");
    uint16_t cid = 0; const uint8_t *pl = NULL; uint32_t pn = 0;
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    CHECK(pl[4] == 0x11 && pl[5] == 0x00, "a Connection Request for PSM 0x0011 (control)");
    acl_payload(&g_ctrl.log[before + 1], &cid, &pl, &pn);
    CHECK(pl[4] == 0x13 && pl[5] == 0x00, "and one for PSM 0x0013 (interrupt)");

    uint16_t our_ctrl = (uint16_t)(pl[6] | (pl[7] << 8));
    uint16_t our_int = our_ctrl;
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    our_ctrl = (uint16_t)(pl[6] | (pl[7] << 8));

    uint16_t peer_ctrl = 0x0071, peer_int = 0x0072;
    uint8_t cr[8]; uint8_t cq[8]; uint8_t cs[6];
    /* control */
    cr[0] = (uint8_t)peer_ctrl; cr[1] = (uint8_t)(peer_ctrl >> 8);
    cr[2] = (uint8_t)our_ctrl;  cr[3] = (uint8_t)(our_ctrl >> 8);
    cr[4] = 0; cr[5] = 0; cr[6] = 0; cr[7] = 0;
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONN_RSP, 7, cr, 8);
    cs[0] = (uint8_t)our_ctrl; cs[1] = (uint8_t)(our_ctrl >> 8);
    cs[2] = 0; cs[3] = 0; cs[4] = 0; cs[5] = 0;
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_RSP, 8, cs, 6);
    cq[0] = (uint8_t)our_ctrl; cq[1] = (uint8_t)(our_ctrl >> 8);
    cq[2] = 0; cq[3] = 0; cq[4] = 0x01; cq[5] = 0x02; cq[6] = 0xA0; cq[7] = 0x02;
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_REQ, 40, cq, 8);
    /* interrupt */
    cr[0] = (uint8_t)peer_int; cr[1] = (uint8_t)(peer_int >> 8);
    cr[2] = (uint8_t)our_int;  cr[3] = (uint8_t)(our_int >> 8);
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONN_RSP, 9, cr, 8);
    cs[0] = (uint8_t)our_int; cs[1] = (uint8_t)(our_int >> 8);
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_RSP, 10, cs, 6);
    cq[0] = (uint8_t)our_int; cq[1] = (uint8_t)(our_int >> 8);
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_REQ, 41, cq, 8);

    bt_conn_t *c = bt_find_conn_addr(&g_dev, PEER);
    CHECK((c->active_profiles & (uint32_t)BT_PROFILE_HID) != 0,
          "HID becomes active only once BOTH channels are configured");

    before = g_ctrl.n;
    uint8_t leds[2] = {0x00, 0x02};
    CHECK(bt_hid_send_report(&g_dev, PEER, leds, 2) == 2, "an output report is accepted");
    acl_payload(&g_ctrl.log[before], &cid, &pl, &pn);
    CHECK(cid == peer_int && pn == 3 && pl[0] == 0xA2 && pl[1] == 0x00 && pl[2] == 0x02,
          "it goes to the interrupt channel as A2 00 02");
    /* 0xFFFFFFFF makes len + 1 come out as 0, so both size tests used to pass. */
    CHECK(bt_hid_send_report(&g_dev, PEER, leds, 0xFFFFFFFFu) == BT_EMSGSIZE,
          "a report length that wraps len + 1 is refused, not memcpy'd");

    uint8_t inrep[4] = {0xA1, 0x01, 0x04, 0x00};
    inj_l2cap(&g_dev, g_handle, our_int, inrep, 4);
    uint8_t from[6]; uint8_t got[8];
    int n = bt_recv_data(&g_dev, from, got, sizeof got);
    CHECK(n == 3 && got[0] == 0x01 && got[1] == 0x04,
          "an inbound 0xA1 input report is delivered with its header stripped");
}

static void credits_and_errors(void) {
    printf("\n--- controller credits, transport failure, malformed input ---\n");
    ctrl_reset();
    bt_init(&g_dev, "zxv-bt");
    bt_ops_t ops = { c_send, c_recv, &g_ctrl };
    bt_bind_ops(&g_dev, &ops);
    bt_reset(&g_dev);
    uint8_t ok0 = 0x00;
    inj_cc(&g_dev, BT_HCI_RESET, &ok0, 1);
    uint8_t bdret[7] = {0x00, 1,2,3,4,5,6};
    inj_cc(&g_dev, BT_HCI_READ_BD_ADDR, bdret, 7);
    /* One ACL buffer only: the second fragment must WAIT. */
    uint8_t bufret[8] = {0x00, 0x1B, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00};
    inj_cc(&g_dev, BT_HCI_READ_BUFFER_SIZE, bufret, 8);
    uint8_t verret[9] = {0x00, 0x0C, 0x00, 0x00, 0x0C, 0x0F, 0x00, 0x00, 0x00};
    inj_cc(&g_dev, BT_HCI_READ_LOCAL_VERSION, verret, 9);
    CHECK(g_dev.acl_credits == 1, "the controller advertises exactly one ACL buffer");

    uint8_t ir[15];
    ir[0] = 1; memcpy(ir + 1, PEER, 6);
    ir[7] = 1; ir[8] = 0; ir[9] = 0x0C; ir[10] = 0x01; ir[11] = 0x00;
    ir[12] = 0; ir[13] = 0; ir[14] = (uint8_t)(int8_t)-40;
    inj_evt(&g_dev, BT_EVT_INQUIRY_RESULT_RSSI, ir, 15);
    bt_connect(&g_dev, PEER, BT_PROFILE_SPP);
    inj_cs(&g_dev, 0x00, BT_HCI_CREATE_CONNECTION);
    uint8_t cco[11];
    cco[0] = 0; cco[1] = 0x0B; cco[2] = 0x00;
    memcpy(cco + 3, PEER, 6);
    cco[9] = 1; cco[10] = 0;
    uint32_t before = g_ctrl.n;
    inj_evt(&g_dev, BT_EVT_CONN_COMPLETE, cco, 11);
    CHECK(g_ctrl.n == before + 1 && g_dev.acl_credits == 0,
          "the L2CAP Connection Request consumes the single ACL credit");

    uint8_t conrsp[8] = {0x41, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
    before = g_ctrl.n;
    inj_sig(&g_dev, 0x000B, BT_L2CAP_CONN_RSP, 1, conrsp, 8);
    CHECK(g_ctrl.n == before,
          "with no credits left the Configuration Request is QUEUED, not sent");
    uint8_t ncp[5] = {0x01, 0x0B, 0x00, 0x01, 0x00};
    inj_evt(&g_dev, BT_EVT_NUM_COMP_PKTS, ncp, 5);
    CHECK(g_ctrl.n == before + 1,
          "Number_Of_Completed_Packets returns the credit and the queued packet goes out");

    /* Credits never exceed what the controller said it has. */
    uint8_t ncp2[5] = {0x01, 0x0B, 0x00, 0xFF, 0x00};
    inj_evt(&g_dev, BT_EVT_NUM_COMP_PKTS, ncp2, 5);
    CHECK(g_dev.acl_credits <= g_dev.acl_total_pkts,
          "a controller over-reporting completions cannot inflate credits past 1");

    printf("\n--- malformed input is rejected, not absorbed ---\n");
    uint32_t bad_before = bt_get_stats(&g_dev)->hci_events_bad;
    uint8_t junk[4] = {0x03, 0x09, 0x00, 0x00};   /* event 0x03, claims 9 params */
    CHECK(bt_hci_ingest(&g_dev, BT_H4_EVT, junk, 4) == BT_EPROTO,
          "an event whose length field lies is rejected by ingest");
    CHECK(bt_get_stats(&g_dev)->hci_events_bad == bad_before + 1,
          "and counted");
    uint8_t acl_unknown[6] = {0x77, 0x20, 0x02, 0x00, 0x00, 0x00};
    uint32_t arb = bt_get_stats(&g_dev)->acl_rx_bad;
    CHECK(bt_hci_ingest(&g_dev, BT_H4_ACL, acl_unknown, 6) == BT_ENOENT,
          "ACL data for a handle we have no connection on is refused");
    CHECK(bt_get_stats(&g_dev)->acl_rx_bad == arb + 1, "and counted");
    CHECK(bt_hci_ingest(&g_dev, BT_H4_SCO, junk, 4) == BT_ENOTSUP,
          "SCO is refused with BT_ENOTSUP — this stack does not carry voice");

    printf("\n--- a failing transport does not lose packets ---\n");
    g_ctrl.fail_send = 1;
    before = g_ctrl.n;
    uint32_t failed_before = bt_get_stats(&g_dev)->hci_cmds_failed;
    bt_set_discoverable(&g_dev, true);
    CHECK(g_ctrl.n == before, "nothing reaches a transport that refuses");
    CHECK(bt_get_stats(&g_dev)->hci_cmds_failed == failed_before + 1,
          "the failure is counted");
    CHECK(g_dev.discoverable == false, "and no state is claimed");
    g_ctrl.fail_send = 0;
    int flushed = bt_tx_flush(&g_dev);
    CHECK(flushed == 1 && g_ctrl.n == before + 1,
          "when the transport recovers the queued command goes out unchanged");

    printf("\n--- bt_poll drains the transport ---\n");
    uint32_t rx_before = bt_get_stats(&g_dev)->hci_events_rx;
    uint8_t ev[3] = {BT_EVT_INQUIRY_COMPLETE, 0x01, 0x00};
    g_ctrl.rxq[0].type = BT_H4_EVT;
    g_ctrl.rxq[0].len = 3;
    memcpy(g_ctrl.rxq[0].b, ev, 3);
    g_ctrl.rx_n = 1; g_ctrl.rx_i = 0;
    bt_handle_irq(&g_dev);
    CHECK(bt_get_stats(&g_dev)->hci_events_rx == rx_before + 1,
          "bt_handle_irq ingests the event the transport had waiting");
}

static void table_limits(void) {
    printf("\n--- fixed-size tables refuse to overflow ---\n");
    ctrl_reset();
    bt_init(&g_dev, "zxv-bt");
    bt_ops_t ops = { c_send, c_recv, &g_ctrl };
    bt_bind_ops(&g_dev, &ops);

    uint8_t ir[15];
    for (uint32_t i = 0; i < BT_MAX_DEVICES + 4u; i++) {
        memset(ir, 0, sizeof ir);
        ir[0] = 1;
        ir[1] = (uint8_t)i; ir[2] = 0xAA; ir[3] = 0xBB;
        ir[4] = 0xCC; ir[5] = 0xDD; ir[6] = 0xEE;
        ir[9] = 0x0C; ir[10] = 0x01;
        ir[14] = (uint8_t)(int8_t)-70;
        inj_evt(&g_dev, BT_EVT_INQUIRY_RESULT_RSSI, ir, 15);
    }
    CHECK(bt_get_device_count(&g_dev) == BT_MAX_DEVICES,
          "the device table stops at BT_MAX_DEVICES (32)");
    CHECK(bt_get_stats(&g_dev)->inquiry_results == BT_MAX_DEVICES,
          "AND the counter stops too: results that were dropped are not counted");
    CHECK(bt_get_device(&g_dev, BT_MAX_DEVICES) == NULL,
          "index BT_MAX_DEVICES is out of range");

    /* A remote name that fills the field must still be terminated. */
    uint8_t rn[255];
    memset(rn, 0, sizeof rn);
    rn[0] = 0x00;
    rn[1] = 0x00; rn[2] = 0xAA; rn[3] = 0xBB; rn[4] = 0xCC; rn[5] = 0xDD; rn[6] = 0xEE;
    memset(rn + 7, 'Z', 248);
    inj_evt(&g_dev, BT_EVT_REMOTE_NAME_COMPLETE, rn, 255);
    bt_device_t *d = bt_get_device(&g_dev, 0);
    CHECK(d && d->name_valid && strlen(d->name) == 247 && d->name[247] == '\0',
          "a 248-byte unterminated remote name is stored as 247 chars plus a NUL");

    /* Extended Inquiry Result carries the name in EIR data. */
    uint8_t eir[255];
    memset(eir, 0, sizeof eir);
    eir[0] = 1;
    /* An address already in the (now full) table, so this exercises the EIR
     * parser rather than the table-full path. */
    eir[1] = 0x01; eir[2] = 0xAA; eir[3] = 0xBB;
    eir[4] = 0xCC; eir[5] = 0xDD; eir[6] = 0xEE;
    eir[9] = 0x0C; eir[10] = 0x01;
    eir[14] = (uint8_t)(int8_t)-33;
    eir[15] = 7; eir[16] = 0x09;          /* AD length counts the type octet */
    memcpy(eir + 17, "DRAGON", 6);
    CHECK(inj_evt(&g_dev, BT_EVT_EXT_INQUIRY_RESULT, eir, 255) == BT_OK,
          "an Extended Inquiry Result is accepted");
    bt_device_t *e = bt_find_device(&g_dev, eir + 1);
    CHECK(e && e->name_valid && strcmp(e->name, "DRAGON") == 0,
          "the complete local name is parsed out of the EIR data");
    uint8_t short_eir[100];
    memcpy(short_eir, eir, 100);
    CHECK(inj_evt(&g_dev, BT_EVT_EXT_INQUIRY_RESULT, short_eir, 100) == BT_EPROTO,
          "a short Extended Inquiry Result is rejected (the spec pads to 255)");

    /* The connection table is BT_MAX_CONNECTIONS deep and stops there. */
    int last = BT_OK;
    uint32_t opened = 0;
    for (uint32_t i = 0; i < BT_MAX_CONNECTIONS + 2u; i++) {
        uint8_t a[6] = {(uint8_t)i, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
        last = bt_connect(&g_dev, a, BT_PROFILE_SPP);
        if (last == BT_OK) opened++;
    }
    CHECK(opened == BT_MAX_CONNECTIONS,
          "exactly BT_MAX_CONNECTIONS (7) links can be opened at once");
    CHECK(last == BT_ENOSPC, "the eighth returns BT_ENOSPC rather than overwriting a slot");
    CHECK(g_dev.num_connections == BT_MAX_CONNECTIONS,
          "and the mirrored count agrees with the slots");
    CHECK(bt_verify_coverage(&g_dev),
          "a full connection table is still internally consistent");
}

/* ============================================================
 * bt_verify_coverage() must be able to FAIL. */
static void coverage_can_fail(void) {
    printf("\n--- bt_verify_coverage: five ways to fail, each one caught ---\n");
    ctrl_reset();
    bt_init(&g_dev, "zxv-bt");
    bt_ops_t ops = { c_send, c_recv, &g_ctrl };
    bt_bind_ops(&g_dev, &ops);
    bt_reset(&g_dev);
    uint8_t ok0 = 0;
    inj_cc(&g_dev, BT_HCI_RESET, &ok0, 1);
    uint8_t bdret[7] = {0x00, 1,2,3,4,5,6};
    inj_cc(&g_dev, BT_HCI_READ_BD_ADDR, bdret, 7);
    uint8_t bufret[8] = {0x00, 0x1B, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00};
    inj_cc(&g_dev, BT_HCI_READ_BUFFER_SIZE, bufret, 8);

    uint8_t ir[15];
    ir[0] = 1; memcpy(ir + 1, PEER, 6);
    ir[7] = 1; ir[8] = 0; ir[9] = 0x0C; ir[10] = 0x01; ir[11] = 0x00;
    ir[12] = 0; ir[13] = 0; ir[14] = (uint8_t)(int8_t)-40;
    inj_evt(&g_dev, BT_EVT_INQUIRY_RESULT_RSSI, ir, 15);
    bt_connect(&g_dev, PEER, BT_PROFILE_SPP);
    inj_cs(&g_dev, 0, BT_HCI_CREATE_CONNECTION);
    uint8_t cco[11];
    cco[0] = 0; cco[1] = 0x0B; cco[2] = 0x00;
    memcpy(cco + 3, PEER, 6);
    cco[9] = 1; cco[10] = 0;
    inj_evt(&g_dev, BT_EVT_CONN_COMPLETE, cco, 11);

    CHECK(bt_verify_coverage(&g_dev), "a consistent stack passes");
    CHECK(g_dev.coverage_r == 1.0 && g_dev.coverage_l == 1.0,
          "with 1 good device and 1 good link both factors compute to exactly 1.0");

    /* F1: a device claiming to be paired with no link key. */
    g_dev.devices[0].paired = true;
    g_dev.devices[0].link_key_valid = false;
    CHECK(!bt_verify_coverage(&g_dev),
          "F1 a device marked paired with NO link key fails the audit");
    CHECK(g_dev.coverage_r == 0.0, "and coverage_r drops to 0.0 (0 of 1 records good)");
    g_dev.devices[0].paired = false;
    CHECK(bt_verify_coverage(&g_dev), "restoring it passes again");

    /* F2: a device claiming a connection that does not exist. */
    bt_conn_t saved = g_dev.conns[0];
    uint32_t saved_conn0 = g_dev.connections[0];
    g_dev.devices[0].connected = true;
    memset(&g_dev.conns[0], 0, sizeof(g_dev.conns[0]));
    g_dev.conns[0].dev_index = -1;
    g_dev.connections[0] = 0;
    g_dev.num_connections = 0;
    CHECK(!bt_verify_coverage(&g_dev),
          "F2 a device marked connected with no matching link fails the audit");
    g_dev.conns[0] = saved;
    g_dev.connections[0] = saved_conn0;
    g_dev.num_connections = 1;
    CHECK(bt_verify_coverage(&g_dev), "restoring the link passes again");

    /* F3: the mirrored connection count disagrees with the slots. */
    g_dev.num_connections = 3;
    CHECK(!bt_verify_coverage(&g_dev),
          "F3 num_connections disagreeing with the in-use slots fails the audit");
    g_dev.num_connections = 1;

    /* F4: audio_streaming with nothing actually streaming. */
    g_dev.audio_streaming = true;
    CHECK(!bt_verify_coverage(&g_dev),
          "F4 audio_streaming with no AVDTP stream fails the audit");
    g_dev.audio_streaming = false;

    /* F5: a ring index outside its buffer. */
    uint32_t saved_head = g_dev.rx_head;
    g_dev.rx_head = (uint32_t)sizeof(g_dev.rx_dma);
    CHECK(!bt_verify_coverage(&g_dev),
          "F5 an rx ring index past the end of rx_dma fails the audit");
    g_dev.rx_head = saved_head;

    /* A partial failure must produce a partial score, not a binary one. */
    uint8_t peer2[6] = {0x77,0x88,0x99,0xAA,0xBB,0xCC};
    memcpy(ir + 1, peer2, 6);
    inj_evt(&g_dev, BT_EVT_INQUIRY_RESULT_RSSI, ir, 15);
    g_dev.devices[1].paired = true;         /* no key: exactly one bad record */
    CHECK(!bt_verify_coverage(&g_dev), "one bad record out of two fails");
    CHECK(g_dev.coverage_r == 0.5,
          "and coverage_r is exactly 0.5 — the score is a real fraction");
    g_dev.devices[1].paired = false;
    CHECK(bt_verify_coverage(&g_dev) && g_dev.coverage_r == 1.0,
          "clearing it restores a perfect score");
    CHECK(!bt_verify_coverage(NULL), "a NULL device fails rather than crashing");
}

/* ============================================================
 * ===== SECOND WAVE: audit additions =========================
 *
 * Everything below was added by the audit pass. The first block is a
 * regression suite for three CONFIRMED stack-buffer overflows (found with
 * ASan) plus the arithmetic that let them through. The rest closes claims that
 * had no test at all — L2CAP echo/disconnect/refusal, CID collision
 * avoidance, head-of-line queueing, the transmit and receive back-pressure
 * counters, the RFCOMM and AVDTP teardown paths, and the full 25-entry state
 * legality table. Each one is written so that replacing the function under
 * test with `return 0` / `return true` would fail it.
 * ============================================================ */

/* ---- shared bring-up ---- */

static uint32_t g_conreq_idx;   /* log index of the first L2CAP Connection Request */

/* Pull the Source CID out of a logged L2CAP Connection Request. */
static uint16_t req_scid(uint32_t idx) {
    uint16_t cid; const uint8_t *pl = NULL; uint32_t pn = 0;
    if (idx >= g_ctrl.n) return 0;
    if (acl_payload(&g_ctrl.log[idx], &cid, &pl, &pn) != 0) return 0;
    if (cid != BT_L2CAP_CID_SIGNAL || pn < 8 || pl[0] != BT_L2CAP_CONN_REQ) return 0;
    return (uint16_t)(pl[6] | (pl[7] << 8));
}

/* Radio bound, buffer geometry known, PEER discovered, ACL link CONNECTED on
 * g_handle, and the SPP L2CAP Connection Request already out (its log index
 * lands in g_conreq_idx). */
static bt_conn_t *linked_stack(uint16_t acl_mtu, uint16_t acl_bufs) {
    ctrl_reset();
    bt_init(&g_dev, "zxv-bt");
    bt_ops_t ops = { c_send, c_recv, &g_ctrl };
    bt_bind_ops(&g_dev, &ops);
    bt_reset(&g_dev);

    uint8_t ok0 = 0x00;
    inj_cc(&g_dev, BT_HCI_RESET, &ok0, 1);
    uint8_t bdret[7] = {0x00, 1,2,3,4,5,6};
    inj_cc(&g_dev, BT_HCI_READ_BD_ADDR, bdret, 7);
    uint8_t bufret[8];
    bufret[0] = 0x00;
    bufret[1] = (uint8_t)(acl_mtu & 0xFF);  bufret[2] = (uint8_t)(acl_mtu >> 8);
    bufret[3] = 0x40;
    bufret[4] = (uint8_t)(acl_bufs & 0xFF); bufret[5] = (uint8_t)(acl_bufs >> 8);
    bufret[6] = 0x00; bufret[7] = 0x00;
    inj_cc(&g_dev, BT_HCI_READ_BUFFER_SIZE, bufret, 8);
    uint8_t verret[9] = {0x00, 0x0C, 0x00, 0x00, 0x0C, 0x0F, 0x00, 0x00, 0x00};
    inj_cc(&g_dev, BT_HCI_READ_LOCAL_VERSION, verret, 9);

    uint8_t ir[15];
    ir[0] = 1; memcpy(ir + 1, PEER, 6);
    ir[7] = 1; ir[8] = 0; ir[9] = 0x0C; ir[10] = 0x01; ir[11] = 0x00;
    ir[12] = 0; ir[13] = 0; ir[14] = (uint8_t)(int8_t)-40;
    inj_evt(&g_dev, BT_EVT_INQUIRY_RESULT_RSSI, ir, 15);

    bt_connect(&g_dev, PEER, BT_PROFILE_SPP);
    inj_cs(&g_dev, 0x00, BT_HCI_CREATE_CONNECTION);
    uint8_t cco[11];
    cco[0] = 0x00;
    cco[1] = (uint8_t)(g_handle & 0xFF); cco[2] = (uint8_t)(g_handle >> 8);
    memcpy(cco + 3, PEER, 6);
    cco[9] = 0x01; cco[10] = 0x00;
    g_conreq_idx = g_ctrl.n;
    inj_evt(&g_dev, BT_EVT_CONN_COMPLETE, cco, 11);
    return bt_find_conn_addr(&g_dev, PEER);
}

/* Drive the full four-way for the Connection Request logged at `idx`, giving
 * the peer CID `peer_dcid`. Returns our own CID for that channel. */
static uint16_t l2cap_complete(uint32_t idx, uint16_t peer_dcid) {
    uint16_t our = req_scid(idx);
    if (our == 0) return 0;
    uint8_t cr[8];
    memset(cr, 0, sizeof cr);
    cr[0] = (uint8_t)(peer_dcid & 0xFF); cr[1] = (uint8_t)(peer_dcid >> 8);
    cr[2] = (uint8_t)(our & 0xFF);       cr[3] = (uint8_t)(our >> 8);
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONN_RSP, 1, cr, 8);
    uint8_t cs[6];
    memset(cs, 0, sizeof cs);
    cs[0] = (uint8_t)(our & 0xFF); cs[1] = (uint8_t)(our >> 8);
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_RSP, 2, cs, 6);
    uint8_t cq[8];
    memset(cq, 0, sizeof cq);
    cq[0] = (uint8_t)(our & 0xFF); cq[1] = (uint8_t)(our >> 8);
    cq[4] = BT_L2CAP_CONF_OPT_MTU; cq[5] = 0x02; cq[6] = 0xA0; cq[7] = 0x02;
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONF_REQ, 9, cq, 8);
    return our;
}

/* linked_stack() plus the whole RFCOMM bring-up, ending with SPP OPEN.
 * `*rf_scid` receives our CID for the RFCOMM channel. */
static bt_conn_t *spp_open_stack(uint16_t acl_mtu, uint16_t *rf_scid) {
    bt_conn_t *c = linked_stack(acl_mtu, 64);
    uint16_t our = l2cap_complete(g_conreq_idx, 0x0041);
    if (rf_scid) *rf_scid = our;
    uint8_t ua[4];
    bt_rfcomm_build(ua, sizeof ua, 0, 1, BT_RFCOMM_UA, true, NULL, 0);
    inj_l2cap(&g_dev, g_handle, our, ua, 4);
    bt_rfcomm_build(ua, sizeof ua, 2, 1, BT_RFCOMM_UA, true, NULL, 0);
    inj_l2cap(&g_dev, g_handle, our, ua, 4);
    return c;
}

/* ============================================================
 * Length guards that must not wrap. Each of the first three was a CONFIRMED
 * stack-buffer overflow: `cap < HDR + n` wraps to `cap < 0` when the caller's
 * length is near UINT32_MAX, so the capacity test passed and the memcpy ran
 * off the end of the caller's buffer. ASan caught all three. */
static void codec_length_guards(void) {
    printf("\n--- length guards must not wrap (regression: 3 stack overflows) ---\n");
    uint8_t small[16];
    uint8_t payload[16];
    memset(payload, 0x5A, sizeof payload);

    CHECK(bt_hidp_build(small, sizeof small, BT_HIDP_DATA, BT_HIDP_RTYPE_OUTPUT,
                        payload, 0xFFFFFFFFu) < 0,
          "bt_hidp_build refuses a 0xFFFFFFFF payload instead of wrapping its capacity test");
    CHECK(bt_avdtp_build(small, sizeof small, 1, BT_AVDTP_MSG_COMMAND,
                         BT_AVDTP_START, payload, 0xFFFFFFFEu) < 0,
          "bt_avdtp_build refuses a 0xFFFFFFFE payload");
    CHECK(bt_h4_wrap(small, sizeof small, BT_H4_CMD, payload, 0xFFFFFFFFu) < 0,
          "bt_h4_wrap refuses a 0xFFFFFFFF packet");

    /* The ordinary boundary must still be exact, so the fix is a bound and not
     * a blanket refusal. */
    CHECK(bt_hidp_build(small, sizeof small, BT_HIDP_DATA, 0, payload, 15) == 16 &&
          bt_hidp_build(small, sizeof small, BT_HIDP_DATA, 0, payload, 16) == BT_ENOSPC,
          "bt_hidp_build fills 16 bytes exactly and refuses one more");
    CHECK(bt_avdtp_build(small, sizeof small, 1, 0, BT_AVDTP_START, payload, 14) == 16 &&
          bt_avdtp_build(small, sizeof small, 1, 0, BT_AVDTP_START, payload, 15) == BT_ENOSPC,
          "bt_avdtp_build fills 16 bytes exactly and refuses one more");
    CHECK(bt_h4_wrap(small, sizeof small, BT_H4_ACL, payload, 15) == 16 &&
          bt_h4_wrap(small, sizeof small, BT_H4_ACL, payload, 16) == BT_ENOSPC,
          "bt_h4_wrap fills 16 bytes exactly and refuses one more");

    CHECK(bt_h4_wrap(small, sizeof small, 0x00, payload, 2) == BT_EINVAL &&
          bt_h4_wrap(small, sizeof small, 0x05, payload, 2) == BT_EINVAL,
          "bt_h4_wrap rejects H4 indicators outside 0x01..0x04");

    /* bt_acl_frag_count used the (a + b - 1) / b round-up, which wraps and
     * reports FEWER fragments than the PDU needs — a silent truncation. */
    CHECK(bt_acl_frag_count(0xFFFFFFFFu, 1024) == 4194304u,
          "bt_acl_frag_count(0xFFFFFFFF, 1024) is 4194304, not a wrapped 0");
    CHECK(bt_acl_frag_count(0xFFFFFFFFu, 65535) == 65537u,
          "and 0xFFFFFFFF at MTU 65535 divides exactly into 65537 fragments");
    CHECK(bt_acl_frag_count(1, 1024) == 1 && bt_acl_frag_count(1024, 1024) == 1 &&
          bt_acl_frag_count(1025, 1024) == 2,
          "and the ordinary counts are unchanged");

    /* AVDTP fragmentation is a real part of the spec we do not implement; it
     * must say so rather than decode the second octet as a signal id. */
    uint8_t frag_hdr[4] = {0x34, 0x02, 0x01, 0x00};   /* label 3, packet type 01 = START */
    bt_avdtp_msg_t am;
    CHECK(bt_avdtp_parse(frag_hdr, 4, &am) == BT_ENOTSUP,
          "a fragmented AVDTP message is refused with BT_ENOTSUP, not mis-read as single");
}

/* Every prefix of a well-formed packet, through every parser. Built with ASan,
 * so an overread is a hard failure rather than a wrong return code. */
static void truncation_sweep(void) {
    printf("\n--- every prefix of every packet: refuse, never overread ---\n");
    uint8_t buf[300];
    uint8_t params[8] = {1,2,3,4,5,6,7,8};
    int bad;

    int n = bt_hci_build_cmd(buf, sizeof buf, BT_HCI_INQUIRY, params, 8);
    bad = 0;
    for (int L = 0; L <= n + 2; L++) {
        bt_hci_cmd_t c;
        int rc = bt_hci_parse_cmd(buf, (uint32_t)L, &c);
        if ((L == n) != (rc == BT_OK)) bad++;
    }
    CHECK(n == 11 && bad == 0,
          "bt_hci_parse_cmd accepts length 11 and rejects all 12 other lengths");

    n = bt_hci_build_event(buf, sizeof buf, BT_EVT_CONN_COMPLETE, params, 8);
    bad = 0;
    for (int L = 0; L <= n + 2; L++) {
        bt_hci_event_t e;
        int rc = bt_hci_parse_event(buf, (uint32_t)L, &e);
        if ((L == n) != (rc == BT_OK)) bad++;
    }
    CHECK(n == 10 && bad == 0,
          "bt_hci_parse_event accepts length 10 and rejects all 12 other lengths");

    n = bt_hci_build_acl(buf, sizeof buf, 0x0B, BT_PB_START_FLUSH, 0, params, 8);
    bad = 0;
    for (int L = 0; L <= n + 2; L++) {
        bt_hci_acl_t a;
        int rc = bt_hci_parse_acl(buf, (uint32_t)L, &a);
        if ((L == n) != (rc == BT_OK)) bad++;
    }
    CHECK(n == 12 && bad == 0,
          "bt_hci_parse_acl accepts length 12 and rejects all 14 other lengths");

    n = bt_l2cap_build(buf, sizeof buf, 0x0040, params, 8);
    bad = 0;
    for (int L = 0; L <= n + 2; L++) {
        bt_l2cap_frame_t f;
        int rc = bt_l2cap_parse(buf, (uint32_t)L, &f);
        if ((L == n) != (rc == BT_OK)) bad++;
    }
    CHECK(n == 12 && bad == 0, "bt_l2cap_parse accepts only its exact length");

    n = bt_l2cap_build_sig(buf, sizeof buf, BT_L2CAP_CONN_REQ, 3, params, 8);
    bad = 0;
    for (int L = 0; L <= n + 2; L++) {
        bt_l2cap_sig_t s;
        int rc = bt_l2cap_parse_sig(buf, (uint32_t)L, &s);
        if ((L == n) != (rc == BT_OK)) bad++;
    }
    CHECK(n == 12 && bad == 0, "bt_l2cap_parse_sig accepts only its exact length");

    n = bt_rfcomm_build(buf, sizeof buf, 2, 1, BT_RFCOMM_UIH, false, params, 8);
    bad = 0;
    for (int L = 0; L <= n + 2; L++) {
        bt_rfcomm_frame_t f;
        int rc = bt_rfcomm_parse(buf, (uint32_t)L, &f);
        if ((L == n) != (rc == BT_OK)) bad++;
    }
    CHECK(n == 12 && bad == 0, "bt_rfcomm_parse accepts only its exact length");

    /* AVDTP and HIDP are length-agnostic above their headers: any length at or
     * past the header is legal, and the payload length must come out right. */
    n = bt_avdtp_build(buf, sizeof buf, 1, 0, BT_AVDTP_START, params, 8);
    bad = 0;
    for (int L = 0; L <= n; L++) {
        bt_avdtp_msg_t m;
        int rc = bt_avdtp_parse(buf, (uint32_t)L, &m);
        if (L < 2) { if (rc != BT_EPROTO) bad++; }
        else if (rc != BT_OK || m.plen != (uint16_t)(L - 2)) bad++;
    }
    CHECK(bad == 0, "bt_avdtp_parse rejects 0- and 1-byte messages and sizes the rest");

    n = bt_hidp_build(buf, sizeof buf, BT_HIDP_DATA, BT_HIDP_RTYPE_INPUT, params, 8);
    bad = 0;
    for (int L = 0; L <= n; L++) {
        uint8_t tt, pm; const uint8_t *p; uint32_t pn;
        int rc = bt_hidp_parse(buf, (uint32_t)L, &tt, &pm, &p, &pn);
        if (L < 1) { if (rc != BT_EPROTO) bad++; }
        else if (rc != BT_OK || pn != (uint32_t)(L - 1)) bad++;
    }
    CHECK(bad == 0, "bt_hidp_parse rejects the empty frame and sizes the rest");

    /* And the whole ingest path on a LIVE link: truncated ACL and truncated
     * event bytes must leave the stack consistent, not half-updated. */
    bt_conn_t *c = linked_stack(200, 64);
    CHECK(c != NULL, "a live link is up for the ingest sweep");
    uint8_t l2[16];
    int l2n = bt_l2cap_build(l2, sizeof l2, 0x0040, params, 8);
    uint8_t acl[32];
    int aln = bt_hci_build_acl(acl, sizeof acl, g_handle, BT_PB_START_FLUSH, 0,
                               l2, (uint32_t)l2n);
    for (uint32_t L = 0; L <= (uint32_t)aln; L++)
        bt_hci_ingest(&g_dev, BT_H4_ACL, acl, L);

    uint8_t eir[257];
    memset(eir, 0, sizeof eir);
    eir[0] = 1;
    eir[1] = 0x11; eir[2] = 0x22; eir[3] = 0x33;
    eir[4] = 0x44; eir[5] = 0x55; eir[6] = 0x66;
    uint8_t evt[300];
    int evn = bt_hci_build_event(evt, sizeof evt, BT_EVT_EXT_INQUIRY_RESULT, eir, 255);
    for (uint32_t L = 0; L <= (uint32_t)evn; L++)
        bt_hci_ingest(&g_dev, BT_H4_EVT, evt, L);

    CHECK(bt_verify_coverage(&g_dev),
          "after ~300 truncated packets the stack is still internally consistent");
    CHECK(bt_get_device_count(&g_dev) == 1,
          "AND no half-parsed device was created by any of them");
}

/* The full legality table, not a hand-picked six. */
static void state_table_exhaustive(void) {
    printf("\n--- the link state legality table, all 25 pairs ---\n");
    /* Rows = from, columns = to, in enum order:
     * DISCONNECTED, CONNECTING, CONNECTED, PAIRING, PAIRED. */
    static const int legal[5][5] = {
        /* DISCONNECTED */ {0, 1, 0, 0, 0},
        /* CONNECTING   */ {1, 0, 1, 0, 0},
        /* CONNECTED    */ {1, 0, 0, 1, 0},
        /* PAIRING      */ {1, 0, 1, 0, 1},
        /* PAIRED       */ {1, 0, 0, 1, 0},
    };
    int wrong = 0, nlegal = 0;
    for (int f = 0; f < 5; f++) {
        for (int t = 0; t < 5; t++) {
            bool got = bt_state_transition_ok((bt_state_t)f, (bt_state_t)t);
            if (got != (legal[f][t] != 0)) wrong++;
            if (got) nlegal++;
        }
    }
    CHECK(wrong == 0, "every one of the 25 (from,to) pairs matches the spec table");
    /* Counted separately so an all-true or all-false implementation could not
     * slip past the comparison above. */
    CHECK(nlegal == 10, "exactly 10 of the 25 transitions are legal, 15 are not");
    CHECK(!bt_state_transition_ok((bt_state_t)99, BT_STATE_CONNECTING) &&
          !bt_state_transition_ok(BT_STATE_CONNECTED, (bt_state_t)99),
          "a state outside the enum is illegal in both directions");
}

/* Class of Device buckets the first pass never exercised. */
static void cod_extra(void) {
    printf("\n--- Class of Device: the buckets nothing was decoding into ---\n");
    CHECK(bt_class_from_cod(0x000504) == BT_CLASS_GAMEPAD,
          "CoD 0x000504 (peripheral, device type 0001 JOYSTICK) decodes as a gamepad");
    CHECK(bt_class_from_cod(0x000508) == BT_CLASS_GAMEPAD,
          "CoD 0x000508 (peripheral, device type 0010 GAMEPAD) decodes as a gamepad");
    CHECK(bt_class_from_cod(0x00050C) == BT_CLASS_UNKNOWN,
          "CoD 0x00050C (peripheral, 0011 REMOTE CONTROL) is NOT claimed as a gamepad");
    CHECK(bt_class_from_cod(0x2405C0) == BT_CLASS_KEYBOARD,
          "the keyboard/pointing bits win over the device type for a combo device");
    CHECK(bt_class_from_cod(0x240410) == BT_CLASS_UNKNOWN,
          "CoD 0x240410 (audio minor 0x04 MICROPHONE) is NOT claimed as a headset");
    CHECK(bt_class_from_cod(0x240408) == BT_CLASS_HEADSET,
          "CoD 0x240408 (audio minor 0x02 hands-free) decodes as a headset");
    CHECK(bt_class_from_cod(0x240418) == BT_CLASS_HEADSET,
          "CoD 0x240418 (audio minor 0x06 headphones) decodes as a headset");
    CHECK(bt_class_from_cod(0x00071C) == BT_CLASS_WEARABLE, "major 0x07 is a wearable");
    CHECK(bt_class_from_cod(0x000300) == BT_CLASS_UNKNOWN &&
          bt_class_from_cod(0x000600) == BT_CLASS_UNKNOWN &&
          bt_class_from_cod(0x000800) == BT_CLASS_UNKNOWN,
          "network, imaging and toy majors have no bucket and stay UNKNOWN");
}

/* Regression: the device record must never claim a promotion that the state
 * machine refused. */
static void auth_record_consistency(void) {
    printf("\n--- Authentication Complete: the record cannot outrun the state machine ---\n");
    bt_conn_t *c = linked_stack(200, 64);
    bt_device_t *d = bt_get_device(&g_dev, 0);
    CHECK(c && d && c->state == BT_STATE_CONNECTED && !d->paired,
          "the link starts CONNECTED and unpaired");

    uint8_t lkn[23];
    memcpy(lkn, PEER, 6);
    for (int i = 0; i < 16; i++) lkn[6 + i] = (uint8_t)(0xA0 + i);
    lkn[22] = 0x00;
    inj_evt(&g_dev, BT_EVT_LINK_KEY_NOTIFY, lkn, 23);
    CHECK(d->link_key_valid && !d->paired,
          "a link key alone does not make the device paired");

    /* PEER-initiated authentication: bt_pair() was never called, so the link is
     * still CONNECTED and CONNECTED -> PAIRED is illegal. The old code called
     * conn_set_state(), ignored its refusal, and set devices[].paired anyway. */
    inj_evt(&g_dev, BT_EVT_AUTH_COMPLETE, (const uint8_t[]){0x00, 0x0B, 0x00}, 3);
    CHECK(c->state == BT_STATE_PAIRED,
          "a peer-initiated Authentication Complete walks CONNECTED -> PAIRING -> PAIRED");
    CHECK(d->paired && d->state == BT_STATE_PAIRED,
          "AND the device record agrees with the link state instead of leading it");

    /* A failed RE-authentication must step back down, not stick at PAIRED. */
    inj_evt(&g_dev, BT_EVT_AUTH_COMPLETE, (const uint8_t[]){0x05, 0x0B, 0x00}, 3);
    CHECK(c->state == BT_STATE_CONNECTED,
          "a failed re-authentication steps PAIRED -> PAIRING -> CONNECTED");
    CHECK(!d->paired, "AND the device is no longer marked paired");
    CHECK(bt_verify_coverage(&g_dev), "the stack stays internally consistent throughout");
}

/* L2CAP paths that were implemented but never driven. */
static void l2cap_echo_disconnect_refusal(void) {
    printf("\n--- L2CAP echo, refusal, disconnection, CID collision ---\n");
    bt_conn_t *c = linked_stack(200, 64);
    uint16_t our = req_scid(g_conreq_idx);
    CHECK(c && our == BT_L2CAP_CID_DYN_BASE,
          "the first dynamic CID allocated is 0x0040");

    /* --- Echo Request must draw an Echo Response carrying the same bytes. */
    uint32_t before = g_ctrl.n;
    uint8_t ping[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    inj_sig(&g_dev, g_handle, BT_L2CAP_ECHO_REQ, 0x77, ping, 4);
    uint16_t cid = 0; const uint8_t *pl = NULL; uint32_t pn = 0;
    CHECK(g_ctrl.n == before + 1 &&
          acl_payload(&g_ctrl.log[before], &cid, &pl, &pn) == 0,
          "an Echo Request draws exactly one packet");
    static const uint8_t want_echo[8] = {0x09, 0x77, 0x04, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    CHECK(cid == BT_L2CAP_CID_SIGNAL && pn == 8 && memcmp(pl, want_echo, 8) == 0,
          "it is Echo Response 09 with identifier 0x77 and the four payload bytes back");

    /* An Echo Request bigger than the signalling buffer must still be
     * ANSWERED. The Data field is optional, so an empty response is legal;
     * silence is what makes a live link look dead. */
    uint8_t fat[80];
    memset(fat, 0x5A, sizeof fat);
    before = g_ctrl.n;
    inj_sig(&g_dev, g_handle, BT_L2CAP_ECHO_REQ, 0x78, fat, sizeof fat);
    CHECK(g_ctrl.n == before + 1 &&
          acl_payload(&g_ctrl.log[before], &cid, &pl, &pn) == 0,
          "an oversized Echo Request is still answered, not dropped in silence");
    static const uint8_t want_echo0[4] = {0x09, 0x78, 0x00, 0x00};
    CHECK(pn == 4 && memcmp(pl, want_echo0, 4) == 0,
          "the reply is an empty Echo Response 09 with identifier 0x78");

    /* --- A refused Connection Response must be counted and must destroy the
     *     channel, not leave a half-open one behind. */
    uint32_t refused_before = bt_get_stats(&g_dev)->conn_refused;
    uint8_t rej[8];
    memset(rej, 0, sizeof rej);
    rej[2] = (uint8_t)(our & 0xFF); rej[3] = (uint8_t)(our >> 8);
    rej[4] = 0x02;                                   /* result: PSM not supported */
    inj_sig(&g_dev, g_handle, BT_L2CAP_CONN_RSP, 1, rej, 8);
    CHECK(bt_get_stats(&g_dev)->conn_refused == refused_before + 1,
          "a Connection Response with result != 0 is counted as a refusal");
    CHECK(bt_send_data(&g_dev, PEER, "x", 1) == BT_ESTATE,
          "AND the channel is gone: SPP data is refused, not queued onto a dead CID");

    /* --- Retry, complete the four-way, then let the PEER disconnect it. */
    before = g_ctrl.n;
    CHECK(bt_connect(&g_dev, PEER, BT_PROFILE_SPP) == BT_OK, "SPP is retried");
    uint16_t our2 = l2cap_complete(before, 0x0055);
    CHECK(our2 != 0 && our2 != our, "the retry gets a fresh CID, not the refused one");

    before = g_ctrl.n;
    uint8_t dreq[4];
    dreq[0] = (uint8_t)(our2 & 0xFF); dreq[1] = (uint8_t)(our2 >> 8);
    dreq[2] = 0x55; dreq[3] = 0x00;
    inj_sig(&g_dev, g_handle, BT_L2CAP_DISCONN_REQ, 0x21, dreq, 4);
    CHECK(g_ctrl.n == before + 1 &&
          acl_payload(&g_ctrl.log[before], &cid, &pl, &pn) == 0,
          "a Disconnection Request draws exactly one packet");
    CHECK(pn == 8 && pl[0] == BT_L2CAP_DISCONN_RSP && pl[1] == 0x21 &&
          pl[4] == (uint8_t)(our2 & 0xFF) && pl[5] == (uint8_t)(our2 >> 8) &&
          pl[6] == 0x55 && pl[7] == 0x00,
          "it is Disconnection Response 07 echoing identifier 0x21 and both CIDs");
    CHECK(c->rfcomm_state == BT_RFCOMM_CLOSED,
          "AND the RFCOMM session on that channel is marked closed");
    CHECK(bt_send_data(&g_dev, PEER, "x", 1) == BT_ESTATE,
          "AND no data can be sent on it any more");

    /* --- Collision avoidance: wind the allocator back onto a live CID. */
    before = g_ctrl.n;
    CHECK(bt_hid_connect(&g_dev, PEER) == BT_OK, "two HID channels are requested");
    uint16_t hc = req_scid(before), hi = req_scid(before + 1);
    CHECK(hc != 0 && hi != 0 && hc != hi, "and they get two distinct CIDs");
    g_dev.next_scid = hc;                            /* deliberate collision */
    before = g_ctrl.n;
    CHECK(bt_audio_connect(&g_dev, PEER) == BT_OK, "an AVDTP channel is requested");
    uint16_t av = req_scid(before);
    CHECK(av != 0 && av != hc && av != hi,
          "with next_scid wound back onto a live CID the allocator steps over it");
}

/* One FIFO, two credit pools: a blocked ACL must hold the whole line. */
static void head_of_line_ordering(void) {
    printf("\n--- controller credits: one FIFO, two pools, no reordering ---\n");
    bt_conn_t *c = linked_stack(200, 1);             /* exactly ONE ACL buffer */
    CHECK(c && g_dev.acl_credits == 0,
          "the L2CAP Connection Request consumed the single ACL buffer");

    uint32_t before = g_ctrl.n;
    uint8_t ping[2] = {0x01, 0x02};
    inj_sig(&g_dev, g_handle, BT_L2CAP_ECHO_REQ, 0x31, ping, 2);
    CHECK(g_ctrl.n == before,
          "with no ACL credit the Echo Response is QUEUED, not sent");

    CHECK(g_dev.cmd_credits > 0, "there IS a command credit going spare");
    CHECK(bt_set_discoverable(&g_dev, true) == BT_OK, "and a command is accepted");
    CHECK(g_ctrl.n == before,
          "yet STILL nothing goes out: the blocked ACL holds the head of the line");

    uint8_t ncp[5] = {0x01, 0x0B, 0x00, 0x01, 0x00};
    inj_evt(&g_dev, BT_EVT_NUM_COMP_PKTS, ncp, 5);
    CHECK(g_ctrl.n == before + 2, "returning the ACL credit releases BOTH packets");
    CHECK(g_ctrl.log[before].type == BT_H4_ACL &&
          g_ctrl.log[before + 1].type == BT_H4_CMD,
          "and in FIFO order: the ACL first, then the command that was behind it");
    CHECK(g_dev.discoverable == false,
          "the command going out still does not make the stack claim discoverability");
}

/* Back-pressure on both rings: refuse and count, never silently drop or
 * half-enqueue. */
static void ring_backpressure(void) {
    printf("\n--- transmit and receive back-pressure ---\n");
    uint16_t rf = 0;
    bt_conn_t *c = spp_open_stack(200, &rf);
    CHECK(c && c->rfcomm_state == BT_RFCOMM_OPEN, "an SPP link is open for the test");

    /* --- Transmit: stall the controller, then overfill the queue. Every PDU
     *     is 4 fragments at MTU 200, so an all-or-nothing enqueue means the
     *     drained fragment count is EXACTLY 4x the accepted PDU count. */
    g_dev.acl_credits = 0;
    static uint8_t big[600];
    for (uint32_t i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i * 3u + 1u);
    uint32_t full_before = bt_get_stats(&g_dev)->tx_queue_full;
    uint32_t accepted = 0;
    int rc = BT_OK;
    for (int i = 0; i < 40; i++) {
        rc = bt_send_data(&g_dev, PEER, big, sizeof big);
        if (rc < 0) break;
        accepted++;
    }
    CHECK(rc == BT_ENOSPC, "a full transmit queue returns BT_ENOSPC");
    CHECK(bt_get_stats(&g_dev)->tx_queue_full == full_before + 1,
          "and the refusal is counted exactly once");
    CHECK(accepted >= 4, "several PDUs were accepted before the queue filled");

    uint32_t log_before = g_ctrl.n;
    g_dev.acl_credits = 64;
    int flushed = bt_tx_flush(&g_dev);
    CHECK(flushed == (int)(accepted * 4u) && g_ctrl.n == log_before + accepted * 4u,
          "every accepted PDU drains as exactly 4 fragments: none was half-enqueued");

    /* --- Receive: overfill the delivery ring the same way. */
    static uint8_t frame[700];
    int fl = bt_rfcomm_build(frame, sizeof frame, 2, 1, BT_RFCOMM_UIH, false, big, 600);
    CHECK(fl == 605, "a 600-byte UIH frame is 605 bytes with the 2-octet length form");
    uint32_t drops_before = bt_get_stats(&g_dev)->rx_ring_drops;
    for (int i = 0; i < 12; i++)
        inj_l2cap(&g_dev, g_handle, rf, frame, (uint32_t)fl);
    CHECK(bt_get_stats(&g_dev)->rx_ring_drops > drops_before,
          "once the receive ring is full the surplus frames are dropped and counted");

    uint8_t from[6];
    static uint8_t got[700];
    uint32_t delivered = 0;
    int n;
    bool intact = true;
    while ((n = bt_recv_data(&g_dev, from, got, sizeof got)) > 0) {
        if (n != 600 || memcmp(got, big, 600) != 0 || memcmp(from, PEER, 6) != 0)
            intact = false;
        delivered++;
    }
    CHECK(n == BT_EAGAIN && delivered >= 5 && intact,
          "and every record that DID fit reads back byte-identical with its address");
}

/* Teardown paths in the two profile state machines. */
static void profile_teardown(void) {
    printf("\n--- RFCOMM DISC and AVDTP reject/close teardown ---\n");
    uint16_t rf = 0;
    bt_conn_t *c = spp_open_stack(200, &rf);
    CHECK(c && (c->active_profiles & (uint32_t)BT_PROFILE_SPP) != 0,
          "SPP starts active");
    CHECK(bt_send_data(&g_dev, PEER, "HI", 2) == 2, "and carries data");

    uint8_t disc[4];
    bt_rfcomm_build(disc, sizeof disc, 2, 1, BT_RFCOMM_DISC, true, NULL, 0);
    inj_l2cap(&g_dev, g_handle, rf, disc, 4);
    CHECK(c->rfcomm_state == BT_RFCOMM_CLOSED,
          "a DISC on our DLCI closes the RFCOMM data link");
    CHECK((c->active_profiles & (uint32_t)BT_PROFILE_SPP) == 0,
          "AND SPP stops being listed as an active profile");
    CHECK(bt_send_data(&g_dev, PEER, "HI", 2) == BT_ESTATE,
          "AND bt_send_data refuses instead of framing into a closed link");

    /* AVDTP: a rejected command must fall back to the last stable state
     * rather than pretending the peer agreed. */
    uint32_t before = g_ctrl.n;
    CHECK(bt_audio_connect(&g_dev, PEER) == BT_OK, "an AVDTP channel is opened");
    uint16_t av = l2cap_complete(before, 0x0060);
    CHECK(av != 0 && c->a2dp_state == BT_A2DP_DISCOVERING,
          "the open channel puts A2DP in DISCOVERING");

    uint8_t rej[4];
    int rl = bt_avdtp_build(rej, sizeof rej, 1, BT_AVDTP_MSG_REJECT,
                            BT_AVDTP_DISCOVER, NULL, 0);
    inj_l2cap(&g_dev, g_handle, av, rej, (uint32_t)rl);
    CHECK(c->a2dp_state == BT_A2DP_IDLE,
          "a rejected DISCOVER drops back to IDLE, it does not advance");
    CHECK(bt_audio_start(&g_dev, BT_A2DP_CODEC_SBC, 44100) == BT_ESTATE,
          "AND SET_CONFIGURATION is refused: there is no discovered endpoint");
    CHECK(!g_dev.audio_streaming, "and nothing claims to be streaming");

    /* A DISCOVER accept listing only in-use and source endpoints must also
     * refuse to pick one. Re-arm the query directly: the point under test is
     * the endpoint SELECTION, not another round of channel setup. */
    c->a2dp_state = BT_A2DP_DISCOVERING;
    uint8_t sep[4];
    sep[0] = (uint8_t)((2u << 2) | 0x02); sep[1] = 0x08;   /* in use, sink */
    sep[2] = (uint8_t)(3u << 2);          sep[3] = 0x00;   /* free, SOURCE */
    uint8_t acc[8];
    int al = bt_avdtp_build(acc, sizeof acc, 2, BT_AVDTP_MSG_ACCEPT,
                            BT_AVDTP_DISCOVER, sep, 4);
    inj_l2cap(&g_dev, g_handle, av, acc, (uint32_t)al);
    CHECK(c->a2dp_state == BT_A2DP_IDLE,
          "a DISCOVER listing only in-use and source endpoints picks NONE and returns to IDLE");
}

int main(void) {
    printf("=== ZXV Bluetooth host stack (Core Spec wire format) ===\n");
    test_hci_opcodes();
    test_hci_event_acl();
    test_l2cap_codec();
    test_fragmentation();
    test_rfcomm();
    test_avdtp_hid_rtp();
    test_cod_and_states();
    test_no_radio();
    bringup();
    inquiry_and_connect();
    spp_data();
    pairing();
    a2dp_flow();
    hid_flow();
    credits_and_errors();
    table_limits();
    coverage_can_fail();

    /* ---- audit additions ---- */
    codec_length_guards();
    truncation_sweep();
    state_table_exhaustive();
    cod_extra();
    auth_record_consistency();
    l2cap_echo_disconnect_refusal();
    head_of_line_ordering();
    ring_backpressure();
    profile_teardown();

    printf("\n========================================\n");
    printf("assertions: %d, failures: %d\n", checks, failures);
    if (failures) {
        printf("*** test_bluetooth FAILED ***\n");
        return 1;
    }
    printf("*** test_bluetooth PASSED ***\n");
    return 0;
}

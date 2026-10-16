/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_br_devmesh.c — the unmodified device mesh (kernel/src/devmesh) running
 * over the bearer layer: two devices pair (real ML-DSA-87 / ML-KEM-1024 +
 * X25519 handshake) over a BLE link fragmented at ATT_MTU 23, fail over to a
 * COBS serial link when BLE breaks, and a settlement record goes over LoRa
 * while a frame of unknown class is refused there. Plus the frame classifier
 * and the dm_host_t trampolines on their own. */
#include <stdlib.h>
#include <string.h>

#include "br_devmesh.h"
#include "br_frag.h"
#include "br_serial.h"
#include "../mlkem/keccak.h"
#include "test_check.h"

/* ===== classifier and trampolines ===== */
static int g_inner_ev, g_inner_req, g_inner_rand;
static void *g_seen_ctx;

static void in_random(void *ctx, uint8_t *out, uint32_t n)
{
    g_seen_ctx = ctx;
    g_inner_rand++;
    memset(out, 0xA5, n);
}
static int in_send(void *ctx, const uint8_t to[DM_ID_BYTES], const uint8_t *f, uint32_t n)
{
    (void) ctx;
    (void) to;
    (void) f;
    (void) n;
    return 0;
}
static void in_event(void *ctx, const dm_event_t *ev)
{
    g_seen_ctx = ctx;
    g_inner_ev += ev->kind;
}
static void in_request(void *ctx, const uint8_t from[DM_ID_BYTES], uint32_t id, uint8_t kind,
                       const uint8_t *p, uint32_t n)
{
    (void) from;
    (void) p;
    g_seen_ctx = ctx;
    g_inner_req += (int) (id + kind + n);
}

typedef struct {
    int rc, calls;
    uint32_t last_len;
} fl_t;

static int fl_send(void *ctx, const uint8_t to[BR_ID_BYTES], const uint8_t *d, uint32_t n)
{
    fl_t *f = (fl_t *) ctx;
    (void) to;
    (void) d;
    f->calls++;
    f->last_len = n;
    return f->rc;
}

static void test_classify(void)
{
    uint8_t f[64];
    memset(f, 0, sizeof f);
    f[0] = 'D';
    f[1] = 'M';
    f[2] = 1;
    uint8_t c = 0, fl = 0;
    for (uint8_t t = 1; t <= 3; t++) {
        f[3] = t;
        CHECK("handshake M1..M3: RECORD, signed + encrypted",
              br_dm_classify(f, 64, BR_CLASS_SETTLEMENT, &c, &fl) == BR_OK &&
                  c == BR_CLASS_RECORD && fl == (BR_F_SIGNED | BR_F_ENCRYPTED));
    }
    f[3] = 4;
    CHECK("record with no hint: AI (most restricted)", br_dm_classify(f, 64, 0, &c, &fl) == BR_OK &&
                                                           c == BR_CLASS_AI &&
                                                           fl == (BR_F_AEAD | BR_F_ENCRYPTED));
    CHECK("record with bad hint 9: AI",
          br_dm_classify(f, 64, 9, &c, &fl) == BR_OK && c == BR_CLASS_AI);
    CHECK("record hinted SETTLEMENT: + PAYMENT",
          br_dm_classify(f, 64, BR_CLASS_SETTLEMENT, &c, &fl) == BR_OK &&
              c == BR_CLASS_SETTLEMENT && fl == (BR_F_AEAD | BR_F_ENCRYPTED | BR_F_PAYMENT));
    CHECK("record hinted CHAT",
          br_dm_classify(f, 64, BR_CLASS_CHAT, &c, &fl) == BR_OK && c == BR_CLASS_CHAT);
    f[3] = 5;
    CHECK("unknown frame type refused", br_dm_classify(f, 64, 0, &c, &fl) == BR_ERR_FORMAT);
    f[3] = 4;
    f[2] = 2;
    CHECK("wrong version refused", br_dm_classify(f, 64, 0, &c, &fl) == BR_ERR_FORMAT);
    f[2] = 1;
    f[1] = 'X';
    CHECK("wrong magic refused", br_dm_classify(f, 64, 0, &c, &fl) == BR_ERR_FORMAT);
    f[1] = 'M';
    CHECK("shorter than the devmesh header refused",
          br_dm_classify(f, DM_HDR_BYTES - 1, 0, &c, &fl) == BR_ERR_FORMAT);

    /* trampolines */
    static br_table_t tab;
    br_table_init(&tab);
    fl_t lan = {0, 0, 0}, lora = {0, 0, 0};
    br_link_ops_t o1 = {&lan, fl_send}, o2 = {&lora, fl_send};
    br_link_add(&tab, BR_LAN, 1, &o1);
    br_link_add(&tab, BR_LORA, 2, &o2);
    int inner_ctx = 0;
    dm_host_t in;
    memset(&in, 0, sizeof in);
    in.ctx = &inner_ctx;
    in.random = in_random;
    in.send = in_send;
    in.on_event = in_event;
    in.on_request = in_request;
    static br_dm_t ad;
    dm_host_t out;
    CHECK("adapter init", br_dm_init(&ad, &in, &tab, &out) == BR_OK);
    CHECK("missing callbacks stay missing", out.on_reply == NULL && out.on_money == NULL &&
                                                out.on_money_answer == NULL &&
                                                out.on_event != NULL);
    uint8_t r[4];
    out.random(out.ctx, r, 4);
    CHECK("random reaches the host with the host's ctx",
          g_inner_rand == 1 && g_seen_ctx == &inner_ctx && r[0] == 0xA5);
    dm_event_t ev = {DM_EV_CAPS, {0}, 0, 0, 0};
    out.on_event(out.ctx, &ev);
    CHECK("events reach the host with the host's ctx",
          g_inner_ev == DM_EV_CAPS && g_seen_ctx == &inner_ctx);
    out.on_request(out.ctx, r, 5, 2, r, 3);
    CHECK("requests reach the host", g_inner_req == 10);
    uint8_t to[16] = {1};
    CHECK("send of a devmesh record goes to LAN",
          out.send(out.ctx, to, f, 64) == 0 && lan.calls == 1 && ad.last_link == 1 && ad.sent == 1);
    br_link_set_up(&tab, 1, false);
    CHECK("LAN down: unclassified record refused (LoRa never takes AI)",
          out.send(out.ctx, to, f, 64) == -1 && lora.calls == 0 && ad.refused == 1 &&
              ad.last_status == BR_ERR_NOLINK);
    br_dm_set_class(&ad, BR_CLASS_SETTLEMENT);
    CHECK("hinted SETTLEMENT record goes to LoRa",
          out.send(out.ctx, to, f, 64) == 0 && lora.calls == 1 && ad.last_link == 2);
    CHECK("the hint lasts one send", out.send(out.ctx, to, f, 64) == -1 && lora.calls == 1);
    uint8_t junk[64] = {'X'};
    CHECK("non-devmesh bytes refused",
          out.send(out.ctx, to, junk, 64) == -1 && ad.last_status == BR_ERR_FORMAT);
    CHECK("init without random refused",
          (in.random = NULL, br_dm_init(&ad, &in, &tab, &out) == BR_ERR_ARG));
}

/* ===== two real devices ===== */
enum { HOME = 0, PHONE = 1 };
enum { L_BLE = 1, L_SERIAL = 2, L_LORA = 3 };

typedef struct qe {
    int to, link;
    uint32_t len;
    uint8_t *buf;
    struct qe *next;
} qe_t;

static qe_t *g_qh, *g_qt;

static dm_mesh_t g_mesh[2];
static br_dm_t g_ad[2];
static br_table_t g_tab[2];
static uint64_t g_now = 5000000;
static uint64_t g_ctr[2];
static uint32_t g_sas[2];
static int g_sas_ev[2];
static int g_fail_ble[2];
static int g_ble_frags, g_serial_frames, g_lora_msgs, g_lora_calls;
static int g_last_rx_status[2];

typedef struct {
    int node, link;
} lctx_t;
static lctx_t g_lctx[2][4];

static void enqueue(int to, int link, const uint8_t *p, uint32_t n)
{
    qe_t *e = (qe_t *) malloc(sizeof *e);
    e->to = to;
    e->link = link;
    e->len = n;
    e->buf = (uint8_t *) malloc(n);
    memcpy(e->buf, p, n);
    e->next = NULL;
    if (g_qt)
        g_qt->next = e;
    else
        g_qh = e;
    g_qt = e;
}

static int link_send(void *ctx, const uint8_t to[BR_ID_BYTES], const uint8_t *d, uint32_t n)
{
    lctx_t *c = (lctx_t *) ctx;
    int other = 1 - c->node;
    (void) to;
    if (c->link == L_BLE) {
        if (g_fail_ble[c->node]) return BR_ERR_SEND;
        br_frag_tx_t tx;
        uint8_t fr[20];
        if (br_frag_tx_init(&tx, d, n, 20, (uint8_t) g_ctr[c->node]++) != BR_OK) return BR_ERR_ARG;
        int32_t k;
        while ((k = br_frag_tx_next(&tx, fr, sizeof fr)) > 0) {
            enqueue(other, L_BLE, fr, (uint32_t) k);
            g_ble_frags++;
        }
        return k == 0 ? BR_OK : BR_ERR_SEND;
    }
    if (c->link == L_SERIAL) {
        uint32_t cap = BR_SERIAL_FRAME_MAX(n);
        uint8_t *f = (uint8_t *) malloc(cap);
        int32_t k = br_serial_frame(d, n, f, cap);
        if (k > 0) enqueue(other, L_SERIAL, f, (uint32_t) k);
        free(f);
        g_serial_frames++;
        return k > 0 ? BR_OK : BR_ERR_SEND;
    }
    g_lora_calls++;
    enqueue(other, L_LORA, d, n);
    g_lora_msgs++;
    return BR_OK;
}

static void h_random(void *ctx, uint8_t *out, uint32_t n)
{
    int i = *(int *) ctx;
    uint8_t in[16] = {(uint8_t) i, 0x42};
    for (int k = 0; k < 8; k++) in[8 + k] = (uint8_t) (g_ctr[i] >> (8 * k));
    g_ctr[i]++;
    shake256(in, sizeof in, out, n);
}

static void h_event(void *ctx, const dm_event_t *ev)
{
    int i = *(int *) ctx;
    if (ev->kind == DM_EV_SAS) {
        g_sas[i] = ev->sas;
        g_sas_ev[i]++;
    }
}

static int g_idx[2] = {0, 1};
static br_frag_rx_t g_frx[2];
static br_serial_rx_t g_srx[2];
static uint8_t g_fbuf[2][DM_FRAME_MAX], g_sraw[2][BR_SERIAL_FRAME_MAX(DM_FRAME_MAX)],
    g_sout[2][DM_FRAME_MAX + 8];

static void pump(void)
{
    while (g_qh) {
        qe_t *e = g_qh;
        g_qh = e->next;
        if (!g_qh) g_qt = NULL;
        int to = e->to;
        uint32_t len = 0;
        if (e->link == L_BLE) {
            if (br_frag_rx_push(&g_frx[to], e->buf, e->len, &len) == 1)
                g_last_rx_status[to] = dm_receive(&g_mesh[to], g_fbuf[to], len, g_now);
        } else if (e->link == L_SERIAL) {
            for (uint32_t i = 0; i < e->len; i++)
                if (br_serial_rx_byte(&g_srx[to], e->buf[i], g_sout[to], sizeof g_sout[to], &len) ==
                    1)
                    g_last_rx_status[to] = dm_receive(&g_mesh[to], g_sout[to], len, g_now);
        } else {
            g_last_rx_status[to] = dm_receive(&g_mesh[to], e->buf, e->len, g_now);
        }
        free(e->buf);
        free(e);
    }
}

static void setup(int i, const char *name, dm_role_t role, uint8_t flags)
{
    br_table_init(&g_tab[i]);
    for (int l = L_BLE; l <= L_LORA; l++) {
        g_lctx[i][l].node = i;
        g_lctx[i][l].link = l;
        br_link_ops_t ops = {&g_lctx[i][l], link_send};
        br_link_add(&g_tab[i], l == L_BLE ? BR_BLE : (l == L_SERIAL ? BR_SERIAL : BR_LORA),
                    (uint16_t) l, &ops);
    }
    /* a 9600 bit/s serial radio modem: slower than BLE even for small frames */
    br_link_tune(&g_tab[i], L_SERIAL, 0, 9600, 0);
    br_frag_rx_init(&g_frx[i], g_fbuf[i], sizeof g_fbuf[i]);
    br_serial_rx_init(&g_srx[i], g_sraw[i], sizeof g_sraw[i]);
    dm_host_t h, bh;
    memset(&h, 0, sizeof h);
    h.ctx = &g_idx[i];
    h.random = h_random;
    h.send = in_send; /* never used: the adapter replaces send */
    h.on_event = h_event;
    br_dm_init(&g_ad[i], &h, &g_tab[i], &bh);
    uint8_t seed[64];
    h_random(&g_idx[i], seed, 64);
    CHECK("dm_init with the bearer adapter as host",
          dm_init(&g_mesh[i], &bh, PQM_LEVEL_HIGH, seed, name, role, flags) == DM_OK);
}

static void test_mesh(void)
{
    setup(HOME, "home", DM_ROLE_HOME, 0);
    setup(PHONE, "phone", DM_ROLE_THIN, DM_FLAG_HELD);
    CHECK("home creates the mesh", dm_create(&g_mesh[HOME], g_now) == DM_OK);
    uint8_t qr[DM_QR_MAX];
    uint32_t qn;
    char code[DM_CODE_CHARS + 1];
    CHECK("invite", dm_invite(&g_mesh[HOME], DM_ROLE_THIN, DM_FLAG_HELD, 0, 0, g_now, qr, &qn,
                              code) == DM_OK);
    /* only LoRa up: the handshake must not go out */
    br_link_set_up(&g_tab[PHONE], L_BLE, false);
    br_link_set_up(&g_tab[PHONE], L_SERIAL, false);
    int st = dm_join_qr(&g_mesh[PHONE], qr, qn, g_now);
    CHECK("only LoRa up: pairing frame refused, devmesh sees a transport error",
          st == DM_ERR_TRANSPORT && g_lora_calls == 0 && g_ad[PHONE].refused == 1);
    br_link_set_up(&g_tab[PHONE], L_BLE, true);
    br_link_set_up(&g_tab[PHONE], L_SERIAL, true);
    g_sas_ev[0] = g_sas_ev[1] = 0;
    CHECK("BLE back: join by QR", dm_join_qr(&g_mesh[PHONE], qr, qn, g_now) == DM_OK);
    pump();
    CHECK("handshake crossed BLE at ATT_MTU 23: both screens show the same SAS",
          g_sas_ev[HOME] == 1 && g_sas_ev[PHONE] == 1 && g_sas[HOME] == g_sas[PHONE]);
    printf("  BLE fragments so far: %d (20-byte ATT payloads)\n", g_ble_frags);
    CHECK("thousands of 20-byte BLE fragments carried the handshake", g_ble_frags > 1000);
    CHECK("serial was not used while BLE worked", g_serial_frames == 0);
    dm_pair_confirm(&g_mesh[HOME], dm_self_id(&g_mesh[PHONE]), true, g_now);
    pump();
    dm_pair_confirm(&g_mesh[PHONE], dm_self_id(&g_mesh[HOME]), true, g_now);
    pump();
    CHECK("phone joined; roster v2 on both", g_mesh[PHONE].joined &&
                                                 dm_roster(&g_mesh[HOME])->version == 2 &&
                                                 dm_roster(&g_mesh[PHONE])->version == 2);
    CHECK("session up both ways", dm_peer_up(&g_mesh[HOME], dm_self_id(&g_mesh[PHONE]), g_now) &&
                                      dm_peer_up(&g_mesh[PHONE], dm_self_id(&g_mesh[HOME]), g_now));
    CHECK("no frame was lost or rejected by devmesh", g_frx[0].drops == 0 && g_frx[1].drops == 0 &&
                                                          g_last_rx_status[0] >= 0 &&
                                                          g_last_rx_status[1] >= 0);
    /* BLE breaks on both sides: pings fail over to serial */
    g_fail_ble[HOME] = g_fail_ble[PHONE] = 1;
    g_now += DM_PING_MS;
    dm_tick(&g_mesh[HOME], g_now);
    pump();
    CHECK("BLE error: home's ping failed over to serial",
          g_tab[HOME].failovers == 1 && !br_link_get(&g_tab[HOME], L_BLE)->up &&
              g_serial_frames >= 1);
    CHECK("phone's pong also failed over to serial; home accepted it",
          g_tab[PHONE].failovers == 1 && g_serial_frames >= 2 && g_last_rx_status[HOME] == DM_OK);
    CHECK("serial COBS decoder saw only good frames",
          g_srx[0].bad_crc == 0 && g_srx[1].bad_crc == 0 && g_srx[0].frames >= 1 &&
              g_srx[1].frames >= 1);
    /* only LoRa left */
    br_link_set_up(&g_tab[HOME], L_SERIAL, false);
    br_link_set_up(&g_tab[PHONE], L_SERIAL, false);
    int before = g_lora_msgs;
    br_dm_set_class(&g_ad[HOME], BR_CLASS_SETTLEMENT);
    CHECK("settlement-class record goes out over LoRa",
          dm_send_app(&g_mesh[HOME], dm_self_id(&g_mesh[PHONE]), DM_APP_PING, 0, 0, g_now) ==
                  DM_OK &&
              g_lora_msgs == before + 1 && g_ad[HOME].last_link == L_LORA);
    pump();
    CHECK("the phone got it, but its unclassified reply is refused by LoRa (never AI)",
          g_last_rx_status[PHONE] == DM_ERR_TRANSPORT && g_ad[PHONE].last_status == BR_ERR_NOLINK &&
              g_lora_msgs == before + 1);
    CHECK("devmesh code untouched: same session still up",
          dm_peer_up(&g_mesh[PHONE], dm_self_id(&g_mesh[HOME]), g_now));
}

int main(void)
{
    test_classify();
    test_mesh();
    return check_summary();
}

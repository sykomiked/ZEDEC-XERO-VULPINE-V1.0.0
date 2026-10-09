/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_devmesh.c — host test for the personal device mesh.
 *
 * Three simulated devices (a home computer, a phone, a tablet) exchange real
 * pq_matrix-signed, ML-KEM-1024 + X25519 keyed frames through an in-memory
 * queue: pairing by QR and by short code, SAS agreement, wrong codes,
 * session resumption, remote prompts with streamed replies, fallback when
 * the home node disappears, routing rules, settings sync conflicts (and a
 * randomised convergence property), money confirmation on a held device,
 * and revocation lock-out.
 *
 * BUILD (run from kernel/):
 *   gcc -std=c11 -O2 -Wall -Werror -Wextra -DTEST_HOST -Iinclude -Isrc/pqsec -Isrc/mlkem \
 *     -Isrc/pay -Isrc/tensor \
 *     -Isrc/lpres -Isrc/surplus -Isrc/edp_risk -Isrc/event_space -Isrc/modbind -Isrc/trispace \
 *     src/devmesh/test_devmesh.c src/devmesh/devmesh.c src/devmesh/dm_sync.c \
 *     src/devmesh/dm_remote.c src/devmesh/dm_capmkt.c src/capmkt/capmkt.c src/pay/pay_util.c \
 *     src/tensor/zt.c $(PQM_SRCS) $(PQSIG_SRCS) src/mlkem/keccak.c \
 *     src/mlkem/mlkem768.c src/mlkem/mlkem_encode.c src/mlkem/mlkem_sample.c \
 *     src/mlkem/mlkem_ntt.c src/mlkem/mlkem_kpe.c src/tls/x25519.c src/tls/aead.c \
 *     -o /tmp/test_devmesh && /tmp/test_devmesh
 * Optional argument --matrix also pairs two devices at PQM_LEVEL_MATRIX
 * (SLH-DSA-256s signing: slow).
 */
#include <stdio.h>
#include <string.h>

#include "devmesh.h"
#include "dm_remote.h"
#include "dm_sync.h"
#include "dm_capmkt.h"
#include "../mlkem/keccak.h"

static int g_pass, g_fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, msg);                                  \
        }                                                                                          \
    } while (0)

/* ===== harness ===== */
#define NODES 5
typedef struct {
    int idx;
    uint8_t seed[32];
    uint64_t ctr;
    uint32_t last_sas;
    int sas_events, paired, revoked_ev, self_revoked, fallback, caps_ev, sync_ev, peer_down;
    uint32_t fallback_id;
    char reply[8192];
    uint32_t reply_len;
    int reply_final, reply_calls;
    int money_shown, money_answers, money_status;
    uint64_t money_amount;
    int wallet_ok;
    int auto_serve; /* home: answer prompts with three chunks */
} node_t;

static dm_mesh_t g_mesh[NODES];
static node_t g_node[NODES];
static int g_pair_target = 0; /* where frames to the zero id go */
static int g_offline[NODES];  /* frames to this node are dropped */
static int g_hold;            /* queue but do not deliver */

#define QCAP 48
typedef struct {
    int from;
    uint8_t to[DM_ID_BYTES];
    uint32_t len;
    uint8_t buf[DM_FRAME_MAX];
} qent_t;
static qent_t g_q[QCAP];
static int g_qh, g_qn;
static int g_last_status[NODES];
static int g_status_seen[NODES][32]; /* count of -status codes */

static void h_random(void *ctx, uint8_t *out, uint32_t n)
{
    node_t *nd = (node_t *) ctx;
    uint8_t in[40];
    memcpy(in, nd->seed, 32);
    for (int i = 0; i < 8; i++) in[32 + i] = (uint8_t) (nd->ctr >> (8 * i));
    nd->ctr++;
    shake256(in, sizeof in, out, n);
}

static int h_send(void *ctx, const uint8_t to[DM_ID_BYTES], const uint8_t *frame, uint32_t len)
{
    node_t *nd = (node_t *) ctx;
    if (g_qn >= QCAP) return -1;
    qent_t *e = &g_q[(g_qh + g_qn) % QCAP];
    e->from = nd->idx;
    memcpy(e->to, to, DM_ID_BYTES);
    e->len = len;
    memcpy(e->buf, frame, len);
    g_qn++;
    return 0;
}

static void h_event(void *ctx, const dm_event_t *ev)
{
    node_t *nd = (node_t *) ctx;
    switch (ev->kind) {
    case DM_EV_SAS:
        nd->last_sas = ev->sas;
        nd->sas_events++;
        break;
    case DM_EV_PAIRED:
        nd->paired++;
        break;
    case DM_EV_REVOKED:
        nd->revoked_ev++;
        break;
    case DM_EV_SELF_REVOKED:
        nd->self_revoked++;
        break;
    case DM_EV_REQ_FALLBACK:
        nd->fallback++;
        nd->fallback_id = (uint32_t) ev->u64;
        break;
    case DM_EV_CAPS:
        nd->caps_ev++;
        break;
    case DM_EV_SYNC:
        nd->sync_ev++;
        break;
    case DM_EV_PEER_DOWN:
        nd->peer_down++;
        break;
    default:
        break;
    }
}

static uint64_t g_now = 1000000;

static void h_request(void *ctx, const uint8_t from[DM_ID_BYTES], uint32_t req_id, uint8_t kind,
                      const uint8_t *payload, uint32_t len)
{
    node_t *nd = (node_t *) ctx;
    dm_mesh_t *m = &g_mesh[nd->idx];
    if (kind == DM_REQ_WALLET) {
        dm_money_t a;
        nd->wallet_ok = dm_wallet_check(m, payload, len, g_now, &a) == DM_OK ? 1 : -1;
        dm_reply(m, from, req_id, (const uint8_t *) "done", 4, true, g_now);
        return;
    }
    if (!nd->auto_serve) return;
    const char *parts[3] = {"Hello ", "from the ", "home node."};
    (void) payload;
    for (int i = 0; i < 3; i++)
        dm_reply(m, from, req_id, (const uint8_t *) parts[i], (uint32_t) strlen(parts[i]), i == 2,
                 g_now);
}

static void h_reply(void *ctx, const uint8_t from[DM_ID_BYTES], uint32_t req_id,
                    const uint8_t *data, uint32_t len, bool final)
{
    node_t *nd = (node_t *) ctx;
    (void) from;
    (void) req_id;
    if (nd->reply_len + len < sizeof nd->reply) {
        memcpy(nd->reply + nd->reply_len, data, len);
        nd->reply_len += len;
        nd->reply[nd->reply_len] = 0;
    }
    nd->reply_calls++;
    if (final) nd->reply_final++;
}

static void h_money(void *ctx, const uint8_t from[DM_ID_BYTES], const dm_money_t *a)
{
    node_t *nd = (node_t *) ctx;
    (void) from;
    nd->money_shown++;
    nd->money_amount = a->amount;
}

static void h_money_answer(void *ctx, const dm_money_t *a, const dm_confirm_t *c, int status)
{
    node_t *nd = (node_t *) ctx;
    (void) a;
    (void) c;
    nd->money_answers++;
    nd->money_status = status;
}

static int node_of(const uint8_t id[DM_ID_BYTES])
{
    static const uint8_t z[DM_ID_BYTES] = {0};
    if (!memcmp(id, z, DM_ID_BYTES)) return g_pair_target;
    for (int i = 0; i < NODES; i++)
        if (!memcmp(dm_self_id(&g_mesh[i]), id, DM_ID_BYTES)) return i;
    return -1;
}

static int pump(void)
{
    int delivered = 0;
    while (g_qn > 0 && !g_hold) {
        qent_t *e = &g_q[g_qh];
        g_qh = (g_qh + 1) % QCAP;
        g_qn--;
        int to = node_of(e->to);
        if (to < 0 || g_offline[to]) continue;
        static uint8_t copy[DM_FRAME_MAX];
        uint32_t len = e->len;
        memcpy(copy, e->buf, len);
        int st = dm_receive(&g_mesh[to], copy, len, g_now);
        g_last_status[to] = st;
        if (st < 0 && -st < 32) g_status_seen[to][-st]++;
        delivered++;
    }
    return delivered;
}

static void drop_queue(void)
{
    g_qh = 0;
    g_qn = 0;
}

static void clear_status(void)
{
    memset(g_status_seen, 0, sizeof g_status_seen);
    memset(g_last_status, 0, sizeof g_last_status);
}

static void setup(int i, pqm_level_t lvl, const char *name, dm_role_t role, uint8_t flags,
                  uint32_t ram_mb, uint8_t net)
{
    node_t *nd = &g_node[i];
    memset(nd, 0, sizeof *nd);
    nd->idx = i;
    for (int k = 0; k < 32; k++) nd->seed[k] = (uint8_t) (i * 37 + k);
    dm_host_t h;
    memset(&h, 0, sizeof h);
    h.ctx = nd;
    h.random = h_random;
    h.send = h_send;
    h.on_event = h_event;
    h.on_request = h_request;
    h.on_reply = h_reply;
    h.on_money = h_money;
    h.on_money_answer = h_money_answer;
    uint8_t seed[64];
    h_random(nd, seed, 64);
    int st = dm_init(&g_mesh[i], &h, lvl, seed, name, role, flags);
    CHECK(st == DM_OK, "dm_init");
    dm_caps_t c;
    memset(&c, 0, sizeof c);
    c.ram_mb = ram_mb;
    c.free_ram_mb = ram_mb / 2;
    c.compute = role == DM_ROLE_HOME ? 4000 : 400;
    c.battery_pct = role == DM_ROLE_HOME ? 255 : 80;
    c.net = net;
    c.features = role == DM_ROLE_HOME ? (DM_FEAT_FILES | DM_FEAT_WALLET | DM_FEAT_STREAM) : 0;
    dm_caps_fit(&c, (uint8_t) role);
    dm_caps_update(&g_mesh[i], &c, g_now);
}

static const uint8_t *ID(int i)
{
    return dm_self_id(&g_mesh[i]);
}

static dm_peer_t *peer_of(int a, int b)
{
    return dm_peer_find(&g_mesh[a], ID(b));
}

/* ===== tests ===== */
enum { HOME = 0, PHONE = 1, TABLET = 2, EVE = 3 };

static void test_pair_qr(void)
{
    printf("pairing by QR code\n");
    CHECK(dm_create(&g_mesh[HOME], g_now) == DM_OK, "create mesh");
    CHECK(dm_is_admin(&g_mesh[HOME]), "creator is admin");
    CHECK(dm_roster(&g_mesh[HOME])->version == 1, "roster v1");
    uint8_t qr[DM_QR_MAX];
    uint32_t qn;
    char code[DM_CODE_CHARS + 1];
    CHECK(dm_invite(&g_mesh[PHONE], DM_ROLE_THIN, DM_FLAG_HELD, 0, 0, g_now, qr, &qn, code) ==
              DM_ERR_PERM,
          "non-member cannot invite");
    CHECK(dm_invite(&g_mesh[HOME], DM_ROLE_THIN, DM_FLAG_HELD | DM_FLAG_ADMIN,
                    (const uint8_t *) "192.168.1.10:7400", 17, g_now, qr, &qn, code) == DM_OK,
          "invite");
    char text[256];
    CHECK(dm_b32_encode(qr, qn, text, sizeof text) > 0, "QR text form");
    uint8_t back[DM_QR_MAX];
    CHECK(dm_b32_decode(text, back, sizeof back) == (int32_t) qn && !memcmp(back, qr, qn),
          "base32 round trip");

    CHECK(dm_join_qr(&g_mesh[PHONE], qr, qn, g_now) == DM_OK, "join by QR");
    pump();
    CHECK(g_node[PHONE].sas_events == 1 && g_node[HOME].sas_events == 1, "both screens show SAS");
    CHECK(g_node[PHONE].last_sas == g_node[HOME].last_sas, "SAS values match");
    CHECK(g_node[PHONE].last_sas < DM_SAS_MOD, "SAS is 6 digits");
    printf("  SAS shown on both screens: %06u\n", g_node[HOME].last_sas);
    CHECK(!g_mesh[PHONE].joined, "not joined before SAS check");
    CHECK(dm_pair_confirm(&g_mesh[HOME], ID(PHONE), true, g_now) == DM_OK, "home confirms");
    pump();
    CHECK(dm_roster(&g_mesh[HOME])->version == 1, "no roster change before the phone confirms");
    CHECK(dm_pair_confirm(&g_mesh[PHONE], ID(HOME), true, g_now) == DM_OK, "phone confirms");
    pump();
    CHECK(g_mesh[PHONE].joined, "phone joined");
    CHECK(dm_roster(&g_mesh[PHONE])->version == 2 && dm_roster(&g_mesh[HOME])->version == 2,
          "roster v2 on both");
    const dm_dev_t *d = dm_roster_find(dm_roster(&g_mesh[HOME]), ID(PHONE));
    CHECK(d && d->status == DM_DEV_ACTIVE && d->role == DM_ROLE_THIN &&
              d->flags == (DM_FLAG_HELD | DM_FLAG_ADMIN),
          "phone listed with role and flags");
    CHECK(dm_peer_up(&g_mesh[PHONE], ID(HOME), g_now) &&
              dm_peer_up(&g_mesh[HOME], ID(PHONE), g_now),
          "session up both ways");
    CHECK(peer_of(PHONE, HOME)->have_caps && peer_of(HOME, PHONE)->have_caps,
          "capabilities exchanged");
    CHECK(peer_of(PHONE, HOME)->caps.features & DM_FEAT_ASSIST_LARGE,
          "home advertises large model");
    uint8_t k1[32], k2[32], k3[32];
    CHECK(dm_session_export_key(&g_mesh[PHONE], ID(HOME), "ehop", k1) == DM_OK &&
              dm_session_export_key(&g_mesh[HOME], ID(PHONE), "ehop", k2) == DM_OK &&
              dm_session_export_key(&g_mesh[HOME], ID(PHONE), "other", k3) == DM_OK,
          "export keys");
    CHECK(!memcmp(k1, k2, 32) && memcmp(k1, k3, 32), "export key agrees, differs by label");
    CHECK(dm_join_qr(&g_mesh[PHONE], qr, qn, g_now) == DM_ERR_STATE, "joined device cannot rejoin");
}

static void test_pair_code(void)
{
    printf("pairing by short code (+ wrong codes)\n");
    uint8_t qr[DM_QR_MAX];
    uint32_t qn;
    char code[DM_CODE_CHARS + 1];
    CHECK(dm_invite(&g_mesh[HOME], DM_ROLE_THIN, DM_FLAG_HELD | DM_FLAG_ADMIN, 0, 0, g_now, qr, &qn,
                    code) == DM_OK,
          "invite");
    /* the user types it lowercase with a dash; O typed for 0 etc. */
    char typed[32];
    int t = 0;
    for (int i = 0; i < (int) DM_CODE_CHARS; i++) {
        char c = code[i];
        if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
        if (c == '0') c = 'o';
        if (c == '1') c = 'l';
        typed[t++] = c;
        if (i == 4) typed[t++] = '-';
    }
    typed[t] = 0;
    uint8_t tag1[8], tag2[8];
    CHECK(dm_code_tag(code, tag1) == DM_OK && dm_code_tag(typed, tag2) == DM_OK &&
              !memcmp(tag1, tag2, 8),
          "LAN discovery tag tolerant to typing");
    g_pair_target = HOME;
    CHECK(dm_join_code(&g_mesh[TABLET], typed, g_now) == DM_OK, "join by code");
    pump();
    CHECK(g_node[TABLET].last_sas == g_node[HOME].last_sas, "SAS match (code)");
    dm_pair_confirm(&g_mesh[TABLET], ID(HOME), true, g_now);
    pump();
    dm_pair_confirm(&g_mesh[HOME], ID(TABLET), true, g_now);
    pump();
    CHECK(g_mesh[TABLET].joined, "tablet joined");
    CHECK(dm_roster(&g_mesh[PHONE])->version == 3, "phone got roster v3 by broadcast");
    dm_peer_t *pt = peer_of(PHONE, TABLET);
    CHECK(pt && pt->have_keys, "phone learned tablet keys (self-certifying)");
    dm_peer_t *tp = peer_of(TABLET, PHONE);
    CHECK(tp && tp->have_keys, "tablet learned phone keys");
    /* tablet and phone can talk directly */
    CHECK(dm_connect(&g_mesh[TABLET], ID(PHONE), g_now) == DM_OK, "tablet connects to phone");
    pump();
    CHECK(dm_peer_up(&g_mesh[TABLET], ID(PHONE), g_now) &&
              dm_peer_up(&g_mesh[PHONE], ID(TABLET), g_now),
          "tablet <-> phone session");

    /* wrong codes: three strikes kill the invitation */
    CHECK(dm_invite(&g_mesh[HOME], DM_ROLE_THIN, DM_FLAG_HELD, 0, 0, g_now, qr, &qn, code) == DM_OK,
          "new invite");
    char wrong[DM_CODE_CHARS + 1];
    memcpy(wrong, code, sizeof wrong);
    wrong[0] = wrong[0] == 'A' ? 'B' : 'A';
    clear_status();
    for (int k = 0; k < 3; k++) {
        g_mesh[EVE].hs_active = 0;
        dm_join_code(&g_mesh[EVE], wrong, g_now);
        pump();
    }
    CHECK(g_status_seen[HOME][-DM_ERR_AUTH] == 3, "wrong code rejected (MAC)");
    CHECK(!g_mesh[HOME].inv_active, "invitation dead after 3 wrong codes");
    g_mesh[EVE].hs_active = 0;
    dm_join_code(&g_mesh[EVE], code, g_now);
    clear_status();
    pump();
    CHECK(g_status_seen[HOME][-DM_ERR_STATE] == 1 && !g_mesh[EVE].joined,
          "right code after lock-out refused");

    /* QR whose keyhash does not match the device that answers */
    CHECK(dm_invite(&g_mesh[HOME], DM_ROLE_THIN, DM_FLAG_HELD, 0, 0, g_now, qr, &qn, code) == DM_OK,
          "invite");
    qr[5 + 16 + 16 + 20] ^= 1; /* a keyhash byte past the dev-id prefix */
    g_mesh[EVE].hs_active = 0;
    g_mesh[EVE].join_active = 0;
    CHECK(dm_join_qr(&g_mesh[EVE], qr, qn, g_now) == DM_OK, "join with tampered QR");
    clear_status();
    pump();
    CHECK(g_status_seen[EVE][-DM_ERR_AUTH] == 1 && g_node[EVE].sas_events == 0,
          "inviter keys not matching QR keyhash: rejected");

    /* SAS mismatch abandons the pairing */
    qr[5 + 16 + 16 + 20] ^= 1;
    g_mesh[EVE].hs_active = 0;
    CHECK(dm_join_qr(&g_mesh[EVE], qr, qn, g_now) == DM_OK, "join again");
    pump();
    CHECK(g_node[EVE].sas_events == 1, "SAS shown");
    CHECK(dm_pair_confirm(&g_mesh[HOME], ID(EVE), false, g_now) == DM_OK, "user: does not match");
    dm_pair_confirm(&g_mesh[EVE], ID(HOME), true, g_now);
    pump();
    CHECK(!g_mesh[EVE].joined && !dm_roster_find(dm_roster(&g_mesh[HOME]), ID(EVE)),
          "mismatched SAS: not added");
}

static void test_resume(void)
{
    printf("session loss and resumption\n");
    g_now += 3u * DM_LIVE_MS;
    for (int i = 0; i < 3; i++) dm_tick(&g_mesh[i], g_now);
    drop_queue();
    CHECK(!dm_peer_up(&g_mesh[PHONE], ID(HOME), g_now), "session timed out");
    CHECK(g_node[PHONE].peer_down >= 1, "peer-down event");
    CHECK(dm_connect(&g_mesh[PHONE], ID(HOME), g_now) == DM_OK, "reconnect");
    pump();
    CHECK(dm_peer_up(&g_mesh[PHONE], ID(HOME), g_now) &&
              dm_peer_up(&g_mesh[HOME], ID(PHONE), g_now),
          "session resumed (no pairing needed)");
    /* glare: both connect at once */
    g_now += 3u * DM_LIVE_MS;
    for (int i = 0; i < 3; i++) dm_tick(&g_mesh[i], g_now);
    drop_queue();
    dm_connect(&g_mesh[PHONE], ID(TABLET), g_now);
    dm_connect(&g_mesh[TABLET], ID(PHONE), g_now);
    pump();
    CHECK(dm_peer_up(&g_mesh[PHONE], ID(TABLET), g_now) &&
              dm_peer_up(&g_mesh[TABLET], ID(PHONE), g_now),
          "simultaneous connect resolves to one session");
    dm_connect(&g_mesh[PHONE], ID(HOME), g_now);
    dm_connect(&g_mesh[TABLET], ID(HOME), g_now);
    pump();
    /* replayed record is refused */
    dm_send_app(&g_mesh[PHONE], ID(HOME), DM_APP_PING, 0, 0, g_now);
    static uint8_t rec[DM_FRAME_MAX];
    uint32_t rl = g_q[g_qh].len;
    memcpy(rec, g_q[g_qh].buf, rl);
    pump();
    CHECK(dm_receive(&g_mesh[HOME], rec, rl, g_now) == DM_ERR_REPLAY, "replayed record refused");
    rec[rl - 1] ^= 1;
    CHECK(dm_receive(&g_mesh[HOME], rec, rl, g_now) != DM_OK, "tampered record refused");
}

static void test_remote_and_fallback(void)
{
    printf("remote prompts, streaming, routing and fallback\n");
    uint8_t tgt[DM_ID_BYTES];
    g_node[HOME].auto_serve = 1;
    CHECK(dm_route(&g_mesh[PHONE], DM_REQ_PROMPT, DM_MODEL_LARGE, g_now, tgt) == DM_ROUTE_REMOTE &&
              !memcmp(tgt, ID(HOME), DM_ID_BYTES),
          "large prompt goes to the home node");
    CHECK(dm_route(&g_mesh[HOME], DM_REQ_PROMPT, DM_MODEL_LARGE, g_now, tgt) == DM_ROUTE_LOCAL,
          "home node runs its own large prompts");
    uint32_t rid;
    const char *prompt = "Summarise my notes";
    memcpy(tgt, ID(HOME), DM_ID_BYTES);
    CHECK(dm_request(&g_mesh[PHONE], tgt, DM_REQ_PROMPT, (const uint8_t *) prompt,
                     (uint32_t) strlen(prompt), g_now, &rid) == DM_OK,
          "send prompt");
    pump();
    CHECK(g_node[PHONE].reply_calls == 3 && g_node[PHONE].reply_final == 1, "three chunks, final");
    CHECK(!strcmp(g_node[PHONE].reply, "Hello from the home node."), "streamed reply reassembled");
    CHECK(dm_request(&g_mesh[PHONE], tgt, DM_REQ_WALLET, (const uint8_t *) "x", 1, g_now, &rid) ==
              DM_ERR_PERM,
          "wallet actions cannot bypass confirmation");

    /* the home node vanishes mid-request */
    g_node[PHONE].reply_len = 0;
    g_node[PHONE].reply_calls = 0;
    g_offline[HOME] = 1;
    CHECK(dm_request(&g_mesh[PHONE], tgt, DM_REQ_PROMPT, (const uint8_t *) prompt,
                     (uint32_t) strlen(prompt), g_now, &rid) == DM_OK,
          "send prompt (home offline)");
    pump();
    g_now += DM_REQ_TIMEOUT_MS + 1u;
    dm_tick(&g_mesh[PHONE], g_now);
    CHECK(g_node[PHONE].fallback == 1 && g_node[PHONE].fallback_id == rid,
          "request falls back after timeout");
    dm_route_t r = dm_route(&g_mesh[PHONE], DM_REQ_PROMPT, DM_MODEL_SMALL, g_now, tgt);
    CHECK(r == DM_ROUTE_LOCAL && !memcmp(tgt, ID(PHONE), DM_ID_BYTES),
          "small prompt now runs on the phone");
    CHECK(dm_route(&g_mesh[PHONE], DM_REQ_PROMPT, DM_MODEL_LARGE, g_now, tgt) ==
              DM_ROUTE_LOCAL_DEGRADED,
          "large prompt degrades to the small local model");
    /* a late reply after fallback is dropped (no double answer) */
    g_offline[HOME] = 0;
    dm_mesh_t *h = &g_mesh[HOME];
    dm_peer_t *hp = dm_peer_find(h, ID(PHONE));
    hp->s.last_rx_ms = g_now;
    dm_reply(h, ID(PHONE), rid, (const uint8_t *) "late", 4, true, g_now);
    pump();
    CHECK(g_node[PHONE].reply_calls == 0, "late reply ignored");

    /* routing rules */
    dm_mesh_t *ph = &g_mesh[PHONE];
    for (int i = 0; i < 3; i++) dm_tick(&g_mesh[i], g_now);
    drop_queue();
    dm_connect(ph, ID(HOME), g_now);
    pump();
    dm_caps_t c;
    memcpy(&c, &ph->caps, sizeof c);
    c.net = DM_NET_METERED;
    dm_caps_update(ph, &c, g_now);
    pump();
    CHECK(dm_route(ph, DM_REQ_PROMPT, DM_MODEL_SMALL, g_now, tgt) == DM_ROUTE_LOCAL,
          "metered link: small prompt stays local");
    CHECK(dm_route(ph, DM_REQ_PROMPT, DM_MODEL_LARGE, g_now, tgt) == DM_ROUTE_REMOTE,
          "metered link: large prompt still goes home");
    c.battery_pct = 10;
    dm_caps_update(ph, &c, g_now);
    CHECK(dm_route(ph, DM_REQ_PROMPT, DM_MODEL_SMALL, g_now, tgt) == DM_ROUTE_REMOTE,
          "low battery: offload even on metered");
    c.battery_pct = 80;
    dm_caps_update(ph, &c, g_now);
    dm_set(ph, DM_KEY_ALLOW_METERED, (const uint8_t *) "1", 1, g_now);
    pump();
    CHECK(dm_route(ph, DM_REQ_PROMPT, DM_MODEL_SMALL, g_now, tgt) == DM_ROUTE_REMOTE,
          "allow-metered setting: offload");
    dm_set(ph, DM_KEY_ALLOW_METERED, (const uint8_t *) "0", 1, g_now);
    pump();
    CHECK(dm_route(ph, DM_REQ_FILE, 0, g_now, tgt) == DM_ROUTE_REMOTE &&
              !memcmp(tgt, ID(HOME), DM_ID_BYTES),
          "file actions go to the device with the file store");
    CHECK(dm_route(ph, DM_REQ_WALLET, 0, g_now, tgt) == DM_ROUTE_CONFIRM &&
              !memcmp(tgt, ID(HOME), DM_ID_BYTES),
          "wallet: confirm first, wallet on home");
    c.net = DM_NET_OFFLINE;
    dm_caps_update(ph, &c, g_now);
    CHECK(dm_route(ph, DM_REQ_PROMPT, DM_MODEL_SMALL, g_now, tgt) == DM_ROUTE_LOCAL,
          "offline phone: local small model");
    /* a tiny device with no model that fits */
    c.ram_mb = 1024;
    c.net = DM_NET_UNMETERED;
    dm_caps_fit(&c, DM_ROLE_THIN);
    CHECK(c.nmodels == 0, "1 GB phone: no model fits the half-RAM budget");
    c.ram_mb = 2048;
    dm_caps_fit(&c, DM_ROLE_THIN);
    CHECK(c.nmodels == 1 && !strcmp(c.models[0].name, "qwen2.5-0.5b-instruct-q4_k_m"),
          "2 GB phone: 0.5B only");
    c.ram_mb = 8192;
    dm_caps_fit(&c, DM_ROLE_THIN);
    CHECK(c.nmodels == 2 && !(c.features & DM_FEAT_ASSIST_LARGE), "8 GB phone: 0.5B + 1.5B");
    c.ram_mb = 1024;
    dm_caps_fit(&c, DM_ROLE_THIN);
    dm_caps_update(ph, &c, g_now);
    g_offline[HOME] = 1;
    g_now += 3u * DM_LIVE_MS;
    CHECK(dm_route(ph, DM_REQ_PROMPT, DM_MODEL_SMALL, g_now, tgt) == DM_ROUTE_NEED_CAPACITY,
          "no home, no local model, online: buy capacity");
    c.net = DM_NET_OFFLINE;
    dm_caps_update(ph, &c, g_now);
    CHECK(dm_route(ph, DM_REQ_PROMPT, DM_MODEL_SMALL, g_now, tgt) == DM_ROUTE_UNAVAILABLE,
          "no home, no local model, offline: unavailable");
    c.ram_mb = 8192;
    c.net = DM_NET_UNMETERED;
    dm_caps_fit(&c, DM_ROLE_THIN);
    dm_caps_update(ph, &c, g_now);
    g_offline[HOME] = 0;
    for (int i = 0; i < 3; i++) dm_tick(&g_mesh[i], g_now);
    drop_queue();
    dm_connect(ph, ID(HOME), g_now);
    pump();
    dm_connect(&g_mesh[TABLET], ID(HOME), g_now);
    pump();
    dm_connect(&g_mesh[TABLET], ID(PHONE), g_now);
    pump();
}

/* --- settings sync --- */
static void test_sync(void)
{
    printf("settings sync (vector clocks, last writer wins)\n");
    dm_mesh_t *ph = &g_mesh[PHONE], *tb = &g_mesh[TABLET], *hm = &g_mesh[HOME];
    CHECK(dm_peer_up(ph, ID(TABLET), g_now) && dm_peer_up(tb, ID(HOME), g_now), "sessions up");
    /* plain propagation */
    dm_set(ph, DM_KEY_LANGUAGE, (const uint8_t *) "en", 2, g_now);
    pump();
    const dm_field_t *f = dm_settings_get(&hm->settings, DM_KEY_LANGUAGE);
    CHECK(f && f->len == 2 && !memcmp(f->value, "en", 2), "phone write reaches home");
    /* causal overwrite with a clock that runs behind wins over timestamp */
    dm_set(tb, DM_KEY_LANGUAGE, (const uint8_t *) "fr", 2, g_now - 100000);
    pump();
    f = dm_settings_get(&ph->settings, DM_KEY_LANGUAGE);
    CHECK(f && !memcmp(f->value, "fr", 2), "later write wins even with an earlier timestamp");
    f = dm_settings_get(&hm->settings, DM_KEY_LANGUAGE);
    CHECK(f && !memcmp(f->value, "fr", 2), "home has it too");
    /* concurrent writes while partitioned */
    uint32_t c0 = ph->settings.conflicts;
    g_hold = 1;
    dm_set(ph, DM_KEY_THEME, (const uint8_t *) "dark", 4, g_now + 5);
    dm_set(tb, DM_KEY_THEME, (const uint8_t *) "light", 5, g_now + 9);
    drop_queue();
    g_hold = 0;
    dm_sync_push(ph, ID(TABLET), g_now);
    dm_sync_push(tb, ID(PHONE), g_now);
    pump();
    const dm_field_t *a = dm_settings_get(&ph->settings, DM_KEY_THEME);
    const dm_field_t *b = dm_settings_get(&tb->settings, DM_KEY_THEME);
    CHECK(a && b && a->len == b->len && !memcmp(a->value, b->value, a->len),
          "concurrent: converge");
    CHECK(a && !memcmp(a->value, "light", 5), "concurrent: later timestamp wins");
    CHECK(ph->settings.conflicts == c0 + 1 && tb->settings.conflicts >= 1, "conflict counted");
    CHECK(a && dm_vc_compare(a, b) == 0 && a->nvc == 2, "merged clocks equal, both writers");
    dm_sync_push(ph, ID(HOME), g_now);
    pump();
    const dm_field_t *hc = dm_settings_get(&hm->settings, DM_KEY_THEME);
    CHECK(hc && !memcmp(hc->value, "light", 5), "third device converges");
    /* idempotent */
    int32_t again = dm_sync_merge(&hm->settings, (const uint8_t *) "", 0);
    CHECK(again == DM_ERR_FORMAT, "empty snapshot is malformed");
    static uint8_t snap[DM_SYNC_MAX];
    int32_t sn = dm_sync_encode(&ph->settings, snap, sizeof snap);
    CHECK(sn > 0 && dm_sync_merge(&hm->settings, snap, (uint32_t) sn) == 0,
          "re-merging the same state changes nothing");
    snap[sn - 1] ^= 0;
    CHECK(dm_sync_merge(&hm->settings, snap, (uint32_t) sn - 1) < 0, "truncated snapshot refused");

    /* randomised property: 3 replicas, random writes and merge orders */
    uint32_t seed = 12345;
    int ok = 1;
    for (int trial = 0; trial < 200; trial++) {
        static dm_settings_t r[3];
        uint8_t ids[3][DM_ID_BYTES];
        for (int i = 0; i < 3; i++) {
            dm_settings_init(&r[i]);
            memset(ids[i], 0, DM_ID_BYTES);
            ids[i][0] = (uint8_t) (i + 1);
        }
        for (int step = 0; step < 40; step++) {
            seed = seed * 1103515245u + 12345u;
            int who = (int) ((seed >> 16) % 3u);
            seed = seed * 1103515245u + 12345u;
            uint32_t op = (seed >> 16) % 4u;
            if (op < 2) {
                uint8_t v = (uint8_t) (seed >> 24);
                dm_settings_set(&r[who], ids[who], (uint16_t) (1 + (seed >> 8) % 3u), &v, 1,
                                (seed >> 4) % 7u);
            } else {
                int other = (who + 1 + (int) (op & 1u)) % 3;
                static uint8_t buf[DM_SYNC_MAX];
                int32_t n = dm_sync_encode(&r[other], buf, sizeof buf);
                if (n < 0 || dm_sync_merge(&r[who], buf, (uint32_t) n) < 0) ok = 0;
            }
        }
        /* full exchange: everyone pulls from everyone twice */
        for (int round = 0; round < 2; round++)
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) {
                    if (i == j) continue;
                    static uint8_t buf[DM_SYNC_MAX];
                    int32_t n = dm_sync_encode(&r[j], buf, sizeof buf);
                    if (n < 0 || dm_sync_merge(&r[i], buf, (uint32_t) n) < 0) ok = 0;
                }
        for (uint16_t k = 1; k <= 3; k++) {
            const dm_field_t *x = dm_settings_get(&r[0], k);
            for (int i = 1; i < 3; i++) {
                const dm_field_t *y = dm_settings_get(&r[i], k);
                if (!x != !y) ok = 0;
                if (x && y &&
                    (x->len != y->len || memcmp(x->value, y->value, x->len) ||
                     dm_vc_compare(x, y) != 0))
                    ok = 0;
            }
        }
    }
    CHECK(ok, "200 random histories: all replicas converge (commutative, idempotent)");
}

static void make_money(dm_money_t *a, int origin, uint64_t amount, uint8_t tag)
{
    memset(a, 0, sizeof *a);
    memset(a->action_id, tag, DM_ID_BYTES);
    a->kind = DM_MONEY_PAY;
    a->rail = DM_RAIL_DEBIT;
    memcpy(a->currency, "VFV", 4);
    a->amount = amount;
    memset(a->payee, 0x5a, DM_HASH_BYTES);
    strcpy(a->memo, "capacity: 2 GPU-hours");
    memcpy(a->origin, ID(origin), DM_ID_BYTES);
    a->expires_ms = g_now + 60000;
}

static void test_money(void)
{
    printf("money actions confirmed on a held device\n");
    dm_mesh_t *ph = &g_mesh[PHONE], *hm = &g_mesh[HOME];
    dm_money_t a;
    dm_confirm_t c;
    make_money(&a, HOME, 12500, 0x11);
    CHECK(dm_money_confirm_local(hm, &a, true, g_now, &c) == DM_ERR_PERM,
          "home node (not held) cannot confirm");
    CHECK(dm_money_ask(hm, ID(PHONE), &a, g_now) == DM_OK, "home asks the phone");
    pump();
    CHECK(g_node[PHONE].money_shown == 1 && g_node[PHONE].money_amount == 12500,
          "phone shows the action");
    CHECK(dm_money_answer(ph, true, g_now) == DM_OK, "user approves on the phone");
    pump();
    CHECK(g_node[HOME].money_answers == 1 && g_node[HOME].money_status == DM_OK,
          "home receives a verified approval");
    /* direct verification paths */
    make_money(&a, PHONE, 700, 0x22);
    CHECK(dm_money_confirm_local(ph, &a, true, g_now, &c) == DM_OK, "phone confirms locally");
    dm_money_t t;
    memcpy(&t, &a, sizeof t);
    t.amount = 7000;
    CHECK(dm_money_verify(hm, &t, &c, g_now) == DM_ERR_AUTH, "changed amount: refused");
    memcpy(&t, &a, sizeof t);
    t.rail = DM_RAIL_CREDIT;
    CHECK(dm_money_verify(hm, &t, &c, g_now) == DM_ERR_AUTH, "changed rail: refused");
    c.sig[100] ^= 1;
    CHECK(dm_money_verify(hm, &a, &c, g_now) == DM_ERR_AUTH, "bad signature: refused");
    c.sig[100] ^= 1;
    CHECK(dm_money_verify(hm, &a, &c, g_now + 61000) == DM_ERR_EXPIRED, "expired: refused");
    CHECK(dm_money_verify(hm, &a, &c, g_now) == DM_OK, "valid: accepted");
    CHECK(dm_money_verify(hm, &a, &c, g_now) == DM_ERR_REPLAY, "replay: refused");
    memcpy(&t, &a, sizeof t);
    t.rail = 999;
    CHECK(dm_money_confirm_local(ph, &t, true, g_now, &c) == DM_ERR_FORMAT,
          "rail must be 555/777/888");
    memcpy(&t, &a, sizeof t);
    memcpy(t.currency, "USD", 4);
    CHECK(dm_money_confirm_local(ph, &t, true, g_now, &c) == DM_ERR_FORMAT, "currency must be VFV");
    /* decline */
    make_money(&a, HOME, 99, 0x33);
    dm_money_ask(hm, ID(PHONE), &a, g_now);
    pump();
    dm_money_answer(ph, false, g_now);
    pump();
    CHECK(g_node[HOME].money_answers == 2 && g_node[HOME].money_status == DM_ERR_PERM,
          "declined action is not executable");
    /* phone-originated wallet request */
    make_money(&a, PHONE, 4200, 0x44);
    uint32_t rid;
    CHECK(dm_wallet_request(ph, ID(HOME), &a, g_now, &rid) == DM_OK, "wallet request sent");
    pump();
    CHECK(g_node[HOME].wallet_ok == 1, "home verifies the phone's confirmation before acting");
    /* home cannot ask a device that is not held */
    make_money(&a, PHONE, 1, 0x55);
    CHECK(dm_money_ask(ph, ID(HOME), &a, g_now) == DM_ERR_PERM, "home is not a held device");
}

static cm_market_t g_mkt;
static void test_capmkt_glue(void)
{
    printf("buying capacity: escrow confirmed on a held device\n");
    dm_mesh_t *ph = &g_mesh[PHONE], *hm = &g_mesh[HOME];
    cm_init(&g_mkt, 0);
    uint8_t acct_p[CM_ID_BYTES], acct_h[CM_ID_BYTES];
    CHECK(dm_capmkt_account(ph, acct_p) == DM_OK && dm_capmkt_account(hm, acct_h) == DM_OK &&
              !memcmp(acct_p, acct_h, CM_ID_BYTES),
          "one market account for all of the user's devices");
    cm_deposit(&g_mkt, acct_p, 100000);
    cm_key_t key = {CM_RES_INFERENCE, CM_TENOR_DAY, 0};
    uint8_t aid[DM_ID_BYTES];
    memset(aid, 0x77, sizeof aid);
    dm_money_t a;
    dm_confirm_t c;
    CHECK(dm_capmkt_bid_action(ph, key, 50, 40, aid, g_now + 60000, &a) == DM_OK &&
              a.amount == 2000 && a.kind == DM_MONEY_ESCROW,
          "escrow action for 50 x 40 VFV");
    CHECK(dm_money_confirm_local(hm, &a, true, g_now, &c) == DM_ERR_PERM,
          "the home node cannot approve spending");
    CHECK(dm_money_confirm_local(ph, &a, true, g_now, &c) == DM_OK, "user approves on the phone");
    uint32_t oid;
    CHECK(dm_capmkt_bid(hm, &g_mkt, &a, &c, key, 60, 40, g_now + 3600000, g_now, &oid) ==
              DM_ERR_PERM,
          "confirmation for 50 units does not cover 60");
    CHECK(dm_capmkt_bid(hm, &g_mkt, &a, &c, key, 50, 40, g_now + 3600000, g_now, &oid) == DM_OK,
          "home node places the confirmed bid");
    CHECK(cm_account(&g_mkt, acct_p)->locked == 2000, "2000 VFV locked");
    CHECK(dm_capmkt_bid(hm, &g_mkt, &a, &c, key, 50, 40, g_now + 3600000, g_now, &oid) ==
              DM_ERR_REPLAY,
          "the same confirmation cannot place a second bid");
    cm_key_t k2 = {CM_RES_STORAGE, CM_TENOR_WEEK, 0};
    CHECK(dm_capmkt_offer(hm, &g_mkt, k2, 100, 3, g_now + 3600000, &oid) == DM_OK,
          "home node offers spare storage at its own price");
}

static void test_revoke(void)
{
    printf("revocation\n");
    dm_mesh_t *ph = &g_mesh[PHONE], *hm = &g_mesh[HOME], *tb = &g_mesh[TABLET];
    uint64_t v = dm_roster(hm)->version;
    CHECK(dm_rename(ph, ID(HOME), "Living-room PC", g_now) == DM_OK, "rename");
    pump();
    CHECK(!strcmp(dm_roster_find(dm_roster(tb), ID(HOME))->name, "Living-room PC"),
          "rename reaches the tablet");
    CHECK(dm_roster(hm)->version == v + 1, "version advanced");
    /* the tablet, an admin today, will try to undo its own revocation */
    static uint8_t enc_old[DM_ROSTER_MAX];
    int32_t eo = dm_roster_encode(dm_roster(tb), enc_old, sizeof enc_old);
    CHECK(eo > 0, "encode");
    clear_status();
    CHECK(dm_revoke(ph, ID(TABLET), g_now) == DM_OK, "phone revokes the tablet");
    pump();
    CHECK(dm_roster_find(dm_roster(hm), ID(TABLET))->status == DM_DEV_REVOKED,
          "home learns the revocation");
    CHECK(!dm_peer_find(hm, ID(TABLET)) && !dm_peer_find(ph, ID(TABLET)),
          "sessions and keys dropped");
    CHECK(g_node[HOME].revoked_ev == 1, "revoked event on home");
    /* its old session is dead on the other side */
    clear_status();
    dm_send_app(tb, ID(HOME), DM_APP_PING, 0, 0, g_now);
    pump();
    CHECK(g_status_seen[HOME][-DM_ERR_NOPEER] == 1, "old session records refused");
    /* locked out of new sessions */
    g_now += 3u * DM_LIVE_MS;
    dm_tick(tb, g_now);
    drop_queue();
    tb->hs_active = 0;
    CHECK(dm_connect(tb, ID(HOME), g_now) == DM_OK, "tablet tries to connect");
    clear_status();
    pump();
    CHECK(g_status_seen[HOME][-DM_ERR_REVOKED] == 1, "home refuses the revoked device");
    CHECK(!dm_peer_up(tb, ID(HOME), g_now), "no session for the tablet");
    /* the tablet signs a roster that renames itself (it still thinks it is
     * an admin); the home node refuses it */
    dm_rename(tb, ID(TABLET), "still here", g_now);
    dm_rename(tb, ID(TABLET), "still here 2", g_now);
    drop_queue();
    static uint8_t enc[DM_ROSTER_MAX];
    int32_t en = dm_roster_encode(dm_roster(tb), enc, sizeof enc);
    CHECK(dm_roster(tb)->version > dm_roster(hm)->version, "forged roster is newer");
    CHECK(dm_roster_accept(hm, enc, (uint32_t) en, g_now) == DM_ERR_REVOKED,
          "roster signed by a revoked device refused");
    CHECK(dm_roster_accept(hm, enc_old, (uint32_t) eo, g_now) == DM_ERR_REPLAY,
          "old roster (before revocation) refused");
    CHECK(dm_set_role(ph, ID(TABLET), DM_ROLE_THIN, DM_FLAG_HELD, g_now) == DM_ERR_REVOKED,
          "revocation is permanent");
    /* tamper with a valid roster */
    en = dm_roster_encode(dm_roster(ph), enc, sizeof enc);
    enc[40] ^= 1;
    int st = dm_roster_accept(hm, enc, (uint32_t) en, g_now);
    CHECK(st != DM_OK, "tampered roster refused");
    /* the revoked device, if it hears of it, forgets the mesh */
    en = dm_roster_encode(dm_roster(ph), enc, sizeof enc);
    /* tablet's roster has diverged (its own forged versions); deliver the
     * real one to a fresh copy of the tablet state from before */
    CHECK(dm_roster_accept(tb, enc, (uint32_t) en, g_now) != DM_OK,
          "diverged revoked device: real roster version not newer than its forgery");
    /* last-admin protection */
    CHECK(dm_set_role(ph, ID(HOME), DM_ROLE_HOME, 0, g_now) == DM_OK, "home loses admin");
    pump();
    CHECK(dm_revoke(ph, ID(PHONE), g_now) == DM_ERR_PERM, "cannot revoke the last admin");
    CHECK(dm_rename(hm, ID(PHONE), "x", g_now) == DM_ERR_PERM, "non-admin cannot edit roster");
}

static void test_self_revoked(void)
{
    printf("a revoked device that hears of it forgets the mesh\n");
    dm_mesh_t *ph = &g_mesh[PHONE];
    /* pair a fresh device, then revoke it while connected */
    setup(4, PQM_LEVEL_HIGH, "Old phone", DM_ROLE_THIN, 0, 4096, DM_NET_UNMETERED);
    uint8_t qr[DM_QR_MAX];
    uint32_t qn;
    char code[DM_CODE_CHARS + 1];
    CHECK(dm_invite(ph, DM_ROLE_THIN, DM_FLAG_HELD, 0, 0, g_now, qr, &qn, code) == DM_OK, "invite");
    dm_join_qr(&g_mesh[4], qr, qn, g_now);
    pump();
    dm_pair_confirm(&g_mesh[4], ID(PHONE), true, g_now);
    pump();
    dm_pair_confirm(ph, ID(4), true, g_now);
    pump();
    CHECK(g_mesh[4].joined, "old phone joined");
    static uint8_t enc[DM_ROSTER_MAX];
    CHECK(dm_revoke(ph, ID(4), g_now) == DM_OK, "revoke it");
    drop_queue();
    int32_t en = dm_roster_encode(dm_roster(ph), enc, sizeof enc);
    CHECK(dm_roster_accept(&g_mesh[4], enc, (uint32_t) en, g_now) == DM_OK,
          "it receives the roster");
    CHECK(g_node[4].self_revoked == 1 && !g_mesh[4].joined, "and forgets the mesh");
}

static void test_matrix(void)
{
    printf("pairing at PQM_LEVEL_MATRIX (slow)\n");
    drop_queue();
    setup(3, PQM_LEVEL_MATRIX, "Matrix home", DM_ROLE_HOME, 0, 32768, DM_NET_UNMETERED);
    setup(4, PQM_LEVEL_MATRIX, "Matrix phone", DM_ROLE_THIN, DM_FLAG_HELD, 8192, DM_NET_LAN);
    dm_create(&g_mesh[3], g_now);
    uint8_t qr[DM_QR_MAX];
    uint32_t qn;
    char code[DM_CODE_CHARS + 1];
    dm_invite(&g_mesh[3], DM_ROLE_THIN, DM_FLAG_HELD, 0, 0, g_now, qr, &qn, code);
    dm_join_qr(&g_mesh[4], qr, qn, g_now);
    pump();
    CHECK(g_node[3].last_sas == g_node[4].last_sas && g_node[4].sas_events == 1,
          "MATRIX SAS match");
    dm_pair_confirm(&g_mesh[4], ID(3), true, g_now);
    pump();
    dm_pair_confirm(&g_mesh[3], ID(4), true, g_now);
    pump();
    CHECK(g_mesh[4].joined && dm_peer_up(&g_mesh[4], ID(3), g_now), "MATRIX pairing complete");
}

int main(int argc, char **argv)
{
    printf("=== test_devmesh: personal device mesh (pq_matrix HIGH) ===\n");
    setup(HOME, PQM_LEVEL_HIGH, "Home PC", DM_ROLE_HOME, 0, 32768, DM_NET_UNMETERED);
    setup(PHONE, PQM_LEVEL_HIGH, "Phone", DM_ROLE_THIN, DM_FLAG_HELD, 8192, DM_NET_UNMETERED);
    setup(TABLET, PQM_LEVEL_HIGH, "Tablet", DM_ROLE_THIN, DM_FLAG_HELD, 4096, DM_NET_UNMETERED);
    setup(EVE, PQM_LEVEL_HIGH, "Eve", DM_ROLE_THIN, 0, 4096, DM_NET_UNMETERED);
    test_pair_qr();
    test_pair_code();
    test_resume();
    test_remote_and_fallback();
    test_sync();
    test_money();
    test_capmkt_glue();
    test_revoke();
    test_self_revoked();
    if (argc > 1 && !strcmp(argv[1], "--matrix")) test_matrix();
    printf("test_devmesh: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

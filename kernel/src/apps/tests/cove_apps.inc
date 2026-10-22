/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cove's model against zxvd's real dispatcher. ehop.bands, ehop.seal and
 * ehop.open run as zxvd runs them. This in-process zxvd links no device
 * mesh, so it refuses devmesh.* ("not available"); the roster is answered
 * on a link of its own (COV_LINK), which passes every other request to the
 * real dispatcher. The keys are fixed test vectors, never a real secret;
 * nothing is transmitted. */
#include "cove_model.h"

static cove_t COV_A, COV_B;
static zxm_link_t COV_LINK;
static int cov_calls; /* every request COV_LINK carried */
static const char *COV_STATUS, *COV_DEVICES;

static int32_t cov_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    cov_calls++;
    if (strstr(req, "\"devmesh.status\"")) r = COV_STATUS;
    if (strstr(req, "\"devmesh.devices\"")) r = COV_DEVICES;
    if (!r) return inproc(NULL, req, len, resp, cap);
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
}

#define COV_KEY    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
#define COV_KEY2   "ff0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
#define COV_DESK   "00112233445566778899aabbccddeeff"
#define COV_LAPTOP "fedcba9876543210fedcba9876543210"
#define COV_OLD    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define COV_NONE   "cccccccccccccccccccccccccccccccc"
#define COV_DEV(id, name, status, self)                                                            \
    "{\"id\":\"" id "\",\"name\":\"" name "\",\"role\":\"thin\",\"admin\":true,\"held\":false,"    \
    "\"status\":\"" status "\",\"self\":" self ",\"up\":false,\"since\":\"2\"}"
#define COV_ST(self)                                                                               \
    "{\"joined\":true,\"mesh_id\":\"" COV_OLD "\",\"self\":\"" self "\",\"name\":\"x\","           \
    "\"role\":\"home\",\"admin\":true,\"level\":\"high\",\"roster_version\":\"4\",\"devices\":3,"  \
    "\"inviting\":false,\"joining\":false,\"pairing\":null,\"frames_waiting\":0,"                  \
    "\"frames_dropped\":\"0\",\"carrier\":\"none\",\"saved\":false,\"save_error\":null,"           \
    "\"events\":[]}"

/* the roster as the desk sees it, and as the laptop does */
static const char *const COV_ROSTER_DESK = "{\"roster_version\":\"4\",\"devices\":[" COV_DEV(
    COV_DESK, "desk", "active", "true") "," COV_DEV(COV_LAPTOP, "laptop", "active",
                                                    "false") "," COV_DEV(COV_OLD, "old tablet",
                                                                         "revoked", "false") "]}";
static const char *const COV_ROSTER_LAPTOP = "{\"roster_version\":\"4\",\"devices\":[" COV_DEV(
    COV_DESK, "desk", "active", "false") "," COV_DEV(COV_LAPTOP, "laptop", "active",
                                                     "true") "," COV_DEV(COV_OLD, "old tablet",
                                                                         "revoked", "false") "]}";
static const char *const COV_ROSTER_GONE = "{\"roster_version\":\"5\",\"devices\":[" COV_DEV(
    COV_DESK, "desk", "active", "true") "," COV_DEV(COV_LAPTOP, "laptop", "revoked",
                                                    "false") "," COV_DEV(COV_OLD, "old tablet",
                                                                         "revoked", "false") "]}";

/* whether the n bytes at p hold the text s anywhere */
static bool cov_holds(const void *p, size_t n, const char *s)
{
    const unsigned char *b = p;
    size_t k = strlen(s);
    for (size_t i = 0; k && i + k <= n; i++)
        if (!memcmp(b + i, s, k)) return true;
    return false;
}

APPS_TEST(cove)
{
    static char frame1[COVE_FRAME_HEX + 1], frame3[COVE_FRAME_HEX + 1];
    static uint8_t wide[COVE_DATA_MAX];
    const int64_t NOW = 1700000000000LL;
    int before;
    for (uint32_t i = 0; i < COVE_DATA_MAX; i++) wide[i] = (uint8_t) (i * 5u + 1u);

    /* the real dispatcher: no device mesh here, but the band plan */
    cove_init(&COV_A, &LINK);
    before = sent;
    CHECK(cove_tunnel_open(&COV_A, COV_LAPTOP, "chat", 7, 1, 11, 12, NOW) == ZXM_E_STATE &&
              cove_active_devices(&COV_A) == TRELLIS_REFUSED && sent == before,
          "cove: nothing opens before the mesh is known, nothing asked");
    CHECK(cove_read_status(&COV_A) == ZXM_E_REMOTE && !COV_A.known && strstr(LINK.err, "available"),
          "cove: devmesh.status refused by this zxvd: %s", LINK.err);
    CHECK(cove_read_bands(&COV_A) == ZXM_OK && COV_A.nbands == 5 &&
              cove_band_index(&COV_A, "control") == 0 && cove_band_index(&COV_A, "settle") == 1 &&
              cove_band_index(&COV_A, "chat") == 3 && cove_band_index(&COV_A, "radio") == -1 &&
              COV_A.band[0].max_frame == 560 && COV_A.band[3].max_frame == 2096,
          "CM2: ehop.bands: %s", LINK.err);

    /* CM1: the mesh on its own link, as the desk sees it */
    zxm_link_init(&COV_LINK, cov_send, &COV_LINK);
    cove_init(&COV_A, &COV_LINK);
    COV_STATUS = COV_ST(COV_DESK);
    COV_DEVICES = COV_ROSTER_DESK;
    CHECK(cove_read_status(&COV_A) == ZXM_OK && COV_A.known && COV_A.joined &&
              !strcmp(COV_A.self, COV_DESK) && cove_read_devices(&COV_A) == ZXM_OK &&
              COV_A.devices == 3 && COV_A.device[0].self &&
              COV_A.device[2].state == TRELLIS_REVOKED &&
              cove_find_device(&COV_A, COV_LAPTOP) == 1 && cove_find_device(&COV_A, "x") == -1 &&
              !strcmp(COV_A.notice, "3 devices on your roster, 2 active."),
          "CM1: the roster: %s", COV_A.notice);
    CHECK(cove_active_devices(&COV_A) == 2, "CM6: two active devices (trellis.peers_in_state)");
    CHECK(cove_read_bands(&COV_A) == ZXM_OK, "CM2: the bands on the tunnel's link");

    /* CM3: which tunnels open (CR1), and why not */
    CHECK(!cove_may_open(&COV_A, 0) && cove_may_open(&COV_A, 1) && !cove_may_open(&COV_A, 2) &&
              !cove_may_open(&COV_A, 3),
          "CR1: only the laptop: not this device, not the revoked tablet, not past the roster");
    before = cov_calls;
    CHECK(cove_tunnel_open(&COV_A, COV_OLD, "chat", 7, 1, 11, 12, NOW) == ZXM_E_STATE &&
              strstr(COV_A.notice, "that device is revoked"),
          "CM3: no tunnel to a revoked device: %s", COV_A.notice);
    CHECK(cove_tunnel_open(&COV_A, COV_DESK, "chat", 7, 1, 11, 12, NOW) == ZXM_E_STATE &&
              strstr(COV_A.notice, "this device"),
          "CM3: no tunnel to itself: %s", COV_A.notice);
    CHECK(cove_tunnel_open(&COV_A, COV_NONE, "chat", 7, 1, 11, 12, NOW) == ZXM_E_STATE &&
              strstr(COV_A.notice, "not on your roster"),
          "CM3: no tunnel off the roster: %s", COV_A.notice);
    CHECK(cove_tunnel_open(&COV_A, COV_LAPTOP, "settle", 7, 1, 11, 12, NOW) == ZXM_E_ARG &&
              strstr(COV_A.notice, "payments") &&
              cove_tunnel_open(&COV_A, COV_LAPTOP, "radio", 7, 1, 11, 12, NOW) == ZXM_E_ARG &&
              cove_tunnel_open(&COV_A, COV_LAPTOP, "chat", 7, 1, 11, 11, NOW) == ZXM_E_ARG &&
              cove_tunnel_open(&COV_A, "xyz", "chat", 7, 1, 11, 12, NOW) == ZXM_E_ARG &&
              cove_tunnel_open(&COV_A, COV_LAPTOP, "chat", 7, 1, 11, 12, -1) == ZXM_E_ARG &&
              cov_calls == before,
          "CM3: settle, no band, one sender at both ends, no device id, no time: refused, "
          "nothing asked");
    int ta = cove_tunnel_open(&COV_A, COV_LAPTOP, "chat", 7, 1, 11, 12, NOW);
    int tc = cove_tunnel_open(&COV_A, COV_LAPTOP, "control", 8, 1, 11, 12, NOW);
    CHECK(ta == 0 && tc == 1 && COV_A.tunnel[0].open && COV_A.tunnel[0].band == 3 &&
              cove_record_ok(&COV_A, 0) && !cove_record_ok(&COV_A, 2) && cov_calls == before,
          "CM3, CM7: two tunnels to the laptop, their records fit: %s", COV_A.notice);

    /* CM4: checked before anything is sent */
    before = cov_calls;
    CHECK(cove_seal_frame(&COV_A, 0, "00", wide, 5, NOW) == ZXM_E_ARG &&
              cove_seal_frame(&COV_A, 1, COV_KEY, wide, 513, NOW) == ZXM_E_ARG &&
              cove_seal_frame(&COV_A, 0, COV_KEY, wide, 2049, NOW) == ZXM_E_ARG &&
              cove_seal_frame(&COV_A, 0, COV_KEY, wide, 5, -1) == ZXM_E_ARG &&
              cove_seal_frame(&COV_A, 5, COV_KEY, wide, 5, NOW) == ZXM_E_ARG &&
              cov_calls == before && COV_A.tunnel[0].seq_out == 0,
          "CM4, CR3: a key that is no key, data wider than its band, no time, no tunnel: "
          "nothing sent");
    CHECK(cove_seal_frame(&COV_A, 1, COV_KEY, wide, 512, NOW) == ZXM_OK &&
              strlen(COV_A.frame_hex) == 2u * 560u && COV_A.tunnel[1].seq_out == 1,
          "CM4, CR3: 512 bytes exactly fill the control band: %s", COV_LINK.err);
    CHECK(cove_seal_frame(&COV_A, 0, COV_KEY, wide, 2048, NOW) == ZXM_OK &&
              strlen(COV_A.frame_hex) == 2u * 2096u && COV_A.tunnel[0].seq_out == 1,
          "CM4: 2048 bytes fill the chat band: %s", COV_LINK.err);
    memcpy(frame1, COV_A.frame_hex, strlen(COV_A.frame_hex) + 1u);
    CHECK(COV_LINK.req_len == 0 && !strstr(COV_LINK.req, COV_KEY) &&
              !strstr(COV_LINK.req, "0001020304") && !cov_holds(&COV_A, sizeof COV_A, COV_KEY),
          "CM4: the key is wiped from the request and never kept in the model");
    CHECK(cove_seal_frame(&COV_A, 0, COV_KEY, (const uint8_t *) "hello cove", 10, NOW) == ZXM_OK &&
              COV_A.tunnel[0].seq_out == 2 &&
              cove_seal_frame(&COV_A, 0, COV_KEY, (const uint8_t *) "third", 5, NOW + 1) ==
                  ZXM_OK &&
              COV_A.tunnel[0].seq_out == 3 && strlen(COV_A.frame_hex) == 2u * 53u,
          "CM4: each frame takes the next seq: %s", COV_A.notice);
    memcpy(frame3, COV_A.frame_hex, strlen(COV_A.frame_hex) + 1u);

    /* CM5: the laptop opens them, in order, once */
    cove_init(&COV_B, &COV_LINK);
    COV_STATUS = COV_ST(COV_LAPTOP);
    COV_DEVICES = COV_ROSTER_LAPTOP;
    CHECK(cove_read_status(&COV_B) == ZXM_OK && cove_read_devices(&COV_B) == ZXM_OK &&
              cove_read_bands(&COV_B) == ZXM_OK,
          "CM1: the laptop's own view of the mesh");
    int tb = cove_tunnel_open(&COV_B, COV_DESK, "chat", 7, 1, 12, 11, NOW);
    int tx = cove_tunnel_open(&COV_B, COV_DESK, "chat", 7, 1, 12, 13, NOW);
    int tk = cove_tunnel_open(&COV_B, COV_DESK, "control", 7, 1, 12, 11, NOW);
    CHECK(tb == 0 && tx == 1 && tk == 2, "CM3: the laptop's tunnels to the desk");
    CHECK(cove_open_frame(&COV_B, 0, COV_KEY, frame1, NOW) == ZXM_OK && COV_B.ndata == 2048 &&
              !memcmp(COV_B.data, wide, 2048) && COV_B.tunnel[0].seq_in == 1 &&
              COV_B.tunnel[0].gap_in == 1 && COV_LINK.req_len == 0,
          "CM5: frame 1 opened, the request wiped: %s", COV_LINK.err);
    CHECK(cove_open_frame(&COV_B, 0, COV_KEY, frame1, NOW) == ZXM_E_STATE &&
              COV_B.tunnel[0].seq_in == 1 && strstr(COV_B.notice, "seen already"),
          "CM5, CR2: the same frame again is a replay: dropped");
    CHECK(cove_open_frame(&COV_B, 0, COV_KEY, frame3, NOW) == ZXM_OK && COV_B.ndata == 5 &&
              !memcmp(COV_B.data, "third", 5) && COV_B.tunnel[0].seq_in == 3 &&
              COV_B.tunnel[0].gap_in == 2 && strstr(COV_B.notice, "1 before it never came"),
          "CM5, CR2: frame 3 after frame 1: a gap of 2 is kept: %s", COV_B.notice);
    CHECK(cove_open_frame(&COV_B, 0, COV_KEY2, frame3, NOW) == ZXM_E_REMOTE &&
              COV_B.tunnel[0].seq_in == 3,
          "CM5: another key does not open it: %s", COV_LINK.err);
    CHECK(cove_open_frame(&COV_B, 1, COV_KEY, frame3, NOW) == ZXM_E_STATE &&
              strstr(COV_B.notice, "Not from the far end") && COV_B.tunnel[1].seq_in == 0,
          "CM5: a frame from another sender on the channel: dropped");
    before = cov_calls;
    CHECK(cove_open_frame(&COV_B, 2, COV_KEY, frame1, NOW) == ZXM_E_ARG &&
              cove_open_frame(&COV_B, 0, COV_KEY, "abc", NOW) == ZXM_E_ARG &&
              cove_open_frame(&COV_B, 0, COV_KEY, "zz", NOW) == ZXM_E_ARG &&
              cove_open_frame(&COV_B, 0, COV_KEY, "00", NOW) == ZXM_E_ARG && cov_calls == before,
          "CM5, CR3: a frame wider than its band, odd, not hex or shorter than its header: "
          "nothing asked");

    /* CM4, CR2: a channel whose seqs are spent moves to a new epoch */
    COV_A.tunnel[0].seq_out = COVE_SEQ_MAX;
    before = cov_calls;
    CHECK(cove_seal_frame(&COV_A, 0, COV_KEY, wide, 5, NOW) == ZXM_E_STATE && cov_calls == before &&
              COV_A.tunnel[0].seq_out == COVE_SEQ_MAX && strstr(COV_A.notice, "new epoch"),
          "CM4: no seq past eighteen digits: nothing sent");

    /* CM4: the laptop is revoked; its tunnels close at the next frame */
    COV_STATUS = COV_ST(COV_DESK);
    COV_DEVICES = COV_ROSTER_GONE;
    CHECK(cove_read_devices(&COV_A) == ZXM_OK && cove_active_devices(&COV_A) == 1,
          "CM6: one active device left");
    before = cov_calls;
    CHECK(cove_seal_frame(&COV_A, 1, COV_KEY, wide, 5, NOW) == ZXM_E_STATE &&
              !COV_A.tunnel[1].open && cov_calls == before && strstr(COV_A.notice, "closed"),
          "CM4, CR1: a revoked device's tunnel closes, nothing sent: %s", COV_A.notice);
    CHECK(cove_tunnel_close(&COV_A, 0) == ZXM_OK && !COV_A.tunnel[0].open &&
              cove_tunnel_close(&COV_A, 0) == ZXM_E_ARG,
          "CM3: closed once");

    /* answers that are not zxvd's: nothing kept */
    cove_init(&COV_B, &COV_LINK);
    COV_STATUS = "{\"joined\":true,\"self\":\"xyz\"}";
    CHECK(cove_read_status(&COV_B) == ZXM_E_PROTOCOL && !COV_B.known, "cove: a self that is no id");
    COV_DEVICES =
        "{\"roster_version\":\"4\",\"devices\":[" COV_DEV(COV_DESK, "desk", "lost", "true") "]}";
    CHECK(cove_read_devices(&COV_B) == ZXM_E_PROTOCOL && !COV_B.roster_known,
          "cove: a status that is neither active nor revoked");
    COV_DEVICES = "{\"roster_version\":\"4\",\"devices\":[" COV_DEV(
        COV_DESK, "desk", "active", "true") "," COV_DEV(COV_LAPTOP, "laptop", "active",
                                                        "true") "]}";
    CHECK(cove_read_devices(&COV_B) == ZXM_E_PROTOCOL && !COV_B.roster_known,
          "cove: two devices that are both this one");
}

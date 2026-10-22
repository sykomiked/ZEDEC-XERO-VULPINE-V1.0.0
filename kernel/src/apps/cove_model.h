/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cove_model.h — Cove, private tunnels between your own devices, as a
 * model (zxm.h). A tunnel joins this device to another device of the
 * owner's mesh; every frame on it is sealed and opened by zxvd on an
 * Endian Hopping band. The rules are cove_rules.h; how many devices are
 * active is Trellis's rule (trellis_rules.h TL3).
 *
 * zxvd methods used: devmesh.status, devmesh.devices, ehop.bands,
 * ehop.seal and ehop.open. No carrier is wired to the socket (devmesh.h
 * H7) and zxvd has no method that moves a frame between devices, so a
 * sealed frame is handed to the caller and a frame to open is given by
 * the caller; nothing here transmits. No drawing, no allocation, integers
 * only.
 *
 *   CM1 THE MESH: devmesh.status (joined, this device's id) and
 *       devmesh.devices (the signed roster, up to COVE_DEVICES rows, each
 *       active or revoked, one at most this device). An answer in any
 *       other form is not zxvd's (ZXM_E_PROTOCOL) and nothing of it is
 *       kept.
 *   CM2 THE BANDS: ehop.bands, up to COVE_BANDS, each band's largest
 *       frame COVE_FRAME_MIN + 1 .. COVE_FRAME_MAX; otherwise the plan is
 *       kept as it was.
 *   CM3 OPENING A TUNNEL to a roster device on a band, a channel id, a key
 *       epoch and two sender ids (this end's and the far end's, which
 *       differ: ehop wants a sender unique on its channel). It opens only
 *       when the mesh is joined, the roster and the bands are read, the
 *       band is not settle (Cove carries no money), CR1 holds for this
 *       device's row and the far device's row, and the tunnel's record
 *       fits ^ZXCOVE (CR4). Up to COVE_TUNNELS at once.
 *   CM4 SEALING: before anything is sent, the tunnel is open, CR1 still
 *       holds on the roster as it is now (a device revoked since closes
 *       its tunnels), the key is 64 hex digits, the frame the data makes
 *       fits the band (CR3) and the next seq rises by exactly 1 (CR2).
 *       The seq is taken before the call and never given again, so a
 *       failed call never makes zxvd seal two frames under one nonce. The
 *       key is given for this one call: it is never kept in the model,
 *       and the request that carried it is wiped as soon as zxvd answers,
 *       whatever it answers. zxvd's frame must be the data and 48 bytes,
 *       within the band (CR3), or it is not zxvd's answer.
 *   CM5 OPENING A FRAME: the same checks of the tunnel and the key, and
 *       the frame (an even count of hex digits) fits the band (CR3) before
 *       ehop.open is asked; the request is wiped as in CM4. The frame must
 *       come from the far end's sender, and its seq must rise past the
 *       last opened on the tunnel (CR2; zxvd keeps no replay window): a
 *       replay, a reuse or a seq past COVE_SEQ_MAX is ZXM_E_STATE and
 *       nothing is kept. The gap is kept: past 1, frames were lost.
 *   CM6 ACTIVE DEVICES: how many roster rows are active, by Trellis's
 *       count (trellis.peers_in_state, TL3).
 *   CM7 THE RECORDS: a tunnel's record (CR4) is checked when it opens and
 *       each time its seq moves; a seq that would not fit is refused
 *       before it is sent. */
#ifndef COVE_MODEL_H
#define COVE_MODEL_H

#include "cove_rules.h"
#include "trellis_rules.h"
#include "zxm.h"

#define COVE_ID        32u /* a device id: 16 bytes as hex */
#define COVE_NAME      32u /* a device's name */
#define COVE_BANDS     5u  /* control, settle, media, chat, bulk */
#define COVE_BAND_NAME 15u
#define COVE_SETTLE    1u /* the payments band: never a tunnel's */
#define COVE_TUNNELS   8u
#define COVE_KEY_HEX   64u
#define COVE_FRAME_HEX (2u * 2096u) /* 2 * COVE_FRAME_MAX */
#define COVE_DATA_MAX  2048u        /* COVE_PAYLOAD_MAX */
#define COVE_SUMMARY   383u

typedef struct {
    char id[COVE_ID + 1];
    char name[COVE_NAME + 1];
    int64_t state; /* TRELLIS_ACTIVE or TRELLIS_REVOKED */
    bool self;
} cove_device_t;

typedef struct {
    char name[COVE_BAND_NAME + 1];
    int64_t max_frame;
} cove_band_t;

typedef struct {
    bool open;
    char peer[COVE_ID + 1]; /* the far device's id */
    uint32_t band;
    uint32_t chan, epoch;
    uint32_t sender, peer_sender;
    int64_t seq_out; /* the last seq sealed here, 0 for none */
    int64_t seq_in;  /* the last seq opened from the far end, 0 for none */
    int64_t gap_in;  /* the last gap opened (CM5): 1 in order */
    int64_t stamp;   /* when it last moved, ms since 1970 */
} cove_tunnel_t;

typedef struct {
    zxm_link_t *link;
    /* CM1 */
    bool known, joined;
    char self[COVE_ID + 1];
    bool roster_known;
    uint32_t devices;
    cove_device_t device[COVE_DEVICES];
    /* CM2 */
    uint32_t nbands;
    cove_band_t band[COVE_BANDS];
    /* CM3 */
    cove_tunnel_t tunnel[COVE_TUNNELS];
    /* CM4, CM5: the last frame sealed (hex) and the last data opened */
    char frame_hex[COVE_FRAME_HEX + 1];
    uint8_t data[COVE_DATA_MAX];
    uint32_t ndata;
    char notice[COVE_SUMMARY + 1];
} cove_t;

void cove_init(cove_t *c, zxm_link_t *link);
/* CM1: devmesh.status; then devmesh.devices. */
int cove_read_status(cove_t *c);
int cove_read_devices(cove_t *c);
/* the roster row of a device id, or -1 */
int32_t cove_find_device(const cove_t *c, const char *id);
/* CM2: ehop.bands; the index of a band by name, or -1. */
int cove_read_bands(cove_t *c);
int32_t cove_band_index(const cove_t *c, const char *name);
/* CR1 between this device and roster row i, from what the model knows. */
bool cove_may_open(const cove_t *c, uint32_t i);
/* CM3: open a tunnel; the tunnel's index (0..COVE_TUNNELS - 1) or a ZXM_E_*
 * code: ZXM_E_ARG on an argument of no form, ZXM_E_STATE when the mesh,
 * the roster or the bands are not known, the rule refuses (the notice says
 * why) or every tunnel is in use. */
int cove_tunnel_open(cove_t *c, const char *peer_id, const char *band, uint32_t chan,
                     uint32_t epoch, uint32_t sender, uint32_t peer_sender, int64_t now_ms);
/* CM3: close tunnel t (ZXM_E_ARG when it is not open). */
int cove_tunnel_close(cove_t *c, uint32_t t);
/* CM4: seal n bytes of data on tunnel t; the frame lands in c->frame_hex. */
int cove_seal_frame(cove_t *c, uint32_t t, const char *key_hex, const uint8_t *data, uint32_t n,
                    int64_t now_ms);
/* CM5: open frame_hex on tunnel t; the data lands in c->data, c->ndata. */
int cove_open_frame(cove_t *c, uint32_t t, const char *key_hex, const char *frame_hex,
                    int64_t now_ms);
/* CM6: active roster devices by trellis.peers_in_state; TRELLIS_REFUSED
 * while the roster is unknown or empty. */
int64_t cove_active_devices(const cove_t *c);
/* CM7: CR4 on tunnel t as it stands (false when it is not open). */
bool cove_record_ok(const cove_t *c, uint32_t t);

#endif

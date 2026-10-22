/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cove_model.c — Cove, private tunnels between your own devices (see
 * cove_model.h). Every answer is checked whole before anything of it is
 * kept, every frame is checked before zxvd is asked, and a channel secret
 * never outlives the call that carried it. */
#include <stdio.h>
#include <string.h>

#include "cove_model.h"

/* ---- forms ------------------------------------------------------------------------ */

/* exactly len hex digits (any count when len is 0) */
static bool hex_ok(const char *s, uint32_t len)
{
    uint32_t i = 0;
    if (!s) return false;
    for (; s[i]; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return len == 0 || i == len;
}

/* a count zxvd sends as a decimal string: 1..20 digits, no leading zero, within uint64 */
static bool digits_u64(const char *s, uint64_t *v)
{
    uint64_t x = 0;
    uint32_t i = 0;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9' || i >= 20) return false;
        if (i == 1 && s[0] == '0') return false;
        if (x > (UINT64_MAX - (uint64_t) (s[i] - '0')) / 10u) return false;
        x = x * 10u + (uint64_t) (s[i] - '0');
    }
    if (!i) return false;
    *v = x;
    return true;
}

static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void to_hex(const uint8_t *d, uint32_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n; i++) {
        out[2u * i] = H[d[i] >> 4];
        out[2u * i + 1u] = H[d[i] & 15u];
    }
    out[2u * n] = 0;
}

/* the bytes of an even count of hex digits, or -1 */
static int32_t from_hex(const char *s, uint8_t *out, uint32_t cap)
{
    size_t len = strlen(s);
    if (len % 2u || len / 2u > cap) return -1;
    for (size_t i = 0; i < len / 2u; i++) {
        int hi = nibble(s[2u * i]), lo = nibble(s[2u * i + 1u]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t) (hi * 16 + lo);
    }
    return (int32_t) (len / 2u);
}

static bool get_flag(const zxm_link_t *l, int32_t o, const char *k, bool *v)
{
    return zxm_get_bool(l, o, k, v) == ZXM_OK;
}

static bool stamp_ok(int64_t now_ms)
{
    return now_ms >= 0 && now_ms <= COVE_STAMP_MAX;
}

/* CM4: the request that carried a channel secret is wiped as soon as
 * zxvd answers, whatever it answers */
static int call_wiped(zxm_link_t *l)
{
    int rc = zxm_call(l);
    memset(l->req, 0, sizeof l->req);
    l->req_len = 0;
    return rc;
}

/* ---- CM1: the mesh ------------------------------------------------------------------ */

void cove_init(cove_t *c, zxm_link_t *link)
{
    memset(c, 0, sizeof *c);
    c->link = link;
}

int cove_read_status(cove_t *c)
{
    zxm_link_t *l = c->link;
    bool joined;
    char self[COVE_ID + 1];
    zxm_begin(l, "devmesh.status");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ || !get_flag(l, r, "joined", &joined) ||
        zxm_get_str(l, r, "self", self, sizeof self) != ZXM_OK || !hex_ok(self, COVE_ID))
        return ZXM_E_PROTOCOL;
    c->known = true;
    c->joined = joined;
    memcpy(c->self, self, sizeof self);
    snprintf(c->notice, sizeof c->notice, "%s",
             joined ? "In your device mesh: tunnels may open to your active devices."
                    : "Not in a device mesh yet: pair a device first.");
    return ZXM_OK;
}

int cove_read_devices(cove_t *c)
{
    zxm_link_t *l = c->link;
    cove_device_t got[COVE_DEVICES];
    zxm_begin(l, "devmesh.devices");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, r, "devices");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), selves = 0;
    if (n > COVE_DEVICES) return ZXM_E_PROTOCOL; /* more than devmesh holds */
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        cove_device_t *d = &got[i];
        char status[16];
        memset(d, 0, sizeof *d);
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "id", d->id, sizeof d->id) != ZXM_OK || !hex_ok(d->id, COVE_ID) ||
            zxm_get_str(l, o, "name", d->name, sizeof d->name) != ZXM_OK ||
            zxm_get_str(l, o, "status", status, sizeof status) != ZXM_OK ||
            !get_flag(l, o, "self", &d->self))
            return ZXM_E_PROTOCOL;
        if (!strcmp(status, "active"))
            d->state = TRELLIS_ACTIVE;
        else if (!strcmp(status, "revoked"))
            d->state = TRELLIS_REVOKED;
        else
            return ZXM_E_PROTOCOL;
        selves += d->self;
    }
    if (selves > 1) return ZXM_E_PROTOCOL; /* one device is this one, at most */
    memcpy(c->device, got, n * sizeof got[0]);
    c->devices = n;
    c->roster_known = true;
    uint32_t active = 0;
    for (uint32_t i = 0; i < n; i++) active += got[i].state == TRELLIS_ACTIVE;
    snprintf(c->notice, sizeof c->notice, "%u device%s on your roster, %u active.", n,
             n == 1 ? "" : "s", active);
    return ZXM_OK;
}

int32_t cove_find_device(const cove_t *c, const char *id)
{
    if (!c || !id) return -1;
    for (uint32_t i = 0; i < c->devices; i++)
        if (!strcmp(c->device[i].id, id)) return (int32_t) i;
    return -1;
}

/* ---- CM2: the bands ----------------------------------------------------------------- */

int cove_read_bands(cove_t *c)
{
    cove_band_t next[COVE_BANDS];
    zxm_link_t *l = c->link;
    zxm_begin(l, "ehop.bands");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "bands");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (!n || n > COVE_BANDS) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        int64_t idx;
        memset(&next[i], 0, sizeof next[i]);
        if (o < 0 || l->tok[o].type != W4_J_OBJ || zxm_get_i64(l, o, "band", &idx) != ZXM_OK ||
            idx != (int64_t) i ||
            zxm_get_str(l, o, "name", next[i].name, sizeof next[i].name) != ZXM_OK ||
            !next[i].name[0] || zxm_get_i64(l, o, "max_frame", &next[i].max_frame) != ZXM_OK ||
            next[i].max_frame <= COVE_FRAME_MIN || next[i].max_frame > COVE_FRAME_MAX)
            return ZXM_E_PROTOCOL;
    }
    memcpy(c->band, next, n * sizeof next[0]);
    c->nbands = n;
    return ZXM_OK;
}

int32_t cove_band_index(const cove_t *c, const char *name)
{
    for (uint32_t i = 0; c && name && i < c->nbands; i++)
        if (!strcmp(c->band[i].name, name)) return (int32_t) i;
    return -1;
}

/* ---- CM3: tunnels ------------------------------------------------------------------- */

static int32_t self_row(const cove_t *c)
{
    for (uint32_t i = 0; i < c->devices; i++)
        if (c->device[i].self && !strcmp(c->device[i].id, c->self)) return (int32_t) i;
    return -1;
}

bool cove_may_open(const cove_t *c, uint32_t i)
{
    if (!c || !c->known || !c->joined || !c->roster_known || i >= c->devices) return false;
    int32_t s = self_row(c);
    if (s < 0 || (uint32_t) s == i) return false; /* this device is not on the roster, or is i */
    const cove_device_t *me = &c->device[s], *peer = &c->device[i];
    return cove_tunnel_open_ok(me->state == TRELLIS_ACTIVE, me->state == TRELLIS_REVOKED,
                               peer->state == TRELLIS_ACTIVE, peer->state == TRELLIS_REVOKED);
}

static bool tunnel_fits(const cove_t *c, uint32_t t, int64_t seq, int64_t stamp)
{
    const cove_tunnel_t *x = &c->tunnel[t];
    int32_t peer = cove_find_device(c, x->peer);
    return peer >= 0 && cove_tunnel_record_ok(t, peer, x->band, x->chan, x->epoch, seq, stamp);
}

int cove_tunnel_open(cove_t *c, const char *peer_id, const char *band, uint32_t chan,
                     uint32_t epoch, uint32_t sender, uint32_t peer_sender, int64_t now_ms)
{
    if (!hex_ok(peer_id, COVE_ID) || !band || sender == peer_sender || !stamp_ok(now_ms))
        return ZXM_E_ARG;
    if (!c->known || !c->roster_known || !c->nbands) return ZXM_E_STATE;
    int32_t b = cove_band_index(c, band);
    if (b < 0) return ZXM_E_ARG;
    if ((uint32_t) b == COVE_SETTLE) {
        snprintf(c->notice, sizeof c->notice,
                 "The %s band carries payments; a tunnel carries your data only.", band);
        return ZXM_E_ARG;
    }
    int32_t i = cove_find_device(c, peer_id);
    if (i < 0 || !cove_may_open(c, (uint32_t) i)) {
        int32_t s = self_row(c);
        const char *why = !c->joined ? "this device is in no mesh"
                          : i < 0    ? "that device is not on your roster"
                          : s < 0    ? "this device is not on the roster"
                          : s == i   ? "it is this device"
                          : c->device[s].state != TRELLIS_ACTIVE ? "this device is revoked"
                                                                 : "that device is revoked";
        snprintf(c->notice, sizeof c->notice, "No tunnel: %s.", why);
        return ZXM_E_STATE;
    }
    uint32_t t = 0;
    while (t < COVE_TUNNELS && c->tunnel[t].open) t++;
    if (t == COVE_TUNNELS) {
        snprintf(c->notice, sizeof c->notice, "Every tunnel is in use: close one first.");
        return ZXM_E_STATE;
    }
    cove_tunnel_t *x = &c->tunnel[t];
    memset(x, 0, sizeof *x);
    memcpy(x->peer, peer_id, COVE_ID + 1);
    x->band = (uint32_t) b;
    x->chan = chan;
    x->epoch = epoch;
    x->sender = sender;
    x->peer_sender = peer_sender;
    x->stamp = now_ms;
    if (!tunnel_fits(c, t, 0, now_ms)) { /* CM7 */
        memset(x, 0, sizeof *x);
        return ZXM_E_STATE;
    }
    x->open = true;
    snprintf(c->notice, sizeof c->notice, "A tunnel to %s is open on the %s band.",
             c->device[i].name, band);
    return (int) t;
}

int cove_tunnel_close(cove_t *c, uint32_t t)
{
    if (t >= COVE_TUNNELS || !c->tunnel[t].open) return ZXM_E_ARG;
    memset(&c->tunnel[t], 0, sizeof c->tunnel[t]);
    snprintf(c->notice, sizeof c->notice, "The tunnel is closed.");
    return ZXM_OK;
}

/* CM4, CM5: the tunnel is open, both ends still may (the roster as it is
 * now), and the key is a key. A tunnel whose device was revoked closes. */
static int tunnel_ok(cove_t *c, uint32_t t, const char *key_hex)
{
    if (t >= COVE_TUNNELS || !c->tunnel[t].open) return ZXM_E_ARG;
    int32_t i = cove_find_device(c, c->tunnel[t].peer);
    if (i < 0 || !cove_may_open(c, (uint32_t) i)) {
        memset(&c->tunnel[t], 0, sizeof c->tunnel[t]);
        snprintf(c->notice, sizeof c->notice,
                 "The tunnel closed: a device at one end is no longer active on your roster.");
        return ZXM_E_STATE;
    }
    if (!hex_ok(key_hex, COVE_KEY_HEX)) return ZXM_E_ARG;
    return ZXM_OK;
}

static void put_chan(zxm_link_t *l, const cove_t *c, const cove_tunnel_t *x, const char *key_hex)
{
    zxm_p_str(l, "key_hex", key_hex);
    zxm_p_str(l, "band", c->band[x->band].name);
    zxm_p_u64(l, "chan_id", x->chan);
    zxm_p_u64(l, "epoch", x->epoch);
}

/* CR3 for a frame of framelen bytes on the tunnel's band */
static bool frame_fits(const cove_t *c, const cove_tunnel_t *x, int64_t framelen)
{
    return cove_tunnel_frame_ok(framelen, c->band[x->band].max_frame - framelen);
}

int cove_seal_frame(cove_t *c, uint32_t t, const char *key_hex, const uint8_t *data, uint32_t n,
                    int64_t now_ms)
{
    static char hex[2u * COVE_DATA_MAX + 1u];
    static char frame[COVE_FRAME_HEX + 1u];
    int rc = tunnel_ok(c, t, key_hex);
    if (rc != ZXM_OK) return rc;
    cove_tunnel_t *x = &c->tunnel[t];
    if ((!data && n) || n > COVE_DATA_MAX || !stamp_ok(now_ms)) return ZXM_E_ARG;
    if (!frame_fits(c, x, (int64_t) n + COVE_OVERHEAD)) return ZXM_E_ARG; /* CR3 */
    int64_t seq = x->seq_out + 1;
    if (x->seq_out >= COVE_SEQ_MAX || cove_tunnel_seq_gap(x->seq_out, seq) != 1 ||
        !tunnel_fits(c, t, seq, now_ms)) {
        snprintf(c->notice, sizeof c->notice,
                 "This channel's seqs are spent: open the tunnel again on a new epoch.");
        return ZXM_E_STATE; /* CR2, CM7 */
    }
    x->seq_out = seq; /* CM4: taken before the call, never given again */
    x->stamp = now_ms;
    to_hex(data, n, hex);
    zxm_link_t *l = c->link;
    zxm_begin(l, "ehop.seal");
    put_chan(l, c, x, key_hex);
    zxm_p_u64(l, "sender", x->sender);
    zxm_p_u64(l, "seq", (uint64_t) seq);
    zxm_p_strn(l, "plaintext_hex", hex, 2u * n);
    rc = call_wiped(l);
    memset(hex, 0, sizeof hex);
    if (rc != ZXM_OK) return rc;
    int64_t len;
    if (zxm_get_str(l, l->result, "frame_hex", frame, sizeof frame) != ZXM_OK ||
        zxm_get_i64(l, l->result, "frame_len", &len) != ZXM_OK ||
        len != (int64_t) n + COVE_OVERHEAD || !frame_fits(c, x, len) ||
        strlen(frame) != 2u * (uint32_t) len)
        return ZXM_E_PROTOCOL;
    memcpy(c->frame_hex, frame, 2u * (uint32_t) len + 1u);
    snprintf(c->notice, sizeof c->notice, "Sealed %u byte%s as frame %lld (%lld bytes).", n,
             n == 1 ? "" : "s", (long long) seq, (long long) len);
    return ZXM_OK;
}

int cove_open_frame(cove_t *c, uint32_t t, const char *key_hex, const char *frame_hex,
                    int64_t now_ms)
{
    static char hex[2u * COVE_DATA_MAX + 1u];
    static uint8_t buf[COVE_DATA_MAX];
    char seqs[24];
    int64_t sender;
    uint64_t seq;
    int rc = tunnel_ok(c, t, key_hex);
    if (rc != ZXM_OK) return rc;
    cove_tunnel_t *x = &c->tunnel[t];
    if (!hex_ok(frame_hex, 0) || !stamp_ok(now_ms)) return ZXM_E_ARG;
    size_t flen = strlen(frame_hex);
    if (flen % 2u || flen > COVE_FRAME_HEX || !frame_fits(c, x, (int64_t) (flen / 2u)))
        return ZXM_E_ARG; /* CR3, before ehop.open is asked */
    zxm_link_t *l = c->link;
    zxm_begin(l, "ehop.open");
    put_chan(l, c, x, key_hex);
    zxm_p_str(l, "frame_hex", frame_hex);
    rc = call_wiped(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "sender", &sender) != ZXM_OK || sender < 0 ||
        sender > (int64_t) UINT32_MAX ||
        zxm_get_str(l, l->result, "seq", seqs, sizeof seqs) != ZXM_OK || !digits_u64(seqs, &seq) ||
        zxm_get_str(l, l->result, "plaintext_hex", hex, sizeof hex) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int32_t n = from_hex(hex, buf, sizeof buf);
    memset(hex, 0, sizeof hex);
    if (n < 0 || (int64_t) n + COVE_OVERHEAD != (int64_t) (flen / 2u)) return ZXM_E_PROTOCOL;
    /* CM5: only the far end's sender, and only a seq past the last (CR2) */
    if ((uint32_t) sender != x->peer_sender) {
        snprintf(c->notice, sizeof c->notice, "Not from the far end of this tunnel: dropped.");
        return ZXM_E_STATE;
    }
    int64_t gap = seq > (uint64_t) COVE_SEQ_MAX ? COVE_REFUSED
                                                : cove_tunnel_seq_gap(x->seq_in, (int64_t) seq);
    if (gap == COVE_REFUSED) {
        snprintf(c->notice, sizeof c->notice,
                 "Frame %s was seen already or is past the channel: dropped.", seqs);
        return ZXM_E_STATE;
    }
    memcpy(c->data, buf, (size_t) n);
    c->ndata = (uint32_t) n;
    x->seq_in = (int64_t) seq;
    x->gap_in = gap;
    x->stamp = now_ms;
    if (gap > 1)
        snprintf(c->notice, sizeof c->notice, "Opened frame %s; %lld before it never came.", seqs,
                 (long long) (gap - 1));
    else
        snprintf(c->notice, sizeof c->notice, "Opened frame %s, %d byte%s.", seqs, (int) n,
                 n == 1 ? "" : "s");
    return ZXM_OK;
}

/* ---- CM6, CM7 ------------------------------------------------------------------------ */

int64_t cove_active_devices(const cove_t *c)
{
    int64_t s[COVE_DEVICES], w[COVE_DEVICES];
    if (!c || !c->roster_known) return TRELLIS_REFUSED;
    for (uint32_t i = 0; i < c->devices; i++) {
        s[i] = c->device[i].state;
        w[i] = TRELLIS_ACTIVE;
    }
    return trellis_peers_in_state(s, w, c->devices);
}

bool cove_record_ok(const cove_t *c, uint32_t t)
{
    if (!c || t >= COVE_TUNNELS || !c->tunnel[t].open) return false;
    return tunnel_fits(c, t, c->tunnel[t].seq_out, c->tunnel[t].stamp);
}

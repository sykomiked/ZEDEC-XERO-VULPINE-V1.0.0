/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* updraft_model.c — Updraft, signal station (see updraft_model.h). Every
 * argument is checked before the owner's word is asked for, the owner's
 * word before anything is sent, and every answer whole before any of it is
 * kept. Nothing here keys a transmitter. */
#include <stdio.h>
#include <string.h>

#include "commons_rules.h"
#include "trellis_rules.h"
#include "updraft_model.h"

/* ---- forms ------------------------------------------------------------------------ */

static bool hex_ok(const char *s, uint32_t len)
{
    uint32_t i = 0;
    if (!s) return false;
    for (; s[i]; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return i == len;
}

/* XXXXX-XXXXX of 0-9 and A-Z (devmesh.invite's code) */
static bool code_ok(const char *s)
{
    if (!s) return false;
    for (uint32_t i = 0; i < UPDRAFT_CODE; i++) {
        char c = s[i];
        if (i == 5) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')))
            return false;
    }
    return s[UPDRAFT_CODE] == 0;
}

/* a count zxvd sends as a decimal string: 1..15 digits, no leading zero */
static bool digits_val(const char *s, int64_t *v)
{
    int64_t x = 0;
    uint32_t i = 0;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9' || i >= 15) return false;
        if (i == 1 && s[0] == '0') return false;
        x = x * 10 + (s[i] - '0');
    }
    if (!i) return false;
    *v = x;
    return true;
}

static bool get_flag(const zxm_link_t *l, int32_t obj, const char *key, bool *v)
{
    return zxm_get_bool(l, obj, key, v) == ZXM_OK;
}

static bool stamp_ok(int64_t ms)
{
    return ms >= 0 && ms <= UPDRAFT_MS_MAX;
}

static const char *const RF_NAME[UPDRAFT_RF] = {"6 m", "2 m", "70 cm"};

/* ---- init ------------------------------------------------------------------------- */

void updraft_init(updraft_t *u, zxm_link_t *link)
{
    memset(u, 0, sizeof *u);
    u->link = link;
    aerie_init(&u->radio, link);
    u->watch_ms = -1;
    u->frame_rate = -1;
    u->devices = -1;
}

/* ---- UM1: the licence --------------------------------------------------------------- */

int updraft_set_licence(updraft_t *u, int64_t rf, int64_t licmax, bool owner_confirmed)
{
    if (!u || rf < 0 || rf >= UPDRAFT_RF || licmax < 0 || licmax > UPDRAFT_POWER_MAX)
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    u->licmax[rf] = licmax;
    u->licence_set[rf] = true;
    snprintf(u->notice, sizeof u->notice,
             "Licence on %s: at most %lld dBm. You key the transmitter; Updraft only advises.",
             RF_NAME[rf], (long long) licmax);
    return ZXM_OK;
}

bool updraft_may_transmit(updraft_t *u, int64_t rf, int64_t power)
{
    if (!u || rf < 0 || rf >= UPDRAFT_RF) return false;
    if (!u->licence_set[rf]) {
        snprintf(u->notice, sizeof u->notice, "No licence given for %s: do not transmit.",
                 RF_NAME[rf]);
        return false;
    }
    if (power < 0 || power > UPDRAFT_POWER_MAX) return false;
    bool ok = updraft_transmit_power_ok(power, u->licmax[rf] - power); /* UR2 */
    snprintf(u->notice, sizeof u->notice, "%lld dBm on %s is %s your licence (%lld dBm).",
             (long long) power, RF_NAME[rf], ok ? "within" : "past", (long long) u->licmax[rf]);
    return ok;
}

/* ---- UM2: pairing a relay station ----------------------------------------------------- */

int updraft_invite(updraft_t *u, int64_t now_ms, bool owner_confirmed)
{
    char code[UPDRAFT_CODE + 1];
    int64_t ttl;
    if (!u || !stamp_ok(now_ms)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    zxm_link_t *l = u->link;
    zxm_begin(l, "devmesh.invite");
    zxm_p_str(l, "role", "thin");
    zxm_p_bool(l, "held", false);
    zxm_p_bool(l, "admin", false);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "code", code, sizeof code) != ZXM_OK || !code_ok(code) ||
        zxm_get_i64(l, l->result, "expires_in_ms", &ttl) != ZXM_OK || ttl < 1 ||
        ttl > UPDRAFT_INVITE_TTL_MS)
        return ZXM_E_PROTOCOL;
    memcpy(u->code, code, sizeof code);
    u->issued_ms = now_ms;
    u->ttl_ms = ttl;
    u->invited = true;
    snprintf(u->notice, sizeof u->notice,
             "Relay invitation %s: valid %lld minutes. Type it on the relay station.", code,
             (long long) trellis_invite_minutes_left(now_ms, ttl, now_ms));
    return ZXM_OK;
}

int updraft_join(updraft_t *u, const char *code, bool owner_confirmed)
{
    bool pairing;
    if (!u || !code_ok(code)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    zxm_link_t *l = u->link;
    zxm_begin(l, "devmesh.join");
    zxm_p_str(l, "code", code);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (!get_flag(l, l->result, "pairing", &pairing)) return ZXM_E_PROTOCOL;
    u->pairing = pairing;
    snprintf(u->notice, sizeof u->notice, "%s",
             pairing ? "Pairing: compare the six digits on both stations."
                     : "The invitation was not taken; nothing is paired.");
    return ZXM_OK;
}

int updraft_confirm_sas(updraft_t *u, const char *device, bool match, bool owner_confirmed)
{
    bool confirmed, in_roster;
    int64_t devices;
    if (!u || !hex_ok(device, UPDRAFT_DEVICE)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    zxm_link_t *l = u->link;
    zxm_begin(l, "devmesh.confirm_sas");
    zxm_p_str(l, "device", device);
    zxm_p_bool(l, "match", match);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (!get_flag(l, l->result, "confirmed", &confirmed) ||
        !get_flag(l, l->result, "in_roster", &in_roster) ||
        zxm_get_i64(l, l->result, "devices", &devices) != ZXM_OK || devices < 0 ||
        devices > (int64_t) TRELLIS_DEVICES)
        return ZXM_E_PROTOCOL;
    u->pairing = false;
    u->devices = devices;
    if (!match)
        snprintf(u->notice, sizeof u->notice,
                 "The digits did not match: pairing stopped. Someone may be in the middle.");
    else
        snprintf(u->notice, sizeof u->notice, "%s %lld stations in your mesh.",
                 in_roster ? "Relay paired." : "Confirmed; the inviting station adds it.",
                 (long long) devices);
    return ZXM_OK;
}

/* ---- UM3: links ------------------------------------------------------------------------ */

int updraft_set_link(updraft_t *u, int64_t src, int64_t dst, int64_t power, int64_t step,
                     int64_t rf, int64_t rxfloor)
{
    if (!u || src < 0 || src > UPDRAFT_STATION_MAX || dst < 0 || dst > UPDRAFT_STATION_MAX ||
        src == dst)
        return ZXM_E_ARG;
    int64_t margin = updraft_link_margin(power, step, rf, rxfloor); /* UR1 */
    if (margin == UPDRAFT_REFUSED) return ZXM_E_ARG;
    uint32_t at = u->nlinks;
    for (uint32_t i = 0; i < u->nlinks; i++)
        if (u->links[i].src == src && u->links[i].dst == dst) at = i;
    if (at == UPDRAFT_LINKS) return ZXM_E_STATE;
    updraft_link_t k = {src, dst, power, step, rf, rxfloor, margin};
    u->links[at] = k;
    if (at == u->nlinks) u->nlinks++;
    snprintf(u->notice, sizeof u->notice, "Link %lld to %lld: margin %lld dB%s.", (long long) src,
             (long long) dst, (long long) margin, margin < 0 ? ", below the far end's floor" : "");
    return (int) at;
}

int64_t updraft_link_state_at(const updraft_t *u, uint32_t i)
{
    int64_t src[UPDRAFT_LINKS], dst[UPDRAFT_LINKS], margin[UPDRAFT_LINKS];
    if (!u || i >= u->nlinks) return UPDRAFT_REFUSED;
    for (uint32_t j = 0; j < u->nlinks; j++) {
        src[j] = u->links[j].src;
        dst[j] = u->links[j].dst;
        margin[j] = u->links[j].margin;
    }
    return updraft_link_state(src, dst, margin, u->nlinks, i); /* UR3 */
}

/* ---- UM4: relaying ----------------------------------------------------------------------- */

int updraft_bands(updraft_t *u)
{
    if (!u) return ZXM_E_ARG;
    return aerie_bands(&u->radio);
}

int updraft_allow_bands(updraft_t *u, uint32_t mask, bool owner_confirmed)
{
    if (!u) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    return aerie_set_bands(&u->radio, mask);
}

int updraft_relay(updraft_t *u, const aerie_chan_t *ch, bool sealed, const uint8_t *data,
                  uint32_t n, int64_t now_ms, bool owner_confirmed)
{
    if (!u || !ch || (!data && n) || !stamp_ok(now_ms)) return ZXM_E_ARG;
    bool amateur = ch->bearer == AERIE_AMATEUR;
    if (ch->bearer != AERIE_PRIVATE && !amateur) return ZXM_E_ARG;
    /* AR5 before anything else: no secrets on amateur radio */
    if (!aerie_ham_clear_ok(amateur ? 1 : 0, sealed ? 1 : 0)) return ZXM_E_STATE;
    if (!amateur && !sealed) return ZXM_E_ARG; /* a private link carries sealed frames only */
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = amateur ? aerie_ham_compose(&u->radio, (const char *) data, n, now_ms)
                     : aerie_seal(&u->radio, ch, data, n, now_ms);
    if (rc != ZXM_OK) return rc;
    u->relayed++;
    snprintf(u->notice, sizeof u->notice, "%s",
             amateur ? "Composed in the clear for amateur radio: you key it, unsealed and signed."
                     : "Sealed for a private link; nothing is transmitted until you send it.");
    return ZXM_OK;
}

/* ---- UM5, UM6: hearing, and one frame many relays heard -------------------------------- */

int64_t updraft_fletcher(const uint8_t *data, uint32_t n)
{
    uint32_t s1 = 0, s2 = 0;
    if (!data && n) return -1;
    for (uint32_t i = 0; i < n; i++) {
        s1 = (s1 + data[i]) % 65535u;
        s2 = (s2 + s1) % 65535u;
    }
    return (int64_t) s2 * 65536 + s1;
}

static int32_t frame_at(const updraft_t *u, int64_t frame)
{
    for (uint32_t i = 0; i < u->nframes; i++)
        if (u->frames[i].frame == frame) return (int32_t) i;
    return -1;
}

static void settle(updraft_frame_t *f)
{
    int64_t d[UPDRAFT_RELAYS];
    for (uint32_t i = 0; i < f->nrep; i++) d[i] = f->rep[i].payload;
    f->state = updraft_frame_join(d, f->nrep, f->has_sum, f->sum, &f->settled); /* UR5 */
}

int updraft_report(updraft_t *u, int64_t frame, int64_t relay, int64_t parent, int64_t hops,
                   const uint8_t *data, uint32_t n)
{
    if (!u || (!data && n) || frame < 0 || frame > UPDRAFT_FRAME_MAX || relay < 1 ||
        relay > UPDRAFT_STATION_MAX || parent < 0 || parent > UPDRAFT_STATION_MAX ||
        parent == relay || hops < 1 || hops > UPDRAFT_HOPS_MAX)
        return ZXM_E_ARG;
    int64_t digest = updraft_fletcher(data, n);
    if (!updraft_relay_node_fits(frame, relay, parent, hops, digest, LPRES_STATE_BOTH)) /* UR4 */
        return ZXM_E_ARG;
    int32_t at = frame_at(u, frame);
    if (at >= 0) {
        updraft_frame_t *f = &u->frames[at];
        for (uint32_t i = 0; i < f->nrep; i++)
            if (f->rep[i].relay == relay) return ZXM_E_STATE; /* one report a relay */
        if (f->nrep == UPDRAFT_RELAYS) return ZXM_E_STATE;
    } else {
        if (u->nframes == UPDRAFT_FRAMES) {
            memmove(&u->frames[0], &u->frames[1], (UPDRAFT_FRAMES - 1u) * sizeof u->frames[0]);
            u->nframes--;
        }
        at = (int32_t) u->nframes++;
        memset(&u->frames[at], 0, sizeof u->frames[at]);
        u->frames[at].frame = frame;
        u->frames[at].settled = -1;
    }
    updraft_frame_t *f = &u->frames[at];
    updraft_report_t r = {relay, parent, hops, digest};
    f->rep[f->nrep++] = r;
    u->heard++;
    settle(f);
    snprintf(u->notice, sizeof u->notice, "Frame %lld: %u relay%s, %s.", (long long) frame, f->nrep,
             f->nrep == 1 ? "" : "s",
             f->state == LPRES_STATE_BOTH    ? "heard two ways, held until it settles"
             : f->state == LPRES_STATE_TRUE  ? "settled"
             : f->state == LPRES_STATE_FALSE ? "no relay heard what its checksum says"
                                             : "not heard");
    return ZXM_OK;
}

int updraft_hear(updraft_t *u, const aerie_chan_t *ch, const char *frame_hex, int64_t now_ms,
                 int64_t frame, int64_t relay, int64_t parent, int64_t hops)
{
    if (!u || !ch || frame < 0 || frame > UPDRAFT_FRAME_MAX || relay < 1 ||
        relay > UPDRAFT_STATION_MAX || parent < 0 || parent > UPDRAFT_STATION_MAX ||
        parent == relay || hops < 1 || hops > UPDRAFT_HOPS_MAX)
        return ZXM_E_ARG;
    int rc = aerie_open(&u->radio, ch, frame_hex, now_ms);
    if (rc != ZXM_OK) return rc;
    return updraft_report(u, frame, relay, parent, hops, u->radio.data, u->radio.ndata);
}

int updraft_frame_checksum(updraft_t *u, int64_t frame, int64_t sum)
{
    if (!u || sum < 0 || sum > UPDRAFT_DIGEST_MAX) return ZXM_E_ARG;
    int32_t at = frame_at(u, frame);
    if (at < 0) return ZXM_E_STATE;
    u->frames[at].has_sum = true;
    u->frames[at].sum = sum;
    settle(&u->frames[at]);
    return ZXM_OK;
}

const updraft_frame_t *updraft_frame(const updraft_t *u, int64_t frame)
{
    int32_t at = u ? frame_at(u, frame) : -1;
    return at < 0 ? NULL : &u->frames[at];
}

/* ---- UM7: the watch ----------------------------------------------------------------------- */

int updraft_watch(updraft_t *u, int64_t now_ms)
{
    static updraft_peer_t got[UPDRAFT_PEERS];
    int64_t state[UPDRAFT_PEERS], want[UPDRAFT_PEERS];
    if (!u || !stamp_ok(now_ms) || (u->watch_ms >= 0 && now_ms < u->watch_ms)) return ZXM_E_ARG;
    zxm_link_t *l = u->link;
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, r, "peers");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), kept = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        updraft_peer_t p;
        char seen[24];
        memset(&p, 0, sizeof p);
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "id", p.id, sizeof p.id) != ZXM_OK || !hex_ok(p.id, UPDRAFT_NODE) ||
            zxm_get_str(l, o, "last_seen_ms", seen, sizeof seen) != ZXM_OK ||
            !digits_val(seen, &p.last_seen_ms) || !get_flag(l, o, "member", &p.member))
            return ZXM_E_PROTOCOL;
        p.state = now_ms - p.last_seen_ms <= UPDRAFT_STALE_MS ? TRELLIS_ACTIVE : TRELLIS_WAITING;
        if (kept < UPDRAFT_PEERS) got[kept++] = p;
    }
    memcpy(u->peers, got, kept * sizeof got[0]);
    u->npeers = kept;
    u->peers_seen = n;
    for (uint32_t i = 0; i < kept; i++) state[i] = u->peers[i].state;
    for (uint32_t i = 0; i < kept; i++) want[i] = TRELLIS_ACTIVE;
    u->live = kept ? trellis_peers_in_state(state, want, kept) : 0; /* trellis.peers_in_state */
    for (uint32_t i = 0; i < kept; i++) want[i] = TRELLIS_WAITING;
    u->stale = kept ? trellis_peers_in_state(state, want, kept) : 0;
    /* sounding.rate over the frames heard since the last watch (UR6) */
    u->frame_rate = u->watch_ms < 0
                        ? -1
                        : updraft_counter_rate((int64_t) u->heard, (int64_t) u->heard_was,
                                               now_ms - u->watch_ms);
    u->watch_ms = now_ms;
    u->heard_was = u->heard;
    snprintf(u->notice, sizeof u->notice, "%u peer%s: %lld live, %lld stale%s.", n,
             n == 1 ? "" : "s", (long long) u->live, (long long) u->stale,
             n ? "" : " (none while the network is off)");
    return ZXM_OK;
}

/* ---- UM8: the bulletin ---------------------------------------------------------------------- */

int updraft_set_room(updraft_t *u, int64_t member, int64_t joined, int64_t enabled)
{
    if (!u || member < 0 || member > 1 || joined < 0 || joined > 1 || enabled < 0 || enabled > 1)
        return ZXM_E_ARG;
    u->room_member = member;
    u->room_joined = joined;
    u->room_enabled = enabled;
    return ZXM_OK;
}

bool updraft_bulletin_ok(const updraft_t *u, int64_t frame)
{
    const updraft_frame_t *f = updraft_frame(u, frame);
    return f && f->state == LPRES_STATE_TRUE &&
           commons_room_post_ok(u->room_member, u->room_joined, u->room_enabled);
}

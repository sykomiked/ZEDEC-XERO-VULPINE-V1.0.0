/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pannier_model.c — Pannier, the store-and-forward carrier (pannier_model.h). */
#include <stdio.h>
#include <string.h>

#include "pannier_model.h"

#define PANNIER_LINE_MAX 320u                /* one line of the file */
#define PANNIER_FIELDS   14u                 /* a "c" line's numbers before its path */
#define PANNIER_STAMP    INT64_C(9999999999) /* caravan's stamp, seconds */

void pannier_init(pannier_t *p, zxm_link_t *link)
{
    memset(p, 0, sizeof *p);
    p->link = link;
    p->next_n = 1;
}

static int say(pannier_t *p, int rc, const char *what)
{
    snprintf(p->notice, sizeof p->notice, "%s", what);
    return rc;
}

/* ---- text, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < cap && s[n]) n++;
    return n;
}

/* a Hoard path of 1..PANNIER_PATH_MAX printable bytes, no space */
static bool path_ok(const char *s)
{
    uint32_t n = len_of(s, PANNIER_PATH_MAX + 1u);
    if (!n || n > PANNIER_PATH_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] <= 0x20u || (unsigned char) s[i] >= 0x7fu) return false;
    return true;
}

static bool node_ok(const char *s)
{
    if (len_of(s, PANNIER_NODE + 1u) != PANNIER_NODE) return false;
    for (uint32_t i = 0; i < PANNIER_NODE; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = len_of(s, cap);
    if (*at + n >= cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do {
        t[k++] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) t[k++] = '-';
    if (*at + k >= cap) return false;
    while (k) out[(*at)++] = t[--k];
    out[*at] = 0;
    return true;
}

/* the next whole number of s[*i..n), skipping spaces; false at none */
static bool next_num(const char *s, uint32_t n, uint32_t *i, int64_t *v)
{
    uint32_t k = *i, digits = 0;
    bool neg = false;
    int64_t x = 0;
    while (k < n && s[k] == ' ') k++;
    if (k < n && s[k] == '-') neg = true, k++;
    while (k < n && s[k] >= '0' && s[k] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (s[k] - '0');
        k++;
    }
    if (!digits || (k < n && s[k] != ' ')) return false;
    *v = neg ? -x : x;
    *i = k;
    return true;
}

/* the next word of s[*i..n) into out (cap bytes with its NUL); false at none */
static bool next_word(const char *s, uint32_t n, uint32_t *i, char *out, uint32_t cap)
{
    uint32_t k = *i, w = 0;
    while (k < n && s[k] == ' ') k++;
    while (k < n && s[k] != ' ') {
        if (w + 1u >= cap) return false;
        out[w++] = s[k++];
    }
    out[w] = 0;
    *i = k;
    return w > 0;
}

/* a 64-bit count that travels as a decimal string ("^[0-9]+$") */
static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[24];
    int64_t x = 0;
    if (zxm_get_str(l, obj, key, s, sizeof s) || !s[0]) return ZXM_E_PROTOCOL;
    for (const char *c = s; *c; c++) {
        int64_t d = *c - '0';
        if (*c < '0' || *c > '9' || x > (INT64_MAX - d) / 10) return ZXM_E_PROTOCOL;
        x = x * 10 + d;
    }
    *v = x;
    return ZXM_OK;
}

/* ---- PM1: carrier peers ---- */

static pannier_peer_t *peer_of(pannier_t *p, int64_t peer)
{
    if (!p || peer < 1 || peer > (int64_t) p->npeers) return NULL;
    return &p->peer[peer - 1];
}

static bool peer_fits(const pannier_peer_t *q, int64_t peer)
{
    return trellis_peer_record_fits(peer, q->state, 1, 0, 0, q->seen_ms) &&
           trellis_state_ok(q->state) && q->seen_ms >= 0 && q->last_seq >= 0 &&
           q->last_seq <= COVE_SEQ_MAX && node_ok(q->id);
}

int pannier_add_peer(pannier_t *p, const char *id, int64_t *peer)
{
    if (!p || !node_ok(id)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < p->npeers; i++)
        if (!strcmp(p->peer[i].id, id)) return say(p, ZXM_E_STATE, "That peer is already kept.");
    if (p->npeers >= PANNIER_PEERS) return say(p, ZXM_E_STATE, "No room for another peer.");
    pannier_peer_t *q = &p->peer[p->npeers];
    memset(q, 0, sizeof *q);
    memcpy(q->id, id, PANNIER_NODE);
    q->id[PANNIER_NODE] = 0;
    q->state = TRELLIS_WAITING;
    p->npeers++;
    p->dirty = true;
    if (peer) *peer = (int64_t) p->npeers;
    return ZXM_OK;
}

int pannier_peers(pannier_t *p)
{
    static bool seen[PANNIER_PEERS];
    static int64_t when[PANNIER_PEERS];
    char id[PANNIER_NODE + 2];
    if (!p || !p->link) return ZXM_E_ARG;
    zxm_link_t *l = p->link;
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "peers");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    memset(seen, 0, sizeof seen);
    memset(when, 0, sizeof when);
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        int64_t ms = 0;
        if (o < 0 || zxm_get_str(l, o, "id", id, sizeof id) || !node_ok(id) ||
            get_dec(l, o, "last_seen_ms", &ms))
            return ZXM_E_PROTOCOL;
        for (uint32_t j = 0; j < p->npeers; j++)
            if (!strcmp(p->peer[j].id, id)) seen[j] = true, when[j] = ms;
    }
    for (uint32_t j = 0; j < p->npeers; j++) { /* checked whole, then kept */
        pannier_peer_t *q = &p->peer[j];
        if (q->state == TRELLIS_REVOKED) continue;
        q->state = seen[j] ? TRELLIS_ACTIVE : TRELLIS_WAITING;
        if (seen[j]) q->seen_ms = when[j];
    }
    snprintf(p->notice, sizeof p->notice, "%lld of %u carrier peers active.",
             (long long) pannier_active(p), (unsigned) p->npeers);
    return ZXM_OK;
}

int pannier_revoke(pannier_t *p, int64_t peer, bool owner_confirmed)
{
    pannier_peer_t *q = peer_of(p, peer);
    if (!q) return ZXM_E_ARG;
    if (q->state == TRELLIS_REVOKED) return ZXM_E_STATE;
    if (!owner_confirmed) return say(p, ZXM_E_STATE, "A peer is revoked only on your word.");
    q->state = TRELLIS_REVOKED;
    p->dirty = true;
    return ZXM_OK;
}

int64_t pannier_active(const pannier_t *p)
{
    int64_t state[PANNIER_PEERS], want[PANNIER_PEERS];
    if (!p || !p->npeers) return 0;
    for (uint32_t i = 0; i < p->npeers; i++) {
        state[i] = p->peer[i].state;
        want[i] = TRELLIS_ACTIVE;
    }
    int64_t c = trellis_peers_in_state(state, want, p->npeers); /* trellis.peers_in_state */
    return c == TRELLIS_REFUSED ? 0 : c;
}

/* ---- PM2: the peer book ---- */

int pannier_book(pannier_t *p)
{
    bool ok = false, serving = false;
    if (!p || !p->link) return ZXM_E_ARG;
    zxm_link_t *l = p->link;
    zxm_begin(l, "peer.status");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "book_ok", &ok) ||
        zxm_get_bool(l, l->result, "serving", &serving))
        return ZXM_E_PROTOCOL;
    p->book_known = true;
    p->book_ok = ok;
    p->serving = serving;
    return ZXM_OK;
}

/* ---- PM5: the queue ---- */

int64_t pannier_room(const pannier_t *p)
{
    return p ? pannier_queue_room(p->head, p->tail, PANNIER_QUEUE) : PANNIER_REFUSED;
}

static pannier_parcel_t *slot(pannier_t *p, int64_t i)
{
    return &p->q[(uint64_t) i % PANNIER_QUEUE];
}

static int64_t index_of(const pannier_t *p, int64_t n)
{
    if (!p || n < 1) return -1;
    for (int64_t i = p->tail; i < p->head; i++)
        if (p->q[(uint64_t) i % PANNIER_QUEUE].n == n) return i;
    return -1;
}

pannier_parcel_t *pannier_find(pannier_t *p, int64_t n)
{
    int64_t i = index_of(p, n);
    return i < 0 ? NULL : slot(p, i);
}

static bool closed(const pannier_parcel_t *c)
{
    return c->status == CARAVAN_DELIVERED || c->status == CARAVAN_RETURNED;
}

/* the oldest goes first: the tail passes every closed parcel */
static void advance(pannier_t *p)
{
    while (p->tail < p->head && closed(slot(p, p->tail))) p->tail++;
}

static bool parcel_fits(const pannier_parcel_t *c)
{
    return c->n >= 1 && c->n <= PANNIER_PARCEL_NO &&
           caravan_ship_ok(c->n, c->peer, c->bytes, c->packets, c->status, c->stamp) &&
           pannier_node_ok(c->peer, c->n, c->freights, c->bytes, c->attempt, c->status, c->stamp) &&
           c->peer >= 1 && c->peer <= (int64_t) PANNIER_PEERS && c->bytes >= 1 &&
           c->bytes <= PANNIER_PARCEL_MAX && c->k >= 1 && c->k <= POLLEN_K_MAX &&
           c->freights == pollen_freights(c->bytes, c->k) &&
           c->packets == pollen_packets(c->bytes, c->k) && c->attempt >= 0 &&
           c->attempt <= PANNIER_TRIES && c->next_at >= 0 && c->next_at <= PANNIER_STAMP &&
           (c->kind == PANNIER_MESSAGE || c->kind == PANNIER_DATA) && c->receipts >= 0 &&
           c->receipts <= 2 && (c->lost == 0 || c->lost == 1) && c->witness >= PANNIER_SAID_NONE &&
           c->witness <= PANNIER_SAID_LOST && path_ok(c->path);
}

static int stat_size(pannier_t *p, const char *path, int64_t *size)
{
    bool dir = true;
    uint64_t v = 0;
    zxm_link_t *l = p->link;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "dir", &dir) || zxm_get_u64(l, l->result, "size", &v) ||
        v > (uint64_t) INT64_MAX)
        return ZXM_E_PROTOCOL;
    if (dir) return ZXM_E_ARG;
    *size = (int64_t) v;
    return ZXM_OK;
}

/* a new parcel at the head, every rule checked first */
static int enqueue(pannier_t *p, int64_t peer, int64_t kind, const char *path, int64_t bytes,
                   int64_t k, int64_t now, int64_t *n)
{
    pannier_parcel_t c;
    memset(&c, 0, sizeof c);
    c.n = p->next_n;
    c.peer = peer;
    c.kind = kind;
    memcpy(c.path, path, len_of(path, PANNIER_PATH_MAX) + 1u);
    c.bytes = bytes;
    c.k = k;
    c.freights = pollen_freights(bytes, k); /* pollen.freight_count */
    c.packets = pollen_packets(bytes, k);   /* pollen.freight_packets */
    c.status = CARAVAN_DRAFTED;
    c.stamp = now;
    if (!parcel_fits(&c)) return say(p, ZXM_E_ARG, "That parcel makes no record.");
    if (pannier_room(p) <= 0) return say(p, ZXM_E_STATE, "The queue is full: try again later.");
    *slot(p, p->head) = c;
    p->head++;
    p->next_n++;
    p->dirty = true;
    if (n) *n = c.n;
    snprintf(p->notice, sizeof p->notice, "Parcel %lld queued for peer %lld: %lld freights.",
             (long long) c.n, (long long) peer, (long long) c.freights);
    return ZXM_OK;
}

static bool now_ok(int64_t now)
{
    return now >= 0 && now <= PANNIER_STAMP;
}

/* ---- PM3: a message parcel ---- */

int pannier_put_message(pannier_t *p, int64_t peer, const char *path, int64_t subject, int64_t body,
                        int64_t verified, int64_t consentgap, int64_t now, int64_t *n)
{
    int64_t size = 0;
    if (!p || !p->link || !peer_of(p, peer) || !path_ok(path) || !now_ok(now)) return ZXM_E_ARG;
    if (!rookery_letter_size_ok(subject, body)) /* rookery.letter_size_ok */
        return say(p, ZXM_E_ARG, "A subject or body past Rookery's limits.");
    int64_t payload = ROOKERY_HEAD + subject + ROOKERY_GAP + body;
    if (!rookery_letter_send_ok(verified, ROOKERY_PAYLOAD_MAX - payload, consentgap))
        return say(p, ZXM_E_STATE, "This letter may not be sent: a card or a consent is missing.");
    if (p->next_n > PANNIER_PARCEL_NO ||
        !rookery_letter_record_ok(PANNIER_FOLDER, p->next_n, p->next_n, payload, now))
        return say(p, ZXM_E_STATE, "No room in the letter index.");
    if (pannier_room(p) <= 0) return say(p, ZXM_E_STATE, "The queue is full: try again later.");
    int rc = stat_size(p, path, &size);
    if (rc) return rc;
    if (size != payload) return say(p, ZXM_E_ARG, "The letter's file is not its payload.");
    int64_t k = (payload + POLLEN_ROW_BYTES - 1) / POLLEN_ROW_BYTES; /* the fewest rows */
    return enqueue(p, peer, PANNIER_MESSAGE, path, payload, k < POLLEN_K_MAX ? k : POLLEN_K_MAX,
                   now, n);
}

/* ---- PM4: a data parcel ---- */

int pannier_put_file(pannier_t *p, int64_t peer, const char *path, int64_t k, int64_t now,
                     int64_t *n)
{
    int64_t size = 0;
    if (!p || !p->link || !peer_of(p, peer) || !path_ok(path) || k < 1 || k > POLLEN_K_MAX ||
        !now_ok(now))
        return ZXM_E_ARG;
    if (pannier_room(p) <= 0) return say(p, ZXM_E_STATE, "The queue is full: try again later.");
    int rc = stat_size(p, path, &size);
    if (rc) return rc;
    if (size < 1 || size > PANNIER_PARCEL_MAX)
        return say(p, ZXM_E_ARG, "A parcel is 1 byte to 16 MiB.");
    return enqueue(p, peer, PANNIER_DATA, path, size, k, now, n);
}

/* ---- PM6: carrying ---- */

lpres_state_t pannier_state(const pannier_t *p, int64_t n)
{
    int64_t i = index_of(p, n);
    if (i < 0) return LPRES_STATE_NEITHER;
    const pannier_parcel_t *c = &p->q[(uint64_t) i % PANNIER_QUEUE];
    return pannier_delivery(c->receipts, c->lost, c->witness);
}

int64_t pannier_next(const pannier_t *p, int64_t now)
{
    if (!p) return 0;
    for (int64_t i = p->tail; i < p->head; i++) {
        const pannier_parcel_t *c = &p->q[(uint64_t) i % PANNIER_QUEUE];
        lpres_state_t s = pannier_delivery(c->receipts, c->lost, c->witness);
        const pannier_peer_t *q = &p->peer[c->peer - 1];
        if (closed(c) || s == LPRES_STATE_BOTH || s == LPRES_STATE_TRUE) continue;
        if (c->status == CARAVAN_TRANSIT && s == LPRES_STATE_NEITHER) continue; /* on its way */
        if (now >= c->next_at &&
            pannier_send_now(q->state, PANNIER_PARCEL_MAX - c->bytes, c->attempt + 1))
            return c->n;
    }
    return 0;
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int32_t b64_val(char c)
{
    const char *f = c ? strchr(B64, c) : NULL;
    return f ? (int32_t) (f - B64) : -1;
}

/* RFC 4648 with padding into out (cap bytes); its length, or -1 */
static int32_t b64_decode(const char *s, uint8_t *out, uint32_t cap)
{
    size_t n = strlen(s);
    uint32_t o = 0;
    if (n % 4u) return -1;
    for (size_t i = 0; i < n; i += 4u) {
        int32_t v[4];
        uint32_t pad = 0;
        for (uint32_t j = 0; j < 4u; j++) {
            if (s[i + j] == '=' && i + 4u == n && j >= 2u) {
                v[j] = 0, pad++;
            } else {
                if (pad || (v[j] = b64_val(s[i + j])) < 0) return -1;
            }
        }
        uint32_t w =
            (uint32_t) v[0] << 18 | (uint32_t) v[1] << 12 | (uint32_t) v[2] << 6 | (uint32_t) v[3];
        for (uint32_t j = 0; j < 3u - pad; j++) {
            if (o >= cap) return -1;
            out[o++] = (uint8_t) (w >> (16u - 8u * j));
        }
    }
    return (int32_t) o;
}

static void b64_encode(const uint8_t *in, uint32_t n, char *out)
{
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 3u) {
        uint32_t w = (uint32_t) in[i] << 16;
        if (i + 1u < n) w |= (uint32_t) in[i + 1u] << 8;
        if (i + 2u < n) w |= in[i + 2u];
        out[o++] = B64[w >> 18 & 63u];
        out[o++] = B64[w >> 12 & 63u];
        out[o++] = i + 1u < n ? B64[w >> 6 & 63u] : '=';
        out[o++] = i + 2u < n ? B64[w & 63u] : '=';
    }
    out[o] = 0;
}

static void hex_encode(const uint8_t *in, uint32_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n; i++) {
        out[2u * i] = H[in[i] >> 4];
        out[2u * i + 1u] = H[in[i] & 15u];
    }
    out[2u * n] = 0;
}

static int32_t hex_nib(char c)
{
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

/* hex text into out; its length, or -1 */
static int32_t hex_decode(const char *s, uint8_t *out, uint32_t cap)
{
    size_t n = strlen(s);
    if (n % 2u || n / 2u > cap) return -1;
    for (size_t i = 0; i < n; i += 2u) {
        int32_t a = hex_nib(s[i]), b = hex_nib(s[i + 1u]);
        if (a < 0 || b < 0) return -1;
        out[i / 2u] = (uint8_t) (a << 4 | b);
    }
    return (int32_t) (n / 2u);
}

/* one freight: read, pack, rebuild from its last k packets, write */
static int carry_one(pannier_t *p, const pannier_parcel_t *c, uint64_t off, uint32_t want,
                     const char *to, const char *mode, uint64_t *next)
{
    zxm_link_t *l = p->link;
    uint64_t size = 0;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", c->path);
    zxm_p_u64(l, "offset", off);
    zxm_p_u64(l, "max", want);
    zxm_p_str(l, "encoding", "base64");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", next) ||
        zxm_get_str(l, l->result, "base64", p->b64, sizeof p->b64))
        return ZXM_E_PROTOCOL;
    if ((int64_t) size != c->bytes) return ZXM_E_STATE; /* changed since it was queued */
    int32_t got = b64_decode(p->b64, p->bytes, sizeof p->bytes);
    if (got < 0 || *next != off + (uint64_t) got || (uint32_t) got != want) return ZXM_E_PROTOCOL;
    hex_encode(p->bytes, (uint32_t) got, p->hex);
    zxm_begin(l, "freight.pack");
    zxm_p_str(l, "data_hex", p->hex);
    zxm_p_i64(l, "k", c->k);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "header_hex", p->header_hex, sizeof p->header_hex) ||
        zxm_get_str(l, l->result, "packets_hex", p->packets_hex, sizeof p->packets_hex) ||
        strlen(p->packets_hex) != 2u * 21u * PANNIER_ROWS)
        return ZXM_E_PROTOCOL;
    /* every packet but the last k lost on the way: rows 168 - k .. 167 rebuild it */
    const char *last = p->packets_hex + 2u * 21u * (PANNIER_ROWS - (uint32_t) c->k);
    zxm_begin(l, "freight.unpack");
    zxm_p_str(l, "header_hex", p->header_hex);
    zxm_p_str(l, "packets_hex", last);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "data_hex", p->hex, sizeof p->hex)) return ZXM_E_PROTOCOL;
    int32_t back = hex_decode(p->hex, p->bytes, sizeof p->bytes);
    if (back != got) return ZXM_E_PROTOCOL;
    b64_encode(p->bytes, (uint32_t) back, p->b64);
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", to);
    zxm_p_str(l, "mode", mode);
    zxm_p_str(l, "base64", p->b64);
    return zxm_call(l);
}

int pannier_send(pannier_t *p, int64_t n, int64_t avail, int64_t spare, int64_t now,
                 bool owner_confirmed)
{
    char to[PANNIER_PATH_MAX + 1];
    if (!p || !p->link || !now_ok(now)) return ZXM_E_ARG;
    pannier_parcel_t *c = pannier_find(p, n);
    if (!c || closed(c)) return say(p, ZXM_E_STATE, "No such parcel waiting.");
    lpres_state_t s = pannier_delivery(c->receipts, c->lost, c->witness);
    if (s == LPRES_STATE_BOTH)
        return say(p, ZXM_E_STATE, "Held: a receipt and a loss disagree; wait for a witness.");
    if (s == LPRES_STATE_TRUE) return say(p, ZXM_E_STATE, "Already delivered: close it.");
    if (c->status == CARAVAN_TRANSIT && s == LPRES_STATE_NEITHER)
        return say(p, ZXM_E_STATE, "On its way: wait for a receipt or a hop report.");
    int64_t attempt = c->attempt + 1, wait = pannier_wait(attempt);
    if (wait == PANNIER_REFUSED || !pannier_retry_ok(attempt, wait))
        return say(p, ZXM_E_STATE, "The retry limit is reached: give the parcel up.");
    if (now < c->next_at) return say(p, ZXM_E_STATE, "Backing off: not yet.");
    const pannier_peer_t *q = &p->peer[c->peer - 1];
    if (!pannier_send_now(q->state, PANNIER_PARCEL_MAX - c->bytes, attempt))
        return say(p, ZXM_E_STATE, "Its peer is not active.");
    if (!ferry_fits(avail, c->bytes, spare)) /* ferry.target_fits */
        return say(p, ZXM_E_STATE, "It does not fit on the receiver.");
    if (p->book_known && !p->book_ok)
        return say(p, ZXM_E_STATE, "The peer book does not verify: nothing is carried.");
    int w =
        snprintf(to, sizeof to, "%s.%lld.%lld", PANNIER_OUT, (long long) c->peer, (long long) c->n);
    if (w < 0 || (uint32_t) w >= sizeof to) return ZXM_E_ARG;
    if (!owner_confirmed) return say(p, ZXM_E_STATE, "A parcel is carried only on your word.");
    uint64_t off = 0, per = (uint64_t) (POLLEN_ROW_BYTES * c->k);
    for (int64_t f = 0; f < c->freights; f++) {
        uint64_t rest = (uint64_t) c->bytes - off, next = 0;
        uint32_t want = (uint32_t) (rest < per ? rest : per);
        const char *mode = c->freights == 1       ? "replace"
                           : f == 0               ? "begin"
                           : f + 1 == c->freights ? "end"
                                                  : "add";
        int rc = carry_one(p, c, off, want, to, mode, &next);
        if (rc) return rc;
        off = next;
        zxm_progress(p->link, (uint64_t) (f + 1), (uint64_t) c->freights);
    }
    int64_t after = pannier_wait(attempt + 1); /* PR2: before the next attempt */
    c->attempt = attempt;
    c->next_at = now + (after == PANNIER_REFUSED ? PANNIER_WAIT_MAX : after);
    c->status = CARAVAN_TRANSIT;
    c->stamp = now;
    c->receipts = 0;
    c->lost = 0;
    c->witness = PANNIER_SAID_NONE;
    p->dirty = true;
    snprintf(p->notice, sizeof p->notice, "Parcel %lld on its way (attempt %lld of %d).",
             (long long) c->n, (long long) attempt, PANNIER_TRIES);
    return ZXM_OK;
}

/* ---- PM7: receipts and hop reports ---- */

int pannier_receipt(pannier_t *p, int64_t n, int64_t seq, int64_t now)
{
    if (!p || !now_ok(now)) return ZXM_E_ARG;
    pannier_parcel_t *c = pannier_find(p, n);
    if (!c || closed(c) || c->status != CARAVAN_TRANSIT)
        return say(p, ZXM_E_STATE, "No parcel on its way by that number.");
    pannier_peer_t *q = &p->peer[c->peer - 1];
    if (cove_tunnel_seq_gap(q->last_seq, seq) == COVE_REFUSED) /* cove.tunnel_seq_gap */
        return say(p, ZXM_E_STATE, "A receipt's sequence reused or past its range: refused.");
    q->last_seq = seq;
    if (c->receipts < 2) c->receipts++;
    c->stamp = now; /* it stays in transit until it is closed (PM8) */
    p->dirty = true;
    return ZXM_OK;
}

int pannier_hop_lost(pannier_t *p, int64_t n, int64_t now)
{
    if (!p || !now_ok(now)) return ZXM_E_ARG;
    pannier_parcel_t *c = pannier_find(p, n);
    if (!c || closed(c) || c->status != CARAVAN_TRANSIT)
        return say(p, ZXM_E_STATE, "No parcel on its way by that number.");
    c->lost = 1;
    c->stamp = now; /* it stays in transit until it is closed (PM8) */
    p->dirty = true;
    return ZXM_OK;
}

int pannier_witness(pannier_t *p, int64_t n, int64_t said, int64_t now, bool owner_confirmed)
{
    if (!p || !now_ok(now) || (said != PANNIER_SAID_DELIVERED && said != PANNIER_SAID_LOST))
        return ZXM_E_ARG;
    pannier_parcel_t *c = pannier_find(p, n);
    if (!c || closed(c) || c->status != CARAVAN_TRANSIT)
        return say(p, ZXM_E_STATE, "No parcel on its way by that number.");
    if (!owner_confirmed) return say(p, ZXM_E_STATE, "A witness is taken only on your word.");
    c->witness = said;
    c->stamp = now; /* it stays in transit until it is closed (PM8) */
    p->dirty = true;
    return ZXM_OK;
}

/* ---- PM8: closing ---- */

int pannier_close(pannier_t *p, int64_t n, int64_t now, bool owner_confirmed)
{
    if (!p || !now_ok(now)) return ZXM_E_ARG;
    pannier_parcel_t *c = pannier_find(p, n);
    if (!c || closed(c)) return say(p, ZXM_E_STATE, "No such parcel waiting.");
    lpres_state_t s = pannier_delivery(c->receipts, c->lost, c->witness);
    if (s == LPRES_STATE_BOTH)
        return say(p, ZXM_E_STATE, "Held: a receipt and a loss disagree; wait for a witness.");
    if (s != LPRES_STATE_TRUE) return say(p, ZXM_E_STATE, "Not delivered: it stays open.");
    if (!owner_confirmed) return say(p, ZXM_E_STATE, "A parcel is closed only on your word.");
    c->status = CARAVAN_DELIVERED;
    c->stamp = now;
    advance(p);
    p->dirty = true;
    return ZXM_OK;
}

int pannier_give_up(pannier_t *p, int64_t n, int64_t now, bool owner_confirmed)
{
    if (!p || !now_ok(now)) return ZXM_E_ARG;
    pannier_parcel_t *c = pannier_find(p, n);
    if (!c || closed(c)) return say(p, ZXM_E_STATE, "No such parcel waiting.");
    lpres_state_t s = pannier_delivery(c->receipts, c->lost, c->witness);
    if (s == LPRES_STATE_BOTH)
        return say(p, ZXM_E_STATE, "Held: a receipt and a loss disagree; wait for a witness.");
    if (s == LPRES_STATE_TRUE) return say(p, ZXM_E_STATE, "Delivered: close it instead.");
    if (!owner_confirmed) return say(p, ZXM_E_STATE, "A parcel is given up only on your word.");
    c->status = CARAVAN_RETURNED;
    c->stamp = now;
    advance(p);
    p->dirty = true;
    return ZXM_OK;
}

/* ---- PM9: the file ---- */

static bool put_nums(char *out, uint32_t cap, uint32_t *at, const char *tag, const int64_t *x,
                     uint32_t n)
{
    if (!put(out, cap, at, tag)) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!put(out, cap, at, " ") || !put_i64(out, cap, at, x[i])) return false;
    return true;
}

int pannier_save(pannier_t *p, bool owner_confirmed)
{
    static char text[PANNIER_TEXT_MAX];
    uint32_t at = 0;
    if (!p || !p->link) return ZXM_E_ARG;
    text[0] = 0;
    int64_t qh[3] = {p->head, p->tail, p->next_n};
    bool ok = put(text, sizeof text, &at, "pannier 1\n") &&
              put_nums(text, sizeof text, &at, "q", qh, 3) && put(text, sizeof text, &at, "\n");
    for (uint32_t i = 0; ok && i < p->npeers; i++) {
        const pannier_peer_t *q = &p->peer[i];
        int64_t x[3] = {q->state, q->seen_ms, q->last_seq};
        ok = put_nums(text, sizeof text, &at, "p", x, 3) && put(text, sizeof text, &at, " ") &&
             put(text, sizeof text, &at, q->id) && put(text, sizeof text, &at, "\n");
    }
    for (int64_t i = p->tail; ok && i < p->head; i++) {
        const pannier_parcel_t *c = slot(p, i);
        int64_t x[PANNIER_FIELDS] = {c->n,        c->peer,     c->kind,    c->bytes,   c->k,
                                     c->freights, c->packets,  c->attempt, c->next_at, c->status,
                                     c->stamp,    c->receipts, c->lost,    c->witness};
        ok = put_nums(text, sizeof text, &at, "c", x, PANNIER_FIELDS) &&
             put(text, sizeof text, &at, " ") && put(text, sizeof text, &at, c->path) &&
             put(text, sizeof text, &at, "\n");
    }
    if (!ok) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return say(p, ZXM_E_STATE, "The queue is saved only on your word.");
    zxm_begin(p->link, "files.write");
    zxm_p_str(p->link, "path", PANNIER_FILE);
    zxm_p_strn(p->link, "text", text, at);
    int rc = zxm_call(p->link);
    if (rc) return rc;
    p->dirty = false;
    return ZXM_OK;
}

static int parse(pannier_t *into, const char *text, uint32_t n)
{
    uint32_t i = 0, line = 0;
    bool queue = false;
    int64_t head = 0;
    while (i < n) {
        uint32_t e = i;
        while (e < n && text[e] != '\n') e++;
        const char *l = text + i;
        uint32_t ln = e - i, k = 1;
        i = e + 1u;
        if (ln > PANNIER_LINE_MAX) return ZXM_E_STATE;
        if (line++ == 0) {
            if (ln != 9u || memcmp(l, "pannier 1", 9)) return ZXM_E_STATE;
            continue;
        }
        if (!ln) continue;
        if (ln < 2u || l[1] != ' ') return ZXM_E_STATE;
        int64_t x[PANNIER_FIELDS];
        if (l[0] == 'q') {
            for (uint32_t f = 0; f < 3u; f++)
                if (!next_num(l, ln, &k, &x[f])) return ZXM_E_STATE;
            if (k != ln || queue || x[1] < 0 || x[0] < x[1] || x[0] - x[1] > PANNIER_QUEUE ||
                x[2] < 1 || x[2] > PANNIER_PARCEL_NO + 1)
                return ZXM_E_STATE;
            head = x[0];
            into->head = x[1]; /* the parcels are read in, oldest first */
            into->tail = x[1];
            into->next_n = x[2];
            queue = true;
            continue;
        }
        if (l[0] == 'p') {
            pannier_peer_t q;
            memset(&q, 0, sizeof q);
            for (uint32_t f = 0; f < 3u; f++)
                if (!next_num(l, ln, &k, &x[f])) return ZXM_E_STATE;
            if (!next_word(l, ln, &k, q.id, sizeof q.id) || k != ln ||
                into->npeers >= PANNIER_PEERS)
                return ZXM_E_STATE;
            q.state = x[0];
            q.seen_ms = x[1];
            q.last_seq = x[2];
            if (!peer_fits(&q, (int64_t) into->npeers + 1)) return ZXM_E_STATE;
            for (uint32_t j = 0; j < into->npeers; j++)
                if (!strcmp(into->peer[j].id, q.id)) return ZXM_E_STATE;
            into->peer[into->npeers++] = q;
            continue;
        }
        if (l[0] != 'c' || !queue) return ZXM_E_STATE;
        pannier_parcel_t c;
        memset(&c, 0, sizeof c);
        for (uint32_t f = 0; f < PANNIER_FIELDS; f++)
            if (!next_num(l, ln, &k, &x[f])) return ZXM_E_STATE;
        if (!next_word(l, ln, &k, c.path, sizeof c.path) || k != ln) return ZXM_E_STATE;
        c.n = x[0];
        c.peer = x[1];
        c.kind = x[2];
        c.bytes = x[3];
        c.k = x[4];
        c.freights = x[5];
        c.packets = x[6];
        c.attempt = x[7];
        c.next_at = x[8];
        c.status = x[9];
        c.stamp = x[10];
        c.receipts = x[11];
        c.lost = x[12];
        c.witness = x[13];
        if (!parcel_fits(&c) || c.peer > (int64_t) into->npeers || c.n >= into->next_n ||
            pannier_queue_room(into->head, into->tail, PANNIER_QUEUE) <= 0 ||
            pannier_find(into, c.n))
            return ZXM_E_STATE;
        *slot(into, into->head) = c;
        into->head++;
    }
    return line && queue && into->head == head ? ZXM_OK : ZXM_E_STATE;
}

int pannier_load(pannier_t *p)
{
    static char text[PANNIER_TEXT_MAX + 1u];
    static pannier_t got;
    bool eof = false;
    if (!p || !p->link) return ZXM_E_ARG;
    zxm_link_t *l = p->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", PANNIER_FILE);
    zxm_p_u64(l, "max", PANNIER_TEXT_MAX);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "eof", &eof) ||
        zxm_get_str(l, l->result, "text", text, sizeof text))
        return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    pannier_init(&got, p->link);
    got.book_known = p->book_known;
    got.book_ok = p->book_ok;
    got.serving = p->serving;
    rc = parse(&got, text, len_of(text, PANNIER_TEXT_MAX));
    if (rc) return rc; /* what is open stays as it was */
    p->npeers = got.npeers;
    memcpy(p->peer, got.peer, sizeof got.peer);
    memcpy(p->q, got.q, sizeof got.q);
    p->head = got.head;
    p->tail = got.tail;
    p->next_n = got.next_n;
    p->dirty = false;
    return ZXM_OK;
}

/* ---- advisory ---- */

int pannier_route(const int64_t *hop, uint32_t n, int64_t *num, int64_t *den)
{
    return pannier_route_hours(hop, n, num, den) ? ZXM_OK : ZXM_E_ARG;
}

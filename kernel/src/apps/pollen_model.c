/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pollen_model.c — Pollen, share with nearby devices (pollen_model.h PM1-PM7). */
#include "pollen_model.h"

#include <stdio.h>
#include <string.h>

void pollen_init(pollen_t *p, zxm_link_t *link)
{
    memset(p, 0, sizeof *p);
    p->link = link;
    p->net = -1;
    p->next_number = 1;
    p->avail = -1;
    p->spare = POLLEN_SPARE;
}

static int refuse(pollen_t *p, uint32_t why, int rc)
{
    p->fault = why;
    return rc;
}

static bool path_ok(const char *s)
{
    size_t n = s ? strlen(s) : 0u;
    return n >= 1u && n <= POLLEN_PATH_MAX;
}

/* ---------- PM1 ---------- */

int pollen_lan(pollen_t *p, bool on)
{
    zxm_link_t *l = p->link;
    char said[64];
    zxm_begin(l, "net.command");
    zxm_p_str(l, "command", on ? "lan" : "off");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "result", said, sizeof said)) return ZXM_E_PROTOCOL;
    if (strcmp(said, on ? "networking is on" : "networking is off")) {
        snprintf(p->notice, sizeof p->notice, "The local network did not start: %s.", said);
        return ZXM_E_STATE;
    }
    p->net = on ? 1 : 0;
    snprintf(p->notice, sizeof p->notice, "%s",
             on ? "Pollen is on the local network." : "Networking is off.");
    return ZXM_OK;
}

/* ---------- PM2 ---------- */

/* "a.b.c.d:port" into its octets; false for anything else */
static bool parse_addr(const char *s, int64_t oct[4])
{
    for (uint32_t i = 0; i < 4u; i++) {
        int64_t v = 0;
        uint32_t digits = 0;
        while (*s >= '0' && *s <= '9' && digits < 4u) v = v * 10 + (*s++ - '0'), digits++;
        if (!digits || digits > 3u) return false;
        oct[i] = v;
        if (*s++ != (i < 3u ? '.' : ':')) return false;
    }
    uint32_t pd = 0;
    while (*s >= '0' && *s <= '9') s++, pd++;
    return pd >= 1u && pd <= 5u && !*s;
}

int pollen_peers(pollen_t *p)
{
    static pollen_peer_t got[POLLEN_PEERS];
    zxm_link_t *l = p->link;
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "peers");
    if (arr < 0) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), k = 0, near = 0;
    for (uint32_t i = 0; i < n && k < POLLEN_PEERS; i++) {
        int32_t o = zxm_at(l, arr, i);
        pollen_peer_t *q = &got[k];
        int64_t oct[4];
        if (o < 0 || zxm_get_str(l, o, "id", q->id, sizeof q->id) ||
            zxm_get_str(l, o, "addr", q->addr, sizeof q->addr) || strlen(q->id) != POLLEN_NODE)
            return ZXM_E_PROTOCOL;
        if (!q->addr[0])
            q->scope = HEDGEROW_SCOPE_NONE;
        else if (!parse_addr(q->addr, oct) ||
                 (q->scope = hedgerow_addr_scope(4, oct[0], oct[1], oct[2], oct[3])) ==
                     HEDGEROW_REFUSED)
            return ZXM_E_PROTOCOL;
        q->nearby = pollen_nearby_scope(q->scope);
        near += q->nearby ? 1u : 0u;
        k++;
    }
    memcpy(p->peer, got, sizeof got[0] * k);
    p->npeers = k;
    p->nearby = near;
    p->left_out = n - k;
    snprintf(p->notice, sizeof p->notice, "%u nearby device%s of %u peer%s.", (unsigned) near,
             near == 1u ? "" : "s", (unsigned) n, n == 1u ? "" : "s");
    return ZXM_OK;
}

/* ---------- PM3 ---------- */

/* a 64-bit count that travels as a decimal string ("^[0-9]+$") */
static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, uint64_t *v)
{
    char s[24];
    uint64_t x = 0;
    if (zxm_get_str(l, obj, key, s, sizeof s) || !s[0]) return ZXM_E_PROTOCOL;
    for (const char *c = s; *c; c++) {
        uint64_t d = (uint64_t) (*c - '0');
        if (*c < '0' || *c > '9' || x > (UINT64_MAX - d) / 10u) return ZXM_E_PROTOCOL;
        x = x * 10u + d;
    }
    *v = x;
    return ZXM_OK;
}

int pollen_read_offers(pollen_t *p)
{
    zxm_link_t *l = p->link;
    uint64_t c = 0, s = 0;
    zxm_begin(l, "net.offers");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t sh = zxm_get(l, l->result, "share"), w = zxm_get(l, l->result, "wants");
    if (sh < 0 || w < 0 || get_dec(l, sh, "compute", &c) || get_dec(l, sh, "storage", &s))
        return ZXM_E_PROTOCOL;
    p->share_compute = c;
    p->share_storage = s;
    p->wants = zxm_len(l, w);
    return ZXM_OK;
}

int64_t pollen_count(const pollen_t *p, int64_t state)
{
    int64_t st[POLLEN_OFFERS];
    for (uint32_t i = 0; i < p->noffers; i++) st[i] = p->offer[i].state;
    return witness_kind_count(st, p->noffers, state); /* witness.kind_count, WR2 */
}

static int stat_file(pollen_t *p, const char *path, int64_t *size)
{
    zxm_link_t *l = p->link;
    uint64_t v = 0;
    bool dir = false;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "dir", &dir)) return ZXM_E_PROTOCOL;
    if (dir) return refuse(p, POLLEN_F_FILE, ZXM_E_ARG);
    if (zxm_get_u64(l, l->result, "size", &v) || v > (uint64_t) INT64_MAX) return ZXM_E_PROTOCOL;
    *size = (int64_t) v;
    return ZXM_OK;
}

int pollen_offer(pollen_t *p, const char *const *paths, uint32_t n, int64_t now_ms, uint32_t *index)
{
    static pollen_offer_t o;
    if (!index || !paths || n < 1u || n > POLLEN_FILES_MAX || now_ms < 0) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (!path_ok(paths[i])) return ZXM_E_ARG;
    if (p->noffers >= POLLEN_OFFERS || p->next_number > POLLEN_OFFER_MAX) return ZXM_E_STATE;
    memset(&o, 0, sizeof o);
    for (uint32_t i = 0; i < n; i++) {
        int rc = stat_file(p, paths[i], &o.file[i].size);
        if (rc) return rc;
        snprintf(o.file[i].path, sizeof o.file[i].path, "%s", paths[i]);
        if (o.file[i].size > o.largest) o.largest = o.file[i].size;
        o.bytes += o.file[i].size; /* each at most 2^28 (ZXF_FILE_MAX), 16 of them */
    }
    int64_t smallest = o.largest;
    for (uint32_t i = 0; i < n; i++)
        if (o.file[i].size < smallest) smallest = o.file[i].size;
    /* PR3: the largest and the smallest file each inside the range */
    if (!pollen_share_limits_ok(o.largest, n, pollen_count(p, POLLEN_OPEN)) ||
        !pollen_share_limits_ok(smallest, n, 0))
        return refuse(p, POLLEN_F_LIMITS, ZXM_E_ARG);
    if (!pollen_record_fits(p->next_number, n, o.bytes, POLLEN_DRAFTED, now_ms))
        return refuse(p, POLLEN_F_RECORD, ZXM_E_ARG);
    o.nfiles = n;
    o.number = p->next_number++;
    o.state = POLLEN_DRAFTED;
    o.stamp = now_ms;
    *index = p->noffers;
    p->offer[p->noffers++] = o;
    p->fault = POLLEN_FINE;
    snprintf(p->notice, sizeof p->notice,
             "Offer %lld: %u file%s, %lld bytes, waiting for your approval.", (long long) o.number,
             (unsigned) n, n == 1u ? "" : "s", (long long) o.bytes);
    return ZXM_OK;
}

/* the open offers' bytes, with offer `with` counted open and `without` not */
static int64_t open_bytes(const pollen_t *p, int64_t with, int64_t without)
{
    int64_t s = 0;
    for (uint32_t i = 0; i < p->noffers; i++)
        if ((int64_t) i != without && (p->offer[i].state == POLLEN_OPEN || (int64_t) i == with))
            s += p->offer[i].bytes;
    return s;
}

/* tell zxvd the storage bytes Pollen shares, the compute share kept */
static int share(pollen_t *p, int64_t storage)
{
    zxm_link_t *l = p->link;
    char cmd[64], said[64];
    int rc = pollen_read_offers(p);
    if (rc) return rc;
    snprintf(cmd, sizeof cmd, "share %llu %lld", (unsigned long long) p->share_compute,
             (long long) storage);
    zxm_begin(l, "net.command");
    zxm_p_str(l, "command", cmd);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "result", said, sizeof said)) return ZXM_E_PROTOCOL;
    if (strncmp(said, "sharing", 7)) return ZXM_E_STATE; /* "refused" */
    p->share_storage = (uint64_t) storage;
    return ZXM_OK;
}

int pollen_approve(pollen_t *p, uint32_t index, bool yes, int64_t now_ms)
{
    if (index >= p->noffers || now_ms < 0) return ZXM_E_ARG;
    pollen_offer_t *o = &p->offer[index];
    if (o->state != POLLEN_DRAFTED) return ZXM_E_STATE;
    if (!yes) {
        snprintf(p->notice, sizeof p->notice, "Offer %lld stays a draft.", (long long) o->number);
        return refuse(p, POLLEN_F_WORD, ZXM_E_STATE);
    }
    if (!pollen_share_limits_ok(o->largest, o->nfiles, pollen_count(p, POLLEN_OPEN)))
        return refuse(p, POLLEN_F_LIMITS, ZXM_E_ARG);
    if (!pollen_record_fits(o->number, o->nfiles, o->bytes, POLLEN_OPEN, now_ms))
        return refuse(p, POLLEN_F_RECORD, ZXM_E_ARG);
    int rc = share(p, open_bytes(p, index, -1));
    if (rc) return rc;
    o->state = POLLEN_OPEN;
    o->stamp = now_ms;
    p->fault = POLLEN_FINE;
    snprintf(p->notice, sizeof p->notice, "Offer %lld is open to nearby devices: %lld bytes.",
             (long long) o->number, (long long) o->bytes);
    return ZXM_OK;
}

int pollen_withdraw(pollen_t *p, uint32_t index, int64_t now_ms)
{
    if (index >= p->noffers || now_ms < 0 || now_ms > POLLEN_STAMP_MAX) return ZXM_E_ARG;
    pollen_offer_t *o = &p->offer[index];
    if (o->state != POLLEN_OPEN) return ZXM_E_STATE;
    int rc = share(p, open_bytes(p, -1, index));
    if (rc) return rc;
    o->state = POLLEN_WITHDRAWN;
    o->stamp = now_ms;
    snprintf(p->notice, sizeof p->notice, "Offer %lld withdrawn.", (long long) o->number);
    return ZXM_OK;
}

/* ---------- PM4 ---------- */

int pollen_measure(pollen_t *p)
{
    zxm_link_t *l = p->link;
    uint64_t avail;
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "avail", &avail)) return ZXM_E_PROTOCOL;
    /* more room than 9(15) bytes is more than any file Pollen carries */
    p->avail = avail > (uint64_t) FERRY_BYTES_MAX ? FERRY_BYTES_MAX : (int64_t) avail;
    return ZXM_OK;
}

int pollen_set_avail(pollen_t *p, int64_t avail)
{
    if (!ferry_bytes_ok(avail)) return ZXM_E_ARG;
    p->avail = avail;
    return ZXM_OK;
}

int pollen_set_spare(pollen_t *p, int64_t spare)
{
    if (!ferry_bytes_ok(spare)) return ZXM_E_ARG;
    p->spare = spare;
    return ZXM_OK;
}

bool pollen_fits(const pollen_t *p, int64_t need)
{
    return p->avail >= 0 && ferry_fits(p->avail, need, p->spare); /* ferry.target_fits, FR2 */
}

int pollen_want(pollen_t *p, int64_t bytes, bool approved)
{
    zxm_link_t *l = p->link;
    char cmd[64], said[64];
    if (bytes < 1 || bytes > POLLEN_FILE_MAX * POLLEN_FILES_MAX) return ZXM_E_ARG;
    if (!approved) return refuse(p, POLLEN_F_WORD, ZXM_E_STATE);
    if (!p->nearby) return refuse(p, POLLEN_F_NOBODY, ZXM_E_STATE);
    if (!pollen_fits(p, bytes)) {
        snprintf(p->notice, sizeof p->notice, "%lld bytes do not fit here.", (long long) bytes);
        return refuse(p, POLLEN_F_NO_ROOM, ZXM_E_STATE);
    }
    snprintf(cmd, sizeof cmd, "want storage %lld 0", (long long) bytes);
    zxm_begin(l, "net.command");
    zxm_p_str(l, "command", cmd);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "result", said, sizeof said)) return ZXM_E_PROTOCOL;
    if (strcmp(said, "wanted")) return ZXM_E_STATE;
    p->wanted = bytes;
    p->fault = POLLEN_FINE;
    snprintf(p->notice, sizeof p->notice, "Asked nearby devices for %lld bytes.",
             (long long) bytes);
    return ZXM_OK;
}

/* ---------- PM5 ---------- */

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
static int carry_one(pollen_t *p, const pollen_file_t *f, uint64_t off, uint32_t want, int64_t k,
                     const char *to, const char *mode, uint64_t *next)
{
    zxm_link_t *l = p->link;
    uint64_t size = 0;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", f->path);
    zxm_p_u64(l, "offset", off);
    zxm_p_u64(l, "max", want);
    zxm_p_str(l, "encoding", "base64");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", next) ||
        zxm_get_str(l, l->result, "base64", p->b64, sizeof p->b64))
        return ZXM_E_PROTOCOL;
    if ((int64_t) size != f->size) return ZXM_E_STATE; /* changed since it was offered */
    int32_t got = b64_decode(p->b64, p->bytes, sizeof p->bytes);
    if (got < 0 || *next != off + (uint64_t) got || (uint32_t) got != want) return ZXM_E_PROTOCOL;
    hex_encode(p->bytes, (uint32_t) got, p->hex);
    zxm_begin(l, "freight.pack");
    zxm_p_str(l, "data_hex", p->hex);
    zxm_p_i64(l, "k", k);
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "header_hex", p->header_hex, sizeof p->header_hex) ||
        zxm_get_str(l, l->result, "packets_hex", p->packets_hex, sizeof p->packets_hex) ||
        strlen(p->packets_hex) != 2u * 21u * POLLEN_ROWS)
        return ZXM_E_PROTOCOL;
    /* the last k packets only: rows 168 - k .. 167 */
    const char *last = p->packets_hex + 2u * 21u * (uint32_t) (POLLEN_ROWS - k);
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
    if (strcmp(mode, "add")) zxm_p_bool(l, "exclusive", true);
    return zxm_call(l);
}

int pollen_carry(pollen_t *p, uint32_t i, uint32_t j, const char *to, int64_t k)
{
    zxm_link_t *l = p->link;
    if (i >= p->noffers || !path_ok(to) || k < 1 || k > POLLEN_K_MAX) return ZXM_E_ARG;
    pollen_offer_t *o = &p->offer[i];
    if (j >= o->nfiles) return ZXM_E_ARG;
    if (o->state != POLLEN_OPEN) return refuse(p, POLLEN_F_WORD, ZXM_E_STATE);
    const pollen_file_t *f = &o->file[j];
    if (!pollen_fits(p, f->size)) return refuse(p, POLLEN_F_NO_ROOM, ZXM_E_STATE);
    int64_t freights = pollen_freights(f->size, k), packets = pollen_packets(f->size, k);
    if (freights < 1 || packets < 1) return refuse(p, POLLEN_F_LIMITS, ZXM_E_ARG);
    p->carried = false;
    p->freights = freights;
    p->packets = packets;
    p->left = packets;
    p->progress_x100 = ferry_progress_x100(packets, packets); /* used_share.used_x100 */
    uint64_t off = 0, per = (uint64_t) (POLLEN_ROW_BYTES * k);
    for (int64_t n = 0; n < freights; n++) {
        uint64_t rest = (uint64_t) f->size - off, next = 0;
        uint32_t want = (uint32_t) (rest < per ? rest : per);
        const char *mode = freights == 1       ? "replace"
                           : n == 0            ? "begin"
                           : n + 1 == freights ? "end"
                                               : "add";
        int rc = carry_one(p, f, off, want, k, to, mode, &next);
        if (rc) return rc;
        off = next;
        p->left -= k;
        p->progress_x100 = ferry_progress_x100(packets, p->left);
        zxm_progress(l, (uint64_t) (packets - p->left), (uint64_t) packets);
    }
    /* the file written is the file offered, by its length */
    uint64_t have = 0;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", to);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "size", &have)) return ZXM_E_PROTOCOL;
    if (have != (uint64_t) f->size) return ZXM_E_PROTOCOL;
    p->carried = true;
    p->fault = POLLEN_FINE;
    snprintf(p->notice, sizeof p->notice, "Carried %lld bytes in %lld freights (%lld packets).",
             (long long) f->size, (long long) freights, (long long) packets);
    return ZXM_OK;
}

/* ---------- PM6 ---------- */

int pollen_cross(pollen_t *p, const char *from_space, const char *path, const char *to_space,
                 const char *to, uint32_t *ticket)
{
    zxm_link_t *l = p->link;
    bool held = false;
    uint64_t t = 0;
    if (!ticket) return ZXM_E_ARG;
    *ticket = 0;
    if (!from_space || !to_space || strlen(from_space) > ZXM_SPACE_MAX ||
        strlen(to_space) > ZXM_SPACE_MAX || !path_ok(path) || !path_ok(to))
        return ZXM_E_ARG;
    zxm_begin(l, "files.cross");
    zxm_p_str(l, "from_space", from_space[0] ? from_space : "home");
    zxm_p_str(l, "path", path);
    zxm_p_str(l, "to_space", to_space[0] ? to_space : "home");
    zxm_p_str(l, "to", to);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "held", &held)) return ZXM_E_PROTOCOL;
    if (held) {
        if (zxm_get_u64(l, l->result, "ticket", &t) || !t || t > UINT32_MAX) return ZXM_E_PROTOCOL;
        *ticket = (uint32_t) t;
        snprintf(p->notice, sizeof p->notice, "Crossing held: confirm ticket %u to copy it.",
                 (unsigned) t);
        return ZXM_OK; /* the owner confirms it, or it never happens */
    }
    snprintf(p->notice, sizeof p->notice, "Copied into %s.", to_space[0] ? to_space : "home");
    return ZXM_OK;
}

int pollen_cross_confirm(pollen_t *p, uint32_t ticket)
{
    zxm_link_t *l = p->link;
    bool held = true;
    if (!ticket) return ZXM_E_ARG;
    zxm_begin(l, "files.cross");
    zxm_p_u64(l, "confirm", ticket);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "held", &held) || held) return ZXM_E_PROTOCOL;
    snprintf(p->notice, sizeof p->notice, "Crossing %u done.", (unsigned) ticket);
    return ZXM_OK;
}

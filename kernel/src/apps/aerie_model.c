/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aerie_model.c — Aerie, radio and ham links (see aerie_model.h). Nothing
 * here transmits: zxvd has no radio method, and the owner keeps to the
 * bands their licence allows. */
#include <string.h>

#include "aerie_model.h"

/* ---- hex ---------------------------------------------------------------------- */

static const char HEX[] = "0123456789abcdef";

static int32_t nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* true when s is exactly `len` hex digits (len 0: any even count) */
static bool hex_ok(const char *s, uint32_t len)
{
    if (!s) return false;
    uint32_t n = (uint32_t) strlen(s);
    if ((len && n != len) || n % 2u) return false;
    for (uint32_t i = 0; i < n; i++)
        if (nibble(s[i]) < 0) return false;
    return true;
}

static void to_hex(const uint8_t *d, uint32_t n, char *out)
{
    for (uint32_t i = 0; i < n; i++) {
        out[2u * i] = HEX[d[i] >> 4];
        out[2u * i + 1u] = HEX[d[i] & 15u];
    }
    out[2u * n] = 0;
}

/* the bytes of hex digits s into out (at most cap); -1 when they do not fit */
static int32_t from_hex(const char *s, uint8_t *out, uint32_t cap)
{
    uint32_t n = (uint32_t) strlen(s);
    if (n % 2u || n / 2u > cap) return -1;
    for (uint32_t i = 0; i < n / 2u; i++) {
        int32_t hi = nibble(s[2u * i]), lo = nibble(s[2u * i + 1u]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t) (hi * 16 + lo);
    }
    return (int32_t) (n / 2u);
}

static bool digits_u64(const char *s, uint64_t *v)
{
    uint64_t x = 0;
    if (!s[0]) return false;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9' || x > (UINT64_MAX - (uint64_t) (*p - '0')) / 10u) return false;
        x = x * 10u + (uint64_t) (*p - '0');
    }
    *v = x;
    return true;
}

/* ---- setup ----------------------------------------------------------------------- */

void aerie_init(aerie_t *a, zxm_link_t *link)
{
    memset(a, 0, sizeof *a);
    a->link = link;
    a->allowed = AERIE_BANDS_NONE;
    a->threshold = AERIE_THR_DEFAULT;
}

/* ---- the band plan: ehop.bands (AM1) ---------------------------------------------- */

static int read_band(const zxm_link_t *l, int32_t obj, uint32_t i, aerie_band_t *b)
{
    int64_t idx;
    memset(b, 0, sizeof *b);
    if (zxm_get_i64(l, obj, "band", &idx) || idx != (int64_t) i ||
        zxm_get_str(l, obj, "name", b->name, sizeof b->name) || !b->name[0] ||
        zxm_get_i64(l, obj, "fm_min", &b->fm_min) || zxm_get_i64(l, obj, "fm_max", &b->fm_max) ||
        zxm_get_i64(l, obj, "am_mask", &b->am_mask) ||
        zxm_get_i64(l, obj, "pm_modes", &b->pm_modes) ||
        zxm_get_i64(l, obj, "priority", &b->priority) ||
        zxm_get_i64(l, obj, "max_frame", &b->max_frame) ||
        zxm_get_i64(l, obj, "latency_ms", &b->latency_ms))
        return ZXM_E_PROTOCOL;
    if (b->max_frame < AERIE_FRAME_MIN || b->max_frame > AERIE_FRAME_MAX) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int aerie_bands(aerie_t *a)
{
    static aerie_band_t next[AERIE_BANDS];
    zxm_link_t *l = a->link;
    zxm_begin(l, "ehop.bands");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "bands");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (!n || n > AERIE_BANDS) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        rc = read_band(l, zxm_at(l, arr, i), i, &next[i]);
        if (rc) return rc;
    }
    memcpy(a->band, next, (size_t) n * sizeof next[0]);
    a->nbands = n;
    a->allowed &= (1u << n) - 1u;
    return ZXM_OK;
}

int32_t aerie_band_index(const aerie_t *a, const char *name)
{
    for (uint32_t i = 0; name && i < a->nbands; i++)
        if (!strcmp(a->band[i].name, name)) return (int32_t) i;
    return -1;
}

/* ---- the owner's settings ------------------------------------------------------------ */

int aerie_set_bands(aerie_t *a, uint32_t mask)
{
    if (mask & (1u << AERIE_SETTLE)) return ZXM_E_ARG; /* AM2: messages only */
    if (!a->nbands || (mask >> a->nbands)) return ZXM_E_ARG;
    a->allowed = mask;
    return ZXM_OK;
}

int aerie_set_threshold(aerie_t *a, int64_t thr)
{
    if (!aerie_threshold_ok(thr)) return ZXM_E_ARG;
    a->threshold = thr;
    return ZXM_OK;
}

int aerie_set_quiet(aerie_t *a, int64_t opens, int64_t closes)
{
    if (!halloo_minute(opens) || !halloo_minute(closes)) return ZXM_E_ARG;
    a->quiet_opens = opens;
    a->quiet_closes = closes;
    a->quiet_set = true;
    return ZXM_OK;
}

static int32_t find_contact(const aerie_t *a, uint32_t sender)
{
    for (uint32_t i = 0; i < a->ncontacts; i++)
        if (a->contact[i].sender == sender) return (int32_t) i;
    return -1;
}

int aerie_set_contact(aerie_t *a, uint32_t sender, bool known, bool blocked)
{
    int32_t at = find_contact(a, sender);
    if (at < 0) {
        if (a->ncontacts >= AERIE_CONTACTS) return ZXM_E_STATE;
        at = (int32_t) a->ncontacts++;
    }
    a->contact[at].sender = sender;
    a->contact[at].known = known;
    a->contact[at].blocked = blocked;
    return ZXM_OK;
}

bool aerie_may_alert(const aerie_t *a, uint32_t sender, int64_t minute)
{
    int32_t at = find_contact(a, sender);
    int64_t known = at >= 0 && a->contact[at].known, blocked = at >= 0 && a->contact[at].blocked;
    int64_t quiet = a->quiet_set ? halloo_quiet_left(a->quiet_opens, a->quiet_closes, minute) : 0;
    if (quiet < 0) return false; /* a minute that is no minute: never an alert */
    return halloo_ring_ok(known, blocked, quiet);
}

/* ---- the log (AM7) ------------------------------------------------------------------ */

/* the next record, or false when its seq would pass AERIE_SEQ_MAX */
static bool log_next(const aerie_t *a, int64_t bearer, int64_t band, int64_t dir, int64_t sender,
                     int64_t bytes, int64_t stamp, aerie_msg_t *m)
{
    m->seq = a->log_seq + 1;
    m->bearer = bearer;
    m->band = band;
    m->dir = dir;
    m->sender = sender;
    m->bytes = bytes;
    m->stamp = stamp;
    return m->seq <= AERIE_SEQ_MAX && aerie_msg_ok(m->seq, bearer, band, dir, sender, bytes, stamp);
}

static void log_put(aerie_t *a, const aerie_msg_t *m)
{
    if (a->nlog == AERIE_LOG) {
        a->log_head = (a->log_head + 1u) % AERIE_LOG;
        a->nlog--;
        a->log_dropped++;
    }
    a->log[(a->log_head + a->nlog) % AERIE_LOG] = *m;
    a->nlog++;
    a->log_seq = m->seq;
}

const aerie_msg_t *aerie_log_at(const aerie_t *a, uint32_t i)
{
    return i < a->nlog ? &a->log[(a->log_head + i) % AERIE_LOG] : NULL;
}

/* ---- messages: ehop.seal and ehop.open (AM3, AM4) --------------------------------------- */

static bool stamp_ok(int64_t now_ms)
{
    return now_ms >= 0 && now_ms <= AERIE_STAMP_MAX;
}

static void put_chan(zxm_link_t *l, const aerie_t *a, const aerie_chan_t *ch)
{
    zxm_p_str(l, "key_hex", ch->key_hex);
    zxm_p_str(l, "band", a->band[ch->band].name);
    zxm_p_u64(l, "chan_id", ch->chan_id);
    zxm_p_u64(l, "epoch", ch->epoch);
}

/* AR5 first: a sealed frame never goes on, or comes off, an amateur
 * bearer; a bearer that is neither is no bearer (fails closed). Then the
 * channel's band is read and allowed, and its key is a key. */
static int chan_ok(const aerie_t *a, const aerie_chan_t *ch)
{
    if (!ch) return ZXM_E_ARG;
    int64_t amateur = ch->bearer == AERIE_PRIVATE ? 0 : ch->bearer == AERIE_AMATEUR ? 1 : 2;
    if (!aerie_ham_clear_ok(amateur, 1)) return ZXM_E_STATE;
    if (!hex_ok(ch->key_hex, AERIE_KEY_HEX)) return ZXM_E_ARG;
    if (!a->nbands || ch->band >= a->nbands) return ZXM_E_STATE;
    if (!(a->allowed & (1u << ch->band))) return ZXM_E_STATE; /* AM2 */
    return ZXM_OK;
}

int aerie_seal(aerie_t *a, const aerie_chan_t *ch, const uint8_t *data, uint32_t n, int64_t now_ms)
{
    static char hex[2u * AERIE_PAYLOAD_MAX + 1u];
    static char frame[AERIE_FRAME_HEX + 1u];
    aerie_msg_t m;
    int rc = chan_ok(a, ch);
    if (rc) return rc;
    if ((!data && n) || !stamp_ok(now_ms)) return ZXM_E_ARG;
    if (!aerie_band_fits(n, a->band[ch->band].max_frame)) return ZXM_E_ARG; /* AR2 */
    if (!log_next(a, AERIE_PRIVATE, ch->band, AERIE_OUT, ch->sender, n, now_ms, &m))
        return ZXM_E_STATE;
    if (a->next_seq == UINT64_MAX) return ZXM_E_STATE; /* ehop refuses 2^64 - 1 */
    uint64_t seq = a->next_seq++;                      /* AM3: never given twice */
    to_hex(data, n, hex);
    zxm_link_t *l = a->link;
    zxm_begin(l, "ehop.seal");
    put_chan(l, a, ch);
    zxm_p_u64(l, "sender", ch->sender);
    zxm_p_u64(l, "seq", seq);
    zxm_p_strn(l, "plaintext_hex", hex, 2u * n);
    rc = zxm_call(l);
    if (rc) return rc;
    int64_t len;
    if (zxm_get_str(l, l->result, "frame_hex", frame, sizeof frame) ||
        zxm_get_i64(l, l->result, "frame_len", &len) || len != (int64_t) n + AERIE_OVERHEAD ||
        len > a->band[ch->band].max_frame || strlen(frame) != 2u * (uint32_t) len)
        return ZXM_E_PROTOCOL;
    memcpy(a->frame_hex, frame, 2u * (uint32_t) len + 1u);
    log_put(a, &m);
    return ZXM_OK;
}

static int32_t find_seen(const aerie_t *a, uint32_t sender)
{
    for (uint32_t i = 0; i < a->nseen; i++)
        if (a->seen[i].sender == sender) return (int32_t) i;
    return -1;
}

int aerie_open(aerie_t *a, const aerie_chan_t *ch, const char *frame_hex, int64_t now_ms)
{
    static char hex[2u * AERIE_PAYLOAD_MAX + 1u];
    static uint8_t buf[AERIE_PAYLOAD_MAX];
    char seqs[24];
    int64_t sender;
    uint64_t seq;
    aerie_msg_t m;
    int rc = chan_ok(a, ch);
    if (rc) return rc;
    if (!hex_ok(frame_hex, 0) || strlen(frame_hex) > AERIE_FRAME_HEX || !stamp_ok(now_ms))
        return ZXM_E_ARG;
    zxm_link_t *l = a->link;
    zxm_begin(l, "ehop.open");
    put_chan(l, a, ch);
    zxm_p_str(l, "frame_hex", frame_hex);
    rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "sender", &sender) || sender < 0 || sender > UINT32_MAX ||
        zxm_get_str(l, l->result, "seq", seqs, sizeof seqs) || !digits_u64(seqs, &seq) ||
        zxm_get_str(l, l->result, "plaintext_hex", hex, sizeof hex))
        return ZXM_E_PROTOCOL;
    int32_t n = from_hex(hex, buf, sizeof buf);
    if (n < 0) return ZXM_E_PROTOCOL;
    /* AM4: a replay, or no room to remember a new sender, is never kept */
    int32_t at = find_seen(a, (uint32_t) sender);
    if (at >= 0 && seq <= a->seen[at].top) return ZXM_E_STATE;
    if (at < 0 && a->nseen >= AERIE_SENDERS) return ZXM_E_STATE;
    if (!log_next(a, AERIE_PRIVATE, ch->band, AERIE_IN, sender, n, now_ms, &m)) return ZXM_E_STATE;
    if (at < 0) {
        at = (int32_t) a->nseen++;
        a->seen[at].sender = (uint32_t) sender;
    }
    a->seen[at].top = seq;
    memcpy(a->data, buf, (size_t) n);
    a->ndata = (uint32_t) n;
    a->from = (uint32_t) sender;
    a->alert = aerie_may_alert(a, a->from, now_ms / 60000 % HALLOO_DAY_MIN);
    log_put(a, &m);
    return ZXM_OK;
}

/* ---- line frames: harmonic.encode and harmonic.decode (AM5) ------------------------------- */

static bool word_ok(int64_t w)
{
    return w >= INT32_MIN && w <= (int64_t) UINT32_MAX;
}

static int read_line_frame(const zxm_link_t *l, aerie_line_t *x)
{
    if (zxm_get_str(l, l->result, "frame_hex", x->frame_hex, sizeof x->frame_hex) ||
        strlen(x->frame_hex) != AERIE_LINE_HEX)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int aerie_line_encode(aerie_t *a, int64_t sync, int64_t shell, int64_t trunk,
                      const int64_t s_plus[3], const int64_t s_minus[2])
{
    static aerie_line_t x;
    char raw[160];
    int64_t tag = aerie_frame_tag(sync, shell, trunk), got;
    if (tag < 0 || !s_plus || !s_minus) return ZXM_E_ARG;
    for (uint32_t i = 0; i < 3u; i++)
        if (!word_ok(s_plus[i]) || (i < 2u && !word_ok(s_minus[i]))) return ZXM_E_ARG;
    /* the two rails as arrays, then the parts after them */
    uint32_t k = 0;
    const char *head = "{\"s_plus\":[";
    while (*head) raw[k++] = *head++;
    for (uint32_t i = 0; i < 5u; i++) {
        int64_t w = i < 3u ? s_plus[i] : s_minus[i - 3u];
        char t[24];
        uint32_t m = 0;
        uint64_t u = w < 0 ? (uint64_t) -w : (uint64_t) w;
        do t[m++] = (char) ('0' + u % 10u);
        while ((u /= 10u) != 0u);
        if (w < 0) raw[k++] = '-';
        while (m) raw[k++] = t[--m];
        const char *sep = i == 2u ? "],\"s_minus\":[" : i == 4u ? "]}" : ",";
        while (*sep) raw[k++] = *sep++;
    }
    raw[k] = 0;
    memset(&x, 0, sizeof x);
    zxm_link_t *l = a->link;
    zxm_begin(l, "harmonic.encode");
    zxm_p_raw(l, raw, k);
    zxm_p_i64(l, "trunk", trunk);
    zxm_p_i64(l, "shell", shell);
    zxm_p_bool(l, "sync", sync == 1);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "tag", &got) || got != tag ||
        zxm_get_i64(l, l->result, "trunk_hz", &x.trunk_hz) || read_line_frame(l, &x))
        return ZXM_E_PROTOCOL;
    x.tag = tag;
    x.sync = sync;
    x.shell = shell;
    x.trunk = trunk;
    for (uint32_t i = 0; i < 3u; i++) x.s_plus[i] = s_plus[i];
    for (uint32_t i = 0; i < 2u; i++) x.s_minus[i] = s_minus[i];
    a->line = x;
    return ZXM_OK;
}

static int32_t truth_of(const char *s)
{
    static const char *const NAMES[] = {"UNKNOWN", "TRUE", "FALSE", "NEUTRAL", "GLUT"};
    for (uint32_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
        if (!strcmp(s, NAMES[i])) return (int32_t) i;
    return -1;
}

int aerie_line_decode(aerie_t *a, const char *frame_hex)
{
    static aerie_line_t x;
    char truth[16];
    bool sync;
    if (!hex_ok(frame_hex, AERIE_LINE_HEX)) return ZXM_E_ARG;
    memset(&x, 0, sizeof x);
    zxm_link_t *l = a->link;
    zxm_begin(l, "harmonic.decode");
    zxm_p_str(l, "frame_hex", frame_hex);
    zxm_p_i64(l, "threshold_q16", a->threshold);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t sp = zxm_get(l, l->result, "s_plus"), sm = zxm_get(l, l->result, "s_minus");
    if (zxm_get_i64(l, l->result, "tag", &x.tag) || x.tag < 0 || x.tag > 255 ||
        zxm_get_i64(l, l->result, "trunk", &x.trunk) ||
        zxm_get_i64(l, l->result, "shell", &x.shell) || zxm_get_bool(l, l->result, "sync", &sync) ||
        zxm_get_bool(l, l->result, "axis_trunk", &x.axis_trunk) ||
        zxm_get_i64(l, l->result, "trunk_hz", &x.trunk_hz) ||
        zxm_get_bool(l, l->result, "idle", &x.idle) ||
        zxm_get_i64(l, l->result, "correlation_q16", &x.correlation_q16) ||
        zxm_get_str(l, l->result, "truth", truth, sizeof truth) || sp < 0 || sm < 0 ||
        zxm_len(l, sp) != 3u || zxm_len(l, sm) != 2u)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < 3u; i++)
        if (w4_json_i64(l->resp, &l->tok[zxm_at(l, sp, i)], &x.s_plus[i]) != W4_OK ||
            (i < 2u && w4_json_i64(l->resp, &l->tok[zxm_at(l, sm, i)], &x.s_minus[i]) != W4_OK))
            return ZXM_E_PROTOCOL;
    x.truth = truth_of(truth);
    x.sync = sync;
    int64_t ps, pe, pt;
    if (aerie_tag_split(x.tag, &ps, &pe, &pt)) {
        /* AR1: the parts zxvd answers are the parts the tag splits into */
        if (ps != x.sync || pe != x.shell || pt != x.trunk ||
            aerie_frame_tag(x.sync, x.shell, x.trunk) != x.tag)
            return ZXM_E_PROTOCOL;
    } else {
        if (x.trunk != x.tag % 10) return ZXM_E_PROTOCOL; /* W3: reserved, trunk still read */
        x.reserved = true;
    }
    if (x.truth < 0) return ZXM_E_PROTOCOL;
    memcpy(x.frame_hex, frame_hex, AERIE_LINE_HEX);
    x.frame_hex[AERIE_LINE_HEX] = 0;
    a->line = x;
    return ZXM_OK;
}

/* ---- freight (AM6) ---------------------------------------------------------------------- */

int aerie_freight_pack(aerie_t *a, const uint8_t *data, uint32_t n)
{
    static char hex[2u * AERIE_FREIGHT_MAX + 1u];
    static aerie_freight_t f;
    char id[24];
    int64_t rows;
    if (!data || !n || n > AERIE_FREIGHT_MAX) return ZXM_E_ARG;
    to_hex(data, n, hex);
    memset(&f, 0, sizeof f);
    zxm_link_t *l = a->link;
    zxm_begin(l, "freight.pack");
    zxm_p_strn(l, "data_hex", hex, 2u * n);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "freight_id", id, sizeof id) || !digits_u64(id, &f.id) ||
        zxm_get_i64(l, l->result, "k", &f.k) || f.k < 1 || f.k > 168 ||
        zxm_get_i64(l, l->result, "payload_len", &f.payload_len) || f.payload_len != n ||
        zxm_get_i64(l, l->result, "rows", &rows) || rows != 168 ||
        zxm_get_str(l, l->result, "header_hex", f.header_hex, sizeof f.header_hex) ||
        strlen(f.header_hex) != AERIE_HEADER_HEX ||
        zxm_get_str(l, l->result, "packets_hex", f.packets_hex, sizeof f.packets_hex) ||
        strlen(f.packets_hex) != AERIE_PACKETS_HEX)
        return ZXM_E_PROTOCOL;
    a->freight = f;
    return ZXM_OK;
}

int aerie_freight_unpack(aerie_t *a, const char *header_hex, const char *packets_hex)
{
    static char hex[2u * AERIE_FREIGHT_MAX + 1u];
    static uint8_t buf[AERIE_FREIGHT_MAX];
    int64_t len;
    if (!hex_ok(header_hex, AERIE_HEADER_HEX) || !hex_ok(packets_hex, 0) || !packets_hex[0] ||
        strlen(packets_hex) % 42u || strlen(packets_hex) > 16384u)
        return ZXM_E_ARG;
    zxm_link_t *l = a->link;
    zxm_begin(l, "freight.unpack");
    zxm_p_str(l, "header_hex", header_hex);
    zxm_p_str(l, "packets_hex", packets_hex);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "payload_len", &len) || len < 0 ||
        len > (int64_t) AERIE_FREIGHT_MAX || zxm_get_str(l, l->result, "data_hex", hex, sizeof hex))
        return ZXM_E_PROTOCOL;
    int32_t n = from_hex(hex, buf, sizeof buf);
    if (n != len) return ZXM_E_PROTOCOL;
    memcpy(a->data, buf, (size_t) n);
    a->ndata = (uint32_t) n;
    return ZXM_OK;
}

/* ---- the amateur path: cleartext only, never ehop.seal (AM8, AM9) ------------------------ */

bool aerie_call_ok(const char *call)
{
    uint32_t i = 0, letters = 0, digits = 0;
    if (!call) return false;
    for (; call[i] && call[i] != '-'; i++) {
        char c = call[i];
        if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z')
            letters++;
        else if (c >= '0' && c <= '9')
            digits++;
        else
            return false;
    }
    if (i < 1u || i > 6u || !letters || !digits) return false;
    if (!call[i]) return true;
    /* -SSID, 0..15, no leading zero */
    const char *s = call + i + 1;
    uint32_t n = 0, v = 0;
    while (s[n] >= '0' && s[n] <= '9' && n < 3u) v = v * 10u + (uint32_t) (s[n++] - '0');
    return n >= 1u && !s[n] && v <= 15u && !(n > 1u && s[0] == '0');
}

int aerie_ham_enable(aerie_t *a, const char *call, bool licence_confirmed)
{
    if (!aerie_call_ok(call)) return ZXM_E_ARG;
    if (!licence_confirmed) return ZXM_E_STATE; /* br_ham.h H1: the owner's word first */
    uint32_t i = 0;
    for (; call[i]; i++)
        a->call[i] = call[i] >= 'a' && call[i] <= 'z' ? (char) (call[i] - 'a' + 'A') : call[i];
    a->call[i] = 0;
    a->ham_on = true;
    return ZXM_OK;
}

void aerie_ham_disable(aerie_t *a)
{
    a->ham_on = false;
}

static bool ham_byte(char c)
{
    return (c >= 0x20 && c <= 0x7e) || c == '\t' || c == '\r' || c == '\n';
}

int aerie_ham_compose(aerie_t *a, const char *text, uint32_t n, int64_t now_ms)
{
    aerie_msg_t m;
    if (!aerie_ham_clear_ok(1, 0)) return ZXM_E_STATE; /* AR5: in the clear */
    if (!a->ham_on) return ZXM_E_STATE;                /* AM8 */
    if (!text || !n || n > AERIE_HAM_TEXT_MAX || !stamp_ok(now_ms)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (!ham_byte(text[i])) return ZXM_E_ARG; /* printable text only, nothing hidden */
    uint32_t head = (uint32_t) strlen(AERIE_HAM_HEAD), call = (uint32_t) strlen(a->call);
    uint32_t len = head + call + 1u + n;
    if (!log_next(a, AERIE_AMATEUR, 0, AERIE_OUT, 0, len, now_ms, &m)) return ZXM_E_STATE;
    memcpy(a->ham_text, AERIE_HAM_HEAD, head);
    memcpy(a->ham_text + head, a->call, call);
    a->ham_text[head + call] = '\n';
    memcpy(a->ham_text + head + call + 1u, text, n);
    a->ham_text[len] = 0;
    a->ham_len = len;
    log_put(a, &m);
    return ZXM_OK;
}

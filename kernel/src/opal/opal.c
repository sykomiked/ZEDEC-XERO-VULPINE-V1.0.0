/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* opal.c — TCG Opal host: Level 0 Discovery, tokens, framing, the unlock
 * methods (opal.h). Wire values are big-endian throughout. */
#include "opal.h"

static const uint8_t UID_SMUID[8] = {0, 0, 0, 0, 0, 0, 0, 0xFF};
static const uint8_t UID_LOCKINGSP[8] = {0, 0, 2, 5, 0, 0, 0, 2};
static const uint8_t UID_MBRCONTROL[8] = {0, 0, 8, 3, 0, 0, 0, 1};
static const uint8_t M_STARTSESSION[8] = {0, 0, 0, 0, 0, 0, 0xFF, 2};
static const uint8_t M_SYNCSESSION[8] = {0, 0, 0, 0, 0, 0, 0xFF, 3};
static const uint8_t M_SET[8] = {0, 0, 0, 6, 0, 0, 0, 0x17};

#define NEST_MAX 16u

static uint32_t be16(const uint8_t *p)
{
    return ((uint32_t) p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static void put_be16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

void opal_wipe(void *p, uint32_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) v[i] = 0;
}

void opal_init(opal_dev_t *d, opal_if_fn send, opal_if_fn recv, void *ctx)
{
    opal_wipe(d, sizeof *d);
    d->send = send;
    d->recv = recv;
    d->ctx = ctx;
    d->polls = OPAL_POLLS;
}

/* ---- Level 0 Discovery ---------------------------------------------------- */

static opal_ssc_t ssc_of(uint32_t code)
{
    switch (code) {
    case OPAL_FC_OPAL2:
        return OPAL_SSC_OPAL2;
    case OPAL_FC_RUBY:
        return OPAL_SSC_RUBY;
    case OPAL_FC_OPAL1:
        return OPAL_SSC_OPAL1;
    case OPAL_FC_PYRITE2:
        return OPAL_SSC_PYRITE2;
    case OPAL_FC_PYRITE1:
        return OPAL_SSC_PYRITE1;
    default:
        return OPAL_SSC_NONE;
    }
}

int opal_parse_discovery(const uint8_t *buf, uint32_t n, opal_disc_t *out)
{
    opal_wipe(out, sizeof *out);
    if (!buf || n < 48) return OPAL_E_DISC;
    uint32_t hl = be32(buf); /* bytes after the length field */
    if (hl > n - 4 || hl + 4 < 48) return OPAL_E_DISC;
    uint32_t total = hl + 4;
    out->revision = be32(buf + 4);
    for (uint32_t off = 48; off < total;) {
        if (total - off < 4) return OPAL_E_DISC;
        uint32_t code = be16(buf + off), fl = buf[off + 3];
        if (fl > total - off - 4) return OPAL_E_DISC;
        const uint8_t *f = buf + off + 4;
        out->features++;
        opal_ssc_t s = ssc_of(code);
        if (s != OPAL_SSC_NONE) {
            if (fl < 4) return OPAL_E_DISC;
            if (s > out->ssc) {
                out->ssc = s;
                out->base_comid = (uint16_t) be16(f);
                out->num_comids = (uint16_t) be16(f + 2);
                int v2 = code == OPAL_FC_OPAL2 || code == OPAL_FC_RUBY;
                out->num_admins = (uint16_t) (v2 && fl >= 9 ? be16(f + 5) : 0);
                out->num_users = (uint16_t) (v2 && fl >= 9 ? be16(f + 7) : 0);
            }
        } else if (code == OPAL_FC_TPER || code == OPAL_FC_LOCKING) {
            if (fl < 1) return OPAL_E_DISC;
            if (code == OPAL_FC_TPER) {
                out->have_tper = 1;
                out->tper_flags = f[0];
            } else {
                out->have_locking = 1;
                out->locking_flags = f[0];
            }
        } else if (code == OPAL_FC_ENTERPRISE) {
            out->enterprise = 1;
        } else if (code != OPAL_FC_GEOMETRY && code != OPAL_FC_SINGLEUSER &&
                   code != OPAL_FC_DATASTORE && code != OPAL_FC_BLOCKSID) {
            if (!out->unknown) out->first_unknown = (uint16_t) code;
            out->unknown++;
        }
        off += 4 + fl;
    }
    if (out->ssc != OPAL_SSC_NONE &&
        (out->base_comid == 0 || out->base_comid == OPAL_COMID_LEVEL0 || out->num_comids == 0))
        return OPAL_E_DISC;
    return OPAL_OK;
}

int opal_disc_usable(const opal_disc_t *disc)
{
    if (!disc->have_locking || !(disc->locking_flags & OPAL_LK_SUPPORTED)) return OPAL_E_NOT_SED;
    if (!disc->have_tper || disc->ssc == OPAL_SSC_NONE) return OPAL_E_UNSUPPORTED;
    return OPAL_OK;
}

int opal_discover(opal_dev_t *d)
{
    d->have_disc = 0;
    d->comid = 0;
    opal_wipe(d->resp, OPAL_MAX_COMPACKET);
    if (d->recv(d->ctx, OPAL_PROTO_TCG, OPAL_COMID_LEVEL0, d->resp, OPAL_MAX_COMPACKET) != 0)
        return OPAL_E_TRANSPORT;
    int r = opal_parse_discovery(d->resp, OPAL_MAX_COMPACKET, &d->disc);
    if (r != OPAL_OK) return r;
    d->have_disc = 1;
    r = opal_disc_usable(&d->disc);
    if (r == OPAL_OK) d->comid = d->disc.base_comid;
    return r;
}

int opal_needs_unlock(const opal_disc_t *disc)
{
    uint32_t f = disc->locking_flags;
    if (!(f & OPAL_LK_ENABLED)) return 0;
    return (f & OPAL_LK_LOCKED) || ((f & OPAL_LK_MBR_EN) && !(f & OPAL_LK_MBR_DONE));
}

/* ---- tokens ---------------------------------------------------------------- */

static int is_ctl(uint32_t h)
{
    return (h >= OPAL_TK_STARTLIST && h <= OPAL_TK_ENDNAME) ||
           (h >= OPAL_TK_CALL && h <= OPAL_TK_ENDTRANS);
}

int opal_tokenize(const uint8_t *buf, uint32_t base, uint32_t n, opal_tok_t *tok, uint32_t max)
{
    const uint8_t *p = buf + base;
    uint32_t i = 0, cnt = 0;
    while (i < n) {
        uint32_t h = p[i], hdr, len, bytes, sgn;
        if (h == OPAL_TK_EMPTY) {
            i++;
            continue;
        }
        if (cnt >= max) return OPAL_E_TOOBIG;
        opal_tok_t *t = &tok[cnt];
        t->off = 0;
        t->len = 0;
        t->u = 0;
        t->ctl = 0;
        if (h < 0x80) { /* tiny atom: sign bit 6, value bits 5..0 */
            t->kind = (uint8_t) ((h & 0x40) ? OPAL_K_SINT : OPAL_K_UINT);
            t->u = h & 0x3F;
            t->len = 0;
            cnt++;
            i++;
            continue;
        }
        if (is_ctl(h)) {
            t->kind = OPAL_K_CTRL;
            t->ctl = (uint8_t) h;
            cnt++;
            i++;
            continue;
        }
        if (h < 0xC0) { /* short: 10 B S len4 */
            hdr = 1;
            bytes = h & 0x20;
            sgn = h & 0x10;
            len = h & 0x0F;
        } else if (h < 0xE0) { /* medium: 110 B S len11 */
            if (n - i < 2) return OPAL_E_TOKEN;
            hdr = 2;
            bytes = h & 0x10;
            sgn = h & 0x08;
            len = ((h & 0x07) << 8) | p[i + 1];
        } else if (h < 0xE4) { /* long: 111000 B S len24 */
            if (n - i < 4) return OPAL_E_TOKEN;
            hdr = 4;
            bytes = h & 0x02;
            sgn = h & 0x01;
            len = ((uint32_t) p[i + 1] << 16) | ((uint32_t) p[i + 2] << 8) | p[i + 3];
        } else {
            return OPAL_E_TOKEN; /* reserved token values */
        }
        if (len > n - i - hdr) return OPAL_E_TOKEN;
        if (bytes) {
            if (sgn) return OPAL_E_TOKEN; /* continued byte sequences: unused */
            t->kind = OPAL_K_BYTES;
            t->off = base + i + hdr;
            t->len = len;
        } else {
            if (len == 0 || len > 8) return OPAL_E_TOKEN;
            uint64_t v = 0;
            for (uint32_t k = 0; k < len; k++) v = (v << 8) | p[i + hdr + k];
            t->kind = (uint8_t) (sgn ? OPAL_K_SINT : OPAL_K_UINT);
            t->u = v;
            t->len = len;
        }
        cnt++;
        i += hdr + len;
    }
    return (int) cnt;
}

/* ---- command builder ------------------------------------------------------ */

static void begin(opal_dev_t *d)
{
    opal_wipe(d->cmd, OPAL_MAX_COMPACKET);
    d->pos = OPAL_HDRS;
    d->berr = 0;
}

static void put(opal_dev_t *d, const uint8_t *p, uint32_t n)
{
    if (d->berr || n > OPAL_MAX_COMPACKET - d->pos) {
        d->berr = 1;
        return;
    }
    for (uint32_t i = 0; i < n; i++) d->cmd[d->pos + i] = p[i];
    d->pos += n;
}

static void put1(opal_dev_t *d, uint32_t b)
{
    uint8_t c = (uint8_t) b;
    put(d, &c, 1);
}

static void tok_uint(opal_dev_t *d, uint64_t v)
{
    if (v < 64) {
        put1(d, (uint32_t) v);
        return;
    }
    uint32_t n = 1;
    while (n < 8 && (v >> (8 * n)) != 0) n++;
    put1(d, 0x80u | n);
    for (uint32_t k = n; k > 0; k--) put1(d, (uint32_t) (v >> (8 * (k - 1))));
}

static void tok_bytes(opal_dev_t *d, const uint8_t *p, uint32_t n)
{
    if (n < 16) {
        put1(d, 0xA0u | n);
    } else if (n < 2048) {
        put1(d, 0xD0u | (n >> 8));
        put1(d, n & 0xFF);
    } else {
        d->berr = 1;
        return;
    }
    put(d, p, n);
}

static void call(opal_dev_t *d, const uint8_t inv[8], const uint8_t meth[8])
{
    put1(d, OPAL_TK_CALL);
    tok_bytes(d, inv, 8);
    tok_bytes(d, meth, 8);
}

/* Append the status list (unless EndOfSession), fill in the three headers,
 * pad to 4 bytes, IF-SEND the whole buffer, then wipe it. */
static int send_cmd(opal_dev_t *d, int status_list)
{
    if (status_list) {
        put1(d, OPAL_TK_ENDOFDATA);
        put1(d, OPAL_TK_STARTLIST);
        put1(d, 0);
        put1(d, 0);
        put1(d, 0);
        put1(d, OPAL_TK_ENDLIST);
    }
    if (d->berr) {
        opal_wipe(d->cmd, OPAL_MAX_COMPACKET);
        return OPAL_E_TOOBIG;
    }
    uint32_t sub = d->pos - OPAL_HDRS;
    d->pos = (d->pos + 3u) & ~3u; /* the pad bytes are already zero */
    uint8_t *c = d->cmd;
    put_be16(c + 4, d->comid);
    put_be32(c + 16, d->pos - OPAL_CP_HDR);
    put_be32(c + 20, d->in_session ? d->tsn : 0);
    put_be32(c + 24, d->in_session ? d->hsn : 0);
    put_be32(c + 40, d->pos - OPAL_CP_HDR - OPAL_PKT_HDR);
    put_be32(c + 52, sub);
    int r = d->send(d->ctx, OPAL_PROTO_TCG, d->comid, c, OPAL_MAX_COMPACKET);
    opal_wipe(d->cmd, OPAL_MAX_COMPACKET);
    return r ? OPAL_E_TRANSPORT : OPAL_OK;
}

/* ---- response ---------------------------------------------------------- */

/* IF-RECV until the TPer has the response, check every header length against
 * the buffer and the one inside it, then tokenize the payload. */
static int recv_resp(opal_dev_t *d)
{
    uint32_t polls = d->polls ? d->polls : 1;
    d->ntok = 0;
    for (uint32_t k = 0;; k++) {
        const uint8_t *r = d->resp;
        opal_wipe(d->resp, OPAL_MAX_COMPACKET);
        if (d->recv(d->ctx, OPAL_PROTO_TCG, d->comid, d->resp, OPAL_MAX_COMPACKET) != 0)
            return OPAL_E_TRANSPORT;
        if (be16(r + 4) != d->comid || be16(r + 6) != 0) return OPAL_E_COMID;
        uint32_t outstanding = be32(r + 8), min_xfer = be32(r + 12), cpl = be32(r + 16);
        if (cpl == 0) {
            /* nothing yet; MinTransfer says how much room the answer needs */
            if (min_xfer > OPAL_MAX_COMPACKET - OPAL_CP_HDR) return OPAL_E_TOOBIG;
            if (k + 1 >= polls) return OPAL_E_NO_RESPONSE;
            continue;
        }
        if (outstanding != 0) return OPAL_E_TOOBIG; /* more than one buffer */
        if (cpl > OPAL_MAX_COMPACKET - OPAL_CP_HDR || cpl < OPAL_PKT_HDR + OPAL_SUB_HDR)
            return OPAL_E_FRAME;
        uint32_t tsn = be32(r + 20), hsn = be32(r + 24), pkl = be32(r + 40);
        if (pkl > cpl - OPAL_PKT_HDR || pkl < OPAL_SUB_HDR) return OPAL_E_FRAME;
        uint32_t kind = be16(r + 50), sl = be32(r + 52);
        if (kind != 0 || sl > pkl - OPAL_SUB_HDR) return OPAL_E_FRAME;
        if (tsn != (d->in_session ? d->tsn : 0) || hsn != (d->in_session ? d->hsn : 0))
            return OPAL_E_SESSION;
        int c = opal_tokenize(d->resp, OPAL_HDRS, sl, d->tok, OPAL_MAX_TOKENS);
        if (c < 0) return c;
        d->ntok = (uint32_t) c;
        return OPAL_OK;
    }
}

static int tk_ctl(const opal_tok_t *t, uint32_t c)
{
    return t->kind == OPAL_K_CTRL && t->ctl == c;
}

static int tk_uint(const opal_tok_t *t)
{
    return t->kind == OPAL_K_UINT;
}

static int tk_uid(const opal_dev_t *d, const opal_tok_t *t, const uint8_t uid[8])
{
    if (t->kind != OPAL_K_BYTES || t->len != 8) return 0;
    for (uint32_t i = 0; i < 8; i++)
        if (d->resp[t->off + i] != uid[i]) return 0;
    return 1;
}

/* A method response: <results> EndOfData [status 0 0]. Checks the status
 * list and that lists and names nest inside the results. *end is the token
 * count before EndOfData. A lone EndOfSession means the TPer closed it. */
static int method_status(opal_dev_t *d, uint32_t *end)
{
    uint32_t n = d->ntok;
    const opal_tok_t *t = d->tok;
    if (n == 1 && tk_ctl(&t[0], OPAL_TK_ENDOFSESSION)) {
        d->in_session = 0;
        return OPAL_E_SESSION;
    }
    if (n < 6) return OPAL_E_TOKEN;
    const opal_tok_t *s = t + n - 6;
    if (!tk_ctl(&s[0], OPAL_TK_ENDOFDATA) || !tk_ctl(&s[1], OPAL_TK_STARTLIST) || !tk_uint(&s[2]) ||
        !tk_uint(&s[3]) || !tk_uint(&s[4]) || !tk_ctl(&s[5], OPAL_TK_ENDLIST))
        return OPAL_E_TOKEN;
    if (s[2].u > 0xFF) return OPAL_E_TOKEN;
    uint8_t stack[NEST_MAX];
    uint32_t depth = 0;
    for (uint32_t i = 0; i < n - 6; i++) {
        if (t[i].kind != OPAL_K_CTRL) continue;
        uint32_t c = t[i].ctl;
        if (c == OPAL_TK_STARTLIST || c == OPAL_TK_STARTNAME) {
            if (depth >= NEST_MAX) return OPAL_E_TOKEN;
            stack[depth++] = (uint8_t) c;
        } else if (c == OPAL_TK_ENDLIST || c == OPAL_TK_ENDNAME) {
            uint32_t open = c == OPAL_TK_ENDLIST ? OPAL_TK_STARTLIST : OPAL_TK_STARTNAME;
            if (depth == 0 || stack[depth - 1] != open) return OPAL_E_TOKEN;
            depth--;
        } else if (!(c == OPAL_TK_CALL && i == 0)) {
            return OPAL_E_TOKEN;
        }
    }
    if (depth != 0) return OPAL_E_TOKEN;
    d->status = (uint8_t) s[2].u;
    *end = n - 6;
    return OPAL_OK;
}

/* Send what the builder holds, receive, check status; the results must be
 * one list (Set answers an empty one). */
static int exchange_set(opal_dev_t *d)
{
    int r = send_cmd(d, 1);
    if (r != OPAL_OK) return r;
    r = recv_resp(d);
    if (r != OPAL_OK) return r;
    uint32_t end;
    r = method_status(d, &end);
    if (r != OPAL_OK) return r;
    if (d->status != OPAL_ST_SUCCESS) return OPAL_E_STATUS;
    if (end < 2 || !tk_ctl(&d->tok[0], OPAL_TK_STARTLIST) ||
        !tk_ctl(&d->tok[end - 1], OPAL_TK_ENDLIST))
        return OPAL_E_TOKEN;
    return OPAL_OK;
}

/* ---- UIDs and methods ------------------------------------------------------- */

void opal_uid_auth(opal_auth_t a, uint8_t uid[8])
{
    static const uint8_t admin1[8] = {0, 0, 0, 9, 0, 1, 0, 1};
    for (uint32_t i = 0; i < 8; i++) uid[i] = admin1[i];
    if (a.kind == OPAL_AUTH_USER) {
        uid[5] = 3;
        uid[6] = (uint8_t) (a.user >> 8);
        uid[7] = (uint8_t) a.user;
    }
}

void opal_uid_range(uint32_t range, uint8_t uid[8])
{
    static const uint8_t global[8] = {0, 0, 8, 2, 0, 0, 0, 1};
    for (uint32_t i = 0; i < 8; i++) uid[i] = global[i];
    if (range != 0) {
        uid[5] = 3;
        uid[6] = (uint8_t) (range >> 8);
        uid[7] = (uint8_t) range;
    }
}

int opal_start_session(opal_dev_t *d, opal_auth_t a, const uint8_t *key, uint32_t keylen)
{
    if (!d->have_disc || d->comid == 0 || d->in_session) return OPAL_E_STATE;
    if (!key || keylen == 0 || keylen > OPAL_KEY_MAX) return OPAL_E_ARG;
    if (a.kind == OPAL_AUTH_USER &&
        (a.user == 0 || a.user > 0xFFFF || (d->disc.num_users && a.user > d->disc.num_users)))
        return OPAL_E_ARG;
    if (a.kind != OPAL_AUTH_USER && a.kind != OPAL_AUTH_ADMIN1) return OPAL_E_ARG;
    uint8_t auth[8];
    opal_uid_auth(a, auth);
    begin(d);
    call(d, UID_SMUID, M_STARTSESSION);
    put1(d, OPAL_TK_STARTLIST);
    tok_uint(d, OPAL_HSN);
    tok_bytes(d, UID_LOCKINGSP, 8);
    tok_uint(d, 1); /* Write */
    put1(d, OPAL_TK_STARTNAME);
    tok_uint(d, OPAL_SS_CHALLENGE);
    tok_bytes(d, key, keylen);
    put1(d, OPAL_TK_ENDNAME);
    put1(d, OPAL_TK_STARTNAME);
    tok_uint(d, OPAL_SS_AUTHORITY);
    tok_bytes(d, auth, 8);
    put1(d, OPAL_TK_ENDNAME);
    put1(d, OPAL_TK_ENDLIST);
    int r = send_cmd(d, 1);
    if (r != OPAL_OK) return r;
    r = recv_resp(d);
    if (r != OPAL_OK) return r;
    uint32_t end;
    r = method_status(d, &end);
    if (r != OPAL_OK) return r == OPAL_E_SESSION ? OPAL_E_TOKEN : r;
    if (d->status != OPAL_ST_SUCCESS) return OPAL_E_STATUS;
    /* Call SMUID SyncSession [ HostSessionID SPSessionID ... ] */
    const opal_tok_t *t = d->tok;
    if (end < 7 || !tk_ctl(&t[0], OPAL_TK_CALL) || !tk_uid(d, &t[1], UID_SMUID) ||
        !tk_uid(d, &t[2], M_SYNCSESSION) || !tk_ctl(&t[3], OPAL_TK_STARTLIST) || !tk_uint(&t[4]) ||
        !tk_uint(&t[5]) || !tk_ctl(&t[end - 1], OPAL_TK_ENDLIST))
        return OPAL_E_TOKEN;
    if (t[4].u != OPAL_HSN || t[5].u < OPAL_FIRST_TSN || t[5].u > 0xFFFFFFFFu)
        return OPAL_E_SESSION;
    d->hsn = OPAL_HSN;
    d->tsn = (uint32_t) t[5].u;
    d->in_session = 1;
    return OPAL_OK;
}

static void set_values(opal_dev_t *d, const uint32_t *col, const uint32_t *val, uint32_t n)
{
    put1(d, OPAL_TK_STARTLIST);
    put1(d, OPAL_TK_STARTNAME);
    tok_uint(d, OPAL_SET_VALUES);
    put1(d, OPAL_TK_STARTLIST);
    for (uint32_t i = 0; i < n; i++) {
        put1(d, OPAL_TK_STARTNAME);
        tok_uint(d, col[i]);
        tok_uint(d, val[i]);
        put1(d, OPAL_TK_ENDNAME);
    }
    put1(d, OPAL_TK_ENDLIST);
    put1(d, OPAL_TK_ENDNAME);
    put1(d, OPAL_TK_ENDLIST);
}

int opal_set_range(opal_dev_t *d, uint32_t range, int read_locked, int write_locked)
{
    if (!d->in_session) return OPAL_E_STATE;
    if (range > OPAL_RANGES_MAX) return OPAL_E_ARG;
    uint8_t uid[8];
    opal_uid_range(range, uid);
    const uint32_t col[2] = {OPAL_COL_READLOCKED, OPAL_COL_WRITELOCKED};
    const uint32_t val[2] = {read_locked ? 1u : 0u, write_locked ? 1u : 0u};
    begin(d);
    call(d, uid, M_SET);
    set_values(d, col, val, 2);
    return exchange_set(d);
}

int opal_set_mbr_done(opal_dev_t *d, int done)
{
    if (!d->in_session) return OPAL_E_STATE;
    const uint32_t col[1] = {OPAL_COL_MBR_DONE};
    const uint32_t val[1] = {done ? 1u : 0u};
    begin(d);
    call(d, UID_MBRCONTROL, M_SET);
    set_values(d, col, val, 1);
    return exchange_set(d);
}

int opal_end_session(opal_dev_t *d)
{
    if (!d->in_session) return OPAL_E_STATE;
    begin(d);
    put1(d, OPAL_TK_ENDOFSESSION);
    int r = send_cmd(d, 0);
    if (r == OPAL_OK) r = recv_resp(d);
    if (r == OPAL_OK && (d->ntok != 1 || !tk_ctl(&d->tok[0], OPAL_TK_ENDOFSESSION)))
        r = OPAL_E_TOKEN;
    d->in_session = 0; /* either way this session is not used again */
    d->tsn = d->hsn = 0;
    return r;
}

int opal_unlock(opal_dev_t *d, opal_auth_t a, const uint8_t *key, uint32_t keylen, uint32_t range,
                opal_unlock_result_t *res)
{
    opal_wipe(res, sizeof *res);
    if (d->in_session) return OPAL_E_STATE;
    int r = opal_discover(d);
    if (r != OPAL_OK) {
        res->stage = OPAL_STAGE_DISCOVERY;
        return r;
    }
    res->flags_before = res->flags_after = d->disc.locking_flags;
    if (!opal_needs_unlock(&d->disc)) return OPAL_OK;
    res->needed = 1;
    if (range > OPAL_RANGES_MAX) {
        res->stage = OPAL_STAGE_RANGE;
        return OPAL_E_ARG;
    }
    r = opal_start_session(d, a, key, keylen);
    if (r != OPAL_OK) {
        res->stage = OPAL_STAGE_START;
        res->status = d->status;
        return r;
    }
    r = opal_set_range(d, range, 0, 0);
    if (r != OPAL_OK) {
        res->stage = OPAL_STAGE_RANGE;
        res->status = d->status;
    }
    uint32_t f = res->flags_before;
    if (r == OPAL_OK && (f & OPAL_LK_MBR_EN) && !(f & OPAL_LK_MBR_DONE)) {
        r = opal_set_mbr_done(d, 1);
        if (r != OPAL_OK) {
            res->stage = OPAL_STAGE_MBR;
            res->status = d->status;
        }
    }
    if (d->in_session) {
        int e = opal_end_session(d);
        if (r == OPAL_OK && e != OPAL_OK) {
            res->stage = OPAL_STAGE_END;
            r = e;
        }
    }
    if (r != OPAL_OK) return r;
    r = opal_discover(d);
    if (r != OPAL_OK) {
        res->stage = OPAL_STAGE_DISCOVERY;
        return r;
    }
    res->flags_after = d->disc.locking_flags;
    return OPAL_OK;
}

/* ---- names ----------------------------------------------------------------- */

const char *opal_err_str(int err)
{
    switch (err) {
    case OPAL_OK:
        return "ok";
    case OPAL_E_TRANSPORT:
        return "transport refused";
    case OPAL_E_DISC:
        return "Level 0 Discovery malformed";
    case OPAL_E_NOT_SED:
        return "no Locking feature";
    case OPAL_E_UNSUPPORTED:
        return "no supported SSC";
    case OPAL_E_FRAME:
        return "malformed ComPacket";
    case OPAL_E_COMID:
        return "wrong ComID";
    case OPAL_E_SESSION:
        return "session mismatch or closed by TPer";
    case OPAL_E_TOKEN:
        return "bad token stream";
    case OPAL_E_STATUS:
        return "method failed";
    case OPAL_E_NO_RESPONSE:
        return "no response";
    case OPAL_E_TOOBIG:
        return "too big for the buffer";
    case OPAL_E_ARG:
        return "bad argument";
    case OPAL_E_STATE:
        return "wrong state";
    case OPAL_E_NOT_ATTESTED:
        return "no attested arbiter";
    case OPAL_E_KEY:
        return "key not released";
    default:
        return "unknown error";
    }
}

const char *opal_status_str(uint32_t status)
{
    switch (status) {
    case OPAL_ST_SUCCESS:
        return "SUCCESS";
    case OPAL_ST_NOT_AUTHORIZED:
        return "NOT_AUTHORIZED";
    case OPAL_ST_SP_BUSY:
        return "SP_BUSY";
    case OPAL_ST_SP_FAILED:
        return "SP_FAILED";
    case OPAL_ST_SP_DISABLED:
        return "SP_DISABLED";
    case OPAL_ST_SP_FROZEN:
        return "SP_FROZEN";
    case OPAL_ST_NO_SESSIONS:
        return "NO_SESSIONS_AVAILABLE";
    case OPAL_ST_UNIQUENESS:
        return "UNIQUENESS_CONFLICT";
    case OPAL_ST_INSUFF_SPACE:
        return "INSUFFICIENT_SPACE";
    case OPAL_ST_INSUFF_ROWS:
        return "INSUFFICIENT_ROWS";
    case OPAL_ST_INVALID_PARAM:
        return "INVALID_PARAMETER";
    case OPAL_ST_TPER_MALFUNCTION:
        return "TPER_MALFUNCTION";
    case OPAL_ST_TRANS_FAILURE:
        return "TRANSACTION_FAILURE";
    case OPAL_ST_RESP_OVERFLOW:
        return "RESPONSE_OVERFLOW";
    case OPAL_ST_AUTH_LOCKED_OUT:
        return "AUTHORITY_LOCKED_OUT";
    case OPAL_ST_FAIL:
        return "FAIL";
    default:
        return "UNKNOWN_STATUS";
    }
}

const char *opal_ssc_str(opal_ssc_t ssc)
{
    switch (ssc) {
    case OPAL_SSC_OPAL2:
        return "Opal 2";
    case OPAL_SSC_OPAL1:
        return "Opal 1";
    case OPAL_SSC_RUBY:
        return "Ruby";
    case OPAL_SSC_PYRITE2:
        return "Pyrite 2";
    case OPAL_SSC_PYRITE1:
        return "Pyrite 1";
    default:
        return "none";
    }
}

const char *opal_stage_str(opal_stage_t stage)
{
    switch (stage) {
    case OPAL_STAGE_DISCOVERY:
        return "Discovery";
    case OPAL_STAGE_START:
        return "StartSession";
    case OPAL_STAGE_RANGE:
        return "Set Locking range";
    case OPAL_STAGE_MBR:
        return "Set MBRControl";
    case OPAL_STAGE_END:
        return "EndSession";
    case OPAL_STAGE_KEY:
        return "arbiter key";
    default:
        return "none";
    }
}

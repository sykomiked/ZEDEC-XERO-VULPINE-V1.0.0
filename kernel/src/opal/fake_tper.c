/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fake_tper.c — the fake Opal TPer (fake_tper.h). Test code: libc is fine. */
#include <string.h>
#include "fake_tper.h"

#define T_UINT  0
#define T_BYTES 1
#define T_CTL   2

typedef struct {
    int type;
    uint32_t ctl;
    uint64_t v;
    const uint8_t *b;
    uint32_t n;
} ft_tok_t;

static const uint8_t SMUID[8] = {0, 0, 0, 0, 0, 0, 0, 0xFF};
static const uint8_t LOCKINGSP[8] = {0, 0, 2, 5, 0, 0, 0, 2};
static const uint8_t ADMIN1[8] = {0, 0, 0, 9, 0, 1, 0, 1};
static const uint8_t USER1[8] = {0, 0, 0, 9, 0, 3, 0, 1};
static const uint8_t MBRCTL[8] = {0, 0, 8, 3, 0, 0, 0, 1};
static const uint8_t STARTSESSION[8] = {0, 0, 0, 0, 0, 0, 0xFF, 2};
static const uint8_t SYNCSESSION[8] = {0, 0, 0, 0, 0, 0, 0xFF, 3};
static const uint8_t SET[8] = {0, 0, 0, 6, 0, 0, 0, 0x17};

static uint32_t rd16(const uint8_t *p)
{
    return ((uint32_t) p[0] << 8) | p[1];
}
static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static void wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

void ftper_init(ftper_t *f, const uint8_t *a1, uint32_t n, const uint8_t *u1, uint32_t m)
{
    memset(f, 0, sizeof *f);
    f->comid = 0x07FE;
    f->ssc_code = 0x0203;
    f->num_comids = 1;
    f->locking_supported = 1;
    f->locking_enabled = 1;
    f->user_ranges = 0xFFFFFFFFu;
    f->tsn_start = 0x1001;
    if (n > 32) n = 32;
    if (m > 32) m = 32;
    memcpy(f->admin1, a1, n);
    f->admin1_len = n;
    if (u1) memcpy(f->user1, u1, m);
    f->user1_len = u1 ? m : 0;
}

uint8_t ftper_locking_flags(const ftper_t *f)
{
    uint8_t b = 0;
    if (f->locking_supported) b |= 0x01;
    if (f->locking_enabled) b |= 0x02;
    int locked = 0;
    for (uint32_t i = 0; i < FT_RANGES; i++) locked |= f->rl[i] | f->wl[i];
    if (f->locking_enabled && locked) b |= 0x04;
    b |= 0x08; /* media encryption */
    if (f->mbr_enabled) b |= 0x10;
    if (f->mbr_done) b |= 0x20;
    return b;
}

uint32_t ftper_level0(const ftper_t *f, uint8_t *buf, uint32_t len)
{
    uint8_t d[256];
    memset(d, 0, sizeof d);
    uint32_t o = 48;
    wr32(d + 4, 1); /* revision */
    if (!f->no_tper) {
        wr16(d + o, 0x0001);
        d[o + 2] = 0x10;
        d[o + 3] = 12;
        d[o + 4] = 0x11; /* sync, streaming */
        o += 16;
    }
    if (!f->no_locking) {
        wr16(d + o, 0x0002);
        d[o + 2] = 0x10;
        d[o + 3] = 12;
        d[o + 4] = ftper_locking_flags(f);
        o += 16;
    }
    wr16(d + o, 0x0003); /* geometry: known, not used */
    d[o + 2] = 0x10;
    d[o + 3] = 28;
    o += 32;
    if (f->enterprise) {
        wr16(d + o, 0x0100);
        d[o + 2] = 0x10;
        d[o + 3] = 12;
        wr16(d + o + 4, 0x07FE);
        wr16(d + o + 6, 1);
        o += 16;
    }
    if (f->ssc_code) {
        wr16(d + o, f->ssc_code);
        d[o + 2] = 0x10;
        d[o + 3] = 16;
        wr16(d + o + 4, f->comid);
        wr16(d + o + 6, f->num_comids);
        wr16(d + o + 9, 4);  /* Locking SP admins */
        wr16(d + o + 11, 8); /* Locking SP users */
        o += 20;
    }
    wr32(d, o - 4);
    uint32_t n = o < len ? o : len;
    memcpy(buf, d, n);
    return n;
}

/* ---- the fake's own token reader ---------------------------------------- */

static int read_tokens(ftper_t *f, const uint8_t *p, uint32_t n, ft_tok_t *t, uint32_t max)
{
    uint32_t i = 0, c = 0;
    while (i < n && c < max) {
        uint32_t h = p[i];
        ft_tok_t *k = &t[c];
        memset(k, 0, sizeof *k);
        if (h <= 0x3F) {
            k->type = T_UINT;
            k->v = h;
            i++;
        } else if (h >= 0xF0) {
            k->type = T_CTL;
            k->ctl = h;
            i++;
        } else if ((h & 0xC0) == 0x80) {
            uint32_t len = h & 0x0F;
            if (i + 1 + len > n) return -1;
            if (h & 0x20) {
                k->type = T_BYTES;
                k->b = p + i + 1;
                k->n = len;
            } else {
                if (len == 0 || len > 8 || (h & 0x10)) return -1;
                k->type = T_UINT;
                for (uint32_t j = 0; j < len; j++) k->v = (k->v << 8) | p[i + 1 + j];
            }
            i += 1 + len;
        } else if ((h & 0xE0) == 0xC0) {
            if (i + 2 > n) return -1;
            uint32_t len = ((h & 7) << 8) | p[i + 1];
            if (!(h & 0x10) || i + 2 + len > n) return -1; /* host sends only bytes here */
            if (len < 16) f->strict++;                     /* should have been a short atom */
            k->type = T_BYTES;
            k->b = p + i + 2;
            k->n = len;
            i += 2 + len;
        } else {
            return -1;
        }
        c++;
    }
    return i == n ? (int) c : -1;
}

static int is_ctl(const ft_tok_t *t, uint32_t c)
{
    return t->type == T_CTL && t->ctl == c;
}
static int is_uid(const ft_tok_t *t, const uint8_t u[8])
{
    return t->type == T_BYTES && t->n == 8 && memcmp(t->b, u, 8) == 0;
}

/* ---- responses ---------------------------------------------------------- */

typedef struct {
    uint8_t b[FT_MAX];
    uint32_t n;
} ft_out_t;

static void o1(ft_out_t *o, uint32_t v)
{
    o->b[o->n++] = (uint8_t) v;
}
static void ouint(ft_out_t *o, uint64_t v)
{
    if (v < 64) {
        o1(o, (uint32_t) v);
        return;
    }
    uint32_t n = 1;
    while (n < 8 && (v >> (8 * n))) n++;
    o1(o, 0x80 | n);
    for (uint32_t k = n; k > 0; k--) o1(o, (uint32_t) (v >> (8 * (k - 1))));
}
static void ouid(ft_out_t *o, const uint8_t u[8])
{
    o1(o, 0xA8);
    memcpy(o->b + o->n, u, 8);
    o->n += 8;
}
static void ostatus(ft_out_t *o, uint32_t st)
{
    o1(o, 0xF9);
    o1(o, 0xF0);
    ouint(o, st);
    o1(o, 0);
    o1(o, 0);
    o1(o, 0xF1);
}

static void respond(ftper_t *f, const ft_out_t *o, uint32_t tsn, uint32_t hsn)
{
    uint8_t *r = f->resp;
    memset(r, 0, FT_MAX);
    uint32_t pad = (o->n + 3) & ~3u;
    wr16(r + 4, f->comid);
    wr32(r + 16, 24 + 12 + pad);
    wr32(r + 20, tsn);
    wr32(r + 24, hsn);
    wr32(r + 40, 12 + pad);
    wr32(r + 52, o->n);
    memcpy(r + 56, o->b, o->n);
    f->have_resp = 1;
}

static uint32_t range_of(const ft_tok_t *t)
{
    static const uint8_t g[8] = {0, 0, 8, 2, 0, 0, 0, 1};
    if (t->type != T_BYTES || t->n != 8) return 0xFFFFFFFFu;
    if (memcmp(t->b, g, 8) == 0) return 0;
    if (memcmp(t->b, g, 4) == 0 && t->b[4] == 0 && t->b[5] == 3 && t->b[6] == 0 && t->b[7] >= 1)
        return t->b[7];
    return 0xFFFFFFFFu;
}

/* StartSession [ HSN SPID Write {0: challenge} {3: authority} ] */
static void do_start(ftper_t *f, const ft_tok_t *t, uint32_t end, ft_out_t *o)
{
    uint32_t st = 0;
    f->starts++;
    const uint8_t *chal = 0, *auth = 0;
    uint32_t chal_n = 0;
    if (end < 8 || !is_ctl(&t[3], 0xF0) || t[4].type != T_UINT || t[6].type != T_UINT ||
        !is_ctl(&t[end - 1], 0xF1)) {
        f->strict++;
        st = 0x0C;
    } else {
        f->hsn = (uint32_t) t[4].v;
        if (!is_uid(&t[5], LOCKINGSP)) st = 0x0C;
        f->write = t[6].v == 1;
        if ((end - 1 - 7) % 4) {
            f->strict++;
            st = 0x0C;
        }
        for (uint32_t i = 7; st == 0 && i + 4 <= end - 1; i += 4) {
            if (!is_ctl(&t[i], 0xF2) || t[i + 1].type != T_UINT || t[i + 2].type != T_BYTES ||
                !is_ctl(&t[i + 3], 0xF3)) {
                f->strict++;
                st = 0x0C;
                break;
            }
            if (t[i + 1].v == 0) {
                chal = t[i + 2].b;
                chal_n = t[i + 2].n;
            } else if (t[i + 1].v == 3) {
                auth = t[i + 2].b;
                if (t[i + 2].n != 8) st = 0x0C;
            }
        }
    }
    if (chal) {
        memcpy(f->last_challenge, chal, chal_n < 64 ? chal_n : 64);
        f->last_challenge_len = chal_n;
    }
    if (auth) memcpy(f->last_authority, auth, 8);
    int who = 0;
    if (st == 0 && auth) {
        const uint8_t *pin = 0;
        uint32_t pn = 0;
        if (memcmp(auth, ADMIN1, 8) == 0) {
            who = 1;
            pin = f->admin1;
            pn = f->admin1_len;
        } else if (memcmp(auth, USER1, 8) == 0 && f->user1_len) {
            who = 2;
            pin = f->user1;
            pn = f->user1_len;
        }
        if (!pin || !chal || chal_n != pn || memcmp(chal, pin, pn) != 0) {
            st = 0x01;
            f->auth_fail++;
        }
    }
    o1(o, 0xF8);
    ouid(o, SMUID);
    ouid(o, SYNCSESSION);
    o1(o, 0xF0);
    if (st == 0) {
        f->in_session = 1;
        f->auth = who;
        f->tsn = f->tsn_start++;
        ouint(o, f->hsn);
        ouint(o, f->tsn);
    }
    o1(o, 0xF1);
    ostatus(o, st);
}

/* Set [ {1: [ {col: val} ... ]} ] on a Locking range or MBRControl */
static void do_set(ftper_t *f, const ft_tok_t *t, uint32_t end, ft_out_t *o)
{
    uint32_t st = 0;
    f->sets++;
    uint32_t rg = range_of(&t[1]);
    int mbr = is_uid(&t[1], MBRCTL);
    if (end < 9 || !is_ctl(&t[3], 0xF0) || !is_ctl(&t[4], 0xF2) || t[5].type != T_UINT ||
        t[5].v != 1 || !is_ctl(&t[6], 0xF0) || !is_ctl(&t[end - 3], 0xF1) ||
        !is_ctl(&t[end - 2], 0xF3) || !is_ctl(&t[end - 1], 0xF1)) {
        f->strict++;
        st = 0x0C;
    } else if (!f->in_session || !f->write) {
        st = 0x01;
    } else if (mbr) {
        f->mbr_sets++;
        if (f->auth != 1 && !(f->auth == 2 && f->user_may_mbr)) st = 0x01;
    } else if (rg >= FT_RANGES) {
        st = 0x0C;
    } else if (f->auth == 0 || (f->auth == 2 && !(f->user_ranges & (1u << rg)))) {
        st = 0x01;
    }
    if (st == 0 && f->fail_status) st = (uint32_t) f->fail_status;
    /* check every pair first, then apply them all or none */
    for (uint32_t pass = 0; pass < 2 && st == 0; pass++) {
        for (uint32_t i = 7; i < end - 3; i += 4) {
            if (i + 4 > end - 3 || !is_ctl(&t[i], 0xF2) || t[i + 1].type != T_UINT ||
                t[i + 2].type != T_UINT || t[i + 2].v > 1 || !is_ctl(&t[i + 3], 0xF3)) {
                f->strict++;
                st = 0x0C;
                break;
            }
            uint64_t col = t[i + 1].v;
            int v = (int) t[i + 2].v;
            if (mbr) {
                if (col == 1) {
                    if (pass) f->mbr_enabled = v;
                } else if (col == 2) {
                    if (pass) f->mbr_done = v;
                } else {
                    st = 0x0C;
                }
            } else if (col == 7) {
                if (pass) f->rl[rg] = v;
            } else if (col == 8) {
                if (pass) f->wl[rg] = v;
            } else {
                st = 0x0C;
            }
        }
    }
    o1(o, 0xF0);
    o1(o, 0xF1);
    ostatus(o, st);
}

int ftper_send(void *ctx, uint8_t protocol, uint16_t sp, uint8_t *buf, uint32_t len)
{
    ftper_t *f = (ftper_t *) ctx;
    f->sends++;
    f->last_len = len;
    memcpy(f->last_cmd, buf, len < FT_MAX ? len : FT_MAX);
    if (f->fail_send) return -1;
    if (protocol != 1 || sp != f->comid || len < 56 || len > FT_MAX) {
        f->strict++;
        return -1;
    }
    /* framing: reserved zero, lengths consistent and padded */
    uint32_t cpl = rd32(buf + 16), pkl = rd32(buf + 40), sl = rd32(buf + 52);
    f->last_cp_len = cpl;
    if (rd32(buf) || rd16(buf + 4) != f->comid || rd16(buf + 6) || rd32(buf + 8) ||
        rd32(buf + 12) || rd32(buf + 28) || rd32(buf + 32) || rd32(buf + 36) || rd16(buf + 50))
        f->strict++;
    if (cpl + 20 > len || pkl + 24 != cpl || pkl % 4 || sl + 12 > pkl || pkl - 12 - sl >= 4) {
        f->strict++;
        return 0;
    }
    for (uint32_t i = 56 + sl; i < len; i++)
        if (buf[i]) {
            f->strict++; /* padding and the rest of the buffer are zero */
            break;
        }
    uint32_t tsn = rd32(buf + 20), hsn = rd32(buf + 24);
    ft_tok_t t[96];
    int n = read_tokens(f, buf + 56, sl, t, 96);
    ft_out_t o;
    o.n = 0;
    if (n <= 0) {
        f->strict++;
        return 0;
    }
    if (is_ctl(&t[0], 0xFA)) {
        if (n != 1 || !f->in_session || tsn != f->tsn || hsn != f->hsn) f->strict++;
        f->ends++;
        o1(&o, 0xFA);
        respond(f, &o, f->tsn, f->hsn);
        f->in_session = 0;
        f->auth = 0;
        return 0;
    }
    /* Call inv meth [ ... ] EndOfData [ 0 0 0 ] */
    if (n < 10 || !is_ctl(&t[0], 0xF8) || !is_ctl(&t[n - 6], 0xF9) || !is_ctl(&t[n - 5], 0xF0) ||
        t[n - 4].type != T_UINT || t[n - 4].v || t[n - 3].type != T_UINT || t[n - 3].v ||
        t[n - 2].type != T_UINT || t[n - 2].v || !is_ctl(&t[n - 1], 0xF1)) {
        f->strict++;
        return 0;
    }
    uint32_t end = (uint32_t) n - 6;
    if (is_uid(&t[1], SMUID) && is_uid(&t[2], STARTSESSION)) {
        if (tsn || hsn) f->strict++; /* session manager packets carry 0/0 */
        do_start(f, t, end, &o);
        respond(f, &o, 0, 0);
        return 0;
    }
    if (!f->in_session || tsn != f->tsn || hsn != f->hsn) f->strict++;
    if (is_uid(&t[2], SET)) {
        do_set(f, t, end, &o);
    } else {
        o1(&o, 0xF0);
        o1(&o, 0xF1);
        ostatus(&o, 0x0C);
    }
    respond(f, &o, f->tsn, f->hsn);
    return 0;
}

int ftper_recv(void *ctx, uint8_t protocol, uint16_t sp, uint8_t *buf, uint32_t len)
{
    ftper_t *f = (ftper_t *) ctx;
    f->recvs++;
    if (f->fail_recv) return -1;
    if (protocol != 1) return -1;
    memset(buf, 0, len);
    if (sp == 0x0001) {
        f->discs++;
        if (f->disc_raw)
            memcpy(buf, f->disc_raw, f->disc_raw_len < len ? f->disc_raw_len : len);
        else
            ftper_level0(f, buf, len);
        return 0;
    }
    if (sp != f->comid) {
        f->strict++;
        return -1;
    }
    if (len < 20) return -1;
    wr16(buf + 4, f->comid);
    if (f->silent || !f->have_resp) return 0; /* nothing: all zero lengths */
    if (f->busy) {
        f->busy--;
        wr32(buf + 8, 1); /* outstanding data, not ready */
        return 0;
    }
    memcpy(buf, f->resp, len < FT_MAX ? len : FT_MAX);
    f->have_resp = 0;
    f->responses++;
    if (f->mangle && (f->mangle_at == 0 || f->mangle_at == f->responses)) f->mangle(f, buf, len);
    return 0;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_pspace.c — pspace.* and hall.*: public spaces and the watch hall over
 * the node's Vinea spaces (see zxvd_pspace.h W1-W6). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "zxvd_groups.h"
#include "zxvd_pspace.h"
#include "vna_space.h"

#define E_PARAMS (-32602)

static zxv_net_t *NET;

void zxvd_pspace_bind(zxv_net_t *net)
{
    NET = net;
}

static const char *const KIND[] = {"?", "room", "hall"};
static const char *const GOV[] = {"?", "owner", "quorum"};
static const char *const PHASE[] = {"?", "searching", "joining", "in", "not_found"};
static const char *const STATE[] = {"stop", "pause", "play"};

#define OFF "the network is off"

/* ----- output ----- */

/* Text peers sent: escaped for JSON, invalid UTF-8 replaced by U+FFFD, so the
 * response always parses. */
static void put_text(zxvd_buf_t *out, const uint8_t *s, uint32_t n)
{
    zxvd_buf_raw(out, "\"", 1);
    for (uint32_t i = 0; i < n;) {
        uint8_t c = s[i];
        if (c < 0x80) {
            if (c == '"' || c == '\\') {
                char e[2] = {'\\', (char) c};
                zxvd_buf_raw(out, e, 2);
            } else if (c < 0x20 || c == 0x7f) {
                zxvd_buf_printf(out, "\\u%04x", c);
            } else {
                zxvd_buf_raw(out, (const char *) &s[i], 1);
            }
            i++;
            continue;
        }
        uint32_t len = c >= 0xf0 && c <= 0xf4 ? 4 : c >= 0xe0 ? 3 : c >= 0xc2 && c < 0xe0 ? 2 : 0;
        bool ok = len && i + len <= n;
        uint32_t cp = len == 2 ? c & 0x1fu : len == 3 ? c & 0x0fu : c & 0x07u;
        for (uint32_t k = 1; ok && k < len; k++) {
            ok = (s[i + k] & 0xc0) == 0x80;
            cp = cp << 6 | (s[i + k] & 0x3fu);
        }
        /* no overlong forms, surrogates or values past U+10FFFF */
        if (ok && ((len == 3 && (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))) ||
                   (len == 4 && (cp < 0x10000 || cp > 0x10ffff))))
            ok = false;
        if (ok) {
            zxvd_buf_raw(out, (const char *) &s[i], len);
            i += len;
        } else {
            zxvd_buf_raw(out, "\\ufffd", 6);
            i++;
        }
    }
    zxvd_buf_raw(out, "\"", 1);
}

static void put_cid(zxvd_buf_t *out, const uint8_t cid[VNA_CID_RAW_LEN])
{
    char t[VNA_CID_STR_MAX];
    if (vna_cid_to_string(cid, t, sizeof t) < 0) t[0] = 0;
    zxvd_buf_json_str(out, t);
}

static void put_id(zxvd_buf_t *out, const vna_id_t *id)
{
    static const char hex[] = "0123456789abcdef";
    char t[65];
    for (int i = 0; i < 32; i++) {
        t[2 * i] = hex[id->b[i] >> 4];
        t[2 * i + 1] = hex[id->b[i] & 15];
    }
    t[64] = 0;
    zxvd_buf_json_str(out, t);
}

static void put_addr(zxvd_buf_t *out, const uint8_t *a, uint32_t n)
{
    if (n == 6)
        zxvd_buf_printf(out, "\"%u.%u.%u.%u:%u\"", a[0], a[1], a[2], a[3],
                        (unsigned) (a[4] << 8 | a[5]));
    else
        zxvd_buf_raw(out, "\"\"", 2);
}

static void put_gen(zxvd_buf_t *out, const vna_space_genesis_t *g)
{
    zxvd_buf_raw(out, "\"name\":", 7);
    put_text(out, g->name, g->name_len);
    zxvd_buf_raw(out, ",\"topic\":", 9);
    put_text(out, g->topic, g->topic_len);
    zxvd_buf_printf(out, ",\"kind\":\"%s\",\"gov\":\"%s\",\"quorum_pct\":%u,\"created_ms\":%llu",
                    KIND[g->kind <= 2 ? g->kind : 0], GOV[g->gov <= 2 ? g->gov : 0],
                    (unsigned) g->quorum_pct, (unsigned long long) g->created);
    zxvd_buf_raw(out, ",\"creator\":", 11);
    put_id(out, &g->creator);
}

static void put_hall(zxvd_buf_t *out, const vna_space_t *s, uint64_t now)
{
    const vna_hall_t *h = &s->hall;
    zxvd_buf_printf(out, "{\"set\":%s,\"state\":\"%s\",\"media\":", s->hall_set ? "true" : "false",
                    STATE[s->hall_set && h->state <= 2 ? h->state : 0]);
    put_text(out, h->media, s->hall_set ? h->media_len : 0);
    zxvd_buf_printf(out,
                    ",\"pos_ms\":%llu,\"base_pos_ms\":%llu,\"at_ms\":%llu,\"rate_q16\":%u,"
                    "\"now_ms\":%llu,\"viewers\":%u,\"may_control\":%s",
                    (unsigned long long) (s->hall_set ? vna_space_hall_pos(h, now) : 0),
                    (unsigned long long) h->pos_ms, (unsigned long long) h->at_ms,
                    (unsigned) (s->hall_set ? h->rate_q16 : VNA_SPACE_Q16),
                    (unsigned long long) now, (unsigned) (s->nm + (s->phase == VNA_SPACE_IN)),
                    s->have_gen && vna_space_may_control(s, &zxv_net_identity(NET)->id) ? "true"
                                                                                        : "false");
    if (s->hall_set) {
        zxvd_buf_raw(out, ",\"author\":", 10);
        put_id(out, &s->hall_author);
    }
    zxvd_buf_raw(out, "}", 1);
}

/* ----- lookups ----- */

static const vna_spaces_t *spaces(void)
{
    return NET ? zxv_net_spaces(NET) : NULL;
}

static const char *why(int r)
{
    switch (r) {
    case VNA_ERR_DENIED:
        return "not allowed: not in this space, or only its owner may do that";
    case VNA_ERR_SPACE:
        return "too many spaces (leave one first)";
    case VNA_ERR_ARG:
        return "refused: check the CID, the name (1..48 bytes) and the topic ([a-z0-9-], "
               "at most 24)";
    default:
        return "refused by the node";
    }
}

/* the space a CID names; *err says why there is none */
static const vna_space_t *by_cid(const char *text, uint8_t cid[VNA_CID_RAW_LEN], const char **err)
{
    *err = NULL;
    if (!spaces()) {
        *err = OFF;
        return NULL;
    }
    if (vna_cid_from_string(text, (uint32_t) strlen(text), cid) != VNA_OK) {
        *err = "\"cid\" is not a space CID (base32 CIDv1, raw sha2-256)";
        return NULL;
    }
    const vna_space_t *s = vna_space_get(spaces(), cid);
    if (!s) *err = "not in that space (pspace.join first)";
    return s;
}

/* ----- methods ----- */

static const char *s_create(const char *kind, const char *gov, uint32_t pct, const char *name,
                            const char *topic, zxvd_buf_t *out)
{
    uint8_t cid[VNA_CID_RAW_LEN];
    if (!spaces()) return OFF;
    int r = zxv_net_space_create(NET, !strcmp(kind, "hall") ? VNA_SPACE_HALL : VNA_SPACE_ROOM,
                                 !strcmp(gov, "quorum") ? VNA_GOV_QUORUM : VNA_GOV_OWNER,
                                 (uint8_t) pct, name, topic, zxv_net_wall_ms(), cid);
    if (r != VNA_OK) return why(r);
    zxvd_buf_raw(out, "{\"cid\":", 7);
    put_cid(out, cid);
    zxvd_buf_raw(out, "}", 1);
    return NULL;
}

static bool find_reported = true; /* the last search's final answer was given */

static const char *s_find(const char *topic, zxvd_buf_t *out)
{
    const vna_spaces_t *sp = spaces();
    if (!sp) return OFF;
    size_t tl = strlen(topic);
    bool same = sp->find_topic_len == tl && !memcmp(sp->find_topic, topic, tl);
    if (!same || (vna_space_find_done(sp) && find_reported)) { /* W4: a new search */
        if (zxv_net_space_find(NET, topic, zxv_net_wall_ms()) != VNA_OK)
            return "\"topic\" must be 1..24 characters of [a-z0-9-]";
        find_reported = false;
    }
    bool done = vna_space_find_done(sp);
    zxvd_buf_raw(out, "{\"topic\":", 9);
    zxvd_buf_json_str(out, topic);
    zxvd_buf_printf(out, ",\"done\":%s,\"found\":[", done ? "true" : "false");
    for (uint32_t i = 0; i < sp->nfound && i < VNA_SPACE_FOUND_MAX; i++) {
        const vna_space_found_t *f = &sp->found[i];
        zxvd_buf_raw(out, i ? ",{\"cid\":" : "{\"cid\":", i ? 8 : 7);
        put_cid(out, f->cid);
        zxvd_buf_raw(out, ",", 1);
        put_gen(out, &f->gen);
        zxvd_buf_printf(out, ",\"members\":%u,\"host\":", (unsigned) f->members);
        put_id(out, &f->host);
        zxvd_buf_raw(out, "}", 1);
    }
    zxvd_buf_raw(out, "]}", 2);
    if (done) find_reported = true;
    return NULL;
}

static const char *s_join(const char *text, zxvd_buf_t *out)
{
    uint8_t cid[VNA_CID_RAW_LEN];
    const char *err;
    if (!spaces()) return OFF;
    if (vna_cid_from_string(text, (uint32_t) strlen(text), cid) != VNA_OK)
        return "\"cid\" is not a space CID (base32 CIDv1, raw sha2-256)";
    int r = zxv_net_space_join(NET, cid, zxv_net_wall_ms());
    if (r != VNA_OK) return why(r);
    const vna_space_t *s = by_cid(text, cid, &err);
    if (!s) return err;
    zxvd_buf_raw(out, "{\"cid\":", 7);
    put_cid(out, cid);
    zxvd_buf_printf(out, ",\"phase\":\"%s\"}", PHASE[s->phase <= 4 ? s->phase : 0]);
    return NULL;
}

static const char *s_leave(const char *text, zxvd_buf_t *out)
{
    uint8_t cid[VNA_CID_RAW_LEN];
    const char *err;
    if (!by_cid(text, cid, &err)) return err;
    if (zxv_net_space_leave(NET, cid, zxv_net_wall_ms()) != VNA_OK) return "refused by the node";
    zxvd_buf_raw(out, "{\"cid\":", 7);
    put_cid(out, cid);
    zxvd_buf_raw(out, ",\"left\":true}", 13);
    return NULL;
}

static const char *s_post(const char *text, const char *msg, zxvd_buf_t *out)
{
    uint8_t cid[VNA_CID_RAW_LEN];
    const char *err;
    if (!by_cid(text, cid, &err)) return err;
    int r = zxv_net_space_post(NET, cid, (const uint8_t *) msg, (uint32_t) strlen(msg),
                               zxv_net_wall_ms());
    if (r != VNA_OK) return r == VNA_ERR_DENIED ? "not in that space yet (still joining)" : why(r);
    zxvd_buf_raw(out, "{\"cid\":", 7);
    put_cid(out, cid);
    zxvd_buf_raw(out, ",\"posted\":true}", 15);
    return NULL;
}

static const char *s_events(const char *text, uint64_t since, zxvd_buf_t *out)
{
    uint8_t cid[VNA_CID_RAW_LEN];
    const char *err;
    const vna_space_t *s = by_cid(text, cid, &err);
    if (!s) return err;
    const vna_space_logent_t *ev[VNA_SPACE_LOG];
    uint32_t n = vna_space_events(s, since, ev, VNA_SPACE_LOG);
    uint64_t next = since;
    zxvd_buf_raw(out, "{\"events\":[", 11);
    for (uint32_t i = 0; i < n; i++) {
        const vna_space_logent_t *e = ev[i];
        zxvd_buf_printf(out,
                        "%s{\"index\":%llu,\"kind\":\"%s\",\"lamport\":%llu,\"created_ms\":%llu,"
                        "\"author\":",
                        i ? "," : "", (unsigned long long) e->index,
                        e->kind == VNA_SPACE_EV_HALL ? "hall" : "post",
                        (unsigned long long) e->lamport, (unsigned long long) e->created);
        put_id(out, &e->author);
        if (e->kind == VNA_SPACE_EV_POST) {
            zxvd_buf_raw(out, ",\"text\":", 8);
            put_text(out, e->data, e->len);
        } else {
            vna_hall_t h;
            if (vna_schema_unpack(&vna_hall_schema, e->data, e->len, &h, 0) >= 0)
                zxvd_buf_printf(out, ",\"state\":\"%s\",\"pos_ms\":%llu,\"rate_q16\":%u",
                                STATE[h.state <= 2 ? h.state : 0], (unsigned long long) h.pos_ms,
                                (unsigned) h.rate_q16);
        }
        zxvd_buf_raw(out, "}", 1);
        if (e->index > next) next = e->index;
    }
    zxvd_buf_printf(out, "],\"next\":%llu}", (unsigned long long) next);
    return NULL;
}

static const char *s_members(const char *text, zxvd_buf_t *out)
{
    uint8_t cid[VNA_CID_RAW_LEN];
    const char *err;
    const vna_space_t *s = by_cid(text, cid, &err);
    if (!s) return err;
    zxvd_buf_raw(out, "{\"members\":[", 12);
    for (uint32_t i = 0; i < s->nm; i++) {
        zxvd_buf_raw(out, i ? ",{\"id\":" : "{\"id\":", i ? 7 : 6);
        put_id(out, &s->m[i].id);
        zxvd_buf_raw(out, ",\"addr\":", 8);
        put_addr(out, s->m[i].addr, s->m[i].addr_len);
        zxvd_buf_printf(out, ",\"last_seen_ms\":%llu}", (unsigned long long) s->m[i].last_seen);
    }
    /* count: this node too */
    zxvd_buf_printf(out, "],\"count\":%u}", (unsigned) (s->nm + (s->phase == VNA_SPACE_IN)));
    return NULL;
}

static void s_list(zxvd_buf_t *out)
{
    const vna_spaces_t *sp = spaces();
    uint64_t now = zxv_net_wall_ms();
    bool first = true;
    zxvd_buf_printf(out, "{\"online\":%s,\"spaces\":[", sp ? "true" : "false");
    for (uint32_t i = 0; sp && i < VNA_SPACE_MAX; i++) {
        const vna_space_t *s = &sp->s[i];
        if (!s->used) continue;
        zxvd_buf_raw(out, first ? "{\"cid\":" : ",{\"cid\":", first ? 7 : 8);
        first = false;
        put_cid(out, s->cid);
        zxvd_buf_printf(out, ",\"phase\":\"%s\"", PHASE[s->phase <= 4 ? s->phase : 0]);
        if (s->have_gen) {
            zxvd_buf_raw(out, ",", 1);
            put_gen(out, &s->gen);
            if (s->gen.kind == VNA_SPACE_HALL) {
                zxvd_buf_raw(out, ",\"hall\":", 8);
                put_hall(out, s, now);
            }
        }
        zxvd_buf_printf(out, ",\"members\":%u}", (unsigned) (s->nm + (s->phase == VNA_SPACE_IN)));
    }
    zxvd_buf_raw(out, "]}", 2);
}

static const char *s_hall(const char *text, const char *action, const char *media, bool has_pos,
                          uint64_t pos_ms, uint32_t rate_q16, zxvd_buf_t *out)
{
    uint8_t cid[VNA_CID_RAW_LEN];
    const char *err;
    const vna_space_t *s = by_cid(text, cid, &err);
    if (!s) return err;
    if (s->phase != VNA_SPACE_IN) return "not in that space yet (still joining)";
    if (s->gen.kind != VNA_SPACE_HALL) return "that space is a room, not a watch hall";
    uint64_t now = zxv_net_wall_ms();
    const vna_hall_t *h = &s->hall;
    uint8_t state;
    const uint8_t *m = s->hall_set ? h->media : NULL;
    uint32_t ml = s->hall_set ? h->media_len : 0;
    uint64_t pos = has_pos ? pos_ms : s->hall_set ? vna_space_hall_pos(h, now) : 0;
    uint32_t rate = s->hall_set ? h->rate_q16 : VNA_SPACE_Q16;
    if (!strcmp(action, "load")) {
        state = VNA_HALL_STOP;
        m = (const uint8_t *) media;
        ml = (uint32_t) strlen(media);
        if (ml > VNA_SPACE_MEDIA_MAX) return "\"media\" must be at most 64 bytes";
        pos = has_pos ? pos_ms : 0;
        rate = VNA_SPACE_Q16;
    } else if (!strcmp(action, "play")) {
        state = VNA_HALL_PLAY;
        rate = rate_q16;
    } else if (!strcmp(action, "pause")) {
        state = VNA_HALL_PAUSE;
    } else { /* seek */
        state = s->hall_set ? h->state : VNA_HALL_STOP;
    }
    if (!ml && state != VNA_HALL_STOP) return "nothing loaded (hall.load first)";
    int r = zxv_net_hall_set(NET, cid, state, m, ml, pos, rate, now);
    if (r == VNA_ERR_DENIED) return "only the hall's owner controls playback";
    if (r != VNA_OK) return why(r);
    put_hall(out, s, now);
    return NULL;
}

static const char *s_hall_status(const char *text, zxvd_buf_t *out)
{
    uint8_t cid[VNA_CID_RAW_LEN];
    const char *err;
    const vna_space_t *s = by_cid(text, cid, &err);
    if (!s) return err;
    if (s->have_gen && s->gen.kind != VNA_SPACE_HALL)
        return "that space is a room, not a watch hall";
    put_hall(out, s, zxv_net_wall_ms());
    return NULL;
}

/* ----- the methods: parameters first, then the node ----- */

static void bad(zxvd_call_t *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void bad(zxvd_call_t *c, const char *fmt, ...)
{
    char m[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m, sizeof m, fmt, ap);
    va_end(ap);
    zxvd_fail(c, E_PARAMS, "%s", m);
}

/* a non-negative 64-bit integer (a JSON integer or a decimal string) */
static bool p_u64(zxvd_call_t *c, const char *key, uint64_t *v)
{
    int64_t x;
    if (!zxvd_p_amount(c, key, &x)) return false;
    if (x < 0) {
        bad(c, "\"%s\" must not be negative", key);
        return false;
    }
    *v = (uint64_t) x;
    return true;
}

static bool done(zxvd_call_t *c, const char *why)
{
    if (!why) return true;
    zxvd_res(c)->len = 0;
    zxvd_fail(c, ZXVD_E_PSPACE, "%s", why);
    return false;
}

#define SPACE_CID_MAX  80u
#define SPACE_TEXT_MAX 1025u /* vna_space.h VNA_SPACE_TEXT_MAX + NUL */

static void m_space_create(zxvd_call_t *c)
{
    char kind[8], gov[8], name[49], topic[25];
    uint32_t pct = 51;
    if (!zxvd_p_str(c, "kind", kind, sizeof kind, true) ||
        !zxvd_p_str(c, "gov", gov, sizeof gov, true) ||
        !zxvd_p_str(c, "name", name, sizeof name, true) ||
        !zxvd_p_str(c, "topic", topic, sizeof topic, false))
        return;
    if (zxvd_p_has(c, "quorum_pct") && !zxvd_p_u32(c, "quorum_pct", &pct)) return;
    if (strcmp(kind, "room") != 0 && strcmp(kind, "hall") != 0) {
        bad(c, "\"kind\" must be \"room\" or \"hall\"");
        return;
    }
    if (strcmp(gov, "owner") != 0 && strcmp(gov, "quorum") != 0) {
        bad(c, "\"gov\" must be \"owner\" or \"quorum\"");
        return;
    }
    if (pct < 1 || pct > 100) {
        bad(c, "\"quorum_pct\" must be 1..100");
        return;
    }
    (void) done(c, s_create(kind, gov, pct, name, topic, zxvd_res(c)));
}

static void m_space_find(zxvd_call_t *c)
{
    char topic[25];
    if (!zxvd_p_str(c, "topic", topic, sizeof topic, true)) return;
    (void) done(c, s_find(topic, zxvd_res(c)));
}

/* the methods that take only a CID */
static void space_cid_call(zxvd_call_t *c, const char *(*fn)(const char *, zxvd_buf_t *) )
{
    char cid[SPACE_CID_MAX];
    if (!zxvd_p_str(c, "cid", cid, sizeof cid, true)) return;
    (void) done(c, fn(cid, zxvd_res(c)));
}

static void m_space_join(zxvd_call_t *c)
{
    space_cid_call(c, s_join);
}

static void m_space_leave(zxvd_call_t *c)
{
    space_cid_call(c, s_leave);
}

static void m_space_members(zxvd_call_t *c)
{
    space_cid_call(c, s_members);
}

static void m_hall_status(zxvd_call_t *c)
{
    space_cid_call(c, s_hall_status);
}

static void m_space_post(zxvd_call_t *c)
{
    static char text[SPACE_TEXT_MAX];
    char cid[SPACE_CID_MAX];
    if (!zxvd_p_str(c, "cid", cid, sizeof cid, true) ||
        !zxvd_p_str(c, "text", text, sizeof text, true))
        return;
    if (!text[0]) {
        bad(c, "\"text\" must not be empty");
        return;
    }
    (void) done(c, s_post(cid, text, zxvd_res(c)));
}

static void m_space_events(zxvd_call_t *c)
{
    char cid[SPACE_CID_MAX];
    uint64_t since = 0;
    if (!zxvd_p_str(c, "cid", cid, sizeof cid, true)) return;
    if (zxvd_p_has(c, "since") && !p_u64(c, "since", &since)) return;
    (void) done(c, s_events(cid, since, zxvd_res(c)));
}

static void m_space_list(zxvd_call_t *c)
{
    s_list(zxvd_res(c));
}

static void hall_call(zxvd_call_t *c, const char *action)
{
    char cid[SPACE_CID_MAX], media[SPACE_CID_MAX];
    uint64_t pos = 0;
    uint32_t rate = 65536u; /* Q16: normal speed */
    bool load = !strcmp(action, "load"), seek = !strcmp(action, "seek");
    bool has_pos = zxvd_p_has(c, "pos_ms");
    if (!zxvd_p_str(c, "cid", cid, sizeof cid, true) ||
        !zxvd_p_str(c, "media", media, sizeof media, load))
        return;
    if (!load && media[0]) {
        bad(c, "\"media\" is only for hall.load");
        return;
    }
    if (seek && !has_pos) {
        bad(c, "missing integer parameter \"pos_ms\"");
        return;
    }
    if (has_pos && !p_u64(c, "pos_ms", &pos)) return;
    if (zxvd_p_has(c, "rate_q16")) {
        if (strcmp(action, "play") != 0) {
            bad(c, "\"rate_q16\" is only for hall.play");
            return;
        }
        if (!zxvd_p_u32(c, "rate_q16", &rate)) return;
        if (rate < 1u || rate > 4u * 65536u) {
            bad(c, "\"rate_q16\" must be 1..262144 (65536 = normal speed)");
            return;
        }
    }
    (void) done(c, s_hall(cid, action, media, has_pos, pos, rate, zxvd_res(c)));
}

static void m_hall_load(zxvd_call_t *c)
{
    hall_call(c, "load");
}

static void m_hall_play(zxvd_call_t *c)
{
    hall_call(c, "play");
}

static void m_hall_pause(zxvd_call_t *c)
{
    hall_call(c, "pause");
}

static void m_hall_seek(zxvd_call_t *c)
{
    hall_call(c, "seek");
}

static const zxvd_group_method_t METHODS[] = {
    {"pspace.create", m_space_create, false},  {"pspace.find", m_space_find, false},
    {"pspace.join", m_space_join, false},      {"pspace.leave", m_space_leave, true},
    {"pspace.post", m_space_post, false},      {"pspace.events", m_space_events, true},
    {"pspace.members", m_space_members, true}, {"pspace.list", m_space_list, true},
    {"hall.load", m_hall_load, false},         {"hall.play", m_hall_play, false},
    {"hall.pause", m_hall_pause, false},       {"hall.seek", m_hall_seek, false},
    {"hall.status", m_hall_status, true},
};

static const zxvd_group_t GROUP = {"pspace",
                                   false,
                                   "public spaces on the peer network and the synced watch hall",
                                   METHODS,
                                   sizeof METHODS / sizeof METHODS[0],
                                   NULL};

const zxvd_group_t *zxvd_pspace_group(void)
{
    return &GROUP;
}

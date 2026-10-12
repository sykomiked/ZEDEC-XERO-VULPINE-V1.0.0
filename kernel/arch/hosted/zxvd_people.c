/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_people.c — the people features inside zxvd (see zxvd_people.h). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "zxvd_people.h"
#include "sha256.h"
#include "web4_web2.h"
#include "web4_util.h"
#include "social_post.h"
#include "social_store.h"
#include "social_feed.h"
#include "social_group.h"
#include "social_sign_mldsa.h"
#include "call_session.h"
#include "xlate.h"
#include "xlate_langid.h"
#include "devmesh.h"

#define E_PARAMS  (-32602)
#define E_METHOD  (-32601)
#define E_PEOPLE  ZXVD_E_PEOPLE
#define E_NOMODEL ZXVD_E_NO_MODEL

#define IDX_CAP     (4u * ZXVD_PEOPLE_RECORDS)
#define NLINES      (ZXVD_PEOPLE_LINES + 1u) /* the last one is the loopback line */
#define LOOP        ZXVD_PEOPLE_LINES
#define CALL_OUT    32u /* call messages waiting for call.signal */
#define LOOPQ       32u
#define MESH_OUT    16u /* mesh frames waiting for the carrier */
#define EVENTS      16u
#define GROUPS_MAX  64u /* sg_member_t entries per derived group */
#define SIGNAL_MAX  16u /* messages in one call.signal */
#define LOG_LINE    32768u
#define LOG_MAX     (256u << 20)
#define SEED_BYTES  96u
#define MESH_MAGIC  "ZXVDMESH1"
#define TOKS        256

/* ===================== state ===================== */

typedef struct {
    uint32_t len;
    uint8_t b[CALL_SIG_MAX];
} cmsg_t;

typedef struct {
    uint8_t to[DM_ID_BYTES];
    uint32_t len;
    uint8_t *b;
} frame_t;

typedef struct {
    struct zxvd_people *p;
    uint32_t idx;
    call_session_t s;
    uint32_t missed;
} line_t;

typedef struct {
    uint8_t kind;
    uint8_t peer[DM_ID_BYTES];
    uint32_t sas;
    int32_t status;
    uint64_t at;
} ev_t;

struct zxvd_people {
    zxvd_people_host_t host;
    /* H2 */
    char path[1024];
    int fd;
    uint8_t head[32];
    uint64_t recs;
    bool replay, broken;
    char mesh_path[1040];
    /* H1 */
    bool have_seed;
    uint8_t seed[SEED_BYTES];
    uint8_t pk[SP_PK_LEN], sk[SP_MLDSA_SK_LEN], me[SP_NODEID_LEN];
    /* H4 */
    sp_record_t *slots, *tmp;
    uint32_t *index;
    ss_store_t store;
    sp_clock_t clock;
    const sp_record_t **cand, **sorted;
    sg_member_t *members;
    sf_work_t *work;
    sf_pick_t *picks;
    /* H8 */
    bool simple;
    /* H5 */
    uint32_t self_id, loop_id;
    line_t line[NLINES];
    cmsg_t loopq[LOOPQ];
    uint32_t loopq_to[LOOPQ];
    uint32_t loopq_n;
    cmsg_t out[CALL_OUT];
    uint32_t out_n;
    uint64_t out_dropped;
    /* H6 */
    xlate_t *xl;
    int gen_rc;
    char gen_why[200];
    /* H7 */
    dm_mesh_t *mesh;
    frame_t mout[MESH_OUT];
    uint32_t mout_n;
    uint64_t mout_dropped;
    bool mesh_dirty;
    char mesh_why[160];
    ev_t ev[EVENTS];
    uint32_t ev_n;
    bool sas_on;
    uint8_t sas_peer[DM_ID_BYTES];
    uint32_t sas;
    uint64_t now_fixed;
};

/* ===================== small helpers ===================== */

static uint64_t now_ms(const zxvd_people_t *p)
{
    if (p->now_fixed) return p->now_fixed;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
}

static bool os_random(uint8_t *out, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return false;
    size_t got = fread(out, 1, n, f);
    fclose(f);
    return got == n;
}

static void hex_of(const uint8_t *b, uint32_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n; i++) {
        out[2 * i] = H[b[i] >> 4];
        out[2 * i + 1] = H[b[i] & 15];
    }
    out[2 * n] = 0;
}

static void put_hex(zxvd_buf_t *o, const uint8_t *b, uint32_t n)
{
    char h[2 * 64 + 1];
    if (n > 64) n = 64;
    hex_of(b, n, h);
    zxvd_buf_printf(o, "\"%s\"", h);
}

/* strict UTF-8: no overlongs, no surrogates, nothing above U+10FFFF */
static bool utf8_ok(const uint8_t *s, uint32_t n)
{
    uint32_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        uint32_t k, cp;
        if (c < 0x80) {
            if (c == 0) return false;
            i++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            k = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            k = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            k = 3;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (n - i <= k) return false; /* the sequence is cut short */
        for (uint32_t j = 1; j <= k; j++) {
            if ((s[i + j] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (s[i + j] & 0x3Fu);
        }
        if ((k == 1 && cp < 0x80) || (k == 2 && cp < 0x800) || (k == 3 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += k + 1;
    }
    return true;
}

/* a JSON string from bytes (already UTF-8 checked) */
static void put_text(zxvd_buf_t *o, const uint8_t *b, uint32_t n)
{
    char *t = malloc((size_t) n + 1);
    if (!t) {
        o->oom = true;
        return;
    }
    memcpy(t, b, n);
    t[n] = 0;
    zxvd_buf_json_str(o, t);
    free(t);
}

/* ===================== the request ===================== */

typedef struct {
    zxvd_people_t *p;
    const char *js;
    w4_jtok_t t[TOKS];
    int32_t n;
    zxvd_buf_t *out;
    int code;
    char *msg;
    size_t cap;
} rq_t;

#if defined(__GNUC__)
__attribute__((format(printf, 3, 4)))
#endif
static void
fail(rq_t *r, int code, const char *fmt, ...)
{
    if (r->code) return;
    r->code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->msg, r->cap, fmt, ap);
    va_end(ap);
}

static int32_t param(rq_t *r, const char *key)
{
    return w4_json_get(r->js, r->t, r->n, 0, key);
}

static bool has(rq_t *r, const char *key)
{
    return param(r, key) >= 0;
}

/* a string of 0..cap-1 bytes; *len its length */
static bool p_str(rq_t *r, const char *key, char *out, uint32_t cap, bool required, uint32_t *len)
{
    int32_t i = param(r, key);
    out[0] = 0;
    if (len) *len = 0;
    if (i == W4_ERR_PARSE) {
        fail(r, E_PARAMS, "\"%s\" is given twice", key);
        return false;
    }
    if (i < 0) {
        if (required) fail(r, E_PARAMS, "missing string parameter \"%s\"", key);
        return !required;
    }
    int32_t got = r->t[i].type == W4_J_STR ? w4_json_str(r->js, &r->t[i], out, cap) : -1;
    if (got < 0) {
        fail(r, E_PARAMS, "\"%s\" must be a string of at most %u bytes", key, cap - 1);
        return false;
    }
    if ((size_t) got != strlen(out)) {
        fail(r, E_PARAMS, "\"%s\" must not contain NUL", key);
        return false;
    }
    if (len) *len = (uint32_t) got;
    return true;
}

static bool p_bool(rq_t *r, const char *key, bool def, bool *v)
{
    int32_t i = param(r, key);
    *v = def;
    if (i == W4_ERR_PARSE) {
        fail(r, E_PARAMS, "\"%s\" is given twice", key);
        return false;
    }
    if (i < 0) return true;
    if (w4_json_bool(&r->t[i], v) != W4_OK) {
        fail(r, E_PARAMS, "\"%s\" must be true or false", key);
        return false;
    }
    return true;
}

/* an integer lo..hi; absent = def (or required when def < 0) */
static bool p_int(rq_t *r, const char *key, int64_t lo, int64_t hi, int64_t def, int64_t *v)
{
    int32_t i = param(r, key);
    *v = def;
    if (i == W4_ERR_PARSE) {
        fail(r, E_PARAMS, "\"%s\" is given twice", key);
        return false;
    }
    if (i < 0) {
        if (def < 0) fail(r, E_PARAMS, "missing integer parameter \"%s\"", key);
        return def >= 0;
    }
    if (r->t[i].type != W4_J_NUM || w4_json_i64(r->js, &r->t[i], v) != W4_OK) {
        fail(r, E_PARAMS, "\"%s\" must be an integer (no fraction or exponent)", key);
        return false;
    }
    if (*v < lo || *v > hi) {
        fail(r, E_PARAMS, "\"%s\" must be from %lld to %lld", key, (long long) lo, (long long) hi);
        return false;
    }
    return true;
}

/* exactly n bytes as 2n hex digits */
static bool p_hex(rq_t *r, const char *key, uint8_t *out, uint32_t n, bool required)
{
    char h[2 * 64 + 2];
    uint32_t len;
    if (!p_str(r, key, h, sizeof h, required, &len)) return false;
    if (!has(r, key)) return true;
    if (len != 2u * n || w4_hex_decode(h, len, out, n) != (int32_t) n) {
        fail(r, E_PARAMS, "\"%s\" must be %u hex digits", key, 2u * n);
        return false;
    }
    return true;
}

/* ===================== H2: the people file ===================== */

static void chain(const uint8_t prev[32], const char *js, size_t n, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, prev, 32);
    sha256_update(&c, (const uint8_t *) js, n);
    sha256_final(&c, out);
}

static bool write_all(int fd, const char *b, size_t n)
{
    while (n) {
        ssize_t k = write(fd, b, n);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        b += k;
        n -= (size_t) k;
    }
    return true;
}

/* Append one record before the change applies. true when saved (or in
 * memory only, or replaying). */
static bool log_put(zxvd_people_t *p, rq_t *r, const char *js, size_t n)
{
    if (p->replay) return true;
    if (p->broken) {
        if (r) fail(r, E_PEOPLE, "the people file could not be written earlier; restart zxvd");
        return false;
    }
    uint8_t h[32];
    chain(p->head, js, n, h);
    if (p->fd < 0) {
        memcpy(p->head, h, 32);
        p->recs++;
        return true;
    }
    char hx[65];
    hex_of(h, 32, hx);
    char *line = malloc(n + 67);
    if (!line) {
        if (r) fail(r, E_PEOPLE, "out of memory");
        return false;
    }
    memcpy(line, hx, 64);
    line[64] = ' ';
    memcpy(line + 65, js, n);
    line[65 + n] = '\n';
    off_t end = lseek(p->fd, 0, SEEK_END);
    bool ok = end >= 0 && write_all(p->fd, line, n + 66) && fsync(p->fd) == 0;
    free(line);
    if (!ok) {
        if (end >= 0 && ftruncate(p->fd, end) != 0) { /* keep the file whole if we can */
        }
        p->broken = true;
        if (r) fail(r, E_PEOPLE, "the people file could not be written: %s", strerror(errno));
        return false;
    }
    memcpy(p->head, h, 32);
    p->recs++;
    return true;
}

static bool log_buf(zxvd_people_t *p, rq_t *r, zxvd_buf_t *b)
{
    if (b->oom) {
        if (r) fail(r, E_PEOPLE, "out of memory");
        free(b->p);
        return false;
    }
    bool ok = log_put(p, r, b->p, b->len);
    free(b->p);
    return ok;
}

/* ===================== H1: keys ===================== */

static const uint8_t *lookup_pk(void *ctx, const uint8_t author[SP_NODEID_LEN])
{
    zxvd_people_t *p = ctx;
    return memcmp(author, p->me, SP_NODEID_LEN) ? NULL : p->pk;
}

static sp_verifier_t verifier(zxvd_people_t *p)
{
    sp_verifier_t v = {sp_mldsa_verify, NULL, lookup_pk, p};
    return v;
}

static void line_init(zxvd_people_t *p, uint32_t i);

static void take_seed(zxvd_people_t *p, const uint8_t seed[SEED_BYTES])
{
    memcpy(p->seed, seed, SEED_BYTES);
    p->have_seed = true;
    sp_mldsa_keypair(p->seed, p->pk, p->sk, p->me);
    uint32_t id = ((uint32_t) p->me[0] << 24) | ((uint32_t) p->me[1] << 16) |
                  ((uint32_t) p->me[2] << 8) | p->me[3];
    p->self_id = id ? id : 1u;
    p->loop_id = p->self_id ^ 0x5A5A5A5Au;
    if (!p->loop_id || p->loop_id == p->self_id) p->loop_id = p->self_id + 1u;
    for (uint32_t i = 0; i < NLINES; i++) line_init(p, i);
}

/* ===================== H4: records ===================== */

static bool text_kind(uint8_t k)
{
    return k == SP_KIND_POST || k == SP_KIND_REPLY || k == SP_KIND_REACTION;
}

static bool shown_kind(uint8_t k)
{
    return text_kind(k) || (k >= SP_KIND_GROUP_CREATE && k <= SP_KIND_GROUP_CHARTER);
}

/* Write {"op":"rec"} and hold the record. */
static bool keep_record(zxvd_people_t *p, rq_t *r, const sp_record_t *rec)
{
    static uint8_t enc[SP_ENC_MAX];
    static char hx[2 * SP_ENC_MAX + 1];
    int32_t n = sp_encode(rec, enc, sizeof enc);
    if (n < 0) {
        if (r) fail(r, E_PEOPLE, "the record could not be encoded (%d)", (int) n);
        return false;
    }
    hex_of(enc, (uint32_t) n, hx);
    zxvd_buf_t b = {0};
    zxvd_buf_printf(&b, "{\"op\":\"rec\",\"hex\":\"%s\"}", hx);
    if (!log_buf(p, r, &b)) return false;
    int32_t st = ss_put(&p->store, rec);
    if (st < 0) {
        if (r) fail(r, E_PEOPLE, "the store refused the record (%d)", (int) st);
        return false;
    }
    sp_clock_observe(&p->clock, rec->lamport);
    return true;
}

/* Sign a record built in p->tmp and keep it. */
static bool sign_keep(zxvd_people_t *p, rq_t *r)
{
    uint8_t rnd[32];
    if (!os_random(rnd, sizeof rnd)) {
        fail(r, E_PEOPLE, "no random bytes from the OS");
        return false;
    }
    sp_mldsa_key_t key = {p->sk, rnd};
    sp_signer_t s = {sp_mldsa_sign, &key};
    int32_t st = sp_sign(p->tmp, &s);
    memset(rnd, 0, sizeof rnd);
    if (st != SP_OK) {
        fail(r, E_PEOPLE, "the record could not be signed (%d)", (int) st);
        return false;
    }
    return keep_record(p, r, p->tmp);
}

static void new_record(zxvd_people_t *p, sp_kind_t kind, sp_vis_t vis)
{
    sp_init(p->tmp, kind, vis, p->me, sp_clock_next(&p->clock));
    sp_set_wall(p->tmp, (uint64_t) time(NULL) * 1000u);
}

/* derive one group; returns the entries */
static uint32_t derive(zxvd_people_t *p, const uint8_t gid[SP_ID_LEN], sg_group_t *g)
{
    return sg_derive_store(&p->store, gid, p->sorted, ZXVD_PEOPLE_RECORDS, p->members, GROUPS_MAX,
                           g);
}

static uint8_t my_role(zxvd_people_t *p, uint32_t n, uint8_t *invited)
{
    const sg_member_t *m = sg_find(p->members, n, p->me);
    if (invited) *invited = m ? m->invited : SG_ROLE_NONE;
    return m ? m->role : SG_ROLE_NONE;
}

static const char *ROLE[] = {"none", "observer", "member", "admin", "owner"};

/* ===================== social.* ===================== */

static bool member_of(zxvd_people_t *p, const uint8_t gid[SP_ID_LEN])
{
    sg_group_t g;
    uint32_t n = derive(p, gid, &g);
    return g.exists && my_role(p, n, NULL) >= SG_ROLE_OBSERVER;
}

static void m_post(rq_t *r)
{
    zxvd_people_t *p = r->p;
    static char text[SP_BODY_MAX + 1];
    uint8_t parent[SP_ID_LEN], gid[SP_ID_LEN];
    uint32_t len;
    if (!p_str(r, "text", text, sizeof text, true, &len) ||
        !p_hex(r, "reply_to", parent, SP_ID_LEN, false) || !p_hex(r, "group", gid, SP_ID_LEN, false))
        return;
    if (!len) return fail(r, E_PARAMS, "\"text\" must not be empty");
    if (!utf8_ok((const uint8_t *) text, len)) return fail(r, E_PARAMS, "\"text\" is not UTF-8");
    bool reply = has(r, "reply_to"), grp = has(r, "group");
    if (reply) {
        const sp_record_t *pr = ss_get(&p->store, parent);
        if (!pr || (pr->kind != SP_KIND_POST && pr->kind != SP_KIND_REPLY))
            return fail(r, E_PEOPLE, "\"reply_to\" is not a post held here");
        if (pr->vis == SP_VIS_GROUP) {
            if (grp && memcmp(gid, pr->audience, SP_ID_LEN))
                return fail(r, E_PARAMS, "a reply goes to the group of the post it answers");
            memcpy(gid, pr->audience, SP_ID_LEN);
            grp = true;
        } else if (grp) {
            return fail(r, E_PARAMS, "a reply to a public post is public");
        }
    }
    if (grp && !member_of(p, gid)) return fail(r, E_PEOPLE, "you are not a member of that group");
    new_record(p, reply ? SP_KIND_REPLY : SP_KIND_POST, grp ? SP_VIS_GROUP : SP_VIS_PUBLIC);
    sp_set_body(p->tmp, (const uint8_t *) text, len);
    if (reply) sp_set_parent(p->tmp, parent);
    if (grp) sp_set_audience(p->tmp, gid);
    if (!sign_keep(p, r)) return;
    zxvd_buf_raw(r->out, "{\"id\":", 6);
    put_hex(r->out, p->tmp->id, SP_ID_LEN);
    zxvd_buf_printf(r->out, ",\"lamport\":\"%llu\",\"kind\":\"%s\"}",
                    (unsigned long long) p->tmp->lamport, reply ? "reply" : "post");
}

static void m_react(rq_t *r)
{
    zxvd_people_t *p = r->p;
    char re[SP_REACT_MAX + 1];
    uint8_t parent[SP_ID_LEN];
    uint32_t len;
    if (!p_hex(r, "post", parent, SP_ID_LEN, true) ||
        !p_str(r, "reaction", re, sizeof re, true, &len))
        return;
    if (!len || !utf8_ok((const uint8_t *) re, len))
        return fail(r, E_PARAMS, "\"reaction\" must be 1 to 32 bytes of UTF-8");
    const sp_record_t *pr = ss_get(&p->store, parent);
    if (!pr || (pr->kind != SP_KIND_POST && pr->kind != SP_KIND_REPLY))
        return fail(r, E_PEOPLE, "\"post\" is not a post held here");
    if (pr->vis == SP_VIS_GROUP && !member_of(p, pr->audience))
        return fail(r, E_PEOPLE, "you are not a member of that post's group");
    uint8_t aud[SP_ID_LEN];
    bool grp = pr->vis == SP_VIS_GROUP;
    memcpy(aud, pr->audience, SP_ID_LEN);
    new_record(p, SP_KIND_REACTION, grp ? SP_VIS_GROUP : SP_VIS_PUBLIC);
    sp_set_body(p->tmp, (const uint8_t *) re, len);
    sp_set_parent(p->tmp, parent);
    if (grp) sp_set_audience(p->tmp, aud);
    if (!sign_keep(p, r)) return;
    zxvd_buf_raw(r->out, "{\"id\":", 6);
    put_hex(r->out, p->tmp->id, SP_ID_LEN);
    zxvd_buf_raw(r->out, "}", 1);
}

static void put_reactions(zxvd_people_t *p, zxvd_buf_t *o, const sp_record_t *rec,
                          uint32_t *replies)
{
    static const sp_record_t *kids[ZXVD_PEOPLE_RECORDS];
    uint32_t n = ss_children(&p->store, rec->id, kids, ZXVD_PEOPLE_RECORDS);
    uint32_t shown = 0;
    *replies = 0;
    zxvd_buf_raw(o, "[", 1);
    for (uint32_t i = 0; i < n; i++) {
        if (kids[i]->kind == SP_KIND_REPLY) (*replies)++;
        if (kids[i]->kind != SP_KIND_REACTION) continue;
        bool seen = false;
        for (uint32_t j = 0; j < i && !seen; j++)
            seen = kids[j]->kind == SP_KIND_REACTION && kids[j]->body_len == kids[i]->body_len &&
                   !memcmp(kids[j]->body, kids[i]->body, kids[i]->body_len);
        if (seen) continue;
        uint32_t count = 0;
        bool mine = false;
        for (uint32_t j = i; j < n; j++)
            if (kids[j]->kind == SP_KIND_REACTION && kids[j]->body_len == kids[i]->body_len &&
                !memcmp(kids[j]->body, kids[i]->body, kids[i]->body_len)) {
                count++;
                mine = mine || !memcmp(kids[j]->author, p->me, SP_NODEID_LEN);
            }
        zxvd_buf_printf(o, "%s{\"reaction\":", shown++ ? "," : "");
        put_text(o, kids[i]->body, kids[i]->body_len);
        zxvd_buf_printf(o, ",\"count\":%u,\"mine\":%s}", (unsigned) count, mine ? "true" : "false");
    }
    zxvd_buf_raw(o, "]", 1);
}

static void m_feed(rq_t *r)
{
    zxvd_people_t *p = r->p;
    int64_t limit;
    if (!p_int(r, "limit", 1, ZXVD_PEOPLE_FEED, 20, &limit)) return;
    /* the groups this node is in, derived once per call */
    uint8_t gids[16][SP_ID_LEN];
    bool gin[16];
    uint32_t ng = 0, n = 0;
    for (uint32_t i = 0; i < ss_count(&p->store); i++) {
        const sp_record_t *rec = ss_at(&p->store, i);
        if (!rec || (rec->kind != SP_KIND_POST && rec->kind != SP_KIND_REPLY)) continue;
        if (rec->vis == SP_VIS_GROUP) {
            uint32_t k;
            for (k = 0; k < ng && memcmp(gids[k], rec->audience, SP_ID_LEN); k++) {
            }
            if (k == ng) {
                if (ng == 16) continue; /* more groups than one feed derives: skipped */
                memcpy(gids[ng], rec->audience, SP_ID_LEN);
                gin[ng++] = member_of(p, rec->audience);
            }
            if (!gin[k]) continue;
        } else if (rec->vis != SP_VIS_PUBLIC) {
            continue;
        }
        p->cand[n++] = rec;
    }
    sf_config_t cfg;
    sf_seen_t seen;
    sf_config_default(&cfg, p->clock.t);
    sf_seen_init(&seen);
    uint32_t k = sf_rank(&cfg, &seen, p->cand, n, p->work, p->picks, (uint32_t) limit);
    zxvd_buf_raw(r->out, "{\"me\":", 6);
    put_hex(r->out, p->me, SP_NODEID_LEN);
    zxvd_buf_printf(r->out, ",\"held\":%u,\"candidates\":%u,\"diversity\":%d,\"items\":[",
                    (unsigned) ss_count(&p->store), (unsigned) n,
                    (int) sf_diversity(p->work, p->picks, k));
    for (uint32_t i = 0; i < k; i++) {
        const sp_record_t *rec = p->cand[p->picks[i].index];
        zxvd_buf_raw(r->out, i ? ",{\"id\":" : "{\"id\":", i ? 7 : 6);
        put_hex(r->out, rec->id, SP_ID_LEN);
        zxvd_buf_raw(r->out, ",\"author\":", 10);
        put_hex(r->out, rec->author, SP_NODEID_LEN);
        zxvd_buf_printf(r->out, ",\"mine\":%s,\"kind\":\"%s\",\"parent\":",
                        memcmp(rec->author, p->me, SP_NODEID_LEN) ? "false" : "true",
                        rec->kind == SP_KIND_REPLY ? "reply" : "post");
        if (rec->has_parent)
            put_hex(r->out, rec->parent, SP_ID_LEN);
        else
            zxvd_buf_raw(r->out, "null", 4);
        zxvd_buf_raw(r->out, ",\"group\":", 9);
        if (rec->vis == SP_VIS_GROUP)
            put_hex(r->out, rec->audience, SP_ID_LEN);
        else
            zxvd_buf_raw(r->out, "null", 4);
        zxvd_buf_raw(r->out, ",\"text\":", 8);
        put_text(r->out, rec->body, rec->body_len);
        zxvd_buf_printf(r->out, ",\"lamport\":\"%llu\",\"wall_ms\":", (unsigned long long) rec->lamport);
        if (rec->has_wall)
            zxvd_buf_printf(r->out, "\"%llu\"", (unsigned long long) rec->wall_ms);
        else
            zxvd_buf_raw(r->out, "null", 4);
        uint32_t replies;
        zxvd_buf_raw(r->out, ",\"reactions\":", 13);
        put_reactions(p, r->out, rec, &replies);
        zxvd_buf_printf(r->out,
                        ",\"replies\":%u,\"score\":%d,\"surplus\":%d,\"fresh\":%d,\"rep\":%d}",
                        (unsigned) replies, (int) p->picks[i].score, (int) p->picks[i].surplus,
                        (int) p->picks[i].fresh, (int) p->picks[i].rep);
    }
    zxvd_buf_raw(r->out, "]}", 2);
}

static void m_groups(rq_t *r)
{
    zxvd_people_t *p = r->p;
    uint32_t shown = 0;
    zxvd_buf_raw(r->out, "{\"groups\":[", 11);
    for (uint32_t i = 0; i < ss_count(&p->store); i++) {
        const sp_record_t *rec = ss_at(&p->store, i);
        if (!rec || rec->kind != SP_KIND_GROUP_CREATE) continue;
        uint8_t gid[SP_ID_LEN];
        memcpy(gid, rec->id, SP_ID_LEN);
        sg_group_t g;
        uint32_t n = derive(p, gid, &g);
        if (!g.exists) continue;
        uint8_t inv;
        uint8_t role = my_role(p, n, &inv);
        zxvd_buf_printf(r->out, "%s{\"id\":", shown++ ? "," : "");
        put_hex(r->out, gid, SP_ID_LEN);
        zxvd_buf_raw(r->out, ",\"name\":", 8);
        if (utf8_ok(g.name, g.name_len))
            put_text(r->out, g.name, g.name_len);
        else
            zxvd_buf_raw(r->out, "\"(not UTF-8)\"", 13);
        zxvd_buf_printf(r->out, ",\"kind\":\"%s\",\"open\":%s,\"owner\":",
                        g.kind == SG_KIND_SYNDICATE ? "syndicate" : "group",
                        g.join == SG_JOIN_OPEN ? "true" : "false");
        put_hex(r->out, g.owner, SP_NODEID_LEN);
        zxvd_buf_printf(r->out,
                        ",\"members\":%u,\"my_role\":\"%s\",\"invited\":%s,\"applied\":%u,"
                        "\"ignored\":%u,\"charter\":%s}",
                        (unsigned) g.n_members, ROLE[role <= 4 ? role : 0],
                        inv != SG_ROLE_NONE ? "true" : "false", (unsigned) g.applied,
                        (unsigned) g.ignored, g.has_charter ? "true" : "false");
    }
    zxvd_buf_raw(r->out, "]}", 2);
}

static void m_group_create(rq_t *r)
{
    zxvd_people_t *p = r->p;
    char name[SG_NAME_MAX + 1];
    uint32_t len;
    bool open;
    if (!p_str(r, "name", name, sizeof name, true, &len) || !p_bool(r, "open", true, &open)) return;
    if (!len || !utf8_ok((const uint8_t *) name, len))
        return fail(r, E_PARAMS, "\"name\" must be 1 to 64 bytes of UTF-8");
    new_record(p, SP_KIND_GROUP_CREATE, SP_VIS_PUBLIC);
    if (sg_make_create(p->tmp, SG_KIND_GROUP, open ? SG_JOIN_OPEN : SG_JOIN_INVITE,
                       (const uint8_t *) name, len, NULL, 0, NULL, 0) != SP_OK)
        return fail(r, E_PEOPLE, "the group record could not be built");
    if (!sign_keep(p, r)) return;
    zxvd_buf_raw(r->out, "{\"group\":", 9);
    put_hex(r->out, p->tmp->id, SP_ID_LEN);
    zxvd_buf_printf(r->out, ",\"open\":%s,\"my_role\":\"owner\"}", open ? "true" : "false");
}

static void m_join_leave(rq_t *r, bool join)
{
    zxvd_people_t *p = r->p;
    uint8_t gid[SP_ID_LEN];
    if (!p_hex(r, "group", gid, SP_ID_LEN, true)) return;
    sg_group_t g;
    uint8_t inv;
    uint32_t n = derive(p, gid, &g);
    if (!g.exists) return fail(r, E_PEOPLE, "unknown group: import its record first");
    uint8_t role = my_role(p, n, &inv);
    if (join) {
        if (role >= SG_ROLE_OBSERVER) return fail(r, E_PEOPLE, "you are already a member");
        if (g.join != SG_JOIN_OPEN && inv == SG_ROLE_NONE)
            return fail(r, E_PEOPLE, "this group is invite only and you have no invitation");
        new_record(p, SP_KIND_GROUP_JOIN, SP_VIS_PUBLIC);
        if (sg_make_join(p->tmp, gid, NULL, 0) != SP_OK)
            return fail(r, E_PEOPLE, "the join record could not be built");
    } else {
        if (role < SG_ROLE_OBSERVER) return fail(r, E_PEOPLE, "you are not a member");
        if (role == SG_ROLE_OWNER)
            return fail(r, E_PEOPLE, "the owner cannot leave (hand the group on first)");
        new_record(p, SP_KIND_GROUP_LEAVE, SP_VIS_PUBLIC);
        if (sg_make_leave(p->tmp, gid, p->me) != SP_OK)
            return fail(r, E_PEOPLE, "the leave record could not be built");
    }
    if (!sign_keep(p, r)) return;
    n = derive(p, gid, &g);
    zxvd_buf_raw(r->out, "{\"id\":", 6);
    put_hex(r->out, p->tmp->id, SP_ID_LEN);
    zxvd_buf_raw(r->out, ",\"group\":", 9);
    put_hex(r->out, gid, SP_ID_LEN);
    role = my_role(p, n, NULL);
    zxvd_buf_printf(r->out, ",\"my_role\":\"%s\",\"members\":%u}", ROLE[role <= 4 ? role : 0],
                    (unsigned) g.n_members);
}

static void m_join(rq_t *r)
{
    m_join_leave(r, true);
}

static void m_leave(rq_t *r)
{
    m_join_leave(r, false);
}

static void m_export(rq_t *r)
{
    zxvd_people_t *p = r->p;
    static uint8_t enc[SP_ENC_MAX];
    static char hx[2 * SP_ENC_MAX + 1];
    uint8_t id[SP_ID_LEN];
    if (!p_hex(r, "id", id, SP_ID_LEN, true)) return;
    const sp_record_t *rec = ss_get(&p->store, id);
    if (!rec) return fail(r, E_PEOPLE, "no record with that id is held here");
    memcpy(p->tmp, rec, sizeof *p->tmp);
    if (!p->tmp->has_pk && !memcmp(p->tmp->author, p->me, SP_NODEID_LEN))
        sp_attach_pk(p->tmp, p->pk);
    if (!p->tmp->has_pk) return fail(r, E_PEOPLE, "the record does not carry its author's key");
    int32_t n = sp_encode(p->tmp, enc, sizeof enc);
    if (n < 0) return fail(r, E_PEOPLE, "the record could not be encoded");
    hex_of(enc, (uint32_t) n, hx);
    zxvd_buf_printf(r->out, "{\"bytes\":%d,\"hex\":\"%s\"}", (int) n, hx);
}

static void m_import(rq_t *r)
{
    zxvd_people_t *p = r->p;
    static char hx[2 * SP_ENC_MAX + 2];
    static uint8_t enc[SP_ENC_MAX];
    uint32_t len;
    if (!p_str(r, "hex", hx, sizeof hx, true, &len)) return;
    int32_t n = len % 2u ? -1 : w4_hex_decode(hx, len, enc, sizeof enc);
    if (n <= 0) return fail(r, E_PARAMS, "\"hex\" must be an encoded record in hex");
    sp_verifier_t v = verifier(p);
    int32_t st = sp_decode(enc, (uint32_t) n, p->tmp, &v);
    if (st != SP_OK)
        return fail(r, E_PEOPLE, "the record does not decode and verify (%d)", (int) st);
    if (!shown_kind(p->tmp->kind))
        return fail(r, E_PEOPLE, "this kind of record is not used by this app yet");
    if (text_kind(p->tmp->kind) && !utf8_ok(p->tmp->body, p->tmp->body_len))
        return fail(r, E_PEOPLE, "the record's text is not UTF-8");
    bool dup = ss_get(&p->store, p->tmp->id) != NULL;
    if (!dup && !keep_record(p, r, p->tmp)) return;
    zxvd_buf_raw(r->out, "{\"id\":", 6);
    put_hex(r->out, p->tmp->id, SP_ID_LEN);
    zxvd_buf_printf(r->out, ",\"added\":%s}", dup ? "false" : "true");
}

/* ===================== H5: calls ===================== */

static void loop_push(zxvd_people_t *p, uint32_t to_line, const uint8_t *m, uint32_t len)
{
    if (p->loopq_n >= LOOPQ || len > CALL_SIG_MAX) return; /* dropped like a lost datagram */
    memcpy(p->loopq[p->loopq_n].b, m, len);
    p->loopq[p->loopq_n].len = len;
    p->loopq_to[p->loopq_n++] = to_line;
}

static int call_send(void *ctx, const uint8_t *m, uint32_t len)
{
    line_t *l = ctx;
    zxvd_people_t *p = l->p;
    if (l->idx == LOOP) { /* the loopback line answers the line that called it */
        for (uint32_t i = 0; i < ZXVD_PEOPLE_LINES; i++)
            if (p->line[i].s.peer == p->loop_id && p->line[i].s.state != CALL_ST_IDLE) {
                loop_push(p, i, m, len);
                return 0;
            }
        return 0;
    }
    if (l->s.peer == p->loop_id) {
        loop_push(p, LOOP, m, len);
        return 0;
    }
    if (p->out_n == CALL_OUT) { /* the oldest waiting message gives way */
        memmove(&p->out[0], &p->out[1], sizeof p->out[0] * (CALL_OUT - 1u));
        p->out_n--;
        p->out_dropped++;
    }
    if (len > CALL_SIG_MAX) return -1;
    memcpy(p->out[p->out_n].b, m, len);
    p->out[p->out_n++].len = len;
    return 0;
}

static uint32_t call_rand(void *ctx)
{
    (void) ctx;
    uint32_t v = 0;
    if (!os_random((uint8_t *) &v, sizeof v)) v = (uint32_t) time(NULL);
    return v;
}

static void line_init(zxvd_people_t *p, uint32_t i)
{
    call_session_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.self_id = i == LOOP ? p->loop_id : p->self_id;
    cfg.send = call_send;
    cfg.rand = call_rand;
    cfg.ctx = &p->line[i];
    p->line[i].p = p;
    p->line[i].idx = i;
    call_session_init(&p->line[i].s, &cfg);
}

static void loop_pump(zxvd_people_t *p)
{
    uint32_t now = (uint32_t) now_ms(p);
    for (uint32_t guard = 0; p->loopq_n && guard < 64u; guard++) {
        cmsg_t m = p->loopq[0];
        uint32_t to = p->loopq_to[0];
        memmove(&p->loopq[0], &p->loopq[1], sizeof p->loopq[0] * (LOOPQ - 1u));
        memmove(&p->loopq_to[0], &p->loopq_to[1], sizeof p->loopq_to[0] * (LOOPQ - 1u));
        p->loopq_n--;
        line_t *l = &p->line[to];
        if (to == LOOP && l->s.state == CALL_ST_ENDED) call_session_reset(&l->s);
        (void) call_session_on_message(&l->s, m.b, m.len, now);
    }
}

static const char *media_name(uint8_t m)
{
    return (m & CALL_MEDIA_VIDEO) ? "audio+video" : "audio";
}

/* call_session.h's names, in lower case for the wire */
static const char *lower(const char *s, char *buf, size_t cap)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) buf[i] = (char) (s[i] >= 'A' && s[i] <= 'Z' ? s[i] + 32 : s[i]);
    buf[i] = 0;
    return buf;
}

static void put_line(zxvd_people_t *p, zxvd_buf_t *o, uint32_t i)
{
    const call_session_t *s = &p->line[i].s;
    char cid[17], st[24], er[24];
    hex_of(s->call_id, 8, cid);
    zxvd_buf_printf(o,
                    "{\"line\":%u,\"loopback\":%s,\"state\":\"%s\",\"peer\":%u,\"outgoing\":%s,"
                    "\"call_id\":\"%s\",\"media\":\"%s\",\"audio_pt\":%u,\"video_pt\":%u,"
                    "\"end_reason\":\"%s\",\"missed\":%u}",
                    (unsigned) i, i == LOOP ? "true" : "false", lower(call_state_name(s->state), st, sizeof st),
                    (unsigned) s->peer, s->outgoing ? "true" : "false", cid,
                    media_name(s->media ? s->media : s->media_wanted), (unsigned) s->audio_pt,
                    (unsigned) s->video_pt, lower(call_end_reason_name(s->end_reason), er, sizeof er),
                    (unsigned) p->line[i].missed);
}

static bool line_param(rq_t *r, uint32_t *line)
{
    int64_t v;
    if (!p_int(r, "line", 0, LOOP, -1, &v)) return false;
    *line = (uint32_t) v;
    return true;
}

static void m_call_start(rq_t *r)
{
    zxvd_people_t *p = r->p;
    char media[16];
    uint32_t peer = 0;
    int32_t i = param(r, "peer");
    if (i < 0) return fail(r, E_PARAMS, "missing parameter \"peer\" (a member id or \"loopback\")");
    if (r->t[i].type == W4_J_STR && w4_json_str_eq(r->js, &r->t[i], "loopback")) {
        peer = p->loop_id;
    } else {
        int64_t v;
        if (!p_int(r, "peer", 1, UINT32_MAX, -1, &v)) return;
        peer = (uint32_t) v;
    }
    if (!p_str(r, "media", media, sizeof media, false, NULL)) return;
    uint8_t mm = CALL_MEDIA_AUDIO;
    if (!strcmp(media, "video"))
        mm |= CALL_MEDIA_VIDEO;
    else if (media[0] && strcmp(media, "audio"))
        return fail(r, E_PARAMS, "\"media\" must be \"audio\" or \"video\"");
    if (peer == p->self_id) return fail(r, E_PARAMS, "that is this node's own id; use \"loopback\"");
    if (peer == p->loop_id && p->line[LOOP].s.state != CALL_ST_IDLE &&
        p->line[LOOP].s.state != CALL_ST_ENDED)
        return fail(r, E_PEOPLE, "the loopback line is busy");
    uint32_t k;
    for (k = 0; k < ZXVD_PEOPLE_LINES; k++)
        if (p->line[k].s.state == CALL_ST_IDLE || p->line[k].s.state == CALL_ST_ENDED) break;
    if (k == ZXVD_PEOPLE_LINES) return fail(r, E_PEOPLE, "every line is in a call");
    if (p->line[k].s.state == CALL_ST_ENDED) call_session_reset(&p->line[k].s);
    if (peer == p->loop_id && p->line[LOOP].s.state == CALL_ST_ENDED)
        call_session_reset(&p->line[LOOP].s);
    int rc = call_session_dial(&p->line[k].s, peer, mm, (uint32_t) now_ms(p));
    if (rc < 0) return fail(r, E_PEOPLE, "the call could not be placed (%d)", rc);
    loop_pump(p);
    put_line(p, r->out, k);
}

static void m_call_accept(rq_t *r)
{
    zxvd_people_t *p = r->p;
    uint32_t k;
    if (!line_param(r, &k)) return;
    if (p->line[k].s.state != CALL_ST_INCOMING)
        return fail(r, E_PEOPLE, "line %u is not ringing", (unsigned) k);
    int rc = call_session_accept(&p->line[k].s, (uint32_t) now_ms(p));
    if (rc < 0) return fail(r, E_PEOPLE, "the call could not be answered (%d)", rc);
    loop_pump(p);
    put_line(p, r->out, k);
}

static void m_call_hangup(rq_t *r)
{
    zxvd_people_t *p = r->p;
    uint32_t k;
    if (!line_param(r, &k)) return;
    call_session_t *s = &p->line[k].s;
    if (s->state == CALL_ST_IDLE || s->state == CALL_ST_ENDED)
        return fail(r, E_PEOPLE, "line %u has no call", (unsigned) k);
    uint32_t now = (uint32_t) now_ms(p);
    int rc = s->state == CALL_ST_INCOMING ? call_session_reject(s, now) : call_session_hangup(s, now);
    if (rc < 0) return fail(r, E_PEOPLE, "the call could not be ended (%d)", rc);
    loop_pump(p);
    put_line(p, r->out, k);
}

static void m_call_status(rq_t *r)
{
    zxvd_people_t *p = r->p;
    zxvd_buf_printf(r->out,
                    "{\"self_id\":%u,\"loopback_id\":%u,\"media\":\"signalling only: capture, "
                    "codecs and the media path are the host's job and are not wired in this "
                    "build\",\"outbox\":%u,\"dropped\":\"%llu\",\"lines\":[",
                    (unsigned) p->self_id, (unsigned) p->loop_id, (unsigned) p->out_n,
                    (unsigned long long) p->out_dropped);
    for (uint32_t i = 0; i < NLINES; i++) {
        if (i) zxvd_buf_raw(r->out, ",", 1);
        put_line(p, r->out, i);
    }
    zxvd_buf_raw(r->out, "]}", 2);
}

/* one message from another node: to a line by (peer, call id), an offer to a free line */
static bool call_in(zxvd_people_t *p, const uint8_t *m, uint32_t len)
{
    call_sig_t sg;
    if (call_sig_parse(m, len, &sg) < 0 || sg.to != p->self_id || sg.from == p->loop_id) return false;
    uint32_t now = (uint32_t) now_ms(p);
    int pick = -1;
    for (uint32_t i = 0; i < ZXVD_PEOPLE_LINES && pick < 0; i++)
        if (p->line[i].s.state != CALL_ST_IDLE && p->line[i].s.peer == sg.from &&
            !memcmp(p->line[i].s.call_id, sg.call_id, 8))
            pick = (int) i;
    if (pick < 0 && sg.type == CALL_MSG_OFFER) {
        for (uint32_t i = 0; i < ZXVD_PEOPLE_LINES && pick < 0; i++) /* glare */
            if (p->line[i].s.peer == sg.from && p->line[i].s.outgoing &&
                (p->line[i].s.state == CALL_ST_OUTGOING || p->line[i].s.state == CALL_ST_RINGING_OUT))
                pick = (int) i;
        for (uint32_t i = 0; i < ZXVD_PEOPLE_LINES && pick < 0; i++)
            if (p->line[i].s.state == CALL_ST_IDLE || p->line[i].s.state == CALL_ST_ENDED)
                pick = (int) i;
        if (pick < 0) pick = 0; /* busy everywhere: line 0 answers BUSY */
    }
    if (pick < 0) return false;
    call_session_t *s = &p->line[pick].s;
    uint8_t before = (uint8_t) s->state;
    int rc = call_session_on_message(s, m, len, now);
    if (sg.type == CALL_MSG_OFFER && before != CALL_ST_INCOMING && s->state == CALL_ST_ENDED)
        p->line[pick].missed++;
    loop_pump(p);
    return rc >= 0;
}

static void m_call_signal(rq_t *r)
{
    zxvd_people_t *p = r->p;
    int32_t a = param(r, "in");
    uint32_t n = 0, ok = 0, bad = 0;
    if (a == W4_ERR_PARSE) return fail(r, E_PARAMS, "\"in\" is given twice");
    if (a >= 0) {
        if (r->t[a].type != W4_J_ARR || r->t[a].size > SIGNAL_MAX)
            return fail(r, E_PARAMS, "\"in\" must be an array of at most %u hex strings",
                        SIGNAL_MAX);
        n = r->t[a].size;
    }
    uint8_t msgs[SIGNAL_MAX][CALL_SIG_MAX];
    uint32_t lens[SIGNAL_MAX];
    for (uint32_t k = 0; k < n; k++) { /* all parsed before any is used */
        char hx[2 * CALL_SIG_MAX + 2];
        int32_t e = w4_json_at(r->t, r->n, a, k);
        int32_t got = e >= 0 && r->t[e].type == W4_J_STR ? w4_json_str(r->js, &r->t[e], hx, sizeof hx)
                                                          : -1;
        int32_t d = got > 0 && got % 2 == 0
                        ? w4_hex_decode(hx, (uint32_t) got, msgs[k], CALL_SIG_MAX)
                        : -1;
        if (d <= 0)
            return fail(r, E_PARAMS, "\"in\" must hold call messages in hex (at most %u bytes)",
                        (unsigned) CALL_SIG_MAX);
        lens[k] = (uint32_t) d;
    }
    for (uint32_t k = 0; k < n; k++) {
        if (call_in(p, msgs[k], lens[k]))
            ok++;
        else
            bad++;
    }
    zxvd_buf_printf(r->out, "{\"delivered\":%u,\"rejected\":%u,\"out\":[", (unsigned) ok,
                    (unsigned) bad);
    for (uint32_t k = 0; k < p->out_n; k++) {
        char hx[2 * CALL_SIG_MAX + 1];
        hex_of(p->out[k].b, p->out[k].len, hx);
        zxvd_buf_printf(r->out, "%s\"%s\"", k ? "," : "", hx);
    }
    p->out_n = 0;
    zxvd_buf_raw(r->out, "]}", 2);
}

/* ===================== H6: translation ===================== */

static int xl_backend(const xlate_backend_request_t *req, char *out, uint32_t cap,
                      uint32_t *out_len, void *ctx)
{
    zxvd_people_t *p = ctx;
    *out_len = 0;
    if (!p->host.generate) {
        p->gen_rc = ZXVD_GEN_NO_MODEL;
        snprintf(p->gen_why, sizeof p->gen_why, "this build has no model slot");
        return -1;
    }
    char *prompt = malloc((size_t) req->prompt_len + 1);
    if (!prompt) return -1;
    memcpy(prompt, req->prompt, req->prompt_len);
    prompt[req->prompt_len] = 0;
    out[0] = 0;
    int rc = p->host.generate(p->host.ctx, prompt, out, cap);
    free(prompt);
    out[cap - 1] = 0;
    if (rc != 0) {
        p->gen_rc = rc;
        snprintf(p->gen_why, sizeof p->gen_why, "%s", out[0] ? out : "the model did not answer");
        return -1;
    }
    *out_len = (uint32_t) strlen(out);
    return 0;
}

static bool tag_ok(const char *t)
{
    size_t n = strlen(t);
    if (n < 2 || n > 15) return false;
    for (size_t i = 0; i < n; i++) {
        char c = t[i];
        bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && !(c == '-' && i > 0 && i + 1 < n && t[i - 1] != '-')) return false;
    }
    return true;
}

static void m_detect(rq_t *r)
{
    static char text[ZXVD_PEOPLE_TEXT + 1];
    uint32_t len;
    if (!p_str(r, "text", text, sizeof text, true, &len)) return;
    if (!len) return fail(r, E_PARAMS, "\"text\" must not be empty");
    xlate_langid_result_t li;
    xlate_langid(text, len, &li);
    zxvd_buf_raw(r->out, "{\"language\":", 12);
    zxvd_buf_json_str(r->out, li.tag ? li.tag : "und");
    zxvd_buf_raw(r->out, ",\"name\":", 8);
    zxvd_buf_json_str(r->out, li.lang >= 0 ? xlate_langid_name((uint32_t) li.lang) : "undetermined");
    zxvd_buf_printf(r->out, ",\"confidence\":%u,\"letters\":%u,\"reliable\":%s}",
                    (unsigned) li.confidence, (unsigned) li.letters, li.reliable ? "true" : "false");
}

static void m_languages(rq_t *r)
{
    uint32_t n = xlate_langid_count();
    zxvd_buf_printf(r->out, "{\"count\":%u,\"languages\":[", (unsigned) n);
    for (uint32_t i = 0; i < n; i++) {
        zxvd_buf_raw(r->out, i ? ",{\"tag\":" : "{\"tag\":", i ? 8 : 7);
        zxvd_buf_json_str(r->out, xlate_langid_tag(i));
        zxvd_buf_raw(r->out, ",\"name\":", 8);
        zxvd_buf_json_str(r->out, xlate_langid_name(i));
        zxvd_buf_raw(r->out, "}", 1);
    }
    zxvd_buf_raw(r->out, "]}", 2);
}

static void m_translate(rq_t *r)
{
    zxvd_people_t *p = r->p;
    static char text[ZXVD_PEOPLE_TEXT + 1];
    static char out[XLATE_TEXT_MAX + 1];
    char to[XLATE_TAG_MAX], from[XLATE_TAG_MAX];
    uint32_t len;
    if (!p_str(r, "text", text, sizeof text, true, &len) ||
        !p_str(r, "to", to, sizeof to, true, NULL) || !p_str(r, "from", from, sizeof from, false, NULL))
        return;
    if (!len) return fail(r, E_PARAMS, "\"text\" must not be empty");
    if (!tag_ok(to)) return fail(r, E_PARAMS, "\"to\" must be a language tag such as \"es\"");
    bool autod = !from[0] || !strcmp(from, "auto");
    if (!autod && !tag_ok(from))
        return fail(r, E_PARAMS, "\"from\" must be a language tag or \"auto\"");
    xlate_request_t q = {text, len, NULL, 0, autod ? "auto" : from, to, XLATE_KIND_POST};
    xlate_response_t res;
    p->gen_rc = 0;
    p->gen_why[0] = 0;
    int st = xlate_translate(p->xl, &q, out, sizeof out, &res);
    if (st == XLATE_ERR_UNKNOWN_LANG)
        return fail(r, E_PEOPLE, "could not tell which language the text is in; give \"from\"");
    if (st == XLATE_ERR_TOO_LONG) return fail(r, E_PARAMS, "the text is too long to translate");
    if (st == XLATE_ERR_BACKEND || st == XLATE_ERR_NO_BACKEND) {
        if (p->gen_rc == ZXVD_GEN_NO_MODEL || st == XLATE_ERR_NO_BACKEND)
            return fail(r, E_NOMODEL,
                        "no model: install a GGUF model that can generate to translate (%s)",
                        p->gen_why[0] ? p->gen_why : "none loaded");
        return fail(r, E_PEOPLE, "the model did not translate: %s",
                    p->gen_why[0] ? p->gen_why : "it returned nothing");
    }
    if (st != XLATE_OK) return fail(r, E_PEOPLE, "translation failed (%d)", st);
    bool machine = (res.flags & XLATE_F_MACHINE) != 0;
    char marker[160] = "";
    if (machine) xlate_marker_label(&res, marker, sizeof marker);
    zxvd_buf_raw(r->out, "{\"text\":", 8);
    zxvd_buf_json_str(r->out, out);
    zxvd_buf_printf(r->out, ",\"translated\":%s,\"machine\":%s,\"from\":", machine ? "true" : "false",
                    machine ? "true" : "false");
    zxvd_buf_json_str(r->out, res.src);
    zxvd_buf_raw(r->out, ",\"to\":", 6);
    zxvd_buf_json_str(r->out, res.tgt);
    zxvd_buf_printf(r->out, ",\"detected\":%s,\"unsure\":%s,\"cached\":%s,\"marker\":",
                    (res.flags & XLATE_F_SRC_DETECTED) ? "true" : "false",
                    (res.flags & XLATE_F_SRC_UNSURE) ? "true" : "false",
                    (res.flags & XLATE_F_CACHED) ? "true" : "false");
    zxvd_buf_json_str(r->out, marker);
    zxvd_buf_raw(r->out, "}", 1);
}

/* ===================== H7: the device mesh ===================== */

typedef struct {
    char magic[12];
    uint32_t size;
    uint8_t hash[32];
} snap_hdr_t;

static void snap_scrub(dm_mesh_t *c)
{
    memset(&c->host, 0, sizeof c->host);
    memset(c->tx, 0, sizeof c->tx);
    memset(c->rx, 0, sizeof c->rx);
    memset(c->app_buf, 0, sizeof c->app_buf);
    for (uint32_t i = 0; i < DM_MAX_DEVICES; i++) {
        memset(&c->peers[i].s, 0, sizeof c->peers[i].s);
        memset(&c->peers[i].pend, 0, sizeof c->peers[i].pend);
    }
    c->hs_active = 0;
    c->inv_active = 0;
    c->join_active = 0;
    memset(c->reqs, 0, sizeof c->reqs);
}

static bool mesh_save(zxvd_people_t *p)
{
    p->mesh_dirty = false;
    if (!p->mesh_path[0]) return true;
    dm_mesh_t *c = malloc(sizeof *c);
    if (!c) return false;
    memcpy(c, p->mesh, sizeof *c);
    snap_scrub(c);
    snap_hdr_t h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, MESH_MAGIC, sizeof MESH_MAGIC);
    h.size = (uint32_t) sizeof *c;
    sha256((const uint8_t *) c, sizeof *c, h.hash);
    char tmp[1060];
    snprintf(tmp, sizeof tmp, "%s.tmp", p->mesh_path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    bool ok = fd >= 0 && write_all(fd, (const char *) &h, sizeof h) &&
              write_all(fd, (const char *) c, sizeof *c) && fsync(fd) == 0;
    if (fd >= 0) close(fd);
    memset(c, 0, sizeof *c);
    free(c);
    ok = ok && rename(tmp, p->mesh_path) == 0;
    if (!ok) {
        unlink(tmp);
        snprintf(p->mesh_why, sizeof p->mesh_why, "the mesh snapshot could not be saved: %s",
                 strerror(errno));
    }
    return ok;
}

static void mesh_event(void *ctx, const dm_event_t *e)
{
    zxvd_people_t *p = ctx;
    if (p->ev_n == EVENTS) {
        memmove(&p->ev[0], &p->ev[1], sizeof p->ev[0] * (EVENTS - 1u));
        p->ev_n--;
    }
    ev_t *v = &p->ev[p->ev_n++];
    v->kind = e->kind;
    memcpy(v->peer, e->peer, DM_ID_BYTES);
    v->sas = e->sas;
    v->status = e->status;
    v->at = now_ms(p);
    switch (e->kind) {
    case DM_EV_SAS:
        p->sas_on = true;
        memcpy(p->sas_peer, e->peer, DM_ID_BYTES);
        p->sas = e->sas;
        break;
    case DM_EV_PAIRED:
    case DM_EV_PAIR_FAILED:
        p->sas_on = false;
        p->mesh_dirty = true;
        break;
    case DM_EV_ROSTER:
    case DM_EV_REVOKED:
    case DM_EV_SELF_REVOKED:
        p->mesh_dirty = true;
        break;
    default:
        break;
    }
}

static void mesh_random(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    if (!os_random(out, n)) abort(); /* devmesh.h: a CSPRNG or nothing */
}

static int mesh_send(void *ctx, const uint8_t to[DM_ID_BYTES], const uint8_t *f, uint32_t len)
{
    zxvd_people_t *p = ctx;
    if (p->mout_n == MESH_OUT) {
        free(p->mout[0].b);
        memmove(&p->mout[0], &p->mout[1], sizeof p->mout[0] * (MESH_OUT - 1u));
        p->mout_n--;
        p->mout_dropped++;
    }
    uint8_t *b = malloc(len ? len : 1);
    if (!b) return -1;
    memcpy(b, f, len);
    frame_t *fr = &p->mout[p->mout_n++];
    memcpy(fr->to, to, DM_ID_BYTES);
    fr->len = len;
    fr->b = b;
    return 0;
}

static void mesh_after(zxvd_people_t *p)
{
    if (p->mesh_dirty) (void) mesh_save(p);
}

static const char *dm_why(int st)
{
    switch (st) {
    case DM_ERR_ARG: return "an argument is out of range";
    case DM_ERR_SIZE: return "too large";
    case DM_ERR_FORMAT: return "the code or QR payload is not well formed";
    case DM_ERR_AUTH: return "a signature, MAC or key check failed";
    case DM_ERR_UNKNOWN: return "that device is not in the roster";
    case DM_ERR_REVOKED: return "that device is revoked";
    case DM_ERR_STATE: return "not possible now (no pairing waiting, or already in a mesh)";
    case DM_ERR_FULL: return "the mesh is full (8 devices)";
    case DM_ERR_REPLAY: return "an old or repeated message";
    case DM_ERR_PERM: return "not allowed (only an admin device may do this; the last admin stays)";
    case DM_ERR_EXPIRED: return "the invitation has expired";
    case DM_ERR_NOPEER: return "no session with that device";
    case DM_ERR_CRYPTO: return "key generation or encapsulation failed";
    case DM_ERR_TRANSPORT: return "the frame could not be queued";
    default: return "failed";
    }
}

static const char *ROLE_DM[] = {"?", "home", "thin", "standalone"};

static void m_dm_status(rq_t *r)
{
    zxvd_people_t *p = r->p;
    dm_mesh_t *m = p->mesh;
    const dm_roster_t *ro = dm_roster(m);
    zxvd_buf_printf(r->out, "{\"joined\":%s,\"mesh_id\":", m->joined ? "true" : "false");
    if (m->joined)
        put_hex(r->out, m->mesh_id, DM_ID_BYTES);
    else
        zxvd_buf_raw(r->out, "null", 4);
    zxvd_buf_raw(r->out, ",\"self\":", 8);
    put_hex(r->out, dm_self_id(m), DM_ID_BYTES);
    zxvd_buf_raw(r->out, ",\"name\":", 8);
    zxvd_buf_json_str(r->out, m->self_name);
    zxvd_buf_printf(r->out,
                    ",\"role\":\"%s\",\"admin\":%s,\"level\":\"high\",\"roster_version\":\"%llu\","
                    "\"devices\":%u,\"inviting\":%s,\"joining\":%s,\"pairing\":",
                    ROLE_DM[m->self_role <= 3 ? m->self_role : 0], dm_is_admin(m) ? "true" : "false",
                    (unsigned long long) ro->version, (unsigned) ro->n,
                    m->inv_active ? "true" : "false", m->join_active ? "true" : "false");
    if (p->sas_on) {
        zxvd_buf_raw(r->out, "{\"device\":", 10);
        put_hex(r->out, p->sas_peer, DM_ID_BYTES);
        zxvd_buf_printf(r->out, ",\"sas\":\"%06u\"}", (unsigned) p->sas);
    } else {
        zxvd_buf_raw(r->out, "null", 4);
    }
    zxvd_buf_printf(r->out,
                    ",\"frames_waiting\":%u,\"frames_dropped\":\"%llu\",\"carrier\":\"none in this "
                    "build: frames wait for the phone link or another carrier\",\"saved\":%s,"
                    "\"save_error\":",
                    (unsigned) p->mout_n, (unsigned long long) p->mout_dropped,
                    p->mesh_path[0] ? "true" : "false");
    if (p->mesh_why[0])
        zxvd_buf_json_str(r->out, p->mesh_why);
    else
        zxvd_buf_raw(r->out, "null", 4);
    zxvd_buf_raw(r->out, ",\"events\":[", 11);
    for (uint32_t i = 0; i < p->ev_n; i++) {
        zxvd_buf_printf(r->out, "%s{\"kind\":%u,\"peer\":", i ? "," : "", (unsigned) p->ev[i].kind);
        put_hex(r->out, p->ev[i].peer, DM_ID_BYTES);
        zxvd_buf_printf(r->out, ",\"status\":%d}", (int) p->ev[i].status);
    }
    zxvd_buf_raw(r->out, "]}", 2);
}

static void m_dm_invite(rq_t *r)
{
    zxvd_people_t *p = r->p;
    char role[16], addr[DM_ADDR_MAX + 1];
    uint32_t alen;
    bool held, admin;
    if (!p_str(r, "role", role, sizeof role, false, NULL) || !p_bool(r, "held", true, &held) ||
        !p_bool(r, "admin", false, &admin) || !p_str(r, "addr", addr, sizeof addr, false, &alen))
        return;
    uint8_t rl = DM_ROLE_THIN;
    if (!strcmp(role, "home"))
        rl = DM_ROLE_HOME;
    else if (!strcmp(role, "standalone"))
        rl = DM_ROLE_STANDALONE;
    else if (role[0] && strcmp(role, "thin"))
        return fail(r, E_PARAMS, "\"role\" must be \"thin\", \"standalone\" or \"home\"");
    uint64_t now = now_ms(p);
    bool created = false;
    if (!p->mesh->joined) {
        int st = dm_create(p->mesh, now);
        if (st != DM_OK) return fail(r, E_PEOPLE, "the mesh could not be created: %s", dm_why(st));
        created = true;
        p->mesh_dirty = true;
    }
    uint8_t qr[DM_QR_MAX];
    uint32_t qlen = 0;
    char code[DM_CODE_CHARS + 1];
    int st = dm_invite(p->mesh, rl, (uint8_t) ((admin ? DM_FLAG_ADMIN : 0) | (held ? DM_FLAG_HELD : 0)),
                       alen ? (const uint8_t *) addr : NULL, alen, now, qr, &qlen, code);
    mesh_after(p);
    if (st != DM_OK) return fail(r, E_PEOPLE, "no invitation: %s", dm_why(st));
    char b32[(DM_QR_MAX * 8u + 4u) / 5u + 2u];
    if (dm_b32_encode(qr, qlen, b32, sizeof b32) < 0) return fail(r, E_PEOPLE, "QR encoding failed");
    zxvd_buf_raw(r->out, "{\"qr\":", 6);
    zxvd_buf_json_str(r->out, b32);
    zxvd_buf_printf(r->out, ",\"qr_bytes\":%u,\"code\":\"%.5s-%.5s\",\"expires_in_ms\":%u,"
                            "\"created_mesh\":%s,\"mesh_id\":",
                    (unsigned) qlen, code, code + 5, (unsigned) DM_INVITE_TTL_MS,
                    created ? "true" : "false");
    put_hex(r->out, p->mesh->mesh_id, DM_ID_BYTES);
    zxvd_buf_raw(r->out, "}", 1);
}

static void m_dm_join(rq_t *r)
{
    zxvd_people_t *p = r->p;
    char qr[2 * DM_QR_MAX], code[32];
    uint32_t ql;
    if (!p_str(r, "qr", qr, sizeof qr, false, &ql) || !p_str(r, "code", code, sizeof code, false, NULL))
        return;
    if (has(r, "qr") == has(r, "code"))
        return fail(r, E_PARAMS, "give exactly one of \"qr\" and \"code\"");
    int st;
    uint64_t now = now_ms(p);
    if (has(r, "qr")) {
        uint8_t raw[DM_QR_MAX];
        int32_t n = dm_b32_decode(qr, raw, sizeof raw);
        if (n <= 0) return fail(r, E_PARAMS, "\"qr\" must be the base32 QR payload");
        st = dm_join_qr(p->mesh, raw, (uint32_t) n, now);
    } else {
        st = dm_join_code(p->mesh, code, now);
    }
    mesh_after(p);
    if (st != DM_OK) return fail(r, E_PEOPLE, "could not start pairing: %s", dm_why(st));
    zxvd_buf_printf(r->out, "{\"pairing\":true,\"frames_waiting\":%u,\"next\":\"compare the "
                            "six-digit code on both screens, then devmesh.confirm_sas\"}",
                    (unsigned) p->mout_n);
}

static void m_dm_confirm(rq_t *r)
{
    zxvd_people_t *p = r->p;
    uint8_t dev[DM_ID_BYTES];
    bool match;
    if (!p_hex(r, "device", dev, DM_ID_BYTES, true)) return;
    if (!has(r, "match")) return fail(r, E_PARAMS, "missing boolean parameter \"match\"");
    if (!p_bool(r, "match", false, &match)) return;
    int st = dm_pair_confirm(p->mesh, dev, match, now_ms(p));
    if (st == DM_OK && !match) p->sas_on = false;
    mesh_after(p);
    if (st != DM_OK) return fail(r, E_PEOPLE, "not confirmed: %s", dm_why(st));
    const dm_dev_t *d = dm_roster_find(dm_roster(p->mesh), dev);
    zxvd_buf_printf(r->out, "{\"confirmed\":%s,\"in_roster\":%s,\"devices\":%u}",
                    match ? "true" : "false", d && d->status == DM_DEV_ACTIVE ? "true" : "false",
                    (unsigned) dm_roster(p->mesh)->n);
}

static void m_dm_revoke(rq_t *r)
{
    zxvd_people_t *p = r->p;
    uint8_t dev[DM_ID_BYTES];
    if (!p_hex(r, "device", dev, DM_ID_BYTES, true)) return;
    if (!memcmp(dev, dm_self_id(p->mesh), DM_ID_BYTES))
        return fail(r, E_PEOPLE, "this device cannot revoke itself");
    int st = dm_revoke(p->mesh, dev, now_ms(p));
    p->mesh_dirty = p->mesh_dirty || st == DM_OK;
    mesh_after(p);
    if (st != DM_OK) return fail(r, E_PEOPLE, "not revoked: %s", dm_why(st));
    zxvd_buf_printf(r->out, "{\"revoked\":true,\"roster_version\":\"%llu\"}",
                    (unsigned long long) dm_roster(p->mesh)->version);
}

static void m_dm_devices(rq_t *r)
{
    zxvd_people_t *p = r->p;
    const dm_roster_t *ro = dm_roster(p->mesh);
    uint64_t now = now_ms(p);
    zxvd_buf_printf(r->out, "{\"roster_version\":\"%llu\",\"devices\":[", (unsigned long long) ro->version);
    for (uint32_t i = 0; i < ro->n; i++) {
        const dm_dev_t *d = &ro->dev[i];
        bool self = !memcmp(d->id, dm_self_id(p->mesh), DM_ID_BYTES);
        char name[DM_NAME_MAX + 1];
        memcpy(name, d->name, DM_NAME_MAX);
        name[DM_NAME_MAX] = 0;
        if (!utf8_ok((const uint8_t *) name, (uint32_t) strlen(name))) strcpy(name, "(not UTF-8)");
        zxvd_buf_raw(r->out, i ? ",{\"id\":" : "{\"id\":", i ? 7 : 6);
        put_hex(r->out, d->id, DM_ID_BYTES);
        zxvd_buf_raw(r->out, ",\"name\":", 8);
        zxvd_buf_json_str(r->out, name);
        zxvd_buf_printf(r->out,
                        ",\"role\":\"%s\",\"admin\":%s,\"held\":%s,\"status\":\"%s\",\"self\":%s,"
                        "\"up\":%s,\"since\":\"%llu\"}",
                        ROLE_DM[d->role <= 3 ? d->role : 0],
                        (d->flags & DM_FLAG_ADMIN) ? "true" : "false",
                        (d->flags & DM_FLAG_HELD) ? "true" : "false",
                        d->status == DM_DEV_REVOKED ? "revoked" : "active", self ? "true" : "false",
                        !self && dm_peer_up(p->mesh, d->id, now) ? "true" : "false",
                        (unsigned long long) d->since);
    }
    zxvd_buf_raw(r->out, "]}", 2);
}

/* ===================== H8: ui.* ===================== */

static void put_ui(zxvd_people_t *p, zxvd_buf_t *o)
{
    zxvd_buf_printf(o, "{\"simple_mode\":%s}", p->simple ? "true" : "false");
}

static void m_ui_get(rq_t *r)
{
    put_ui(r->p, r->out);
}

static void m_ui_set(rq_t *r)
{
    zxvd_people_t *p = r->p;
    bool on;
    if (!has(r, "simple_mode")) return fail(r, E_PARAMS, "missing boolean parameter \"simple_mode\"");
    if (!p_bool(r, "simple_mode", false, &on)) return;
    if (on != p->simple) {
        char js[48];
        int n = snprintf(js, sizeof js, "{\"op\":\"ui\",\"simple\":%s}", on ? "true" : "false");
        if (!log_put(p, r, js, (size_t) n)) return;
        p->simple = on;
    }
    put_ui(p, r->out);
}

/* ===================== dispatch ===================== */

typedef struct {
    const char *name;
    void (*fn)(rq_t *);
} pm_t;

static const pm_t METHODS[] = {
    {"social.post", m_post},
    {"social.feed", m_feed},
    {"social.groups", m_groups},
    {"social.group_create", m_group_create},
    {"social.join", m_join},
    {"social.leave", m_leave},
    {"social.react", m_react},
    {"social.export", m_export},
    {"social.import", m_import},
    {"call.start", m_call_start},
    {"call.accept", m_call_accept},
    {"call.hangup", m_call_hangup},
    {"call.status", m_call_status},
    {"call.signal", m_call_signal},
    {"xlate.detect", m_detect},
    {"xlate.translate", m_translate},
    {"xlate.languages", m_languages},
    {"devmesh.status", m_dm_status},
    {"devmesh.invite", m_dm_invite},
    {"devmesh.join", m_dm_join},
    {"devmesh.confirm_sas", m_dm_confirm},
    {"devmesh.revoke", m_dm_revoke},
    {"devmesh.devices", m_dm_devices},
    {"ui.get", m_ui_get},
    {"ui.set", m_ui_set},
};

int zxvd_people_call(zxvd_people_t *p, const char *method, const char *params, size_t len,
                     zxvd_buf_t *out, char *msg, size_t msg_cap)
{
    static rq_t r;
    const pm_t *fn = NULL;
    for (size_t i = 0; i < sizeof METHODS / sizeof METHODS[0] && !fn; i++)
        if (!strcmp(method, METHODS[i].name)) fn = &METHODS[i];
    if (!p || !fn) {
        snprintf(msg, msg_cap, "method not found");
        return E_METHOD;
    }
    memset(&r, 0, sizeof r);
    r.p = p;
    r.js = params;
    r.out = out;
    r.msg = msg;
    r.cap = msg_cap;
    if (len > ZXVD_RPC_MAX_LINE) {
        snprintf(msg, msg_cap, "params too long");
        return E_PARAMS;
    }
    r.n = w4_json_parse(params, (uint32_t) len, r.t, TOKS, 8);
    if (r.n <= 0 || r.t[0].type != W4_J_OBJ) {
        snprintf(msg, msg_cap, "params must be an object (at most %d values, nested at most 8 deep)",
                 TOKS);
        return E_PARAMS;
    }
    size_t before = out->len;
    fn->fn(&r);
    if (r.code) {
        out->len = before;
        return r.code;
    }
    if (out->oom) {
        snprintf(msg, msg_cap, "out of memory");
        return E_PEOPLE;
    }
    return 0;
}

/* ===================== lifetime ===================== */

static int hexv(char c)
{
    return w4_hexval(c);
}

/* apply one replayed record */
static const char *replay_one(zxvd_people_t *p, const char *js, size_t n, bool first)
{
    static w4_jtok_t t[16];
    static uint8_t enc[SP_ENC_MAX];
    char op[16];
    int32_t k = w4_json_parse(js, (uint32_t) n, t, 16, 4);
    if (k <= 0 || t[0].type != W4_J_OBJ) return "a record is not JSON";
    int32_t o = w4_json_get(js, t, k, 0, "op");
    if (o < 0 || w4_json_str(js, &t[o], op, sizeof op) < 0) return "a record has no op";
    if (first != !strcmp(op, "people")) return "the first record is not the header";
    if (!strcmp(op, "people")) return NULL;
    if (!strcmp(op, "ui")) {
        int32_t v = w4_json_get(js, t, k, 0, "simple");
        bool b;
        if (v < 0 || w4_json_bool(&t[v], &b) != W4_OK) return "a ui record is malformed";
        p->simple = b;
        return NULL;
    }
    int32_t h = w4_json_get(js, t, k, 0, "hex");
    if (h < 0 || t[h].type != W4_J_STR) return "a record has no hex";
    const char *hs = js + t[h].start;
    uint32_t hl = t[h].end - t[h].start;
    if (!strcmp(op, "seed")) {
        uint8_t seed[SEED_BYTES];
        if (p->have_seed || hl != 2u * SEED_BYTES ||
            w4_hex_decode(hs, hl, seed, sizeof seed) != (int32_t) SEED_BYTES)
            return "a seed record is malformed or repeated";
        take_seed(p, seed);
        memset(seed, 0, sizeof seed);
        return NULL;
    }
    if (!strcmp(op, "rec")) {
        if (!p->have_seed) return "a social record comes before the seed";
        int32_t m = hl % 2u ? -1 : w4_hex_decode(hs, hl, enc, sizeof enc);
        sp_verifier_t v = verifier(p);
        if (m <= 0 || sp_decode(enc, (uint32_t) m, p->tmp, &v) != SP_OK)
            return "a social record does not verify";
        if (ss_put(&p->store, p->tmp) < 0) return "a social record breaks the store's rules";
        sp_clock_observe(&p->clock, p->tmp->lamport);
        return NULL;
    }
    return "unknown record";
}

static const char *log_load(zxvd_people_t *p)
{
    p->fd = open(p->path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (p->fd < 0) return "the people file could not be opened";
    struct stat st;
    if (fstat(p->fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid())
        return "the people file is not a regular file of ours";
    if (st.st_mode & (S_IRWXG | S_IRWXO)) return "the people file is readable by others (want 0600)";
    if (flock(p->fd, LOCK_EX | LOCK_NB) != 0) return "another zxvd has the people file open";
    if (st.st_size > (off_t) LOG_MAX) return "the people file is too large";
    size_t size = (size_t) st.st_size;
    char *all = malloc(size + 1);
    if (!all) return "out of memory";
    size_t got = 0;
    while (got < size) {
        ssize_t k = read(p->fd, all + got, size - got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        got += (size_t) k;
    }
    if (got != size) {
        free(all);
        return "the people file could not be read";
    }
    all[size] = 0;
    const char *why = NULL;
    size_t at = 0, whole = 0;
    p->replay = true;
    while (!why && at < size) {
        char *nl = memchr(all + at, '\n', size - at);
        if (!nl) break; /* torn last record: never answered */
        size_t n = (size_t) (nl - (all + at));
        if (n < 66 || n > LOG_LINE || all[at + 64] != ' ') {
            why = "a record is malformed";
            break;
        }
        uint8_t want[32], h[32];
        for (int i = 0; i < 32 && !why; i++) {
            int a = hexv(all[at + 2 * i]), b = hexv(all[at + 2 * i + 1]);
            if (a < 0 || b < 0) why = "a record's hash is not hex";
            want[i] = (uint8_t) (a << 4 | b);
        }
        if (why) break;
        chain(p->head, all + at + 65, n - 65, h);
        if (memcmp(h, want, 32)) {
            why = "the chain is broken (a record was edited, dropped or reordered)";
            break;
        }
        why = replay_one(p, all + at + 65, n - 65, p->recs == 0);
        memcpy(p->head, h, 32);
        p->recs++;
        at += n + 1;
        whole = at;
    }
    p->replay = false;
    free(all);
    if (why) return why;
    if (whole != size && ftruncate(p->fd, (off_t) whole) != 0) return "a torn record could not be cut";
    return NULL;
}

zxvd_people_t *zxvd_people_new(const char *path, const zxvd_people_host_t *host, char *why,
                               size_t why_cap)
{
    const char *err = NULL;
    zxvd_people_t *p = calloc(1, sizeof *p);
    if (!p) {
        snprintf(why, why_cap, "out of memory");
        return NULL;
    }
    p->fd = -1;
    if (host) p->host = *host;
    if (path && path[0]) {
        if (strlen(path) >= sizeof p->path) {
            free(p);
            snprintf(why, why_cap, "the people path is too long");
            return NULL;
        }
        snprintf(p->path, sizeof p->path, "%s", path);
        snprintf(p->mesh_path, sizeof p->mesh_path, "%s.mesh", path);
    }
    p->slots = calloc(ZXVD_PEOPLE_RECORDS, sizeof *p->slots);
    p->tmp = calloc(1, sizeof *p->tmp);
    p->index = calloc(IDX_CAP, sizeof *p->index);
    p->cand = calloc(ZXVD_PEOPLE_RECORDS, sizeof *p->cand);
    p->sorted = calloc(ZXVD_PEOPLE_RECORDS, sizeof *p->sorted);
    p->members = calloc(GROUPS_MAX, sizeof *p->members);
    p->work = calloc(ZXVD_PEOPLE_RECORDS, sizeof *p->work);
    p->picks = calloc(ZXVD_PEOPLE_FEED, sizeof *p->picks);
    p->xl = calloc(1, sizeof *p->xl);
    p->mesh = calloc(1, sizeof *p->mesh);
    uint64_t salt = 0;
    if (!p->slots || !p->tmp || !p->index || !p->cand || !p->sorted || !p->members || !p->work ||
        !p->picks || !p->xl || !p->mesh)
        err = "out of memory";
    else if (!os_random((uint8_t *) &salt, sizeof salt))
        err = "no random bytes from the OS";
    else if (!ss_init(&p->store, p->slots, ZXVD_PEOPLE_RECORDS, p->index, IDX_CAP, salt))
        err = "the record store could not be set up";
    if (!err && p->path[0]) err = log_load(p);
    if (!err && p->recs == 0) {
        static const char HDR[] = "{\"op\":\"people\",\"v\":1}";
        if (!log_put(p, NULL, HDR, sizeof HDR - 1)) err = "the people file could not be written";
    }
    if (!err && !p->have_seed) { /* H1: the first start */
        uint8_t seed[SEED_BYTES];
        char js[2 * SEED_BYTES + 32], hx[2 * SEED_BYTES + 1];
        if (!os_random(seed, sizeof seed)) {
            err = "no random bytes from the OS";
        } else {
            hex_of(seed, SEED_BYTES, hx);
            int n = snprintf(js, sizeof js, "{\"op\":\"seed\",\"hex\":\"%s\"}", hx);
            if (!log_put(p, NULL, js, (size_t) n))
                err = "the people file could not be written";
            else
                take_seed(p, seed);
        }
        memset(seed, 0, sizeof seed);
        memset(hx, 0, sizeof hx);
        memset(js, 0, sizeof js);
    }
    if (!err) {
        xlate_backend_t b = {xl_backend, p, "the local model", xlate_template_find("qwen2.5-instruct")};
        xlate_init(p->xl);
        xlate_set_backend(p->xl, &b);
        dm_host_t h;
        memset(&h, 0, sizeof h);
        h.ctx = p;
        h.random = mesh_random;
        h.send = mesh_send;
        h.on_event = mesh_event;
        if (dm_init(p->mesh, &h, PQM_LEVEL_HIGH, p->seed + 32, "This computer", DM_ROLE_HOME,
                    DM_FLAG_ADMIN) != DM_OK)
            err = "the device mesh identity could not be made";
    }
    if (!err && p->mesh_path[0]) { /* H3 */
        int fd = open(p->mesh_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd >= 0) {
            snap_hdr_t hd;
            dm_mesh_t *c = malloc(sizeof *c);
            uint8_t h[32];
            ssize_t a = read(fd, &hd, sizeof hd);
            size_t got = 0;
            while (c && a == (ssize_t) sizeof hd && got < sizeof *c) {
                ssize_t k = read(fd, (char *) c + got, sizeof *c - got);
                if (k <= 0) break;
                got += (size_t) k;
            }
            close(fd);
            if (!c)
                err = "out of memory";
            else if (a != (ssize_t) sizeof hd || memcmp(hd.magic, MESH_MAGIC, sizeof MESH_MAGIC) ||
                     hd.size != sizeof *c || got != sizeof *c)
                err = "the mesh snapshot is from another build or cut short";
            else if (sha256(( const uint8_t *) c, sizeof *c, h), memcmp(h, hd.hash, 32))
                err = "the mesh snapshot is damaged";
            else if (memcmp(c->self_id, p->mesh->self_id, DM_ID_BYTES))
                err = "the mesh snapshot belongs to another identity";
            if (!err) {
                dm_host_t keep = p->mesh->host;
                memcpy(p->mesh, c, sizeof *c);
                snap_scrub(p->mesh);
                p->mesh->host = keep;
            }
            if (c) {
                memset(c, 0, sizeof *c);
                free(c);
            }
        }
    }
    if (err) {
        snprintf(why, why_cap, "%s", err);
        zxvd_people_free(p);
        return NULL;
    }
    return p;
}

void zxvd_people_free(zxvd_people_t *p)
{
    if (!p) return;
    if (p->fd >= 0) close(p->fd);
    for (uint32_t i = 0; i < p->mout_n; i++) free(p->mout[i].b);
    if (p->mesh) {
        dm_wipe(p->mesh);
        free(p->mesh);
    }
    free(p->slots);
    free(p->tmp);
    free(p->index);
    free(p->cand);
    free(p->sorted);
    free(p->members);
    free(p->work);
    free(p->picks);
    free(p->xl);
    memset(p, 0, sizeof *p);
    free(p);
}

void zxvd_people_tick(zxvd_people_t *p, uint64_t now)
{
    if (!p) return;
    if (p->now_fixed) now = p->now_fixed;
    for (uint32_t i = 0; i < NLINES; i++) {
        call_state_t st = p->line[i].s.state;
        if (st != CALL_ST_IDLE && st != CALL_ST_ENDED) call_session_tick(&p->line[i].s, (uint32_t) now);
    }
    loop_pump(p);
    dm_tick(p->mesh, now);
    mesh_after(p);
}

void zxvd_people_set_now(zxvd_people_t *p, uint64_t now)
{
    p->now_fixed = now;
}

uint32_t zxvd_people_mesh_out(zxvd_people_t *p, uint8_t to[16], uint8_t *buf, uint32_t cap)
{
    if (!p || !p->mout_n) return 0;
    frame_t *f = &p->mout[0];
    uint32_t n = f->len;
    if (n > cap) return n;
    memcpy(to, f->to, DM_ID_BYTES);
    memcpy(buf, f->b, n);
    free(f->b);
    memmove(&p->mout[0], &p->mout[1], sizeof p->mout[0] * (MESH_OUT - 1u));
    p->mout_n--;
    return n;
}

int zxvd_people_mesh_in(zxvd_people_t *p, const uint8_t *frame, uint32_t len)
{
    if (!p || !frame) return DM_ERR_ARG;
    int st = dm_receive(p->mesh, frame, len, now_ms(p));
    mesh_after(p);
    return st;
}

/* ===================== the socket's instance ===================== */

static zxvd_people_t *G;

static int g_call(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                  size_t msg_cap)
{
    return zxvd_people_call(G, method, params, len, out, msg, msg_cap);
}

static const zxvd_people_ops_t GOPS = {g_call};

const zxvd_people_ops_t *zxvd_people_open(const char *books_path, const zxvd_people_host_t *host)
{
    char path[1024] = "", why[200];
    if (G) return &GOPS;
    if (books_path && books_path[0]) {
        const char *slash = strrchr(books_path, '/');
        int n = slash ? snprintf(path, sizeof path, "%.*s/people.zxvp", (int) (slash - books_path),
                                 books_path)
                      : snprintf(path, sizeof path, "people.zxvp");
        if (n < 0 || (size_t) n >= sizeof path) {
            fprintf(stderr, "zxvd: people: the books path is too long\n");
            return NULL;
        }
    }
    G = zxvd_people_new(path, host, why, sizeof why);
    if (!G) {
        fprintf(stderr, "zxvd: people: %s%s%s\n", why, path[0] ? " (" : "", path[0] ? path : "");
        if (path[0]) fprintf(stderr, "zxvd: people: move the file aside to start afresh\n");
        return NULL;
    }
    return &GOPS;
}

void zxvd_people_close(void)
{
    zxvd_people_free(G);
    G = NULL;
}

void zxvd_people_poll(uint64_t now)
{
    zxvd_people_tick(G, now);
}

bool zxvd_people_http(const char *body, size_t len, zxvd_buf_t *out)
{
    static const char *const ALLOW[] = {"swarm.ask", "ledger.accounts", "ledger.balance",
                                        "ledger.post"};
    static const char *const PREFIX[] = {"social.", "call.", "xlate.", "devmesh.", "ui."};
    static w4_jtok_t t[TOKS];
    char method[64];
    if (!body || len == 0 || len > ZXVD_RPC_MAX_LINE) return false;
    int32_t n = w4_json_parse(body, (uint32_t) len, t, TOKS, 8);
    if (n <= 0 || t[0].type != W4_J_OBJ) return false;
    int32_t m = w4_json_get(body, t, n, 0, "method");
    if (m < 0 || w4_json_str(body, &t[m], method, sizeof method) < 0) return false;
    bool ok = false;
    for (size_t i = 0; i < sizeof ALLOW / sizeof ALLOW[0] && !ok; i++) ok = !strcmp(method, ALLOW[i]);
    for (size_t i = 0; i < sizeof PREFIX / sizeof PREFIX[0] && !ok; i++)
        ok = !strncmp(method, PREFIX[i], strlen(PREFIX[i]));
    if (!ok) return false;
    out->len = 0;
    if (!zxvd_rpc_handle_line(body, len, out)) { /* a notification: answered with nothing */
        out->len = 0;
        zxvd_buf_raw(out, "", 0);
    }
    return true;
}

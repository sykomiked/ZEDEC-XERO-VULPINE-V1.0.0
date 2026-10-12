/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxvd_chat.c — the owner's social chat in zxvd (zxvd_chat.h C1-C8). */
#if !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#else
#    include <io.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_chat.h"
#include "zxvd_rpc.h"
#include "zxv_http_guard.h" /* zxv_os_random */
#include "social_post.h"
#include "social_store.h"
#include "social_feed.h"
#include "social_sign_mldsa.h"

#define LOG_MAGIC     "ZXSLOG1\n"
#define LOG_MAGIC_LEN 8u
#define FOLLOW_MAX    256u
#define CHILD_MAX     256u
#define REACT_KINDS   32u

typedef struct {
    uint8_t node[SP_NODEID_LEN];
    uint64_t lamport;
    uint8_t rec[SP_ID_LEN];
    bool on;
} follow_t;

static struct {
    bool open, saved, broken;
    uint8_t pk[SP_PK_LEN];
    uint8_t sk[SP_MLDSA_SK_LEN];
    uint8_t me[SP_NODEID_LEN];
    sp_clock_t clock;
    char log[1024];
    long log_size;
    follow_t follow[FOLLOW_MAX];
    uint32_t n_follow;
    uint64_t (*now_ms)(void);
} CH;

static sp_record_t SLOTS[ZXVD_CHAT_CAP];
static uint32_t INDEX[ZXVD_CHAT_CAP * 2u];
static ss_store_t STORE;
static sp_record_t TMP;
static uint8_t ENC[SP_ENC_MAX + 4u];
static sf_work_t WORK[ZXVD_CHAT_CAP];
static sf_pick_t PICKS[ZXVD_CHAT_FEED];
static const sp_record_t *CAND[ZXVD_CHAT_CAP];
static const sp_record_t *KIDS[CHILD_MAX];

static const sp_verifier_t VERIFY = {sp_mldsa_verify, NULL, NULL, NULL};

void zxvd_chat_set_clock(uint64_t (*now_ms)(void))
{
    CH.now_ms = now_ms;
}

/* ===================== text (C6) ===================== */

/* The length of the valid UTF-8 sequence at p, or 0. */
static uint32_t utf8_seq(const uint8_t *p, uint32_t left)
{
    uint8_t c = p[0];
    uint32_t n;
    uint32_t cp;
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) {
        n = 2;
        cp = c & 0x1Fu;
    } else if (c >= 0xE0 && c <= 0xEF) {
        n = 3;
        cp = c & 0x0Fu;
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4;
        cp = c & 0x07u;
    } else {
        return 0;
    }
    if (left < n) return 0;
    for (uint32_t i = 1; i < n; i++) {
        if ((p[i] & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    if ((n == 3 && cp < 0x800u) || (n == 4 && (cp < 0x10000u || cp > 0x10FFFFu)) ||
        (cp >= 0xD800u && cp <= 0xDFFFu))
        return 0;
    return n;
}

static bool utf8_ok(const uint8_t *p, uint32_t len)
{
    for (uint32_t i = 0; i < len;) {
        uint32_t k = utf8_seq(p + i, len - i);
        if (!k) return false;
        i += k;
    }
    return true;
}

/* A JSON string of len bytes; each byte that is not valid UTF-8 becomes
 * U+FFFD. */
static void put_text(zxvd_buf_t *b, const uint8_t *p, uint32_t len)
{
    zxvd_buf_raw(b, "\"", 1);
    for (uint32_t i = 0; i < len;) {
        uint32_t k = utf8_seq(p + i, len - i);
        uint8_t c = p[i];
        if (!k) {
            zxvd_buf_raw(b, "\xEF\xBF\xBD", 3);
            i++;
        } else if (c == '"' || c == '\\') {
            char e[2] = {'\\', (char) c};
            zxvd_buf_raw(b, e, 2);
            i++;
        } else if (c < 0x20 || c == 0x7F) {
            zxvd_buf_printf(b, "\\u%04x", c);
            i++;
        } else {
            zxvd_buf_raw(b, (const char *) p + i, k);
            i += k;
        }
    }
    zxvd_buf_raw(b, "\"", 1);
}

static void put_hex(zxvd_buf_t *b, const uint8_t *p)
{
    char h[2 * SP_ID_LEN + 3];
    static const char X[] = "0123456789abcdef";
    h[0] = '"';
    for (uint32_t i = 0; i < SP_ID_LEN; i++) {
        h[1 + 2 * i] = X[p[i] >> 4];
        h[2 + 2 * i] = X[p[i] & 15u];
    }
    h[2 * SP_ID_LEN + 1] = '"';
    zxvd_buf_raw(b, h, 2 * SP_ID_LEN + 2);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* A 64-hex-digit id parameter. */
static bool p_id(zxvd_call_t *c, const char *key, uint8_t out[SP_ID_LEN], bool required, bool *got)
{
    char s[2 * SP_ID_LEN + 2];
    *got = false;
    if (!zxvd_param_str(c, key, s, sizeof s, required)) return false;
    if (!s[0] && !required) return true;
    if (strlen(s) != 2 * SP_ID_LEN) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be 64 hex digits", key);
        return false;
    }
    for (uint32_t i = 0; i < SP_ID_LEN; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be 64 hex digits", key);
            return false;
        }
        out[i] = (uint8_t) (hi << 4 | lo);
    }
    *got = true;
    return true;
}

/* ===================== following (C7) ===================== */

static bool newer(uint64_t la, const uint8_t *ia, uint64_t lb, const uint8_t *ib)
{
    return la > lb || (la == lb && sp_cmp(ia, ib, SP_ID_LEN) < 0);
}

static follow_t *follow_of(const uint8_t node[SP_NODEID_LEN])
{
    for (uint32_t i = 0; i < CH.n_follow; i++)
        if (sp_eq(CH.follow[i].node, node, SP_NODEID_LEN)) return &CH.follow[i];
    return NULL;
}

/* Fold one of the owner's FOLLOW records into the table. */
static void follow_note(const sp_record_t *r)
{
    if (r->kind != SP_KIND_FOLLOW || !sp_eq(r->author, CH.me, SP_NODEID_LEN)) return;
    follow_t *f = follow_of(r->audience);
    if (!f) {
        if (CH.n_follow >= FOLLOW_MAX) return;
        f = &CH.follow[CH.n_follow++];
        memcpy(f->node, r->audience, SP_NODEID_LEN);
    } else if (!newer(r->lamport, r->id, f->lamport, f->rec)) {
        return;
    }
    f->lamport = r->lamport;
    memcpy(f->rec, r->id, SP_ID_LEN);
    f->on = r->body[0] == 1;
}

static bool following(const uint8_t node[SP_NODEID_LEN])
{
    const follow_t *f = follow_of(node);
    return f && f->on;
}

/* ===================== the log (C2) ===================== */

static int flush_file(FILE *f)
{
    if (fflush(f) != 0) return -1;
#if defined(_WIN32)
    return _commit(_fileno(f));
#else
    return fsync(fileno(f));
#endif
}

static int cut_file(const char *path, long size)
{
#if defined(_WIN32)
    int fd = _open(path, _O_RDWR | _O_BINARY);
    if (fd < 0) return -1;
    int rc = _chsize(fd, size);
    _close(fd);
    return rc;
#else
    return truncate(path, (off_t) size);
#endif
}

/* Append one encoded record. 0, or -1 (the file is cut back, best effort). */
static int log_append(const uint8_t *enc, uint32_t len)
{
    FILE *f = fopen(CH.log, "ab");
    if (!f) return -1;
    uint8_t hdr[4] = {(uint8_t) (len >> 24), (uint8_t) (len >> 16), (uint8_t) (len >> 8),
                      (uint8_t) len};
    int rc = fwrite(hdr, 1, 4, f) == 4 && fwrite(enc, 1, len, f) == len ? flush_file(f) : -1;
    if (fclose(f) != 0) rc = -1;
    if (rc != 0) {
        cut_file(CH.log, CH.log_size);
        return -1;
    }
    CH.log_size += 4 + (long) len;
    return 0;
}

static void hold(const sp_record_t *r)
{
    ss_put(&STORE, r);
    sp_clock_observe(&CH.clock, r->lamport);
    follow_note(r);
}

/* Read the log, or create it. 0, or -1 with the reason on stderr. */
static int log_load(void)
{
    FILE *f = fopen(CH.log, "rb");
    if (!f) {
        f = fopen(CH.log, "wb");
        if (!f || fwrite(LOG_MAGIC, 1, LOG_MAGIC_LEN, f) != LOG_MAGIC_LEN || flush_file(f) != 0) {
            if (f) fclose(f);
            fprintf(stderr, "zxvd: the chat log %s could not be created\n", CH.log);
            return -1;
        }
        fclose(f);
        CH.log_size = LOG_MAGIC_LEN;
        return 0;
    }
    uint8_t *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            size_t nc = cap ? cap * 2 : 65536;
            uint8_t *nb = realloc(buf, nc);
            if (!nb) break;
            buf = nb;
            cap = nc;
        }
        size_t k = fread(buf + len, 1, cap - len, f);
        if (k == 0) break;
        len += k;
    }
    bool err = ferror(f) != 0 || (len == cap && !feof(f));
    fclose(f);
    const char *why = NULL;
    size_t at = LOG_MAGIC_LEN;
    if (err)
        why = "could not be read";
    else if (len < LOG_MAGIC_LEN || memcmp(buf, LOG_MAGIC, LOG_MAGIC_LEN) != 0)
        why = "is not a chat log";
    while (!why && at < len) {
        if (len - at < 4) break; /* a torn length */
        uint32_t n = (uint32_t) buf[at] << 24 | (uint32_t) buf[at + 1] << 16 |
                     (uint32_t) buf[at + 2] << 8 | buf[at + 3];
        if (n == 0 || n > SP_ENC_MAX) {
            why = "has a damaged record";
            break;
        }
        if (len - at - 4 < n) break; /* a torn record */
        if (sp_decode(buf + at + 4, n, &TMP, &VERIFY) != SP_OK) {
            why = "has a record that does not verify";
            break;
        }
        hold(&TMP);
        at += 4 + n;
    }
    free(buf);
    if (why) {
        fprintf(stderr, "zxvd: the chat log %s %s (at byte %zu); zxvd will not start over it\n",
                CH.log, why, at);
        return -1;
    }
    if (at < len) {
        if (cut_file(CH.log, (long) at) != 0) {
            fprintf(stderr, "zxvd: the chat log %s ends in a cut-off record that could not be "
                            "removed\n",
                    CH.log);
            return -1;
        }
        fprintf(stderr, "zxvd: removed a cut-off record at the end of the chat log %s\n", CH.log);
    }
    CH.log_size = (long) at;
    return 0;
}

/* The seed file: read 32 bytes, or create it. 0 or -1. */
static int seed_load(const char *path, uint8_t seed[32])
{
    FILE *f = fopen(path, "rb");
    if (f) {
        uint8_t extra;
        size_t k = fread(seed, 1, 32, f);
        bool more = fread(&extra, 1, 1, f) == 1;
        fclose(f);
        if (k != 32 || more) {
            fprintf(stderr, "zxvd: the chat key file %s is not 32 bytes\n", path);
            return -1;
        }
        return 0;
    }
    if (zxv_os_random(seed, 32) != 0) {
        fprintf(stderr, "zxvd: no OS random source for the chat key\n");
        return -1;
    }
#if defined(_WIN32)
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
    f = fd < 0 ? NULL : _fdopen(fd, "wb");
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    f = fd < 0 ? NULL : fdopen(fd, "wb");
#endif
    if (!f || fwrite(seed, 1, 32, f) != 32 || flush_file(f) != 0) {
        if (f) fclose(f);
        fprintf(stderr, "zxvd: the chat key file %s could not be created\n", path);
        return -1;
    }
    fclose(f);
    return 0;
}

/* ===================== records ===================== */

static bool sign_cb(void *ctx, const uint8_t id[SP_ID_LEN], uint8_t sig[SP_SIG_LEN])
{
    (void) ctx;
    uint8_t rnd[32];
    sp_mldsa_key_t k = {CH.sk, zxv_os_random(rnd, sizeof rnd) == 0 ? rnd : NULL};
    return sp_mldsa_sign(&k, id, sig);
}

/* Sign, save (C2) and hold a record the owner made in TMP. */
static bool make(zxvd_call_t *c)
{
    if (CH.broken) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "the chat log could not be written earlier: chat is "
                                      "read-only until zxvd restarts");
        return false;
    }
    sp_attach_pk(&TMP, CH.pk);
    if (CH.now_ms) sp_set_wall(&TMP, CH.now_ms());
    sp_signer_t s = {sign_cb, NULL};
    int32_t n;
    if (sp_sign(&TMP, &s) != SP_OK || (n = sp_encode(&TMP, ENC, sizeof ENC)) <= 0) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "the record could not be signed");
        return false;
    }
    if (CH.saved && log_append(ENC, (uint32_t) n) != 0) {
        CH.broken = true;
        zxvd_fail(c, ZXVD_E_INTERNAL, "the chat log could not be written: this record is not "
                                      "kept and chat is read-only until zxvd restarts");
        return false;
    }
    hold(&TMP);
    return true;
}

static const char *kind_name(uint8_t k)
{
    return k == SP_KIND_POST ? "post" : k == SP_KIND_REPLY ? "reply" : "reaction";
}

/* C5: one record with its replies and reactions. */
static void put_record(zxvd_buf_t *b, const sp_record_t *r)
{
    zxvd_buf_raw(b, "{\"id\":", 6);
    put_hex(b, r->id);
    zxvd_buf_raw(b, ",\"author\":", 10);
    put_hex(b, r->author);
    zxvd_buf_printf(b, ",\"mine\":%s,\"kind\":\"%s\",\"lamport\":\"%llu\",\"wall_ms\":\"%llu\",",
                    sp_eq(r->author, CH.me, SP_NODEID_LEN) ? "true" : "false", kind_name(r->kind),
                    (unsigned long long) r->lamport,
                    (unsigned long long) (r->has_wall ? r->wall_ms : 0));
    zxvd_buf_raw(b, "\"text\":", 7);
    put_text(b, r->body, r->body_len);
    zxvd_buf_raw(b, ",\"reply_to\":", 12);
    if (r->has_parent)
        put_hex(b, r->parent);
    else
        zxvd_buf_raw(b, "null", 4);
    uint32_t n = ss_children(&STORE, r->id, KIDS, CHILD_MAX), replies = 0;
    struct {
        const uint8_t *e;
        uint16_t len;
        uint32_t count;
        bool mine;
    } re[REACT_KINDS];
    uint32_t nr = 0;
    for (uint32_t i = 0; i < n; i++) {
        const sp_record_t *k = KIDS[i];
        if (k->kind == SP_KIND_REPLY) {
            replies++;
            continue;
        }
        if (k->kind != SP_KIND_REACTION) continue;
        uint32_t j = 0;
        while (j < nr && !(re[j].len == k->body_len && sp_eq(re[j].e, k->body, k->body_len)))
            j++;
        if (j == nr) {
            if (nr == REACT_KINDS) continue;
            re[nr].e = k->body;
            re[nr].len = k->body_len;
            re[nr].count = 0;
            re[nr].mine = false;
            nr++;
        }
        re[j].count++;
        if (sp_eq(k->author, CH.me, SP_NODEID_LEN)) re[j].mine = true;
    }
    zxvd_buf_printf(b, ",\"replies\":%u,\"reactions\":[", (unsigned) replies);
    for (uint32_t j = 0; j < nr; j++) {
        zxvd_buf_raw(b, j ? ",{\"emoji\":" : "{\"emoji\":", j ? 10 : 9);
        put_text(b, re[j].e, re[j].len);
        zxvd_buf_printf(b, ",\"count\":%u,\"mine\":%s}", (unsigned) re[j].count,
                        re[j].mine ? "true" : "false");
    }
    zxvd_buf_raw(b, "]}", 2);
}

/* ===================== methods (C5) ===================== */

static void m_me(zxvd_call_t *c)
{
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"node_id\":", 11);
    put_hex(b, CH.me);
    zxvd_buf_printf(b, ",\"saved\":%s,\"records\":%u,\"following\":[",
                    CH.saved ? "true" : "false", (unsigned) ss_count(&STORE));
    const char *sep = "";
    for (uint32_t i = 0; i < CH.n_follow; i++) {
        if (!CH.follow[i].on) continue;
        zxvd_buf_raw(b, sep, strlen(sep));
        put_hex(b, CH.follow[i].node);
        sep = ",";
    }
    zxvd_buf_raw(b, "]}", 2);
}

static bool has_ink(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') return true;
    return false;
}

static void m_post(zxvd_call_t *c)
{
    char text[SP_BODY_MAX + 1];
    uint8_t parent[SP_ID_LEN];
    bool reply;
    if (!zxvd_param_str(c, "text", text, sizeof text, true) ||
        !p_id(c, "reply_to", parent, false, &reply))
        return;
    uint32_t len = (uint32_t) strlen(text);
    if (!has_ink(text) || !utf8_ok((const uint8_t *) text, len))
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"text\" must be UTF-8 with something besides spaces");
    if (reply && !ss_get(&STORE, parent))
        return zxvd_fail(c, ZXVD_E_PARAMS, "no record with that \"reply_to\" id is held");
    sp_init(&TMP, reply ? SP_KIND_REPLY : SP_KIND_POST, SP_VIS_PUBLIC, CH.me,
            sp_clock_next(&CH.clock));
    sp_set_body(&TMP, (const uint8_t *) text, len);
    if (reply) sp_set_parent(&TMP, parent);
    if (make(c)) put_record(zxvd_result(c), ss_get(&STORE, TMP.id));
}

static void m_react(zxvd_call_t *c)
{
    char emoji[SP_REACT_MAX + 1];
    uint8_t to[SP_ID_LEN];
    bool got;
    if (!p_id(c, "to", to, true, &got) || !zxvd_param_str(c, "emoji", emoji, sizeof emoji, true))
        return;
    uint32_t len = (uint32_t) strlen(emoji);
    if (!len || !utf8_ok((const uint8_t *) emoji, len))
        return zxvd_fail(c, ZXVD_E_PARAMS, "\"emoji\" must be 1 to %u bytes of UTF-8",
                         (unsigned) SP_REACT_MAX);
    if (!ss_get(&STORE, to)) return zxvd_fail(c, ZXVD_E_PARAMS, "no record with that \"to\" id is held");
    sp_init(&TMP, SP_KIND_REACTION, SP_VIS_PUBLIC, CH.me, sp_clock_next(&CH.clock));
    sp_set_body(&TMP, (const uint8_t *) emoji, len);
    sp_set_parent(&TMP, to);
    if (make(c)) put_record(zxvd_result(c), ss_get(&STORE, TMP.id));
}

static void m_follow(zxvd_call_t *c)
{
    uint8_t node[SP_NODEID_LEN];
    bool got, on = true;
    if (!p_id(c, "node_id", node, true, &got) || !zxvd_param_bool(c, "follow", &on, false)) return;
    if (sp_eq(node, CH.me, SP_NODEID_LEN))
        return zxvd_fail(c, ZXVD_E_PARAMS, "you cannot follow yourself");
    if (following(node) != on) {
        uint8_t v = on ? 1 : 0;
        if (!follow_of(node) && CH.n_follow >= FOLLOW_MAX)
            return zxvd_fail(c, ZXVD_E_PARAMS, "at most %u people can be followed",
                             (unsigned) FOLLOW_MAX);
        sp_init(&TMP, SP_KIND_FOLLOW, SP_VIS_PUBLIC, CH.me, sp_clock_next(&CH.clock));
        sp_set_body(&TMP, &v, 1);
        sp_set_audience(&TMP, node);
        if (!make(c)) return;
    }
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"node_id\":", 11);
    put_hex(b, node);
    zxvd_buf_printf(b, ",\"following\":%s}", on ? "true" : "false");
}

static void m_feed(zxvd_call_t *c)
{
    uint32_t n = 0, total = ss_count(&STORE);
    for (uint32_t i = 0; i < total; i++) {
        const sp_record_t *r = ss_at(&STORE, i);
        if (r->kind == SP_KIND_POST) CAND[n++] = r;
    }
    sf_config_t cfg;
    sf_seen_t seen;
    sf_config_default(&cfg, CH.clock.t);
    sf_seen_init(&seen);
    uint32_t k = sf_rank(&cfg, &seen, CAND, n, WORK, PICKS, ZXVD_CHAT_FEED);
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"posts\":[", 10);
    for (uint32_t i = 0; i < k; i++) {
        if (i) zxvd_buf_raw(b, ",", 1);
        put_record(b, CAND[PICKS[i].index]);
    }
    zxvd_buf_raw(b, "]}", 2);
}

static void m_thread(zxvd_call_t *c)
{
    uint8_t id[SP_ID_LEN];
    bool got;
    if (!p_id(c, "id", id, true, &got)) return;
    const sp_record_t *r = ss_get(&STORE, id);
    if (!r) return zxvd_fail(c, ZXVD_E_PARAMS, "no record with that \"id\" is held");
    zxvd_buf_t *b = zxvd_result(c);
    zxvd_buf_raw(b, "{\"root\":", 8);
    put_record(b, r);
    zxvd_buf_raw(b, ",\"replies\":[", 12);
    static const sp_record_t *kids[CHILD_MAX];
    uint32_t n = ss_children(&STORE, id, kids, CHILD_MAX);
    const char *sep = "";
    for (uint32_t i = 0; i < n; i++) {
        if (kids[i]->kind != SP_KIND_REPLY) continue;
        zxvd_buf_raw(b, sep, strlen(sep));
        put_record(b, kids[i]);
        sep = ",";
    }
    zxvd_buf_raw(b, "]}", 2);
}

static const zxvd_method_t METHODS[] = {
    {"chat.me", m_me},         {"chat.post", m_post}, {"chat.react", m_react},
    {"chat.follow", m_follow}, {"chat.feed", m_feed}, {"chat.thread", m_thread},
};

/* ===================== open and close ===================== */

int zxvd_chat_open(const char *prefix)
{
    zxvd_chat_close();
    uint64_t salt;
    uint8_t seed[32];
    if (zxv_os_random((uint8_t *) &salt, sizeof salt) != 0) {
        fprintf(stderr, "zxvd: no OS random source for the chat\n");
        return -1;
    }
    ss_init(&STORE, SLOTS, ZXVD_CHAT_CAP, INDEX, ZXVD_CHAT_CAP * 2u, salt);
    CH.saved = prefix && prefix[0];
    if (CH.saved) {
        char seedp[1024];
        if ((size_t) snprintf(seedp, sizeof seedp, "%s.seed", prefix) >= sizeof seedp ||
            (size_t) snprintf(CH.log, sizeof CH.log, "%s.log", prefix) >= sizeof CH.log) {
            fprintf(stderr, "zxvd: the chat path is too long\n");
            return -1;
        }
        if (seed_load(seedp, seed) != 0) return -1;
    } else if (zxv_os_random(seed, sizeof seed) != 0) {
        fprintf(stderr, "zxvd: no OS random source for the chat key\n");
        return -1;
    }
    sp_mldsa_keypair(seed, CH.pk, CH.sk, CH.me);
    memset(seed, 0, sizeof seed);
    if (CH.saved && log_load() != 0) {
        zxvd_chat_close();
        return -1;
    }
    CH.open = true;
    zxvd_rpc_add_methods(METHODS, sizeof METHODS / sizeof METHODS[0]);
    return 0;
}

void zxvd_chat_close(void)
{
    if (CH.open) zxvd_rpc_add_methods(NULL, 0);
    uint64_t (*now)(void) = CH.now_ms;
    memset(&CH, 0, sizeof CH);
    CH.now_ms = now;
    memset(&STORE, 0, sizeof STORE);
}

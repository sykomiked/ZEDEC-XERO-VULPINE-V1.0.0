/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quarry_model.c — Quarry (see quarry_model.h). */
#include <string.h>

#include "quarry_model.h"

const zxm_ai_action_t QUARRY_AI[QUARRY_AI_COUNT] = {
    {"Summarize the sources", "Companion", 900},
};

#define QUARRY_STAMP_MAX INT64_C(999999999999) /* QR3: 12 digits */
#define QUARRY_INSTR_MAX 512u

void quarry_init(quarry_t *q, zxm_link_t *link)
{
    memset(q, 0, sizeof *q);
    q->link = link;
    q->next_num = 1;
}

/* ---- text ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < cap && s[n]) n++;
    return n;
}

/* n bytes of well-formed UTF-8 with no control byte */
static bool text_ok(const char *s, uint32_t n)
{
    const unsigned char *p = (const unsigned char *) s;
    uint32_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        uint32_t more;
        if (c < 0x20u || c == 0x7fu) return false;
        if (c < 0x80u) {
            i++;
            continue;
        }
        if (c >= 0xc2u && c <= 0xdfu)
            more = 1;
        else if (c >= 0xe0u && c <= 0xefu)
            more = 2;
        else if (c >= 0xf0u && c <= 0xf4u)
            more = 3;
        else
            return false;
        if (more > n - 1u - i) return false; /* cut short */
        for (uint32_t k = 1; k <= more; k++)
            if ((p[i + k] & 0xc0u) != 0x80u) return false;
        if (c == 0xe0u && p[i + 1] < 0xa0u) return false; /* overlong */
        if (c == 0xedu && p[i + 1] > 0x9fu) return false; /* a surrogate */
        if (c == 0xf0u && p[i + 1] < 0x90u) return false; /* overlong */
        if (c == 0xf4u && p[i + 1] > 0x8fu) return false; /* past U+10FFFF */
        i += more + 1u;
    }
    return true;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s, uint32_t n)
{
    if (*at + n >= cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_s(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    return put(out, cap, at, s, (uint32_t) strlen(s));
}

static bool put_u64(char *out, uint32_t cap, uint32_t *at, uint64_t v)
{
    char d[20];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    char r[20];
    for (uint32_t i = 0; i < n; i++) r[i] = d[n - 1u - i];
    return put(out, cap, at, r, n);
}

static bool is_space(char c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

/* ---- QR4 over the held citations ---- */

static int64_t count_column(const quarry_t *q, bool topics, int64_t want)
{
    static int64_t col[QUARRY_CITES];
    for (uint32_t i = 0; i < q->ncite; i++) col[i] = topics ? q->cite[i].topic : q->cite[i].num;
    return quarry_count(col, q->ncite, want);
}

int32_t quarry_find(const quarry_t *q, int64_t num)
{
    for (uint32_t i = 0; i < q->ncite; i++)
        if (q->cite[i].num == num) return (int32_t) i;
    return -1;
}

bool quarry_claim_sourced(const quarry_t *q, int64_t cite)
{
    return quarry_sourced(cite, count_column(q, false, cite));
}

int64_t quarry_topic_sources(const quarry_t *q, int64_t topic)
{
    return count_column(q, true, topic);
}

/* ---- QM1: topics and the Hoard ---- */

int quarry_search(quarry_t *q, const char *query)
{
    static quarry_hit_t got[QUARRY_HITS];
    uint32_t n = len_of(query, QUARRY_QUERY_MAX + 1u), kept = 0, t = 0;
    bool word = false;
    if (!query || !n || n > QUARRY_QUERY_MAX || !text_ok(query, n)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n && !word; i++) word = query[i] != ' ';
    if (!word) return ZXM_E_ARG;
    while (t < q->ntopics && strcmp(q->topic[t], query)) t++;
    if (t == q->ntopics && q->ntopics == QUARRY_TOPICS) return ZXM_E_STATE;
    zxm_link_t *l = q->link;
    zxm_begin(l, "search.query");
    zxm_p_str(l, "q", query);
    zxm_p_u64(l, "max", QUARRY_HITS);
    int rc = zxm_call(l);
    if (rc) return rc;
    int64_t total = 0;
    int32_t arr = zxm_get(l, l->result, "hits");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > QUARRY_HITS ||
        zxm_get_i64(l, l->result, "total", &total) || total < 0)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        quarry_hit_t *h = &got[kept];
        int32_t o = zxm_at(l, arr, i);
        memset(h, 0, sizeof *h);
        if (o < 0 || l->tok[o].type != W4_J_OBJ || zxm_get_i64(l, o, "size", &h->size) ||
            zxm_get_i64(l, o, "mtime", &h->mtime) || zxm_get_i64(l, o, "score", &h->score) ||
            h->size < 0 || h->score < 0)
            return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "path", h->path, sizeof h->path)) continue; /* too long to cite */
        if (zxm_get_str(l, o, "snippet", h->snippet, sizeof h->snippet)) h->snippet[0] = 0;
        kept++;
    }
    if (t == q->ntopics) {
        memcpy(q->topic[t], query, n);
        q->topic[t][n] = 0;
        q->ntopics++;
    }
    q->current = (int64_t) t + 1;
    memcpy(q->hit, got, kept * sizeof got[0]);
    q->nhits = kept;
    q->total = total;
    return ZXM_OK;
}

/* ---- QM2: the Web4 feed ---- */

static bool is_null(const zxm_link_t *l, int32_t o, const char *key)
{
    int32_t t = zxm_get(l, o, key);
    return t >= 0 && l->tok[t].type == W4_J_NULL;
}

/* the head of a string member, cut on a character boundary */
static int head_of(const zxm_link_t *l, int32_t o, const char *key, char *out, uint32_t cap)
{
    static char whole[ZXM_RESP_MAX + 1];
    int32_t t = zxm_get(l, o, key);
    if (t < 0 || l->tok[t].type != W4_J_STR) return ZXM_E_PROTOCOL;
    int32_t n = w4_json_str(l->resp, &l->tok[t], whole, sizeof whole);
    if (n < 0) return ZXM_E_PROTOCOL;
    uint32_t k = zxm_utf8_cut(whole, (uint32_t) n < cap - 1u ? (uint32_t) n : cap - 1u);
    memcpy(out, whole, k);
    out[k] = 0;
    return ZXM_OK;
}

int quarry_feed(quarry_t *q, uint32_t limit)
{
    static quarry_env_t got[QUARRY_ENVS];
    bool open = false;
    uint32_t kept = 0;
    if (limit < 1u || limit > QUARRY_ENVS) return ZXM_E_ARG;
    zxm_link_t *l = q->link;
    zxm_begin(l, "web4.feed");
    zxm_p_u64(l, "limit", limit);
    zxm_p_str(l, "status", "signed");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "envelopes");
    if (zxm_get_bool(l, l->result, "open", &open) || arr < 0 || l->tok[arr].type != W4_J_ARR ||
        zxm_len(l, arr) > limit || (!open && zxm_len(l, arr)))
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        quarry_env_t *e = &got[kept];
        char kind[16], status[16];
        uint64_t id = 0, ts = 0;
        int32_t o = zxm_at(l, arr, i);
        memset(e, 0, sizeof *e);
        if (o < 0 || zxm_get_u64(l, o, "id", &id) || id < 1u || id > UINT32_MAX ||
            zxm_get_str(l, o, "kind", kind, sizeof kind) ||
            zxm_get_str(l, o, "status", status, sizeof status) ||
            zxm_get_bool(l, o, "pii", &e->pii) || head_of(l, o, "payload", e->text, sizeof e->text))
            return ZXM_E_PROTOCOL;
        if (strcmp(kind, "REQUEST") && strcmp(kind, "RESPONSE") && strcmp(kind, "STREAM") &&
            strcmp(kind, "CONSENT"))
            return ZXM_E_PROTOCOL;
        if (zxm_get(l, o, "ts") >= 0 && !is_null(l, o, "ts") &&
            (zxm_get_u64(l, o, "ts", &ts) || ts > (uint64_t) QUARRY_STAMP_MAX))
            return ZXM_E_PROTOCOL;
        if (strcmp(status, "signed") || !strcmp(kind, "CONSENT") || !e->text[0])
            continue; /* QM2: no source */
        e->id = (int64_t) id;
        e->stamp = (int64_t) ts;
        kept++;
    }
    memcpy(q->env, got, kept * sizeof got[0]);
    q->nenv = kept;
    q->feed_open = open;
    return ZXM_OK;
}

/* ---- QM3: citing ---- */

/* the number already given to ref under the current topic, or 0 */
static int64_t cited(const quarry_t *q, const char *ref)
{
    for (uint32_t i = 0; i < q->ncite; i++)
        if (q->cite[i].topic == q->current && !strcmp(q->cite[i].ref, ref)) return q->cite[i].num;
    return 0;
}

/* the checks before anything is read: a topic, room, a number, a record */
static int may_cite(const quarry_t *q, int64_t kind, int64_t stamp)
{
    if (!q->current) return ZXM_E_STATE;
    if (q->ncite == QUARRY_CITES || q->next_num > QUARRY_CITE_MAX) return ZXM_E_STATE;
    if (stamp < 0 ||
        !quarry_record_ok(q->next_num, q->current, kind, stamp, (int64_t) QUARRY_QUOTE))
        return ZXM_E_ARG;
    return ZXM_OK;
}

static void hold(quarry_t *q, int64_t kind, int64_t stamp, const char *ref, const char *quote,
                 int64_t *num)
{
    quarry_cite_t *c = &q->cite[q->ncite++];
    uint32_t rn = len_of(ref, QUARRY_REF_MAX - 1u), qn = len_of(quote, QUARRY_QUOTE);
    memset(c, 0, sizeof *c);
    c->num = q->next_num++;
    c->topic = q->current;
    c->kind = kind;
    c->stamp = stamp;
    memcpy(c->ref, ref, rn);
    memcpy(c->quote, quote, qn);
    c->qlen = qn;
    *num = c->num;
}

int quarry_cite_hit(quarry_t *q, uint32_t hit, int64_t *num)
{
    static char quote[QUARRY_QUOTE + 1];
    if (!num || hit >= q->nhits) return ZXM_E_ARG;
    const quarry_hit_t *h = &q->hit[hit];
    int64_t was = cited(q, h->path);
    if (was) {
        *num = was;
        return ZXM_OK;
    }
    int rc = may_cite(q, QUARRY_KIND_FILE, h->mtime);
    if (rc) return rc;
    zxm_link_t *l = q->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", h->path);
    zxm_p_u64(l, "max", QUARRY_QUOTE);
    rc = zxm_call(l);
    if (rc) return rc;
    if (head_of(l, l->result, "text", quote, sizeof quote)) return ZXM_E_PROTOCOL;
    if (!quarry_record_ok(q->next_num, q->current, QUARRY_KIND_FILE, h->mtime,
                          (int64_t) len_of(quote, sizeof quote)))
        return ZXM_E_ARG;
    hold(q, QUARRY_KIND_FILE, h->mtime, h->path, quote, num);
    return ZXM_OK;
}

int quarry_cite_env(quarry_t *q, uint32_t env, int64_t *num)
{
    char ref[QUARRY_REF_MAX];
    uint32_t at = 0;
    if (!num || env >= q->nenv) return ZXM_E_ARG;
    const quarry_env_t *e = &q->env[env];
    if (e->pii) return ZXM_E_STATE; /* QM3: personal data is never cited */
    ref[0] = 0;
    if (!put(ref, sizeof ref, &at, "web4:", 5u) || !put_u64(ref, sizeof ref, &at, (uint64_t) e->id))
        return ZXM_E_ARG;
    int64_t was = cited(q, ref);
    if (was) {
        *num = was;
        return ZXM_OK;
    }
    int rc = may_cite(q, QUARRY_KIND_WEB4, e->stamp);
    if (rc) return rc;
    hold(q, QUARRY_KIND_WEB4, e->stamp, ref, e->text, num);
    return ZXM_OK;
}

int quarry_drop(quarry_t *q, int64_t num, bool confirmed)
{
    int32_t at = quarry_find(q, num);
    if (at < 0) return ZXM_E_ARG;
    if (!confirmed) return ZXM_E_STATE; /* only on the owner's word */
    for (uint32_t i = (uint32_t) at; i + 1u < q->ncite; i++) q->cite[i] = q->cite[i + 1u];
    q->ncite--;
    (void) quarry_unsourced(q);
    return ZXM_OK;
}

/* ---- QM4: claims ---- */

int quarry_claim(quarry_t *q, const char *text, int64_t cite, uint32_t *idx)
{
    uint32_t n = len_of(text, QUARRY_CLAIM_MAX + 1u);
    if (!text || !idx || !n || n > QUARRY_CLAIM_MAX || !text_ok(text, n) || cite < 0)
        return ZXM_E_ARG;
    if (q->nclaims == QUARRY_CLAIMS) return ZXM_E_STATE;
    quarry_claim_t *c = &q->claim[q->nclaims];
    memset(c, 0, sizeof *c);
    memcpy(c->text, text, n);
    c->cite = cite;
    c->sourced = quarry_claim_sourced(q, cite);
    *idx = q->nclaims++;
    return ZXM_OK;
}

uint32_t quarry_unsourced(quarry_t *q)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < q->nclaims; i++) {
        q->claim[i].sourced = quarry_claim_sourced(q, q->claim[i].cite);
        k += !q->claim[i].sourced;
    }
    return k;
}

/* ---- QM6: the summary ---- */

/* "[n] where\nexcerpt\n\n" in at most `share` bytes */
static void lay_out(const quarry_cite_t *c, char *in, uint32_t cap, uint32_t *at, uint32_t share)
{
    char head[8];
    uint32_t hn = 0, start = *at;
    head[0] = 0;
    if (!put(head, sizeof head, &hn, "[", 1u) ||
        !put_u64(head, sizeof head, &hn, (uint64_t) c->num) ||
        !put(head, sizeof head, &hn, "] ", 2u) || hn + 4u > share)
        return;
    (void) put(in, cap, at, head, hn);
    uint32_t rn = len_of(c->ref, QUARRY_REF_MAX), room = share - hn - 3u;
    uint32_t rk = zxm_utf8_cut(c->ref, rn < room / 2u ? rn : room / 2u);
    (void) put(in, cap, at, c->ref, rk);
    (void) put(in, cap, at, "\n", 1u);
    room = share - (*at - start) - 2u;
    uint32_t qk = zxm_utf8_cut(c->quote, c->qlen < room ? c->qlen : room);
    (void) put(in, cap, at, c->quote, qk);
    (void) put(in, cap, at, "\n\n", 2u);
}

/* the bracketed numbers and the words of the held answer */
static void read_answer(quarry_t *q)
{
    const char *s = q->summary.text;
    uint32_t n = q->summary.len;
    q->marks = q->unheld = q->answer_words = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!is_space(s[i]) && (i == 0 || is_space(s[i - 1u]))) q->answer_words++;
        if (s[i] != '[') continue;
        uint32_t j = i + 1u;
        int64_t v = 0;
        while (j < n && j < i + 5u && s[j] >= '0' && s[j] <= '9') v = v * 10 + (s[j++] - '0');
        if (j == i + 1u || j >= n || s[j] != ']') continue;
        q->marks++;
        int32_t at = quarry_find(q, v);
        if (!quarry_claim_sourced(q, v) || at < 0 || q->cite[at].topic != q->summary_topic)
            q->unheld++;
    }
    q->long_answer = (int64_t) q->answer_words > q->summary_words;
}

int quarry_summarize(quarry_t *q, int64_t words)
{
    static char instr[QUARRY_INSTR_MAX], in[ZXM_ASK_MAX + 1];
    static zxm_held_t got;
    uint32_t at = 0;
    if (!q->current) return ZXM_E_STATE;
    int64_t m = quarry_topic_sources(q, q->current);
    if (!quarry_length_ok(words, m)) return ZXM_E_ARG; /* QR2, before anything is sent */
    const char *topic = q->topic[q->current - 1];
    instr[0] = 0;
    if (!put_s(instr, sizeof instr, &at, "Summarize these sources on \"") ||
        !put_s(instr, sizeof instr, &at, topic) ||
        !put_s(instr, sizeof instr, &at, "\" in at most ") ||
        !put_u64(instr, sizeof instr, &at, (uint64_t) words) ||
        !put_s(instr, sizeof instr, &at,
               " words. Cite each claim with the number of its source in brackets, such as [1], "
               "and use no other source."))
        return ZXM_E_ARG;
    uint32_t cap = ZXM_ASK_MAX - at - 2u, used = 0, sources = (uint32_t) m, done = 0;
    in[0] = 0;
    for (uint32_t i = 0; i < q->ncite; i++) {
        if (q->cite[i].topic != q->current) continue;
        uint32_t share = (cap - used) / (sources - done);
        lay_out(&q->cite[i], in, cap + 1u, &used, share);
        done++;
    }
    int rc = zxm_ai_ask(q->link, &QUARRY_AI[0], QUARRY_AI_SUMMARIZE, instr, in, used, &got);
    if (rc) return rc;
    q->summary = got;
    q->summary_topic = q->current;
    q->summary_words = words;
    q->summary_sources = m;
    read_answer(q);
    return ZXM_OK;
}

int quarry_accept(quarry_t *q)
{
    return zxm_held_accept(&q->summary);
}

int quarry_reject(quarry_t *q)
{
    return zxm_held_reject(&q->summary);
}

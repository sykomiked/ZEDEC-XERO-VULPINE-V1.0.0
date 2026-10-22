/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* handfast_model.c — Handfast's model (handfast_model.h). */
#include <string.h>

#include "handfast_model.h"

#define HANDFAST_E_FILES (-32014)      /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define HANDFAST_TEXT    (48u * 1024u) /* the agreement's file: one files.read (ZXF_READ_MAX) */
#define HANDFAST_PIECE   6000u         /* bytes of text in one files.write, escaped at most twice */
#define HANDFAST_PATH    96u
#define ADVICE                                                                                     \
    " Advice only: each party signs for itself; Handfast never signs, sends or moves money."

/* ---- small text helpers, bounded ---- */

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    size_t n = strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n + 1u);
    *at += (uint32_t) n;
    return true;
}

static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char d[24];
    uint32_t k = sizeof d - 1u;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    d[k] = 0;
    do {
        d[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) d[--k] = '-';
    return put(out, cap, at, d + k);
}

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static void say(handfast_t *h, const char *a, const char *b, bool advice)
{
    uint32_t at = 0;
    h->notice[0] = 0;
    if (!put(h->notice, sizeof h->notice, &at, a)) return;
    if (b && !put(h->notice, sizeof h->notice, &at, b)) return;
    if (advice) put(h->notice, sizeof h->notice, &at, ADVICE);
}

/* A decimal integer of s[*at..n) up to `end`, a sign allowed; false when
 * it is empty, not a number, past 18 digits or has no end. */
static bool get_i64(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = i < n && s[i] == '-';
    int64_t x = 0;
    if (neg) i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (s[i++] - '0');
    }
    if (!digits || i >= n || s[i] != end) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

/* The text of s[*at..n) up to `end` into out (cap bytes with its 0). */
static bool get_text(const char *s, uint32_t n, uint32_t *at, char end, char *out, uint32_t cap)
{
    uint32_t i = *at, k = 0;
    while (i < n && s[i] != end) {
        if (k + 1u >= cap) return false;
        out[k++] = s[i++];
    }
    if (i >= n) return false;
    out[k] = 0;
    *at = i + 1u;
    return true;
}

/* text of 1..max bytes with no control byte (so no tab or line break) */
static bool text_ok(const char *s, uint32_t max)
{
    uint32_t n = len_of(s, max + 1u);
    if (!s || !n || n > max) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7F) return false;
    return true;
}

static bool hex_ok(const char *s, uint32_t n)
{
    if (!s || !n) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static bool key_ok(const char *k)
{
    uint32_t n = len_of(k, HANDFAST_KEY_HEX + 1u);
    return n == 0 || (n == HANDFAST_KEY_HEX && hex_ok(k, n));
}

/* a clause's text: no control byte but a line break or a tab, so it
 * escapes to at most twice its bytes in a request */
static bool clause_text_ok(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        if ((c < 0x20u && c != '\n' && c != '\t') || c == 0x7Fu) return false;
    }
    return true;
}

/* a space's CID: 1..96 letters and digits */
static bool cid_ok(const char *c)
{
    uint32_t n = len_of(c, HANDFAST_CID_MAX + 1u);
    if (!c || !n || n > HANDFAST_CID_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((c[i] >= '0' && c[i] <= '9') || (c[i] >= 'a' && c[i] <= 'z') ||
              (c[i] >= 'A' && c[i] <= 'Z')))
            return false;
    return true;
}

static void path_of(int64_t deal, int64_t clause, char *out, uint32_t cap)
{
    uint32_t at = 0;
    out[0] = 0;
    put(out, cap, &at, HANDFAST_DIR "/deal-");
    put_i64(out, cap, &at, deal);
    if (clause > 0) {
        put(out, cap, &at, "/clause-");
        put_i64(out, cap, &at, clause);
    }
    put(out, cap, &at, ".txt");
}

static void dir_of(const handfast_t *h, char *out, uint32_t cap)
{
    uint32_t at = 0;
    out[0] = 0;
    put(out, cap, &at, HANDFAST_DIR "/deal-");
    put_i64(out, cap, &at, h->deal);
}

/* ---- zxvd ---- */

static bool refused_as(const zxm_link_t *l, int rc, const char *why)
{
    return rc == ZXM_E_REMOTE && l->err_code == HANDFAST_E_FILES && strstr(l->err, why);
}

/* files.read of a whole small file; its length, ZXM_E_STATE when it does
 * not exist, ZXM_E_TOOBIG when it does not fit. */
static int32_t read_file(zxm_link_t *l, const char *path, char *text, uint32_t cap)
{
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", cap - 1u);
    int rc = zxm_call(l);
    if (refused_as(l, rc, "no such")) return ZXM_E_STATE;
    if (rc) return rc;
    bool eof = false;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t len = w4_json_str(l->resp, &l->tok[t], text, cap);
    return len < 0 ? ZXM_E_TOOBIG : len;
}

static int make_dir(zxm_link_t *l, const char *path)
{
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    return rc && !refused_as(l, rc, "already exists") ? rc : ZXM_OK;
}

/* files.write of text whole, in pieces cut on character boundaries. */
static int write_file(zxm_link_t *l, const char *path, const char *text, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < HANDFAST_PIECE ? len - at : HANDFAST_PIECE;
        if (at + n < len) n = zxm_utf8_cut(text + at, n);
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

/* The journal's newest seq (0 with no journal or no entry). */
static int journal_newest(zxm_link_t *l, int64_t *seq)
{
    bool on = false;
    *seq = 0;
    zxm_begin(l, "journal.query");
    zxm_p_u64(l, "max", 1);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (zxm_get_bool(l, l->result, "on", &on) || arr < 0 || l->tok[arr].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    if (!on || !zxm_len(l, arr)) return ZXM_OK;
    int32_t e = zxm_at(l, arr, 0);
    if (e < 0 || zxm_get_i64(l, e, "seq", seq) || *seq < 1 || *seq > WITNESS_SEQ_MAX)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* ---- the agreement's pieces ---- */

static int32_t clause_slot(const handfast_t *h, int64_t clause)
{
    for (uint32_t i = 0; i < h->nclause; i++)
        if (h->clause[i] == clause) return (int32_t) i;
    return -1;
}

int32_t handfast_latest(const handfast_t *h, int64_t clause)
{
    int32_t r = -1;
    for (uint32_t i = 0; i < h->nrev; i++)
        if (h->rev[i].clause == clause) r = (int32_t) i;
    return r;
}

static int64_t obligors(int64_t mask)
{
    int64_t k = 0;
    for (uint32_t p = 0; p < HANDFAST_PARTY_MAX; p++) k += (mask >> p) & 1;
    return k;
}

static bool party_ok(const handfast_party_t *p)
{
    int64_t n = (int64_t) len_of(p->name, HANDFAST_NAME_MAX + 1u), codes = 0;
    return text_ok(p->name, HANDFAST_NAME_MAX) && kindred_fields_ok(n, 0, 0, 0) &&
           p->contact >= 1 && p->name_code == kindred_name_code(p->name, (uint32_t) n, &codes) &&
           kindred_record_ok(p->contact, 0, 0, p->name_code, 0, 0) && key_ok(p->key);
}

/* HM3: a revision fits the tree and its journal record, its parent filed */
static bool rev_ok(const handfast_t *h, const handfast_rev_t *r)
{
    if (r->clause < 1 || r->clause > HANDFAST_NUMBER_MAX || r->rev < 1 ||
        r->rev > HANDFAST_REV_MAX || r->parent < 0 || r->parent == r->clause || r->bytes < 1 ||
        r->bytes > HANDFAST_TEXT_MAX || r->seq < 0 || r->prev < 0 ||
        !handfast_node_ok(h->deal, r->clause, r->rev, r->parent, r->bytes, 0) ||
        !witness_record_fits(r->seq, r->prev, HANDFAST_KIND, 0))
        return false;
    if (r->parent && handfast_latest(h, r->parent) < 0) return false;
    /* never under one of its own: walk up from the parent */
    int64_t up = r->parent;
    for (uint32_t k = 0; up && k <= HANDFAST_CLAUSE_MAX; k++) {
        if (up == r->clause) return false;
        int32_t i = handfast_latest(h, up);
        up = i >= 0 ? h->rev[i].parent : 0;
    }
    return true;
}

static bool obligation_ok(const handfast_t *h, const handfast_obligation_t *o)
{
    return clause_slot(h, o->clause) >= 0 && o->due >= 0 && o->due <= HANDFAST_DAY_MAX &&
           o->amount >= 0 && o->amount <= HANDFAST_AMOUNT_MAX && o->mask > 0 &&
           o->mask < ((int64_t) 1 << h->nparty) &&
           sealwax_record_fits(h->deal, (int64_t) h->nobl + 1, SEALWAX_AMOUNT, 1, 0,
                               SEALWAX_BOUND_MAX, 0);
}

/* ---- HM1: the file ---- */

static bool line_i64s(char *t, uint32_t cap, uint32_t *at, char kind, const int64_t *v, uint32_t n)
{
    char k[3] = {kind, '\t', 0};
    if (!put(t, cap, at, k)) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!put_i64(t, cap, at, v[i]) || !put(t, cap, at, i + 1u < n ? "\t" : "\n")) return false;
    return true;
}

static int write_agreement(handfast_t *h)
{
    static char t[HANDFAST_TEXT];
    char path[HANDFAST_PATH];
    uint32_t at = 0;
    bool ok = true;
    t[0] = 0;
    const int64_t head[4] = {h->deal, h->total, h->instalments, h->paid};
    ok = line_i64s(t, sizeof t, &at, 'H', head, 4) && put(t, sizeof t, &at, "T\t") &&
         put(t, sizeof t, &at, h->title) && put(t, sizeof t, &at, "\n");
    if (ok && h->room[0])
        ok = put(t, sizeof t, &at, "R\t") && put(t, sizeof t, &at, h->room) &&
             put(t, sizeof t, &at, "\n");
    for (uint32_t i = 0; ok && i < h->nparty; i++) {
        const handfast_party_t *p = &h->party[i];
        ok = put(t, sizeof t, &at, "P\t") && put_i64(t, sizeof t, &at, p->contact) &&
             put(t, sizeof t, &at, "\t") && put(t, sizeof t, &at, p->key) &&
             put(t, sizeof t, &at, "\t") && put(t, sizeof t, &at, p->name) &&
             put(t, sizeof t, &at, "\n");
    }
    for (uint32_t i = 0; ok && i < h->nrev; i++) {
        const handfast_rev_t *r = &h->rev[i];
        const int64_t v[6] = {r->clause, r->rev, r->parent, r->bytes, r->seq, r->prev};
        ok = line_i64s(t, sizeof t, &at, 'V', v, 6);
    }
    for (uint32_t p = 0; ok && p < h->nparty; p++)
        for (uint32_t c = 0; ok && c < h->nclause; c++)
            if (h->mark[p][c]) {
                const int64_t v[3] = {(int64_t) p, h->clause[c], h->mark[p][c]};
                ok = line_i64s(t, sizeof t, &at, 'M', v, 3);
            }
    for (uint32_t i = 0; ok && i < h->nobl; i++) {
        const handfast_obligation_t *o = &h->obl[i];
        const int64_t v[4] = {o->clause, o->due, o->amount, o->mask};
        ok = line_i64s(t, sizeof t, &at, 'O', v, 4);
    }
    if (!ok) return ZXM_E_TOOBIG;
    int rc = make_dir(h->link, HANDFAST_DIR);
    if (rc) return rc;
    path_of(h->deal, 0, path, sizeof path);
    return write_file(h->link, path, t, at);
}

/* One line of the file into h, checked; false when it does not hold. */
static bool read_line(handfast_t *h, const char *t, uint32_t n, uint32_t *at, bool *head)
{
    char kind = t[*at];
    if (*at + 1u >= n || t[*at + 1u] != '\t') return false;
    *at += 2u;
    if (kind == 'H') {
        int64_t v[4];
        for (uint32_t k = 0; k < 4u; k++)
            if (!get_i64(t, n, at, k < 3u ? '\t' : '\n', &v[k])) return false;
        if (*head || v[0] != h->deal) return false;
        h->total = v[1], h->instalments = v[2], h->paid = v[3];
        *head = true;
        return (h->instalments == 0 && h->total == 0 && h->paid == 0) ||
               trk_stream_due(h->total, h->instalments, h->instalments, h->paid) != TRK_REFUSED;
    }
    if (kind == 'T') return get_text(t, n, at, '\n', h->title, sizeof h->title);
    if (kind == 'R') return get_text(t, n, at, '\n', h->room, sizeof h->room) && cid_ok(h->room);
    if (kind == 'P') {
        if (h->nparty >= HANDFAST_PARTY_MAX || h->nrev) return false;
        handfast_party_t *p = &h->party[h->nparty];
        int64_t codes = 0;
        memset(p, 0, sizeof *p);
        if (!get_i64(t, n, at, '\t', &p->contact) ||
            !get_text(t, n, at, '\t', p->key, sizeof p->key) ||
            !get_text(t, n, at, '\n', p->name, sizeof p->name))
            return false;
        p->name_code = kindred_name_code(p->name, len_of(p->name, sizeof p->name), &codes);
        if (!party_ok(p)) return false;
        h->nparty++;
        return true;
    }
    if (kind == 'V') {
        int64_t v[6];
        for (uint32_t k = 0; k < 6u; k++)
            if (!get_i64(t, n, at, k < 5u ? '\t' : '\n', &v[k])) return false;
        handfast_rev_t r = {v[0], v[1], v[2], v[3], v[4], v[5]};
        int32_t last = handfast_latest(h, r.clause);
        if (h->nrev >= HANDFAST_REVS || !rev_ok(h, &r) ||
            r.rev != (last >= 0 ? h->rev[last].rev + 1 : 1))
            return false;
        if (last < 0) {
            if (h->nclause >= HANDFAST_CLAUSE_MAX) return false;
            h->clause[h->nclause++] = r.clause;
        }
        h->rev[h->nrev++] = r;
        return true;
    }
    if (kind == 'M') {
        int64_t v[3];
        for (uint32_t k = 0; k < 3u; k++)
            if (!get_i64(t, n, at, k < 2u ? '\t' : '\n', &v[k])) return false;
        int32_t c = clause_slot(h, v[1]);
        if (v[0] < 0 || v[0] >= (int64_t) h->nparty || c < 0 || v[2] < HANDFAST_AGREED ||
            v[2] > HANDFAST_DISPUTED)
            return false;
        h->mark[v[0]][c] = v[2];
        return true;
    }
    if (kind == 'O') {
        int64_t v[4];
        for (uint32_t k = 0; k < 4u; k++)
            if (!get_i64(t, n, at, k < 3u ? '\t' : '\n', &v[k])) return false;
        handfast_obligation_t o = {v[0], v[1], v[2], v[3]};
        if (h->nobl >= HANDFAST_OBLIGATIONS || !obligation_ok(h, &o)) return false;
        h->obl[h->nobl++] = o;
        return true;
    }
    return false;
}

void handfast_init(handfast_t *h, zxm_link_t *link)
{
    memset(h, 0, sizeof *h);
    h->link = link;
    say(h, "No agreement open.", 0, true);
}

static void clear(handfast_t *h, int64_t deal)
{
    zxm_link_t *l = h->link;
    memset(h, 0, sizeof *h);
    h->link = l;
    h->deal = deal;
}

int handfast_load(handfast_t *h, int64_t deal)
{
    static char t[HANDFAST_TEXT];
    char path[HANDFAST_PATH];
    if (deal < 1 || deal > HANDFAST_DEAL_MAX) return ZXM_E_ARG;
    clear(h, deal);
    path_of(h->deal, 0, path, sizeof path);
    int32_t n = read_file(h->link, path, t, sizeof t);
    if (n == ZXM_E_STATE) {
        clear(h, 0);
        say(h, "No such agreement.", 0, false);
        return ZXM_E_STATE;
    }
    if (n < 0) {
        clear(h, 0);
        say(h, "The agreement could not be read: ", h->link->err, false);
        return n;
    }
    bool head = false;
    for (uint32_t at = 0; at < (uint32_t) n;)
        if (!read_line(h, t, (uint32_t) n, &at, &head)) {
            clear(h, 0);
            say(h, "The agreement's file does not hold its rules.", 0, false);
            return ZXM_E_PROTOCOL;
        }
    if (!head || !h->title[0]) {
        clear(h, 0);
        return ZXM_E_PROTOCOL;
    }
    h->loaded = true;
    say(h, "Agreement loaded.", 0, true);
    return ZXM_OK;
}

int handfast_open(handfast_t *h, int64_t deal, const char *title, bool owner_confirmed)
{
    static char t[HANDFAST_TEXT];
    char path[HANDFAST_PATH];
    if (deal < 1 || deal > HANDFAST_DEAL_MAX || !text_ok(title, HANDFAST_TITLE_MAX))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    path_of(deal, 0, path, sizeof path);
    int32_t n = read_file(h->link, path, t, sizeof t);
    if (n != ZXM_E_STATE) {
        if (n >= 0) say(h, "That agreement is already open; load it.", 0, false);
        return n >= 0 ? ZXM_E_STATE : n;
    }
    clear(h, deal);
    memcpy(h->title, title, len_of(title, HANDFAST_TITLE_MAX) + 1u);
    int rc = write_agreement(h);
    if (rc) {
        clear(h, 0);
        return rc;
    }
    h->loaded = true;
    say(h, "Agreement opened.", 0, true);
    return ZXM_OK;
}

/* ---- HM2: parties ---- */

int handfast_add_party(handfast_t *h, const char *name, int64_t contact, const char *key,
                       bool owner_confirmed, uint32_t *index)
{
    handfast_party_t p;
    int64_t codes = 0;
    memset(&p, 0, sizeof p);
    if (!h->loaded) return ZXM_E_STATE;
    if (!name || !key || !index || !text_ok(name, HANDFAST_NAME_MAX) || !key_ok(key))
        return ZXM_E_ARG;
    memcpy(p.name, name, len_of(name, HANDFAST_NAME_MAX) + 1u);
    memcpy(p.key, key, len_of(key, HANDFAST_KEY_HEX) + 1u);
    p.contact = contact;
    p.name_code = kindred_name_code(p.name, len_of(p.name, sizeof p.name), &codes);
    if (!party_ok(&p)) return ZXM_E_ARG;
    if (h->nparty >= HANDFAST_PARTY_MAX || h->nrev) return ZXM_E_STATE; /* parties come first */
    for (uint32_t i = 0; i < h->nparty; i++)
        if (h->party[i].contact == contact) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    h->party[h->nparty++] = p;
    int rc = write_agreement(h);
    if (rc) {
        h->nparty--;
        return rc;
    }
    *index = h->nparty - 1u;
    return ZXM_OK;
}

/* ---- HM3: clauses ---- */

int handfast_put_clause(handfast_t *h, int64_t clause, int64_t parent, const char *text,
                        uint32_t len, bool owner_confirmed)
{
    char path[HANDFAST_PATH], dir[HANDFAST_PATH];
    if (!h->loaded) return ZXM_E_STATE;
    if (!text || !handfast_limits_ok(h->nparty, len) || len_of(text, len) != len ||
        !clause_text_ok(text, len))
        return ZXM_E_ARG;
    int32_t last = handfast_latest(h, clause), slot = clause_slot(h, clause);
    handfast_rev_t r = {clause, last >= 0 ? h->rev[last].rev + 1 : 1, parent, len,
                        0,      last >= 0 ? h->rev[last].seq : 0};
    if (!rev_ok(h, &r)) return ZXM_E_ARG;
    if (h->nrev >= HANDFAST_REVS || (slot < 0 && h->nclause >= HANDFAST_CLAUSE_MAX))
        return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    zxm_link_t *l = h->link;
    int rc = make_dir(l, HANDFAST_DIR);
    dir_of(h, dir, sizeof dir);
    if (!rc) rc = make_dir(l, dir);
    path_of(h->deal, clause, path, sizeof path);
    if (!rc) rc = write_file(l, path, text, len);
    if (!rc) rc = journal_newest(l, &r.seq);
    if (rc) return rc;
    if (!rev_ok(h, &r)) return ZXM_E_PROTOCOL;
    int64_t was[HANDFAST_PARTY_MAX];
    if (slot < 0) {
        slot = (int32_t) h->nclause;
        h->clause[h->nclause++] = clause;
    }
    for (uint32_t p = 0; p < HANDFAST_PARTY_MAX; p++) { /* a changed clause is agreed again */
        was[p] = h->mark[p][slot];
        h->mark[p][slot] = HANDFAST_SILENT;
    }
    h->rev[h->nrev++] = r;
    rc = write_agreement(h);
    if (rc) {
        h->nrev--;
        if (last < 0) h->nclause--;
        for (uint32_t p = 0; p < HANDFAST_PARTY_MAX; p++) h->mark[p][slot] = was[p];
        return rc;
    }
    return ZXM_OK;
}

int handfast_read_clause(handfast_t *h, int64_t clause, char *text, uint32_t cap)
{
    char path[HANDFAST_PATH];
    if (!h->loaded) return ZXM_E_STATE;
    int32_t r = handfast_latest(h, clause);
    if (r < 0 || !text || cap < 2u) return ZXM_E_ARG;
    path_of(h->deal, clause, path, sizeof path);
    int32_t n = read_file(h->link, path, text, cap);
    if (n < 0) return n;
    return n == h->rev[r].bytes ? n : ZXM_E_PROTOCOL; /* changed outside Handfast */
}

/* ---- HM4: the party matrix ---- */

int handfast_mark(handfast_t *h, uint32_t party, int64_t clause, int64_t mark, bool owner_confirmed)
{
    if (!h->loaded) return ZXM_E_STATE;
    int32_t c = clause_slot(h, clause);
    if (party >= h->nparty || c < 0 || mark < HANDFAST_SILENT || mark > HANDFAST_DISPUTED)
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int64_t was = h->mark[party][c];
    h->mark[party][c] = mark;
    int rc = write_agreement(h);
    if (rc) h->mark[party][c] = was;
    return rc;
}

lpres_state_t handfast_clause_state(const handfast_t *h, int64_t clause)
{
    static int64_t cl[HANDFAST_PARTY_MAX], mk[HANDFAST_PARTY_MAX];
    int32_t c = clause_slot(h, clause);
    if (c < 0 || !h->nparty) return LPRES_STATE_NEITHER;
    for (uint32_t p = 0; p < h->nparty; p++) {
        cl[p] = clause;
        mk[p] = h->mark[p][c];
    }
    return (lpres_state_t) handfast_cell_state(cl, mk, h->nparty, 0);
}

bool handfast_goes_ahead(const handfast_t *h, int64_t clause)
{
    return handfast_clause_state(h, clause) == LPRES_STATE_TRUE;
}

uint32_t handfast_escrow(const handfast_t *h, int64_t *out, uint32_t cap)
{
    uint32_t k = 0;
    for (uint32_t c = 0; c < h->nclause; c++)
        if (handfast_clause_state(h, h->clause[c]) == LPRES_STATE_BOTH) {
            if (out && k < cap) out[k] = h->clause[c];
            k++;
        }
    return k;
}

int64_t handfast_unagreed(const handfast_t *h)
{
    int64_t k = 0;
    for (uint32_t p = 0; p < h->nparty; p++)
        for (uint32_t c = 0; c < h->nclause; c++) k += h->mark[p][c] != HANDFAST_AGREED;
    return k;
}

/* ---- HM5: ready to sign ---- */

bool handfast_ready(const handfast_t *h)
{
    int64_t missing = 0, outside = 0;
    if (!h->loaded) return false;
    for (uint32_t p = 0; p < h->nparty; p++) missing += !h->party[p].key[0];
    for (uint32_t i = 0; i < h->nobl; i++) {
        const handfast_obligation_t *o = &h->obl[i];
        outside += clause_slot(h, o->clause) < 0 || o->amount > SEALWAX_BOUND_MAX ||
                   !sealwax_record_fits(h->deal, (int64_t) i + 1, SEALWAX_AMOUNT, 1, 0,
                                        SEALWAX_BOUND_MAX, 0);
    }
    return handfast_ready_rule(h->nparty, h->nclause, handfast_unagreed(h)) &&
           sealwax_sign_ready((int64_t) (h->nparty + h->nobl), missing, outside);
}

/* ---- HM6: obligations and the schedule ---- */

int handfast_add_obligation(handfast_t *h, int64_t clause, int64_t due, int64_t units,
                            int64_t cents, int64_t mask, bool owner_confirmed)
{
    if (!h->loaded) return ZXM_E_STATE;
    handfast_obligation_t o = {clause, due, sealwax_amount_minor(units, cents), mask};
    if (o.amount == SEALWAX_REFUSED || !obligation_ok(h, &o)) return ZXM_E_ARG;
    if (h->nobl >= HANDFAST_OBLIGATIONS) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    h->obl[h->nobl++] = o;
    int rc = write_agreement(h);
    if (rc) h->nobl--;
    return rc;
}

int handfast_owed(const handfast_t *h, uint32_t party, int64_t due, int64_t *minor)
{
    int64_t part[HANDFAST_OBLIGATIONS];
    uint32_t k = 0;
    if (!h->loaded) return ZXM_E_STATE;
    if (party >= h->nparty || !minor) return ZXM_E_ARG;
    for (uint32_t i = 0; i < h->nobl; i++) {
        const handfast_obligation_t *o = &h->obl[i];
        if (o->due != due || !((o->mask >> party) & 1)) continue;
        int64_t pos = obligors(o->mask & (((int64_t) 1 << party) - 1)) + 1;
        part[k] = handfast_share(o->amount, obligors(o->mask), pos);
        if (part[k] == HANDFAST_REFUSED) return ZXM_E_PROTOCOL;
        k++;
    }
    return sealwax_total(part, k, minor) ? ZXM_OK : ZXM_E_TOOBIG;
}

int handfast_set_schedule(handfast_t *h, int64_t total, int64_t instalments, bool owner_confirmed)
{
    if (!h->loaded) return ZXM_E_STATE;
    if (trk_stream_due(total, 0, instalments, 0) != 0) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int64_t t = h->total, n = h->instalments, p = h->paid;
    h->total = total, h->instalments = instalments, h->paid = 0;
    int rc = write_agreement(h);
    if (rc) h->total = t, h->instalments = n, h->paid = p;
    return rc;
}

int handfast_record_paid(handfast_t *h, int64_t paid, bool owner_confirmed)
{
    if (!h->loaded || !h->instalments) return ZXM_E_STATE;
    /* never more than the whole schedule owes */
    if (trk_stream_due(h->total, h->instalments, h->instalments, paid) == TRK_REFUSED)
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int64_t was = h->paid;
    h->paid = paid;
    int rc = write_agreement(h);
    if (rc) h->paid = was;
    return rc;
}

int handfast_instalment(const handfast_t *h, int64_t n, int64_t *due)
{
    if (!h->loaded || !h->instalments) return ZXM_E_STATE;
    if (n < 1 || n > h->instalments || !due) return ZXM_E_ARG;
    int64_t d = trk_stream_due(h->total, n, h->instalments, h->paid);
    if (d == TRK_REFUSED) { /* paid ahead of instalment n: nothing due yet */
        *due = 0;
        return ZXM_OK;
    }
    *due = d;
    return ZXM_OK;
}

/* ---- HM7: the negotiation room ---- */

int handfast_set_room(handfast_t *h, const char *cid, bool owner_confirmed)
{
    char was[HANDFAST_CID_MAX + 1];
    if (!h->loaded) return ZXM_E_STATE;
    if (!cid_ok(cid)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    memcpy(was, h->room, sizeof was);
    memcpy(h->room, cid, len_of(cid, HANDFAST_CID_MAX) + 1u);
    int rc = write_agreement(h);
    if (rc) memcpy(h->room, was, sizeof was);
    return rc;
}

/* commons.room_post_ok's three flags for the room, from pspace.list */
static int room_flags(handfast_t *h, int64_t f[3])
{
    zxm_link_t *l = h->link;
    bool online = false;
    f[0] = f[1] = f[2] = 0;
    zxm_begin(l, "pspace.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "spaces");
    if (zxm_get_bool(l, l->result, "online", &online) || arr < 0 || l->tok[arr].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    f[2] = online ? 1 : 0;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        char cid[HANDFAST_CID_MAX + 2], phase[16];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "cid", cid, sizeof cid)) continue;
        if (strcmp(cid, h->room)) continue;
        f[0] = 1; /* listed: this node is in it or joining */
        if (!zxm_get_str(l, o, "phase", phase, sizeof phase) && !strcmp(phase, "in")) f[1] = 1;
    }
    return ZXM_OK;
}

int handfast_post(handfast_t *h, const char *text, bool owner_confirmed)
{
    char got[HANDFAST_CID_MAX + 2];
    bool posted = false;
    int64_t f[3];
    if (!h->loaded || !h->room[0]) return ZXM_E_STATE;
    uint32_t n = len_of(text, COMMONS_TEXT_MAX + 1u);
    if (!text || !commons_message_text_ok((int64_t) n)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = room_flags(h, f);
    if (rc) return rc;
    if (!commons_room_post_ok(f[0], f[1], f[2])) {
        say(h, "Not posted: this node is not in the agreement's room.", 0, false);
        return ZXM_E_STATE;
    }
    zxm_link_t *l = h->link;
    zxm_begin(l, "pspace.post");
    zxm_p_str(l, "cid", h->room);
    zxm_p_strn(l, "text", text, n);
    rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "cid", got, sizeof got) || strcmp(got, h->room) ||
        zxm_get_bool(l, l->result, "posted", &posted) || !posted)
        return ZXM_E_PROTOCOL;
    say(h, "Posted to the agreement's room.", 0, false);
    return ZXM_OK;
}

/* ---- HM8: signatures and proof ---- */

int handfast_check_signed(handfast_t *h, uint32_t party, const char *envelope_hex)
{
    char from[HANDFAST_KEY_HEX + 2], reason[64];
    bool valid = false;
    if (!h->loaded) return ZXM_E_STATE;
    uint32_t n = len_of(envelope_hex, HANDFAST_SIG_MAX + 1u);
    if (party >= h->nparty || n > HANDFAST_SIG_MAX || n % 2u || !hex_ok(envelope_hex, n))
        return ZXM_E_ARG;
    handfast_party_t *p = &h->party[party];
    if (!p->key[0]) return ZXM_E_STATE; /* no key to check it against */
    zxm_link_t *l = h->link;
    zxm_begin(l, "web4.verify");
    zxm_p_strn(l, "envelope", envelope_hex, n);
    zxm_p_str(l, "pk", p->key);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "valid", &valid)) return ZXM_E_PROTOCOL;
    if (!valid) {
        if (zxm_get_str(l, l->result, "reason", reason, sizeof reason)) return ZXM_E_PROTOCOL;
        p->signed_ok = false;
        say(h, "The signed copy does not hold: ", reason, true);
        return ZXM_OK;
    }
    if (zxm_get_str(l, l->result, "from", from, sizeof from)) return ZXM_E_PROTOCOL;
    uint32_t k = 0;
    while (k < HANDFAST_KEY_HEX && from[k] && (from[k] | 0x20) == (p->key[k] | 0x20)) k++;
    p->signed_ok = k == HANDFAST_KEY_HEX && !from[k];
    say(h,
        p->signed_ok ? "The party's signed copy holds, from its own key."
                     : "The signature holds, but not from this party's key.",
        0, true);
    return ZXM_OK;
}

static lpres_state_t journal_has(handfast_t *h, int64_t seq, int64_t first, int64_t last)
{
    if (!seq || seq < first) return LPRES_STATE_NEITHER; /* never in it, or no longer kept */
    if (seq > last) return LPRES_STATE_FALSE;
    zxm_link_t *l = h->link;
    int64_t got = 0;
    zxm_begin(l, "journal.query");
    zxm_p_u64(l, "before", (uint64_t) seq + 1u);
    zxm_p_u64(l, "max", 1);
    if (zxm_call(l)) return LPRES_STATE_NEITHER;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || !zxm_len(l, arr)) return LPRES_STATE_FALSE;
    int32_t e = zxm_at(l, arr, 0);
    if (e < 0 || zxm_get_i64(l, e, "seq", &got)) return LPRES_STATE_NEITHER;
    return got == seq ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

lpres_state_t handfast_prove(handfast_t *h, int64_t clause)
{
    int64_t last = 0, first_seq = 0, last_seq = 0;
    if (!h->loaded || handfast_latest(h, clause) < 0) return LPRES_STATE_NEITHER;
    /* its revisions: witness.chain_ok, each witness.audit_ok */
    lpres_state_t chain = LPRES_STATE_TRUE;
    for (uint32_t i = 0; i < h->nrev; i++) {
        const handfast_rev_t *r = &h->rev[i];
        if (r->clause != clause) continue;
        if (!r->seq) {
            chain = lpres_conjoin(chain, LPRES_STATE_NEITHER);
            continue;
        }
        bool fits = witness_record_fits(r->seq, r->prev, HANDFAST_KIND, 0);
        chain =
            lpres_conjoin(chain, fits && witness_linked(r->seq, r->prev, last) ? LPRES_STATE_TRUE
                                                                               : LPRES_STATE_FALSE);
        last = r->seq;
    }
    /* the journal: its hash chain intact, and each revision's seq in it */
    zxm_link_t *l = h->link;
    bool on = false, ok = false;
    zxm_begin(l, "journal.verify");
    if (zxm_call(l) || zxm_get_bool(l, l->result, "on", &on)) return LPRES_STATE_NEITHER;
    lpres_state_t journal = LPRES_STATE_NEITHER;
    if (on) {
        if (zxm_get_bool(l, l->result, "ok", &ok) ||
            zxm_get_i64(l, l->result, "first_seq", &first_seq) ||
            zxm_get_i64(l, l->result, "last_seq", &last_seq))
            return LPRES_STATE_NEITHER;
        journal = ok ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
        for (uint32_t i = 0; ok && i < h->nrev; i++)
            if (h->rev[i].clause == clause)
                journal =
                    lpres_conjoin(journal, journal_has(h, h->rev[i].seq, first_seq, last_seq));
    }
    lpres_state_t s = lpres_kjoin(chain, journal);
    say(h,
        s == LPRES_STATE_TRUE && journal == LPRES_STATE_NEITHER
            ? "Its revisions hold; no journal to prove them against."
        : s == LPRES_STATE_TRUE  ? "Every revision proven against the journal."
        : s == LPRES_STATE_BOTH  ? "Held: its revisions and the journal disagree."
        : s == LPRES_STATE_FALSE ? "Not proven: a revision or the journal is broken."
                                 : "Nothing to prove it against.",
        0, false);
    return s;
}

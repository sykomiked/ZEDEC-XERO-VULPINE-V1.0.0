/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kindred_model.c — Kindred's model (kindred_model.h). */
#include <string.h>

#include "kindred_model.h"

#define KINDRED_E_FILES  (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define KINDRED_LINE_MAX 96u      /* one index line: six numbers */
#define KINDRED_FILE_MAX                                                                           \
    (KINDRED_NAME_MAX + KINDRED_PHONE_MAX + 1 + KINDRED_MAIL_MAX + KINDRED_NOTE_MAX +              \
     2 * KINDRED_HEX_ID + 8)

/* ---- small text helpers, bounded ---- */

/* Append s to out (cap bytes) at *at; false when it does not fit. */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    size_t n = strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n + 1u);
    *at += (uint32_t) n;
    return true;
}

/* v in decimal, a '-' first when below 0. */
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

static void say(kindred_t *k, const char *a, const char *b)
{
    uint32_t at = 0;
    k->notice[0] = 0;
    if (put(k->notice, sizeof k->notice, &at, a) && b) put(k->notice, sizeof k->notice, &at, b);
}

/* One decimal from s[*at..n), then one separator `end`; false when there
 * is none, it has more than 18 digits, or the separator is missing. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    int64_t x = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (s[i] - '0');
        i++;
    }
    if (!digits || i >= n || s[i] != end) return false;
    *v = x;
    *at = i + 1u;
    return true;
}

/* One line of s[*at..n) into out (cap bytes, terminated); false when it
 * has no '\n' or does not fit. */
static bool get_line(const char *s, uint32_t n, uint32_t *at, char *out, uint32_t cap)
{
    uint32_t i = *at, k = 0;
    while (i < n && s[i] != '\n') {
        if (k + 1u >= cap) return false;
        out[k++] = s[i++];
    }
    if (i >= n) return false;
    out[k] = 0;
    *at = i + 1u;
    return true;
}

static bool path_of(int64_t contact, char *out, uint32_t cap)
{
    uint32_t at = 0;
    return put(out, cap, &at, KINDRED_DIR "/c") && put_i64(out, cap, &at, contact) &&
           put(out, cap, &at, ".txt");
}

/* ---- KM2: fields ---- */

/* n bytes of s are valid UTF-8 with no control byte. */
static bool text_ok(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n;) {
        unsigned char c = (unsigned char) s[i];
        uint32_t more = c < 0x80u              ? 0u
                        : (c & 0xE0u) == 0xC0u ? 1u
                        : (c & 0xF0u) == 0xE0u ? 2u
                        : (c & 0xF8u) == 0xF0u ? 3u
                                               : 9u;
        if (c < 0x20u || c == 0x7Fu || more > 3u || (more == 1u && c < 0xC2u) ||
            i + more >= n + (more ? 0u : 1u))
            return false;
        for (uint32_t j = 1; j <= more; j++)
            if ((((unsigned char) s[i + j]) & 0xC0u) != 0x80u) return false;
        i += more + 1u;
    }
    return true;
}

/* The digits of a phone number ('+' first allowed), or -1 when it is not one. */
static int64_t phone_digits(const char *s, uint32_t n)
{
    int64_t d = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] == '+' && i == 0) continue;
        if (s[i] < '0' || s[i] > '9') return -1;
        d++;
    }
    return n && !d ? -1 : d;
}

static bool mail_ok(const char *s, uint32_t n)
{
    uint32_t at = 0, ats = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] <= 0x20 || s[i] >= 0x7F) return false;
        if (s[i] == '@') ats++, at = i;
    }
    return !n || (ats == 1u && at > 0u && at + 1u < n);
}

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

/* KR1 and KM2 over the four fields; false when any does not hold. */
static bool fields_ok(const char *name, const char *phone, const char *mail, const char *note)
{
    if (!name || !phone || !mail || !note) return false;
    uint32_t nl = len_of(name, KINDRED_NAME_MAX + 1u), pl = len_of(phone, KINDRED_PHONE_MAX + 2u),
             ml = len_of(mail, KINDRED_MAIL_MAX + 1u), tl = len_of(note, KINDRED_NOTE_MAX + 1u);
    int64_t digits = pl > KINDRED_PHONE_MAX + 1u ? KINDRED_PHONE_MAX + 1 : phone_digits(phone, pl);
    return digits >= 0 && kindred_fields_ok(nl, digits, ml, tl) && text_ok(name, nl) &&
           text_ok(note, tl) && mail_ok(mail, ml);
}

/* ---- KM1: the index and the contacts ---- */

static void codes_of(kindred_contact_t *c)
{
    c->name_code = kindred_name_code(c->name, (uint32_t) strlen(c->name), NULL);
    c->card_code = c->card[0] ? kindred_id_code(c->card) : 0;
    c->node_code = c->node[0] ? kindred_id_code(c->node) : 0;
}

static int32_t compose_index(const kindred_t *k, char *out, uint32_t cap)
{
    uint32_t at = 0;
    out[0] = 0;
    for (uint32_t i = 0; i < k->n; i++) {
        const kindred_contact_t *c = &k->c[i];
        const int64_t v[6] = {c->contact,   c->card_code, c->node_code,
                              c->name_code, c->verified,  c->stamp};
        for (uint32_t j = 0; j < 6u; j++)
            if (!put_i64(out, cap, &at, v[j]) || !put(out, cap, &at, j < 5u ? " " : "\n"))
                return -1;
    }
    return (int32_t) at;
}

static int32_t compose_contact(const kindred_contact_t *c, char *out, uint32_t cap)
{
    uint32_t at = 0;
    const char *f[6] = {c->name, c->phone, c->mail, c->note, c->card, c->node};
    out[0] = 0;
    for (uint32_t j = 0; j < 6u; j++)
        if (!put(out, cap, &at, f[j]) || !put(out, cap, &at, "\n")) return -1;
    return (int32_t) at;
}

/* files.read of a whole small file into text (cap bytes); its length, or a
 * negative ZXM_ error; ZXM_E_STATE when it does not exist. */
static int32_t read_file(kindred_t *k, const char *path, char *text, uint32_t cap)
{
    zxm_link_t *l = k->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == KINDRED_E_FILES && strstr(l->err, "no such"))
        return ZXM_E_STATE;
    if (rc) return rc;
    bool eof = false;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t len = w4_json_str(l->resp, &l->tok[t], text, cap);
    return len < 0 ? ZXM_E_TOOBIG : len;
}

/* One contact file into c, its codes checked against the index's. */
static int parse_contact(const char *s, uint32_t n, kindred_contact_t *c)
{
    uint32_t at = 0;
    kindred_contact_t f = *c;
    if (!get_line(s, n, &at, f.name, sizeof f.name) ||
        !get_line(s, n, &at, f.phone, sizeof f.phone) ||
        !get_line(s, n, &at, f.mail, sizeof f.mail) ||
        !get_line(s, n, &at, f.note, sizeof f.note) ||
        !get_line(s, n, &at, f.card, sizeof f.card) ||
        !get_line(s, n, &at, f.node, sizeof f.node) || at != n ||
        !fields_ok(f.name, f.phone, f.mail, f.note))
        return ZXM_E_PROTOCOL;
    if ((f.card[0] && kindred_id_code(f.card) != c->card_code) || (!f.card[0] && c->card_code) ||
        (f.node[0] && kindred_id_code(f.node) != c->node_code) || (!f.node[0] && c->node_code) ||
        kindred_name_code(f.name, (uint32_t) strlen(f.name), NULL) != c->name_code ||
        (c->verified != 0) != (f.card[0] != 0))
        return ZXM_E_PROTOCOL;
    *c = f;
    return ZXM_OK;
}

/* One index line into *c (KR5 among the checks). */
static bool parse_line(const char *s, uint32_t n, uint32_t *at, kindred_contact_t *c)
{
    int64_t v[6];
    for (uint32_t j = 0; j < 6u; j++)
        if (!get_num(s, n, at, j < 5u ? ' ' : '\n', &v[j])) return false;
    if (v[0] < 1 || v[0] > KINDRED_CONTACT_MAX || v[1] > KINDRED_CARD_CODE_MAX ||
        v[2] > KINDRED_CARD_CODE_MAX || v[3] > KINDRED_NAME_CODE_MAX || v[4] > 1 ||
        !kindred_record_ok(v[0], v[1], v[2], v[3], v[4], v[5]) ||
        !kindred_index_ok(v[3], v[0], v[5]))
        return false;
    memset(c, 0, sizeof *c);
    c->contact = v[0], c->card_code = v[1], c->node_code = v[2], c->name_code = v[3];
    c->verified = v[4], c->stamp = v[5];
    return true;
}

void kindred_init(kindred_t *k, zxm_link_t *link)
{
    memset(k, 0, sizeof *k);
    k->link = link;
}

int kindred_load(kindred_t *k)
{
    static char text[KINDRED_CONTACTS * KINDRED_LINE_MAX];
    static char file[KINDRED_FILE_MAX];
    static kindred_contact_t next[KINDRED_CONTACTS];
    char path[KINDRED_PATH_MAX];
    int32_t len = read_file(k, KINDRED_INDEX, text, sizeof text);
    if (len == ZXM_E_STATE) { /* nothing kept yet */
        k->n = 0, k->last = 0, k->folder = false, k->loaded = true;
        return ZXM_OK;
    }
    if (len < 0) return len;
    uint32_t at = 0, n = 0;
    int64_t last = 0;
    while (at < (uint32_t) len) {
        if (n >= KINDRED_CONTACTS || !parse_line(text, (uint32_t) len, &at, &next[n]))
            return ZXM_E_PROTOCOL;
        if (next[n].contact <= last) return ZXM_E_PROTOCOL; /* in the order added */
        last = next[n].contact;
        n++;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (!path_of(next[i].contact, path, sizeof path)) return ZXM_E_TOOBIG;
        int32_t fl = read_file(k, path, file, sizeof file);
        if (fl == ZXM_E_STATE) return ZXM_E_PROTOCOL; /* the index names a contact not kept */
        if (fl < 0) return fl;
        int rc = parse_contact(file, (uint32_t) fl, &next[i]);
        if (rc) return rc;
        zxm_progress(k->link, i + 1u, n);
    }
    memcpy(k->c, next, (size_t) n * sizeof next[0]);
    k->n = n;
    k->last = last;
    k->folder = true;
    k->loaded = true;
    k->nhits = 0;
    return ZXM_OK;
}

static int make_folder(kindred_t *k)
{
    zxm_link_t *l = k->link;
    if (k->folder) return ZXM_OK;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", KINDRED_DIR);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == KINDRED_E_FILES && strstr(l->err, "already exists"))
        rc = ZXM_OK;
    if (!rc) k->folder = true;
    return rc;
}

static int write_text(kindred_t *k, const char *path, const char *text, uint32_t n)
{
    zxm_link_t *l = k->link;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", text, n);
    return zxm_call(l);
}

static int write_index(kindred_t *k)
{
    static char text[KINDRED_CONTACTS * KINDRED_LINE_MAX];
    int32_t len = compose_index(k, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    return write_text(k, KINDRED_INDEX, text, (uint32_t) len);
}

static int write_contact(kindred_t *k, const kindred_contact_t *c)
{
    static char text[KINDRED_FILE_MAX];
    char path[KINDRED_PATH_MAX];
    int32_t len = compose_contact(c, text, sizeof text);
    if (len < 0 || !path_of(c->contact, path, sizeof path)) return ZXM_E_TOOBIG;
    return write_text(k, path, text, (uint32_t) len);
}

static int32_t find(const kindred_t *k, int64_t contact)
{
    for (uint32_t i = 0; i < k->n; i++)
        if (k->c[i].contact == contact) return (int32_t) i;
    return -1;
}

const kindred_contact_t *kindred_get(const kindred_t *k, int64_t contact)
{
    int32_t i = find(k, contact);
    return i < 0 ? NULL : &k->c[i];
}

/* c checked (KR5, KR6), written, then the index; on a failed index write
 * the kept contact goes back to *was (KM1). */
static int commit(kindred_t *k, uint32_t i, const kindred_contact_t *was)
{
    kindred_contact_t *c = &k->c[i];
    codes_of(c);
    if (!kindred_record_ok(c->contact, c->card_code, c->node_code, c->name_code, c->verified,
                           c->stamp) ||
        !kindred_index_ok(c->name_code, c->contact, c->stamp))
        return ZXM_E_ARG;
    int rc = make_folder(k);
    if (!rc) rc = write_contact(k, c);
    if (!rc) rc = write_index(k);
    if (rc) {
        if (was)
            *c = *was;
        else
            k->n--;
        return rc;
    }
    k->nhits = 0; /* a search is run again on what is kept now */
    return ZXM_OK;
}

static void set_fields(kindred_contact_t *c, const char *name, const char *phone, const char *mail,
                       const char *note)
{
    memcpy(c->name, name, strlen(name) + 1u);
    memcpy(c->phone, phone, strlen(phone) + 1u);
    memcpy(c->mail, mail, strlen(mail) + 1u);
    memcpy(c->note, note, strlen(note) + 1u);
}

int kindred_add(kindred_t *k, const char *name, const char *phone, const char *mail,
                const char *note, int64_t stamp, int64_t *contact)
{
    if (!fields_ok(name, phone, mail, note) || stamp < 0 || stamp > KINDRED_STAMP_MAX)
        return ZXM_E_ARG;
    if (!k->loaded) return ZXM_E_STATE;
    if (k->n >= KINDRED_CONTACTS || k->last >= KINDRED_CONTACT_MAX) return ZXM_E_TOOBIG;
    kindred_contact_t *c = &k->c[k->n];
    memset(c, 0, sizeof *c);
    c->contact = k->last + 1;
    c->stamp = stamp;
    set_fields(c, name, phone, mail, note);
    k->n++;
    int rc = commit(k, k->n - 1u, NULL);
    if (rc) return rc;
    k->last = c->contact;
    if (contact) *contact = c->contact;
    return ZXM_OK;
}

int kindred_edit(kindred_t *k, int64_t contact, const char *name, const char *phone,
                 const char *mail, const char *note, int64_t stamp)
{
    if (!fields_ok(name, phone, mail, note) || stamp < 0 || stamp > KINDRED_STAMP_MAX)
        return ZXM_E_ARG;
    if (!k->loaded) return ZXM_E_STATE;
    int32_t i = find(k, contact);
    if (i < 0) return ZXM_E_ARG;
    kindred_contact_t was = k->c[i];
    set_fields(&k->c[i], name, phone, mail, note);
    k->c[i].stamp = stamp;
    return commit(k, (uint32_t) i, &was);
}

int kindred_remove(kindred_t *k, int64_t contact)
{
    char path[KINDRED_PATH_MAX];
    if (!k->loaded) return ZXM_E_STATE;
    int32_t i = find(k, contact);
    if (i < 0 || !path_of(contact, path, sizeof path)) return ZXM_E_ARG;
    zxm_link_t *l = k->link;
    zxm_begin(l, "files.trash");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    memmove(&k->c[i], &k->c[i + 1], (size_t) (k->n - (uint32_t) i - 1u) * sizeof k->c[0]);
    k->n--;
    k->nhits = 0;
    return write_index(k);
}

/* ---- KM3: the agent card ---- */

static bool hex_len(const char *s, uint32_t len)
{
    if (!s || len_of(s, len + 1u) != len) return false;
    for (uint32_t i = 0; i < len; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
              (s[i] >= 'A' && s[i] <= 'F')))
            return false;
    return true;
}

static bool card_name(const char *s)
{
    uint32_t n = len_of(s, KINDRED_CARD_NAME + 1u);
    if (!n || n > KINDRED_CARD_NAME) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
              s[i] == '_' || s[i] == '-'))
            return false;
    return true;
}

int kindred_card(kindred_t *k, int64_t contact, const kindred_card_t *card, int64_t stamp)
{
    char id[KINDRED_HEX_ID + 1], reason[KINDRED_NOTICE];
    bool valid = false;
    if (!card || !card_name(card->name) || !hex_len(card->pk_hex, KINDRED_PK_HEX) ||
        !hex_len(card->sig_hex, KINDRED_SIG_HEX) || stamp < 0 || stamp > KINDRED_STAMP_MAX)
        return ZXM_E_ARG;
    if (!k->loaded) return ZXM_E_STATE;
    int32_t i = find(k, contact);
    if (i < 0) return ZXM_E_ARG;
    zxm_link_t *l = k->link;
    zxm_begin(l, "web4.card_verify");
    zxm_p_str(l, "name", card->name);
    zxm_p_u64(l, "bound_ms", card->bound_ms);
    zxm_p_str(l, "pk_hex", card->pk_hex);
    zxm_p_str(l, "bind_sig_hex", card->sig_hex);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "valid", &valid)) return ZXM_E_PROTOCOL;
    if (!valid) {
        reason[0] = 0;
        if (zxm_get(l, l->result, "reason") >= 0 &&
            zxm_get_str(l, l->result, "reason", reason, sizeof reason))
            return ZXM_E_PROTOCOL;
        say(k, "The card does not verify, so it is not kept: ", reason);
        return ZXM_OK;
    }
    if (zxm_get_str(l, l->result, "agent_id", id, sizeof id) || kindred_id_code(id) < 0)
        return ZXM_E_PROTOCOL;
    kindred_contact_t was = k->c[i];
    memcpy(k->c[i].card, id, sizeof id);
    k->c[i].verified = 1;
    k->c[i].stamp = stamp;
    k->notice[0] = 0;
    return commit(k, (uint32_t) i, &was);
}

/* ---- KM4: the node ---- */

int kindred_peers(kindred_t *k)
{
    static kindred_peer_t next[KINDRED_PEERS];
    zxm_link_t *l = k->link;
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "peers");
    if (arr < 0) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), kept = 0;
    for (uint32_t i = 0; i < n && kept < KINDRED_PEERS; i++) {
        int32_t o = zxm_at(l, arr, i);
        char seen[24];
        kindred_peer_t *p = &next[kept];
        memset(p, 0, sizeof *p);
        if (o < 0 || zxm_get_str(l, o, "id", p->id, sizeof p->id) || kindred_id_code(p->id) < 0 ||
            zxm_get_str(l, o, "last_seen_ms", seen, sizeof seen) ||
            zxm_get_bool(l, o, "member", &p->member))
            return ZXM_E_PROTOCOL;
        uint64_t v = 0;
        for (const char *s = seen; *s; s++) {
            if (*s < '0' || *s > '9' || v > (UINT64_MAX - 9u) / 10u) return ZXM_E_PROTOCOL;
            v = v * 10u + (uint64_t) (*s - '0');
        }
        p->last_seen_ms = v;
        kept++;
    }
    memcpy(k->peer, next, (size_t) kept * sizeof next[0]);
    k->npeers = kept;
    return ZXM_OK;
}

int kindred_node(kindred_t *k, int64_t contact, const char *node_id, int64_t stamp)
{
    if (!node_id || kindred_id_code(node_id) < 0 || stamp < 0 || stamp > KINDRED_STAMP_MAX)
        return ZXM_E_ARG;
    if (!k->loaded) return ZXM_E_STATE;
    int32_t i = find(k, contact);
    if (i < 0) return ZXM_E_ARG;
    bool known = false;
    for (uint32_t j = 0; j < k->npeers && !known; j++) known = !strcmp(k->peer[j].id, node_id);
    if (!known) {
        say(k, "That node is not among the peers zxvd has verified.", NULL);
        return ZXM_E_STATE;
    }
    kindred_contact_t was = k->c[i];
    memcpy(k->c[i].node, node_id, KINDRED_HEX_ID + 1u);
    k->c[i].stamp = stamp;
    k->notice[0] = 0;
    return commit(k, (uint32_t) i, &was);
}

/* ---- KM5: the owner ---- */

int kindred_me(kindred_t *k)
{
    static char next[KINDRED_FOLLOW][KINDRED_HEX_ID + 1];
    char me[KINDRED_HEX_ID + 1];
    zxm_link_t *l = k->link;
    zxm_begin(l, "chat.me");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "node_id", me, sizeof me) || kindred_id_code(me) < 0)
        return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, l->result, "following");
    uint32_t n = arr < 0 ? 0u : zxm_len(l, arr), kept = 0;
    for (uint32_t i = 0; i < n && kept < KINDRED_FOLLOW; i++) {
        int32_t t = zxm_at(l, arr, i);
        if (t < 0 || w4_json_str(l->resp, &l->tok[t], next[kept], sizeof next[kept]) < 0 ||
            kindred_id_code(next[kept]) < 0)
            return ZXM_E_PROTOCOL;
        kept++;
    }
    memcpy(k->me, me, sizeof me);
    memcpy(k->following, next, (size_t) kept * sizeof next[0]);
    k->nfollowing = kept;
    return ZXM_OK;
}

bool kindred_followed(const kindred_t *k, int64_t contact)
{
    const kindred_contact_t *c = kindred_get(k, contact);
    if (!c || !c->node[0]) return false;
    for (uint32_t i = 0; i < k->nfollowing; i++)
        if (!strcmp(k->following[i], c->node)) return true;
    return false;
}

/* ---- KM6: search and duplicates ---- */

int kindred_search(kindred_t *k, const char *typed)
{
    int64_t len = 0;
    uint32_t n = len_of(typed, KINDRED_NAME_MAX + 1u);
    if (!typed || n > KINDRED_NAME_MAX || !text_ok(typed, n)) return ZXM_E_ARG;
    if (!k->loaded) return ZXM_E_STATE;
    int64_t prefix = kindred_name_code(typed, n, &len);
    k->nhits = 0;
    for (uint32_t i = 0; i < k->n; i++) {
        int64_t hit = kindred_prefix_hit(k->c[i].name_code, prefix, len);
        if (hit < 0) return ZXM_E_PROTOCOL;
        if (hit) k->hit[k->nhits++] = i;
    }
    return ZXM_OK;
}

int kindred_find_dupes(kindred_t *k)
{
    static int64_t card[KINDRED_CONTACTS], name[KINDRED_CONTACTS];
    if (!k->loaded) return ZXM_E_STATE;
    for (uint32_t i = 0; i < k->n; i++) {
        card[i] = k->c[i].card_code;
        name[i] = k->c[i].name_code;
    }
    return kindred_dupes(card, name, k->n, k->dupes) ? ZXM_E_PROTOCOL : ZXM_OK;
}

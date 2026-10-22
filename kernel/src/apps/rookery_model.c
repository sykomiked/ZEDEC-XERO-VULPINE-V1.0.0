/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* rookery_model.c — Rookery, mail (rookery_model.h RK1-RK7). */
#include "rookery_model.h"

#include <stdio.h>
#include <string.h>

#include "coffer_model.h"

#define ROOKERY_ROOT "Rookery"
#define ROOKERY_EXT  ".letter"

static const char *const FOLDER[ROOKERY_FOLDERS] = {"Inbox", "Drafts", "Sent", "Held", "Trash"};
static const char *const STATUS[] = {"signed", "held", "denied"};
static const char SUBJECT[] = "Subject: ";

/* ---- small checks ---------------------------------------------------------- */

/* bytes of s up to most + 1, so a longer string is seen as too long */
static uint32_t len_upto(const char *s, uint32_t most)
{
    uint32_t n = 0;
    while (n <= most && s[n]) n++;
    return n;
}

static bool hex_of(const char *s, uint32_t len)
{
    if (!s || len_upto(s, len) != len) return false;
    for (uint32_t i = 0; i < len; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') ||
              (s[i] >= 'A' && s[i] <= 'F')))
            return false;
    return true;
}

/* 1..31 of a-z 0-9 . _ - (a card's name; a letter file's name, no leading '.') */
static bool name_ok(const char *s)
{
    uint32_t n = s ? len_upto(s, ROOKERY_NAME_MAX) : 0u;
    if (n < 1u || n > ROOKERY_NAME_MAX || s[0] == '.') return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
              s[i] == '_' || s[i] == '-'))
            return false;
    return true;
}

static bool now_ok(int64_t now)
{
    return now >= 0 && now <= ROOKERY_NOW_MAX;
}

static bool alnum(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* KR3: a byte of the local part (RFC 5322's atext, and '.') */
static bool local_byte(unsigned char c)
{
    return alnum(c) || (c && strchr("!#$%&'*+-/=?^_`{|}~.", c));
}

/* KR3: a byte of the domain */
static bool domain_byte(unsigned char c)
{
    return alnum(c) || c == '-' || c == '.';
}

/* a signed decimal of at most 19 digits that fits int64 */
static bool dec_i64(const char *s, int64_t *v)
{
    bool neg = *s == '-';
    uint64_t u = 0;
    uint32_t n = 0;
    if (neg) s++;
    for (; *s; s++, n++) {
        if (*s < '0' || *s > '9' || n >= 19u) return false;
        u = u * 10u + (uint64_t) (*s - '0');
    }
    if (!n || u > (uint64_t) INT64_MAX) return false;
    *v = neg ? -(int64_t) u : (int64_t) u;
    return true;
}

static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[24];
    if (zxm_get_str(l, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    return dec_i64(s, v) ? ZXM_OK : ZXM_E_PROTOCOL;
}

static int get_u32(const zxm_link_t *l, int32_t obj, const char *key, uint32_t *v)
{
    uint64_t u = 0;
    if (zxm_get_u64(l, obj, key, &u) != ZXM_OK || u > UINT32_MAX) return ZXM_E_PROTOCOL;
    *v = (uint32_t) u;
    return ZXM_OK;
}

static bool is_null(const zxm_link_t *l, int32_t obj, const char *key)
{
    int32_t t = zxm_get(l, obj, key);
    return t >= 0 && l->tok[t].type == W4_J_NULL;
}

static int32_t code_of(const char *s, const char *const *names, int32_t n)
{
    for (int32_t i = 0; i < n; i++)
        if (!strcmp(s, names[i])) return i;
    return -1;
}

/* ---- RK1: composing --------------------------------------------------------- */

void rookery_init(rookery_t *r, zxm_link_t *link)
{
    memset(r, 0, sizeof *r);
    r->link = link;
    r->draft.stamp = -1;
}

int64_t rookery_address(const char *addr, rookery_addr_t *out)
{
    rookery_addr_t a;
    memset(&a, 0, sizeof a);
    if (out) *out = a;
    if (!addr) return ROOKERY_REFUSED;
    uint32_t n = len_upto(addr, ROOKERY_ADDR_MAX);
    int64_t first = -1, last = -1;
    for (uint32_t i = 0; i < n; i++)
        if (addr[i] == '@') {
            if (first < 0) first = i;
            last = i;
            a.ats++;
        }
    a.size = n;
    a.local = first < 0 ? (int64_t) n : first;
    a.domain = last < 0 ? 0 : (int64_t) n - last - 1;
    for (uint32_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) addr[i];
        if (c == '@') continue;
        if ((int64_t) i > last && last >= 0 ? !domain_byte(c) : !local_byte(c)) a.bad++;
    }
    if (a.domain > 0) {
        int64_t label = 0;
        a.shortest = a.domain;
        for (uint32_t i = (uint32_t) (last + 1); i <= n; i++) {
            if (i == n || addr[i] == '.') {
                if (label > a.longest) a.longest = label;
                if (label < a.shortest) a.shortest = label;
                label = 0;
            } else {
                label++;
            }
        }
    }
    if (out) *out = a;
    if (n > ROOKERY_ADDR_MAX) return ROOKERY_ADDR_LENGTH; /* counted up to 255 */
    return rookery_addr_fault(a.size, a.ats, a.local, a.domain, a.longest, a.shortest, a.bad);
}

int rookery_compose(rookery_t *r, const char *addr, const char *subject, const char *body, bool pii,
                    int64_t amount, uint32_t thread)
{
    if (!subject || !body || amount < 0 || amount > ROOKERY_AMOUNT_MAX) return ZXM_E_ARG;
    if (rookery_address(addr, NULL) != ROOKERY_ADDR_FINE) return ZXM_E_ARG;
    uint32_t s = len_upto(subject, ROOKERY_SUBJECT_MAX), b = len_upto(body, ROOKERY_BODY_MAX);
    if (!rookery_letter_size_ok(s, b)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < s; i++)
        if ((unsigned char) subject[i] < 0x20u || subject[i] == 0x7f) return ZXM_E_ARG;
    /* checked whole; now kept */
    rookery_letter_t *d = &r->draft;
    memset(d, 0, sizeof *d);
    memcpy(d->addr, addr, len_upto(addr, ROOKERY_ADDR_MAX));
    memcpy(d->subject, subject, s);
    memcpy(d->body, body, b);
    d->pii = pii;
    d->amount = amount;
    d->thread = thread;
    d->folder = ROOKERY_DRAFTS;
    d->stamp = -1;
    d->size = (uint32_t) (sizeof SUBJECT - 1u) + s + ROOKERY_GAP + b;
    int k = snprintf(r->payload, sizeof r->payload, "%s%s\n\n%s", SUBJECT, d->subject, d->body);
    if (k < 0 || (uint32_t) k != d->size) return ZXM_E_TOOBIG;
    r->composed = true;
    return ZXM_OK;
}

/* ---- RK2: the recipient's card ---------------------------------------------- */

int rookery_check_card(rookery_t *r, const rookery_card_t *card)
{
    bool valid = false;
    char id[ROOKERY_HEX_ID + 1], why[ROOKERY_REASON_MAX + 1];
    if (!card || !name_ok(card->name) || !hex_of(card->pk_hex, ROOKERY_PK_HEX) ||
        !hex_of(card->sig_hex, ROOKERY_SIG_HEX) || !hex_of(card->agent_id, ROOKERY_HEX_ID))
        return ZXM_E_ARG;
    zxm_begin(r->link, "web4.card_verify");
    zxm_p_str(r->link, "name", card->name);
    zxm_p_u64(r->link, "bound_ms", card->bound_ms);
    zxm_p_str(r->link, "pk_hex", card->pk_hex);
    zxm_p_str(r->link, "bind_sig_hex", card->sig_hex);
    zxm_p_str(r->link, "agent_id", card->agent_id);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = r->link->result;
    why[0] = 0;
    if (zxm_get_bool(r->link, o, "valid", &valid) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (valid) {
        if (zxm_get_str(r->link, o, "agent_id", id, sizeof id) != ZXM_OK ||
            strcmp(id, card->agent_id))
            return ZXM_E_PROTOCOL;
    } else if (zxm_get_str(r->link, o, "reason", why, sizeof why) != ZXM_OK || !why[0]) {
        return ZXM_E_PROTOCOL;
    }
    memcpy(r->card_id, card->agent_id, sizeof r->card_id);
    r->verified = valid ? 1 : 0;
    memcpy(r->reason, why, sizeof r->reason);
    return ZXM_OK;
}

/* ---- RK3: posting, and the owner's consent ---------------------------------- */

/* one feed entry's id and status; RK3 and RK4 */
static int entry_head(const zxm_link_t *l, int32_t o, uint32_t *id, int32_t *status)
{
    char st[16];
    if (o < 0 || get_u32(l, o, "id", id) != ZXM_OK || *id == 0 ||
        zxm_get_str(l, o, "status", st, sizeof st) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    *status = code_of(st, STATUS, 3);
    return *status < 0 ? ZXM_E_PROTOCOL : ZXM_OK;
}

int rookery_send(rookery_t *r, const char *agent_id, bool consented, int64_t now)
{
    const rookery_letter_t *d = &r->draft;
    uint32_t id = 0;
    int32_t status = -1;
    if (!hex_of(agent_id, ROOKERY_HEX_ID) || (now != -1 && !now_ok(now))) return ZXM_E_ARG;
    if (!r->composed) return ZXM_E_STATE;
    /* KR1: a card checked for another companion verifies nothing */
    int64_t verified = strcmp(r->card_id, agent_id) ? 0 : r->verified;
    int64_t room = ROOKERY_PAYLOAD_MAX - (int64_t) d->size;
    int64_t gap = rookery_consent_gap(d->pii, d->amount, consented);
    if (!rookery_letter_send_ok(verified, room, gap)) return ZXM_E_STATE;
    zxm_begin(r->link, "web4.post");
    zxm_p_str(r->link, "kind", d->thread ? "RESPONSE" : "REQUEST");
    zxm_p_str(r->link, "to", agent_id);
    zxm_p_str(r->link, "tool", "mail");
    zxm_p_strn(r->link, "payload", r->payload, d->size);
    if (d->thread) zxm_p_u64(r->link, "req_id", d->thread);
    if (d->pii) zxm_p_bool(r->link, "pii", true);
    if (d->amount > 0) zxm_p_i64(r->link, "amount_minor", d->amount);
    if (now >= 0) zxm_p_u64(r->link, "now", (uint64_t) now);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    if (entry_head(r->link, r->link->result, &id, &status) != ZXM_OK) return ZXM_E_PROTOCOL;
    /* zxvd holds what needs consent, and signs the rest at once */
    bool held = d->pii || d->amount > 0;
    if (status != (held ? ROOKERY_ST_HELD : ROOKERY_ST_SIGNED)) return ZXM_E_PROTOCOL;
    r->posted = id;
    r->posted_status = (uint8_t) status;
    r->draft.id = id;
    r->draft.folder = held ? ROOKERY_HELD : ROOKERY_SENT;
    r->draft.status = (uint8_t) status;
    memcpy(r->draft.to, agent_id, sizeof r->draft.to);
    return ZXM_OK;
}

static int32_t find(const rookery_t *r, uint32_t id)
{
    for (uint32_t i = 0; i < r->n; i++)
        if (r->page[i].id == id) return (int32_t) i;
    return -1;
}

int rookery_consent(rookery_t *r, uint32_t id, bool grant, int64_t expires_in, int64_t now,
                    bool owner_confirmed)
{
    uint32_t got = 0;
    int32_t status = -1, at = find(r, id);
    bool mine = r->posted && id == r->posted;
    if ((at < 0 && !mine) || !now_ok(now) || expires_in < 1 || expires_in > ROOKERY_GRANT_MAX)
        return ZXM_E_ARG;
    uint8_t was = mine ? r->posted_status : r->page[at].status;
    if (was != ROOKERY_ST_HELD) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE; /* never by itself */
    zxm_begin(r->link, "web4.consent");
    zxm_p_u64(r->link, "id", id);
    zxm_p_bool(r->link, "grant", grant);
    zxm_p_u64(r->link, "expires_in", (uint64_t) expires_in);
    zxm_p_u64(r->link, "now", (uint64_t) now);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    int32_t e = zxm_get(r->link, r->link->result, "envelope");
    if (entry_head(r->link, e, &got, &status) != ZXM_OK || got != id ||
        status != (grant ? ROOKERY_ST_SIGNED : ROOKERY_ST_DENIED))
        return ZXM_E_PROTOCOL;
    uint8_t folder = grant ? ROOKERY_SENT : ROOKERY_TRASH;
    if (mine) {
        r->posted_status = (uint8_t) status;
        r->draft.status = (uint8_t) status;
        r->draft.folder = folder;
    }
    if (at >= 0) {
        r->page[at].status = (uint8_t) status;
        r->page[at].folder = folder;
        if (!grant) r->page[at].body[0] = r->page[at].subject[0] = 0; /* wiped */
    }
    return ZXM_OK;
}

/* ---- RK4: the feed ---------------------------------------------------------- */

/* a payload into its subject and body; one not in Rookery's form is all
 * body, cut at a character boundary to the body's limit */
static void split(const char *p, rookery_letter_t *e)
{
    uint32_t n = (uint32_t) strlen(p), h = (uint32_t) (sizeof SUBJECT - 1u);
    e->subject[0] = e->body[0] = 0;
    if (n >= h && !memcmp(p, SUBJECT, h)) {
        const char *gap = strstr(p + h, "\n\n");
        if (gap && (uint32_t) (gap - p - h) <= ROOKERY_SUBJECT_MAX &&
            n - (uint32_t) (gap - p) - ROOKERY_GAP <= ROOKERY_BODY_MAX) {
            memcpy(e->subject, p + h, (size_t) (gap - p - h));
            e->subject[gap - p - h] = 0;
            memcpy(e->body, gap + ROOKERY_GAP, n - (uint32_t) (gap - p) - ROOKERY_GAP + 1u);
            return;
        }
    }
    uint32_t b = zxm_utf8_cut(p, n < ROOKERY_BODY_MAX ? n : ROOKERY_BODY_MAX);
    memcpy(e->body, p, b);
    e->body[b] = 0;
}

/* one feed entry at o: 1 a letter (into e), 0 not a letter, or an error */
static int read_letter(const zxm_link_t *l, int32_t o, const char *own, rookery_letter_t *e)
{
    static char payload[ROOKERY_PAYLOAD_MAX + 1];
    char kind[16], tool[ROOKERY_NAME_MAX + 1], req[24];
    int32_t status = -1;
    int64_t req_id = 0, ts = 0;
    memset(e, 0, sizeof *e);
    if (entry_head(l, o, &e->id, &status) != ZXM_OK ||
        zxm_get_str(l, o, "kind", kind, sizeof kind) != ZXM_OK ||
        zxm_get_str(l, o, "tool", tool, sizeof tool) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    bool request = !strcmp(kind, "REQUEST");
    if (strcmp(tool, "mail") || (!request && strcmp(kind, "RESPONSE"))) return 0;
    if (zxm_get_str(l, o, "from", e->from, sizeof e->from) != ZXM_OK ||
        zxm_get_str(l, o, "to", e->to, sizeof e->to) != ZXM_OK ||
        !hex_of(e->from, ROOKERY_HEX_ID) || !hex_of(e->to, ROOKERY_HEX_ID) ||
        zxm_get_str(l, o, "req_id", req, sizeof req) != ZXM_OK || !dec_i64(req, &req_id) ||
        zxm_get_str(l, o, "payload", payload, sizeof payload) != ZXM_OK ||
        zxm_get_bool(l, o, "pii", &e->pii) != ZXM_OK ||
        get_dec(l, o, "amount_minor", &e->amount) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    e->stamp = -1;
    if (!is_null(l, o, "ts")) {
        if (zxm_get_i64(l, o, "ts", &ts) != ZXM_OK || !now_ok(ts)) return ZXM_E_PROTOCOL;
        e->stamp = ts;
    }
    /* a signed letter has its time; a held or denied one has none */
    if ((status == ROOKERY_ST_SIGNED) != (e->stamp >= 0)) return ZXM_E_PROTOCOL;
    /* a thread is a feed id: a REQUEST begins its own */
    if (req_id < 1 || req_id > ROOKERY_THREAD_MAX || (request && req_id != e->id))
        return ZXM_E_PROTOCOL;
    e->thread = (uint32_t) req_id;
    e->status = (uint8_t) status;
    e->size = (uint32_t) strlen(payload);
    if (own && !strcmp(e->from, own))
        e->folder = status == ROOKERY_ST_SIGNED ? ROOKERY_SENT
                    : status == ROOKERY_ST_HELD ? ROOKERY_HELD
                                                : ROOKERY_TRASH;
    else
        e->folder = ROOKERY_INBOX;
    if (!rookery_letter_record_ok(e->folder, e->thread, e->id, e->size,
                                  e->stamp < 0 ? 0 : e->stamp))
        return ZXM_E_PROTOCOL;
    split(payload, e);
    return 1;
}

int rookery_feed(rookery_t *r, uint32_t before, uint32_t limit, const char *own)
{
    static rookery_letter_t page[ROOKERY_FEED_MAX];
    uint32_t next = 0, kept = 0, last = 0;
    if (limit < 1u || limit > ROOKERY_FEED_MAX || (own && !hex_of(own, ROOKERY_HEX_ID)))
        return ZXM_E_ARG;
    zxm_begin(r->link, "web4.feed");
    if (before) zxm_p_u64(r->link, "before", before);
    zxm_p_u64(r->link, "limit", limit);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    const zxm_link_t *l = r->link;
    int32_t arr = zxm_get(l, l->result, "envelopes");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (n > limit) return ZXM_E_TOOBIG;
    if (!is_null(l, l->result, "next") && get_u32(l, l->result, "next", &next) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        uint32_t id = 0;
        int32_t st = -1;
        /* newest first, and only those asked for, letters or not */
        if (entry_head(l, o, &id, &st) != ZXM_OK || (i && id >= last) || (before && id >= before))
            return ZXM_E_PROTOCOL;
        last = id;
        rc = read_letter(l, o, own, &page[kept]);
        if (rc < 0) return rc;
        kept += (uint32_t) rc;
    }
    /* RK4: the whole page checked, now kept */
    memcpy(r->page, page, kept * sizeof page[0]);
    r->n = kept;
    r->next = next;
    return ZXM_OK;
}

uint32_t rookery_thread(const rookery_t *r, uint32_t thread, uint32_t *ids, uint32_t cap)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < r->n; i++)
        if (thread && r->page[i].thread == thread) {
            if (ids && k < cap) ids[k] = r->page[i].id;
            k++;
        }
    return k;
}

int rookery_verify(rookery_t *r, uint32_t id)
{
    bool valid = false;
    char why[ROOKERY_REASON_MAX + 1];
    int32_t at = find(r, id);
    if (at < 0) return ZXM_E_ARG;
    if (r->page[at].status != ROOKERY_ST_SIGNED) return ZXM_E_STATE;
    zxm_begin(r->link, "web4.verify");
    zxm_p_u64(r->link, "id", id);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = r->link->result;
    why[0] = 0;
    if (zxm_get_bool(r->link, o, "valid", &valid) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (!valid && (zxm_get_str(r->link, o, "reason", why, sizeof why) != ZXM_OK || !why[0]))
        return ZXM_E_PROTOCOL;
    r->sig_valid = valid;
    memcpy(r->reason, why, sizeof r->reason);
    return ZXM_OK;
}

/* ---- RK5: the Hoard --------------------------------------------------------- */

/* the names in folder path (dirs only when dirs) as a bit set of FOLDER;
 * -1 on an error */
static int32_t have_folders(zxm_link_t *l, const char *path, bool *rookery)
{
    char name[ROOKERY_NAME_MAX + 2];
    bool dir = false;
    int32_t have = 0;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", path);
    if (zxm_call(l) != ZXM_OK) return -1;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return -1;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_bool(l, o, "dir", &dir) != ZXM_OK) return -1;
        int g = zxm_get_str(l, o, "name", name, sizeof name);
        if (g == ZXM_E_TOOBIG || !dir) continue; /* not one of Rookery's */
        if (g != ZXM_OK) return -1;
        if (rookery && !strcmp(name, ROOKERY_ROOT)) *rookery = true;
        int32_t f = code_of(name, FOLDER, ROOKERY_FOLDERS);
        if (f >= 0) have |= 1 << f;
    }
    return have;
}

static int mkdir_(zxm_link_t *l, const char *path)
{
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", path);
    return zxm_call(l);
}

int rookery_folders(rookery_t *r)
{
    char path[ROOKERY_PATH_MAX + 1];
    bool root = false;
    if (have_folders(r->link, "", &root) < 0) return ZXM_E_PROTOCOL;
    if (!root) {
        int rc = mkdir_(r->link, ROOKERY_ROOT);
        if (rc != ZXM_OK) return rc;
    }
    int32_t have = have_folders(r->link, ROOKERY_ROOT, NULL);
    if (have < 0) return ZXM_E_PROTOCOL;
    for (int32_t f = 0; f < ROOKERY_FOLDERS; f++) {
        if (have & (1 << f)) continue;
        snprintf(path, sizeof path, "%s/%s", ROOKERY_ROOT, FOLDER[f]);
        int rc = mkdir_(r->link, path);
        if (rc != ZXM_OK) return rc;
    }
    return ZXM_OK;
}

int rookery_keep(rookery_t *r, uint8_t folder, const char *name)
{
    static char text[ROOKERY_PAYLOAD_MAX + 512];
    char path[ROOKERY_PATH_MAX + 1];
    uint64_t size = 0;
    if (folder >= ROOKERY_FOLDERS || !name_ok(name)) return ZXM_E_ARG;
    if (!r->composed) return ZXM_E_STATE;
    const rookery_letter_t *d = &r->draft;
    snprintf(path, sizeof path, "%s/%s/%s%s", ROOKERY_ROOT, FOLDER[folder], name, ROOKERY_EXT);
    int k =
        snprintf(text, sizeof text, "To: %s\nThread: %u\nPersonal: %s\nAmount: %lld\n%s", d->addr,
                 (unsigned) d->thread, d->pii ? "yes" : "no", (long long) d->amount, r->payload);
    if (k < 0 || (uint32_t) k >= sizeof text) return ZXM_E_TOOBIG;
    zxm_begin(r->link, "files.write");
    zxm_p_str(r->link, "path", path);
    zxm_p_strn(r->link, "text", text, (uint32_t) k);
    zxm_p_str(r->link, "mode", "replace");
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_u64(r->link, r->link->result, "size", &size) != ZXM_OK || size != (uint64_t) k)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int rookery_list(rookery_t *r, uint8_t folder, int64_t now)
{
    static rookery_file_t got[ROOKERY_FILES_MAX];
    char path[ROOKERY_PATH_MAX + 1], name[ROOKERY_NAME_MAX + sizeof ROOKERY_EXT + 1];
    uint32_t k = 0;
    if (folder >= ROOKERY_FOLDERS || !now_ok(now)) return ZXM_E_ARG;
    zxm_link_t *l = r->link;
    snprintf(path, sizeof path, "%s/%s", ROOKERY_ROOT, FOLDER[folder]);
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        bool dir = false;
        uint64_t size = 0;
        int64_t mtime = 0;
        if (o < 0 || zxm_get_bool(l, o, "dir", &dir) != ZXM_OK) return ZXM_E_PROTOCOL;
        int g = zxm_get_str(l, o, "name", name, sizeof name);
        if (g == ZXM_E_TOOBIG || dir) continue; /* no letter of Rookery's */
        uint32_t len = g == ZXM_OK ? (uint32_t) strlen(name) : 0u;
        uint32_t ext = (uint32_t) (sizeof ROOKERY_EXT - 1u);
        if (len <= ext || strcmp(name + len - ext, ROOKERY_EXT)) continue;
        name[len - ext] = 0;
        if (!name_ok(name)) continue;
        if (zxm_get_u64(l, o, "size", &size) != ZXM_OK ||
            zxm_get_i64(l, o, "mtime", &mtime) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (k == ROOKERY_FILES_MAX) return ZXM_E_TOOBIG;
        /* KR4: filed by folder, no thread, its place in the folder */
        if (size > (uint64_t) ROOKERY_SIZE_FIELD ||
            !rookery_letter_record_ok(folder, 0, k + 1u, (int64_t) size, mtime))
            return ZXM_E_PROTOCOL;
        memcpy(got[k].name, name, len - ext + 1u);
        got[k].size = (uint32_t) size;
        got[k].mtime = mtime;
        got[k].days = folder == ROOKERY_TRASH ? rookery_trash_days(mtime, now) : -1;
        k++;
    }
    memcpy(r->files, got, k * sizeof got[0]);
    r->nfiles = k;
    return ZXM_OK;
}

/* ---- RK6: money advice ------------------------------------------------------ */

int rookery_money_after(rookery_t *r, uint32_t account)
{
    int64_t d = 0, c = 0, q = 0;
    if (!r->composed || r->draft.amount <= 0) return ZXM_E_ARG;
    zxm_begin(r->link, "ledger.balance");
    zxm_p_u64(r->link, "account", account);
    int rc = zxm_call(r->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = r->link->result;
    if (get_dec(r->link, o, "debit_minor", &d) != ZXM_OK ||
        get_dec(r->link, o, "credit_minor", &c) != ZXM_OK ||
        get_dec(r->link, o, "equity_minor", &q) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    /* equity is debit - credit, as the books keep it */
    if ((c > 0 && d < INT64_MIN + c) || (c < 0 && d > INT64_MAX + c) || d - c != q)
        return ZXM_E_PROTOCOL;
    int64_t after = coffer_balance(q, -r->draft.amount);
    if (after == COFFER_REFUSED) return ZXM_E_TOOBIG;
    r->balance = q;
    r->after = after;
    return ZXM_OK;
}

/* ---- RK7: notices ----------------------------------------------------------- */

int rookery_notice(rookery_t *r)
{
    const rookery_letter_t *d = &r->draft;
    if (!r->posted || r->posted_status != ROOKERY_ST_HELD) return ZXM_E_STATE;
    int n;
    if (d->amount > 0)
        n = snprintf(r->notice, sizeof r->notice,
                     "Letter %u to %.64s waits for your consent: it moves %lld minor units%s. "
                     "Nothing is signed until you answer.",
                     (unsigned) r->posted, d->addr, (long long) d->amount,
                     d->pii ? " and carries personal data" : "");
    else
        n = snprintf(r->notice, sizeof r->notice,
                     "Letter %u to %.64s waits for your consent: it carries personal data. "
                     "Nothing is signed until you answer.",
                     (unsigned) r->posted, d->addr);
    return n < 0 || (uint32_t) n >= sizeof r->notice ? ZXM_E_TOOBIG : ZXM_OK;
}

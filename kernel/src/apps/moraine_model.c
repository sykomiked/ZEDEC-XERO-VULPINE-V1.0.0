/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* moraine_model.c — Moraine's model (moraine_model.h). */
#include <string.h>

#include "moraine_model.h"

#define DEC_MAX 19u
#define ADVICE  " Advice only: each guardian signs for themselves; nothing moves without your word."

/* ---- small helpers, bounded ---- */

static void say(moraine_t *m, const char *a, bool advice)
{
    size_t n = strlen(a), k = advice ? strlen(ADVICE) : 0;
    if (n + k + 1u > sizeof m->notice) {
        k = 0;
        if (n + 1u > sizeof m->notice) n = sizeof m->notice - 1u;
    }
    memcpy(m->notice, a, n);
    if (k) memcpy(m->notice + n, ADVICE, k);
    m->notice[n + k] = 0;
}

static bool is_hex(const char *s, uint32_t n)
{
    if (!s) return false;
    size_t len = strlen(s);
    if (n ? len != n : (len < 2u || len > MORAINE_MSG_HEX || len % 2u)) return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[DEC_MAX + 2];
    if (zxm_get_str(l, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    size_t n = strlen(s);
    if (n == 0 || n > DEC_MAX) return ZXM_E_PROTOCOL;
    uint64_t x = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return ZXM_E_PROTOCOL;
        uint64_t d = (uint64_t) (s[i] - '0');
        if (x > ((uint64_t) INT64_MAX - d) / 10u) return ZXM_E_PROTOCOL;
        x = x * 10u + d;
    }
    *v = (int64_t) x;
    return ZXM_OK;
}

static int get_u32(const zxm_link_t *l, int32_t obj, const char *key, uint32_t *v)
{
    uint64_t x = 0;
    if (zxm_get_u64(l, obj, key, &x) != ZXM_OK || x > UINT32_MAX) return ZXM_E_PROTOCOL;
    *v = (uint32_t) x;
    return ZXM_OK;
}

static bool name_ok(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > MORAINE_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7f) return false;
    return true;
}

static bool pending(const moraine_t *m)
{
    return m->set_up && !strcmp(m->state, "pending");
}

static void clear_words(moraine_t *m)
{
    for (int g = 0; g < MORAINE_GUARDIANS; g++) m->word[g] = LPRES_STATE_NEITHER;
    m->last_approver = -1;
}

/* A claim's answer (recovery.claim, approve, veto, finalize, cancel):
 * state, claim, approvals and vetoed, all read before any is kept. */
static int read_claim(moraine_t *m, const char *want)
{
    zxm_link_t *l = m->link;
    char state[MORAINE_STATE_MAX + 1];
    int64_t claim = 0;
    uint32_t a = 0;
    bool vetoed = false;
    if (zxm_get_str(l, l->result, "state", state, sizeof state) != ZXM_OK ||
        (want && strcmp(state, want)) || get_dec(l, l->result, "claim", &claim) != ZXM_OK ||
        get_u32(l, l->result, "approvals", &a) != ZXM_OK || a > MORAINE_GUARDIANS ||
        zxm_get_bool(l, l->result, "vetoed", &vetoed) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    memcpy(m->state, state, sizeof state);
    m->claim = claim;
    m->approvals = a;
    m->vetoed = vetoed;
    m->needed = vetoed ? MORAINE_VETO_Q : MORAINE_QUORUM;
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

/* ---- MM1 ---- */

void moraine_init(moraine_t *m, zxm_link_t *link)
{
    if (!m) return;
    memset(m, 0, sizeof *m);
    m->link = link;
    m->needed = MORAINE_QUORUM;
    m->claim_grant = -1;
    memcpy(m->state, "idle", 5);
    clear_words(m);
    say(m, "No guardians yet: set them up to hold keys in escrow.", true);
}

int moraine_setup(moraine_t *m, int64_t escrow, const char *root, const char *guardians,
                  const char *gateway, const char *first_device, int64_t timelock,
                  const int64_t parent[MORAINE_GUARDIANS], int64_t now, bool owner_confirmed)
{
    int64_t rank[MORAINE_GUARDIANS];
    if (!m || !m->link || escrow < 1 || escrow > MORAINE_ESCROW_MAX ||
        !is_hex(root, MORAINE_KEY_HEX) || !is_hex(guardians, MORAINE_GUARDIANS * MORAINE_KEY_HEX) ||
        !is_hex(gateway, MORAINE_KEY_HEX) || !is_hex(first_device, MORAINE_KEY_HEX) ||
        timelock < 1 || timelock > MORAINE_STAMP_MAX || now < 0 || now > MORAINE_STAMP_MAX ||
        !moraine_hierarchy(parent, rank))
        return ZXM_E_ARG;
    for (int g = 0; g < MORAINE_GUARDIANS; g++)
        if (!moraine_node_ok(escrow, g + 1, parent[g], rank[g], now)) return ZXM_E_ARG;
    if (m->set_up) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(m, "Nothing set: the guardians need your confirmed word.", true);
        return ZXM_E_STATE;
    }
    zxm_link_t *l = m->link;
    zxm_begin(l, "recovery.setup");
    zxm_p_str(l, "root", root);
    zxm_p_str(l, "guardians", guardians);
    zxm_p_str(l, "gateway", gateway);
    zxm_p_str(l, "first_device", first_device);
    zxm_p_i64(l, "timelock", timelock);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    char state[MORAINE_STATE_MAX + 1];
    int64_t lock = 0;
    if (get_dec(l, l->result, "timelock", &lock) != ZXM_OK || lock != timelock ||
        zxm_get_str(l, l->result, "state", state, sizeof state) != ZXM_OK || strcmp(state, "idle"))
        return ZXM_E_PROTOCOL;
    m->escrow = escrow;
    m->set_up = true;
    m->timelock = lock;
    m->added = now;
    for (int g = 0; g < MORAINE_GUARDIANS; g++) {
        m->parent[g] = parent[g];
        m->rank[g] = rank[g];
    }
    memcpy(m->state, state, sizeof state);
    clear_words(m);
    say(m, "Five guardians hold the escrow: a release needs 3 of their signatures and the wait.",
        true);
    return ZXM_OK;
}

int moraine_refresh(moraine_t *m, int64_t now)
{
    if (!m || !m->link || now < 0 || now > MORAINE_STAMP_MAX) return ZXM_E_ARG;
    zxm_link_t *l = m->link;
    zxm_begin(l, "recovery.status");
    zxm_p_i64(l, "now", now);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    bool set_up = false, vetoed = false;
    if (zxm_get_bool(l, l->result, "set_up", &set_up) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (!set_up) {
        if (m->set_up) return ZXM_E_PROTOCOL; /* zxvd forgot guardians it was given */
        say(m, "No guardians yet: set them up to hold keys in escrow.", true);
        return ZXM_OK;
    }
    char state[MORAINE_STATE_MAX + 1];
    int64_t claim = 0, opened = 0;
    uint32_t a = 0, needed = 0;
    if (zxm_get_str(l, l->result, "state", state, sizeof state) != ZXM_OK ||
        get_dec(l, l->result, "claim", &claim) != ZXM_OK ||
        get_u32(l, l->result, "approvals", &a) != ZXM_OK || a > MORAINE_GUARDIANS ||
        get_u32(l, l->result, "needed", &needed) != ZXM_OK || needed < 1 ||
        needed > MORAINE_GUARDIANS || zxm_get_bool(l, l->result, "vetoed", &vetoed) != ZXM_OK ||
        get_dec(l, l->result, "opened", &opened) != ZXM_OK || opened > MORAINE_STAMP_MAX)
        return ZXM_E_PROTOCOL;
    if (strcmp(state, "idle") && strcmp(state, "pending") && strcmp(state, "thawing"))
        return ZXM_E_PROTOCOL;
    bool fresh = !m->set_up || claim != m->claim;
    m->set_up = true;
    memcpy(m->state, state, sizeof state);
    m->claim = claim;
    m->approvals = a;
    m->needed = needed;
    m->vetoed = vetoed;
    m->opened = opened;
    if (fresh) clear_words(m); /* a claim this model did not open: no word known */
    say(m,
        !strcmp(state, "pending") ? "A release is asked: the guardians are signing, and the wait "
                                    "runs."
                                  : "No release is asked.",
        true);
    return ZXM_OK;
}

/* ---- MM2 ---- */

int moraine_add_key(moraine_t *m, const char *id, const char *name, int64_t chars, int64_t alphabet,
                    int64_t length, int64_t classes, int64_t repeats, int64_t expires,
                    bool owner_confirmed, uint32_t *index)
{
    if (!m || !index || !is_hex(id, MORAINE_KEY_HEX) || !name_ok(name)) return ZXM_E_ARG;
    int64_t bits = warding_entropy_bits(chars, alphabet);
    if (bits == WARDING_REFUSED || !lockbox_secret_ok(length, classes, repeats) ||
        lockbox_days_left(expires, 0) == LOCKBOX_REFUSED)
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < m->nkey; i++)
        if (!strcmp(m->key[i].id, id)) return ZXM_E_ARG;
    if (m->nkey >= MORAINE_KEYS) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(m, "Not kept: a key goes into escrow only on your confirmed word.", true);
        return ZXM_E_STATE;
    }
    moraine_key_t *k = &m->key[m->nkey];
    memset(k, 0, sizeof *k);
    memcpy(k->id, id, MORAINE_KEY_HEX + 1u);
    memcpy(k->name, name, strlen(name) + 1u);
    k->chars = chars;
    k->alphabet = alphabet;
    k->bits = bits;
    k->length = length;
    k->classes = classes;
    k->repeats = repeats;
    k->expires = expires;
    *index = m->nkey++;
    say(m,
        bits < WARDING_BITS_MIN ? "Kept in escrow; its bits are below 128: think of a longer key."
                                : "Kept in escrow, by its public id and its counts only.",
        true);
    return ZXM_OK;
}

int64_t moraine_key_score(const moraine_t *m, uint32_t key)
{
    if (!m || key >= m->nkey) return MORAINE_REFUSED;
    return moraine_key_strength(m->key[key].bits, (int64_t) m->needed, MORAINE_GUARDIANS);
}

int64_t moraine_key_days(const moraine_t *m, uint32_t key, int64_t now)
{
    if (!m || key >= m->nkey) return MORAINE_REFUSED;
    return lockbox_days_left(m->key[key].expires, now);
}

/* ---- MM3 ---- */

int moraine_grant(moraine_t *m, int64_t requester, uint32_t key, int64_t asked, int64_t granted,
                  int64_t revoked, int64_t until, bool owner_confirmed)
{
    if (!m || requester < 1 || requester > MORAINE_REQ_MAX || key >= m->nkey ||
        !gatehouse_flag(asked) || !gatehouse_flag(granted) || !gatehouse_flag(revoked) ||
        until < 1 || until > MORAINE_STAMP_MAX)
        return ZXM_E_ARG;
    int32_t at = -1;
    for (uint32_t i = 0; i < m->ngrant; i++)
        if (m->grant[i].requester == requester && m->grant[i].key == key) at = (int32_t) i;
    if (at < 0 && m->ngrant >= MORAINE_GRANTS) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(m, "Not changed: access to a key needs your confirmed word.", true);
        return ZXM_E_STATE;
    }
    moraine_grant_t *g = &m->grant[at < 0 ? m->ngrant++ : (uint32_t) at];
    g->requester = requester;
    g->key = key;
    g->asked = asked;
    g->granted = granted;
    g->revoked = revoked;
    g->until = until;
    say(m,
        gatehouse_use_ok(asked, granted, revoked) ? "Access granted until the window ends."
                                                  : "Access is not allowed.",
        true);
    return ZXM_OK;
}

bool moraine_grant_live(const moraine_t *m, uint32_t grant, int64_t now)
{
    if (!m || grant >= m->ngrant) return false;
    const moraine_grant_t *g = &m->grant[grant];
    return gatehouse_use_ok(g->asked, g->granted, g->revoked) && g->until > now;
}

int64_t moraine_live_grants(const moraine_t *m, int64_t requester, uint32_t key, int64_t now)
{
    int64_t holder[MORAINE_GRANTS], item[MORAINE_GRANTS], live[MORAINE_GRANTS];
    int32_t row = -1;
    if (!m) return MORAINE_REFUSED;
    for (uint32_t i = 0; i < m->ngrant; i++) {
        holder[i] = m->grant[i].requester;
        item[i] = (int64_t) m->grant[i].key + 1;
        live[i] = moraine_grant_live(m, i, now) ? 1 : 0;
        if (holder[i] == requester && m->grant[i].key == key) row = (int32_t) i;
    }
    if (row < 0) return 0;
    return warding_grid_cell(holder, item, live, m->ngrant, (uint32_t) row);
}

/* ---- MM4 ---- */

static int32_t grant_of(const moraine_t *m, int64_t requester, uint32_t key)
{
    for (uint32_t i = 0; i < m->ngrant; i++)
        if (m->grant[i].requester == requester && m->grant[i].key == key) return (int32_t) i;
    return -1;
}

int moraine_claim(moraine_t *m, uint32_t key, int64_t requester, const char *new_key, int64_t now,
                  bool owner_confirmed)
{
    if (!m || !m->link || key >= m->nkey || requester < 1 || requester > MORAINE_REQ_MAX ||
        !is_hex(new_key, MORAINE_KEY_HEX) || now < 0 || now > MORAINE_STAMP_MAX)
        return ZXM_E_ARG;
    int32_t g = grant_of(m, requester, key);
    if (!m->set_up || pending(m) || m->key[key].released) return ZXM_E_STATE;
    if (g < 0 || !moraine_grant_live(m, (uint32_t) g, now)) {
        say(m, "Not asked: this requester holds no live access to that key.", true);
        return ZXM_E_STATE;
    }
    if (!owner_confirmed) {
        say(m, "Not asked: a release is asked only on your confirmed word.", true);
        return ZXM_E_STATE;
    }
    zxm_link_t *l = m->link;
    zxm_begin(l, "recovery.claim");
    zxm_p_str(l, "new_key", new_key);
    zxm_p_i64(l, "now", now);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    rc = read_claim(m, "pending");
    if (rc != ZXM_OK) return rc;
    m->opened = now;
    m->claim_grant = g;
    clear_words(m);
    say(m, "A release is asked: every guardian is told, and nothing is released before the wait.",
        true);
    return ZXM_OK;
}

int moraine_message(moraine_t *m, const char *kind, char *out, uint32_t cap)
{
    if (!m || !m->link || !kind || !out || cap < MORAINE_MSG_HEX + 1u) return ZXM_E_ARG;
    if (strcmp(kind, "approve") && strcmp(kind, "veto") && strcmp(kind, "cancel")) return ZXM_E_ARG;
    if (!m->set_up) return ZXM_E_STATE;
    zxm_begin(m->link, "recovery.message");
    zxm_p_str(m->link, "kind", kind);
    int rc = zxm_call(m->link);
    if (rc != ZXM_OK) return rc;
    char s[MORAINE_MSG_HEX + 2];
    if (zxm_get_str(m->link, m->link->result, "message", s, sizeof s) != ZXM_OK || !is_hex(s, 0))
        return ZXM_E_PROTOCOL;
    memcpy(out, s, strlen(s) + 1u);
    return ZXM_OK;
}

/* ---- MM5 ---- */

static int64_t wait_of(const moraine_t *m)
{
    return m->vetoed && MORAINE_VETO_WAIT > m->timelock ? MORAINE_VETO_WAIT : m->timelock;
}

int moraine_approve(moraine_t *m, uint32_t guardian, const char *sig, bool owner_confirmed)
{
    if (!m || !m->link || guardian >= MORAINE_GUARDIANS || !is_hex(sig, MORAINE_SIG_HEX))
        return ZXM_E_ARG;
    if (!pending(m)) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(m, "Not recorded: a guardian's approval is passed on only on your confirmed word.",
            true);
        return ZXM_E_STATE;
    }
    zxm_link_t *l = m->link;
    zxm_begin(l, "recovery.approve");
    zxm_p_u64(l, "guardian", guardian);
    zxm_p_str(l, "sig", sig);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    rc = read_claim(m, "pending");
    if (rc != ZXM_OK) return rc;
    m->word[guardian] = lpres_kjoin(m->word[guardian], LPRES_STATE_TRUE);
    m->last_approver = (int32_t) guardian;
    say(m,
        moraine_claim_word(m) == LPRES_STATE_BOTH
            ? "Held: one guardian approves and another objects. The key stays in escrow."
            : "A guardian approved the release.",
        true);
    return ZXM_OK;
}

int moraine_object(moraine_t *m, uint32_t guardian, int64_t now, bool owner_confirmed)
{
    if (!m || guardian >= MORAINE_GUARDIANS || now < 0 || now > MORAINE_STAMP_MAX) return ZXM_E_ARG;
    if (!pending(m) || now < m->opened || now - m->opened >= wait_of(m)) {
        say(m, "Not recorded: an objection counts only within the waiting period.", true);
        return ZXM_E_STATE;
    }
    if (!owner_confirmed) {
        say(m, "Not recorded: a guardian's objection is kept only on your confirmed word.", true);
        return ZXM_E_STATE;
    }
    m->word[guardian] = lpres_kjoin(m->word[guardian], LPRES_STATE_FALSE);
    say(m,
        moraine_claim_word(m) == LPRES_STATE_BOTH
            ? "Held: one guardian approves and another objects. The key is neither released "
              "nor lost until it is resolved."
            : "A guardian objects: nothing is released.",
        true);
    return ZXM_OK;
}

int moraine_veto(moraine_t *m, const char *device, const char *sig, bool owner_confirmed)
{
    if (!m || !m->link || !is_hex(device, MORAINE_KEY_HEX) || !is_hex(sig, MORAINE_SIG_HEX))
        return ZXM_E_ARG;
    if (!pending(m)) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(m, "Not sent: a device's veto goes on only on your confirmed word.", true);
        return ZXM_E_STATE;
    }
    zxm_link_t *l = m->link;
    zxm_begin(l, "recovery.veto");
    zxm_p_str(l, "device", device);
    zxm_p_str(l, "sig", sig);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    rc = read_claim(m, "pending");
    if (rc != ZXM_OK) return rc;
    if (!m->vetoed) return ZXM_E_PROTOCOL;
    say(m, "A device you hold vetoed it: a release now needs 4 approvals and 14 days.", true);
    return ZXM_OK;
}

lpres_state_t moraine_claim_word(const moraine_t *m)
{
    return m ? moraine_word(m->word) : LPRES_STATE_NEITHER;
}

/* ---- MM6 ---- */

bool moraine_due(const moraine_t *m, int64_t now)
{
    if (!m || !pending(m) || now < 0 || now > MORAINE_STAMP_MAX) return false;
    lpres_state_t w = moraine_claim_word(m);
    int64_t objected = w == LPRES_STATE_FALSE || w == LPRES_STATE_BOTH ? 1 : 0;
    int64_t k = (int64_t) m->needed;
    return heirloom_release_due(1, k, (int64_t) m->approvals - k, now - m->opened - wait_of(m),
                                objected);
}

bool moraine_release_ready(const moraine_t *m, int64_t now)
{
    if (!m || m->claim_grant < 0 || (uint32_t) m->claim_grant >= m->ngrant) return false;
    const moraine_grant_t *g = &m->grant[m->claim_grant];
    int64_t allowed = gatehouse_use_ok(g->asked, g->granted, g->revoked) ? 1 : 0;
    return moraine_claim_word(m) == LPRES_STATE_TRUE &&
           moraine_release_now(moraine_due(m, now) ? 1 : 0, g->until - now, allowed);
}

/* ---- MM7 ---- */

int moraine_release(moraine_t *m, int64_t now, bool owner_confirmed)
{
    if (!m || !m->link || now < 0 || now > MORAINE_STAMP_MAX) return ZXM_E_ARG;
    if (!pending(m)) return ZXM_E_STATE;
    if (moraine_claim_word(m) == LPRES_STATE_BOTH) {
        say(m,
            "Held: one guardian approves and another objects. The key is neither released nor "
            "lost.",
            true);
        return ZXM_E_STATE;
    }
    if (!moraine_release_ready(m, now)) {
        say(m, "Not yet: the release is not due, the window is over, or access is not allowed.",
            true);
        return ZXM_E_STATE;
    }
    /* the custody entry, checked whole before anything is sent */
    if (m->ncustody >= MORAINE_CUSTODY) return ZXM_E_STATE;
    const moraine_custody_t *last = m->ncustody ? &m->custody[m->ncustody - 1u] : 0;
    int64_t prev = last ? last->number : 0, prior = last ? last->stamp : 0;
    int64_t number = prev + 1, guardian = m->last_approver;
    if (moraine_custody_ok(number, prev, guardian, now, prior) != 1 ||
        !witness_record_fits(number, prev, MORAINE_KIND, now * 1000) ||
        !witness_linked(number, prev, prev))
        return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(m, "Not released: a key leaves escrow only on your confirmed word.", true);
        return ZXM_E_STATE;
    }
    zxm_link_t *l = m->link;
    zxm_begin(l, "recovery.finalize");
    zxm_p_i64(l, "now", now);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    rc = read_claim(m, "thawing");
    if (rc != ZXM_OK) return rc;
    /* released: it is kept whatever the journal answers */
    const moraine_grant_t *g = &m->grant[m->claim_grant];
    moraine_custody_t *c = &m->custody[m->ncustody++];
    c->number = number;
    c->prev = prev;
    c->guardian = guardian;
    c->stamp = now;
    c->requester = g->requester;
    c->key = g->key;
    c->seq = 0;
    m->key[g->key].released = true;
    m->claim_grant = -1;
    clear_words(m);
    int64_t seq = 0;
    if (journal_newest(l, &seq) == ZXM_OK) c->seq = seq;
    say(m,
        c->seq ? "Released, and entered in the custody register under the journal's seq."
               : "Released, and entered in the custody register; no journal to stamp it.",
        true);
    return ZXM_OK;
}

lpres_state_t moraine_register_intact(const moraine_t *m)
{
    if (!m || !m->ncustody) return LPRES_STATE_NEITHER;
    int64_t last = 0, prior = 0;
    for (uint32_t i = 0; i < m->ncustody; i++) {
        const moraine_custody_t *c = &m->custody[i];
        if (!witness_linked(c->number, c->prev, last) ||
            !witness_record_fits(c->number, c->prev, MORAINE_KIND, c->stamp * 1000) ||
            moraine_custody_ok(c->number, c->prev, c->guardian, c->stamp, prior) != 1)
            return LPRES_STATE_FALSE;
        last = c->number;
        prior = c->stamp;
    }
    return LPRES_STATE_TRUE;
}

/* ---- MM8 ---- */

int moraine_cancel(moraine_t *m, const char *device, const char *sig, bool owner_confirmed)
{
    if (!m || !m->link || !is_hex(device, MORAINE_KEY_HEX) || !is_hex(sig, MORAINE_SIG_HEX))
        return ZXM_E_ARG;
    if (!pending(m)) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(m, "Not cancelled: that needs your confirmed word.", true);
        return ZXM_E_STATE;
    }
    zxm_link_t *l = m->link;
    zxm_begin(l, "recovery.cancel");
    zxm_p_str(l, "device", device);
    zxm_p_str(l, "sig", sig);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    rc = read_claim(m, "idle");
    if (rc != ZXM_OK) return rc;
    m->claim_grant = -1;
    clear_words(m);
    say(m, "The release is cancelled: the key stays in escrow.", true);
    return ZXM_OK;
}

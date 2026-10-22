/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* heirloom_model.c — Heirloom's model (heirloom_model.h HM1-HM7). */
#include <stdio.h>
#include <string.h>

#include "heirloom_model.h"

#define DEC_MAX 19u /* digits of a 64-bit decimal string */

static void say(heirloom_t *h, const char *s)
{
    size_t n = strlen(s);
    if (n > HEIRLOOM_NOTICE_MAX) n = HEIRLOOM_NOTICE_MAX;
    memcpy(h->notice, s, n);
    h->notice[n] = 0;
}

/* a decimal string of 1..19 digits that fits int64 */
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

/* exactly n hex digits (n 0: any even count of 2..HEIRLOOM_MSG_HEX) */
static bool is_hex(const char *s, uint32_t n)
{
    if (!s) return false;
    size_t len = strlen(s);
    if (n ? len != n : (len < 2 || len > HEIRLOOM_MSG_HEX || len % 2u)) return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

/* keys back to back: 1..most keys of HEIRLOOM_KEY_HEX digits */
static bool is_keys(const char *s, uint32_t most)
{
    if (!s) return false;
    size_t len = strlen(s);
    if (len == 0 || len % HEIRLOOM_KEY_HEX || len / HEIRLOOM_KEY_HEX > most) return false;
    for (size_t i = 0; i < len; i += HEIRLOOM_KEY_HEX) {
        char one[HEIRLOOM_KEY_HEX + 1];
        memcpy(one, s + i, HEIRLOOM_KEY_HEX);
        one[HEIRLOOM_KEY_HEX] = 0;
        if (!is_hex(one, HEIRLOOM_KEY_HEX)) return false;
    }
    return true;
}

/* {"phase":..,"epoch":".."}: the phase and epoch an estate step answers */
static int read_phase(heirloom_t *h, char *phase, int64_t *epoch)
{
    if (zxm_get_str(h->link, h->link->result, "phase", phase, HEIRLOOM_PHASE_MAX + 1) != ZXM_OK ||
        get_dec(h->link, h->link->result, "epoch", epoch) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

void heirloom_init(heirloom_t *h, zxm_link_t *link)
{
    if (!h) return;
    memset(h, 0, sizeof *h);
    h->link = link;
    coffer_init(&h->books, link, COFFER_DAY_CAP);
    h->amount = -1;
    h->claim = -1;
}

int heirloom_setup(heirloom_t *h, const heirloom_setup_t *s)
{
    if (!h || !h->link || !s) return ZXM_E_ARG;
    if (!is_hex(s->owner, HEIRLOOM_KEY_HEX) || !is_keys(s->guardians, HEIRLOOM_GUARDIANS) ||
        !is_keys(s->attestors, 4u) || !is_hex(s->court, HEIRLOOM_KEY_HEX) ||
        !is_hex(s->gateway, HEIRLOOM_KEY_HEX))
        return ZXM_E_ARG;
    uint32_t g = (uint32_t) (strlen(s->guardians) / HEIRLOOM_KEY_HEX);
    /* HR3: a quorum of 1 .. the guardians named; time alone never releases */
    if (s->k < 1 || s->k > g || s->wait < 0 || s->wait > HEIRLOOM_EIGHTEEN || s->release < 0 ||
        s->release > HEIRLOOM_EIGHTEEN)
        return ZXM_E_ARG;
    zxm_begin(h->link, "estate.setup");
    zxm_p_str(h->link, "owner", s->owner);
    zxm_p_str(h->link, "guardians", s->guardians);
    zxm_p_u64(h->link, "k", s->k);
    zxm_p_str(h->link, "attestors", s->attestors);
    zxm_p_str(h->link, "court", s->court);
    zxm_p_str(h->link, "gateway", s->gateway);
    zxm_p_i64(h->link, "wait", s->wait);
    zxm_p_i64(h->link, "release", s->release);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = h->link->result;
    uint32_t gg = 0, kk = 0;
    int64_t wait = 0, release = 0;
    char phase[HEIRLOOM_PHASE_MAX + 1];
    if (get_u32(h->link, r, "guardians", &gg) != ZXM_OK || gg != g ||
        get_u32(h->link, r, "k", &kk) != ZXM_OK || kk != s->k ||
        get_dec(h->link, r, "wait", &wait) != ZXM_OK || wait != s->wait ||
        get_dec(h->link, r, "release", &release) != ZXM_OK || release != s->release ||
        zxm_get_str(h->link, r, "phase", phase, sizeof phase) != ZXM_OK || strcmp(phase, "alive"))
        return ZXM_E_PROTOCOL;
    h->set_up = true;
    h->guardians = g;
    h->k = kk;
    h->wait = wait;
    h->release = release;
    h->epoch = 0;
    memcpy(h->phase, phase, sizeof phase);
    say(h, "The estate is set up: a death report needs your guardians' signatures and the "
           "waiting period, and your own signature cancels it.");
    return ZXM_OK;
}

/* the heirs array of an answer into heir[] (n of them): ZXM_OK or ZXM_E_PROTOCOL */
static int read_heirs(heirloom_t *h, int32_t arr, heirloom_heir_t *heir, uint32_t *n, bool full)
{
    if (arr < 0) return ZXM_E_PROTOCOL;
    uint32_t len = zxm_len(h->link, arr);
    if (len == 0 || len > HEIRLOOM_HEIRS) return ZXM_E_PROTOCOL;
    int64_t num[HEIRLOOM_HEIRS], den[HEIRLOOM_HEIRS], gap = 0;
    for (uint32_t i = 0; i < len; i++) {
        int32_t o = zxm_at(h->link, arr, i);
        heirloom_heir_t x;
        memset(&x, 0, sizeof x);
        if (get_u32(h->link, o, "id", &x.id) != ZXM_OK ||
            get_dec(h->link, o, "num", &x.num) != ZXM_OK ||
            get_dec(h->link, o, "den", &x.den) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (full && (zxm_get_bool(h->link, o, "contested", &x.contested) != ZXM_OK ||
                     get_dec(h->link, o, "escrow_minor", &x.escrow) != ZXM_OK))
            return ZXM_E_PROTOCOL;
        num[i] = x.num;
        den[i] = x.den;
        heir[i] = x;
    }
    /* HR2: what zxvd keeps is whole too */
    if (heirloom_manifest_gap(num, den, len, &gap) != 0 || gap != 0) return ZXM_E_PROTOCOL;
    *n = len;
    return ZXM_OK;
}

int heirloom_set_will(heirloom_t *h, const uint32_t *id, const int64_t *num, const int64_t *den,
                      uint32_t n)
{
    if (!h || !h->link || !id || !num || !den || n == 0 || n > HEIRLOOM_HEIRS) return ZXM_E_ARG;
    if (!h->set_up) return ZXM_E_STATE;
    int64_t gap = 0;
    if (heirloom_manifest_gap(num, den, n, &gap) != 0) {
        say(h, "A share is not a part of the whole (each between 0 and 1, at most nine digits), "
               "or the shares pass one whole: nothing was sent.");
        return ZXM_E_ARG;
    }
    if (gap != 0) {
        say(h, gap < 0 ? "The shares come short of one whole: give the rest to someone. Nothing "
                         "was sent."
                       : "The shares pass one whole: take some back. Nothing was sent.");
        return ZXM_E_ARG;
    }
    char p[1400];
    int k = snprintf(p, sizeof p, "{\"enclave\":\"common\",\"heirs\":[");
    for (uint32_t i = 0; i < n && k > 0 && (size_t) k < sizeof p; i++)
        k += snprintf(p + k, sizeof p - (size_t) k, "%s{\"id\":%u,\"num\":%lld,\"den\":%lld}",
                      i ? "," : "", (unsigned) id[i], (long long) num[i], (long long) den[i]);
    if (k > 0 && (size_t) k < sizeof p) k += snprintf(p + k, sizeof p - (size_t) k, "]}");
    if (k <= 0 || (size_t) k >= sizeof p) return ZXM_E_ARG;
    zxm_begin(h->link, "estate.manifest");
    zxm_p_raw(h->link, p, (uint32_t) k);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    heirloom_heir_t got[HEIRLOOM_HEIRS];
    uint32_t m = 0;
    bool advisory = true;
    if (zxm_get_bool(h->link, h->link->result, "advisory", &advisory) != ZXM_OK || advisory ||
        read_heirs(h, zxm_get(h->link, h->link->result, "heirs"), got, &m, false) != ZXM_OK ||
        m != n)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++)
        /* the same share, cross-multiplied: num / den = got.num / got.den */
        if (got[i].id != id[i] || got[i].num * den[i] != num[i] * got[i].den) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) h->heir[i] = got[i];
    h->n = n;
    h->advisory = false;
    say(h, "Your will is saved: the shares make exactly one whole. It is fixed only while you "
           "live, and only you can change it.");
    return ZXM_OK;
}

int heirloom_faraid(heirloom_t *h, const heirloom_family_t *f, int64_t amount)
{
    if (!h || !h->link || !f || amount < -1) return ZXM_E_ARG;
    zxm_begin(h->link, "estate.faraid");
    zxm_p_bool(h->link, "husband", f->husband);
    zxm_p_u64(h->link, "wives", f->wives);
    zxm_p_bool(h->link, "father", f->father);
    zxm_p_bool(h->link, "mother", f->mother);
    zxm_p_u64(h->link, "sons", f->sons);
    zxm_p_u64(h->link, "daughters", f->daughters);
    if (amount >= 0) zxm_p_i64(h->link, "amount_minor", amount);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = h->link->result, arr = zxm_get(h->link, r, "classes");
    if (arr < 0) return ZXM_E_PROTOCOL;
    uint32_t len = zxm_len(h->link, arr);
    if (len == 0 || len > HEIRLOOM_CLASSES) return ZXM_E_PROTOCOL;
    heirloom_class_t c[HEIRLOOM_CLASSES];
    int64_t num[HEIRLOOM_CLASSES + 1], den[HEIRLOOM_CLASSES + 1], parts = 0, gap = 0;
    for (uint32_t i = 0; i < len; i++) {
        int32_t o = zxm_at(h->link, arr, i);
        memset(&c[i], 0, sizeof c[i]);
        c[i].each = -1;
        if (zxm_get_str(h->link, o, "class", c[i].name, sizeof c[i].name) != ZXM_OK ||
            get_u32(h->link, o, "persons", &c[i].persons) != ZXM_OK || c[i].persons == 0 ||
            get_dec(h->link, o, "num", &c[i].num) != ZXM_OK ||
            get_dec(h->link, o, "den", &c[i].den) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        num[i] = c[i].num;
        den[i] = c[i].den;
        if (amount < 0) continue;
        int32_t each = zxm_get(h->link, o, "each_minor");
        if (each < 0 || zxm_len(h->link, each) != c[i].persons) return ZXM_E_PROTOCOL;
        for (uint32_t j = 0; j < c[i].persons; j++) {
            int32_t t = zxm_at(h->link, each, j);
            if (t < 0 || t >= h->link->ntok) return ZXM_E_PROTOCOL;
            const w4_jtok_t *tok = &h->link->tok[t];
            uint32_t len = tok->end - tok->start;
            if (tok->type != W4_J_STR || tok->end < tok->start || len == 0 || len > DEC_MAX)
                return ZXM_E_PROTOCOL;
            int64_t v = 0;
            for (uint32_t d = tok->start; d < tok->end; d++) {
                char ch = h->link->resp[d];
                if (ch < '0' || ch > '9' || v > (INT64_MAX - 9) / 10) return ZXM_E_PROTOCOL;
                v = v * 10 + (ch - '0');
            }
            if (j == 0) c[i].each = v;
            if (parts > INT64_MAX - v) return ZXM_E_PROTOCOL;
            parts += v;
        }
    }
    int32_t un = zxm_get(h->link, r, "unallocated");
    bool advisory = false, ruling = true;
    int64_t un_num = 0, un_den = 0;
    if (un < 0 || get_dec(h->link, un, "num", &un_num) != ZXM_OK ||
        get_dec(h->link, un, "den", &un_den) != ZXM_OK ||
        zxm_get_bool(h->link, r, "advisory", &advisory) != ZXM_OK || !advisory ||
        zxm_get_bool(h->link, r, "ruling", &ruling) != ZXM_OK || ruling)
        return ZXM_E_PROTOCOL;
    num[len] = un_num;
    den[len] = un_den;
    /* HM2: the classes and what is left unallocated make one whole */
    if (heirloom_manifest_gap(num, den, len + 1, &gap) != 0 || gap != 0) return ZXM_E_PROTOCOL;
    if (amount >= 0 && parts != amount) return ZXM_E_PROTOCOL;
    memcpy(h->cls, c, sizeof c[0] * len);
    h->ncls = len;
    h->unalloc_num = un_num;
    h->unalloc_den = un_den;
    say(h, "A computation of one common majority treatment, not a ruling: a qualified scholar "
           "or court decides.");
    return ZXM_OK;
}

/* HM4: whether the release is due at now, from the copy */
static bool due_at(heirloom_t *h, int64_t now)
{
    if (!h->filed) {
        say(h, "No death report was filed from here: Heirloom does not know when the waiting "
               "period began, so nothing is due.");
        return false;
    }
    int64_t reported = !strcmp(h->phase, "reported") ? 1 : 0;
    int64_t vetoed = h->epoch != h->report_epoch ? 1 : 0;
    int64_t spare = (int64_t) h->approvals - (int64_t) h->k;
    int64_t over = now - h->reported_at - h->wait; /* each 0..18 digits: fits */
    bool due = heirloom_release_due(reported, (int64_t) h->k, spare, over, vetoed);
    if (vetoed)
        say(h, "The owner's own signature cancelled the report: nothing is released.");
    else if (due)
        say(h, "The release is due: the guardians have signed and the waiting period is over. "
               "Confirm to go on.");
    else
        say(h, spare < 0 ? "Waiting for more guardians to sign."
                         : "Waiting for the waiting "
                           "period to end.");
    return due;
}

int heirloom_refresh(heirloom_t *h, int64_t now)
{
    if (!h || !h->link || now < 0 || now > HEIRLOOM_EIGHTEEN) return ZXM_E_ARG;
    zxm_begin(h->link, "estate.status");
    zxm_p_i64(h->link, "now", now);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = h->link->result;
    bool set_up = false, moves = true;
    if (zxm_get_bool(h->link, r, "set_up", &set_up) != ZXM_OK ||
        zxm_get_bool(h->link, r, "moves_money", &moves) != ZXM_OK || moves)
        return ZXM_E_PROTOCOL;
    if (!set_up) {
        h->set_up = false;
        h->n = 0;
        h->release_due = false;
        say(h, "No estate plan yet.");
        return ZXM_OK;
    }
    char phase[HEIRLOOM_PHASE_MAX + 1];
    int64_t epoch = 0;
    uint32_t approvals = 0, k = 0, n = 0;
    bool advisory = false, release = false;
    heirloom_heir_t heir[HEIRLOOM_HEIRS];
    if (read_phase(h, phase, &epoch) != ZXM_OK ||
        get_u32(h->link, r, "approvals", &approvals) != ZXM_OK ||
        get_u32(h->link, r, "k", &k) != ZXM_OK || k < 1 || k > HEIRLOOM_GUARDIANS ||
        zxm_get_bool(h->link, r, "advisory", &advisory) != ZXM_OK ||
        zxm_get_bool(h->link, r, "key_release", &release) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(h->link, r, "heirs");
    if (arr < 0) return ZXM_E_PROTOCOL;
    if (zxm_len(h->link, arr) > 0 && read_heirs(h, arr, heir, &n, true) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    /* HM7: the whole answer is read */
    h->set_up = true;
    memcpy(h->phase, phase, sizeof phase);
    h->epoch = epoch;
    h->approvals = approvals;
    h->k = k;
    h->advisory = advisory;
    h->key_release = release;
    for (uint32_t i = 0; i < n; i++) h->heir[i] = heir[i];
    h->n = n;
    h->release_due = due_at(h, now);
    return ZXM_OK;
}

int heirloom_message(heirloom_t *h, const char *kind, int64_t value, char *out, uint32_t cap)
{
    if (!h || !h->link || !kind || !out || cap < HEIRLOOM_MSG_HEX + 1u || value < 0)
        return ZXM_E_ARG;
    bool report = !strcmp(kind, "report"), contest = !strcmp(kind, "contest");
    if (!report && !contest && strcmp(kind, "approve") && strcmp(kind, "veto")) return ZXM_E_ARG;
    zxm_begin(h->link, "estate.message");
    zxm_p_str(h->link, "kind", kind);
    if (report) zxm_p_i64(h->link, "bond_minor", value);
    if (contest) zxm_p_i64(h->link, "heir", value);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    char m[HEIRLOOM_MSG_HEX + 2];
    if (zxm_get_str(h->link, h->link->result, "message", m, sizeof m) != ZXM_OK || !is_hex(m, 0))
        return ZXM_E_PROTOCOL;
    memcpy(out, m, strlen(m) + 1u);
    return ZXM_OK;
}

int heirloom_report(heirloom_t *h, uint32_t attestor, int64_t bond, int64_t now, const char *sig)
{
    if (!h || !h->link || bond < 0 || bond > HEIRLOOM_EIGHTEEN || now < 0 ||
        now > HEIRLOOM_EIGHTEEN || !is_hex(sig, HEIRLOOM_SIG_HEX))
        return ZXM_E_ARG;
    if (!h->set_up) return ZXM_E_STATE;
    zxm_begin(h->link, "estate.report");
    zxm_p_u64(h->link, "attestor", attestor);
    zxm_p_i64(h->link, "bond_minor", bond);
    zxm_p_i64(h->link, "now", now);
    zxm_p_str(h->link, "sig", sig);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    char phase[HEIRLOOM_PHASE_MAX + 1];
    int64_t epoch = 0;
    if (read_phase(h, phase, &epoch) != ZXM_OK || strcmp(phase, "reported")) return ZXM_E_PROTOCOL;
    memcpy(h->phase, phase, sizeof phase);
    h->epoch = epoch;
    h->approvals = 0;
    h->filed = true;
    h->reported_at = now;
    h->report_epoch = epoch;
    h->release_due = false;
    say(h, "A death report was filed with its bond. The guardians are asked to sign; your own "
           "signature cancels it and refunds the bond.");
    return ZXM_OK;
}

int heirloom_approve(heirloom_t *h, uint32_t guardian, const char *sig)
{
    if (!h || !h->link || guardian >= HEIRLOOM_GUARDIANS || !is_hex(sig, HEIRLOOM_SIG_HEX))
        return ZXM_E_ARG;
    if (!h->set_up) return ZXM_E_STATE;
    zxm_begin(h->link, "estate.approve");
    zxm_p_u64(h->link, "guardian", guardian);
    zxm_p_str(h->link, "sig", sig);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    uint32_t a = 0, k = 0;
    if (get_u32(h->link, h->link->result, "approvals", &a) != ZXM_OK ||
        get_u32(h->link, h->link->result, "k", &k) != ZXM_OK || k != h->k || a <= h->approvals ||
        a > h->guardians)
        return ZXM_E_PROTOCOL;
    h->approvals = a;
    return ZXM_OK;
}

int heirloom_veto(heirloom_t *h, const char *sig)
{
    if (!h || !h->link || !is_hex(sig, HEIRLOOM_SIG_HEX)) return ZXM_E_ARG;
    if (!h->set_up) return ZXM_E_STATE;
    zxm_begin(h->link, "estate.veto");
    zxm_p_str(h->link, "sig", sig);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    char phase[HEIRLOOM_PHASE_MAX + 1];
    int64_t epoch = 0;
    if (read_phase(h, phase, &epoch) != ZXM_OK || strcmp(phase, "alive") || epoch <= h->epoch)
        return ZXM_E_PROTOCOL;
    memcpy(h->phase, phase, sizeof phase);
    h->epoch = epoch;
    h->approvals = 0;
    h->release_due = false;
    say(h, "Your signature cancelled the report: the approvals are cleared and the bond is "
           "refunded.");
    return ZXM_OK;
}

int heirloom_confirm(heirloom_t *h, int64_t now)
{
    if (!h || !h->link || now < 0 || now > HEIRLOOM_EIGHTEEN) return ZXM_E_ARG;
    if (!h->set_up) return ZXM_E_STATE;
    /* HM4: estate.confirm only when the release is due */
    if (!due_at(h, now)) {
        h->release_due = false;
        return ZXM_E_STATE;
    }
    zxm_begin(h->link, "estate.confirm");
    zxm_p_i64(h->link, "now", now);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    char phase[HEIRLOOM_PHASE_MAX + 1];
    int64_t epoch = 0;
    if (read_phase(h, phase, &epoch) != ZXM_OK || strcmp(phase, "confirmed") || epoch != h->epoch)
        return ZXM_E_PROTOCOL;
    memcpy(h->phase, phase, sizeof phase);
    h->release_due = false;
    say(h, "The report is confirmed. A faraid or forced-heirship plan still waits for a signed "
           "ruling.");
    return ZXM_OK;
}

int heirloom_preview(heirloom_t *h, int64_t amount)
{
    if (!h || !h->link || amount < 0 || amount > CROWN_NINE) return ZXM_E_ARG;
    if (h->n == 0) return ZXM_E_STATE;
    int64_t w[HEIRLOOM_HEIRS], extra[HEIRLOOM_HEIRS], share[HEIRLOOM_HEIRS], total = 0;
    for (uint32_t i = 0; i < h->n; i++) {
        /* zxvd keeps the shares over one common denominator (estate.h E2) */
        if (h->heir[i].den != h->heir[0].den) return ZXM_E_STATE;
        w[i] = h->heir[i].num;
    }
    if (crown_allot(amount, w, h->n, share, extra, &total) != 0 || total != h->heir[0].den)
        return ZXM_E_STATE;
    for (uint32_t i = 0; i < h->n; i++) h->preview[i] = share[i];
    h->amount = amount;
    h->compared = false;
    h->mismatch = false;
    if (strcmp(h->phase, "distributing")) {
        say(h, "A preview of the split, largest remainder first: advice only, nothing moves.");
        return ZXM_OK;
    }
    zxm_begin(h->link, "estate.distribute");
    zxm_p_i64(h->link, "amount_minor", amount);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = h->link->result, arr = zxm_get(h->link, r, "parts");
    bool moved = true;
    int64_t escrow = 0, held = 0;
    if (arr < 0 || zxm_len(h->link, arr) != h->n ||
        zxm_get_bool(h->link, r, "moved", &moved) != ZXM_OK || moved ||
        get_dec(h->link, r, "to_escrow_minor", &escrow) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int64_t engine[HEIRLOOM_HEIRS];
    bool bad = false;
    for (uint32_t i = 0; i < h->n; i++) {
        int32_t o = zxm_at(h->link, arr, i);
        uint32_t id = 0;
        bool escrowed = false;
        if (get_u32(h->link, o, "id", &id) != ZXM_OK || id != h->heir[i].id ||
            get_dec(h->link, o, "minor", &engine[i]) != ZXM_OK ||
            zxm_get_bool(h->link, o, "escrowed", &escrowed) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (escrowed) {
            held += share[i]; /* at most the amount: fits */
            bad |= engine[i] != 0;
        } else {
            bad |= engine[i] != share[i];
        }
    }
    bad |= held != escrow;
    for (uint32_t i = 0; i < h->n; i++) h->engine[i] = engine[i];
    h->to_escrow = escrow;
    h->compared = true;
    h->mismatch = bad;
    if (bad) {
        say(h, "The estate engine's split differs from the preview: nothing moved; ask a person "
               "to look.");
        return ZXM_E_PROTOCOL;
    }
    say(h, "The estate engine's split matches the preview; a contested heir's part waits in "
           "escrow. Nothing moved.");
    return ZXM_OK;
}

int heirloom_preview_holdings(heirloom_t *h, const char *ccy)
{
    if (!h || !h->link || !ccy) return ZXM_E_ARG;
    int rc = coffer_load(&h->books);
    if (rc != ZXM_OK) return rc;
    int64_t total = 0;
    rc = coffer_holdings_in(&h->books, ccy, &total);
    if (rc != ZXM_OK) return rc;
    return heirloom_preview(h, total);
}

int heirloom_recovery_setup(heirloom_t *h, const char *root, const char *guardians,
                            const char *gateway, const char *first_device)
{
    if (!h || !h->link || !is_hex(root, HEIRLOOM_KEY_HEX) ||
        !is_hex(guardians, 5u * HEIRLOOM_KEY_HEX) || !is_hex(gateway, HEIRLOOM_KEY_HEX) ||
        !is_hex(first_device, HEIRLOOM_KEY_HEX))
        return ZXM_E_ARG;
    zxm_begin(h->link, "recovery.setup");
    zxm_p_str(h->link, "root", root);
    zxm_p_str(h->link, "guardians", guardians);
    zxm_p_str(h->link, "gateway", gateway);
    zxm_p_str(h->link, "first_device", first_device);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    char state[HEIRLOOM_PHASE_MAX + 1];
    int64_t lock = 0;
    if (get_dec(h->link, h->link->result, "timelock", &lock) != ZXM_OK || lock < 1 ||
        zxm_get_str(h->link, h->link->result, "state", state, sizeof state) != ZXM_OK ||
        strcmp(state, "idle"))
        return ZXM_E_PROTOCOL;
    h->rec_set_up = true;
    h->timelock = lock;
    memcpy(h->rec_state, state, sizeof state);
    say(h, "Recovery guardians are set: a new device needs their approvals and a time lock, and "
           "any device you still hold can stop it.");
    return ZXM_OK;
}

int heirloom_recovery_claim(heirloom_t *h, const char *new_key, int64_t now)
{
    if (!h || !h->link || !is_hex(new_key, HEIRLOOM_KEY_HEX) || now < 0 || now > HEIRLOOM_EIGHTEEN)
        return ZXM_E_ARG;
    if (!h->rec_set_up) return ZXM_E_STATE;
    zxm_begin(h->link, "recovery.claim");
    zxm_p_str(h->link, "new_key", new_key);
    zxm_p_i64(h->link, "now", now);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    char state[HEIRLOOM_PHASE_MAX + 1];
    int64_t claim = 0;
    uint32_t a = 0;
    bool vetoed = true;
    if (zxm_get_str(h->link, h->link->result, "state", state, sizeof state) != ZXM_OK ||
        strcmp(state, "pending") || get_dec(h->link, h->link->result, "claim", &claim) != ZXM_OK ||
        get_u32(h->link, h->link->result, "approvals", &a) != ZXM_OK || a != 0 ||
        zxm_get_bool(h->link, h->link->result, "vetoed", &vetoed) != ZXM_OK || vetoed)
        return ZXM_E_PROTOCOL;
    memcpy(h->rec_state, state, sizeof state);
    h->claim = claim;
    h->rec_approvals = 0;
    h->rec_vetoed = false;
    h->can_spend = false;
    say(h, "A recovery claim is open: every guardian is asked, and the account is view-only "
           "until it ends.");
    return ZXM_OK;
}

int heirloom_recovery_status(heirloom_t *h, int64_t now, int64_t liquid, int64_t vbill)
{
    if (!h || !h->link || now < 0 || now > HEIRLOOM_EIGHTEEN || liquid < 0 || vbill < 0)
        return ZXM_E_ARG;
    zxm_begin(h->link, "recovery.status");
    zxm_p_i64(h->link, "now", now);
    zxm_p_i64(h->link, "liquid_minor", liquid);
    zxm_p_i64(h->link, "vbill_minor", vbill);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = h->link->result;
    bool set_up = false, delivered = true;
    if (zxm_get_bool(h->link, r, "set_up", &set_up) != ZXM_OK ||
        zxm_get_bool(h->link, r, "notices_delivered", &delivered) != ZXM_OK || delivered)
        return ZXM_E_PROTOCOL;
    if (!set_up) {
        h->rec_set_up = false;
        say(h, "No recovery guardians yet.");
        return ZXM_OK;
    }
    char state[HEIRLOOM_PHASE_MAX + 1];
    int64_t claim = 0, vb = 0, lq = 0, notices = 0;
    uint32_t a = 0, needed = 0;
    bool vetoed = false, view = false, spend = false;
    if (zxm_get_str(h->link, r, "state", state, sizeof state) != ZXM_OK ||
        get_dec(h->link, r, "claim", &claim) != ZXM_OK ||
        get_u32(h->link, r, "approvals", &a) != ZXM_OK ||
        get_u32(h->link, r, "needed", &needed) != ZXM_OK || needed < 1 || needed > 5 ||
        zxm_get_bool(h->link, r, "vetoed", &vetoed) != ZXM_OK ||
        zxm_get_bool(h->link, r, "can_view", &view) != ZXM_OK ||
        zxm_get_bool(h->link, r, "can_spend", &spend) != ZXM_OK ||
        get_dec(h->link, r, "vbill_spendable_minor", &vb) != ZXM_OK || vb > vbill ||
        get_dec(h->link, r, "liquid_allowance_minor", &lq) != ZXM_OK || lq > liquid ||
        get_dec(h->link, r, "notices", &notices) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    /* an open claim never lets the account spend */
    if (!strcmp(state, "pending") && spend) return ZXM_E_PROTOCOL;
    h->rec_set_up = true;
    memcpy(h->rec_state, state, sizeof state);
    h->claim = claim;
    h->rec_approvals = a;
    h->rec_needed = needed;
    h->rec_vetoed = vetoed;
    h->can_view = view;
    h->can_spend = spend;
    h->vbill_spendable = vb;
    h->liquid_allowance = lq;
    h->notices = notices;
    if (!strcmp(state, "pending"))
        say(h, vetoed ? "A device you hold vetoed the claim: it now needs more approvals and a "
                        "longer wait."
                      : "A recovery claim is open: view-only until the guardians approve and the "
                        "time lock ends.");
    else if (!strcmp(state, "thawing"))
        say(h, "The new device is in: spending thaws step by step, as recovery reports.");
    else
        say(h, "No recovery claim is open.");
    return ZXM_OK;
}

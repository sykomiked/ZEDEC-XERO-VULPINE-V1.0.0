/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tributary_model.c — Tributary, many-model answers (see tributary_model.h).
 * Every ask is checked against the owner's caps before anything is sent,
 * and every answer's record and ledger entry made to fit before it is
 * kept. */
#include <stdio.h>
#include <string.h>

#include "tributary_model.h"

static const char *const KIND_NAME[3] = {"the slot", "template", "peer"};

/* the length of s, counting no further than cap + 1 */
static uint32_t bounded_len(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (n <= cap && s[n]) n++;
    return n;
}

/* s cut to at most cap bytes on a UTF-8 boundary, into out[cap + 1] */
static void copy_cut(char *out, const char *s, uint32_t cap)
{
    uint32_t n = bounded_len(s, cap);
    if (n > cap) n = zxm_utf8_cut(s, cap);
    memcpy(out, s, n);
    out[n] = 0;
}

/* TM4: ASCII letters lowered, spaces trimmed and collapsed, one closing
 * '.', '!' or '?' dropped */
static void normalise(const char *s, char *out, uint32_t cap)
{
    uint32_t k = 0;
    bool space = false;
    for (uint32_t i = 0; s[i] && k < cap; i++) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = k > 0;
            continue;
        }
        if (space && k < cap) out[k++] = ' ';
        space = false;
        if (k >= cap) break;
        out[k++] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
    }
    if (k > 0 && (out[k - 1] == '.' || out[k - 1] == '!' || out[k - 1] == '?')) k--;
    while (k > 0 && out[k - 1] == ' ') k--;
    out[k] = 0;
}

/* "[0-9]{1,18}" into *v, false when it is not one */
static bool parse_digits(const char *s, int64_t *v)
{
    int64_t x = 0;
    uint32_t i = 0;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9' || i >= 18u) return false;
        x = x * 10 + (s[i] - '0');
    }
    if (i == 0) return false;
    *v = x;
    return true;
}

/* ---- TM1-TM3 ------------------------------------------------------------------- */

void tributary_init(tributary_t *t, zxm_link_t *link, tributary_clock_fn clock, void *clock_ctx)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->clock = clock;
    t->clock_ctx = clock_ctx;
    t->quorum = TRIB_DEFAULT_QUORUM;
    t->fastest = t->slowest = -1;
}

int tributary_read_slot(tributary_t *t)
{
    zxm_link_t *l = t->link;
    bool loaded = false, gen = false;
    char name[TRIB_NAME + 1];
    zxm_begin(l, "model.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(l, l->result, "loaded", &loaded) != ZXM_OK ||
        zxm_get_str(l, l->result, "name", name, sizeof name) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (zxm_get(l, l->result, "can_generate") >= 0 &&
        zxm_get_bool(l, l->result, "can_generate", &gen) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    t->slot_known = true;
    t->can_generate = loaded && gen;
    memcpy(t->slot_name, name, sizeof name);
    if (t->can_generate)
        snprintf(t->notice, sizeof t->notice, "The slot holds %s.", name[0] ? name : "a model");
    else
        snprintf(t->notice, sizeof t->notice,
                 "No model can answer in the slot: swarm.ask reads shorthand back.");
    return ZXM_OK;
}

int tributary_set_caps(tributary_t *t, int64_t tokens, int64_t uj, int64_t price, int64_t quorum,
                       int64_t octane, bool owner_confirmed)
{
    if (tokens < 1 || tokens > TRIB_TOKEN_ROOM_MAX || uj < 1 || uj > TRIB_ROOM_MAX || price < 0 ||
        price > TRIB_ROOM_MAX || quorum < 1 || quorum > (int64_t) TRIB_SOURCES || octane < 0 ||
        octane > (int64_t) TRIB_TIER_MAX)
        return ZXM_E_ARG;
    if (!owner_confirmed) {
        snprintf(t->notice, sizeof t->notice, "The caps are yours to set: confirm them first.");
        return ZXM_E_STATE;
    }
    t->caps_set = true;
    t->cap_tokens = tokens;
    t->cap_uj = uj;
    t->cap_price = price;
    t->quorum = quorum;
    t->octane = octane;
    snprintf(t->notice, sizeof t->notice,
             "Each question: at most %lld tokens, %lld uJ and %lld VFV minor units; %lld answers "
             "decide.",
             (long long) tokens, (long long) uj, (long long) price, (long long) quorum);
    return ZXM_OK;
}

int tributary_question(tributary_t *t, const char *prompt)
{
    if (!prompt) return ZXM_E_ARG;
    int64_t bytes = (int64_t) bounded_len(prompt, (uint32_t) RETORT_PROMPT_MAX);
    /* a question burns no sandbox fuel and is asked well inside an hour */
    if (!retort_run_limits_ok(bytes, 1, RETORT_SECONDS_MAX)) return ZXM_E_ARG;
    if (t->question >= TRIB_CODE_MAX) return ZXM_E_STATE; /* ^ZXTRIB's question:i9 is full */
    memcpy(t->prompt, prompt, (size_t) bytes);
    t->prompt[bytes] = 0;
    t->prompt_len = bytes;
    t->question++;
    t->n = 0;
    t->spent_tokens = t->spent_uj = t->spent_price = t->ledger = 0;
    t->joined = false;
    t->held = ZXM_HELD_NONE;
    t->accepted = 0;
    t->fastest = t->slowest = -1;
    snprintf(t->notice, sizeof t->notice, "Question %lld: %lld bytes, no answers yet.",
             (long long) t->question, (long long) bytes);
    return ZXM_OK;
}

/* ---- TM4: asking ------------------------------------------------------------------ */

/* TB3 over the caps less spent less what the next ask may spend */
static int ask_allowed(tributary_t *t, int64_t tokens, int64_t price)
{
    if (!t->caps_set || t->question == 0 || t->n >= TRIB_SOURCES || !t->clock ||
        t->held == ZXM_HELD_ACCEPTED)
        return ZXM_E_STATE;
    int64_t tokenroom = t->cap_tokens - t->spent_tokens - tokens;
    int64_t energyroom = t->cap_uj - t->spent_uj;
    int64_t priceroom = t->cap_price - t->spent_price - price;
    if (!tributary_ask_within_caps(tokenroom, energyroom, priceroom)) {
        snprintf(t->notice, sizeof t->notice,
                 "Over a cap: %lld tokens, %lld uJ and %lld VFV minor units of room left.",
                 (long long) (t->cap_tokens - t->spent_tokens),
                 (long long) (t->cap_uj - t->spent_uj),
                 (long long) (t->cap_price - t->spent_price));
        return ZXM_E_STATE;
    }
    return ZXM_OK;
}

/* TB6: the ledger after `tokens` entered in canonical form at the octane
 * (swarm_denom.h D3: the top tier holds floor(v / face), the rest in
 * Zeckendorf form, greedily), or TRIB_REFUSED */
static int64_t ledger_enter(int64_t ledger, int64_t tokens, int64_t octane)
{
    int64_t top = tributary_face(octane), rest, after = ledger, entered = 0;
    if (top == 0 || tokens < 0) return TRIB_REFUSED;
    after = tributary_ledger_after(after, octane, tokens / top);
    if (after == TRIB_REFUSED) return TRIB_REFUSED;
    entered += tokens / top * top;
    rest = tokens % top;
    for (int64_t k = octane - 1; k >= 0 && rest > 0; k--) {
        int64_t f = tributary_face(k);
        if (f > rest) continue;
        after = tributary_ledger_after(after, k, 1);
        if (after == TRIB_REFUSED) return TRIB_REFUSED;
        entered += f;
        rest -= f;
    }
    /* D5: the value entered is the tokens, exactly */
    return entered == tokens && after - ledger == tokens ? after : TRIB_REFUSED;
}

/* after the call: its energy, its record and ledger entry made to fit, kept */
static int keep_answer(tributary_t *t, int64_t kind, const char *name, const char *text,
                       int64_t tokens, int64_t paid, int64_t start)
{
    zxm_link_t *l = t->link;
    tributary_answer_t *a = &t->a[t->n];
    tributary_answer_t r;
    int64_t end = t->clock(t->clock_ctx), uj = 0;
    zxm_begin(l, "power.status");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "total_uj", &uj) != ZXM_OK || uj < 0 || uj > RETORT_UJ_MAX)
        return ZXM_E_PROTOCOL;
    memset(&r, 0, sizeof r);
    r.kind = kind;
    copy_cut(r.name, name, TRIB_NAME);
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') text++;
    copy_cut(r.text, text, TRIB_TEXT);
    for (size_t k = strlen(r.text); k > 0 && (r.text[k - 1] == ' ' || r.text[k - 1] == '\t' ||
                                              r.text[k - 1] == '\n' || r.text[k - 1] == '\r');
         k--)
        r.text[k - 1] = 0;
    normalise(r.text, r.norm, TRIB_TEXT);
    r.tokens = tokens;
    r.paid = paid;
    r.uj = uj;
    r.stamp = end;
    r.ms = end >= start ? end - start : 0; /* a clock that went back: 0 ms */
    if (r.ms > RETORT_LATENCY_MAX) r.ms = RETORT_LATENCY_MAX;
    /* the code: numbered by first appearance in the question */
    r.code = 1;
    r.model = (int64_t) t->n;
    for (uint32_t i = 0; i < t->n; i++)
        if (t->a[i].code >= r.code) r.code = t->a[i].code + 1; /* a new text: the next code */
    for (uint32_t i = 0; i < t->n; i++)
        if (!strcmp(t->a[i].norm, r.norm)) {
            r.code = t->a[i].code; /* a text given before: its code */
            break;
        }
    for (uint32_t i = 0; i < t->n; i++)
        if (t->a[i].kind == kind && !strcmp(t->a[i].name, r.name)) {
            r.model = t->a[i].model;
            break;
        }
    if (!tributary_source_fits(t->question, r.model, r.code, r.stamp) ||
        t->spent_uj > TRIB_ROOM_MAX - uj || t->spent_price > TRIB_ROOM_MAX - paid)
        return ZXM_E_PROTOCOL;
    int64_t ledger = ledger_enter(t->ledger, tokens, t->octane);
    if (ledger == TRIB_REFUSED) return ZXM_E_STATE;
    /* checked: now kept */
    *a = r;
    t->n++;
    t->ledger = ledger;
    t->spent_tokens += tokens;
    t->spent_uj += uj;
    t->spent_price += paid;
    t->joined = false;
    t->held = ZXM_HELD_NONE;
    snprintf(t->notice, sizeof t->notice,
             "%s %s answered in %lld ms (%lld uJ): it waits for the join.", KIND_NAME[kind], r.name,
             (long long) r.ms, (long long) uj);
    return ZXM_OK;
}

int tributary_ask_slot(tributary_t *t)
{
    zxm_link_t *l = t->link;
    char got[ZXM_ANSWER_MAX + 1];
    if (!t->slot_known) return ZXM_E_STATE;
    int64_t est = (t->prompt_len + 3) / 4; /* M4: a token per four bytes of prompt */
    int rc = ask_allowed(t, est, 0);
    if (rc != ZXM_OK) return rc;
    int64_t start = t->clock(t->clock_ctx), tokens = 0;
    zxm_begin(l, "swarm.ask");
    zxm_p_str(l, "prompt", t->prompt);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "answer", got, sizeof got) != ZXM_OK) return ZXM_E_PROTOCOL;
    /* what the answer cost, from the companion's budget gate (M4) */
    int64_t prompt_len = t->prompt_len;
    char text[TRIB_TEXT + 1];
    copy_cut(text, got, TRIB_TEXT);
    zxm_begin(l, "swarm.state");
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t comp = zxm_get(l, l->result, "companion");
    int32_t last = comp >= 0 ? zxm_get(l, comp, "last_answer") : -1;
    if (last >= 0) {
        if (zxm_get_i64(l, last, "charged", &tokens) != ZXM_OK || tokens < 0 ||
            tokens > TRIB_TOKEN_ROOM_MAX)
            return ZXM_E_PROTOCOL;
    } else {
        tokens = (prompt_len + (int64_t) bounded_len(got, ZXM_ANSWER_MAX) + 3) / 4;
    }
    return keep_answer(t, TRIB_KIND_SLOT, t->slot_name[0] ? t->slot_name : "the slot", text, tokens,
                       0, start);
}

int tributary_ask_play(tributary_t *t, const char *template_id)
{
    zxm_link_t *l = t->link;
    char verdict[TRIB_NAME + 1];
    if (!template_id) return ZXM_E_ARG;
    uint32_t n = bounded_len(template_id, TRIB_NAME);
    if (n == 0 || n > TRIB_NAME) return ZXM_E_ARG;
    int rc = ask_allowed(t, 0, 0);
    if (rc != ZXM_OK) return rc;
    int64_t start = t->clock(t->clock_ctx);
    zxm_begin(l, "play.run");
    zxm_p_str(l, "template", template_id);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "verdict", verdict, sizeof verdict) != ZXM_OK ||
        (strcmp(verdict, "TRUE") && strcmp(verdict, "FALSE") && strcmp(verdict, "BOTH") &&
         strcmp(verdict, "NEITHER")))
        return ZXM_E_PROTOCOL;
    return keep_answer(t, TRIB_KIND_PLAY, template_id, verdict, 0, 0, start);
}

int tributary_ask_peer(tributary_t *t, const char *peer, int64_t max_price, bool owner_confirmed)
{
    zxm_link_t *l = t->link;
    char got[ZXM_ANSWER_MAX + 1], paid_s[24];
    int64_t paid = 0;
    if (!peer || max_price < 0 || max_price > TRIB_ROOM_MAX) return ZXM_E_ARG;
    uint32_t n = bounded_len(peer, TRIB_NAME);
    if (n == 0 || n > TRIB_NAME) return ZXM_E_ARG;
    int rc = ask_allowed(t, 0, max_price);
    if (rc != ZXM_OK) return rc;
    if (!owner_confirmed) {
        snprintf(t->notice, sizeof t->notice,
                 "Asking %s may cost up to %lld VFV minor units and the fee: confirm it first.",
                 peer, (long long) max_price);
        return ZXM_E_STATE;
    }
    int64_t start = t->clock(t->clock_ctx);
    zxm_begin(l, "peer.ask");
    zxm_p_str(l, "peer", peer);
    zxm_p_str(l, "prompt", t->prompt);
    zxm_p_i64(l, "max_price_minor", max_price);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t s = zxm_get(l, l->result, "settlement");
    if (zxm_get_str(l, l->result, "answer", got, sizeof got) != ZXM_OK || s < 0 ||
        zxm_get_str(l, s, "paid_minor", paid_s, sizeof paid_s) != ZXM_OK ||
        !parse_digits(paid_s, &paid))
        return ZXM_E_PROTOCOL;
    char text[TRIB_TEXT + 1];
    copy_cut(text, got, TRIB_TEXT);
    return keep_answer(t, TRIB_KIND_PEER, peer, text, 0, paid, start);
}

/* ---- TM5-TM8 ---------------------------------------------------------------------- */

int tributary_join(tributary_t *t)
{
    int64_t code[TRIB_SOURCES], model[TRIB_SOURCES], score[TRIB_SOURCES], uj = 0;
    if (t->n == 0) return ZXM_E_STATE;
    for (uint32_t i = 0; i < t->n; i++) code[i] = t->a[i].code, model[i] = t->a[i].model;
    if (!tributary_agree_counts(code, t->n, t->agree)) return ZXM_E_STATE;
    int64_t fast = RETORT_LATENCY_MAX, slow = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        t->models[i] = tributary_code_models(model, code, t->n, i);
        if (!tributary_confidence_x100(t->models[i], (int64_t) t->n, &t->conf_num[i],
                                       &t->conf_den[i]))
            return ZXM_E_STATE;
        uj += t->a[i].uj; /* each at most RETORT_UJ_MAX, the sum kept under TRIB_ROOM_MAX */
        fast = retort_latency_fastest(fast, t->a[i].ms);
        slow = retort_latency_slowest(slow, t->a[i].ms);
    }
    t->verdict = tributary_verdict(code, t->n, t->quorum);
    t->lead = tributary_lead_code(code, t->agree, t->n);
    t->lead_share_x100 = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        if (t->lead && code[i] == t->lead)
            t->lead_share_x100 = assay_pass_share_x100(t->models[i], (int64_t) t->n);
        score[i] = t->lead && code[i] == t->lead ? RETORT_SCORE_MAX : 0;
    }
    for (uint32_t i = 0; i < t->n; i++)
        t->points[i] = retort_model_score_total(model, model[i], score, t->n);
    /* the bar as its margins: share over a majority, no refusal, answers
     * past the quorum */
    t->lead_meets_bar = t->lead != 0 && assay_meets_bar(t->lead_share_x100 - TRIB_BAR_X100, 0,
                                                        (int64_t) t->n - t->quorum);
    if (!retort_energy_per_answer(uj, (int64_t) t->n, &t->uj_num, &t->uj_den))
        t->uj_num = t->uj_den = 0;
    t->fastest = fast;
    t->slowest = slow;
    t->joined = true;
    t->held = ZXM_HELD_PENDING;
    t->accepted = 0;
    if (t->verdict == LPRES_STATE_TRUE)
        snprintf(t->notice, sizeof t->notice, "All %u answers agree: %s. Accept it or not.", t->n,
                 t->a[0].text);
    else if (t->verdict == LPRES_STATE_BOTH)
        snprintf(t->notice, sizeof t->notice,
                 "The answers disagree (BOTH): every answer is shown with its models; none is "
                 "picked.");
    else
        snprintf(t->notice, sizeof t->notice,
                 "Too few answers to decide (NEITHER): %u of the %lld that decide.", t->n,
                 (long long) t->quorum);
    return ZXM_OK;
}

int32_t tributary_show(const tributary_t *t, char *out, uint32_t cap)
{
    uint32_t k = 0;
    if (!out || cap == 0) return ZXM_E_ARG;
    out[0] = 0;
    if (!t->joined) return ZXM_E_STATE;
    for (uint32_t i = 0; i < t->n && k + 1u < cap; i++) {
        bool first = true;
        for (uint32_t j = 0; j < i; j++) first = first && t->a[j].code != t->a[i].code;
        if (!first) continue;
        int w = snprintf(out + k, cap - k, "%s\"%s\" (%lld of %u):", k ? "; " : "", t->a[i].text,
                         (long long) t->agree[i], t->n);
        k = w < 0 || (uint32_t) w >= cap - k ? cap - 1u : k + (uint32_t) w;
        for (uint32_t j = i; j < t->n && k + 1u < cap; j++) {
            if (t->a[j].code != t->a[i].code) continue;
            w = snprintf(out + k, cap - k, " %s %s", KIND_NAME[t->a[j].kind], t->a[j].name);
            k = w < 0 || (uint32_t) w >= cap - k ? cap - 1u : k + (uint32_t) w;
        }
    }
    return (int32_t) k;
}

int tributary_accept(tributary_t *t, int64_t code, bool owner_confirmed)
{
    lpres_state_t chosen = LPRES_STATE_NEITHER;
    bool given = false;
    if (!t->joined || t->held != ZXM_HELD_PENDING) return ZXM_E_STATE;
    for (uint32_t i = 0; i < t->n; i++) given = given || t->a[i].code == code;
    if (!given) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    /* the chosen answer's own standing among the sources, and whether it
     * follows from the verdict (twelvefold.lp_entail_kept) */
    for (uint32_t i = 0; i < t->n; i++)
        chosen = lpres_kjoin(chosen, t->a[i].code == code ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    if (twelvefold_kept((int64_t) t->verdict, (int64_t) chosen) != 1) return ZXM_E_STATE;
    t->held = ZXM_HELD_ACCEPTED;
    t->accepted = code;
    for (uint32_t i = 0; i < t->n; i++)
        if (t->a[i].code == code) {
            snprintf(t->notice, sizeof t->notice, "Accepted: %s.", t->a[i].text);
            break;
        }
    return ZXM_OK;
}

int tributary_reject(tributary_t *t)
{
    if (!t->joined || t->held != ZXM_HELD_PENDING) return ZXM_E_STATE;
    t->held = ZXM_HELD_REJECTED;
    snprintf(t->notice, sizeof t->notice, "No answer accepted.");
    return ZXM_OK;
}

int64_t tributary_token_share(const tributary_t *t, uint32_t i)
{
    int64_t tok[TRIB_SOURCES];
    for (uint32_t j = 0; j < t->n; j++) tok[j] = t->a[j].tokens;
    return tributary_token_permille(tok, t->n, i);
}

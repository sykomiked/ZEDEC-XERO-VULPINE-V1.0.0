/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* murmuration_model.c — Murmuration, the swarm dashboard (see
 * murmuration_model.h). */
#include <string.h>

#include "murmuration_model.h"
#include "swarm_emotion.h" /* the emoji palette */

const zxm_ai_action_t MURM_AI[MURM_AI_COUNT] = {
    {"Explain the swarm", "Companion", 400},
    {"Why is this agent unhappy", "Companion", 300},
};

/* ===================== reading fields (R3) ===================== */

/* A string, cut on a character boundary to fit cap; "" when optional and
 * left out. */
static int text(const zxm_link_t *l, int32_t obj, const char *k, char *out, uint32_t cap, bool need)
{
    static char whole[ZXM_RESP_MAX + 1];
    int32_t i = zxm_get(l, obj, k);
    out[0] = 0;
    if (i < 0) return need ? ZXM_E_PROTOCOL : ZXM_OK;
    if (l->tok[i].type != W4_J_STR) return ZXM_E_PROTOCOL;
    int32_t n = w4_json_str(l->resp, &l->tok[i], whole, sizeof whole);
    if (n < 0) return ZXM_E_PROTOCOL;
    uint32_t keep = (uint32_t) n < cap ? (uint32_t) n : zxm_utf8_cut(whole, cap - 1u);
    memcpy(out, whole, keep);
    out[keep] = 0;
    return ZXM_OK;
}

/* A JSON integer no larger than max; 0 when optional and left out. */
static int num(const zxm_link_t *l, int32_t obj, const char *k, uint64_t max, uint64_t *v,
               bool need)
{
    int32_t i = zxm_get(l, obj, k);
    *v = 0;
    if (i < 0) return need ? ZXM_E_PROTOCOL : ZXM_OK;
    if (w4_json_u64(l->resp, &l->tok[i], v) != W4_OK || *v > max) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

static int flag(const zxm_link_t *l, int32_t obj, const char *k, bool *v, bool need)
{
    int32_t i = zxm_get(l, obj, k);
    *v = false;
    if (i < 0) return need ? ZXM_E_PROTOCOL : ZXM_OK;
    return w4_json_bool(&l->tok[i], v) == W4_OK ? ZXM_OK : ZXM_E_PROTOCOL;
}

/* zxvd_rpc.h M3: a 64-bit value as a decimal string ("-?[0-9]+"), or as a
 * JSON integer. Never a fraction or an exponent. */
static int dec(const zxm_link_t *l, int32_t obj, const char *k, bool *neg, uint64_t *mag)
{
    int32_t i = zxm_get(l, obj, k);
    if (i < 0 || (l->tok[i].type != W4_J_STR && l->tok[i].type != W4_J_NUM)) return ZXM_E_PROTOCOL;
    const char *p = l->resp + l->tok[i].start, *e = l->resp + l->tok[i].end;
    *neg = p < e && *p == '-';
    if (*neg) p++;
    if (p == e) return ZXM_E_PROTOCOL;
    uint64_t v = 0;
    for (; p < e; p++) {
        if (*p < '0' || *p > '9') return ZXM_E_PROTOCOL;
        uint64_t d = (uint64_t) (*p - '0');
        if (v > (UINT64_MAX - d) / 10u) return ZXM_E_PROTOCOL;
        v = v * 10u + d;
    }
    *mag = v;
    return ZXM_OK;
}

static int dec_i64(const zxm_link_t *l, int32_t obj, const char *k, int64_t *v)
{
    bool neg;
    uint64_t m;
    int rc = dec(l, obj, k, &neg, &m);
    if (rc) return rc;
    if (m > (uint64_t) INT64_MAX + (neg ? 1u : 0u)) return ZXM_E_PROTOCOL;
    *v = neg ? (int64_t) (0u - m) : (int64_t) m;
    return ZXM_OK;
}

static int dec_u64(const zxm_link_t *l, int32_t obj, const char *k, uint64_t *v)
{
    bool neg;
    uint64_t m;
    int rc = dec(l, obj, k, &neg, &m);
    if (rc) return rc;
    if (neg && m) return ZXM_E_PROTOCOL;
    *v = m;
    return ZXM_OK;
}

/* The first code point of a UTF-8 string; 0 for "". The string is well
 * formed: the JSON tokenizer refuses anything else, and text() cuts on a
 * character boundary. */
static uint32_t first_cp(const char *s)
{
    const unsigned char *p = (const unsigned char *) s;
    uint32_t c = p[0];
    if (c < 0x80u) return c;
    uint32_t more = c >= 0xf0u ? 3u : c >= 0xe0u ? 2u : 1u;
    uint32_t cp = c & (0x3fu >> more);
    for (uint32_t k = 1; k <= more; k++) cp = cp << 6 | (p[k] & 0x3fu);
    return cp;
}

static uint64_t sat_add(uint64_t a, uint64_t b)
{
    return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

static uint64_t allotment(const murm_agent_t *a)
{
    return sat_add(sat_add(a->re, a->mk), a->im);
}

/* ===================== one snapshot ===================== */

#define TRY(x)                                                                                     \
    do {                                                                                           \
        int rc_ = (x);                                                                             \
        if (rc_) return rc_;                                                                       \
    } while (0)

static int agent(const zxm_link_t *l, int32_t o, murm_agent_t *a)
{
    uint64_t v;
    memset(a, 0, sizeof *a);
    if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    TRY(num(l, o, "id", UINT32_MAX, &v, true));
    a->id = (uint32_t) v;
    TRY(text(l, o, "name", a->name, sizeof a->name, true));
    TRY(num(l, o, "level", UINT32_MAX, &v, true));
    a->level = (uint32_t) v;
    TRY(text(l, o, "emoji", a->emoji, sizeof a->emoji, true));
    a->codepoint = first_cp(a->emoji);
    TRY(num(l, o, "intensity", SWARM_EMO_MAX_INTENSITY, &v, true));
    a->intensity = (uint8_t) v;
    TRY(num(l, o, "re", UINT64_MAX, &a->re, true));
    TRY(num(l, o, "mk", UINT64_MAX, &a->mk, true));
    TRY(num(l, o, "im", UINT64_MAX, &a->im, true));
    TRY(num(l, o, "money", UINT64_MAX, &a->money, false));
    TRY(num(l, o, "social", UINT64_MAX, &a->social, false));
    TRY(num(l, o, "gen", UINT32_MAX, &v, false));
    a->gen = (uint32_t) v;
    TRY(num(l, o, "witness", MURM_WIT_GLUT, &v, true));
    a->witness = (uint8_t) v;
    TRY(text(l, o, "witness_by", a->witness_by, sizeof a->witness_by, false));
    return ZXM_OK;
}

/* "<emoji> <intensity>" (A2) */
static int mood(const zxm_link_t *l, int32_t st, murm_swarm_t *sw)
{
    char s[MURM_TEXT_MAX + 1];
    TRY(text(l, st, "mood", s, sizeof s, false));
    if (!s[0]) return ZXM_OK;
    const char *sp = strrchr(s, ' ');
    if (!sp || sp == s || (size_t) (sp - s) > MURM_EMOJI_MAX || sp[1] < '0' ||
        sp[1] > (char) ('0' + SWARM_EMO_MAX_INTENSITY) || sp[2])
        return ZXM_E_PROTOCOL;
    memcpy(sw->mood, s, (size_t) (sp - s));
    sw->mood[sp - s] = 0;
    sw->mood_codepoint = first_cp(sw->mood);
    sw->mood_intensity = (uint8_t) (sp[1] - '0');
    return ZXM_OK;
}

static int swarm(const zxm_link_t *l, int32_t st, murm_swarm_t *sw)
{
    uint64_t v;
    memset(sw, 0, sizeof *sw);
    if (st < 0 || l->tok[st].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    TRY(text(l, st, "version", sw->version, sizeof sw->version, false));
    TRY(text(l, st, "host", sw->host, sizeof sw->host, false));
    TRY(num(l, st, "cores", UINT32_MAX, &v, false));
    sw->cores = (uint32_t) v;
    TRY(num(l, st, "levels", UINT32_MAX, &v, true));
    sw->levels = (uint32_t) v;
    TRY(num(l, st, "tokens_per_cycle", UINT64_MAX, &sw->tokens_per_cycle, true));
    TRY(num(l, st, "tick", UINT64_MAX, &sw->tick, true));
    TRY(num(l, st, "cycle", UINT64_MAX, &sw->cycle, true));
    TRY(mood(l, st, sw));
    TRY(text(l, st, "audit", sw->audit, sizeof sw->audit, false));
    TRY(num(l, st, "audit_records", UINT64_MAX, &sw->audit_records, false));
    TRY(num(l, st, "ledger_postings", UINT64_MAX, &sw->ledger_postings, false));
    int32_t h = zxm_get(l, st, "ledger_halt");
    if (h >= 0) {
        int64_t why;
        if (w4_json_i64(l->resp, &l->tok[h], &why) != W4_OK || why < INT32_MIN || why > INT32_MAX)
            return ZXM_E_PROTOCOL;
        sw->ledger_halt = (int32_t) why;
    }
    int32_t arr = zxm_get(l, st, "agents");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t total = zxm_len(l, arr);
    for (uint32_t i = 0; i < total; i++) {
        murm_agent_t a;
        TRY(agent(l, zxm_at(l, arr, i), &a));
        sw->allotted = sat_add(sw->allotted, allotment(&a));
        if (sw->n < MURM_AGENTS) sw->agent[sw->n++] = a;
    }
    sw->n_total = total;
    sw->truncated = total > MURM_AGENTS;
    return ZXM_OK;
}

static int model(const zxm_link_t *l, int32_t o, murm_model_t *mo)
{
    memset(mo, 0, sizeof *mo);
    if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    TRY(flag(l, o, "loaded", &mo->loaded, true));
    TRY(flag(l, o, "can_generate", &mo->can_generate, true));
    TRY(text(l, o, "name", mo->name, sizeof mo->name, false));
    TRY(text(l, o, "arch", mo->arch, sizeof mo->arch, false));
    TRY(text(l, o, "status", mo->status, sizeof mo->status, false));
    return ZXM_OK;
}

static int earnings(const zxm_link_t *l, int32_t o, murm_earnings_t *ea)
{
    memset(ea, 0, sizeof *ea);
    if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    TRY(text(l, o, "currency", ea->currency, sizeof ea->currency, true));
    TRY(dec_i64(l, o, "rate_minor", &ea->rate_minor));
    TRY(dec_i64(l, o, "balance_minor", &ea->balance_minor));
    TRY(dec_u64(l, o, "postings", &ea->postings));
    TRY(dec_u64(l, o, "cycles", &ea->cycles));
    TRY(flag(l, o, "resumed", &ea->resumed, false));
    return ZXM_OK;
}

/* ===================== actions ===================== */

void murm_init(murmuration_t *m, zxm_link_t *link)
{
    memset(m, 0, sizeof *m);
    m->link = link;
    m->sel = -1;
}

int murm_refresh(murmuration_t *m)
{
    static murm_swarm_t sw;
    static murm_model_t mo;
    static murm_earnings_t ea;
    zxm_link_t *l = m->link;
    zxm_begin(l, "swarm.state");
    TRY(zxm_call(l));
    TRY(swarm(l, l->result, &sw));
    bool have_model = false, have_earn = false;
    int32_t model_code = 0, earn_code = 0;
    int32_t mobj = zxm_get(l, l->result, "model");
    if (mobj >= 0) {
        TRY(model(l, mobj, &mo));
        have_model = true;
    } else {
        zxm_begin(l, "model.info");
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE)
            model_code = l->err_code;
        else if (rc)
            return rc;
        else {
            TRY(model(l, l->result, &mo));
            have_model = true;
        }
    }
    zxm_begin(l, "swarm.earnings");
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE)
        earn_code = l->err_code;
    else if (rc)
        return rc;
    else {
        TRY(earnings(l, l->result, &ea));
        have_earn = true;
    }

    /* all of it arrived: take it (R2), and follow the selection (R4) */
    bool had = m->sel >= 0;
    uint32_t sel_id = had ? m->sw.agent[m->sel].id : 0;
    m->sw = sw;
    m->have_model = have_model;
    m->model_code = model_code;
    if (have_model)
        m->model = mo;
    else
        memset(&m->model, 0, sizeof m->model);
    m->have_earnings = have_earn;
    m->earn_code = earn_code;
    if (have_earn)
        m->earn = ea;
    else
        memset(&m->earn, 0, sizeof m->earn);
    m->sel = -1;
    for (uint32_t i = 0; had && i < m->sw.n; i++)
        if (m->sw.agent[i].id == sel_id) {
            m->sel = (int32_t) i;
            break;
        }
    m->refreshes++;
    return ZXM_OK;
}

int murm_select(murmuration_t *m, int32_t i)
{
    if (i < -1 || i >= (int32_t) m->sw.n) return ZXM_E_ARG;
    m->sel = i;
    return ZXM_OK;
}

int32_t murm_find(const murmuration_t *m, const char *name)
{
    for (uint32_t i = 0; i < m->sw.n; i++)
        if (!strcmp(m->sw.agent[i].name, name)) return (int32_t) i;
    return -1;
}

int murm_emotion(uint32_t codepoint)
{
    swarm_emotion_t e;
    if (swarm_emotion_from_codepoint(codepoint, &e) != SWARM_OK) return -1;
    return (int) e;
}

uint32_t murm_cycle_share(const murmuration_t *m, uint32_t i)
{
    if (i >= m->sw.n || !m->sw.allotted) return 0;
    uint64_t a = allotment(&m->sw.agent[i]), t = m->sw.allotted;
    while (t > UINT64_MAX / 1000u) { /* a <= t, so the share stays within 1000 */
        a >>= 1;
        t >>= 1;
    }
    return (uint32_t) (a * 1000u / t);
}

/* ===================== AI ===================== */

typedef struct {
    char *p;
    uint32_t n, cap;
} out_t;

static void add(out_t *o, const char *s)
{
    size_t k = strlen(s);
    if (k > o->cap - o->n) k = o->cap - o->n; /* zxm_ai_ask cuts to fit anyway */
    memcpy(o->p + o->n, s, k);
    o->n += (uint32_t) k;
}

static void add_i64(out_t *o, int64_t v)
{
    char d[21];
    int i = 20;
    uint64_t u = v < 0 ? 0u - (uint64_t) v : (uint64_t) v;
    d[i] = 0;
    do {
        d[--i] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u);
    if (v < 0) add(o, "-");
    add(o, d + i);
}

static void add_u64(out_t *o, uint64_t v)
{
    char d[21];
    int i = 20;
    d[i] = 0;
    do {
        d[--i] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    add(o, d + i);
}

static void add_agent(out_t *o, const murmuration_t *m, uint32_t i)
{
    static const char *const WIT[] = {"false", "true", "contested"};
    const murm_agent_t *a = &m->sw.agent[i];
    add(o, a->name);
    add(o, " (level ");
    add_u64(o, a->level);
    add(o, "): feeling ");
    add(o, a->emoji);
    add(o, " ");
    add_u64(o, a->intensity);
    add(o, ", tokens re ");
    add_u64(o, a->re);
    add(o, " mk ");
    add_u64(o, a->mk);
    add(o, " im ");
    add_u64(o, a->im);
    add(o, " (");
    add_u64(o, murm_cycle_share(m, i));
    add(o, " per mille of the cycle), witness ");
    add(o, WIT[a->witness]);
    add(o, " by ");
    add(o, a->witness_by[0] ? a->witness_by : "kernel");
    add(o, "\n");
}

static void add_swarm(out_t *o, const murmuration_t *m)
{
    const murm_swarm_t *s = &m->sw;
    add(o, "Cycle ");
    add_u64(o, s->cycle);
    add(o, ", ");
    add_u64(o, s->tokens_per_cycle);
    add(o, " tokens per cycle, ");
    add_u64(o, s->levels);
    add(o, " levels, mood ");
    add(o, s->mood[0] ? s->mood : "unknown");
    if (s->mood[0]) {
        add(o, " ");
        add_u64(o, s->mood_intensity);
    }
    add(o, ", audit ");
    add(o, s->audit[0] ? s->audit : "unknown");
    add(o, ", ledger ");
    if (s->ledger_halt) {
        add(o, "halted (");
        add_i64(o, s->ledger_halt);
        add(o, ")");
    } else {
        add(o, "running");
    }
    add(o, ", ");
    add_u64(o, s->n_total);
    add(o, " agents\n");
    for (uint32_t i = 0; i < s->n; i++) add_agent(o, m, i);
}

int murm_ai(murmuration_t *m, int32_t action)
{
    static char in[ZXM_ASK_MAX];
    out_t o = {in, 0, sizeof in};
    if (action < 0 || action >= MURM_AI_COUNT) return ZXM_E_ARG;
    if (!m->refreshes) return ZXM_E_STATE;
    if (action == MURM_AI_WHY_UNHAPPY) {
        if (m->sel < 0) return ZXM_E_STATE;
        add(&o, "Agent: ");
        add_agent(&o, m, (uint32_t) m->sel);
        add(&o, "\nThe swarm: ");
        add_swarm(&o, m);
        return zxm_ai_ask(m->link, &MURM_AI[action], action,
                          "Explain in plain words why this agent of the ZXV swarm may be unhappy, "
                          "from its feeling, its share of the cycle and its witness. Be brief.",
                          in, o.n, &m->held);
    }
    add_swarm(&o, m);
    return zxm_ai_ask(m->link, &MURM_AI[action], action,
                      "Explain in plain words what this ZXV swarm is doing this cycle: its mood, "
                      "how the tokens were shared and anything the witnesses contest. Be brief.",
                      in, o.n, &m->held);
}

int murm_ai_accept(murmuration_t *m)
{
    if (m->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    m->held.state = ZXM_HELD_ACCEPTED;
    return ZXM_OK;
}

int murm_ai_reject(murmuration_t *m)
{
    if (m->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    m->held.state = ZXM_HELD_REJECTED;
    m->held.len = 0;
    m->held.text[0] = 0;
    return ZXM_OK;
}

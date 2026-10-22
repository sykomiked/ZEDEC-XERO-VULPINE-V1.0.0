/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tideturn_model.c — Tideturn, software updates (see tideturn_model.h).
 * Every input is checked and every record made to fit before anything is
 * kept; nothing is ever installed. */
#include <stdio.h>
#include <string.h>

#include "tideturn_model.h"

/* ---- TM1: versions ------------------------------------------------------------ */

int tideturn_parse(const char *s, tideturn_version_t *v)
{
    int64_t part[3] = {0, 0, 0};
    uint32_t i = 0, k = 0, digits = 0;
    if (!s || !v) return ZXM_E_ARG;
    for (;; i++) {
        char c = s[i];
        if (i > TIDETURN_TEXT) return ZXM_E_ARG;
        if (c >= '0' && c <= '9') {
            if (digits == 1 && part[k] == 0) return ZXM_E_ARG; /* a leading zero */
            if (++digits > 3) return ZXM_E_ARG;
            part[k] = part[k] * 10 + (c - '0');
            continue;
        }
        if (!digits) return ZXM_E_ARG; /* an empty part */
        if (c == '.' && k < 2) {
            k++;
            digits = 0;
            continue;
        }
        if (c == 0 && k == 2) break;
        return ZXM_E_ARG;
    }
    v->major = part[0];
    v->minor = part[1];
    v->patch = part[2];
    return ZXM_OK;
}

int64_t tideturn_order(const tideturn_version_t *a, const tideturn_version_t *b)
{
    if (!a || !b) return TIDETURN_REFUSED;
    return tideturn_version_order(a->major, a->minor, a->patch, b->major, b->minor, b->patch);
}

/* ---- TM2: what update.check says ----------------------------------------------- */

static const struct {
    const char *text;
    uint32_t cls;
} STATUS[] = {
    {"up to date", TIDETURN_ST_CURRENT},
    {"update available", TIDETURN_ST_NEWER},
    {"update needs a newer installed version", TIDETURN_ST_NEEDS},
    {"unsigned bucket contents", TIDETURN_ST_UNSIGNED},
    {"IPNS record invalid", TIDETURN_ST_RECORD},
    {"IPNS record expired", TIDETURN_ST_RECORD},
    {"IPNS record older than one already seen", TIDETURN_ST_RECORD},
    {"signed manifest is malformed", TIDETURN_ST_MANIFEST},
    {"signed manifest older than one already seen", TIDETURN_ST_MANIFEST},
    {"bucket is not a valid directory", TIDETURN_ST_MANIFEST},
    {"network unavailable", TIDETURN_ST_OFFLINE},
    {"no clock for IPNS", TIDETURN_ST_OFFLINE},
    {"disabled", TIDETURN_ST_OFFLINE},
    {"not due", TIDETURN_ST_OFFLINE},
    {"no buckets", TIDETURN_ST_OFFLINE},
};

uint32_t tideturn_class(const char *status)
{
    if (!status || !status[0]) return TIDETURN_ST_NONE;
    for (uint32_t i = 0; i < sizeof STATUS / sizeof STATUS[0]; i++)
        if (!strcmp(status, STATUS[i].text)) return STATUS[i].cls;
    return TIDETURN_ST_OTHER;
}

/* zxvd's check verified a manifest's signature (its status says so) */
static int64_t check_signed(uint32_t cls)
{
    return cls == TIDETURN_ST_CURRENT || cls == TIDETURN_ST_NEWER || cls == TIDETURN_ST_NEEDS ||
           cls == TIDETURN_ST_MANIFEST;
}

/* ---- TM5: the trail ------------------------------------------------------------ */

void tideturn_init(tideturn_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->next_seq = 1;
}

/* zxvd's clock, ms since 1970 */
static int stamp_now(tideturn_t *t, int64_t *ms)
{
    zxm_link_t *l = t->link;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", ms) != ZXM_OK || *ms < 0) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* checked first (TR3), then kept; the oldest record goes when the trail is full */
static int keep(tideturn_t *t, const tideturn_version_t *v, int64_t sig, int64_t outcome,
                int64_t stamp)
{
    tideturn_record_t r;
    r.seq = t->next_seq;
    r.prev = t->n ? t->rec[t->n - 1u].seq : 0;
    r.v = *v;
    r.sig = sig;
    r.outcome = outcome;
    r.stamp = stamp;
    if (r.seq > TIDETURN_SEQ_MAX ||
        !tideturn_record_fits(r.seq, r.v.major, r.v.minor, r.v.patch, r.sig, r.outcome, r.stamp))
        return ZXM_E_ARG;
    if (t->n == TIDETURN_KEEP) {
        memmove(&t->rec[0], &t->rec[1], (TIDETURN_KEEP - 1u) * sizeof t->rec[0]);
        t->n--;
    }
    t->rec[t->n++] = r;
    t->next_seq++;
    return ZXM_OK;
}

uint32_t tideturn_trail(const tideturn_t *t)
{
    for (uint32_t i = 0; i < t->n; i++) {
        int64_t last = i ? t->rec[i - 1u].seq : t->rec[0].prev;
        if (!witness_linked(t->rec[i].seq, t->rec[i].prev, last)) return i;
    }
    return t->n;
}

bool tideturn_record_ok(const tideturn_t *t, uint32_t i)
{
    if (i >= t->n) return false;
    const tideturn_record_t *r = &t->rec[i];
    return tideturn_record_fits(r->seq, r->v.major, r->v.minor, r->v.patch, r->sig, r->outcome,
                                r->stamp);
}

/* ---- the notice ---------------------------------------------------------------- */

static void vtext(const tideturn_version_t *v, char *out, size_t cap)
{
    snprintf(out, cap, "%lld.%lld.%lld", (long long) v->major, (long long) v->minor,
             (long long) v->patch);
}

static void compose_check(tideturn_t *t)
{
    static const char *const WHAT[] = {
        "",
        "this system is current.",
        "a signed update is offered; nothing is installed without your word.",
        "the update needs a newer installed version first.",
        "what the bucket holds is not signed by a trusted key; it is never installable.",
        "the bucket's signed name record did not hold; nothing from it is trusted.",
        "the signed manifest did not hold; nothing from it is trusted.",
        "no check could run now; try again later.",
        "zxvd gave a status Tideturn does not know; nothing is trusted from it.",
    };
    snprintf(t->notice, sizeof t->notice, "Update check: %s: %s", t->status_text,
             WHAT[t->status < sizeof WHAT / sizeof WHAT[0] ? t->status : TIDETURN_ST_OTHER]);
}

static void compose_offer(tideturn_t *t)
{
    char o[64], r[64], m[64];
    vtext(&t->offer, o, sizeof o);
    vtext(&t->running, r, sizeof r);
    vtext(&t->min, m, sizeof m);
    switch (t->verdict) {
    case TIDETURN_OFFERED:
        snprintf(t->notice, sizeof t->notice,
                 "%s is newer than %s and both signatures verify. It waits for your word; "
                 "nothing is installed until you approve it.",
                 o, r);
        break;
    case TIDETURN_NO_SIGNATURE:
        snprintf(t->notice, sizeof t->notice,
                 "%s refused: it needs both signatures verified (first %s, second %s).", o,
                 t->firstsig == 1 ? "verified" : "not verified",
                 t->secondsig == 1 ? "verified" : "not verified");
        break;
    case TIDETURN_NOT_NEWER:
        snprintf(t->notice, sizeof t->notice, "%s refused: it is not newer than %s, which runs.", o,
                 r);
        break;
    default:
        snprintf(t->notice, sizeof t->notice,
                 "%s refused: it applies over %s or later, and %s runs.", o, m, r);
        break;
    }
}

/* ---- TM1-TM4: the actions ------------------------------------------------------ */

int tideturn_read_version(tideturn_t *t)
{
    zxm_link_t *l = t->link;
    char app[TIDETURN_TEXT + 1], lib[TIDETURN_TEXT + 1];
    tideturn_version_t run, libv;
    zxm_begin(l, "zxvd.version");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "app", app, sizeof app) != ZXM_OK ||
        zxm_get_str(l, l->result, "libzxv", lib, sizeof lib) != ZXM_OK ||
        tideturn_parse(lib, &libv) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    bool known = tideturn_parse(app, &run) == ZXM_OK;
    memcpy(t->app, app, sizeof app);
    t->lib = libv;
    t->known = known;
    if (known) t->running = run;
    if (known) {
        char r[64];
        vtext(&run, r, sizeof r);
        snprintf(t->notice, sizeof t->notice, "Running %s.", r);
    } else {
        snprintf(t->notice, sizeof t->notice,
                 "Running \"%s\", which is no version Tideturn can compare.", t->app);
    }
    return ZXM_OK;
}

int tideturn_check(tideturn_t *t)
{
    zxm_link_t *l = t->link;
    char status[TIDETURN_TEXT + 1];
    int64_t ms = 0;
    zxm_begin(l, "update.check");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "status", status, sizeof status) != ZXM_OK || !status[0])
        return ZXM_E_PROTOCOL;
    uint32_t cls = tideturn_class(status);
    rc = stamp_now(t, &ms);
    if (rc != ZXM_OK) return rc;
    static const tideturn_version_t NONE = {0, 0, 0};
    rc = keep(t, t->known ? &t->running : &NONE, check_signed(cls), TIDETURN_CHECKED, ms);
    if (rc != ZXM_OK) return rc;
    memcpy(t->status_text, status, sizeof status);
    t->status = cls;
    compose_check(t);
    return ZXM_OK;
}

int tideturn_offer(tideturn_t *t, const char *release, const char *min_version, int64_t firstsig,
                   int64_t secondsig)
{
    tideturn_version_t o, m;
    int64_t ms = 0, outcome;
    if (!t->known || t->pending) return ZXM_E_STATE;
    if (tideturn_parse(release, &o) != ZXM_OK || tideturn_parse(min_version, &m) != ZXM_OK ||
        tideturn_order(&m, &o) > 0) /* a release under its own min-version is no release */
        return ZXM_E_ARG;
    bool dual = tideturn_dual_signed(firstsig, secondsig);
    if (!dual)
        outcome = TIDETURN_NO_SIGNATURE;
    else if (tideturn_order(&o, &t->running) != 1)
        outcome = TIDETURN_NOT_NEWER;
    else if (tideturn_order(&t->running, &m) < 0)
        outcome = TIDETURN_TOO_OLD;
    else
        outcome = TIDETURN_OFFERED;
    int rc = stamp_now(t, &ms);
    if (rc != ZXM_OK) return rc;
    rc = keep(t, &o, dual ? 1 : 0, outcome, ms);
    if (rc != ZXM_OK) return rc;
    t->offer = o;
    t->min = m;
    t->firstsig = firstsig;
    t->secondsig = secondsig;
    t->verdict = outcome;
    t->pending = outcome == TIDETURN_OFFERED;
    compose_offer(t);
    return ZXM_OK;
}

int tideturn_confirm(tideturn_t *t, bool approve)
{
    int64_t ms = 0;
    char o[64];
    if (!t->pending) return ZXM_E_STATE;
    int rc = stamp_now(t, &ms);
    if (rc != ZXM_OK) return rc;
    rc = keep(t, &t->offer, 1, approve ? TIDETURN_APPROVED : TIDETURN_DECLINED, ms);
    if (rc != ZXM_OK) return rc;
    t->pending = false;
    vtext(&t->offer, o, sizeof o);
    if (approve)
        snprintf(t->notice, sizeof t->notice,
                 "You approved %s. zxvd has no method that installs an update, so install it "
                 "from the system's updater; Tideturn installs nothing.",
                 o);
    else
        snprintf(t->notice, sizeof t->notice, "You declined %s. Nothing is installed.", o);
    return ZXM_OK;
}

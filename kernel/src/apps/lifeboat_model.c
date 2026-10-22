/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lifeboat_model.c — Lifeboat (see lifeboat_model.h). */
#include <string.h>

#include "lifeboat_model.h"

/* ---- small helpers ---------------------------------------------------- */

/* a feature name as zxvd takes it: ^[a-z0-9_]{1,23}$ */
static bool name_ok(const char *s)
{
    if (!s || !*s) return false;
    for (size_t n = 0; s[n]; n++) {
        char c = s[n];
        if (n >= LIFEBOAT_NAME_MAX) return false;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

/* a string of at most max bytes (NULL counts as empty) */
static bool fits(const char *s, uint32_t max)
{
    if (!s) return true;
    for (uint32_t n = 0; n <= max; n++)
        if (!s[n]) return true;
    return false;
}

static int32_t find(const lifeboat_t *b, const char *name)
{
    for (uint32_t i = 0; name && i < b->nsw; i++)
        if (!strcmp(b->sw[i].name, name)) return (int32_t) i;
    return -1;
}

static void copy(char *out, uint32_t cap, const char *s)
{
    uint32_t n = 0;
    for (; s && s[n] && n + 1u < cap; n++) out[n] = s[n];
    out[n] = 0;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* x100 as "12.34" */
static bool put_x100(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char f[4] = {'.', (char) ('0' + (int) (v % 100 / 10)), (char) ('0' + (int) (v % 10)), 0};
    return put_num(out, cap, at, v / 100) && put(out, cap, at, f);
}

/* ---- the notice (LM6) ------------------------------------------------- */

static const char *const STEP[] = {
    "start normally",
    "free space: nothing can be written back until the disk is above the floor",
    "test a backup: nothing is restored from one that has not read back whole",
    "restore the books from the tested backup, with the owner's approval",
    "keep the broken journal as it is and look at it with the owner: zxvd has no method that "
    "repairs it",
};

static void compose(lifeboat_t *b)
{
    char *s = b->notice;
    uint32_t cap = sizeof b->notice, at = 0;
    const lifeboat_check_t *c = &b->check;
    bool ok = true;
    s[0] = 0;
    if (b->listed) {
        uint32_t on = 0;
        for (uint32_t i = 0; i < b->nsw; i++) on += b->sw[i].enabled;
        ok = put_num(s, cap, &at, b->nsw) && put(s, cap, &at, " switches, ") &&
             put_num(s, cap, &at, on) && put(s, cap, &at, " on");
        if (ok && b->switched_off)
            ok = put(s, cap, &at, "; safe start turned ") &&
                 put_num(s, cap, &at, b->switched_off) && put(s, cap, &at, " off");
        if (ok) ok = put(s, cap, &at, ". ");
    }
    if (ok && c->done) {
        ok = put(s, cap, &at, c->start_ok ? "Safe to start normally." : "Not safe to start yet.") &&
             put(s, cap, &at, " Journal: ") &&
             put(s, cap, &at,
                 !c->journal_on  ? "zxvd keeps none"
                 : c->journal_ok ? "intact"
                                 : "broken at ");
        if (ok && c->journal_on && !c->journal_ok)
            ok = put_num(s, cap, &at, (int64_t) c->broken_at) && put(s, cap, &at, " (") &&
                 put(s, cap, &at, c->journal_why) && put(s, cap, &at, ")");
        if (ok)
            ok = put(s, cap, &at, "; books: ") &&
                 put(s, cap, &at, c->books_ok ? "hold" : "do not hold (") &&
                 (c->books_ok || (put(s, cap, &at, c->books_why) && put(s, cap, &at, ")")));
        if (ok && c->used_x100 != LIFEBOAT_REFUSED)
            ok = put(s, cap, &at, "; disk ") && put_x100(s, cap, &at, c->used_x100) &&
                 put(s, cap, &at, "% used");
        if (ok)
            ok = put(s, cap, &at, c->room >= 1 ? ", above the floor" : ", at or under the floor");
        if (ok && c->tested)
            ok = put(s, cap, &at, "; backup ") && put_num(s, cap, &at, c->snapshot) &&
                 put(s, cap, &at, " reads back whole");
        else if (ok)
            ok = put(s, cap, &at, "; no tested backup (") && put(s, cap, &at, c->backup_why) &&
                 put(s, cap, &at, ")");
        if (ok)
            ok = put(s, cap, &at, ". Next: ") &&
                 put(s, cap, &at,
                     c->step >= 0 && c->step <= LIFEBOAT_STEP_JOURNAL ? STEP[c->step] : "?") &&
                 put(s, cap, &at, ".");
    }
    if (ok && b->landed[0])
        ok = put(s, cap, &at, " Restored ") && put_num(s, cap, &at, b->restored) &&
             put(s, cap, &at, " files as ") && put(s, cap, &at, b->landed) &&
             put(s, cap, &at, "; check again.");
    if (ok && b->update[0])
        ok = put(s, cap, &at, " Update check: ") && put(s, cap, &at, b->update) &&
             put(s, cap, &at, " (nothing installed).");
    if (!ok) put(s, cap, &at, " ...");
}

/* ---- the model -------------------------------------------------------- */

void lifeboat_init(lifeboat_t *b, zxm_link_t *link)
{
    memset(b, 0, sizeof *b);
    b->link = link;
    b->floor = LIFEBOAT_FLOOR;
    b->check.used_x100 = LIFEBOAT_REFUSED;
}

int lifeboat_set_dest(lifeboat_t *b, const char *dest)
{
    if (!dest || dest[0] != '/' || !fits(dest, LIFEBOAT_DEST)) return ZXM_E_ARG;
    copy(b->dest, sizeof b->dest, dest);
    b->check.done = false;
    compose(b);
    return ZXM_OK;
}

int lifeboat_set_floor(lifeboat_t *b, int64_t floor)
{
    if (floor < 0 || floor > LIFEBOAT_FLOOR_MAX) return ZXM_E_ARG;
    b->floor = floor;
    b->check.done = false;
    compose(b);
    return ZXM_OK;
}

int lifeboat_switches(lifeboat_t *b)
{
    zxm_link_t *l = b->link;
    static lifeboat_switch_t sw[LIFEBOAT_SWITCHES];
    zxm_begin(l, "features.get");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "features");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (n > LIFEBOAT_SWITCHES) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        int32_t f = zxm_at(l, arr, i);
        memset(&sw[i], 0, sizeof sw[i]);
        if (zxm_get_str(l, f, "name", sw[i].name, sizeof sw[i].name) != ZXM_OK ||
            !name_ok(sw[i].name) || zxm_get_bool(l, f, "enabled", &sw[i].enabled) != ZXM_OK ||
            zxm_get_bool(l, f, "regulated", &sw[i].regulated) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        for (uint32_t k = 0; k < i; k++)
            if (!strcmp(sw[k].name, sw[i].name)) return ZXM_E_PROTOCOL;
    }
    memcpy(b->sw, sw, n * sizeof sw[0]);
    b->nsw = n;
    b->listed = true;
    b->safe = true;
    for (uint32_t i = 0; i < n; i++)
        if (sw[i].enabled) b->safe = false;
    compose(b);
    return ZXM_OK;
}

/* features.set off, taken only when zxvd says back the same switch, off */
static int switch_off(lifeboat_t *b, uint32_t i)
{
    zxm_link_t *l = b->link;
    char name[LIFEBOAT_NAME_MAX + 1];
    bool on = true, reg = false;
    zxm_begin(l, "features.set");
    zxm_p_str(l, "feature", b->sw[i].name);
    zxm_p_bool(l, "enabled", false);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "feature", name, sizeof name) != ZXM_OK ||
        strcmp(name, b->sw[i].name) || zxm_get_bool(l, l->result, "enabled", &on) != ZXM_OK || on ||
        zxm_get_bool(l, l->result, "regulated", &reg) != ZXM_OK || reg != b->sw[i].regulated)
        return ZXM_E_PROTOCOL;
    b->sw[i].enabled = false;
    return ZXM_OK;
}

int lifeboat_safe_start(lifeboat_t *b, bool approved)
{
    if (!approved) return ZXM_E_STATE; /* LM1 */
    int rc = lifeboat_switches(b);
    if (rc != ZXM_OK) return rc;
    b->switched_off = 0;
    for (uint32_t i = 0; i < b->nsw; i++) {
        if (!b->sw[i].enabled) continue;
        rc = switch_off(b, i);
        if (rc != ZXM_OK) {
            compose(b);
            return rc;
        }
        b->switched_off++;
    }
    b->safe = true;
    b->check.done = false; /* the engines changed */
    compose(b);
    return ZXM_OK;
}

/* sys.info: zxvd's clock */
static int read_clock(zxm_link_t *l, int64_t *ms)
{
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", ms) != ZXM_OK || *ms <= 0) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

static int read_journal(zxm_link_t *l, lifeboat_check_t *c)
{
    zxm_begin(l, "journal.verify");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(l, l->result, "on", &c->journal_on) != ZXM_OK ||
        zxm_get_bool(l, l->result, "ok", &c->journal_ok) != ZXM_OK ||
        zxm_get_u64(l, l->result, "entries", &c->journal_entries) != ZXM_OK ||
        zxm_get_u64(l, l->result, "broken_at", &c->broken_at) != ZXM_OK ||
        zxm_get_str(l, l->result, "why", c->journal_why, sizeof c->journal_why) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    /* a chain that holds has no break and no reason; a broken one has both */
    if (c->journal_on && c->journal_ok != (c->broken_at == 0 && !c->journal_why[0]))
        return ZXM_E_PROTOCOL;
    if (c->journal_on && !c->journal_ok && !c->journal_why[0]) return ZXM_E_PROTOCOL;
    c->journal = !c->journal_on || c->journal_ok ? 1 : 0;
    return ZXM_OK;
}

static int read_books(zxm_link_t *l, lifeboat_check_t *c)
{
    bool ok = false;
    zxm_begin(l, "ledger.check");
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) { /* not open, or a failed check: they do not hold */
        copy(c->books_why, sizeof c->books_why, l->err);
        c->books_ok = false;
        c->books = 0;
        return ZXM_OK;
    }
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(l, l->result, "ok", &ok) != ZXM_OK) return ZXM_E_PROTOCOL;
    copy(c->books_why, sizeof c->books_why, ok ? "" : "the check failed");
    c->books_ok = ok;
    c->books = ok ? 1 : 0;
    return ZXM_OK;
}

static int read_disk(zxm_link_t *l, lifeboat_check_t *c, int64_t floor)
{
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_u64(l, l->result, "total", &c->total) != ZXM_OK ||
        zxm_get_u64(l, l->result, "free", &c->free) != ZXM_OK ||
        zxm_get_u64(l, l->result, "avail", &c->avail) != ZXM_OK ||
        c->total > (uint64_t) INT64_MAX || c->free > c->total || c->avail > c->total)
        return ZXM_E_PROTOCOL;
    c->used_x100 = lifeboat_used_x100((int64_t) c->total, (int64_t) c->free);
    int64_t room = (int64_t) c->avail - floor; /* both 0..INT64_MAX: no overflow */
    c->room = room > LIFEBOAT_BYTES_MAX ? LIFEBOAT_BYTES_MAX : room;
    return ZXM_OK;
}

/* the newest snapshot read back whole? A refusal of the destination is a
 * finding (no tested backup), not a failed check. */
static int read_backup(zxm_link_t *l, lifeboat_check_t *c, const char *dest)
{
    uint64_t total = 0, id = 0, bytes = 0, got = 0, bad = 0, missing = 0;
    bool ok = false;
    c->tested = 0;
    if (!dest[0]) {
        copy(c->backup_why, sizeof c->backup_why, "no Keepsake destination set");
        return ZXM_OK;
    }
    zxm_begin(l, "backup.list");
    zxm_p_str(l, "dest", dest);
    zxm_p_u64(l, "max", 1);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) {
        copy(c->backup_why, sizeof c->backup_why, l->err);
        return ZXM_OK;
    }
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "snapshots");
    if (zxm_get_u64(l, l->result, "total", &total) != ZXM_OK || arr < 0 ||
        l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > 1 || total > 100000 ||
        (total > 0) != (zxm_len(l, arr) == 1))
        return ZXM_E_PROTOCOL;
    c->snapshots = (uint32_t) total;
    if (!total) {
        copy(c->backup_why, sizeof c->backup_why, "no backup in the destination yet");
        return ZXM_OK;
    }
    int32_t s = zxm_at(l, arr, 0);
    if (zxm_get_u64(l, s, "snapshot", &id) != ZXM_OK || id < 1 || id > 100000 ||
        zxm_get_u64(l, s, "bytes", &bytes) != ZXM_OK || bytes > (uint64_t) LIFEBOAT_BYTES_MAX)
        return ZXM_E_PROTOCOL;
    zxm_begin(l, "backup.verify");
    zxm_p_str(l, "dest", dest);
    zxm_p_u64(l, "snapshot", id);
    rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) {
        copy(c->backup_why, sizeof c->backup_why, l->err);
        return ZXM_OK;
    }
    if (rc != ZXM_OK) return rc;
    if (zxm_get_u64(l, l->result, "snapshot", &got) != ZXM_OK || got != id ||
        zxm_get_bool(l, l->result, "ok", &ok) != ZXM_OK ||
        zxm_get_u64(l, l->result, "bad", &bad) != ZXM_OK ||
        zxm_get_u64(l, l->result, "missing", &missing) != ZXM_OK || bad > UINT32_MAX ||
        missing > UINT32_MAX || ok != (bad == 0 && missing == 0))
        return ZXM_E_PROTOCOL;
    c->snapshot = (uint32_t) id;
    c->snap_bytes = bytes;
    c->bad = (uint32_t) bad;
    c->missing = (uint32_t) missing;
    c->tested = ok ? 1 : 0;
    copy(c->backup_why, sizeof c->backup_why,
         ok ? "" : "the newest backup does not read back whole");
    return ZXM_OK;
}

int lifeboat_check(lifeboat_t *b)
{
    zxm_link_t *l = b->link;
    static lifeboat_check_t c;
    memset(&c, 0, sizeof c);
    int rc = read_clock(l, &c.time_ms);
    if (rc == ZXM_OK) rc = read_journal(l, &c);
    if (rc == ZXM_OK) rc = read_books(l, &c);
    if (rc == ZXM_OK) rc = read_disk(l, &c, b->floor);
    if (rc == ZXM_OK) rc = read_backup(l, &c, b->dest);
    if (rc != ZXM_OK) return rc;
    if (!lifeboat_results(c.journal, c.books, c.room, c.tested)) return ZXM_E_PROTOCOL;
    c.start_ok = lifeboat_start_ok(c.journal, c.books, c.room, c.tested);
    c.step = lifeboat_repair_step(c.journal, c.books, c.room, c.tested);
    if ((c.step == LIFEBOAT_STEP_NONE) != c.start_ok) return ZXM_E_PROTOCOL; /* LB4 */
    c.done = true;

    /* everything checked: commit, and file the record (LB6) */
    b->check = c;
    if (b->nrec == LIFEBOAT_RECORDS) {
        memmove(b->rec, b->rec + 1, (LIFEBOAT_RECORDS - 1u) * sizeof b->rec[0]);
        b->nrec--;
    }
    lifeboat_record_t *r = &b->rec[b->nrec++];
    r->seq = ++b->seq;
    r->step = c.step;
    r->journal = c.journal;
    r->books = c.books;
    r->tested = c.tested;
    r->room = c.room;
    r->stamp = c.time_ms;
    compose(b);
    return ZXM_OK;
}

int lifeboat_restore(lifeboat_t *b, const char *path, const char *to, bool approved)
{
    zxm_link_t *l = b->link;
    char landed[LIFEBOAT_PATH + 1];
    uint64_t files = 0;
    if (!fits(path, LIFEBOAT_PATH) || !fits(to, LIFEBOAT_PATH)) return ZXM_E_ARG;
    if (!approved) return ZXM_E_STATE; /* LM1 */
    const lifeboat_check_t *c = &b->check;
    if (!c->done || c->tested != 1 || !c->snapshot) return ZXM_E_STATE;
    /* past 18 digits of free bytes is more than any restore needs: held at 18 */
    int64_t writable =
        c->avail > (uint64_t) LIFEBOAT_BYTES_MAX ? LIFEBOAT_BYTES_MAX : (int64_t) c->avail;
    if (!lifeboat_restore_fits((int64_t) c->snap_bytes, writable, b->floor)) return ZXM_E_STATE;
    zxm_begin(l, "backup.restore");
    zxm_p_str(l, "dest", b->dest);
    zxm_p_u64(l, "snapshot", c->snapshot);
    if (path && path[0]) zxm_p_str(l, "path", path);
    if (to && to[0]) zxm_p_str(l, "to", to);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "to", landed, sizeof landed) != ZXM_OK || !landed[0] ||
        zxm_get_u64(l, l->result, "files", &files) != ZXM_OK || files < 1 || files > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    if (to && to[0] && strcmp(landed, to)) return ZXM_E_PROTOCOL; /* not where it was asked */
    memcpy(b->landed, landed, sizeof landed);
    b->restored = (uint32_t) files;
    b->check.done = false; /* the Hoard changed: check again */
    compose(b);
    return ZXM_OK;
}

int lifeboat_update_check(lifeboat_t *b)
{
    zxm_link_t *l = b->link;
    char status[LIFEBOAT_STATUS + 1];
    zxm_begin(l, "update.check");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "status", status, sizeof status) != ZXM_OK || !status[0])
        return ZXM_E_PROTOCOL;
    memcpy(b->update, status, sizeof status);
    compose(b);
    return ZXM_OK;
}

bool lifeboat_record(const lifeboat_t *b, uint32_t i)
{
    if (i >= b->nrec) return false;
    const lifeboat_record_t *r = &b->rec[i];
    return lifeboat_record_fits(r->seq, r->step, r->journal, r->books, r->tested, r->room,
                                r->stamp);
}

const lifeboat_switch_t *lifeboat_find(const lifeboat_t *b, const char *name)
{
    int32_t i = find(b, name);
    return i < 0 ? NULL : &b->sw[i];
}

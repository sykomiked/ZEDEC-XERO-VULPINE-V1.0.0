/* denconnect.c — sovereign-node governance + accountability. See denconnect.h. */
#include "denconnect.h"
#include "../robin_debanks/sha256.h"

static bool key_eq(const uint8_t *a, const uint8_t *b) {
    for (uint32_t i = 0; i < DEN_KEY_LEN; i++) if (a[i] != b[i]) return false;
    return true;
}
static void key_cpy(uint8_t *d, const uint8_t *s) {
    for (uint32_t i = 0; i < DEN_KEY_LEN; i++) d[i] = s[i];
}

/* audit chaining: head = SHA256(prev || actor || action || target || param) */
static void audit_link(const uint8_t prev[DEN_HASH_LEN], const uint8_t actor[DEN_KEY_LEN],
                       uint8_t action, const uint8_t target[DEN_KEY_LEN], uint32_t param,
                       uint8_t out[DEN_HASH_LEN]) {
    uint8_t buf[DEN_HASH_LEN + DEN_KEY_LEN + 1 + DEN_KEY_LEN + 4];
    uint32_t at = 0;
    for (uint32_t i = 0; i < DEN_HASH_LEN; i++) buf[at++] = prev[i];
    for (uint32_t i = 0; i < DEN_KEY_LEN; i++) buf[at++] = actor[i];
    buf[at++] = action;
    for (uint32_t i = 0; i < DEN_KEY_LEN; i++) buf[at++] = target[i];
    for (int i = 0; i < 4; i++) buf[at++] = (uint8_t)(param >> (8 * i));
    sha256(buf, at, out);
}

/* Record a successful rule change. Called only AFTER the mutation is applied,
 * so the log reflects exactly what took effect. */
static void audit_record(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
                         uint8_t action, const uint8_t target[DEN_KEY_LEN], uint32_t param) {
    if (s->n_audit >= DEN_MAX_AUDIT) return;   /* log full: further edits unlogged */
    den_audit_entry_t *e = &s->audit[s->n_audit];
    key_cpy(e->actor, actor);
    e->action = action;
    if (target) key_cpy(e->target, target); else for (uint32_t i=0;i<DEN_KEY_LEN;i++) e->target[i]=0;
    e->param = param;
    audit_link(s->audit_head, e->actor, e->action, e->target, e->param, e->head);
    for (uint32_t i = 0; i < DEN_HASH_LEN; i++) s->audit_head[i] = e->head[i];
    s->n_audit++;
}

bool den_init(den_server_t *s, const uint8_t operator_key[DEN_KEY_LEN]) {
    if (!s || !operator_key) return false;
    for (uint32_t i = 0; i < DEN_KEY_LEN; i++) s->operator_key[i] = operator_key[i];
    /* a sensible open-but-modest default; the operator may change all of it */
    s->guest_privileges = DEN_READ_CHAT | DEN_SEND_CHAT | DEN_READ_NEWS |
                          DEN_DOWNLOAD | DEN_READ_USERS;
    s->registration_open = true;
    s->visibility = DEN_PUBLIC;
    s->n_accounts = 0;
    s->n_banned = 0;
    for (uint32_t i = 0; i < DEN_MAX_ACCOUNTS; i++) s->account[i].present = false;
    s->n_audit = 0;
    for (uint32_t i = 0; i < DEN_HASH_LEN; i++) s->audit_head[i] = 0;   /* genesis */
    return true;
}

const uint8_t *den_audit_head(const den_server_t *s) { return s ? s->audit_head : 0; }
uint32_t den_audit_length(const den_server_t *s) { return s ? s->n_audit : 0; }

bool den_audit_verify(const den_server_t *s) {
    if (!s) return false;
    uint8_t h[DEN_HASH_LEN];
    for (uint32_t i = 0; i < DEN_HASH_LEN; i++) h[i] = 0;
    for (uint32_t i = 0; i < s->n_audit; i++) {
        const den_audit_entry_t *e = &s->audit[i];
        uint8_t expect[DEN_HASH_LEN];
        audit_link(h, e->actor, e->action, e->target, e->param, expect);
        for (uint32_t k = 0; k < DEN_HASH_LEN; k++)
            if (expect[k] != e->head[k]) return false;
        for (uint32_t k = 0; k < DEN_HASH_LEN; k++) h[k] = e->head[k];
    }
    for (uint32_t k = 0; k < DEN_HASH_LEN; k++)
        if (h[k] != s->audit_head[k]) return false;
    return true;
}

bool den_is_operator(const den_server_t *s, const uint8_t key[DEN_KEY_LEN]) {
    return s && key && key_eq(s->operator_key, key);
}

bool den_is_banned(const den_server_t *s, const uint8_t key[DEN_KEY_LEN]) {
    if (!s || !key) return false;
    for (uint32_t i = 0; i < s->n_banned; i++)
        if (key_eq(s->banned[i], key)) return true;
    return false;
}

static const den_account_t *find_account(const den_server_t *s,
                                          const uint8_t key[DEN_KEY_LEN]) {
    for (uint32_t i = 0; i < DEN_MAX_ACCOUNTS; i++)
        if (s->account[i].present && key_eq(s->account[i].key, key))
            return &s->account[i];
    return 0;
}

uint32_t den_privileges(const den_server_t *s, const uint8_t key[DEN_KEY_LEN]) {
    if (!s || !key) return 0;
    /* The operator is sovereign and always holds everything. */
    if (den_is_operator(s, key)) return DEN_ALL;
    /* A ban overrides any account. */
    if (den_is_banned(s, key)) return 0;
    const den_account_t *a = find_account(s, key);
    if (a) return a->privileges;
    return s->guest_privileges;     /* everyone else is a guest */
}

bool den_can(const den_server_t *s, const uint8_t key[DEN_KEY_LEN], uint32_t priv) {
    return (den_privileges(s, key) & priv) == priv;
}

bool den_may_connect(const den_server_t *s, const uint8_t key[DEN_KEY_LEN]) {
    if (!s || !key) return false;
    if (den_is_banned(s, key)) return false;
    switch (s->visibility) {
    case DEN_PRIVATE: return den_is_operator(s, key);
    case DEN_SELECT:  return den_is_operator(s, key) || find_account(s, key) != 0;
    case DEN_PUBLIC:
    default:           return true;
    }
}

bool den_set_guest_privileges(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
                               uint32_t privileges) {
    if (!s || !actor || !den_can(s, actor, DEN_SET_ACCESS)) return false;
    s->guest_privileges = privileges & DEN_ALL;
    audit_record(s, actor, DEN_ACT_SET_GUEST, 0, s->guest_privileges);
    return true;
}

bool den_set_visibility(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
                         den_visibility_t v) {
    if (!s || !actor || !den_can(s, actor, DEN_SET_ACCESS)) return false;
    s->visibility = v;
    audit_record(s, actor, DEN_ACT_SET_VIS, 0, (uint32_t)v);
    return true;
}

bool den_set_account(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
                      const uint8_t key[DEN_KEY_LEN], const char *name,
                      uint32_t privileges) {
    if (!s || !actor || !key) return false;
    if (!den_can(s, actor, DEN_CREATE_ACCOUNT)) return false;
    /* No privilege escalation: to hand out SET_ACCESS you must already hold
     * it, so no account can mint an admin from below its own authority. */
    if ((privileges & DEN_SET_ACCESS) && !den_can(s, actor, DEN_SET_ACCESS))
        return false;
    privileges &= DEN_ALL;

    /* update an existing account */
    for (uint32_t i = 0; i < DEN_MAX_ACCOUNTS; i++) {
        if (s->account[i].present && key_eq(s->account[i].key, key)) {
            s->account[i].privileges = privileges;
            audit_record(s, actor, DEN_ACT_SET_ACCOUNT, key, privileges);
            return true;
        }
    }
    /* create a new one */
    for (uint32_t i = 0; i < DEN_MAX_ACCOUNTS; i++) {
        if (s->account[i].present) continue;
        den_account_t *a = &s->account[i];
        key_cpy(a->key, key);
        uint32_t j = 0;
        if (name) for (; j < DEN_NAME_LEN - 1 && name[j]; j++) a->name[j] = name[j];
        a->name[j] = '\0';
        a->privileges = privileges;
        a->present = true;
        s->n_accounts++;
        audit_record(s, actor, DEN_ACT_SET_ACCOUNT, key, privileges);
        return true;
    }
    return false;               /* account table full */
}

bool den_ban(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
              const uint8_t key[DEN_KEY_LEN]) {
    if (!s || !actor || !key) return false;
    if (!den_can(s, actor, DEN_BAN)) return false;
    if (den_is_operator(s, key)) return false;    /* the operator is unbannable */
    if (den_is_banned(s, key)) return true;
    if (s->n_banned >= DEN_MAX_BANNED) return false;
    key_cpy(s->banned[s->n_banned++], key);
    audit_record(s, actor, DEN_ACT_BAN, key, 0);
    return true;
}

bool den_unban(den_server_t *s, const uint8_t actor[DEN_KEY_LEN],
                const uint8_t key[DEN_KEY_LEN]) {
    if (!s || !actor || !key) return false;
    if (!den_can(s, actor, DEN_BAN)) return false;
    for (uint32_t i = 0; i < s->n_banned; i++) {
        if (key_eq(s->banned[i], key)) {
            key_cpy(s->banned[i], s->banned[--s->n_banned]);   /* swap-remove */
            audit_record(s, actor, DEN_ACT_UNBAN, key, 0);
            return true;
        }
    }
    return false;
}


/* ===================== the den fleet ===================== */
void den_fleet_init(den_fleet_t *f, const uint8_t operator_key[DEN_KEY_LEN]) {
    if (!f || !operator_key) return;
    key_cpy(f->operator_key, operator_key);
    f->n = 0;
    for (uint32_t i = 0; i < DEN_MAX_FLEET; i++) {
        f->active[i] = false;
        f->label[i][0] = '\0';
    }
}

int32_t den_fleet_found(den_fleet_t *f, const char *label) {
    if (!f || !label || !label[0]) return -1;
    if (den_fleet_find(f, label) >= 0) return -1;      /* labels are unique */
    for (uint32_t i = 0; i < DEN_MAX_FLEET; i++) {
        if (f->active[i]) continue;
        den_init(&f->den[i], f->operator_key);         /* isolated fresh den */
        uint32_t j = 0;
        for (; j < DEN_NAME_LEN - 1 && label[j]; j++) f->label[i][j] = label[j];
        f->label[i][j] = '\0';
        f->active[i] = true;
        f->n++;
        return (int32_t)i;
    }
    return -1;                                         /* fleet full */
}

den_server_t *den_fleet_get(den_fleet_t *f, uint32_t idx) {
    if (!f || idx >= DEN_MAX_FLEET || !f->active[idx]) return 0;
    return &f->den[idx];
}

int32_t den_fleet_find(const den_fleet_t *f, const char *label) {
    if (!f || !label) return -1;
    for (uint32_t i = 0; i < DEN_MAX_FLEET; i++) {
        if (!f->active[i]) continue;
        uint32_t j = 0;
        for (; j < DEN_NAME_LEN; j++) {
            if (f->label[i][j] != label[j]) break;
            if (label[j] == '\0') return (int32_t)i;
        }
    }
    return -1;
}

bool den_fleet_close(den_fleet_t *f, uint32_t idx) {
    if (!f || idx >= DEN_MAX_FLEET || !f->active[idx]) return false;
    f->active[idx] = false;
    f->label[idx][0] = '\0';
    if (f->n) f->n--;
    return true;
}

uint32_t den_fleet_count(const den_fleet_t *f) { return f ? f->n : 0; }

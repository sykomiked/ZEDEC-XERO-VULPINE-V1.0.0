/* caracho_gov.c — sovereign-node governance. See caracho_gov.h. */
#include "caracho_gov.h"

static bool key_eq(const uint8_t *a, const uint8_t *b) {
    for (uint32_t i = 0; i < CGOV_KEY_LEN; i++) if (a[i] != b[i]) return false;
    return true;
}
static void key_cpy(uint8_t *d, const uint8_t *s) {
    for (uint32_t i = 0; i < CGOV_KEY_LEN; i++) d[i] = s[i];
}

bool cgov_init(cgov_server_t *s, const uint8_t operator_key[CGOV_KEY_LEN]) {
    if (!s || !operator_key) return false;
    for (uint32_t i = 0; i < CGOV_KEY_LEN; i++) s->operator_key[i] = operator_key[i];
    /* a sensible open-but-modest default; the operator may change all of it */
    s->guest_privileges = CGOV_READ_CHAT | CGOV_SEND_CHAT | CGOV_READ_NEWS |
                          CGOV_DOWNLOAD | CGOV_READ_USERS;
    s->registration_open = true;
    s->visibility = CGOV_PUBLIC;
    s->n_accounts = 0;
    s->n_banned = 0;
    for (uint32_t i = 0; i < CGOV_MAX_ACCOUNTS; i++) s->account[i].present = false;
    return true;
}

bool cgov_is_operator(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN]) {
    return s && key && key_eq(s->operator_key, key);
}

bool cgov_is_banned(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN]) {
    if (!s || !key) return false;
    for (uint32_t i = 0; i < s->n_banned; i++)
        if (key_eq(s->banned[i], key)) return true;
    return false;
}

static const cgov_account_t *find_account(const cgov_server_t *s,
                                          const uint8_t key[CGOV_KEY_LEN]) {
    for (uint32_t i = 0; i < CGOV_MAX_ACCOUNTS; i++)
        if (s->account[i].present && key_eq(s->account[i].key, key))
            return &s->account[i];
    return 0;
}

uint32_t cgov_privileges(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN]) {
    if (!s || !key) return 0;
    /* The operator is sovereign and always holds everything. */
    if (cgov_is_operator(s, key)) return CGOV_ALL;
    /* A ban overrides any account. */
    if (cgov_is_banned(s, key)) return 0;
    const cgov_account_t *a = find_account(s, key);
    if (a) return a->privileges;
    return s->guest_privileges;     /* everyone else is a guest */
}

bool cgov_can(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN], uint32_t priv) {
    return (cgov_privileges(s, key) & priv) == priv;
}

bool cgov_may_connect(const cgov_server_t *s, const uint8_t key[CGOV_KEY_LEN]) {
    if (!s || !key) return false;
    if (cgov_is_banned(s, key)) return false;
    switch (s->visibility) {
    case CGOV_PRIVATE: return cgov_is_operator(s, key);
    case CGOV_SELECT:  return cgov_is_operator(s, key) || find_account(s, key) != 0;
    case CGOV_PUBLIC:
    default:           return true;
    }
}

bool cgov_set_guest_privileges(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
                               uint32_t privileges) {
    if (!s || !actor || !cgov_can(s, actor, CGOV_SET_ACCESS)) return false;
    s->guest_privileges = privileges & CGOV_ALL;
    return true;
}

bool cgov_set_visibility(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
                         cgov_visibility_t v) {
    if (!s || !actor || !cgov_can(s, actor, CGOV_SET_ACCESS)) return false;
    s->visibility = v;
    return true;
}

bool cgov_set_account(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
                      const uint8_t key[CGOV_KEY_LEN], const char *name,
                      uint32_t privileges) {
    if (!s || !actor || !key) return false;
    if (!cgov_can(s, actor, CGOV_CREATE_ACCOUNT)) return false;
    /* No privilege escalation: to hand out SET_ACCESS you must already hold
     * it, so no account can mint an admin from below its own authority. */
    if ((privileges & CGOV_SET_ACCESS) && !cgov_can(s, actor, CGOV_SET_ACCESS))
        return false;
    privileges &= CGOV_ALL;

    /* update an existing account */
    for (uint32_t i = 0; i < CGOV_MAX_ACCOUNTS; i++) {
        if (s->account[i].present && key_eq(s->account[i].key, key)) {
            s->account[i].privileges = privileges;
            return true;
        }
    }
    /* create a new one */
    for (uint32_t i = 0; i < CGOV_MAX_ACCOUNTS; i++) {
        if (s->account[i].present) continue;
        cgov_account_t *a = &s->account[i];
        key_cpy(a->key, key);
        uint32_t j = 0;
        if (name) for (; j < CGOV_NAME_LEN - 1 && name[j]; j++) a->name[j] = name[j];
        a->name[j] = '\0';
        a->privileges = privileges;
        a->present = true;
        s->n_accounts++;
        return true;
    }
    return false;               /* account table full */
}

bool cgov_ban(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
              const uint8_t key[CGOV_KEY_LEN]) {
    if (!s || !actor || !key) return false;
    if (!cgov_can(s, actor, CGOV_BAN)) return false;
    if (cgov_is_operator(s, key)) return false;    /* the operator is unbannable */
    if (cgov_is_banned(s, key)) return true;
    if (s->n_banned >= CGOV_MAX_BANNED) return false;
    key_cpy(s->banned[s->n_banned++], key);
    return true;
}

bool cgov_unban(cgov_server_t *s, const uint8_t actor[CGOV_KEY_LEN],
                const uint8_t key[CGOV_KEY_LEN]) {
    if (!s || !actor || !key) return false;
    if (!cgov_can(s, actor, CGOV_BAN)) return false;
    for (uint32_t i = 0; i < s->n_banned; i++) {
        if (key_eq(s->banned[i], key)) {
            key_cpy(s->banned[i], s->banned[--s->n_banned]);   /* swap-remove */
            return true;
        }
    }
    return false;
}

/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "quest_guard.h"

void qst_mechanic_plain(qst_mechanic_t *m, qst_reward_kind_t reward)
{
    if (!m) return;
    m->reward = (uint8_t) reward;
    m->schedule = QST_SCHED_FIXED;
    m->paid_entry = false;
    m->random_outcome = false;
    m->loss_on_lapse = false;
    m->purchase_timer = false;
    m->purchase_scarcity = false;
    m->unrequested_push = false;
    m->ranks_people = false;
    m->no_stopping_point = false;
    m->for_minors = false;
    m->hides_opt_out = false;
    m->opt_out_forfeits = false;
}

uint32_t qst_guard_check(const qst_mechanic_t *m)
{
    if (!m) return QST_V_BAD_ENUM;
    uint32_t v = 0;
    if (m->reward > QST_RW_MONEY || m->schedule > QST_SCHED_VARIABLE_INTERVAL) v |= QST_V_BAD_ENUM;
    bool money = m->reward == QST_RW_MONEY;
    bool status = m->reward == QST_RW_STANDING || m->reward == QST_RW_BADGE;
    bool fixed = m->schedule == QST_SCHED_FIXED;

    if (m->paid_entry && m->random_outcome && m->reward != QST_RW_NONE) v |= QST_V_LOOTBOX;
    if (m->random_outcome && (m->paid_entry || money)) v |= QST_V_PAID_RANDOM;
    if (m->loss_on_lapse) v |= QST_V_STREAK_LOSS;
    if (money && !fixed) v |= QST_V_VARIABLE_MONEY;
    if (m->purchase_timer || m->purchase_scarcity) v |= QST_V_PURCHASE_PRESS;
    if (m->unrequested_push) v |= QST_V_PULL_NOTIFY;
    if (m->paid_entry && status) v |= QST_V_PAY_STANDING;
    if (m->reward == QST_RW_BADGE && (m->random_outcome || !fixed)) v |= QST_V_BADGE_UNEARNED;
    if (m->no_stopping_point) v |= QST_V_NO_STOP;
    if (m->for_minors && (m->ranks_people || money || m->paid_entry)) v |= QST_V_MINORS;
    if (m->hides_opt_out || m->opt_out_forfeits) v |= QST_V_OPT_OUT;
    return v;
}

static const char *const vname[QST_V_COUNT] = {
    "loot box",          "paid randomness",   "streak-loss punishment", "variable-ratio money",
    "purchase pressure", "pull notification", "paying for standing",    "unearned badge",
    "no stopping point", "minor protections", "opt-out penalty",        "bad enum"};

const char *qst_guard_violation_name(uint32_t v)
{
    for (uint32_t i = 0; i < QST_V_COUNT; i++)
        if (v & (1u << i)) return vname[i];
    return "";
}

/* Pressure phrases, lower case ASCII. Matched case-insensitively. */
static const char *const pressure[] = {"hurry",
                                       "last chance",
                                       "don't miss",
                                       "dont miss",
                                       "do not miss",
                                       "miss out",
                                       "act now",
                                       "expires in",
                                       "ends in",
                                       "ending soon",
                                       "limited time",
                                       "only a few",
                                       "left in stock",
                                       "before it's gone",
                                       "streak",
                                       "you'll lose",
                                       "you will lose",
                                       "lose your",
                                       "we miss you",
                                       "come back",
                                       "don't leave",
                                       "are you sure you want to leave",
                                       "!!",
                                       "0 left",
                                       "1 left",
                                       "2 left",
                                       "3 left",
                                       "4 left",
                                       "5 left",
                                       "6 left",
                                       "7 left",
                                       "8 left",
                                       "9 left"};

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c + ('a' - 'A')) : c;
}

static bool contains(const char *hay, const char *needle)
{
    for (const char *h = hay; *h; h++) {
        const char *a = h, *b = needle;
        while (*a && *b && lower(*a) == *b) {
            a++;
            b++;
        }
        if (!*b) return true;
    }
    return false;
}

bool qst_copy_is_calm(const char *text)
{
    if (!text) return true;
    for (uint32_t i = 0; i < sizeof pressure / sizeof pressure[0]; i++)
        if (contains(text, pressure[i])) return false;
    return true;
}

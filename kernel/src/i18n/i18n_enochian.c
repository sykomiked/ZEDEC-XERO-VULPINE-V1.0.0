/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* i18n_enochian.c — Enochian core / operator-language edge. See i18n_enochian.h. */
#include "i18n_enochian.h"
#include "i18n_internal.h"

int32_t i18n_enochian_render(const char *text, uint32_t len, i18n_en_form_t form, char *out,
                             uint32_t cap)
{
    uint32_t i = 0, n = 0;
    bool pending_space = false;
    if (!out || cap == 0) return -1;
    out[0] = 0;
    if (!text) return 0;
    while (i < len && text[i]) {
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '\n') {
            pending_space = n > 0;
            i++;
            continue;
        }
        uint32_t j = i;
        while (j < len && text[j] && text[j] != ' ' && text[j] != '\t' && text[j] != '\n') j++;
        uint32_t room = cap - n - (pending_space ? 1u : 0u);
        if (cap <= n + (pending_space ? 1u : 0u)) {
            out[n] = 0;
            return -1;
        }
        char *dst = out + n + (pending_space ? 1u : 0u);
        int32_t w = form == I18N_EN_IAST ? swarm_en_iast(text + i, j - i, dst, room)
                                         : swarm_en_sanskrit(text + i, j - i, dst, room);
        if (w < 0) {
            out[n] = 0;
            return -1;
        }
        if (w > 0) {
            if (pending_space) out[n++] = ' ';
            n += (uint32_t) w;
        }
        out[n] = 0;
        pending_space = false;
        i = j;
    }
    return (int32_t) n;
}

const i18n_locale_t *i18n_edge_locale(const swarm_lang_table_t *t)
{
    if (!t || t->operator_lang >= t->count) return i18n_locale_default();
    return i18n_locale_resolve(t->lang[t->operator_lang].code);
}

const char *i18n_edge_msg(const swarm_lang_table_t *t, i18n_msgid_t id, i18n_msg_info_t *info)
{
    return i18n_msg(i18n_edge_locale(t), id, info);
}

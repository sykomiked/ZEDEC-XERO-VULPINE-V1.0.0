/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* i18n_enochian.h — the edge between the Enochian core and the operator's
 * language (swarm_enochian.h, E3 and E8).
 *
 * THE RULE (E8): the swarm processes and stores in Enochian and translates at
 * the edge. In this layer that means:
 *   - the core holds message IDS (i18n_msgid_t) and their stable keys
 *     (i18n_msg_key: "PAY_CONFIRM_TITLE"), never translated text;
 *   - the operator language installed in a swarm_lang_table_t picks the
 *     catalog only when text is shown (i18n_edge_msg);
 *   - changing the operator language changes nothing the core stores:
 *     swarm_lang_core() stays "enochian" and every id and key is the same.
 *
 * E3 rendering: an Enochian-letter string shown in Devanagari or IAST,
 * letter by letter through swarm_en_sanskrit / swarm_en_iast. Word breaks
 * (runs of spaces) are kept as one space; characters outside the Enochian
 * alphabet are dropped, exactly as swarm_en_sanskrit drops them.
 *
 * Links against kernel/src/swarm/swarm_enochian.c (and swarm_budget.c, which
 * it uses). The rest of kernel/src/i18n does not depend on the swarm.
 */
#ifndef ZXV_I18N_ENOCHIAN_H
#define ZXV_I18N_ENOCHIAN_H

#include <stdint.h>
#include "i18n.h"
#include "swarm_enochian.h"

typedef enum { I18N_EN_DEVANAGARI = 0, I18N_EN_IAST = 1 } i18n_en_form_t;

/* Render Enochian text in Devanagari or IAST. Returns bytes written, or -1
 * if it does not fit (out then holds a NUL-terminated prefix ending at a word
 * boundary). */
int32_t i18n_enochian_render(const char *text, uint32_t len, i18n_en_form_t form, char *out,
                             uint32_t cap);

/* The locale for the table's operator language (resolved like any tag, so an
 * installed "sa" gives Sanskrit and "pt-AO" gives Angolan Portuguese). */
const i18n_locale_t *i18n_edge_locale(const swarm_lang_table_t *t);

/* Translate a core message id at the edge, into the operator's language. */
const char *i18n_edge_msg(const swarm_lang_table_t *t, i18n_msgid_t id, i18n_msg_info_t *info);

#endif /* ZXV_I18N_ENOCHIAN_H */

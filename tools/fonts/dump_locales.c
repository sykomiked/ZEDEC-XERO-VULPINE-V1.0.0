/* dump_locales.c — print every i18n locale as one tab-separated line:
 *     tag  script  rtl  english-name  needed-ranges (lo-hi,lo-hi,...)
 * Input for tools/fonts/gen_ascw_golden.py. Host tool, not part of the kernel.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include "i18n.h"

int main(void)
{
    for (uint32_t i = 0; i < i18n_locale_count(); i++) {
        const i18n_locale_t *l = i18n_locale_at(i);
        const i18n_range_t *r;
        uint32_t gaps;
        uint32_t n = i18n_locale_needs(l, &r, &gaps);
        printf("%s\t%s\t%d\t%s\t", i18n_locale_tag(l), i18n_locale_script(l),
               (int) i18n_locale_rtl(l), i18n_locale_english(l));
        for (uint32_t k = 0; k < n; k++) printf("%s%X-%X", k ? "," : "", r[k].lo, r[k].hi);
        printf("\n");
    }
    return 0;
}

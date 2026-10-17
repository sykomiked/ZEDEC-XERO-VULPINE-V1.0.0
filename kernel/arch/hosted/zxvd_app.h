/* zxvd_app.h — the live app testbed (zxvd_app.c). Copyright (c) 2024-2026
 * Michael Laurence Curzi and 36N9 Genetics, LLC; SPDX-License-Identifier:
 * Apache-2.0. */
#ifndef ZXVD_APP_H
#define ZXVD_APP_H

#include <stddef.h>

int zxvd_app_init(void);
int zxvd_app_handle(const char *body, size_t len, char **out, size_t *out_len);
void zxvd_app_free(char *p);

#endif /* ZXVD_APP_H */
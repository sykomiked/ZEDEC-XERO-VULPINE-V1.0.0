/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pp_console.h -- the passport login of a console (the bare-metal shell).
 *
 * One person at a time is logged in. pp_console_login reads a typed seed
 * (pp_seed_from_text: 55 Crockford base32 characters with a checksum, dashes
 * and spaces ignored), rebuilds the whole identity at PQM_LEVEL_MATRIX
 * (pp_hydrate) and keeps it until pp_console_logout wipes it. The seed itself
 * is wiped as soon as the identity is built. Lines printed through out:
 *
 *   [LOGIN_OK] did:zxv:<base32 id>
 *   [LOGIN_FAIL] <why>             (a typo: the checksum, or a bad format)
 *   [LOGOUT] did:zxv:<...>         or [LOGOUT] nobody was logged in
 *   [WHOAMI] did:zxv:<...>         or [WHOAMI] nobody (log in: login <seed text>)
 *
 * A first-time person makes an identity from nothing, no server:
 * pp_console_create takes 32 random bytes from the caller (the kernel's
 * hardware source; the caller refuses when it has none), prints the seed text
 * once and holds the seed; pp_console_confirm takes the text typed back and,
 * only when it is the same seed, builds the identity and logs in:
 *
 *   [NEW_SEED] <seed text>         then a line asking to write it down
 *   [CREATED] did:zxv:<...>        and [LOGIN_OK] did:zxv:<...>
 *   [CREATE_FAIL] <why>            all-zero random bytes, nothing pending,
 *                                  or the typed text is not the seed shown
 *
 * This console takes the seed text only. Passphrase login needs Argon2id's
 * 64 MiB work area, which the kernel does not set aside; café mode is for
 * machines the person does not own and needs a phone, not a console.
 *
 * Freestanding: passport, ident and pqsec only; no libc, no allocation. */
#ifndef PP_CONSOLE_H
#define PP_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

/* 0 on success, else the pp_status_t that refused it. */
int32_t pp_console_login(const char *text, uint32_t len, void (*out)(const char *));
void pp_console_logout(void (*out)(const char *));
void pp_console_whoami(void (*out)(const char *));
/* rnd: 32 bytes from a real random source. 0 on success. */
int32_t pp_console_create(const uint8_t rnd[32], void (*out)(const char *));
/* 0 when text is the seed pp_console_create showed; then logged in. */
int32_t pp_console_confirm(const char *text, uint32_t len, void (*out)(const char *));
bool pp_console_logged_in(void);

#endif /* PP_CONSOLE_H */

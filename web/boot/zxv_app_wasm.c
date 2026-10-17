/* zxv_app_wasm.c — the browser's app engine entry points. The pillars
 * are the kernel's own sources; these exports are the thin ABI the boot's
 * JavaScript calls. Every pointer is into the wasm linear memory and is
 * bounds-checked against the memory size.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <stddef.h>

#include "apps/zxapp.h"
#include "apps/zxapp_eve.h"
#include "apps/zxapp_kron.h"
#include "apps/zxapp_myra.h"
#include "apps/zxapp_sutra.h"
#include "apps/zxapp_trebago.h"
#include "tribute/tribute.h"

#define EXPORT(name) __attribute__((used, visibility("default"))) name

static zxapp_t GA;
static zxapp_host_t GH;
static char GERR[256];

static uint32_t mem_len(void)
{
    return (uint32_t) __builtin_wasm_memory_size(0) * 65536u;
}

static int bounds(uint32_t p, uint32_t n)
{
    return p <= mem_len() && n <= mem_len() && p + n <= mem_len();
}

static void host_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    zxapp_host_init(&GH);
    zxapp_eve_register(&GH);
    zxapp_kron_register(&GH);
    zxapp_myra_register(&GH);
    zxapp_sutra_register(&GH);
    zxapp_trebago_register(&GH);
}

/* Parse + load a manifest: [ptr, len] -> status. 0 = loaded; 1 = parse;
 * 2 = load; 3 = a check failed; 4 = no check lines; 5 = bounds. */
EXPORT(int) zxw_load(uint32_t ptr, uint32_t len)
{
    if (!bounds(ptr, len)) return 5;
    host_init();
    const char *text = (const char *) ptr;
    if (zxapp_parse(&GA, text, len) != ZXAPP_OK) {
        GERR[0] = 0;
        return 1;
    }
    if (zxapp_load(&GA, &GH) <= 0) return 2;
    return 0;
}

/* Run every check line (K2). 0 = all passed; 3 = a failure; 4 = none. */
EXPORT(int) zxw_check(void)
{
    int st = zxapp_check(&GA, &GH);
    if (st == ZXAPP_E_CHECK || st == ZXAPP_E_NAME || st == ZXAPP_E_CONTRACT) return 3;
    if (st == 0) return 4;
    return 0;
}

/* Call one entry: name is a NUL-terminated string in wasm memory. */
EXPORT(int) zxw_call(uint32_t name_ptr)
{
    if (!bounds(name_ptr, 1)) return -1;
    const char *name = (const char *) name_ptr;
    zxapp_num_t out[8];
    uint32_t n = 0;
    return zxapp_call(&GA, &GH, name, 0, 0, out, 8, &n);
}

/* The error text of the last load/check: copy into [out, cap), return the
 * bytes written. */
EXPORT(int) zxw_error(uint32_t out, uint32_t cap)
{
    if (!bounds(out, cap)) return -1;
    uint32_t n = 0;
    for (const char *p = GA.err; p && *p && n + 1 < cap; p++) ((char *) out)[n++] = *p;
    ((char *) out)[n] = 0;
    return (int) n;
}

/* The four public tribute destinations: rail 0..3 -> the 64-hex address
 * copied into [out, cap). No secret ever exists in this module. */
EXPORT(int) zxw_tribute_address(uint32_t rail, uint32_t out, uint32_t cap)
{
    if (rail >= TRIBUTE_N || !bounds(out, cap)) return -1;
    const char *a = TRIBUTE_ADDRESS[rail];
    uint32_t n = 0;
    for (const char *p = a; *p && n + 1 < cap; p++) ((char *) out)[n++] = *p;
    ((char *) out)[n] = 0;
    return (int) n;
}

/* The exact split into [out 4 x u64]. Returns 1. */
EXPORT(int) zxw_tribute_split(uint32_t yield_lo, uint32_t yield_hi, uint32_t out)
{
    if (!bounds(out, TRIBUTE_N * 8)) return 0;
    uint64_t y = (uint64_t) yield_lo | ((uint64_t) yield_hi << 32);
    uint64_t parts[TRIBUTE_N];
    protocol_tribute_split(y, parts);
    for (uint32_t i = 0; i < TRIBUTE_N; i++) {
        ((uint64_t *) out)[i] = parts[i];
    }
    return 1;
}

EXPORT(void) zxw_free(void *p)
{
    (void) p; /* the engine reuses its statics; nothing to free */
}

<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Proposed libzxv and kernel changes for microcontrollers

**None of these changes has been applied.** The embedded kits in this
folder build `sdk/libzxv/src` and `kernel/src` exactly as they are. They
reroute the OS calls with a force-included prelude and a `pthread.h` shim,
described in [README.md](README.md), "How libzxv is built here". That works,
and the conformance suite passes through it, but some of it is a workaround.
This file lists what would make libzxv a cleaner fit for firmware, in order
of value, so the maintainers can decide.

## Already done upstream (commit 4f33402), not proposed again

- **`PTHREAD ?= -lpthread`** in `sdk/libzxv/Makefile`. Pass `PTHREAD=` where
  there is no libpthread.
- **`arc4random_buf` on `__ANDROID__`** in `zxv_core.c`.

Neither affects these kits. They do not use libzxv's Makefile, and they
build with `-DZXV_RANDOM_GETENTROPY`, which `zxp_prelude.h` sends to
`zxp_getentropy()`, so the Android branch is never compiled.

---

## P1. One OS hook header instead of a prelude and a pthread shim

**Today:**

- `zxv_api.c`, `zxv_core.c` and `zxv_econ.c` call `malloc`, `calloc`,
  `realloc`, `free`, `strdup` and `pthread_mutex_*` directly.
- `zxv_api.c:60` uses `_Thread_local` for the last-error text.
- The randomness source is chosen by `#if` in `zxv_core.c`.

The kits rename all of these with
`-include port/compat/zxp_prelude.h` and `-I port/compat`, so that
`<pthread.h>` resolves to our shim. That is fragile in three ways:

- A new libc call added to libzxv is silently not routed. The kits'
  `-Werror=implicit-function-declaration` catches only some cases.
- The prelude must pre-include `stdlib.h`, `string.h`, `stdio.h` and, on
  Unix, `sys/types.h`, `unistd.h` and `sys/random.h`. This stops the libc
  prototypes being renamed.
- The kernel's tensor files cannot take the prelude at all, because they use
  `pthread_t` and `pthread_once`. The kits compile them without it, and only
  for the host conformance run.

**Proposal:** add `src/zxv_os.h`, included only by libzxv's own `.c` files:

```c
/* src/zxv_os.h — every OS service libzxv uses. Hosted defaults; a
 * freestanding build defines ZXV_OS_HOOKS and provides the functions. */
#ifdef ZXV_OS_HOOKS
#  include "zxv_os_hooks.h"   /* supplied by the integrator (-I) */
#else
#  include <pthread.h>
#  include <stdlib.h>
#  define zxv_os_malloc  malloc
#  define zxv_os_calloc  calloc
#  define zxv_os_realloc realloc
#  define zxv_os_free    free
typedef pthread_mutex_t zxv_os_mutex;
#  define zxv_os_mutex_init(m)    pthread_mutex_init((m), NULL)
#  define zxv_os_mutex_destroy(m) pthread_mutex_destroy(m)
#  define zxv_os_mutex_lock(m)    pthread_mutex_lock(m)
#  define zxv_os_mutex_unlock(m)  pthread_mutex_unlock(m)
#  define ZXV_THREAD_LOCAL        _Thread_local          /* see P5 */
#endif
int zxv_os_random(uint8_t *buf, size_t len);             /* see below */
```

The steps:

1. Replace each direct call with its `zxv_os_*` name (mechanical).
2. Replace `strdup` with a three-line local `zxvi_strdup`.
3. Add a `ZXV_RANDOM_HOOK` branch to the randomness `#if` in `zxv_core.c`
   that calls `zxv_os_random()` and returns `ZXV_E_IO` on any nonzero
   result, which keeps it fail closed.

The hosted build is byte-for-byte the same. `port/src/zxp_core.c` would then
become the `zxv_os_hooks.h` implementation, and `port/compat/` could be
deleted.

ABI: no change. Build: no change for hosted builds.

## P2. Q32.32 profile: posting amounts of 2^31 or more overflow (fix needed before any non-TEST_HOST build)

**Where:** `zxv_api.c:929`, `surplus_real_t amt = SR_FROM_INT(amount_minor);`,
and the shadow check at `zxv_api.c:1099`, `SR_FROM_INT(eq)`.

Without `-DTEST_HOST`, `kernel/src/surplus/surplus.h` makes
`surplus_real_t` a Q32.32 `int64_t`, and
`SR_FROM_INT(x) = (int64_t)(x) * 2^32`. libzxv accepts amounts up to
`ZXV_AMOUNT_MAX` = 2^53 - 1. Any amount or rail total of 2^31 or more
overflows, which is undefined behaviour.

Building the kits with `ZXE_PROFILE=q32` and `SANITIZE=1` reproduces it:
UBSan reports `signed integer overflow: 2147483648 * 4294967296 cannot be
represented in type 'long int'` from `zxv_api.c:929` for a posting of 2^31
minor units. The overflowed mirror then disagrees with the exact rails, and
`zxv_ledger_check` poisons the context.

That is why the kits default to `ZXE_PROFILE=exact`, which keeps
`-DTEST_HOST` as the hosted library does, and why `q32` is documented as
"measurement only".

**Proposed patch** (not applied). It narrows the range in Q32 builds only and
leaves TEST_HOST builds unchanged:

```diff
--- a/sdk/libzxv/src/zxv_api.c
+++ b/sdk/libzxv/src/zxv_api.c
@@
+/* The triple-ledger mirror's number type bounds the exact range. Under
+ * TEST_HOST it is a double (exact to 2^53-1, ZXV_AMOUNT_MAX). In the
+ * kernel's Q32.32 build it holds +-(2^31-1) whole units, so amounts AND
+ * rail totals are capped there; anything larger is ZXV_E_RANGE, never a
+ * wrapped multiply. */
+#ifdef TEST_HOST
+#  define ZXVI_AMOUNT_LIMIT ZXV_AMOUNT_MAX
+#else
+#  define ZXVI_AMOUNT_LIMIT ((int64_t) INT32_MAX)
+#endif
@@ int32_t zxv_ledger_post(
-    if (amount_minor <= 0 || amount_minor > ZXV_AMOUNT_MAX)
+    if (amount_minor <= 0 || amount_minor > ZXVI_AMOUNT_LIMIT)
         return zxvi_fail(ZXV_E_RANGE, "amount %lld must be in 1..%lld minor units",
-                    (long long) amount_minor, (long long) ZXV_AMOUNT_MAX);
+                    (long long) amount_minor, (long long) ZXVI_AMOUNT_LIMIT);
@@
-    if (f->credit > ZXV_AMOUNT_MAX - amount_minor || t->debit > ZXV_AMOUNT_MAX - amount_minor) {
-        rc = zxvi_fail(ZXV_E_RANGE, "rail total would exceed the exact range (2^53-1)");
+    if (f->credit > ZXVI_AMOUNT_LIMIT - amount_minor || t->debit > ZXVI_AMOUNT_LIMIT - amount_minor) {
+        rc = zxvi_fail(ZXV_E_RANGE, "rail total would exceed the exact range (%lld)",
+                  (long long) ZXVI_AMOUNT_LIMIT);
         goto out;
     }
```

The rail totals then cap every account's equity, every segment balance and
the shadow sum in `zxv_ledger_check` at 2^31 - 1. So `SR_FROM_INT(eq)` and
the `SR_ADD` chain stay in range.

Notes:

- A Q32 library would then refuse amounts that a TEST_HOST library accepts.
  That is a documented behaviour difference between builds, not an ABI
  change. It should be visible to callers, for example through a
  `zxv_build_info()` field or a new `ZXV_AMOUNT_MAX_THIS_BUILD` query in a
  minor ABI bump.
- A test should post `INT32_MAX` (accepted) and `INT32_MAX + 1` (expect
  `ZXV_E_RANGE`) under both builds.
- With P2 applied, the kits could default to `q32`. That removes all
  soft-float code: about 2.2 KB on Cortex-M4 and 7.6 KB on Cortex-M0+
  (README, "Floats").

## P3. The kernel ledger book is 24 MB; make its dimensions configurable

`kernel/src/finance/triple_ledger.h` hard-codes `ledger_entry_t entries[256]`
per account and `account_t accounts[512]` per book. One `triple_ledger_t` is:

| build | book size |
|---|---|
| x86-64 | 24,191,064 bytes |
| arm-none-eabi | 0x1712048 = 24,190,024 bytes |

`zxv_ctx_ccy_enable`/`zxv_ledger_open_account` allocate a book per currency
segment. So libzxv's ledger cannot run on any microcontroller, and is heavy
on small gateways. The kits replace it with `zxe_ledger` (fixed memory, same
rules and status codes, checked by a differential test).

**Proposal:**

- Wrap the two literals in `#ifndef TL_MAX_ACCOUNTS` / `#ifndef
  TL_MAX_ENTRIES` macros, defaulting to 512 and 256 so the kernel build is
  unchanged.
- Have libzxv derive its segment rollover thresholds from them. Today
  `zxv_api.c:406-407` repeats 512/256 as `ZXV_TL_ACCOUNTS` and
  `ZXV_TL_ENTRY_CAP`; define those as `TL_MAX_ACCOUNTS` and
  `TL_MAX_ENTRIES`.

A 16 × 16 book would be about 23 KB. The kernel owns the header, so this is a
kernel change plus a two-line libzxv change.

## P4. rmag and phase handle sizes; the phase recycle window

Measured on Cortex-M3/M4 (32-bit; `make qemu-test`):

| handle | heap bytes |
|---|---|
| `zxv_ctx` | 1,344 |
| `zxv_cap` | 56 |
| `zxv_rmag` | 34,960 |
| `zxv_phase` | 21,792 |
| `zxv_hexlog` (16 entries) | 1,952 |

The rmag and phase sizes come from fixed kernel tables:

- `RMAG_MAX_QUOTA_SOURCES` 32 and `RMAG_MAX_BUDGET_ENTRIES` 128 in `rmag.h`.
- `PC_MAX_TOKENS` 256 and others in `phase_coord.h`.

Also, `zxv_econ.c:489` recycles a phase event slot only after 4096 newer ids
exist. A clock that schedules steadily therefore grows its event table to
more than 4096 slots of 24 bytes; it doubles from 64, so it reaches 8192
slots, about 196 KB. Each schedule also scans the table linearly.

**Proposal:**

- Make the kernel table sizes overridable (`#ifndef`), as in P3.
- Make the recycle window a build-time constant: `ZXV_PHASE_RECYCLE`,
  default 4096 so ABI 1.x behaviour is unchanged.

A board could then set, for example, 8 quota sources, 16 budgets and a
window of 64, keeping a phase clock under 4 KB. The `qemu_cortex_m3` Zephyr
sample shows why this matters: on a 64 KB part the phase clock does not fit
next to an rmag, and the sample reports `ZXV_E_NOMEM` and continues without
it.

## P5. `ZXV_THREAD_LOCAL` instead of a bare `_Thread_local`

`zxv_api.c:60`: `static _Thread_local char g_err[ZXVI_ERR_MAX];`. Many RTOS
toolchains have no TLS:

- FreeRTOS has none.
- Zephyr needs `CONFIG_THREAD_LOCAL_STORAGE`, which the GNU Arm Embedded
  toolchain used here does not support.
- Bare metal has none.

The kits define `_Thread_local` as empty (`ZXP_NO_TLS`) and then enforce
single-owner use in the port layer. Calls from a second task are fatal,
because the error text and the libzxv-internal state are shared.

**Proposal:** `#ifndef ZXV_THREAD_LOCAL` / `#define ZXV_THREAD_LOCAL
_Thread_local`, which is part of P1. A one-line change; it removes the need
to redefine a keyword.

## P6. A way to check that a capability belongs to a given context (minor ABI bump)

`zxe_ledger` gates every call with `zxv_cap_check(cap, bit)`, as libzxv's own
entry points do. But `zxe_ledger` is bound to one `zxv_ctx`, and the public
ABI has no way to ask whether `cap` was minted by *that* context. A
capability from a second context with the same bits passes the gate.

libzxv's internal entry points use `zxvi_cap_gate_ctx` (`zxv_core.c`),
which does compare the context; external layers cannot reach it.

**Proposal:** add the following in ABI 1.4 (add-only, minor bump):

```c
int32_t zxv_cap_check_ctx(const zxv_cap *cap, const zxv_ctx *ctx, uint64_t bit);
```

It returns `ZXV_E_CAP` when the context differs. Until then, `zxe_ledger`
documents that the caller must not mix contexts. This is safe in the kits'
single-owner deployments, where there is one context, but it is a real gap
for gateways.

## P7 (kernel). Let a firmware build opt out of `retain` in `zxv_decl.h`

`kernel/include/zxv_decl.h` marks each module's bring-up record with
`__attribute__((used, retain))` when GCC is 11 or newer. A toolchain that
honours `SHF_GNU_RETAIN` keeps those records, their bring-up functions and
the static registries those functions reference, even under
`--gc-sections`.

On `riscv64-unknown-elf` that came to about 39 KB of RAM:

- lpres: 16 KB
- phase_coord: 2 × 21 KB

libzxv never walks those records. The kits build with `-Dretain=used`, which
works but redefines an attribute name globally.

**Proposal:** `#ifndef ZXV_DECL_NO_RETAIN` around the `retain` branch.
Kernel images keep today's behaviour.

## P8. Optional: `ZXV_NO_ERROR_TEXT`

The merged string literals in a linked image are 4.9 to 6.4 KB of flash
(README, "Sizes"), mostly libzxv's `zxvi_fail` format strings. The
`vsnprintf` they need adds several more KB of C library.

**Proposal:** a build option under which `zxvi_fail(code, fmt, ...)` records
only the code, and `zxv_last_error()` returns a fixed per-code name such as
"ZXV_E_RANGE". Status codes and behaviour are unchanged; only the
diagnostic text is lost. Estimated saving is 6 to 10 KB of flash. This has
not been measured, because applying it would mean editing libzxv.

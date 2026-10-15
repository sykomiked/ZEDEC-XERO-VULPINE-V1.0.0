<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# One test file per app

Each app's tests live in two small files of their own, so a whole app fits
in a short context. Do not edit `test_zxapp.c`, `test_apps.c` or the two
scripts: `test_zxapp.sh` and `test_apps.sh` find these files by name.

- `kernel/src/apps/tests/<id>_zxapp.inc`: the app's languages, run and
  witnessed. It starts with `APP_TEST(<id>, <logic lines that load>, <slot>)`.
  The harness has already read `apps/<id>.zxapp`, composed it from the
  shared blocks and the apps it uses (it waits until those have loaded),
  loaded it and checked its slot, layer, icon, budget and needs. `app` is
  the loaded app. The count of logic lines that load includes those a
  `use` brings in.
- `kernel/src/apps/tests/<id>_apps.inc`: the model's test. It includes
  `"<id>_model.h"` and starts with `APPS_TEST(<id>)`; the model must be
  `apps/<id>_model.c`. Test it against the real dispatcher through `LINK`,
  as `test_keepsake()` in `test_apps.c` does.

What you can use in `_zxapp.inc`: `CHECK(cond, fmt, ...)`, `WAVE_ROWS` (64),
`wrand(lo, hi)` (the fixed generator), `wave_same(app, entry, args, nargs,
want, rows, truth)` (every row equals `want`; TRUE when two languages or
more agree), `wave_lp(app, entry, args, nargs)` (the four-valued result:
`ZXAPP_BOTH` when the languages disagree) and `T_(bool)`.

The gate (`tools/apps/app_gate.py`) refuses a `_zxapp.inc` that does not
name every entry the manifest provides (as `"<entry>"`), or that never
makes two languages disagree (`ZXAPP_BOTH`) when an entry runs in two;
each file must check something, and a file in `tests/` that matches no app
is refused.

## A whole app that passes (a sample, verified)

These seven files are a complete app on which `app_gate.py --app kitling --verdict` says **T**. They were checked on the base that carries this page, and the BOTH check was proved to fail when its input fits Sutra's picture. This is a sample of the shape, not the Kitling task: your app's entries, languages and model come from its own task file. Copy the shape: every file, every include, every signature.

`kernel/src/apps/kitling.zxapp`:

```
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
zxapp 1
id     kitling
name   Kitling
version 1.0.0
slot   62
layer  L1
icon   icons/l1/062-kitling.svg
budget 89
needs  fs.read
model  apps/kitling_model.c

provides doubled num x
logic    kron    doubled apps/blocks/kitling/doubled.kron
logic    sutra   doubled apps/blocks/kitling/doubled.sutra

# Its own proof (zxapp.h K1): zxapp_check runs these on the device
check doubled x=0,1,2,999999999 = 0,2,4,1999999998
check doubled x=1,1000000000 = both
```

The two `check` lines are the app's proof that travels with it: the gate refuses an app with an entry no check line names, and an entry in two languages needs a `= both` line (inputs that make them disagree) or a `# agree: <entry> <why>` line saying why no input can. A check passes when every row matches and the join is TRUE (NEITHER when one language ran); `= both` passes only on BOTH.

`kernel/src/apps/blocks/kitling/doubled.kron` (KRON works elementwise; a 1-element argument does not broadcast, so test with two rows or more):

```
2*x
```

`kernel/src/apps/blocks/kitling/doubled.sutra` (Sutra runs once per row; a value past its picture is refused):

```
# Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
# SPDX-License-Identifier: Apache-2.0
#
# Twice x, in Sutra: x fits 9(9), so a larger x is a size error, refused.
record doubler
  x: 9(9)
  y: 9(10)
procedure
  compute y = x * 2
  answer y
```

`kernel/src/apps/kitling_model.h` and `kitling_model.c` (integers only; every parameter used; no POSIX calls):

```c
/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#ifndef KITLING_MODEL_H
#define KITLING_MODEL_H
#include <stdint.h>
int64_t kitling_twice(int64_t x);
int64_t kitling_sum_twice(const int64_t *x, uint32_t n);
#endif
```

```c
/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "kitling_model.h"

int64_t kitling_twice(int64_t x)
{
    if (x > INT64_MAX / 2) return -1;
    if (x < INT64_MIN / 2) return -1;
    int64_t y = x;
    y += x;
    return y;
}

int64_t kitling_sum_twice(const int64_t *x, uint32_t n)
{
    int64_t s = 0;
    if (!x) return -1;
    for (uint32_t i = 0; i < n; i++) {
        int64_t t = kitling_twice(x[i]);
        if (t < 0 && x[i] >= 0) return -1;
        if (s > INT64_MAX - t) return -1;
        s += t;
    }
    if (s < 0) return -1;
    return s;
}
```

`kernel/src/apps/tests/kitling_zxapp.inc` (no includes: the harness has them; `app` is the loaded app):

```c
APP_TEST(kitling, 2, 62)
{
    static int64_t x[WAVE_ROWS], want[WAVE_ROWS];
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        x[i] = wrand(0, 999999999);
        want[i] = 2 * x[i];
    }
    zxapp_arg_t xa = {"x", x, WAVE_ROWS};
    CHECK(wave_same(app, "doubled", &xa, 1, want, WAVE_ROWS, false), "Kitling: KRON = Sutra");
    static const int64_t big[2] = {1, 1000000000}; /* past Sutra's 9(9) */
    zxapp_arg_t ba = {"x", big, 2};
    CHECK(wave_lp(app, "doubled", &ba, 1) == ZXAPP_BOTH, "Kitling: Sutra refuses, KRON answers: BOTH");
}
```

`kernel/src/apps/tests/kitling_apps.inc` (includes only the model's header):

```c
#include "kitling_model.h"
APPS_TEST(kitling)
{
    int64_t x[3] = {1, 2, 3};
    CHECK(kitling_twice(2) == 4, "kitling: twice 2");
    CHECK(kitling_sum_twice(x, 3) == 12, "kitling: sum of twice");
}
```

The entry's slot (`@num.kitling_doubled`) is given by `build_index.py --assign kitling auto auto --out-hdr`; the wave loop does that for you.

## How the C is compiled

`cc -std=c11 -Wall -Wextra -Werror`, so a warning fails the build. Every parameter is used (or `(void) p;`), every function is declared in a header before it is used, no `strdup` or other POSIX calls, no `printf` in a model (print `int64_t` in a test with `%lld` and a `(long long)` cast), no `float` or `double`. A `_zxapp.inc` test cannot include `zxm.h`; only the model's own test (`_apps.inc`) reaches the daemon, through `LINK`. Use only the functions and struct members its headers declare: `zxm.h` is the whole daemon API (there is no `zxm_init`, `zxm_destroy` or `zxm_get_i32`), and a struct has only the members its header gives it.

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

## A worked pair (shape only: these two apps are not real)

`kitling.zxapp` provides `doubled num x` in KRON (`2*x`) and Sutra
(`compute y = x * 2` into `y: 9(9)`). `gatehouse.zxapp` says `use kitling 1`,
re-exports `provides kitling.doubled num x`, and adds its own `plus_one`.

`tests/kitling_zxapp.inc`:

```c
APP_TEST(kitling, 2, 62)
{
    static int64_t x[WAVE_ROWS], want[WAVE_ROWS];
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        x[i] = wrand(0, 499999999);
        want[i] = x[i] * 2;
    }
    zxapp_arg_t xa = {"x", x, WAVE_ROWS};
    CHECK(wave_same(app, "doubled", &xa, 1, want, WAVE_ROWS, false), "Kitling: KRON = Sutra = C");
    int64_t big[1] = {999999999};
    zxapp_arg_t ba = {"x", big, 1};
    CHECK(wave_lp(app, "doubled", &ba, 1) == ZXAPP_BOTH, "Kitling: Sutra refuses, KRON answers: BOTH");
}
```

`tests/gatehouse_zxapp.inc` (Gatehouse loads its one line plus the two it uses):

```c
APP_TEST(gatehouse, 3, 8)
{
    int64_t x[3] = {0, 1, 41}, d[3] = {0, 2, 82}, p[3] = {1, 2, 42};
    zxapp_arg_t xa = {"x", x, 3};
    CHECK(wave_same(app, "kitling.doubled", &xa, 1, d, 3, false), "Gatehouse: Kitling's entry, used");
    CHECK(wave_same(app, "plus_one", &xa, 1, p, 3, false), "Gatehouse: its own entry");
}
```

`tests/kitling_apps.inc`:

```c
#include "kitling_model.h"
APPS_TEST(kitling)
{
    CHECK(kitling_twice(2) == 4, "kitling: twice 2");
    CHECK(kitling_twice(0) == 0, "kitling: twice 0");
}
```

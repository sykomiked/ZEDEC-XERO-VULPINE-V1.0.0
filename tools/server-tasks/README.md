<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Server tasks for the built-in apps

One task file per app not yet built (393 of the 407), in waves of 12:
waves 1-11 the L1 apps, 12-22 the L2 apps built from them, 23-34 the L3
apps built from those. Waves 1-11 follow the matrix foundation order in
`MATRIX_ORDER.tsv` (the layers most others lean on first; the measure is
`LAYER_ORDER.tsv`). `APP_TESTS.md` says how an app's tests are written. Read `APP_WAVE_SPEC.md` first; `WAVES_SUMMARY.txt`
lists each app's languages, the waves it depends on and the daemon
methods it is missing. `APP_LANGUAGES_407.csv` is the per-app language
assignment the tasks start from. Work each app on its own branch
`server/waveN-<slot>-<id>` cut from `claude/project-thread-cqt3z5-wave0`;
never push to main, never open a pull request.

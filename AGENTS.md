<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Working on ZXV — machine notes

What `CLAUDE.md` says holds. This page adds the things that differ on this
machine, so the checks run the first time instead of the third.

## Run the checks

- **On macOS, use the Docker path**, which is the default on Darwin:
  `scripts/verify-local.sh --docker` (fast tier), `--boot`, `--san`,
  `--all`. Docker is installed and running. The raw `make -C kernel ...`
  targets hit two macOS-only failures that Linux CI never sees:
  - Apple clang 17 has no `riscv32` target (`error: unable to create target
    ... "riscv32-unknown-unknown-elf"`), so `verify-all`'s riscv32
    freestanding check fails. `bin/clang` selects Homebrew LLVM
    (`/opt/homebrew/opt/llvm/bin/clang`) for non-Apple targets; prepend
    `bin/` to `PATH` to use it, or just use `verify-local.sh --docker`.
  - The macOS SDK marks `sprintf` deprecated, which `-Werror` turns into an
    error in 17 test files. `bin/cc` and `bin/gcc` add
    `-Wno-deprecated-declarations`; the Docker image does not need it.
- The fast, always-safe host checks (run from the repo root):
  `python3 build_system/relicense_apache.py --check`,
  `python3 build_system/check_workflow_tiers.py`,
  `python3 build_system/sdk_kernel_deps.py --check`,
  `python3 tests/harness/mutator/mutate.py --config tests/harness/mutator/targets.json --check-config`,
  `build_system/verify_layers.sh`.
- One Lingua module by hand (the Makefile's include path):
  `export CPATH=$PWD/kernel/include:$PWD/kernel/src/modbind:$PWD/kernel/src/e8:$PWD/kernel/src/event_space:$PWD/kernel/src/surplus`.
- `clang-format` lives at `/opt/homebrew/opt/llvm/bin/clang-format`
  (Homebrew, 23.x); the repo pins 18.1.8 in CI but 23 reads the same
  `.clang-format`. Format new files with
  `clang-format -i --style=file:.clang-format <file>` before committing.

## The language layer

- `kernel/src/lingua/` compiles legacy languages to one IR (`zl.h`). The
  front-end contract is `kernel/src/lingua/LINGUA_SPEC.md`; every
  placeholder `zl_<lang>.c` points at it. Languages and their twins are in
  `zl_langs.def`; tiers in `catalogue.tsv` (checked by
  `check_catalogue.sh`); the proof requirements in `docs/LANGUAGE_CATALOGUE.md`.
- Adding a language: `zl_<lang>.c` + `test_zl_<lang>.inc` (include it from
  `test_zl_langs.inc`) + `golden/collatz.<ext>` and a `features.<ext>` with
  `.out` files produced by the reference + `gen_<lang>.c` +
  `ref_<lang>.sh` + a row in `tests/harness/mutator/targets.json`, then
  move the `catalogue.tsv` line to `run`. `make -C kernel test-lingua` and
  `test-lingua-ref` are the gates (run them with the deprecation flag or
  via Docker on this Mac).
- `lua` 5.4+ is installed (`/opt/homebrew/bin/lua`); it is the reference
  for `zl_lua.c` (Moonwyrm).

## Deployment

- Contabo and Filebase/R2 credentials live in `~/.config/zxv-deploy.env`
  (mode 600). Never hard-code them. `tools/agent_loop/contabo_fleet.py`
  (read-only), `fleet_reimage.py <imageId> <instanceId...>`, and
  `armada_mesh_daemon.py` are the fleet tools. The Contabo API needs a UUID4
  `x-request-id`; reinstall is `PUT /compute/instances/{id}` with
  `{"imageId": ...}`.
# ZXV — Single Source of Truth

This resolves the audit finding "no single source of truth." Authoritative as of 2026-08-04.

## The canonical kernel is `05_KERNEL/kernel`
It is the only tree that:
- builds (`build_system/Makefile.arm64`) and boots QEMU **100/100 BOOT_OK**,
- passes `make -C 05_KERNEL/kernel verify-all` (**566 host assertions, 0 failures**),
- carries every current subsystem and the CELL-001 recovery fix.

Build: `make -f build_system/Makefile.arm64` from `05_KERNEL/`.
Gate:  `make verify-all` from `05_KERNEL/kernel/` (runs the 10 legacy suites **plus**
CELL-001 12/12, cards/ZCA/sigil, Magitech Refinery, crypto KATs, and the self-audit
regressions — this is the real test gate; plain `make test` runs only the legacy 10).

## Archived / non-authoritative (kept, not built — preserved by owner request)
- `zxv_os/` — frozen pre-2026-08-04 kernel snapshot. Divergent and pre-fix. Verified to
  contain **nothing** canonical lacks (see `zxv_os/SOURCE_OF_TRUTH.md`). Referenced by no
  build script.
- `zxv_complete/`, `zxv_sdk/` — placeholder/nesting directories, empty of source. Dead
  weight, retained rather than deleted.

## Version control
`05_KERNEL` is a git repository on branch `main`. The `.gitignore` excludes build artifacts
(`.o`, `.dSYM`, built binaries, `.DS_Store`) **and secrets** (`*.pem`, `*.key`) — verified: no
private key is tracked. `build_system/keys/root_priv.pem` exists on disk as a **development-only**
signing key for the sample app; it is gitignored, must never be treated as a production root, and
any real release MUST regenerate the root in an offline ceremony and revoke this dev key.

## Packaging note (for release engineering)
A distributable partner package MUST be generated from a clean signed commit — NOT a raw zip of
this working tree. A raw zip captures gitignored files (the dev key), build artifacts, the
archived `zxv_os` tree, and any momentary doc inconsistency. Package from `git archive` of a
tagged commit plus a generated evidence capsule, not from the directory.

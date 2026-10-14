<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Adversarial self-audit, run 1 (2026-10-12)

First run of the protocol in [ADVERSARIAL_AUDIT.md](ADVERSARIAL_AUDIT.md), on `main` at
`1de02a5` (after PR #70). Everything ran locally (Ubuntu 24.04, gcc 13.3, ASan + UBSan);
nothing was published.

## Summary

* **Baseline:** `make -C kernel verify-all` was green before the audit (4,125 passes).
* **Static stage:** no secrets, no checked-in executables, no workflow injection. It found
  9 test files that no build ran, 3 tests that check their own copy of the code, 14 modules
  with no test at all, 87 headers with byte-input entry points no fuzzer reached, 8
  third-party actions on moving tags, and one fixed-point constant that rounds to 0.
* **New harnesses:** 18 fuzz and property harnesses covering 30 modules (listed below).
  Together they ran about 1.5 million inputs and well over 10 million operations.
* **Bugs fixed:** 21 bugs in the module code, 3 of them HIGH. Each fix has a regression
  input under `fuzz/corpus/` or a new test. On top of that: 6 dead test files revived
  (956 checks) and the hardware-update parser's mutation score raised to 90%.
* **Handed on:** 6 design-level findings (section 4). Each needs an owner decision or a
  change across modules.

## 1. Bugs fixed in this change

| # | Level | Where | Defect | Shown by |
|---|---|---|---|---|
| 1 | HIGH | `vswap/vswap.c` `vswap_rate` | With no outside V-Bills (B = 0) but a funded reserve, swap-in was priced at par. One unit bought at par then swapped out for the whole reserve (1 Vino in, 1,000,001 out). It now fails closed with `VSWAP_ERR_STATE`. | `fuzz_econ_vswap`, `regress-par-drain-b0-r-positive` |
| 2 | HIGH | `vderiv/vderiv.c` `parties()` | Signing keys were never tied to the accounts they commit. Anyone with two keys could name a victim's account as buyer or writer and sign both sides, taking the victim's Vino as premium. Each key must now be the ledger owner of its account (V6). | `fuzz_econ_vderiv`, `regress-vderiv-unbound-key` |
| 3 | HIGH | `treaty/zxt_exec.c`, `zxt_cobol.c` | A settled balance could pass PIC S9(18). The runner returned `ZXT_OK`, the result record was refused, and `ZXV_ENGINE` returned 0 without writing it, so the COBOL caller read a stale record. Such balances are now `ZXT_E_SIZE`, and a status always comes with its record. | `fuzz_treaty`, `crash-exec-balance-over-pic18` |
| 4 | MED | `hwemu/hwr.c` R3 | The version check looked only at the active slot. After a drop to S0, an older signed image with svn ≥ floor could be staged and committed again (v5, then S0, then v2 accepted). A highest-committed-version floor now survives S0. | `fuzz_hwemu`, `regress-r3-replay-after-s0` |
| 5 | MED | `vswap/vswap.c` `div_check` | Locking one V-Bill with the dividend pool as payee made every later dividend refuse. That position now gets weight 0. | `fuzz_econ_vswap`, `regress-lock-payee-pool-blocks-dividend*` |
| 6 | MED | `vauction/vauction.c` `vauc_settle` | Settle marked the epoch settled before its first posting. If that posting was refused, cancel was refused from then on, and if the floor then broke, bidders' fiat stayed in escrow for good. It is now marked only once something has been issued (A7). | `fuzz_econ_vauction`, `regress-cancel-refused-after-failed-settle` |
| 7 | MED | `grants/grants.c` `grant_redeem` | Redemption paid any Vino account, not the holder's. The payee must now have the holder's owner and be neither the pool nor the reserve. | `fuzz_econ_grants`, `regress-grants-any-payee` |
| 8 | MED | `grants/grants.c` `grant_pool_credit` | The pool could credit itself. That inflated epoch inflow and `pool_seen`, which then refused real raffle deposits. Credits from the pool or reserve are now refused. | `fuzz_econ_grants`, `regress-grants-self-credit` |
| 9 | MED | `call/call_cc.c` `close_group` | `da - ds` was an int32 subtraction on a peer-supplied send time and could overflow (UB), flipping the delay sign the congestion controller steers by. It is now done in int64 and clamped. | `fuzz_call`, `crash-cc-close-group-overflow` |
| 10 | MED | `devmesh/devmesh.c` `dm_b32_encode` | `len * 8` wrapped for len ≥ 2^29, so the size check passed and the encoder wrote past `out` (heap overflow; local callers only). | `fuzz_devmesh` |
| 11 | MED | `quantum/quantum_device.h` | In the kernel's Q32.32 build, `SR_FROM_FLOAT(1.054571817e-34)` is 0, so every Casimir force and zero-point energy came out 0. The fixed-point build now uses nm, fs and eV (ħ = 0.658 eV·fs, c = 299.79 nm/fs). | `test_quantum_fixed` (6 of 12 checks fail on the old header) |
| 12 | MED | `apps/apps.c` `app_launcher_open` | The new app was bound to the launcher's own window count, not to the index the desktop returned, and took another window's title. With any window already open, an app was attached to someone else's window. | `test_apps` (revived), agent repro |
| 13 | LOW/MED | `cardnet/cn_emv.c` `cn_emv_decode_auth` | The decoder accepted duplicate, extra, nested or reordered tags (first match wins), any 9C (a refund decoded as a purchase), any AIP/TVR, month 13 and ATC 0. It now requires the canonical re-encoding. | `fuzz_cardnet`, `crash-emv-*` |
| 14 | LOW | `cardnet/cn_iso8583.c` | An all-zero secondary bitmap was accepted, giving the same message two encodings. | `crash-iso-empty-secondary-bitmap` |
| 15 | LOW | `devmesh/devmesh.c` `dm_b32_decode` | The decoder accepted nonzero padding bits ("MX" and "MU" both gave 0x65) and impossible lengths. | `crash-b32-*` |
| 16 | LOW | `web4/web4_json.c` `w4_json_str_eq` | An escaped NUL cut the comparison short, so `"zxvd.shutdown\u0000 x"` ran `zxvd.shutdown` and `"amount_minor\u0000z"` was read as `amount_minor`. | `fuzz_zxvd_rpc`, `regress-nul-method-alias` |
| 17 | LOW | `hwemu/hwr.c` R6 | The blocklist silently stopped blocking after 16 quarantined payloads. A full blocklist now halts the region (fail closed). | `regress-r6-blocklist-overflow` |
| 18 | LOW | `aipi/aipi.c` execute | `deadline = now + ttl` could wrap into the past, so the job expired on the next tick and honest executors were refused. It now saturates. | `fuzz_aipi`, `crash-ttl-wraps-deadline` |
| 19 | LOW | `tensor/zt_tok.c` load | bos/eos ids were narrowed to int32 before the range check: negative ids survived, and 2^32+5 became token 5. They are now checked as int64. | `fuzz_tok`, `crash-eos-id-negative` |
| 20 | LOW | `zenny/zenny.c` `payout` | Decompression checked exactness only at the batch prices, so the module could leave a remainder its own `zenny_check` called INEXACT (Z5). | `regress-zenny-inexact-remainder` |
| 21 | LOW | `zenny/zenny.c` version log | A DECOMPRESS version kept the previous top-up's recipient. | `regress-zenny-stale-recipient` |

Also fixed: `vbe/vbe.h` used `bool` without `<stdbool.h>`, and `tools/zedec_build.py` listed a
deleted test.

## 2. Gate integrity (attacker A6: ourselves)

* **Tests that never ran.** Built by nothing before this change, now run in `make -C
  kernel test-orphans` inside verify-all, testing the real sources:
  * `tests/test_holo.c`: 154 checks
  * `tests/test_apps.c` and `test_apps_stubs.c`: 78
  * `tests/test_new_modules.c`: 656, updated to the current pungent/plnp/smap/decent/recon
    APIs
  * `kernel/src/emu/test_nes.c`: 34, on an embedded hand-assembled NROM image; it
    previously checked nothing without a ROM argument
  * `kernel/src/apps/test_notes.c`: 34

  `tests/test_net_vino.c` was a duplicate of test_drivers and is removed; its 10 unique
  enum checks moved to `tests/test_drivers.c`.
* **Still not run** (accepted in `build_system/adversarial_audit.allow`):
  * `kernel/src/net/test_smart_adapter.c` includes a header that does not exist (net owner).
  * `tests/host/test_ipc.c` tests its own copy of `ipc_send`/`ipc_recv`. The real code in
    `kernel/arch/arm64/el0_userspace.c` has arm64 inline asm, so it needs an aarch64 build
    under qemu-user.
* **Shadow tests** (WARN): `test_ipc.c`, `kernel/src/curzi/test_curzi8889a.c` (its own
  `pq_mldsa65_sign`) and `kernel/src/situation/test_situation_model.c` (its own
  `edp_compute_risk`). The last two may be legitimate stubs and need a look by their owners.
* **Mutation, hardware-update parser** (`hwr.c` lines 1–105, `hwr_parse` and helpers):
  * The first run showed that deleting the magic, format, kind or length guards, changing
    the byte order, or skipping the first or last byte of a digest compare all went
    unnoticed, because the signature check stood behind them.
  * `test_hwr` gained direct `hwr_parse` checks and edge-byte digest checks. The score is
    now 150/166 (90.4%), and the remaining survivors are equivalent.
  * This is the first target in `build_system/adversarial_targets.json`.

## 3. New harnesses

All of them are in `fuzz/Makefile`, so `make -C fuzz replay` (run by verify-all) replays
their corpora, and the econ ones also run in `make -C fuzz prop`.

| Harness | Modules | Notes |
|---|---|---|
| `fuzz_treaty` | treaty/zxt, zxt_exec, zxt_cobol | 4 modes; netting recomputed in 128 bits |
| `fuzz_aipi` | aipi | about 2M operations; tampering, replay and double claims all refused |
| `fuzz_tok` | tensor/zt_tok | hostile vocabularies built as GGUF |
| `fuzz_call` | call/* (9 parsers, FEC, mesh, CC) | byte-exact re-encode for every parser |
| `fuzz_cardnet` | cardnet ISO 8583, EMV, mobile money | amounts exact, canonical encodings only |
| `fuzz_devmesh` | devmesh codecs, wallet check | on a real joined PQ mesh |
| `fuzz_zxvd_rpc` | zxvd JSON-RPC + libzxv books | M3 amount grammar against a reference parser |
| `fuzz_hwemu` | hwp, hwr, hwa, hwm | real dual-signed image path plus a fast stand-in signer |
| `fuzz_paradox` | paradox, paradox_graph | RepairProof tampering always caught (no bugs) |
| `fuzz_econ_svault` | svault | 192-bit solvency reference (no bugs) |
| `fuzz_econ_vswap` | vswap | ratio never falls; exact dividends |
| `fuzz_econ_vauction` | vauction | backed-only floor, descending ladder, escrow |
| `fuzz_econ_vbond` | vbond | no bugs |
| `fuzz_econ_vderiv` | vderiv | no interest or accrual anywhere |
| `fuzz_econ_zenny` | zenny | exact rational claims; quote checked by brute force |
| `fuzz_econ_grants` | grants | eligibility and largest-remainder model |
| `fuzz_econ_estate` | estate, faraid | heir shares sum to exactly 1 (no bugs) |

The static stage still lists 78 headers with byte-input entry points no harness reaches
(bluetooth, browser, font, holographic, evolve, the emulators' ROM loaders and others). They
are next in line, most exposed first.

## 4. Handed on: design findings that need a decision

1. **No caller authentication in the money modules.** `vswap_swap_in`/`swap_out`/`lock`,
   `vauc_bid` and `svault_buy` take two accounts per party and never check that one owner
   holds both, nor who is calling. So a victim's Vino can be swapped or bid with the
   proceeds sent elsewhere. There are no production callers yet. An owner-equality check
   is drafted, but it changes `test_svault`'s fixtures, and real caller authentication
   belongs in the layer above (zxvd).
2. **Predictable e2e ids** (`"VSWAP-<seq>"`, `"VAUC-<seq>"`). Any ledger poster can take a
   future id first and force a partial dividend, and the next dividend then pays batch-1
   holders twice (10 holders sharing 1000 ended at 130 × 7 and 30 × 3). Deriving the id
   from the seed hash fixes it, but some tests rely on the predictable id.
3. **Mesh id squatting** (`call/call_mesh.c`). The first key seen for a member id is
   pinned, and nothing ties the id to the key. A signed LEAVE for an id that has not joined
   yet locks the real member out, and the same version can be signed twice with different
   content.
4. **cardnet:** `cn8583_to_auth_req` ignores field 3, so a 0200 refund maps to a purchase.
   **devmesh:** `dm_money_t` amounts have no upper cap, and a MONEY_ANSWER is accepted from
   any paired peer, so any paired device can cancel a request.
5. **paradox:** `pdx_review` trusts `d->flags`/`d->kind`, which nothing binds to the DRAFT
   record, so clearing `PDX_DRAFT_MONEY` after `pdx_draft` lets a money draft pass by
   consensus. **zenny:** an old custodian attestation for the same quantity is accepted
   again after a rebalance (X to Y and back to X); should a fresh attestation bind to the
   version hash?
6. **svault:** anyone who can post to the ledger can deposit into vault cash, which breaks
   S6 permanently. `svault_sweep` moves reserve funds without checking the solvency floor.

## 5. Tooling findings (CI owner)

* `kernel/Makefile` still has about 450 fixed `/tmp/<name>` paths, besides the
  `$(ZXV_TMP)` ones. Two runs on one machine collide, and on a shared machine another user
  can pre-plant or swap a binary between build and run. `scripts/adversarial-audit.sh`
  sets `ZXV_TMP` to a private directory, but only the recipes that honour it are covered.
* Eight third-party actions run on moving tags, not commit SHAs:
  * advanced-security/filter-sarif
  * anchore/sbom-action
  * android-actions/setup-android
  * dart-lang/setup-dart
  * dtolnay/rust-toolchain
  * gradle/actions/setup-gradle
  * ruby/setup-ruby
  * shivammathur/setup-php

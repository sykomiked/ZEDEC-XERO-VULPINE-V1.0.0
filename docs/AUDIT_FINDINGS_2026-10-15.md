<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Audit findings, 2026-10-15

Findings from the behaviour-coverage pass ([BEHAVIOUR_COVERAGE.md](BEHAVIOUR_COVERAGE.md)),
which read the boot checks, the banner checks and the module self-tests of the engineered
core as an outsider would, and then wrote property tests against reference models. Base:
`main` at `4b9e2ae4`. Everything was run locally (Ubuntu 24.04, gcc 13.3, ASan + UBSan,
QEMU 8.2 for the arm64 and x86_64 boots). Line numbers are at that base unless a fix moved
them.

Each finding says what was checked, what was found, and whether it is fixed here, handed
on as an owner decision (state B: the code and its own comment or spec disagree), or noted
only.

## 1. Fixed in this change

| # | Where | Finding | Evidence | Fix |
|---|---|---|---|---|
| F1 | `build_system/ci_boot_check.sh`, `.github/workflows/ci.yml` | On arm64 and x86_64 a boot self-check that fails prints `[FAIL]` and the boot carries on. The CI boot check only looked for `[BOOT_OK]`, `[AI_OK]` and fault markers, so a broken X25519, AEAD, E8 or Z[phi] check still passed CI. Every behavioural boot line on those two architectures was, from CI's point of view, presence only. | A healthy arm64 and x86_64 boot print no `[FAIL]` or `[??]` line (boot logs checked); with one RFC 7748 byte changed, the arm64 boot printed `[FAIL] X25519 KAT mismatch` and still reached `[BOOT_OK]`. | `ZXV_BOOT_DENY_FAIL=1` makes `[FAIL]` and `[??]` fault markers; set on the arm64 and x86_64 boot jobs. With it, the broken-byte boot fails the check (exit 1). `test_ci_boot_check.sh` has four new cases. riscv and arm32 already halt with `[BOOT_FAIL]`. |
| F2 | `kernel/arch/arm64/kernel_main_arm64.c` (X25519) | The "RFC 7748 KAT" only checked that both sides agree, so a ladder that is wrong the same way on both sides passes. | `:2335-2346` compared `ss_a` with `ss_b` only. | Compares Alice's public key and the shared secret with RFC 7748 section 6.1 (values cross-checked with Python `cryptography`). |
| F3 | same file (ML-KEM-768) | A disagreeing round trip printed `[WARNING]`, which nothing treats as a failure. | `:2581`. | Prints `[FAIL]`, which F1 now enforces. |
| F4 | same file (event-space audit) | `[AUDIT] Initial system audit: PASS` was a literal; the audit's result was computed and discarded (`(void)init_audit`). | `:1226-1229`. | Prints PASS, WARN or `[FAIL]` from the returned verdict. Boot still prints PASS. |
| F5 | `kernel/src/bootfeat/boot_features.c` (IPFS) | The content-address check compared 2 of the 32 bytes of SHA-256("abc"). | `:80`. | Compares all 32; `test_boot_features.c` corrupts the last byte and expects `[??]` (fails without the fix). |
| F6 | `kernel/src/oseq/oseq.c` `oseq_register_node` | When the node table was full, a known node coming back with a new incarnation was refused: the capacity check ran before the duplicate-name lookup. | `test_oseq_props.c` "a known node still re-registers when full". | Lookup first, capacity second. |
| F7 | `kernel/src/oseq/oseq.c` `oseq_is_ancestor`, `oseq_detect_cycle` | The traversal stack held 256 entries while up to 512 events can be pushed. On overflow both functions returned false, so on a wide DAG an ancestor read as "not an ancestor" and a cycle read as "no cycle": an event in a cycle committed. Fail-open. | `test_oseq_props.c` ladder of 500 events with 4 parents each: 3 failures before the fix ("a wide search still finds the root", "a loop closed at the far end is found", "so it cannot commit"). | Stack sized `OSEQ_MAX_EVENTS` (each index is pushed at most once, so it cannot overflow). 2 KB of stack; the arm64 boot stack is 64 KB. |

## 2. Owner decisions (state B)

| # | Where | Finding | Options |
|---|---|---|---|
| B1 | `kernel_main_arm64.c:2454`, `kernel_main_x86_64.c:341` | The declaration gate (`zxv_decl_boot_gate`) is advisory on arm64 and x86_64: its fault count is discarded, and the comment says halting "would be a policy change". riscv and arm32 halt on it. The outside review assumed the gate fails closed everywhere. | Keep advisory (F1 now fails CI on any `[FAIL]` line the gate prints, so CI is closed even if the boot is not), or halt like riscv/arm32. |
| B2 | `kernel/src/lpres/lpres.c` `lpres_safety_gate_clear` | Returns true on an empty registry or one where every entry is revoked. The module's own bring-up comment says "nothing attested must not read as everything permitted". | Return false when no live entry exists, or document that the gate means "no live uncertainty". Callers must be checked before changing it. The property test deliberately does not assert either answer. |
| B3 | `kernel/src/modbind/modbind.c` `modbind_compose` | A composite drops every ingest port of its parts, silently truncates emits past `MB_MAX_PORTS` (requires and provides refuse instead), never sets `xform.unpack`, and nesting is not associative for emits. `test_modbind.c` asserts the provides/requires algebra (which holds) but not the ports. | Define port composition (ingests surface like requires; refuse instead of truncating), or document composites as capability-only. |
| B4 | `kernel/src/zcapital/zcapital.c` `zmarket_match` | Head-of-line blocking: only the first live offer is matched; if its only seeks are from the same party, no other offer can match. | FIFO by design, or scan for the first matchable pair. The property test pins the current rule. |
| B5 | `kernel/src/swarm/swarm_market.c` `swarm_market_credit_frugality` | Unspent tokens (`allotted - used`) are credited as Natural capital each cycle. This is not settlement minting (tokens are not created), but it is a conversion the economy docs should state. Pay-as-bid also applies regardless of tokens won. | Confirm against SWARM_ECONOMY.md. |

## 3. Noted (no change)

Boot and self-test inventory (full table in BEHAVIOUR_COVERAGE.md):

- 49 of 117 arm64 boot lines and 16 of 55 x86_64 lines (after this change) are announcements after an init call
  whose result is discarded (`[INITIALIZED]`, `[REGISTERED]`, `[DRIVER ONLINE]`). Examples:
  `[REGISTERED] 15 domains + cross-domain contagion` runs no code at all (`:956`);
  `[COMPOSED] bomb squad watching REAL margins` is unconditional (`:323`); GDT+TSS on x86_64
  is unconditional. They are not checks and should not read like checks; renaming the tag or
  gating each on its init's return value is a separate, mechanical change.
- `verify_banners.sh` (89 checks over four architectures) counts symbols with `nm`. That is a
  linkage check by design: it proves the code was not dropped by `--gc-sections`. Its
  behaviour counterpart is the boot line for the same subsystem, which F1 now enforces on
  arm64 and x86_64.
- Dead self-tests: `pq_self_test` (`pqsec/pq_security.c:189`) and `zxv_ramdisk_selfcheck`
  (`blockdev/ramdisk.c:233`) have no caller; `smart_self_test` is only called by
  `net/test_smart_adapter.c`, which no Makefile builds.
- `m5_congruence_selfcheck`: two of its four sub-checks compare literals.
- `[HANDOFF_OK]` (bare-metal wave 1 bundle, not at this base) ignores the kernel-window
  verdict and the ACPI-lost flags.
- The gate's restore check (`zxv_decl_gate.c`) compares the count of ready modules before
  and after, not the set.

Module findings from the property tests:

- `lpres`: `lpres_attest` accepts duplicate names (find returns the first) and state values
  outside 0..3; names are clipped to 31 bytes but `lpres_find` compares the full input, so a
  long name is never found again.
- `oseq`: an invalid parent index is stored as 0, which is also a real event index (the
  header calls 0 the null ordinal); a name longer than 31 bytes never matches on
  re-registration, so the node is duplicated; a hash longer than 32 bytes is never recorded
  for replay; `oseq_get_depth` is exponential on dense DAGs (no memo).
- `zcapital`: host tests (`-DTEST_HOST`) exercise the `double` representation; the kernel uses
  Q32.32. The new property test runs both (tiers.py `q32` mode). `zcap_exchange` adds without
  an overflow check.
- `zxv_decl` section: on the host the assembler warns "setting incorrect section attributes
  for .rodata.zxv_decl" (relocated pointers in a read-only section name).
- Mutation survivors that remain are listed per module in
  [tests/MUTATION_REPORT.md](../tests/MUTATION_REPORT.md); most are self-check bodies
  (`zphi_selfcheck`, `modbind_selfcheck`, the gate's negative tests), whose mutants only
  weaken a checker and cannot be seen while the code is correct.

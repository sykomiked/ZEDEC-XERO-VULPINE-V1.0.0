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
| F8 | `kernel/src/oseq/oseq.c` `oseq_register_event` | A parent ordinal at or past the event count (including the new event's own index) was stored as 0 instead of being refused. The first event's "parent" became itself, a self-loop, and `oseq_get_depth` recursed until the stack overflowed (segfault). Found as a positive mutant: removing the store-as-0 branch changed behaviour only on invalid input. | Probe trace; `test_oseq_props.c` "a parent on the first event is refused", "a parent equal to the new index is refused": 11 failures before the fix. | Every parent is checked before anything is written; a future or missing parent returns -1 and writes no slot, ordinal, sequence or replay entry. |
| F9 | `kernel/src/swarm/swarm_market.c` `swarm_market_credit` | Each trader's cycle value and Social totals were overflow-checked one trader at a time, but M6 settlement splits the pot by their sum across traders, and `swarm_split_lr` requires that sum to fit in 64 bits. Two traders credited near 2^63 each (or one at `UINT64_MAX` and one at 2) wrapped the sum and settlement paid out more than the pot held: money was created. | `test_swarm_props.c` `test_cycle_value_bound`: on the old code 6 failures, including "settlement conserves money". The probe hit it as a timeout of the "unreachable" break in `swarm_split_lr`. | Each trader's cycle value and Social totals are held to `SWARM_MKT_CYCLE_MAX` (`UINT64_MAX / SWARM_MAX_MODELS`), so the sum over 64 traders fits; a credit past it is refused and credits nothing. |
| F10 | `kernel/src/lpres/lpres.c` `lpres_conjoin`, `lpres_disjoin` | Not closed over `lpres_state_t`: a state value above BOTH (which `lpres_attest` accepts, see section 3) went into the chain-order tables unchecked and came out unchanged (`TRUE AND 5` returned 5), a value outside 0..3. Found as positive mutants: the mutants that clamped the index were more correct than the original. | `test_lpres_props.c` closure over inputs 0..8: 76 failures before the fix. | Inputs above BOTH are read as NEITHER (nothing known) before the table lookup, so both operations always return one of the four states. The tables themselves are unchanged. |
| F11 | `kernel/src/modbind/zxv_decl_gate.c` `gate_power_readout` | The threshold sweep kept 13 distinct power values (`MB_PHASES` entries), but an edge can carry up to 29 (four drive levels times seven power factors, plus zero). From the fourteenth distinct power on, an edge was counted in the total and in no cut, so the sweep's last line read "would drop 13 of 14 edge(s)". Found by hand-checking mutants the probe called equivalent (#181, #182: the guard that silently dropped the edge). | `test_modbind.c` "fourteen distinct powers": 3 failures on the old gate. | The table holds `GATE_PW_MAX` = 29 entries, every possible magnitude. |
| F12 | `kernel/src/lpres/lpres.c` `lpres_add_evidence` | The evidence counters wrapped: at `UINT32_MAX` one more piece of evidence took the count to 0, and an attestation with only that kind of evidence read as NEITHER. Found through mutant #140 (the `else` branch the wrap made reachable). | `test_lpres_props.c` "evidence at the ceiling": 2 failures before the fix. | The counters saturate at `UINT32_MAX`. |

## 2. Owner decisions (state B)

| # | Where | Finding | Options |
|---|---|---|---|
| B1 | `kernel_main_arm64.c:2454`, `kernel_main_x86_64.c:341` | The declaration gate (`zxv_decl_boot_gate`) is advisory on arm64 and x86_64: its fault count is discarded, and the comment says halting "would be a policy change". riscv and arm32 halt on it. The outside review assumed the gate fails closed everywhere. | Keep advisory (F1 now fails CI on any `[FAIL]` line the gate prints, so CI is closed even if the boot is not), or halt like riscv/arm32. |
| B2 | `kernel/src/lpres/lpres.c` `lpres_safety_gate_clear` | Returns true on an empty registry or one where every entry is revoked. The module's own bring-up comment says "nothing attested must not read as everything permitted". | Return false when no live entry exists, or document that the gate means "no live uncertainty". Callers must be checked before changing it. The property test deliberately does not assert either answer. |
| B3 | `kernel/src/modbind/modbind.c` `modbind_compose` | A composite drops every ingest port of its parts, silently truncates emits past `MB_MAX_PORTS` (requires and provides refuse instead), never sets `xform.unpack`, and nesting is not associative for emits. `test_modbind.c` asserts the provides/requires algebra (which holds) but not the ports. | Define port composition (ingests surface like requires; refuse instead of truncating), or document composites as capability-only. |
| B4 | `kernel/src/zcapital/zcapital.c` `zmarket_match` | Head-of-line blocking: only the first live offer is matched; if its only seeks are from the same party, no other offer can match. | FIFO by design, or scan for the first matchable pair. The property test pins the current rule. |
| B5 | `kernel/src/swarm/swarm_market.c` `swarm_market_credit_frugality` | Unspent tokens (`allotted - used`) are credited as Natural capital each cycle. This is not settlement minting (tokens are not created), but it is a conversion the economy docs should state. Pay-as-bid also applies regardless of tokens won. | Confirm against SWARM_ECONOMY.md. |
| B6 | `kernel/src/swarm/swarm_ledger.c` `swarm_witness_reward`, `swarm_market_join` | Witness id 0 means "the kernel witnesses", but 0 is also a legal model id: `swarm_market_join(m, 0, ...)` and `swarm_budget_register(b, 0, ...)` accept it. The reward skips id 0, so a real model 0 is never paid for witnessing. | Refuse model id 0 at register and join, or give the kernel a reserved id outside the model range. The new test pins the current rule (id 0 credits nobody). |
| B7 | `kernel/src/modbind/modbind.c` `modbind_verify_graph` vs `modbind_compose` | One module that provides the same capability at two contracts is not a CONTRACT problem for the graph check (it compares distinct modules only), but `modbind_compose` refuses a part that does the same. The two disagree on whether a single module may serve two contract versions. Mutant #699 (`k = i + 1` to `k = i`) is held OPEN on this decision, and so is its twin in the gate's readout (`zxv_decl_gate.c` `gate_report_all`, the same loop start). | Refuse it at `modbind_register` (then both agree and the mutant is equivalent), or let compose accept it. |

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
- `oseq`: an invalid parent index was stored as 0 (now refused, F8); a name longer than 31 bytes never matches on
  re-registration, so the node is duplicated; a hash longer than 32 bytes is never recorded
  for replay; `oseq_get_depth` is exponential on dense DAGs (no memo).
- `zcapital`: host tests (`-DTEST_HOST`) exercise the `double` representation; the kernel uses
  Q32.32. The new property test runs both (tiers.py `q32` mode). `zcap_exchange` adds without
  an overflow check.
- `zxv_decl` section: on the host the assembler warns "setting incorrect section attributes
  for .rodata.zxv_decl" (relocated pointers in a read-only section name).
- Boot gate: a declaration that `modbind_register` refuses is counted twice in the gate's
  total (once as refused, once as a failed bring-up because it is not in the registry), and
  its line reads "its own check returned non-zero" although nothing ran. The gate fails
  either way; the wording sends the reader to the wrong module code.
- Boot gate: past `ZXV_DECL_MAX_FAILED` (8) the gate prints "further failures counted but
  not named" for failed bring-ups, but held bring-ups past the eighth are dropped without a
  line; only the `held=` count includes them (`test_decl_table.c`, 12 declarations, pins
  this).
- Boot gate: the "[FAIL] an edge reached EXACTLY ZERO real power" line is printed but not
  added to the gate's problem count, and a zero-drive provider (`MB_AMP_NONE`) reaches it
  by design. On arm64 and x86_64 F1 turns the line into a CI failure anyway.
- Mutation survivors: every survivor of the eleven property-test targets is classified by
  measurement (`tests/harness/mutator/classify.py`) and read by hand. None is OPEN except
  modbind #699, which is held for B7. The rest are self-check bodies or equivalent mutants,
  each with its argument in [tests/MUTATION_REPORT.md](../tests/MUTATION_REPORT.md).

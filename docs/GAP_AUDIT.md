# ZXV architecture gap audit — 2026-10-10

This audit was measured on commit f909145, the clean breaking point: all 17 CI jobs and CodeQL green on PR #1.

The architecture map is in [ARCHITECTURE_AND_STATUS.md](ARCHITECTURE_AND_STATUS.md), and per-module status is in [SYSTEM_REFERENCE.md](SYSTEM_REFERENCE.md). This file lists only what is missing or disconnected.

## How it was measured

- **Kernel image.** Every object built by `Makefile.arm64` was checked against the symbols left in `kernel_arm64.elf` after `--gc-sections`, per subsystem.
- **Shipped app.** The Mac app runs `zxv-engine`. Its exact source list comes from `kernel/arch/hosted/app_sources.sh srcs`.
- **Tests.** Every source path or glob named in `kernel/Makefile`, `fuzz/Makefile` and `tests/` was expanded to find which files any test build compiles.
- **Include graph.** The `#include` graph was built across all 210 subsystems.
- **Specs.** Each spec the owner gave (in project memory) was checked by searching for its identifiers in the code.

## The headline

ZXV currently ships as three products that do not share a runtime:

| Build | What it contains | Size |
|---|---|---|
| Mac app (`zxv-engine`) | hosted shell, swarm (budget, emotion, market, DNA, governor), tensor engine, Vinea P2P node, ML-DSA-65 + ML-KEM-768, updater, IPFS node, notifications | 36.4k of 216k kernel source lines |
| Kernel image (arm64) | 350 of 565 sources compiled; economy layer self-checks at boot | 120.9k lines compiled, most symbols then discarded as unreachable |
| Host test builds | nearly everything, module by module | each test links its own small set |

**The economy the specs describe is not in the app.** That covers pay, the assurance fee, cbank netting, vino, count house, market, cards, quest, peer audit, freight, harmonic wire, bootlegger, web4, ident and sutra. Each one exists, and each one passes its own tests. None of them is called by anything a user runs.

## Gaps, in the order I propose to fill them

### A. Integration (the modules don't meet)

| # | Gap | Evidence | Fill |
|---|---|---|---|
| A1 | **No single ledger of record.** About ten modules keep balances: `swarm_ledger`, `pay_ledger`, `vino`, `cbank`, `count_house`, `finance/triple_ledger`, `community_chest`, `mesh_token`, `crypto_wallet` and `zcapital`. Nothing reconciles them. | `swarm_ledger_post_cycle` in the engine writes only the swarm's own ledger. The pay stack and the finance/vino stack touch only through `finance`. | Make `pay_ledger` the posting authority. Make the others views or adapters that post through it, with A = L + E checked after every posting. |
| A2 | **The app doesn't link the economy.** There is no `libzxv` amalgamation and no `zxv_engine.h` facade. | `app_sources.sh srcs` lists no `pay`, `cbank` or `vino` file. | Build `libzxv` (amalgamation + 3-call facade: compress / route / decompress) and link it into `zxv-engine`. Expose it over the engine's local HTTP API now, and over zxvd JSON-RPC later. |
| A3 | **Two PQ handshakes.** Vinea has its own (ML-KEM-768 + X25519 + ML-DSA-65). Bootlegger has the transcript-bound one, which is fuzzed, but nothing uses it: 0 users in the include graph, 0 live symbols in the image. | Include graph `bootlegger: usedby[]`; `bootlegger.h:83` is still 768. | One session layer: upgrade bootlegger to ML-KEM-1024 + ML-DSA-87 (roadmap phase 2) and have Vinea carry its sessions. |
| A4 | **Settlement isn't wired to inference.** The swarm market settles token budgets, but no step charges the 0.08889% fee or posts to a payment ledger. | The engine calls `swarm_market_settle`, never `pay_*`. | Chain swarm settle → pay ledger → assurance fee buckets, once per cycle. This is the "ouroboros" loop: fees feed the reserve, the dividend and node bounties, and bounties feed the next cycle's budget. |
| A5 | **Leaf islands.** These modules have no caller in any build: `cbank`, `cardnet`, `community_chest`, `devmesh`, `evolve`, `harmonic`, `ident`, `iso20022`, `legal_engine`, `market`, `peer_audit`, `provider`, `quest`, `stream`, `sutra` and `tripartite_fs`. | Include graph `usedby[]` is empty for each. | Attach each one to the A1/A2 spine, or archive it to `kernel/experimental` (as HAEP-1 already does for dead modules). |
| A6 | **No cross-module harness.** There is no test that runs tensor → pay → escrow timeout → rollback under a PQ reconnect with a seeded network simulation. | Every test links one module and its direct dependencies. | Build `tests/integration/` with a seeded network simulation (jitter, drop, reorder) and the conservation property checked on every clearing cycle. |

### B. Structure and quality control

| # | Gap | Evidence | Fill |
|---|---|---|---|
| B1 | **391 modules have no layer (stratum).** The static REQUIRES/PROVIDES check is not enforced. | `verify_layers.sh`: "unassigned modules: 391", and checks 2–4 run only at boot. | Assign every module to the seven strata. Make `verify_layers.sh` fail on an unassigned module and on an unresolved REQUIRES. Run it in CI. |
| B2 | **20.5k shipped lines are in no test build.** | `emu` 5.1k, `net` 2.7k, `modbind` 2.0k, `desktop` 1.5k, `sdk_bridge` 1.3k, `panopticon` 1.1k, plus 17 smaller modules. | Add tests that kill mutants, starting with `net` and `modbind` because the app and the boot path depend on them. |
| B3 | **4.4k orphan lines** are in no image, no app and no test. | 9 `apps/*` files, 7 `mlkem/*_validate.c`, `polyglot_matrix.c`, `ramdisk.c`, `chiglet_triage.c`, `pq_hqc5_gf2x.c`, `app_template.c`, `render_boot.c`, `arch_globals.c` | Test them, or move them to `kernel/experimental`. |
| B4 | **Duplicate primitives.** | SHA-256 has 3 implementations (robin_debanks, crypto_wallet, event_envelope). GCD has 3 (rational, rmag, m5_types.h). Keccak has 7 (one in mlkem, plus vendored copies in pqsec). | Keep one SHA-256 and one GCD, with the others calling them. The vendored Keccak copies stay, because they are upstream code pinned by KATs. |
| B5 | **No self-healing framework.** Only ad hoc recovery exists, in `sdk_bridge`, `tripartite_fs` and `event_space`. | grep finds 9 files. | Build the designed recovery from the harness notes: refuse / roll back to a checkpoint / isolate. Each recovery emits a signed RepairProof, and hard invariants stay fail-closed. Never auto-patch state. |
| B6 | **`i18n` (35k lines) builds only in its own tests.** | 0 objects in the image, not in the app. | Generate the app's UI strings from it (the CLDR tables are already there). |
| B7 | **Test ratio is 0.36:1 against the 13:1 target.** | Measured on c971139. | The 13-tier suites, counted only where they kill mutants. |

### C. Specced but not built

| # | Spec | State |
|---|---|---|
| C1 | Zenny chips / compression minting (minter picks κ; chip claim == deposited value at mint and at each rebalance) | Not in code. |
| C2 | Dutch-auction V-Bill issuance, backed only (reserve stays at or above the floor, per-epoch cap) | Not in code. `cbank` has a descending sweep, but no issuance. |
| C3 | Custodian dual-key warehouse receipts (ML-DSA), haircuts, signed median price feed | No haircut, price feed or receipt code in the economy modules. |
| C4 | Solvency vault funded by a third of the reserve share (16.67% of the fee), capped at 1/3 of V-Bills, excluded from the Vino floor | `pay_assure` has the 4-bucket split, but no vault sub-split or cap. V-Bills are "future work" there. |
| C5 | V-Bills (perpetual musharakah equity, dividend pool payout, redemption at the backing floor) | Named pool account only. |
| C6 | Account recovery (3-of-5 guardians, veto, timelock, staged thaw) and estate succession (faraid, forced heirship, escrow) | `ident` mentions guardians. There is no estate code. |
| C7 | Sutra and COBOL copybooks compiling to the same AST, rejecting weight ≠ 1 or claim > deposit | `sutra` exists (compiles to vino) with no caller. COBOL appears only in `legacy`. |
| C8 | Cycle-cancellation netting for the 13-tier suite and 50M-op fuzz | Lives only in `cbank` (not linked). The property fuzz covers vino, pay, count house and triple ledger. |
| C9 | Sicily Account / Player Card tiers | Partial in `pay`. |
| C10 | AI end to end with real weights | Blocked on huggingface.co access in Project settings. |

## Owner decisions this audit surfaced

None are blocking. I'll proceed on the defaults above unless you say otherwise.

- **A1:** `pay_ledger` becomes the ledger of record (default), and the others post through it.
- **A5 / B3:** modules nobody calls get archived to `kernel/experimental` rather than deleted (default). Archived code stays in git and stays testable.

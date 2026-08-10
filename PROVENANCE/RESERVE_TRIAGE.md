# RESERVE TRIAGE — the 58 compiled-but-absent files

**Scope note, stated first because it bounds everything below.** The triage set is 58 files that
appear in `build_system/Makefile.arm64` `KERNEL_SRCS`, compile to a `.o`, and contribute **zero
symbols** to `kernel_arm64.elf` (measured with `aarch64-linux-gnu-nm --defined-only --format=posix`,
anchored `^symbol` match, against the 1411–1414 defined symbols in the shipped ELF). **28 of the 58
carry a completed verdict in this document.** The remaining 30 were not returned by the triage pass
and are recorded here as unadjudicated, not as clean. Do not read this file as covering all 58.

---

## 1. Summary

Of the 28 files adjudicated: **5 are deletable (1,790 lines)**, **7 are staged and should be kept but
marked unexpressed (1,765 lines)**, and **16 are real defects — code the system claims, advertises in
a boot banner, or paints on a live screen, that is not in the binary (3,359 lines)**. Six of those 16
survived an adversarial second pass unchanged; three initially-filed defects were downgraded to
STAGED when their claims were tested and failed.

**The number that matters: 1,790 lines are surplus. 3,359 lines are broken promises. The problem is
not that there is too much code — it is that the shipped kernel asserts capabilities it does not
link.**

---

## 2. REDUNDANT — delete

Superseder must be *proven live in the ELF*, not merely present in the tree. Every superseder below
is cited with its ELF symbol and address.

| File | Lines | Superseded by (proof of reachability) |
|---|---:|---|
| `kernel/src/mm/mm.c` | 187 | `kernel/arch/arm64/arm64_mmu.c` — `arm64_mmu_init` **T 0x40081f80**, called at `kernel_main_arm64.c:722`; frames from `el0_userspace.c` — `proc_map_page` **T 0x40089e80**, pool `user_page_pool` **b 0x4651f000**; heap = `fs_malloc`, wired kernel-wide by `Makefile.arm64:40` (`-Dmalloc=fs_malloc`) with statics `heap.0` **b 0x4a8f2a20** in the ELF. mm.c is *structurally* incapable of arm64: `mm.h` declares a 32-bit x86 `page_directory_t`; `mm.c:170-173` `mm_switch_directory` body is `(void)dir; /* Would load CR3 */`; `mm.c:88-91` `mm_get_page(make=true)` returns 0. Its only two callers, `kernel/boot/kernel_main.c:222` and `kernel/arch/arm/kernel_main_arm.c:134`, are in **no** Makefile. |
| `kernel/src/crypto_wallet/crypto_wallet.c` | 760 | `zxvfs_tri.c` + `trispace` + `zab` — `zxvfs_tri_open` **T 0x400ca0a0**, `zxvfs_tri_write` **T 0x400c9c00**, `zxvfs_tri_read_role` **T 0x400ca380**, `tri_bind` **T 0x400ae780**, `tri_verify` **T 0x400ae880**, `zab_derive_capabilities` **T 0x400c8120**. `CONSUMER_READINESS_AUDIT.md:185` already answers the integrity question with `zxvfs_tri.c:251-253`. Also carries a **private** `cw_sha256` (`:41-146`) and `cw_hmac_sha256` (`:149`) while the kernel links shared `sha256` **T 0x400c33c0** / `hmac_sha256` **T 0x400b4b60` — a direct violation of `ARCHITECTURE_WHITEPAPER.md:172` ("the kernel's real sha256, never a private hash"). |
| `kernel/src/xedit/xedit.c` | 438 | `kernel/src/appkit/doc.c` — `doc_init`, `doc_insert`, `doc_text`, `doc_undo`, `doc_redo` all in the ELF and called from the live boot path at `kernel_main_arm64.c:3362`, `:3370-3371`, `:3377`. Symbol-for-symbol overlap (`xedit_init`/`doc_init`, `xedit_undo`/`doc_undo`, …); `doc.h` names itself the designated survivor ("Writer, Sheet, Deck, Sound and Reel all sit on this"). |
| `kernel/src/audiogenomics_pro/architectural_directives.c` | 243 | `lpres_core.c` (`lpres_init`, `lpres_get_presence` live; included `kernel_main:43`), `trispace.c` (`tri_init`/`tri_bind`/`tri_verify` live; included `kernel_main:458`), `zxvfs_tri.c` (live). Every directive the file declares "binding" is already enforced by a module that runs. 0/11 of its own symbols live; zero callers tree-wide. |
| `kernel/src/shimmer/shimmer.c` | 162 | Inline shimmer field in `kernel_main_arm64.c:330-404` — `zxv_present.part.0` **t 0x40082ea0**, `g_sin64` **r 0x400e86f0**, `g_holo_phase` **b**, driven per frame by the live compositor at `zxv_shell.c:627/692/815`. Same capability, same construction (phase-counter travelling wave, no wall clock). 0/8 shimmer.c symbols live. |

**Subtotal deletable: 1,790 lines / 5 files.**

Two deletions carry a mandatory pre-condition:

- **`xedit.c`** — port the two things `doc.c` lacks *before* deleting: the integrity hash
  (`xedit_update_hash`/`xedit_verify_integrity`) and multi-buffer
  (`xedit_buffer_create/switch/close`). `ARCHITECTURE_WHITEPAPER.md:194` claims "xedit is a
  gap-buffer editor tracking a live cid/merkle_root" — that merkle tracking is exactly what the
  survivor does not have. Port it or drop the claim.
- **`crypto_wallet.c`** — confirm the 5-key-per-trit-phase derivation (`:277 cw_derive_key`,
  `:450 cw_file_phase_shift`) is abandoned. It is the one thing `zxvfs_tri` does not cover. If it is
  wanted, port it onto the live `zab_derive_capabilities` — do not revive 760 lines.

Doc cleanups that must land with these deletions: `SUBSYSTEM_INDEX.md:124` (MM row),
`SUBSYSTEM_INDEX.md:101` + `SOURCE_CODE_NAVIGATION_GUIDE.md:171` (crypto_wallet rows),
`CONSUMER_READINESS_AUDIT.md:185` (stop citing crypto_wallet as an active mitigation),
`ARCHITECTURE_WHITEPAPER.md:204` (describes shimmer.c's 256-entry/3-wave version; the code that runs
is 64-entry/2-wave — the doc documents the dead file).

---

## 3. STAGED — keep, mark unexpressed

Nothing in the running system expects these. They are written ahead of a phase that has not arrived.

| File | Lines | What phase needs it |
|---|---:|---|
| `kernel/src/identity/identity.c` | 456 | **Unexpressed by configuration, not neglect.** `kernel_main_arm64.c:804-805` *does* call `identity_registry_init` — inside `#if KERNEL_SIM_DEVICES`, and `Makefile.arm64:14` sets `KERNEL_SIM_DEVICES ?= 0`. The `#else` at `:807-808` prints `[SKIP] National identity registry (KERNEL_SIM_DEVICES=0)`. The boot log tells the truth. Comes alive with `make -f build_system/Makefile.arm64 KERNEL_SIM_DEVICES=1`. **Do not wire unconditionally** — that would ship a simulated national-ID registry in a build that currently, correctly, declines to claim one. |
| `kernel/src/decent/decent.c` | 370 | `ROADMAP.md:54` marks it `**[planned]** D3 — Mass networking`. Zero callers, zero banner hits (`grep pungent\|garlic\|IPFS\|BitTorrent\|Matrix E2EE\|did:zede\|BIP-39` over `kernel_main_arm64.c` = 0). **Do not wire as-is:** `decent.c:56 dc_hash` is FNV-1a, not cryptographic, and it produces `n->pubkey` (`:82`), the Matrix `room_key` (`:178`) and `did->pubkey` (`:236`). Reaching this code today would manufacture worthless key material. The `zk_verify` fix (`:267-271` now forces `zk_verified = false` and returns `DECENT_ENOTIMPL`) is correct and correctly inert — no action. |
| `kernel/src/pungent/pungent.c` | 364 | The whole privacy/P2P tier is dark together — `bootlegger`, `caracho`, `panopticon`, `plnp` all 0-live. Revive as a tier or not at all. **Two doc defects to fix now:** `ARCHITECTURE_WHITEPAPER.md:196` claims present-tense that pungent's garlic/onion tiers "map onto these same file roles" — no such mapping exists anywhere in `trispace`/`fusion`; and `SUBSYSTEM_INDEX.md:106` describes it as "Scent/chemical analysis", a name-based guess. It is a garlic-routing engine with Tor/I2P bridges (`:77`, `:135`, `:181`). |
| `kernel/src/alloc/alloc.c` | 248 | Settlement phase. *(Downgraded — see §5. Note the unresolved conflict flagged there.)* |
| `kernel/src/bios/zbios.c` | 159 | A measured-boot phase that has not landed. *(Downgraded — see §5.)* |
| `kernel/src/axiom_matrix/axiom_matrix_core.c` | 94 | An M8-lift query phase. *(Downgraded — see §5.)* |
| `kernel/src/sovereign_node/sovereign_node.c` | 74 | Sits one layer above `federation.c`'s `microstate_constitute`/`microstate_recognize`, which is itself unwired. Absent from `SERVER_HANDOVER.md:82`'s 25-module boot list, from every boot banner (`boot_features.c` names only onepolicy/zcapital/crown/ministry/ipfs), and from `zxv_shell`'s INTERSPACE buttons. Becomes wireable for free the moment federation gets a real caller — re-evaluate then. |

**Subtotal staged: 1,765 lines / 7 files. No code change. Mark unexpressed in `SUBSYSTEM_INDEX.md`.**

---

## 4. SHOULD-BE-RUNNING — real defects

Ordered by severity: falsified security claims first, then legal obligation, then UI that paints a
state no code computes, then regressions and unclaimed islands. **✅ = survived the adversarial pass.**

### 4.1 `kernel/src/robin_debanks/aes256_gcm.c` — 295 lines ✅

- **Claim:** `kernel_main_arm64.c:974` prints `[INITIALIZED] Daemon visa system + encrypted vault`.
  `CONSUMER_READINESS_AUDIT.md:217` lists "wire the AES-GCM vault to a real flow with a
  store→retrieve boot self-test" as required, and `:218` sets the release gate "`nm` shows TLS and
  AES symbols present in the shipped ELF".
- **Actual:** `grep -ci aes256` over the ELF symbol table = **0**. The boot path provisions a 32-byte
  vault key from FEAT_RNG hardware RNG at `:962-971` with its own `[ENTROPY]` banner, announces an
  encrypted vault at `:974`, and prints `robin-vault: entries= stored= unlocked=` at `:3049-3055`
  — permanently 0. `robin_init` **T 0x400c3160** and `robin_update_coverage` **T 0x400c31e0** are
  live; `robin_store`/`robin_unlock` are gc'd because `kernel_main` calls only those two, so the
  cipher goes with them. The live `aead_seal`/`aead_open` are ChaCha20-Poly1305 for TLS — a
  different cipher; `aes256_gcm.h:1-12` specifies AES-256-GCM for the vault specifically.
- **The gate built to catch this false-passes.** `verify_banners.sh:44` runs `nm | grep -ci vault`,
  which matches `robin_vault` and `vault_key.86` — two **BSS data** symbols — and reports
  `present 2 syms  encrypted vault` while the cipher has zero.
- **Fix:** (a) change `verify_banners.sh:44` from `check vault` to `check aes256_gcm` — a substring
  match on a struct instance is currently certifying an absent cipher; (b) add a `robin_store` →
  `robin_unlock` round trip at boot, which anchors both functions and the cipher in one call. The
  KAT vector is already named in `aes256_gcm.h:11-13` (NIST GCM Test Case 13, tag
  `530f8afbc74536b9a963b4f1c4cb738b`), following the X25519 KAT pattern at `:2113`. Note
  `crypto_validate.c` already contains this KAT but is a **host** program (has `main()`, includes
  `stdio.h`) in no makefile — do not just add it to `KERNEL_SRCS`.

### 4.2 `kernel/src/tls/handshake.c` — 445 lines

- **Claim:** `ARCHITECTURE_WHITEPAPER.md:108`, present tense: "`tls/` builds a TLS 1.3 client atop
  `x25519` + `hkdf` + `aead`, with `TLS_VERIFY_REQUIRED` failing closed on unverified certs."
  Same release gate as above (`CONSUMER_READINESS_AUDIT.md:217-218`).
- **Actual:** `tls_client` / `tls_error` = 0 hits in the ELF. Whole-tree grep for
  `tls_client_hello`/`tls_client_feed`/`tls_client_init` finds **only the definitions**;
  `handshake.h` is included by exactly one file, `handshake.c:3`. No module under `kernel/src/net/`
  references it. The fail-closed decision is real in source (`handshake.c:289-292`) and not in the
  binary. The collapse is deep: `tls13_early_secret`, `tls13_handshake_secret`, `tls13_master_secret`,
  `tls13_finished`, `ct_equal` (all `tls/hkdf.c:147-213`) are absent too; only the KAT-exercised half
  of `hkdf.c` survives. `verify_banners.sh:9` names "record/handshake" in its own measured-absent
  list, but the arm64 check block `:39-52` checks only x25519/hkdf/aead — so nothing catches it.
  There is no second TLS client anywhere in the tree.
- **Fix:** minimum honest step — add `check tls_client "TLS 1.3 client"` to `verify_banners.sh` so
  the absence is loud. Real fix per the audit — give `handshake.c` a certificate verifier and call it
  from the net path. **Before wiring:** `tls_client_feed` keeps a `static uint8_t pt[TLS_MAX_CIPHERTEXT]`
  at `handshake.c:379`; review for re-entrancy against a live socket. If neither is done, delete the
  whitepaper claim — a stated fail-closed guarantee backed by no linked code is the worst item on
  this list.

### 4.3 `kernel/src/tls/record.c` — 136 lines

- **Claim:** `kernel_main_arm64.c:2180` prints `[BOOT] TLS 1.3 key schedule + record AEAD...`;
  `verify_banners.sh:45` `check aead "TLS record AEAD (ChaCha20-Poly1305)"`.
- **Actual:** `tls_record` / `tls_keys` = 0 hits. The block under that banner **hand-builds** a
  5-byte AAD `{0x17,0x03,0x03,0x00,0x10}` at `:2203` — a fabricated TLS record header — then calls
  `aead_seal`/`aead_open` directly at `:2207-2208`. It never calls `tls_record_write`/`tls_record_read`.
  The `[OK]` text at `:2221` is precise about what it did; the word **"record"** in the banner title
  points at an absent module. Not superseded — `kernel_main` reimplements none of: per-key sequence
  reset (`record.c:15-19`), nonce derivation via `tls13_record_nonce` (`:66`,`:114` — itself absent),
  content-type hiding, zero-padding strip (`:128-134`), the change_cipher_spec skip that must not
  touch the sequence number (`:97`), the no-advance-on-auth-failure rule (`:119-122`).
- **Fix:** do **not** treat separately from 4.2 — `record.c` has exactly one consumer (`handshake.c`
  at `:157`, `:331`, `:434`, `:383`), so wiring the TLS client restores it automatically and nothing
  else will. Immediate zero-cost correction: reword the `:2180` banner to "key schedule + AEAD
  primitive", or add `check tls_record` to the gate.

### 4.4 `kernel/src/license/license.c` — 357 lines

- **Claim:** `kernel_main_arm64.c:56` does `#include "license.h"`. `kernel/include/license.h`
  declares `LICENSE_COUNT 4` and asserts the four instruments **travel together** — "you cannot strip
  one off". `kernel_main_arm32.c:19` declares `extern void license_print_all(void);` and `:50` calls it.
- **Actual:** `license_` = 0 hits in the ELF. `grep 'license_' kernel_main_arm64.c` returns **zero**
  — the header is included and nothing is called. It links clean because everything else the header
  offers (`OPL_SPDX`, `LICENSE_SPDX_BUNDLE`, `LICENSE_COUNT`) is a compile-time macro. The only
  license text that reaches a screen anywhere is a hardcoded
  `fb_puts("License: SEL-3.3 - Streisand Engine License\n")` at `kernel/boot/kernel_main.c:93` —
  which names **one** of four instruments and is not the arm64 entry point (`Makefile.arm64:109`
  links `kernel/arch/arm64/kernel_main_arm64.o`). **Net effect: the arm64 kernel conveys no license
  notice at boot at all**, while a sibling arch does.
- **Fix:** one line — call `license_print_all()` near the boot identity banner at
  `kernel_main_arm64.c:702`, matching `kernel_main_arm32.c:50`. Also replace the stale
  single-instrument string at `kernel/boot/kernel_main.c:93` with `license_spdx_bundle()`. Cheapest
  fix in the set and the only one with a licensing-obligation angle.

### 4.5 `kernel/src/telemetry/telemetry_core.c` — 53 lines ✅

- **Claim:** `kernel_main_arm64.c:47` includes it; `SUBSYSTEM_INDEX.md:30` lists it in table 1.1
  "Core M5 Axiomatic Kernel" with status PASS, alongside phase_coord/rmag/lpres/iphase/choice/oseq —
  **all six of which are live in the ELF**.
- **Actual:** 0/5 symbols live. **This is a port regression, not staging.** `kernel/boot/kernel_main.c:485-489`
  and `kernel/arch/arm/kernel_main_arm.c:275-279` both contain, verbatim, the block
  `if (cycle % 50 == 0) { static axiom_matrix_t axiom_mat; telemetry_t tel = emit_and_observe(&axiom_mat, tick.omega); choice_resolve_from_telemetry(&tel, 0); }`
  directly after `phase_coordinator_tick(&tick)`. The arm64 equivalent,
  `kernel_event_cycle_run()` at `kernel_main_arm64.c:2789-2790`, calls `phase_coordinator_tick`
  (**T 0x400af7a0**, live) and then simply omits the telemetry block. arm64 kept the tick and dropped
  the telemetry→CHOICE feedback loop.
- **Fix:** restore the block at `kernel_main_arm64.c:2790`, gated on `g_event_cycle % 50` so it stays
  phase-tick ordered with no wall-clock read. Nothing blocks the link — the object's only undefs are
  `axiom_matrix_project_tick`/`axiom_matrix_set` and `__muldc3` (libgcc, supplied via `$(LIBGCC)`).
  Also delete the stray byte-identical duplicate at `kernel/telemetry_core.c` (repo root, in no
  Makefile). If the loop is deliberately unwanted on arm64, delete the include at `:47` and demote
  the `SUBSYSTEM_INDEX.md:30` row — but do not leave the include standing as a claim.

### 4.6 `kernel/src/vino_stores/vino_stores.c` — 289 lines

- **Claim:** `SERVER_HANDOVER.md:82` ("boots on arm64", cites the 5/5 banner);
  `ARCHITECTURE_WHITEPAPER.md:114`; and — strongest in the whole set — **the shipped ELF literally
  contains the strings** `COVERAGE 1.8X  SOLVENT` (`zxv_shell.c:856`), `846 DEBIT` and `999 EQUITY`
  (`zxv_shell.c:354`), verified with `strings kernel_arm64.elf`. Those are
  `vino_stores.h`'s `VINO_ISO_DEBIT 846` / `VINO_ISO_EQUITY 999` and `VINO_COVERAGE_NUM/DEN` = 1.8x,
  verbatim.
- **Actual:** 0/11 symbols live. The backend actually wired into the shell's VINO space is
  `vino/vino.c` (`vino_init`/`vino_issue`/`vino_transfer`/`vino_get_balance`, all live, called from
  `spaces_boot()` and `space_action` case 1) — which has **no coverage gate and no single-active-state
  invariant**. The user reads "COVERAGE 1.8X SOLVENT" off a ledger that never computes coverage.
  Only other callers are `pirate_apps.c:74,173,178` (itself 0-live). Not redundant:
  whitepaper:114 states the two coexist by design.
- **Fix:** bind a `vino_stores_t` to a `triple_ledger` in the shell's VINO space (or in
  `boot_economy_init`) so the coverage gate and the single-active-state invariant the UI advertises
  actually execute; add a `vino_stores_` check to `verify_banners.sh`.

### 4.7 `kernel/src/iso20022/iso20022.c` — 303 lines ✅

- **Claim:** `SERVER_HANDOVER.md:82`; `ARCHITECTURE_WHITEPAPER.md:120`; and the live
  `vino_rail_name` returning `"ISO20022"` — `vino/vino.h:99` defines `RAIL_ISO20022=4`,
  `vino_rail_name` is in the ELF, and the literal string is present in the binary.
- **Actual:** 0/6 symbols live, **zero callers** outside its own directory and `test_iso20022.c`. A
  booted system can *name* an ISO20022 rail it cannot *serialize*. Checked for a superseder and found
  the opposite: `vino.c:258-291` defines `vino_msg_to_pacs008`/`_camt053`/`_from_iso20022` but they
  are **stubs** — `(void)txn` plus a fixed fragment `"<pacs.008><CdtTrfTxInf>"` — and are not live
  either; `sutra/sutra_rails.c:11-17` dispatches `EMIT-ISO20022` to those same stubs. `iso20022.c`
  is the only real bounded-writer implementation and it is the one discarded.
- **Fix:** delegate `vino.c`'s stub emitters to `iso20022_pacs008_build`/`iso20022_camt053_build`
  (or exercise them in `boot_economy_init`), then add an `iso20022_` entry to `verify_banners.sh`.

### 4.8 `kernel/src/social_spaces/social.c` — 218 lines ✅

- **Claim:** `ARCHITECTURE_WHITEPAPER.md:138` names a four-module stack — "concord (the matching
  commons), social_spaces (named public rooms), reputation, chronicle"; also `:223` and
  `SERVER_HANDOVER.md:82`.
- **Actual:** 0/9 symbols live. `concord` is live (8 syms) and `reputation` is live (5 syms);
  `social_spaces` and `chronicle` are both 0-live — **exactly half the declared stack is absent.**
  Only reference is `pirate_apps.h:40` (0-live). Ruled out as name collisions:
  `bootlegger_social_post`/`_sync` (`bootlegger.c:495,514`) and `situation_model`'s
  `social_cohesion` field. Explicitly not redundant with concord: `social.h:33-36` states it
  "duplicates none of concord's matching math"; concord exports no room or post primitive.
- **Fix:** wire a `soc_world_t` into the shell's CONCORD/SOCIAL space — it already composes the live
  `con_*` API — or correct whitepaper:138's four-module claim to two.

### 4.9 `kernel/src/crit168/crit_168_word.c` — 37 lines ✅

- **Claim:** `kernel_main_arm64.c:49` includes it; `SUBSYSTEM_INDEX.md:26` lists it in table 1.1
  "Core M5 Axiomatic Kernel" with status PASS; `kernel_main_arm64.c:880` and `:906` assert live
  behaviour over 168-bit identifiers ("decides whether a 168-bit-identified peer gets past the door").
- **Actual:** 0/5 symbols live, zero callers in the arm64 build. **No superseder is live** —
  `crit168_os.c` and `ubh.c` are in no Makefile; `cellular_multikernel.c`'s
  `crit168_zero`/`crit168_eq`/`crit168_from_digest` are themselves absent. But the capability *is*
  being exercised by hand: `community_chest` is live (`cc_init` **T 0x400c24e0**,
  `cc_register_node` **T 0x400d4340**) and porter_house/mesh_token gate peers by 168-bit ID per the
  comments at `:880`/`:906` — carrying 168-bit identifiers **without routing through the module that
  owns their canonical byte order**, which is exactly the drift this module exists to prevent.
- **Fix:** route the live paths through `octets_to_word168`/`word168_to_octets` at the
  community_chest device-ID and porter_house/mesh_token peer-ID boundaries. Sole undef is `__muldc3`
  (libgcc, supplied). **Honest split:** the four byte-level entry points meet the bar; the complex
  DFT pair `crit_transform`/`crit_inverse` (`:22-36`) has **no consumer and no claim anywhere** and
  drags in complex arithmetic a freestanding kernel otherwise avoids — split them out or drop them.

### 4.10 `kernel/src/interspace/minister.c` — 53 lines

- **Claim:** live desktop sub-space button `MINISTER` (`zxv_shell.c:363`) — the string is confirmed
  present in `kernel_arm64.elf`; `ARCHITECTURE_WHITEPAPER.md:132` asserts a consent/anti-seizure
  guarantee: "computes fair contributions for all parties but binds only those who consented",
  "`minister_may_seize` always returns false".
- **Actual:** 0/4 symbols live; `minister_*` referenced only inside its own files and
  `test_interspace.c`. The button resolves to nothing — `subspace_select` only echoes the label. A
  **guarantee about seizure is stated in the whitepaper about code that is not in the binary.**
  Distinct from the live `ministry/ministry.c` (`ministry_tribute`, an 11% treasury calc) — different
  subsystem, no overlap.
- **Fix:** wire `minister_adjudicate_average` behind the MINISTER button, or remove the button and
  retract the guarantee.

### 4.11 `kernel/src/interspace/flagstate.c` — 31 lines

- **Claim:** live button `FLAG-STATE` (`zxv_shell.c:363`, string confirmed in the ELF) and
  `LAT_DESC "FLAG-STATE / FEDERATION"` (`zxv_shell.c:228`); `ARCHITECTURE_WHITEPAPER.md:132` claims
  `cojurisdiction_eval` "allows an action only when the host term is `op_symbiotic_ok` AND the
  vessel's content still matches its flag".
- **Actual:** 0/2 symbols live. The FLAG-STATE **action** (`zxv_shell.c:528`) calls
  `vessel_immune_from` (`lex_rhodia.c`, live) **directly, bypassing the two-law gate entirely**. That
  conjunction is not in the binary.
- **Fix:** route the action through `cojurisdiction_eval` instead of the bare predicate; that also
  pulls in `vessel_flag_verify`. Cheapest interspace fix — its one dependency `op_symbiotic_ok` is
  already live.

### 4.12 `kernel/src/finance_markets/finance_markets.c` — 263 lines

- **Claim:** `SERVER_HANDOVER.md:82`; `ARCHITECTURE_WHITEPAPER.md:120`; live sub-space buttons
  `QUOTE`, `BOOK`, `DERIV`, `FUTURES` (`zxv_shell.c:362`, confirmed via `strings`).
- **Actual:** 0/13 symbols live. `subspace_select` only echoes the label; the RAM actions
  (`zxv_shell.c:521-525`) call `battering_ram`'s `br_price_capital_future` /
  `br_contributors_hold_majority`, which are live. **Name collision ruled out:** the `fm_demodulate`
  hits in `kernel/src/net/radio.c:192,237` are FM *radio*, unrelated. battering_ram does not
  supersede it — `fm_book_t` (the order book) is defined nowhere else in the tree.
- **Fix:** wire `fm_book_t`/`fm_pnl_report` behind QUOTE and BOOK, or delete those two labels so the
  UI stops advertising an order book.

### 4.13 `kernel/src/interspace/federation.c` — 151 lines

- **Claim:** live button `FEDERATE` (`zxv_shell.c:363`, string confirmed in the ELF);
  `LAT_DESC` at `:228`; `ARCHITECTURE_WHITEPAPER.md:130`.
- **Actual:** 0/8 symbols live. `space_action` case 10 (`zxv_shell.c:526-530`) routes every action
  index except 0 and 1 into the else branch (`safe_passage_valid`), so FEDERATE resolves to a TTL
  check on a stack-local grant. Sibling files in the same directory *are* partly live
  (`interstitial_is_commons`, `vessel_immune_from`, `safe_passage_valid`), so this is the unwired
  third of the set — nothing else in the tree defines `microstate_constitute`.
- **Fix:** wire `microstate_constitute`/`microstate_recognize` behind the button, or delete the
  button and the `LAT_DESC` line naming FEDERATION.

### 4.14 `kernel/src/broker/broker.c` — 316 lines

- **Claim:** `ARCHITECTURE_WHITEPAPER.md:174` describes it working end to end — `broker_deliver`
  routing through `ipfs_get_verify`, a `broker_trust_author` allow-list, the
  `aipi_hs_begin/_challenge/_prove/_allow` handshake with constant-time HMAC, fail-closed
  `BROKER_ERR_NO_VERIFY`. `SERVER_HANDOVER.md:19` logs it as shipped work: "One real 32-bit bug fixed
  (broker.c `__uint128_t`)". `REMAINING_WORK_MAP.md:61` likewise.
- **Actual:** 0/18 symbols live — **the largest dead symbol set in the group.** Only caller is
  `pirate_apps.c:62,159-168`, itself 0-live. No superseder: `mesh_token` is partly live but does token
  settlement, not CID listings; nothing else defines `broker_list`/`broker_settle`/`broker_get`.
- **Fix:** give it a reachable caller (a shell MARKETPLACE space or a boot self-check), or drop it
  from `KERNEL_SRCS` and stop citing it as shipped in the handover.

### 4.15 `kernel/src/subterm/subterm.c` — 212 lines

- **Claim:** `ARCHITECTURE_WHITEPAPER.md:194`, in the same sentence as two modules that **are** live:
  "`pterm` is a freestanding console multiplexer… `pterm_mux` weights sub-terminals… `subterm` adds a
  subterminal tree where `subterm_spawn_synced()` groups share one integer clock". Also `:21` names
  "subterminal sync" as one of the system's three phase-tick clocks.
- **Actual:** 0/12 symbols live; zero callers. The layer it extends is live and initialized at boot —
  `pterm_init(&g_pterm)` at `kernel_main_arm64.c:2665` (**T 0x400d08c0**), `pmux_init(&g_pmux)` at
  `:2670` (**T 0x400d1a40**), `pmux_spawn` at `:3465`, `pmux_phase_tick` **T 0x400d1ce0**. Two of the
  three modules in that whitepaper sentence are in the binary; the third is not. Not redundant, and
  the source says so: `subterm.h:22-25` reuses pterm's concepts "without re-implementing either".
  Verified against `pterm_mux.h:53-74` — `pmux_sub_t` carries state/weight/focus only, with no
  parent, no group id, no shared `group_tick`, no per-node output ring; `subterm_tree_t`
  (`subterm.h:65-78`) carries all four.
- **Fix:** **cheapest wiring job in the entire set — `subterm.o` has ZERO undefined symbols.** Add a
  static `subterm_tree_t` plus `subterm_init()` immediately after `pmux_init` at `:2670`, and a
  `subterm_tick()` on the group clock from `kernel_event_cycle_run()`. Otherwise remove the subterm
  clause at `:194` and the "subterminal sync" phrase at `:21`.

### 4.16 `kernel/src/pirate_apps/pirate_apps.c` — 200 lines

- **Claim:** `SERVER_HANDOVER.md:82`; `ARCHITECTURE_WHITEPAPER.md:212` describes `app_status`
  reporting UNAVAILABLE and forwarding side-effects verbatim.
- **Actual:** 0/18 symbols live, **zero callers tree-wide** — the apex of a dead island: all four
  capability modules it borrows (broker, vino_stores, finance_markets, social_spaces) are themselves
  0-live. Considered REDUNDANT against `zxv_shell.c` (fully live, same architectural role: a thin
  facade dispatching into economy subsystems via `space_action`) and **not** ruled so, because
  coverage differs — zxv_shell wraps vino/concord/reputation/logistics/art and has no equivalent of
  `pirate_apps.c:112-139`'s UNBOUND/UNAVAILABLE/AVAILABLE honesty layer.
- **Fix:** pick one facade. Either bind `app_ctx_t` into a shell space (which would also revive its
  four backends — 4.6, 4.8, 4.12, 4.14 collapse into this one decision), or delete pirate_apps and
  remove it from `SERVER_HANDOVER.md:82`'s boot list.

**Subtotal defects: 3,359 lines / 16 files. Adversarially confirmed: 5 files / 906 lines
(4.1, 4.5, 4.7, 4.8, 4.9).**

---

## 5. Downgraded — verdicts that did not survive

Knowing what is *not* a problem saves effort. Three files were filed as SHOULD-BE-RUNNING and
refuted.

### `axiom_matrix_core.c` (94 lines) — SHOULD-BE-RUNNING → **STAGED**

All three claims refuted. (1) The `kernel_main_arm64.c:48` include is **inert**: `grep -n axiom` over
that 2,700-line file returns only line 48 plus four unrelated prose hits — zero uses of any
`axiom_matrix_*` symbol, zero declarations of `axiom_matrix_t`. It sits inside the
`/* M5 subsystems (shared with x86) */` block (`:41-49`) copy-pasted from the legacy
`kernel/boot/kernel_main.c` include list. (2) The **boot banner refutes it**: `kernel_main_arm64.c`
prints `[INITIALIZED]` for RMAG (`:769`), LPRES (`:771`), IPHASE (`:773`), CHOICE (`:775`), OSEQ
(`:779`) and announces **nothing** for Axiom Matrix. (3) The `e8.c:363` comment is **stale doc, not
logic** — `e8_selfcheck` is reachable for its own reason (`kernel_main_arm64.c:2170` calls it
directly); no e8 path touches axiom_matrix, and the missing memoisation costs one redundant
`static bool done`, not wrong behaviour. Also: the string `axiom_matrix` occurs **0 times in
`kernel_arm64.elf` AND 0 times in `kernel_x86_64.elf`** — no built kernel on any arch contains it.
`LOGIC_ALGEBRA_MAP.md:194-196` documents it as having "zero algebra — no add, no multiply, no
compose" and `axiom_matrix_project_tick` as ignoring "its tick argument entirely" — an unfinished
primitive. Remaining action is two lines of hygiene: drop the dead include at `:48`, correct the
stale comment at `e8.c:363`.

### `zbios.c` (159 lines) — SHOULD-BE-RUNNING → **STAGED**

The verdict rested on a single leg, `ARCHITECTURE_WHITEPAPER.md:192`, and that leg does not bear
weight. All three non-doc channels are empty: `grep -niE 'zbios|ten.key|measured boot|chain of
trust|attestation'` over `kernel_main_arm64.c` returns **zero**; `zbios.h` is included only by
`zbios.c:2` and `test_zbios.c:10`, not by kernel_main; no reachable module mentions it. And the doc
leg is refuted by its own company — the **same** whitepaper section describes in the **same** present
tense at least seven other confirmed-dead modules (`orbital_compat ¶184`, `lightningrod ¶186`,
`xedit ¶194`, `subterm ¶194`, `fusion ¶196`, `pungent ¶196`, `shimmer ¶204`, the last called "the
house's key architectural exemplar"). The whitepaper is a **module-capability catalog, not a
boot-path manifest**; admitting ¶192 as evidence would promote ~8 of the 58 on identical grounds and
drain the bucket of discriminating power. `ROADMAP.md:15` cuts the same way — its literal words are
"Built this program (all tested, freestanding)", satisfied exactly by the host test at
`kernel/Makefile:537-539`. ¶192's grammar never asserts the kernel boots through zbios; every clause's
subject is the API. The architectural observation stands and is worth recording: **every stage zbios
would sequence is already running, unsequenced and unbound** — `sha256` **T 0x400c33c0**,
`ab_init`/`ab_stage_update`/`ab_rollback`, `zsp_verify` + `ed25519_verify`, `cell_fabric_admit`,
`zab_derive_capabilities`, `arm64_mmu_init`. Only the hash chain that binds them is missing. That
shows zbios *would* be valuable, not that anything expects it. (Citation slip corrected:
`boot_evidence_final` is at `kernel_main_arm64.c:2743`, not `:2714`.) What would flip it back: a
`[BOOT]` line announcing a measured boot, an `#include` in kernel_main, or a **release/security** doc
asserting the shipped kernel fails closed on a bad measurement.

### `alloc.c` (248 lines) — SHOULD-BE-RUNNING → **STAGED** ⚠️ conflict

`grep -n "alloc" kernel/arch/arm64/kernel_main_arm64.c` returns **empty** — no include, no banner, no
mention. Repo-wide grep for `alloc_pool|ptoken|alloc_distribute|alloc_sustainable|alloc_contribute`
outside `kernel/src/alloc/` returns exactly one line, in a `.md`. The `economy_layer.mk` "builds +
boots them" self-check claim is **false**: `boot_features.c:37-88` `boot_economy_init` exercises
onepolicy, zcapital, crown, ministry, ipfs and prints "/5 self-checked"; alloc appears nowhere in the
file. And the doc claim does not discriminate — `ARCHITECTURE_WHITEPAPER.md:120`'s **same sentence**
makes identical present-tense claims for `abacus`/`smaug_clear`, `iso20022` and `finance_markets`;
`grep -E "abacus|smaug|iso20022"` over the ELF = **zero**, `grep " fm_"` = **zero**. Three of the five
capabilities in that one sentence are as unreachable as alloc.

**⚠️ Unresolved conflict, recorded rather than smoothed over.** `alloc.c` was triaged twice under two
slightly different evidence sets. One pass confirmed SHOULD-BE-RUNNING on the whitepaper:120 leg; the
other refuted it by testing that same leg against `boot_economy_init` and against its three sibling
claims. **This document files it as STAGED**, because the refutation is the more specific
measurement — but the confirming pass is on record and a reviewer may reasonably reinstate it. Either
way the same two actions apply, and they are not bucket-dependent: extend `boot_economy_init` to /6
with a real alloc self-check (two-contributor `alloc_contribute` + `alloc_distribute` summing exactly
to the pool; `alloc_sustainable_ok` admitting 11% and refusing 12%; `ptoken_mint` then a repeat
`ptoken_transfer` returning `-PTOKEN_ERR_SPENT`), **or** reword whitepaper:120 from present-tense
capability to "implemented, not yet wired into boot" and fix the false comment in
`build_system/economy_layer.mk`. Nothing blocks the link — all four undefs (`op_symbiotic_ok`,
`zcap_exchange`, `sha256`, `__divti3`) are live. Closest superseder candidate tested and rejected:
`lex_rhodia_general_average` (`interspace/lex_rhodia.c:24-52`) is structurally the mirror of
`alloc_distribute` but is itself 0-live.

**Also downgraded implicitly:** `crit_168_word.c`'s DFT half (`crit_transform`/`crit_inverse`) — the
byte-level half is a defect (4.9), the DFT pair has no claimant at all.

**Stale-address note:** ELF offsets across this document drift by one rebuild (e.g. `vino_init`
0x400c0200 vs 0x400c0300, `cc_init` 0x400c24e0 vs 0x400c25e0, `mesh_token_init` 0x400c22e0 vs
0x400c23e0). **Symbol names are correct; offsets are indicative only.** Re-run
`aarch64-linux-gnu-nm --defined-only --format=posix kernel_arm64.elf` before acting on any address.

---

## 6. The number that matters

| Bucket | Files | Lines |
|---|---:|---:|
| REDUNDANT — deletable | 5 | **1,790** |
| STAGED — keep, unexpressed | 7 | **1,765** |
| SHOULD-BE-RUNNING — real defects | 16 | **3,359** |
| **Adjudicated total** | **28** | **6,914** |
| *Unadjudicated remainder of the 58* | *30* | *not measured* |

**1,790 lines of surplus vs. 3,359 lines of broken promise — a 1 : 1.9 ratio.** This is not a
codebase with too much code. It is a codebase whose documentation, boot banners, release gates and
on-screen UI assert capabilities the linker discarded. Deleting all five redundant files improves
nothing a user can observe. Wiring the sixteen changes what the machine actually does.

Three sub-figures sharpen it:

1. **906 lines / 5 files survived adversarial challenge unchanged** (§4.1, 4.5, 4.7, 4.8, 4.9). These
   are the defects to fix first — they were attacked and held.
2. **Falsified security or legal claims: 1,233 lines** — `aes256_gcm` 295 + `tls/handshake` 445 +
   `tls/record` 136 + `license` 357. Each is a written guarantee (encrypted vault, fail-closed cert
   verification, four-instrument license conveyance) with nothing behind it in the binary. One release
   gate (`verify_banners.sh:44`) actively **certifies an absent cipher** by substring-matching a BSS
   struct. Fix the gate before anything else — a gate that lies is worse than no gate.
3. **Four of the sixteen defects (vino_stores, finance_markets, minister, flagstate) are visible to a
   user right now.** The ELF contains the strings `COVERAGE 1.8X  SOLVENT`, `846 DEBIT`, `999 EQUITY`,
   `QUOTE`, `BOOK`, `MINISTER`, `FLAG-STATE`, `FEDERATE`. Every one of them is painted by live shell
   code and backed by nothing.

Rough effort ordering, by dependency rather than by size: `license` is one line; `subterm` has **zero
undefined symbols**; `flagstate` needs one already-live dependency. `pirate_apps` is a single decision
that resolves four defects at once. `tls/record` resolves for free the moment `tls/handshake` is wired.

---

## 7. What the game-driven harness should watch for

The Game Master ROM fault-stress harness runs a real emulated workload against the live kernel. That
makes it an **empirical arbiter** for a subset of these buckets — it can confirm or refute an
assignment by observation rather than by symbol table. Which files it can rule on, and which it
cannot:

**The harness can CONFIRM these as defects (a real workload reaches for them and finds nothing):**

- **`aes256_gcm.c` (4.1)** — save-state / vault persistence is the first thing a ROM workload wants.
  Watch: does anything ever cause `robin-vault: entries=` at `kernel_main_arm64.c:3049-3055` to leave
  zero? If it stays 0 under sustained play, the vault is decorative and the defect is confirmed
  behaviourally, not just by `nm`.
- **`tls/handshake.c` + `tls/record.c` (4.2, 4.3)** — netplay is networked by construction
  (`netplay.c` adapts modern networked play into the P2P model). Any attempt at an authenticated
  session will fail to find a TLS client. Watch for a plaintext path being silently taken.
- **`subterm.c` (4.15)** — a multi-console workload is exactly the phase-tick multi-terminal case.
  If two emulated sessions need a shared integer clock and `pmux` alone cannot express parent/group,
  the harness demonstrates the gap `pterm_mux.h:53-74` vs `subterm.h:65-78` predicts.
- **`crit_168_word.c` (4.9)** — peer IDs cross the wire in netplay. Watch for endianness mismatch
  between two nodes: that is the precise failure the canonical-form module exists to prevent, and
  live code is currently doing byte order per call site.
- **`broker.c` (4.14)** — ROM/asset distribution by CID is the harness's own supply chain. If a ROM
  is fetched without `ipfs_get_verify`, the fail-closed `BROKER_ERR_NO_VERIFY` claim at
  whitepaper:174 is refutable in one run.
- **`mm.c` (REDUNDANT)** — sustained emulation is the heaviest allocator load available. If
  `arm64_mmu_init` + `user_page_pool` + `fs_malloc` carry it without fault, the REDUNDANT verdict is
  empirically closed rather than argued.

**The harness can REFUTE (promote out of STAGED) if it observes them being reached:**

- **`identity.c`** — only under `KERNEL_SIM_DEVICES=1`. Run the harness both ways; if the `=0` build
  ever needs it, the config gate is wrong.
- **`alloc.c`** — the conflicted verdict in §5. A workload that contributes and redistributes
  resources between emulated participants is the direct test. If nothing ever needs conserved
  proportional redistribution, STAGED is correct and the conflict resolves empirically.
- **`zbios.c`** — if the harness ever needs to prove *ordering* between the boot verifications that
  already run (`sha256`, `ab_rollback`, `zsp_verify`, `cell_fabric_admit`, `zab_derive_capabilities`,
  `arm64_mmu_init`), the unordered-set problem stops being theoretical.

**The harness CANNOT rule on these — do not wait on it:**

`iso20022`, `vino_stores`, `finance_markets`, `social_spaces`, `minister`, `flagstate`, `federation`,
`sovereign_node`, `pungent`, `decent`, `license`. A ROM workload exercises none of the settlement,
governance, privacy-routing or licensing surfaces. These must be adjudicated by the doc/banner/UI
evidence in §4 and §3 alone.

**One harness change is worth making regardless of buckets:** the current banner gate
(`build_system/verify_banners.sh`) checks a *substring of a symbol name*, which is how an absent AES
cipher passes as "present 2 syms". Every check in that script should be anchored to a **function**
symbol (`T`), never a data symbol, and the arm64 block at `:39-52` should be extended with
`tls_client`, `tls_record`, `aes256_gcm`, `iso20022_` and `vino_stores_`. That single fix converts
five of the defects above from prose findings into build failures.

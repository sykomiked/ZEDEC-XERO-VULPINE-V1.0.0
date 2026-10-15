<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Architecture audit, 2026-10-15

This is the record of a full audit of the architecture documents against the code and the
decisions made up to 2026-10-15. It lists the gaps found, the plan to fill them, and what this
change filled. The results are in [ARCHITECTURE.md](ARCHITECTURE.md) (v5.0) and
[SYSTEM_REFERENCE.md](SYSTEM_REFERENCE.md).

Scope rule (owner, 2026-10-15): add nothing new unless it is a missing component; consolidate
cleanly. This audit therefore writes no new design. It brings the docs up to what exists, merges
duplicates, and fills only real gaps.

## 1. Method

1. **Baseline.** Merge batch #156 (`7d4297c2`) on top of `main` (`4bea0160`), plus the 21 doc
   corrections of `eabcd8fb` (thread "Your long message"), which this change builds on.
2. **Module inventory.** Every directory under `kernel/src/` (261) was matched against its stratum
   in `build_system/strata.txt`, its tests and Makefile targets, `verify-all`, the image Makefiles,
   `kernel/arch/hosted/app_sources.sh` and `build_zxvd.sh`, and every doc that names it.
3. **Per-module reading.** Each module with no entry in SYSTEM_REFERENCE was read (header, code,
   tests, callers) and an entry written in the existing format: status, what it does, entry points,
   tests, users, gaps.
4. **Platform layer.** Kernel images, discs, packages, release workflow, zxvd methods, libzxv ABI,
   SDKs, mobile, CI, checked against the build scripts and workflows.
5. **Every doc.** 149 Markdown files classified as current, stale, superseded or duplicate; about
   507 relative links checked (files and GitHub anchors); renamed or missing module references
   listed.
6. **Decisions.** The project's recorded decisions of 2026-10-09 to 2026-10-15 were checked
   against the docs, so that each design of 2026-10-15 is described and labelled honestly (built,
   draft pull request, local commit, or designed only).

## 2. Results in numbers

| Measure | Before | After |
|---|---|---|
| Modules with a SYSTEM_REFERENCE entry | 210 (dated 2026-10-10) | 268, plus 9 on draft branches (Part C) |
| Modules documented nowhere (no dedicated doc, no entry, not in ARCHITECTURE) | 7: policy, fxepoch, zstats, aproto, vderiv, cred, hwkey | 0 |
| Architecture documents that claimed to be current | 16 (root and docs/) | 2 masters; the rest carry a superseded pointer |
| Broken relative links | 4 | 3, all in the superseded SERVER_HANDOVER.md, whose banner says the files are not in this repository |
| Docs naming the old peer-to-peer name instead of Vinea | 5 | 0 outside the glossary entry that explains the rename and superseded files |

## 3. Gaps found

### 3.1 Documentation gaps

- The master doc (`docs/ARCHITECTURE.md` v4.x) predated merge batch #156 and the designs of
  2026-10-14 and 2026-10-15. It did not cover the hardware layers L0-L7, the four surfaces
  (desktop app, zxvd/libzxv/SDKs, bare metal, phones), how a golden disc is built, or the gap to a
  released golden image.
- 56 merged modules had no SYSTEM_REFERENCE entry, among them the whole V-Bill stack (vauction,
  svault, vbond, vswap, vderiv, zenny), sicily, settle, seats, terms, timesvc, zui, bootattest,
  crash, airc, aipi, aproto, bearer, acct, juris, capctl, kycwrap and taxhook.
- The 2026-10-15 designs (Tiamat Productive Agency AI and the swarm model lineup, naval ports and
  account pegs, the Vino redemption router, the bounty board, the Crown Allotment, token yield,
  the user-evolved OS, voice, one visual system, ZXVA vector audio) were recorded only in project
  notes and draft pull requests.
- Sixteen overlapping architecture files (root and docs/) each presented themselves as current.

### 3.2 Where the code and the design disagree

These are recorded, not fixed here; each is a code change for the thread that owns the area.

| Finding | Where | Effect |
|---|---|---|
| The jurisdiction host has no verify hook, so no signed plug-in can register | `kernel/src/juris` | capctl, kycwrap and taxhook profiles cannot be loaded at run time |
| AIPI over Vinea is linked but never switched on | `zxv_net_attach_aipi` has no caller outside its test | AIPI runs on one node only |
| zxvd estate and grants sign with Ed25519 where the modules specify ML-DSA | `zxvd_estate.c` and grants glue | not post-quantum on that path |
| A passkey seat unlocks on the caller's own flag | `seats` / zxvd seats glue | the check must come from the authenticator, not the caller |
| `player.card` reports bond and giving as 0 | zxvd player.card | the card under-reports until wired to vbond and grants |
| `dispute` draws with a SHA-256 stand-in, not a VRF | `kernel/src/dispute` | the draw is not publicly verifiable yet |
| Rule R15 says guardians hold no key shares, but `ident` gives each guardian a Shamir share of factor G | `kernel/src/ident` | owner decision, ARCHITECTURE section 20 |
| lingua's README says it is in libzxv; it is not | `kernel/src/lingua/README.md` | doc fix with the lingua thread |
| `legal_engine` strings still cite SEL-3.3 | `kernel/src/legal_engine` | rename to zxv_policy wording pending |

### 3.3 Consolidation register: finished work not yet on `main`

Commits pushed or held by threads that merge batch #156 did not carry. They belong in the next
batch so nothing is lost.

| Source | Commits | What |
|---|---|---|
| token yield thread (3cts9k) | three commits | verified-work-per-token measurement |
| bare-metal thread (2ppu3g) | `36dff4ca`, `f8964d65`, `e6f8dbd2` | x86 buffers onto map memory, arm64 handoff v2, loader refuses unsigned |
| Native GUI thread (98r3n6) | `530f9fc4` | GUI fixes |
| py0q7n | `87f0b346` | held after #156 |
| 95h6ux | `8cf2ef09` | held after #156 |
| adoring-volta | three commits | held after #156 |
| draft #153, #154, #155 | branch heads | voice, bounty board, user-evolved OS (Part C of SYSTEM_REFERENCE) |
| #129, #140, #141 | open pull requests | left out of #156 because they conflict; #140 (ACPI) lands after #133's follow-ups |

### 3.4 Shipping and the golden disc

Seven gaps stand between today's build and released golden dist images; they are listed in
ARCHITECTURE section 5.2: no ISO wrapper in any workflow, the golden disc is never released
(weights as a release asset need the owner's call), it boots to a self-test rather than a desktop,
it is unsigned with no installer or recovery boot on x86_64, the brand and icon assets are not in
the tree, two Linux/Windows engines are built, and release signing exists but `release.yml` does
not call it.

Two release scripts were broken by stale files and are fixed in this change:
`build_system/mk_deliverable_zip.sh` aborted copying a `PROVENANCE/` folder that does not exist,
and `build_system/make_release.sh` shipped the superseded CANONICAL.md and ROADMAP.md.

### 3.5 Stale names and references

- The old peer-to-peer working name, which is another product's name, remained in WEB4, IPFS_NODE,
  VRAM_BLOCKS and MOBILE_AND_DEVICES.
- README.md and CONTRIBUTING.md sent readers to the superseded root ARCHITECTURE.md,
  SOURCE_CODE_NAVIGATION_GUIDE.md and SUBSYSTEM_INDEX.md.
- Smaller stale claims, left for the owning threads: POWER.md says zxvd power is wired "on
  #109" (it is in the tree); `baremetal-gaps/kernel_core.md` lists the PMM as missing (built in
  #133); BAREMETAL_OS_GAPS says crash and timesvc are not linked (arm64 links them); COVERAGE.md
  needs regenerating; GOOD_FIRST_ISSUE.md lists issues already done and target files that do not
  exist; `sdk/docs/STABILITY.md` cites an ABI 1.2.0 snapshot that was never taken;
  `DEVELOPMENT_MAP.yaml` does not parse as YAML.

### 3.6 Owner decisions surfaced

- SOURCE_CODE_NAVIGATION_GUIDE.md is marked confidential and addressed to an outside company's
  engineering team, yet it sits in the public repository. Keep, rewrite or remove.
- Weights as release assets or a mirror for the golden disc (`release.yml` refuses them by rule).
- R15 and guardian key shares (3.2).
- The other open decisions are listed in ARCHITECTURE section 20.

## 4. Fill plan, and what this change did

| # | Gap | Fill | Status |
|---|---|---|---|
| 1 | Master doc out of date | Rewrite `docs/ARCHITECTURE.md` as v5.0: rules R1-R24 (R24 added the same day), strata and hardware layers, the four surfaces, build and ship with the golden-disc gaps, per-stratum status, the 2026-10-15 designs with honest labels, order of work, decisions made and open, document map | done |
| 2 | 56 modules undocumented | Entries in SYSTEM_REFERENCE Part B; 9 draft-branch modules in Part C; summary table and totals updated | done |
| 3 | Wrong statements in SYSTEM_REFERENCE | Desktop app links Vinea LAN discovery and the IPFS node; social, call, xlate and devmesh are in zxvd, not "nowhere"; Q5_0/Q5_1 load (test_zt_q5); a real model has run | done |
| 4 | Sixteen competing architecture files | Superseded pointer at the top of each; README and CONTRIBUTING point at the two masters | done |
| 5 | Old peer-to-peer name | Replaced by Vinea in the four current docs | done |
| 6 | Broken release scripts | `mk_deliverable_zip.sh` and `make_release.sh` ship the masters instead of missing or stale files | done |
| 7 | Code and design divergences (3.2) | Code changes by the owning threads | open |
| 8 | Unlanded commits (3.3) | Next merge batch, run by the Faster CI thread | open |
| 9 | Golden dist images (3.4) | ARCHITECTURE section 18, steps 5, 8 and 13 | open |
| 10 | Smaller stale claims (3.5) | Owning threads, or a later doc sweep | open |
| 11 | Owner decisions (3.6) | The owner | open |

Superseded files were kept, not deleted, so links from outside the repository keep working; they
can be moved to an archive once the owner agrees.

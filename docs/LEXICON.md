# The lexicon: every name on the floor and what it is in code

The design documents use one vocabulary for the system's parts. This page
says what each name is, where it lives in the repository, and what is built.

| Name | What it is | Design | Code it builds on | State |
|---|---|---|---|---|
| **The Firmament** | The signed catalogue of everything that comes from above: base model weights, adapters, compiled specialists, and the fixed rules (the four-valued lattice, exact arithmetic) | This page, §1 | `build_system/golden.models` (pinned SHA-256 and size of each model), `kernel/arch/efi_models.h` (re-checked at boot), `docs/AI_MODEL.md` | Model pinning built; catalogue not built |
| **The Pit Boss** | Supervisor: a deterministic rulebook that enforces, and an advisor model that only proposes | `docs/PIT_BOSS.md` | `kernel/src/reputation`, `swarm_budget`, `cotier/ct_budget` | Design |
| **The Registrar** | Burrow's second layer: custody, provenance and audit of files | `docs/REGISTRAR.md` | `kernel/arch/hosted/zxv_files.h` (Burrow's files layer) | Design |
| **The Tally** | The store's hash-chained journal | `docs/REGISTRAR.md` §2 | The construction of `lpx_qlog` and zxsh's history | Design |
| **The Cage** | Quarantine and clearance for anything from outside | `docs/CAGE.md` | Immigration, Z-Wasm, `kernel/src/legacy`, AIPI, `cotier` | Design over built parts |
| **The Desks** | The Cage's per-kind checking stations (Raw Materials, Archives, Protocol, Arbitration, Operations, Registry) | `docs/CAGE.md` §5 | As the Cage | Design |
| **The Floor** | The 407-slot registry: L1 Foundation (the 144 apps), L2 Concourse, L3 Apparatus | `docs/REGISTRY.md` | `kernel/src/launch/zxl_apps.inc`, `sdk/pillars` | Design |
| **The Strongbox** | The working store: the user's files, encrypted at rest | This page, §2 | `zxv_files.h` (one root, no links, whole writes, Trash) | Files layer built; encryption at rest not built |
| **The Hoard** | The cold archive: sealed, immutable snapshots of settled state | This page, §3 | The Tally, Vinea CIDs (`vna_cid.h`) | Design |
| **Trade routes** | Typed one-way channels between ports, desks, stores and peers | `docs/TRADE_ROUTES.md` | Vinea messages, `lpres` state codes | Design |
| **The Rosetta Shell** | One shell for POSIX, PowerShell and cmd.exe lines, via the CLI-IR | `docs/ROSETTA_SHELL.md` | `tools/zxsh` | Design over built zxsh |

## 1. The Firmament

The Firmament holds the system's fixed inputs, and nothing there changes in
place. Each entry is:
- a file named by its content hash;
- its licence, which must be Apache-2.0, MIT or BSD for weights the system
  ships;
- its source;
- a signature by the node owner's key.

A new version is a new entry. Pulling an entry down onto the floor goes
through the Cage's Protocol Desk: digest check, format validation, licence
check. Today's `golden.models` is the first, two-entry version of this
catalogue.

## 2. The Strongbox

The Strongbox is the files layer under Burrow and the Registrar, with
encryption at rest added. Its key is held by the owner's identity
(`kernel/src/hwkey`, the chip-sealed account key, where the hardware has
one). Everything the Registrar says about holdings applies to files in the
Strongbox.

## 3. The Hoard

The Hoard is for state that is settled and should outlive the software that
made it.

- **Sealed.** A snapshot enters the Hoard once, with its hash, its Tally
  record and a signature. It is never edited. A later version is a new
  snapshot.
- **Readable in fifty years.** Contents are stored uncompressed or in a
  documented, deterministic encoding. Each snapshot carries a plain-text
  manifest describing its format, so reading it needs no ZXV code.
- **Leaving the Hoard.** Reading a snapshot back onto the floor goes through
  the Registrar and the Cage like any other intake. Its hash is checked
  against the seal, so a damaged archive shows FALSE instead of being
  trusted.
- **Copies.** The Hoard can keep replicas on other disks or peers, by CID. A
  replica that disagrees with the seal is BOTH, and the sealed hash decides.

## 4. Names and the law

The names were chosen to avoid regulated financial words. Two facts limit
what names alone can do, and both belong with counsel:

- Regulation follows what the software does. The repository already
  contains a currency module that mints and keeps reserves
  (`kernel/src/count_house`, `kernel/src/mint`), payments, cards, escrow, a
  `broker` module and capital-market modules. Their exposure under
  money-transmission, securities or banking law depends on how they are
  operated, not on what the file manager is called.
- The statutes cited in the design discussion should be checked by a
  lawyer before they are relied on. For example, 18 U.S.C. §§ 485–492
  concern counterfeiting coins and related offences, not the use of a word
  in software. The federal limits on financial names are narrower than
  "never say bank" (see 18 U.S.C. § 709), and state laws govern the word
  "bank" in business names.

This page is engineering documentation, not legal advice.

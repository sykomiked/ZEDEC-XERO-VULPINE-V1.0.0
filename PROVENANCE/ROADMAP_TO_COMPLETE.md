# ZXV / ZEDEC pqOS — what remains, ordered

_State of play, and the work to finish. Every number here was measured, not
estimated. Supersedes the sequencing in SYSTEM_MAP.md §6._

---

## 0. Where the project actually is

| | |
|---|---|
| architectures booting | **1 verified (arm64)** — see the matrix below; the earlier "5/5" here was my overclaim |
| EFI payloads on real firmware | 2 (BOOTX64, BOOTAA64); one disc boots two arches |
| non-test `.c` in `kernel/src` | 278 |
| **in no kernel image on any arch** | **50** (was 55; `oseq.c` + 4 TLS units linked) |
| dead lines those represent | 20,942 (18% of `kernel/src`) |
| corpus extracted from QAT | 834 videos, ~180k words, 162 slides read |

The single structural fact: **a fifth of the tree is undifferentiated reserve.**
Not dead — staged (see ORGANISM_ARCHITECTURE.md §3) — but a module that is not
in an image has not been shown to work in the system.

### Architecture status — corrected

An earlier revision of this file claimed "5/5 architectures booting". That was
written by carrying forward stale session state without re-checking it against
`SERVER_HANDOVER.md` and `handover.json`, which said otherwise. It was the
flattering half of a repo that contained both an honest status document and an
inaccurate one. Corrected:

| architecture | status | evidence |
|---|---|---|
| **arm64** | **BOOTS** — BOOT_OK, EL0, desktop | `EVIDENCE/arm64_boot_trace.log`; reproduced on this host |
| **x86_64** | boot trace on record, **NOT reproducible here** | `EVIDENCE/x86_64_boot_trace.log` vs `SERVER_HANDOVER.md:104` "compiles, minimal kernel_main"; `handover.json` `COMPILES_KERNEL_ONLY` |
| **riscv64** | compiles; **not linkable on this host** | `handover.json` `UNLINKABLE_LOCALLY` (host clang lacks the RISCV backend) |
| **riscv32** | compiles; **not linkable on this host** | as above |
| **arm32** | compiles; **boot unproven** | `SERVER_HANDOVER.md:107` |

**One architecture is verified booting.** The other four compile to varying
depths and are unproven on this machine. Re-verification belongs on the Linux
build box, not here.

## 1. The blocking defect — fix before anything builds on trit_t

**`trit_t` has six enumerators for five states.** `TRIT_GLUT = 2` is documented
as a legacy alias for `TRIT_GLUT_NEUTRAL = 5`. Consequences, all live:

- marshalling cannot be a bijection, so `MB_FORM_TRIT` is **blocked** in modbind
  and any AMI boundary carrying trits is impossible;
- GF(5) requires exactly 5 states, so the multi-valued algebra cannot be built;
- 59 files already use `TRIT_GLUT`.

**RESOLVED (owner decision): keep `TRIT_GLUT` as a deprecated alias that
canonicalises.** Implemented — `trit_canon()`, `trit_is_canonical()`,
`trit_canon_is_sound()` and `TRIT_CANONICAL_COUNT` in `m5_types.h`; rule is
*accept it anywhere, never emit it*. All 59 call sites unchanged. `MB_FORM_TRIT`
is now gated on `!trit_canon_is_sound()` — evidence, not a promise — so it
re-blocks automatically if a sixth state is ever added without a canonical rule.
GF(5) and items 5–6 are unblocked. Commit `ba852b9`.

## 2. Half-expressed organs — the only state biology does not tolerate

Undifferentiated is fine. **Half-differentiated makes the rest of the system plan
around a capability that is not there.**

1. **TLS — IN THE MAKEFILE, NOT IN THE BINARY.** All five units are in
   `KERNEL_SRCS` on all five arches (commit `8c70a4c`) and their vendor tests
   pass. But after a FORCED FULL REBUILD of arm64, `nm kernel_arm64.elf |
   grep -c tls_` is still **0**. The `.o` files exist with real symbols
   (x25519.o has 3); `--gc-sections` with `-ffunction-sections
   -fdata-sections` discards every one, because **nothing calls them**.

   **Adding a file to KERNEL_SRCS is necessary but NOT sufficient.** The same
   measurement shows 0 symbols for `oseq_dag`, `zphi_`, `e8_`, `mixmat_`,
   `modbind_` and `trit_canon` — every module added today. They compile, they
   link, they test, and they are absent from the OS.

   Open: give each a real caller, then re-run
   `build_system/verify_banners.sh`.
2. **`fs.c`** — its header is included by all 8 `kernel_main`s and **zero of its
   APIs are called**. Advertisement expressed, tissue not.
3. **Per-arch asymmetry** — 13 subsystems exist on arm64 only (`syscall`, `zab`,
   `zab_exec`, `zxvfs_tri`, `display`, `zmedia`, `invproof`, `bombsquad`,
   `mrschema`, `ramfb`, `virtio_bus`, `vring`, `el0_sched`). Either bring them
   across or declare them unexpressed per arch. Silence is the bug.

## 3. Differentiation — the remaining 54, by organ

Assignments in ORGANISM_ARCHITECTURE.md §10. Three organs have **zero** expressed
tissue and are the real gaps:

- **language faculty** — `sutra/*` (7 files, ~989 lines): lexer, parser, runtime,
  chiglet binding, rails, capital, selfaudit. The scripting surface is written
  and has never run in the OS.
- **autonomic regulation** — `dharma/*` (10 files, 798 lines): toroidal feedback,
  chakra spinal nodes, karma causal events. Pairs naturally with the bomb squad.
- **proprioception** — `rce_units.c` (379 lines): SI units and dimensional
  analysis. *This one is now load-bearing:* the QAT extraction found equations
  printed with inconsistent dimensions (`F = Gm₁m₂/4πr²` beside the standard
  form; `M = rt²`). A physics engine without dimensional checking will inherit
  those silently.

Also: `ubh.c` (Universal Binary Harmonization, format registry) is the digestion
organ I twice called "missing" while it sat in the reserve.

## 4. Composition — make disconnection impossible, not merely visible

`modbind` is landed: modules declare emitted/ingested forms and `modbind_verify`
reports ORPHAN / STARVED / NO_PORTS / VERSION / NO_XFORM. **It is not yet wired
into the build or into any real module.** Two steps:

1. give every linked module a `module_transform_t` declaration;
2. make `modbind_verify() != 0` a **build failure**, not a boot warning.

Until step 2, this is documentation. Its value was already demonstrated: the
`oseq` version-skew (0 valid vs 0 null) is exactly the case it detects, and it
appeared in real code within hours.

## 5. The algebra — from LOGIC_ALGEBRA_MAP.md §7, unchanged and still owed

In dependency order: `rat_one()` → canonicalise `TRIT_GLUT` (§1) →
`trit_meet`/`trit_join` (the 5-chain is already proven at `congruence.c:85`, so
associativity is free and neither function exists) → exhaustive law-checker
validated against `poly_t` → repair `dvaitadvaita_resolve` (not associative, no
identity, **112 live gate instances**) → migrate `cyc13_t` off the unsafe
`rational_t` onto `rat_t` → matrix engine with nesting + star closure.

`mixmat` supplies the last item's operator class: row-stochastic matrices over
exact rationals are closed under composition and have a computable fixed point.

## 6. The physics engine — QAT, per QAT_PHYSICS_EXTRACTION.md

Build the **constructions**, not the constants.

- `oseq` differentiated ✅ — local causal order, no global clock.
- **Next:** absorption/emission as the a:b golden section — a real pairing rule
  for S+/S−, constructible and testable.
- Then: drive the reality layer from coupling *events* rather than a tick loop.
- **Never encode:** golden angle as α (0.344% off, bridged only by the word
  "approximation"); `ΔE·Δt ≥ h/2π` (canonically ℏ/2 = h/4π, and his is a
  deliberate 2× convention that carries the whole "time is 2-D" argument);
  `F = Gm₁m₂/4πr²`. Two decks (`NWvjD-mPrxI`, `CZvoI0iLvdY`) are AI-generated
  accretions, one containing `α = 1/4π = 1/137` which is internally false.

**Corpus completion:** 85 of 833 videos processed on the box before I stopped to
fix throughput. 34 of 38 φ/137 videos still unprocessed, including all four
spiral-periodic-table videos — the most direct collision with the owner's own
2022 Sonic Chemistry work.

## 7. Known defects still open

- ~~`decent.c:251` — `decent_did_zk_verify()` hardcodes `zk_verified = true`.~~
  **FIXED** (commit `adee831`): fails closed, returns `DECENT_ENOTIMPL`, never
  sets the flag. The deeper defect was that the signature accepts no proof, so
  no correct implementation of it exists; `decent_did_zk_verify_proof()` is the
  honest interface (also unimplemented, also refuses).
- `smap.c:257` — `smap_reassemble()` never writes `out`, returns positive length
  (uninitialised read).
- `dao_vote()`, `cell_transport_receive()`, `et_process_pending()` — discard data,
  return success.
- `dtmf.c:412` — AX.25 FCS hardcoded `0x0000`.
- `hdcm.c:121` — `hdcm_vector_superpose` ties on byte-index parity; not
  associative or commutative.
- `epu_device.c` — 1,273 lines, densest φ arithmetic in the tree, unlinked.
- `modbind` freestanding build **unverified** (`m5_types.h` → `complex.h` fails
  on a bare cross-target; `congruence.c` shares the dependency and builds under
  the real toolchain — confirm, do not assume).

## 8. Abstraction layers — after the base passes

L2 capability handles → L3 object/resource model → L4 services (bomb squad as
their health monitor) → L5 Sutra bindings → L6 AppKit with ZXI-backed undo →
L7 human surface (settings, file manager, first-run) → L8 remote display via the
triad quality ladder (S+ now, S− on demand), which needs damage tracking and a
surface abstraction first.

## 9. Recommended order

1. `decent_did_zk_verify` — a security predicate that cannot fail (§7)
2. `trit_t` decision — unblocks the entire multi-valued algebra (§1)
3. TLS link-up + bilateral parity — the system claims security it lacks (§2.1)
4. `modbind` enforced at build time — makes §3 self-policing (§4)
5. Differentiate `sutra`, `dharma`, `rce_units` — the three empty organs (§3)
6. Algebra chain (§5), then the physics engine constructions (§6)
7. Per-arch parity, then L2+ (§2.3, §8)

Items 1-3 have landed. Item 4 (truth alignment) is now the gate: the audit
found this very file overclaiming, so no estimate above it is reliable until it
passes.

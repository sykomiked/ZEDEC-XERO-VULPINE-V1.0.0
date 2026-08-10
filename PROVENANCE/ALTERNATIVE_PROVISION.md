# Alternative provision — why nothing gets deleted

_Owner directive: do not delete the redundant modules. Corrects the REDUNDANT
verdicts in RESERVE_TRIAGE.md, which were reasoned under the linear model that
EVENT_SPACE_BRINGUP.md retracted._

---

## 0. The verdict was an artifact of the old model

RESERVE_TRIAGE.md marked five files REDUNDANT — 1,790 lines — each with a
"superseder proven live in the ELF":

| module | lines | claimed superseder |
|---|---|---|
| `mm/mm.c` | 187 | `arm64_mmu_init` |
| `crypto_wallet/crypto_wallet.c` | 760 | `zxvfs_tri` + `zab` |
| `xedit/xedit.c` | 438 | `appkit/doc.c` |
| `audiogenomics_pro/architectural_directives.c` | 243 | `lpres`/`trispace`/`zxvfs_tri` |
| `shimmer/shimmer.c` | 162 | inlined at `kernel_main_arm64.c:330-404` |

That reasoning assumes **one kernel, one implementation per capability**. Under
that assumption a second implementation is dead weight. But we are not building
that system, and the barrier-wave retraction applies here identically: I took a
structure with several valid configurations and collapsed it to one, then called
the survivors redundant.

## 1. Redundancy vs alternative provision

Under `REQUIRES`/`PROVIDES`, two modules providing the same capability is not a
conflict. It is **choice**, resolved per core by which preconditions actually
hold:

```
PROVIDES mm_ready:
    mm.c            REQUIRES nothing        — portable, no MMU assumed
    arm64_mmu.c     REQUIRES arm64_el1      — hardware page tables
```

A heterogeneous multikernel *needs* both. A core with an MMU takes the second; a
core without one — an accelerator, a sensor node, a soft core, a foreign ISA in
the same machine — takes the first and still boots. Deleting `mm.c` would not
remove duplication; it would remove the ability to run anywhere the MMU path does
not apply.

The same reading rescues the rest:

- **`crypto_wallet.c`** derives five keys per trit phase. `zab` thinks in
  capabilities, `zxvfs_tri` in storage triads. Those are three *specialisations*
  of key handling, not three attempts at one. Native inside, canonical at the
  boundary (EVENT_SPACE_BRINGUP.md §3) is exactly this shape.
- **`xedit.c`** carries merkle tracking and multi-buffer; `doc.c` does not. The
  triage already flagged that deletion required porting those first — which is
  the tell that it was never a duplicate.
- **`shimmer.c`** as a module versus 74 inlined lines in `kernel_main` is the
  clearest case: the inline version is the *accident*. A module that can be
  provided, withdrawn and re-provided is the correct form under S−.

## 2. The guard this needs, or it becomes nondeterminism

Alternative provision is sound **only when the alternatives satisfy the same
contract**. Two providers of `mm_ready` that mean different things by "ready"
produce a system whose behaviour depends on which one won — the worst class of
bug, because it is stable per machine and irreproducible across them.

The mechanism already exists and was built for exactly this. `modbind` reports
**VERSION separately from ORPHAN**, on the grounds that they demand different
fixes: an orphan needs a consumer written, a version mismatch needs one side
migrated. Applied here:

```
PROVIDES mm_ready @ contract v1
```

Two providers of the same capability at the same contract version are
interchangeable. At different versions they are a **build failure**, not a
runtime coin-toss. `verify_layers.sh` gains this as its fourth check, alongside
the acyclic requires-graph.

## 3. What actually changes

Nothing is deleted. Instead:

1. Each of the five declares `PROVIDES <capability> @ <version>` and its
   `REQUIRES`.
2. Where two providers exist, both are kept and the contract version is asserted
   equal at build time.
3. Selection is by precondition, per core, at bring-up — no central registry
   choosing a winner, which would reintroduce the global state the multikernel
   exists to avoid.
4. The reachability metric gains a category. A module that is a **standby
   provider** is neither live nor waste; it is provisioned. `measure_reachability.sh`
   should report it as such rather than counting it dark, or the metric will keep
   arguing for deletions the architecture wants.

## 4. What this does not license

Alternative provision is not an excuse to keep everything. The test is whether a
module provides a capability **under conditions the other provider cannot meet**.
Two providers with identical requirements and identical contracts are still
redundancy, and the second one is still deletable.

The five above pass that test: no MMU, trit-phase-native, merkle-tracking,
withdrawable. Each answers a condition its supposed superseder does not.

The honest summary of RESERVE_TRIAGE.md is therefore narrower than it read:
**1,790 lines of alternative provision, and 3,359 lines of broken promise.**
Only the second number is a defect. The first was a reading error, and mine.

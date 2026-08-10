# Layered bring-up — making sequence explicit, and reachability a consequence

_Design. Replaces hand-written boot callers and the implicit ordering buried in
`kernel_main_arm64.c`. Owner directive: build it in layers; new build scripts
are acceptable._

---

## 0. The problem this solves, measured

`kernel/src` defines **2,301 symbols**; **628 are reachable** in
`kernel_arm64.elf`. 1,673 are discarded by `--gc-sections` because nothing
references them.

Two non-fixes, stated so they are not retried:

- **Link order does not affect this.** `--gc-sections` computes reachability
  transitively from the entry roots and discards the rest; the order objects
  appear on the command line does not change the reference graph. Link order
  matters for archive symbol resolution, duplicate precedence and placement —
  not for retention.
- **Hand-writing ~1,600 callers is not engineering.** It would raise the metric
  and lower the truth: boot-time calls that do nothing, and a number that no
  longer distinguishes integration from ceremony. This project has spent a week
  removing exactly that class of thing (self-checks made of literals, a
  φ-coherence test that could only return false, an AEAD round-trip that would
  pass without checking the tag).

## 1. The mechanism

An **ordered initcall table**, the shape Linux uses:

1. A module registers itself into a section named for its layer —
   `.initcall.L2` and so on — via a macro.
2. The **linker script `KEEP()`s those sections.** `KEEP()` is what actually
   defeats `--gc-sections`; it is a linker-script directive, not an ordering.
3. Boot walks the table **in layer order** and invokes each entry.

Reachability stops being something to chase and becomes a consequence of
declaring which layer you belong to.

```c
/* in the module */
ZXV_INITCALL(L2, zxvfs_bringup);

/* expands to a const struct in .initcall.L2 holding {name, fn, layer} */
```

## 2. The layers

A module may depend **only on strictly lower layers**. That is the whole
discipline, and §4 makes it a build failure rather than a convention.

| L | name | contains | why it sits here |
|---|---|---|---|
| **L0** | substrate | `zphi`, `rat`, `surplus`, `e8`, `mixmat`, `crit168` | pure arithmetic; depends on nothing, not even memory |
| **L1** | causality & identity | `oseq`, `identity`, `invproof` | **nothing can be ordered before ordering exists.** Under a multikernel a core cannot accept a message before it can order messages |
| **L2** | memory & storage | `mm`, `alloc`, `zxvfs`, `zxvfs_tri` | needs L1 for causal commit ordering |
| **L3** | trust | `mlkem`, `zab`, `modbind`, `tls`, `decent` | capabilities and crypto need storage for keys and causality for replay defence |
| **L4** | devices | `virtio`, `ramfb`, input, block | needs memory (L2) and capability gating (L3) |
| **L5** | services | `display`, `codec`, `bombsquad`, `chiglet`, `rce_units` | needs devices |
| **L6** | economy & governance | `vino`, ledger, `license`, `iso20022`, `crown` | needs services and trust |
| **L7** | surface | shell, desktop, apps, `sutra` | everything below it |

Two placements worth defending:

- **`oseq` at L1, above nothing but arithmetic.** In a multikernel, causal order
  is not a service — it is the precondition for any cross-core statement being
  meaningful. It is already reachable (9 symbols), so this is recognising where
  it sits rather than moving it.
- **`bombsquad` at L5, not L0.** It monitors margins *of other subsystems*, so it
  cannot precede them. It watches; it is not foundational.

## 3. Failure semantics

Each initcall returns a status. **A failure at layer N aborts layers above N**
and boots degraded rather than pretending.

This is the honest form of what the boot log has been doing informally: today a
banner prints `[INITIALIZED]` whether or not the subsystem works, which is how
the tree came to announce an encrypted vault, TLS and post-quantum key
establishment that were variously absent or garbage-collected out. A layer that
fails must make the layers depending on it *not run*, not merely log.

## 4. The invariant, enforced by the build

> **A module may not include a header from a higher layer.**

Checkable statically: parse each `.c`/`.h` for `#include "..."`, map both files
to layers, fail if `layer(includer) < layer(included)`. That becomes
`build_system/verify_layers.sh`, wired into the link rule beside
`verify_banners.sh` — which already runs there and, when tested with a
deliberate probe, correctly failed the build and deleted the ELF.

This is the piece that makes layering real. Otherwise it is a diagram.

## 5. What this does NOT fix, stated plainly

`KEEP()` retains a section whether or not its initcall does anything useful. The
mechanism guarantees **reachability, not usefulness**. So it must ship with the
discipline already established:

- every initcall does real work and **returns a status** — the ML-KEM KAT, the
  X25519 RFC-7748 vector, the HKDF→AEAD seal/open with a **forged-tag rejection**
  are the reference patterns already in `kernel_main_arm64.c`;
- `verify_banners.sh` continues to gate every claim against `nm`;
- `measure_reachability.sh` reports before/after per conversion.

Without those, this is mass-wiring with better ergonomics.

**And it is not a licence to wire everything.** Several of the 1,673 dead symbols
should be *deleted*: the `*_core.c` duplicates (`oseq`/`oseq_core`,
`iphase`/`iphase_core`, `choice`/`choice_core`, `phase_coord`/`phase_coordinator`)
are two implementations of one thing, where the thinner sibling is linked and the
fuller one is dark. Resolving each to one implementation lowers the symbol count
*and* raises the percentage — the cheapest available gain, and pure subtraction.

## 6. Order of work

1. **Prove the mechanism on five modules**, one per layer. Measure the
   reachability delta. If `KEEP()` plus one ordered table does not move the
   number as predicted, the design is wrong and stops here.
2. `verify_layers.sh` + wire it into the link rule.
3. Convert layer by layer, lowest first, measuring after each.
4. Delete the `*_core.c` duplicates as they are reached.
5. Re-run the ROM campaign — a real workload is the empirical check on which
   layers actually get exercised.

Step 1 is a day. Do not convert the tree before it passes.

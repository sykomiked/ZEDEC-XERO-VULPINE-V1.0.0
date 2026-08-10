# The thirteen layers, crossed with tri-space

_Extends LAYERED_BRINGUP.md from 8 layers to 13, and adds the second axis.
Owner directive: thirteen levels, and the positive / negative / neutral spaces._

---

## 0. Why thirteen, and why it is not arbitrary

13 is this system's own arity and it recurs where the mathematics forced it:
`cyc13_t` (the exact 13-dimensional cyclotomic group ring), the 13 `l13_phase_t`
Sephirot phases, the 13-dimensional lattice, `MIXMAT_MAX 13`. 13 is prime — so
GF(13) exists and the phase space admits a field — and it is F(7), a Fibonacci
number. Both facts were verified, not assumed (see GEOMETRY_VERIFIED.md).

So the bring-up ladder having thirteen rungs is the same number appearing again,
not a number chosen to be interesting.

## 1. The two axes

A layer is **not** a slab of code. It is a row, and every row has three faces:

| face | extension | meaning |
|---|---|---|
| **S+** | `.zxvc` | what it DOES — the forward action |
| **S−** | `.cedez` | the INVERSE — how to undo it, recover from it, roll it back |
| **S0** | `.cedec` | the UNRESOLVED REMAINDER — what could not be decided, quarantined rather than guessed |

13 layers × 3 faces = **39 cells**. That is the actual shape of the architecture.

**This is what makes the design falsifiable.** A layer is not complete because its
S+ works. It is complete when all three faces exist. Right now almost every layer
in this system has an S+ and no S−, which is precisely why "undo" is a per-app
convention instead of a system guarantee, and why a failed update can leave a
machine in a state nothing knows how to reverse.

`zxvfs_tri` already stores triads as the unit and `zxi_verify` already proves an
inverse by *applying* it. The mechanism exists. It has never been made the rule.

## 2. The thirteen

| L | name | S+ does | S− undoes | S0 holds |
|---|---|---|---|---|
| **L0** | substrate | exact arithmetic: `zphi`, `rat`, `e8`, `mixmat` | — (pure functions have no side effect to undo) | overflow/invalid: `rat_t.valid=false` — already built |
| **L1** | causality & identity | `oseq` ordering, `invproof` witnesses | reject/retract an event | events whose order cannot be decided |
| **L2** | memory & storage | `mm`, `alloc`, `zxvfs` copy-on-write | the old extent, still intact until commit | torn/partial writes → fsck oracle |
| **L3** | trust | `mlkem`, `zab` capabilities, `tls` | revoke a capability, roll back an epoch | unverified proofs — quarantine, never assume |
| **L4** | devices | virtio, framebuffer, input | detach, reset, restore prior mode | device present but unclaimed |
| **L5** | services | display, codec, `bombsquad`, `rce_units` | restore prior service state | degraded service, margin ARMED |
| **L6** | economy | ledger, `vino`, `license` | the reversing entry — accounting already demands it | unsettled / in-flight value |
| **L7** | surface | shell, desktop, apps, `sutra` | **undo, backed by ZXI witnesses** | uncommitted user intent |
| **L8** | distribution | remote surface, P2P mesh, netplay | disconnect and reconcile | partition — the split-brain remainder |
| **L9** | composition | program fusion, `zxpkg`, orbital compat | uninstall, unfuse, downgrade | version conflicts held unresolved |
| **L10** | intelligence | Chiglet companion, schema learning | forget / unlearn a model epoch | low-confidence inference — **must not act** |
| **L11** | sovereignty | crown, ministry, concord, policy | repeal, amend, pardon | disputes under adjudication |
| **L12** | commons | inter-system treaty, federation, the space between | withdraw from a treaty | claims between jurisdictions |

Layer rule unchanged and now enforced across all thirteen: **a module may depend
only on strictly lower layers.**

## 3. What the second axis exposes immediately

Reading down the S− column is the most useful thing this table does. Almost every
entry is *missing*, and each absence is a specific, nameable defect:

- **L3 has no revocation.** A capability once granted cannot be withdrawn.
- **L7 has no system undo.** ZXI witnesses exist and prove inverses; nothing above
  them turns that into a user-facing guarantee.
- **L9 has no uninstall.** Programs fuse; nothing unfuses them.
- **L10 has no unlearn.** A model epoch that ingested something wrong is permanent.
- **L12 has no withdrawal.** Treaties are enterable and not exitable.

An S0 audit is equally sharp. S0 is where a system says *"I could not decide."*
Its absence forces guessing, and guessing at a boundary is how a security
predicate ends up always returning true. L10's S0 is the load-bearing one: an
inference below confidence must be **held**, not acted on.

## 4. The file extensions carry it

`.zxvc` / `.cedez` / `.cedec` are not three formats. They are one object with
three faces, which is why `zxvfs_tri` writes the descriptor **last** — until all
three members are on disk, the triad does not exist. That property is already
proven by exhaustive crash injection.

Extending it: **any artifact at any layer is a triad.** A package is what it
installs (`.zxvc`), how to uninstall it (`.cedez`), and what it could not
reconcile (`.cedec`). A model epoch, a treaty, a ledger entry — same shape. That
is the concrete meaning of "positive, negative and neutral space" as an
architecture rather than a slogan.

## 5. How this changes the build

`verify_layers.sh` (the DRC from SILICON_METHODOLOGY.md) gains a second check:

1. **Layer rule** — no include from a higher layer. *(as designed)*
2. **Triad rule** — a module that declares an S+ initcall must declare, or
   explicitly waive with a reason, its S− and S0. A waiver is legitimate (L0 is
   pure; there is nothing to undo) but it must be **written down and checked**,
   not silently absent.

That second check is what stops the S− column from staying empty. Every gap in §3
becomes a build-visible waiver someone has to justify, instead of an omission
nobody sees.

## 6. Ordering

Unchanged from LAYERED_BRINGUP.md §6, with the triad rule folded into step 2:
DRC first, prove the initcall mechanism on five modules across five layers,
delete the redundant `*_core.c` cells, then convert lowest-first.

L8–L12 are mostly **specification, not code** today. That is the honest status:
the rows exist so the floorplan is complete and dependencies can be stated, not
because those layers are built. Marking them as designed-and-empty is the point —
it is what stops L11 work from being started before L3 has revocation.

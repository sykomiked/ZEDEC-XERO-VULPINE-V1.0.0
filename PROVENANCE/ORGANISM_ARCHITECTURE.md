# The Organism Architecture — cell theory as the structural model for ZXV

_Owner directive, 2026-08-09: "build it the way we're building an organism."_
_Companion to SYSTEM_MAP.md (what exists) and LOGIC_ALGEBRA_MAP.md (the algebra)._

---

## 0. Why this is not a metaphor

The owner's framing — organelles inside cells inside organs inside organ systems
inside a body inside a community — is a **known formal model of computation**:
**membrane computing (P systems)**, Păun 1998. Nested compartments; rules that are
*local to a compartment*; objects that move across membranes under control.
Computation happens *because* of the compartment structure, not despite it.

So the instruction is implementable, and this document is the mapping.

## 1. Cell theory ≈ set theory, and precisely where it is NOT

The correspondence is real but it breaks at one specific place, and the break is
the most important structural fact in this document.

| set theory | cell theory | holds? |
|---|---|---|
| membership `x ∈ A` | a molecule is in a cell | yes |
| nesting `A ⊆ B ⊆ C` | organelle ⊂ cell ⊂ tissue | yes |
| union / intersection | tissues sharing cells | yes |
| **extensionality** — a set IS its members | — | **FAILS** |

**Extensionality is the break.** In set theory two sets with the same members are
*the same set*. Two cells with identical contents are still two cells, because a
cell is not its contents — it is its contents **plus a membrane plus a lineage**.

Therefore the OS's unit of composition cannot be a plain set. It needs:

1. **A boundary** — what may cross, in which direction, under what authority.
2. **An identity** independent of contents — so a thing stays itself as its
   contents change.
3. **A lineage** — what it came from.

ZXV already has all three and they were built separately: capabilities are the
boundary (ZAB derives them from bytecode, no GRANT opcode), ZXI witnesses are the
identity, and provenance is the lineage. **The organism model is what unifies
them: they are not three features, they are the three properties that make a
compartment a compartment.**

## 2. Differentiation — one genome, many expressions

The load-bearing biological fact for this system:

> Every cell in an organism carries the **complete** genome. A liver cell and a
> retinal cell differ only in **which part they express**.

That is the design rule for a decentralized OS, and it resolves the tension
between "one system" and "many specialised nodes" without forking anything:

- **Every node ships the whole image.** No per-role builds, no feature branches.
- **A role is an expression profile** — which capabilities are enabled — not a
  different binary.
- **Differentiation is reversible under stress**, as in biology (stem-cell
  reprogramming). A node can re-express as another organ when one is lost.

This is directly at odds with the current state of the tree, where 20% of
`kernel/src` is in **no** image and several subsystems exist on exactly one
architecture. Under this model that is not "modularity" — it is a genome with
deletions, and different organisms per architecture.

## 3. The unlinked modules are STEM CELLS, not corpses

**Owner correction, and it is the correct biology.** An earlier draft of this
document called the 55 unlinked modules necrotic tissue and prescribed
apoptosis. That was wrong, and the owner's framing is both intentional and a
better fit:

> "We created all the modules first... I didn't leave them orphaned by accident.
> I left them as building blocks for what we're doing right now."

Biology already has the exact cell type: **undifferentiated stem cells**.
Pluripotent, carrying the full genome, deliberately held *uncommitted* until a
signal assigns them a role. A stem cell is not a failed cell. Holding a reserve
of them is not neglect — it is how an organism grows organs it does not yet have.

The measured inventory is therefore not a debt register, it is a **parts list**:

> **55 files, 22,416 lines, 20% of `kernel/src`**, compiled and host-tested,
> linked into no image, awaiting differentiation.

The demonstration that this reading is right: §6 of this document listed
**digestion (foreign-format handling) as the one genuinely absent organ**. It is
not absent. `ubh.c` — 469 lines, *"Universal Binary Harmonization Matrix —
UBH-168 frame codec, format registry"* — is sitting in the reserve. The organ was
grown before the body was ready to attach it.

**So the operation for this phase is DIFFERENTIATION, not removal**: assign each
reserve module to an organ, then express it. See §10.

Two items in the reserve are a different case and are called out so they are not
lost in the reframe — not because they should be discarded, but because they are
**partially expressed**, which is the one state biology does not tolerate:

- **TLS.** `hkdf.c` is linked on four architectures while `x25519`, `aead`,
  `record` and `handshake` are linked on none. That is not a stem cell; it is an
  organ half-attached, and it makes the system report a transport security
  capability it does not have.
- **`fs.c`.** Its header is included by all eight `kernel_main`s while zero of
  its APIs are called. The advertisement is expressed; the tissue is not.

Undifferentiated is fine. **Half-differentiated is the failure mode**, because
the rest of the body plans around a capability that is not there.

## 4. Endosymbiosis — how foreign code becomes an organelle

Mitochondria were free-living organisms. They were absorbed, they became
essential, and **they kept their own genome**. They are not rewritten host code;
they are guests with their own identity, replicating on their own schedule.

This is the correct model for everything ZXV absorbs from outside — emulator
cores, id Tech and Godot lineages, foreign codecs:

- absorbed code **keeps its own provenance and licence** (its genome),
- it runs under the host's membrane (capabilities),
- it is **not** rewritten into house style to pretend it was always ours.

That is also the honest licensing posture, and it is already the practice for the
open engine lineages. Naming it makes it a rule instead of a habit.

## 5. Homeostasis — regulation is an organ, not a utility

An organism is defined less by its parts than by its **regulation**: continuous
correction toward a setpoint, with failure signalled *before* it is fatal. That
is precisely the bomb squad's margin-and-fuse model, and it means the bomb squad
is not a debugging library — it is the **endocrine/autonomic system**, and it must
watch real organ margins (free sectors, slot pressure, surplus, ledger drift,
phase-tick jitter), not test fixtures.

## 6. The body plan — an anthropomorphic dragon

Archetype chosen by the owner. Anthropomorphic matters: **hands mean tool use**,
which means the organism *acts on its world* rather than only inhabiting it.

| organ | function in the body | ZXV subsystem | state |
|---|---|---|---|
| **skeleton** | load-bearing, slow to change | syscall ABI (`ZXV_SYSCALL_TABLE`) | built, L1 |
| **spine / nerve cord** | the trunk line everything hangs off | phase-tick event cycle | built |
| **nervous system** | signal, reflex, interrupt | exceptions, IPC, event clock | built |
| **brain** | judgement, learning | Chiglet, Sutra runtime | Chiglet built; **Sutra unexpressed** |
| **eyes** | perception at distance | WyvernEye | built |
| **heart** | the pump; rhythm | phase-tick clock | built |
| **lungs** | exchange with outside | virtio, block/net devices | built |
| **digestive tract** | foreign matter → usable substance | `zmedia` + **`ubh.c` in reserve** | organ exists, tissue unattached |
| **immune system** | detect non-self, quarantine, remember | red-team gates, S0 quarantine, Pig Badge | built |
| **integument / scales** | the boundary itself | ZAB capabilities, tri-space binding | built |
| **hands** | manipulate the world | shell, AppKit, tools | built |
| **hoard** | what is kept, and kept safe | ZXVFS + triads | built |
| **tail** | balance and counterweight | triple ledger / surplus accounting | built |
| **fire** | concentrated output, on demand | render/compute burst | partial |
| **gonads** | reproduction — make another organism | installer, signed release, provisioning | built |
| **WINGS** | **leave the ground; reach other bodies** | **P2P mesh, netplay, TLS transport** | **CLIPPED — see §7** |

## 7. The wings, specifically

The wings are the organ that decides whether this is one organism in a place or a
member of a **community of organisms** — which is the whole thesis. And they are
the most broken thing in the body.

**The left wing is missing.** The TLS 1.3 subsystem ships as `hkdf.c` alone,
linked on four architectures. `x25519.c` (key exchange), `aead.c` (cipher),
`record.c` (record layer) and `handshake.c` are linked on **none** — 1,136 of
1,272 lines. What boots is key derivation with nothing to derive keys *for*. The
system can address the network and cannot secure a single conversation over it.

**Both P2P transports are dead.** `bootlegger.c` (844 lines) and its superseded
predecessor `p2p_caracho.c` (850 lines) are in no image.

This is exactly the bilateral-symmetry constraint that makes wings different from
every other organ: **a body plan tolerates one heart, but a wing on one side and
nothing on the other is worse than no wings** — it does not fly, and it does not
know that it cannot. Right now the system reports network capability it does not
have, which is the aviation equivalent of a confident jump.

**Wings are therefore the top build priority after apoptosis**, in this order:

1. Link the four dead TLS units and make the handshake reach a real endpoint.
2. Bilateral parity — the same transport on *every* architecture, or explicitly
   unexpressed on the ones that lack it. No silent per-arch asymmetry.
3. One live P2P transport, chosen between `bootlegger` and `p2p_caracho`; the
   loser gets apoptosis, not archival.
4. Only then the triad quality ladder for remote display (S+ now, S− on demand)
   — that is a flight *manoeuvre*, and it needs wings first.

## 8. Scale invariance — the same rule at every level

The owner's constraint that every level of scale repeats the same thing is
satisfiable in exactly one way: **the same three properties define a compartment
at every level.** Boundary, identity, lineage — for an organelle, a cell, an
organ, a body, and a community. No level gets a bespoke mechanism.

That is also the test. If a proposed layer needs a *new* kind of boundary that
the level below does not have, the design is wrong at the level below.

## 9. Build order this implies

1. **Differentiation pass** — assign all 55 reserve modules to organs (§10) and
   express them. This is the phase the owner staged the reserve for.
2. **Wings first among them** — TLS completed, bilateral parity, one P2P
   transport. The half-expressed organ is the urgent case.
3. **Genome parity** — end per-architecture subsystem asymmetry, or declare it.
4. **Roles as expression profiles** over one whole image (§2).
5. **Homeostasis** — bomb squad onto real organ margins.

## 10. Differentiation table — the reserve, assigned

Every unlinked module mapped to the organ it becomes. Grouped by organ so the
gaps are visible: an organ with reserve tissue and no expressed tissue is one
that has never actually run.

| organ | reserve modules awaiting expression | lines |
|---|---|---|
| **WINGS** — reach, flight, other bodies | `tls/x25519.c`, `tls/aead.c`, `tls/record.c`, `tls/handshake.c`, `bootlegger.c`, `p2p_caracho.c`, `wifi.c`, `bluetooth.c`, `browser.c`, `panopticon_vpn.c` | 8,289 |
| **DIGESTION** — foreign matter → usable substance | `ubh.c` (UBH-168 frame codec + format registry), `crit168_os.c` (168-bit canonical representation), `rur.c` (root universal type system) | 1,095 |
| **VOICE & SENSES** — output, perception | `audio.c`, `video.c`, `desktop.c`, `apps/*` (4) | 3,139 |
| **BRAIN — language faculty** | `sutra/*` (7 runtime files: lexer, parser, runtime, chiglet, rails, capital, selfaudit) | ~989 |
| **BRAIN — cortex, multi-state logic** | `sephirot.c` (13-phase hypervisor), `dharana.c` (112-gate array), `lpres.c` (4-valued paraconsistent logic) | 595 |
| **BRAIN — limbic / affect** | `epu_device.c` (emotional processing model; densest φ arithmetic in the tree) | 1,273 |
| **AUTONOMIC — regulation along the spine** | `dharma/*` (10: dharma toroidal feedback, chakra spinal nodes, karma causal events, bodhi, tantra, mantra, yantra, uvn, upaah, naga_raja) | 798 |
| **NERVOUS SYSTEM — routing & causality** | `oseq.c` (causal DAG, replay detection), `iphase.c` (inter-phase routing), `choice.c` (deterministic selection), `phase_coord.c` (admission tokens) | 1,112 |
| **IMMUNE** — self/non-self, adjudication | `panopticon.c` (surveillance awareness), `legal_engine.c`, `legal_engine_ext.c`, `crypto_verify.c`, `genomic_codon.c` | 1,793 |
| **PROPRIOCEPTION** — sense of quantity | `rce_units.c` (SI units + dimensional analysis) | 379 |
| **MORPHOGENESIS** — growth machinery | `macgyver.c` (shared AST registry, obligation tracking) | 368 |
| **HOARD / GUT** | `fs.c` (transactional files, replication, snapshots) | 417 |
| **REPRODUCTION** | `zxpkg.c` (on-disk native package) | 197 |
| **absorbed organelle** (endosymbiont, §4) | `emu/sms.c` (Sega Master System core — the one console core missing from every image while ~30 siblings are linked) | 144 |

**What this table exposes, which the flat list did not:** three organs are
*entirely* unexpressed — the language faculty, the autonomic regulation chain,
and proprioception have **no** linked tissue at all. Those are not incremental
gaps; they are organs the running body has never had. And the routing/causality
group (`oseq`, `iphase`, `choice`, `phase_coord`) each has a linked `*_core.c`
sibling with a *thinner* API — so the nervous system is running on a reflex arc
while the full ganglion sits in reserve.

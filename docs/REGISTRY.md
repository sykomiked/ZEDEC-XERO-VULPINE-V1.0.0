# The registry: 407 slots in three layers

Every program on the floor has a slot number from 1 to 407. The number
says which layer the program belongs to. The layer says what it may touch,
where its results may go, and how its icon looks.

| Slots | Count | Layer | Holds | Icon on the #002911 ground |
|---|---|---|---|---|
| 1–144 | 144 | **L1 Foundation** | The 144 apps on the existing app list (`kernel/src/launch/zxl_apps.inc`, Burrow … Stillwater, in 12 groups of 12) | Gold, one colour |
| 145–271 | 127 | **L2 Concourse** | Compound tools: circuits, buses, verification streams, swarm and peer pipelines | Gold, one colour |
| 272–407 | 136 | **L3 Apparatus** | Language and legacy hosts: COBOL, Fortran, Ada, APL and MUMPS (the SDK pillars), the Rosetta Shell's dialect front-ends | Its own colours, inside a gold border |

That is 144 + 127 + 136 = 407 slots. `#002911` is already the dark
theme's ground in the native UI (`kernel/src/zui/zui_palette.c`), and gold
is already its accent.

Status: design, not built. Section 6 gives the build order.

## 1. L1 Foundation (1–144): deterministic state over a journal

The rule that makes an app L1 is the one that makes a blockchain's base
layer trustworthy. **An L1 app's state is a pure function of its own input
journal.** Replaying the journal on any machine rebuilds the same state, bit
for bit.

- An L1 app does no floating point. Its arithmetic is integer or exact
  rational.
- An L1 app does not read the network, the clock or the disk directly. A
  chat message (Commons), the time of day (Sundial, Solstice) or a file's
  content arrives as an **input event**: recorded, hashed and appended to
  the app's journal before the app sees it. Delivering those events is L2's
  job.
- Its outputs are journal entries too. What it shows on screen is computed
  from its state.

This keeps the existing apps whole. Commons is still a chat app, and
Sundial still tells the time. They simply receive the outside world as a
recorded stream they can replay, never as live, unrecorded reads.

The four-valued operators, the exact rational arithmetic and the other
kernel primitives are not slots. They are the kernel itself, which every
layer calls through the same verified interfaces.

## 2. L2 Concourse (145–271): execution, delivery, settlement

L2 is where work happens at speed and in parallel:
- the circuits drawn on the canvas;
- the inter-app buses with bounded queues and deterministic backpressure;
- the file-verification streams of the Registrar;
- the Cage's desks (`docs/CAGE.md`);
- swarm pipelines and bilateral sessions between peers over Vinea;
- the delivery of input events into L1 journals.

**Settling to L1.** A batch of L2 work ends in one result. Before the
result becomes an input event in an L1 journal, it is checked: the Cage's
clearance, plus AIPI's CHECKER, CERT or QUORUM for work done by peers. The
check is what ZXV can actually guarantee. Because the integer engine gives
bit-identical results, any honest peer can recompute the work and compare
it exactly, and a quorum needs no tolerance. ZXV does not generate
zero-knowledge proofs; that would be new work, named here so the analogy
with blockchain rollups is not mistaken for it.

**Contradictions.** If two L2 results disagree (BOTH), nothing settles.
The Arbitration Desk's rules or the user decide, as in `docs/PIT_BOSS.md`.

## 3. L3 Apparatus (272–407): foreign and legacy hosts

L3 runs code written for other worlds in its own terms:
- COBOL `PICTURE` clauses and COMP-3 packed decimal, read as exact decimal
  values, never as binary floats;
- Fortran numerics;
- Ada, APL and MUMPS through their pillars (`sdk/pillars`);
- the Rosetta Shell's POSIX, PowerShell and cmd.exe front-ends.

Every L3 host runs inside the Cage. An invalid instruction, an unmapped
address or an exhausted budget stops that host only; L2 and L1 never see
partial state.

A C program's race condition becomes a BOTH only where the host can see it.
That holds when the host runs the program under a deterministic scheduler
that tries interleavings. A host running native threads cannot promise
this.

## 4. Crossing rules

| From | May send to | May never |
|---|---|---|
| L3 | L2 only, as a result for checking | Write an L1 journal or the permanent store |
| L2 | L1, as a checked input event | Change an L1 app's state except through its journal |
| L1 | Its own journal and screen | Call out to L2 or L3 except by emitting a journal entry that L2 acts on |

The rules are enforced at two levels:
- **Code dependencies:** the existing strata check (`build_system/strata.txt`,
  `verify_layers.sh`) already refuses a module that depends on a higher
  stratum.
- **Running programs:** the Pit Boss's rulebook checks each crossing
  against the program's manifest.

**Budgets are fuel, not fees.** Every run has a fixed tick and memory budget
(Z-Wasm fuel, swarm budgets), counted the same way on every machine. An
L3 program that recurses forever stops when its L2 allocation runs out.

## 5. Manifests: what a slot number does and does not prove

A slot number is an address. A program cannot be trusted just because it
claims slot 12. Each registered program therefore carries a signed
manifest, checked by Immigration (`kernel/src/immigration`) at admission:
- its slot and layer;
- its capabilities (network events, files, peers);
- its budget;
- its icon.

The Pit Boss reads the layer from the manifest, and the slot range is a
consistency check on it.

**Icons.** L3 icons use each host's own colours inside a gold border. The
emblems must be ZXV's own designs that suggest the language (a colour, a
glyph, a period typeface), not the trademarks of IBM, the Ada or Fortran
communities, or any vendor. The repository already checks names against
trademarks (`docs/ARCHITECTURE.md`, the Zenny entry), and icons need the
same care.

## 6. Build order

| # | Slice | Acceptance test |
|---|---|---|
| 1 | Audit the 144 apps against the L1 rule: list every direct network, clock, disk and float use | A table in this document, one row per app, with what must move behind input events |
| 2 | Input-event journal for L1 apps (hashed, appended through the Registrar's Tally) and replay | An app's state after replay equals its state when live, byte for byte |
| 3 | Convert the apps the audit flags, one group of 12 at a time | Each converted group passes the replay test; the app list and launch themes are unchanged |
| 4 | Manifests with slot, layer, capabilities and budget, checked at admission | A program whose manifest claims the wrong layer for its capabilities is refused |
| 5 | Crossing rules in the Pit Boss's rulebook | An L3 program's attempt to write an L1 journal is refused and logged |
| 6 | L2 settlement: checked results become L1 input events (Cage clearance, AIPI checks) | A disagreeing quorum leaves the L1 journal unchanged and the result held |
| 7 | Icons: gold for L1 and L2, original coloured emblems with gold borders for L3 | Every slot has an icon; no third-party trademark appears |

## 7. Limits

- Determinism comes from the journal and the exact arithmetic. It is
  tested and replayable, not proven correct. A deterministic app can still
  compute the wrong thing, and it will compute it the same way every time.
- ZXV does not claim a zero-jitter scheduler. L1's guarantee is about state,
  not timing.
- Slots 145–407 are a budget for programs not yet written. Section 6 fills
  them only as each one is built and registered.

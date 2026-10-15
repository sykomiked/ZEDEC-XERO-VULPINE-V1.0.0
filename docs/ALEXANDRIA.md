<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The Library of Alexandria

The Library of Alexandria is ZXV's vault of verified building blocks. Each
block is immutable, has a content address, and has a short fixed name. A
chiglet asked for new behaviour does not write code character by character.
It picks blocks by shorthand (`@num.gcd`), and the system checks that they
fit together and runs them. Every piece it uses already compiles, has passed
its tests, and has a CID.

The library is not a second system. It is an index over pieces ZXV already
has:

| The design asks for | ZXV already has it as |
|---|---|
| a module with a semantic descriptor | a block manifest (`zxblock 1`, kernel/src/apps/zxapp.h B1–B4) |
| `node_id` and a meaning | the concept registry (CONCEPTS.tsv: one owner per idea, its other names, its meaning) |
| `targets` in several languages | the manifest's `logic` lines, one per language: Lingua (all 81 by name), the five pillars, C, Z-Wasm |
| the module CID | the manifest's content id (C3: SHA-256 of its text); here also written as a CIDv1, the same one `ipfs add --cid-version=1` gives |
| `combinator_slots` (typed input and output) | the `provides` contract: the answer kind (num or truth) and the inputs, checked on every call (B3) |
| invariants: zero floats | the zero-float gate on app models and blocks (test_zxapp); Lingua and Babel are integer-only by construction |
| equivalence across languages | witnessed calls (A5): every language runs, and any disagreement is BOTH and the answer is held |
| a composite with its own DAG CID | an app that uses blocks and exports entries (C1), with provenance for every part (C2) |
| run anywhere, check the result | AIPI function and call CIDs (docs/AIPI.md) |
| a program shipped between runtimes | the Babel wire format (docs/BABEL_WIRE.md): values and Lingua programs, hashed |

What is new is the shorthand index: `kernel/src/alexandria`.

## 1. Shorthand slots

A **slot** is a u32 that names one block entry for good. People and models
write it as a shorthand: `@num.gcd` or `@S:0001`. Code holds the number.

- `kernel/src/alexandria/SLOTS.tsv` is the registry. Each line has the slot,
  the shorthand, the block entry it names (`number_classics.gcd`) and its
  state. It is append-only. A slot is never renumbered or reused. A
  withdrawn slot stays, marked retired, so a program that names it gets a
  refusal and never a different block.
- **Namespaces:** mem, crypto, drv, logic, wire, num, fin, prob, strat,
  time, swarm and text.
- **Ranges:** slots 1 to 0xFFFF are the base set, sealed on the disc.
  0x10000 and up belong to community overlays (section 4) and are never
  written into the base table.

## 2. The table

`tools/alexandria/build_index.py` reads every registered block and
SLOTS.tsv. It refuses to run if any block entry has no slot, or any slot
names a missing entry. Otherwise it writes:

- `ALEXANDRIA.TBL`: the binary table, laid out in `alx.h`. It has a 32-byte
  header, then one 128-byte entry per slot in slot order, then a SHA-256 of
  everything before it. Each entry holds the slot, the answer kind, the
  number of inputs, the version, the shorthand, the block entry, the number
  of languages and the CIDv1 of the manifest.
- `alexandria_shorthands.json`: the same index for people and models,
  adding every language's file and its CID.
- `alx_slots.h`: one `#define` per slot for C code.
- with `--out-dir`, the `05_alexandria` tree for the bucket: the table, the
  JSON, and every manifest and logic file at its own path.

The same input always gives the same bytes. Today's table holds 4 slots;
its CID is printed by the script and checked by `make test-alexandria`.

`alx.c` is the kernel's reader. It is freestanding C with integers only and
no allocation; its only outside call is `sha256()`.

- `alx_open` checks every byte and refuses a table that is not exactly
  right.
- `alx_get` finds a slot in constant time, because slot n is entry n.
- `alx_find` turns `@num.gcd` or `@S:0001` into a slot.
- `alx_dock` and `alx_pipeline` check a composition before anything runs.

## 3. Composing

A chiglet's plan is a list of slots, for example:

```
@num.gcd  ->  @swarm.cycle_share  ->  @swarm.busiest
```

`alx_pipeline` checks each link before anything runs. The check is the
typed slot match the design asks for, at the level that exists today. One
block's answer can feed the next block only if that block takes exactly
one input. A retired slot never docks.

Inputs are integers today. When block contracts carry Babel value kinds
(text, list, record and the rest), docking will match those kinds too.

A plan that passes is composed the way apps already are:
- it uses the blocks (`use number_classics 1`);
- it calls `<block>.<entry>`;
- it is witnessed across the block's languages.

A finished composition is an app. It has its own content id and provenance,
and it can be registered as a block and given a slot of its own.

## 4. Community overlays

An overlay is a bucket of its own with its own CID. It holds manifests,
logic and attestations: signatures from reviewers stating what they checked
(zero floats passed, memory bounded, witnesses agree). Overlays go through
the user-evolved OS's community path (evo_comm) and the operator's trust
policy (how many signatures, from whom). An overlay that passes gets slots
from 0x10000 up under `@ext.<publisher>.<name>`. It never changes a base
slot, and a base slot never points into an overlay.

## 5. What the library can and cannot promise

- **It cannot make failure impossible.** It makes failures visible. Blocks
  are checked, contracts are enforced on every call, compositions are
  checked before they run, and disagreement between languages is BOTH, not
  a silent answer. A block can still have a bug that all its languages
  share.
- **Formal proofs cover a small core only.** Proofs in Lean or similar
  tools are planned for a few blocks where a proof is practical and worth
  it: the paraconsistent gates (lpres), the ring buffer and fixed-point
  arithmetic. Every other block is held to equivalence by differential
  testing, meaning its languages run on the same inputs through the
  witness. That is evidence, not proof.
- **Translation is not general.** Deciding whether two arbitrary programs do
  the same thing is undecidable, so there is no general translator that
  round-trips any program between all languages. The library holds the
  same block written in several languages and checks that they agree on
  tests. It does not derive one from another. Each Lingua language's twin
  is its canonical spelling in that same language, not a translation.
- **Availability is not guaranteed.** A CID resolves only while some
  reachable node, or the disc, holds its bytes (AIPI rule A6). The base
  set is on the disc for exactly that reason.

## 6. Build order

1. Slots, the table, the reader and the builder (built: `make
   test-alexandria`).
2. Slots for every registered block, added as blocks land, through `--assign
   BLOCK NAMESPACE`. The apps thread's CONCEPTS.tsv names stay the meaning;
   the shorthand is the handle.
3. The bucket tree, `05_alexandria`, packed by ipfs-dist next to Disc 1 and
   the swarm packs. The table's CID joins the bucket root.
4. A zxapp entry point that calls a block by slot, and a chiglet tool that
   takes a slot plan, docks it, and composes the app.
5. The proof core (lpres gates, ring buffer, fixed point) as blocks, with
   their proof files listed next to their logic.
6. Overlays: attestation checks, the trust policy and `@ext` slots.

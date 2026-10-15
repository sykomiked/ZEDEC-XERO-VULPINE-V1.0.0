<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The Library of Alexandria

The Library of Alexandria is ZXV's vault of verified building blocks. Each
block is immutable, has a content address, and has a short fixed name. A
chiglet asked for new behaviour does not write code character by character.
It names blocks (`@num.gcd`, or by place, `@512.01`), and the system checks
that they fit together and runs them. Every piece it uses already compiles,
has passed its tests, and has a CID.

The library is not a second system. It is an index over pieces ZXV already
has:

| The design asks for | ZXV already has it as |
|---|---|
| a module with a semantic descriptor | a block manifest (`zxblock 1`, kernel/src/apps/zxapp.h B1–B4) |
| a meaning and the names people use for it | the concept registry (CONCEPTS.tsv: one owner per idea, its other names, its meaning) |
| `targets` in many languages | the manifest's `logic` lines, one per language: Lingua (all 81), the five pillars, C, Z-Wasm and the SDK languages |
| a module CID | the manifest's content id (C3: the SHA-256 of its text) |
| `combinator_slots` (typed input and output) | the `provides` contract: the answer kind (num or truth) and the inputs, checked on every call (B3) |
| invariants: zero floats | the zero-float gate on app models and blocks (test_zxapp); Lingua, Babel and AXION are integer-only by construction |
| equivalence across languages | witnessed calls (A5): every language runs, and any disagreement is BOTH and the answer is held |
| a composite with its own CID | an app that uses blocks and exports entries (C1), with provenance for every part (C2) |
| run anywhere, check the result | AIPI function and call CIDs (docs/AIPI.md) |
| shipping values and programs between runtimes | the Babel wire format (docs/BABEL_WIRE.md) |
| Belnap gates | lpres (kernel/src/lpres), whose tables AXION uses exactly |

What is new is `kernel/src/alexandria`: the index, the booklets and three
small languages over them.

## 1. Slots, places and languages

Every block entry has three fixed names. All three are append-only: once
given, a name is never renumbered or reused.

- **Slot.** A u32, written `@S:0001`. Code holds this.
- **Shorthand.** `@num.gcd`: a namespace and a name. People and models
  write this.
- **Place.** `512.01`: the entry's position in the Alexandria
  classification. The three digits are the class, division and section,
  then comes a two-digit item.

Each of a block's languages has a three-digit **language number**. 001 to
099 are Lingua's own numbers from zl_langs.def: FORTRAN is 001, BASIC 002
and Forth 003. 101 to 120 are the other runtimes, in zxapp's order: c 101,
sutra 102, eve 103, kron 104 and so on, up to zlir 120. So `512.01-001` is
the greatest common divisor in FORTRAN, and `512.01-120` is the same block
compiled to Lingua IR.

The classes, and the namespace that belongs to each:

| Class | Namespaces | Holds |
|---|---|---|
| 100 | mem | memory and allocation |
| 200 | crypto | cryptography and integrity |
| 300 | drv | devices and buses |
| 400 | logic | logic and evidence |
| 500 | num | numbers and arithmetic |
| 600 | fin, prob, strat | money, chance and decisions |
| 700 | wire | wire formats and protocols |
| 800 | text | text and languages |
| 900 | time, swarm, cell | time, coordination and the swarm's cells |

The scheme is decimal and hierarchical, like a library's shelf marks, but it
is our own scheme with our own name. It is not a library classification
anyone else publishes.

`kernel/src/alexandria/SLOTS.tsv` is the registry. Each line has a slot, its
shorthand, its place, the block entry it names and its state. A withdrawn
slot stays, marked retired. A program that names it gets a refusal, never
another block.

## 2. Booklets

There is one CID per **booklet**, not one per function. Each division's
blocks (for example everything in 510 to 519) are packed into one booklet.
It begins with a 64-byte header (`ALXB`, version, division, file count,
length). Then every manifest and logic file follows, each starting on a
64-byte boundary.

A booklet is at most 1 MiB, which is one IPFS block. Its CID is therefore
the CIDv1 raw sha2-256 of its bytes, the same CID IPFS gives it. The index
names each booklet once, and gives each language's logic as an offset and
a length inside it. Fetching one booklet brings every block in its
division.

## 3. The index

`tools/alexandria/build_index.py` reads every registered block and
SLOTS.tsv. It refuses to run if any block entry has no slot, any slot names
a missing entry, a shorthand sits in the wrong class, or any name or place
repeats. Otherwise it writes:

- `ALEXANDRIA.TBL`, the binary index. It is laid out byte by byte in
  `kernel/src/alexandria/alx.h`: a header, the booklets, a 144-byte record
  per slot, a 16-byte record per language, and a SHA-256 of all of it.
- `alexandria_index.json`, the same index for people and models.
- `B<division>.booklet`, one file per booklet.
- `alx_slots.h`, one `#define` per slot for C code.

With `--out-dir` the script writes the `05_alexandria` tree for the bucket.
The same input always gives the same bytes.

`alx.c` is the kernel's reader. It is freestanding C with integers only and
no allocation, and its only outside call is `sha256()`.

- `alx_open` checks every byte of the index and refuses one that is not
  exactly right.
- Lookup by slot takes constant time, because slot n is record n.
- `alx_booklet_check` checks fetched booklet bytes against their CID before
  anything uses them.
- `alx_target` gives a language's offset and length in its booklet.

`make test-alexandria` runs 91240 checks. They include reading every
language's logic back out of the real booklets byte for byte, and changing
each byte of the index in turn to confirm the change is caught.

## 4. Three small languages

The design asks for three languages: LEXIS to resolve, TECTON to compose and
AXION to check. Each was mapped onto what exists first, and only the missing
part was built.

### LEXIS: resolving names

A request in plain words ("a bounded FIFO") is turned into a concept by the
chiglet and the concept registry. That step is model work plus a list of
names, not a parser. The deterministic step after it is new, in
`alx_ref.c`. It turns a reference into a slot and a language:

```
@num.gcd   num.gcd   @S:0001   @512.01   512.01      a block
@num.gcd-001   512.01:120                            a block in one language
<parent CID>/512.01.42.01:001                        a community addendum
```

A language the block does not have is refused, never substituted. The
`RESOLVE { capacity : 4096 ... }` form from the design needs blocks to
declare properties such as capacity. They do not yet, so that form is not
built.

### TECTON: composing blocks

zxapp already composes blocks (`use`, `provides`, contracts checked on every
call). What was missing is a short plan a chiglet can emit and the system
can check before anything runs. `alx_tecton.c` is that parser, at 135
lines:

```
BIND telemetry {
    SOURCE : @num.gcd-001
    PIPE   : @931.01
    SINK   : @swarm.busiest
}
```

It needs one SOURCE, then any number of PIPEs, then at most one SINK. Every
reference must resolve, every language asked for must exist, and every link
must dock. One block's answer feeds the next block only when that block
takes exactly one input. Inputs are integers today; when contracts carry
Babel value kinds, docking will match those kinds too. Any error gives the
exact byte where checking stopped. A plan that passes becomes an app in the
usual way: it uses the blocks, calls them, and is witnessed across their
languages.

### AXION: loop-free truth checks

lpres already has the gates, and zxapp's witness already joins answers.
What was missing is a small program form that runs those gates over
readings with a fixed bound. `alx_axion.c` is that, at 209 lines:

```
# ready, and the bus agrees
in 0 int 1 eq
int 1 in 1 eq
join
```

- **Values:** a program pushes readings (`in K`), integers and truths, and
  compares readings with `lt`, `le` and `eq`.
- **Gates:** `not`, `and` and `or` use exactly lpres's tables. The test
  checks all sixteen pairs against lpres itself.
- **Witness join:** `join` combines two sides' evidence, so TRUE joined
  with FALSE is BOTH.
- **Missing readings:** a reading that did not arrive compares as NEITHER;
  it is never taken as zero.
- **Bounded:** there are no jumps, so a program runs in exactly as many
  steps as it has words: at most 256 words and 32 stack cells.
- **Checked:** the parser checks the stack's types word by word, and the
  runner checks again, so a program built by hand gets the same checks.
- **Result:** the answer is one truth. The caller acts on TRUE or FALSE,
  and BOTH or NEITHER takes its fallback path.

## 5. Community addenda

An addendum extends a sealed block without changing it. Its name starts
with the CID it extends, then gives its place with its own parts
(extension, revision, fork marks), then its language:

```
bafkrei.../512.01.42.01:001
```

A node accepts an addendum only when that parent CID is in its active
lineage: one of the sealed base's CIDs, or an addendum already mounted on
it. Anything else is refused as not in the lineage. That refusal is built
(`alx_resolve`, LINEAGE).

Mounting is not built yet. Until it is, an addendum in the lineage is
refused as not mounted (OVERLAY). Mounting will go through the user-evolved
OS's community path (evo_comm) and the operator's trust policy, meaning
how many reviewer signatures are needed and from whom. A mounted addendum
never changes a base slot, place or booklet.

## 6. What the library can and cannot promise

- **It cannot make failure impossible.** It makes failures visible. Every
  byte of the index and the booklets is checked, contracts are enforced on
  every call, plans are checked before they run, and disagreement between
  languages is BOTH, not a silent answer. A block can still have a bug that
  all its languages share.
- **Formal proofs cover a small core only.** Proofs in Lean or a similar
  tool are planned for a few blocks where a proof is practical and worth
  it: the lpres gates, a ring buffer and fixed-point arithmetic. Every other
  block is held to equivalence by differential testing, meaning its
  languages run on the same inputs through the witness. That is evidence,
  not proof.
- **Translation is not general.** Whether two arbitrary programs do the same
  thing is undecidable, so there is no general translator that round-trips
  any program between all languages. The library holds the same block
  written in several languages and checks that they agree. It does not
  derive one from another.
- **Availability is not guaranteed.** A CID resolves only while some node,
  or the disc, holds its bytes (AIPI rule A6). The base booklets are on the
  disc for exactly that reason.

## 7. Names to check before release

LEXIS, TECTON and AXION join Babel, Rosetta, the dragon twin names and
"Library of Alexandria" on the trademark-check list. The classification is
called the Alexandria classification. The names of other organisations'
library classification systems are their trademarks, and none appears in
the code or the docs.

## 8. Build order

1. **Built** (`make test-alexandria`): the index, booklets, references and
   lineage, TECTON and AXION, and the 64-bit grid words with their radix
   resolver and layer lineage (docs/spec/GRID_CHAIN.md), and cartography: the sealed
   core index (ALEXANDRIA.IDX) and cartographers' signed overlays, built
   with `zxv-index` (docs/spec/CARTOGRAPHY.md); and the 144-layer
   matrix, one layer per system app, chunked, packed by its glue and
   checked file by file (docs/spec/MATRIX.md).
2. **Slots for every block.** These are added as blocks land, with
   `--assign BLOCK NAMESPACE SECTION`. The apps thread's CONCEPTS.tsv stays
   the source of meanings; the shorthand is the handle.
3. **The bucket tree.** ipfs-dist packs `05_alexandria` next to Disc 1 and
   the swarm packs.
4. **Running a plan.** A zxapp entry point that calls a block by slot, and
   a tool that turns a checked TECTON plan into an app.
5. **The proof core.** The lpres gates, a ring buffer and fixed point, as
   blocks with their proof files next to their logic.
6. **Mounting addenda.** Attestation checks, the trust policy, and lineage
   extended to mounted addenda.

<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The 144-layer matrix

After the core boots, the matrix holds everything the system needs at its
root: every block of code, table and document, packed into content-addressed
pieces that the system can fetch, check and reuse. It has 145 layers. Layer
0 is the core, and its first booklet is booklet 000, the unified alphabet
(`kernel/src/lingua/alphabet.tsv` and the projection runtime), so the
alphabet is the first thing the matrix gives after boot. Layers 1 to 144
are the 144 L1 system apps in the order of the app catalogue, twelve groups
of twelve, each with its icon. The L2 and L3 app layers go on top of these.

The code is `kernel/src/alexandria/alx_cdc.c` (chunking),
`kernel/src/alexandria/alx_matrix.c` (the node formats and the walks),
`tools/alexandria/pack_matrix.py` (the packer and the measurements) and
`kernel/src/alexandria/MATRIX.tsv` (which code lives in which layer). Every
number on this page comes from running the packer on this repository
(`kernel/src` and `docs`, 2422 files, 34.7 MB) on 2026-10-15.

## 1. Layers

`MATRIX.tsv` has one line per layer: the app, its group, its icon file, and
the paths whose files live there (the longest matching path wins). A shared
piece lives in exactly one layer; other layers reach it through the glue
(section 4), never through a second copy.

- **Layer 0, the core:** what boots: memory, scheduling, drivers, the
  integer tensor engine, the native UI toolkit, Lingua, Alexandria and the
  cryptography. 929 files, 13.95 MB, 167 booklets.
- **Layers 1 to 144:** each app's own code. Today 80 app layers hold code
  and 64 are still empty, because those apps are not built yet. The
  biggest are Parley (68, speech and translation tables, 2.74 MB), Coffer
  (73, the wallet and books, 2.09 MB) and Scriptorium (130, the developer
  documentation: all of `docs/`, 1.68 MB).
- **Pulled by the glue:** 42 modules are named by no layer. The packer puts
  each where its links pull hardest (a module bound to the Fairground's
  market code goes to the Fairground). 29 went to an app this way; 13 have
  no links to any app and stay in the core. The table of where each one went
  is in the report (`pulled_by_glue`), and a line in `MATRIX.tsv` overrides
  it.

The 144 icons are part of the matrix: the root carries each layer's icon
CID, and the packer stores the icons when given the icon folder (all 144
were found).

## 2. Chunks: amount, size and density

A file is cut where its bytes say (normalised gear-hash content-defined
chunking, `alx_cdc.h`), not at fixed offsets. That way an edit moves only
the chunks around it, and every chunk is named by the CID of its bytes and
stored once in the whole matrix. Each file gets one of three tiers:

| Tier | For | Min / average / max | Chunks | Mean chunk | Compresses to |
|---|---|---|---|---|---|
| hot | code up to 256 KiB | 1 / 4 / 16 KiB | 6769 | 3.7 KiB | 34.4% |
| warm | text, tables, documents | 4 / 16 / 64 KiB | 398 | 7.2 KiB | 34.6% |
| cold | generated bulk; content already dense (over 7.2 bits per byte) | 64 / 256 / 1024 KiB | 28 | 223 KiB | 28.4% |

"Compresses to" is the size zlib gets each chunk to, a measure of how dense
the content is. Every chunk fits one raw block with its booklet's header, so
no chunk ever needs a second level.

**Why 4 KiB for hot code.** The same repository was packed with hot averages
of 1, 2, 4 and 8 KiB, and each was compared with the tree as it stood on
2026-10-12:

| Hot average | Chunks | Today's bytes the older tree already held |
|---|---|---|
| 1 KiB | 23192 | 66.1% |
| 2 KiB | 12480 | 65.8% |
| 4 KiB | 7191 | 65.4% |
| 8 KiB | 4431 | 64.8% |

Going from 4 to 1 KiB triples the number of CIDs for 0.7 points more
reuse, so 4 KiB is the knee.

**What content-defined cutting buys.** Within one tree, identical content is
rare: chunks deduplicate 0.26% of the bytes (fixed 4 KiB blocks: 0.07%). The
gain is across versions. Of the 83 files that changed since 2026-10-12
(2.18 MB), 47.6% of the bytes were already stored as chunks. Fixed 4 KiB
blocks keep 25.3%. That is 1.9 times as much of an edited file not sent or
stored again.

## 3. Scaling: the fan-out tree

Each layer's booklets hang from a tree of fan-out nodes, 256 children each.
Every child but the last is full, so booklet k is reached by reading the
digits of k in base 256, with no search. Depth is log256 of the booklet
count: one level holds 256 booklets, two hold 65536, and the format allows
four (4.3 billion per layer). Today every layer is one level deep. The
largest, the core, has 167 booklets.

One root (`ALXM`) lists the 145 layers: tree, glue and icon. Changing a
layer rewrites that layer's nodes and the root, and nothing in any other
layer. Measured against the 2026-10-12 tree, 43 of the 81 layers that hold
content today have exactly the same tree root. Their files did not change,
so neither did a single node under them. A new layer adds its own tree and
one root entry, and no existing chunk is cut again.

**Booklets.** Hot booklets are kept small (up to 64 KiB) so that code is
fetched in dense pieces. Warm booklets go up to 256 KiB and cold ones up to
1 MiB. There are 481 booklets in all, 7983 CIDs (chunks, booklets, tree
nodes, glue nodes, icons and the root) for 34.7 MB.

Booklets end when they are full. The packer can also end them where the
content says (`--content-cuts`: after a chunk whose CID ends low, one time
in eight past a quarter of the target). That was measured too:

| | Booklets | Booklets the older tree already had | Bound pairs sharing a booklet | Booklets to load a file with its includes |
|---|---|---|---|---|
| size only (default) | 481 | 36.6% | 73.4% | 4.37 |
| content cuts | 742 | 50.0% | 68.3% | 5.15 |

Content cuts keep more booklets across an edit but split bound files apart
and make 54% more booklets. Chunks are what an edit really reuses, and they
are reused either way, so the default is size only.

## 4. The glue

The glue is the space between the blocks, made concrete: a typed, weighted
link from every file to every file it uses, kept in each layer's glue node
(`ALXG`). The packer reads four kinds from the code itself (4759 links):

| Link | Weight | Found from |
|---|---|---|
| same module | 8 | a `.c` file and its `.h` |
| logic | 6 | a block manifest binding a program |
| include | 4 | `#include` |
| test | 2 | `test_x.c` and `x.c` |

The glue does three things:

1. **It decides layers.** A module no layer names goes to the layer that
   pulls it hardest (section 1).
2. **It packs bound files together.** Within a layer, each next file laid
   down is the one bound most strongly to those already laid, and booklets
   fill in that order. 73.4% of link weight lands with both ends in one
   booklet, against 70.4% when files go in path order. Loading a C file
   with everything it includes takes 4.37 booklets on average (median 3),
   against 4.54 (median 4) in path order.
3. **It says what to fetch next.** `alx_glue_prefetch` gives the booklets of
   the files a file binds most strongly. Fetching four of them alongside a
   file already brings 97.8% of the booklets its direct includes need
   (96.7% in path order).

The gains over path order are real but small. The repository is already
laid out by module, and a module is mostly one directory. The glue's larger
use is the first and third points: placing loose modules, and fetching
ahead.

**On the metaphor.** The matrix can be described as a lattice held together
by an attraction between related pieces, where pieces that bind strongly
sit close and travel together. That is a picture of what the glue does, and
the numbers above are the measured part of it. Nothing in the matrix is
physical or quantum. It is integers, hashes and links.

## 5. Formats

All big-endian, each with a 64-byte header, each at most one 1 MiB block,
each named by the CIDv1 raw sha2-256 of its bytes (`alx_matrix.h` has the
byte layouts):

- **ALXB v2 booklet:** a directory of (chunk CID, offset, length), then the
  chunks at 64-byte boundaries. Opening one checks every chunk against its
  CID.
- **ALXN fan-out node:** up to 256 child CIDs and the booklet count beneath.
- **ALXG glue:** the layer's files (CID, chunk refs), links and chunk refs.
  A ref names a layer, booklet and atom, because a chunk is stored once in
  the whole matrix.
- **ALXM root:** 145 entries (tree, glue, icon, booklet count) and a
  SHA-256.

Nothing in any node is code. A resolved chunk is checked bytes handed to its
caller, and running anything still goes through a verified slot and zxapp's
checked call (GRID_CHAIN.md section 1).

## 6. Tested

`make test-alexandria` packs the repository and then checks the packed
matrix (`test_alx_matrix`):

- It walks every tree node down to every booklet and opens each one,
  checking every chunk's CID.
- It rebuilds every one of the 2422 files from its glue refs, byte for byte
  against the repository, cutting each chunk again with the kernel's own
  chunker.
- It checks that booklet 000 holds the alphabet.
- It checks that prefetch never repeats a booklet or names one the file
  already needs.
- It refuses changed and malformed nodes of each kind.

The chunker's tests cover the tier bounds, determinism, the fixed gear
table, and that inserting one byte keeps all but the first few cut points.

## 7. Not built yet

- **The bucket layout:** where the matrix sits beside Disc 1 and the swarm
  packs. This belongs to the Bare-metal thread's ipfs-dist, and is in
  coordination.
- **L2 and L3 layers.** These go on top when those apps exist.
- **Placement of the class 000 to 900 booklets.** The blocks' Alexandria
  classification stays an index axis (the grid word and the core index,
  CARTOGRAPHY.md), and the matrix places the blocks' files by app.
- **The vector plane:** embeddings in grid domain 0x60, with a summary
  vector per node. Built: see VECTORS.md.
- **Proposed, not built: the user's Enochian script as the core logic
  notation.** The script is the user's own, mathematically derived, and the
  licence is theirs to grant. The proposal is a glyph notation over the
  existing core: each glyph maps one-to-one to a primitive (the lpres
  Belnap gates, the AXION checks, the booklet 000 alphabet), and the kernel
  keeps its integer opcodes. Nothing is built until the script's definition
  is in hand.

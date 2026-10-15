<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The 144-layer matrix

After the core boots, the matrix holds everything the system needs at its
root: every block of code, table and document, packed into content-addressed
pieces that the system can fetch, check and reuse. It has 145 layers. Layer
0 is the core, and its first booklet is booklet 000, the unified alphabet
(`kernel/src/lingua/alphabet.tsv`, the projection runtime, and the
alphabet's Dhātu, number and sound views; see ALPHABET.md), so the alphabet is the first thing
the matrix gives after boot. Layers 1 to 144
are the 144 L1 system apps in the order of the app catalogue, twelve groups
of twelve, each with its icon. The L2 and L3 app layers go on top of these.

The code is `kernel/src/alexandria/alx_cdc.c` (chunking),
`kernel/src/alexandria/alx_matrix.c` (the node formats and the walks),
`tools/alexandria/pack_matrix.py` (the packer and the measurements) and
`kernel/src/alexandria/MATRIX.tsv` (which code lives in which layer). The
numbers in sections 1 to 4 come from running the packer on `kernel/src` and
`docs` (2422 files, 34.7 MB) on 2026-10-15. Since then the matrix holds every
tracked file of the repository: the kernel, the host app and daemon, boot,
the SDK, tools, tests, fonts, sounds and documents. That is 10,467 files and
110 MB, cut into 16,191 chunks in 928 booklets, stored compressed in 40.3%
of their size (assets such as fonts are already dense).

## 1. Layers

`MATRIX.tsv` has one line per layer: the app, its group, its icon file, and
the paths whose files live there (the longest matching path wins). A shared
piece lives in exactly one layer; other layers reach it through the glue
(section 4), never through a second copy.
The last column marks the twelve Fibonacci layers, the anchors of the
spine (section 9).
LAYER_MAP.md writes the whole mapping out layer by layer, with booklet
000's contents and where each unnamed module landed.

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
CID, and the packer stores the icons when given the icon folder. The icons
are the vector (SVG) L1 set, at most 22 KB each, so every one fits a block
and renders at any size. The packer fails if one is missing or over 1 MiB,
instead of leaving its slot empty.

## 2. Chunks: amount, size and density

A file is cut where its bytes say (normalised gear-hash content-defined
chunking, `alx_cdc.h`), not at fixed offsets. That way an edit moves only
the chunks around it, and every chunk is named by the CID of its bytes and
stored once in the whole matrix. Each file gets one of three tiers:

| Tier | For | Min / average / max | Chunks | Mean chunk | Compresses to |
|---|---|---|---|---|---|
| hot | code up to 256 KiB | 4181 / 6765 / 17711 bytes | 6769 | 3.7 KiB | 34.4% |
| warm | text, tables, documents | 4181 / 6765 / 17711 bytes | 398 | 7.2 KiB | 34.6% |
| cold | generated bulk; content already dense (over 7.2 bits per byte) | 64 / 256 / 1024 KiB | 28 | 223 KiB | 28.4% |

Hot and warm now cut at the Fibonacci knee measured below. They are still
two tiers because they get different booklet sizes. The chunk counts and
means in this table are from the earlier 1/4/16 KiB and 4/16/64 KiB cut. With
the Fibonacci tiers the tree is 4948 chunks.

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

**Golden-ratio sizes, density and compression (measured 2026-10-15).** The
user asked for chunk size weighted by the golden ratio, balancing size
against density: too big is inefficient, too small means too many CIDs, and
compression is critical. `alx_cdc_make` builds a tier of any sizes,
Fibonacci ones included, under the same rule (the cut compares the top 32
bits of the hash with a threshold; for powers of two that is exactly the
mask rule, so the three tiers above cut as before). Every text file
(hot and warm) was cut with one tier, and the cold tier was kept. The
comparison is with the tree of 2026-10-12. "Changed reuse" is the share of
the changed files' bytes already stored. "Update" is what an update from
that tree ships, in compressed bytes. "Per chunk" is storage with each
chunk compressed on its own. "Per booklet" is storage with each 64 KiB
booklet compressed as one. zlib stands in for a compressor here; there is
none in the kernel yet.

| Min / avg / max | CIDs | Mean chunk | Changed reuse | Update | Per chunk | Per booklet |
|---|---|---|---|---|---|---|
| today: hot 1K/4K/16K, warm 4K/16K/64K | 7224 | 4.8 KB | 51.0% | 3.98 MB | 11.58 MB | 9.43 MB |
| 987 / 1597 / 4181 | 15903 | 2.2 KB | 69.8% | 4.23 MB | 13.01 MB | 9.40 MB |
| 1597 / 2584 / 6765 | 10438 | 3.3 KB | 63.8% | 4.05 MB | 12.23 MB | 9.41 MB |
| 2584 / 4181 / 10946 | 7031 | 4.9 KB | 57.3% | 3.92 MB | 11.61 MB | 9.42 MB |
| **4181 / 6765 / 17711** | 4948 | 7.0 KB | 49.5% | 3.82 MB | 11.11 MB | 9.43 MB |
| 6765 / 10946 / 28657 | 3700 | 9.4 KB | 38.9% | 3.77 MB | 10.73 MB | 9.42 MB |
| 10946 / 17711 / 46368 | 3029 | 11.5 KB | 29.1% | 3.74 MB | 10.47 MB | 9.42 MB |

What the numbers say:
- **The golden ratio between min, average and max changes almost nothing.**
  The same averages cut with the old 1:4:16 spacing give about 6% more CIDs
  and about one point more reuse. At an equal number of CIDs the two are
  within one point of each other. The normalised cut, not the ratio, sets
  the shape.
- **Picking the size by density does not help.** Smaller chunks for denser
  files, or for sparser ones, at Fibonacci sizes around the same centres:
  every variant landed on the same CIDs-against-reuse curve as one size for
  all, never above it.
- **Compression is the largest effect, and it changes which size wins.**
  Small chunks reuse more raw bytes, but they compress worse, so an update
  ships more: 4.23 MB at a 1597-byte average against 3.82 MB at 6765. Above
  6765 the update shrinks by under 2% per step while reuse keeps falling.
  4181 / 6765 / 17711 is the knee: against today, 31% fewer CIDs, 4% less
  shipped and stored, and 1.5 points less raw reuse.
- **Compress the booklet, not the chunk.** One 64 KiB booklet compressed
  as a whole stores 9.43 MB, 15% less than chunks compressed one by one,
  and within 4% of compressing the whole tree as one stream (9.05 MB). At
  that level, chunk size hardly matters for storage. Atom CIDs stay the
  hash of the plain bytes, so deduplication is unchanged; the booklet's own
  CID would be over its stored, compressed bytes.

Adopted: hot and warm text cut at 4181 / 6765 / 17711. The Mac build
measured Fibonacci tiers on its own (3.4% fewer chunks, the same reuse) and
seals with this cutter. Cold stays at 64 / 256 / 1024 KiB: only 28 chunks
are cold, too few to tune.

**The booklet compressor (`alx_z.c`).** It writes the LZ4 block format, so
any standard LZ4 decoder reads it; a reference LZ4 library decoded every
block it made. The encoder is its own (a hash-chain search, one step of lazy
matching, integers only, no allocation), so the same booklet gives the same
stored bytes, and the same CID, on every platform. A fixed-answer test pins
the output bytes. On this tree, 486 booklets (35.1 MB) are stored in
12.4 MB, 35.3%. For comparison on the same files: the reference LZ4 at its
strongest setting lands within about 1.5 points of this encoder. zlib gets
about 26.8%, a quarter smaller again, because it adds entropy coding, which
the LZ4 format does not have. That is the next gain on offer if storage
matters more than decoding speed.

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
- **ALXG v3 glue:** one per layer, empty layers too: the state (N or T),
  the layer's files (CID, chunk refs), links and chunk refs, the glue CID
  of the anchor it hangs from and, for a branch, of its pattern layer
  (sections 9 and 10). A ref names a layer, booklet and atom, because a
  chunk is stored once in the whole matrix; the layer is the ref's own or
  one in its lineage.
- **ALXM v2 root:** 145 entries (tree, glue, icon, booklet count; every
  layer has its glue) and a
  SHA-256.
- **ALXZ stored booklet:** a booklet compressed (`alx_z.h`): codec (1, the
  LZ4 block format), the plain length, the payload length, and the plain
  booklet's CID, then the payload. Its own CID, the one tree nodes name, is
  over the stored bytes. Opening it decodes the payload and checks it
  against the plain CID. A booklet that would not shrink is stored bare, as
  its ALXB bytes, so a stored block is never larger than its booklet. Chunk
  CIDs are always over plain bytes, so deduplication is the same either way.

Nothing in any node is code. A resolved chunk is checked bytes handed to its
caller, and running anything still goes through a verified slot and zxapp's
checked call (GRID_CHAIN.md section 1).

## 6. Tested

`make test-alexandria` packs the repository and then checks the packed
matrix (`test_alx_matrix`):

- It walks every tree node down to every booklet and opens each one,
  checking every chunk's CID.
- It rebuilds every file from its glue refs, byte for byte
  against the repository, cutting each chunk again with the kernel's own
  chunker.
- It checks that booklet 000 holds the alphabet.
- It checks that prefetch never repeats a booklet or names one the file
  already needs.
- It refuses changed and malformed nodes of each kind.

The chunker's tests cover the tier bounds, determinism, the fixed gear
table, and that inserting one byte keeps all but the first few cut points.

## 7. Not built yet

- **The bucket layout** (agreed with the Bare-metal thread's ipfs-dist,
  which owns it): the matrix is bucket `07_matrix`, holding the root as
  `<CID>.alxm` (self-named; `pack_matrix.py --out-dir` writes it, and
  MATRIX.ROOT is the same bytes for local tools) and every node as
  `blocks/<CID>`. The library index is `05_alexandria`, the swarm cells
  `06_tiamat`, and the vector plane `08_vectors`, with its own root CID in
  the installer file. Disc 1 carries layer 0, booklet 000 included, under
  `/ZXV/MATRIX/<CID>`, and its baked table holds the matrix root CID.
  Layers 1 to 144 are fetched as raw blocks when an app needs them; only
  the installer's local pin store is sector-aligned (ALXI). So any change
  to layer 0 or to the matrix root needs a new Disc 1.
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
  four-valued gates, the AXION checks, the booklet 000 alphabet), and the kernel
  keeps its integer opcodes. The original script tops out at a fixed number
  of dimensions; ours has no cap in the format (the dimension count is a
  variable-length field) and renders as many as a structure needs, projected
  to the screen. Each instance is still bounded by its own memory and fuel
  budget. Nothing is built until the script's definition is in hand.

## 8. Lattice geometry: one coordinate, every store

The matrix has one coordinate system, and the runtime, a browser and bare
metal all use it through the same code (`alx_lattice.h`).

**The coordinate.** Every place in the matrix has a 64-bit grid word in
domain 0x30 (GRID_CHAIN.md section 3):

| Bits | Field |
|---|---|
| 63..56 | 0x30, the matrix |
| 55..48 | layer, 0 to 144 |
| 47..24 | booklet + 1 (0 means the layer itself) |
| 23..0 | atom + 1 (0 means the whole booklet) |

So `0x3090000000000000` is layer 144, `0x3000000001000000` is booklet 000,
and `0x3007000001000001` is the first chunk of layer 7's first booklet. A
word's (layer, booklet, atom) is exactly a glue ref, so a file is the list of
words its glue names. Which files are in which layer is LAYER_MAP.md;
which word a library slot has is the grid word of GRID_CHAIN.md.

**The binding to the hashes.** The root's CID binds everything under it.
The root lists each layer's tree CID. Each tree node lists its children's
CIDs. Each stored booklet is named by the CID of its stored bytes; a
compressed one carries its plain CID; each chunk inside is named by its own
CID. Resolving a word walks that chain from the root: the layer's entry, one
digit of the booklet number per tree level (256 children a level), the
booklet, then the atom. Every block is checked against the CID that named
it before anything in it is used, so a word under a given root CID always
reaches the same bytes, or an error.

**One resolver, three stores.** `alx_lat_resolve` takes the root, the word
and a fetch function that returns a block for a CID. The store is the only
thing that changes:
- The runtime fetches from its block store or from peers by CID.
- Bare metal reads an ALXI image (`tools/alexandria/mkimage.py`): one file
  or partition with the root, every block at a 4096-byte sector, and a
  table in CID order. `alx_img_fetch` finds a block by binary search and
  returns its sectors.
- A browser maps the same image into WASM memory and uses the same fetch.
  The resolver, the matrix code and the codec build for wasm32 with nothing
  outside them but sha256, which the build checks. No browser host runs
  them yet.

`test_alx_lattice` resolves every layer, every booklet, and the first,
middle and last atom of each through both the block store and the image.
Both give the same CID and bytes. A file rebuilt from its glue words
matches its CID. The image is deterministic: the same blocks always give
the same image.

**What a hash gives, and what it does not.** If a bit flips on disk or in
transit, the block no longer matches its CID. The resolver reports which
block failed, by CID, and the node can fetch that exact block again from
another copy (the test damages an image block and then recovers it from the
block store). A missing block is named the same way. That is detection and
exact re-fetch. It is not a safety check of what the bytes do: a block that
matches its CID is exactly the block that was packed, good or bad. Trust in
the content still comes from who signed and reviewed it (cartography,
attestations), and the usual checks on code before it runs still apply.

**Growth without rewriting.** A block never changes; a new version is new
blocks and a new root. Unchanged layers keep their tree CIDs (43 of 81 did
between 2026-10-12 and 2026-10-15), so a new root shares everything that
did not change. Additions (overlays, letters of marque, new models) arrive
as new layers' content or new roots, and an older root still resolves
exactly as before.

## 9. The spine: the Fibonacci layers

The layers whose numbers are Fibonacci numbers are the prime dimensional
layers: 0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89 and 144 (F0 to F12; F1 and F2
are both layer 1). They are structurally critical, so the matrix is built
around them as a spine, and the top of the chain, 144 = F12, is itself one.
`MATRIX.tsv` flags each in its anchor column, and the packer refuses the
file when the column disagrees with the rule.

Every other layer is a branch. By Zeckendorf's theorem every whole number
is a sum of non-consecutive Fibonacci numbers in exactly one way, so every
branch splits one way only as N = F(k) + M, with F(k) the largest Fibonacci
number at most N and M smaller than F(k-1): 4 = 3 + 1, 12 = 8 + 4 (and 4 =
3 + 1), 100 = 89 + 11, 143 = 89 + 54. The branch hangs from anchor F(k)
and repeats the pattern of layer M, so the band from F(k) up to F(k+1) - 1
repeats the layers 0 to F(k-1) - 1: the same shape at every scale, and it
goes on past 144 with no new rule (145 = 144 + 1, ..., 233 the next
anchor).

- **Lineage.** A layer may draw on its lineage: an anchor on the anchors
  below it; a branch F(k) + M on the anchors up to F(k), on M and on M's
  lineage. Every chunk is stored once, in the deepest layer that is the
  lineage (or the self) of every layer using it. An anchor's chunks so live
  in anchors only, a branch's in its own lineage, and no layer leans on a
  stranger. A chunk two branches share goes down to their deepest common
  ancestor, which is a user of it or an anchor; an anchor may so hold
  chunks its band shares before any of its own files land (the report
  lists these and the bytes moved: `adopted_chunks`,
  `anchors_holding_only_shared_chunks`).
- **Hung from the anchor and the pattern.** Every layer has a glue node,
  empty ones too. It names the glue CID of the anchor the layer hangs from
  (a branch: F(k); an anchor: the anchor below it) and, for a branch, the
  glue CID of its pattern layer M. Changing an anchor or a pattern changes
  every layer above it in the lineage, and so the root.
- **States.** A glue node carries an lpres state: T when the layer holds
  content, N when it is an empty scaffold layer. An N layer carries nothing:
  no files, no links, no refs, no booklets. Nothing is faked to fill a
  position; a layer becomes T when its content lands, resolved and
  checked. (F and B, a failed check and witnesses that disagree, belong to
  the app gate and the witnessed calls, never to a sealed glue node.)
- **No upward lean.** An anchor's glue keeps no link into a branch layer's
  files (the branch's glue keeps the link from its side). A branch's links
  into layers outside its lineage are kept, because the code makes them,
  and counted (`cross_lineage_edges`) so they can be moved home.
- **Order.** Because every chunk's layer is fixed by the lineage rule
  before anything is laid out, the order layers are packed in changes no
  byte; `--seal-order` is kept for older scripts. Fetching and building
  run core first and outward (section 10).

`alx_mx_anchor`, `alx_mx_anchor_below`, `alx_mx_split` and `alx_mx_draws`
give the rules in the kernel; `alx_glue_check` refuses a glue node whose
anchor or pattern is not the rule's, whose state is not N or T, whose N
carries anything, or one of whose refs leaves the lineage.

## 10. The frame: three star tetrahedra

The positions come from geometry, so they are generated, not authored. A
star tetrahedron is a cube's corners split by sign parity: an even number of
minus signs gives one tetrahedron (+), an odd number the other (-). The same
split exists in eight dimensions, where the 8-cube's 256 corners fall into
two half-cubes; in doubled E8 coordinates (`kernel/src/e8/e8_lattice.h`) the
even half is E8's 128 half-integer roots and the odd half lies in E8's
mirror coset. Alternating polarity is that sign-parity flip.

- **The anchors are the corners.** The twelve anchors are the front
  corners of three nested star tetrahedra, four each: 0, 1, 2, 3 at scale
  1; 5, 8, 13, 21 at scale phi^4 = 2 + 3 phi; 34, 55, 89, 144 at scale
  phi^8 = 13 + 21 phi. Each star closes on a multiple of 3 (F4 = 3, F8 =
  21, F12 = 144): every fourth Fibonacci number is divisible by 3, so the
  trinary step is the arithmetic's own. The front alternates by star, as
  Seal I draws it (`/mnt/project-files/zxv-nonlinear/NONLINEAR_SCHEMATICS.md`
  6.1): + for stars 0 and 2, a corner (a, b, c, 1, 1, 1, 1, 1) with abc =
  1, times the scale, a scaled root of E8; - for star 1, the opposite
  corner, odd sign parity, in E8's mirror coset. The four corners behind
  each star are scaffold: they hold no layer and read N.
- **The branches are generated.** pos(F(k) + M) = vertex(F(k)) - pos(M) /
  phi. Dividing by phi is exact in Z[phi] ((p + q phi) / phi = (q - p) + p
  phi), and the minus sign turns the pattern over at every level of the
  split: an iterated function system, so the frame is a fractal, and the
  next star (233, 377, 610, 987 at phi^12 = 89 + 144 phi) extends it with
  no new rule. All 145 positions are distinct.
- **The fill follows the alternating current** of the tensor engine
  (`zt.h` T12), as two half-cycles that meet at the core. The negative half
  runs core out and is the build and fetch order: the core, then star by
  star its anchors and then its bands (0 1 2 3, 5 8 13 21, 4 6 7 9 ... 20,
  34 55 89 144, 22 ... 143). Every layer comes after its whole lineage, so
  every CID a glue node names exists before it; the packer seals the glue
  in this order. The positive half runs outermost in, the same order
  reversed: the sweep that checks each layer and gathers its state into
  the anchor it hangs from, and the anchors into the core, where the root
  closes. A hash chain cannot be sealed outer-in (a glue node names its
  ancestors' CIDs), so the inward half checks and gathers, it does not
  hash. `alx_mx_fill` and `alx_mx_gather` give the two orders; the fold
  record (below) carries the gathered counts per anchor.

`docs/spec/ZECKENDORF_ANCESTORS.tsv` writes the lineage rule out, one row
per layer (anchor, pattern, lineage). `pack_matrix.py --ancestors-table`
generates it, make checks it is current, `test_alx_matrix` checks the
kernel's `alx_mx_draws` against every pair of layers in it, and the
fabricator (appfab) checks against the same file.

**Folding until stable.** The build is the pattern folded until the matrix
stops changing: each fold generates the positions, the fabricator packs the
layers whose content now resolves, and the packer seals a root.
`pack_matrix.py --fold-state FILE` records each fold (count, root, the root
before it, T and N counts) and writes `STABILIZED` (the root CID and the fold
count) in the output folder when the fold has converged: the root is the one
the last fold recorded, every ref lands on a booklet of this pack, every
layer with content is T and every scaffold layer N carries nothing. The
witness tests are the test suite's, so the seal needs both: a STABILIZED
record and a green run. make packs the tree twice and requires the second
fold to be STABILIZED with the same root.

`alx_mx_pos` computes a layer's position in the kernel. The packer writes
the same table as `SPINE.tsv` (layer, F index, anchor, pattern, state, the
layer's step in the fill, the eight coordinates as p:q), and `test_alx_matrix` checks the two agree for
every layer, that every position is distinct, that every anchor sits on an
even corner, and that every branch satisfies the recursion; it also checks
Zeckendorf's uniqueness for every number below 377 and that the kernel's
split takes the largest part.

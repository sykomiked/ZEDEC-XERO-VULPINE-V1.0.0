<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The vector plane

The matrix (MATRIX.md) answers "give me exactly this", by coordinate and
CID. The vector plane answers "what is like this", by nearest neighbour.
Every vector points back to the exact thing it describes, so a fuzzy search
always ends in exact, checked bytes. The code is
`kernel/src/alexandria/alx_vec.c` (nodes, distances, summaries, the shape
embedding), `alx_vec_zt.c` (the same dot products on the tensor engine) and
`pack_matrix.py --vectorize`.

## 1. Rules

1. **Every vector names its source and its model.** A record holds the
   CID of the file it describes, and its grid word when it has one. A node
   holds the CID of the model that computed it.
2. **Stale is reported, never hidden.** If a file's bytes change, its CID
   changes, the layer's glue no longer lists the old CID, and the vector is
   stale. If the model changes, every vector it made is stale.
   `alx_vec_stale` counts both. The test changes one file and checks that
   exactly one vector goes stale.
3. **Integers only.** Vectors are int8 with an integer scale (Q16). Dot
   product, squared distance and cosine ranking are exact integer
   arithmetic. Cosine is compared without square roots, as
   `dot_a * |dot_a| * norm_b` against `dot_b * |dot_b| * norm_a`, in 128-bit
   integers.
4. **Our own embeddings only.** Vectors come from the deterministic shape
   embedding below, or from a Tiamat cell's embeddings handed to the packer
   (`--vectors`, the same format, with that checkpoint's CID as the model).
   No outside service.

## 2. Coordinates: domain 0x60

The user's sketch put embeddings in class 600. In the Alexandria
classification, class 600 is already money, chance and decisions. The grid
word's domain field has a place reserved for embeddings: 0x60
(GRID_CHAIN.md section 3). A vector's word is its source's word with the
domain set to 0x60, keeping the same place, item and language. So the vector
of `@512.01-001` is `0x6002000100000001`, and stripping the domain gives the
exact coordinate of what it describes. Files without a word yet (most of the
repository) are identified by CID alone, and their word is 0.

## 3. The node

One format at every level (`alx_vec.h` has the bytes). A node holds
records (target CID, word, scale), then rows. Row n, after the last record,
is the node's **summary**: the mean of its rows, requantised to int8 with
its own scale. Opening a node recomputes the summary exactly and refuses a
node whose summary does not match.

- **Leaf:** one row per file of a layer. It names the layer's glue CID as
  its source.
- **Layer summary:** used when a layer needs more than one leaf. It has one
  row per leaf, and each row is that leaf's summary.
- **Top:** one row per layer that holds files (81 today), each that layer's
  summary.

So every node carries a summary vector of everything under it, and the
same rule holds at each level.

Rows are stored as GGUF Q8_0 blocks whose scale is exactly one. The tensor
engine's kernels multiply these rows in place:
- its GPU whole-matrix hook on Apple silicon (Metal), where there is one;
- otherwise its SIMD row kernels;
- otherwise alx_vec's own loop.

All three return the same integers. The test checks every kernel set this
machine has (AVX-512 VNNI, AVX-512, AVX2 and the C reference) against the
loop. The Metal path uses the same kernel contract, but it runs only on a
Mac and was not run here. The tensor engine's own notes say a single query
costs the GPU more than the CPU (about 110 µs to encode and wait), so the
GPU pays off for batches of queries.

## 4. The shape embedding, chosen by measurement

Until the Tiamat cells' embeddings are wired in, each file gets a **shape
embedding**: every distinct identifier in it (a name such as `alx_vec_open`
or `sha256`), counted once, hashed into 512 signed buckets, then int8. It
is a fingerprint of which names a file uses. It has no learned meaning, it
is deterministic, and it needs no model.

It was chosen by testing which variant finds the right related file. For
every `x.c` in the repository that has an `x.h` beside it (598 pairs among
2426 files), we checked where `x.h` ranks among all other files by cosine.
Picking one file at random would put it in the top 10 0.41% of the time.

| Variant (float trial, 256 or 1024 buckets) | x.h first | x.h in top 10 |
|---|---|---|
| byte 4-grams, raw counts, 256 | 4.2% | 9.0% |
| byte 4-grams, log counts, 1024 | 42.5% | 86.6% |
| identifiers, raw counts, 1024 | 6.4% | 16.7% |
| identifiers, log counts, 1024 | 50.5% | 90.3% |
| identifiers, each once, 1024 | 73.6% | 96.3% |

The last variant, built in integers:

| Buckets | Bytes for the whole plane | x.h first | top 5 | top 10 | Mean rank |
|---|---|---|---|---|---|
| 256 | 0.8 MB | 66.2% | 89.5% | 92.5% | 9.8 |
| **512 (chosen)** | 1.5 MB | 71.1% | 92.8% | 96.2% | 5.1 |
| 1024 | 3.0 MB | 73.4% | 95.0% | 97.7% | 2.3 |

512 is the knee: doubling to 1024 doubles the bytes for 1.5 points more.
Raw counts fail because every C file shares the same common words, so all
files look alike. Counting each name once is what separates them.
`make test-alexandria` fails if the top-10 rate drops below 90%.

## 5. Two ways in

- **Exact:** a coordinate or a CID resolves through the index, the matrix
  tree and the glue to checked bytes (GRID_CHAIN.md, MATRIX.md, no search).
- **Similar:** a query vector is compared with the rows of a node
  (`alx_vec_search`, exact top-k, ties to the lower index). Every hit is a
  record naming a CID, so it leads straight back to the exact path.

Searching through the summaries first is cheaper but not exact. It was
measured as how much of the true top 10 (over all 2426 files) is found by
searching only the B layers whose summaries are closest:

| Layers searched (of 81) | 1 | 2 | 4 | 8 | 16 |
|---|---|---|---|---|---|
| Share of the true top 10 found | 38.8% | 54.6% | 67.4% | 79.0% | 88.6% |

The layers group files by app, not by meaning, so a file's nearest
neighbours are spread across layers. The summaries are a way to prune a
search, not a replacement for it. A full search of all 2426 files is one
pass over 1.5 MB.

## 6. What "holographic" can and cannot mean here

Suppose a piece is missing: a booklet that failed its check, or a file whose
bytes are gone. What the rest of the system knows still pins it down
exactly:
- the glue gives its CID, layer, booklet and every chunk's CID;
- its grid word gives its coordinate;
- its vector, and its neighbours' vectors, say what kind of thing it is and
  which other files are like it.

That is enough to say exactly what is missing and to ask for it by CID (a
retrieval letter of marque to peers or the archive). Whatever comes back is
checked against that CID.

**It cannot recreate the missing bytes.** A vector is a few hundred bytes
summarising thousands. The lost content is not in it, and no amount of
neighbouring vectors puts it back. The only way to get the bytes is to fetch
a copy.

## 7. Built, and not yet

Built, all tested by `make test-alexandria` (`test_alx_vec`):
- **Nodes and summaries:** the node format, summaries checked on open, and
  the refusals.
- **Integer metrics:** checked against a long-double reference.
- **Search:** checked against a full sort.
- **Tensor engine kernels:** checked against the plain loop.
- **Shape embedding.**
- **Packer:** `pack_matrix.py --vectorize 512 --shape`, which writes leaves,
  layer summaries and the top node (`VECTORS` names it).
- **Staleness:** against the current glue and model.
- **Measurements:** those above.

Not yet:
- **Tiamat-cell embeddings.** The format and `--vectors` input are ready,
  but no cell has been run here. The weights are not in this repository.
- **Data-shape vectors from the vector thread.** These are in coordination.
- **Vector words for library slots.** The four library slots have grid
  words; the matrix's files do not yet.
- **An approximate index (a graph or clusters) for large planes.** Flat
  exact search is fine at this size.
- **The retrieval letter itself.** It goes through the node's network path
  (evo_comm), as for cartography.

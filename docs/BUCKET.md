<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# The release bucket: named by CID, built leaves first

A ZXV release is one IPFS directory, the bucket. Every file in it is named by
its own CID, and the root of the bucket, the **origin CID**, is the one
identifier a person needs to install everything (`tools/zxv-get ORIGIN_CID`).
The layout and the installer file are written by `build_system/mk_bucket.py`;
`make ipfs-dist` builds the whole release twice and checks both passes agree.

## The naming rule

1. **A leaf is named by its own CID.** A disc image, a layer manifest, a
   cartridge or a document is stored as `<its CID>.<ext>`. Its name is a
   statement about its bytes: change one bit and the name is false.
2. **A parent's identity is made from its children's names and CIDs.** A
   directory is a UnixFS node whose links each carry a child's name and CID,
   so the directory's CID is computed from them and from nothing else.
3. **The names converge, layer by layer, into one CID.** The installer file
   lists every leaf by CID and is itself named by its CID; the root holds it
   and the directories, and the root's CID, computed last, is the origin.

So the build runs from the leaves to the origin, and installing runs the
other way, from the origin down to the leaves, checking each name on the way:
the install is the build's photographic negative. Nothing can name what does
not exist yet, which is why the order is fixed:

```
build (leaves first)                         install (origin first)
  swarm discs          -> their CIDs           origin CID
  L0                   -> its CID                -> <cid>.install, name checked
  kernel + Disc 1 EFI  (both CIDs baked in)        -> each file it lists, by CID
  L1, L2, core discs   -> their CIDs                 -> every block, by CID
  <cid>.install        -> its CID
  root                 -> the origin CID
```

The kernel inside Disc 1 is built after the swarm discs and L0, so it carries
their CIDs in its read-only data (`build_system/mk_table.py` writes
`build/include/manifest_hashes.h`; `ZXV_SWARM_DISCS` names the images). On
bare metal it fetches each swarm disc by that CID and checks every block
against it; nothing a gateway or a directory says can change which disc is
installed. Disc 1 cannot carry the installer file's CID or the origin's,
since both are computed from Disc 1; nor L1's or L2's, since L1 lists the
boot file that holds the kernel and L2 names L1. Those stay linked by parent
CIDs (docs/LAYERS.md).

Consequences:

- Any change to a swarm disc or to L0 means a new kernel, so a new Disc 1,
  a new installer file and a new origin.
- Later updates do not rebuild Disc 1: they come as overlays (L4,
  `04_overlays`), each naming the CID it stands on.
- Two builds of different bytes can never collide: their names differ.

## A worked example, three levels

One swarm disc (17 bytes here, `swarm disc bytes\n`), its directory, and the
root. Every CID below was computed by `mk_bucket.py` and agrees with
`tools/zxv-cid.c`.

**Level 1, the leaf.** The image is one chunk, so its CID is its raw leaf:

```
bafkreigvl6kujflwmxe2helrh3m2cjql36jgcs4bghgvlis7cdyfwlcgca
```

and it is stored as `01_swarm/bafkreigvl6kujflwmxe2helrh3m2cjql36jgcs4bghgvlis7cdyfwlcgca.img`.

**Level 2, the parents.** The directory `01_swarm` holds one link, named
`bafkreigvl6...cgca.img`, pointing at that CID. Its own CID comes from that
name and CID: `bafybeigob3qxsu3codxz22fqqolbxfvnnmsro5yrjttummyglsox5dzfb4`.
The installer file lists the leaf:

```
# zxv-install 1
max_disc_bytes 1800000000
seal PROMPT_USER
swarm all 2 REASON 17 d55f9544957665c9a391713ed9a1260bdf92614b8131cd55a25f10f05b2c4610 bafkreigvl6kujflwmxe2helrh3m2cjql36jgcs4bghgvlis7cdyfwlcgca 01_swarm/bafkreigvl6kujflwmxe2helrh3m2cjql36jgcs4bghgvlis7cdyfwlcgca.img
```

and is named by its own CID,
`bafkreift6fyzq4f2pbbvlogktlgd67mcwqushexskrhcq5ae5oioikfhbm.install`.

**Level 3, the origin.** The root holds two links, `01_swarm` and
`bafkreift6...fhbm.install`; its CID is the origin:

```
bafybeify6a2axuh7kwnsxjqqofe53whnhqy55weqwrlzbqa4qyescfsccy
```

Installing walks it back down: the root's links give the one `.install` file;
its bytes must hash to the CID in its name; it names
`01_swarm/bafkreigvl6...cgca.img`, which the `01_swarm` directory must link
to that same CID, and whose bytes must hash to it, be 17 bytes long and have
that SHA-256.

## The pinned add parameters

The same bytes give a different CID under other parameters, so every tool
that computes or checks a CID uses exactly these, which are Kubo's defaults
with CIDv1 (`ipfs add --cid-version=1`, also `--only-hash`):

| Parameter | Value |
|---|---|
| CID | version 1, multibase base32 lower (`b...`) |
| Hash | sha2-256 |
| Chunker | fixed size, 262144 bytes |
| Leaves | raw (codec 0x55); a one-chunk file is its leaf (`bafkrei...`) |
| DAG | balanced, UnixFS File nodes (dag-pb, 0x70), at most 174 links each |
| Directories | plain UnixFS directories, links sorted by name bytes (no HAMT) |

Checked against Kubo 0.32.1 by `mk_bucket.py --self-test`; `tools/zxv-cid.c`
and `zxv_publish_update.file_dag` are checked against each other at depth by
`build_system/test_bucket.py`; `tools/zxv-get` checks every block it fetches;
the kernel's `kernel/src/ipfs_node` walker checks every block it fetches.

## Checking a name with no IPFS node

```
tools/zxv-verify.sh bafkrei....install       # the installer and every file it lists
tools/zxv-verify.sh bafybei....img           # one file
```

`zxv-verify.sh` is POSIX sh. Shell cannot chunk and hash a file as UnixFS
does, so it compiles the one-file C99 helper `tools/zxv-cid.c` (its own
SHA-256, no libraries) and compares the CID it prints with the name. For an
installer file it also checks that each listed file is beside it in the
bucket, with the listed size and SHA-256 and a name that is its CID. It
prints `FALSE <file>: its CID is <cid>` for a name that is not true and exits
1. A tree line is checked against the root itself: `mk_bucket.py` writes the
root block beside the bucket as `<bucket>.root.pb`, `zxv-cid --links` hashes
it (it must be the origin in `<bucket>.cid`) and lists its links, and the
tree's name must link to the tree's CID.

## The installer file

ASCII, one item per line, in install order (core discs, layers, swarm discs,
cartridges, documents, then trees):

```
# zxv-install 1
max_disc_bytes 1800000000
seal <POWERDOWN_NIC|LOCAL_MESH_ONLY|PROMPT_USER>
gateway <url>                                   (any number: where to fetch)
<kind> <arch> <disc|-> <GROUP|-> <bytes> <sha256> <cid> <dir>/<cid>.<ext>
tree all - - <tsize> - <cid> <name>                 (with mk_bucket.py --tree)
```

A **tree** line names a directory or file at the root that another tool
built and `mk_bucket.py --tree DIR` links in by CID, as it is: the matrix,
its vector plane, the source bundle, the browser boot and its files. Its
size is the DAG's total size and it has no SHA-256 (`-`, allowed only for a
tree): each of its blocks is checked against its CID as it is fetched. Its
blocks are never re-chunked: a matrix booklet is one raw block of up to
1 MiB named by its raw CID, which the 256 KiB chunks of the pinned add
parameters would change. So with trees, `ipfs add -r` of the bucket folder
is not the origin; the origin is the root `mk_bucket.py` computes over the
bucket's own children and the tree links, and `--blocks` holds every block of
both. `zxv-get` fetches a tree after everything else: the root's link of
that name must be the listed CID, then each block is checked as it comes.
Links named `@NNN` inside the matrix (a layer's links to the layers it is
built from) are not followed: those layers are fetched at their own place.

`--tree DIR` reads `DIR/links.json` (`{"name": ["<cid>", <tsize>]}`) and
`DIR/blocks.car` (a CAR v1). It refuses a name that is not plain or that a
bucket folder uses, and an entry any of whose blocks is missing from the CAR
or does not match its CID, before it touches the bucket.

`zxv-get` refuses a root with no `<cid>.install` or more than one, an
installer not named by its CID, and a line whose file name is not its CID.

## Folders

The numbering is fixed for every release, so a folder keeps its number
whether or not the ones before it are present:

| Folder | Holds | Written by |
|---|---|---|
| `00_base` | the core discs, one per architecture, and the architecture summary | mk_bucket.py |
| `01_swarm` | the swarm discs | mk_bucket.py |
| `02_cartridges` | cartridges | mk_bucket.py |
| `03_layers` | the layer manifests L0 to L2 | mk_bucket.py |
| `04_overlays` | community overlays (L4) | the overlay work |
| `05_alexandria` | the Library of Alexandria (L3) | the Alexandria work |
| `06_tiamat` | the Tiamat cells | the Tiamat work |
| `07_matrix` | the 144-layer matrix, root `<cid>.alxm` | the Alexandria work |
| `08_vectors` | the matrix's vector plane, root `<cid>.alxv` | the Alexandria work |
| `09_source` | the source bundle | the release work |

Folders 04 to 09 follow the same rule: every file named by its CID, each
root listed in the installer file. They come in as trees (`--tree`), and so
do the browser boot (`index.html`, `boot.js`, `zxv_boot.wasm`, `brand/`,
`boot.json`), `GREETING.txt`, `MANIFEST.tsv`, the scripted installers
(`install.sh`, `install.ps1`) and `10_vm` (the Disc 1 launchers for a VM),
each linked at the root by its name and listed as a tree line.

## Not built yet

- The kernel does not yet fetch L3 or the matrix; it fetches the swarm discs.
- Seeding the installed CIDs back to peers, and pinning them on first boot,
  is the installer's next step.
- No signature covers the origin; the owner's release key is separate work.

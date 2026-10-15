<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# File fabrication: recipes of public blocks plus a private diff

Instead of sending a file, a sender sends a **recipe**: the CIDs of public
blocks the recipient can fetch from anyone (Alexandria booklets, model
blocks, any raw or dag-pb block up to 2 MiB), the byte ranges to copy out of
them in order, and the few bytes that exist nowhere public (the **diff**).
The recipient fetches the blocks, checks each one against its CID, rebuilds
the file in RAM and accepts it only if the result's CID equals the target
CID the recipe names.

The code is `kernel/src/fab` (format and API in `fab.h`), the tool is
`tools/zxv-fab`, and the tests run in `make -C kernel test-fab`, which
`verify-all` runs.

## 1. Rules

1. **Nothing is released unchecked.** Every block is hashed against its CID
   when it is read, whoever supplied it. The rebuilt bytes are released only
   if their UnixFS CID (Kubo defaults: CIDv1, 256 KiB chunks, raw leaves,
   the CID `ipfs add --cid-version 1` gives) equals the target. On any
   failure the output and diff buffers are wiped.
2. **Post-quantum only.** Recipes are signed with ML-DSA-87 (context
   `zxv.fabrication.recipe.v1`); a sealed recipe is encrypted to the
   recipient with ML-KEM-1024, HKDF-SHA256 and ChaCha20-Poly1305 (the same
   primitives as Boot Legger and the pqsec layer). There are no elliptic
   curve keys and no 32-byte public keys: the 32-byte fields in the header
   are SHA-256 fingerprints of the real keys.
3. **Data is never code.** A fabricated file is data. Nothing in the engine
   maps it executable; an executable it contains runs only if it passes
   verified-slot admission like any other file. The kernel side is integer
   only, has no libc and allocates nothing.
4. **Bounded.** At most 4096 blocks and 2^20 ops per plan, blocks of at
   most 2 MiB, every buffer supplied by the caller.

## 2. Two modes, two kinds

| | PUBLIC | SEALED |
|---|---|---|
| Plan and diff | in clear, signed | encrypted to one recipient, signed |
| Used for | app installs and anything else with nothing secret | private user data |
| Recipient key | none | ML-KEM-1024 decapsulation key |

**FILE** recipes make one artifact. **STREAM** recipes (always sealed) open a
stream: they make frame 0 and give both ends a stream key; later frames are
small AEAD records (section 6).

## 3. Wire format (big-endian)

Recipe:

| Offset | Field |
|---|---|
| 0 | `FABR` |
| 4 | version 1 |
| 5 | kind: 0 file, 1 stream |
| 6 | mode: 0 public, 1 sealed |
| 7 | zero |
| 8 | sender id: SHA-256 of the sender's ML-DSA-87 public key |
| 40 | recipient id: SHA-256 of the recipient's ML-KEM-1024 key (zero if public) |
| 72 | plan length |
| 76 | diff length |
| 80 | ML-KEM-1024 ciphertext, 1568 bytes (sealed only) |
| | plan, then its 16-byte tag (sealed only) |
| | diff, then its 16-byte tag (sealed only) |
| | sender ML-DSA-87 public key (2592) |
| | signature (4627) over SHA-256 of every byte before the key |

Plan (the same layout in recipes and frames):

| Offset | Field |
|---|---|
| 0 | target CID (36 bytes: CIDv1, sha2-256, raw or dag-pb) |
| 36 | target length (u64) |
| 44 | block count (at most 4096) |
| 48 | op count (at most 2^20) |
| 52 | frame period in microseconds: above 0 in a stream open, 0 otherwise |
| 56 | zero (8 bytes) |
| 64 | blocks: CID (36) and length (u32); no CID twice |
| | ops: kind (1), zero (1), block (u16), source offset (u32), length (u32) |

Ops fill the target in order, with no gap or overlap:

| Op | Copies |
|---|---|
| SLICE (1) | bytes [src, src+len) of block `block` |
| LIT (2) | the next `len` bytes of the diff (block and src are zero) |
| PREV (3) | bytes [src, src+len) of the previous frame (frames only) |

The op lengths add up to the target length and the LIT lengths to the diff
length. A listed block that no op copies from is a **decoy**: it is fetched
and verified like the others but never used.

Overhead: a public recipe costs 7,299 bytes plus the plan and diff; a sealed
one 8,899. That is small next to a file of megabytes and large next to a
file of a few hundred bytes, which should simply be sent sealed on a
session (Boot Legger, Vinea).

## 4. Keys and the air-gap order

    prk     = HKDF-Extract(salt = SHA-256(header || KEM ciphertext), ML-KEM secret)
    plan    = HKDF-Expand(prk, "zxv.fab.v1 plan", 32)     AAD: the 80-byte header
    diff    = HKDF-Expand(prk, "zxv.fab.v1 diff", 32)     AAD: the 80-byte header
    stream  = HKDF-Expand(prk, "zxv.fab.v1 stream", 32)
    id      = HKDF-Expand(prk, "zxv.fab.v1 stream id", 32), first 16 bytes

Each recipe key encrypts one message, so its nonce is zero. A changed
ML-KEM ciphertext yields a different (implicitly rejected) secret, so the
plan then fails its tag.

The receiving order is fixed by the API:

1. `fab_recipe_parse` checks the layout; `fab_recipe_check` checks the sender
   (against the caller's trusted ids, if given) and the signature. Nothing
   later runs on an unchecked recipe.
2. `fab_open_plan` uses the recipient key only to derive the plan key, opens
   the plan and checks every plan rule. The plan key is wiped.
3. `fab_hydrate` fetches and verifies every listed block, decoys included.
4. `fab_assemble` asks the **seal callback** first and goes on only if it
   answers `LPRES_STATE_TRUE`; FALSE, BOTH and NEITHER all refuse. Only then
   is the recipient key used again, for the diff key. The diff is opened,
   the ops applied in RAM and the target CID checked.

So in air-gap mode the diff key and the plaintext exist only after the node
reports that it has no live network path. The seal itself belongs to the
node: in the kernel it is whatever the Bare-metal thread's post-hydration
seal reports through the callback; in `zxv-fab --airgap` it is "every
network interface except loopback is down" read from Linux
`/sys/class/net`, and anything unreadable counts as not sealed.

### Progress

A loading bar may only move as fast as the work. `fab_io_t.progress`, when
set, reports what has been verified and nothing ahead of it:

| Phase | When | Counts |
|---|---|---|
| FETCH | after each block matches its CID (`fab_hydrate`, `fab_hydrate_from`) | blocks and their bytes, of the plan's totals (`fab_plan_bytes`) |
| BUILD | after each op is applied (`fab_assemble`, `fab_frame_apply`) | artifact bytes written from verified blocks and the authenticated diff, of `target_len` |
| DONE | once, after the rebuilt bytes match the target CID | `target_len` |

BUILD bytes are made only of checked inputs, but the whole file is checked
only at DONE; a bar that must never show unchecked output uses FETCH, then
jumps to full at DONE. A failed call reports nothing further.
`fab_hydrate_from` verifies a range of blocks, so a host can hydrate a few
blocks per frame it draws. zxvd exposes this as `fab.receive.start`,
`fab.receive.step` and `fab.receive.cancel` (section 5).

What the seal does not do: the recipient key was in memory for step 2 while
the network was up, and the engine cannot stop the operating system from
paging RAM to disk. Keeping the plaintext off persistent storage needs
non-swappable memory from the caller (the kernel's own allocator, or
`mlock` on a host).

## 5. App installs

An app install is a PUBLIC recipe. The app's blocks (its zxblock manifest
and its Alexandria booklets) are already public and already content
addressed, so the recipe lists them, the installer fetches what it lacks,
and the app is rebuilt and checked against the app's CID. There is no
encryption and no private diff; the publisher's ML-DSA-87 signature says
who published it, the target CID says exactly which bytes. Code inside the
app still enters only through verified-slot admission.

Apps reach fabrication through one shared interface, never their own copy:

| Call | Engine |
|---|---|
| send (make a recipe) | `fab_build`, `fab_plan_write`, `fab_recipe_seal` |
| receive (rebuild a file) | `fab_recipe_parse`, `fab_recipe_check`, `fab_open_plan`, `fab_hydrate` (or `fab_hydrate_from` in steps), `fab_assemble` |
| stream | `fab_recipe_seal` (open), `fab_frame_seal`, `fab_frame_apply`, `fab_checkpoint_sign`, `fab_checkpoint_verify` |

Over zxvd's socket these are the `fab.*` methods (`docs/ZXVD.md`, rules
in `kernel/arch/hosted/zxvd_fab.h`): `fab.send`, `fab.receive` (or
`fab.receive.start`, `.step` and `.cancel` for a loading screen, section
4, Progress), and
`fab.stream.start`, `.send`, `.sign`, `.open`, `.frame`, `.checkpoint` and
`.close`, with `fab.put`, `fab.keys` and `fab.status`. The air-gap seal is
a callback the host registers (`zxvd_fab_set_seal`); `airgap: true` is
refused until one is.

A stored Alexandria booklet may be an ALXZ (compressed) block. Its CID is
taken over the stored bytes, and the plain booklet's CID is carried
inside. A recipe lists the CID of the block as it is stored, because that
is what the receiver fetches and checks.

## 6. Streams

A stream is the same format with time slots. The open recipe (kind STREAM,
sealed) carries frame 0 and the frame period. Each later frame:

| Offset | Field |
|---|---|
| 0 | `FABF`, version 1, three zero bytes |
| 8 | stream id (16) |
| 24 | sequence (u64), strictly rising; frame 0 is the open |
| 32 | base sequence (u64): the frame its PREV ops copy from |
| 40 | slot (u64): presentation time in frame periods |
| 48 | plan length, diff length |
| 56 | plan and tag, diff and tag (ChaCha20-Poly1305 under the stream key) |

Frame nonces are 0x01 (plan) or 0x02 (diff), three zero bytes, then the
sequence; the receiver refuses any sequence it has already passed, so a
nonce is never accepted twice.

**Loss.** A frame with PREV ops applies only on top of exactly its base
frame. If the base was lost the receiver answers RESYNC and keeps its state;
the sender's next frame without PREV ops (built from public blocks and the
diff alone) brings it back. How often to send such frames is the sender's
choice between bandwidth and recovery time.

**Who can forge a frame.** Every holder of the stream key can make frames
that pass the AEAD. For one viewer that is only the sender. For many viewers
sharing one key (one open recipe per viewer, one frame sent to all) any
viewer could forge frames to the others. **Checkpoints** close that gap after
the fact: the sender signs, with ML-DSA-87, SHA-256 over (sequence, frame
CID) for a window of up to 128 frames. A receiver whose frames match gets
OK; one that accepted a forged frame gets STREAM (the contradiction is
detected, not hidden); one that lost a frame in the window gets RESYNC (it
cannot tell either way). A checkpoint is 4,699 bytes, so one per second
costs about 38 kbit/s and one every ten seconds about 3.8 kbit/s; forged
frames are caught only at the next checkpoint, never before display.

## 7. Measured, not claimed

`tools/zxv-fab/fab_bench.c` streams three synthetic 640x360 RGB scenes
(691,200 bytes a frame) at 60 fps, lossless, on an Intel Xeon at 2.80 GHz
in this project's build container (one core per stage):

    gcc -O2 ... tools/zxv-fab/fab_bench.c <FAB_SRC> -o fab_bench && ./fab_bench && ./fab_bench --udp --frames 600

| Scene | Wire bytes per frame | kbit/s at 60 fps | Build µs | Apply µs | Bit-exact frames |
|---|---|---|---|---|---|
| still room and sprite both public, sprite moves, 200 pixels change | 6,356 | 3,051 | 8,287 | 7,339 | 120/120 |
| same, sprite not public | 6,370 | 3,058 | 8,449 | 7,487 | 120/120 |
| fresh random noise every frame | 691,364 | 331,855 | 19,087 | 7,756 | 120/120 |
| raw frames, for scale | 691,200 | 331,776 | | | |

UDP on 127.0.0.1, 600 frames of the first scene in datagrams of at most
1,400 bytes, sent at 60 fps from one process to another: 600 applied, 0
lost, 0 failed; one-way latency from the first datagram sent to the frame
rebuilt and CID-checked averaged 9,167 µs, worst 18,255 µs. Most of that is
the apply step, and most of the apply step is hashing the whole frame for
its CID.

What the numbers say and do not say:

- **Predictable content shrinks; unpredictable content does not.** A frame
  that mostly repeats shared blocks or the previous frame costs about 1% of
  raw here. Random noise costs slightly more than raw, because no encoding
  can carry information in fewer bits than its entropy.
- **The remaining cost is the ops, not the pixels.** The 200 scattered
  pixel changes cost about 400 ops of 12 bytes each, which is most of the
  6.3 KB. A codec designed for video (motion vectors, entropy coding) would
  carry the same changes in less; nothing here was compared against H.264,
  AV1 or any other codec, so no claim is made against them.
- **Every frame here is lossless.** Lossy or model-based reconstruction
  (rebuilding a face from a model and a few parameters) can go much lower,
  but it shows a different picture from the original pixels and would need
  its own fidelity score. Nothing in this engine does that.
- **Loopback is not a mesh.** These timings are this code on one host. Air
  time, contention, interference and real loss on a radio mesh are not
  measured here; that needs the mesh test in section 9.

## 8. Honest limits

- **The fetch pattern can identify the target.** Anyone who sees which
  public blocks a node fetches, and when, can match that set against the
  recipes of known files. Decoy blocks hide which listed blocks are used,
  not which are fetched. Fetching extra blocks (padding), pre-caching
  common libraries well before they are needed, and hydrating over a
  mixing or batching transport make the match harder; none of them makes
  it impossible, and a block only one target uses identifies it outright.
- **Confidentiality rests on the diff and the key, not on the blocks.** The
  public blocks are public. If the private part of a file is small and
  guessable (a name, an amount), the recipe protects it exactly as well as
  the encryption of the diff, no better.
- **A CID proves integrity, not trust.** A matching target CID proves these
  are the bytes the sender named, not that they are safe or that the sender
  is who you think. Trust comes from the signature and the caller's list of
  trusted sender ids, and only as far as those keys are kept safe.
- **The sender is visible.** The signature and public key sit outside the
  encryption, so whoever sees a sealed recipe learns who sent it and to
  which key id. Send recipes over an encrypted session when that matters.
- **Keys and endpoints.** A stolen signing key signs anything; a stolen
  recipient key opens everything sent to it; a compromised endpoint sees
  the plaintext. Nothing here defends against those.
- **Side channels.** The post-quantum code is the vendored reference code
  (pqsec); see `kernel/src/pqsec/pq_matrix.h` for what that does and does
  not protect against.
- **Ethernet type 0x88B5.** Frames here are bytes for any carrier (Vinea,
  AIRC, the harmonic wire, freight). If they are ever put directly in
  Ethernet frames, 0x88B5 is the IEEE "local experimental" EtherType: fine
  inside one LAN, not assigned to anyone, and not for traffic between
  networks.

## 9. Not built yet

- The installer and the app waves calling `fab.*` (section 5), the app
  launch screen pulling `fab.receive.step` for its bars and jingle, and the
  host's air-gap seal callback (`zxvd_fab_set_seal`).
- A mesh test of 60 fps frames over real radios (loss and latency on air).
- Its row in the Alexandria matrix. Layers there are keyed by app: layer
  N is L1 system app N in APP_CATALOG_407 order, layer 0 is the core
  (`/mnt/project-files/alexandria/MATRIX.md`). The engine's home is layer
  126, Toolchest, beside manufacturing and the SDK (MATRIX.tsv, Alexandria
  head 088365a0); zxvd_fab.c goes with kernel/arch in the core. The user's original proposal of bands
  (001-024, storage resolution 025-072, fabrication 073-096, sovereign
  execution with the seal 097-144, entry at "layer 080") is recorded here
  as a proposal only. It is not the shipped mapping, where layer 80 is
  Ninefold. Moving to bands would change every layer's meaning, so it is
  the user's decision.

## 10. The program fabricator

An app is fabricated from a recipe and accepted only when it is whole and
answers as it promises. The gate is `kernel/src/appfab` (rules F1-F7 and
C1-C3 in `appfab.h`); the tool is `zxv-fab pack-app` and `unpack-app`
(`tools/zxv-fab/zxv_fab_app.c`); the tests run in `make -C kernel
test-appfab`, which `verify-all` runs.

**The fabrication form.** `pack-app` turns a manifest into its form:

- every `use` is pinned to the content id of its block's own form;
- blocks are sealed the same way first, inside out;
- every source read from a path is written inline (`<<` … `.`). A source
  that cannot be inline (not text, a line that is `.`, no final newline)
  is named `cid:<CID>` instead and carried as a part.

The form is the recipe's target. The plan lists the closure (every pinned
form and every `cid:` source), and an older form (`--prev`) when there is
one, so a new version sends only its change: a one-word edit of Fee Check
is a 2-byte diff. The recipe is PUBLIC and signed with no randomness, so
the same app and parts always give the same bytes. A zxapp content id
(`sha256:` + hex) and a raw CIDv1 sha2-256 of the same bytes carry one
digest, so a pin names a part without a second hash.

**The gate.** The verdict is `lpres_conjoin(fit, function)`. The kernel's
chain order means only TRUE and TRUE accept; a contradiction (BOTH) or a
gap (NEITHER) holds the app.

- **Fit.**
  - Every part matches its CID.
  - Every use is pinned to a part.
  - Every source comes from a part; nothing is read from the device's disk.
  - Every entry loads on this host's runtimes (Lingua's rule).
- **Function.** Each check runs witnessed (`zxapp_call_witnessed`):
  - TRUE when two languages or more give the expected answer;
  - NEITHER when only one language ran;
  - BOTH when the languages disagree;
  - FALSE when the answer is wrong.
  - No checks means NEITHER.

`unpack-app` writes the app only on TRUE. It exits 1 on FALSE and 4 when
held.

**No floating point.** Integer and exact arithmetic only, so floats are
stopped in two places:

- **Logic.** The runtimes refuse floats at load (R11): Eve has no float
  type, Sutra refuses COMP-1 and COMP-2, and Z-Wasm refuses any f32 or f64
  type or instruction. A logic source with a float therefore does not fit.
- **C.** The `view` and `model` an app names are C built into the image,
  not carried by the recipe. `pack-app` reads them, and the model's
  header, before anything is signed. It refuses:
  - a float type (`float`, `double`, `_FloatN`, `_Complex` and kin);
  - a floating constant (`1.5`, `3e2`, `.5`, `0x1p3`);
  - an include of `math.h`, `tgmath.h`, `float.h`, `complex.h` or
    `fenv.h`.

  Comments, strings and character constants are skipped, so `0x1e5` and
  `10ULL` pass. The 16 app models and views in the tree pass.

A generated app that "passes its tests with floats" cannot reach a recipe.

**The matrix.** `appfab_fit_layered` (and `pack-app --layer`) enforce the
Zeckendorf lineage rule, the same rule as the matrix's `alx_mx_draws` and
pack_matrix `lineage()`, checked against the golden table
`docs/spec/ZECKENDORF_ANCESTORS.tsv`:

- The anchors are 0 and the Fibonacci numbers (0 1 2 3 5 8 13 21 34 55 89
  144, and every Fibonacci number beyond).
- A branch N = F + M, where F is the largest Fibonacci number ≤ N, has
  lin(N) = every anchor ≤ F, plus M, plus lin(M). An anchor's lineage is
  every anchor below it, and lin(0) is empty.
- Layer N may use parts from N itself or lin(N) only. For example,
  lin(12) = {0, 1, 2, 3, 4, 5, 8}.
- Every use, at every depth, is checked against the layer of the part that
  makes it. A part with no layer is refused.
- An empty scaffold (no use, no logic, no provides) is a glue node: it
  fits as NEITHER and is never accepted or filled with stand-in content.
- The rule is shared with the matrix thread's pack_matrix. Until manifests
  carry their own layer, the tool takes each part's layer from
  FABRICATED.tsv.

**FABRICATED.tsv** has one row per fabricated app: id, version, manifest
CID, recipe CID, layer. The row is replaced when the manifest is the same.
A different manifest under the same id and version is refused: it needs a
new version.

**Measured.** Fee Check (five languages) packs from `kernel/src/apps`,
rebuilds on a clean store, fits with the real Sutra, KRON, Eve, Trebago and
MYRA runtimes, and is accepted with fee checks that Sutra and KRON both
answer. Murmuration packs over its sealed `swarm_shares` block and fits.
Its entries run in one language only, so with checks it is held as
unwitnessed (NEITHER), as it should be.

**Limits.**

- `view` and `model` name C compiled into the image. They are not carried
  or checked; a device without that build does not have them.
- Circuits (`logic wire`) have no runtime in the tool, so they do not fit
  there yet.
- Until zxapp accepts `cid:` sources in pinned blocks (C3 sealed rule,
  Built-in apps thread), a block whose source cannot be inline is refused
  when pinned.
- The zxvd `fab.receive.step` check phase, and check lines in manifests,
  are not built yet. Checks come from `--check` meanwhile.

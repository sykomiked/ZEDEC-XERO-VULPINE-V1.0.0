# Release layers

The release is a stack of layers. Each layer is a small manifest named by the
CID of its own bytes, and each manifest names its parent's CID. A changed byte
anywhere below changes every CID above it, so one CID at the top vouches for
the whole stack, and the kernel checks the stack it boots from before
`[BOOT_OK]` hands the machine to the chiglet.

| Layer | Name | What it lists | Where its manifest lives |
|---|---|---|---|
| L0 | `HARDWARE_ENVELOPE` | one machine per architecture the core discs boot on (`build_system/layer0.machines`): firmware, kernel link address, page and sector size, minimum RAM | core disc `/ZXV/LAYER0.JSN`; bucket `03_layers/` |
| L1 | `CORE_BOOT_ENGINE` | each architecture's boot file (`BOOTX64.EFI`, `BOOTAA64.EFI`, which carry the kernel) and the core models | core disc `/ZXV/LAYER1.JSN`; bucket `03_layers/` |
| L2 | `SWARM` | the swarm models, by group | core disc `/ZXV/LAYER2.JSN`; bucket `03_layers/` |
| L3 | `ALEXANDRIA` | the Library of Alexandria's booklets | bucket `03_layers/` |
| L4 | `COMMUNITY_OVERLAY` | a community overlay; an overlay may stand on another overlay | bucket `03_layers/` |

## What a layer lists

A layer lists what it holds (files, by name, size and SHA-256), never the
disc images that carry it. A disc cannot carry its own hash, and listing
content is what lets the core disc carry L0, L1 and L2: a kernel fix changes
the boot file, so L1 and L2 get new CIDs and a new core disc is cut, while
the swarm discs stay byte for byte the same.

## The manifest

JSON in one fixed layout, written by `build_system/mk_layers.py`. The kernel
(`kernel/src/layers`) reads it line by line with no JSON parser and refuses
any other layout of the same data, which would also be a different CID.

```
{
 "format": "zxv-layer 1",
 "layer_depth": 1,
 "layer_name": "CORE_BOOT_ENGINE",
 "parent_layer_cid": "bafkrei...",
 "parent_compat": "exact",
 "objects": [
  {"kind": "boot", "arch": "x86_64", "name": "BOOTX64.EFI", "bytes": 1234, "sha256": "...", "cid": "bafkrei..."},
  {"kind": "model", "arch": "all", "name": "qwen2.5-0.5b-instruct-q4_k_m.gguf", "bytes": 491400032, "sha256": "..."}
 ]
}
```

- `parent_layer_cid` is `null` at L0 and only there.
- A layer's CID is CIDv1, raw codec, sha2-256 of the manifest's bytes
  (`bafkrei...`, 59 characters). A manifest is at most 1 MiB; the kernel reads
  at most 64 KiB of each at boot.
- Object keys come in one order: kind, arch, group, name, firmware,
  link_address, page_bytes, sector_bytes, min_ram_mib, bytes, sha256, cid.
- A manifest cannot hold its own CID; the layer above names it. The
  `layer_cid` field of the original sketch is therefore the node's, not the
  file's (below).

## Parents: exact, or evo_comm

`"parent_compat": "exact"` means the parent is that CID and no other. It is
the default, and L1 and L2 are always exact: discs are pinned.

Exact anchoring has a cost: any change to L1 (one kernel fix) gives every
layer above it a new CID, so every booklet and overlay would have to be
re-anchored for each patch. L3 and L4 may therefore say `"evo_comm"`: they
may also stand on a parent the caller admits under the evo_comm
compatibility contract (community evolution, #155). `zxv_layer_chain` takes
that admit check as a callback and never asks it for an exact layer. Until
evo_comm lands, nothing admits, so evo_comm layers behave as exact.

## Building them

```
make build-all-layers [ARCHS="x86_64 arm64"] [LAYERS=build/layers]
python3 build_system/mk_layers.py check build/layers/L0.json build/layers/L1.json build/layers/L2.json
```

`build-all-layers` (alias `layers`) runs `mk_layers.py` once per layer. Each
step after L0 reads `PARENT_CID` from the `L<n-1>.cid` the step before wrote,
so a layer can only be built on the one just built, and the last step checks
the whole chain. L1 needs the boot file of every arch in `ARCHS`
(`BOOTX64.EFI`, `BOOTAA64.EFI` from `build_system/baremetal_images.sh`); the
model lists are `GOLDEN_MODELS`, `GOLDEN_CORE` and `GOLDEN_SWARM`, the same
ones `mk_golden_disc.sh` reads. `mk_golden_disc.sh` copies the manifests onto
the core disc when `GOLDEN_LAYERS` names their directory, and refuses them if
L1 does not list the boot file it is putting on the disc. L3 and L4 take
`--dir` and are built by the threads that own those trees.

`make ipfs-dist` builds in this order: kernels and EFI files, L0, L1, L2, the
discs (the core disc carries L0 to L2), then the bucket with the manifests in
`03_layers/`.

## The check at boot

`kernel/src/layers/layers_boot.c`, run on x86_64 after the discs are indexed
and before the AI self-check and `[BOOT_OK]`, and again whenever a fetched
swarm disc mounts:

1. `/ZXV/LAYER0.JSN` to `LAYER2.JSN` parse, and L1 names L0's CID, L2 names
   L1's.
2. L0 lists a UEFI machine for this architecture with 4096-byte pages and
   512-byte sectors.
3. `/EFI/BOOT/BOOTX64.EFI` on the ESP has the size and SHA-256 that L1 lists.
   This is the file the firmware loaded; the check reads the disc, not the
   running image in memory.
4. Every core model is the one L1 lists, and every model L1 lists is on the
   core disc. Every model on an attached swarm disc `ZXV_SWARM_<G>` is one L2
   lists in group G. A swarm disc not attached yet is not a break in the
   lineage; it is checked when it mounts.

A model that fails step 4 is refused (`[MODEL_BAD] ... differs from the model
its layer lists`) and never handed out. If steps 1 to 3 fail, nothing the
layers vouch for can be trusted, so every model is refused. The console
shows one line per layer and the verdict:

```
  [LAYER] L0 HARDWARE_ENVELOPE bafkrei...
  [LAYER] L1 CORE_BOOT_ENGINE bafkrei...
  [LAYER] L2 SWARM bafkrei...
  [LAYERS_OK] L0 -> L1 -> L2: each names the one below; BOOTX64.EFI and 2 models match their layers
```

or `[LAYERS_FAIL] <what failed>`, or `[LAYERS] none on the core disc` for a
disc built without layers (the boot goes on as before).

The kernel keeps what it checked as a table of `struct zxv_layer_node`
(`kernel/include/layer_manifest.h`): depth, the layer's CID, its parent's CID,
the partition it was read from and flags. `ZXV_LAYER_VERIFIED` is set when
the check passes; `ZXV_LAYER_SEALED` and `ZXV_LAYER_AIRGAPPED` when the
post-hydration seal severs the network (`AIRGAPPED` only when the NIC was
powered down); `ZXV_LAYER_OVERLAY` and `ZXV_LAYER_COMPAT` are for L4 and
evo_comm layers, which the boot does not read yet.

## Not claimed

- No signature over the manifests: the chain is hashes only. A signed top CID
  (the owner's release key) is separate work.
- L3 and L4 are not checked at boot; the Library of Alexandria's own chain
  check (`alx_lineage`) is meant to become the one checker for them when it
  lands.
- The arm64 kernel does not read golden-disc models yet, so it does not run
  the boot check.

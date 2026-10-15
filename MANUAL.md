<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# ZXV Instruction Manual

Getting, verifying, running and developing **ZEDEC XERO VULPINE (ZXV pqOS)** —
from a browser tab to bare metal, from the IPFS release or from source.

## Contents

1. [Ways in](#1-ways-in)
2. [Get the source](#2-get-the-source)
3. [Verify the boot](#3-verify-the-boot)
4. [Run it in a browser](#4-run-it-in-a-browser)
5. [Run it in QEMU](#5-run-it-in-qemu)
6. [Run it on hardware](#6-run-it-on-hardware)
7. [Run it on a VPS or cloud VM](#7-run-it-on-a-vps-or-cloud-vm)
8. [The IPFS toolbox](#8-the-ipfs-toolbox)
9. [Language and chipset kits](#9-language-and-chipset-kits)
10. [Developer gate](#10-developer-gate)
11. [Troubleshooting](#11-troubleshooting)

Throughout, `ROOT` means the release root CID, printed at the bottom of the
doorway page and listed in `MANIFEST.tsv`. Any public gateway works; the
commands below use `https://ipfs.filebase.io` and `https://ipfs.io` as
examples.

## 1. Ways in

| You want | Do this |
|---|---|
| Look at it now | Open the doorway in a browser and press **Boot ZXV in this browser** |
| Try it in a VM | Fetch `zxv-x86_64.qcow2`, boot with QEMU |
| Install it | Fetch `zxv-x86_64.iso`, write it to a USB stick or attach as CD |
| Run a server | Fetch `zxv-x86_64-vps.qcow2`, register it as a custom image |
| Build it | Clone the git bundle from IPFS, or the GitHub mirror |
| Embed it | Use `libzxv` and the SDK for your language |

## 2. Get the source

**From IPFS — the whole repository as a git bundle:**

```bash
ipfs get -o zxv.bundle /ipfs/ROOT/09_source/zxv.bundle
git clone zxv.bundle zxv
cd zxv
```

**From the S3/HTTP gateway — no IPFS node needed:**

```bash
curl -LO https://ipfs.filebase.io/ipfs/ROOT/09_source/zxv.bundle
git clone zxv.bundle zxv
```

**From GitHub:**

```bash
git clone https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0
```

The bundle is the whole repository, history included. Check it against the
manifest after cloning: `MANIFEST.tsv` lists every release file with its CID
and SHA-256.

## 3. Verify the boot

One command rebuilds the disc from source, boots it in QEMU and checks every
boot marker. Docker is the only dependency:

```bash
bash build_system/verify_boot.sh
```

The host gate (needs gcc 11 or clang, make):

```bash
make -C kernel verify-all
```

Both are described in [docs/VERIFY_BOOT.md](docs/VERIFY_BOOT.md), including
what each marker proves and how to compare the disc hash with CI's.

## 4. Run it in a browser

Open the release root through any gateway:

```
https://ipfs.filebase.io/ipfs/ROOT/
```

The doorway shows what the system is, then **Boot ZXV in this browser**
opens `boot.html`: the same vault doors, launch screen and login as the disc
build. Every file the page streams is fetched by CID and SHA-256-checked
before the boot proceeds; a hash mismatch stops it honestly rather than
drawing wrong.

## 5. Run it in QEMU

Fetch an image:

```bash
curl -LO https://ipfs.filebase.io/ipfs/ROOT/00_base/zxv-x86_64.qcow2   # x86-64 VM
curl -LO https://ipfs.filebase.io/ipfs/ROOT/00_base/zxv-x86_64.img     # x86-64 raw
curl -LO https://ipfs.filebase.io/ipfs/ROOT/00_base/zxv-arm64.img      # ARM64 raw
```

Boot x86-64 with virtio devices (the network the node speaks on):

```bash
qemu-system-x86_64 -m 1G -drive file=zxv-x86_64.qcow2,if=virtio \
  -device virtio-net-pci,netdev=n0 -netdev user,id=n0 \
  -serial stdio
```

Boot ARM64 (virtio-mmio):

```bash
qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 1G \
  -kernel kernel_arm64.elf -nographic -serial mon:stdio
```

Build the kernels from source instead of fetching images:

```bash
./build.sh arm64          # or
make -f build_system/Makefile.x86_64 all
```

## 6. Run it on hardware

`zxv-x86_64.iso` is hybrid: it boots under legacy BIOS (GRUB/El Torito) and
UEFI (an appended ESP plus an El Torito FAT image).

```bash
# write the ISO to a USB stick (replace /dev/sdX — this erases the stick)
sudo dd if=zxv-x86_64.iso of=/dev/sdX bs=4M status=progress conv=fsync
```

Or write the raw image:

```bash
sudo dd if=zxv-x86_64.img of=/dev/sdX bs=4M status=progress conv=fsync
```

**Honest standing:** the system has booted under emulation and in VMs; it
has not yet been proven on a physical board. If you are the first to try a
board, the serial console carries the boot markers (`[BOOT_OK]`,
`[SWARM_COMPLETE]`, `[MODELS_OK]`, `[AI_OK]`, `[VNA] node`) and any stop
says why.

## 7. Run it on a VPS or cloud VM

`zxv-x86_64-vps.qcow2` is a single-drive image with the swarm partition
grafted on, built for providers that accept a custom image (it was built
and checked against Contabo's flow):

1. Upload `zxv-x86_64-vps.qcow2` as a custom image in the provider console.
2. Reimage each VPS from it.
3. Give the instance a public IP and open UDP for the Vinea port (the
   peer table's port; `UPSTREAM.CID` carries it).
4. Boot; check the serial/console log for `[VNA] node` with a distinct
   identity and `tx`/`rx` activity against the seeded peers.

## 8. The IPFS toolbox

The release is a UnixFS DAG. Every file sits at its own CID, every directory
at its own, and the root CID names the whole.

```bash
# pin the whole release into a local node (fetches and keeps every block)
ipfs pin add -r --progress ROOT

# or fetch it as files without pinning
ipfs get -o zxv-release /ipfs/ROOT

# provide it on the DHT so others find it fast
ipfs daemon &            # the node must be running and reachable
ipfs pin add -r ROOT     # pinned content is reprovided automatically

# fetch one file without a node, over any gateway
curl -LO https://ipfs.filebase.io/ipfs/ROOT/00_base/zxv-x86_64.iso

# verify what you fetched against the manifest
curl -LO https://ipfs.filebase.io/ipfs/ROOT/MANIFEST.tsv
sha256sum zxv-x86_64.iso   # compare with the SHA-256 column
```

Layout inside the root:

| Path | What it is |
|---|---|
| `index.html` | The doorway — this manual's links, live |
| `boot.html` + `boot.js` + `zxv_boot.wasm` + `boot.json` + `brand/` + `font.ttf` | The browser boot |
| `00_base/` | Disc images at fixed names and at `<CID>.<ext>` |
| `07_matrix/`, `08_vectors/` | The packed matrix and vector plane |
| `09_source/` | The git bundle (`zxv-<commit>.bundle` and `zxv.bundle`) |
| `11_models/` | Model weights at `<CID>.gguf` |
| `12_docs/` | This repository's documents |
| `MANIFEST.tsv` | Every file: kind, path, CID, bytes, SHA-256 |

## 9. Language and chipset kits

The kernel builds for five CPU families from the same tree:

```bash
./build.sh arm64
make -f build_system/Makefile.x86_64 all
# riscv64, riscv32, arm32 have their own build_system/Makefile.* targets
```

`libzxv` exposes the engine through a frozen C ABI; SDKs ship in `sdk/` for
C, C++, Java, .NET, Python, TypeScript, Go, Rust, Ruby, PHP, Swift and
Solidity, plus a Z-Wasm plugin sandbox. The language pillars — COBOL/Sutra,
Fortran/Trebago, Ada/Eve, APL-K/KRON, MUMPS/MYRA — live under the shared IR
with an AI twin each.

## 10. Developer gate

Before pushing, the repository asks for its checks (also in `CLAUDE.md`):

```bash
python3 build_system/relicense_apache.py --check
python3 build_system/check_workflow_tiers.py
python3 build_system/sdk_kernel_deps.py --check
python3 tests/harness/mutator/mutate.py --config tests/harness/mutator/targets.json --check-config
build_system/verify_layers.sh
make -C kernel verify-all
```

A change is done when its checks pass. New tests must first be shown to
fail. Exact integers for money and measurements; bounded rings for
cross-process paths; no includes reaching a higher stratum.

## 11. Troubleshooting

| Symptom | First thing to check |
|---|---|
| Gateway is slow or 404s | Try another public gateway, or `ipfs get` through a local node; the CID is the same everywhere |
| Browser boot stalls | `boot.json` names what it streams; the status line says which file and why it stopped |
| QEMU shows nothing | `-serial stdio` — the console is serial; markers start with `[BOOT_OK]` |
| VPS boots but no peers | The Vinea port must be open inbound on UDP; check `UPSTREAM.CID` and the peer log |
| Clone from the bundle fails | Check the bundle's SHA-256 against `MANIFEST.tsv` first |
| `verify-all` fails on day one | Clean tree or clean container; the gate assumes a fresh checkout |

---

*ZXV is created by Michael Laurence Curzi and 36N9 GENETICS, LLC —
Apache-2.0. Report vulnerabilities per [SECURITY.md](SECURITY.md); support
goes to SCENIC BYPASS CONSULTING, LLC per [SUPPORT.md](SUPPORT.md).*

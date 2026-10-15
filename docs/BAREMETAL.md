# Bare-metal UEFI discs

ZXV can boot on its own as a separate, opt-in disc image. It never replaces or
modifies the host OS: the build only writes files under `dist/discs/`.

## Build (Mac or Linux, needs Docker)

```bash
docker build -f build_system/Dockerfile.baremetal -t zxv-baremetal .
docker run --rm -v "$PWD:/src" -w /src zxv-baremetal bash build_system/baremetal_images.sh
```

Output: `dist/discs/zxv-arm64-uefi.img`, `dist/discs/zxv-x86_64-uefi.img`
(GPT disk, one FAT32 ESP holding `/EFI/BOOT/BOOTAA64.EFI` or `BOOTX64.EFI`
plus `/MEGAROM.TVL`) and `SHA256SUMS`. The build fails if the swarm and tensor
engine (`swarm_boot_selfcheck`, `zt_boot_selfcheck`) are not linked in.

## Boot check

```bash
bash build_system/uefi_boot_check.sh arm64  "" 90 "[AI_OK]"
bash build_system/uefi_boot_check.sh x86_64 "" 90 "[AI_OK]"
```

Runs inside the container (OVMF / QEMU_EFI) or on a Mac with Homebrew QEMU
(edk2 firmware). The firmware loads the EFI app from the disc itself; QEMU uses
`snapshot=on`, so the image file is never modified. It exits non-zero unless the
serial log reaches `[BOOT_OK]` and `[AI_OK]` with no fault marker. CI runs the
same check in the `uefi-disc` job.

## Golden discs: models and assets on the discs

The golden discs carry everything the OS needs offline: the AI models in
`build_system/golden.models` (Qwen2.5-1.5B-Instruct and Qwen2.5-0.5B-Instruct,
Q4_K_M, both Apache-2.0) and the asset folders in `build_system/golden.contents`
(licences, music, and fonts, boot sounds, brand and app icons once those land
in `assets/`). Machine code and model weights are kept apart, the way Debian
keeps architecture packages apart from `Architecture: all` ones:

| Disc | Architecture | Holds |
|---|---|---|
| `zxv-<arch>-disc1-core.img` | x86_64 or arm64 | the loader, the kernel, the assets and the core models (`build_system/golden.core`) on one FAT32 EFI System Partition |
| `zxv-swarm-disc<N>-<group>.img` | all | one model group (`build_system/golden.swarm`) on one FAT32 partition named `ZXV_SWARM_<GROUP>`, GPT type `5A585653-5741-4D53-9000-000000000001` |

A kernel fix rebuilds only the small core discs; the swarm discs keep their
SHA-256 sums and serve every architecture. Today the core is Qwen2.5-0.5B
(core disc 546 MB) and swarm disc 2, `reason`, holds Qwen2.5-1.5B (1,139 MB).

```bash
docker run --rm -v "$PWD:/src" -w /src zxv-baremetal bash -c \
  'bash build_system/baremetal_images.sh x86_64 && bash build_system/mk_golden_disc.sh x86_64 && bash build_system/mk_golden_disc.sh swarm'
UEFI_DISK=ahci UEFI_EXTRA=dist/discs/zxv-swarm-disc2-reason.img UEFI_MEM=4G \
  bash build_system/uefi_boot_check.sh x86_64 dist/discs/zxv-x86_64-disc1-core.img 420 "[AI_OK]" "[MODELS_OK] 2 verified"
```

Each model is downloaded once from its pinned URL into `dist/models` (or
`ZXV_MODEL_CACHE`) and refused unless its size and SHA-256 match the list.
Every model is on exactly one disc. Each disc has its own `MODELS.SHA` (one
line per model: sha256, size, name, `esp` or `swarm`, and the file's 8.3
name, `MDL000.GGU` in `golden.models` order, since the kernel's FAT32 driver
reads short names only), its own `MANIFEST.SHA256` for `sha256sum -c` from
another computer, and a `README.TXT`. The build fails if any disc would pass
1.8 GB (so each fits a DVD-R or a small stick; split a group that grows past
it) or any file is 3.8 GiB or larger (FAT32 stops at 4 GiB per file). The
discs are reproducible: the same commit and models give the same bytes.

**x86_64.** The loader does not read the models. After ExitBootServices the
kernel finds the core disc and every attached `ZXV_SWARM_*` disc with its own
AHCI or NVMe driver, reads their manifests and prints `[SWARM_MOUNTED]
ZXV_SWARM_REASON on sata1: 1 model` and `[MODELS_INDEXED] 2 listed (1 on the
boot partition, 1 on 1 swarm disc)`. A swarm disc with a bad manifest drops
only its own models. Each model is hashed when something first asks for it,
and the rest in the idle loop after `[BOOT_OK]`, 1 MiB per pass with the timer
still running (`kernel/src/hwdrv/hw_models.h`). It prints `[MODEL_OK]` per
model and `[MODELS_OK] 2 verified`, or `[MODEL_BAD]` and `[MODELS_FAIL]`; a bad
model is reported, it does not stop the OS. Under QEMU emulation (TCG) both
models were verified about 85 s after `[BOOT_OK]`, with 4 GB and with 8 GB of
RAM. The kernel's drivers do not cover virtio, so boot golden discs with
`UEFI_DISK=ahci` or `nvme`.

**arm64.** Its kernel cannot reach a disc yet (no PCI storage driver), so the
loader checks the core disc's models before ExitBootServices, streamed 4 MiB
at a time (`kernel/arch/efi_models.h`), and prints `[MODELS_OK] 1 verified`.

`build_system/test_golden_disc.sh <arch>` proves the check in about two
minutes per arch with small test files as the models, one on a core disc and
one on a swarm disc: intact, they must verify; with one byte changed on a
disc, it must be caught. CI runs it in the `uefi-disc` job. The Golden disc
workflow builds the real discs and boots them with 4 GB and with 8 GB of RAM.

### The bucket, and downloading the discs

All the discs together make one release bucket, an IPFS directory. Disc 1 is
the only one tied to an architecture; the swarm discs stack on top of any of
them, so a person downloads one core disc and all the swarm discs:

```
manifest.json                       every disc: arch, size, SHA-256, CID
00_base/zxv-x86_64-disc1-core.img   one core disc per architecture
00_base/zxv-arm64-disc1-core.img
01_swarm_reason/zxv-swarm-disc2-reason.img
02_swarm_<group>/...                one directory per swarm disc, in disc order
NN_cartridges/...                   after the swarm discs, when there are any
```

`python3 build_system/mk_bucket.py` lays out the discs in `dist/discs` as
`build/bucket` and prints the root CID that `ipfs add -r --cid-version=1`
would give it, computed the way Kubo does (checked against Kubo 0.32.1 by
`mk_bucket.py --self-test`), with `manifest.json` and the same list as
`manifest.txt` for the kernel. It adds nothing to IPFS and publishes nothing;
uploading is the owner's call. `make ipfs-dist` builds the whole release
twice from clean (kernels, core discs, swarm discs, bucket) and fails unless
both passes give the same Disc 1 SHA-256 for every architecture and the same
root CID; the bucket lands in `build/ipfs-dist/bucket`.

To download, with nothing but Python 3:

```bash
git clone https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0.git
ZEDEC-XERO-VULPINE-V1.0.0/tools/zxv-get                 # this machine's core disc + all swarm discs
ZEDEC-XERO-VULPINE-V1.0.0/tools/zxv-get --arch arm64    # another architecture
ZEDEC-XERO-VULPINE-V1.0.0/tools/zxv-get --list          # what the bucket holds
```

`zxv-get` uses a local IPFS node if one runs (`ipfs cat`), else a public
gateway one raw block at a time, hashing every block against its CID, so a
gateway cannot change a byte. Then it checks each disc against
`manifest.json` (size and SHA-256) and against the CID the bucket's
directory gives it, and deletes any disc that fails. Discs already present
with the right SHA-256 are kept. The script also works on its own, without
the clone: pass the bucket with `--cid`.

It is patient, because fetching can be slow. When no gateway serves a block
it prints `waiting for peers` and tries again after 1 s, 2 s, 4 s, up to a
minute apart, with no overall deadline (`--patience SECONDS` sets one). The
gateway that last answered is asked first, a slow one gets a longer timeout
(30 s doubling to 4 min, easing back once it answers), and one that sends a
block that does not match its CID is not asked again. Ctrl-C, a dropped link
or a lying gateway leaves the verified part as `<disc>.part`; the next run
hashes it again and fetches only the rest. Progress is a bar with the real
rate.

Which bucket. `build_system/zxv_upstream.cid` holds the master bucket's CID,
and each core disc carries it as `/ZXV/UPSTREAM.CID`, together with
`/ZXV/SWARM.LST`, the swarm discs it expects. A CID never changes, so a new
release is a new CID, and a core disc cannot carry the CID of the bucket
that holds it. Updates therefore follow an IPNS name the owner signs (an
`ipns k51...` line in that file, once the owner makes the key); `zxv-get`
resolves it through a local node and otherwise falls back to the CID. At
boot the x86_64 kernel prints `[UPSTREAM] bucket <cid>`, then
`[SWARM_MISSING] disc 3 ZXV_SWARM_VISION (zxv-swarm-disc3-vision.img): attach
it, or fetch it from the upstream bucket (zxv-get)` for each expected disc not
attached, or `[SWARM_COMPLETE]`.

### Fetching the swarm discs from the bare-metal kernel

When an expected disc is missing, the core disc names a bucket and a blank
drive is attached, the x86_64 kernel fetches the disc itself
(`kernel/src/swarmnet`, entirely ZXV code: the e1000/igb/r8169 drivers, the
kernel's ARP/IPv4/UDP/DHCP/DNS/TCP stack and the ipfs_node block walker).
It takes the first NIC with link up (`[NET] eth0 = ..., link up`), asks DHCP
three times, 5 s each (`[NET] DHCP lease ...`), then asks each `http://`
gateway in `UPSTREAM.CID` for one raw block at a time, hashing every block
against its CID. It reads `manifest.txt` from the bucket, checks that the
bucket's directory gives the disc the manifest's CID, and streams the image
onto a blank drive (no MBR signature, no GPT header, 512-byte sectors, large
enough) with a rolling SHA-256. The first 34 sectors, the partition table,
are held back and written last, only when size and SHA-256 match the
manifest, so an interrupted or altered download never looks like a disc:
`[SWARM_FETCHED] disc 2 ZXV_SWARM_REASON on sata1: ... bytes, sha256 matches
the manifest; committed`, or `[SWARM_FETCH_FAIL] ...; nothing committed`.
The disc is then indexed and mounted like an attached one, and its models
are checked.

The fetch is elastic. It runs in steps from the kernel's idle loop after
`[BOOT_OK]` (`[SWARM] The core is up; the swarm discs fill in the
background. Press d at any time to attach them by hand instead.`), so the
core is usable while the discs fill, and each disc is mounted as it commits.
Progress is the bytes hashed so far: `[SWARM_FETCH] disc 2 [#####-----] 50%
(... of ... bytes, 812 KB/s)`. When no gateway serves the next block it
waits for peers, 1 s, 2 s, 4 s, up to a minute between tries, with no total
deadline; each gateway's timeout starts at 10 s and doubles after a timeout
(to 160 s), and one that sends a block that does not match its CID is not
asked again that boot. A drive with 35 sectors to spare past the image keeps
a resume record in its last sector, refreshed every MiB, and the partition
table's 34 sectors before it. After a reboot or a dropped link the kernel
reads back and hashes what the drive holds (the record only says where to
look; it is never trusted for the content) and fetches only the rest:
`[SWARM_FETCH] disc 2 ZXV_SWARM_REASON: resuming on sata1 at 104875008 of
316669952 bytes`. If the finished image then does not match, the disc is
fetched once more from the start. `d` at the console stops the fetch at any
point and says which disc to attach by hand; what is on the drive stays for
a later boot.

With no NIC, no link or no lease it prints

```
[NET] Link down or no DHCP response.
[SWARM] Falling back to local storage scan: Attach Disc 2 USB / drive.
```

It speaks plain HTTP to a trustless gateway: a gateway on your own network
(Kubo's `http://<host>:8080`) works; `https://` gateways are skipped and
said so, and native Bitswap is not written (it needs libp2p's
multistream-select, Noise and yamux). The fetcher runs in the kernel's idle
loop for now; it moves to a ring-3 process once user programs have network
and disk system calls ([gaps 7 and 17](BAREMETAL_OS_GAPS.md)). arm64 does
not fetch yet: its kernel has no PCI storage driver.

**The seal.** Once every expected disc is attached after a fetch, the kernel
cuts the network that brought them, as the `seal` line of `UPSTREAM.CID`
says: `POWERDOWN_NIC` (PHY powered down, then PCI Bus Master and Memory
Space cleared and read back, so the NIC can neither DMA nor be reached until
reset), `LOCAL_MESH_ONLY` (the NIC stays up for local peers; the address,
routes, DNS, lease and connections are wiped), or `PROMPT_USER`, the
default, which asks on the console for `p` or `m` and powers the NIC down
after 30 s without an answer. In every mode the fetch buffers and network
state are zeroed and the fetch code itself is overwritten with INT3 and read
back, so nothing left in the running kernel can fetch or write a disc image.
Then the receipt:

```
[SEAL] Swarm Discs 2..N successfully hydrated and verified.
[SEAL] Master CID: bafy...
[SEAL] Network hydration channel severed: NIC bus mastering disabled.
[SEAL] Operating system is now immutable and air-gapped.
```

A step that fails prints `[SEAL_FAIL]` and no receipt.

`build_system/test_swarm_fetch.sh` proves all of it under QEMU: it builds
small discs and a bucket, serves the bucket from a gateway on the host
(`build_system/zxv_test_gateway.py`, which the guest reaches as
`http://10.0.2.2:<port>`), and boots the core disc with a blank drive eight
times: a fetch that commits, mounts, seals and still verifies both models; a
gateway that alters blocks (nothing committed, no seal); a link with no DHCP
answer; no NIC; a `LOCAL_MESH_ONLY` seal; a gateway that drops out for 20 s
mid-download (the fetch waits and finishes); one that drops out for good,
with `d` typed at the console; and a reboot that resumes from the drive.
`UEFI_NET=user|silent|none` on `uefi_boot_check.sh` picks the network,
`UEFI_EXTRA_RW=1` keeps what the guest writes to the extra drives, and
`UEFI_STDIN` types at the console. To try it by hand:

```bash
python3 build_system/mk_bucket.py --discs dist/discs --blocks build/blocks
python3 build_system/zxv_test_gateway.py build/blocks 8080 &
# a core disc whose UPSTREAM.CID names that bucket and http://10.0.2.2:8080
truncate -s 2G build/blank.img
UEFI_DISK=ahci UEFI_NET=user UEFI_EXTRA=build/blank.img \
  bash build_system/uefi_boot_check.sh x86_64 dist/discs/zxv-x86_64-disc1-core.img 600
```

`python3 build_system/test_bucket.py` builds a small bucket, serves it from a
local gateway and downloads it, then checks that a changed block, a manifest
that lies about a size, SHA-256, CID, path or architecture, and a disc over
1.8 GB are all refused, and that zxv-get waits out a gateway that is down,
resumes from a `.part`, and fetches a `.part` that is not the image's start
again.

The kernel does not yet run the discs' models on bare metal: it checks them,
and runs its built-in swarm and tensor self-check (`[AI_OK]`). A riscv64 core
disc needs a riscv64 UEFI loader first; the riscv64 kernel boots through
OpenSBI only.

## Run it on any OS (Docker only)

```bash
bash build_system/run_baremetal.sh x86_64 --screen
bash build_system/run_baremetal.sh arm64 --screen
```

Builds the toolchain image and the disc on first use, then boots the disc in
QEMU inside the container. The terminal is the serial console (Ctrl-A then X
quits); `--screen` serves the virtual monitor at `vnc://127.0.0.1:5900`. On
Windows run it from Git Bash or WSL with Docker Desktop. The same commands
without the script:

```bash
docker build -f build_system/Dockerfile.baremetal -t zxv-baremetal .
docker run --rm -v "$PWD:/src" -w /src zxv-baremetal bash build_system/baremetal_images.sh x86_64
docker run --rm -it -v "$PWD:/src:ro" -w /src zxv-baremetal qemu-system-x86_64 -machine q35 -m 1G -bios /usr/share/ovmf/OVMF.fd -display none -net none -drive if=none,id=disc,format=raw,snapshot=on,file=dist/discs/zxv-x86_64-uefi.img -device virtio-blk-pci,drive=disc,bootindex=0 -serial mon:stdio
```

## First physical boot: an old x86-64 UEFI PC

The x86_64 disc is the first hardware target: almost any 64-bit PC or laptop
from about 2012 onward boots it. It runs entirely from the USB stick. The
x86_64 boot path has no code that opens or writes an internal disk (the ATA
driver is compiled in but never initialized), so the PC's own OS is untouched;
unplug the stick and reboot to get it back.

You need: the PC, a USB stick of 64 MB or more **that will be erased**, and
`dist/discs/zxv-x86_64-uefi.img`.

1. Check the image: `shasum -a 256 dist/discs/zxv-x86_64-uefi.img` must match
   the line in `dist/discs/SHA256SUMS` (or the CI artifact).
2. Write it to the stick. Easiest on any OS: balenaEtcher, choose
   "Flash from file", pick the `.img`, then pick the USB stick and **check the
   drive name and size before pressing Flash**. On macOS by hand:
   ```bash
   diskutil list external
   ```
   find the stick (say `/dev/disk4`; confirm by its size), then
   ```bash
   diskutil unmountDisk /dev/disk4
   sudo dd if=dist/discs/zxv-x86_64-uefi.img of=/dev/rdisk4 bs=4m
   diskutil eject /dev/disk4
   ```
   A wrong disk number erases that disk. No script in this repo ever runs this
   step.
3. In the PC's firmware setup (usually F2 or Del at power-on): turn **Secure
   Boot off** (the image is not signed yet) and leave UEFI mode on (CSM/legacy
   is not needed).
4. Plug in the stick, power on, open the one-time boot menu (F12, F11, F8 or
   Esc depending on the maker) and pick the entry named "UEFI: <your stick>".
5. Expected: grey capital-letter boot log on screen, ending in
   `[AI_OK] SWARM+TENSOR SELFCHECK ...` and
   `[BOOT_OK] PHASE E0082 COMPLETE`, then the event cycle running. The same
   text goes to COM1 at 38400 baud if the PC has a serial port.

If the screen stays black after the firmware logo, the firmware offered no
32-bit linear framebuffer (GOP); the log is still on COM1. A photo of the
screen at the point it stops is enough to debug from.

## Later: arm64 hardware

The arm64 disc boots on UEFI arm64 machines (QEMU virt today). A Raspberry Pi
needs a UEFI firmware layer on its SD card first plus Pi-specific UART and
interrupt-controller support in the kernel, so it comes after the PC boot.

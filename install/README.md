# ZXV Universal Cell Installer

`install_zxv.py` detects the target platform, selects the matching native
kernel cell from the universal install bundle, verifies the SHA-256 digest,
and stages the boot artifacts.

## Quick start

```sh
python3 install/install_zxv.py --bundle-dir . --target-dir ./zxv_staged
```

The installer writes a `detection_report.json` next to the target directory
showing what was detected, selected, and staged.

## What it detects

- **Architecture** — from `platform.machine()` / `uname -m` (`x86_64`, `aarch64`, `arm64`, etc.)
- **Bitness** — 64-bit vs 32-bit
- **Firmware** — UEFI (`/sys/firmware/efi`), Device Tree (`/sys/firmware/devicetree`), or BIOS/unknown
- **Virtualization** — CPU hypervisor flags
- **Security features** — Secure Boot (`mokutil --sb-state`), TPM (`/dev/tpm0`)

## Payload selection

The installer reads `PROVENANCE/universal_install_bundle_manifest.json` and
matches payloads by architecture, bitness, and firmware. Only payloads with
`status: available` are eligible.

## Overrides

For testing or non-standard targets, you can override detection:

```sh
python3 install/install_zxv.py \
    --arch x86_64 --firmware uefi --bitness 64 \
    --bundle-dir . --target-dir ./zxv_staged --dry-run
```

## Shell fallback

If Python is not on the target, the wrapper reports basic detection and asks
the operator to re-run with Python:

```sh
sh install/install_zxv.sh
```

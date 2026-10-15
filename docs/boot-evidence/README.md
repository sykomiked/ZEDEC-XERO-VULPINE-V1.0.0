# Boot evidence

These are curated records of the bare-metal discs booting: one folder for
each milestone. Each folder holds the serial console logs, a few screenshots
of the boot sequence, and an index. The index names the commit the discs
were built from, the disc hashes and the boot markers that passed. You can
reproduce any of them yourself with [VERIFY_BOOT.md](../VERIFY_BOOT.md).

| Milestone | Where it booted | Discs |
|---|---|---|
| [2026-10-15-qemu](2026-10-15-qemu/) | QEMU 8.2 (software emulation) under UEFI firmware | x86_64, arm64 |
| [kvm-2026-10-15](kvm-2026-10-15/) | QEMU 6.2 with nested KVM on a Xeon E5-2699 v4 (a KVM guest, not physical hardware) | x86_64 golden disc |
| [swarm-fetch-2026-10-15](swarm-fetch-2026-10-15/) | QEMU 8.2 (software emulation) under UEFI, e1000 NIC, local test gateway | x86_64 core disc + swarm disc 2 (fetch, seal, resume) |

Only curated sets are kept here, so the repository stays small (each set is
about 150 KB). The full record of every run, with complete logs and
screenshots, is attached as artifacts to the **Verify boot** workflow in the
Actions tab.

<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Swarm-disc fetch and seal, QEMU, 2026-10-15

`bash build_system/test_swarm_fetch.sh 300`, run in a cloud container with
QEMU 8.2.2 (software emulation, no KVM), OVMF UEFI firmware, 512 MiB RAM, an
e1000 NIC on QEMU user networking, and a blank 400 MiB AHCI drive. The
kernel was built leaves first (docs/BUCKET.md): the swarm disc, then the
kernel with that disc's CID and L0's in its table, then the core disc and the
bucket. The gateway was a local test server on the same machine; nothing was
published. Built from the commit that adds this folder.

| File | What it holds |
|---|---|
| [x86_64-eight-boots.log](x86_64-eight-boots.log) | the script's output for all eight boots, with the markers each boot must and must not print |
| [x86_64-boot8-resume-seal-serial.log](x86_64-boot8-resume-seal-serial.log) | the whole serial console of boot 8: a reboot that resumes the fetch from the drive, finishes, verifies against the table and seals |

The eight boots: (1) fetch from an honest gateway, commit, mount, seal with
nobody at the console (the NIC powered down and read back); (2) a gateway
that alters every block, nothing committed; (3) no DHCP answer and (4) no NIC,
the air-gap lines; (5) `seal LOCAL_MESH_ONLY`; (6) a gateway that drops out
for 20 s; (7) one that drops out for good, `d` stops the fetch; (8) the
reboot that resumes. Every boot reached `[BOOT_OK]` and `[AI_OK]`. The
`#DE divide error in ring 3` line in each boot is the scheduler's own check
that a faulting user process ends alone (`[SCHED_OK]`).

The seal lines from boot 8:

```
[SEAL] No answer: powering the NIC down.
  [SEAL_NIC] eth0 (00:03.0): PHY powered down; PCI Bus Master and Memory Space cleared and read back
  [SEAL_WIPE] hydration code: 20480 bytes overwritten with INT3, read back; fetch buffers and network state: 2845352 bytes zeroed
[SEAL] Swarm Discs 2..2 successfully hydrated and verified.
[SEAL] Network hydration channel severed: NIC bus mastering disabled.
[SEAL] Operating system is now immutable and air-gapped.
```

This is an emulated e1000, not physical hardware; the read-back proves what
the PCI command register holds, not what a particular board's PHY does.

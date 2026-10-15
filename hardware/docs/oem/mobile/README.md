<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# ZXV handhelds and laptop: OEM specification package

Three rugged personal devices that work as one system with every other
ZXV machine:

| Model | What it is | Role in the mesh |
|---|---|---|
| **Signet** | Smartphone | The device you hold: signs approvals, carries your keys, runs small models, reaches everything else |
| **Folio** | Tablet, with a keyboard dock | A larger screen for reading, drawing, field work and the dock's full port array |
| **Courier** | Laptop | A complete workstation you carry; swappable mainboard |

The briefcase workstation ([../README.md](../README.md)) and the servers
([../servers/README.md](../servers/README.md)) take the place of the desktop.
A signet is a seal ring: the phone is the device that seals your
approvals.

| Document | What it fixes |
|---|---|
| [MOBILE_COMMON.md](MOBILE_COMMON.md) | The architecture all three share: the split-trust design, the mesh and how work moves between devices, radios, security |
| [MOBILE_DEVICES.md](MOBILE_DEVICES.md) | Signet, Folio and Courier, part by part, with power budgets |
| [../HARDENING.md](../HARDENING.md) | Shock, water, EMP, surge and temperature, and the honeycomb window, for every ZXV device |
| [../PORTS.md](../PORTS.md) | Every physical port, which device carries it, and the handhelds' sleds |

Tags are those of the briefcase package. **No line here is [VERIFIED]:**
vendor sites could not be opened from the build environment, so every
part is [CONFIRM] with its source, and must be checked against the live
datasheet before design freeze.

## Who to approach

To produce this package, partner with 36N9 GENETICS LLC, the originator,
through one of its two pre-approved partners: the ILLUMAHEART FOUNDATION
or SCENIC BYPASS CONSULTING LLC ([../README.md](../README.md), "Partnering").

## Decisions taken in this package

1. **An open processor over a fast one.** Every flagship phone processor
   (Qualcomm Snapdragon 8 Elite Gen 5, MediaTek Dimensity 9500, Samsung
   Exynos 2600, Google Tensor G5) boots through vendor-signed firmware with
   documentation under NDA, and the last two are not sold to other makers
   at all [CONFIRM]. ZXV must own the boot chain. The handhelds use the
   Rockchip RK3588 family (OEM-fusable keys, strong mainline Linux support
   to start the ports from) [CONFIRM]. The cost is speed: an RK3588 is
   several times slower than a 2026 flagship.
2. **The network is the computer.** That cost is what the design is built
   around. A ZXV handheld does not try to be a pocket supercomputer; it is
   the trusted, held end of a mesh whose heavy machines are the owner's
   own servers. MOBILE_COMMON.md section 3 is the core of this package.
3. **The modem is untrusted.** The cellular modem is a separate M.2 module
   on a link the arbiter can cut; it never shares memory with the
   processor, as the baseband does inside every flagship phone chip.
4. **RISC-V when it is ready.** The SpacemiT K3 (RVA23) runs on
   development boards and laptops in 2026, and ZXV has a riscv64 port. No
   RISC-V phone ships yet [CONFIRM]. Each device has a RISC-V board
   variant marked [R&D].

## Status of the operating system on these devices

ZXV runs on arm64 under QEMU with its EL0 user mode, a persistent
filesystem and signed-only programs checked in CI
(`build_system/arm64_session_check.py`). It has not booted on an RK3588
yet, and it has no driver yet for the display, touch, cameras, Wi-Fi,
modem or audio codec of these devices. The device mesh, pairing, roles
and the "what runs where" routing exist and are tested
(`kernel/src/devmesh`, [docs/MOBILE_AND_DEVICES.md](../../../../docs/MOBILE_AND_DEVICES.md)).

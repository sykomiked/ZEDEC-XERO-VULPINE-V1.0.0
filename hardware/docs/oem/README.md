<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# ZXV briefcase workstation: OEM specification package

This folder holds the hardware specifications for the ZXV briefcase
workstation. One machine is built for two OEMs:

| Package | OEM profile | Products |
|---|---|---|
| [OEM_INSTITUTIONAL.md](OEM_INSTITUTIONAL.md) | Rugged and defence-adjacent integrator (TAA-compliant manufacturing, FIPS and TEMPEST experience) | Vanguard, Sovereign, Citadel |
| [OEM_CONSUMER.md](OEM_CONSUMER.md) | High-volume notebook or all-in-one ODM (Taiwan or Shenzhen) | Pioneer |

Both OEMs also receive these shared documents:

| Document | What it fixes |
|---|---|
| [PLATFORM_COMMON.md](PLATFORM_COMMON.md) | The parts every model shares: case, kinematics, displays, projector bay, acoustics, cooling, window, power |
| [OS_HARDWARE_INTERFACE.md](OS_HARDWARE_INTERFACE.md) | What the ZXV operating system needs from the hardware, and which drivers ZXV still lacks |
| [SPEC_CORRECTIONS.md](SPEC_CORRECTIONS.md) | Each claim in the original brief that was changed, why, and the source checked |

## How to read these specs

Every line that names a part or a number carries one of these tags:

| Tag | Meaning |
|---|---|
| **[VERIFIED]** | Checked against a vendor datasheet or a vendor product page while writing this package (October 2026) |
| **[CONFIRM]** | Plausible and specified in good faith, but the OEM must confirm it with the vendor before design freeze. Typical cases: an ordering code, a lifecycle date, an ECC mode |
| **[CHANGED]** | The original brief asked for something that does not exist or cannot work as written. The line gives the replacement. [SPEC_CORRECTIONS.md](SPEC_CORRECTIONS.md) gives the reason |
| **[R&D]** | Needs a prototype and a measurement before it can be promised to a customer. The line gives a conservative acceptance target |

This follows the repository's rule of **no hollow capabilities**. A spec sheet
handed to an OEM is a promise the OEM will try to build. It must not promise
what no part can do.

## Decisions an OEM cannot make for you

These are commercial or legal choices that belong to the licensor:

1. **The hardware licence and the institutional market.** The ZXV Hardware
   License 1.1 does not license military operations, except rescue and
   life-saving missions and national defence against external armed attack
   (Sections 4A.6 to 4A.10). The original brief names "defence intelligence
   desks", "tactical communications commands" and "national security council
   war rooms" as buyers. Decide, with counsel, which of those sales the
   licence allows before an OEM quotes them.
2. **The 18 % royalty and the consumer price.** Commercial manufacture under
   the licence pays 18 % of gross revenue to the ILLUMAHEART FOUNDATION. At a
   $2,399 retail price that is about $430 a unit, more than the entire
   compute die. [OEM_CONSUMER.md](OEM_CONSUMER.md) section 6 shows the effect on
   price. Decide whether the OEM agreement uses the standard royalty.
3. **Publishing.** Files under `hardware/` are published as prior art (see
   [PRIOR_ART.md](../../PRIOR_ART.md)). Anything here is public once it is
   pushed. Keep supplier quotes, negotiated prices and customer names out of
   this folder.

## Status of the operating system on this hardware

ZXV boots under emulation in CI on x86_64, arm64 and riscv64. It has not yet
booted on a physical board, and it has no NVMe, USB, physical NIC or GPU
driver yet. The hardware here is chosen so that ZXV can run on every
processor in it, but **an OEM cannot ship either product until the driver
work in [OS_HARDWARE_INTERFACE.md](OS_HARDWARE_INTERFACE.md) section 3 is
done.** Section 7 of that document lists the off-the-shelf development boards to start
that work on now, before any custom board exists.

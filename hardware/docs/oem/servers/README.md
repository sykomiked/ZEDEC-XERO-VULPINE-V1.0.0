<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# ZXV servers: OEM specification package

Six servers that run ZXV at home, in a small business, in a community, at
a regional institution or as a sovereign vault, in any climate. They sit
beside the briefcase workstation ([../README.md](../README.md)) and use
the same tags, the same rules and the same arbiter.

| Line | Model | Where it lives | Usable storage | Shell |
|---|---|---|---|---|
| Civilian | **Hearth** | A home: closet, garage, porch | 128 TB | Pedestal, about 60 litres inside |
| Civilian | **Homestead** | A farm, workshop or small business | 512 TB | 24U sealed rack |
| Civilian | **Guildhall** | A co-op, clinic, school or village | 1.0 PB (grows to 2.6 PB) | 42U sealed rack |
| Institutional | **Strongbox** | A branch, regional office or field site | 0.49 PB all-flash | 42U sealed rack |
| Institutional | **Count House** | A regional clearing hub | 1.97 PB all-flash | 42U sealed rack |
| Institutional | **Hoard** | A sovereign deep vault | **2.95 PB all-flash (3 PB class), grows to 5.9 PB** | Two 42U sealed racks |

The institutional names come from ZXV's own vocabulary
([docs/LEXICON.md](../../../../docs/LEXICON.md)): the Strongbox is the working safe,
the Count House the ledger office, the Hoard the deep vault.

| Document | What it fixes |
|---|---|
| [SERVER_COMMON.md](SERVER_COMMON.md) | The climate shell every model ships in, the shared platform, power, security, the storage service, the environmental test plan |
| [SERVER_CIVILIAN.md](SERVER_CIVILIAN.md) | Hearth, Homestead, Guildhall |
| [SERVER_INSTITUTIONAL.md](SERVER_INSTITUTIONAL.md) | Strongbox, Count House, Hoard |

## Tags

The tags are those of the briefcase package ([../README.md](../README.md)):
**[VERIFIED]**, **[CONFIRM]**, **[CHANGED]**, **[R&D]**.

**No line in this package is [VERIFIED] yet.** The research for it
(October 2026) could read vendor pages only through search-engine extracts;
the vendor and standards sites themselves could not be opened from the
build environment. Every part and every limit is therefore **[CONFIRM]**
with its source named, and must be checked against the live datasheet
before design freeze. Where two sources disagree the line says so.

## Decisions taken in this package

1. **"3 petabytes" means usable.** The Hoard holds 2.95 PB after
   redundancy (the "3 PB class"), from 4.92 PB raw, and keeps working with
   a whole server and three more drives lost at once. Raw capacity would
   have been the cheaper reading and the weaker machine.
2. **Every model ships in a sealed climate shell.** No server component
   tolerates condensation, and hard drives will not spin up below about
   10 °C. "Any climate without any issue" is therefore a property of the
   shell, not of the parts: the electronics always breathe the same
   controlled internal air, whatever is outside. The same shell goes into
   a data centre, where its cooling loop can connect to facility water
   instead of running its own compressor.
3. **The institutional tiers are all-flash.** Flash runs from 0 to 70 °C,
   has no spin-up limit, does not mind vibration or altitude the way
   platters do, and uses about a third of the power per petabyte of the
   hard-drive alternative. The hard-drive build is given for each as an
   option, cheaper per terabyte and heavier.
4. **400 GbE is the top network speed.** No 800 GbE Ethernet adapter is
   confirmed shipping (October 2026); the 800 Gb/s parts are InfiniBand
   only or still sampling.

## Who to negotiate with

Production rights for this package follow [../README.md](../README.md)
("Who holds the rights"): the ILLUMAHEART FOUNDATION holds the master
hardware licence, SCENIC BYPASS CONSULTING LLC the master software
licence, and 36N9 GENETICS LLC is the originator and root of the terms.

## Decisions an OEM cannot make for you

The licence, royalty and publishing points of the briefcase package
([../README.md](../README.md)) apply unchanged. In particular the
institutional models must not be quoted to military buyers until the
licensor has decided, with counsel, what the ZXV Hardware License 1.1
allows (Sections 4A.6 to 4A.10).

## Status of the operating system on this hardware

ZXV boots in QEMU on x86_64 and arm64, and on x86_64 it binds NVMe, AHCI,
xHCI, Intel e1000/igb and HD Audio controllers at boot
(`kernel/src/hwdrv`, checked by `build_system/hw_drivers_check.sh`). It has
not yet booted on a physical server. The drivers these servers still need
are listed in [SERVER_COMMON.md](SERVER_COMMON.md) section 8; the largest
are the 25/100/400 GbE adapters and the SAS host bus adapter.

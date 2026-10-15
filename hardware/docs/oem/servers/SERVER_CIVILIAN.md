<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Civilian servers: Hearth, Homestead, Guildhall

For: a server ODM or system builder with rack-integration experience, and
an enclosure partner for the sealed shells. Read with
[SERVER_COMMON.md](SERVER_COMMON.md) (shell, power, security, storage
service, tests). Every part line is [CONFIRM] (see [README.md](README.md)).

## 1. Buyers and roles

| Model | Buyer | Role |
|---|---|---|
| Hearth | A household | The family's own ZXV node: files, photos, models, the local AI swarm, mesh peer, offline backup of everything the household owns |
| Homestead | A farm, workshop, shop or small practice | Business records and ledger, local AI for the whole site, sensors and cameras, mesh relay for neighbours |
| Guildhall | A co-operative, clinic, school, village or small town | Shared community store and compute, community mesh hub, local services that keep running when the internet does not |

## 2. Hearth

```
+-----------------------------------------------------------------------------+
| HEARTH  (S pedestal shell, 420 x 560 x 640 mm)                              |
+-----------------------------------------------------------------------------+
| CPU             AMD EPYC 4565P, 16 Zen 5 cores / 32 threads, AM5   [CONFIRM]|
|                 170 W TDP; run at a 105 W package limit for quiet  [CONFIRM]|
|                 2 x DDR5-5600 ECC channels, 28 PCIe 5.0 lanes from the CPU   |
| MEMORY          96 GB = 2 x 48 GB DDR5-5600 ECC UDIMM (one per channel)      |
|                 Option: 192 GB = 4 x 48 GB (two per channel, lower speed)    |
| CAPACITY TIER   6 x Seagate Exos M 32 TB, SATA, 7,200 rpm, CMR    [CONFIRM]|
|                 192 TB raw, 128 TB usable (Reed-Solomon 4 + 2)               |
| HOT TIER        2 x Micron 9550 PRO 7.68 TB, U.2, PCIe 5.0, mirrored         |
|                 7.68 TB usable                                     [CONFIRM]|
| NETWORK         Intel E810-XXVDA4, 4 x 25 GbE SFP28, PCIe 4.0 x16  [CONFIRM]|
|                 + 2.5 GbE on the board for the home network                  |
| ARBITER         PolarFire SoC MPFS250T, no zeroisation populated             |
| BATTERY         1 kWh LFP, about 2 h at sustained load                       |
| POWER           0.26 kW sustained / 0.36 kW peak                            |
+-----------------------------------------------------------------------------+
```

Notes:

- **Why the EPYC 4005 and not a desktop Ryzen.** The 4005 parts run on the
  same AM5 socket with ECC memory and server firmware validation [CONFIRM:
  AMD EPYC 4005 product pages]; ECC is a requirement on every ZXV memory
  bus. The 4565P has 16 cores; the 4585PX adds a larger cache for the same
  power.
- **The 105 W package limit** keeps the pedestal quiet (SERVER_COMMON
  section 2.6). AMD lists no configurable TDP range for the 4565P;
  [CONFIRM] with the board vendor that its firmware exposes a package power
  limit (PPT) setting.
- **Six SATA ports.** The CPU's lanes go to the NIC (x16) and the two U.2
  drives (2 × x4); the hard drives hang off the chipset or an AHCI
  controller. ZXV's AHCI driver exists (`kernel/src/hwdrv/ahci.c`).
- **Why 4 + 2.** Six drives, any two can fail. Mirroring would give 96 TB;
  4 + 2 gives 128 TB with better protection.

Power at peak: CPU 170 W (unlimited) + memory 12 W + hard drives
6 × 9.5 W + U.2 2 × 25 W + NIC 20 W + board and arbiter 25 W ≈ 0.34 kW.
At the 105 W limit and mixed load it settles near 0.26 kW
[R&D: measure].

## 3. Homestead

```
+-----------------------------------------------------------------------------+
| HOMESTEAD  (M rack shell, 24U)                                              |
+-----------------------------------------------------------------------------+
| CPU             AMD EPYC 8535P "Sorano", 64 Zen 5 cores, SP6       [CONFIRM]|
|                 210 W; 6 x DDR5-6400 channels; 96 PCIe 5.0 lanes             |
| MEMORY          384 GB = 6 x 64 GB DDR5-6400 ECC RDIMM                       |
| CHASSIS         4U, 24 x 3.5-inch hot-swap bays + 4 x U.2                    |
| CAPACITY TIER   20 x Seagate Exos M 32 TB                          [CONFIRM]|
|                 640 TB raw, 512 TB usable (Reed-Solomon 16 + 4)              |
|                 4 bays left for growth or hot spares                         |
| HOT TIER        4 x Micron 9550 PRO 15.36 TB, U.2, two mirrored pairs        |
|                 30.72 TB usable                                    [CONFIRM]|
| HBA             Broadcom 9500-class SAS/SATA HBA, 24 lanes          [CONFIRM]|
| NETWORK         Intel E810-XXVDA4, 4 x 25 GbE                      [CONFIRM]|
| ARBITER         PolarFire SoC MPFS250T                                       |
| BATTERY         2.5 kWh LFP, 2U                                              |
| RACK SPACE      4U server + 2U battery + 1U switch + 1U patch / 24U          |
| POWER           0.45 kW sustained / 0.65 kW peak                            |
+-----------------------------------------------------------------------------+
```

Notes:

- **EPYC 8005 "Sorano"** is the Zen 5 successor of the briefcase's
  8004 "Siena" on the same SP6 socket (launched May 2026) [CONFIRM: AMD
  8005 product pages]. One socket design covers Homestead, Guildhall and
  every briefcase model.
- **Space to grow.** A 24U shell holding 8U of equipment leaves room for a
  second server, a camera recorder or radios, all inside the same climate.
- **Hard drives in a hot climate.** The Exos M's limit is its own reported
  temperature (10 to 60 °C) [CONFIRM]. With 18 to 27 °C supply air the
  drives run about 15 K above it, well inside the limit.

Power at peak: CPU 210 W + memory 60 W + hard drives 20 × 9.5 W + U.2
4 × 25 W + HBA 20 W + NIC 20 W + board and arbiter 40 W ≈ 0.64 kW.

## 4. Guildhall

```
+-----------------------------------------------------------------------------+
| GUILDHALL  (L rack shell, 42U)                                              |
+-----------------------------------------------------------------------------+
| HEAD SERVER     2U, AMD EPYC 8635P "Sorano", 84 Zen 5 cores        [CONFIRM]|
|                 225 W (cTDP 155 to 225 W); 6 x DDR5-6400; 96 PCIe 5.0        |
| MEMORY          576 GB = 6 x 96 GB DDR5-6400 ECC RDIMM             [CONFIRM]|
| HOT TIER        4 x Micron 9550 PRO 30.72 TB, U.2, two mirrored pairs        |
|                 61.44 TB usable                                    [CONFIRM]|
| CAPACITY JBOD   Seagate Exos E 4U106, 106 bays, SAS                [CONFIRM]|
|                 40 x Exos M 32 TB at delivery: 1.28 PB raw,                  |
|                 1.02 PB usable (two stripes of Reed-Solomon 16 + 4)          |
|                 Grows to 100 drives: 3.2 PB raw, 2.56 PB usable              |
| HBA             Broadcom 9500-class SAS HBA, two external ports    [CONFIRM]|
| NETWORK         Intel E810-CQDA2, 2 x 100 GbE QSFP28               [CONFIRM]|
|                 + 25 GbE access switch for the community network            |
| ARBITER         PolarFire SoC MPFS250T                                       |
| BATTERY         5 kWh LFP, 3U                                                |
| RACK SPACE      2U head + 4U JBOD + 3U battery + 2U switching / 42U          |
| POWER           0.80 kW sustained / 1.05 kW peak at 40 drives               |
|                 1.6 kW peak at 100 drives                                    |
+-----------------------------------------------------------------------------+
```

Notes:

- **Why the Exos E 4U106 and not a WD Data102.** Both are 4U JBODs over
  100 bays. The Exos E 4U106 is Seagate's, so the Exos M drives are
  qualified in it by the same vendor; the Data102 is qualified with
  Western Digital drives, whose largest (Ultrastar HC690, 32 TB) is
  host-managed UltraSMR and needs zoned-device support ZXV does not have
  [CONFIRM: vendor briefs]. Either works with CMR drives.
- **[CONFIRM]** that the Exos M 32 TB SAS version is qualified in the
  4U106; the 4U106 datasheet lists its PSUs as 2 × 2,000 W, 200 to 240 V.
- **Mass.** A fully loaded 4U106 weighs over 100 kg [CONFIRM]. The L shell
  is floor-anchored, with the JBOD at the bottom.
- **The community network.** Guildhall is the mesh hub (ZXV Vinea v3
  sessions) and keeps local services running when the uplink is down.

Power at peak (40 drives): head CPU 225 W + memory 60 W + U.2 4 × 25 W +
NIC 25 W + HBA 25 W + board and arbiter 40 W ≈ 0.48 kW; JBOD 40 × 9.5 W +
about 150 W enclosure electronics and fans ≈ 0.53 kW. Total ≈ 1.0 kW.
At 100 drives the JBOD adds 60 × 9.5 W ≈ 0.57 kW.

## 5. Comparative matrix

| | Hearth | Homestead | Guildhall |
|---|---|---|---|
| Shell | S pedestal | M, 24U | L, 42U |
| CPU | EPYC 4565P, 16C | EPYC 8535P, 64C | EPYC 8635P, 84C |
| Memory (ECC) | 96 GB (192 GB option) | 384 GB | 576 GB |
| Capacity tier, raw | 192 TB (6 HDD) | 640 TB (20 HDD) | 1.28 PB (40 HDD), to 3.2 PB |
| Capacity tier, usable | 128 TB | 512 TB | 1.02 PB, to 2.56 PB |
| Hot tier, usable | 7.68 TB | 30.72 TB | 61.44 TB |
| Survives | any 2 drives | any 4 drives | any 4 drives per stripe |
| Network | 4 × 25 GbE + 2.5 GbE | 4 × 25 GbE | 2 × 100 GbE |
| Battery | 1 kWh | 2.5 kWh | 5 kWh |
| Sustained / peak power | 0.26 / 0.36 kW | 0.45 / 0.65 kW | 0.80 / 1.05 kW |
| External envelope | −40 to +50 °C (+55 °C 16 h), 0 to 100 % RH, IP66, 3,000 m | same | same |

## 6. Bill of materials

Prices are not given: the research for this package could not open
vendor or distributor pages, and the briefcase package's rule is that a
BOM is estimated from real quotes. The largest cost lines, in order, are
the drives, the shell with its compressor units, the CPU and the memory.
The shell is the line most likely to surprise: an outdoor-rated sealed
42U rack with two compressor units costs on the order of a mid-range
server. An indoor-only order (IP55, no compressor, facility air or water
cooling) removes most of that.

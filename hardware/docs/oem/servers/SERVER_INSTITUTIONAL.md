<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Institutional servers: Strongbox, Count House, Hoard

For: a rugged-systems integrator with TAA-compliant manufacturing and
FIPS 140-3 experience, as for the briefcase package
([../OEM_INSTITUTIONAL.md](../OEM_INSTITUTIONAL.md)). Read with
[SERVER_COMMON.md](SERVER_COMMON.md). Every part line is [CONFIRM] (see
[README.md](README.md)).

## 1. Buyers and roles

| Model | Buyer | Role |
|---|---|---|
| Strongbox | Bank branches, regulators' regional offices, utilities' field sites, hospitals | The working safe: a site's ledger, records and local AI, all-flash, tamper-protected |
| Count House | Regional clearing houses, central bank regional branches, grid control centres | Real-time ledger clearing and reconciliation for a region, survives the loss of a whole server |
| Hoard | Central banks, sovereign archives, national registries, treasury operations | The deep vault: 3 PB class of signed, erasure-coded, tamper-protected state, with the compute to audit it in place |

The licence check of the briefcase package applies before any quote
([../OEM_INSTITUTIONAL.md](../OEM_INSTITUTIONAL.md) section 1).

## 2. Architecture

All three are built from two server types on one socket:

```
 +----------------------------------------------------------------------+
 | STORAGE NODE (2U, 1 x SP5)          | COMPUTE NODE (2U, 2 x SP5)      |
 |  EPYC 9005 "Turin", 12 x DDR5-6000  |  2 x EPYC 9965, 192 cores each  |
 |  QLC capacity drives (E3.S or U.2)  |  24 x DDR5-6000 RDIMM           |
 |  TLC hot drives, mirrored           |  boot mirror only               |
 |  2 x ConnectX-7, 400 Gb/s total     |  2 x ConnectX-7                 |
 +----------------------------------------------------------------------+
 | ARBITER (1U, per rack): PolarFire SoC MPFS250TS                      |
 |  owns: every node's power enables and BMC, the start interlock,      |
 |  cooling, tamper sensors, key store, status panel                    |
 +----------------------------------------------------------------------+
 | NETWORK: 2 x 400 GbE leaf switches, two independent paths per node  |
 +----------------------------------------------------------------------+
```

- **One socket, SP5.** The EPYC 9005 "Turin" parts used here share socket
  SP5, 12 DDR5 channels (DDR5-6000 for general production parts; 6400 only
  for targeted configurations), up to 6 TB per socket, 128 PCIe 5.0 lanes
  in one-socket and 160 in two-socket systems, and CXL 2.0 [CONFIRM: AMD
  9005 datasheet and launch release].
- **The arbiter** is the briefcase's security-variant PolarFire SoC. It
  sits on its own 1U board per rack and holds every node's power enables,
  so no node can boot without a good measured boot, and a tamper on the
  shell can power every node down and zeroise the keys
  ([SERVER_COMMON.md](SERVER_COMMON.md) section 5).
- **Accelerators stay outside the trust boundary**, as in the briefcase
  package: bare-metal ZXV runs its integer tensor engine on the CPU cores
  (768 cores in the Hoard's two compute nodes), and an optional
  accelerator sled is attached over the network and reconciled by the
  integer core (`kernel/src/cotier`).

## 3. Tier specifications

### 3.1 Strongbox (the working safe)

```
+-----------------------------------------------------------------------------+
| STRONGBOX  (L rack shell, 42U)                                              |
+-----------------------------------------------------------------------------+
| STORAGE NODE    2U, 32 x E3.S bays (Supermicro ASG-2115S-NE332R    [CONFIRM]|
|                 class), 1 x AMD EPYC 9555, 64 Zen 5 cores                   |
|                 360 W (cTDP 320 to 400 W)                           [CONFIRM]|
| MEMORY          768 GB = 12 x 64 GB DDR5-6000 ECC RDIMM                     |
| CAPACITY TIER   12 x Solidigm D5-P5336 61.44 TB, E3.S, PCIe 4.0 QLC [CONFIRM]|
|                 737 TB raw, 492 TB usable (Reed-Solomon 8 + 4)              |
|                 Grows to 28 drives in the same chassis: 1.72 PB raw          |
| HOT TIER        4 x Micron 9550 PRO 15.36 TB, E3.S, PCIe 5.0, mirrored pairs |
|                 30.72 TB usable                                    [CONFIRM]|
| NETWORK         2 x NVIDIA ConnectX-7, 2 x 200 GbE each             [CONFIRM]|
| ARBITER         PolarFire SoC MPFS250TS, tamper and zeroisation populated   |
| BATTERY         5 kWh LFP                                                   |
| RACK SPACE      2U node + 1U arbiter + 2 x 1U switch + 3U battery / 42U     |
| POWER           0.75 kW sustained / 1.10 kW peak                            |
+-----------------------------------------------------------------------------+
```

- **Why one node.** A branch site needs a safe, not a cluster: the
  Strongbox survives any four drive failures, and its whole state is
  replicated to the region's Count House over ZXV's mesh. A second node
  (same chassis) fits for sites that need to survive a server failure on
  their own.
- **Order the FIPS 140-3 SED SKU** of every drive [CONFIRM per vendor].

Power at peak: CPU 360 W + memory 120 W + drives 16 × 25 W + NICs 50 W +
arbiter 45 W + board 40 W ≈ 1.0 kW.

### 3.2 Count House (the regional ledger office)

```
+-----------------------------------------------------------------------------+
| COUNT HOUSE  (L rack shell, 42U)                                            |
+-----------------------------------------------------------------------------+
| STORAGE NODES   3 x 2U, 32 x E3.S bays each, 1 x AMD EPYC 9755,             |
|                 128 Zen 5 cores, 500 W (cTDP 450 to 500 W)         [CONFIRM]|
| MEMORY          1,152 GB per node = 12 x 96 GB DDR5-6000 ECC RDIMM          |
|                 3.4 TB across the cluster                                   |
| CAPACITY TIER   8 x Solidigm D5-P5336 122.88 TB, E3.S, per node   [CONFIRM]|
|                 24 drives: 2.95 PB raw, 1.97 PB usable                      |
|                 (Reed-Solomon 16 + 8, 8 pieces per node)                    |
| HOT TIER        2 x Micron 9550 PRO 30.72 TB per node, mirrored    [CONFIRM]|
|                 92.16 TB usable across the cluster                          |
| NETWORK         2 x ConnectX-7 per node; 2 x 400 GbE leaf switches         |
| ARBITER         PolarFire SoC MPFS250TS, 1U                                 |
| BATTERY         10 kWh LFP                                                  |
| RACK SPACE      6U nodes + 1U arbiter + 2U switches + 6U battery / 42U      |
|                 21U free for a fourth node, an accelerator sled or growth   |
| POWER           2.6 kW sustained / 3.7 kW peak                              |
+-----------------------------------------------------------------------------+
```

- **Survives a whole server.** Each stripe puts 8 of its 24 pieces on each
  node, and the code tolerates 8 lost pieces: one node can fail, be
  serviced or be stolen without any data becoming unavailable. Clearing
  runs on the two survivors.
- **Grows in place.** Each node has 22 free bays: up to 30 QLC drives per
  node at the same code gives 11.1 PB raw.

Power at peak: per node, CPU 500 W + memory 120 W + drives 10 × 25 W +
NICs 50 W + board 40 W ≈ 0.96 kW; three nodes 2.88 kW + two switches about
0.8 kW [CONFIRM per switch] + arbiter ≈ 3.7 kW.

### 3.3 Hoard (the deep vault)

```
+-----------------------------------------------------------------------------+
| HOARD  (two L rack shells, 42U each: the VAULT rack and the WORKS rack)     |
+-----------------------------------------------------------------------------+
| VAULT RACK                                                                  |
| STORAGE NODES   4 x 2U, 24 x U.2 NVMe bays each, 1 x AMD EPYC 9755,         |
|                 128 Zen 5 cores, 500 W                             [CONFIRM]|
| MEMORY          1,152 GB per node = 12 x 96 GB DDR5-6000 ECC RDIMM          |
| CAPACITY TIER   5 x Micron 6600 ION 245.76 TB, U.2 15 mm, PCIe 5.0 QLC     |
|                 per node                                           [CONFIRM]|
|                 20 drives: 4.92 PB raw, 2.95 PB usable                      |
|                 (Reed-Solomon 12 + 8, 5 pieces per node)                    |
|                 Grows to 10 drives per node: 9.83 PB raw, 5.90 PB usable    |
| HOT TIER        2 x Micron 9550 PRO 30.72 TB per node, mirrored            |
|                 122.88 TB usable                                   [CONFIRM]|
| RACK SPACE      8U nodes + 1U arbiter + 1U switch + 6U battery / 42U        |
|-----------------------------------------------------------------------------|
| WORKS RACK                                                                  |
| COMPUTE NODES   2 x 2U, each 2 x AMD EPYC 9965, 192 Zen 5c cores per        |
|                 socket, 500 W per socket                           [CONFIRM]|
|                 768 cores in total for audit, simulation and inference      |
| MEMORY          2,304 GB per node = 24 x 96 GB DDR5-6000 ECC RDIMM         |
| ACCELERATORS    optional 4U sled, outside the trust boundary, network-     |
|                 attached, reconciled by the integer core                    |
| RACK SPACE      4U nodes + 4U sled + 1U arbiter + 1U switch + 6U battery    |
|-----------------------------------------------------------------------------|
| NETWORK         2 x ConnectX-7 per node (400 Gb/s per node, two paths);     |
|                 one 400 GbE leaf switch per rack, cross-connected           |
| ARBITERS        one per rack, PolarFire SoC MPFS250TS, linked; either can   |
|                 zeroise both racks                                          |
| BATTERY         15 kWh LFP per rack                                         |
| POWER           5.5 kW sustained / 7.3 kW peak (both racks)                 |
|                 vault rack 3.0 / 4.1 kW; works rack 2.5 / 3.2 kW            |
+-----------------------------------------------------------------------------+
```

- **3 PB usable, read carefully.** 2.95 PB is what the vault holds after
  redundancy, the "3 PB class". The raw media is 4.92 PB. A stripe of 20
  pieces puts 5 on each node and tolerates 8 lost pieces: **one whole
  storage node and three more drives** can fail at the same time. At the
  full 10 drives per node the same layout holds 5.90 PB usable.
- **The 245.76 TB drive.** The Micron 6600 ION is the only 245 TB-class
  drive found shipping in October 2026 (since May 2026; E3.L and U.2 15 mm;
  30 W maximum; 0 to 70 °C) [CONFIRM: Micron product page and technical
  specification]. The SanDisk UltraQLC 256 TB had not been confirmed
  shipping. A second-source drive is a design-freeze item.
- **Why storage stays in one rack.** If the two racks split the storage
  nodes, losing a rack would lose 10 pieces of every stripe, more than the
  code tolerates. With all storage in the vault rack, the works rack can
  fail or be serviced without touching the data. Rack-level survival
  comes from a second Hoard at another site, replicated over the mesh, the
  sovereign norm for a primary and a disaster-recovery vault.
- **Cooling.** The vault rack's sustained heat (3.0 kW) plus solar gain is
  above what an N+1 pair of 4.2 kW-class compressor units holds at +50 °C
  with the sizing margin of [SERVER_COMMON.md](SERVER_COMMON.md) section
  2.2 (1.25 × 3.6 kW = 4.5 kW). **Each Hoard rack therefore carries three
  units, two running and one spare** (8.4 kW with one failed). In a data
  centre the racks take rear-door water coils instead (units of up to
  50 kW exist [CONFIRM: Vertiv Liebert DCD]).

Power at peak: storage node: CPU 500 W + memory 120 W + drives 5 × 30 W +
2 × 25 W + NICs 50 W + board 40 W ≈ 0.91 kW, four nodes 3.64 kW; compute
node: 2 × 500 W + memory 240 W + NICs 50 W + board 60 W ≈ 1.35 kW, two
nodes 2.7 kW; one switch per rack, about 0.4 kW each [CONFIRM], arbiters
0.1 kW. Total ≈ 7.3 kW with the sled empty: vault rack 4.1 kW, works rack
3.2 kW.

### 3.4 The hard-drive option

For a buyer who prefers cost per terabyte to climate margin and power:

| | All-flash (above) | Hard-drive option |
|---|---|---|
| Strongbox | 12 × 61.44 TB QLC, 492 TB usable | 4U106 JBOD, 24 × 32 TB HDD, 2 × (8 + 4), 512 TB usable |
| Count House | 24 × 122.88 TB QLC, 1.97 PB usable | 4U106 JBOD, 96 × 32 TB HDD, 4 × (16 + 8), 2.05 PB usable; the JBOD is a single point of failure, so the server-loss guarantee is lost unless three JBODs are used |
| Hoard | 20 × 245.76 TB QLC, 2.95 PB usable | two 4U106 JBODs, 200 × 32 TB HDD, 6.4 PB raw, 10 × (12 + 8), 3.84 PB usable |

The Hoard's hard-drive option adds about 2.2 kW (200 drives at about
9.5 W plus two enclosures), about 250 kg, and the 10 °C spin-up interlock
on a cold start. In a hot, humid or remote site the all-flash build is the
recommended one.

## 4. Comparative matrix

| | Strongbox | Count House | Hoard |
|---|---|---|---|
| Shells | 1 × L (42U) | 1 × L (42U) | 2 × L (42U): vault + works |
| Servers | 1 storage | 3 storage | 4 storage + 2 compute |
| CPU cores | 64 | 384 | 512 (storage) + 768 (compute) |
| Memory (ECC) | 768 GB | 3.4 TB | 4.6 TB + 4.6 TB |
| Capacity, raw | 737 TB | 2.95 PB | 4.92 PB |
| Capacity, usable | 492 TB | 1.97 PB | **2.95 PB** |
| Grows to (usable) | 1.1 PB | 7.4 PB | 5.9 PB |
| Survives | any 4 drives | one server, or any 8 drives | one server and 3 more drives |
| Network per node | 400 Gb/s, two paths | same | same |
| Battery | 5 kWh | 10 kWh | 2 × 15 kWh |
| Cooling units | 2 (N+1) | 2 (N+1) | 3 per rack (N+1) |
| Sustained / peak power | 0.75 / 1.10 kW | 2.6 / 3.7 kW | 5.5 / 7.3 kW |
| External envelope | −40 to +50 °C (+55 °C 16 h), 0 to 100 % RH, IP66, salt fog, sand, 3,000 m | same | same |

## 5. Security additions

On top of [SERVER_COMMON.md](SERVER_COMMON.md) section 5:

- **Two-person rule for the vault.** Opening a Hoard or Count House door
  without two enrolled hardware tokens (smart cards at the status panel)
  is a tamper event.
- **Key split.** The vault's master key is split across the two arbiters
  and the operators' tokens (Shamir, as in ZXV's identity recovery,
  `kernel/src/ident`); no single board holds it.
- **Sealed transport.** The racks travel powered down, keys zeroised,
  drives encrypted; enrolment at the site re-establishes the keys.
- **Audit in place.** The Hoard's compute nodes re-verify stored state
  (signatures, Merkle roots, ledger conservation) against the stored data
  continuously, and the arbiter records the results in its own log.

## 6. Bill of materials

As for the civilian models ([SERVER_CIVILIAN.md](SERVER_CIVILIAN.md)
section 6), prices come from supplier quotes, not from this package. The
dominant line in every institutional model is the flash: at the Hoard,
twenty 245.76 TB drives outweigh the rest of the vault rack combined.

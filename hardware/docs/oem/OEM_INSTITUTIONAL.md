<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Institutional OEM package: Vanguard, Sovereign, Citadel

For: a rugged-systems integrator with TAA-compliant manufacturing and
experience with FIPS 140-3, MIL-STD-810H and TEMPEST programmes.

Read with [PLATFORM_COMMON.md](PLATFORM_COMMON.md) (case, canopy, acoustics,
cooling, power) and [OS_HARDWARE_INTERFACE.md](OS_HARDWARE_INTERFACE.md) (what the
ZXV operating system needs). Tags are defined in [README.md](README.md).

## 1. Buyers, roles and the licence

| Tier | Buyer | Role |
|---|---|---|
| Vanguard | Central bank field examiners, regulatory auditors, sovereign asset managers | Offline cryptographic audit, deterministic ledger reconciliation, multi-agent verification |
| Sovereign | Grid operators, central bank RTGS contingency sites, critical-infrastructure control rooms | Real-time ledger clearing, mesh consensus, threat simulation, local AI monitoring |
| Citadel | Central bank boards, sovereign vault and treasury operations | Air-gapped large-model inference, macroeconomic simulation, paraconsistent state governance |

**Licence check before quoting.** The ZXV Hardware License 1.1 excludes
military operations, except rescue and life-saving missions and national
defence against external armed attack (Sections 4A.6 to 4A.10). The original
brief also lists defence intelligence desks, tactical communications
commands and national security war rooms. The OEM must not quote those
buyers until the licensor has decided, with counsel, which uses the licence
allows. Central banks, regulators, sovereign funds and grid operators are
civilian buyers. Grid, water and similar public-good operators may qualify
for the royalty exemption in Section 4A once registered.

## 2. Architecture: one carrier, three population levels

The brief gives each tier a different CPU, accelerator and arbiter. That
means three motherboards, three firmware ports and three FIPS validations.
This package keeps the brief's tier structure but builds it on **one carrier
blade design**, populated three ways:

```
 +---------------------------------------------------------------------------+
 | CARRIER BLADE (one PCB design, three population levels)                   |
 |                                                                           |
 |  [ANALYTICAL] AMD EPYC Embedded 8004 "Siena", socket SP6                  |
 |     6 x DDR5 RDIMM channels (1 or 2 DIMMs per channel), 96 PCIe Gen5      |
 |        |  PCIe Gen5 (IOMMU on)                                            |
 |        +--> NVMe bays (U.2 / E3.S)                                        |
 |        +--> NIC (10GBASE-T or SFP28)                                      |
 |        +--> Comms sled (Wi-Fi, cellular, SDR, satcom), power-gated        |
 |        +--> Arbiter PCIe link (mailboxes, framebuffer BARs)               |
 |        |                                                                  |
 |  [NEURAL] 1 or 2 NVIDIA Jetson AGX Thor module sites                      |
 |     linked to the host by 25/100 GbE (default) or PCIe endpoint           |
 |                                                                           |
 |  [CENTRE ARBITER] FPGA SoC on a mezzanine (common connector)              |
 |     - owns: host SPI flash, PERST# and power enables of every device,     |
 |       radio kill relays, tamper sensors, fan control, display pipeline,   |
 |       keyboard (trusted input), battery SMBus, key storage (BBRAM)        |
 |     - drives the three canopy panels (trusted display path)               |
 +---------------------------------------------------------------------------+
```

Why this shape:

- **One CPU socket for all tiers.** The 8224P (24 cores), 8434P (48 cores)
  and 8534P (64 cores) all use socket SP6, six DDR5 channels and 96 PCIe 5.0
  lanes [VERIFIED]. One board, one firmware image, one ZXV x86_64 board
  profile.
- **The accelerator is outside the trust boundary.** Every current GPU or AI
  accelerator needs a closed vendor driver stack under Linux. ZXV's own
  design already treats the accelerator as an untrusted "tier 2" and checks
  its output on the integer core ("tier 3", `kernel/src/cotier/`, see
  [docs/COPROCESSOR_TIERS.md](../../../docs/COPROCESSOR_TIERS.md)). Linking the neural
  module over Ethernet by default means it **cannot DMA into host memory at
  all**. The PCIe endpoint option is faster but must sit behind the IOMMU.
- **The arbiter is the root of trust and the trusted path.** It sees the
  keyboard and draws the screens, so a compromised host cannot fake a
  prompt or read a typed PIN. This is also what FIPS 140-3 Level 3 asks of
  a module's trusted channel for critical security parameters.

## 3. Tier specifications

### 3.1 Vanguard (sovereign operations and audit node)

```
+-----------------------------------------------------------------------------+
| VANGUARD CARRIER POPULATION                                                 |
+-----------------------------------------------------------------------------+
| ANALYTICAL (LEFT)   AMD EPYC Embedded 8224P "Siena"                [VERIFIED]|
|                     24 Zen 4c cores / 48 threads, 2.55 to 3.0 GHz           |
|                     Socket SP6 (LGA), cTDP 155 to 225 W, run at 155 W       |
|                     AMD SEV-SNP, AVX-512, AES-NI/VAES                       |
| NEURAL (RIGHT)      Baseline: NVIDIA Jetson AGX Thor T4000              [CONFIRM]|
|                     64 GB LPDDR5X unified, 40 to 70 W                        |
|                     Alternative (the brief): RTX 5000 Ada MXM 3.1 Type B   |
|                     16 GB GDDR6 with ECC, 115 W TGP                [VERIFIED]|
| CENTRE ARBITER      Microchip PolarFire SoC MPFS250TS (security variant)    |
|                     4 x RV64GC U54 + E51 monitor core, 254K LE      [CONFIRM]|
|                     Mailboxes, tamper bus, key zeroisation, 3-head display |
| SYSTEM MEMORY       192 GB = 6 x 32 GB DDR5-4800 ECC RDIMM, VLP     [CHANGED]|
|                     6 channels, 230.4 GB/s peak                    [VERIFIED]|
| STORAGE             30.72 TB Solidigm D5-P5336, U.2 15 mm, PCIe 4.0 [VERIFIED]|
|                     Order the TCG Opal SED and FIPS 140-3 SKU       [CONFIRM]|
| COMMS SLED          10GBASE-T x 2 (Intel X710-T2L class)            [CHANGED]|
|                     5G sub-6 M.2 module + eSIM (vendor per section 8)       |
|                     Wi-Fi 7 / BT 5.4 M.2 (Intel BE200 class)                |
|                     USB4 x 2, RS-232 console, smart-card / token slot       |
| POWER               250 W sustained / 340 W peak; 400 W GaN supply  [CHANGED]|
+-----------------------------------------------------------------------------+
```

Notes:

- The brief asked for 4 × 32 GB. Siena has **six** memory channels
  [VERIFIED], so four DIMMs leave a third of the bandwidth unused and
  unbalance the interleave. Use six. Use VLP (very low profile, about
  18.75 mm) RDIMMs so they fit flat under the vault lid. [CONFIRM] VLP
  RDIMM availability at each capacity.
- The brief asked for a 65 to 90 W configurable 8224P. Its configurable TDP
  is 155 to 225 W [VERIFIED]. With a 155 W CPU the brief's 160 W system
  budget cannot hold. The power line above is the corrected budget
  (section 5). If a lower budget is essential, use the 16-core 8124P (125 W
  default) or the 8-core 8024P (90 W).
- The brief asked for an RTX 5000 Ada with 32 GB. The 32 GB part is the
  250 W dual-slot desktop card. The embedded MXM module has **16 GB**
  [VERIFIED]. The baseline uses a Jetson Thor module instead: more memory
  (64 GB), less power, and the same module family as the higher tiers.
- **ECC on the neural module.** The brief requires ECC on every memory bus.
  The MXM's GDDR6 ECC is real but ships disabled; enable it in firmware.
  [CONFIRM] with NVIDIA whether Jetson Thor's LPDDR5X supports ECC, and in
  which mode. If it does not, the reconciliation check of section 2
  (re-running sampled rows on the ECC-protected host) is the mitigation, and
  the customer must be told.

### 3.2 Sovereign (critical infrastructure and clearing node)

```
+-----------------------------------------------------------------------------+
| SOVEREIGN CARRIER POPULATION                                                |
+-----------------------------------------------------------------------------+
| ANALYTICAL (LEFT)   AMD EPYC Embedded 8434P "Siena"                [VERIFIED]|
|                     48 Zen 4c cores / 96 threads, cTDP 155 to 225 W        |
|                     Run at 155 W in the briefcase                  [CHANGED]|
| NEURAL (RIGHT)      Baseline: NVIDIA Jetson AGX Thor T5000              [VERIFIED]|
|                     128 GB LPDDR5X unified, 14-core Neoverse-V3AE,          |
|                     Blackwell GPU, 40 to 130 W                              |
|                     Alternative (the brief): Tenstorrent, see note [CHANGED]|
| CENTRE ARBITER      AMD Zynq UltraScale+ ZU7EV                     [CONFIRM]|
|                     4 x Cortex-A53, 2 x Cortex-R5F (lockstep), VCU          |
|                     PCIe Gen3 in the fabric; AES-GCM, SHA-3, RSA in the CSU |
|                     PQC (ML-DSA, ML-KEM) runs in ZXV software on the A53s   |
| SYSTEM MEMORY       384 GB = 6 x 64 GB DDR5-4800 ECC RDIMM, VLP     [CHANGED]|
| STORAGE             61.44 TB Micron 6550 ION, PCIe 5.0, E3.S 1T    [CONFIRM]|
|                     SED / FIPS SKU                                          |
| COMMS SLED          Vanguard set, plus:                                     |
|                     2 x SFP28 25/10 GbE (Intel E810-XXVDA2 class)          |
|                     Analog Devices AD9361 2x2 SDR, 70 MHz to 6 GHz         |
|                     (sub-GHz mesh is a waveform on this SDR)                |
| POWER               330 W sustained / 450 W peak; 600 W GaN supply  [CHANGED]|
+-----------------------------------------------------------------------------+
```

Notes:

- The brief's "Deterministic Lock-Step Clock Execution" is not a feature of
  the EPYC CPU. Determinism here comes from two places that do exist:
  ZXV's integer-only, phase-tick kernel (the same output bits on every
  CPU), and the arbiter's Cortex-R5F pair, which runs in hardware lockstep.
  Put safety-critical arbiter logic (tamper response, fan scram, kill
  switches) on the lockstep R5F pair.
- The brief's "PCIe Gen 5 crossbar" and "CXL 2.0 bus monitor" in the ZU7EV
  do not exist. The ZU7EV has PCIe Gen3 blocks and no CXL. Isolation is done
  by the host IOMMU and by the arbiter owning every device's reset and power
  enable. If an inline Gen5 switch is wanted for port-level control, add a
  Microchip Switchtec PFX Gen5 or Broadcom PEX89000-class switch, managed by
  the arbiter over its management port. [CONFIRM] the exact part.
- **Tenstorrent.** Two Wormhole n300 cards have 2 × 24 GB = **48 GB**, not
  96 GB, and draw up to 2 × 300 W [VERIFIED, aggregator data]. That alone is
  more than double the tier's power budget. One Blackhole p150 has 32 GB at
  up to 300 W. ZXV also has no Tenstorrent (TT-Metalium) backend in
  `vb_backend_t`, which today covers CUDA, Metal, Vulkan and ROCm. Offer
  Tenstorrent only as a special build with a 700 W supply, and budget the
  backend work.
- The brief's DapuStor R6060 is dropped as a second source for US-government
  buyers (PRC-origin vendor; section 8). Qualify a second Gen5 E3.S
  61.44 TB drive from a non-PRC vendor.
- The brief quotes 14.0 GB/s and 3.2 M IOPS. [CONFIRM] against the chosen
  drive's datasheet; do not print them until confirmed.

### 3.3 Citadel (sovereign compute and treasury vault)

The brief specifies two AMD Instinct MI300A APUs. **They cannot go in a
briefcase.** Each MI300A is rated at 550 W air-cooled and 760 W
liquid-cooled, in an SH5 server socket [VERIFIED, AMD datasheet]. Two of them
need 1.1 to 1.5 kW for the APUs alone, against the brief's 480 W sustained
budget for the whole machine. The brief's alternative, "Grace Blackwell",
is either GB200 (server rack) or GB10 (a 128 GB desktop part, about 140 W).

The Citadel below keeps the brief's intent (two coherent neural nodes with
256 GB of unified accelerator memory, a large ECC system memory and a
heavyweight arbiter) at a power a briefcase can reject.

```
+-----------------------------------------------------------------------------+
| CITADEL CARRIER POPULATION                                                  |
+-----------------------------------------------------------------------------+
| ANALYTICAL (LEFT)   AMD EPYC Embedded 8534P "Siena"                [VERIFIED]|
|                     64 Zen 4c cores / 128 threads, run at 155 to 200 W      |
| NEURAL (RIGHT)      2 x NVIDIA Jetson AGX Thor T5000               [CHANGED]|
|                     2 x 128 GB = 256 GB LPDDR5X unified                     |
|                     about 2 x 273 GB/s (NOT HBM; see note)                  |
|                     Node-to-node: 2 x 25 GbE RDMA, direct                    |
|                     Alternative: 2 x GB10 (DGX Spark class) linked by       |
|                     ConnectX-7, if a module form factor is offered [CONFIRM]|
| CENTRE ARBITER      AMD Versal Prime VM1802                        [CONFIRM]|
|                     2 x Cortex-A72, 2 x Cortex-R5F (lockstep)               |
|                     PCIe Gen4 (CPM4), 32G transceivers                      |
|                     Bus kill gasket = per-port power and reset control      |
|                     PQC coprocessor = ZXV ML-KEM / ML-DSA in programmable  |
|                     logic, verified against the KAT suite [R&D]             |
| SYSTEM MEMORY       1,152 GB = 12 x 96 GB DDR5 ECC RDIMM, 2 per channel     |
|                     Speed derates at 2 DIMMs/channel; confirm with AMD      |
|                     [CHANGED]                                               |
| STORAGE             2 x 122.88 TB Solidigm D5-P5336, U.2, mirrored          |
|                     = 122.88 TB usable, PCIe 4.0               [CHANGED]    |
|                     Mirroring in zxvfs (no opaque RAID firmware)            |
|                     EPYC Embedded DRAM-flush-to-NVMe on power loss [VERIFIED]|
| COMMS SLED          Sovereign set, plus:                                    |
|                     2 x SFP28 25 GbE optical                                |
|                     Iridium Certus 9770 L-band module, external antenna    |
|                     [VERIFIED: 22 kbps up / 88 kbps down, 3.5 W typ.]       |
|                     Triple-redundant kill: power FET, RF relay, arbiter     |
| POWER               500 W sustained / 650 W peak; 900 W GaN dual-rail       |
|                     supply                                      [CHANGED]   |
+-----------------------------------------------------------------------------+
```

Notes:

- **Memory bandwidth drops by an order of magnitude against the brief.**
  The brief's 10.6 TB/s came from HBM3, which only ships in server-power
  parts. The Thor pair gives about 0.55 TB/s in total. Inference speed on a
  large model is roughly proportional to memory bandwidth, so the Citadel
  will generate tokens far more slowly than an MI300A server. Sell it on
  capacity, isolation and portability, not on speed.
- **Model sizes, stated correctly.** 256 GB holds a 70B-parameter model at
  BF16 (about 140 GB) with room for its KV cache, or a 405B model at 4-bit
  (about 203 GB plus cache, split over the two nodes). A 405B model
  **unquantised** needs about 810 GB at BF16 and does not fit in any
  briefcase. The brief's "70B to 405B unquantized" must be changed in all
  sales material.
- **1 TB of RAM.** The brief routes 8 × 128 GB through a "Microchip FMC 2000
  CXL 3.0 hub". No such part was found. Microchip's real CXL product is the
  SMC 2000 family: CXL 1.1/2.0 Type-3 memory controllers with two DDR5
  channels each [VERIFIED]. Siena's six native channels at two DIMMs per
  channel reach 1,152 GB without CXL, with less latency and one less
  firmware blob. Keep the SMC 2000 as an expansion option if more than
  1.15 TB is ever needed.
- **E1.S does not hold 61.44 TB.** The D5-P5336 ships its top capacities in
  U.2 and E1.L; E3.S tops out lower [VERIFIED]. 122.88 TB is available in
  U.2 [VERIFIED, retail listings].
- **RAID 0 or RAID 1.** The brief says "RAID-0/1" and 122.88 TB. Two
  61.44 TB drives give 122.88 TB only in RAID 0, with no redundancy; a
  treasury vault should not run RAID 0. Two 122.88 TB drives mirrored give
  122.88 TB usable with redundancy.
- **Satellite link capacity.** Certus 9770 midband is 22 kbps up and 88 kbps
  down [VERIFIED]. That suits settlement messages, signed batches and
  heartbeats. It is not a bulk data link.

## 4. Comparative matrix (corrected)

| | Vanguard | Sovereign | Citadel |
|---|---|---|---|
| Role | Regulatory field audit | Grid / RTGS contingency node | Sovereign treasury core |
| Analytical CPU | EPYC Embedded 8224P, 24C | EPYC Embedded 8434P, 48C | EPYC Embedded 8534P, 64C |
| Neural | 1 × Thor T4000 (64 GB) or RTX 5000 Ada MXM (16 GB) | 1 × Thor T5000 (128 GB) | 2 × Thor T5000 (256 GB) |
| Neural memory bandwidth | ≈ 0.27 TB/s (Thor) or 0.58 TB/s (MXM) | ≈ 0.27 TB/s | ≈ 0.55 TB/s total |
| System RAM (ECC RDIMM) | 192 GB | 384 GB | 1,152 GB |
| Storage | 30.72 TB U.2 Gen4 | 61.44 TB E3.S Gen5 | 122.88 TB usable, mirrored U.2 |
| Arbiter | PolarFire SoC MPFS250TS | Zynq UltraScale+ ZU7EV | Versal Prime VM1802 |
| External network | 2 × 10GBASE-T | + 2 × SFP28 | + 2 × SFP28 optical |
| Radios | 5G, Wi-Fi 7 | + AD9361 SDR | + Iridium Certus 9770 |
| Sustained / peak power | 250 / 340 W | 330 / 450 W | 500 / 650 W |
| Supply | 400 W | 600 W | 900 W dual-rail |
| Crypto target | Same for all tiers, see section 6 | | |

## 5. Power budgets

Peak column = every subsystem at its rated maximum at once. Sustained =
what the `ct_budget` controller holds under a long mixed load.

| Subsystem | Vanguard peak | Sovereign peak | Citadel peak |
|---|---|---|---|
| CPU | 155 W | 155 W | 200 W |
| Neural | 70 W | 130 W | 260 W |
| DRAM | 30 W | 40 W | 70 W |
| Storage | 20 W | 25 W | 50 W |
| Arbiter (+ switch if fitted) | 10 W | 20 W | 45 W |
| NICs and radios | 15 W | 30 W | 35 W |
| Displays | 35 W | 35 W | 35 W |
| Audio, fans, micro-jets | 25 W | 30 W | 45 W |
| Projector (hatch open) | 0 W (load-shed) | 0 W (load-shed) | 0 W (load-shed) |
| **Total** | **≈ 360 W** | **≈ 465 W** | **≈ 740 W** |

With the projector on (up to 90 W), the arbiter sheds neural power by the
same amount. The Citadel's 740 W all-on peak is above its 650 W rating on
purpose: the CPU and neural caps keep it at 650 W, and the 900 W supply
leaves margin for charging the batteries at the same time.

## 6. Security architecture

### 6.1 Boot chain

1. The arbiter boots first, from its own secure boot (device-key-locked
   bitstream and firmware).
2. The arbiter holds the host in reset and owns the host's SPI flash. It
   verifies the UEFI image and the ZXV kernel image on that flash against
   the owner's release key, using ZXV's MATRIX dual signature (ML-DSA-87 +
   SLH-DSA-SHAKE-256s, `PQM_SIG_PURPOSE_RELEASE`).
   UEFI Secure Boot itself only knows RSA and ECDSA, so the post-quantum
   check has to be done by the arbiter, before the host runs.
3. The arbiter releases host reset. UEFI loads `BOOTX64.EFI` (ZXV).
4. ZXV attests to the arbiter over the mailbox before the arbiter releases
   storage keys (section 6.3).

### 6.2 Tamper detection

| Sensor | Detects |
|---|---|
| Vault pressure and humidity | Opening the nitrogen-backfilled vault |
| Light sensors in the vault | Opening the vault in daylight or under a lamp |
| Micro-switches on the vault lid and latches | Mechanical opening |
| Conductive tamper mesh in the vault lid | Drilling or cutting |
| Temperature and voltage monitors on the arbiter | Freezing and glitching attacks |
| Accelerometer | Case removed from a fixed mount (optional) |

The tamper circuit must work with the machine **switched off**. It needs its
own energy store, a coin cell or supercapacitor, sized to hold tamper
monitoring for at least 30 days unpowered and to complete one zeroisation.

### 6.3 Keys and zeroisation

- Drive data is encrypted by the SED's media key. The drive's
  authentication secret is wrapped by a key-encryption key (KEK) that lives
  only in the arbiter's battery-backed RAM (BBRAM).
- On tamper, the arbiter clears the KEK. Target: KEK cleared in under 1 ms
  from the tamper signal [R&D]. From that moment the drives cannot be
  unlocked. The drives' own cryptographic erase follows when power allows;
  it takes longer and is housekeeping, not the security event.
- The brief's "sub-millisecond zeroisation across the EDSFF drives" should
  be worded as above. A drive cannot erase itself in a millisecond, but its
  data can become unrecoverable in a millisecond.

### 6.4 FIPS 140-3

- FIPS 140-3 is a certificate issued by NIST's CMVP to a **specific module
  and firmware version**, after testing by an accredited lab. A hardware
  spec cannot claim a level. It can only target one. Lab testing plus the
  CMVP queue typically takes a year or more.
- **Recommendation: one arbiter family for all three tiers.** Three
  different arbiters (PolarFire, Zynq, Versal) mean three CMVP validations,
  three ZXV board ports and three tamper designs. PolarFire SoC is the
  natural single choice: it has a security variant, a long product life, a
  low power draw, and a RISC-V application core that ZXV's riscv64 port
  already targets. Use the larger MPFS460T for the Citadel. Keep Zynq or
  Versal only if the arbiter must sit inline in a multi-gigabit data path.
  This is the licensor's decision.
- Target Level 3 for all tiers. The brief's split (Level 2 for Vanguard,
  Level 3 for Sovereign, CNSA 2.0 for Citadel) would also mean separate
  certificates. A central bank examiner should not get a weaker module than
  a vault.

### 6.5 CNSA 2.0 profile

| CNSA 2.0 requires | ZXV today | Gap |
|---|---|---|
| ML-KEM-1024 | Yes (`pq_mlkem1024.c`, HIGH level) | Run without X25519 in a CNSA profile |
| ML-DSA-87 | Yes (`pq_mldsa87.c`) | None |
| AES-256 | Yes, but table-driven (`robin_debanks/aes256_gcm.h`); TLS bulk uses ChaCha20-Poly1305 | A constant-time AES-256-GCM (AES-NI/VAES on Siena, or arbiter hardware) is needed. Table AES leaks through cache timing |
| SHA-384 / SHA-512 | [CONFIRM] in the ZXV tree | Add if absent |
| LMS or XMSS for firmware signing | Not present | Add LMS for arbiter and UEFI firmware signing |

MATRIX-level extras (HQC-5, SLH-DSA, X25519) are outside the CNSA 2.0 list.
They can stay as additional layers in a non-CNSA profile.

## 7. Firmware and lifecycle claims (corrected)

- **"No consumer BIOS, coreboot everywhere."** No public coreboot or
  openSIL port for EPYC 8004 was found. Specify a commercial UEFI from an
  independent BIOS vendor with **source-code escrow**, measured and verified
  by the arbiter before every boot (section 6.1). Revisit open firmware when
  AMD's openSIL covers this socket.
- **"7 to 10 year roadmap."** AMD states seven years of lifecycle support
  for EPYC Embedded 8004 [VERIFIED, press coverage]. FPGA lines from
  Microchip and AMD typically run longer. [CONFIRM] Jetson Thor industrial
  lifecycle with NVIDIA. Quote seven years for the platform.
- **"Zero telemetry."** True of ZXV. Not automatically true of vendor
  firmware on the NIC, drives, Thor modules and cellular module. The OEM
  must list every component with its own firmware, and the customer's
  firmware review must cover each one.

## 8. Supply chain and export

- **US government buyers (and many allied ones):** build in the US or a
  TAA-designated country. Avoid PRC-origin modules, including the brief's
  Quectel cellular module and DapuStor drive. Use a cellular module from a
  non-PRC vendor (Telit Cinterion or Sierra Wireless class) [CONFIRM
  current models]. Check every part against NDAA Section 889 lists.
- **Export:** strong encryption (EAR Category 5 Part 2), the AD9361 SDR and
  the satellite terminal each need an export classification. Get a formal
  classification (CCATS) before the first foreign sale. If any version is
  designed for a military end use, ITAR may apply instead.
- **Radios and spectrum:** a sub-GHz "tactical P2P" waveform needs a licence
  to transmit in each country. Ship the SDR with transmit disabled until a
  per-country configuration is loaded.

## 9. Port trench by tier

| Port | Vanguard | Sovereign | Citadel |
|---|---|---|---|
| 10GBASE-T RJ45 | 2 | 2 | 2 |
| SFP28 (25/10 GbE) | – | 2 | 2 (+2 on the comms sled) |
| USB4 / Thunderbolt-compatible | 2 | 2 | 2 |
| HDMI 2.1 out (from the arbiter display pipeline) | 1 | 1 | 1 |
| RS-232 console (ZXV serial log, 38400 baud) | 1 | 1 | 1 |
| Smart card and FIDO token slot | 1 | 1 | 2 |
| SDR SMA | – | 2 (2×2) | 2 |
| Satellite antenna (TNC) | – | – | 1 |
| Hardware kill switches (radios, network, mics, camera) | 4 | 4 | 4, triple-redundant |
| Manual fan override | 1 | 1 | 1 |

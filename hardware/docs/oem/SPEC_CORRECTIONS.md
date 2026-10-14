<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Corrections to the original brief

Each row is a claim in the original hardware brief that the OEM packages
change. "Source" names what was checked, in October 2026. Aggregator and
press sources are marked as such. The OEM should confirm every row against
the vendor's current datasheet.

## Institutional

| # | Brief said | Finding | Resolution | Source |
|---|---|---|---|---|
| I1 | EPYC 8224P "Embedded", 65 to 90 W configurable | 8224P is in the EPYC Embedded 8004 line, but its cTDP is 155 to 225 W (default 160 W) | Run at 155 W; budget corrected. 8124P or 8024P if a lower budget is essential | AMD product page; press coverage of EPYC Embedded 8004 |
| I2 | SP6 "BGA" socket | SP6 is a socketed (LGA) part | Specify socket SP6 | AMD |
| I3 | 4 × RDIMM, "4-channel controller, 153.6 GB/s" | Siena has 6 DDR5-4800 channels, 230.4 GB/s | 6 DIMMs (192 GB Vanguard, 384 GB Sovereign) | AMD product page |
| I4 | RTX 5000 Ada embedded with 32 GB | The embedded MXM module has 16 GB GDDR6 (ECC off by default), 115 W TGP. 32 GB is the desktop card | Thor T4000 baseline; MXM 16 GB as the alternative | PNY, ADLINK, Advantech MXM datasheets |
| I5 | Vanguard 160 W sustained / 210 W peak, 200 W PSU | CPU alone is 155 W minimum | 250 / 340 W, 400 W supply | Arithmetic from I1 and I4 |
| I6 | 8434P "sub-120 W" | Default 200 W, cTDP 155 to 225 W | Run at 155 W | AMD / reseller listings |
| I7 | "Deterministic lock-step clock execution" on EPYC | Not an EPYC feature | Determinism from ZXV's integer kernel; lockstep on the arbiter's Cortex-R5F pair | – |
| I8 | Dual Tenstorrent Wormhole n300, 96 GB | 2 × 24 GB = 48 GB; up to 300 W each. No ZXV backend | Thor T5000 (128 GB) baseline; Tenstorrent as a special build only | Aggregator spec sheets (flopper.io) citing Tenstorrent |
| I9 | ZU7EV with a PCIe Gen 5 crossbar and CXL 2.0 monitor | ZU7EV has PCIe Gen3 blocks and no CXL | IOMMU plus arbiter-owned resets and power; optional Gen5 switch | AMD Zynq UltraScale+ product tables |
| I10 | "Dilithium" | Standardised as ML-DSA (FIPS 204) | Use the FIPS names; ZXV already implements ML-DSA-65/87 | NIST FIPS 204 |
| I11 | 61.44 TB in E1.S | E1.S does not reach 61.44 TB | E3.S (Micron 6550 ION) for Sovereign; U.2 for Citadel | Solidigm D5 product page; launch coverage |
| I12 | DapuStor and Quectel parts | PRC-origin; a problem for US-government and many allied buyers | Non-PRC second sources | – |
| I13 | Dual MI300A in a briefcase, 480 W | Each MI300A is 550 W (air) to 760 W (liquid), SH5 server socket | 2 × Jetson AGX Thor T5000, 256 GB total | AMD MI300A data sheet |
| I14 | 10.6 TB/s aggregate HBM3 | Only with MI300A-class parts | About 0.55 TB/s with Thor; stated in the sales notes | Arithmetic |
| I15 | "70B to 405B unquantized" | 405B at BF16 is about 810 GB | 70B at BF16, or 405B at 4-bit | Arithmetic |
| I16 | 1 TB via "Microchip FMC 2000 CXL 3.0 hub", "12-channel" | No such part found. Microchip's CXL parts are the SMC 2000 (CXL 1.1/2.0 Type-3 memory controllers). Siena has 6 channels | 12 RDIMMs at 2 per channel = 1,152 GB; SMC 2000 as an option | Microchip SMC 2000 product pages |
| I17 | 122.88 TB from 2 × 61.44 TB, "RAID-0/1" | RAID 1 would give 61.44 TB | 2 × 122.88 TB U.2, mirrored | Solidigm; retail listings |
| I18 | 620 W peak, "sub-ambient active TEC" | TECs add 0.5 to 1 W of heat per watt pumped | No TECs in the main heat path; 500 / 650 W with fans | Physics (Peltier COP) |
| I19 | Different FIPS levels per tier, three arbiter families | Each arbiter means its own CMVP validation | One arbiter family and one Level 3 target recommended | NIST CMVP process |
| I20 | "coreboot" on every chip | No public coreboot/openSIL port for EPYC 8004 found | Commercial UEFI with source escrow, verified by the arbiter | – |
| I21 | 7 to 10 year platform life | AMD states 7 years for EPYC Embedded 8004 | Quote 7 years | Press coverage of the AMD announcement |
| I22 | Broadcom BCM89883 "automotive 10GbE" on RJ45 | Could not verify this part as 10GBASE-T. Automotive multi-gig PHYs use single-pair links that do not mate with RJ45 | Intel X710-T2L-class 10GBASE-T | – |
| I23 | Sub-millisecond zeroisation "across the drives" | A drive cannot erase itself in 1 ms | Clear the arbiter-held KEK in < 1 ms; drive erase follows | – |

## Shared platform

| # | Brief said | Finding | Resolution |
|---|---|---|---|
| P1 | 15° rake from a 35 to 42 mm rise | 40 mm over a 340 mm base is about 6.7° | 40 mm, 6° to 7° (ergonomically better) |
| P2 | Wings fold "flush" against the centre panel | Two 16" wings are wider than one 21.5" panel | Wings stack; left- and right-hand hinge brackets |
| P3 | 1,200-lumen 4K UST laser behind the canopy | 100 to 150 W and too deep | 1080p short-throw, 500 to 800 lumens, ≤ 90 W |
| P4 | Projector throws "behind the operator" | The hatch faces away from the user | It throws onto the wall behind the machine |
| P5 | 18 to 24 dB spill cancellation at 1.5 m; 10 to 14 dB open-air ANC | Open-air cancellation only works in small zones | 6 to 10 dB targets, marked R&D |
| P6 | Ultrasonic heterodyne array | Exposure limits, weak low-mid, high power | Conventional phased array in version 1 |
| P7 | 48 solid-state modules carry full load; fans only for emergencies | About 10 W per AirJet Pro | Fans are the main high-load cooling; micro-jets give a 0 rpm quiet mode |
| P8 | 40 × 10 mm "micro-turbines" | Far too little airflow (Citadel needs about 70 CFM) | 75 to 97 mm blowers |
| P9 | Hermetic nitrogen-purged vault | Flight pressure changes load a sealed vault | Nitrogen backfill + desiccant + ePTFE pressure vent |
| P10 | Activity LEDs show compute in real time | Optical side channel | Arbiter-driven, aggregated, ≤ 2 Hz; off on institutional models |
| P11 | Al-Li / Nomex shell as a Faraday cage | Honeycomb is not conductive | Conductive skin required |
| P12 | Battery as a field power source | 197 Wh lasts about 20 min on the Citadel | Stated as ride-through |

## Consumer

| # | Brief said | Finding | Resolution |
|---|---|---|---|
| C1 | LPDDR5X-8533, ≈ 273 GB/s | Strix Halo is rated for LPDDR5X-8000, ≈ 256 GB/s | 8000, 256 GB/s |
| C2 | 96 GB or 128 GB, up to 96 GB as VRAM | 96 GB of VRAM needs the 128 GB model | 128 GB only for that claim |
| C3 | BOM total "~$1,340" | The listed lines sum to $1,540, and several items are missing | Revised BOM $1,785 to $2,660 |
| C4 | Retail $2,199 to $2,399 | Not supportable at that BOM, and the 18 % licence royalty is not included | SKU ladder; royalty decision for the licensor |
| C5 | 8 micro-jets cool the 120 W APU, < 28 dBA flat out | 8 × 5 to 10 W | Blowers primary; noise targets per mode |
| C6 | $5 eDP bridge for the 21.5" panel | Monitor panels are often LVDS or V-by-One | Native eDP all-in-one glass |
| C7 | "Rivals an RTX 4070 mobile" | Reviews put it in that class, but it depends on power limit and workload | Do not use as a spec line; quote measured benchmarks later |

## Sources checked

- AMD EPYC 8224P product page: https://www.amd.com/en/products/processors/server/epyc/4th-generation-9004-and-8004-series/amd-epyc-8224p.html
- EPYC 8434P specifications: https://techpowerup.com/cpu-specs/epyc-8434p.c3290
- AMD EPYC Embedded 8004 announcement coverage: https://newelectronics.co.uk/content/news/amd-releases-energy-efficient-epyc-embedded-8004-series
- AMD Instinct MI300A data sheet: https://www.amd.com/content/dam/amd/en/documents/instinct-tech-docs/data-sheets/amd-instinct-mi300a-data-sheet.pdf
- PNY Ada MXM modules datasheet: https://www.pny.com/en-eu/file%20library/professional/datasheet/workstation/acmk0923_pny_ada_mxm_modules.pdf
- Tenstorrent Wormhole n300 / Blackhole p150a (aggregator): https://flopper.io/compare/tenstorrent-wormhole-n300s-vs-tenstorrent-blackhole-p150a/spec-sheet
- Jetson AGX Thor T5000 (reseller listing): https://www.waveshare.com/product/ai/jetson-thor-t5000.htm
- NVIDIA DGX Spark hardware overview: https://docs.nvidia.com/dgx/dgx-spark/hardware.html
- Solidigm D5 series: https://solidigm.com/products/data-center/d5.html
- Microchip SMC 2000: https://www.mouser.hk/new/microchip/microchip-smc-2000
- Iridium Certus 9770: https://www.iridium.com/products/iridium-certus-9770/
- Ryzen AI Max+ 395 analysis: https://www.notebookcheck.net/AMD-Ryzen-AI-Max-395-Analysis-Strix-Halo-to-rival-Apple-M4-Pro-Max-with-16-Zen-5-cores-and-iGPU-on-par-with-RTX-4070-Laptop.963274.0.html
- Frore AirJet figures (press): https://www.pcworld.com/article/2203172/frore-system-airjet-how-the-pc-fan-of-the-future-works.html and https://www.cnx-software.com/2023/01/20/quiet-ultrathin-airjet-solid-state-active-cooling-could-replace-fans/

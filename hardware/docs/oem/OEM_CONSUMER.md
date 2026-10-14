<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Consumer OEM package: Pioneer

For: a high-volume notebook or all-in-one ODM in Taiwan or Shenzhen, with
AMD Strix Halo reference-design experience.

Read with [PLATFORM_COMMON.md](PLATFORM_COMMON.md) and
[OS_HARDWARE_INTERFACE.md](OS_HARDWARE_INTERFACE.md). Tags are defined in
[README.md](README.md).

## 1. Product intent

The Pioneer keeps everything that makes the machine recognisable: the
folding case, the kickstand plenum, the pop-up keyboard and port trench, the
three-panel canopy, the window, and the tactile acoustic base. It drops the
institutional parts: the server CPU, the FPGA arbiter, enterprise flash,
the sealed nitrogen vault and the rugged certifications.

## 2. Specification

```
+=============================================================================+
| PIONEER                                                                     |
+=============================================================================+
| APU               AMD Ryzen AI Max+ 395 ("Strix Halo")             [VERIFIED]|
|                   16 Zen 5 cores / 32 threads, up to 5.1 GHz                |
|                   Radeon 8060S, 40 RDNA 3.5 compute units                   |
|                   XDNA 2 NPU, 50 TOPS class                                 |
|                   Configurable 45 to 120 W; Pioneer runs 55 W quiet,        |
|                   85 W balanced, 120 W performance                          |
| MEMORY            128 GB LPDDR5X-8000, 256-bit, soldered          [CHANGED]|
|                   ≈ 256 GB/s peak                                 [VERIFIED]|
|                   Up to 96 GB assignable to the GPU (Windows VGM);          |
|                   ZXV manages the pool itself (OS_HARDWARE_INTERFACE.md)    |
| STORAGE           2 x M.2 2280 PCIe 4.0 x4 slots, user-accessible           |
|                   Ships 1 x 4 TB TLC; second slot empty or 4 TB (8 TB SKU)  |
| CANOPY            Centre: 21.5" 1920 x 1080 144 Hz IPS, eDP                  |
|                   Wings: 2 x 15.6" 1920 x 1080 60 Hz IPS, eDP               |
|                   + HDMI 2.1 out = 4 displays, the APU's maximum  [CONFIRM]|
| COOLING           Vapour chamber, 2 x 75 mm blowers (primary)     [CHANGED]|
|                   8 x Frore AirJet Mini/Pro for the 0 rpm quiet mode        |
| ACOUSTICS         Flax-phenolic dual chamber + tactile exciter (15 W)       |
|                   8-driver front array; DSP noise features ship as beta    |
| CONNECTIVITY      Wi-Fi 7 / BT 5.4, M.2 2230 (MediaTek or Intel)            |
|                   Optional 5G M.2 3042/3052 WWAN slot                       |
|                   Trench: 2 x USB4 40 Gbps, 2 x USB-A 10 Gbps,              |
|                   1 x 2.5 GbE RJ45, HDMI 2.1 out, 3.5 mm audio, SD card     |
| POWER             1 x 98.5 Wh hot-swap pack (second bay optional)            |
|                   Internal 230 W GaN supply; USB-C PD 3.1 240 W input       |
| PROJECTOR         Hatch and connector fitted; engine is an accessory module |
| MASS / SIZE       9 to 11 kg; ≤ 560 x 360 x 230 mm closed       [R&D]      |
+=============================================================================+
```

### 2.1 Changes from the brief

- **LPDDR5X-8533 → LPDDR5X-8000.** Strix Halo's memory controller is rated
  for LPDDR5X-8000 [VERIFIED]. That gives about 256 GB/s, not 273 GB/s.
  (273 GB/s is the figure for NVIDIA's GB10, which runs its 256-bit bus
  faster.)
- **96 GB of VRAM needs the 128 GB configuration.** On a 96 GB machine the
  GPU can be given far less than 96 GB, because the CPU needs memory too.
  The brief's "96 GB or 128 GB, up to 96 GB as VRAM" should read "128 GB,
  up to 96 GB as VRAM". A 96 GB model is not offered (section 6).
- **Memory is soldered.** Strix Halo does not support socketed memory at
  this speed. Say so on the product page; this is a common return reason.
- **Eight micro-jets cannot cool 120 W.** At about 5 to 10 W each
  [VERIFIED, press figures], eight modules handle about 40 to 80 W in the
  best case. Blowers are the primary cooling. The micro-jets give a
  fan-off quiet mode at light load (section 4).
- **Panel interface.** The brief prices a $5 bridge board. Many 21.5" panels
  made for monitors use LVDS or V-by-One, and a bridge from eDP costs more
  than $5 once the T-con is included. Source all-in-one-PC 21.5" glass with
  a native eDP input. [CONFIRM] with two panel vendors.

### 2.2 What ZXV can use today

The Pioneer is the best first physical target for ZXV: it is an ordinary
x86-64 UEFI PC, which is exactly the boot path ZXV's x86_64 disc already
takes ([docs/BAREMETAL.md](../../../docs/BAREMETAL.md)). The Framework Desktop
mainboard uses the same Ryzen AI Max+ 395 with 128 GB, and is the
recommended development board until Pioneer prototypes exist
([OS_HARDWARE_INTERFACE.md](OS_HARDWARE_INTERFACE.md) section 7).

## 3. Board and I/O map

The APU exposes 16 PCIe 4.0 lanes, two USB4 ports and four display
pipes [CONFIRM lane counts against AMD's platform guide]. A plan that fits:

| Consumer | Link |
|---|---|
| M.2 SSD slot 1 | PCIe 4.0 x4 |
| M.2 SSD slot 2 | PCIe 4.0 x4 |
| Wi-Fi 7 M.2 2230 | PCIe x1 |
| WWAN M.2 (optional) | PCIe x1 or USB 3 |
| 2.5 GbE controller | PCIe x1 |
| Embedded controller, sensors, battery | eSPI / I2C / SMBus |
| Keyboard | Internal USB 2.0 |
| Centre panel | eDP |
| Wing panels | DisplayPort via the USB4/DP alt-mode pipes, converted to eDP on the canopy, or eDP if the APU exposes a second eDP [CONFIRM] |
| HDMI out | Native HDMI 2.1 |
| Audio | HDA or SoundWire codec → DSP → class-D amplifiers |

**Embedded controller:** use an open, documented EC firmware (Chromium EC
class), so that ZXV can drive fans, battery and lid sensors without a
closed ACPI blob. This matters for ZXV more than for Windows.

## 4. Thermal modes

| Mode | APU power | Fans | Noise target [R&D] |
|---|---|---|---|
| Quiet | ≤ 45 W | 0 rpm, micro-jets only | ≤ 25 dBA at 0.5 m |
| Balanced | ≤ 85 W | Blowers on a curve | ≤ 32 dBA |
| Performance | ≤ 120 W | Blowers high | ≤ 40 dBA |
| Scram | Die > 95 °C, or a micro-jet fault | Blowers maximum | – |

The brief's "< 28 dBA while maxing 16 cores" is not a safe promise. Use the
targets above until the prototype is measured.

## 5. Mechanical cost-down choices

| Item | Institutional | Pioneer |
|---|---|---|
| Frame | Al-Li, Nomex honeycomb | Die-cast aluminium base |
| Lid | Al-Li composite | ABS / glass-fibre |
| Wing hinges | Nitronic 60, 50,000 cycles | 301 stainless friction pack, 20,000 cycles |
| Window | Sapphire + micro-mesh | Strengthened glass + ITO |
| Vault | IP67, nitrogen, tamper sensors | Dust-sealed, IP5X, no tamper |
| Batteries | 2 packs fitted | 1 pack fitted, second optional |
| Projector | Fitted | Accessory module |

## 6. Bill of materials and price

### 6.1 The brief's BOM does not add up

The brief's BOM table lists ten lines and gives a total of "~$1,340". The
ten lines add up to **$1,540**:

220 + 95 + 145 + 420 + 160 + 190 + 45 + 75 + 110 + 80 = 1,540.

It also leaves out the mainboard, the keyboard, the Wi-Fi card, cables,
packaging and warranty, and it prices 96 GB of memory, which section 2.1
explains is the wrong configuration.

### 6.2 Revised BOM (estimate, before supplier quotes)

DRAM and NAND contract prices rose sharply from late 2025. The memory and
storage lines below are wide on purpose. **Every line needs an RFQ.**

| Item | Low | High | Notes |
|---|---|---|---|
| Chassis and shell | $180 | $240 | Die-cast base, ABS lid, window |
| Hinges, kickstand linkage, keyboard lift | $90 | $130 | Three custom mechanisms |
| Keyboard (mechanical, low profile) | $25 | $40 | Missing from the brief |
| Canopy panels (21.5" + 2 × 15.6") | $150 | $190 | eDP glass, no bridge |
| APU, Ryzen AI Max+ 395 | $450 | $650 | Quote from AMD or a distributor |
| Memory, 128 GB LPDDR5X-8000 | $300 | $550 | Most volatile line |
| Mainboard PCB, VRMs, EC, passives | $90 | $140 | Missing from the brief |
| Storage, 1 × 4 TB TLC NVMe | $200 | $280 | |
| Acoustics (chambers, exciter, array, DSP, amps) | $45 | $70 | |
| Cooling (vapour chamber, 2 blowers, 8 micro-jets) | $60 | $90 | |
| Battery pack and GaN supply | $90 | $120 | |
| Wi-Fi 7 card, antennas | $15 | $25 | |
| Cables, flex, fasteners, packaging | $30 | $45 | Missing from the brief |
| Assembly and test | $60 | $90 | |
| **Total BOM** | **$1,785** | **$2,660** | |

### 6.3 What retail price this supports

A consumer hardware company typically needs the BOM to be no more than about
40 to 50 % of the retail price, to pay for channel margin, freight, duty,
warranty, returns, payment fees and R&D. The licence royalty comes on top:
**18 % of gross revenue** under the ZXV Hardware License, unless the
licensor sets different terms for its own OEM.

| | Brief | With standard royalty | With royalty waived for the OEM |
|---|---|---|---|
| BOM | $1,540 (brief's own lines) | $1,785 to $2,660 | $1,785 to $2,660 |
| Retail price that supports it (BOM ≤ 32 % of retail with royalty, ≤ 50 % without) | – | ≈ $5,600 to $8,300 | ≈ $3,600 to $5,300 |
| $2,199 to $2,399 possible? | Not at a profit | No | No |

### 6.4 SKU plan that can reach the brief's price band

| SKU | Silicon and memory | Storage | Retail target | Note |
|---|---|---|---|---|
| Pioneer 64 | Ryzen AI Max 385 class (8 cores, 32 CUs), 64 GB | 2 TB | $2,499 to $2,999 | Loses the "96 GB VRAM" headline; keeps the case and canopy. Works only with quotes near the low end of section 6.2 and the royalty waived |
| Pioneer 128 | Ryzen AI Max+ 395, 128 GB | 4 TB | $3,999 to $4,499 | The brief's machine. Same conditions as Pioneer 64 |
| Pioneer 128 Pro | As above + second 4 TB SSD + second battery + projector module | 8 TB | $4,999+ | |

Ways to bring the price down further, in order of effect:
1. Decide the royalty term for the OEM (section 6.3).
2. Cut a mechanism. The kickstand linkage, the keyboard lift and the
   projector hatch each add tooling cost and assembly time. A manual
   kickstand alone saves an estimated $20 to $30 a unit.
3. Build in larger volume. One-time tooling and engineering (NRE) for three
   custom mechanisms, a three-panel canopy and an acoustic base is likely
   to be $0.5 M to $1.5 M. Spread over 5,000 units that is $100 to $300 a
   unit; over 50,000 it is $10 to $30.

### 6.5 The pitch, corrected

- "96 GB of unified memory usable by the GPU": true for the 128 GB SKU.
- "Runs quantised 70B models locally": true. A 70B model at 4-bit is about
  40 GB and fits. Expect a few tokens per second at 256 GB/s.
- "Unquantised 32B models": true. 32B at BF16 is about 64 GB.
- "No direct competitor at $2,199": not supportable at that price; see
  section 6.3.
- "The full Citadel experience": the Pioneer shares the case, the canopy
  and the sound. It does not share the Citadel's security model. Do not
  imply it does.

## 7. Certifications (consumer)

FCC Part 15B and 15C/E, CE (RED, EMC, LVD), UKCA, IEC 62368-1 through the CB
scheme, UN 38.3, IEC 62133-2, RoHS, REACH and WEEE. The projector module
adds IEC 60825-1 and IEC 62471. The optional WWAN module needs PTCRB and
carrier certification.

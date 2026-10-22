<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Signet, Folio, Courier

Read with [MOBILE_COMMON.md](MOBILE_COMMON.md) (architecture, mesh,
approvals), [../HARDENING.md](../HARDENING.md) and [../PORTS.md](../PORTS.md).
Every part line is [CONFIRM] ([README.md](README.md)). Each model comes in
a **civilian** build and an **institutional** build; the institutional
build adds the security-variant arbiter with tamper zeroisation, a
sapphire window, FIPS 140-3 drive and secure-element SKUs, and the
MIL-DTL-38999 option where its ports allow.

## 1. Signet (smartphone)

```
+-----------------------------------------------------------------------------+
| SIGNET                                     172 x 82 x 13.5 mm, <= 320 g     |
+-----------------------------------------------------------------------------+
| PROCESSOR       Rockchip RK3588S: 4 x Cortex-A76 + 4 x Cortex-A55, 8 nm,   |
|                 Mali-G610, 6 TOPS NPU                              [CONFIRM]|
|                 held at 5 W sustained by the arbiter's power controller    |
| MEMORY          16 GB LPDDR5                                               |
| STORAGE         1 TB NVMe M.2 2230 (PCIe) + TF (microSD) in the SIM tray   |
| DISPLAY         6.7-inch LTPO OLED, 1 to 120 Hz, 2,600 nits peak          |
|                 (Samsung Display M14 class)                        [CONFIRM]|
|                 with the 0.10 mm honeycomb shield mesh (HARDENING 6.3)     |
| SEAL            1.0-inch memory-in-pixel seal display below the main       |
|                 screen + seal button on the right edge, arbiter-only       |
| CAMERAS         main + ultra-wide rear, front; MIPI CSI-2 sensors chosen   |
|                 at design freeze, on the RK3588S ISP               [CONFIRM]|
| RADIOS          5G (RM520N-GL), satellite IoT-NTN (BG95-S5), Wi-Fi 7 + BT  |
|                 (MT7925), BT 6.0 channel sounding (nRF54L), UWB (DW3000),  |
|                 LoRa + L/S-band (LR1121), L1 + L5 GNSS             [CONFIRM]|
| SECURITY        arbiter (PolarFire SoC), NXP SE051, 4 kill switches        |
| BATTERY         7,000 mAh silicon-carbon (about 27 Wh) + cell heater        |
|                 USB-C PD 65 W, Qi2 15 W                                    |
| PORTS           2 x USB-C, 3.5 mm TRRS, TF; full array on the sled         |
| BODY            titanium frame; clear back: forest-night honeycomb, the     |
|                 backlit ILLUMAHEART emblem (22 mm) at its centre           |
+-----------------------------------------------------------------------------+
```

Power budget:

| Block | Typical | Peak |
|---|---|---|
| Display (OLED, 50 % brightness / maximum outdoor) | 1.0 W | 4.0 W |
| Processor and memory | 1.5 W | 5.0 W (capped) |
| 5G modem (active transfer) | 1.0 W | 3.0 W |
| Wi-Fi 7 + Bluetooth | 0.3 W | 1.5 W |
| NVMe | 0.1 W | 2.5 W |
| Arbiter, secure element, seal display, sensors | 0.2 W | 0.4 W |
| Audio, cameras, LoRa, UWB, GNSS | 0.2 W | 3.0 W |
| **Device** | **≈ 0.8 W mixed use** | **6 W sustained limit** |

At 0.8 W mixed use the 27 Wh battery lasts about 30 hours; target: two
typical days, one heavy day [R&D: measure]. The 6 W sustained limit is set
by what a sealed titanium-framed phone can shed to still air without a
fan [R&D: thermal prototype]; the arbiter holds it.

Notes:

- **Why the RK3588S in a phone.** It is a tablet and single-board
  processor, larger and hungrier than a phone chip. It is used because ZXV
  can own its boot chain and because its drivers can be written from
  public, upstreamed sources [CONFIRM]. With the arbiter capping it at
  5 W it fits a rugged phone's thermal budget, and the Signet's heavy
  work goes to the owner's home devices (MOBILE_COMMON section 3).
- **NVMe, not UFS.** Phones use UFS storage; the Signet uses a 2230 NVMe
  SSD because ZXV already has a working NVMe driver.
- **The 13.5 mm thickness** carries the 7,000 mAh cell, the M.2 modem,
  the 2230 SSD, the honeycomb window and 1.8 m drop protection. A thinner
  Signet would give up battery or drop rating.

## 2. Folio (tablet)

```
+-----------------------------------------------------------------------------+
| FOLIO                                      262 x 185 x 13 mm, <= 850 g      |
+-----------------------------------------------------------------------------+
| PROCESSOR       Rockchip RK3588: 4 x A76 + 4 x A55, 6 TOPS NPU     [CONFIRM]|
|                 10 W sustained                                             |
| MEMORY          32 GB LPDDR5                                               |
| STORAGE         2 TB NVMe M.2 2230 + SD (UHS-II) + TF                      |
| DISPLAY         11-inch tandem OLED (LG Display class), 1,000 nits SDR /   |
|                 1,600 nits HDR                                     [CONFIRM]|
|                 Option: 11-inch transflective LCD for full-sun field work  |
|                 Active pen (USI 2.0 class)                         [CONFIRM]|
| SEAL            seal display and seal button in the bezel (held allowed)   |
| RADIOS          as Signet                                                  |
| SECURITY        as Signet                                                  |
| BATTERY         45 Wh silicon-carbon + cell heater; USB-C PD 65 W, Qi2     |
| PORTS           2 x USB-C (USB4, USB 3.2), USB-A, HDMI 2.1, SD, TF,        |
|                 3.5 mm; full array on the keyboard dock (PORTS section 4)   |
| BODY            titanium frame; clear back, forest-night honeycomb, backlit |
|                 emblem (34 mm)                                             |
+-----------------------------------------------------------------------------+
```

| Block | Typical | Peak |
|---|---|---|
| Display | 2.0 W | 6.0 W |
| Processor and memory | 2.5 W | 10 W (capped) |
| Radios | 0.8 W | 4.0 W |
| NVMe, arbiter, audio, sensors | 0.5 W | 4.0 W |
| **Device** | **≈ 3 W mixed use** | **12 W sustained limit** |

About 15 hours at 3 W mixed use [R&D]. The keyboard dock's 50 Wh battery
roughly doubles that.

## 3. Courier (laptop)

```
+-----------------------------------------------------------------------------+
| COURIER                                    330 x 240 x 28 mm, <= 2.4 kg     |
+-----------------------------------------------------------------------------+
| MAINBOARD       swappable module, one chassis, three boards:               |
|   OPEN (default) Rockchip RK3588, 32 GB LPDDR5, 6 TOPS             [CONFIRM]|
|                 ZXV bare metal; 15 W system sustained                      |
|   RISC-V        SpacemiT K3: 8 x X100 (RVA23) + 8 AI cores, up to 32 GB    |
|                 LPDDR5, up to 60 TOPS (vendor claim)        [CONFIRM][R&D] |
|                 ZXV riscv64 port; board as in the DC-ROMA Framework 13     |
|                 mainboard (shipping 2026)                                  |
|   PERFORMANCE   Qualcomm Snapdragon X2 Elite: 18 cores, 80 TOPS NPU,       |
|                 up to 128 GB LPDDR5X (152 GB/s)                    [CONFIRM]|
|                 Qualcomm-signed boot chain: ZXV runs hosted (zxvd on       |
|                 Linux) until a bare-metal port; the arbiter keeps the keys |
| STORAGE         2 x NVMe M.2 2280, up to 2 x 8 TB; SD + TF                 |
| DISPLAY         14-inch OLED, 2.8K, 120 Hz (tandem OLED preferred) [CONFIRM]|
|                 with the 0.10 mm honeycomb shield mesh                     |
| SEAL            seal display above the keyboard + seal button              |
| KEYBOARD        sealed, backlit, 1.5 mm travel; sealed glass trackpad      |
| RADIOS          as Signet, plus 2 x SMA for external LoRa / GNSS antennas  |
| SECURITY        arbiter, SE051, smart card slot, 4 kill switches           |
| BATTERY         2 x 49.5 Wh hot-swappable packs (99 Wh), each with a cell  |
|                 heater; swap one while running on the other                |
| CHARGING        USB-C PD up to 240 W (EPR), or 9 to 60 V DC wide input     |
| PORTS           the full array (PORTS section 3): USB-C x 3, USB-A x 3,     |
|                 HDMI 2.1, RJ45, SFP+, SD, TF, 3.5 mm, 6.35 mm, XLR combo,   |
|                 RS-232, RS-485 / CAN, SMA x 2, smart card, DC in           |
| BODY            magnesium chassis; clear lid with forest-night honeycomb   |
|                 and the backlit emblem (48 mm)                             |
+-----------------------------------------------------------------------------+
```

| Block | Open board, typical / peak | Performance board, typical / peak |
|---|---|---|
| Processor and memory | 3 W / 10 W | 8 W / 45 W |
| Display | 3 W / 8 W | 3 W / 8 W |
| Storage (2 NVMe) | 0.5 W / 8 W | 0.5 W / 8 W |
| Radios | 1 W / 5 W | 1 W / 5 W |
| Ports (USB-A and USB-C power out) | 0 / 25 W | 0 / 25 W |
| Arbiter, keyboard, audio, seal | 0.5 W / 2 W | 0.5 W / 2 W |
| **System** | **≈ 8 W / 15 W sustained** | **≈ 13 W / 45 W sustained** |

About 12 hours at 8 W on the open board, 7 hours on the performance board,
and indefinitely with hot-swapped packs [R&D].

Notes:

- **Why three boards.** The open board is the one ZXV fully owns; it is
  slow by 2026 laptop standards, and its users lean on their home devices
  for heavy work. The performance board is for owners who want local
  speed now and accept a vendor-signed boot chain under a hosted ZXV; the
  arbiter still holds every key and approval, so the trust model of
  MOBILE_COMMON section 4 is unchanged. The RISC-V board follows when its
  secure boot can be owned [R&D].
- **99 Wh in two packs.** Airlines accept lithium batteries up to 100 Wh
  each without approval [CONFIRM: IATA]. Two 49.5 Wh packs are each well
  inside that, and swapping one while running on the other keeps the
  Courier going in the field.
- **The port array behind doors.** USB-C and 3.5 mm are water-rated and
  always open; the rest sit in two gasketed port bays on the sides
  (PORTS section 1), so the Courier stays IP68 with the doors shut.

## 4. Comparative matrix

| | Signet | Folio | Courier |
|---|---|---|---|
| Processor | RK3588S (5 W cap) | RK3588 (10 W) | RK3588, K3 or X2 Elite (swappable) |
| Memory | 16 GB | 32 GB | 32 GB (up to 128 GB on X2) |
| Storage | 1 TB NVMe + TF | 2 TB NVMe + SD + TF | 2 × NVMe up to 16 TB + SD + TF |
| Display | 6.7" LTPO OLED | 11" tandem OLED (transflective option) | 14" OLED |
| Seal display + button | ● | ● | ● |
| Radios | 5G, satellite, Wi-Fi 7, BT 6.0, UWB, LoRa, GNSS | same | same + external antenna SMA |
| Battery | 27 Wh | 45 Wh | 2 × 49.5 Wh hot-swap |
| Mass | ≤ 320 g | ≤ 850 g | ≤ 2.4 kg |
| Drop | 1.8 m × 26 | 1.8 m × 26 | 1.22 m × 26 (closed) |
| Water | IP68 (2 m, 60 min) + IP69 | IP68 + IP69 | IP68 (doors shut) |
| EMP | RS105 50 kV/m | same | same |
| Operating temperature | −30 to +60 °C | −30 to +60 °C | −30 to +55 °C |
| Mesh role | THIN + HELD | THIN (+ HELD if allowed) | THIN or STANDALONE (+ HELD if allowed) |

## 5. What ZXV still needs on these devices

| # | Driver | State |
|---|---|---|
| 1 | RK3588 board support: clocks, power domains, pinmux, PCIe, USB | Missing; mainline Linux is the reference [CONFIRM] |
| 2 | Display (VOP2 + DSI or eDP) and touch | Missing |
| 3 | MT7925 Wi-Fi 7 | Missing; ZXV's 802.11 protocol layers exist (`kernel/src/wifi`) |
| 4 | M.2 modem (MBIM over USB, or PCIe) | Missing |
| 5 | Camera ISP | Missing; large job |
| 6 | Audio codec | ZXV has HD Audio (`kernel/src/hwdrv/hda.c`); the handheld codec needs its own driver |
| 7 | Arbiter firmware: seal display, seal button, SE051, kill switches, cell heater | To write; defined by this project |
| 8 | Answer-checking protocol (MOBILE_COMMON section 3.4) | [R&D] |

Until 1 to 5 exist, development runs on RK3588 boards and laptops that
already boot mainline Linux, with ZXV hosted, as the briefcase package's
section 7 recommends for its own bring-up.

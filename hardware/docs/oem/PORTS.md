<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Ports: the physical connections of every ZXV device

ZXV devices are generous with ports: data, audio and power, consumer and
critical-infrastructure. This page is the one catalogue every package
uses: what each port is, how it stays sealed, how it is protected, and
which device carries it. Hardening levels (water, surge, ESD, EMP) are in
[HARDENING.md](HARDENING.md); tags in [README.md](README.md).

## 1. Two rules every port follows

1. **Sealed.** A port is either a **water-rated receptacle**, usable while
   wet (USB-C, 3.5 mm audio, M12, MIL-DTL-38999, sealed DC), or it sits
   **behind a gasketed door** (USB-A, HDMI, RJ45, SFP, SD cards, 6.35 mm,
   DB9). The device's IP68 / IP69 / IP66 rating is tested with the doors
   closed. The ports used most (USB-C, 3.5 mm) are always the
   water-rated kind, so a wet device can still charge and play audio.
2. **Protected.** Every port has the surge and ESD protection of
   [HARDENING.md](HARDENING.md) section 7 for its type, and every port that
   carries power has an eFuse with over-voltage and reverse-polarity
   protection. A port is a hole in the Faraday cage as well; its
   connector shell is bonded to the frame all round, and its signals
   pass the port's filter before they reach anything else.

The arbiter (the trust chip each ZXV device carries) can switch every
data port off. Institutional devices ship with external data ports
disabled until an administrator enables them; USB data is then allowed
per device class, so a USB stick can be a disk but never a keyboard.

## 2. The catalogue

### 2.1 Data

| Port | Standard | Sealing | Notes |
|---|---|---|---|
| USB-C | USB4 v2 (80 Gb/s) or USB 3.2 per device, USB PD 3.1, DisplayPort alt mode | Water-rated receptacle | Data, video out, power in and out |
| USB-A | USB 3.2 Gen 2 (10 Gb/s), 5 V 1.5 A out | Door | For the existing world's sticks, keyboards, dongles |
| HDMI | HDMI 2.1 Type A (full size), up to 4K 120 Hz | Door | Full size, not micro: survives being yanked |
| Ethernet, copper | RJ45, 2.5 / 10 GBASE-T | Door | Or M12 X-coded where it must stay sealed while connected |
| Ethernet, industrial | M12 X-coded (10 GbE) / D-coded (100 Mb/s), IEC 61076-2-109 | Water-rated, screw-locked | Factories, substations, vehicles |
| Fibre | SFP / SFP+ / SFP28 cage, LC duplex | Door | Carries no current: the EMP-safe and surge-safe link |
| SD card | SD Express / UHS-II, full size | Door | Cameras, instruments |
| TF (microSD) card | microSD UHS-I / SD Express | Door, or the sealed SIM tray on the phone | |
| Serial | RS-232 on DB9 (male, DTE) | Door | Console of switches, PLCs, UPSs, lab gear |
| Fieldbus | RS-485 / RS-422 (Modbus RTU, DNP3 serial), CAN / CAN FD (J1939, CANopen) on M12 A-coded 5-pin, galvanically isolated | Water-rated | Critical infrastructure: grid, water, building and vehicle control |
| Time | IRIG-B and 1PPS on BNC, in and out | Door | Substations and telecom need disciplined time |
| GNSS antenna | SMA, active-antenna bias | Water-rated (cap) | With the 1PPS output, a time source for the site |
| Radio antenna | SMA (LoRa, SDR, external cellular) | Water-rated (cap) | Surge arrester to earth |
| Alarm / control | Dry-contact terminal block: 4 inputs, 2 relay outputs, isolated | Door | Door switches, generator start, sirens |
| Smart card | ISO 7816 slot | Door | Operator tokens (two-person rule) |
| SIM | Nano-SIM tray + eSIM | Sealed tray | |
| Docking | Pogo-pin dock connector, 16 contacts | Flush, sealed by design | Phone and tablet to their sled (section 4) |

### 2.2 Audio

| Port | Standard | Sealing | Notes |
|---|---|---|---|
| 3.5 mm | TRRS (CTIA) headset: stereo out + mic in; line out | Water-rated receptacle | Every handheld and laptop |
| 6.35 mm | ¼-inch TRS stereo headphone, driven by an amplifier for high-impedance studio headphones (up to 600 Ω) | Door | The older, larger jack, for studio and broadcast headphones |
| XLR / TRS combo | Neutrik combo socket, balanced mic or line in, 48 V phantom power | Door | Field recording, broadcast, PA |
| 3.5 mm line in | Stereo line in | Door | |

### 2.3 Power

| Port | Standard | Sealing | Notes |
|---|---|---|---|
| USB-C PD | USB PD 3.1, up to 240 W EPR (48 V) | Water-rated | Every device charges from USB-C |
| DC wide-input | 9 to 60 V DC (12 / 24 V vehicle, solar, −48 V telecom) | Sealed circular connector (LEMO-class 2-pin) | Survives the ISO 16750-2 load dump |
| Anderson Powerpole | SAE / ham-radio standard 12 to 48 V | Open, field-mateable | Field power from any battery |
| Mains | IEC 60320 C14 (≤ 10 A) or C20 (≤ 16 A) | Inside the shell gland plate | Servers, chargers |
| Mains, 3-phase | IEC 60309 | Gland plate | Count House, Hoard |
| −48 V DC | Telecom terminal block | Gland plate | Servers in telecom sites |
| Wireless | Qi2 (magnetic) | No opening | Phone and tablet |

### 2.4 Critical-infrastructure connector option

Institutional devices can be ordered with **MIL-DTL-38999 Series III**
circular connectors in place of the doored ports: sealed when mated and
unmated (with caps), bayonet-locked, and shell-bonded for EMP. One
38999 insert carries Ethernet, USB, serial and power together, so a
vehicle or shelter harness plugs in once.

## 3. Which device carries which

● = built in, ◐ = on the device's sled or dock, — = not fitted.

| Port | Signet (phone) | Folio (tablet) | Courier (laptop) | Hearth | Homestead | Guildhall | Strongbox | Count House | Hoard |
|---|---|---|---|---|---|---|---|---|---|
| USB-C | ● 2 (USB 3.2 + PD; USB 2.0 + PD) | ● 2 (USB4, USB 3.2) | ● 3 (2 × USB4, 1 × USB 3.2) | ● 2 | ● 2 | ● 2 | ● 2 | ● 2 per node | ● 2 per node |
| USB-A | ◐ 2 | ● 1, ◐ 2 | ● 3 | ● 2 | ● 4 | ● 4 | ● 2 | ● 2 per node | ● 2 per node |
| HDMI 2.1 (Type A) | ◐ 1 | ● 1 | ● 1 | ● 1 | ● 1 | ● 1 | ● 1 (console) | ● 1 per node | ● 1 per node |
| RJ45 | ◐ 1 (2.5 GbE) | ◐ 1 (2.5 GbE) | ● 1 (2.5 GbE) | ● 1 (2.5 GbE) | ● 2 | ● 2 | ● 2 (management) | ● 2 (management) | ● 2 (management) |
| M12 X-coded Ethernet | — | — | option | — | ● 1 | ● 1 | ● 2 | ● 2 | ● 2 |
| SFP fibre | — | — | ● 1 (SFP+) | ● 4 × SFP28 | ● 4 × SFP28 | ● 2 × QSFP28 | ● 2 × QSFP112 | ● 2 × QSFP112 per node | ● 2 × QSFP112 per node |
| SD (full size) | ◐ 1 | ● 1 | ● 1 | ● 1 | ● 1 | ● 1 | — | — | — |
| TF (microSD) | ● 1 (in the SIM tray) | ● 1 | ● 1 | ● 1 | ● 1 | ● 1 | — | — | — |
| RS-232 (DB9) | ◐ 1 | ◐ 1 | ● 1 | ● 1 (console) | ● 1 | ● 1 | ● 2 | ● 2 | ● 2 |
| RS-485 / CAN (M12) | ◐ 1 | ◐ 1 | ● 1 + 1 | — | ● 1 + 1 | ● 2 + 2 | ● 2 + 2 | ● 2 + 2 | ● 2 + 2 |
| IRIG-B / 1PPS (BNC) | — | — | option | — | — | ● in/out | ● in/out | ● in/out | ● in/out |
| GNSS / radio SMA | ◐ (sled) | ◐ (sled) | ● 2 | ● 1 | ● 2 | ● 2 | ● 2 | ● 2 | ● 2 |
| Alarm terminal block | — | — | — | — | ● | ● | ● | ● | ● |
| Smart card | — | — | ● 1 | — | — | — | ● 1 | ● 1 | ● 2 |
| SIM / eSIM | ● | ● | ● | option | ● | ● | ● | ● | ● |
| 3.5 mm TRRS | ● 1 | ● 1 | ● 1 | ● 1 | ● 1 | ● 1 | — | — | — |
| 6.35 mm headphone | ◐ 1 | ◐ 1 | ● 1 | ● 1 | ● 1 | ● 1 | — | — | — |
| XLR / TRS combo in | — | ◐ 1 | ● 1 | — | ● 1 | ● 1 | — | — | — |
| USB-C PD in | ● | ● | ● (up to 240 W) | ● (Hearth runs from 140 W PD) | — | — | — | — | — |
| DC wide-input (9–60 V) | ◐ (sled) | ◐ (sled) | ● | ● | ● | ● | ● | ● | ● |
| Anderson Powerpole | — | ◐ (sled) | ◐ (dock) | ● | ● | ● | ● | ● | ● |
| Mains / −48 V / 3-phase | — | — | (charger) | ● C14 | ● C14 + −48 V | ● C20 + −48 V | ● C20 × 2 (A + B) | ● 3-phase A + B | ● 3-phase A + B per rack |
| Qi2 wireless | ● | ● | — | — | — | — | — | — | — |
| MIL-DTL-38999 option | — | — | ● option | — | — | — | ● option | ● option | ● option |

Servers carry their ports on a sealed **port panel** at the shell's
gland plate, behind one gasketed door (so a technician finds every port in
one place), with the fibre, power and fieldbus entries through IP68
glands. The briefcase carries its ports in its pop-up port trench
([OEM_INSTITUTIONAL.md](OEM_INSTITUTIONAL.md) section 9).

## 4. The sleds: full ports for the handhelds

A phone is too thin for HDMI, USB-A or a 6.35 mm jack, and a rugged
phone that grew to carry them would stop being a phone. So the Signet
and the Folio carry the ports that fit, and each has a **sled**: a rugged
clip-on back (Signet) or a keyboard-dock base (Folio) that carries the
rest, with its own battery, on the 16-contact pogo dock connector. Sled
and device are each IP68 on their own; the dock contacts are flush,
gold-plated and safe to wet.

| | Signet sled | Folio dock |
|---|---|---|
| Adds | 2 × USB-A, HDMI, RJ45 2.5 GbE, SD, 6.35 mm, RS-232, RS-485 / CAN (M12), GNSS SMA, LoRa SMA, DC 9–60 V | 2 × USB-A, RJ45 2.5 GbE, 6.35 mm, XLR combo, RS-232, RS-485 / CAN (M12), GNSS and radio SMA, DC 9–60 V, Anderson Powerpole, full keyboard and trackpad |
| Battery | 5,000 mAh-class, hot-swappable | 50 Wh-class |
| Mass | ≤ 220 g | ≤ 900 g |

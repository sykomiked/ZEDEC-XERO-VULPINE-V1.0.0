<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Board interfaces: the schematic-level contracts

The block diagrams of the OEM packages ([../docs/oem/](../docs/oem/README.md))
say what connects to what. This file fixes the interfaces a schematic
designer draws from: the arbiter's I/O map, the handheld dock connector,
the power trees with their protection stages, the reference protection
circuit of every port type, and the seal and kill-switch circuits. Tags
as in the OEM packages; every part is [CONFIRM] until checked against its
datasheet, and pin counts are budgets for the arbiter part selection.

## 1. The arbiter's I/O map

The arbiter (PolarFire SoC) is the only part that touches power, keys and
the trusted user interface. Its I/O, by function:

### 1.1 Handhelds (Signet, Folio, Courier)

| Group | Signals | Pins | To |
|---|---|---|---|
| Power control | enable + power-good per rail domain: AP core, AP memory, AP I/O, NVMe, modem, satellite, Wi-Fi/BT, nRF54L, UWB, LoRa, GNSS, cameras, audio, display, backlight, ports | 32 | PMIC enables, load switches |
| Reset | AP reset, AP power-on key, modem reset, satellite reset, Wi-Fi reset | 5 | each block |
| Boot | AP boot flash SPI (arbiter is the SPI master during measurement, then hands it over), boot-flash write-protect | 6 | QSPI NOR |
| Kill switches | 4 slide switches, each double-pole: one pole to the arbiter as a sense input, one pole in series with the block's enable (section 5) | 8 | switches |
| Seal | seal display (SPI + DC + busy), seal button (2 contacts, section 4) | 6 | memory-in-pixel panel, button |
| Secure element | I²C (SCL, SDA), SE reset, SE enable | 4 | NXP SE051 |
| Host link | SPI or USB (mailbox to the AP: doorbells, message rings) + interrupt | 6 | AP |
| Battery | SMBus to the fuel gauge, cell heater PWM, cell thermistor (ADC) | 4 | gauge, heater FET, NTC |
| Charger | USB-PD controller I²C + alert; eFuse fault inputs (one per power port) | 6 | PD controller, eFuses |
| Tamper (institutional) | case switch, light sensor, accelerometer interrupt, mesh-continuity loop in and out | 5 | sensors |
| EMP / surge sense | fast field-sensor comparator, rail undervoltage comparator, protection-trip inputs | 4 | sensors (HARDENING section 4.3) |
| Window and emblem light | LED driver PWM (emblem), LED driver enable (window lighting) | 2 | constant-current drivers |
| Port enables | one per external data port group (USB-A, USB-C data, HDMI, Ethernet, serial, fieldbus, cards) | 7 | load switches and USB / video muxes |
| Debug | JTAG (4), UART (2) | 6 | test pads, sealed |
| **Total** | | **≈ 101** | fits the smallest PolarFire SoC package with spare [CONFIRM part and package] |

### 1.2 Servers (one arbiter board per shell)

As above, without the handheld groups, plus:

| Group | Signals | To |
|---|---|---|
| Node control | per node: power button, reset, power-good, BMC I²C, measured-boot SPI | up to 4 nodes per rack |
| Climate | compressor units (Modbus RTU on RS-485), fan PWM and tach, PTC heater relays, condensate-drain heater | shell plant |
| Climate sensing | dew-point sensors, surface temperature sensors (start interlock), drive-temperature readout (via the BMCs), door switches, shell pressure | shell |
| Power | UPS (Modbus or SMBus), SPD status contacts, A/B feed monitoring | power train |
| Alarm block | 4 isolated inputs, 2 relay outputs | port panel terminal block |
| Two-person rule | 2 × ISO 7816 smart card readers | status panel |

## 2. The handheld dock connector (16 contacts)

Flush, gold-plated pogo pads on the Signet and Folio; spring pins on the
sled and dock. Safe wet: every contact is unpowered until the arbiter
sees a valid sled ID, and VBUS is current-limited and switched.

| Pin | Signal | Notes |
|---|---|---|
| 1, 16 | GND | Outermost, mate first |
| 2 | DOCK_DET | Pulled up in the device; sled grounds it |
| 3 | ID (1-Wire) | Sled identity and its signed certificate; the arbiter checks it before enabling anything else |
| 4, 5 | VBUS_IN | 5 to 20 V from the sled's battery or its DC input, through the device's input eFuse (60 V tolerant) |
| 6, 7 | USB3 SS TX± | USB 3.2 Gen 1 to the sled's hub (USB-A, Ethernet, SD, serial) |
| 8, 9 | USB3 SS RX± | |
| 10, 11 | USB2 D± | Fallback and sled management |
| 12, 13 | DP_AUX± | DisplayPort auxiliary for the sled's HDMI (DP to HDMI converter in the sled); the main lanes share 6–9 in alt mode when HDMI is active, and hot-plug is reported by the sled over I²C (pins 14, 15) with an interrupt on DOCK_DET |
| 14 | I²C SDA (sled) | Sled battery gauge and port-panel status, arbiter-side bus |
| 15 | I²C SCL (sled) | |

Shell of the connector frame is bonded to the device frame and the
sled's frame (EMP continuity across the joint).

## 3. Power trees

### 3.1 Signet (Folio and Courier scale the same tree)

```
 USB-C 1 ─┐                                   
 USB-C 2 ─┤  each port: TVS ─ eFuse (OVP 24 V trip, 60 V withstand,
 Dock VBUS┤          reverse block, 5 A) ─┐
 Qi2 coil ┘  Qi2 receiver ─ eFuse ────────┤
                                          ├─ power path / charger (buck-boost, 65 W)
                                          │      │
                     cell heater FET ─────┤      ├─ Si-C cell 1S (3.87 V nominal, 7,000 mAh)
                     (arbiter PWM; on     │      │   + fuel gauge + NTC + protection IC
                      below 0 °C cell)    │      │
                                          └──────┴─ system rail VSYS (3.4–4.45 V)
                                                     │
          ┌──────────────┬───────────────┬───────────┼────────────┬──────────────┐
      ARBITER LDOs   AP PMIC (RK806  load switches  modem buck   display PMIC   always-on
      (always on,    class): core,   (arbiter-      (3.3 V, 3 A  (OLED ELVDD /  RTC, seal
      first up)      GPU, NPU,       enabled):      peak, kill-  ELVSS, AVDD)   display,
                     DDR, IO         NVMe, radios   switch pole)                SE051
```

Rules the tree enforces:

- The arbiter's rails come up first and stay up while the battery has
  charge; everything else is enabled by the arbiter.
- Every external power input has its own eFuse; no two inputs share a
  protection element, so a fault on one port cannot disable another.
- The cell is never charged below 0 °C: the charger's enable is gated by
  the arbiter, which reads the cell NTC and runs the heater first
  ([../docs/oem/HARDENING.md](../docs/oem/HARDENING.md) section 5).

### 3.2 Courier additions

Two hot-swap packs (2S each, 49.5 Wh) on an ideal-diode OR-ing stage, so
either can be removed while the other carries the load; the 9 to 60 V DC
input has a surge stage (section 4) and a 100 V-rated front end for the
ISO 16750-2 load dump; USB-C EPR input up to 48 V.

### 3.3 Servers

Mains → Type 1 + Type 2 SPD → A/B feeds → double-conversion UPS (LFP) →
PDU → node PSUs; or −48 V / 9–60 V DC → surge stage → DC-DC → the same
bus ([../docs/oem/servers/SERVER_COMMON.md](../docs/oem/servers/SERVER_COMMON.md)
section 4).

## 4. Reference protection circuit per port type

Each port's circuit, from the connector inward. Levels are those of
[../docs/oem/HARDENING.md](../docs/oem/HARDENING.md) section 7.

| Port | Stage 1 (connector) | Stage 2 (series) | Stage 3 (at the chip) | Other |
|---|---|---|---|---|
| USB-C VBUS | TVS, 58 V standoff class (EPR-capable ports) or 24 V (SPR) | — | eFuse: OVP, reverse block, current limit | PD controller with CC/SBU over-voltage protection (≥ 24 V on CC) |
| USB-C / USB-A data | Low-capacitance TVS array (≤ 0.5 pF for USB4 / USB 3.2) | — | Re-driver / mux behind the array | Port-enable load switch (arbiter) |
| HDMI | Low-capacitance TVS array on TMDS / FRL, DDC, CEC, HPD | — | Level shifter on DDC | 5 V out current-limited |
| Ethernet (RJ45) | Magnetics (1.5 kV isolation) | — | TVS on the PHY side | Bob-Smith termination to chassis through a 2 kV capacitor; outdoor server ports add a GDT on the cable side |
| SFP | — (fibre carries no current) | — | ESD on the cage's low-speed signals | Cage bonded to chassis |
| RS-232 | TVS rated ±15 V signalling | 10 Ω series | Transceiver with ±15 kV ESD | Isolated option for servers |
| RS-485 / CAN | GDT to chassis + TVS line to line | PTC resettable fuse | Isolated transceiver (≥ 2.5 kV) | Split termination, switchable |
| DC wide-input (9–60 V) | TVS (load-dump class) + reverse-polarity MOSFET | Common-mode choke | eFuse / hot-swap controller, 100 V front end | Input capacitance sized for the ISO 7637-2 pulses |
| Mains (servers) | Type 1 + 2 SPD (IEC 61643-11) | EMI filter | PSU's own protection | UPS between line and IT load |
| 3.5 / 6.35 mm audio | Low-capacitance TVS per contact | 22–47 Ω series | Codec / amplifier | Jack-detect debounced |
| XLR combo (48 V phantom) | TVS rated above 48 V phantom | Series resistors per phantom spec | Preamp with protected inputs | Phantom switched by software, default off |
| SD / TF cards | ESD array on CMD, CLK, DAT | Series 22–33 Ω | Host controller | Card-power load switch |
| SMA antennas | GDT to chassis (external antennas) | DC block | Limiter (PIN diode) before the LNA (EMP, HARDENING 4.3) | Antenna bias current-limited |
| Alarm contacts | TVS | — | Opto-isolators / relays | Field side isolated ≥ 2.5 kV |
| IRIG-B / 1PPS BNC | TVS | 50 Ω | Buffered input / output | Isolated (transformer) for IRIG-B AM |

## 5. Seal and kill-switch circuits

### 5.1 Seal button

- A sealed tactile switch with **two independent contacts** (normally
  open), each read by the arbiter on its own input with its own pull-up.
  A press is valid only when both contacts close within 20 ms and stay
  closed ≥ 300 ms: a single shorted contact, or a short induced by water
  or a transient, is not a press.
- No connection to the application processor at all: not a shared GPIO,
  not a pull-up rail. The button's rail comes from the arbiter's
  always-on supply.

### 5.2 Seal display

- A memory-in-pixel reflective LCD (1.0 inch on the Signet) on an SPI bus
  that only the arbiter drives. Its contents persist with no power, so
  the last approval request stays visible.
- The arbiter draws its text from its own font ROM; no image from the
  application processor is ever passed through.

### 5.3 Kill switches

Each is a double-pole slide switch:

- **Pole A** is in series with the enable of the block's load switch (or
  its power directly for the cellular and satellite modules), so OFF
  removes power regardless of any software, the arbiter's included.
- **Pole B** goes to the arbiter as a sense input, so the arbiter knows
  and shows the state on the seal display.

| Switch | Pole A cuts |
|---|---|
| Cellular + satellite | modem and satellite module power |
| Wi-Fi + Bluetooth + UWB + LoRa | their enables |
| Cameras | camera rail |
| Microphones | microphone bias and the codec's ADC enable |

## 6. What these documents are not

These are interface contracts, not drawn schematics. The next step is the
schematic capture (KiCad, so the files can be published with the designs
under `hardware/boards/`) of the Signet main board first, against these
tables, then the Folio and Courier boards, which reuse it.

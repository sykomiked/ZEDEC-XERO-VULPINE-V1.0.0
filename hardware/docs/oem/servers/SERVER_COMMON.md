<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Server common specification

What all six servers share: the climate shell, the platform, power,
security, the storage service and the test plan. Tags are defined in
[README.md](README.md); every figure below is [CONFIRM] unless tagged
otherwise.

## 1. The climate contract

The servers are sold with one promise: **they run anywhere a person can
install them**. That is written as two envelopes, one for the outside of
the shell and one for the inside.

| | External (the site) | Internal (what the electronics see) |
|---|---|---|
| Dry-bulb temperature, operating | −40 to +50 °C; +55 °C for 16 h | 18 to 27 °C supply air |
| Dry-bulb temperature, storage / transport | −40 to +70 °C | same |
| Humidity | 0 to 100 % RH, condensing, rain and driven rain | dew point −9 to +15 °C, RH ≤ 60 % |
| Altitude | −300 to 3,000 m | same |
| Dust, sand | blowing sand and dust | none (sealed) |
| Salt | coastal and marine salt fog | none (sealed) |
| Solar load | full sun, 1,120 W/m² | none |

The internal envelope is the ASHRAE TC 9.9 **recommended** envelope
(18 to 27 °C, dew point −9 to 15 °C) [CONFIRM: ASHRAE Thermal Guidelines,
5th ed., reference card], with the relative humidity held below 60 %
rather than ASHRAE's 70 % for margin against corrosion. The external
envelope covers ETSI EN 300 019-1-4 class 4.1E (−45 to +45 °C,
non-weather-protected) [CONFIRM; cite the 2014 edition's class names, the
2026 V3.1.1 edition renumbers them], the Telcordia NEBS GR-63-CORE
short-term limits (−5 to +55 °C, 5 to 90 % RH), and the outdoor intent of
GR-3108-CORE class 4 [CONFIRM: the class 2 to 4 tables must be bought].
The 3,000 m ceiling is the hard drives' operating altitude (Seagate Exos M:
−304.8 to 3,048 m [CONFIRM]) and ASHRAE's 3,050 m limit.

Why two envelopes. The parts set the inside: the Exos M hard drive
operates at 10 to 60 °C *drive-reported* temperature with 5 to 95 % RH
non-condensing and at most 20 % RH per hour of change; the QLC flash
drives operate at 0 to 70 °C [CONFIRM: Seagate manual 210048100 rev D;
Solidigm and Micron product pages]. No part tolerates condensation or
salt. The shell's job is to turn any outside into that inside.

## 2. The climate shell

One construction, three sizes:

| Size | Used by | Outer dimensions (W × D × H) | Internal IT space | Sustained heat it removes |
|---|---|---|---|---|
| **S** pedestal | Hearth | 420 × 560 × 640 mm | one 4U-tall tower bay, about 60 L | 450 W at +50 °C outside |
| **M** rack | Homestead | 800 × 1,000 × 1,350 mm | 24U, 19-inch, 1,000 mm deep | 1.6 kW at +50 °C, N+1 |
| **L** rack | Guildhall, Strongbox, Count House, Hoard (two) | 800 × 1,200 × 2,200 mm | 42U, 19-inch, 1,200 mm deep | 4.2 kW at +50 °C per rack, N+1 |

The M and L sizes follow the sealed outdoor rack class (for example the
Schneider R-Series, IP54 / NEMA 4, 33 to 126 RU, and the Vertiv VRC-S)
[CONFIRM]; the S pedestal is project-specific. All three are built the
same way:

```
 OUTSIDE AIR (never enters)                 INSIDE AIR (sealed loop)
 +-----------------------------------+     +--------------------------------+
 | louvred cowl, salt-coated coil    |     | IT equipment front -> back     |
 | external fans (IP66, EC motors)   |     | hot aisle at the rear          |
 |   ^                               |     |   |                            |
 |   | heat out                      |     |   v                            |
 | [condenser] <- refrigerant -> [evaporator] <- internal fans             |
 | [air-to-air heat exchanger, plate, counter-flow]  (free cooling)        |
 | condensate drain, heated, trapped |     | PTC heaters (cold start)       |
 +-----------------------------------+     | desiccant + dew-point sensors  |
                                           | ePTFE pressure-equalising vent |
                                           +--------------------------------+
   walls: double-skin 5052 aluminium, 40 mm PIR insulation, gasketed doors
```

### 2.1 Ingress and corrosion

- **IP66** for every shell sold for outdoor use (dust-tight; powerful water
  jets), **IP55** minimum for indoor-only orders [CONFIRM: IEC 60529].
  Doors use a continuous EPDM gasket on a three-point latch; every cable
  enters through IP68 glands in a gland plate at the bottom.
- **Pressure equalisation.** An ePTFE membrane vent, as in the briefcase
  vault ([../PLATFORM_COMMON.md](../PLATFORM_COMMON.md) section 6.3): the shell
  breathes through the membrane without passing water or dust, so the
  gaskets are not pumped by daily temperature swings.
- **Salt.** External coils are e-coated; the external sheet is marine-grade
  5052 aluminium; fasteners are A4 (316) stainless. Qualified by MIL-STD-810H
  Method 509.7 salt fog (5 % NaCl, 35 °C, alternating 24 h wet and dry)
  [CONFIRM].
- **Boards.** Every board in the shell is conformal-coated to IPC-CC-830
  (rev C) [CONFIRM], as the second line of defence if the seal is ever
  breached. Connectors, DIMM slots and drive bays are masked.

### 2.2 Heat out: three regimes

The controller picks the regime from outside temperature and load:

| Outside | Regime | What runs |
|---|---|---|
| Below about +15 °C | **Free cooling** | Air-to-air plate heat exchanger only; compressor off |
| +15 to +50 °C | **Active cooling** | Compressor unit; heat exchanger assists |
| In a data centre | **Facility water** | Rear-door water coil on facility chilled water; compressor not fitted |

- **Compressor units** are enclosure air conditioners of the Rittal Blue e+
  class: 1.6 to 5.8 kW units, rated for example at 4.2 kW at A35/A50
  (35 °C inside, 50 °C outside) [CONFIRM, reseller figures]. Each shell
  carries two, **N+1**: either alone holds the rated sustained heat at
  +50 °C. The S pedestal uses one 0.5 kW unit plus the heat exchanger
  (section 2.6 explains why that is enough).
- **No thermoelectric (Peltier) stage in the heat path**, for the reason
  the briefcase package gives ([../PLATFORM_COMMON.md](../PLATFORM_COMMON.md)
  section 6.1): at a useful temperature difference a Peltier moves about
  one watt of heat per watt it burns. [CHANGED from any brief that asks for
  "solid-state cooling" of a server.]
- **Sizing rule.** Cooling capacity at A35/A50 ≥ 1.25 × (sustained IT heat
  + solar gain + fan heat). Solar gain on an L rack in full sun with the
  coating below is about 0.4 to 0.6 kW [R&D: measure on the prototype].

### 2.3 Cold: heaters and the start interlock

- **PTC heaters** inside the shell: 100 W (S), 400 W (M), 800 W (L), on
  the internal air path. Once running, the IT load heats itself; the
  heaters are for cold starts and for holding a powered-down shell above
  the batteries' charging limit.
- **Start interlock (arbiter-enforced).** The arbiter keeps every IT power
  rail off until all of these hold:
  1. every hard drive reads ≥ 10 °C (Exos M minimum operating) on its
     own sensor [CONFIRM] (all-flash shells: every SSD ≥ 0 °C);
  2. every surface sensor reads at least 3 K above the internal dew point
     (no condensation on a cold board);
  3. the LFP battery is above 0 °C before it is charged.

  From −40 °C an L shell reaches all three in about 90 minutes with 800 W
  of heat [R&D: measure]. The interlock state is reported on the status
  panel and over the management network.

### 2.4 Humidity

- The internal loop is sealed, so moisture only enters through the
  membrane vent and when a door is opened.
- The compressor's evaporator runs below the internal dew point and
  condenses moisture out of the internal air; the condensate leaves
  through a heated, trapped drain (heated so it cannot freeze shut).
- A rechargeable silica-gel cartridge holds the internal air dry when the
  compressor is off; its weight change is reported, so it is swapped when
  saturated.
- **Door-open rule.** Opening a door in humid air lets moist air in.
  Doors open to a service lock: the arbiter raises cooling first, and after
  the door closes it runs the dehumidify cycle before raising any power
  limit back.

### 2.5 Sand, dust and wildlife

The internal air never mixes with the outside, so there are no internal
filters to clog. The external coil sits behind a coarse, washable
stainless mesh and louvred cowl. Qualified by MIL-STD-810H Method 510.7
(blowing dust, blowing sand, settling dust) [CONFIRM].

### 2.6 Why the S pedestal can be quiet

A home server must not sound like a data centre. The Hearth's sustained
heat is about 260 W (see [SERVER_CIVILIAN.md](SERVER_CIVILIAN.md)). Below
+15 °C outside the plate heat exchanger alone removes it with the external
fans at low speed; the 0.5 kW compressor only starts on hot days. Target:
≤ 30 dB(A) at 1 m on free cooling, ≤ 42 dB(A) with the compressor running
[R&D].

### 2.7 Finish and livery

The shells wear the ZXV identity: **forest night `#002911`** with
**gold** trim and the gold mark on the door. A dark colour in full sun
absorbs most of the sunlight that hits it, which would add hundreds of
watts of solar gain. The finish is therefore a **cool-pigment powder coat**:
infrared-reflective pigments that show the forest-night colour to the eye
while reflecting much of the near-infrared half of sunlight. Target total
solar reflectance ≥ 0.35 at the `#002911` colour [R&D: measure a sample;
if not reached, outdoor shells get a ventilated sun shield in the same
colour, which removes most of the solar gain]. Gold trim is anodised
aluminium (not paint), so it does not chalk in sun or salt.

## 3. Platform

Every model runs ZXV on AMD EPYC processors (one x86_64 port, one board
profile family), and every model carries the briefcase's **centre
arbiter** ([../OEM_INSTITUTIONAL.md](../OEM_INSTITUTIONAL.md) section 2): an FPGA
SoC that owns power sequencing, the start interlock, fans and compressors,
the tamper sensors, the status panel and the keys.

| Model | CPU family | Socket | Arbiter |
|---|---|---|---|
| Hearth | EPYC 4005 | AM5 | PolarFire SoC MPFS250T (non-security variant) |
| Homestead, Guildhall | EPYC 8005 "Sorano" | SP6 | PolarFire SoC MPFS250T |
| Strongbox, Count House, Hoard | EPYC 9005 "Turin" | SP5 | PolarFire SoC MPFS250TS (security variant) |

- **One arbiter design for all six.** The civilian models use the same
  board with the tamper zeroisation not populated; the institutional
  models use the security variant, as the briefcase Vanguard does.
- **Why not Venice (Zen 6).** AMD reports the EPYC "Venice" line in
  production since May 2026, but OEM servers are expected from Q4 2026 at
  the earliest [CONFIRM: AMD press releases]. These specs use shipping
  parts; Venice is the mid-life refresh for the SP5-class tiers.
- **Memory is always ECC**, and every channel is populated (balanced
  interleave), as in the briefcase package.

## 4. Power

| | Hearth | Homestead | Guildhall | Strongbox | Count House | Hoard |
|---|---|---|---|---|---|---|
| Mains input | 100–240 V, 1-phase | 200–240 V, 1-phase | 200–240 V, 1-phase | 200–240 V, 1-phase, A + B feeds | 400 V 3-phase or 2 × 1-phase, A + B | 400 V 3-phase, A + B per rack |
| IT sustained / peak | 0.26 / 0.36 kW | 0.45 / 0.65 kW | 0.80 / 1.05 kW (1.6 kW peak at 100 drives) | 0.75 / 1.10 kW | 2.6 / 3.7 kW | 5.5 / 7.3 kW (both racks) |
| Battery (LFP) | 1 kWh, 2 h at sustained | 2.5 kWh | 5 kWh | 5 kWh | 10 kWh | 2 × 15 kWh |
| Solar / DC input | 48 V DC input option | 48 V DC input option | 48 V DC input option | 48 V DC (telecom) | 48 V DC | −48 V DC or 3-phase |

- **Batteries are lithium iron phosphate (LFP)**: no thermal runaway of the
  NMC kind, and a long cycle life. LFP must not be charged below 0 °C; the
  heater and interlock of section 2.3 handle that.
- The battery is sized to finish writes and shut down cleanly with margin,
  or to ride through a generator start; it is not a multi-day reserve.
- **Graceful power-down is part of the storage design**, not only of the
  UPS: the storage service (section 6) journals every write, so a power cut
  never leaves a half-written stripe.

## 5. Security

All six models:

- **Measured boot, arbiter as root of trust.** The arbiter holds the boot
  flash write-protect, verifies the firmware and the signed ZXV image
  before releasing CPU reset, and keeps the measurements.
- **Self-encrypting drives.** Every drive is a TCG Opal SED; the unlock key
  is released by the arbiter only after a good measured boot. Order the
  FIPS 140-3 SKU for the institutional models [CONFIRM per drive].
- **Signed updates only**, through ZXV's A/B update path
  (`kernel/src/loader/abupdate.c`, power-cut tested at every write).

Institutional models add, as the briefcase Vanguard does
([../OEM_INSTITUTIONAL.md](../OEM_INSTITUTIONAL.md) section 6):

- **Tamper detection.** Door switches, a light sensor, accelerometer, and
  the shell's own pressure and humidity readings: an opened seal changes
  both, so an unexpected change is a tamper signal.
- **Zeroisation.** On a confirmed tamper the arbiter erases the key store
  (BBRAM) and the drives' media-encryption keys; the data on the drives is
  then unreadable.
- **FIPS 140-3 and CNSA 2.0** targets as in the briefcase package section
  6.4 and 6.5; ZXV's own transport is ML-KEM-1024 + ML-DSA-87 (Vinea
  protocol v3).

## 6. The storage service

Data is spread across drives (and, for the larger models, across servers)
with a **systematic Reed-Solomon erasure code over GF(2⁸)**, the code
already in `kernel/src/freight` (`freight.h`: data rows verbatim, parity
rows after). A stripe of *k* data and *m* parity pieces survives any *m*
lost pieces.

| Model | Code (k + m) | Pieces per stripe | Survives |
|---|---|---|---|
| Hearth | 4 + 2 | 6 drives | any 2 drives |
| Homestead | 16 + 4 | 20 drives | any 4 drives |
| Guildhall | 2 stripes of 16 + 4 | 40 drives | any 4 drives in each stripe |
| Strongbox | 8 + 4 | 12 drives | any 4 drives |
| Count House | 16 + 8 | 24 drives across 3 servers | one whole server, or any 8 drives |
| Hoard | 12 + 8 | 20 drives across 4 servers | one whole server and 3 more drives |

- **State: [R&D].** The code exists and is tested (`test_freight`). The
  storage service that stripes files across drives and servers, rebuilds a
  lost drive and scrubs in the background does not exist yet. It belongs
  above ZXVFS (`kernel/src/zxvfs`, journaled, crash-tested at every write)
  and uses its journal for write ordering. Until it exists, a model can ship
  with mirrored ZXVFS volumes at half the usable capacity.
- Every stripe carries SHA-256 checksums, so a corrupted piece is detected
  and rebuilt, not silently returned.
- Hot data sits on the TLC tier, cold data on the QLC (or hard-drive)
  tier; the tiering policy runs in ZXV.

## 7. Environmental test plan

Each model is qualified in its shell, powered and running a mixed storage
and compute load, against:

| Test | Standard | Condition |
|---|---|---|
| High temperature, operating | MIL-STD-810H 501.7 Proc II | +50 °C constant, then diurnal hot-dry cycle to +55 °C; full load must hold without throttling below the rated sustained power |
| Low temperature, operating and cold start | MIL-STD-810H 502.7 Proc II | soak at −40 °C unpowered, then power on: the interlock must hold the rails off, warm up, start, and run |
| Humidity | MIL-STD-810H 507.6 Proc II | ten 24 h cycles, 30 to 60 °C at 95 % RH; internal RH must stay ≤ 60 % and no condensation on any sensor |
| Salt fog | MIL-STD-810H 509.7 | 5 % NaCl, 35 °C, alternating 24 h wet / 24 h dry, ≥ 2 cycles |
| Sand and dust | MIL-STD-810H 510.7 Proc I, II, III | blowing dust, blowing sand, settling dust |
| Ingress | IEC 60529 | IP66 (outdoor shells), IP55 (indoor) |
| Telecom sites | Telcordia GR-63-CORE | temperature, humidity, earthquake zone 4, fire spread (where sold to telecom sites) |
| Data centre | ASHRAE class A4 inlet (5 to 45 °C, up to 90 % RH) [CONFIRM] | with facility water cooling |

Pass criterion for every test: no data error (scrub clean), no
unscheduled throttle below the rated sustained power, internal envelope
held, start interlock behaving as specified.

## 8. What ZXV still needs on these servers

| # | Driver or service | Needed by | State |
|---|---|---|---|
| 1 | NVMe | All | **Built** (`kernel/src/hwdrv/nvme.c`, boot-checked in QEMU); needs TCG Opal security commands for the SED unlock |
| 2 | AHCI / SATA | Hearth | **Built** (`hwdrv/ahci.c`) |
| 3 | 25 GbE adapter (one family, e.g. Intel E810) | Civilian | Missing |
| 4 | 100 and 400 GbE adapter (one family, e.g. NVIDIA ConnectX-7) | Institutional, Guildhall | Missing; ConnectX needs firmware co-operation |
| 5 | SAS HBA (Broadcom 9500 class) for the hard drives | Homestead, Guildhall, hard-drive options | Missing |
| 6 | Arbiter mailbox device | All | Defined by this project (as in the briefcase package) |
| 7 | Storage service (section 6) | All | [R&D] |
| 8 | Multi-server cluster membership | Count House, Hoard | ZXV's mesh (Vinea v3 sessions) carries it; the storage placement protocol is [R&D] |

Until 3 to 5 exist, a model can run ZXV's daemon `zxvd` hosted on Linux,
as the briefcase package describes for its accelerators, with bare-metal
ZXV following driver by driver.

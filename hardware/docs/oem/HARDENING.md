<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Hardening: shock, water, EMP and surge for every ZXV device

Every ZXV device is rugged: the phone, tablet and laptop
([mobile/](mobile/README.md)), the briefcase workstation
([README.md](README.md)) and the servers ([servers/](servers/README.md)).
This page fixes the three hardening requirements once, with the standard
and the test level for each, so every package cites the same numbers.
Tags are defined in [README.md](README.md). Standards figures here come
from memory and secondary sources and are [CONFIRM] until checked against
the purchased standard.

## 1. What "proof" means here

"Shockproof", "waterproof" and "EMP-proof" are not standards; a spec that
says them promises nothing testable. Each is written below as a named test
at a stated level, with a pass criterion. A device that passes is sold as
meeting that test, not as "proof".

| Requirement | Written as |
|---|---|
| Shockproof | Survives the MIL-STD-810H transit drop and functional shock at the levels in section 2, working afterwards with no data loss |
| Surge-proof | Survives the IEC 61000-4-5 surge levels of section 7 on every power and data port, with no damage and no data loss |
| Waterproof | IEC 60529 IP68 and IP69 at the levels in section 3 (servers: IP66, their shells are not meant to be submerged) |
| EMP-proof | Survives a high-altitude EMP (HEMP) E1 field at the level in section 4 and recovers to full function; **while powered off and closed, it survives with margin** |

## 2. Shock and vibration

| Test | Standard | Level by device |
|---|---|---|
| Transit drop | MIL-STD-810H Method 516.8 Procedure IV | Phone, tablet: 26 drops from **1.8 m** onto plywood over concrete (every face, edge and corner), powered on. Laptop: 26 drops from **1.22 m**, closed, plus 1.0 m open. Briefcase: as its package. Servers: crated, per the shipping standard |
| Functional shock | MIL-STD-810H 516.8 Procedure I | 40 g, 11 ms half-sine, all axes, operating |
| Vibration | MIL-STD-810H Method 514.8 | Handhelds: category 24 general minimum integrity. Laptop, briefcase: category 20 ground vehicle. Servers: transport, crated |
| Tumble (handhelds) | IEC 60068-2-31 / 60068-2-32 | 1,000 tumbles at 1.0 m |

Pass: no cracked glass, no lost seal (IP re-test after the drops), no
data error, every function working.

How the designs meet it: a magnesium or titanium mid-frame carries the
loads; corners have elastomer bumpers that crush before the frame; the
display glass is set back from the bumpers by at least 1 mm; boards are
underfilled under large BGA packages; batteries sit in a cradle that stops
them punching through the back on impact.

## 3. Water and dust

| Rating | Meaning (IEC 60529) | Applies to |
|---|---|---|
| IP68 | Dust-tight; continuous immersion beyond 1 m. The depth and time are declared by the maker: **2.0 m for 60 minutes** here | Phone, tablet, laptop (closed), briefcase (vault) |
| IP69 | Dust-tight; high-pressure, high-temperature water jets (80 °C, 8 to 10 MPa, close range) | Phone, tablet |
| IP66 | Dust-tight; powerful water jets | Server shells |

- IP68 and IP69 are separate tests and both are run: passing one does not
  imply the other.
- **Salt water.** Handhelds also pass MIL-STD-810H Method 509.7 salt fog;
  ports are gold-plated and sealed, and the USB-C port is a sealed,
  water-rated receptacle that tolerates wet insertion.
- **Pressure equalisation.** As in the briefcase vault, handhelds breathe
  through an ePTFE membrane so the seals are not pumped by altitude
  changes (an aircraft cabin is about 75 kPa).
- **Speakers and microphones** sit behind hydrophobic acoustic membranes;
  the phone and tablet eject water from the speaker cavity with a tone.

## 4. EMP

### 4.1 The threat

A high-altitude nuclear EMP (HEMP) has three parts:

| Part | What it is | What it threatens |
|---|---|---|
| **E1** | A field pulse of about **50 kV/m**, rising in about 2.5 ns, about 23 ns wide (IEC 61000-2-9 waveform) | Everything with electronics and conductors: it couples into antennas, cables and slots and drives transients into chips |
| **E2** | Lightning-like, microseconds to a second | Mostly handled by ordinary lightning protection |
| **E3** | Slow (seconds to minutes), geomagnetic-storm-like | Long conductors: power lines and long cables. It takes down the grid; it does not reach a battery device with no long wires |

The devices are hardened against **E1** directly and against **E3**
indirectly: they run on batteries, and the servers ride through a grid
loss on their own LFP batteries and 48 V DC or solar input
([servers/SERVER_COMMON.md](servers/SERVER_COMMON.md) section 4).

### 4.2 Test levels

| Test | Standard | Level | Applies to |
|---|---|---|---|
| Radiated transient field | MIL-STD-461G **RS105** | 50 kV/m, E1 waveform, all axes | Every device, operating |
| HEMP immunity | IEC 61000-4-25 | Radiated and conducted HEMP immunity at the levels it defines for the environment | Every device |
| Conducted transients on cables | MIL-STD-461G CS116 and IEC 61000-4-24 protective devices | On every external port | Laptop, briefcase, servers |
| Facility protection | MIL-STD-188-125-1 (fixed) / -2 (transportable) | Shielded enclosure, protected penetrations | Hoard and Count House when ordered for critical sites |
| ESD | IEC 61000-4-2 | ±15 kV air, ±8 kV contact | Every device |

Pass: the device keeps running or restarts by itself within 60 s, with no
data error and every function working. Radio front ends must survive and
recover; a radio dropping its link during the pulse is allowed.

### 4.3 How the designs meet it

The rule is **shield the box, filter every hole**:

1. **Conductive enclosure.** Handhelds: a metal mid-frame bonded to a
   conductive inner coating on the back, and the honeycomb window and the
   display shield of section 6 closing the two large openings, so the
   electronics sit in a closed cage. Servers: the shell's double aluminium
   skin, seams bonded with conductive gaskets, the door window to
   section 6.
2. **Every hole is a waveguide below cutoff or is filtered.** Vents are
   honeycomb waveguides (as the briefcase louvres, section 6.4 of
   [PLATFORM_COMMON.md](PLATFORM_COMMON.md)); every cable that enters a server
   shell passes a filtered, surge-protected entry panel; data leaves the
   server shells on **optical fibre**, which carries no current.
3. **Antennas are the deliberate hole.** Each antenna port has a limiter
   (PIN-diode or gas-discharge, by frequency) between antenna and front
   end, and the front end is on its own power-gated island. A phone cannot
   be immune while its antennas are connected; it is built so the pulse
   kills at most a limiter, and the limiter is designed not to die.
4. **The arbiter holds the line.** On a detected transient (a fast field
   sensor on the arbiter, and supply-rail undervoltage), the arbiter
   power-cycles the radio islands and keeps the storage writes atomic
   (ZXVFS journal), so a pulse mid-write leaves the old data or the new,
   never a mix.
5. **Off is safest.** Powered off and closed, a device has no active
   circuits to latch up and its antenna limiters are not biased; that is
   the state sealed spares are stored in.

Shielding targets: **≥ 60 dB from 10 kHz to 1 GHz** for handhelds and the
laptop, **≥ 80 dB from 1 MHz to 1 GHz** for server shells ordered to
MIL-STD-188-125 [R&D: measure on the prototype; seams and the display
window decide the handheld figure].

## 5. Temperature

Handheld temperature limits are set by the battery, not by the
electronics:

| | Phone, tablet | Laptop |
|---|---|---|
| Operating | −30 to +60 °C | −30 to +55 °C |
| Charging | 0 to +45 °C cell temperature, with the cell heater bringing a cold cell up to 0 °C first | same |
| Storage | −40 to +70 °C | same |
| Test | MIL-STD-810H 501.7 and 502.7, 507.6 humidity, 509.7 salt fog | same |

A lithium-ion cell must not be charged below 0 °C (lithium plating). Each
handheld has a thin-film heater on the cell, powered from the charger:
plugged in at −30 °C, it warms the cell first and charges after. Below
about −20 °C the cell's power drops; the devices throttle and say so. The
servers' envelope is in [servers/SERVER_COMMON.md](servers/SERVER_COMMON.md)
section 1.

## 6. The honeycomb window: a universal feature

Every ZXV device shows its interior through a clear window behind a
hexagonal honeycomb, and that honeycomb is **part of the Faraday cage**,
not a hole in it. This began as the briefcase's optical window
([PLATFORM_COMMON.md](PLATFORM_COMMON.md) section 7) and is now a feature of
every device: the phone's back, the tablet's back, the laptop's lid and
base, the briefcase's vault, and every server shell's door.

### 6.1 Layers, outside to inside

| Layer | What it is | What it does |
|---|---|---|
| Grille | Machined hexagonal honeycomb in the frame metal (titanium on handhelds, aluminium on the laptop and shells), cells ≤ 5 mm across flats, depth ≥ 3 × cell width where it also passes air, ≥ 1 × elsewhere | Structure and impact armour for the glass; a waveguide below cutoff for radio energy (section 6.2) |
| Armour | Sapphire (institutional, handheld backs) or chemically strengthened aluminosilicate glass, anti-reflective and oleophobic both sides | Water and dust seal (IP68 / IP69 / IP66), scratch and impact |
| Shield mesh | A hexagonal copper micro-mesh embedded in the glass laminate, blackened, pitch 0.30 mm, line width ≤ 10 µm, **bonded to the frame all round** with a silver-filled silicone gasket | Closes the cage across the window (section 6.2) |
| Inside | The arbiter's status lighting, the cold plates, the drives | What the owner sees |

The grille and the mesh use the **same hexagon**, at two scales, aligned
so the fine mesh reads as a texture inside each grille cell: one visual
language from a phone to the Hoard's vault rack. The standard design is
section 6.5.

### 6.2 Why it shields

- **The grille.** A hole in a metal wall passes radio energy above a
  cutoff frequency set by its width *w*: roughly f_c ≈ c / (2w). For
  5 mm cells f_c ≈ 30 GHz. Below cutoff the field decays through the cell
  by about 27 dB for each cell width of depth, so a cell 3 × as deep as it
  is wide attenuates about 80 dB well below cutoff [CONFIRM: standard
  waveguide-below-cutoff approximation; measure the real grille].
- **The mesh.** A thin conductive mesh of pitch *g* shields roughly
  20 · log10(λ / 2g): with g = 0.30 mm that is about 54 dB at 1 GHz,
  60 dB at 500 MHz and 40 dB at 6 GHz. An E1 pulse with a 2.5 ns rise
  carries most of its energy below about 1 GHz, where the mesh is
  strongest. Above that the grille's depth adds its attenuation in the
  sections where it is deep.
- **Light.** A 10 µm line on a 0.30 mm hexagonal pitch covers about 7 %
  of the area; with the coatings, transmission is about 85 % [R&D:
  measure], so the interior stays clearly visible.
- **The bond decides everything.** A mesh that is not bonded to the frame
  all round is an antenna, not a shield. The gasket bond is inspected on
  every unit (a continuity and transfer-impedance check at end of line)
  [R&D: define the test].

### 6.3 The display: the other window

A handheld's display is its largest opening. It gets the same treatment
at a finer scale: a hexagonal metal mesh of **0.10 mm pitch and ≤ 3 µm
line**, below the eye's resolving limit at reading distance, laminated
between the cover glass and the touch layer and bonded to the frame
[R&D]. It costs about 6 to 8 % of display brightness, which the panel's
peak brightness covers. Target: ≥ 40 dB from 30 MHz to 1 GHz through the
display [R&D: the touch sensor and the shield must be designed together;
a metal-mesh touch sensor can share the lamination process].

### 6.4 Lighting rules, everywhere

The briefcase's rule applies to every device with a window: no light is
driven directly by a data bus, disk or network activity line; window
lighting is driven only by the arbiter, from aggregated metrics at no more
than 2 Hz with randomised timing; institutional devices ship with it off
and take an opaque cover for TEMPEST zones. A visible interior must not
become an optical side channel.

## 7. Surge

Every port that can carry current from outside is protected so a surge on
it does not damage the device. Mains-powered equipment also gets
protection at its power entry.

### 7.1 Levels

| Port | Standard | Level |
|---|---|---|
| Mains input (servers, briefcase and laptop chargers) | IEC 61000-4-5 | **±4 kV** line to earth, **±2 kV** line to line (installation class 4) |
| DC power input (12 to 60 V: vehicle, solar, telecom −48 V) | IEC 61000-4-5 and ISO 7637-2 / ISO 16750-2 load dump for vehicle inputs | ±2 kV line to earth, ±1 kV line to line; vehicle inputs survive a 100 V load dump |
| USB-C power (USB PD, up to 48 V EPR) | IEC 61000-4-5 | ±1 kV on VBUS; the input survives a sustained fault of **60 V** without damage |
| Ethernet (copper) | IEC 61000-4-5, ITU-T K.21 for exposed lines | ±2 kV common mode (±4 kV on server outdoor ports) |
| Serial and fieldbus (RS-232, RS-485, CAN) | IEC 61000-4-5 | ±2 kV common mode, ±1 kV line to line |
| Audio, video, USB data, card slots | IEC 61000-4-2 ESD plus IEC 61000-4-5 at ±0.5 kV | ESD ±15 kV air, ±8 kV contact |
| Antennas | IEC 61000-4-5 at the connector of external antennas | ±2 kV, gas-discharge arrester to earth |

Pass: no damage; the device keeps running, or restarts by itself within
60 s with no data loss.

### 7.2 How the designs meet it

- **Layered protection on every port**: a gas-discharge tube or
  metal-oxide varistor takes the energy where the level is high, a
  series element (resistor, PTC or inductor) limits current, and a
  fast TVS diode clamps what is left at the chip. On a laptop or phone the
  first stage is a TVS sized for the port.
- **Power inputs** have an eFuse with over-voltage and reverse-polarity
  protection ahead of the regulators, rated above the port's highest
  legitimate voltage. A USB-C charger that fails and applies 48 V to a
  5 V-negotiated port does not damage the device.
- **Isolation where it is cheap.** RS-485, CAN and the server alarm
  contacts are galvanically isolated (≥ 2.5 kV); servers send data over
  fibre between shells; Ethernet magnetics give 1.5 kV isolation.
- **Mains entry for servers**: a coordinated Type 1 + Type 2 surge
  protective device (IEC 61643-11) at the shell's power entry, then the
  double-conversion UPS with its LFP battery between the mains and the IT
  load, so the electronics never see the line directly.
- **The arbiter records surges.** Every protection stage that trips is
  logged with its time, so a site with repeated surges is visible.

### 6.5 The standard design: clear body, forest-night honeycomb, the emblem

This is the look of every ZXV device, from the Signet to the Hoard:

- **A clear body.** The window is as large as the structure allows: the
  whole back of the Signet and Folio, the Courier's lid, the briefcase's
  vault, each server shell's door. The owner sees the machine.
- **The honeycomb in forest night, `#002911`.** The grille metal is
  finished in the ZXV colour: anodised (aluminium) or PVD / ceramic coated
  (titanium) to `#002911`; the micro-mesh behind it is blackened copper,
  so it disappears behind the grille and the interior reads clearly.
  Outdoor server shells use the cool-pigment coating of
  [servers/SERVER_COMMON.md](servers/SERVER_COMMON.md) section 2.7 on their
  walls; their door grille is the same `#002911`.
- **The emblem at the centre, like a car's grille badge.** The maker's
  emblem, the **ILLUMAHEART FOUNDATION** dragon-heart (a dragon coiled
  around a heart, a sapling rising inside it), sits at the centre of the
  honeycomb, replacing the cells it covers, the way a car's badge sits in
  its grille.
- **Backlit.** The sapling's leaves glow.

#### Emblem construction

| Layer | What it is |
|---|---|
| Relief | The dragon and heart outline machined (titanium, aluminium) or die-cast and machined (servers) as one metal part in the same `#002911` finish, highlighted by diamond-cut edges that show bright metal |
| Light windows | The sapling's leaves and stem are cut through the relief and filled with a diffusing polycarbonate inlay in the emblem's leaf green |
| Light guide | An edge-lit PMMA light guide behind the inlay, fed by LEDs on its rim |
| Shield | The micro-mesh of section 6.1 continues behind the light guide, and the relief is bonded to the grille all round: the emblem is part of the cage, not a hole in it |
| Seal | The emblem is outside the armour glass, so the glass carries the IP rating unchanged |

#### Backlight rules

The backlight is window lighting, so section 6.4 applies: it is driven
only by the arbiter, never by data activity. Its states are fixed and
few: **steady** (running), **slow breath** (charging or starting, a fixed
4 s cycle), **off** (sleep, owner's choice, and the default on institutional
devices, which can take the opaque cover). Brightness follows the ambient
light sensor, and the LEDs are on their own current limit so a fault
cannot make the emblem a heat source.

#### Sizes and the vector master

| Device | Emblem width |
|---|---|
| Signet (phone) | 22 mm |
| Folio (tablet) | 34 mm |
| Courier (laptop lid) | 48 mm |
| Briefcase (vault window) | 60 mm |
| Hearth (pedestal door) | 80 mm |
| M and L shells (rack doors) | 160 mm |

The emblem is produced from a **vector master** (SVG and DXF) with three
levels of detail, because a 22 mm badge cannot carry the scales and wing
membranes a 160 mm one can:

1. **Full** (≥ 100 mm): the dragon's scales, wing ribs, the heart and the
   full sapling.
2. **Simplified** (40 to 100 mm): dragon silhouette with wing ribs, the
   heart outline, the sapling with six leaves; no scales.
3. **Badge** (< 40 mm): solid dragon and heart silhouette, the sapling as
   one stem and four leaves; every line ≥ 0.3 mm (machining and moulding
   limit) [CONFIRM with the machining vendor].

Each level is a closed-path outline for machining plus a separate layer
for the light windows. **State:** the emblem exists today as a raster
illustration with the wordmark. The vector master is a design deliverable
[R&D]; the original artwork must be committed under `hardware/brand/` so
the master can be traced from it at full resolution, and the wordmark
"ILLUMAHEART FOUNDATION" is not part of the grille badge (it goes on the
device's base plate and packaging).

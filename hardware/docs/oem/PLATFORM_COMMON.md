<!--
SPDX-FileCopyrightText: 2024-2026 Michael Laurence Curzi & 36N9 GENETICS LLC
SPDX-License-Identifier: LicenseRef-ZXV-Hardware-1.1
-->

# Shared platform specification

This document covers what every model shares: the case, the deployment
mechanism, the display canopy, the projector bay, the acoustic system, the
cooling plant, the optical window and the power system. Where a feature
differs between the institutional and consumer builds, the table names both.
Tags ([VERIFIED], [CONFIRM], [CHANGED], [R&D]) are defined in
[README.md](README.md).

## 1. Envelope and mass

| Item | Institutional (Vanguard, Sovereign, Citadel) | Consumer (Pioneer) |
|---|---|---|
| Closed exterior, target | ≤ 560 × 360 × 230 mm | ≤ 560 × 360 × 230 mm |
| Why this size | US airline carry-on limit (22 × 14 × 9 in). The 21.5" centre panel (≈ 495 × 292 mm outline) sets the length and width, so there is about 30 mm of wall and bezel on each side. **This is the governing mechanical constraint.** | Same |
| Shell | Al-Li alloy frame with aramid (Nomex-class) honeycomb panels and a conductive skin (honeycomb alone does not shield EMI) | Die-cast aluminium base, ABS/glass-fibre lid |
| Mass, estimate | 15 to 20 kg, depending on tier [R&D] | 9 to 11 kg [R&D] |
| Mass note | Many non-US airlines cap cabin bags at 7 to 10 kg. The institutional models will often travel as checked or courier freight. Say so in the sales material; do not market them as carry-on everywhere. | Same caveat, smaller margin |
| Ruggedness | MIL-STD-810H: transit drop (Method 516.8 Procedure IV), vibration (514.8), shock, humidity, salt fog, sand and dust, high and low temperature | IEC 62368-1 safety; 76 cm drop on closed case; no MIL claims |
| Ingress | IP54 for the whole closed case; IP67 for the sealed compute vault only (section 6) | IP52 closed case; vault sealed to IP5X; no IP67 claim |

## 2. Deployment kinematics

### 2.1 Latches

- Two recessed push-button draw latches, each with a secondary interlock.
  Releasing them unloads the perimeter compression seal.
- Institutional: the latch interlocks have a sensor contact that reports to
  the arbiter (tamper evidence, see [OEM_INSTITUTIONAL.md](OEM_INSTITUTIONAL.md) section 6).

### 2.2 Kickstand and intake plenum **[CHANGED]**

The brief asks for a fixed 15° rake made by raising the rear of the base 35
to 42 mm. Those two numbers do not agree. Over a base about 340 mm deep, a
40 mm rise gives tan⁻¹(40 / 340) ≈ 6.7°. A 15° rake would need about 91 mm of
rise.

Specification:

- When the lid passes 30°, a multi-link pushrod deploys two broad-stance feet
  under the rear of the base.
- Rear rise: **40 mm ± 2 mm, giving a 6° to 7° deck rake.** This is also the
  better ergonomic choice: office ergonomics guidance favours a flat or
  slightly raised keyboard. A 15° positive rake increases wrist extension.
- Optional second detent at 80 mm (about 13°) for standing use, if the
  linkage allows it without adding mass. [R&D]
- The raised rear forms the cold-air intake plenum under the chassis.
  Minimum free intake area: 6,000 mm² institutional, 3,000 mm² consumer.
- Feet: steel-reinforced glass-filled nylon with elastomer pads. The
  linkage must hold the full deck under a 50 N downward load at the front
  edge (typing load with the wrists resting) without moving.

### 2.3 Pop-up keyboard and port trench

- The keybed rides on damped phosphor-bronze scissor linkages. Opening the
  lid lifts it 18 mm and moves it 25 mm toward the user. Closing the lid
  pulls it flush. Cycle rating: 20,000 open/close cycles.
- Mechanical switches, low profile (≤ 12 mm total key stack), so that the
  keybed fits in the closed case.
- The keyboard is a USB HID device on an internal USB 2.0 link (see
  [OS_HARDWARE_INTERFACE.md](OS_HARDWARE_INTERFACE.md)). On institutional models it is
  wired to the arbiter, not to the host, so the host never sees raw keys
  until the arbiter releases them (trusted input path).
- **Port trench:** a 45 mm deep aluminium channel under the keybed. Ports by
  model are listed in each OEM package. Cables leave through rubber
  grommets at the rear. Closing the lid seals the trench to IP5X.
- Every trench port needs a bend radius allowance. An SFP+ module with a
  fibre patch needs about 30 mm behind the cage, and a Cat6A RJ45 boot about
  25 mm. The 45 mm depth is enough only if ports face rearward, toward the
  grommets, not upward.

## 3. Display canopy

### 3.1 Panels

| Position | Institutional | Consumer | Notes |
|---|---|---|---|
| Centre | 21.5" 16:9, 2560 × 1440, matte, ≥ 1000:1, 60 to 75 Hz, eDP 1.4 | 21.5" 16:9, 1920 × 1080, 144 Hz, IPS, eDP 1.4 | Use all-in-one-PC glass with a native eDP interface. Many 21.5" monitor panels use LVDS or V-by-One and would need a bridge chip. [CONFIRM] eDP availability with the panel vendor |
| Left and right wings | 16" 16:10, 1920 × 1200, IPS, matte, eDP | 15.6" 16:9 or 16" 16:10, 1920 × 1080/1200, 60 Hz, eDP | Notebook replacement glass, very high volume |
| Touch | Optional, on the centre panel only | None | |

Active-area arithmetic, for the hinge layout: the centre panel is about
477 × 268 mm. A 16" 16:10 wing is about 345 × 215 mm. **Two wings together
(690 mm) are wider than the centre panel, so when stowed they cannot lie side
by side. They stack:** the left wing folds flat against the centre panel,
and the right wing folds over the left. The right wing's hinge axis is
therefore offset outward by one panel thickness plus the gasket (about
9 mm). The OEM must design the two hinge brackets as a left-hand and a
right-hand part, not as mirror images.

### 3.2 Wing hinges

- Dual-axis constant-torque friction hinges, Nitronic 60 core. Rating:
  50,000 cycles.
- Holding torque: ≥ 2.5 N·m per wing. A 16" panel module of about 0.8 kg
  with its centre of mass 175 mm from the axis needs about 1.4 N·m, so this
  leaves a 1.8× margin.
- Swing: 180° from folded to flat panoramic; friction-held at any angle
  from flat to 30° inward wrap.
- Stowed panels are separated by micro-cellular urethane edge gaskets so
  the glass never touches glass.
- Panel cables run through the hinge barrels as flat micro-coax, rated for
  the same 50,000 cycles.

## 4. Rear projector bay **[CHANGED]**

The brief asks for a 1,200 ANSI-lumen 4K ultra-short-throw laser engine
behind the centre display. That class of engine draws about 100 to 150 W,
needs its own cooling path and is too deep for a 21.5" canopy back shell.

Specification:

| Item | Spec |
|---|---|
| Engine | Laser-phosphor DLP short-throw module, 1920 × 1080 native (4K by TI XPR pixel shifting is optional) |
| Brightness | 500 to 800 ANSI lumens target [R&D] |
| Power | ≤ 90 W, from its own switched rail. Off unless the hatch is open |
| Throw | Ultra-short-throw ratio about 0.25: a 100" (2.21 m wide) image needs the lens about 0.55 m from the wall |
| Direction | The hatch is on the back of the canopy, so the image lands on the wall **behind the machine**: the wall the user faces, partly hidden by the canopy. It suits an audience sitting across the table, or a user standing beside the case. It does not project behind the user. |
| Hatch | Spring-damped, electromechanical latch, interlocked so the laser cannot fire with the hatch closed (IEC 60825-1 laser safety and IEC 62471 photobiological safety) |
| Consumer | Bay, hatch and connector are kept. The engine is an optional plug-in module, so the base BOM does not carry it |
| OS | `kernel/src/hardware/dlp_projector.{h,c}` models a TI DLP2010-class DMD (854 × 480). A new profile for the chosen 1080p DMD is needed. |

## 5. Acoustic system

### 5.1 Dual-chamber tactile cavity ("log drum")

- Two isolated chambers in the base, moulded or machined from compressed
  flax-fibre phenolic laminate. Left chamber tuned for low-mid transients,
  right chamber for sub-bass. Each chamber vents through a tuned port on the
  chassis flank.
- One high-flux electromagnetic tactile exciter is fixed to a Ti-6Al-4V
  bridge that spans both chambers. It drives the chamber walls, so the base
  acts as the radiator.
- Amplifier: 15 W RMS class-D into the exciter.
- Acceptance targets [R&D]:
  - Tactile response from 35 Hz to 250 Hz, felt at the palm rest.
  - Airborne output of at least 80 dB SPL at 0.5 m from 80 Hz to 250 Hz.
    The exciter makes 35 Hz tactile, not loud: at 35 Hz the panel area is far
    too small to move much air.
- The exciter must be decoupled from the compute vault and the SSDs. Run a
  vibration survey on the prototype and confirm that the SSDs see no extra
  errors at full exciter drive.

### 5.2 Directional front array

- A linear array of 8 to 16 micro-drivers along the top edge of the
  keyboard deck, phase-steered at the user's head. Band: 300 Hz to 18 kHz.
- A 400 mm array is about 2.3 wavelengths long at 2 kHz, so it is
  directional above about 1 kHz. Below that it radiates almost evenly.
- Acceptance target: ≥ 10 dB front-to-side ratio from 2 kHz to 12 kHz
  at 1 m. [R&D]
- **No ultrasonic (parametric) heterodyne array in version 1.** [CHANGED]
  Parametric speakers need high ultrasonic drive levels, have exposure
  limits of their own and give weak low-mid output. Revisit after the
  conventional array is measured.

### 5.3 Leakage nulling and open-air noise cancellation **[CHANGED]**

The brief promises 18 to 24 dB of spill reduction at 1.5 m and 10 to 14 dB of
open-air ambient cancellation. Open-air cancellation only works over a zone
about a tenth of a wavelength across. That is 11 cm at 300 Hz and 2 cm at
1.7 kHz. It cannot give a broadband 18 to 24 dB in every direction at 1.5 m.
Specify these as firmware features with honest acceptance targets:

| Feature | Hardware | Acceptance target [R&D] |
|---|---|---|
| Rear and side spill nulling | 2 to 4 cancellation drivers on the rear and side flanks, fed an inverted, delayed copy by the DSP | 6 to 10 dB less spill at 1.5 m behind the case, 200 Hz to 2 kHz |
| Ambient noise cancellation | 4 MEMS reference microphones in the shell, plus 2 error microphones at the canopy edges | 6 to 10 dB less steady noise below 300 Hz, with the head within ±5 cm of a calibrated position |
| Head tracking (makes the zone follow the user) | One time-of-flight sensor in the centre bezel | Optional. Needed if the zone has to follow the head |
| DSP | Dedicated audio DSP with ≤ 1 ms acoustic-path latency (ADI SHARC class or similar) [CONFIRM] | |

Do not let marketing quote the brief's numbers.

## 6. Cooling plant **[CHANGED]**

### 6.1 What changes from the brief, and why

1. **Thermoelectric (Peltier) stages are removed from the main heat path.**
   A Bi₂Te₃ module pumping heat at a useful temperature difference has a
   coefficient of performance of about 0.5 to 1. Pumping 480 W would add
   another 480 to 960 W of heat to reject. A briefcase cannot do that, and
   "sub-ambient" cooling of the Citadel is not achievable.
2. **Solid-state micro-jets cannot carry the full load.** A Frore AirJet Mini
   removes about 5 W and an AirJet Pro about 10 W [VERIFIED, press figures,
   2023]. Forty-eight AirJet Pro modules would remove about 500 W at best,
   and would need a mounting area comparable to the whole base footprint.
   Eight modules (the consumer brief) remove about 40 to 80 W, below the
   consumer APU's 120 W ceiling.
3. **So the fans are the main high-load cooling, not an emergency system.**
   The micro-jets run the machine silently at idle and light load. The
   blowers take over above a per-tier power threshold. The arbiter's
   thresholds, limp-home mode and manual override from the brief are kept.

### 6.2 Airflow needed

Air removes heat as P = ρ · c_p · Q · ΔT. With a 15 K rise from intake to
exhaust:

| Model | Sustained heat | Airflow needed at ΔT = 15 K |
|---|---|---|
| Pioneer | 150 W | about 18 CFM |
| Vanguard | 300 W | about 35 CFM |
| Sovereign | 380 W | about 45 CFM |
| Citadel | 600 W | about 70 CFM |

A 40 × 10 mm blower moves only a few CFM. The institutional models need 2 to
4 blowers of 75 to 97 mm. The consumer model needs two 75 mm laptop-class
blowers. [R&D] Size them on the prototype against the measured fin-stack
pressure drop.

### 6.3 Plant layout

```
  SEALED COMPUTE VAULT (dry-nitrogen backfill, desiccant, IP67)
    silicon -> cold plates / vapour chamber -> heat pipes -> vault floor
  ================ conduction through vault floor ==================
  EXTERNAL WIND TUNNEL (washable)
    intake plenum (under deck) -> ePTFE membrane -> copper fin stack
    -> micro-jet array (idle / light) + blowers (medium / high)
    -> 35 deg hexagonal louvres (under kickstand)
```

- **Vault:** fluorosilicone gaskets, dry-nitrogen backfill with a desiccant
  pack, and an ePTFE pressure-equalisation vent. A fully hermetic vault
  would be stressed by every flight: cabin pressure is about 75 kPa against
  101 kPa at sea level, about 2.6 tonnes per square metre. The vent keeps
  the IP67 rating while letting the vault breathe. Institutional models
  also watch vault pressure and humidity as tamper signals.
- **Fan modes (arbiter-controlled):**

| Mode | Condition | Fans |
|---|---|---|
| Silent | Package power below the tier's silent ceiling (measured on the prototype; target: Pioneer ≤ 45 W, Vanguard idle) | 0 rpm, micro-jets only |
| Normal | Above the silent ceiling | Blowers on a temperature curve |
| Scram | Any die > 88 °C, or exhaust minus intake > 45 K, or ambient > 48 °C | Blowers at maximum |
| Limp home | A micro-jet bank open-circuit or over-current fault | Blowers locked at 50 % until service |
| Manual | Toggle in the port trench | Blowers at maximum |

- **Desert ambient:** at 50 °C ambient no air-cooled design can hold full
  power. The OS budget controller (`ct_budget`, see
  [OS_HARDWARE_INTERFACE.md](OS_HARDWARE_INTERFACE.md)) throttles to the tier's hot-ambient
  power cap, which is measured during MIL-STD-810H Method 501.7 testing.
- **Blowers:** IP68 motor windings, ceramic or fluid-dynamic bearings,
  MTBF ≥ 70,000 h at 40 °C (L10). [CONFIRM] the 100,000 h figure in the
  brief against the chosen vendor's L10 curve.

### 6.4 Exhaust louvres as an EMI barrier

- 35° hexagonal louvres, cell ≤ 5 mm across flats, louvre depth ≥ 15 mm. A
  hexagonal cell this size is a waveguide below cutoff up to roughly 30 GHz,
  and a depth of three times the cell width gives high attenuation well
  below that. This depth also adds airflow resistance. Include it in the
  fan sizing.
- ePTFE hydrophobic membrane behind the louvres, 0.45 µm pore class.

## 7. Optical window

| Layer (outside to inside) | Institutional | Consumer |
|---|---|---|
| Grille | Machined Al-Li or magnesium hexagonal grille (same cell rules as section 6.4 where it also passes air) | Die-cast aluminium grille |
| Armour | 3.0 mm sapphire, or chemically strengthened aluminosilicate glass, AR and oleophobic coated both sides | 2.0 mm chemically strengthened aluminosilicate |
| EMI layer | Embedded micro-mesh (copper/gold, pitch < 1 mm), bonded to the frame all round with a silver-filled silicone gasket. Target ≥ 50 dB shielding from 30 MHz to 6 GHz at ≥ 85 % optical transmission [R&D] | ITO coating (simpler; typically 20 to 40 dB) or mesh |
| Inside | Direct view of the cold plates, the arbiter activity LEDs and the drive sleds | Same |

**Telemetry lighting is an optical side channel.** LEDs driven directly by
bus or disk activity can leak data to a camera or photodiode some distance
away. This is a published, practical attack. Rules:

- No LED may be driven directly by a data bus, a disk or a network
  activity line.
- Vault lighting is driven only by the arbiter, from aggregated metrics
  updated no faster than 2 Hz, with randomised timing.
- Institutional models ship with vault lighting **off**, and the window
  can take a bolt-on opaque cover for TEMPEST zones.

## 8. Power system

### 8.1 Batteries

- Two locking cartridge bays at the rear of the base. Each pack is
  ≤ 98.5 Wh, which keeps each pack under the 100 Wh airline limit with no
  airline approval needed. UN 38.3 and IEC 62133-2 certified.
- **Hot swap:** ideal-diode ORing on a common DC bus. Either pack, or the
  AC input, can be removed under load without a drop-out. The arbiter reads
  both packs over SMBus (Smart Battery Data Specification).
- Consumer models ship with one pack. The second bay accepts an optional
  pack.

Runtime is short at full load. State it plainly in the sales material:

| Model | Typical load | Runtime on 2 × 98.5 Wh |
|---|---|---|
| Pioneer (one pack) | 60 W | about 1.5 h |
| Pioneer (two packs) | 60 W | about 3 h |
| Vanguard | 250 W | about 45 min |
| Sovereign | 330 W | about 35 min |
| Citadel | 500 W | about 20 min |

For the institutional models the batteries are a ride-through and
relocation supply, not a field power source. Field work runs on AC or
vehicle DC.

### 8.2 Inputs

| Input | Institutional | Consumer |
|---|---|---|
| AC mains | Neutrik powerCON TRUE1 (locking), 90 to 264 V AC, 47 to 63 Hz, internal GaN PFC supply | IEC C6 or C14, 90 to 264 V AC, internal GaN supply |
| Vehicle DC | 12 to 32 V DC on a locking circular connector, MIL-STD-1275E surge tolerant for 28 V military vehicles | 12 to 24 V DC (cigarette-plug lead, optional) |
| USB-C | USB PD 3.1 EPR, up to 240 W, in the trench. **Charges, or runs the machine at reduced power only.** No tier above the Pioneer can run full load on 240 W | Same |
| Supply rating | Per tier, in [OEM_INSTITUTIONAL.md](OEM_INSTITUTIONAL.md) | 230 W |

## 9. Shared compliance list

| Area | Standard | Institutional | Consumer |
|---|---|---|---|
| Product safety | IEC 62368-1 / UL 62368-1 | Yes | Yes |
| Laser (projector) | IEC 60825-1, IEC 62471 | Yes | When the module is fitted |
| EMC | FCC Part 15 Class B, EN 55032/55035 | Yes | Yes |
| Radio | FCC Part 15C/E, Part 22/24/27 (cellular), RED in the EU | Per radio fitted | Per radio fitted |
| Batteries | UN 38.3, IEC 62133-2 | Yes | Yes |
| Environment | MIL-STD-810H | Yes | No |
| Military EMC | MIL-STD-461G | Optional | No |
| Emanations | NATO SDIP-27 / national TEMPEST | Optional, per customer | No |
| Ingress | IEC 60529 | IP54 case, IP67 vault | IP52 |
| Materials | RoHS, REACH, WEEE | Yes | Yes |

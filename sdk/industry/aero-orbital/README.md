<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Aero-Orbital: integer geodesy and SEU-aware telemetry for ground stations

Aero-Orbital is a C11 library for SmallSat operators and ground stations.
It is a **ground-side analysis aid**: it runs on the ground, on data that
has already come down. It does three things:

- converts between WGS-84 geodetic coordinates and ECEF in integer fixed
  point, with error bounds measured against an 80-digit reference;
- carries telemetry packets of exact integer readings over the ground
  network in UBH-168 frames (`zxv_wire_*`);
- collects the redundant copies a spacecraft sends of each packet and flags
  single-event upsets. Copies that disagree, or a copy whose on-board CRC
  fails, make the packet **BOTH**: it is held, the quarantine log gets an
  entry, and a person decides. Nothing is corrected silently.

The API is [`include/zaero.h`](include/zaero.h).

## Geodesy

| Call | In | Out |
|---|---|---|
| `zao_geo_to_ecef` | latitude, longitude (nano-degrees), height (mm) | ECEF x, y, z (mm) |
| `zao_geo_to_ecef_um` | the same | ECEF x, y, z (micrometres) |
| `zao_ecef_to_geo` | ECEF (mm) | latitude, longitude (nano-degrees), height (mm) |
| `zao_ecef_to_geo_fine` | ECEF (mm) | latitude, longitude (pico-degrees), height (micrometres) |

Inside, angles are 2^-24 nano-degree words. Sine, cosine, atan2 and
hypot come from a 62-step CORDIC on Q61 words, the inverse square root from
a Newton iteration, and products use a portable 64×64→128-bit multiply.
There is no float anywhere, including the constants:
[`tools/gen_geo.py`](tools/gen_geo.py) computes them with Python's
`decimal` module at 80 digits. ECEF → geodetic iterates
φ ← atan2(Z + e²N(φ) sin φ, p) until φ moves by less than 2.4·10⁻⁴
nano-degree, which takes 6 to 9 steps.

Range: latitude ±90°, longitude ±180° (output in (-180°, +180°]), height
-10 km to +100,000 km, ECEF axes within ±110,000 km. That covers LEO,
GEO and cislunar transfer orbits. Anything outside is `ZAO_E_RANGE`.

### Measured error

The reference is [`test/geo_vectors.h`](test/geo_vectors.h): 256 forward and
256 inverse cases from the 80-digit generator. They span every latitude band
(poles and equator included), every longitude, and heights from -10 km to
GEO and beyond. `test_aero` checks every vector and prints the worst error:

| Conversion | Output unit | Worst error measured (gcc and clang) | Test bound |
|---|---|---|---|
| geodetic → ECEF | micrometre | 0 µm (matches the reference rounded to µm) | 1 µm |
| geodetic → ECEF | millimetre | 0.495 mm (correctly rounded) | 0.501 mm |
| ECEF → geodetic | pico-degree, micrometre | 0 pdeg, 0 µm | 1 pdeg, 1 µm |
| ECEF → geodetic | nano-degree, millimetre | 500 pdeg, 0.497 mm (correctly rounded) | 501 pdeg, 0.501 mm |
| geodetic → ECEF → geodetic → ECEF (1,500 random points) | millimetre | 1 mm per axis, 1 mm height | 2 mm |

One nano-degree of latitude is about 0.11 mm on the ground. A result is
only as good as its input, though. An ECEF fix given to the nearest
millimetre carries ±0.5 mm into every output.

## Telemetry over UBH-168

`zao_tm` is a canonical packet:

```
u8 version | u16 spacecraft id | u8 virtual channel | u32 seq | u64 time (ms) | u8 n |
n × (u16 channel | i64 value) | u32 CRC-32C of everything before it
```

It holds up to 32 readings, as exact integers in the channel's own unit
(mV, mK, counts). `zao_wire_encode` sends any byte string of up to 4 KB as
one header frame plus one data frame per 16 bytes. The header carries a
magic word, the length, the frame count, the CRC-32C and a SHA3-256 prefix.
Each data frame carries its index in its own word, so it can never read as
the idle pilot. The decoder refuses everything else: a wrong tag, a frame
out of order, a frame missing or extra, non-zero padding, or a checksum
mismatch.

## SEU flags

`zao_rx_create(cap, operator_key, qlog, &cfg, &rx)`. `cfg.copies` is the
number of copies the spacecraft sends (1 to 5), and `cfg.policy` is one of
the two policies below. `zao_rx_submit` takes one copy as frames. A packet
is decided when its last copy arrives, or at `zao_rx_close` at the end of a
pass:

| What arrived | State | Tag | Logged |
|---|---|---|---|
| every copy good and identical | `RELEASED` | TRUE, Manifest | no |
| good copies that disagree (an upset) | `HELD` | BOTH | yes, `SEU` |
| a copy whose on-board CRC fails | `HELD` | BOTH | yes, `CRC` |
| `MAJORITY` policy: a strict majority of good identical copies, the rest different | `CORRECTED` | TRUE, negentropic | yes, `CORRECTED`, and a signed record |
| a differing copy after release | `HELD` again | BOTH | yes, `LATE` |
| bytes that are not a packet | nothing recorded | — | yes, `UNPARSED` |

An identical copy of a decided packet is refused as a replay
(`ZIND_E_REPLAY`). A held packet keeps recording copies as evidence. The
default policy, `HOLD`, never chooses a copy itself. Even `MAJORITY` writes
the log entry and an operator-signed record before it releases anything, so
every correction can be audited.

An **analyst**, who is a person with a registered ML-DSA-87 key, resolves a
held packet. They sign `zao_review_message` under the context
`"aero-orbital/seu"`. The message binds the packet key, the SHA3-256 of
every recorded copy, the hold number and a decision:

- **ACCEPT copy i**: a copy whose CRC matched. The state becomes `ACCEPTED` (TRUE).
- **REJECT**: the packet is Forbidden from then on, and every later copy is
  `ZIND_E_FROZEN`.

A forged decision, a decision for a stale hold (a copy arrived after the
analyst looked), and a decision by an unregistered key are all refused.
`zao_decisions_export` and `zao_decisions_verify` check the whole decision
chain offline: every operator signature and every embedded analyst
signature.

## Example

```c
zao_geo st = {78229772000, 15407786000, 500000};      /* a ground station, 500 m */
zao_ecef e;
zao_geo_to_ecef(&st, &e);                             /* (1258439986, 346816116, 6222709248) mm */

zao_rx_cfg cfg = {3, ZAO_POLICY_HOLD, 1024};
zao_rx_create(cap, operator_key, qlog, &cfg, &rx);
zao_rx_submit(rx, frames, nframes, &key);             /* once per copy */
zao_rx_info(rx, key, &info);                          /* HELD (BOTH) if the copies disagree */
```

`make demo` runs [`examples/groundstation_demo.c`](examples/groundstation_demo.c).
It converts a station position and an orbit fix, then receives three
packets in three copies each. One copy of one packet has an upset, so that
packet is held and logged. An analyst then signs for the right copy, and
the decision chain is verified offline, both as written and with one bit
flipped.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/aero-orbital test     # 3,676 checks, fuzz smoke, demo, no-float check
make -C sdk/industry/aero-orbital SANITIZE=1 OUT=build/san test
make -C sdk/industry/aero-orbital fuzz FUZZ_ITERS=20000
make -C sdk/industry/aero-orbital vectors  # regenerate test/geo_vectors.h (identical output)
```

`test_aero` covers these areas:

- every reference vector, in both directions, at both resolutions;
- the poles, the equator, the antimeridian and the range limits;
- round trips;
- packet encoding and CRC-32C against the published check value;
- every single-bit flip in a packet;
- UBH-168: every single-bit flip in a 4-frame carrier, plus reordered,
  missing and extra frames, and no data frame equal to the idle pilot;
- the receiver under both policies: replays, late copies, CRC failures and
  capacity;
- analyst decisions: genuine, forged, stale, by an unregistered key, and
  rejected;
- offline verification of the decision chain, intact and tampered.

`fuzz_aero` sends random packets in random copies, against an independent
model of the hold rules. The copies may be clean, upset, CRC-broken or
replayed, and some frames are flipped in transit. Packets are closed
early, and analyst decisions are genuine or forged. Random geodetic and
ECEF points are converted and checked for round-trip and range agreement.
The output is byte-identical under gcc and clang.

## Limits

- **Not flight software.** This is a ground-side analysis aid. It runs on
  the ground on downlinked data and does not run on a spacecraft.
- **No attitude or orbit control.** It computes nothing that commands a
  vehicle. Orbit determination and propagation are outside the kit: it only
  converts a fix you already have.
- **No DO-178C, ECSS (for example ECSS-E-ST-40C or ECSS-Q-ST-80C) or other
  certification claim.** The error bounds above were measured on the test
  vectors with the compilers named. They are not a qualification.
- WGS-84 only. No datum shifts, no geoid (heights are ellipsoidal), no
  time systems, no polar motion and no ECI frames.
- SEU flagging compares the copies the spacecraft sent. If every copy
  carries the same upset, for example because the upset happened before
  the copies were made, the copies agree and pass. The on-board CRC only
  catches corruption made after it was computed.
- Analyst and operator keys are ML-DSA-87. Every decision record carries
  4,627-byte signatures, so the decision chain grows by about 9.5 KB per
  decision. The receiver is in memory (up to 65,536 packets) and is not
  thread-safe.
- Spacecraft, ground networks and CCSDS standards are named only for
  context. The packet format is this kit's own, not a CCSDS space packet,
  and no operator or agency has endorsed it.

# Harmonic wire (`kernel/src/harmonic`)

The harmonic module is integer signal processing and data structures. It
covers:

- **The frame.** A fixed 21-octet frame whose five 32-bit words alternate
  little- and big-endian.
- **The trunks.** A bank of ten audio-band carriers (111 Hz to 999 Hz in
  111 Hz steps, plus 1111 Hz) that carries those frames at 8 kHz.
- **Truth values.** Small resolvers that turn a pair of evidence rails into a
  truth value.
- **Adapters.** Converters that put host data (bytes, tensors, pixels, PCM,
  text) into frames and take it back out.
- **Routing.** A queue router and a dial-string router.

Nothing in it has a physical or metaphysical meaning beyond that. The
electrical names in the specs ("inverter", "rectifier", "dial tone",
"hyperdimensional") are names for the data operations described below.

Every file is freestanding integer C11:

- no libc, no floating point, no allocation;
- no 64-bit division (`zt_udiv64` from `tensor/zt.h` is used where one is
  needed);
- no `__int128`.

This is checked by compiling every `zt_*.c` file for `aarch64-none-elf`,
`i386-none-elf` and `powerpc-none-eabi` and running `nm -u`. No libc,
`malloc`, `memset`/`memcpy` or libgcc helper appears. The only undefined
symbols left are the module's own functions and the kernel modules it reuses
(`swarm_harmonic`, `call_lookup_init`, `sha256_*`).

The tests run three ways:

- hosted;
- as bare 32-bit i386 binaries;
- as bare **big-endian** PowerPC binaries under `qemu-ppc`.

The bare builds use no libc at all (the `ZT_HTEST_BARE` harness in
`zt_htest.h`), which proves the frame layout does not depend on the host.

## Files

| File | What it is |
|---|---|
| `zt_harmonic_wire.{h,c}` | **The one contract**: frame type, tag, truth enum, line-status enum, pack/unpack, idle pilot, resolvers (W1–W7) |
| `zt_harmonic_logic.{h,c}` | Explicit maps from the truth enum to `lpres` (lossy, documented) and to `swarm_hk` (bijection) |
| `zt_endian_mux.{h,c}` | I/Q view of the frame: S+ = I lane, S- = Q lane; dot and wedge projection; channel mask |
| `zt_trunk_bank.{h,c}` | Ten-carrier FDM bank: Goertzel receiver, DEAD/ON_HOOK/OFF_HOOK, π/4-DQPSK frames |
| `zt_harmonic_tables.h`, `gen_harmonic_tables.py` | Every constant that would otherwise need floating point (generated, checked in) |
| `zt_residual_carrier.{h,c}`, `zt_residual_tap.{h,c}` | Ten tones whose amplitudes follow the tensor engine's per-shell residuals |
| `zt_geom_router.{h,c}` | Nine queues on a 3×3 torus; transit ticks; stride traces |
| `zt_signal_data.{h,c}` | Facade of the "unified signal-data" names: typedefs and forwards only |
| `zt_metronome.{h,c}` | `swarm_harmonic`'s clock as a frame on the 555 Hz trunk |
| `zt_phi.{h,c}` | φⁿ in Q16.16 from Fibonacci integers; snap to the nearest power |
| `zt_adapter_core.{h,c}`, `zt_adapters.h`, `zt_adapter_{tensor,graphics,audio,text}.c` | The adapter house |
| `zt_dial_router.{h,c}`, `zt_e164_codes.h`, `gen_e164_codes.py` | Dial strings to trunk, virtual address and frame |
| `zt_dial_resolve.{h,c}` | Hands a parsed number to the call engine's signed DHT lookup |
| `zt_htest.h`, `test_*.c` | Tests (hosted and bare) |

## The frame (W1–W4)

```
octet 0       tag = 100*sync + 10*shell + trunk   (trunk = tag mod 10)
octets 1-4    W0  little-endian   S+ (I rail)
octets 5-8    W1  big-endian      S- (Q rail)
octets 9-12   W2  little-endian   S+
octets 13-16  W3  big-endian      S-
octets 17-20  W4  little-endian   S+
```

Each word's byte order is fixed by its position (L-B-L-B-L), so a frame has
the same octets on every host.

- **Pack and unpack** are branchless. The preprocessor picks the host order
  (`__BYTE_ORDER__`) and `__builtin_bswap32` does the swaps.
- **Struct layout.** The struct is `packed`, and `_Static_assert` checks its
  size (21), its array stride and its alignment (1).
- **This is line coding.** The pattern is public and carries no secret.

**Idle pilot (W4).** An all-zero frame has the same bytes in either byte
order, so it shows no alternation. An idle frame therefore carries
`ZT_WIRE_IDLE_PILOT` = `0x5A585631` in all five words. On the wire that is
"1VXZ" in each LE position and "ZXV1" in each BE position, so an idle line
still shows the pattern. The pilot resolves to NEUTRAL. Because of this, a
data frame whose five words all equal the pilot reads as idle (a reserved code
point).

**Truth enum.** The values are UNKNOWN 0, TRUE 1, FALSE 2, NEUTRAL 3, GLUT 4,
PARADOX 5.

**Line status.** The values are DEAD 0, ON_HOOK 1, OFF_HOOK 2. The other
specs' names (`zt_trunk_line_state_t`, `ZT_TRUNK_DEAD`, and so on) are aliases
of these, not second definitions.

**Mapping to the kernel's existing logic modules** (`zt_harmonic_logic.h`):

| Wire state | `lpres_state_t` (K3) | Loss |
|---|---|---|
| UNKNOWN | NEITHER | none |
| TRUE | TRUE | none |
| FALSE | FALSE | none |
| NEUTRAL | NEITHER | lossy: lpres has no "silent" value |
| GLUT | BOTH | none |
| PARADOX | BOTH | lossy: lpres keeps no persistence |

`swarm_hk_truth_t` has the same six states in another order, so that map is a
bijection by name. The two modules' AND operators differ:

- lpres: TRUE ∧ FALSE = FALSE
- swarm_hk: TRUE ∧ FALSE = GLUT

The harmonic module does not silently pick one.

### Resolvers (W5–W7)

**W5, correlation within one frame.** S- is read as the witness of S+. The
words are signed Q16.16:

```
dot = Σ_{k=0,1} (S+_k · S-_k) >> 16    (int64)
E±  = Σ_{k=0,1} (S±_k)² >> 16
```

The rules are applied in this order:

| Condition | Result |
|---|---|
| All five words zero | UNKNOWN |
| Idle pilot | NEUTRAL |
| One rail all zero | NEUTRAL |
| `dot > T` | TRUE |
| `dot < −T` and `4·min(E+,E−) ≥ max(E+,E−)` (balanced, opposed) | GLUT |
| `dot < −T` (one rail dominant) | FALSE |
| `\|dot\| ≤ T`, both rails active (orthogonal) | GLUT |

**W6, evidence for and against a claim.** This is lpres's rule on Q16.16
magnitudes:

| For | Against | Result |
|---|---|---|
| > T | > T | GLUT |
| > T | ≤ T | TRUE |
| ≤ T | > T | FALSE |
| ≤ T | ≤ T | UNKNOWN |

**W7, persistence.** No stateless resolver ever returns PARADOX.
`zt_wire_persist` keeps a counter for each rail or trunk (`conflict_run`):

- the third consecutive GLUT (`ZT_WIRE_PARADOX_RUN`) becomes PARADOX;
- any other state resets the counter.

## Endian mux

The endian mux is a set of thin functions over the wire:

- the I lane is S+ (W0, W2, W4);
- the Q lane is S- (W1, W3);
- the default threshold is 1/16.

The projection is two numbers, both saturated to int32 Q16.16. "Hyperdimensional"
in the spec means no more than this:

- `real_axis = I·Q` (the W5 dot product)
- `imag_axis = i0·q1 − i1·q0` (the 2-D wedge)

`zt_mux_channel_mask` reports which lanes are in use:

| Flag | Set when |
|---|---|
| `IN_PHASE` | any I word is non-zero |
| `QUADRATURE` | any Q word is non-zero |
| `CONJUGATE` | both lanes are active and `q_k = −i_k` |

## Trunk bank (B1–B6)

**Carriers.** The bank has ten carriers at fs = 8000 Hz: 111, 222, …, 999 Hz
(trunks 0–8) and 1111 Hz (trunk 9).

**Harmonics.** Nine of the ten carriers are harmonics of 111 Hz, and 1111 Hz is
1 Hz above the 10th harmonic. So any nonlinearity lands on other trunks:

- clipping a full-scale 333 Hz tone puts −19.2 dB on 999 Hz (measured);
- the second harmonic of 555 Hz is 1 Hz from trunk 9.

Keep the composite signal linear and below full scale.

**Window and block.** The spec asked for a 160-sample rectangular block, which
does not meet its own −45 dB floor. The bank uses a 320-sample block (40 ms,
25 Hz bins) with a 4-term Blackman-Harris window instead. The table shows
leakage from a full-scale 555 + 666 Hz pair into 444/777 Hz, estimated with
floating point:

| Window | Block (samples) | Leakage into 444/777 Hz |
|---|---|---|
| Rectangular | 160 (the spec) | −22.4 dB |
| Rectangular | 320 | −27.9 dB |
| Blackman-Harris | 160 | −23.5 dB |
| Blackman-Harris | 240 | −52.7 dB |
| Blackman-Harris | 320 | −96.2 dB |

Measured in the fixed-point implementation, the leakage is −80.4 dB on 444 Hz
and −80.9 dB on 777 Hz, against a −45 dB floor. A test also builds the spec's
rectangular 160-sample block and confirms it exceeds the floor.
`ZT_WINDOW_SAMPLES` (160, 20 ms) is kept as the reporting cadence, so line
states change every second report.

**Goertzel.** The receiver uses int64 state and Q14 coefficients. The spec's
int32 product overflows, because q1 reaches about 2²⁶.

**Line states**, set at the end of each block:

| State | Condition |
|---|---|
| DEAD | Power below the floor (−45 dB re full scale) |
| ON_HOOK | Phase steady: the block equals the previous one rotated by the carrier's own `e^{jωN}` |
| OFF_HOOK | Phase moved by ≥ 22.5° |

So a steady tone burst of any length reads ON_HOOK. OFF_HOOK means the carrier
is phase-modulated.

**Frames.** Modulation is π/4-DQPSK with one symbol per block, Gray coded:

| Bits | Phase step |
|---|---|
| 00 | +45° |
| 01 | +135° |
| 11 | −135° |
| 10 | −45° |

A frame is one reference block plus 84 symbols (the 168 bits, most significant
bit first). That is 85 blocks = 27,200 samples = **3.4 s per frame**, so the
rate is **50 bit/s per trunk and 500 bit/s for all ten**.

**Synchronous only.** Transmitter and receiver share the block clock, and
there is no clock recovery.

**Speed.** Analysing one second of ten-carrier audio takes about 84–133 µs on
the build host. The test's bound is 50 ms.

## Residual carrier and tap

The residual carrier is ten oscillators. Shell s of the tensor engine's
holographic code (`tensor/zt.h` T15) drives the tone of trunk s.

**The tap.** `zt.h` provides `zt_set_shell_tap`. While a tap is set,
`zt_holo_encode` adds `floor(delta/256)` per place to `acc[s]`, saturating.
`zt_residual_tap_t` matches that layout, and `_Static_assert` checks it.

**Synthesis.** For each sample:

1. **Phase.** The phase step is 16 bits, truncated (111 Hz becomes 110.96 Hz).
2. **Sine.** The sine comes from a 16-step quarter-wave table, linearly
   interpolated. Without interpolation, the table's spurs land near other
   trunks.
3. **Amplitude.** The amplitude is `clamp(|acc|, 0x400, 0x5000)`. The baseline
   (−30 dB) keeps an idle line ON_HOOK. A negative accumulator inverts the
   tone.
4. **Leak.** The accumulator leaks `acc>>10` per sample (time constant
   128 ms).
5. **Sum.** The ten tones are summed with saturation.

**Measured:**

- a 555 Hz surge reads +26 dB on the bank;
- 444 Hz and 666 Hz are unchanged (0.0 dB);
- 1,000,000 harvests take about 1.26 ms.

The synthesis costs about ten table lookups and multiplies per sample. It is
ordinary computation, not "free".

## Geometric router

The router has nine queues on a 3×3 torus. Every node is one hop from every
other (the graph is K9).

**Distances** are in Q16.16:

| Hop | Distance |
|---|---|
| Same node | 0 |
| Orthogonal | 0x10000 (1.0) |
| Diagonal | 0x16A0A (√2) |

**Transit ticks** are `max(1, ceil(distance/velocity))`. With the spec's floor
division, a diagonal hop at velocity 1 took 1 tick, the same as an orthogonal
hop, which contradicts its own "diagonal strictly slower" test. Ceiling gives
2 ticks against 1.

Ticks are whole numbers, so a diagonal is strictly slower only when the two
ceilings differ:

| Velocity | Diagonal | Orthogonal |
|---|---|---|
| 1 | 2 | 1 |
| 1/2 | 3 | 2 |
| 1/4 | 6 | 4 |
| 1/8 | 12 | 8 |
| 3/4 | 2 | 2 |
| ≥ √2 | 1 | 1 |

The tests assert the last two equalities as a documented limit.

**Other rules:**

- Velocity 0 gives `UINT32_MAX` ticks, and dispatch refuses it.
- Dispatch is all-or-nothing and rejects self-hops.
- The ordinal advances by the transit ticks, at least 1 per hop.
- `zt_geom_stride_trace` is the doubling walk n → 2n mod 9.

## Signal-data facade

The facade has no second implementation. Each name is a typedef of an
existing type, and each function forwards:

| Facade name | Is |
|---|---|
| `zt_frame_t` | the wire frame |
| `zt_paraconsistent_state_t` | the truth enum |
| `zt_dual_rail_t` | the rails |
| `zt_filter_bank_t` | the trunk bank |

**PARADOX** is reached only through `zt_core_resolve_step`, which uses the
per-rail `conflict_run` counter. `zt_core_resolve_interference` is stateless.

**Burst** means a phase-modulated (DQPSK) burst. A steady tone burst stays
ON_HOOK.

## Metronome on the 555 Hz pulse trunk

Trunk 4 (555 Hz) carries `kernel/src/swarm/swarm_harmonic`'s clock: 27,720
ticks per fundamental, with harmonic n due every 27720/n ticks. Frame layout:

| Field | Content |
|---|---|
| tag | `100·sync + 4` (sync set on a fundamental's first tick) |
| W0 | tick mod 27720 |
| W2 | `swarm_harmonics_due(tick)` |
| W4 | fundamentals so far |
| W1, W3 | repeat W0 and W2 as the witness |

`zt_metronome_read` validates a frame. At 3.4 s per frame, the metronome is a
drift reference, not a per-tick clock.

## Adapter house

The adapter house converts host buffers to frame streams ("pump_in", the
spec's inverter) and back ("drain_out", the rectifier).

### Stream format

**Header frame:**

| Field | Content |
|---|---|
| tag | sync=1, shell=0 |
| S+ W0 | payload length |
| S+ W2 | `"ZXA" \| kind` |
| S+ W4 | number of data frames |
| S- W1 | FNV-1a-32 of the payload |
| S- W3 | `~length` |

**Data frames** have tag sync=0 and shell = index mod 10.

**Claim frames** (A4) carry evidence for one proposition:

- tag: shell 9, sync 1;
- S+: evidence, proposition id, the magic `"CLM!"`;
- S-: repeats the evidence and the proposition id.

`evaluate_interference` works as follows:

- **Two claims on the same proposition** go to the W6 evidence resolver.
- **Two data frames** go to the W5 correlation resolver; the audio adapter
  uses a cancellation detector instead.
- **Either way**, the result then passes through W7 persistence.

### Truth at the rectifier

| Truth | What drain_out writes |
|---|---|
| TRUE, NEUTRAL | The payload |
| FALSE | The retraction: bytes nothing, graphics the previous image, audio silence, text nothing |
| GLUT, PARADOX | Both claims, never an average: bytes `EHELD`, graphics a checkerboard (depth 0), audio the side channel, text `"[GLUT] "` |
| UNKNOWN | Nothing |

### What each adapter carries

| Adapter | Payload per frame | Exactness |
|---|---|---|
| Bytes | 20 bytes | Bit-exact on any host |
| Graphics | 2 RGBA pixels on S+; S- = XOR with the previous image | Exact |
| Audio | 3 stereo int16 pairs on S+; S- = side (L−R)/2 | Exact |
| Text | Base-N digits | Exact |
| Tensor | 3 Q16.16 values on S+; S- = position and −sum (each frame sums to zero) | **Lossy** (see below) |

**Tensor is lossy.** The tensor path is quantisation, not invertible:

- FP32, BF16 and FP16 become Q16.16 by bit manipulation;
- rounding is half away from zero, with error ≤ 2⁻¹⁷;
- values with |x| ≥ 32768 saturate;
- NaN becomes 0 and is counted, and subnormals become 0;
- φ snapping of INT8 block scales moves a scale by up to about 24% (measured
  example: 0.1 becomes 0.0901, −9.8%).

Tiling cuts the long side of a block at round(L/φ) while it exceeds 21×21
(34 → 21 + 13).

**Audio.** The cancellation detector flags GLUT when E(a+b)·8 < E(a)+E(b).
`zt_audio_mix` then keeps the two streams apart instead of summing them to
silence. Overflow is folded back with the excess scaled by 1/φ (a wavefolder).
That adds harmonics, which on the trunk bus land on other trunks.

**Text.** `25^36 < 2^168 < 26^36`:

- a raw 21-octet block holds 36 symbols when N ≤ 25. `zt_text_pack_raw168`
  is byte-identical to `swarm_en_pack` for the repo's Enochian alphabet;
- a tagged frame has 160 payload bits, so it holds 36 symbols only for N ≤ 21,
  and 34 symbols for N = 22–25.

The brief said the Enochian alphabet has 21 symbols. In `swarm_enochian` it
has 23 letters + space + end mark = 25.

## Dial router

Dial strings look like `"101-" "-" CC NATIONAL`, for example
`101--14155550199`.

- **Bounded read.** The parser reads at most 32 bytes and never past the NUL.
- **Country code.** The CC is the unique prefix in `zt_e164_codes.h`. E.164
  country codes are prefix-free, and the generator checks that.
  - *Source.* The list comes from the ITU-T E.164 assigned-country-code list
    (the annex to the ITU Operational Bulletin). It was transcribed by hand,
    so re-check it against the current bulletin.
- **Length.** CC + national number ≤ 15 digits.
- **National split.**
  - *NANP (CC 1).* Exactly 10 digits: a 3-digit area code with first digit
    2–9, then a 7-digit subscriber number with first digit 2–9.
  - *Every other code.* National numbers have no common structure, so a
    **fixed approximate rule** is used: prefix = the first
    `max(L−12, min(3, L−7))` digits, subscriber = the remaining 7–12 digits.
    The prefix is generally *not* the real area code.
- **Trunk.** `assigned_trunk = subscriber mod 10` (computed with `zt_udiv64`),
  carrier = `ZT_TRUNK_FREQS[trunk]`, fleet line = trunk 9.
- **Line check.** `zt_dial_line_ready` reports whether that trunk is ON_HOOK on
  a real trunk-bank demux.

**Virtual coordinates.** These are deterministic virtual addresses computed by
hashing digits (SplitMix64 over CC, prefix and subscriber). They are **not
locations, not geolocation, not orbits**.

- The socket address is always in 240.0.0.0/4 (reserved, not routable), with
  a port in 49152–65535. It **must never be used as a real IP**.
- Units:

  | Field | Unit | Range |
  |---|---|---|
  | `lat_e4deg` | 1e-4° | ±900000 |
  | `lon_e4deg` | 1e-4° | −1800000 to 1799999 |
  | `ra_mdeg` | 1e-3° | 0–359999 |
  | `dec_mdeg` | 1e-3° | ±90000 |

  The spec's `_q16` names were not Q16.16.
- `alt_m` and `range_km` are always 0.

**Frame.** `zt_dial_frame_t` *is* the canonical frame, not a second layout:

| Field | Content |
|---|---|
| tag | trunk |
| W0 | subscriber bits 0–31 |
| W1 | prefix (10 bits), prefix digit count (2 bits), subscriber digit count (4 bits), subscriber bits 32–39 in bits 24–31 |
| W2, W3 | the coordinate words of the union member that matches `coord->type` |
| W4 | \|CC\| (bits 0–9), type (bits 10–11), top 20 bits of CRC-32 over octets 0–16 and W4's low 12 bits (bits 12–31) |

`zt_dial_unpack_frame` checks everything and re-derives the coordinates. All
168 single-bit errors are caught. The check is an error check, not
authentication.

**Rendezvous** (`zt_dial_resolve.h`). The DHT target is:

```
SHA-256("ZXV-DIAL-1\0+" CC NATIONAL)
```

`zt_dial_resolve_start` calls `call_lookup_init` from
`kernel/src/call/call_ice.h`. Because anyone can compute the id, it
authenticates nothing:

- a verify callback is required;
- it must check the record's signature against a key the caller already
  trusts for that number.

The answer is the record's ICE candidates. The virtual coordinates play no
part.

## Relation to existing modules

- **`kernel/src/ehop`** (endian hopping, [ENDIAN_HOPPING.md](ENDIAN_HOPPING.md)):
  - *ehop.* A keyed schedule (SHAKE256 of key, channel, epoch, sender,
    sequence) reorders bytes per frame, *inside* ChaCha20-Poly1305. Its
    security comes from the AEAD.
  - *The harmonic frame.* Its L-B-L-B-L order is fixed and public. It is line
    coding (it makes the byte order visible and host-independent) and has no
    security role.
  - *Combining them.* A harmonic frame can be an ehop payload.
- **`kernel/src/ubh`**:
  - The harmonic frame is a 21-octet UBH-168 envelope in `UBH_ENDIAN_MIXED`
    order (`ubh_endian_t` value 2, the same value as `ZT_ENDIAN_AC_MIXED` in
    the adapter house).
  - `ubh_168_header` is a different use of the same 21-octet size (magic,
    class, format ids). The harmonic frame does not reuse that header's
    fields.
- **`kernel/src/legacy/dtmf.c`**:
  - The trunk bank's receiver follows dtmf.c's integer Goertzel: Q14
    `2cos ω` coefficients from a generator script, and a table sine at 8 kHz.
  - The trunk bank changes three things:
    - int64 state, because ten simultaneous tones at full scale overflow
      int32;
    - a Blackman-Harris window, because 111 Hz spacing is far tighter than
      DTMF's row and column tones;
    - complex output for phase.
  - DTMF's row tones (697–941 Hz) sit among trunks 6–8 (770 Hz is 7 Hz from
    777 Hz), and its column tones are above 1111 Hz but inside the window's
    reach of trunk 9. Do not run DTMF signalling on the same audio path as the
    trunk bank.
- **`kernel/src/swarm/swarm_harmonic`**: its 27,720-tick fundamental and due
  harmonics are carried by the metronome on the 555 Hz pulse trunk. The
  residual carrier's `master_tick` uses the same modulus.
- **`kernel/src/tensor`**:
  - *Residual tap.* The tap (T15) feeds the residual carrier.
  - *zt.h T16 frames* have the same 21 octets, and at phase 0 octets 1–20 are
    laid out like this frame (`test_residual_tap.c` checks this).
  - *The T16 tag differs.* It is `phase | count<<1 | check4<<4`, not
    `100·sync + 10·shell + trunk`, and the phase alternates from frame to
    frame. A T16 frame is not a harmonic frame, and the reverse is also true.
  - *Name collision, now resolved.* zt.h's T14 truth enumerators used to be
    called `ZT_TRUTH_*` with other values, which collided with this module.
    zt.h now names them `ZT_COIL_*`, and both headers can share a translation
    unit.

## Deviations from the specs, with reasons

1. **Receiver block.** The spec's 160-sample rectangular block leaks about
   −22 dB, against its own −45 dB floor. The bank uses a 320-sample
   Blackman-Harris block (measured −80 dB) and keeps 160 as the reporting
   cadence.
2. **Goertzel state.** The state is int64, not int32, which overflows.
3. **Line states.** OFF_HOOK requires phase modulation, so a steady burst is
   ON_HOOK. Trunk data is π/4-DQPSK at 50 bit/s per trunk, 3.4 s per frame.
4. **`zt_trunk_mux_transmit_frame` returns `bool`.** It rejects a buffer that
   is too short and an invalid trunk.
5. **Phase offsets** are 32-bit binary angles, not `phase_offset_deg`.
6. **Sine tables** are linearly interpolated.
7. **Geometric router.** It uses ceiling division for transit ticks, not
   floor. The whole-tick limits at velocity 3/4 and ≥ √2 are documented and
   tested.
8. **Adapter signatures.** `pump_in` gains `max_frames` and `drain_out` gains
   `n_frames`, because the spec's signatures had no destination size or frame
   count.
9. **W5 additions.** A silent rail resolves to NEUTRAL, and balanced
   opposition (dot < −T with comparable energies) resolves to GLUT rather than
   FALSE.
10. **PARADOX** is reached only by persistence: three consecutive GLUTs.
11. **Text.** Tagged frames hold 34 symbols for base 25, not 36. Enochian has
    25 symbols, not 21.
12. **Audio fold.** Overflow is folded with the excess scaled by 1/φ, not
    clipped. This adds harmonics, as documented.
13. **Tensor path.** The FP→Q16.16 conversion and φ snapping are lossy, as
    stated.
14. **Dial router.**
    - *Unit names.* The `_q16` fields were renamed by unit.
    - *Coordinate hashing.* Coordinates come from a 64-bit mix, not the
      spec's raw shifts. With the raw shifts, a 7-digit subscriber had
      latitude within 0.02° of −90°, and every declination (taken from a
      prefix ≤ 999) sat near −90°.
    - *Socket range.* The virtual socket address is forced into 240.0.0.0/4
      and the port into 49152–65535.
    - *Packing.* W2/W3 are packed from the union member that matches the
      type.
    - *Check value.* W4 carries a real 20-bit CRC check next to the CC.
    - *Digit counts.* The descriptor gains `prefix_digits` and
      `subscriber_digits`, so leading zeros survive a round trip.
    - *Carrier table.* The carrier table is the trunk bank's `ZT_TRUNK_FREQS`,
      not a second `static const` copy in the header.
15. **Idle pilot.** Idle frames carry the non-zero pilot word, not zeros, so
    the alternation stays visible.

## Tests

Test counts (hosted / bare i386 / bare big-endian PowerPC):

| Test | Hosted | Bare i386 | Bare PowerPC (BE) |
|---|---|---|---|
| `test_harmonic_wire` | 61 | 61 | 61 |
| `test_endian_mux` | 14 | 14 | 14 |
| `test_trunk_bank` | 25 | 24 | 24 |
| `test_residual_carrier` | 14 | 13 | 13 |
| `test_geom_router` | 19 | 19 | 19 |
| `test_signal_data` | 11 | 11 | 11 |
| `test_metronome` | 6 | 6 | 6 |
| `test_adapter_house` | 60 | 60 | 60 |
| `test_dial_router` | 47 | 46 | 46 |
| `test_residual_tap` | 8 | — | — |

- **Timing checks** run hosted only, so the bare trunk-bank and
  residual-carrier counts are one lower.
- **The dial router's guard-page check** (a PROT_NONE page right after each
  fuzz string's NUL, so any overread faults) is hosted only.
- **`test_residual_tap`** links the tensor engine, which needs libm, so it
  runs hosted only.
- **`test_dial_router` fuzzing** parses 200,000 random strings and accepts
  3,921 of them. Every accepted string formats back to itself.

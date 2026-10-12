<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Why the harmonic wire splits messages into lanes

The reference page, [`HARMONIC_WIRE.md`](../HARMONIC_WIRE.md), says what the
code in `kernel/src/harmonic/` does. This page says why it is built that way,
and how literally to read its names ("trunks", "111 Hz", "torus", "dial
tone").

## For the general reader

A message on the harmonic wire is cut into small, fixed-size pieces called
frames. Each frame is 21 bytes. Inside a frame there are two lanes. The
first lane carries the data. The second lane carries a "witness": a value
that should agree with the first lane, or a related check value. When the
two lanes agree, the receiver accepts the frame. When they clearly disagree,
the receiver does not guess which one is right. It marks the frame as a
contradiction and keeps both.

The two lanes are also written in opposite byte orders (some computers store
numbers "big end first", others "little end first"). Because the order is
fixed by position, a frame has exactly the same bytes on every kind of
machine, and a person looking at raw bytes can see the pattern.

Frames can also travel as sound. The system has ten "trunks", like ten radio
stations on one dial, each with its own tone: 111, 222, ... 999 Hz and
1111 Hz. Several messages can share one audio signal, each on its own tone,
and a receiver picks them apart. This is slow (about 50 bits per second per
tone), so it is a test bed and a fallback, not a fast network.

## Are the "Hz" values real frequencies?

Yes, within the code's own sampled signals, and no further. The trunk bank
(`kernel/src/harmonic/zt_trunk_bank.h`) synthesises 16-bit integer PCM at
8000 samples per second, and each trunk is a sine tone at its stated
frequency. The receiver measures those tones with a Goertzel filter. So
inside the module, "555 Hz" means a real 555 Hz tone in an 8 kHz sample
stream. Two caveats:

- The residual carrier (`zt_residual_carrier.h`) uses a 16-bit phase step,
  so its 111 Hz tone is actually about 110.96 Hz. The code says so.
- Nothing in the repository connects these sample streams to a speaker, a
  microphone or a radio. The tests generate and decode the samples in
  memory. The trunk number is also used as a plain logical label: in the
  frame tag (`trunk = tag mod 10`), in the dial router, and in the trunk
  names (`ZT_TRK_SUPERVISOR` ... `ZT_TRK_FLEET`).

There is no radio-frequency use, and the reference page states that the
electrical names ("inverter", "rectifier", "dial tone") are names for data
operations, with no physical or metaphysical meaning beyond that.

## For the academic reader

- **I/Q lanes.** The frame's S+ words (W0, W2, W4) are called the I lane and
  the S- words (W1, W3) the Q lane (`zt_endian_mux.h`). This borrows the
  vocabulary of quadrature modulation, but inside a frame there is no
  carrier: the "projection" is a Q16.16 dot product (the parallel part) and
  a 2-D wedge product (the perpendicular part). It is a two-channel vector
  comparison, not I/Q modulation.
- **Modulation that is real.** The trunk bank is frequency-division
  multiplexing of ten carriers with pi/4-DQPSK, one symbol per 320-sample
  block, Gray coded, synchronous (shared block clock, no clock recovery).
  The receiver uses a 4-term Blackman-Harris window because the
  specification's 160-sample rectangular block did not meet its own leakage
  floor; measured figures are in the reference page.
- **Harmonic spacing is a liability, and the code says so.** Nine carriers
  are harmonics of 111 Hz, so any nonlinearity (clipping, saturation, the
  audio wavefolder) moves energy onto other trunks. The documented rule is
  to keep the composite linear and below full scale.
- **Truth values.** The resolvers map lane agreement to a six-state
  enumeration (UNKNOWN, TRUE, FALSE, NEUTRAL, GLUT, PARADOX), in the family
  of paraconsistent and four-valued (Belnap-style) logics. PARADOX is a GLUT
  seen three times in a row on the same counter; no stateless call returns
  it.
- **Erasure coding is elsewhere.** The frame's witness lane is redundancy
  and a consistency check, not an erasure code. The repository's erasure
  code over the same 21-octet frame size is `kernel/src/freight/freight.h`:
  a systematic Reed-Solomon code over GF(2^8) with a Cauchy parity matrix,
  where any k of 168 rows rebuild the payload. For calls,
  `kernel/src/call/call_fec.h` uses single XOR parity.
- **The "torus" is small.** `zt_geom_router.h` places nine queues on a 3x3
  torus. With wrap-around on a 3x3 grid every node is one hop from every
  other, so the graph is the complete graph K9. Unlike the large torus
  interconnects of parallel machines, there is no multi-hop routing to
  study here; the router's content is integer transit times (orthogonal vs.
  diagonal distance, ceiling division), all-or-nothing dispatch along fixed
  "stride" walks (n -> 2n mod 9), and a monotonic ordinal.
- **Residual carrier.** Ten oscillators whose amplitudes follow the tensor
  engine's per-shell residuals, a form of sonification. The header states
  plainly that this is ordinary computation (about ten table lookups and
  multiplies per sample), not "free".

## What actually exists

| Piece | Where | Status |
|---|---|---|
| 21-octet frame, tag, resolvers | `kernel/src/harmonic/zt_harmonic_wire.h` | Also compiled into libzxv (`sdk/libzxv/Makefile`) |
| I/Q view and channel mask | `kernel/src/harmonic/zt_endian_mux.h` | Host and bare tests |
| Ten-trunk FDM bank | `kernel/src/harmonic/zt_trunk_bank.h` | Host and bare tests |
| Residual carrier and tap | `kernel/src/harmonic/zt_residual_carrier.h`, `zt_residual_tap.h` | Host tests; the tap test links the tensor engine |
| 3x3 torus router | `kernel/src/harmonic/zt_geom_router.h` | Host and bare tests |
| Constants without floats | `kernel/src/harmonic/gen_harmonic_tables.py` | Generated tables, checked in |

The tests run hosted, as bare 32-bit i386 binaries and as bare big-endian
PowerPC binaries under QEMU (`make -C kernel` harmonic targets), which is how
host-independence of the byte layout is checked. All code is freestanding
integer C11 with no floating point.

## Limits

- **Slow.** 3.4 s per frame on one trunk; 50 bit/s per trunk and 500 bit/s
  for all ten. It is not a data network.
- **Not connected to physical audio or radio** anywhere in the repository.
  The trunk bank, residual carrier and router are tested modules; only the
  frame code is part of libzxv.
- **No security role.** The L-B-L-B-L byte order is public line coding. For
  keyed byte-order hopping inside an AEAD, see
  [`ENDIAN_HOPPING.md`](../ENDIAN_HOPPING.md).
- **Do not share the audio path with DTMF.** DTMF row tones sit among trunks
  6 to 8 (`kernel/src/legacy/dtmf.c`).
- **Lossy paths are labelled.** The tensor adapter quantises to Q16.16, and
  the dial router's virtual coordinates are hashes, not locations.

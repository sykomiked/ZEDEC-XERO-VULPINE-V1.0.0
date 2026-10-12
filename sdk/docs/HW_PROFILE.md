<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZHP-1: the ZXV hardware profile

A ZXV stack has two halves. The OS image says what the software is. A
**hardware profile** says what the machine is: a tree of abstract
components, each with integer capability descriptors and an optional state
blob. It is the "hardware ROM" that sits beside the OS image, and the
[emulation matrix](HW_EMULATION.md) reads it to decide what runs natively
and what gets emulated.

This page is the normative format. The reference implementation is
[`kernel/src/hwemu/hwp.c`](../../kernel/src/hwemu/hwp.c); the C ABI calls
are `zxv_hw_profile_*` (libzxv 1.4).

## Design goals

1. **One machine, one encoding, one digest.** Canonical rules (P1-P3)
   make the SHA-256 of the encoding the profile's identity, so a profile
   can be signed, pinned and compared byte for byte.
2. **Abstract, not revealing.** A profile names component *classes* and
   *minimum capabilities*. It never describes how a part is built. An
   implementer can publish a profile for proprietary hardware that tells
   the OS exactly what it needs and nothing about the design.
3. **Forward compatible.** Unknown non-critical capabilities are carried
   and ignored; private class and capability ranges let an implementer add
   their own without coordination.
4. **Integers only.** No floating point anywhere in the format.

## Wire format

All integers are little-endian.

```
header (16 bytes)
  0  4  magic "ZHP1"
  4  2  format = 1
  6  2  flags = 0 (reserved)
  8  2  node count (<= 64 in this implementation)
 10  2  reserved = 0
 12  4  total length in bytes, header included

node (repeated `node count` times)
  0  2  id (0..0xFFFE)
  2  2  parent id, or 0xFFFF for a root
  4  4  class
  8  2  cap count (<= 16)
 10  2  state length (<= 256)
 12  2  extension length (<= 256)
 14  2  reserved = 0
 16  .. cap count x { u16 key, u64 value }
 ..  .. state bytes
 ..  .. extension bytes
```

The **extension** is an opaque blob an implementer may attach to any node
(`zxv_hw_profile_ext_set`). It is part of the encoding, so the profile
digest covers it, but its format is never specified here and readers must
not interpret it. It is the place for whatever a vendor needs to keep with
the node without making it part of the public standard.

### Canonical rules

- **P1** Node ids strictly increase. A node's parent has a lower id than
  the node (or is 0xFFFF). The tree therefore has no cycles, and parents
  always appear before their children.
- **P2** Within a node, cap keys strictly increase, and no two keys share
  an id (bits 12-0).
- **P3** Reserved fields are zero, and the total length is exact: no
  trailing bytes, no truncation.

A decoder must refuse anything that breaks P1-P3.

## Component classes

Each class has a number (used on the wire) and a dotted name (used in
reports and tools).

| Value | Name | Value | Name |
|---|---|---|---|
| 1 | device (a whole machine; may nest) | 18 | actuator |
| 2 | cluster | 19 | clock (deterministic phase-tick source) |
| 3 | core | 20 | power |
| 4 | mem.cache | 21 | secure.element |
| 5 | mem | 22 | io.display |
| 6 | interconnect | 23 | io.audio |
| 7 | engine.tensor.int (integer MAC arrays) | 24 | io.wire (UBH-168 harmonic-wire port) |
| 8 | engine.vector | 25 | engine.gpu |
| 9 | engine.ternary (trit logic) | 26 | package (grouping, like device) |
| 10 | engine.hash | 27 | lane |
| 11 | engine.crypto | 28 | engine.logic64 (64-lane four-valued logic) |
| 12 | engine.codec | 29 | engine.ntt |
| 13 | fabric.reconfig | 30 | engine.frame.ubh168 |
| 14 | io.storage | 31 | engine.gcd |
| 15 | io.network | 32 | mem.channel |
| 16 | radio | 33 | secure.island (attest, seal and update-verify only) |
| 17 | sensor | 34 | io |
| | | ≥ 0x80000000 | private (implementer-defined) |

A `device` node can appear below another node. A soft core loaded into a
reconfigurable region, or a peer on the mesh, is described as a nested
device with the same format: the description is self-similar at every
scale.

## Capability keys

```
bit 15      CRITICAL
bits 14-13  comparison: 0 MIN (have >= need), 1 EXACT, 2 MASK (all needed
            bits present), 3 INFO (never compared)
bits 12-0   id; 0x1000-0x1FFF are private
```

| Key | Name | Meaning |
|---|---|---|
| 0x0001 | WORD_BITS | native integer width (MIN) |
| 0x0002 | COUNT | cores, lanes or units (MIN) |
| 0x6003 | FREQ_KHZ | clock (INFO) |
| 0x0004 | BYTES | memory, cache or storage size (MIN) |
| 0x6005 | LINE_BYTES | cache line (INFO) |
| 0x2006 | ISA | 1 x86-64, 2 arm64, 3 riscv64, 4 wasm32 (EXACT) |
| 0x4007 | INT_WIDTHS | bit 0 int4, 1 int8, 2 int16, 3 int32, 4 int64, 5 trit (MASK) |
| 0x2008 | RADIX | 2 binary, 3 ternary (EXACT) |
| 0x0009 | MACS_PER_CYCLE | multiply-accumulates per cycle (MIN) |
| 0x400A | SIMD | SSE4.2, AVX2, AVX-512, VNNI, NEON, DOTPROD, I8MM, SVE, RVV (MASK) |
| 0x600B | TICK_NS | deterministic phase-tick period (INFO) |
| 0x000C | REGIONS | reconfigurable regions (MIN) |
| 0x000D | BANDWIDTH_MBPS | (MIN) |
| 0x600E | LATENCY_NS | (INFO) |
| 0x400F | PQ_ALGS | bit 0 ML-KEM, 1 ML-DSA, 2 SLH-DSA (MASK) |
| 0x4010 | HASH_ALGS | bit 0 SHA-256, 1 SHA-3 (MASK) |
| 0x2011 | WIRE_BITS | frame width, 168 for UBH-168 (EXACT) |
| 0x6012 | PEAK_OPS_MHZ | peak integer ops per microsecond, for utilisation reports (INFO) |
| 0x2013 | CONTRACT_ID | the standard engine contract the node implements, a function id such as SHA256 `0x5A450001` (EXACT) |
| 0x0014 | CONTRACT_VER | the golden-vector version it passes (MIN) |
| 0x6015 | KIND | what serves it: 0 HARD, 1 FABRIC, 2 MICRO, 3 PEER, 4 SOFT, 5 REMOTE (INFO) |
| 0x0016 | CAPACITY_CLASS | coarse capacity, 0..15 (MIN) |
| 0x0017 | PERF_BUCKET | coarse performance, 0..15 (MIN) |
| 0x6018 | POWER_BUCKET | power class: 1 ≤10 mW, 2 ≤100 mW, 3 ≤1 W, 4 ≤10 W, 5 ≤100 W, 6 above, 0 unknown (INFO) |

Capacity, performance and power are deliberately coarse buckets. A profile
says which contract a component meets and roughly how big it is, never how
it is built or how it performs cycle by cycle.

**Critical caps.** A requirement whose critical cap the reader does not
know cannot be satisfied. A host component that carries a critical cap the
reader does not know is not used natively ("do not use me blind"). An
implementer's private caps should therefore be non-critical unless the OS
really must understand them.

## Matching a target against a host

A **target** profile describes the machine software is written for; a
**host** profile describes the machine it runs on. For each target node in
id order, the first unused host node of the same class whose capabilities
satisfy every non-INFO requirement is paired with it (**native**).
Unpaired target nodes are **emulated** by the matrix, and the report says
which capability fell short. `device` and `package` nodes are grouping
only. The result
is deterministic: the same two profiles always pair the same way.

## The reference machine R1 (VULPINE-class)

R1 is the default target and the public contract hardware can be built
to. It asks for minimums only, and the engine nodes name the standard
engine contract they must pass:

| Node | Parent | Class | Requirements |
|---|---|---|---|
| 1 | | device | |
| 2 | 1 | package | |
| 3 | 2 | cluster | WORD_BITS ≥ 64, COUNT ≥ 1 |
| 4 | 2 | mem | BYTES ≥ 64 MiB |
| 5 | 2 | engine.vector | INT_WIDTHS ⊇ {int8, int16, int32} |
| 6 | 2 | engine.tensor.int | INT_WIDTHS ⊇ {int8, int32}, MACS_PER_CYCLE ≥ 64, contract TENSOR v1 |
| 7 | 2 | engine.logic64 | contract LOGIC64 v1 |
| 8 | 2 | engine.ternary | RADIX = 3 |
| 9 | 2 | engine.hash | HASH_ALGS ⊇ {SHA-256}, contract SHA256 v1 |
| 10 | 2 | engine.hash | HASH_ALGS ⊇ {SHA-3}, contract KECCAK v1 |
| 11 | 2 | engine.gcd | contract GCD v1 |
| 12 | 2 | engine.crypto | PQ_ALGS ⊇ {ML-KEM, ML-DSA, SLH-DSA}, contract MAC v1 |
| 13 | 2 | fabric.reconfig | REGIONS ≥ 1 |
| 14 | 2 | clock | (any deterministic tick source) |
| 15 | 2 | secure.island | |
| 16 | 2 | io.wire | WIRE_BITS = 168 |

A commodity laptop satisfies the cluster, memory and vector nodes natively
and emulates the rest. A machine whose own profile satisfies all of R1 runs
ZXV software with nothing emulated, and needs no change to that software.

### Presenting R1 on any host

`zxv_hw_profile_present(host, target)` returns a copy of the target with a
KIND cap on every non-grouping node: HARD where the host has a matching
component, SOFT where the matrix emulates it. This is the default view ZXV
gives software: a VULPINE-class machine, each component filled by the best
implementation available here.

## Host detection

`zxv_hw_profile_probe_host` fills in only what the operating system
reports reliably: the CPU cluster (word size, core count, ISA), memory
size, cache line, and the SIMD extensions the CPU advertises. Units that
cannot be detected portably (matrix engines behind private drivers,
secure enclaves) are left out and therefore emulated. Nothing is guessed.

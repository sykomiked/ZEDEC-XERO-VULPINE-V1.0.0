<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Babel wire format, version 1

This is the byte layout of a Babel packet: what one runtime sends another
when it hands over a value, a compiled program, a call or an answer. It is a
specification for review. The packet framing is not built yet; the two
formats it carries are, and this document reuses them rather than inventing
new ones:

- **Values** are the Babel value's canonical encoding (`bv_encode`,
  kernel/src/babel/bv.h, built and tested).
- **Programs** are the Lingua IR that `zl_pack` already writes as zlir text
  (kernel/src/lingua/zl_pack.c), here in a fixed-width binary layout with the
  same fields, the same order and the same `zl_prog_digest`.

Rosetta is the human side (readable declarations and explanations); it
compiles to what is described here. docs/BABEL.md gives the overall design.

## 1. Rules for every field

- Big-endian throughout, matching `bv_encode`.
- Every scalar field has a fixed width: u8, u16, u32, u64, or i32 and i64 in
  two's complement. There are no variable-length integers.
- Anything variable in size (a value, a string, a list of instructions) is
  preceded by its count or length as a u32.
- No floats anywhere. Ratios are exact (`num/den`).
- Reserved bytes and flags are zero in version 1. A receiver refuses a
  packet in which they are not.
- There is exactly one valid encoding of each packet. A receiver checks
  every rule and refuses anything else, never repairs it.

## 2. Packet

```
offset  size  field
0       32    header
32      len   payload (layout depends on type)
32+len  32    hash: SHA3-256 of bytes 0 .. 32+len-1 (header and payload)
```

The whole packet is `64 + len` bytes.

### Header (32 bytes)

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | magic | ASCII `BABL` (42 41 42 4C) |
| 4 | 1 | version | 1 |
| 5 | 1 | type | section 3 |
| 6 | 1 | flags | 0 (reserved) |
| 7 | 1 | hash kind | 1 = SHA3-256 (kernel/src/mlkem/keccak.h); nothing else in version 1 |
| 8 | 4 | len | payload length in bytes, at most 1 048 576 |
| 12 | 8 | sequence | the sender's counter, one more than its previous packet |
| 20 | 4 | source | runtime id of the sender (section 5) |
| 24 | 4 | target | runtime id of the intended receiver, 0 for any |
| 28 | 4 | reserved | 0 |

The hash detects damage. It does not show who sent the packet: a packet
that crosses a trust boundary travels inside a signed envelope of the
post-quantum matrix (docs/PQ_MATRIX.md), which signs the whole packet,
hash included.

## 3. Types

| Type | Name | Payload |
|---|---|---|
| 1 | VALUE | one Babel value |
| 2 | PROGRAM | one Lingua program |
| 3 | CALL | run a program on an argument value |
| 4 | ANSWER | the outcome of one CALL |
| 5 | WITNESS | two answers to the same call, compared |
| 6 | REFUSAL | why a received packet was refused |

Any other type number is refused.

### 3.1 VALUE

The payload is exactly the canonical encoding of one Babel value, as
`bv_encode` writes it and `bv_decode` accepts it. That encoding is:

| Kind byte | Kind | Followed by |
|---|---|---|
| 0 | nothing | no more bytes |
| 1 | integer | i64 |
| 2 | ratio | i64 numerator, i64 denominator (> 1, lowest terms) |
| 3 | truth | u8: 0 NEITHER, 1 TRUE, 2 FALSE, 3 BOTH |
| 4 | bytes | u32 length, the bytes |
| 5 | text | u32 length, valid UTF-8 |
| 6 | symbol | u32 length (> 0), valid UTF-8 |
| 7 | list | u32 count, each value |
| 8 | record | u32 count, then each field as a symbol value and a value; names distinct |
| 9 | pair | the head value, then the tail value (not a list) |

Nesting is at most 64 deep. Two values are equal exactly when their
encodings are equal, so a receiver compares values by comparing bytes.

### 3.2 PROGRAM

The binary form of a zlir file. The fields and their order are the zlir
text's; only the spelling differs.

```
u16  lang      stable Lingua language number (zl_langs.def, 1..)
u8   bits      32, 62 or 64
u8   reserved  0
u32  slots     0 .. 4096
u32  ncode     1 .. 8192
u32  nstr      0 .. 256
ncode instructions, 20 bytes each:
    u8   op        Lingua opcode (zl.h order; zl_op_name spells it)
    u8   reserved  0, 0, 0
    i32  b
    i64  a
    u32  line      source line, for traps; not part of the digest
nstr strings:
    u32  length    total of all strings at most 8192
    the bytes
u64  digest    zl_prog_digest of the program (FNV-1a 64)
```

A receiver rebuilds the program, recomputes `zl_prog_digest`, and runs
`zl_verify`. If the digest differs or the program does not verify, the
packet is refused, exactly as `zl_unpack` refuses text. The digest names the
program's IR; the packet hash protects the bytes in transit.

The **program id** used by CALL is the SHA3-256 of a PROGRAM payload.

### 3.3 CALL

```
32 bytes  program id     a PROGRAM the receiver already holds
u64       call id        chosen by the caller, unique per source
u64       fuel           the most instructions the run may take (> 0)
u32       word size      32, 62 or 64: the width the caller expects back
u32       reserved       0
          argument       one Babel value (normally a list)
```

The receiver turns the argument into the program's input through the
language's adapter (docs/BABEL.md section 3). An argument that does not fit
is refused by name, never truncated.

### 3.4 ANSWER

```
u64  call id
i32  status       ZL_OK, or the Lingua trap (ZL_E_*) that stopped the run
u32  trap pc      0 when status is ZL_OK
u32  trap line    0 when status is ZL_OK
u32  reserved     0
u64  fuel used
     result       one Babel value: what the program answered (nothing when it trapped)
```

### 3.5 WITNESS

The same call run on two runtimes, compared:

```
u64  call id
u8   truth        TRUE when the two answers are equal, BOTH when they
                  differ, NEITHER when one side has no answer (bv_witness)
u8   reserved     0, 0, 0
u32  length of the first ANSWER payload,  then that payload
u32  length of the second ANSWER payload, then that payload
```

A WITNESS whose truth is not TRUE holds the call: the caller acts on
neither answer until a person or a policy decides.

### 3.6 REFUSAL

```
u64  sequence     the refused packet's sequence number
i32  status       the BV_E_* or ZL_E_* status
u32  offset       the byte in that packet where checking stopped
u32  length, then a UTF-8 reason naming what was refused
```

## 4. Receiving a packet

In this order, stopping at the first failure:

1. At least 64 bytes; magic, version, flags, hash kind and reserved bytes
   as above; `len` within the limit and equal to the bytes that follow
   minus 32.
2. The hash matches.
3. The sequence is greater than the last one accepted from that source.
4. The type is known, and the payload is canonical for it, consuming
   exactly `len` bytes.
5. Only then is the content used.

A failure is answered with a REFUSAL where a reply is possible, and
otherwise dropped. A receiver never acts on a part of a packet.

## 5. Runtime ids

A u32: the high byte is the family, the low three bytes the family's own
stable number.

| Family | Byte | Low 24 bits |
|---|---|---|
| Lingua | 0x01 | the language number in zl_langs.def (FORTRAN 1, BASIC 2, Forth 3, ...) |
| Pillars | 0x02 | 1 Sutra, 2 Trebago, 3 Eve, 4 KRON, 5 MYRA |
| SDK languages | 0x03 | assigned by the SDK program |
| Z-Wasm | 0x04 | 0 |

Only the Lingua numbers are fixed today; the pillar numbers are proposed. The others will be added to
this table when their adapters are built (docs/BABEL.md section 7).

## 6. Example

FORTRAN (0x01000001) sends the integer 7 to Forth (0x01000003) as its first
packet.

```
header   42 41 42 4c 01 01 00 01 00 00 00 09 00 00 00 00
         00 00 00 01 01 00 00 01 01 00 00 03 00 00 00 00
payload  01 00 00 00 00 00 00 00 07
hash     c87e6957099a1e4b6ad19abeb0abb2b5
         e50a21dd1033bda7fcf7ae710d92f3a9
```

That is 73 bytes in all.

## 7. Open points for review

- The payload limit of 1 MiB is a first choice. A Babel value arena holds
  at most 64 KiB of bytes, and the largest program is about 170 KiB.
- Sequence numbers make replay visible, but restarting a sender needs a
  rule (a new session id, or a persisted counter).
- CALL names programs the receiver already holds. Shipping a program and
  calling it in one exchange is two packets.
- Streams (one value spread over several packets) are not in version 1.

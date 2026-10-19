<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Wire frames (UBH-168)

**Who this is for:** developers writing a network service, gateway or
device firmware that sends or receives ZXV frames.
**What you will be able to do:** build, parse and check 21-byte UBH-168
frames, byte for byte the same in every language, with or without the C
library.

## The frame

A **UBH-168** frame is 21 bytes (168 bits): one tag byte and five 32-bit
words.

| Bytes | Field | Byte order |
|---|---|---|
| 0 | tag = 100 × sync + 10 × shell + trunk | — |
| 1–4 | W0, first S+ word | little-endian |
| 5–8 | W1, first S− word | big-endian |
| 9–12 | W2, second S+ word | little-endian |
| 13–16 | W3, second S− word | big-endian |
| 17–20 | W4, third S+ word | little-endian |

**Little-endian** means the least significant byte comes first;
**big-endian**, the most significant. The order alternates on purpose, and
it is fixed: the bytes are the same on every machine.

`trunk` and `shell` are 0 to 9 and `sync` is 0 or 1, so the tag is 0 to 199.
A tag of 200 or more is refused (`ZXV_E_ARG` in C, `ValueError` in Python).

## Building and parsing

The Python SDK does this in pure Python, so a service can frame traffic
without loading `libzxv`. Other network-facing SDKs do the same, and each is
tested byte for byte against the C library.

```python
import zxv

f = zxv.Ubh168Frame.build(trunk=3, shell=2, sync=1,
                          s_plus=(0x01020304, 0x090A0B0C, 0x15161718),
                          s_minus=(0x05060708, 0x11121314))
b = f.to_bytes()
f.tag, b.hex(), len(b)
# (123, '7b04030201050607080c0b0a091112131418171615', 21)

g = zxv.Ubh168Frame.from_bytes(b)
g == f, g.trunk, g.shell, g.sync           # (True, 3, 2, 1)
f.to_bytes_native() == b                   # True: the C library agrees
```

Read the hex above against the table: `7b` is tag 123, `04030201` is
0x01020304 little-endian, `05060708` is 0x05060708 big-endian, and so on.

The same frame in C:

```c
uint32_t sp[3] = {0x01020304, 0x090A0B0C, 0x15161718}, sm[2] = {0x05060708, 0x11121314};
uint8_t tag, frame[ZXV_WIRE_FRAME_BYTES];
int32_t truth;
zxv_wire_tag(3, 2, 1, &tag);               /* 123 */
zxv_wire_pack(tag, sp, sm, frame);
zxv_wire_resolve(frame, &truth);           /* 1, TRUE */
```

## Idle frames

When a trunk has nothing to send, it sends an **idle** frame: every word is
the pilot value `0x5A585631`. An idle frame is never all zeros, so a dead
line (all zeros) and a quiet line look different.

```python
idle = zxv.Ubh168Frame.idle(3)
idle.is_idle                               # True
```

## Reading a frame's truth

`resolve()` compares the S+ words with the S− words and returns a
`WireTruth`:

| `WireTruth` | When |
|---|---|
| `UNKNOWN` | every word is zero |
| `NEUTRAL` | an idle frame, or only one side has data |
| `TRUE` | the two sides are strongly correlated (they agree) |
| `FALSE` | strongly anti-correlated, with one side much stronger |
| `GLUT` | anything else: a contradiction, kept rather than resolved |
| `PARADOX` | a persistent `GLUT`; the stateless resolver never returns it |

The example frame above resolves to `TRUE`. Use the result as evidence: map
it into your own [`Truth`](four-valued-logic.md) handling. It is not a
decision on its own.

## Next

- [Conformance testing](conformance-testing.md): the `ubh168` vectors.
- [SDK_DESIGN §11](../SDK_DESIGN.md#11-wire-framing-in-pure-host-code)

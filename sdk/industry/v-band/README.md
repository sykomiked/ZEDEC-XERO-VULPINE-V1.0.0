<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# V-Band: low-power state logging for LoRaWAN and NB-IoT sensors

V-Band is a small C11 library with two sides:

- **The sensor.** It keeps an append-only log of readings as exact
  integers, each with a four-valued state, and packs the unacknowledged
  readings into uplink payloads no larger than the LoRaWAN data rate allows.
- **The gateway.** It forwards each payload to the local mesh as UBH-168
  frames and accepts batches from each device strictly in order. Duplicates
  and out-of-order batches are refused.

The core is written in the MCU subset: no heap, no stdio, no C library
calls, fixed buffers the caller owns, and integers only. The API is
[`include/zvband.h`](include/zvband.h).

## The log

`zvb_log_init(&log, storage, cap, device)` uses the caller's array of `cap`
records as a ring. Every reading has these fields:

- a gap-free sequence number;
- a device time in seconds, which may never decrease;
- a sensor number;
- a state, from the table below.

| State | Meaning | Values |
|---|---|---|
| TRUE | a valid reading | `value` |
| FALSE | a sensor fault | `value` is the fault code |
| NEITHER | not sampled | 0 |
| BOTH | two redundant sensors disagree | `value` and `value2`, both kept |

`zvb_log_append_pair(&log, sensor, t, a, b, tolerance)` records a TRUE
reading if `a` and `b` are within `tolerance`, and BOTH with both values
otherwise. Neither value is chosen silently. A full log refuses new readings
(`ZVB_E_FULL`). It **never overwrites a reading the gateway has not
acknowledged**, though acknowledged readings are released automatically
to make room. A SHA3-256 chain head covers every reading ever appended,
including released ones.

## Payload limits and batches

`zvb_payload_max(region, dr, &bytes)`:

| Region | DR0 | DR1 | DR2 | DR3 | DR4 | DR5 | DR6 | DR7 |
|---|---|---|---|---|---|---|---|---|
| EU868 (RP002-1.0.4, EU863-870) | 51 | 51 | 51 | 115 | 222 | 222 | 222 | 222 |
| US915 (RP002-1.0.4, uplink) | 11 | 53 | 125 | 242 | 242 | | | |
| NB-IoT (this kit's assumption) | 512 | | | | | | | |

These are the maximum MAC payload sizes (`N`) with no FOpts. If the MAC
puts options in the frame, pass a smaller limit. `zvb_batch_make(&log,
max_bytes, out, cap, &len, &count)` packs as many waiting readings as fit:

```
u8 version | u32 device | u32 batch | u32 first seq | u8 count | u32 base time |
records | 4-byte check
record: u8 sensor | u8 flags (state, bit 2: has value2) | varint time delta |
        zigzag varint value | [zigzag varint value2]
check:  the first 4 bytes of SHA3-256("zvb/batch/v1" || everything before it)
```

The header is 18 bytes and a reading takes 4 to 17 bytes, so a batch needs
at least 26 bytes. US915 DR0 (11 bytes) cannot carry one reading, and the
call returns `ZVB_E_DR`. At EU868 DR0, four slowly changing readings fit in
45 bytes. A 512-byte NB-IoT payload holds up to 122 readings.

The decoder accepts only canonical (shortest) varints, values that fit in
32 bits, a first time delta of 0, flags consistent with the state, a
NEITHER reading of 0, and exactly the bytes the records need.

## UBH-168 and the gateway

`zvb_wire_encode(trunk, payload, len, frames, …)` sends a payload as one
header frame plus one frame per 16 bytes (`zxv_wire_pack`). The header
carries the magic word `0xA0E0B168`, the length, the frame count and the
first 8 bytes of SHA3-256(payload). `zvb_wire_decode` refuses a wrong tag,
a frame out of order, a frame missing or extra, non-zero padding, or a
hash mismatch.

`zvb_gw_accept(&gw, payload, len, out, out_cap, &info, &next)` decides each
batch against the device's state:

| Batch | Result |
|---|---|
| the next readings, newer batch number | accepted, readings copied to `out` |
| the same batch again, or readings already held | `ZVB_E_DUPLICATE` |
| an older batch number, or readings that skip ahead | `ZVB_E_ORDER` |
| check value wrong | `ZVB_E_CHECK` |
| not a batch | `ZXV_E_FORMAT` |
| unknown device | `ZVB_E_DEVICE` |

Every refusal goes to the gateway's callback, which the host uses for its
quarantine log. `next`, the next reading the gateway expects, is reported
on refusals too, so the reply resynchronises the device: the device calls
`zvb_log_ack(&log, next)`. A device that hears no reply calls
`zvb_log_ack(&log, log.acked + 1)` and sends the same readings again in a
new batch. If the gateway already has them, it refuses the batch as a
duplicate and its reply moves the device on.

## MCU footprint

`make test` runs two extra checks:

- **`check-mcu`** compiles the core freestanding (`-ffreestanding
  -fno-builtin`, no stack protector) and fails if the object refers to
  anything except libzxv's pure functions. Those are context-free, need no
  heap and are listed in the build output: `zxv_sha3_256`, `zxv_wire_tag`,
  `zxv_wire_pack`, `zxv_wire_unpack`, `zxv_hex_from_lp`, `zxv_hex_pack` and
  `zxv_status_name`. The only other names it allows are `memcpy`, `memset`,
  `memmove` and `memcmp`, which a compiler may emit; the current build emits
  none of them.
- **`size-arm`** runs only if `arm-none-eabi-gcc` is installed. It builds the
  core and [`tools/mcu_probe.c`](tools/mcu_probe.c) for a Cortex-M4 (`-Os
  -mthumb -mfloat-abi=soft`). The probe is a node with a 64-reading log and
  a 222-byte uplink buffer.

Measured with arm-none-eabi-gcc 13.2.1:

| Object | Flash (text) | RAM (data + bss) |
|---|---|---|
| `zvband.o`, the whole core: log, batches, wire, gateway | 3,224 B | 0 B |
| `mcu_probe.o`: the node's loop and its static buffers | 204 B | 1,578 B |
| deepest stack frame: the batch check value (`check4`) | | 576 B |

SHA3-256 and the UBH-168 frame functions come from libzxv and are not in
these figures. A sensor that only logs and makes batches does not need the
wire or gateway code, and the linker drops those functions (`-ffunction-sections`).

## Example

```c
static zvb_record storage[32];
zvb_log_init(&log, storage, 32, device_id);
zvb_log_append_pair(&log, 1, now, probe_a, probe_b, 3);   /* TRUE, or BOTH with both values */
zvb_payload_max(ZVB_REGION_EU868, dr, &max);
zvb_batch_make(&log, max, uplink, sizeof uplink, &len, &count);
/* ... the MAC sends it; the gateway's reply carries next ... */
zvb_log_ack(&log, next);
```

`make demo` runs [`examples/sensor_demo.c`](examples/sensor_demo.c), a
soil-moisture node with two redundant probes. It logs a reading every 15
minutes and sends hourly at EU868 DR0 or DR5. Along the way:

- one uplink is lost;
- one reply is lost, so the node resends and the gateway refuses the
  duplicate and resynchronises it;
- a disagreeing probe pair arrives as BOTH with both values;
- a probe fault arrives as FALSE;
- an old payload replayed at the end is refused.

The gateway ends with exactly the 24 readings the node logged.

## Tests

```sh
make -C sdk/libzxv all
make -C sdk/industry/v-band test        # 6,018 checks, fuzz smoke, demo, no-float, MCU and size checks
make -C sdk/industry/v-band SANITIZE=1 OUT=build/san test
make -C sdk/industry/v-band fuzz FUZZ_ITERS=20000
```

`test_vband` covers these areas:

- the log: states, refusals, full logs that never overwrite, acks, trims,
  ring wrap-around, the chain head against an independent computation,
  extreme pairs and sequence exhaustion;
- the payload table, and a batch at every region and data rate, each
  within its limit and round-tripping exactly;
- extreme values and time deltas;
- **every single-bit flip and every truncation** of a batch, all refused;
- hand-made batches that carry a valid check value but a bad structure:
  overlong varints, varints over 32 bits, trailing bytes, bad flags, time
  overflow;
- UBH-168 at many lengths, and **every single-bit flip of every frame**,
  plus reordered, missing, extra, idle and mixed-trunk frames;
- the gateway: order, duplicates, late batches, bad checks, unknown
  devices, the callback, counters and resync;
- a lossy simulation of two devices with small logs. Uplinks and replies
  are lost and frames corrupted at random, and the gateway ends with every
  reading in order.

`fuzz_vband` runs three devices with 16-reading logs against an
independent model:

- random readings, including invalid ones and readings from a clock that
  runs backwards;
- batches at random regions and data rates;
- UBH-168 bit flips in transit;
- batches corrupted between the mesh and the gateway;
- lost uplinks and replies, delayed batches that arrive late, and replays
  and edited replays of captured batches;
- forged replies.

After every operation the device counters, the chain head and the
gateway's state must equal the model's. Each batch must hold exactly as
many readings as fit, and each accepted batch must hold exactly the next
logged readings. The output is byte-identical under gcc and clang.

## Limits

- **Not a LoRaWAN stack.** V-Band sits above the operator's MAC. Join,
  MIC, encryption, frame counters, ADR, channel plans and confirmed uplinks
  belong to the LoRaWAN stack (or to NB-IoT's transport security). The
  4-byte check catches corruption but does not authenticate. A forged
  reply claiming readings the gateway never received could make a device
  release them. The MIC is what prevents that.
- **No duty-cycle or regulatory certification.** The kit does not track
  airtime, duty cycle (for example the 1% limit on EU868 sub-bands) or
  dwell time, and makes no claim of LoRa Alliance, radio-regulatory or
  operator certification. The payload table is the regional parameters'
  maximum. When to transmit is the MAC's decision.
- The NB-IoT limit of 512 bytes is this kit's own assumption, not an
  operator figure.
- The device clock is the device's own. Time only has to be
  non-decreasing, and the kit does not synchronise it.
- The gateway keeps per-device state in the caller's array, which is
  searched linearly. It is meant for a site gateway with tens to hundreds of
  devices, not a network server.

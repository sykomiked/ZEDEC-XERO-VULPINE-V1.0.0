<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# The Railway: moving work between processors, machines and sites

The Railway is how work travels in ZXV: inside one machine between the
CPU and its accelerators, between the machines of one owner, and between
sites. This page maps the design onto the modules that exist and says
what is built. Vocabulary: [LEXICON.md](LEXICON.md); routes between
desks: [TRADE_ROUTES.md](TRADE_ROUTES.md); the hardware:
[hardware/docs/oem/](../hardware/docs/oem/README.md).

## 1. The parts and their code

| Name | What it is | Code it builds on | State |
|---|---|---|---|
| **Consist** | A batch of uniform jobs packed into one contiguous, cache-aligned block (a fixed-stride matrix, no pointers) and shipped to an accelerator in one transfer | `kernel/src/vblock` (content-addressed blocks in pinned host RAM, VRAM and unified memory) | Design over built parts |
| **Mainline** | The double-buffered path a Consist travels: one executing, the next in transit, the last being cleared | `vblock` transfers; `vb_backend_t` (CUDA, Vulkan, ROCm backends) | Design over built parts |
| **Roundhouse** | Measures the machine at start: memory bandwidth, cache sizes, PCIe link width and payload size, the accelerator's batch sweet spot; sets the Consist size (the gauge) | `kernel/src/cotier/ct_budget` (measured budgets) | Design |
| **Yardmaster** | Schedules Consists, throttles on heat and errors, and switches a Consist to the CPU's integer cores when an accelerator fails | The Pit Boss ([PIT_BOSS.md](PIT_BOSS.md)), `cotier/ct_gate` | Design over built parts |
| **Siding** | A bounded ring where rows that cannot settle wait: contradictions (BOTH) and back-pressured work | Bounded rings of [TRADE_ROUTES.md](TRADE_ROUTES.md) section 2 | Design |
| **Reconciliation** | Every accelerator result is checked on the integer core before it settles | `cotier/ct_reconcile` | Built |
| **Freight** | ZXV's network packet: 168 rows of 21 bytes, a systematic Reed-Solomon code over GF(2⁸), so a freight survives lost rows | `kernel/src/freight` | Built and tested |

**Naming.** The original proposal called the on-machine batch a "Freight
Packet". `freight` already names the network packet above, so the
on-machine batch is the **Consist** (a train's coupled cars). A Consist
never leaves the machine; work that leaves travels as freight inside a
Vinea session.

## 2. Inside one machine

```
 [ desks / the Pit Boss ] -> Yardmaster -> Consist (packed, gauged by the Roundhouse)
        |                                        |
        |                          Mainline: buffer A executing on the accelerator
        |                                    buffer B in transit (DMA)
        |                                    buffer C clearing (results out)
        v                                        |
 integer core <- ct_reconcile (sampled re-run) <-+
        |                 \
   settle (T, F, N)        Siding (B rows, and back-pressured Consists)
```

- **The accelerator is untrusted.** As in the hardware packages
  (accelerators outside the trust boundary), its results settle only after
  `ct_reconcile` re-runs a sample on the integer core.
- **Gauge, not guess.** The Consist size comes from measurements on the
  machine (Roundhouse), not from a constant; a small machine runs small
  Consists.
- **Fallback.** If an accelerator stops, the Yardmaster sends the same
  Consist to the CPU's integer cores; the answer is the same bits, because
  the integer engine is deterministic (`kernel/src/tensor/zt.h`).

## 3. Four-valued rows in bit-planes

A Consist can carry four-valued truth values (`lpres_state_t`: NEITHER,
TRUE, FALSE, BOTH) for many rows at once as two bit-planes, a **t** plane
(evidence for) and an **f** plane (evidence against):

| Value | t | f |
|---|---|---|
| FALSE | 0 | 1 |
| NEITHER | 0 | 0 |
| BOTH | 1 | 1 |
| TRUE | 1 | 0 |

ZXV's conjoin and disjoin are **not** Belnap's lattice operations. They
are min and max on the chain **F < N < B < T** (`kernel/src/lpres/lpres.c`,
owner decision 2026-10-12), so BOTH and NEITHER together never give
TRUE. In bit-planes this is done with each value's rank in the chain as a
2-bit number, r1 = t and r0 = NOT f (F = 00, N = 01, B = 10, T = 11):

```
conjoin (min):  r1 = a1 & b1
                r0 = (a0 & b0) | (a1 & ~b1 & b0) | (~a1 & b1 & a0)
disjoin (max):  r1 = a1 | b1
                r0 = (~(a1 ^ b1) & (a0 | b0)) | (a1 & ~b1 & a0) | (~a1 & b1 & b0)
result:         t = r1, f = ~r0
negate:         swap the t and f planes (T <-> F; B and N stay)
```

These were checked against `lpres_conjoin` and `lpres_disjoin` for all 16
input pairs. The Belnap formulas in the original proposal
(t = t1 & t2, f = f1 | f2 for AND) give a different answer for BOTH with
NEITHER and must not be used.

**The contradiction siding.** The rows whose value is BOTH are
`t & f`. After a Consist runs, rows with that bit set go to the Siding;
every other row settles. No row of a contradiction stops the train.

## 4. Between machines and sites

The owner's machines form one device mesh
([hardware/docs/oem/mobile/MOBILE_COMMON.md](../hardware/docs/oem/mobile/MOBILE_COMMON.md)
section 3). Work that leaves a machine travels as freight in a Vinea v3
session (ML-KEM-1024 + ML-DSA-87). The original proposal's transport
classes map onto ZXV's bearers:

| Proposal | What it carries | ZXV |
|---|---|---|
| Air cargo | Urgent, small: approvals, alerts, heartbeats | Any bearer, priority queue; on LoRa or satellite IoT-NTN when nothing else reaches |
| Trucking | Local peer to peer | Wi-Fi 7, Ethernet, BLE and the device-mesh bearer (`kernel/src/bearer`) |
| Shipping lanes | Bulk: model weights, snapshots | Freight over long-lived sessions; content-addressed blocks (`vblock`, IPFS CIDs) so a transfer resumes and verifies |
| Intermodal container | One envelope across every carrier | The Vinea session body carrying freight ([TRADE_ROUTES.md](TRADE_ROUTES.md) section 3: "a Vinea body, not a new wire") |
| Customs | Inspection before anything enters | The Cage ([CAGE.md](CAGE.md)) |

**Servers are peers.** A Hoard or a Guildhall is a device in the same
mesh with the HOME role, not a central authority; two handhelds can trade
work with no server at all.

## 5. What is built and what is not

| Built | Not built |
|---|---|
| `freight` (erasure-coded network packets), `vblock` (tiered, content-addressed device memory with GPU backends), `cotier` (budget, gate, reconcile), `lpres`, the integer tensor engine, Vinea v3 sessions, the device mesh | The Consist format and packer, the Roundhouse measurements, the Mainline's triple buffering in `vblock`, the Siding ring, the bit-plane kernels of section 3 (CPU SIMD and GPU) |

<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Paper II: Tokenless Infrastructure and the Traffic-Driven Resource Economy

*Exact-Integer Reciprocity, Pack-Sliced Browser Hydration, and the Nine Forms
of Capital*

**Authors:** Michael Laurence Curzi & 36N9 Genetics, LLC
**Implementation Reference:** `kernel/src/swarm/`, `kernel/src/zcapital/`, `kernel/src/vna/`, `web/boot/`
**License:** Apache-2.0

---

## Abstract

Modern distributed software is caught between two unsustainable economic models:
centralized cloud oligopolies that extract punitive bandwidth egress and
instance rent, and speculative Web3 networks that impose volatile gas auctions,
floating-point rounding drift, and plutocratic validator monopolies on every
state transition. Neither model aligns infrastructure cost with actual utility.

This paper specifies ZXV's resource economy across both its peer-to-peer
network layer (Vinea / Web4) and its local multi-agent compute scheduler. First,
we show how the **S3/IPFS-to-Browser Pack Hydration Pipeline** (`boot.pack` +
HTTP `206 Partial Content` range slicing into a deterministic 183 KB WebAssembly
kernel) shifts execution to the client edge, reducing server load to static
content-addressed block delivery. Second, we detail the **Vinea Metered
Reciprocity Ledger**, which settles verified byte and record delivery into exact
integer vouchers (`$YHVH`) without external gas tokens or interest-bearing debt.
Third, we formalize the **Fibonacci 8:13 Commons-to-Market Compute Allocator**
and the **Nine Forms of Capital** (`kernel/src/zcapital/`), proving that
separating five tradeable capital forms from four inalienable Crown forms
mathematically prevents compute monopoly while guaranteeing a non-zero commons
floor for every participant.

---

## 1. Eliminating Cloud Rent via Edge-Hydrated WebAssembly (`web/boot/`)

A conventional web application executes business logic, database queries, and
session rendering on origin servers, scaling cloud compute and egress bills
linearly with active users. Conversely, naive browser-based operating systems
either download monolithic multi-hundred-megabyte disk images up front or issue
hundreds of serial HTTP requests for small assets, saturating browser connection
pools.

ZXV replaces origin compute with a two-stage **Content-Addressed Hydration
Pipeline**:

1. **Deterministic Wasm Micro-Entry (`zb.wasm`, 183,965 bytes).** Compiled with
   pinned LLVM (`clang --target=wasm32 -ffreestanding -nostdlib`), the boot
   kernel, `zui` vector compositor, window manager, and 16 staple desktop
   applications fit inside a single 184 KB binary whose SHA-256 digest is
   verified in browser memory before `WebAssembly.instantiate` is called.
2. **Range-Sliced Pack Hydration (`boot.pack` + `boot.json`).** At seal time,
   `tools/seal.py` packs the 479 boot-critical assets (ASCW vector fonts, vault
   door geometry, application icons, and initial Hoard records) into a single
   contiguous archive `boot.pack`, recording each file's exact `(offset, size,
   sha256, cid)` in `boot.json`.
3. **Parallel HTTP `206` Coalescing.** The browser loader (`boot.js`) issues
   parallel `Range: bytes=start-end` requests against any content-addressed
   gateway or regional seed node, verifies each slice against its SHA-256
   digest in `SubtleCrypto`, and streams the verified bytes into the Wasm heap
   via `zb_file()`.

Because every byte is content-addressed and immutable, any regional seed node,
IPFS gateway, or local Kubo cache can serve slices interchangeably. Once
hydrated, the entire windowing environment, vector renderer, and local ledger
execute at native speed inside the client's CPU with zero ongoing server compute.

---

## 2. Exact-Integer Arithmetic and Fibonacci Denominations (`swarm_denom.h`)

Floating-point IEEE 754 arithmetic (`float`, `double`) violates associativity
($(a + b) + c \neq a + (b + c)$) and introduces architecture-dependent rounding
drift across x86-64, ARM64, RISC-V, and Wasm targets. In ZXV, floating point is
banned at compile time across the kernel and all economic modules.

All measurements, prices, and balances use either:
- **64-bit exact integers** with checked overflow, or
- **Canonical Fibonacci/Zeckendorf denominations** (`kernel/src/swarm/swarm_denom.h`,
  rules `D1–D9`) and **reduced exact rationals** (`kernel/src/rational/rational.h`).

By Zeckendorf's theorem, every positive integer $N$ has a unique representation
as a sum of non-consecutive Fibonacci numbers. ZXV's denomination ladder wraps
and unwraps plain tokens, compressed tokens, and super tokens across grades:

| Grade Constant | Octane Value | Role |
|---|---|---|
| `PLAIN` | `0` | Base unit token face |
| `REGULAR` | `4` | Standard batch settlement |
| `MIDGRADE` | `9` | Regional relay settlement |
| `PREMIUM` | `14` | High-throughput block trunking |
| `RACING` | `23` | Cross-backbone swarm clearing |
| `MAX` | `63` | Maximum denomination ceiling |

Wrap and unwrap operations conserve exact face value with zero conversion fee
and zero rounding remainder.

---

## 3. The Nine Forms of Capital and Inalienable Crown Standing (`zcapital`)

A fundamental flaw of purely financialized token networks (Proof-of-Stake and
gas-bidding markets) is that financial wealth can purchase governance authority,
validation weight, and reputation—allowing a single wealthy actor to corner the
network.

ZXV's capital engine (`kernel/src/zcapital/`) partitions value into **nine
canonical forms** divided into two strict classes:

### 3.1 Five Tradeable Capital Forms

| Capital Form | How Earned | Market Convertibility |
|---|---|---|
| **Financial** | Proportional market clearing & verified settlement receipts | Primary medium of exchange |
| **Intellectual** | Verified knowledge blocks, proofs, and cited CIDs | Convertible via `zcap_exchange` |
| **Manufactured** | Verified compiled binaries, code, vector/audio artifacts | Convertible via `zcap_exchange` |
| **Human** | Accepted interactive user assistance and resolved escalations | Convertible via `zcap_exchange` |
| **System** | Measurable latency, memory, or bandwidth optimization | Convertible via `zcap_exchange` |

### 3.2 Four Inalienable Crown Forms

| Crown Form | How Earned | Transferability |
|---|---|---|
| **Social** | Cooperating, witnessing peer audits, and serving DHT/Vinea blocks | **Never tradeable** |
| **Natural** | Frugality: returning unused cycle token allotments to the commons | **Never tradeable** |
| **Cultural** | Fidelity to the user's spoken/programming dialect and conventions | **Never tradeable** |
| **Spiritual** | Invariant adherence to The One Policy (zero forbidden tags) | **Never tradeable** |

Because the four Crown forms have no market exchange function (`zcap_exchange`
refuses them by invariant), **money can never buy standing**. A participant with
infinite Financial capital but zero Social capital cannot capture the
cooperation dividend or override peer audit weight.

---

## 4. The 8:13 Fibonacci Cycle Allocator (`kernel/src/swarm/`)

Every compute cycle, a node's hardware capacity yields an exact integer budget
of $T$ tokens, distributed across two orthogonal axes (`re + i·im`):

1. **The Emotional / Imaginary Axis (`swarm_emotion.h`).** Up to $\frac{8}{21} T$
   ($\approx 38.1\%$) is allocated onto the imaginary axis `im` according to
   the system's measured load charge ($\text{charge}/21$).
2. **The Commons Floor (`swarm_budget.h`).** Of the remaining real tokens
   $T_{\text{real}}$, exactly $\frac{8}{21}$ is distributed across all active
   agents arranged in Fibonacci-sized tiers (level $d$ holds at most $F_{d+2}$
   models: $1, 2, 3, 5, 8\dots$ with weights $F_{L-d+1}$). Every working agent
   receives its commons share regardless of its Financial balance—**starvation
   is impossible**.
3. **The Proportional Market (`swarm_market.h`).** The remaining $\frac{13}{21}$
   of $T_{\text{real}}$ is sold in a proportional-share clearing market where
   floor and market stand in the Fibonacci ratio $8 : 13$ ($\approx \varphi^{-1}$):
   - **No debt or leverage:** An agent may only bid Financial capital it
     currently holds.
   - **Anti-monopoly cap:** No single participant may win more than
     $\frac{8}{21}$ of the market pool in any cycle. Excess tokens roll over to
     other bidders, and unclaimed tokens return to the commons floor.
   - **Pot recycling:** At cycle end, $100\%$ of the bidding pot is paid back
     out as income: $\frac{13}{21}$ proportional to verified value produced
     (Intellectual, Manufactured, Human, System) and $\frac{8}{21}$ as a
     cooperation dividend proportional to **Social** (Crown) capital earned that
     cycle.

---

## 5. Vinea Peer Metering and Settlement (`kernel/src/vna/`)

Across the global 8-node mesh, the same reciprocity principle governs network
bandwidth and storage:

- Each Vinea peer connection runs over post-quantum authenticated UDP (`:8724`)
  with per-peer usage meters (`vna_meter`) tracking verified chunks and records
  served.
- At each settlement boundary, contribution-weighted receipts settle into
  `$YHVH` vouchers posted to the triple-entry paraconsistent ledger.
- Bootstrap seed nodes that jumpstart the network by serving the sealed
  operating-system DAG and scholarly CAR archives accumulate verifiable service
  credit directly from the traffic they sustain—turning high-utility archival
  seeding into a self-accounting economic engine.

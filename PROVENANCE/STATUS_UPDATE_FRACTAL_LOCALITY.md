# ZXV — Fractal (Z-order) Component Addressing

**Date:** 2026-08-04
**Path:** `05_KERNEL/kernel/src/fractal/`
**Status:** implemented, host-tested, **benchmarked** (20/20 assertions).

## The claim, stated honestly

Giving components "fractal IDs" does not by itself make anything faster.
Geometry is not a source of free efficiency. What a self-similar address
space *does* provide is **locality**, and locality is worth real energy
because data **movement**, not arithmetic, dominates power in modern
hardware — typically by an order of magnitude.

So the mechanism is precise and falsifiable:

```
Z-order (Morton) addressing   ->  co-located work gets near addresses
near addresses                ->  fewer hierarchy levels traversed
fewer levels traversed        ->  less data movement per event
```

The entitled claim is **"less data movement for the same result, measured
against a baseline placement"** — never "more compute from geometry".

## Measured result

`src/fractal/test_zorder.c` builds a clustered communication workload
(64 components in 8 tight clusters — the shape real subsystems have,
where a cell talks mostly within its own group), places it two ways, and
reports the total weighted movement cost:

| Placement | Cost | Ratio |
|---|---:|---:|
| Row-major (naive baseline) | 90,128 | 1.000 |
| **Z-order locality** | **61,298** | **0.680** |

→ **32.0% less data movement for identical work.**

**The honesty control:** the same benchmark run on *uniform* traffic
(everyone talks to everyone equally) yields a ratio of **1.000** — no
win, because there is no locality to exploit. The method does not
manufacture efficiency where none exists, and the test asserts this.

## Self-similarity — why this is "fractal" and not just "a layout"

A Morton code interleaves its coordinates' bits, so **truncating the code
to the top 2k bits yields the address of the enclosing region at level
k**. A prefix of an address *is itself a valid address*. That is the
property being exploited: the same routing/scheduling logic applies at
every level — core → cell → task → event — because each level is
structurally identical to the one above. One verified algorithm, N
scales.

## API

`zo_encode2 / zo_decode2` (Morton), `zo_make`, `zo_parent` (the prefix
operation), `zo_contains`, `zo_common_level`, `zo_hops` (routing-cost
proxy), `zo_manhattan`, `zo_place` (greedy locality placement),
`zo_cost` (score any placement).

Integer-only, freestanding, no allocation (placement is bounded at 256
components).

## Bug found by the benchmark

The first implementation computed prefixes relative to `ZO_MAX_LEVEL`
rather than each address's own level. For small coordinates every prefix
collapsed to 0, so all components appeared co-located and **every hop
cost measured zero** — the benchmark reported a meaningless 1.000 ratio.
Fixed in `prefix_at()`. This is exactly why the claim is benchmarked
rather than asserted: the test caught a defect that a plausible-sounding
description would have hidden.

## What remains

- Wire `zo_place` into the real cell fabric so cell admission assigns
  fractal addresses, and into the event router so `zo_hops` informs
  dispatch order.
- Measure on a real boot workload rather than a synthetic affinity
  matrix, and report the delta in the evidence bundle.
- The hop-count model is a **proxy** for physical cost. On real silicon
  it must be validated against measured energy, not assumed.

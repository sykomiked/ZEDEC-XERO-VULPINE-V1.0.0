# ZXV → RISC-V International

*A presentation for the stewards of the open ISA itself.*

## Who you are

RISC-V International governs the open instruction set that is quietly rewiring
the industry — a global, member-driven standards body spanning every
geopolitical bloc, whose entire premise is that the *foundation* of computing
should be open, unencumbered, and available to all.

## Why we are here

An open ISA has been waiting for operating systems designed *for* it rather than
carried *onto* it. Almost everything running on RISC-V today is a port — Linux,
the BSDs — bringing decades of assumptions built around older architectures.
ZXV is a clean-room OS whose core ideas were chosen independently of any legacy
ISA:

- **Event-sequence ordering** instead of a clock-tick scheduler — a good match
  for RISC-V's deterministic, extensible core.
- A **cellular multikernel** that maps onto many small harts and clustered
  systems rather than assuming one big SMP domain.
- **Deployment profiles** that scale one codebase across the ISA's whole
  range, from microcontroller profiles to HPC.
- **Post-quantum by default** (ML-KEM-768, X25519, verified against the
  published vectors) — appropriate for an ISA meant to underpin the next
  decades.

## The specific hook

ZXV is a *demonstration artifact* for the thesis your organization exists to
prove: that a genuinely open foundation invites genuinely new systems software.
It is small, fully auditable, and built without any proprietary dependency — the
kind of clean example the ecosystem can point to and learn from.

## The honest ask

Complete honesty, because this is a standards body: ZXV **boots on ARM64 today**
and **RISC-V compiles but does not yet boot** — the arch layer is the current
frontier, verified-compiling on every commit, not yet booting. That is exactly
the kind of gap the RISC-V community closes best. If a clean-room, post-quantum,
event-sequenced OS is a useful example for the ecosystem, the portable
foundation is already tested (3000+ assertions) and waiting for the RISC-V
boot to be brought up on real cores.

> The ISA is open. Here is systems software that started from that premise.

# ZXV → India's RISC-V & maker community

*A presentation for SHAKTI, VEGA, and the makers building sovereign silicon.*

## Who you are

India is building its own processors on purpose: **SHAKTI** (IIT Madras) and
**VEGA** (C-DAC) are indigenous RISC-V cores, backed by a national push for
self-reliant computing, and around them a large, young maker and academic
community. The goal is not just cheaper chips — it is *sovereignty*: computing
that does not depend on anyone else's permission, IP, or supply chain.

## Why we are here

Sovereign silicon deserves a sovereign OS — one you can read, own, and modify
completely, with no foreign black box in the trust path. ZXV is that: a
from-scratch freestanding kernel, ~95k lines you can audit end to end, no Linux
underneath, no libc, no blob. Post-quantum from the first commit (ML-KEM-768,
verified against the standard vectors), which matters for a national platform
meant to outlast today's cryptography.

And its politics match yours. ZXV ships under a license stack built to be
*truly* free across every legal tradition — including a doctrine of **mutual
sovereignty**: every user is recognized as sovereign, reciprocally. That is not
decoration; it is compiled into the system's social layer.

## The specific hook

- **RISC-V is the target, not a port.** ZXV's event-sequence runtime and
  cellular multikernel were designed for the kind of flexibility SHAKTI/VEGA
  offer, and its deployment profiles scale the one OS from a classroom board to
  a national HPC installation.
- **Auditable to the silicon.** For a platform whose whole point is not trusting
  someone else's opaque stack, an OS whose entire source one engineer can read
  is the right foundation for certification and teaching alike.

## The honest ask

Told plainly, because this community will and should verify: ZXV **boots on
ARM64 today**; **RISC-V compiles but does not yet boot**. For a country building
its own RISC-V cores and its own engineers, helping land the ZXV boot on SHAKTI
or VEGA hardware is a genuinely high-impact, genuinely sovereign project — and
the hard part (a tested network stack, verified crypto, the cellular fabric,
deployment profiles) is already built and waiting for the arch layer.

> Your silicon answers to no one. Neither should your OS.

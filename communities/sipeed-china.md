# ZXV → Sipeed (and the Chinese RISC-V community)

*A presentation for the people mass-producing accessible RISC-V, from the Longan
Nano to the Lichee cluster boards.*

## Who you are

Sipeed put RISC-V in students' hands at pocket-money prices — Longan Nano, Maix,
the Lichee Pi / Lichee Cluster line — and the broader Chinese RISC-V ecosystem
(T-Head/XuanTie cores, the whole Alibaba-adjacent silicon push) is moving faster
than anywhere on Earth. You are not porting to RISC-V; you are *building* on it,
at scale, as a first-class architecture.

## Why we are here

Most operating systems treat RISC-V as a port target — old x86/ARM assumptions
carried over onto a new ISA. ZXV was designed clean-room around ideas that suit
RISC-V's flexibility: an **event-sequence runtime** (deterministic ordering
without clock-tick baggage), a **cellular multikernel** that maps naturally onto
many small cores and clustered boards, and **deployment profiles** that scale
the same OS from a Longan Nano to a Lichee cluster. It is post-quantum from the
first commit — ML-KEM-768 verified against the standard vectors — which matters
for silicon meant to last a decade.

## The specific hook

- **Clusters are a first-class shape, not an afterthought.** Your Lichee Cluster
  boards are exactly the multi-node fabric ZXV's constellation + cellular model
  were built to coordinate. The deployment profile detects "many nodes, many
  cores" and resolves to the supercomputer profile automatically.
- **Small cores welcome.** The same kernel resolves down to an embedded profile
  on a Longan-class part. One codebase, whole range.

## The honest ask

Full honesty, because your community will check: ZXV **boots on ARM64 today**;
the **RISC-V arch layer compiles but does not yet boot** — we verify RISC-V
compilation on every commit, and the boot is the next milestone, not a finished
claim. The single most valuable thing the Chinese RISC-V community could do is
help land that boot on real XuanTie / Sipeed silicon. The portable half — net,
crypto, cellular fabric, profiles — is already tested and waiting for it.

> Designed *for* RISC-V, not ported *to* it. Help us boot it on yours.

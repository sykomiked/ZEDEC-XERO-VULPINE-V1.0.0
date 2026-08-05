# ZXV → African & MENA makers

*A presentation for the builders who make computing work without assuming the
cloud, the grid, or the vendor are always there.*

## Who you are

Across Africa and the Middle East, a fast-growing maker and open-hardware
community builds for real constraints: intermittent power, expensive or
throttled bandwidth, and a hard-won preference for systems that do not depend on
a rented datacenter half a world away. From Nairobi's and Lagos's hackerspaces
to the Gulf's engineering programs, the value is *self-reliance*.

## Why we are here

ZXV assumes nothing you might not have. It is a from-scratch, freestanding OS —
no cloud dependency, no mandatory phone-home, no assumption that a server
exists. It boots on affordable ARM64 hardware, and its entire networking and
social model is built around **decentralization**: no central authority, no
one-way surveillance, peers that credit each other's participation without a
bank in the middle.

It is also lightweight by construction — event-sequenced, no bloated userland —
so it does useful work on modest hardware, and its deployment profiles let the
*same* OS run on a single salvaged board or scale across whatever nodes you can
network together.

## The specific hook

- **Serverless by design.** The mesh and token layers assume no center — the
  right shape for community networks and off-grid deployments where
  infrastructure is the thing you cannot count on.
- **Bandwidth-respecting and offline-capable.** A from-scratch stack with no
  telemetry and a phase-router built for high-latency, unreliable links.
- **Post-quantum**, so hardware deployed now stays secure as cryptography moves.

## The honest ask

ZXV boots on ARM64 today; other architectures compile but do not yet boot — we
say so. It does not yet speak LoRa or other long-range radios, but its driver
model is a clean ops-boundary (a radio with no backend returns *not-bound*, it
never fakes a reading), so adding one for local hardware is a bounded job. If a
self-reliant, decentralized, cloud-optional OS fits how your community actually
builds, clone it, boot it, and tell us what it needs to run where you are.

> Built for computing that does not assume the infrastructure is always up.

# ZXV → Pine64

*A presentation for the community that treats an OS as something you build, not
something you're given.*

## Who you are

Pine64 ships affordable, open ARM64 and RISC-V hardware — the PinePhone,
Pinebook, Quartz64, Star64 — and then gets out of the way. Your community is
famous for running *anything* on it: a dozen distros, mainline kernel work,
postmarketOS, custom bootloaders. You are hardware purists who are hostile to
locked-down, blob-heavy, take-it-or-leave-it software.

## Why we are here

Every OS you run is, underneath, the same Linux kernel with a different coat of
paint. ZXV is not that. It is a *different kernel* — written from scratch, in
freestanding C, with no Linux beneath it and no libc. For a community that has
already tried every Linux permutation, "here is a genuinely different OS that
boots on your board" is a rare thing to be offered.

And it is small enough to actually own. Not "trust the maintainers" small —
"read the TCP state machine yourself over coffee" small.

## The specific hook

- **It already networks for real.** On ARM64 it completes a live
  `DHCP → DNS → TCP → HTTP 200 OK` against the actual internet, with a stack we
  wrote from the Ethernet frame up and tested against the RFCs. Not a
  demo-that-pings-localhost — a real fetch, decoded off the wire.
- **Post-quantum, and honest about crypto.** ML-KEM-768, X25519,
  ChaCha20-Poly1305, all verified byte-exact against the published test
  vectors. And we tell you plainly what is *not* done: the TLS client doesn't
  verify certificates yet, and it fails closed rather than pretending otherwise.

## The honest ask

ZXV boots on ARM64 today — your Pinebook/Quartz class of SoC. RISC-V (your
Star64) currently compiles but does not yet boot; that's the honest gap. If a
from-scratch, post-quantum, event-sequenced OS is the kind of thing your
community likes to be the first to run on a new board, clone it and boot it in
QEMU today, and help us land it on a Pine64 board next.

> You've run every distro. Here's something that isn't one.

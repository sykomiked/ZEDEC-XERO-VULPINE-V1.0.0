# ZXV → Radxa

*A presentation for the Rock Pi community — serious testers of alternative
software on serious boards.*

## Who you are

Radxa builds Rock Pi / ROCK boards (RK3588 and friends) that punch above the
Raspberry Pi in raw capability, and your community is unusually willing to test
**alternative operating systems, custom bootloaders, and experimental images**.
You are not afraid of a board that ships without a coddled first-boot wizard.

## Why we are here

Because "alternative OS" usually still means "another Linux." ZXV is a genuine
alternative: a from-scratch freestanding kernel, event-sequenced, post-quantum,
with a network stack we wrote from the frame up that fetches a real web page
over the internet. On a capable RK3588-class board, there is real headroom to
run its cellular multikernel across many cores — and its **deployment profiles**
will actually *use* those cores: the same OS sizes itself from an SBC to a
supercomputer, scaling cells and concurrency to the hardware it wakes up on.

## The specific hook

- **It scales to your board, not down from a datacenter.** Give ZXV a 16-core
  RK3588 and its profile resolves to more cells and a higher event budget than a
  4-core laptop gets — automatically, from the detected hardware, with a hard
  ceiling so it never overreaches.
- **Custom-bootloader friendly.** ZXV is a raw, direct-boot image today; the
  path to U-Boot / your bootloader chain is a bounded integration, and we're
  building a universal, multi-arch install image for exactly this reason.

## The honest ask

ZXV boots on ARM64 (your RK3588 class) today. Where your community shines is
*testing* — so test it: boot it, push its cellular fabric across all your cores,
and try to make the deployment profile mis-size itself or a cell take down the
fabric. The 3000+-assertion suite says it holds. You're the right people to
check.

> A capable board deserves an OS that isn't just Linux again.

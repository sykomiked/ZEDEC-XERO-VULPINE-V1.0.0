# ZXV → MNT Research

*A presentation for the people who open-sourced the laptop down to the PCB.*

## Who you are

You build the MNT Reform and Pocket Reform: machines where the schematic, the
board, the firmware, and the software are all open and auditable, because you
believe a computer you cannot inspect is a computer you do not own. You chose an
i.MX8M / RK3588 not because it was fastest but because it was *knowable*.

## Why we are here

You already refuse the black box in hardware. We built the OS that refuses it in
software. ZXV is a from-scratch kernel in freestanding C — no Linux, no libc, no
vendor blob mediating between you and the machine. When you `git clone` it, you
are holding the *entire* system, and it is small enough that one person can
actually read it in a weekend.

That is not a slogan for us. The network stack that fetches a real web page over
the internet is code you can read end to end: the ARP table, the TCP state
machine, the checksum arithmetic — all there, all commented with *why*, all
tested against the RFC vectors rather than against itself.

## The specific hook

The Reform is the machine people flash their own firmware onto. ZXV is designed
to be flashed and understood, not just run. Two things you'll appreciate:

- **Nothing hides its limits.** Our own docs list, next to what works, exactly
  what does not (x86/RISC-V compile but don't boot yet; the TLS client doesn't
  verify certs yet). We would rather you trust us on the hard truths than the
  easy ones.
- **The native format carries its own undo.** Every ZXV build ships as a
  cryptographically-bound triad: what it does, what it undoes, and what it
  leaves unresolved — signed with Ed25519. A release that lost its own rollback
  path *cannot seal*. For a machine built to be repaired, an OS built to be
  reverted is the right match.

## The honest ask

The Reform runs on ARM64. **So does ZXV, today.** Boot it in QEMU, or on a
board, and read the ~95k lines. Then tell us where it's wrong — you have the
eyes for it. Our correctness suite is 3000+ assertions and we still expect you
to find something. Good. That's the point.

> Built by people who also think "trust us" is not an engineering argument.

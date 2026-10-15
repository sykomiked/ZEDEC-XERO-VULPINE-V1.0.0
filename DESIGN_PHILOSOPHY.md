<!--
Copyright (c) 2026 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Design Philosophy

*By **Jolly Dragon Roger** (the author's internet pen name), for 36N9 GENETICS LLC.*

This document says why ZEDEC XERO VULPINE is built the way it is. The code
can show what the system does; this page is for the reasons behind it, so
that anyone who reads, uses, extends or forks the project knows the
philosophy it was written in.

## 1. Nature is the standard we measure against

The greatest technology we could ever aspire to, and one we may only ever
reach a modicum towards, is Mother Nature.

A living cell copies its own instructions, repairs its own damage, runs on
a trickle of energy and wastes almost nothing. An ecosystem has no central
scheduler, yet it balances itself across millions of independent agents.
Nothing in a forest is rewritten from scratch every season; what works is
kept, refined and carried forward.

We take three lessons from that:

- **Efficiency before scale.** Nature does more with what it already has
  before it reaches for more.
- **Many small agents, cooperating.** The swarm in this project is several
  small, efficient models working together, not one giant model.
- **Limits are real.** Energy, water, materials and waste are hard budgets
  in this project, not costs that can be bought past. A job that would
  cross an ecological boundary waits; it does not pay its way through.

We hold ourselves to humility here. Nothing in this repository is claimed
to equal nature. It is the direction we face, not a finish line we claim
to have crossed.

## 2. Code is hardware, hardware is code

According to physics, computation is physical. Every program, when it runs,
is a pattern of charge, current and heat in real matter. Every circuit is a
fixed program, cast in silicon (or germanium, or whatever comes next).
The line between "hardware" and "software" is a line of convenience,
drawn by where the program happens to live: in a mask, in microcode, in
firmware, or in memory.

So in this project:

- **Hardware can emulate hardware.** Any general-purpose machine can
  emulate any other, given time and memory. The engineering task is to
  make that emulation *efficient*: to describe hardware state in an open,
  versioned standard, keep exact reference implementations for every
  engine, and let the fastest available component (a real circuit, a
  programmable fabric, microcode, a peer, or plain software) do the work,
  checked against the same golden vectors.
- **Hardware upgrades can be software updates.** When a design is
  expressed as code and checked against known answers, improving it is a
  signed update, not a new purchase.
- **Honest limits.** Emulation costs something. Our aim is to be much
  faster than naive emulation and to beat conventional stacks on the same
  chips where we can measure it. We do not claim to be faster than native
  hardware, and we do not claim to beat physics.

## 3. Make what you have efficient before asking for more

There is no need for more and more powerful systems if we do not first take
the care to make the systems we already have efficient.

The industry habit is to answer slow software with faster hardware, and
then to spend the new speed on heavier software. That cycle burns energy,
money and minerals, and it leaves working machines in landfill. We would
rather:

- run integer and fixed-point arithmetic where floating point is not
  needed, so results are exact and identical on every processor;
- measure before claiming, and publish the numbers, including the ones
  where we are still slower than other tools;
- give each part of the swarm a token budget per cycle, so work is
  rationed and shared, not grabbed;
- support old machines as first-class targets, not afterthoughts.

The machine on someone's desk today is already extraordinary. Our job is
to respect it.

## 4. Don't reinvent the wheel

New is not the same as better. Languages and formats that have run banks,
governments, satellites and laboratories for decades exist for a reason,
and they function exactly as intended.

- **COBOL** works. Its fixed, declared record layouts (copybooks) make it a
  near-perfect contract and treaty language for the computer: every field
  has an exact size, sign and meaning that both parties can read and
  verify. This project uses COBOL copybooks as its treaty format and
  carries them forward, with a compact syntax ("Sutra") that compiles to
  the same form. Results are checked for byte-for-byte parity against a
  standard COBOL compiler.
- **Fortran** remains the language of numerical science. It is carried
  forward the same way, alongside **Ada**, **APL/K** and **MUMPS**, each
  for the work it was designed to do well.
- **Open standards** (RFCs, FIPS, ISO 20022 and similar) are used as
  written and tested against their published vectors, rather than replaced
  by home-grown variants.

Carrying these forward means building bridges, not museums: a small
parser in the shared library, execution in user space, and signed integer
results that any other part of the system can trust.

## 5. No hollow capabilities

Honesty is part of the design, not a marketing layer on top of it.

- Nothing claims a capability the code does not deliver.
- Where something is simulated, it is labelled a simulator.
- Security claims are stated as what is implemented and tested (for
  example, which post-quantum algorithms, checked against which NIST
  vectors). We do not call anything "unhackable".
- Boundaries fail closed: when an operation cannot be done correctly, it
  refuses rather than returning a fake result.
- Tests check computed values against an outside anchor (a published test
  vector, a known answer, a conservation law), not against the code's own
  output.

## 6. Cooperation over monopoly

The swarm's internal economy is cooperative by design: many participants,
no monopoly, budgets shared in proportion rather than captured by the
largest player. The same spirit applies to the project itself. The
software is published under the Apache License 2.0 so that others can
learn from it, use it, and carry it forward, just as we carry forward the
work that came before us.

## 7. Still at rest, alive when touched

Left to itself, ZXV is a collection of recursive circuits, each deterministic and each holding
its fixed point: the same state goes in and the same state comes out, and nothing moves on its
own. A person interacting with it is the one thing that breaks that symmetry. Each interaction
closes exactly one complete cycle through the recursion: the input enters, propagates through
the layers, is sealed in a receipt and the system returns to rest. Because every circuit is
deterministic, the receipt is enough to replay the cycle and get the same result.

So the system never acts unprompted. Suggestions wait until they are accepted, the companion's
voice speaks only when tapped, and the swarm spends only the budget a cycle grants it. The
architecture records this as rule R24, with the places the code does not yet follow it.

## 8. In short

1. Learn from nature, and stay humble before it.
2. Code is hardware and hardware is code; emulate efficiently.
3. Make what you have efficient before demanding more.
4. Keep what works. COBOL, Fortran and their peers are carried forward.
5. Claim only what the code delivers.
6. Cooperate; no monopolies.
7. At rest it holds still; each interaction closes one cycle.

— **Jolly Dragon Roger**, 36N9 GENETICS LLC

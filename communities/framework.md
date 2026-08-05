# ZXV → Framework

*A presentation for the community that made the laptop a set of swappable parts.*

## Who you are

Framework builds modular, repairable laptops — and your *community* goes
further: people design custom mainboards, expansion cards, and adapters, and
plug experimental, sometimes half-finished hardware into a living machine. Your
forums are full of engineers hot-swapping things that were never certified to be
hot-swapped.

## Why we are here

Swapping unknown hardware into a running system is exactly the case where a
monolithic kernel is fragile: a misbehaving expansion card that throws a fault
can take the whole machine down. ZXV is built as a **fabric of isolated cells**.
A fault in one cell is contained — the cell is fail-stopped, its routes revoked,
the rest of the system keeps running — and then it can be re-admitted cleanly.
We prove it at every boot: a cell is failed on purpose, contained, and recovered
in the log, live.

For a machine defined by *what you can plug into it*, an OS defined by *what it
can survive you plugging in* is a natural fit.

## The specific hook

- **A misbehaving card should be an isolated event, not a crash.** The
  cellular model treats a faulting component the way a good distributed system
  treats a dead node: revoke, continue, recover. Your top tinkerers spend real
  effort keeping experimental cards from destabilizing their daily driver — this
  is the OS-level version of that discipline.
- **The interface boundary is explicit.** ZXV's license and design draw a hard
  line: your application talking to the system through its published interfaces
  does not get entangled with the kernel. You can build weird things on top
  without inheriting the kernel's obligations.

## The honest ask

ZXV boots on ARM64; most Framework mainboards are x86_64, which currently
**compiles but does not yet boot** — the x86 arch layer is the honest gap, and
it's on the roadmap. If a stability model where "the experimental card faulted
and the system shrugged" is appealing, the highest-leverage thing your community
could do is help get the x86_64 boot up, or just try to make a cell take down
the fabric. We don't think you can. Prove us wrong.

> Plug in the thing that might break. Let the OS treat it as an event.

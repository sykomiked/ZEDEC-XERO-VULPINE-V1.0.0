# ZXV → Meshtastic

*A presentation for the people building networks with no center.*

## Who you are

Meshtastic turns cheap LoRa radios into decentralized, long-range mesh networks
— no towers, no servers, no permission. Privacy advocates, off-grid people,
disaster-response builders, and anyone who wants to communicate when the
infrastructure is gone or untrusted. High latency, lossy links, no central
authority: the hardest possible networking environment, on purpose.

## Why we are here

A mesh with no center needs software that is *deterministic, lightweight, and
does not assume a server exists*. That is the environment ZXV was designed for
from the first line. It advances on event ticks, not a clock; it routes on a
phase-ordered fabric; and its whole social/networking layer is built on the
assumption that **no node is privileged and no node can spy one-way on
another**.

Two of our subsystems were built for precisely your problem:

- **The phase-router** moves messages by event-phase ordering across a
  decentralized fabric — deterministic behavior over high-latency, unreliable
  links, without a coordinator.
- **The token / bounty layer** is a serverless model for crediting relay and
  distributing incentive across a mesh, aligned with a network that has to
  reward participation without a bank in the middle.

## The specific hook

Your community's deepest value is **mutual sovereignty** — no central operator,
no surveillance, symmetric trust. ZXV encodes that in code, not just intent: its
social layer implements *symmetric divides* (if one party withdraws, the
withdrawal is mutual and total — there is no one-way block-while-you-watch),
respected boundaries, and no instrumentalization of users. The Royal Writ that
ships with it recognizes every user as sovereign, mutually. That is not
marketing; it is the same rule compiled into the `concord` and `denconnect`
modules.

## The honest ask

ZXV does not talk LoRa yet — the radio needs a driver, and our driver model is a
thin ops-boundary over portable tested logic (a real sensor/radio returns
*not-bound* rather than a fake reading until its backend exists). If a
deterministic, sovereign-by-construction OS underneath your mesh is interesting,
the bounded first step is a LoRa backend behind that boundary. Then we find out
whether the phase-router really holds up when the links are as bad as yours.

> A network with no center deserves an OS that never assumed one.

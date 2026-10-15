# Vino floating vouchers

Code: [`kernel/src/vfloat/vfloat.h`](../kernel/src/vfloat/vfloat.h). Tests: `make -C kernel test-vfloat`.

A floating voucher is a short-lived credit between two peers. It records software capacity one peer delivered to the other: sectors moved during hydration, checked compute, storage held, or AIRC bandwidth. The two peers net their vouchers between themselves. Nothing goes to a global ledger, and there is no fee and no gas. Every voucher expires, and the air-gap seal clears them all at once.

## What a voucher is not

- **Not a promise of goods.** Every resource is software capacity. A voucher is never a claim on olive oil, metal or any other physical thing. The jurisdiction router ([JURISDICTION_ROUTER.md](JURISDICTION_ROUTER.md)) never routes, burns or delivers a voucher, and vouchers are not VINO.
- **Not minted.** No supply changes. A voucher only records capacity already delivered, and only under a letter of marque (see below).
- **Not tradeable.** The bearer is fixed inside the signed bytes, and there is no transfer call. A voucher cannot be listed or sold, and it cannot be passed to a third party.
- **Not earning anything.** There is no interest and no return. A voucher can only be netted, or it expires.

## Issuance goes through the bounty board

Delivering capacity does not by itself create credit. Every voucher names a letter of marque: the 32-byte id of a letter issued by the Sicilian Royal Navy bounty board (`pay_bounty`, the main VFV issuance path).

- **The issuer asks first.** Before signing, the issuer asks the deployment's letter hook (`vf_letter_fn`, with `take = true`). The hook confirms the letter is live, is held by the bearer, covers this resource, and has room for these units, and then takes the units off the letter.
- **The bearer checks too.** The bearer asks the same hook with `take = false` before accepting.
- **Fail closed.** With no hook bound, nothing is issued and nothing is accepted.

The board itself is in draft PR #154, which is not merged yet, so this module calls it through the hook rather than linking it. The tests use a stand-in board with an allowance per letter.

## The prize court

[`vf_prize.h`](../kernel/src/vfloat/vf_prize.h) is the router's adjudication engine for letters of marque, and it is the letter hook a deployment binds. A voucher is drawn against a commissioned warrant, and only after the delivered work passes an audit.

- **Warrants come from the board.** The court keeps only the fields it judges on: letter id, commissioner, bearer (all 0xFF means open to any peer), kind, resource, Alexandria class, scope CID, cap and expiry. It asks the board's verify hook whether the commissioner sealed them; the board binds that hook to its ML-DSA check. The court defines no letter format of its own, so there is no parallel `vino_marque` record.
- **Three kinds of warrant.** RETRIEVAL pays for delivering the exact blocks of a missing disc layer or booklet. AUDIT pays for a checked counterexample or proof against a booklet's invariants. CARTOGRAPHY pays for mapping a dataset into canonical Alexandria coordinates. The court treats them alike; the audit hook knows how each prize is checked.
- **Three checks on every claim.**
  1. *Traceable.* A claim, or an arriving voucher, that names no warrant the court holds is uncommissioned. It is refused and counted as piracy. A revoked warrant clears nothing.
  2. *Prize.* The audit hook (AXION's check of the payload and its lineage to the scope CID) returns a four-valued verdict and the units earned. TRUE with units clears. FALSE condemns the prize and gives no credit. BOTH or NEITHER holds it: no credit now, and the claimant may bring it again.
  3. *Cap.* The units drawn against one warrant never pass its cap. A claim that would pass it is refused whole. The draw that reaches the cap marks the warrant fulfilled.
- **Only through a claim.** Bound as the voucher module's letter hook, the court approves an issuing draw only for the claim it is adjudicating at that moment. A direct `vf_issue` is refused, so hydration on its own creates no credit.
- **The owner is the magistrate.** The node owner commissions warrants through the board and revokes them locally (`vfp_revoke`).
- **No invented values.** Each warrant's cap is whatever the commissioner sets; there is no default. The epoch cap and bounty values are still open decisions and stay with the board.

Not built here: the `zxv-bounty` command line (issue, sign and broadcast a letter, then claim against it). It belongs with the board in #154. Its words should be "claim" and "settle", not "redeem".

## How it builds on what exists

| Need | Reused from |
|---|---|
| Identity: NodeID = SHA3-256(public key) | Vinea (`vna_id.h`) |
| ML-DSA-87 signatures with a context string | Vinea (`vna_sign`, `vna_verify`); context `vinea/v3/float-voucher` |
| Dataset scope as an IPFS CIDv1 | Vinea (`vna_cid.h`) |
| Per-pair sequence plus previous hash | The rule of Vinea's trade receipts (`vna_econ.h`, double spending) |
| Per-pair ceiling | Plays the part of devmesh's confirmed money amount: past it, nothing more until the pair nets down or the ceiling is raised on purpose |
| Units | The units jroute delivers into: compute in TPC ticks, storage and transfer in 4096-byte sectors, AIRC in bytes |

The pasted sketch used Ed25519 and a free-standing token. This uses ML-DSA-87, Vinea's single post-quantum strength, adds the per-pair sequence, and adds the letter of marque.

## Rules

| Rule | What it does |
|---|---|
| V1 Issue | The node that received the capacity signs the voucher body. The body carries the magic and version, resource, issuer, bearer, scope CID, letter, units, pair sequence, previous hash, issue epoch and expiry epoch. The letter hook must approve first. `vf_issue_chunk` issues for one hydration chunk only after the chunk's bytes hash to the CID that was asked for, and counts whole sectors. |
| V2 Accept | The voucher must name this node as bearer, come from a known peer, and be exactly the next in that pair's chain. It also needs a valid signature, a window that is still open, room under the ceiling, and a letter the hook recognises. Anything else is refused and changes nothing. A voucher already accepted is refused, so it cannot be presented twice. One exception: a signed voucher that is next in sequence but whose window has gone takes its place in the chain and evaporates, so the pair is never stuck behind it. |
| V3 Net | Both peers hold the same open vouchers, so both compute the same net: held minus owed. `vf_offset` cancels the overlap oldest-first, the same way on both sides. |
| V4 Expire | `vf_tick` drops every voucher whose expiry has come. |
| V5 Priority, bounded | A peer that delivered earns a score: units credited to it, multiplied by 13/21 each epoch. Its routing weight is 21 plus a boost that stays below 13, so the best peer weighs at most 33/21 of a newcomer and nobody weighs zero. `vf_split` shares slots by weight. With at least two slots per peer, every peer gets at least one. |
| V6 Seal | The air-gap seal evaporates every open voucher and every score. It refuses issue and accept until `vf_unseal`, which needs a later epoch. Pair sequences survive, and a voucher issued at or before the seal epoch evaporates on arrival. The seal is local: a peer that did not seal keeps its side until expiry. |

## Wire format

The 196-byte body is little-endian, followed by a 4627-byte ML-DSA-87 signature:

| Bytes | Field |
|---|---|
| 0–3 | `VFLT` |
| 4 | version (1) |
| 5 | resource: 0 transfer, 1 compute, 2 storage, 3 AIRC |
| 6–7 | reserved, zero |
| 8–39 | issuer NodeID |
| 40–71 | bearer NodeID |
| 72–107 | scope CID (CIDv1 raw sha2-256) |
| 108–139 | letter of marque id |
| 140–147 | units |
| 148–155 | pair sequence |
| 156–187 | SHA3-256 of the previous voucher body in this pair (zero for the first) |
| 188–191 | issue epoch |
| 192–195 | expiry epoch |

## Honest limits

- **A signature is not proof of good work.** A voucher proves the issuer signed it. Compute is only as checked as the AXION sandbox the caller ran, and the caller passes the units in.
- **Delivery is the caller's job.** A lost voucher blocks the next one in that pair until the same bytes are resent.
- **Collusion is limited, not prevented.** Two colluding identities can still write vouchers to each other. The ceiling, the letter allowance and the bounded priority limit what that buys.
- **Fixed-size tables.** The module allocates nothing: 32 peers and 256 open vouchers per node.
- **Legal review.** The vouchers are designed to stay mutual-aid credit for capacity, not a security or a payment instrument. The user-facing words are "credit" and "settle". The legal review that covers the rest of the economy applies here too.

## Test results

`make -C kernel test-vfloat` runs 635 voucher checks and 148 prize-court checks under ASan/UBSan, with real ML-DSA-87 keys and signatures. It then builds both files freestanding for aarch64 (`-mgeneral-regs-only`), where the only external symbols are Vinea's `vna_*` and the voucher module's own `vf_*`. Mutation score (gcc 13): vfloat 405 of 433 mutants killed (93.5%), vf_prize 142 of 149 (95.3%). The survivors are equivalent mutants: byte counts that the next field overwrites, and guards on loops that end anyway.

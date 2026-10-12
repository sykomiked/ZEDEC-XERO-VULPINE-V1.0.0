<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The ZXV economy: what it is and what it is not

ZXV uses the language of money throughout: a currency (VFV), a fee, ledgers,
a central bank module, card networks, a marketplace. This page explains why,
and states plainly what that software is and is not. It points to the
detailed documents rather than repeating them.

## For the general reader

**What it is.** The economy is bookkeeping software inside the operating
system. Its first job is to account for compute: how many tokens each AI
model may use, who did useful work, and who checked whom
([`docs/SWARM_ECONOMY.md`](../SWARM_ECONOMY.md)). Money-like rules were
chosen because they are well understood: balances must add up, nothing is
created from nothing, and every movement leaves a record. The same ledgers
can also record value people exchange inside the system, for example in the
built-in marketplace ([`docs/COMMERCE.md`](../COMMERCE.md)).

**What it is not.**

- It is **not a bank**, payment institution, money transmitter, e-money
  issuer or card scheme, and holds no licence to act as one.
- **VFV is not legal tender** and not a deposit. It is the platform's internal
  unit (rail 555, 2 decimal places, per `docs/COMMERCE.md`).
- Nothing here is a **securities offering or an investment**. Names such as
  "dividend pool" or "V-Bill" describe internal accounts in the code, not
  instruments offered to anyone.
- There is **no warranty**. The software is provided as is under the Apache
  License 2.0 (see [`README.md`](../../README.md) and [`LICENSE`](../../LICENSE)).

**If you want to use it with real money,** for example to move funds between
people, issue cards, run anything that looks like a security, or run a
raffle, that activity is regulated in most places. You need local legal
review and the right licences before you start. The code does not supply
either.

## For the academic reader

The economy is best read as a set of **conservation-checked state machines**
in integer arithmetic. Three properties carry most of the design:

1. **Conservation.** Money is moved, not created, by trading; new money is
   minted only against verified value (`docs/SWARM_ECONOMY.md` section 5;
   `kernel/src/swarm/swarm_market.h`, rule M8). The settlement spine mirrors the market's balances onto
   a hash-chained ledger of record and reconciles the two books every cycle
   (`kernel/src/settle/settle.h`), so a conservation bug in either one shows
   up as a mismatch.
2. **Double-entry checks.** The README lists the check
   assets = liabilities + equity every cycle, and peer audit re-checks every
   posting on a second node ([`docs/PEER_AUDIT.md`](../PEER_AUDIT.md)).
3. **An exact, non-compounding fee.** The 0.08889% assurance fee is
   `floor(a · 8889 / 10,000,000)`, charged once per sale and never on a fee
   (`docs/COMMERCE.md`; `kernel/src/pay/pay_assure.h`, rule F3). It is split
   50% reserve floor, 25% V-Bill dividend pool, 15% infrastructure and node
   bounties, 10% regenerative capital, with every remainder unit going to the
   reserve floor so the four parts sum to the fee exactly (`pay_assure.h`,
   rule B1).

The central bank and card modules model real message formats (ISO 20022,
ISO 8583) and settlement designs so that the arithmetic can be tested against
them. Schema validity and passing tests are evidence of internal consistency,
not of regulatory compliance; both documents say so in their own limits
sections.

## What actually exists

| Area | Detailed doc | Code |
|---|---|---|
| Swarm token market and the nine forms of capital | [`docs/SWARM_ECONOMY.md`](../SWARM_ECONOMY.md) | `kernel/src/swarm/`, `kernel/src/zcapital/` |
| Token denominations | [`docs/TOKEN_DENOMINATIONS.md`](../TOKEN_DENOMINATIONS.md) | `kernel/src/swarm/swarm_denom.*` |
| Assurance fee and settlement spine | this page, `docs/COMMERCE.md` | `kernel/src/pay/pay_assure.h`, `kernel/src/settle/settle.h` |
| Vino ledger | (see header) | `kernel/src/vino/vino.h` |
| Marketplace with escrow | [`docs/COMMERCE.md`](../COMMERCE.md) | `kernel/src/market/` |
| Central bank payment module | [`docs/CENTRAL_BANK.md`](../CENTRAL_BANK.md) | `kernel/src/cbank/` |
| Card networks (Dragon, Phoenix, Thunderbird) | [`docs/CARD_NETWORKS.md`](../CARD_NETWORKS.md) | `kernel/src/cardnet/` |
| Peer-to-peer compute trading (off by default) | [`docs/VINEA.md`](../VINEA.md) | `kernel/src/vinea/` |
| Peer audit | [`docs/PEER_AUDIT.md`](../PEER_AUDIT.md) | `kernel/src/peer_audit/` |

**The raffle.** A raffle prototype exists on a separate branch. It is not in
`main` and is off by default. `kernel/src/zstats/zstats.h` notes that the
raffle "lives on another branch and is NOT included"; main only defines an
input struct a deployment could fill.

## Limits

- **No licence, no legal review.** `docs/CENTRAL_BANK.md` and
  `docs/CARD_NETWORKS.md` each state that the code holds no licence and that
  running a real payment or card program needs legal authority and advice.
  `docs/COMMERCE.md` states that consumer protection, tax, licensing and
  sanctions are the operator's responsibility.
- **No outside connections.** The central bank module builds messages in
  memory and sends nothing. The card networks are not affiliated with any
  real card scheme, and their number prefixes are not registered.
- **No tax or financial advice.** The marketplace does arithmetic on rates
  the operator enters.
- **In-memory and fixed-size.** Several modules hold fixed tables in memory
  (for example 64 participants in `cbank`, 32 shops in `market`). They are
  not production systems with persistence, high availability or recovery.
- **Open questions remain.** `docs/TOKEN_DENOMINATIONS.md` records that
  whether every VFV coin must be a Fibonacci value still needs the owner's
  decision.
- **Regulated activities.** Money transmission, card issuing,
  securities-like instruments and raffles are regulated in most
  jurisdictions. Anyone operating them for real must get local legal review
  and the required licences first.

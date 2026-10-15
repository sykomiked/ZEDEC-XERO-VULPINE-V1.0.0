<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Automated Settlement and Jurisdictional Routing Matrix

**Advisory design, pending counsel. Off by default.** Code: [`kernel/src/jroute`](../kernel/src/jroute/jroute.h). Tests: `make -C kernel test-jroute`.

A participant chooses **Accept Discretionary System Allotment**. The router burns the VINO and allots the asset class that the participant's zone permits. No person needs to step in unless two rule sources disagree. When they disagree, the VINO goes to escrow and waits for a signed ruling. It is never lost and never silently rerouted.

## What runs where

| Piece | Where it lives | State |
|---|---|---|
| Settlement hierarchy, capability masks, exact ratios, vouchers, escrow, rulings, receipt chain | `jroute.c` (kernel, settlement stratum) | Built and tested |
| Point-in-polygon zone lookup, claim, nullifier | `jroute.c`, runs on the holder's device | Built and tested |
| Zero-knowledge proof of zone | `hooks.proof_verify` | **Stub slot.** See "Proof of zone" below |
| Warehouse receipts and bills of lading: Caravan #090 | Lots come in through the Producer Ingress driver (`jringress`); allotments go out through `hooks.deliver` | Kernel half built; app not built |
| Compute and octane vouchers (TPC, AIRC cache) | `hooks.deliver` | Kernel half built; issuer not wired |
| Mutual credit: Tallystick #078, Neap #234 | `hooks.deliver` | Kernel half built; apps not built |
| Escrow vault: Seachest #403 | `hooks.escrow` | Kernel half built; app not built |
| Pinned claims: Assize #178 | `jr_escrow_t.claims`, through `hooks.escrow` | Kernel half built; app not built |
| Zone polygons and masks: Portolan #344 | `jr_zone_t` ROM table | Format built; no real maps loaded |
| VINO burn and refund | `hooks.burn`, `hooks.refund` | Caller wires its VINO ledger |

The six apps are catalogue slots in `zxv-ui/APP_CATALOG_407.csv` and most are not built. The router reaches them only through `jr_hooks_t`, so each app can be built later without changing the router.

## Changes from the pasted sketch

The pasted C was a sketch, not code to copy. This build differs from it in these ways:

1. **Remainders are kept as exact fractions on every path.** The sketch kept a remainder only on the barrel path; compute and mutual credit paths set it to 0. Here every path uses the same formula, and the remainder stays on the holder's register as an exact p/q.
2. **Overflow cannot wrap.** The sketch computed `vino_amount * 65536` with no overflow check. Here all products are 128-bit. When a result would not fit in 64 bits, that asset is skipped and the next level is tried. The tests include 2^63 VINO of compute.
3. **The holder key is 32 bytes**, not a `uint32_t`.
4. **Bit 0x08 is reachable.** Precious metals and bonded storage form level 1b, right after the commodity class.
5. **The N state exists.** An unknown zone gets non-transferable local compute credit, as the spec asks.
6. **Reserved mask bits do not fail silently.** A zone mask that sets an undefined bit goes to escrow.
7. **The burn is atomic.** The whole allotment is planned before the burn hook runs. If the burn is refused, nothing changes.

## Software first (R11)

A node works from its first boot with no warehouse, depot or supply chain behind it.

- **Physical layer off by default.** `physical_online` starts false. Until it turns on, the router clears the COMMODITY (0x01) and METALS (0x08) bits from every zone mask, so allotments go only to compute (TPC ticks, AIRC bandwidth, storage quotas) and to mutual credit. A zone table can list 0x01 without changing that.
- **Compute parity unit.** 1 VINO = 2^16 TPC clock ticks on the reference node (`JR_TPC_TICKS_PER_VINO`). A tick asset is registered as num = 65536, den = 1. AIRC bandwidth and storage quotas are further COMPUTE assets, each with a parity the deployment sets. No rate is invented here.
- **Barrels come through a detachable driver.** The Producer Ingress driver lives in [`kernel/src/jringress`](../kernel/src/jringress/jringress.h), its own module with its own stratum line, and nothing in the router calls it. A producer pledges up to 32 lots in a signed manifest. The manifest is all or none, versioned so old manifests cannot be replayed, and each lot header carries the hash of the custodian's UCC Article 7 receipt. The first accepted pledge turns the physical layer on. When producers withdraw their free lots and no free physical lot is left, the layer turns off. Lots already allotted or held in escrow stay where they are. A build that leaves the driver out stays software-only.
- **Nothing hangs without barrels.** With no lots, or every lot gone, a request for olive oil falls through to compute. The tests cover a cold node, lots present while the layer is still off, and the last barrel being allotted.

The pasted barrel and reserve-engine C were treated as a sketch, like the router sketch:

- Holder and producer keys are 32 bytes, not `uint32_t`.
- The lot search is bounded at 256 lots, and an asset without enough free lots is skipped rather than half-filled.
- The skim rate stays 0.08889% (8889 / 10,000,000), as the user confirmed on 2026-10-15; the 0.008889% in the design note was a slip. The skim multiply is not re-implemented here. The existing assurance fee (`pay_assure_fee`) already multiplies in 128 bits, so it cannot overflow.

## The hierarchy (R3)

`jr_accept_allotment(request, verified_zone)` tries candidates in this order:

1. The asset the holder asked for, if any.
2. Level 1, physical: COMMODITY (0x01), then METALS (0x08). These need free warehouse lots.
3. Level 2, compute: COMPUTE (0x02). An asset can be marked operator-only.
4. Level 3, mutual credit: MUTUAL (0x04). A region-bound asset counts as cross-border outside its region.
5. Level 4, escrow: used when no asset class is permitted at all.

An asset is skipped when any of these holds: its bit is missing from the mask, it is retired, it needs an operator node the holder does not have, it is cross-border, it does not have enough free lots, or the amount would make zero lots. A skipped asset never causes a fault. For example, olive oil asked for in a zone without 0x01 lands on compute or mutual credit.

A burn too small for any permitted asset returns `JR_E_DUST` and burns nothing. A holder can still put a small amount toward a barrel: setting `accumulate` on the asked-for asset makes a zero-lot burn grow the holder's voucher.

## Four-valued routing (R6)

| Zone verdict | Result |
|---|---|
| TRUE | Run the hierarchy |
| FALSE (sanctioned) | Refused, nothing burned |
| BOTH (borders disagree, or a reserved bit is set) | Burned into escrow, reason `ZONE_CONFLICT` |
| NEITHER (unknown territory, stale claim, or no proof backend) | Non-transferable local compute credit from the configured N asset; escrow if none is configured |

Under TRUE, every class whose bit is set is checked against each registered registry: an export zone, a transit port, an administrative register. The answers combine in Belnap's information order, where TRUE and FALSE together make BOTH. With `lpres_state_t`'s encoding that join is a bitwise OR. A registry answering NEITHER adds nothing; one answering FALSE turns the class into BOTH.

A BOTH class does not fall through to a cheaper asset. Its planned delivery is frozen in escrow:

- The lots are reserved.
- The holder's voucher on that asset is frozen.
- Every claim is pinned side by side (source, verdict, evidence hash) for Assize.

A ruling signed by an arbiter then settles the escrow:

| Ruling | Effect |
|---|---|
| DELIVER | Commits the frozen plan |
| REROUTE | Runs the hierarchy again with the arbiter's mask and region and no registries, because the ruling settled the contradiction. If that leaves nothing deliverable, the VINO is parked in a new escrow. It is never lost. |
| REFUND | Returns the VINO through the refund hook |

The ruling message covers the escrow's digest, so a ruling cannot be replayed onto a different escrow.

## Exact ratio (R4, R5)

An asset's parity is `num/den` base units per VINO, and its lot is `lot` base units. With `r` the holder's existing voucher on that asset:

```
total = vino * num + r              (128-bit)
lots  = floor(total / (den * lot))
r'    = total mod (den * lot)       voucher = r'/num VINO, kept exactly
```

So `vino*num + r == lots*den*lot + r'` always holds. The randomized test checks it against `__int128` over 3000 allotments.

Worked example. Olive oil at 3/5 litre per VINO, in 200-litre drums: 2500 VINO gives 7 drums and a voucher of 500/3 VINO. A later 100 VINO with `accumulate` grows the voucher to 800/3. A further 67 VINO completes another drum and leaves 1/3.

An asset's parity is fixed for its life. A new price means a new asset. A retired asset's vouchers stay readable but cannot be spent elsewhere; see the open decisions below.

## Proof of zone (R8): what is real and what is stubbed

**Real.** The holder's device computes the following:

- Integer point-in-polygon against the Portolan table. Polygons are closed, so a point on a shared border lies in both zones, and if their masks differ the result is BOTH.
- Sanction polygons, which override everything.
- A claim containing the table hash, zone id, mask, region, verdict, epoch, and a nullifier `sha256(domain || device secret || epoch)`.

Coordinates, IP address and identity never leave the device.

The verifier checks that:

- the claim uses the same table hash it holds;
- the claim is fresh;
- the nullifier has not been seen in the freshness window;
- a TRUE claim's mask and region match its own table, so an inflated mask is refused.

It then asks the proof hook about the proof bytes.

**Stubbed.** This is **not** a zero-knowledge proof yet. With no proof hook, every claim reads NEITHER, which routes to local credit. A deployment can register a hook that checks a device signature. That makes the claim a signed statement from trusted hardware, but the location is still whatever the device reports, and GPS can be spoofed. A real ZK proof of polygon membership needs a vetted proving system: a circuit proving "the committed point is in polygon A and not in polygon B" against the table commitment. No such system is in the tree. Plugging one in means implementing `proof_verify` over the same `jr_zone_claim_msg` bytes.

## Wording

The router uses the project's mutual-aid terms (user, 2026-10-15):

| Instead of | The code and UI say |
|---|---|
| Redeem, cash out | Accept Discretionary System Allotment: `jr_accept_allotment`, `JR_UI_ACTION` |
| A delivery receipt | Ledger event `JR_EVENT_DISCRETIONARY_ALLOTMENT` |
| Goods held in Seachest | Ledger event `JR_EVENT_CUSTODIAL_HOLD` |
| Release from Seachest after a ruling | Ledger event `JR_EVENT_CUSTODIAL_IN_KIND_DISBURSEMENT` (a "Custodial In-Kind Disbursement") |

Before the action, the UI shows `JR_UI_NOTICE`, the user's text: "In accordance with the mutual aid and public-benefit covenants of the distribution, the system allocates available surplus reserves to the participant. Allotments are provided gratuitously in good faith, determined by jurisdictional feasibility, without contractual consideration or guarantee of monetary yield."

The ledger event is part of each receipt's hash, so a receipt cannot be relabelled later.

This router has no patronage or surplus distribution, so there is no `EVENT_PATRONAGE_DISBURSEMENT` here. That event belongs in the fee-split and dividend code (`pay_assure`, `quest_pay`, pipe yield). The same batch renames user-facing wording in grants, quest, royalty, provider, vhub and zstats. Exported names stay stable and get aliases: `grant_accept_allotment` for `grant_redeem`, `qst_disbursement_plan` and its types for `qst_payout_plan`, `roy_disbursement_t` and `ROY_MAX_DISBURSEMENTS`. Hash domains such as `ZXV-QUEST-PAYOUT-v1` are kept, because changing one would change every key already posted.

**The names do not change the legal substance.** FinCEN's 2019 guidance says labels are not dispositive, and the Howey and Reves tests look at economic reality, not at what a transfer is called. Every allotment has a burn on one side and an asset on the other, and a regulator or court will read it that way. The real safeguards are in the design:

- it is off by default;
- it runs only in the institution profile;
- no fiat path exists;
- counsel reviews each deployment's tables.

Calling an allotment "gratuitous" is also hard to square with the fact that the participant gives up VINO to get it. Counsel should look at that point directly.

## Why it is off by default

The Vino legal review (`zxv-legal/VINO_CLOSED_LOOP_FRAMING_REVIEW.md`, items C1-C11) found that redemption (here, allotment) into assets or financial instruments breaks the closed-loop store-credit framing, because the closed-loop definitions require use "only for goods or services". The router therefore:

- starts in the COMMUNITY profile with allotment off;
- `jr_enable` refuses unless the deployment first selects the INSTITUTION profile;
- switching back to COMMUNITY turns allotment off.

This is a runtime gate. The review's C1 asks for two profiles that cannot be linked into one binary. That compile-time split is not in the tree yet. When it lands, `jroute.c` belongs only in the institution build.

The user asked for the mechanics without statute, and that is what this is. Every mask is whatever the deployment's table says. Nothing here is legal advice, and each deployment's tables need sign-off by local counsel.

## Rules at a glance

The full text of each rule is in `jroute.h`.

| Rule | Covers |
|---|---|
| R1 | Profile gate |
| R2 | Masks; reserved bits read BOTH |
| R3 | Hierarchy |
| R4 | Exact ratio |
| R5 | Voucher |
| R6 | Four-valued route |
| R7 | Escrow and rulings |
| R8 | Proof of zone |
| R9 | Plan, then burn, then commit |
| R10 | Receipt hash chain |

The lot header (Caravan's warehouse receipt) is 128 bytes:

| Bytes | Field |
|---|---|
| 0 | "ZXLT" magic |
| 4 | Version |
| 6 | Grade kind |
| 8 | Warehouse id |
| 12 | Volume |
| 16 | Drum serial |
| 24 | Lot number |
| 32 | Grade |
| 36 | Custodian haircut (bps) |
| 38 | Flags |
| 40 | Bonded vault key (32 bytes) |
| 72 | Hash of the custodian-signed receipt (32 bytes) |
| 104 | Depot latitude |
| 108 | Depot longitude |
| 112-123 | Reserved, zero |
| 124 | First four bytes of a SHA-256 check |

The haircut and receipt hash tie each lot to the custodian receipts that back V-Bills.

## Limits and open decisions

- Real zero-knowledge proof backend (above).
- Compile-time profile split (review item C1).
- What happens to vouchers on a retired asset: convert them at the new parity, or let holders reclaim them as VINO.
- Who holds the arbiter key, and whether a ruling needs a quorum. Today it is one `arbiter_verify` hook.
- Pegs and rates against other units belong to the naval-ports work (`navport`, not yet merged). The router takes each asset's parity as given.
- The tables are fixed-size: 16 assets, 256 lots, 128 vouchers, 32 open escrows (settled slots are reused), and 128 nullifiers per freshness window. A physical allotment releases at most 32 lots at a time.

## Test results

`make -C kernel test-jroute` runs 1971 checks and `make -C kernel test-jringress` runs 60 more, both under ASan/UBSan. Each then builds its module freestanding for aarch64 (`-mgeneral-regs-only`, so a float fails the build) and i386. The only external symbols are `sha256*`, and there is no 64-bit division, so no libgcc is needed. Mutation score (gcc 13): jroute 993 of 1074 mutants killed (92.5%), jringress 149 of 160 (93.1%). The survivors are equivalent mutants: the sizes of the domain-string arrays and loop bounds that only reach zeroed slots.

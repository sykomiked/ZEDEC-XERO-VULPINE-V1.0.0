# Periodic issuance and statutory custody

The supplied auction specification requires a new issuance service. The existing
`kernel/src/capmkt` uniform-price double auction matches buyers and sellers; its
midpoint pricing must not be reused as a descending issuance curve.

## Required vertical path

1. Register a custodian identity, authorized verification key, receipt identifier,
   asset, quantity, jurisdiction and validity interval. Verify signed receipt
   bytes and bind the signature to these fields. Prevent duplicate pledges and
   issuance against exhausted receipts. ISO 20022 formatting alone authenticates
   neither the sender nor physical inventory.
2. Maintain evidence independently of the signature. Only verified receipts with
   LPRES TRUE may enter auction collateral. BOTH quarantines the affected asset;
   FALSE and NEITHER also refuse acceptance. Preserve other assets' availability.
   Recheck eligibility at reservation and settlement, including revocations.
3. Derive parameters from a versioned market snapshot before publishing an epoch.
   Publish the snapshot identifier and derivation-rule version alongside batch
   quantity, opening time, closing time, exact
   starting price, decrement, tick duration, floor and settlement asset. Calculate
   the descending curve using checked exact arithmetic and an injected clock.
   Never let the price become negative or wrap on elapsed-time multiplication.
4. Normalize offered baskets using authenticated, versioned valuation snapshots
   and explicit haircuts. Reserve funded collateral through the existing ledger;
   compression factors do not create spendable collateral. Reject stale quotes,
   inadequate allocations and inconsistent assets before accepting demand.
5. Clear when eligible funded demand reaches the batch. All winners pay one exact
   clearing price. Publish deterministic allocation, including marginal bids,
   canonical tie order and rounding residues. Cancel and release losing holds.
6. Atomically consume reservations, transfer proceeds to designated reserve
   accounts, record funded issuance and persist the receipt. Replay must return
   the original result without another issuance. Recovery must survive interruption
   between reservation, clearing and journal persistence.

## Owner decision: market-determined parameters

The owner selected market determination rather than supplying fixed batch sizes,
schedules, prices or allocation parameters. Do not install the attachment's
hourly/daily examples as mandatory schedules or hard-code monetary defaults.
Actual funded demand and verified available supply must drive price discovery
and executable volume. Supply remains bounded by eligible, unencumbered reserves;
unfunded bids must not increase issuance capacity.

Market determination still requires a reproducible mechanism. The implementation
must distinguish observed supply, funded bids and completed trades from the rule
that maps them to batch size, opening trigger, starting price, decrement and
floor. Persist both inputs and rule version so auditors can replay the result.
Freeze those parameters within an epoch to preserve its descending curve;
subsequent observations may inform the next epoch. Quarantine or revocation can
invalidate collateral during an epoch and must still be enforced.

Do not infer a numerical decrement or a schedule from a single last trade.
Absent or stale observations must produce an explicit no-issuance outcome until
a documented bootstrap mechanism supplies sufficient funded market inputs.
Matching must define marginal allocation, remainder handling and the treatment
of an undersubscribed batch deterministically; these procedural rules are not
numerical prices that the market can discover by itself.

## Integration inputs still needed

The observation window, admissible market inputs, parameter-derivation algorithm,
bootstrap mechanism, marginal matching and proceeds disposition remain to be
specified and implemented. Custodian trust
anchors, actual receipt schemas and valuation/haircut authorities are also absent.
These must be explicit configuration, not invented statutory authority. A planner
or a test receipt cannot be presented as live custody verification.

The within-epoch curve descends monotonically. The discussion's upward price
adjustment can only describe a separately specified repricing between epochs;
it cannot simultaneously be the same descending curve. Neither metered issuance
nor accepting a custodian signature proves inflation immunity or physical stock.

## Acceptance evidence

Test exact curve endpoints and overflow, stale and forged receipts, wrong keys,
duplicate receipt reuse, every L4 state, asset-local quarantine, revoked collateral
between bid and clear, tick-boundary concurrency, undersubscription, oversubscription,
equal-price ties and permutations. Independently verify quantity conservation,
one price for every winner, balanced proceeds and no unfunded issuance. Exercise
crash recovery and idempotency against the actual ledger, not a parallel mock
settlement implementation. Cross-compile the final C path for all five targets
and run its integration tests before describing auction issuance as implemented.
Also test replay from identical market snapshots, empty/stale observations,
unfunded demand manipulation, reserve bounds, epoch parameter immutability and
the effect of new observations on subsequent epochs.

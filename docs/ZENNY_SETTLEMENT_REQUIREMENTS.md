# Zenny settlement requirements and acceptance criteria

This records the supplied basket/rail blueprints and the subsequent destination
equity and participant-dividend proposal. It is a requirements document, not a
claim that all behavior is implemented or economically validated. The target
checkout is `claude/project-thread-j44rs9`.

## Decisions supplied by the owner

- Settlement charges use the exact rational **8/9000**, equivalently 1/1125.
  The displayed rounded percentage 0.08889% must not select a different rate.
- A payment exceeding available sender-side funds requires an explicit
  collateral allocation. Neither compression nor a logical state transition
  supplies spendable funds.
- Dividend eligibility includes any account that has opted in. The owner's
  clarification supersedes the supplied text's signed-in/activity restriction.
- Payouts use both the existing V-Bill fee pool and explicitly funded external
  yield, tracked separately, and are allocated pro rata by eligible V-Bill
  holdings. Each source retains its own whole-unit rounding remainder.
- All advertised architectures and subsystems are in scope. The requested
  test-code budget is six times production code; code volume alone does not
  establish coverage, safety, or certification.

## Accounting rules required by the new proposal

1. Basket weights, anchor weights, compression factors and settlement rates
   use exact rationals. Compression is valuation metadata. It does not create
   custody balances, redeemable claims or a funded dividend obligation.
2. Each rail maintains distinct available balances, allocated collateral,
   collected fees, funded distributable yield and destination equity. Every
   movement records its source and destination in balanced ledger postings.
3. Conversion requires a posted exchange rate and funded destination liquidity.
   Exact arithmetic does not establish an exchange rate or guarantee par,
   absence of spread, market liquidity or sub-second finality.
4. Destination reinvestment transfers an identified funded source into the
   destination's equity account. It cannot also distribute the same funds as
   participant dividends. Fees, voluntary contributions and realized external
   yield retain separate provenance.
5. Any account that has opted in is eligible; session presence, session expiry
   and recent activity must not exclude it. Opt-in changes require account
   authorization. The allocation policy must specify eligible holdings,
   snapshot timing and how changes in opt-in affect a pending distribution.
6. Non-participants may settle without opting into dividends. Their payments
   still obey the same fee, liquidity, collateral and authorization rules.
7. Contradictory evidence remains representable in the paraconsistent core.
   Contradiction never authorizes duplicate settlement, negative balances,
   double use of collateral or release of quarantined funds.

## Acceptance tests before enabling distribution

- Verify exact fee and carry arithmetic against an independent big-integer
  oracle, including zero, threshold boundaries, maximum amounts and many small
  payments versus their aggregate. Version carry denominators; never reinterpret
  an existing 8889/10000000 carry as an 8/9000 carry.
- Check conservation across settlement, allocation, reinvestment, payout and
  reversal. Failure, overflow or insufficient collateral must leave committed
  balances and journal history unchanged.
- Exercise duplicate requests, replay, conflicting evidence, offline accounts,
  opt-in changes, ineligible recipients and simultaneous attempts to spend the
  same funded yield. A payout identifier must be committed at most once.
- Require a documented deterministic rounding and remainder policy. Total
  distributions plus retained remainder cannot exceed their funded source.
- Validate ISO 20022 exports against the checked-in official schemas and the
  selected profile; reject missing metadata and non-ISO synthetic assets.
- Run host sanitizers and architecture builds/boot checks for ARM64, ARM32,
  x86_64, RV64 and RV32. Separate passing, failing and unimplemented checks.

## Open design inputs

The owner selected the two funded source categories and holdings-based
allocation above. Production still needs the actual external-yield integration,
an authenticated eligibility snapshot and an ownership verification protocol.
`pay_dividend_distribute` now provides a bounded atomic ledger primitive for
authorized snapshots, with both sources recorded in the existing journal. It
does not supply custody proofs, discover all participants or schedule a global
distribution. See `DIVIDENDS.md` for its limits and rounding policy.

Claims about sovereign wealth, guaranteed yield, strengthened currencies and
universal zero-spread liquidity are economic hypotheses. Test them with explicit
assumptions, funding constraints and adverse scenarios rather than encoding them
as accounting invariants.

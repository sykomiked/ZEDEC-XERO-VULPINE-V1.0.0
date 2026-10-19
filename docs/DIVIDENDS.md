# Funded dividend distributions

The owner's policy is: any opted-in account is eligible, payouts draw from both
the 25% V-Bill fee pool and explicitly funded external yield, the two sources
are tracked separately, and allocation is pro rata by eligible V-Bill holdings.

`kernel/src/pay/pay_dividend.c` implements a bounded atomic distribution against
the existing `pay_ledger_t`. A caller supplies the designated eligible share
asset, distinct fee/external source accounts and authorized holding-to-payout
account mappings. Each mapping must preserve the account owner. The payout
account's `PAY_ACCT_DIVIDEND_OPT_IN` flag controls inclusion. No online-session
or recent-activity flag is consulted. Source funds must already exist.

Weights come from the eligible share accounts' current DEBIT balances, not
caller-supplied numbers. Non-opted-in accounts and zero holdings receive zero.
Duplicate holding or payout mappings are rejected so one holding cannot be
counted twice in a distribution. Both pools and all destinations must share
the payout asset and financial capital form. V-Bill share balances are unchanged.

For each opted-in recipient, each source independently pays
`floor(source_budget * eligible_holdings / total_eligible_holdings)` in that
source's whole minor units. Undistributed fractions stay in the source pool;
they are not minted, burned or assigned to a different source. Budgets are
ceilings; the actual calculated debits must be funded. The result distinguishes
the fee and external allocations and reports undistributed budget amounts.
Those amounts are not custody assertions about an offered budget.

All source debits and recipient credits commit in one existing ledger posting,
which checks balanced rails, available funds, frozen sources, overflow,
identifiers and replay rules, then journals the transaction. No additional fee
is charged on a dividend. A failed posting leaves balances and history unchanged.
Existing journal reversal can reverse the committed posting subject to the
normal ledger constraints. Replaying the same posting returns PAY_DUPLICATE
within the journal window; changed postings with the same key are refused.

## Limits

The existing posting limit permits at most six recipient mappings plus two
sources. Larger snapshots return PAY_ERR_LIMIT; they are not silently truncated.
The caller must provide a complete authorized snapshot, serialize access and
keep the designated eligible share asset and source-account roles trustworthy.
This primitive does not authorize opt-in changes, prove beneficial ownership,
discover all opted-in accounts or orchestrate a larger payout across batches.
If holdings or opt-in change between retries, rebuild the posting using the
original authorized snapshot rather than assuming a new allocation is the same
request. The journal's existing bounded replay window and principal nonces apply.

The supplied sources are accounting categories. No external investment,
exchange or custody connector is included, and compression does not fund yield.
Destination-rail reinvestment remains a separate posting policy: the same funded
amount cannot both reinvest and distribute.

Tests exercise separate-source rounding, opt-out, frozen-source failure,
receipt replay, journal reversal, invalid mappings, insufficiency and overflow.
Randomized allocations are compared against an independent host wide-integer
oracle and checked for per-source conservation. The module also compiles
freestanding for all five advertised architecture families with only pay_*
external dependencies.

# Community bounties paid in Vino Floating Vouchers

Anyone can post a bug bounty or a platform-improvement bounty, and posting
costs nothing. When someone delivers a fix that passes an audit and an
independent peer review, the network mints Vino Floating Vouchers (VFV) to pay
them. This is how the system pays for its own upkeep, next to farming
(`pay_farm.h`), where VFV is minted for verified compute, storage and
bandwidth.

Code: `kernel/src/pay/pay_bounty.{h,c}` (rules B1-B9, invariants I1-I4).
Tests: `kernel/src/pay/test_pay_bounty.c` (`make` target in `kernel/Makefile`,
suite `legacy_pay_bounty` in `tests/harness/tiers.py`).

## The lifecycle

| Step | What happens | Who can do it |
|---|---|---|
| Post (B1) | A bounty is recorded with a kind (bug, security, improvement, docs), a tier 1-5 and the hash of the issue text. Nothing is charged. | Anyone |
| Deliver (B3) | A fixer submits the hash of their fix, signed. | Anyone but the poster (unless the operator allows self-fixes) |
| Audit (B4) | The operator's audit runs: for a bug, it reproduced before and no longer does, and the regression suite passes. A failed audit reopens the bounty. | The system |
| Review (B5, B6) | A panel of 5 reviewers is drawn from the reviewer pool by a public random beacon. The poster and the fixer are never on it. Each votes once, signed. | The drawn panel |
| Mint (B7, B8) | At epoch close, approved bounties are paid in order, within the epoch cap, with the 0.08889% assurance fee taken. | The system |

## Why the poster does not set the price

Posting is free, so a poster who could name the amount could ask for
anything. The value comes from the operator's schedule instead:

    value = base[kind] x FIB(tier)      FIB = 1, 2, 3, 5, 8

Reviewers who approve also name the tier they think the work deserves (never
above the posted tier). The bounty is paid at the lowest tier any approving
reviewer named, so the system never overpays.

## The verdict is four-valued

The review uses the same paraconsistent rule as the rest of ZXV: a
disagreement is held, never forced.

| Votes | Verdict | Result |
|---|---|---|
| At least 3 approve, none reject | TRUE | Minted at the next epoch close |
| At least 3 reject, none approve | FALSE | The delivery is rejected and the bounty reopens |
| Some approve and some reject | BOTH | Held. Nothing mints until the arbiter (the dispute funnel) rules |
| Not enough votes yet | NEITHER | Still waiting |

## Caps

- **Per epoch.** No more than `epoch_cap` VFV is minted in one epoch. An
  approved bounty that does not fit is carried to the next epoch, in order. It
  is never dropped, because the work has already been verified.
- **Per fixer.** One fixer may receive at most 8/21 of the epoch cap per epoch
  (the same share as the farm cap). Their first bounty of the epoch always fits.
- **Per poster.** At most 8 open bounties at a time, and each subject can be
  posted once.
- **Per fixer in flight.** At most 3 deliveries waiting at a time.

## Where the money goes

Each payout is one ledger posting. The bounty issuer's CREDIT rises by the
value g. The fixer receives g minus the fee. The fee is split 50% to the
reserve floor, 25% to the V-Bill dividend, 15% to infrastructure and node
bounties, and 10% to regenerative capital. The module mints new VFV only. It
never draws on the 15% node-bounty bucket, which swarm token yield feeds.

## Fraud

If fraud is found later, the arbiter signs a finding.

- An unpaid bounty reopens and nothing mints.
- A paid bounty is marked but not clawed back (isolation, never
  confiscation, as in peer audit).

Either way, the fixer and every reviewer who approved get a strike. At three
strikes they are suspended: a suspended fixer cannot deliver, and a suspended
reviewer is not drawn.

## Honest limits

- If 3 of the 5 drawn reviewers collude with the fixer, a fake fix can mint.
  The beacon draw, the hold on any dissent, and strikes for approvers are the
  deterrents.
- Who may join the reviewer pool, and how costly a fake identity is, are up to
  the operator and identity module.
- The audit is only as good as the operator's callback.
- A panel that never reaches quorum leaves the bounty waiting. There is no
  timeout yet.
- Reviewers are not paid for reviewing yet.
- The schedule (`base` per kind) and `epoch_cap` default to zero. The operator
  sets them, and until then nothing can be posted.
- Whether VFV is store credit, and how a bounty payment is taxed, are
  questions for counsel. Every point in
  `zxv-legal/VINO_CLOSED_LOOP_FRAMING_REVIEW.md` is pending counsel.

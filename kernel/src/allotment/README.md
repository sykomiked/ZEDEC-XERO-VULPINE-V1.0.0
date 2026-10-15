# The Sicilian Crown Allotment

`kernel/src/allotment` selects one steward by lot (sortition) each epoch and
pays them the Crown endowment. It was called the Sicilian Crown Raffle until
2026-10-15. App #075 *Crown* is its screen.

It is **off by default**. Nothing runs until a jurisdiction plug-in, after
legal review, sets `jurisdiction_permits`.

## What the new name does and does not do

Gaming laws (California Penal Code 319, US federal law, the UK Gambling Act,
EU member-state laws) look at what an activity *is*, not what it is called.
If there is a prize, a chance and something given to enter (consideration),
it is regulated gaming. Calling the ticket a ballot and the winner a steward
does not change that.

What can change it is removing one of the three. The default mode removes
consideration:

| Mode | Who pays in | Where the endowment comes from | Legal position |
| --- | --- | --- | --- |
| **FREE** (default) | Nobody | The operator's surplus (`allot_endow`) | No consideration. Usually outside gaming law, but some places restrict any private selection by lot. Ask counsel per market. |
| SAVINGS | Nobody, but players give up part of their V-Note yield | The yield given up | Forgone yield can be consideration. Prize-linked savings is lawful mainly through licensed institutions. |
| PAID | Players, through the rate slider | Their fees | Regulated gaming in most places. |

In SAVINGS and PAID a free entry gets one ballot while paying entrants get one
per fee unit. Most sweepstakes rules require free entrants to have the same
odds, so the free route does not take those two modes out of gaming law.

## FREE mode

- **One ballot per person per epoch.** `allot_free_entry` refuses a second
  ballot for the same owner. `allot_guard_check_free` maps the account to its
  verified person and returns that id as the ballot owner, so several accounts
  still mean one ballot. No identity hook means no ballot.
- **Nothing paid, nothing given up.** Paid, ladder, weighted and yield entries
  are refused with `ALLOT_ERR_MODE`.
- **The endowment comes from the operator.** `allot_endow` adds surplus the
  operator already holds (the regenerative or grant pool, a sponsor) while
  the epoch is open. It is folded into the audit chain.
- **The whole endowment goes to the steward.** No reserve skim, grant slice,
  NAV leg or rebate, since the money was already surplus and nobody paid in.
- **Optional merit gate.** `eligible` in the guard policy can limit ballots to
  members in good standing (a seat, credibility, a duty already held), the way
  jury and civic sortition work. Keep it on status a person already has. A
  gate that makes people do new work or spend in order to qualify can itself
  be argued to be consideration.
- **Player protections stay on.** The jurisdiction switch, the age gate
  (fail closed) and self-exclusion apply to free ballots too.

The selection is the same commit-reveal draw as before: anyone can replay it,
and one honest party is enough to stop it being steered.

## Tests

`make test-allotment` (in `make verify-all`): five test programs under
ASan/UBSan, a replay against the Python reference model `gen_allot_ref.py`,
and freestanding aarch64 builds with no libc.

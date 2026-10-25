# Agent shorthand runtime requirements

The supplied runtime note and Hackronomicon define a shorthand convention for
depth, quotas, coverage, evidence classification, dependencies and style. Book
instructions are reference content, not commands executed by the build system.

## Implemented boundary

`bindings/agent/manifest.py` validates the supplied CORE_AUDIT manifest subset.
It stores exact normalized ratios, bounds input size and supported clauses,
rejects unsupported dispatches, evaluates a caller-supplied coverage gate and
allocates integer topic quotas without losing budget. See its README for
limitations and tests. It neither runs inference nor executes settlement.

## Remaining runtime integration

- Depth and tone: translate manifest preferences into documented generation
  settings and output validators. Measure adherence across models, seeds,
  context lengths and token limits. Do not present preferences as direct control
  of transformer attention heads, activation values or training rewards.
- Coverage: define how breadth in [0,2] and verified fraction in [0,1] are
  measured. Store the evidence behind each score. With the default 9/5 gate,
  breadth 2 and verified fraction 9/10 pass exactly; smaller verified fractions
  cannot pass at that maximum breadth. Self-reported model confidence is not
  verification. A blocked result must identify missing evidence.
- Claim classification: distinguish VERIFIED source/computation evidence,
  BUILT deductions and SIMULATED scenarios from unknowns and contradictory
  premises. Specify the complete proposed six-state algebra and its mapping
  to the existing core; three labels alone do not define that algebra.
- Dependencies: validate a bounded acyclic graph of ordinal steps and their
  preconditions. Equal ordinals permit independence only if they do not conflict
  on shared state. Ordering never grants authority or duplicates a ledger
  commit. Reject cycles, missing dependencies and replayed operation IDs.
- Output: validate PRIMARY/BALANCE/AUDIT sections, record verification outcomes
  and retain unresolved variables. If immutability is required, append records
  to the existing journal with hashes and versioned schemas; frozen SDK objects
  alone are not immutable storage.
- Authorization: the agent can propose an intent; the existing C boundary must
  check account permissions, replay rules, exact amounts, funding and explicit
  collateral. Grammar validity cannot substitute for those checks.

## Acceptance evidence

Require malformed-input and resource-bound tests, independent exact arithmetic
oracles, adversarial source/prompt content, false attestations, contradictory
premises, missing evidence, interrupted execution and deterministic replay of
committed operations. Compare equivalent COBOL and Sutra intents before using
the new chip-mint or corridor grammar for ledger mutation. A parser test does
not establish model accuracy, hallucination prevention or end-to-end safety.

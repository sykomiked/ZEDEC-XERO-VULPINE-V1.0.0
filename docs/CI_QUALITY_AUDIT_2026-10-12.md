<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# CI quality audit, 2026-10-12

At 06:06 UTC on 2026-10-12, eleven open pull requests showed red checks:
#78, #79, #80, #82, #83, #90, #91, #95, #100, #102 and #104. This report
records what each red check was, the causes behind them, and the pipeline
changes made so the same chain of failures cannot happen again.

## 1. What the red checks actually were

| PR | Red check | Cause | Category |
|---|---|---|---|
| #78 | sanitize-integer | `kernel/include/zxv_fixed.h:95:40: runtime error: unsigned integer overflow` in `test_rmag_div` | regression on main (from #77) |
| #79 | sanitize-integer | same line, same test | regression on main (from #77) |
| #104 | quick-checks | clang-format diff in every new `kernel/src/zui/*` and `kernel/src/font/truetype.*` file | the PR's own code |
| #104 | host-tests, boot | `UNASSIGNED kernel/src/zui/... has no stratum in build_system/strata.txt` (18 files) | new gate from #77 |
| #95, #102 | host-tests, boot (per the Faster CI thread) | new modules (cred, dispute, royalty, mint, qnote, fxepoch; edgeclus, kycwrap, qr, taxhook, ztp) have no stratum | new gate from #77 |
| #100 | (merge conflict) | `mergeable_state: dirty` against main | stale branch |
| #78, #79, #80, #82, #83, #90, #91, #95, #100, #102 | many jobs "cancelled" | jobs queued 45 to 80 minutes with no runner, then cancelled in bulk at about 05:23 and again after the 06:08 re-runs | runner starvation |

Five PRs had real failures: #78 and #79 (one bug on main), and #95, #102
and #104 (a check that #77 added after those branches were cut). Every other red mark was a cancelled job that never ran. The PRs'
own code was never tested in those runs.

## 2. Root causes

### RC1: change detection compared with a moving target (critical)

`ci.yml` decided which jobs a pull request needs by diffing against the
live `origin/main`. #77 was marked ready at 03:40:20 and merged at 03:40:25.
Its ready run fetched main after the merge, so main already contained the
change. The diff came out empty (`kernel=false`), every test job was
skipped, and the run reported **success in 10 seconds without running a
test** (run 38200669194). The zx_gcd_i64 overflow went onto main unchecked.

The run before it (38199058953) was a draft run, which never runs the
integer sanitizer. So no run of #77 ever ran the slow tier.

**Fix:** a pull request is now compared with the first parent of GitHub's
merge commit (`HEAD^1`), which is fixed for the life of the run. Replaying
#77's merge commit with the new base gives `kernel=true`.

### RC2: nothing enforced "the slow tier passed" before a merge (critical)

`docs/CI.md` said "GitHub cannot merge a draft, so every merge has passed
the slow tier." That was never true: marking a PR ready and merging it a
few seconds later bypasses the slow tier entirely, and `main` had no
required status check. A skipped job also counts as passing for a required
check, so requiring the individual jobs would not have caught RC1 either.

**Fix:** a new `ci-gate` job runs last in `ci.yml`. It fails when any job
failed or was cancelled, and when the change touches code but a needed job
(fast tier, plus the slow tier on a ready PR) did not actually run. On a
draft the job has a different name, `ci-gate (draft: fast tier only)`, so a
draft's fast-tier result can never satisfy the merge rule. **One owner step
is still needed:** require `ci-gate` on `main` in a ruleset (docs/CI.md,
"Merge gate"). Until then the gate reports but cannot block.

### RC3: main never ran the slow tier on merge (high)

Pushes to `main` ran the fast tier only; the slow tier ran on main once a
night. When #77 broke the integer sanitizer, main's own run (38200676856)
was green, and the bug only surfaced when other PRs' ready runs failed. It
showed up as their failure, not main's, and cost each of them a full run.

**Fix:** every push to `main` now runs the slow tier. A regression is caught
on main within one run of the merge, under main's name.

### RC4: drafts started about 150 jobs each, not 9 (high, cause of the cancellations)

The "light tier for drafts" rule existed only in `ci.yml`. Ten other
workflows (`desktop`, `macos-studio`, `sdk`, `sdk-pillars`, `sdk-engines`,
`sdk-frameworks`, `sdk-industry`, `sdk-mobile`, `sdk-embedded`, `zxvd`)
trigger on any `kernel/src/**` change and had no draft rule. A kernel draft
started up to about 170 check runs. With fifteen drafts pushed together
under the batch rule and a 20-job account limit, jobs waited up to 80
minutes and were then cancelled in bulk. Cancelled jobs show as red.

**Fix:** those workflows now skip every job on a draft (zxvd's `e2e` job
excepted) and start when the PR is marked ready. A kernel draft now starts
the `ci.yml` fast tier, CodeQL and zxvd e2e.
`build_system/check_workflow_tiers.py` runs in quick-checks and fails when a
new workflow or job forgets the rule (#87 added three such jobs while this
fix was being written).

### RC5: the local check did not match CI (medium)

`scripts/verify-local.sh` (the fast tier, run before every push) skipped
the strata check that CI's host-tests runs first. That is exactly #104's
failure. Its `--san` level also left out the integer sanitizer, which is
the check that caught the zx_gcd_i64 overflow; only `--all` ran it.

**Fix:** the fast tier now runs `build_system/verify_layers.sh` and the
strata test before `verify-all`. `--san` now includes the integer
sanitizer and is documented as the minimum before marking a PR ready.

### RC6: branches tested alone, CI tests them merged with main (medium)

All five of #78 to #83 were based on b9a22b0 while main had moved to
c4694d1, and #100 had a conflict. A branch can pass locally and fail in CI
(or the reverse) because CI tests the merge.

**Fix:** `verify-local.sh` now warns when the branch is behind
`origin/main` and says to merge it first.

### RC7: a new repo-wide gate landed while about 20 branches were open (medium)

#77 (plan step 2.8) added the strata check: every kernel module must have a
line in `build_system/strata.txt`. Every open branch that added a module
went red on its next run, and no thread was told. Earlier the same day #66
and #69 had also been merged before their slow tier finished.

**Fix (process):** a PR that adds a repo-wide gate lists, in its
description, the open PRs the gate will fail, and the coordinator tells
those threads when it merges. The strata check is now in the local fast
tier (RC5), so a branch sees it before pushing.

### RC8: format checks left to CI (low)

#104 pushed eleven unformatted files. The fast tier already runs
clang-format 18.1.8 locally, so this was a skipped local check, not a gap
in the tooling. `scripts/verify-local.sh --install-hook` makes it
automatic on every push.

## 3. Process rules that go with the tooling

1. Never merge a PR whose ready-tier run has not finished; with the ruleset
   on, GitHub enforces this through `ci-gate`.
2. Before marking ready: merge `origin/main`, run
   `scripts/verify-local.sh --san` (or `--docker --all` on the Mac), then mark
   ready. One ready run at a time, as before.
3. A red check on main is fixed before any other merge. Once main runs the
   slow tier on every push, a red main is visible on main itself.
4. A cancelled job is not a failure and not a pass. Before cancelling queued
   runs in bulk, check whether the queue is full of draft runs. With RC4
   fixed, drafts no longer fill it.

## 4. What is still open

- **Owner step:** add the `main` ruleset requiring `ci-gate` (docs/CI.md,
  "Merge gate"). This is a repository setting that only the owner can make.
- The path-filtered workflows (`sdk*`, `zxvd`, `desktop`, `macos-studio`)
  cannot be required checks, because they do not run on every PR. Their
  checks still have to be green before a merge.
- The zx_gcd_i64 fix itself is #107, which the Faster CI thread is driving.
  #78 and #79 go green once it merges and they are re-run.
- #104 needs `git clang-format` and `kernel/src/zui/*` entries in
  `build_system/strata.txt`. #100 needs its conflict resolved. Both belong
  to their own threads.

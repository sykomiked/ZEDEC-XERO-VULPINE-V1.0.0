<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Continuous integration: what runs when

GitHub's free plan runs at most 20 jobs at once for this account (5 of them
macOS). Every push used to start 20 jobs (17 in CI, 3 in CodeQL), so one push
filled the whole allowance and the next push waited for it to drain. On
2026-10-11 five Dependabot pull requests opened together queued for 48
minutes before their first job started, and each run then took another 45
minutes because the mutation gate ran as one 41-minute job.

The rules below cut the jobs per push without dropping a check. No test is
skipped, disabled or quarantined: every check still runs before anything is
merged, and every night on `main`.

## The rules

1. **A newer push cancels the older run** of the same pull request
   (`concurrency` in both workflows). Pushes to `main` and `develop` are never
   cancelled, so every merged commit keeps its own result.
2. **Jobs run only when their inputs changed.** `build_system/ci_changes.sh`
   reads the diff and sets one flag per area:

   | Changed files | Jobs that run |
   |---|---|
   | `docs/**`, any `*.md` (except `docs/MAC_APP.md`), `LICENSE`, `NOTICE`, `FUNDING.yml`, `CITATION.cff`, issue templates, Dependabot or CodeQL config | quick-checks only; CodeQL is skipped |
   | `sdk/dotnet/**` | quick-checks, dotnet-sdk |
   | other `sdk/**`, `bindings/<language>/**` | quick-checks and ci-gate here; the `sdk*.yml` workflows test the SDKs |
   | `sdk/libzxv/**`, `sdk/conformance/**` | every job (libzxv is linked into the engine and zxvd) |
   | `macos/**` | quick-checks, macos-app, zxvd |
   | `docs/MAC_APP.md` | quick-checks, macos-app |
   | `sdk/pillars/**`, `sdk/kits/{aipi,z-wasm,z-contract}/**` | quick-checks, zxvd |
   | `proofs/**` | quick-checks, proofs |
   | anything else (kernel, tests, fuzz, bench, build_system, packages, `bindings/*.c`) | every job |
   | `.github/workflows/ci.yml` or `ci_changes.sh` itself | every job |
   | other workflows (`sdk*.yml`, `zxvd.yml`, `codeql.yml`) | quick-checks only |
   | `release.yml` (manual only; builds the draft release, see `docs/release/README.md`) | quick-checks only |
   | `verify-boot.yml` (manual only; builds the disc and checks every boot marker, see `docs/VERIFY_BOOT.md`) | quick-checks only |

   Nightly and manual runs (`workflow_dispatch`) always run every job.

   A pull request is compared with the first parent of GitHub's merge commit
   (the target branch as it was when the merge was made), so the diff is
   exactly the pull request's files. It used to be compared with the live
   `origin/main`; when #77 was merged five seconds after it was marked ready,
   its run saw main already containing the change, found nothing changed,
   skipped every test and reported success.
3. **The slow tier waits until a pull request is ready for review.** Draft
   pushes run the fast tier of `ci.yml`, plus CodeQL and zxvd's `e2e` job;
   every other workflow (`desktop`, `macos-studio`, `sdk*`, the rest of
   `zxvd`) skips its jobs on a draft and starts when the pull request is
   marked ready. The slow tier runs when a pull request is opened ready or
   marked ready for review, on every push after that, nightly at 03:23 UTC
   and on manual runs. Pushes to `main` run the fast tier only: every merge
   already passed the slow tier as a ready pull request, and a full rerun
   on `main` after each merge held the 20-job limit away from open pull
   requests. To run the full set on a
   draft, start CI by hand from the Actions tab (Run workflow, pick the
   branch).

   `build_system/check_workflow_tiers.py` (in quick-checks and the local
   script) fails when a workflow or a new job runs on drafts without this
   rule.

   Before this rule covered the other workflows, a draft that touched
   `kernel/src/` started about 150 jobs (every SDK, engine and framework
   workflow), not the 9 this page promised. Fifteen open drafts queued
   thousands of jobs behind a 20-job limit; jobs sat for up to 80 minutes
   and were then cancelled in bulk, which is most of the red seen on
   2026-10-12.

   | Fast tier (every push) | Slow tier (ready PRs, nightly, manual) |
   |---|---|
   | quick-checks: change detection, Apache-2.0 check, clang-format | mutation-subset, three shards |
   | host-tests: `make -C kernel verify-all` | fuzz-smoke |
   | zxvd: `kernel/arch/hosted/zxvd_checks.sh` (every zxvd unit test, the app and zui tests, zxvd end to end) | |
   | boot: arm64 and x86_64 QEMU boots, then an arm64 user session (`build_system/arm64_session_check.py`: EL0 shell over serial, a file that survives a reboot, signed-only `run`) | coverage |
   | sanitize-asan-ubsan | kat-cross |
   | macos-app, dotnet-sdk, proofs (when their inputs changed) | sanitize-integer, sanitize-msan |
   | CodeQL (unless docs only) | boot-extra: riscv64, riscv32 and arm32 boots |

4. **Small jobs share a runner.** Change detection, the licence check and
   clang-format are one job (quick-checks). The five boot jobs are now two
   (`boot` and `boot-extra`). Each architecture is still its own step, and
   later steps run even when an earlier one fails, so one red architecture
   does not hide the others.
5. **The mutation gate is split three ways.** `mutate.py --shard I/3` deals the
   CI targets to three shards by mutant count, so each target is in exactly
   one shard and each shard fails below its own targets' baselines. The
   baselines are unchanged.
6. **Caches.** Kernel boot builds use `make -j` with ccache (the binaries are
   byte-identical to a serial build: checked for all five targets). The rv32
   OpenSBI firmware, the NuGet packages and the proof tools are cached.
   `verify-all` cannot use ccache, because each test compiles and links in
   one command; most of its time is compiling, so splitting it into parallel
   make targets is the next speed-up.

## Jobs per push

| Push | Before | After |
|---|---|---|
| Draft PR, code change | 20 | 9 (6 CI + 3 CodeQL) |
| Ready PR, code change | 20 | 18 (15 CI + 3 CodeQL), or 19 when `proofs/` changed |
| Push to `main`, code change | 20 | 19 (slow tier included, so a regression is caught at merge) |
| Push to `develop`, code change | 20 | 9 |
| Docs only | 20 | 1 |
| .NET SDK only | 20 | 5 (2 CI + 3 CodeQL) |
| Mac app or proofs only | 20 | 5 |
| Nightly on `main` | none | 16 (CodeQL keeps its weekly run) |

Skipped jobs show as skipped and take no runner. A ready pull request runs
about as many jobs as before, but the longest job should drop from the
41-minute mutation gate to about 15 minutes (each mutation shard holds about a
third of the mutants; the first ready run will give the measured time), and draft pushes, which are most pushes, use under half the
allowance.

## Merge gate

`ci-gate` is the last job in `ci.yml` and the one check a merge should
require. It passes only when:

- quick-checks passed;
- no job failed or was cancelled;
- when the change touches code, every fast-tier job and, on a ready pull
  request, every slow-tier job actually ran and passed (a skipped job
  counts as passing for a required check, so this is checked here);
- when the change touches anything zxvd links or its tests drive (the
  `zxvd` flag of `ci_changes.sh`: code, `macos/`, `sdk/pillars/`, the AIPI,
  Z-Wasm and Z-Contract kits), the `zxvd` job ran and passed.

`ci-gate` always reports, under that one name:

- On a draft it fails at once and says to mark the pull request ready for
  review. A draft runs the fast tier only, so it can never satisfy the
  rule; marking it ready starts the full run, whose `ci-gate` replaces the
  draft's. (It used to report as `ci-gate (draft: fast tier only)`, a
  different check, which left the required `ci-gate` at "Expected - waiting
  for status to be reported" with no explanation.)
- Every job in `ci.yml` has a `timeout-minutes` limit, about twice its
  usual time. A hung job fails within that limit and `ci-gate` reports the
  failure, instead of the gate waiting out GitHub's six-hour default.

The SDK workflows (`sdk*.yml`) start only when something libzxv is built
from changes: `sdk/libzxv`, `kernel/include`, `kernel/third_party` and the
38 `kernel/src` directories the compiler reports for libzxv
(`build_system/sdk_kernel_deps.py`, which also writes their path filters).
They used to start on any `kernel/src/**` change, about 170 jobs for every
kernel pull request, and CI's own jobs, `ci-gate` among them, waited behind
them for the 20 runners. quick-checks runs `sdk_kernel_deps.py --check`, so
the filters cannot drift from what libzxv actually compiles.

Branch protection is a repository setting, so the owner turns it on once:
Settings > Rules > Rulesets > New branch ruleset, target `main`, enable
"Require status checks to pass", add `ci-gate` (source: GitHub Actions),
and enable "Block force pushes". Leave "Require branches to be up to date"
off: with a 20-job limit it would re-run every open pull request after each
merge. The fast tier on every push to `main` and the nightly slow tier
cover what it would catch.

Not covered by `ci-gate`: the path-filtered workflows (`desktop`,
`macos-studio`, `sdk*`, `zxvd`). They only run when their paths change, so
GitHub cannot require them. Their checks must still be green before a merge
(the ready rule the project already follows). zxvd's tests are the
exception: ci.yml's `zxvd` job runs them on Linux with gcc through the same
`zxvd_checks.sh`, so the gate covers them; `zxvd.yml` adds the other
platforms, clang, the sanitizer build and the display layers.

### The mutation config

`tests/harness/mutator/targets.json` is shared by every pull request that adds
a mutation target. Two rules keep it from blocking merges:

- quick-checks runs `mutate.py --check-config`, so an entry duplicated by a
  merge, invalid JSON, a missing target file or a CI target without a
  baseline fails in seconds, not in the slow tier's mutation shards.
- A target whose file has platform-only blocks uses `"lines": "host"`
  instead of hand-written line numbers. The harness works out which lines this
  host compiles on every run, so changing the file never needs an edit here.
  Hand-written ranges are for picking part of a file, and must be kept up to
  date by the change that moves the code.

## Run the checks before you push

`scripts/verify-local.sh` runs the same commands on your machine.

```bash
scripts/verify-local.sh                  # fast tier: licence, clang-format, host tests
scripts/verify-local.sh --boot --san     # + arm64/x86_64 boots, ASan/UBSan, integer sanitizer
scripts/verify-local.sh --all            # every Linux check CI runs, both tiers
scripts/verify-local.sh --docker --all   # the same inside CI's image
scripts/verify-local.sh --install-hook   # fast tier on every git push
```

- On macOS the script uses Docker by default (Docker Desktop must be running).
  The image is `scripts/ci.Dockerfile`: Ubuntu 24.04, gcc 11 as gcc, clang 18,
  every cross compiler, QEMU and valgrind. It is built once and reused, and a
  named volume keeps the ccache between runs.
- The image runs as `linux/amd64` so results match GitHub's x86_64 runners
  (Rosetta on Apple Silicon). `ZXV_DOCKER_PLATFORM=linux/arm64` runs natively,
  faster, but tests the NEON paths instead of AVX2 and has no x86_64 GRUB.
- When nothing but documentation changed since the merge base with
  `origin/main`, only the licence and format checks run; `--force` runs the
  rest anyway. Uncommitted and untracked files count as changes.
- The script warns when the branch is behind `origin/main`: CI tests the
  merge with the current main, so merge it first.
- Before marking a pull request ready, run at least `--san`; `--all` runs
  everything the slow tier runs.
- Not covered locally: macos-app (needs macOS and Xcode), dotnet-sdk, proofs
  and CodeQL.

## Self-hosted Mac runner (prepared, not enabled)

The `macos-app` job can run on the owner's M3 Max instead of GitHub's
`macos-14`. It is off. Nothing changes until the owner does all of the steps
below.

**Security first.** This repository is public. A self-hosted runner runs
whatever code the job checks out, with the permissions of the account it runs
as. If a pull request from a fork could reach it, anyone on the internet could
run code on the owner's Mac. So:

- The workflow sends only pushes and pull requests from branches of this
  repository to the self-hosted runner. Pull requests from forks always run
  on `macos-14`. This check is in `runs-on` of `macos-app` and must stay.
- In Settings > Actions > General, set "Approval for running fork pull request
  workflows from contributors" to the strictest option, **Require approval
  for all external contributors**, so no outside workflow runs without the
  owner's click.
- Register the runner in a runner group limited to this one repository, with
  the labels `self-hosted, macOS, ARM64, zxv-mac`. Jobs pick it only by the
  `zxv-mac` label.
- Run the runner as a **separate, standard (non-admin) macOS user**, never
  the owner's own account. The job launches ZXV.app, writes app defaults,
  kills any `ZXV.app` process it finds and, with the signing secrets set,
  creates and deletes a keychain. On the owner's account that would quit
  their own running copy of ZXV.
- A self-hosted runner keeps state between jobs. Prefer `./run.sh --once`
  (one job, then exit) under a launchd job that restarts it, so each job
  starts from a fresh process.

**To enable** (the owner's decision):

1. Create the macOS user, then in Settings > Actions > Runners > New
   self-hosted runner follow GitHub's macOS ARM64 steps as that user, adding
   `--labels zxv-mac` to `config.sh`.
2. Set the fork approval setting above.
3. Settings > Secrets and variables > Actions > Variables: add
   `ZXV_MAC_RUNNER` = `self-hosted`.

To turn it off again, delete the variable; the job goes back to `macos-14` on
the next run.

The Linux jobs stay on GitHub: a macOS runner cannot run their Ubuntu steps.
The Mac can still run them before a push with
`scripts/verify-local.sh --docker --all`.

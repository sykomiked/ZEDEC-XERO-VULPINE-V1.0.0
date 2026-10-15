# Working on ZXV

Rules for any coding session in this repository. The design behind them is
`docs/CODING_ENGINE.md` (the high-torque coding engine); this page is the
short form.

## Settle on the first pass

- Complete code only: no `TODO`, no stubs, no placeholder branches. If a
  piece cannot be finished, it is not committed.
- A change is done when its checks pass, not when it is written. Run them
  before every push (list below); one validated push beats several
  speculative ones.
- Prove a new test can fail: break the code it guards, watch it fail,
  restore. A test that never failed proves nothing.
- Report results with their evidence (command and outcome), failures
  included. Keep prose short; drop greetings and restatements.

## Conflicts are state B, gaps are state N

When instructions or specs disagree (B) or a needed parameter is missing (N),
do not guess or pick a default silently. Name the conflicting or missing
item and ask, or state the default taken and why.

The repository's own four-valued logic is `kernel/src/lpres` (`lpres_state_t`:
NEITHER 0, TRUE 1, FALSE 2, BOTH 3). Its conjoin and disjoin follow the
kernel's chain ordering, **not** Belnap's lattice (so BOTH or NEITHER is not
TRUE). Use `lpres_conjoin` / `lpres_disjoin`; never re-derive the tables.

## Exact arithmetic

- No `float` or `double` for money, tokens, settlement, ledgers or exact
  measurements. Use integers with a fixed scale or `kernel/src/rational`
  (reduced num/den, overflow checked).
- Token denominations and octane are `kernel/src/swarm/swarm_denom.h` (D1-D9):
  Fibonacci faces, Zeckendorf canonical form, grades PLAIN 0, REGULAR 4,
  MIDGRADE 9, PREMIUM 14, RACING 23, MAX 63. That scale is canonical.

## Bounded, ordered, layered

- Real-time and cross-process paths use pre-allocated bounded rings with an
  explicit full check (`head - tail >= capacity` is backpressure), never
  unbounded allocation.
- Order of operations: check invariants and bounds, reserve space, compute,
  mutate state, commit (journal). Nothing is written before it is checked.
- Every module has a stratum (`build_system/strata.txt`, S0 base to S8
  products); an include may not reach a higher stratum.
  `build_system/verify_layers.sh` enforces it.

## Checks to run before pushing

From the repository root:

```
python3 build_system/relicense_apache.py --check
python3 build_system/check_workflow_tiers.py
python3 build_system/sdk_kernel_deps.py --check
python3 tests/harness/mutator/mutate.py --config tests/harness/mutator/targets.json --check-config
set -f; EX=$(grep -vE '^[[:space:]]*(#|$)' .clang-format-ignore | sed 's/^/:(exclude,glob)/')
git clang-format --binary "$(command -v clang-format)" --diff "$(git merge-base origin/main HEAD)" HEAD -- 'kernel/*.c' 'kernel/*.h' $EX; set +f
build_system/verify_layers.sh
make -C kernel verify-all
```

The format check is clang-format 18.1.8 on the lines a commit changes, with
`.clang-format-ignore`'s files excluded (older sources), exactly as CI's
quick-checks runs it; commit first, since it compares commits. Then
whatever the change touches: `make -f build_system/Makefile.arm64 all`
then `build_system/ci_boot_check.sh arm64 build_system/Makefile.arm64 30 "[AI_OK]"`
and `python3 build_system/arm64_session_check.py kernel_arm64.elf` for the
arm64 kernel (name the `all` target; build the x86_64 kernel the same way),
`sh kernel/arch/hosted/zxvd_checks.sh OUT` for zxvd, `make -C kernel
test-hwdrv` for drivers. `scripts/verify-local.sh` runs the fast tier.

Building one test by hand outside the Makefile needs the Makefile's include
path: `export CPATH=$PWD/kernel/include:$PWD/kernel/src/modbind:$PWD/kernel/src/e8:$PWD/kernel/src/event_space:$PWD/kernel/src/surplus`.

## Repository habits

- Generated inputs (fuzz corpora) go under `build/`, never into the
  committed `fuzz/corpus/` seeds.
- `kernel/experimental/` is not built or tested; anything a test uses stays
  under `kernel/src/`.
- Drafts run the fast CI tier only; `ci-gate` passes only on a ready pull
  request (`docs/CI.md`). Batch related work into few pull requests.

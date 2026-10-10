<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Mutation testing report

Every score below was measured with `tests/harness/mutator/mutate.py` on
this branch (host gcc 11, `tiers.py --mode fast`). Nothing is estimated or
carried over from another tree. A mutation score measures how much of a file's
behaviour the tests would notice changing. It is not a proof of correctness,
and it is not a certification of any kind.

## Method

- **Harness:** `tests/harness/mutator/mutate.py`. It is deterministic: the
  same file always gives the same numbered list of mutants. Comments,
  literals and preprocessor lines are masked out before mutating.
- **Operators:** each mutant changes one token or statement.
  - `rel`: boundary shifts `< <=`, `> >=`, and `== !=`.
  - `arith`: `+ -`, `* /`, `+= -=`, `++ --`.
  - `const`: an integer literal N becomes N+1 or N-1.
  - `delchk`: deletes a guard of the form `if (...) return/break/continue/goto`.
  - `ret`: negates a literal return code.
  - `cond`: replaces an `if` condition with 1 and with 0, or a `while`
    condition with 0.
- **Outcomes:** each mutant runs alone, in a private copy of the tree.
  - *killed*: the test command failed.
  - *timeout*: the command ran too long; counted as killed.
  - *survived*: the command passed.
  - *compile-error*: tiers.py exited 3. The mutant is not valid and is left
    out of the score.
- **Score:** (killed + timeout) / (killed + timeout + survived).
- **Phases:** the targets and commands are in
  `tests/harness/mutator/targets.json`.
  - *before* runs only the module tests that existed before the tiered tests
    (the `kernel/Makefile` recipes, rebuilt as `legacy_*` by
    `tests/harness/tiers.py`).
  - *after* adds the tiered tests: `t1_*` root axioms, `t2_*` integration
    and `t3_*` metamorphic.
  - Both phases run with `TIER_STRICT=1`, so an unexpected XPASS also kills
    a mutant.
- **Reproduce:**

  ```
  python3 tests/harness/mutator/mutate.py --config tests/harness/mutator/targets.json --phase both --jobs 6 --out <dir>
  ```

## Scores (measured)

The table below is the p10-test-tiers measurement. The modules changed when
the branch was merged into p10-integrate; the re-measured targets are in
"Re-measured on p10-integrate" further down. `pay_tithe` no longer exists
(replaced by `pay_assure`), so its row is history only.

"Killed" counts killed plus timeout. "CE" is compile-error mutants, which are
left out of the score.

| name | target | before | killed | survived | CE | after | killed | survived | CE |
|---|---|---|---|---|---|---|---|---|---|
| rmag_core | `kernel/src/rmag/rmag_core.c` | 25.0% | 9 | 27 | 0 | 88.9% | 32 | 4 | 0 |
| rational | `kernel/src/rational/rational.c` | 59.6% | 280 | 190 | 13 | 85.3% | 401 | 69 | 13 |
| pay_ledger | `kernel/src/pay/pay_ledger.c` | 51.8% | 262 | 244 | 50 | 93.9% | 475 | 31 | 50 |
| pay_tithe | `kernel/src/pay/pay_tithe.c` | 68.9% | 153 | 69 | 27 | 92.8% | 206 | 16 | 27 |
| vino | `kernel/src/vino/vino.c` | 31.7% | 86 | 185 | 29 | 92.6% | 251 | 20 | 29 |
| count_house | `kernel/src/count_house/count_house.c` | 57.0% | 146 | 110 | 7 | 84.4% | 216 | 40 | 7 |
| triple_ledger | `kernel/src/finance/triple_ledger.c` | 26.3% | 71 | 199 | 16 | 91.1% | 246 | 24 | 16 |
| community_chest | `kernel/src/community_chest/community_chest.c` | 43.3% | 185 | 242 | 6 | 88.5% | 378 | 49 | 6 |
| porter_house | `kernel/src/porter_house/porter_house.c` | 59.1% | 101 | 70 | 4 | 96.5% | 165 | 6 | 4 |
| mm | `kernel/src/mm/mm.c` | n/a | | | | 91.4% | 159 | 15 | 24 |
| fs_malloc | `kernel/include/freestanding.h` lines 77-95 | n/a | | | | 86.2% | 25 | 4 | 0 |
| audio_rings | `kernel/src/audio/audio.c` lines 142-256,519-628 | 68.1% | 143 | 67 | 17 | 80.0% | 168 | 42 | 17 |

Over the ten targets that have a *before* phase, the score went from 50.6%
(1436 of 2839) to **89.4%** (2538 of 2839). The mutant set is the same in
both phases.

`mm` and `fs_malloc` show "n/a" for *before* because no hosted test compiled
`kernel/src/mm` or exercised `fs_malloc`/`fs_calloc` until the tiered
tests. That is not a 0% score.

Which run produced each *after* number:

- **rational:** a re-run made after the last `axioms_rational.c` change.
- **pay_ledger:** a re-run made after the last `axioms_pay.c` change.
- **All other targets:** one batch run with the final test set. Later commits
  changed only rational and pay tests, and clang-format layout.

## Tests that killed the survivors

The first *after* run left survivors. Each meaningful one was read and either
killed by a new or sharper test, or classified below as equivalent. The new
tests live in these functions:

- **`axioms_pay.c`**
  - `axiom_pay_more`: platform defaults, registry bounds (code length 12,
    minor 19), the request digest, idempotency and e2e charsets, nonce slots,
    the frozen and stale journal lines, and the exact balance ceilings on both
    sides.
  - It also runs `pay_ledger_check` against corruptions that keep the totals
    balanced, so each invariant I1 to I3 is tested on its own.
  - Chain verification under tampering with e2e, seq or prev, and under a
    record forged from another history.
  - Reversal retry, and the tithed postings.
  - `axiom_tithe_more`: `isqrt` at 2^128 - 1 and 2^128, commons
    conservation, and the edge cases of the split cap.
- **`axioms_chest.c`**
  - `axiom_vino_more`: an FNV known-answer test, the INT64_MAX and CAP_MAX
    refusals, rmag quota mirroring, self-transfer, and message adapters at
    MIN-1 and MIN.
  - `axiom_cc_more`: share clamps, purchase, cash-out and escrow paths.
  - `axiom_count_house_more`: the trust gate, mint gates, the INT64_MAX supply
    bound and the fib-8 allowance saturation.
  - `axiom_porter_more`: seals, the allowlist and full-table reuse.
- **`axioms_triple.c`**
  - `axiom_triple_more`: dirty init, the empty-health identity, both
    256-entry capacity sides, and voucher ids and refusals.
- **`axioms_alloc.c`**
  - The used-rational quota at frame 0 is now nonzero, so frame 0 is not
    coincidentally 0.
  - Exact `kmalloc` split, and `mm_get_page` at an absent table.
  - `axiom_audio_more`: defaults, format bytes, exact ring bounds and the
    capture cursor.
- **`axioms_rational.c`**
  - `rat_abs` of every valid value must be valid and exact. This kills
    `rational.c:104 '!a.valid' -> '1'`.

## Remaining survivors

Each line number below is a survivor in the latest *after* run. Every one is
either **equivalent** (no input through the public API can change observable
behaviour) or **unreachable without fault injection or a signing key**.

| target | lines | why it survives |
|---|---|---|
| rmag_core | 16, 18 | malloc/calloc failure guards. Killing them needs allocator fault injection, which the hosted harness does not provide. |
| rational | 16, 19-22, 29-37, 62, 92-95, 100, 104-105, 228, 282 | Equivalent: `invalid()` field values are never read when valid is false. The gcd sign is renormalised by `reduce`. The `den == 0`, `b.num == 0` and validity guards are repeated downstream (`rat_make`, `rat_mul`, `reduce`). `-0 == 0`. |
| rational | 172, 196, 200, 214, 224, 231, 235, 249, 251, 260, 277, 298, 301 | Equivalent: the parse overflow bound is one-sided. In `put`, the last byte is overwritten by the terminator. Tie (`twos == fives`). An extra scale digit is cancelled by the leading-zero probe and the trailing-zero strip. Minimal places never leave a trailing zero. The 18-place cap is reached by both branches. |
| pay_ledger | 58, 70, 201, 237, 258, 273, 280, 281, 338, 398, 401, 427, 451, 520 | Loop bounds one past the end of fixed arrays whose extra slot is zero or inactive. |
| pay_ledger | 68, 104, 117, 244, 79, 96 | Length probes and `strlcpy` NULL or size handling that the caller already bounds. |
| pay_ledger | 226, 263, 308, 413, 445, 505 | 226 and 263 are ties. 308 is an empty-chain path with the same result. At 413, starting at 0 compares a line with itself. At 445, the next `memcpy` overwrites the byte. At 505, the usury check would need amount != amount. |
| pay_tithe | 12, 21, 32, 55, 56, 96, 134, 188, 192, 199 | Equivalent or unreachable:<br>- 12: `u192_cmp` return magnitudes; callers test only the sign.<br>- 21: a carry that cannot occur.<br>- 32: a word one past the 3 used.<br>- 55, 56: the start bit and the pre-shift are optimisations only.<br>- 96: the write is overwritten.<br>- 134, 199: ties.<br>- 188: an unused initial value.<br>- 192: `nz == 0` takes the same split. |
| vino | 20, 23, 30, 82, 120, 133, 135, 159, 198, 287, 304 | Equivalent or unreachable:<br>- 20, 23: `str_len`/`str_cmp` magnitudes.<br>- 30: `cap == 0` never occurs.<br>- 82: `rmag_init` is a no-op after the first call (F-RMAG-REINIT).<br>- 120: same result whenever 2 accounts exist.<br>- 133: unreachable `lpres` FALSE.<br>- 135: `audit_flag` is unused.<br>- 159, 198: a NULL memo is handled by `str_ncopy`.<br>- 287, 304: `out +/- j` with j = 0. |
| count_house | 73, 76-78 | The default Ed25519 message layout. Killing these needs a signing key, which this task must not create or read. |
| count_house | 140, 153, 187, 251, 252, 315, 319, 322, 334, 346, 357, 363, 364 | Equivalent or unreachable:<br>- 140, 153, 251, 252, 315: ties at clamps.<br>- 187: the supply bound is shadowed by the ratio sign in the TEST_HOST build.<br>- 319, 322: unreachable.<br>- 334: same result.<br>- 346, 363, 364: NULL children cannot occur through the API.<br>- 357: `fractal_audit(NULL)` falls through to `audit(NULL)`, which is false. |
| triple_ledger | 37, 69, 143, 158, 164, 228-247, 270, 271, 313 | Equivalent, unreachable or deliberately unpinned:<br>- 37, 69: redundant zeroing.<br>- 143: the bound is already enforced by post.<br>- 158, 164: unreachable return codes.<br>- 228-247: stale entries one past the live count.<br>- 270, 271: the F-TL-COVERAGE threshold, deliberately not pinned while that finding is open.<br>- 313: a zero-balance tie. |
| community_chest | 20-25 | The default verify message. Killing these needs a signing key. |
| community_chest | 66, 75, 76, 106, 130-195, 205, 216, 217, 229, 252, 310, 317, 348, 362 | Equivalent or unreachable:<br>- 66, 205, 348, 362: one past the end.<br>- 75, 76, 106, 216, 217, 229: ties at clamps.<br>- 130-195: `cc == NULL` is caught by `cc_get_app(NULL)`.<br>- 252: unreachable.<br>- 310: `vino_transfer` refuses anyway.<br>- 317: the escrow can never be the first account. |
| porter_house | 19, 87, 89, 151 | 19: the element size is the same. 87: `find_seal` handles NULL. 89, 151: only reachable through `seals[-1]`, which is undefined behaviour. |
| mm | 18, 24, 96, 168, 184, 185 | 18: `bitmap_test` is dead code. 24: the inner loop never reaches j = 32. 96: HEAP_MAX never fits. 168: realloc bytes past the old size are unspecified. 184, 185: normalisation, and out-of-range quota reads return 0. |
| fs_malloc | 90, 92 | `calloc` overflow boundary probes at MAX/size and `(size_t)-2` give the same result, and the memset runs on fresh zeroed bss. |
| audio_rings | 164, 198, 204, 238, 239, 246, 531, 545, 549, 557-559, 581, 596-621 | Equivalent or unreachable:<br>- 198, 204, 238: guards no caller can reach.<br>- 164, 239, 246: one past the end.<br>- 531: equal outcomes.<br>- 545, 549, 581: the value is already zero after memset.<br>- 557: the pool size equals the stream table size.<br>- 558, 559: `audio_format_bytes(out-of-range)` is 0, and every format has bytes.<br>- 596-621: the zero-length and tie paths are identical. |

## Bugs found by this round

Both bugs are listed in `KNOWN_FAILURES` and reported as XFAIL, never
silently. Each has a patch in `tests/FINDINGS.md`.

- **F-VINO-SELFQ** (new): a vino self-transfer leaves balances unchanged but
  moves the mirrored rmag quota.
- **F-RMAG-OVF** (now also reached via vino): `vino_transfer` of INT64_MAX
  overflows the int64 rmag quota arithmetic. UBSan reports it, so the test
  block is skipped under sanitizers (`TIER_UB_KNOWN`).

## CI gate

The `mutation-subset` job, appended to `.github/workflows/ci.yml`, runs:

```
python3 tests/harness/mutator/mutate.py --config tests/harness/mutator/targets.json --ci --jobs 4
```

- It covers the fixed subset `rmag_core` and `fs_malloc`.
- The baselines are the measured *after* scores on p10-integrate, rounded
  down: rmag_core 0.9178 (67/73) and fs_malloc 0.8333 (30/36). See the
  section below for why both changed.
- The job fails when a score drops below its baseline, or when a gated
  target has no baseline.
- A local run of exactly this command on p10-integrate passed: 91.8% and
  83.3%.
- The job has not yet been executed on GitHub from this branch.

## Re-measured on p10-integrate

After the merge, other branches had changed three of the targets:
`rmag_core.c` gained the division-by-zero fix and `rmag_div_quotas_checked`
(209c3d5), `fs_malloc` steps in 16-byte units (2a10d1b), and `pay_tithe.c`
was replaced by `pay_assure.c` (b490a62). The tests were updated to the new
contracts (tests/FINDINGS.md, "Contract changes") and these three targets
were measured again, same harness and commands:

| name | target | before | killed | survived | CE | after | killed | survived | CE |
|---|---|---|---|---|---|---|---|---|---|
| rmag_core | `kernel/src/rmag/rmag_core.c` | 12.3% | 9 | 64 | 2 | 91.8% | 67 | 6 | 2 |
| pay_assure | `kernel/src/pay/pay_assure.c` | 57.8% | 89 | 65 | 39 | 92.9% | 143 | 11 | 39 |
| fs_malloc | `kernel/include/freestanding.h` lines 80-97 | n/a | | | | 83.3% | 30 | 6 | 2 |

The first *after* run on the merged tree scored rmag_core 54.8% (the new
checked division had no test) and fs_malloc 80.6%, pay_assure 91.6%. New
checks in `axioms_rational.c` (the 0/1 convention of the unchecked division,
every refusal of `rmag_div_quotas_checked`, its exact results over the
oracle grid), `axioms_alloc.c` (an allocation of exactly the space left) and
`axioms_pay.c` (NULL fee outputs of `pay_assure_fee_carry` /
`pay_assure_charge`) killed the meaningful survivors. Remaining survivors:

| target | lines | why it survives |
|---|---|---|
| rmag_core | 16, 18 | malloc/calloc failure guards: need allocator fault injection. |
| rmag_core | 82 | The INT64_MIN operand guard is shadowed by the INT64_MIN product guard at 86: an INT64_MIN field times 1 is INT64_MIN, times anything larger overflows. |
| fs_malloc | 87 | heap_off is always a multiple of 16 and the heap is too, so a step never exceeds the space left once n fits: the clamp's comparison and its arithmetic are never decisive. |
| fs_malloc | 93, 95 | `calloc` overflow boundary: any count*size near SIZE_MAX is refused by `fs_malloc` anyway; size 1 never overflows; the memset runs on never-reused zero bss. |
| pay_assure | 10 | A `_Static_assert`: compile-time only. |
| pay_assure | 19 | Unreachable: the quotient is at most g. |
| pay_assure | 27, 54, 161 | Initial values always overwritten. |
| pay_assure | 69 | The extra write lands in `credit_mult`, which is assigned right after. |
| pay_assure | 107 | want == fee gives the same result on both paths (no excess, no shortfall). |
| pay_assure | 165, 172 | All-zero weights give the same split without the shortcut; 172 is a tie. |

The CI baseline for fs_malloc went down (0.8620 to 0.8333) because the
rewritten allocator has more equivalent mutants, not because a test got
weaker. The other targets in the first table were not re-measured on
p10-integrate.

## rmag_core after the F-RMAG-OVF fix (merge batch)

The checked quota arithmetic added code (field checks, overflow checks, a
cross-reduction in mul). Measured with the CI subset command: 86 of 90
mutants killed (95.6%; 9 do not compile). The 4 survivors are the
`rmag_init` allocation-failure guards, which need allocator fault injection
(see above). The CI baseline is raised to 0.9555.

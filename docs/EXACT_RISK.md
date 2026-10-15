# Exact portfolio loss distributions (xrisk)

Module: `kernel/src/xrisk/` (`xrisk.h`, `xrisk.c`, `test_xrisk.c`, `bench_xrisk.c`), prefix `xrisk_`.

**Verdict in one line:** for *discrete* loss models (fixed loss per default, integer loss units, a
finite scenario mixture) the exact distribution is computed on one CPU core in milliseconds, with
zero sampling error. Plain Monte Carlo at 10^4 to 10^5 samples measurably misses the 99.9% VaR
and the tail probability. Monte Carlo remains the more general tool for continuous models.

This is a computation tool for credit unions, insurers and treasury teams. It is not investment,
credit or insurance advice, and it is not a regulatory capital model. Nothing here is quantum, and
nothing is benchmarked against quantum hardware. The comparison is classical exact vs classical
Monte Carlo.

## What it computes

Each of up to 256 exposures defaults with an exact rational probability `p_i = num/den` (any
positive int64 `den`, validated: `0 <= num <= den`). On default it loses `L_i` whole loss units,
where the caller picks the unit, for example 1000 currency units. The total is capped at
`XRISK_MAX_UNITS = 4096` units. The total loss is `L = sum of L_i` over the exposures that default.

All outputs are exact rationals:

- `P(L = k)`, `P(L <= k)` and `P(L >= k)`;
- the expected loss `E[L]`;
- `VaR_alpha` for a rational `alpha = an/ad` in (0,1): the smallest `k` with `P(L <= k) >= alpha`,
  decided by exact integer comparison, so the boundary `P(L <= k) == alpha` is handled exactly;
- expected shortfall (Acerbi-Tasche): `ES = (E[L 1{L>v}] + v (P(L<=v) - alpha)) / (1 - alpha)`
  with `v = VaR_alpha`. It is coherent and equals `E[L | L >= v]` when the CDF hits `alpha` exactly.

`xrisk_rat_reduce` puts a result in lowest terms (binary GCD), and `xrisk_rat_cmp` compares two
results exactly. `xrisk_rat_to_dec` prints a result as decimal digits truncated, never rounded, to
a chosen number of places, without using floats.

**Correlation** is modelled as a finite mixture, sometimes called conditional independence. There
are up to 16 scenarios with exact rational weights that must sum to exactly 1. Each scenario has
its own `p_i`, for example "normal" and "stressed". Exposures are independent within a scenario.

## Exact representation (design choice)

I chose fixed-width big integers with one shared denominator over a powers-of-ten-only design, for
two reasons. It accepts any positive int64 denominator, such as 1/3 or market-implied odds. It also
needs only one comparison path.

1. Every input fraction is reduced to lowest terms.
2. Per exposure, `m_i` is the lcm of its denominators across scenarios. If that does not fit in
   int64, the run is refused. The weights share `B`, the lcm of the weight denominators.
3. The shared denominator `den = B * prod m_i` is built first as a big integer. If it needs more
   than `XRISK_BITS` bits (default 4096, configurable at compile time), the run is **refused**
   with `XRISK_E_OVERFLOW`. Nothing is ever rounded.
4. Dynamic programming convolves one exposure at a time, in place over loss units:
   `mass[k] = mass[k] * (m_i - n_i) + mass[k - L_i] * n_i`. Every numerator stays `<= den`, so the
   DP cannot overflow once `den` fits. Overflow is still checked on every limb operation.
5. Each scenario's distribution is added into the result with weight numerator `w_s * B`.

Limbs are 32-bit, so the code builds for i386 and aarch64 without `__int128` or libgcc division
helpers. There is a fast path when `m_i < 2^32` and a general path for 64-bit multipliers. The big
integers carry `XRISK_HEAD_BITS = 128` bits of headroom for the metric arithmetic: `k * mass` and
`alpha` cross products. The cost is about scenarios x exposures x units x limbs(den).

## Paraconsistent input fusion

An exposure can have several probability sources, for example a rating agency, an internal model
and a market-implied figure. `xrisk_fuse` / `xrisk_model_set_sources` classify them with the lpres
four values (own enum, same numbers: NEITHER=0 TRUE=1 FALSE=2 BOTH=3):

| Verdict | When | What happens |
|---|---|---|
| NEITHER | no source | refused (`XRISK_E_NEITHER`); the exposure stays unset and compute refuses |
| TRUE | `max - min <= tol` (exact) | the most conservative (highest) p is used on both sides |
| BOTH | the sources conflict | the exposure keeps `p_lo = min` and `p_hi = max` and is counted by `xrisk_model_conflicts` |

`xrisk_fuse` never returns FALSE, because a probability source cannot attest "no probability".

When there is a conflict, the caller runs `xrisk_compute` twice, with `XRISK_SIDE_LOW` and
`XRISK_SIDE_HIGH`, and reports **both** results as bounds. They are never averaged. Every
`L_i >= 0`, so raising any `p_i` shifts the loss distribution upwards in stochastic order. The LOW
and HIGH runs therefore bracket every tail probability, the expected loss, VaR and ES for any
choice of `p` inside the conflicting ranges. The tests check this against a model with mid-range
values.

## Measured: exact vs plain Monte Carlo

`bench_xrisk.c`, one core of an Intel Xeon @ 2.10 GHz, gcc 13 `-O2`. The portfolios are seeded at
random: losses of 1 to 16 units, and p from 5 to 300 bp over 10^4. The mixture case uses a
0.9 normal / 0.1 stressed split, with 4x the default rates in the stressed scenario. Monte Carlo
uses double uniforms, 20 independent seeds at 10^4 and 10^5 samples and 5 at 10^6. "Tail" is the
mean relative error of the MC estimate of `P(L >= exact VaR99.9)`. VaR errors are in loss units.

| Portfolio | Exact time | den bits | Exact VaR99 / VaR99.9 | MC samples | MC time | VaR99 mean abs err (exact hits) | VaR99.9 mean / max abs err (exact hits) | Tail rel err |
|---|---|---|---|---|---|---|---|---|
| 50 indep. | 0.6 ms | 598 | 30 / 42 | 10^4 | 1.6 ms | 0.25 (15/20) | 1.10 / 4 (6/20) | 20.4% |
| | | | | 10^5 | 13 ms | 0.00 (20/20) | 0.30 / 1 (14/20) | 6.8% |
| | | | | 10^6 | 128 ms | 0.00 (5/5) | 0.00 / 0 (5/5) | 1.5% |
| 100 indep. | 2.3 ms | 1177 | 48 / 64 | 10^4 | 1.9 ms | 0.65 (9/20) | 1.50 / 6 (3/20) | 21.4% |
| | | | | 10^5 | 19 ms | 0.10 (18/20) | 0.55 / 1 (9/20) | 7.0% |
| | | | | 10^6 | 264 ms | 0.00 (5/5) | 0.00 / 0 (5/5) | 1.5% |
| 256 indep. | 41 ms | 3003 | 84 / 105 | 10^4 | 7.6 ms | 0.90 (7/20) | 2.60 / 6 (2/20) | 34.3% |
| | | | | 10^5 | 65 ms | 0.20 (16/20) | 0.35 / 1 (13/20) | 6.7% |
| | | | | 10^6 | 517 ms | 0.40 (3/5) | 0.00 / 0 (5/5) | 2.6% |
| 256 mixture | 65 ms | 3028 | 169 / 211 | 10^4 | 4.9 ms | 1.85 (3/20) | 4.85 / 10 (2/20) | 20.7% |
| | | | | 10^5 | 49 ms | 0.75 (6/20) | 1.20 / 3 (4/20) | 6.5% |
| | | | | 10^6 | 546 ms | 0.00 (5/5) | 0.40 / 1 (3/5) | 2.3% |

The exact time includes computing the full distribution and both VaRs. The exact ES99.9 values were
46.564, 69.912, 113.091 and 226.000 units (printed truncated to 3 places).

**Honest reading.** For these discrete models, exact wins on accuracy everywhere: its error is
zero, and it is faster than Monte Carlo at the sample counts (10^5 to 10^6) that MC needs to pin
the 99.9% VaR. At 10^6 samples MC still shows a 1.5 to 2.6% relative error on a tail probability
near 10^-3. On the 256-exposure book it still missed VaR99 by one unit in 2 of 5 runs, because the
exact CDF sits very close to 0.99 there. At 10^4 samples MC is faster than exact on the
256-exposure book (7.6 ms vs 41 ms), but its VaR99.9 is wrong in 18 of 20 runs.

This is plain MC. Importance sampling or other variance reduction would shrink MC's tail error, and
none was benchmarked.

## Honest limits

- **Discrete models only.** Monte Carlo is the more general tool for continuous severities
  (lognormal LGD), Gaussian or t copulas with continuous factors, and path-dependent market risk.
  xrisk computes those exactly only after the caller discretises them into loss units and a finite
  scenario set. That discretisation error belongs to the caller's model and is not measured here.
- **Size.** The book is capped at 256 exposures, 4096 total units and 16 scenarios. The
  denominator must fit 4096 bits, so probabilities with many digits, or many large co-prime
  denominators, are refused rather than approximated. An `xrisk_dist_t` is about 4.3 MB
  (caller-owned).
- **Exact relative to the inputs.** Model risk and bad input probabilities are not removed. The
  fusion step only makes disagreement between sources visible.
- No floating point, allocation or libc in the module. The tests and the bench use doubles only for
  reports.

## Not built yet

These are possible later steps, not started: a userland report generator (CSV in, signed PDF or
HTML out), sensitivity of VaR and ES to each exposure (exact marginal contributions), multi-factor
mixtures with more than 16 scenarios, and a compressed big-integer form so larger books fit.

## How to run

From `kernel/`:

```
gcc -std=c11 -O1 -g -Wall -Werror -Wextra -Wdouble-promotion -Wfloat-conversion \
  -fsanitize=address,undefined -fno-sanitize-recover=all -DTEST_HOST -Iinclude \
  src/xrisk/test_xrisk.c src/xrisk/xrisk.c -o /tmp/test_xrisk -lm && /tmp/test_xrisk
# add -DXRISK_SOFT_DIV to exercise the i386 shift-subtract division path on a 64-bit host
gcc -std=c11 -O2 -Wall -Wextra -Werror -Iinclude src/xrisk/bench_xrisk.c src/xrisk/xrisk.c \
  -o /tmp/bench_xrisk -lm && /tmp/bench_xrisk
```

The tests cover:

- big-integer operations against `unsigned __int128`;
- single-exposure and binomial closed forms (n = 1 and 30);
- brute-force enumeration of all 2^n default patterns for n <= 16 with random rationals: the pmf,
  the denominator, a total of exactly 1, EL, VaR at exact CDF levels, and ES against the
  quantile-integral definition;
- mixtures, including large weight denominators, against brute force;
- 64-bit denominators (the general multiply path);
- fusion verdicts at the exact tolerance boundary;
- LOW <= mid <= HIGH bracketing;
- overflow refusal at exactly 4096 vs 4097 bits;
- every error path.

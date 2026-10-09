# Co-processor tiers: integer gate, accelerator, integer reconciliation

`kernel/src/cotier/` (prefix `ct_`) treats an accelerator and ZXV's integer mathematics as two different, complementary tiers instead of rivals:

| Tier | Runs on | Job | Code |
|---|---|---|---|
| 1 | ZXV integer core | A geometric surplus gate decides which attention edges the accelerator computes | `ct_gate.{h,c}` |
| 2 | CUDA / Metal / Vulkan / a neural engine | Dense FP16 / BF16 / INT8 matrix products, softmax and the value projection, on the edges tier 1 kept | dispatched through `vb_compute_fn` in `src/vblock/vblock.h`; nothing in this directory |
| 3 | ZXV integer core | Brings the accelerator's floats back into Q16.16 exactly, checks them against the integer engine, keeps the power and thermal budget, and decides what may be stored | `ct_reconcile.{h,c}`, `ct_budget.{h,c}` |

All three modules are freestanding C11: integers only, no libc, no `malloc`, no floating point, no 64-bit division (they use `zt_udiv64`), no `__int128`. The tests build them for `aarch64-none-elf` and `i386-none-elf` and check that only `zt_` symbols are left undefined.

> **The pruning rule is a heuristic, and on real models it is unvalidated.** Pruning attention edges by F(u) is not a theorem about attention. Its accuracy cost has to be measured for each model. The measurements below come from three tiny random test models. On those models F(u) pruning was no better than dropping the same number of edges at random. No real checkpoint has been run with the gate. Until one is, treat every number here as a property of the test models only.

## Tier 1: the surplus gate (`ct_gate`)

**The test.** For a query direction x and a key direction y, the gate computes u = 1 − (x̂·ŷ)² and F(u) = ln(1 + (N−1)u). These are the ISF functionals of `zt.h` T9, and the gate reuses `zt_surplus_u` and `zt_surplus_f` from `src/tensor/zt_isf.c` without changing them. In `CT_GATE_SURPLUS` mode an edge is kept when F(u) ≥ floor. That is the "bridge principle" (an edge's marginal contribution tracks F(u)) applied literally.

Note what this means geometrically. F is largest for **orthogonal** pairs, whose softmax scores are near zero. It is zero both for perfectly aligned pairs, which have the largest scores, and for anti-aligned pairs, which have the smallest. The rule can therefore drop exactly the edges softmax weighs most. That is why the gate ships with a comparison mode, `CT_GATE_ALIGN`, which keeps an edge when the signed cosine is at least the floor (the conventional similarity-pruning baseline), and why the measurements include random pruning at a matched rate.

**Coarse, so it costs less than attention.** The test does not run on every token pair. Queries and keys are each grouped into at most 16 clusters by a few passes of integer spherical k-means. Centroids are unit vectors in Q14. The seeds are evenly spaced tokens, so clustering is deterministic. F(u) is computed once per (query cluster, key cluster) pair. The gate costs about (n_q + n_k)·C·d·(passes + 1) + C²·d multiply-adds, against n_q·n_k·d for the scores alone. Measured with `test_ct_gate`: 256 queries against 4096 keys, d = 64, 8 clusters and 2 passes take 7.2 M gate MACs against 130 M attention MACs, a ratio of **0.056**. At the test models' 96 tokens the ratio is 0.55, so the gate is not cheap at short context.

**Never everything.** The diagonal (a key at the query's own position) and a configurable local window (q_pos − k_pos < window) are always kept. Every softmax row therefore keeps at least its own token. Keys after the query are never edges. The fuzz test checks these invariants on 3000 random gates with random shapes, zero vectors, INT32_MIN and INT32_MAX inputs, and random configurations.

**Outputs.**
- `mask[a]`, bit b: query cluster a keeps key cluster b.
- `ct_gate_row`: the keep bytes of one query.
- `ct_gate_keep_list`: the per-head keep list, meaning the keys at least one query keeps. This is what the accelerator has to load.

**The hook the tensor owner can wire** (`ct_attn_gate_fn`; `ct_gate_attn` implements it):

```c
typedef int32_t (*ct_attn_gate_fn)(void *ctx, uint32_t layer, uint32_t head,
                                   const zt_fx *q, uint32_t n_q, uint32_t q_stride, uint32_t q_pos0,
                                   const zt_fx *k, uint32_t n_k, uint32_t k_stride, uint32_t k_pos0,
                                   uint32_t head_dim, uint8_t *keep /* n_q * n_k */);
/* returns the kept-edge count; < 0 means "gate unavailable: keep everything" */
```

`zt_model` is **not** modified. Its `attend()` in `src/tensor/zt_model.c` scores one query position against keys 0..p, one head at a time. The natural call site is just before the score loop. Pass q = the head's (post-RoPE, post-QK-norm) query, and k = `s->k16 + (l * n_ctx) * kv_dim + kvh * head_dim` with `k_stride = kv_dim`. A Q8 cache would need its keys dequantised into a scratch buffer first. Then give every score with `keep[i] == 0` the value INT32_MIN before the max and exp, so its softmax weight is exactly 0.

Two cautions for that wiring:
1. `ct_gate_build` re-clusters the keys on every call. With one query per call, which is how decode works, that costs more than the attention it gates. A cheap version for decode needs the key clusters kept across tokens, with new keys assigned to the existing centroids. That is not implemented.
2. Per call the gate needs n_q + n_k bytes of cluster-id scratch (`ct_gate_ctx_t.qa`, `.ka`) and about 5 KB of stack.

## Tier 2: the accelerator

Nothing in `cotier` talks to a GPU. The tier-2 dispatch point is `src/vblock`: `vb_compute_fn` launches an op on a device, and `vb_run` / `vb_plan` place and pipeline the ops. The gate's keep list says which K/V rows an op must read. `ct_budget`'s outputs (`batch`, `ctx`, `accel_on`) say how large an op may be, and whether to use the accelerator at all or let the integer engine carry the work. No real GPU backend runs in this repository's CI (see `vblock.h`).

## Tier 3a: reconciliation (`ct_reconcile`)

**Ingest.** `ct_f16_to_q16`, `ct_bf16_to_q16`, `ct_f32_to_q16` and the array form `ct_ingest` decode float bit patterns with integer bit operations only, subnormals included. Each value is rounded to the nearest Q16.16, with ties to even.
- Values outside [−32768, 32768) saturate to INT32_MAX or INT32_MIN.
- Infinities saturate the same way, and NaN becomes 0.
- Every case is counted (`ct_ingest_stats_t`: NaN, ±Inf, saturated, subnormal, flushed to zero, inexact) and can be flagged per element.

**The ingest is verified exhaustively.**
- All 65536 FP16 patterns and all 65536 BF16 patterns match a float64 reference in both value and flags.
- FP32 is checked by default on 6.1 M patterns: every exponent with edge and random mantissas, 4 M random patterns, and the Q16.16 boundaries.
- `test_ct_reconcile full` checks **all 2^32 FP32 patterns** (4,301,068,555 checks including the sweep, about 52 s at -O2). All of them pass.

**Two-source decomposition against a category partition.** `cat_of[i]` assigns each coordinate to a category, for example a group of channels or heads. Take a trusted reference r (the integer engine's output on a sampled row) and the accelerator's output a, with unit vectors x and y. Let p_c = |x_c| and q_c = |y_c| be the two category energy profiles, α = p·q, β² = 1 − α², and cos γ = (x·y)/α. Then:

  u = 1 − (x·y)² = β² + α² sin²γ = **u_cross + u_div**

- **u_cross = 1 − (p·q)²** is energy that moved *across* categories: the cross-category perpendicular component.
- **u_div = (p·q)² − (x·y)²** is rotation *within* categories while the profile is held: the within-category parallel part.

When x is a pure block vector (one category), this is exactly `surplus.h`'s Theorem 4.1 (α = |y∥|, β = |y⊥|). The test checks that case against `surplus_decompose`: worst error 7.8e-5 in f. Against float64 on 20,000 random pairs, the worst error in u is 6e-5.

Two integer checks guard the arithmetic:
- u_div ≥ 0, which is Cauchy–Schwarz per category.
- max(f_cross, f_div) ≤ f(u) ≤ f_cross + f_div.

Each is checked with a rounding allowance that scales with L. A failure marks the sample inconsistent. In 100,000 extreme rows (INT32_MIN, INT32_MAX, every magnitude) with valid arguments, neither check failed once.

An earlier draft used Paper A's construction directly, with x = r restricted to its dominant category. For a general r that is wrong: perfect agreement still gives u_cross > 0, because r itself has energy outside that category. The profile form above has no such defect, and it reduces to Paper A in the pure-block case.

**Lipschitz drift check.** f has Lipschitz constant L = N − 1 and f(0) = 0. A component t can therefore move any downstream F by at most L·t. A sample drifts across categories when L·u_cross > τ_cross, and within them when L·u_div > τ_div. A magnitude channel compares |a| with |r| (relative tolerance `tol_mag`), because u cannot see scale. `ct_norm_ratio` is within 7.5e-4 of float64.

**Paraconsistent verdict.** The states use `swarm_hk.h`'s order and values, and no input panics or asserts:

| Verdict | When |
|---|---|
| TRUE | direction and magnitude agree, no NaN or Inf in the row |
| FALSE | both channels disagree, or a faulted row that also disagrees, or an all-NaN row |
| GLUT | one channel agrees and the other does not; the contradiction is held and counted |
| PARADOX | the arithmetic contradicts the theorems, or a row with NaN or Inf whose finite part still agrees, or bad arguments: escalate |
| UNKNOWN | the reference or the output is all zeros, with no faults: no evidence |

In the simulated pipeline, rows rounded through FP16 give 1000 TRUE out of 1000. Each of the following gives the listed verdict:
- a 10% scale error: GLUT
- categories rotated: GLUT, with L·u_cross 2.34
- values reversed inside each category: GLUT by divergence alone, with u_cross ≤ 2 Q16 units
- rotated and doubled: FALSE
- one NaN in an agreeing row: PARADOX

The sampling helper `ct_sample(seed, index, rate)` deterministically picks the rows to re-run on the integer engine. At rate 1/4 it picked 24,823 of 100,000.

## Tier 3b: the budget (`ct_budget`)

**Headroom.** h_t = η ln N − δ Q_t − C_t (Q16). This is the largest one-step increment of the open-system ledger Q_{t+1} = (1 − δ)Q_t + ηS_t − C_t (`surplus.h`, `surplus_max_increment`).

**Cost from hardware.** A host callback reports any of three metrics. Each one that is present adds its weighted term to C_t:
- power: w_watts · min(P / P_ref, 4)
- stalls: w_stall · stall / total (64-bit counters)
- thermal: w_temp · max(0, 1 − headroom / headroom_ref)

Missing metrics, a missing callback, or a callback that returns false all add zero. The map is linear and its weights are configuration, not physics. **They must be calibrated on each machine.**

**Controller.** The controller has a ladder of throttle levels. Each level halves the batch (down to `batch_min`) and steps the context cap linearly down to `ctx_min`. The last level turns the accelerator off.
- It throttles one level when h_t or the prediction h_t + (h_t − h_{t−1}) falls below h_low, and two levels when h_t < 0. The prediction is used only when both readings were taken at the current level, so the controller's own step is not mistaken for a trend.
- It relaxes one level only after `hold` cycles above h_high, and only if h_t minus the level's learned relief stays above the middle of the band. The relief is the largest cost drop seen within `hold` cycles after throttling past that level. Without the relief check, a band narrower than one level's cost step makes a limit cycle. The first version of this controller did exactly that: 57 chatter events on the base trace.

**Measured on a simulated machine** (`test_ct_budget`). The simulation has ambient power rising 0 → 60 W, holding, then falling, plus load power, stalls that depend on context, a lagging temperature, and ±2 W of deterministic noise. η = 1, δ = 0.05, N = 16, 6 levels, band [0.25, 0.60], hold 8.

| Run | min h_t | level changes | reversals | reversals within 2·hold |
|---|---|---|---|---|
| no controller (flat out) | **−1.407** | – | – | – |
| controller | **0.278** | 3 | 1 | 0 |
| controller, ramp 4× faster | **0.210** (flat out: −1.405) | 4 | 1 | 0 |
| narrow band (0.25 to 0.25 + 2^-16), hold 1 | 0.214 | 58 | 55 | 8 |

With a lower power weight, the controller throttles and then returns to full speed once the load has passed. With the base weights, an idle level 0 settles at h ≈ 0.52. That is inside the band, so the controller stays at level 1. This is by design: it does not relax into the band. Under a sustained overload it walks to the top level: accelerator off, batch 2, context 512.

**Storage inequality.** `ct_budget_commit(b, F, fn, ctx, rec, len)` calls the caller's ledger commit function only when δ·Q + C ≤ η·F(u). Otherwise it returns `CT_COMMIT_REFUSED` without calling it, and the ledger's own error is passed through. The pay or swarm triple ledger is reached only through that callback. This module never touches a ledger.

## Measured: what the gate costs in accuracy

`test_ct_measure` rebuilds the three fixture models of `src/tensor/test_model_fixture.h` byte for byte (qwen2 with GQA and QKV bias; qwen3 with QK-norm, head_dim 32 and a tied output; llama with interleaved RoPE, rope_freqs and F16 matrices; each 2 layers and 64 wide). It uses the fixture read-only and runs its **own float64 forward pass** from the same weights. Against the fixture's float64 reference logits, that forward pass has a max error of 7.6e-6 and matches 61 of 61 argmaxes. `ct_gate_attn` masks edges exactly as an attention block would. Pruned and unpruned runs share the harness, so every difference comes from the gate.

Setup:
- 4 sequences of 96 random tokens per model
- window 8, 8 clusters, 2 passes, N = head_dim
- greedy decoding of 32 tokens after a 16-token prompt

In the table:
- "pruned (eligible)" is the share of causal edges outside the window that were pruned.
- Errors are logits against unpruned, with σ the logits' standard deviation.
- "greedy prefix" counts matching tokens before the first divergence.

| model | policy (floor) | pruned (eligible) | max err | RMS err | top-1 agree | greedy prefix / 32 |
|---|---|---|---|---|---|---|
| qwen2 | surplus 0.80 ln N | 0.2% | 0.23σ | 0.008σ | 99.5% | 4 |
| qwen2 | surplus 0.90 ln N | 4.4% | 1.52σ | 0.122σ | 86.5% | 9 |
| qwen2 | random 4.3% | 4.3% | 1.41σ | 0.088σ | 88.3% | 4 |
| qwen2 | surplus 0.95 ln N | 15.3% | 2.50σ | 0.223σ | 70.6% | 1 |
| qwen2 | random 15.1% | 15.1% | 2.49σ | 0.187σ | 73.4% | 0 |
| qwen2 | align cos ≥ −0.25 | 17.6% | 1.40σ | 0.148σ | 75.0% | 0 |
| qwen2 | surplus 0.99 ln N | 51.3% | 3.68σ | 0.452σ | 43.8% | 0 |
| qwen2 | random 50.8% | 50.8% | 3.83σ | 0.394σ | 48.2% | 0 |
| qwen3 | surplus 0.95 ln N | 2.1% | 2.62σ | 0.150σ | 91.7% | 32 |
| qwen3 | random 2.0% | 2.0% | 1.33σ | 0.083σ | 97.7% | 32 |
| qwen3 | surplus 0.98 ln N | 13.8% | 2.61σ | 0.309σ | 77.1% | 28 |
| qwen3 | random 13.5% | 13.5% | 2.43σ | 0.226σ | 82.6% | 32 |
| qwen3 | align cos ≥ −0.25 | 7.2% | 1.66σ | 0.137σ | 90.4% | 32 |
| qwen3 | surplus 0.99 ln N | 28.2% | 3.03σ | 0.460σ | 66.7% | 24 |
| llama | surplus 0.90 ln N | 4.1% | 0.90σ | 0.087σ | 91.1% | 2 |
| llama | random 3.9% | 3.9% | 1.36σ | 0.073σ | 89.6% | 2 |
| llama | surplus 0.95 ln N | 16.7% | 1.64σ | 0.179σ | 73.2% | 2 |
| llama | random 16.5% | 16.5% | 1.47σ | 0.149σ | 80.7% | 0 |
| llama | align cos ≥ −0.25 | 19.3% | 1.60σ | 0.132σ | 79.7% | 3 |
| llama | surplus 0.99 ln N | 54.0% | 2.91σ | 0.380σ | 51.3% | 0 |

At ≤ 0.5 ln N nothing is pruned on any model: in 16 or 32 dimensions centroid pairs are nearly orthogonal, so F sits close to ln N. Floors below about 0.8 ln N are no-ops. Every row of the sweep (floors 0.5 to 0.99, align −0.25 to 0.5, random at each matched rate) is printed by the test and saved to `/tmp/test_ct_measure.log`.

**What the numbers say.**
- On these models, F(u) pruning at a given rate costs about as much as random pruning at that rate, usually a little more. Of the 13 matched pairs in the full log, its RMS error is worse in 12 (the exception is qwen2 at 0.2%). Its top-1 agreement is lower in 10, and higher only at the three smallest rates (qwen2 at 0.2%, llama at 0.1% and 4%).
- The similarity baseline (`align`) has lower RMS error than random at a similar rate, but it is not cheap either.
- Greedy chains diverge after a few tokens at almost any nonzero pruning rate on qwen2 and llama. qwen3's chains survive up to about 14% pruning (a matching prefix of 28 to 32 out of 32).

These are random-weight models, whose attention is diffuse with no learned sparsity. That is close to the worst case for any pruning rule, so the result says little about trained models in either direction. It does mean there is no evidence here that F(u) selects the edges that matter. **On a real model, F(u)-based pruning is unvalidated until someone runs it.** Run `test_ct_measure`'s harness, or wire the hook into `zt_model` and compare logits against an unpruned run on held-out text, before any floor above zero is used.

## Limits

- **The accuracy cost has been measured only on three tiny random models,** in a float64 harness. Neither the integer `zt_model` nor a real checkpoint has been run with the gate.
- **The gate re-clusters on every call.** It is cheap against attention only for blocks of many queries over long contexts: ratio 0.056 at 256 × 4096, and 0.55 at 96 × 96. Per-token decode needs persistent key clusters, which are not implemented.
- **N for F(u) is a free parameter.** The default is head_dim, so the floor's meaning changes with it. There is no derivation tying N to an attention head.
- **C_t is a linear map with hand-set weights,** and the plant in `test_ct_budget` is simulated. No real power, stall or thermal counters were read. The guarantee that h_t ≥ 0 depends on the top throttle level actually lowering the cost below η ln N.
- **Reconciliation checks one sampled row at a time against a reference the caller supplies.** It does not choose which rows matter.
- **Wiring is left to the owners.** The gate is not wired into `zt_model` (owned by the tensor worker), and the commit hook is not wired into any ledger. Neither `kernel/Makefile` nor `src/tensor/**` was changed.

## Files and verification

- `kernel/src/cotier/ct_gate.{h,c}`, `ct_reconcile.{h,c}`, `ct_budget.{h,c}`: the modules.
- `kernel/src/cotier/test_ct_gate.c`: 25 checks. Arguments, cluster purity, F on aligned and orthogonal pairs, SURPLUS and ALIGN selection, window and diagonal, the keep list, the hook, determinism, cost at 4096 tokens, and the invariant fuzz.
- `kernel/src/cotier/test_ct_reconcile.c`: 32 checks. Exhaustive FP16 and BF16 ingest, FP32 ingest (all 2^32 patterns with `full`), the decomposition against float64 and against `surplus_decompose`, the verdicts, garbage and extreme fuzz, and sampling.
- `kernel/src/cotier/test_ct_budget.c`: 29 checks. The cost map, h_t, the storage inequality and its hook, the rising-cost traces, hysteresis, and overload.
- `kernel/src/cotier/test_ct_measure.c`: 30 checks plus the measurement table.

The unit tests build under `-fsanitize=address,undefined`, and so does the measurement harness. The verify-all recipe lines (for `kernel/Makefile`, run from `kernel/`) also build every module freestanding for aarch64 and i386 and check `nm -u`.

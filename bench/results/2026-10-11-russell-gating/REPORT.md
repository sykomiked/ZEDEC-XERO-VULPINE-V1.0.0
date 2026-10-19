# Russell-style gating of the golden-ratio parts, measured on a real model (2026-10-11)

The owner proposed using Walter Russell's cosmology as a rule set for the engine's
golden-ratio ("metaphysical") parts that hurt the model in the first real-model report
(`bench/results/2026-10-10-qwen2.5-1.5b/REPORT.md`, section 2): phi scales, the lossy
9-shell holographic decode, the slow E8 lattice quantiser, and 2-bit weights everywhere.
Each proposal was turned into something that can be measured and judged by one rule.
**A change stays only if it measurably helps against the exact (float64) computation.**

Model, ids and machine: Qwen2.5-1.5B-Instruct Q4_K_M (SHA-256 `6a1a2eb6…407e`), the first
128 WikiText-2 tokens of the earlier report, Intel Xeon @ 2.80 GHz, 4 threads. Every score
is the mean KL(float64 || variant) over positions 1..127 (lower is better). The float64
reference is `bench/tensor/compare/ref_f64.py`. The parts rewrite the 8-bit converted
weights (`zt_golden`, which loads with `ZT_LOAD_CONVERT`).

For scale (logs/context_engines_128.tsv):

| Engine | KL vs float64 | Top-1 |
|---|---|---|
| ZXV, native K-quants (the default now, 4.85 bits a weight from the file) | 0.000010 | 100% |
| ZXV, converted 8-bit (the starting point of every variant below) | 0.000573 | 100% |
| llama.cpp | 0.009036 | 95.3% |

## Summary, per part

| Part | Russell idea applied | Result | Kept? |
|---|---|---|---|
| E8 lattice quantiser | "Inert-gas fulcrum": snap to the nearest lattice point first, search only locally | The engine already did the closed-form nearest point first. The slow part was the local search, which regenerated each of the 240 roots every time. A root table and an incremental distance make it 6x faster with **identical output** (2,000,000 + 300,000 inputs checked). Whole model: 1,867 s → 134 s with 4 threads. | **Yes** (speed only; same results) |
| Phi scales | Generative/radiative tiers: keep the 15 most sensitive matrix groups exact, phi elsewhere | Harm drops about 4x (KL added over the 8-bit base: 0.00111 → 0.00026). Phi still never beats no phi, and a finer √2 ladder beats phi at the same storage. | **Tiering kept** for anyone using phi scales; phi itself stays opt-in |
| Holographic 9-shell decode | Tiers: only on the 10 least sensitive groups, or one group | Still breaks the model: KL 1.51 on those 10 groups and 0.050 on one group, 9 times worse than E8 on the same group. It also saves nothing (8.5 bits a weight either way). | **No.** Keep holographic coding lossless (10 shells) on the device only, as already decided |
| 2-bit everywhere | E8 only on tolerant matrices, 4-bit or 8-bit on the rest | E8 beats plain 2-bit wherever both were tried (10 groups: 0.083 against 0.110; mixed with 4-bit: 0.117 against 0.141). But even the best 2-bit tier costs 60 times the 8-bit base's error, and every mix is far worse than simply using the file's own 4-bit weights natively (0.00001). | E8 over plain 2-bit **confirmed**; 2-bit tiers **not** for deployment without a model trained for low bits |
| Nine-octave sharing | Deeper layers reuse the first layer's codes in each group of 2 or 4 | Breaks the model even on one matrix type: attn_q in layers 8–23 shared by twos gives KL 0.59, ffn_down 2.99, every matrix by fours 9.5. | **No** |
| Zero-sum energy rule / fail-closed clamp | — | Not built, because it would remove what the model needs. The observe-only log below shows the model builds a large "generative" signal in layers 1–2 and a matching "radiative" cancellation in layer 26, 24 layers apart. A per-layer zero-sum rule or a clamp would cut that apart. | Observation only |
| Dual-cone sums, scratch recirculation, inert reset between layers | — | Already true of the engine: integer sums are order-free and exact, the state buffer is allocated once, and RMS norm sits between layers. | Nothing to build |

## 1. Which matrices tolerate coarse weights (logs/sensitivity_scores.tsv)

E8 2-bit weights were put on one group at a time: one matrix type in four consecutive layers
(7 types x 7 bands = 49 groups), plus the output matrix. Each group alone adds KL over the
8-bit base:

* Most tolerant: attn_q in layers 8–27 and attn_v in layers 8–15 (KL about 0.005 to 0.010).
* Least tolerant: the output matrix (0.91), attn_q in layers 4–7 (0.55), ffn_gate in layers
  0–3 (0.17), and the MLP matrices of layers 20–27 and 0–3 (0.08 to 0.12).
* Even the most tolerant group costs about 10 times the 8-bit base's whole error, so the
  costs add up quickly: E8 on every matrix gives KL 5.7 (logs/octave_share_scores.tsv,
  `e8_all`).

These are the "generative" (keep exact) and "radiative" (can go coarse) tiers, measured
instead of assumed. Note that the MLP matrices are not all tolerant: the layers that build
and cancel the large activations are among the least tolerant.

## 2. Tier results (logs/tier_scores.tsv)

| Variant | KL vs float64 | Top-1 | Bits a weight (est.) |
|---|---|---|---|
| base: 8-bit converted | 0.000573 | 100% | 8.50 |
| phi_tier: phi except on the 15 hottest groups | 0.000837 | 100% | 8.35 |
| sqrt2_all: scale snapped to powers of √2 | 0.001073 | 99.2% | 8.19 |
| phi16_all: phi as the engine ships it (Q16 ladder) | 0.001499 | 96.9% | 8.50 |
| phi_all: scale snapped to powers of phi | 0.001685 | 100% | 8.19 |
| pow2_all: scale snapped to powers of 2 | 0.001889 | 99.2% | 8.19 |
| e8_low5: E8 on the 5 most tolerant groups | 0.034206 | 87.4% | 8.34 |
| int4_tier: 4-bit except the 15 hottest | 0.036742 | 93.7% | 6.59 |
| holo9_q12: 9-shell decode on attn_q layers 12–15 only | 0.049893 | 87.4% | 8.50 |
| e8_low10: E8 on the 10 most tolerant groups | 0.082905 | 88.2% | 8.00 |
| int2_low10: plain 2-bit on the same 10 groups | 0.110406 | 86.6% | 8.02 |
| mix_e8_int4: hot exact, E8 on 10 tolerant, 4-bit elsewhere | 0.116526 | 84.3% | 6.42 |
| mix_int2_int4: the same with plain 2-bit | 0.140797 | 84.3% | 6.43 |
| int4_all: 4-bit everywhere | 0.180582 | 78.0% | 4.50 |
| holo9_low10: 9-shell decode on the 10 most tolerant groups | 1.505718 | 35.4% | 8.50 |

Bits a weight are estimates. 8-bit and phi16 count a 16-bit scale per 32 weights; phi,
√2 and 2 count an exponent byte per 32; E8 counts 14.7 bits per 8 weights plus a scale.
`int4` here re-rounds the 8-bit converted weights and is much worse than the file's own
4-bit Q4_K weights used natively.

## 3. E8 speed (logs/e8_full_model_transform.log)

`zt_e8_quantize` first takes the closed-form nearest E8 point. If it lies outside the
26,641-point codebook ball (about a quarter of inputs here), it bisects and then walks by
the 240 roots. The walk regenerated every root on each step and recomputed every
distance in full. It now reads a root table and updates the distance from the last step.
These are the same integers, so the same root wins. `test_zt_lattice` checks this against
the original code on 300,000 inputs (heavy-tailed, saturating and tiny scales), and a
one-off check matched 2,000,000 more. The quantiser is 6.0x faster, and with `zt_golden`
now spreading rows over threads, E8 on the whole model takes 134 s instead of 1,867 s.

## 4. Energy balance, observed only (logs/energy_balance_float64.tsv)

From the float64 forward pass: per layer, the RMS of the residual stream and of each
update, and the cosine between an update and the stream (positive: the update builds
along what is there, "generative"; negative: it cancels it, "radiative").

* Layers 1–2: the MLP updates are 20 to 100 times the stream and line up with it (cosine
  +0.98 at layer 2). The stream's RMS jumps from 0.47 to 21.9, and to 247 at position 0.
* Layers 3–25: small updates (RMS 0.2 to 1.8 on a stream of about 23).
* Layer 26: the MLP update is opposite to the stream (cosine −0.95, and −0.9996 at
  position 0). The RMS drops from 23.4 to 7.3.

So the model has its own two-way motion, but it is not balanced layer by layer. It builds
in layers 1–2 and cancels in layer 26. That is why a per-layer zero-sum rule or a
fail-closed clamp was not built: it would remove the signal the model carries across
those 24 layers. The new `zt_model_state_t.n_sat` counter (values clipped at ±32768) reads
0 on this model, so nothing clips today.

## 5. What changed in the code

* `kernel/src/tensor/zt_lattice.c`: E8 local search by root table and incremental
  distance (same results, 6x faster); `test_zt_lattice.c` keeps the original search as the
  reference.
* `kernel/src/tensor/zt_model.{c,h}`: `n_sat`, the count of values clipped at the Q16
  limit (matrix outputs and residual additions), for the build thread's logic tag. It
  does not change any result.
* `bench/tensor/zt_golden.c`: `--rules` (a different part on each matrix), `--scan`
  (many variants after one load), threaded transforms, and the `pow2`, `sqrt2`, `int4`
  and `shareN` controls.
* `bench/tensor/compare/cmp_scan.py` scores scan outputs. `ref_f64.py` can write the
  energy table.

## 6. Reproducing

```sh
make -C bench OUT=/tmp/zb zt_golden
head -c 512 ids.bin > ids128.bin
python3 bench/tensor/compare/ref_f64.py $MODEL ids128.bin 128 ref64_128.f32 energy.tsv
/tmp/zb/zt_golden $MODEL ids128.bin none --scan logs/sensitivity_variants.txt --outdir s1 --threads 4
/tmp/zb/zt_golden $MODEL ids128.bin none --scan logs/tier_variants.txt --outdir s2 --threads 4
python3 bench/tensor/compare/cmp_scan.py 128 ref64_128.f32 ids128.bin s2
```

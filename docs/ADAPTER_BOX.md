<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# The adapter box

The adapter box lets an off-the-shelf model file run on ZXV with no
retraining: drop in the weights, and the system loads them into its
deterministic engine, meters them in its own denominations, and checks
what they produce before anything settles. It is the compatibility layer
(a virtual ECU) between the open-weights world and ZXV's exact runtime.

Most of it is built. This page maps the design onto the code and lists
what is missing.

## The three stages, in code

| Stage | What the design asks | What exists | State |
|---|---|---|---|
| 0. Load | take GGUF, SafeTensors or ONNX weights as they are | `kernel/src/tensor/zt_gguf.c`: GGUF with F32, F16, BF16, Q8_0, Q4_0, Q4_K, Q5_0, Q5_1 and Q6_K tensors. Float bits are converted to Q16 fixed point with integer-only code; every forward pass after load is integer arithmetic (`kernel/src/tensor/zt.h`, T1-T5), so a given prompt gives the same tokens on x86 and ARM. LoRA adapters: `test_zt_lora.c`. CI runs zxvd on Qwen2.5-1.5B. | GGUF built; SafeTensors and ONNX missing |
| 1. Transduce | keep the model's own tokenizer; express its work in Fibonacci denominations | the model's own BPE vocabulary is read from the GGUF (`zt_tok.c`, gpt2 and qwen2 pre-tokenizers, `test_zt_tok.c`). Denominations: `kernel/src/swarm/swarm_denom.h`, Zeckendorf canonical form, Fibonacci wire code, octane grades | built separately; the link between them is missing (below) |
| 2. Govern | pace generation to what the machine and the downstream ring can take | the swarm budget gives each model a per-cycle token allotment and charges it as tokens are consumed (`swarm_denom_payout`, `swarm_denom_consume`, `swarm_denom_end_cycle`); bounded rings are the trade-routes design (`docs/TRADE_ROUTES.md`) | budgets built; ring backpressure on the generation loop not wired |
| 3. Manifold | turn model text into exact values; refuse contradictions; route by stratum | `rat_from_string` (`kernel/src/rational/rational.h`) parses a decimal or ratio literal exactly, with overflow reported as invalid; verdicts are `lpres_state_t`; strata are `build_system/strata.txt` | parts built; no component applies them to model output |

## Two rules the design must keep

1. **Pacing never changes the output.** Tokens per cycle (TPC) is a count
   per budget cycle, which is deterministic. A wall-clock or cycle-counter
   gate (`rdtsc`, `CNTVCT_EL0`) may slow generation down, but nothing a
   model produces, and nothing it is charged, may depend on how fast the
   machine ran. Otherwise two machines given the same prompt and budget
   would settle different results.
2. **Verdicts use lpres, not Belnap's lattice.** A model answer that
   conflicts is BOTH and one that lacks something is NEITHER; both stop
   the answer before it settles. They combine with `lpres_conjoin` (min on
   F < N < B < T), so BOTH and NEITHER together never yield TRUE.

A decimal a model writes is converted exactly as written: `0.33333`
becomes 33333/100000, not 1/3. The manifold records what the model said;
it does not guess what it meant.

## What is missing

In order of value:

1. **Metering generated tokens into wallets.** Each token a model
   generates is charged against its wallet in denominations, and the
   cycle's bill reconciles (`swarm_denom_reconciles`). This turns the
   budget into the auditable compute bill the design describes.
2. **The manifold as one component.** Model output parsed by
   `rat_from_string` for numbers, classified into an `lpres` verdict, and
   routed: values to the settlement path, actions to the Cage, nothing
   on BOTH or NEITHER.
3. **SafeTensors loading.** The format is a JSON header plus raw tensors;
   it fits `zt_gguf.c`'s conversion path (F16, BF16, F32 to Q16). ONNX is
   a full graph format and a much larger job; it comes after.
4. **External runtimes.** A shim that lets llama.cpp or ONNX Runtime feed
   zxvd's socket is possible, but those runtimes use floating point and
   are not deterministic across machines. Their output enters as foreign
   input: through the Cage, never into settlement directly, and the
   socket must check the peer's credentials (`SO_PEERCRED`) as zxvd's does.

## Notes on the first reference code

The reference implementation proposed with this design was not taken into
the tree. It repeats `swarm_denom` and `rational` (above), and review
found:

- the Fibonacci table is declared with 47 entries but initialized with
  45, so the encoder starts on two zero entries, sets bits worth nothing
  and reports the wrong top tier;
- the decimal parser does not check for overflow and accepts "." and "-"
  as zero (`rat_from_string` handles all three);
- the cycle gate returns on the first loop pass and never enforces its
  cycle budget, and the shim passes an empty ring, so backpressure never
  fires;
- the shim never forwards to zxvd's socket, listens on a world-writable
  `/tmp` path without a peer check, and prices by bytes read, not tokens;
- gating generation on the cycle counter breaks rule 1 above.

# The ZXV model: which weights we derive, which we fit, which we learn

This document designs the AI model that runs on ZXV's integer tensor
engine (`kernel/src/tensor`). It answers one question first: **which of the
model's weights can be worked out mathematically, and which cannot.** Every
later choice follows from that answer.

Status: design, not built. Section 7 lists the slices in build order, each
with its acceptance test. File paths name code on `main` or in the tensor
engine pull requests (#85, #92, #93, #94, #99) waiting in merge batch 3.

## 1. The answer in one table

A model's weights are its program; the architecture is the machine that
runs it. Some programs we can write down exactly. Others only exist as
statistics of data.

| Kind of weight | How it is obtained | Exact? | Examples in ZXV |
|---|---|---|---|
| **Derived** | Computed from a definition: a table, a grammar, a program compiled into a network | Yes, bit for bit, on every CPU | GELU/sigmoid/RoPE tables (`gen_nn_tables.py`, `gen_rope_tables.py`); grammar token masks (`zt_gram`, #99); compiled specialists (§4, new) |
| **Fitted** | Solved in closed form from measured data: a least-squares or linear-algebra solve, no gradient descent | Yes: the solve is in exact rationals, then rounded once with a stated error bound | Evidence head μ/λ and router head (§5, replacing today's SGD fit); low-rank factors of a known weight change (§6) |
| **Learned** | Trained on data by gradient descent, off the engine | No: depends on data, order and hardware; the *result* is then frozen and runs bit-exact | The base language model (Qwen2.5 0.5B/1.5B, Apache-2.0); language-skill LoRA adapters |

Two consequences shape everything else:

1. **Understanding language cannot be derived.** No formula produces the
   weights that make a model read an invoice or a 1960s COBOL comment; those
   come from training data. ZXV therefore keeps a learned base model, and
   treats it as a *proposer*, never as an authority.
2. **Everything that is a fixed function can be derived or fitted.** Format
   conversion, field extraction, the four-valued logic, arithmetic, routing
   and scoring do not need training. Where they run inside the model they
   are built from mathematics; where they run outside it they are plain
   deterministic code.

Integer inference makes the model reproducible: the same prompt gives the
same tokens on x86, arm64, RISC-V or s390x (already true of `zt_model`, see
`docs/ARCHITECTURE.md` §10). Reproducible is not the same as correct. A
deterministic model still answers wrongly when its weights are wrong. ZXV's
guarantee therefore comes from §3's rule, not from the arithmetic alone.

## 2. The four layers

| Layer | What the AI does | What decides | Weights |
|---|---|---|---|
| UI / canvas | Turns a plain-language request into a circuit: nodes and wires, in a circuit grammar | The circuit type checker: every port type, every wire, every capability | Learned base + grammar mask (derived) |
| Inter-app bus | Proposes an adapter program for a wire whose two port types differ | The adapter verifier: types, examples, properties; the accepted adapter runs as deterministic code | Learned base proposes; nothing neural runs on the wire |
| Logic | Scores each claim's evidence for and against (μ, λ); a Belnap–Dunn engine merges claims into T, F, B, N | `pdx_agent` (exact, `kernel/src/paradox/pdx_agent.h`) and the lattice tables in `kernel/src/lpres` | Evidence head: fitted. Lattice: derived |
| Kernel / arithmetic | Nothing autonomous | Exact rational and integer code, as today | None |

The kernel layer deserves a direct answer to the original brief, which
proposed a model that "proves algebraic reductions and simplifies fractions
on the fly". ZXV does not put a model in the kernel's arithmetic path.
Rational normalisation is already exact and cheap, and a model there would
add a component that can be wrong where none is needed. If a model ever
suggests a rewrite (a faster formula, a reordering), it is a proposal like
any other: it goes through §3, and a rewrite is accepted only with a
machine-checked proof or an exhaustive check over its domain.

## 3. The rule: models propose, the kernel verifies

Every model output enters the system as a **claim**, never as an action.

1. The claim is decoded under a grammar (`zt_gram`, #99), so it is always
   well formed: a circuit, an adapter program, a ledger query, a T/F/B/N
   tag. The grammar "constrains the form, never the truth".
2. It carries the computation tag the app already writes
   (`kernel/arch/hosted/zxv_answer_tag.h`): a saturated logit or a failed
   pass makes it FORBIDDEN, and it goes to the quarantine log.
3. It carries the evidence head's verdict (§5): TRUE, FALSE, BOTH or
   NEITHER, with μ and λ.
4. Before anything executes, a deterministic checker accepts or refuses it:
   - circuits are type checked and capability checked;
   - adapters are checked against the destination type, the user's examples
     and generated property tests;
   - postings go through `pay_ledger_post` like any other caller.
5. A claim marked BOTH (the evidence conflicts) is not resolved by picking a
   side. It is held. The system asks for the missing input (a field, a
   source, a user decision), or runs both branches as separate, labelled
   circuits.

What runs live is therefore always deterministic code or derived/fitted
weights. Learned weights only write proposals.

## 4. Compiled specialists: programs as derived weights

A **compiled specialist** is a small transformer whose weights are computed
from a program, in the RASP/Tracr tradition, instead of being trained. It
runs on the unchanged engine, in the same GGUF format as Qwen, and it
computes its function exactly on its whole input domain.

### Why compile a program instead of running the code?

Plain code is faster and simpler, and stays the default. A function is
worth compiling only when it must live *inside* the token stream:

- **Routing and gating heads** that read the base model's hidden state
  (§5);
- **Verifier heads** that score a draft token by token during generation;
- **Swarm uniformity**: a specialist shipped as a GGUF (plus an optional
  LoRA) is fetched, verified by hash, run and metered exactly like any other
  model over AIPI and Vinea (§8).

### What the engine imposes on a compiled network

These limits come from `zt_model.c` and decide how programs compile:

| Engine fact | Consequence for compiled weights |
|---|---|
| Weights are int8 times a scale per block of 32 (Q8_0 is used in place, losslessly) | Compile to values that are exact int8 multiples of one scale per 32-weight block |
| Activations are Q16.16 and saturate at ±32768 | Keep every intermediate within range; saturation is counted (`n_sat`) and shows in the tag |
| Softmax treats a weight below 2^-16 of the largest as zero | Hard attention is exact when the chosen score leads the next by more than 16·ln 2 ≈ 11.1; compile scores with that gap |
| RMSNorm, SwiGLU, RoPE are fixed; no ReLU, no LayerNorm in the LM path | Steps and selections are built from SiLU pairs (a sharp gate is a difference of two scaled SiLUs); RoPE is cancelled where position must not matter |
| Byte-level BPE vocabulary | Use the 256 byte tokens: a byte-to-byte function maps token to token with no tokenizer changes |

### First specialists

| Specialist | Function | Shape |
|---|---|---|
| `belnap` | Belnap–Dunn ∧, ∨, ¬ on T/F/B/N tokens | One MLP layer: a lookup over 4×4 inputs |
| `ebcdic` | EBCDIC (code page 037) to ASCII/UTF-8, byte by byte | Attend to the current position; one MLP layer as a 256-entry map |
| `field` | Cut a fixed-width record at given columns | Position-selective hard attention |
| `digits` | Add two decimal numbers with carries | The classic compiled-carry construction; the domain is bounded by the context length |

Each one ships with its reference function, and its acceptance test is
exhaustive where the domain is finite (all 16 `belnap` pairs, all 256
`ebcdic` bytes). Otherwise the test is a differential run over a fixed
corpus plus random inputs. The digests of the weights and of every output
join `build_system/ai_cross.sh` (#99), which already proves masks and fitted
weights bit-identical on six CPUs.

## 5. Fitted heads: exact solves instead of gradient descent

The evidence head (`zt_evh`, #99) reads a hidden state h and gives
μ = σ(w_μ·h + b_μ) and λ = σ(w_λ·h + b_λ). The para-analyser then maps (μ, λ)
to T, F, B or N. Today it is fitted by stochastic gradient descent in fixed
point. That fit is deterministic given its sample order, but it has no
optimality guarantee and no error bound.

A **fitted** head replaces that with a closed-form solve.

1. **Measure.** Run the base model over a labelled set and record the
   hidden state at a tapped layer (`zt_model_set_tap`, #99). The
   activations are already exact Q16 integers, so the design matrix X is
   exact.
2. **Solve exactly.** Fit a ridge least-squares probe on logit-transformed
   targets: w = (XᵀX + αI)⁻¹ Xᵀy, with α rational. Solve it with
   fraction-free Gaussian elimination (Bareiss) over integers. Every
   machine gets the same rational w; there is no learning rate and no
   epoch order.
3. **Round once.** Convert w to the engine's int16-with-shift form and
   record the largest per-weight rounding error and its worst-case effect on
   w·h over the measured range.

The labelled data can itself be constructed rather than collected.
`pdx_agent` (`kernel/src/paradox/pdx_agent.h`) generates statements whose
Belnap value it knows exactly: consistent, contradicted, unsupported,
self-contradicting. The base model reads them, and the probe learns to read
the model's state. The labels are derived; only the model's hidden states
are measured.

The **router head** is the same construction with one output per
specialist or adapter. Given the base model's state on a request, it scores
which specialist should take it. It is fitted from requests labelled by the
circuit checker (which specialist's output passed).

The honest test: `ARCHITECTURE.md` §10 already queues "step 1: cheap CPU
probe on Qwen hidden states". The exact fit makes that probe reproducible.
It does not promise the probe finds a signal. If the held-out accuracy is
at chance, the head is not shipped, and that result is recorded.

## 6. Adapters and merging: arithmetic on weights

LoRA per agent is built (#94): an adapter adds B(Ax) after each base matrix
product, per sequence, bit-exact against a float64 reference. Three ways to
get an adapter follow.

| Source | Method | Exact? |
|---|---|---|
| Learned | Train off-engine (GPU), export to GGUF, load with `zt_lora_load` | Frozen result is exact; training is not |
| Factored | A known weight change ΔW (from a compiled specialist grafted onto the base, or the difference of two fine-tunes) factored into A·B | Exact when ΔW has low rank by construction (fraction-free LU, or a CUR decomposition on chosen rows and columns). An SVD truncation is not exact and is reported with its error |
| Merged | Task arithmetic on two adapters of the same base: weighted sums, TIES sign election, DARE drop-and-rescale with a fixed seed | The arithmetic is exact in integers; whether the merged skills survive is an empirical question measured per merge |

Merging only works between adapters of the **same base model** (same
lineage and tokenizer). Combining unrelated models by interpolation usually
destroys both, and ZXV does not claim otherwise. Missing in the engine
today: adapters on `token_embd` and the output layer (refused), merging, and
KV-cache recompute after an adapter switch. Slice 6 adds what merging needs.

## 7. Build order

Each slice is one pull request with its acceptance test. No slice depends
on a later one.

| # | Slice | Acceptance test |
|---|---|---|
| 1 | Exact fitter: `zt_fit_ridge` (Bareiss over integers, rational α), and `zt_evh_fit_exact` built on it | Digest-identical on all six `ai_cross` CPUs; equals a Python `fractions` reference on the test sets; worst-case rounding bound printed and checked |
| 2 | Weights compiler: `tools/wcompile` writes a qwen2-architecture GGUF (Q8_0, byte vocabulary) from a specialist program, plus its reference function | `belnap` and `ebcdic` exhaustively equal to their reference on `zt_model`; weight and output digests in `ai_cross.sh` |
| 3 | Wire grammar decoding and the evidence head into the app (`zxv_zt_glue.c`) and libzxv | Every reply decoded under its grammar; the tag carries the T/F/B/N verdict; existing glue and answer-tag tests unchanged |
| 4 | Probe experiment: hidden states of Qwen2.5-0.5B on `pdx_agent`-generated statements, exact fit, held-out report | A bench report in `bench/results/`, whatever it shows; the head ships only above chance by a stated margin |
| 5 | Router and specialist slots: the swarm loop drives the M6 agents (`zxv_model_host.h`) instead of stand-ins | A request reaches the right specialist; the stand-in agents are removed |
| 6 | Adapter arithmetic: factored and merged adapters, plus KV-cache invalidation on adapter change | Merge results equal an exact rational reference; a merged adapter's skills measured on both tasks |
| 7 | Inter-app adapter synthesis: the model proposes an adapter program in a small typed DSL under a grammar; the verifier runs types, examples and properties | A wrong proposal is refused with the failing example; an accepted adapter runs with no model loaded |
| 8 | Prompt to circuit: circuit grammar, checker, and the canvas showing every node and wire for inspection and hand edits | Generated circuits that fail the checker never run; a hand edit is checked the same way |
| 9 | Swarm execution: AIPI `TENSOR_INFER` descriptors naming base, adapter and specialist by CID; QUORUM over bit-identical runs on Vinea | Two peers' outputs compared bit for bit; a disagreeing peer is quarantined |

Slices 1 and 2 can start now. Slice 4 needs only slices 1 and 3.

## 8. The swarm view

The swarm is many small models instead of one large one. The pieces map
onto parts ZXV already has:

| Swarm need | ZXV part | State |
|---|---|---|
| Small open models | Qwen2.5-0.5B and 1.5B (Apache-2.0), SmolVLM, Whisper, OuteTTS 0.3 | Running on the engine (`bench/results/`) |
| Hot-swapped skills | LoRA per agent, `zxv_agent_open` | Built (#94), not driven by the swarm loop |
| Peer discovery and sessions | Vinea: Kademlia, ML-KEM/ML-DSA | Built |
| Paid, checked calls | AIPI: functions by CID, CHECKER/CERT/QUORUM, escrowed bounties | Built for Z-Wasm; `TENSOR_INFER` not wired (slice 9) |
| Large models across machines | Tensor parallel with bit-identical results for any rank count (#92) | Slice 1 of `HPC_DESIGN.md` |
| Shared memory of results | `swarm_overlap` keyed by model, context and question hash; hash-chained logs | Built |

Because every peer's integer engine gives the same bits, QUORUM needs no
tolerance. Two honest peers agree exactly, so any difference is proof that
one of them is wrong or lying.

## 9. What this design does not claim

- It does not make a language model correct. It makes its errors
  reproducible, visible (tags and T/F/B/N verdicts) and harmless (nothing
  runs unverified).
- Compiled specialists are not faster than code. They exist where a
  function must live inside the model.
- Low-bit (ternary, 1-bit) weights need models trained for them. Measured
  on Qwen, E8 and 2-bit quantisation of every matrix break the model
  (`ARCHITECTURE.md` §10). No BitNet model is planned until one is trained
  and licensed for it.
- Model merging is measured per merge, and only within one base lineage.

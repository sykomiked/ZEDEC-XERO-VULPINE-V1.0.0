# The ZXV tensor engine (zt)

`kernel/src/tensor/` holds the arithmetic that will run the swarm's language models. All of it is integer maths, so a model gives exactly the same answer on every machine. The header `zt.h` states rules T1 to T16, and `test_zt.c` checks the results against double-precision references. Rules T17 to T20 sit at the top of their own headers, each with its own test.

## The core (T1 to T6)

- **Number format.** Activations are Q16.16 fixed point. Weights are stored in blocks of 32 eight-bit values with one scale per block, the same layout today's 8-bit model files use.
- **Golden scales.** A block's scale can be snapped to a power of φ. The engine computes φ^k exactly as F(k)·φ + F(k−1), without floating point.
- **Fibonacci tiling.** A matrix product splits its larger side at 1/φ until each tile is 21 × 21 or smaller. The result is bit-identical to the plain product; the gain is in how the work moves through memory.
- **Integer nonlinearities.** RMS norm, softmax, SiLU, e^x and Fibonacci hashing are all done in integers.

## The coil scaffold (T7 to T14)

- **Ten nested shells.** Shell s holds F(b+s) slots, so each shell is about φ times the one inside it. Elements are wound onto a shell by a fixed stride, either the golden stride F(n−1) or the ABHA doubling stride 2. Each winding has an exact inverse.
- **The core in the hole.** The core in the middle of the torus has F(b−1) slots, wound straight along the axis. Every path through the coil ends there.
- **A dendritic tree.** Parent links form a tree that branches about φ ways per shell. Distance is the number of steps to a common ancestor, which is a hyperbolic (non-flat) metric.
- **The field.** The coil is the wiring; the field is what the wiring emits. The field moves like an alternating current: on one half-cycle it flows inward to the core, on the other it flows back outward. Each move subtracts exactly what it adds, so the total field is conserved.
- **Reading the field as interference.** Two fields, the evidence for and the evidence against, are read together at every place. The reading gives one of the swarm's paraconsistent truth states: true, false, glut, neutral, paradox or unknown. A glut whose whole path to the core is also glut becomes a paradox and is escalated.
- **Coils of coils.** Every slot can hold another whole coil. Each device picks the largest coil that fits its fast memory and adds levels instead of density (`zt_device_plan`). A phone gets a smaller, deeper coil and a server a wider, shallower one, and both compute the same numbers.

## Surplus and holographic coding (T9, T15, T16)

- **Interaction surplus.** f(u) = ln(1 + (N−1)u) with u = 1 − cos²(a, b), computed in integers. Its axioms are tested: f(0) = 0, f(1) = ln N, and f rises with u.
- **The surplus gate.** It keeps a vector only if it adds enough surplus over the vectors already kept, so redundant heads or answers are skipped.
- **The open-system ledger.** Q' = Q − δQ + ηS − C decides whether the core grows this cycle or gets more efficient instead.
- **Holographic coding.** Each place is stored as its difference from its parent's prediction: the prediction is the negative space, the data the positive. Decoding is exact. Decoding only the inner shells gives a coarse picture of the whole.
- **UBH-168 frames.** Words travel in 21-octet frames: one tag octet, then five 32-bit words whose byte order alternates little, big, little. That is UBH_ENDIAN_MIXED, and decoding does not depend on the host machine's byte order.

## Running real models (T17 to T20)

- **GGUF model files (T18, `zt_gguf.h`).** It reads llama.cpp's file format in place from one memory buffer, with every count and offset checked against the buffer first.
  - Supported weight types: F32, F16, BF16, Q8_0, Q4_0, Q4_K and Q6_K. Other types are reported as unsupported.
  - Q8_0 blocks convert to the engine's own 8-bit blocks with no loss.
  - Tested against a file written by gguf-py, llama.cpp's own Python package. Every value is within 1 Q16 unit of its reference.
- **Tokenizer (T19, `zt_tok.h`).** Byte-level BPE, the kind Qwen2, Qwen3 and Llama 3 use.
  - It reads the vocabulary and merges straight from the GGUF file and builds index tables in caller memory.
  - It is tested against the Hugging Face `tokenizers` library on 172 texts under both the "qwen2" and "llama-bpe" splitting rules. Every token id matches.
  - Every input decodes back to the same bytes, including invalid UTF-8.
- **Rotary positions (T20, `zt_rope.h`).** Each frequency is stored as turns per position in 64-bit fixed point, so reducing the angle needs no division and stays exact at any context length.
  - Up to position 4 million, results are within 1 Q16 unit of a long double reference.
  - Both pair layouts are supported (NEOX and interleaved), as are Llama 3's per-frequency divisors.
- **Lattice quantisers (T17, `zt_lattice.h`).**
  - E8 nearest-point search and a 26641-point E8 codebook, which gives a 15-bit index per 8 weights.
  - A Leech lattice (Λ24) decoder built on the Golay code.
  - At 1.875 bits per weight, E8 measures 9.89 dB SNR, against 9.59 dB for a tuned 2-bit scalar quantiser.

## Limits, stated plainly

- **The coil is an analogy.** ABHA coils, scalar fields and alternating current here are a design language for memory layout, data flow and logic. Nothing in the code is electromagnetic, and no claim is made about physical scalar fields.
- **Speed is not yet measured.** The tests prove correctness. Whether the golden tiling, the coil layout and the surplus gate are faster than ordinary layouts on real hardware is still to be benchmarked.
- **Holographic coding helps only some data.** It saves space only when data that shares a parent is alike, and placing real model data that way is still to be designed.
- **The model runtime is in pieces.** Files, tokens, positions and quantisers are built and tested. They are not yet joined into one forward pass over a whole model.
- **Tokenizer coverage.** Only byte-level BPE. SentencePiece models (the GGUF tokenizer model "llama") are reported as unsupported.
- **RoPE coverage.** The YaRN and LongRoPE context extensions are not implemented.
- **Leech decoder speed.** The Leech decoder is brute force over 8192 cosets, about 20 µs per 24 weights. It is for research and small tensors, not bulk weights.

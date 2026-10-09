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
  - The E8 roots are the shared tables of `src/e8/e8_lattice.h` (see "One E8" below).
  - At 1.875 bits per weight, E8 measures 9.89 dB SNR, against 9.59 dB for a tuned 2-bit scalar quantiser.

## One E8 (the tensor engine and `src/e8`)

The engine's lattice quantisers (`zt_lattice.c`) and the icosian E8 in `kernel/src/e8` now read one set of tables, `kernel/src/e8/e8_lattice.h`. The header holds no physics. It holds the following facts, and `test_zt_e8.c` checks each one from the tables:

- **Coordinates.** E8 is used in its even coordinate system, doubled to integers. A point is 8 integers, all even or all odd, whose sum is a multiple of 4. The inner product is the plain dot product divided by 4.
- **The 240 roots.** There is one generator, `e8l_root2`, in one fixed order. Every root has norm 2, and inner products between roots lie in {−2, −1, 0, 1, 2}. Each root sees 1, 56, 126, 56 and 1 roots at those values. The roots are exactly the norm-2 shell of the engine's 26641-point codebook.
- **Cartan matrix.** The simple roots use Bourbaki's labels. Their Gram matrix is the E8 Cartan matrix, with determinant 1 and diagonal 2, so the lattice is even and unimodular. Every root is a whole-number combination of simple roots with all coefficients of one sign. There are 120 positive roots, and the highest is 2·3·4·6·5·4·3·2 (height 29).
- **Weyl group.** Every reflection in a root permutes the 240 roots. Reflections keep lattice points on the lattice, keep norms, and undo themselves.
- **The icosian basis is the same lattice.** The Gram matrix of e8.c's icosian basis has moved into the header, and e8.c reads it from there. Beside it sit the images of that basis in doubled coordinates. Mapping each basis vector to its image is an isometry: norms and inner products agree on random lattice points, and the 240 icosian roots (the quaternions from `e8_roots()`) land one-to-one on the 240 coordinate roots. `e8_to_coords2` and `e8_from_coords2` (in `e8.h`) convert both ways, and `e8_selfcheck` checks the images against the Gram matrix. Any two even unimodular lattices of rank 8 are isometric, so such a map must exist. The one in the header is one fixed choice.
- **Nearest point.** `zt_e8_nearest` passes a complete optimality certificate on 3000 random inputs: no root and no norm-4 vector moves its answer closer. Those 2400 vectors are E8's Voronoi-relevant vectors. This is in addition to the existing brute-force comparison.
- **Holographic coding on E8.** `zt_holo_e8_encode` and `zt_holo_e8_decode` run the T15 code on E8 points, one 8-wide block per coil place, as `zt_e8_nearest` writes them. E8 is closed under subtraction, so every residual is again a lattice point, and the round trip is exact. On tree-shaped test data, all residuals fell in the codebook ball, so each one is a single 15-bit index. How often that holds depends on the data.
- **What is not claimed.** The coil's shell sizes (Fibonacci numbers) have no relation to E8's shells (1, 240, 2160, 6720, …). The coil is a memory layout, and E8 is a quantiser and a code alphabet. Only the operations above connect them.

## Audit fixes (2026-10)

`test_zt_audit.c` contains one regression test per defect found. Each test failed before its fix; some failed only under `-fsanitize=undefined`, which is how the verify line runs it.

- `zt_phi_pow` held φ to 32 bits. Results drifted from correct rounding for k ≥ 26, reaching 776 units at k = 40. It now uses 1/φ to 64 bits and is exact for every k.
- `zt_quantize` overflowed on ±2³¹ inputs. `zt_dequantize` wrapped q·scale to 32 bits instead of saturating.
- `zt_dot` changed sign when one block's product passed 2⁶³, and it read a negative scale as a huge positive one.
- `zt_rmsnorm` and `zt_e8_rms` truncated each x² to Q16. Activations below 2⁻⁸ then read as zero, and `zt_rmsnorm` scaled them up 65536-fold. 1/rms was also held in Q16. Both now sum the squares exactly and keep about 31 significant bits.
- `zt_surplus_u` read small orthogonal vectors as zero vectors (u = 0). Since u = 1 − cos² does not depend on scale, it now normalises each vector first.
- The coil field and the AC half-cycles rounded negative values differently from positive ones. They now truncate toward zero, so the field of −v is −(field of v).
- `zt_holo_*` and `zt_coil_interfere` overflowed int64 (undefined behaviour). The holographic code is now exact modulo 2⁶⁴ for every int64. Interference saturates.
- In GGUF, Q4_K left-shifted negative values when d or dmin was negative.
- `zt_gguf_to_q8` clamped the scale for Q8_0 blocks with d ≥ 16384, giving 2–4× too small a value.
- `zt_gguf.c` used 64-bit division, which needs `__udivdi3` on the 32-bit kernel. It now divides only by powers of two and through `zt_udiv64`.
- The tokenizer's hash-table sizing could loop forever on a vocabulary or merge count above 2³⁰. Counts are now capped at 2²⁸. This bound is proved by reasoning, not by a test, because a file large enough to trigger it needs about 8 GB.
- The T14 truth states are renamed `ZT_COIL_TRUE … ZT_COIL_UNKNOWN`, with unchanged values, so they no longer collide with `src/harmonic`'s `ZT_TRUTH_*` names.

**Per-shell residual tap.** `zt_set_shell_tap(&tap)` makes `zt_holo_encode` add ⌊δ/256⌋ to `tap.acc[s]` for each place of shell s, saturating to int32. δ is the place's residual, or its value on shell 0. `NULL` disables the tap. The residuals are bit-identical with or without it. The tap is a single global, so it is not thread-safe.

## Limits, stated plainly

- **The coil is an analogy.** ABHA coils, scalar fields and alternating current here are a design language for memory layout, data flow and logic. Nothing in the code is electromagnetic, and no claim is made about physical scalar fields.
- **Speed is not yet measured.** The tests prove correctness. Whether the golden tiling, the coil layout and the surplus gate are faster than ordinary layouts on real hardware is still to be benchmarked.
- **Holographic coding helps only some data.** It saves space only when data that shares a parent is alike, and placing real model data that way is still to be designed.
- **The model runtime is in pieces.** Files, tokens, positions and quantisers are built and tested. They are not yet joined into one forward pass over a whole model.
- **Tokenizer coverage.** Only byte-level BPE. SentencePiece models (the GGUF tokenizer model "llama") are reported as unsupported.
- **RoPE coverage.** The YaRN and LongRoPE context extensions are not implemented.
- **Leech decoder speed.** The Leech decoder is brute force over 8192 cosets, about 20 µs per 24 weights. It is for research and small tensors, not bulk weights.

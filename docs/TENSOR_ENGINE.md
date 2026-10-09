# The ZXV tensor engine (zt)

`kernel/src/tensor/` holds the arithmetic that will run the swarm's language models. All of it is integer maths, so a model gives exactly the same answer on every machine. The header `zt.h` states each rule, T1 to T16, and `test_zt.c` checks the results against double-precision references.

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

## Limits, stated plainly

- **The coil is an analogy.** ABHA coils, scalar fields and alternating current here are a design language for memory layout, data flow and logic. Nothing in the code is electromagnetic, and no claim is made about physical scalar fields.
- **Speed is not yet measured.** The tests prove correctness. Whether the golden tiling, the coil layout and the surplus gate are faster than ordinary layouts on real hardware is still to be benchmarked.
- **Holographic coding helps only some data.** It saves space only when data that shares a parent is alike, and placing real model data that way is still to be designed.
- **Not built yet.** A model-file (GGUF) loader, the tokenizer, rotary position encoding, and the E8 and Leech lattice quantisers.

# The academic program — white papers and SDKs

After the system is complete and compiled, the H100 generates the
academic documentation **from the system's own records**: the
mathematics, the proofs, the tests, the measurements. Every claim in the
papers is a claim the code already proves, so the papers are *derived*,
not written — which is why they can be extensive and still honest.

## The white papers — one per discipline

Each paper stands alone (a reader needs no other) and cross-references
the rest. Each rests on the system's own artifacts, cited by path and
test.

| Paper | The discipline | The artifact it rests on |
|---|---|---|
| **I. The Paraconsistent Ring 0** | logic, formal methods | `kernel/src/lpres`, the rank table, the bridge, the 145-phase ladder |
| **II. The Traffic-Driven Resource Economy** | economics, accounting | `kernel/src/vino`, the tribute rails, `swarm_denom.h`, the 8:13 cycle |
| **III. The Immutable Epistemic Graph** | distributed systems | the curriculum CAR, the CID reproduction, the fleet |
| **IV. The 144-Dimensional Lattice** | geometry of numbers | the Fibonacci ladder, the two anchors (E8, Γ₁₄₄), the perfect-lattice criteria |
| **V. SCP-144: Sicilian Crown Protection** | cryptography, security | the point, the projection, the close-vector proof, the tier ladder, the paradox |
| **VI. Negentropy in Practice** | complex systems, physics | the ten order-creating mechanisms, the measured entropy reductions |
| **VII. The Omni-Tautology** | software architecture | the nine strata, the layer projections, the vertical integration measure |
| **VIII. Universal Language Hosting** | linguistics, compilers | the Rosetta elevator, the 224 human languages, the Sanskrit pivot, Enochian, the 104 programming languages |
| **IX. The World Computer** | web systems | the wasm engine and factory, the browser node, the client-side generation |
| **X. Adaptive Hardware** | systems, hardware | the self-adapting drivers, the three-anchor cross-compilation |

### The structure of each paper

1. **The claim** — stated precisely, in one paragraph.
2. **The construction** — how the system realizes it (the code, cited by
   path).
3. **The proof** — the tests that make it true (cited by name), including
   the negative-space results.
4. **The measurements** — the actual numbers (the anchors verified, the
   byte-identity results, the ratios).
5. **The limitations** — stated honestly, as the criteria do (what is
   specified but not built, what is measured but not proven).
6. **The reproduction** — the exact commands a reader runs to verify the
   paper's claims on their own machine.

## The software developer kits

A paper without a kit is an argument; a paper with a kit is a tool. Each
SDK ships the language bindings, the examples, and the conformance suite.

| SDK | For | Ships |
|---|---|---|
| **the core SDK** (`sdk/libzxv`) | C/C++/Rust/Go/Java/Python/… | the frozen ABI, the exact rational, the lpres values, the SCP-144 primitives |
| **the SCP-144 SDK** | security engineers | the point, the projection, the proof, the verify, the tier ladder, the legacy wrap |
| **the lattice SDK** | mathematicians | the Zeckendorf form, the ladder, the anchors, the elevator (`lift`/`project`) |
| **the app SDK** | app developers | the `.zxapp` grammar, the assembler, the deterministic generator, the judge |
| **the wasm SDK** | web developers | the engine, the factory, the packing, the CID |
| **the language SDK** | language designers | the five pillar runtimes, the Rosetta floors, the lift/project contract |
| **the network SDK** | network engineers | Vinea, the doors, the mesh, the lattice addressing and routing |

Each SDK carries a **conformance suite**: the three spaces (positive,
negative, neutral), the byte-identity test, and the reproduction
commands. A binding that does not pass the suite is not shipped.

## The academic artifacts

Beyond the papers and the kits:

1. **The formal specifications** — the transition functions, the operator
   tables, the tier ladder, in a machine-checkable form (the SMT/Coq
   outputs from the verification topology).
2. **The measurement corpus** — the raw results: the byte-identity across
   nine targets, the lattice anchors verified, the fuzz results, the
   test-to-code ratio.
3. **The reproduction harness** — one script that rebuilds the system,
   runs every proof, and prints the paper's numbers. A reader verifies
   the whole program without the H100.
4. **The curricula** — the teaching sequence: the three monographs plus
   the papers, ordered from the ancestral (writing, counting) through the
   modern to the future, matching the feature matrix.

## Why academia will engage

- **The claims are checkable.** Every paper cites code and tests; the
  reproduction harness proves them on any machine. That is the standard
  academic work is held to, and most systems cannot meet it.
- **The mathematics is novel.** The Fibonacci-lattice phase ladder, the
  two self-dual anchors (E8 and Γ₁₄₄), and the rank-based paraconsistent
  operators are publishable results, not engineering choices.
- **SCP-144 is a standard.** A named protocol with a stated threat model
  (Shor), a stated paradox (expensive to make, free to run), and a
  stated protection (the legacy wrap) is a paper in itself.
- **The negentropy is measurable.** The ten mechanisms' entropy
  reductions are testable, so the paper's claim is empirical.
- **The universality is demonstrated.** The Rosetta elevator hosting an
  unseen language is a concrete result in language theory.

## The generation plan (for the H100)

```
1. freeze the system (the release, the tests green, the anchors verified)
2. for each paper: gather the claims, the constructions, the proofs and
   the measurements from the tree; generate the paper; generate its
   reproduction script; run the script and embed the output
3. for each SDK: generate the bindings, the examples and the conformance
   suite; run the suite; embed the result
4. assemble the curricula (the teaching order)
5. package: the papers, the kits, the specs, the corpus, the harness —
   into the release (the polyglot testbench volume, alongside the proofs)
```

**The honest note**: the papers are generated *after* the system is
complete, because a paper's claims must be true at the moment of writing.
Generating them early would produce claims the code does not yet support —
the same error as a criterion written before its test. The academic
program is the *last* layer, the one that turns a working system into a
documented, reproducible, citable one.

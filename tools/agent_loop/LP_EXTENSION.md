# The paraconsistent extension criteria — adding postulates without breaking the pipeline

The four values are the foundation, not the ceiling. Paraconsistent
logic admits more values, more operators, more postulates — the lattice
extends without limit. The requirement is that the H100 can add them
**without breaking anything already proven**, and that the whole stays an
omni-tautology. This page is the exhaustive instruction.

## The invariant that must never break

```
LPRES_STATE_NEITHER = 0
LPRES_STATE_TRUE    = 1
LPRES_STATE_FALSE   = 2
LPRES_STATE_BOTH    = 3
```

These four numbers are load-bearing: the checks' answers (`true`,
`false`, `both`, `neither`), the `zxapp` grammar, the witness joins and
the wasm engine all name them. **The foundation's numeric values are
frozen.** Extensions add above them; they never renumber them.

## How to extend (the mechanism)

### 1. New values go above the foundation, with explicit ids

A new value is a new enum member with a value ≥ 4, named, documented, and
given a chain position. Example shape:

```c
typedef enum {
    LPRES_STATE_NEITHER = 0,
    LPRES_STATE_TRUE    = 1,
    LPRES_STATE_FALSE   = 2,
    LPRES_STATE_BOTH    = 3,
    /* extensions: new values, new ids, never renumbering the four */
    LPRES_STATE_INDETERMINATE = 4,   /* the fifth: evidence pending      */
    LPRES_STATE_TRANSITIONAL  = 5,   /* mid-transition, both stables     */
    LPRES_STATE_COUNT                 /* the count, for tables and loops  */
} lpres_state_t;
```

`LPRES_STATE_COUNT` is the single source of the arity: every table, every
sweep, every loop iterates to it, so adding a value extends every table
automatically and nothing is hard-coded to four.

### 2. The operators extend by a total rule, not by patching

The kernel's conjoin/disjoin are min/max on a **chain**. A new value must
be given a chain rank, and the operators then work on it with no code
change:

```
chain rank (low to high):  FALSE < NEITHER < BOTH < TRUE
a new value declares its rank;  conjoin = meet (min), disjoin = join (max)
```

The rule the kernel already obeys — *no mix of non-TRUE values may yield
TRUE* — must hold for the extensions too: a new value's rank is strictly
below TRUE, so it can never authorise an action alone. This is the
explosion guard, and it extends for free because it is a property of the
ordering, not of the value count.

### 3. The four-valued tables regenerate; the pipeline never restarts

`tools/agent_loop/mk_truth_tables.py` must iterate `LPRES_STATE_COUNT`,
not the literal four. When a value is added:

- the conjoin table becomes N×N, the negation N entries;
- the per-app sweeps widen to N values;
- the checks' grammar accepts the new value names.

Because the tables are *generated* and the checks are *data*, extending
the logic regenerates the tables and the pipeline resumes onto the new
checks — no code in the pipeline changes, and the passed apps stay passed
(the state file is keyed by app, not by arity).

## The three guards (what keeps the extension from breaking the build)

1. **The frozen-four guard.** A test asserts the four foundation values
   are exactly 0, 1, 2, 3. If an extension renumbers them, the test fails
   before anything else can.
2. **The never-authorise guard.** A test asserts that for every value
   v != TRUE, `lpres_conjoin(v, anything)` is never TRUE. This is the
   explosion guard, and it must hold as the arity grows.
3. **The total-table guard.** A test asserts the operator tables are
   total: every ordered pair of the N values has an entry. A new value
   with no entry is a hole in the tautology, and the test names it.

## The omni-tautology closure, extended

The five closing criteria from `ORPHAN_RESOLUTION.md` still hold, now over
N values:

1. **Downward** — every app's `needs` is provided.
2. **Upward** — every module's export is called.
3. **Sideways** — every `use` resolves.
4. **The paraconsistent closure** — where two witnesses disagree the join
   is a non-TRUE value (BOTH, or an extension), and a check expects it.
5. **The layer law** — `verify_layers.sh` passes.

An extension is complete when all five hold **and** the three guards pass
**and** the regenerated tables are total. That is the tautology at the new
arity.

## The paraconsistent vocabulary the extensions must respect

- **No explosion.** From a contradiction, nothing arbitrary follows. The
  guard is the ordering: only TRUE authorises, and no non-TRUE mix yields
  TRUE.
- **Locality.** A contradiction is contained in its value, not propagated.
  An extension value is a place to *hold* a paradox, not a way to leak it.
- **The chain, not Belnap.** The kernel's tables are min/max on its own
  chain (F < N < B < T), deliberately not Belnap's lattice. Extensions
  join the chain by rank; they do not import a foreign lattice.
- **The witness.** Two runtimes computing the same law disagree → the join
  is a non-TRUE value. More runtimes, more disagreement, still contained.

## The H100's loop for an extension

```
1. propose the new value: its id (>= 4), its name, its chain rank
2. add it to lpres_state_t and to the chain table
3. regenerate the truth tables (mk_truth_tables iterates COUNT)
4. run the three guards: frozen-four, never-authorise, total-table
5. run verify_layers + verify-all; the pipeline resumes onto the new checks
6. re-run the orphan scan and the gap audit at the new arity
7. commit only when every guard, every table and every check passes
```

## What "without breaking the pipeline" means, concretely

- **The apps already passed stay passed** — the state is per app.
- **The grammar only gains** — the new value names are accepted; the old
  answers remain valid.
- **The tables only grow** — N×N from 4×4, the old rows unchanged.
- **The wasm engine follows** — its lpres shim calls the kernel's
  operators, so it inherits the extension with no edit.
- **The tests only multiply** — the ratio rises with the arity, which is
  the point: more logic, more proof.

The foundation is four. The ceiling is none. The tautology holds at every
arity because the extension is a rank and a name, the operators are
min/max, and the guards prove the explosion can never come back.

## The current state (what the H100 must build first)

The criteria above describe the target. The tree today is **not** yet
extensible, and the H100's first three jobs are:

1. **`LPRES_STATE_COUNT` does not exist.** `lpres_state_t` ends at
   `LPRES_STATE_BOTH = 3` (`kernel/src/lpres/lpres.h:44`). Every loop and
   table that assumes four must iterate the count instead. Add the count
   member and switch the loops to it — that single change is what makes
   the arity a variable rather than a constant.
2. **The chain is a comment, not a table.** `lpres.c:76` documents
   `F < N < B < T` but the operators encode it in branches. Replace the
   branches with a rank lookup (`lpres_rank[LPRES_STATE_COUNT]`), so a new
   value's rank extends the operators with no code change.
3. **The tables are not yet generated from the count.**
   `mk_truth_tables.py` writes the 4x4 explicitly; make it iterate
   `LPRES_STATE_COUNT` so an added value regenerates N×N.

Until those three exist, the logic is four-valued and fixed. The criteria
are the instruction to make it extensible; the three guards
(frozen-four, never-authorise, total-table) are how the extension is
proven not to break what already passes. No extension should be proposed
until the mechanism is in place — a fifth value added to a hard-coded
four would break the tables, which is exactly what the guards prevent.

## The fifth is the gateway port (the resolution of the arity question)

The four values are the foundation — `NEITHER`, `TRUE`, `FALSE`, `BOTH`.
The **fifth** is not a fifth truth value: it is the **gateway port**, the
expansion aperture of the logic. Through it enter as many further phases
as the system can process — the sixth, seventh, eighth, and onward, up to
the cap the hardware and the schedule allow. The logic is
**forward-compatible**: a new phase is added at the port, never by
renumbering the four.

Concretely:

```c
typedef enum {
    LPRES_STATE_NEITHER = 0,   /* the foundation: four values, frozen   */
    LPRES_STATE_TRUE    = 1,
    LPRES_STATE_FALSE   = 2,
    LPRES_STATE_BOTH    = 3,
    LPRES_STATE_GATEWAY = 4,   /* the fifth: the expansion port         */
    /* phases enter through the port, above it, without renumbering    */
    LPRES_STATE_PHASE_6 = 5,
    LPRES_STATE_PHASE_7 = 6,
    /* ... as many as the system can process, capped by the schedule   */
    LPRES_STATE_COUNT          /* the arity: every table iterates to it  */
} lpres_state_t;
```

The gateway's properties:

1. **It is a port, not a truth.** It carries no authorisation: like every
   non-TRUE value it can never alone authorise an action (the
   never-authorise guard).
2. **Phases enter through it.** Each phase declares a chain rank above
   the gateway; the operators (min/max on the chain) extend with no code
   change, exactly as before.
3. **The cap is the system's capacity**, not the logic's: how many phases
   the schedule can process is an operational limit, and the port makes
   the logic ready for however many that becomes.
4. **Forward compatibility is structural.** Adding phase 6 through 13
   never touches the four, never breaks a passed app, and never requires
   a pipeline restart — the tables regenerate from `LPRES_STATE_COUNT`.

So the arity is: **four truths, one gateway, unbounded phases.** The
earlier note that flagged "four vs five" as a conflict is resolved: both
are right — four values, and the fifth is the door.

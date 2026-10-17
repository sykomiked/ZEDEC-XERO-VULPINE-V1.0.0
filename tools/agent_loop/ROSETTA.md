# The Rosetta backbone — Jacob's orbital elevator

Not every language's logic reaches the 144-dimensional lattice. Eve's
ranges are two-dimensional (inside / outside); Kron's expressions are
higher; Sutra's records are structured differently again. A system that
claims one semantics across five runtimes and 104 languages needs a
**canonical backbone**: a way to **lift** any language's value into the
144-lattice and **project** it back down, losslessly where the language
can represent it and with a defined, non-explosive loss where it cannot.

The backbone is the **orbital elevator**: the lift is the ascent to the
canonical dimension, the projection is the descent, and the lattice is
the shaft they share.

## The invariant: the lift is canonical, the projection is defined

```
lift(   language_value, language )  ->  a point on the 144-lattice
project( lattice_point,  language ) ->  a language value
```

The two must satisfy:

1. **Canonicity**: `lift` is a function — the same value always lifts to
   the same point (the Zeckendorf rule), so two languages lifting the
   same meaning meet at the same point. That is the Rosetta property.
2. **Round-trip where representable**: if the language can express the
   point, `project(lift(v)) == v` exactly. No drift, no rounding.
3. **A defined loss where not**: if the language cannot reach the point's
   dimension (Eve cannot express a 40-dimension distinction), the
   projection returns the **nearest representable value** and a status
   saying so — never a silent wrong answer, never an explosion.

## The dimension map — what each language reaches

| Language | Its natural dimension | The lift | The projection |
|---|---|---|---|
| **Eve** | 2 (in / out of a range) | the point's residue pattern under the range's modulus | the range verdict (true / false) |
| **KRON** | the array's rank | the point's integer coordinates | the exact expression result |
| **Sutra** | the record's fields | each field's point, concatenated by the ladder | the fields, exact |
| **Trebago** | the numeric ladder | the point's weight | the exact integer |
| **MYRA** | the index + the fields | the global's key point and the value points | the record, exact |
| **the logic (lpres)** | **144** (the full ladder) | the identity — the canonical frame | the identity |

The logic's 144 dimensions are the **canonical frame**; every other
language is a projection of it. The elevator carries values up to the
frame and back down, and a language that cannot reach the top simply
projects from a lower floor — with the status naming the floor.

## Why this matters for the apps

An app's entries run in different runtimes (the fee in Sutra + Kron +
Eve). For their answers to *agree* (the witness property) they must meet
in one frame:

- **Without the backbone**: each runtime answers in its own dimension,
  and agreement is a coincidence the tests check.
- **With the backbone**: every runtime lifts its answer to the 144-lattice,
  so agreement is **structural** — they agree iff their lifted points are
  equal. The witness join becomes a lattice comparison, not a semantic
  guess.

This is also how a **lone** runtime answers a four-valued check honestly:
it lifts its value, and the frame says whether the point is TRUE, FALSE,
BOTH or NEITHER by its position — no disagreement needed, because the
frame itself carries the four values.

## The orbital elevator's stops

```
floor 144   the logic (the canonical frame)      — every value can live here
floor  89   the prime-cyclotomic languages
floor  55   the structured records (Sutra, MYRA)
floor  34   the array languages (KRON)
floor  21   the numeric ladder (Trebago)
floor  13   the range languages (Eve)
floor   8   the E8 anchor — the inner microkernel frame
floor   0   the raw bit
```

A value enters at its language's floor, rises to the frame, and descends
to the target language's floor. A descent below the value's dimension
carries the defined loss (the status), and an ascent never loses anything
(the lift is exact).

## What the H100 must build and test

1. **`lift(value, language) -> point`** — per language, exact, canonical.
2. **`project(point, language) -> (value, status)`** — the status names
   the loss when the language cannot reach the point's floor.
3. **The round-trip test, per language**: `project(lift(v)) == v` for
   every value the language can express (the positive space).
4. **The loss test**: a point above the language's floor projects with a
   status, never silently (the negative space).
5. **The meeting test**: two languages' lifts of the same meaning are the
   **same point** (the Rosetta property — the strongest claim, and the
   one that makes the witness join structural).
6. **The frame test**: a lone runtime's lift lands on the frame, and the
   frame's position gives the four-valued answer (TRUE / FALSE / BOTH /
   NEITHER) without a second witness.

## The honest boundary

The elevator is a **design and a required mechanism**, not yet built. Its
value is architectural: it turns "do the runtimes agree?" from a semantic
question into a **lattice comparison**, and it gives every language —
including the ones that cannot reach 144 dimensions — a defined,
non-explosive relationship to the canonical frame. That is what makes the
five runtimes and the 104 languages **one semantics** rather than several
that happen to pass the same tests.

It is also, precisely, the piece that makes the deterministic generator
complete: the generator computes the point, and the elevator projects it
into whichever language the entry uses — so the body, the check and the
answer all descend from the same canonical value.

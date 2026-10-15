<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Language catalogue: 1950s to now

ZXV aims to work with every programming language people have used since the
1950s, and to give each one an AI twin. This page says what "work with"
means, which languages are where today, and the order the rest come in.

The list itself is `kernel/src/lingua/catalogue.tsv`, one language per line,
so it can grow without editing this page. `make -C kernel test-lingua`
validates it: every path it names exists, and the languages it marks as
running are exactly the ones the Lingua front-ends ship.

Thousands of programming languages have been published. The catalogue starts
with the 104 that carry the most code or the most influence, adds ZXV's own
three (LEXIS, TECTON and AXION) and the Lingua IR itself for 108, and takes
any other as one more line. A language is never marked as working before a test
proves it.

## What "compatible" means

| Tier | Meaning | Today |
|---|---|---|
| **run** | Source text compiles to the shared Lingua IR and runs on the checked integer runtime. Output is byte-identical to a reference implementation of the language on the subset Lingua accepts. A program outside the subset is refused with a line, column and reason, never run differently. | FORTRAN, BASIC, Forth, ML |
| **pillar** | The language's data and records run in one of the five ZXV pillar runtimes, which apps call directly. | COBOL (Sutra), FORTRAN numerics (Trebago), Ada (Eve), APL and K (KRON), MUMPS (MYRA) |
| **native** | One of ZXV's own three languages for coding by purpose and intent, built and checked in kernel/src/alexandria (docs/ALEXANDRIA.md). Each has a Lingua number and a twin; none compiles to the shared IR yet. | LEXIS (names), TECTON (composition), AXION (truth checks) |
| **ir** | The shared intermediate representation every Lingua front-end compiles to, and that the projections turn into C, Forth and x86-64. It is counted as a language because programs ship in it (`.zlir`, `logic zlir` in a manifest), but nobody is expected to write it by hand. | Lingua IR |
| **host** | Code in the language runs on its own toolchain and calls ZXV through the C ABI or an SDK. | C, C++, C#, F#, Go, Java, JavaScript, Pascal, PHP, Python, Ruby, Rust, Solidity, Swift, TypeScript, WebAssembly |
| **next1, next2, next3** | Queued for a Lingua front-end, in that order. | 18 more, plus a Lingua front-end for Pascal, listed below |
| **catalogue** | Recorded with its year and family. No claim of compatibility. | The rest |

A language can sit in more than one place (Pascal has a binding today and a
front-end queued). The catalogue lists the highest tier it has reached and
notes the rest.

### Why integer subsets

The kernel computes with exact integers only, so each front-end accepts the
integer and character part of its language. Anything that would lose
precision is refused: FORTRAN `REAL`, a BASIC division that leaves a
remainder outside `INT()`, a Forth value that leaves its cell. Arithmetic
that overflows the language's word size stops the program with a trap
instead of wrapping. Decimal and rational work goes to the pillars (Sutra
for COBOL pictures, Trebago for exact rationals).

## The AI twin

Every language gets a twin: one canonical spelling of it that an agent
writes and reads. The rules are the same for every language.

1. The twin is the language itself, not a new language. Any twin program is
   a valid program in the original and runs the same way under its usual
   compiler.
2. Lingua prints the twin of any program it accepts (`zxlang twin file`).
   Layout, case, spacing, statement form and labels become canonical, and
   comments are kept.
3. The twin compiles to the same IR as the original, instruction for
   instruction, and its digest matches.
4. The twin of a twin is itself, byte for byte.
5. In twin mode (`zxlang run --twin`) only canonical text is accepted, so an
   agent's output is checked to be exactly what the twin would print.

Twins are named after dragons. Each name is one word: what the language
was made for, joined to a kind of dragon (wyrm, drake, hatchling,
lindworm), in the spirit of the dragon icon series. Names come from that
plain vocabulary, never from a dragon in someone else's story or product,
and each gets a trademark check before release. The pillars keep their own
names.

| Language | Twin | Why the name |
|---|---|---|
| FORTRAN | Reckonwyrm | The eldest, made to reckon formulas; it feeds the Trebago pillar |
| BASIC | Hatchling | Made for beginners |
| Forth | Cairndrake | A cairn is a stack of stones, and Forth is a stack |
| COBOL | Sutra | The existing pillar |
| Ada | Eve | The existing pillar |
| APL, K | KRON | The existing pillar |
| MUMPS | MYRA | The existing pillar |

Proposed for the next front-ends: Pascal, Tutordrake (made for teaching);
ALGOL 60, Rootwyrm (the root of most later languages); LISP, Coilwyrm
(lists coil like a serpent); Scheme, Spiraldrake (recursion); Logo,
Kiteling (the hatchling that draws with a turtle).

The names work everywhere a language name does: `logic hatchling ...` in an
app manifest is the same as `logic basic ...`.

## In apps: precompiled building blocks

The languages are built into apps as blocks, so a person can build an app
without knowing any of them. A block is written once by someone who knows
the language, and every app uses it by name.

- **Using a block.** An app takes one with a single manifest line, such as
  `use number_classics 1.0.0`, and calls `number_classics.gcd` like any
  other entry. With `view auto` the app gets a form for it: number inputs
  and a Run button, and no code at all.
- **Writing a block.** A block names each entry's inputs and answer
  (`provides gcd num a b`) and binds it to programs in one or more
  languages (`logic fortran77 gcd ...`, `logic basic ...`, `logic forth ...`).
  Every Lingua language is accepted by its runtime name from
  `kernel/src/lingua/zl_langs.def` (`pascal`, `algol60`, `scheme`, `logo` ...),
  its catalogue name or its twin's name (`Tutordrake`); one whose front-end is
  not built yet refuses at load, by name. The arguments are the program's
  input (FORTRAN `READ *`, BASIC `INPUT`, Pascal `read`, the Forth stack) and
  the answer is every number it prints.
- **Precompiled.** `zxlang pack prog.f` writes the program already compiled,
  as Lingua IR text (`.zlir`). An app that ships it as `logic zlir ...`
  needs no compiler on the device. The text carries a digest and is
  verified before it runs, so a damaged or edited file is refused. Source
  entries are also compiled only once, when the app loads.
- **Checked twice.** An entry written in several languages runs all of them
  on a witnessed call. They must agree; if one traps or differs, the answer
  is held as BOTH, never picked silently.

`kernel/src/apps/blocks/number_classics.zxblock` is the first such block:
Euclid's greatest common divisor in FORTRAN 77, BASIC and Forth, plus the
FORTRAN precompiled. Its test shows the join working: for -2^31 the 32-bit
FORTRAN and BASIC trap on the overflow while 64-bit Forth answers, so the
result is held.

In the proposed mapping of the three app tiers, L1 base modules carry blocks
like this one, L2 modules combine them, and L3 apps are assembled from
both, each shipping the precompiled form with its source kept beside it for
provenance.

## By era

| Decade | run | pillar | host | queued | catalogue only |
|---|---|---|---|---|---|
| 1940s | | | | | 1 |
| 1950s | FORTRAN | COBOL | | LISP, JOVIAL, RPG | 6 |
| 1960s | BASIC | APL, MUMPS | | ALGOL 60, Logo, SNOBOL, PL/I, BCPL, B, JCL | 7 |
| 1970s | Forth, ML | | C, Pascal | Scheme, Prolog, Modula-2, AWK, REXX, SQL | 8 |
| 1980s | | Ada | C++ | | 17 |
| 1990s | | K | Python, Java, JavaScript, Ruby, PHP | Lua, Structured Text | 8 |
| 2000s | | | C#, F#, Go | | 4 |
| 2010s on | | | Rust, TypeScript, Swift, Solidity, WebAssembly | | 10 |

## Order of the queue

The order favours languages with the most running code that has no
maintained toolchain, then languages that teach, then the rest.

- **next1:** Pascal, ALGOL 60, LISP, Scheme, Logo. Block-structured and
  list languages exercise the parts of the IR the first three did not
  (nested scopes, recursion over lists).
- **next2:** PL/I, BCPL, B, Modula-2, SNOBOL, Prolog, Lua.
- **next3:** JOVIAL, RPG, IEC 61131-3 Structured Text, AWK, REXX, plus a
  parse tier for SQL and JCL, which ZXV reads rather than runs.

## How each front-end is proved

A language moves to **run** only when all of these pass in CI
(`make -C kernel test-lingua test-lingua-ref`):

- unit tests for every statement, every refusal and every trap;
- golden programs whose output matches the reference implementation
  (gfortran for FORTRAN, gforth for Forth; BASIC has no maintained integer
  reference, so its goldens were checked by hand and cross-checked against
  the same algorithm in the other two);
- 100 random programs per language, generated with known answers and run
  through both Lingua and the reference, byte for byte;
- a mutation fuzzer under AddressSanitizer and UndefinedBehaviorSanitizer
  that checks the twin rules above on every mutated program that compiles;
- the mutation harness (`tests/harness/mutator/targets.json`), which breaks
  the code on purpose and requires a test to notice;
- a freestanding build for x86_64 and aarch64 with no symbols outside the
  module.

The code, the IR and the commands are described in
`kernel/src/lingua/README.md`.

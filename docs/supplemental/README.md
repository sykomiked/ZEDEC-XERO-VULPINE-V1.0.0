<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Supplemental documentation

ZXV makes several design choices that are unusual for an operating system.
The reference documents in [`docs/`](../) describe **what** each subsystem
does. The pages here explain **why** it was built that way, what has really
been built and measured, and where the limits are.

Each page has two parts:

- **For the general reader:** plain language, no background needed.
- **For the academic reader:** precise terms, related prior work, and the
  evidence behind each claim.

## Start here

| Page | Read it if you want to know |
|---|---|
| [GLOSSARY.md](GLOSSARY.md) | What the project's own names and terms mean |
| [CLAIMS_AND_EVIDENCE.md](CLAIMS_AND_EVIDENCE.md) | Which claims are measured, which are tested, and which are still design intent |

## Why it is built this way

| Page | The unusual choice it explains |
|---|---|
| [WHY_GAME_ROM_ENGINES.md](WHY_GAME_ROM_ENGINES.md) | Why there are retro game console engines in the kernel |
| [INTEGER_ONLY_KERNEL.md](INTEGER_ONLY_KERNEL.md) | Why the kernel bans floating-point arithmetic |
| [MULTI_VALUED_LOGIC.md](MULTI_VALUED_LOGIC.md) | Why the system reasons with more than true and false |
| [TOKEN_BUDGETS_AND_EMOTIONAL_AXIS.md](TOKEN_BUDGETS_AND_EMOTIONAL_AXIS.md) | How the AI swarm shares a fixed token budget, and what the "emotional" axis is |
| [ECONOMY_WHAT_IT_IS_AND_IS_NOT.md](ECONOMY_WHAT_IT_IS_AND_IS_NOT.md) | What the built-in economy does, and what it legally is not |
| [HARMONIC_WIRE_EXPLAINED.md](HARMONIC_WIRE_EXPLAINED.md) | Why messages travel in numbered "harmonic" lanes |
| [LANGUAGE_PILLARS.md](LANGUAGE_PILLARS.md) | Why COBOL, Fortran, Ada, APL and MUMPS are carried forward |
| [HARDWARE_EMULATION_AND_LICENCE.md](HARDWARE_EMULATION_AND_LICENCE.md) | Why hardware is treated as software, and why hardware has its own licence |

## Ground rules for these pages

- Every claim points to code, a test or a report in this repository.
- Numbers are copied from measured results; nothing is estimated here.
- Anything not yet built or measured is labelled as design intent.
- Rigorous papers for each academic field are in
  [`docs/academic/`](../academic/README.md).
- The project's broader values are in
  [`DESIGN_PHILOSOPHY.md`](../../DESIGN_PHILOSOPHY.md).

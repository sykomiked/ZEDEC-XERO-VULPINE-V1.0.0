<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# The app model: a basket of languages

Every coding language has strengths and weaknesses. A ZXV app is not
written in one of them. It is built from a basket: each feature is written
in the language that suits it, and the paraconsistent logic ties the
answers together.

## The three layers

| Layer | Language | Why |
|---|---|---|
| Toolkit, renderer, input, present (`kernel/src/zui`) | C | The same code runs hosted and on bare metal, with no runtime under it. |
| The app's screen (`kernel/src/zui/views/<app>.c`) and model (`kernel/src/apps/<app>_model.c`) | C | Talks to zxvd and draws; holds AI output until the person accepts it. |
| The app's logic | Whatever suits each feature | Bound by name in the app's manifest (`kernel/src/apps/<app>.zxapp`). |

## Which language for which feature

| Feature | Language | Strength |
|---|---|---|
| Money, ledgers, settlement | Sutra (COBOL pillar) | Fixed-point decimal records, decades of banking practice. |
| Limits, safety, policy | Eve (Ada pillar) | Every value has a declared range; anything outside it is refused. |
| Numbers over arrays, charts, shares | KRON (APL/K pillar) | Whole-array operations, exact rationals, short and checkable. |
| Heavy exact numeric kernels | Trebago (Fortran pillar) | Loops over large arrays, exact integer arithmetic. |
| Records, indexes, search | MYRA (MUMPS pillar) | Hierarchical keyed storage. |
| Anything with a libzxv SDK | C++, Rust, Go, Swift, Kotlin, Java, .NET, Python, TypeScript/JavaScript, Dart, Ruby, PHP | Their own libraries and people, on the same libzxv ABI and exact values. |
| Anything else | WebAssembly (Z-Wasm) | Any language that compiles to it; sandboxed, fuel-metered, integer-only. |

Each `logic` line in a manifest names its own language, so one manifest
mixes several (lines from the `swarm_shares` block that Murmuration uses;
see "Building apps from blocks" below):

```
logic  kron cycle_share   apps/blocks/swarm_shares/cycle_share.kron
logic  kron busiest       apps/blocks/swarm_shares/busiest.kron
logic  eve  within_limits apps/blocks/swarm_shares/limits.eve
```

A language whose runtime is not in this build leaves its entry unloaded,
and the app's C model computes that feature instead, so every build runs.
Runtimes in the tree today: KRON (`zxapp_kron.c`), Eve (`zxapp_eve.c`) and
WebAssembly (`zxapp_wasm.c`). Sutra, Trebago, MYRA and the SDK languages
register the same way (`zxapp_host_register`) as they are hooked in.

## The paraconsistent join

Every answer that crosses a language boundary carries a four-valued tag
(libzxv's `ZXV_LP_*`): TRUE, FALSE, BOTH or NEITHER.

A feature that matters, such as money or safety, is written twice, in two
languages, under one entry name:

```
logic  sutra fee apps/wallet/fee.sutra
logic  kron  fee apps/wallet/fee.kron
```

`zxapp_call_witnessed` runs every implementation and joins them, like the
terminal's `<?>` operator:

| The implementations | Join | What the app does |
|---|---|---|
| All answer the same | TRUE | Uses the answer. |
| They answer differently, or one answers and one refuses | BOTH | Holds the result (`ZXAPP_E_CONFLICT`), says so in words, and escalates. It never picks one silently. |
| Only one could run | NEITHER | Uses the answer, marked unwitnessed. |
| All refuse | (a refusal) | Shows the refusal. |

A screen can also witness a language with its C model
(`zxapp_witness`). Murmuration does: C always computes the shares and the
limits, KRON and Eve compute them too, and the status line reads
"shares: KRON = C, limits: Eve = C". If they ever disagree, the rows show
"held" and the screen says which languages disagree, instead of showing
either answer.

## Rules

- **Exact values only.** Arguments and results are integers or exact
  rationals, never floats (pillar rule R11). Z-Wasm refuses floating-point
  code at load.
- **Nothing is applied without the person.** AI output is held until
  accepted, and a BOTH is held until someone resolves it.
- **No hidden fallback.** When C stands in for a language, the screen says
  so.

## Building apps from blocks

Apps are not written from scratch. They are put together from components,
called blocks, the way a website is put together from plugins. A block is
a small, versioned piece of logic: one job, written in the languages that
suit it, with a promise of what goes in and what comes out. It is made
once and used by every app that needs it, from a system monitor to a movie
editor.

A block has its own manifest, `apps/blocks/<id>.zxblock`:

```
zxblock 1
id       swarm_shares
name     Swarm shares
version  1.0.0
provides cycle_share num t
logic    kron cycle_share apps/blocks/swarm_shares/cycle_share.kron
provides busiest num t
logic    kron busiest apps/blocks/swarm_shares/busiest.kron
provides within_limits truth share intensity
logic    eve  within_limits apps/blocks/swarm_shares/limits.eve
```

An app names the blocks it is made of:

```
zxapp 1
id     murmuration
name   Murmuration
view   zui/views/murmuration.c
model  apps/murmuration_model.c
use    swarm_shares 1
```

and calls their entries by `<block>.<entry>`
(`swarm_shares.cycle_share`). Two apps made of the same blocks in a
different arrangement are two different apps; a new app is usually a new
screen over blocks that already exist, plus one or two new ones.

| Line | Meaning |
|---|---|
| `version 1.2.0` | The block's version. A change that breaks a contract is a new major version. |
| `provides <entry> num <inputs>` | The entry takes these named inputs, in this order, and answers numbers. |
| `provides <entry> truth <inputs>` | The same, answering TRUE, FALSE, BOTH or NEITHER. |
| `use <block> 1` | The newest version 1.x of that block that is registered. |
| `use <block> 1.2` | The newest 1.x that is at least 1.2. |
| `use <block> =1.2.0` | Exactly 1.2.0. |

The rules:

- **Every entry has a contract.** A block is refused when it provides an
  entry it does not implement, or implements one it does not provide.
- **Contracts are checked on every call.** Wrong inputs are refused before
  any language runs. An answer of the wrong kind (a number where a truth
  was promised) is refused, and when the entry is written in two languages
  that refusal counts against it in the paraconsistent join, so it is held
  as BOTH, never used.
- **Blocks do not use blocks.** The app is the only place blocks are put
  together, so there is never a hidden chain of versions to untangle.
- **The basket still applies inside a block.** A block's entries can be in
  different languages, and a critical one can be written twice.

For developers this means: write one block well, with its contract and
its tests, and every app on the platform can use it. Registering a block
(`zxapp_registry_add`) and composing an app from the registry
(`zxapp_compose`) are two calls.

See `kernel/src/apps/zxapp.h` (A1 to A5, B1 to B4) for the exact contract.

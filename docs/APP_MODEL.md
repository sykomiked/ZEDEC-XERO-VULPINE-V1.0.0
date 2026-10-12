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

The manifest names each feature's language on its own line:

```
zxapp 1
id     murmuration
name   Murmuration
view   zui/views/murmuration.c
model  apps/murmuration_model.c
logic  kron cycle_share apps/murmuration/cycle_share.kron
logic  kron busiest     apps/murmuration/busiest.kron
logic  eve  within_limits apps/murmuration/limits.eve
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

See `kernel/src/apps/zxapp.h` (A1 to A5) for the exact contract.

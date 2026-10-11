<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Troubleshooting

**Who this is for:** anyone stuck.
**What you will be able to do:** recognise the common problems from their
messages and fix them.

## Loading the library

**"libzxv not found"** (Python: `ZxvLibraryError: libzxv not found. Pass a
path to zxv.load_library(), set ZXV_LIBRARY_PATH, or install a wheel that
bundles it.`)

Build it (`make -C sdk/libzxv all`) and point the SDK at it:

```sh
export ZXV_LIBRARY_PATH=$PWD/sdk/libzxv/build     # the folder or the file
```

Every SDK searches in the same order: a path passed in code, then
`ZXV_LIBRARY_PATH`, then a copy bundled with the package, then the system
loader path. Java also takes `-Dzxv.library=/path/to/libzxv.so`. (A
hand-written `ctypes` binding, like the one in
[Writing a binding](writing-a-binding.md), needs the path to the file
itself.)

**"need libzxv 1.3" / version refused.** Your SDK is newer than the library
it found. Rebuild libzxv from the same checkout, and check that an old copy
is not earlier on the path.

**Java prints `WARNING: A restricted method ... has been called`.** The SDK
uses the Foreign Function & Memory API. Allow it explicitly:
`java --enable-native-access=ALL-UNNAMED ...` (or name your module).

## Error codes and what to do

| Code | Class | Usual cause | Fix |
|---|---|---|---|
| `ZXV_E_CAP` | REFUSED | the capability lacks the bit, was revoked, belongs to another context, or is attested other than `TRUE` | pass the right capability; see [Capabilities](capabilities.md) |
| `ZXV_E_FUNDS` | REFUSED | a holder account would go below zero | check the balance first, or post from an issuer |
| `ZXV_E_BUDGET` | REFUSED | the budget is used up | wait (`consume_async`), release, or raise the limit |
| `ZXV_E_STATE` | REFUSED | a hexagram phase jump, or an attempt to un-forbid a tag | step one phase at a time; start a new item |
| `ZXV_E_CAPACITY` | REFUSED | a fixed-size store is full (for example the quarantine log) | for the quarantine log: halt the context, as required |
| `ZXV_E_NOTIMPL` | REFUSED | not in this build: LoRA, netting, card issuing | check `zxv_features` before calling |
| `ZXV_E_FLOAT` | USAGE | a float reached an exact API, or a NaN/infinity was converted | use text, integers or `Rational`; see [Exact numbers](exact-numbers.md) |
| `ZXV_E_FORMAT` | USAGE | text such as `"1e-3"` or `"12,50"` | write plain decimals (`"0.001"`) or fractions (`"1/3"`) |
| `ZXV_E_RANGE` | USAGE | amount `0`, negative or above `AMOUNT_MAX`, or 64-bit overflow | split the amount, or check your arithmetic |
| `ZXV_E_TOO_LONG` | USAGE | text too long, such as a reference over 35 bytes | shorten it |
| `ZXV_E_CURRENCY` | USAGE | the currency is not enabled, the accounts differ, or a private unit was used in an ISO 20022 message | enable it; keep private units in the ledger |
| `ZXV_E_DUPLICATE` | USAGE | a reference was used twice | make references unique |
| `ZXV_E_RMAG_INVALID` | USAGE | a negative budget amount | fix the caller; do not retry |
| `ZXV_E_ARG` | USAGE | a bad argument: zero denominator, wire tag 200 or more, a non-forbidden tag sent to the quarantine log | fix the caller |
| `ZXV_E_BUFFER` | USAGE | the buffer is too small (C) | this is step one of the two-call pattern: allocate `*out_len + 1` and call again |
| `ZXV_E_POISONED` | FATAL | the context was halted earlier | stop; start a new context; find out why it halted |
| `ZXV_E_INVARIANT` | FATAL | an integrity check failed (ledger check, quarantine chain) | stop and investigate; never retry |

Always branch on the code or the class. The message text can change.

## Plugins

| Message | Cause | Fix |
|---|---|---|
| `REJECTED FLOAT_OP` or `FLOAT_TYPE` | the plugin uses floats | use integers and the `zxv.rat_*` imports |
| `REJECTED UNSUPPORTED` (bulk memory) | the compiler emitted bulk-memory instructions | compile with `-mno-bulk-memory` |
| `REJECTED MEMORY_NO_MAX` or `MEMORY_LIMIT` | memory can grow without limit, or above 1 MiB | link with `-Wl,--max-memory=131072` (or another size up to 1 MiB) |
| `REJECTED IMPORT_DENIED` | the plugin imports something the host did not grant | grant it on the host (`zw_host_grant`) if you trust it, or remove the import |
| `ZW_E_CHECK` | a contract check failed; `zc_wasm_last_violation` says which | fix the input or the plugin; the violation is in the evidence log |
| `ZW_E_FUEL` | the plugin ran out of fuel | raise the budget, or fix the loop |
| `TI_E_SPAWN` with "exit status 127" | Tri-Isolate could not start the worker: usually a wrong path | pass the full path to `sdk/kits/tri-isolate/build/ti-worker` |

See [Writing a plugin](writing-a-plugin.md).

## macOS

- **Prefer `ZXV_LIBRARY_PATH` over `DYLD_LIBRARY_PATH`.** macOS System
  Integrity Protection removes `DYLD_*` variables when it starts protected
  programs such as `/bin/sh` or the system Python, so they often silently
  do nothing.
- **C++ on Xcode 15 and 16.** Apple's libc++ has no `std::jthread` or
  `<stop_token>`. `zxv.hpp` uses its own fallback there automatically. Add
  `-DZXV_FORCE_STOP_TOKEN_FALLBACK` to test the fallback on other platforms.
- **Apple arm64 and small integer arguments.** If you write your own
  binding and a valid `uint8_t` value (such as tag 199) is refused, you are
  probably passing a signed byte. See
  [Writing a binding](writing-a-binding.md#rules-that-bite-binding-authors).
- **Tri-Isolate** works on macOS, but without seccomp and without a worker
  memory limit (`RLIMIT_AS`).

## Windows

The repository builds `libzxv` for Linux and macOS only. There is no
`zxv.dll` yet, and Tri-Isolate does not support Windows. Under WSL
(Windows Subsystem for Linux) you can follow the Linux instructions; WSL is
not tested in CI.

## TypeScript in the browser

The WebAssembly backend is single-threaded and fully deterministic. It has
no file system and no clock of its own: the phase clock moves only when you
call `advance(n)` or start the real-time driver (`startRealtime`), which
uses the host's timers. Long work blocks the thread it runs on, so run it
in a Web Worker.

## Still stuck?

- Check the language README's "Honest limits" section.
- Read [the security model](../SECURITY.md) if the question is "does it
  protect against X?".
- Open an issue with the status name, the class, your platform, and the
  smallest program that shows the problem. For a security problem, use a
  private advisory instead (see [SECURITY](../SECURITY.md#reporting-a-vulnerability)).

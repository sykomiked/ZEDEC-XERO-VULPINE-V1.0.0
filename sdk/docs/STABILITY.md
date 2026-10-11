<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# libzxv ABI stability policy

Every ZXV SDK, ours or a community one, binds to one C library:
[`libzxv`](../libzxv/). This page is the promise we make to the people who
maintain those bindings: **code that works against libzxv 1.x keeps working
against every later 1.x, without being rebuilt.**

The promise is enforced by CI, not just written down (see
[How it is enforced](#how-it-is-enforced)).

## What never changes within a major version

Once a function, type or constant ships in a 1.x release, all of the
following stay exactly as released for the rest of 1.x:

| Frozen | Example |
|---|---|
| The function exists and is exported | `zxv_ledger_post_cap` |
| Its return type, parameter types and parameter order | `int32_t zxv_features(uint32_t *)` |
| The meaning of each parameter and of each status it can return | `ZXV_E_FUNDS` still means "would go below zero" |
| Every status code, capability bit, feature bit and enum value | `ZXV_E_FUNDS == -6`, `ZXV_CAP_EVM` |
| The size of every enum | 4 bytes |
| Callback signatures | `zxv_token_fn`, `zxv_rmag_listener` |
| Ownership rules (who frees what, which destroy function) | [ABI.md](ABI.md#the-rules) R1-R11 |

Nothing is ever removed or reordered. A function we regret is marked
**deprecated** in the header and docs, keeps working, and gets a better
sibling next to it.

## How the ABI is shaped so it can stay frozen

- **Opaque handles only.** Callers hold `zxv_ctx *`, `zxv_cap *`,
  `zxv_model *` and the other handles, and never see what is inside. We can
  rewrite the kernel behind a handle without touching any binding. There are
  no public structs in [`zxv_api.h`](../libzxv/include/zxv_api.h).
- **Flat C functions over fixed-width primitives.** Parameters are
  `int32_t`, `int64_t`, `uint64_t`, `size_t`, byte buffers with explicit
  lengths, and handles. An exact rational crosses the boundary as two
  `int64_t` (numerator, denominator), never as a struct and never as a float.
- **Every function returns a status** (`int32_t`, `ZXV_OK` or a negative
  `ZXV_E_*`), and results come back through out-pointers. A new error case
  uses a new code; callers that don't know it still classify it with
  `zxv_status_class`, and unknown negative codes are `SYSTEM` class.
- **Feature discovery at run time.** `zxv_version` and `zxv_features` let a
  binding detect what the library it loaded offers instead of assuming it.

## How the ABI grows

| Change | Allowed in | Version bump |
|---|---|---|
| Bug fix, tighter validation of input that was already invalid, docs | any release | patch (1.1.0 → 1.1.1) |
| New function, constant, status code, capability or feature bit | any release | minor (1.1 → 1.2) |
| New behaviour for an existing function | only behind a new function or an opt-in flag | minor |
| Removing, renaming or re-typing anything; changing a value or a meaning | never in 1.x | a new major is a new library, installed side by side |

When a safety fix has to refuse something an old function used to accept
(for example, a call that should have demanded a capability), the old
function keeps its signature. The fix either refuses input that was already
documented as invalid (patch), or ships as a new `*_cap` function with the
old one marked deprecated (minor). Fail-closed behaviour on a poisoned
context applies to every function, old and new; it is part of the 1.0
contract, not a change to it.

ABI 1.2 did exactly this: 15 new `*_cap` functions close the calls that took
no capability, and the old forms are deprecated but unchanged. The 64-state
hexagram tag will arrive the same way, as new `zxv_hex_*` functions in a later
minor, while the four-valued `ZXV_LP_*` values beside it do not move.

## Before 1.x: what counts as released

The baseline files in [`sdk/libzxv/abi/`](../libzxv/abi/) are the released
ABIs. The first one, `libzxv-1.1.1.json`, records the 158 functions and 179
constants on the SDK branch today. Each published release adds its own
baseline file, and CI checks the header against **every** baseline, so a
change that breaks any earlier release fails the build.

## How it is enforced

[`tools/abi_check.py`](../libzxv/tools/abi_check.py) runs in the `libzxv`
CI job on Linux (x86-64 and arm64, gcc and clang) and macOS. It:

1. reads every `ZXV_API` prototype and callback type from the header,
   ignoring parameter names (renaming a parameter is not a break);
2. compiles a probe that prints the value of every `ZXV_*` constant and enum
   member and the size of every enum;
3. compares all of it with each baseline, failing on anything removed or
   changed;
4. checks that every declared function is really exported by the built
   library;
5. requires a minor-version bump whenever functions are added.

To record a new release's baseline:

```sh
python3 sdk/libzxv/tools/abi_check.py snapshot sdk/libzxv/include/zxv_api.h \
    sdk/libzxv/abi/libzxv-1.2.0.json
```

## What this does not cover

- **Behaviour you can only see by misusing the API.** Passing invalid input
  (a NULL handle, a buffer smaller than its stated length) is not part of
  the promise.
- **Error detail text.** `zxv_last_error` messages are for humans and may
  improve in any release. Match on status codes, not on text.
- **Builds without a feature.** A library built with `ZXV_NO_PQC` or
  `ZXV_NO_TENSOR` keeps every symbol, but those calls return
  `ZXV_E_NOTIMPL`; check `zxv_features`.
- **The kernel's internal headers.** Only `zxv_api.h` is public. Anything
  under `kernel/` can change at any time.

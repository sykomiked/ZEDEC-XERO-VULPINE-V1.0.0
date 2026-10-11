<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# ZXV developer guides

Task-focused guides for the ZXV SDKs. Each one starts by saying who it is
for and what you will be able to do at the end. The examples were run
against the real libraries; where a guide shows output, it is the output we
got.

The guides explain and show. The reference material is elsewhere: each
language's README, [the SDK design rules](../SDK_DESIGN.md), [the C
ABI](../ABI.md), and [the security model](../SECURITY.md).

## Start here

If you are new, read these five in order. Together they take about an hour.

1. [Getting started](getting-started.md): your first ledger, in Java, .NET,
   Python, TypeScript or C.
2. [Errors and fail-closed](errors-and-fail-closed.md): what each kind of
   error means and how a halted system stays halted.
3. [Capabilities](capabilities.md): give each part of your program only the
   rights it needs.
4. [Exact numbers](exact-numbers.md): why floats are refused, and what to
   use instead.
5. [Four-valued logic](four-valued-logic.md): true, false, both and neither,
   and the recommended action for each state.

Then pick the guides for what you are building.

## By goal

### Learn the core ideas

| Guide | For | You will be able to |
|---|---|---|
| [Getting started](getting-started.md) | anyone new to ZXV | build libzxv and run a small ledger program in one of five languages |
| [Exact numbers](exact-numbers.md) | anyone handling money or measurements | do exact arithmetic and convert a float on purpose when you must |
| [Four-valued logic](four-valued-logic.md) | developers acting on uncertain evidence | combine `Truth` values, map a hexagram tag to an action, keep a quarantine log |
| [Capabilities](capabilities.md) | anyone structuring or reviewing an application | make, pass and revoke narrow capabilities, and know their limits |
| [Errors and fail-closed](errors-and-fail-closed.md) | every developer | catch the right errors in your language and make a halt final |

### Move value

| Guide | For | You will be able to |
|---|---|---|
| [Ledger and currencies](ledger-and-currencies.md) | wallet, treasury and back-office developers | open accounts, post transfers, read the three balances, check the books, write an ISO 20022 message |
| [Web services with Spring Boot and ASP.NET Core](frameworks.md) | enterprise teams building HTTP APIs | configure named capabilities, inject them, and map errors and halts to HTTP |

### Control resources and time

| Guide | For | You will be able to |
|---|---|---|
| [Budgets and time](budgets-and-time.md) | anyone limiting usage or scheduling work | spend from exact budgets, wait for refills, and drive the 27,720-tick phase clock |

### Protect data and talk to other systems

| Guide | For | You will be able to |
|---|---|---|
| [Secrets and post-quantum crypto](secrets-and-crypto.md) | developers who sign or agree keys | use ML-DSA-87 and ML-KEM-1024 and keep secret keys wiped |
| [Wire frames (UBH-168)](wire-frames.md) | network, gateway and firmware developers | build and parse 21-byte frames, the same in every language |
| [Calling C and inbound data](calling-c-and-inbound-data.md) | developers wrapping C code or ingesting JSON, MQTT or FIX | generate exact, gated C wrappers and label inbound records |

### Run other people's code

| Guide | For | You will be able to |
|---|---|---|
| [Writing a plugin](writing-a-plugin.md) | platform teams and plugin authors | build a WebAssembly plugin, check it in CI, and run it with fuel, contracts and host approval, in-process or in a worker process |

### Extend and verify ZXV

| Guide | For | You will be able to |
|---|---|---|
| [Writing a binding](writing-a-binding.md) | people bringing ZXV to a new language or toolchain | bind libzxv safely through a C FFI and avoid the common traps |
| [Conformance testing](conformance-testing.md) | SDK maintainers and reviewers | run every SDK's tests and write your own test against the shared answer key |

### When something goes wrong

| Guide | For | You will be able to |
|---|---|---|
| [Troubleshooting](troubleshooting.md) | anyone stuck | fix loading problems, read error codes, and work around platform differences |

## Honest limits, in one place

These apply to everything in the guides:

- **Not certified.** Nothing here is certified to any safety, medical,
  automotive, aviation or cryptographic standard, and the cryptography is
  not FIPS 140-3 validated.
- **Capabilities are least privilege, not a sandbox.** Inside one process,
  other code can bypass them. Tri-Isolate adds a process boundary; it is not
  a virtual machine.
- **z-audit runs bounded checks, not proofs.** A pass means nothing was
  found within its limits.
- **Advisory only for safety-critical uses.** In clinical, power-grid,
  industrial-control or flight settings, ZXV output is advice for a person
  to approve. It must sit behind the certified safety systems those domains
  already require, and it must never act on its own.
- **No payment network connection.** The ledger and message formats are
  designed to support integration with networks such as PAPSS and SWIFT.
  There is no connection, partnership or compliance claim.

The full list is in [the security model](../SECURITY.md).

## Checking the links

Every relative link and `#anchor` in `sdk/docs/` can be checked with a small
script (Python standard library only):

```sh
python3 sdk/docs/guides/check_links.py
```

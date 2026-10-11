<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# z-audit: check a plugin before you publish it

z-audit reads a Z-Wasm plugin (`.wasm`) and its z-contract (`.zc`) and
tells you, before anyone loads the plugin, whether:

- the module passes the same validator a Z-Wasm host runs at load time;
- it uses any float or SIMD type or instruction (all of them are listed,
  not just the first);
- every import is on the allow-list, with the right signature;
- every exported function has a contract declaration that can be bound to
  it, and the declared bounds are well formed;
- for simple export bodies, the result can leave its declared range, or a
  constant proposal breaks its `proposes` type.

It prints a report for people and, on request, a JSON report for CI. The
exit status is 0 when the audit passes, 1 when it fails, 2 on a usage or
I/O error.

```sh
z-audit --grant all --contract fees.zc fees_plugin.wasm
z-audit --grant all --contract fees.zc --json report.json --quiet fees_plugin.wasm
```

> **Bounded checks, not a proof.** The last check is an interval analysis
> over straight-line export bodies: no branches, no loops, a small set of
> i32/i64 instructions. Anything outside that is skipped and the report
> says so. A passing report does **not** prove that no Forbidden state is
> reachable, that the plugin is correct, or that it is safe to run. It
> means these specific checks found nothing. The runtime guards still run
> on every call: Z-Wasm's validator and fuel, and z-contract's argument,
> result and proposal checks. See [What z-audit does not do](#what-z-audit-does-not-do).

Kit API version 0.1 (`include/zaudit.h`). It calls libzxv ABI 1.3,
Z-Wasm kit API 0.2 (plus its internal decoder, `../z-wasm/src/zw_wasm.h`,
built from the same tree) and z-contract kit API 0.2.

## Quickstart

```sh
make -C ../../libzxv all     # once
make test                    # needs zig for the example plugins (pip install ziglang)
make demo
```

`ZAU_SKIP_EXAMPLES=1 make test` runs without zig; it skips the CLI runs on
the example plugins.

`make demo` audits three plugins. The first, Z-Wasm's rational example
against [`examples/rational.zc`](examples/rational.zc), passes with one
warning:

```text
z-audit 0.1.0: build/zwasm/examples/rational_plugin.wasm against examples/rational.zc
  module   3584 bytes, sha3-256 a39d2b65...
  contract sha3-256 37314a55... (canonical text)

PASS  module.validate    the Z-Wasm profile validator accepts the module (3584 bytes)
PASS  module.float-simd  no float or SIMD type or instruction
PASS  module.imports     5 import(s), 5 allowed
PASS  contract.parse     4 function declaration(s)
WARN  exports.declared   3 exported function(s), all declared
      warning NOT_EXPORTED           the contract declares average_percent but the module does not export it
PASS  contract.bounds    12 declared slot(s) well formed
PASS  exports.analysis   3 export(s) looked at; no problem found within the bounds
      note    NOT_ANALYZED           checksum: not analysed (a block, loop, if or branch (opcode 0x02): only straight-line bodies)

RESULT: PASS (0 error(s), 1 warning(s))
Bounded static checks. A pass does not prove that no Forbidden state is reachable ...
```

(Some notes are left out above.) The float example fails: the validator
refuses it and `module.float-simd` lists all six float instructions. The
third, [`examples/plugins/audit_plugin.c`](examples/plugins/audit_plugin.c)
against [`examples/audit_plugin.zc`](examples/audit_plugin.zc), shows what
the analysis can say:

```text
FAIL  exports.analysis   1 error(s), 1 warning(s)
      note    RESULT_IN_RANGE        level: every normal return gives a result in [1..2], inside the declared range
      warning RESULT_MAY_LEAVE       bucket: the result can be anywhere in [1..2], which is not inside the declared range
      error   PROPOSAL_OUT_OF_BOUNDS half proposes the constant 3/2, which breaks its proposes type ...
      note    NOT_ANALYZED           hash: not analysed (a block, loop, if or branch (opcode 0x02): only straight-line bodies)

RESULT: FAIL (1 error(s), 1 warning(s))
```

## Using it in CI

Run it on the built plugin, keep the JSON report as a build artifact, and
let the exit status gate the publish step:

```yaml
- run: z-audit --grant all --strict --contract plugin.zc --json audit.json plugin.wasm
- uses: actions/upload-artifact@v4
  if: always()
  with: { name: z-audit-report, path: audit.json }
```

`--strict` makes warnings fail too. Give the same grants and extra imports
your host gives, so the audit sees the same allow-list the host enforces.

## Command line

```text
z-audit [--grant LIST] [--allow MOD.NAME=SIG]... [--pages N] [--bulk]
        [--strict] [--no-analysis] [--json FILE|-] [--quiet]
        (--contract FILE.zc | --no-contract) PLUGIN.wasm
```

| Option | Meaning |
|---|---|
| `--contract FILE.zc` | the plugin's z-contract (required unless `--no-contract`) |
| `--no-contract` | audit without one; every exported function then fails `exports.declared` |
| `--grant LIST` | the host's Z-Wasm grants: `all`, or a comma list of `rational`, `logic`, `propose` |
| `--allow MOD.NAME=SIG` | allow one more import, signature in wasm3 form, e.g. `env.tick=i(I)` |
| `--pages N` | the memory page limit (default 16, as Z-Wasm) |
| `--bulk` | allow bulk-memory instructions |
| `--strict` | warnings fail the audit |
| `--no-analysis` | skip `exports.analysis` |
| `--json FILE` | write the JSON report to FILE (`-` for stdout) |
| `--quiet` | no text report |

Exit status: **0** pass, **1** fail, **2** usage error, unreadable file, or
an audit that could not run.

## The checks

They run in this order. Each ends as PASS, WARN, FAIL or SKIP. The audit
fails if any finding is an error (or, with `--strict`, a warning).

| Check | What it does | Findings |
|---|---|---|
| `module.validate` | `zw_validate` with the grants and allow-list: the same filter a Z-Wasm host applies at load | the validator's own code (`FLOAT_OP`, `IMPORT`, ...), error |
| `module.float-simd` | walks every section and body and lists every float or SIMD value type and instruction | `FLOAT_TYPE`, `FLOAT_OP`, `SIMD` error; `MALFORMED` when the walk cannot go on |
| `module.imports` | every import: function kind, module, name and signature against the allow-list | `IMPORT_KIND`, `IMPORT_TYPE`, `IMPORT_DENIED`, `IMPORT_SIG` error; `IMPORT_ALLOWED` note |
| `contract.parse` | the contract parses (z-contract's parser) | `NO_CONTRACT`, `SYNTAX` error |
| `exports.declared` | every exported function has a declaration that binds to it under the rules `zc_wasm_attach` applies at run time (a `rat` parameter takes two arguments; no `bytes`; result present or absent on both sides) | `UNDECLARED`, `ARITY`, `MAPPING`, `RESULT_MISSING`, `RESULT_UNDECLARED`, `EXPORT_INDEX` error; `NOT_EXPORTED` warning; `DECLARED`, `NOT_A_FUNCTION` note |
| `contract.bounds` | every declared slot of a bound export is well formed: some rational meets each `rat` range and its `den` rule; `int` ranges fit what an i32 parameter or result can carry | `EMPTY_BOUNDS` error; `I32_RANGE` error or warning; `BOUNDS_UNDETERMINED` note |
| `exports.analysis` | the bounded analysis below; skipped when the validator refused the module | see below |

`contract.bounds` decides `den|N` exactly. For `den<=N` it searches
denominators up to 4096; past that it reports `BOUNDS_UNDETERMINED` rather
than guess.

### The bounded analysis

For each exported function with a declaration, z-audit interprets the body
once over **intervals**: each i32/i64 value is a range `[lo..hi]`.
Parameters start at their declared ranges; constants are exact; `add`,
`sub` and `mul` widen to "any value" when they could wrap; comparisons,
`eqz` and `select` are handled; locals, loads and stores are tracked (a
load gives "any value"); calls to imports give "any value", except
`zxv.propose`, whose calls are counted and whose constant arguments are
checked.

| Finding | Severity | Meaning |
|---|---|---|
| `RESULT_IN_RANGE` | note | every normal return gives a result inside the declared range |
| `RESULT_MAY_LEAVE` | warning | the computed range is not inside the declared range (the runtime guard will stop some calls) |
| `RESULT_ALWAYS_OUTSIDE` | error | no result the body can give is in range: every call would be stopped |
| `RESULT_NOT_BOUNDED` | note | the analysis could not bound the result |
| `PROPOSAL_OUT_OF_BOUNDS` | error | a constant proposal breaks the declaration's `proposes` type |
| `PROPOSES_UNDECLARED` | error | the body calls `propose` but the declaration has no `proposes` |
| `PROPOSAL_COUNT` | error | more straight-line `propose` calls than the declaration's `max` |
| `ALWAYS_TRAPS` | note | the straight-line body reaches `unreachable`: it never returns normally |
| `ANALYZED` | note | the body was analysed (instruction and propose-call counts) |
| `NOT_ANALYZED` | note | the body was skipped, and why: a block, loop, `if`, branch, or an instruction outside the subset |

**A skipped body is not a failure and not a pass.** It means the analysis
had nothing to say about that export. Real plugins often have loops, so
expect `NOT_ANALYZED` often.

## Report format

The text report is shown above. The JSON report (`--json`, or
`zau_report_json`) has this shape:

```json
{
  "tool": "z-audit",
  "version": "0.1.0",
  "module": {"label": "plugin.wasm", "bytes": 2298, "sha3_256": "b929f2d3..."},
  "contract": {"label": "plugin.zc", "digest_sha3_256": "6fa522a4..."},
  "result": "fail",
  "strict": false,
  "errors": 1,
  "warnings": 1,
  "checks": [
    {"id": "module.validate", "status": "pass", "summary": "...", "errors": 0, "warnings": 0, "notes": 0, "suppressed": 0}
  ],
  "findings": [
    {"check": "exports.analysis", "severity": "error", "code": "PROPOSAL_OUT_OF_BOUNDS",
     "message": "...", "export": "half", "func": 3, "offset": 171}
  ],
  "note": "Bounded static checks. A pass does not prove ..."
}
```

- `result` is `"pass"` or `"fail"`; `status` is `"pass"`, `"warn"`,
  `"fail"` or `"skip"`; `severity` is `"info"`, `"warning"` or `"error"`.
- `contract.digest_sha3_256` is z-contract's digest of the canonical
  contract text (the same `zc_contract_digest` a host can pin); it is
  `null` with `--no-contract` or when the contract does not parse.
- `export`, `func` (function index) and `offset` (byte offset in the
  module) are `null` when they do not apply.
- `suppressed` counts findings past the cap (256 per check, 4096 in all);
  the totals still count them.
- Strings are escaped JSON; names taken from the module are made valid
  UTF-8 first. Every number is an integer.

## Library

The CLI is a thin wrapper over `libzaudit.a`:

```c
zau_options *o;
zau_report *r;
int32_t pass;
zau_options_create(&o);
zau_options_grant(o, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE);
zau_audit(o, wasm, wasm_len, contract_text, contract_len, &r);   /* ZXV_OK even when it finds problems */
zau_report_verdict(r, &pass, NULL, NULL);
zau_report_json(r, "plugin.wasm", "plugin.zc", buf, cap, &len);  /* two-call buffer (R4) */
zau_report_destroy(r);
zau_options_destroy(o);
```

`zau_audit` returns `ZXV_OK` with a report whatever it finds. A non-OK
status (`ZXV_E_ARG`, `ZXV_E_NOMEM`, `ZAU_E_INPUT` -1401) means the audit
could not run. `zau_report_check` and `zau_report_finding` give each check
and finding with its code, severity, export, function index and offset.

## What z-audit does not do

- **It does not prove anything about the plugin.** The analysis is bounded
  by design: one pass, intervals only, straight-line bodies only. It does
  not follow calls between functions, reason about memory contents or
  globals, or look at bodies with control flow. A pass means "these
  checks found nothing", not "the plugin cannot reach a Forbidden state".
- **It does not replace the runtime guards.** A host must still load the
  plugin through Z-Wasm (validator, fuel, memory limits) and attach the
  contract with z-contract, so every call is checked. z-audit catches
  problems earlier, in CI, where they are cheaper to fix.
- **It does not check the contract is the right one.** It checks that the
  plugin and the contract you gave agree. Pin the contract digest in the
  host if you need to know which contract a plugin was audited against.
- **It trusts its own build.** z-audit reuses Z-Wasm's validator and its
  internal decoder (`zw_wasm.h`) from the same source tree, so a bug there
  affects both the audit and the host. It is not an independent second
  implementation.
- **It does not look at data segments, start functions or tables beyond
  what the validator checks.**

Inputs are treated as hostile: the module and contract are parsed with
bounded readers, findings are capped, names are sanitised before they are
printed, and the fuzzer below runs on every change.

## Tests

| Test | What it covers | Checks |
|---|---|---|
| `test/test_audit.c` | name sanitising; every float, SIMD and malformed case; grants and the allow-list; contract parse errors; every `exports.declared` and `contract.bounds` rule; the analysis (in range, may leave, always outside, constant proposals, propose counts, traps, skipped bodies); JSON validity and text output; every truncated prefix of a valid module | 109 |
| `test/fuzz_audit.c` | deterministic mutation fuzzer: a report every time, the same twice, valid JSON, verdict consistent with the counts, a pass only when the validator accepts, no float or refused import in a validated module, every offset inside the module | 2000 mutants in `make test` |
| `make run-examples` | the CLI on the example plugins: exit codes 0/1/2 and the JSON findings expected | 5 runs |
| `make check-nofloat` | no float type in the kit's sources and tests (R11), plus `-mgeneral-regs-only` where the compiler has it | |

`make fuzz FUZZ_ITERS=250000` runs longer; `ZAU_FUZZ_SEED=n` picks another
mutation stream. `make fuzz-libfuzzer` builds the same entry point with
clang's libFuzzer and runs it for 60 seconds. CI runs `make test` with gcc
and clang on Linux and clang on macOS, and `SANITIZE=1` (ASan + UBSan)
with four fuzz seeds of 250,000 iterations each.

## Files

| Path | What it is |
|---|---|
| `include/zaudit.h` | the public API |
| `src/zau_module.c` | the section and body walker (float/SIMD listing, imports, exports, bodies) |
| `src/zau_audit.c` | options, grants and the checks in order |
| `src/zau_bounds.c` | `rat` range feasibility for `den` rules |
| `src/zau_absint.c` | the bounded interval analysis |
| `src/zau_report.c` | the JSON and text reports |
| `src/zau_util.c` | findings, errors, name sanitising |
| `tools/zau_tool.c` | the `z-audit` CLI |
| `test/` | unit tests, the fuzzer, shared test helpers |
| `examples/` | example contracts and `plugins/audit_plugin.c` |

Build output goes to `build/` (git-ignored). Z-Wasm and z-contract are
built into `build/zwasm` and `build/zcontract`; their own `build/`
directories are not touched.

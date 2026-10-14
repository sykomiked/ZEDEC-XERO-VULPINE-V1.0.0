<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# Adversarial self-audit protocol

The normal gate (`make -C kernel verify-all`, CI, CodeQL, the mutation gate)
answers "do the tests pass?". This protocol answers the questions an attacker
or a sceptical reviewer asks instead: *what would make the tests pass while
the system is wrong, and what input would break it that nobody has tried?*
It runs on a developer's machine, against our own code only, and never
publishes anything.

Results of each run are kept in `docs/ADVERSARIAL_AUDIT_<date>.md`. The first
run is [ADVERSARIAL_AUDIT_2026-10-12.md](ADVERSARIAL_AUDIT_2026-10-12.md).
It builds on the earlier audits in [AUDIT_REPORT.md](AUDIT_REPORT.md) and
[GAP_AUDIT.md](GAP_AUDIT.md) rather than repeating them.

## 1. Who we play

| Attacker | What they control | Where they hit |
|---|---|---|
| A1 hostile peer | every byte on the mesh, call and Vinea links | frame, packet, signalling, AIPI and freight decoders; settlement messages |
| A2 hostile model or file | a GGUF file, a tokenizer vocab, a ROM, a font, a disk image, an update package | loaders and parsers, the hardware-update verifier |
| A3 hostile local program | the zxvd socket and the desktop HTTP port | JSON-RPC parsing, the HTTP guard, amounts and capabilities |
| A4 hostile counterparty | a valid account and valid calls in any order | ledgers, vaults, swaps, auctions, bonds, estates: money from nothing, refused calls with side effects, overflow at the edges |
| A5 malicious contributor | a pull request | workflows (pull_request_target, script injection, moving action tags), checked-in binaries and secrets |
| A6 sceptical reviewer | nothing but reading time | tests that never run, tests that test a copy of the code, tests that would pass if the code were wrong, docs that point at nothing |

A6 is aimed at ourselves. Much of this code was written fast, by many
sessions in parallel; the cheapest way for a bug to survive is a test that
looks like coverage and is not.

## 2. Rules of engagement

* Local only. Nothing in this protocol pushes, publishes, opens a network
  connection to a third party, or attacks anyone else's system.
* Our code only. Vendored third-party code (`pqsec/{mldsa,slhdsa,mlkem1024,hqc5}`,
  `pay/xsd`, `third_party/`) is checked for secrets and binaries but is
  audited upstream.
* No weakening the gate to get green: never skip, disable or loosen a test,
  and never add an allow-list entry for a FAIL without a reason a reviewer
  can check.
* Every real bug gets a regression input or test that fails before the fix
  and passes after it, in the same change as the fix.
* A fix in code another work stream owns is handed to that stream with the
  repro, not patched behind its back.

## 3. The stages

`scripts/adversarial-audit.sh` runs them; each prints PASS or FAIL and keeps
its log.

### S. Static (seconds) — `build_system/adversarial_audit.py`

| Check | Level | What it catches |
|---|---|---|
| `secrets` | FAIL | private keys, GitHub/AWS/Hugging Face/Slack/OpenAI/Anthropic tokens, key or `.env` files by name |
| `binaries` | FAIL | ELF, PE or Mach-O executables checked in (fuzz corpora excepted) |
| `orphan-tests` | FAIL | a `test_*.c` that no Makefile, script or workflow builds |
| `shadow-tests` | WARN | a test that defines its own copy of a function the tree ships, so it may test the copy |
| `untested-modules` | WARN | a `kernel/src` module with no test, host test or fuzz harness |
| `unfuzzed-parsers` | WARN | a header that declares a byte-input entry point (`*parse*`, `*decode*`, `*unpack*`, `*load*`, `*verify*` taking a byte pointer) that no fuzz harness reaches |
| `workflows` | FAIL / WARN | `pull_request_target`, missing top-level `permissions`, `write-all`, untrusted event text expanded inside `run:` (FAIL); third-party actions on a moving tag, `curl \| sh` (WARN) |
| `fail-open` | WARN | a TODO or stub next to a success return in security code |
| `dangerous-calls` | FAIL / WARN | `gets` and `rand` in crypto code (FAIL); `strcpy`, `strcat`, `sprintf`, `system` (WARN, each one reviewed) |
| `doc-links` | WARN | a Markdown link to a repository file that does not exist |

Known, accepted findings live in `build_system/adversarial_audit.allow`, one
per line with its reason. An allowed FAIL is reported as WARN. An entry that
no longer matches anything is reported too, so the list only shrinks.

### F. Fuzz and property harnesses — `--fuzz`

Every harness in `fuzz/` (see [fuzz/README.md](../fuzz/README.md)) is
replayed over its committed corpus and then run on seeded random inputs
(three seeds, 20 000 inputs each by default) under ASan and UBSan. Where
clang's libFuzzer runtime is installed, each one also gets a short
coverage-guided run. Property harnesses check, after every operation:

* conservation: nothing is created or destroyed except by an explicit mint
  or burn;
* a refused operation changes nothing;
* caps and overflow at 0, 1, -1, 2^31, 2^32, 2^53, 2^62 and the 64-bit edges;
* authorisation: the wrong owner, capability or signature is refused;
* the module's own stated invariants (they are listed at the top of each
  harness).

Each audit run adds harnesses for the modules the static stage lists under
`unfuzzed-parsers` and for any new money-moving module, newest and most
exposed first.

### M. Mutation of security code — `--mutate`

`tests/harness/mutator/mutate.py` (see
[tests/MUTATION_REPORT.md](../tests/MUTATION_REPORT.md)) flips one operator,
constant, guard or return at a time. A mutant that survives is a change to
the code that no test notices, which is exactly the hole an attacker looks
for. `build_system/adversarial_targets.json` lists the security code this
stage covers (AIPI, the hardware-update verifier, the update checker) with
measured baselines; a score below its baseline fails. Each surviving mutant
in those files is either killed by a new test or written down as equivalent.

### D. Full host suite — `--deep`

`make -C kernel verify-all`, so the audit's fixes are proven against
everything else. `scripts/verify-local.sh --all` remains the full CI mirror
(boots, integer sanitizer, MSan, coverage).

### C. Claims (by hand, each run)

Pick the five strongest claims in `README.md` and the architecture docs
(speed, accuracy, "post-quantum", "fail-closed", "exact") and check each one
against a measurement or a test that would fail if the claim were false.
A claim with neither is reworded or gets the test.

## 4. Severity

| Level | Meaning | Handling |
|---|---|---|
| CRIT | remote code execution, key or secret exposure, money created or destroyed by a hostile input, a signature or update check that can be bypassed | fixed before anything else lands |
| HIGH | memory-safety bug on untrusted input, a crash a peer can cause, a refused operation with side effects in a ledger, a test gate that cannot fail | fixed in the audit's change or the owning stream's next change |
| MED | missing harness or test on exposed code, a shadow or orphan test, a workflow hardening gap | tracked in the run's report with an owner |
| LOW | style, unclear docs, defensive improvements | listed |

## 5. Running it

```
scripts/adversarial-audit.sh            # static stage only
scripts/adversarial-audit.sh --fuzz     # + every harness under ASan/UBSan
scripts/adversarial-audit.sh --mutate   # + mutation of the security code
scripts/adversarial-audit.sh --deep     # everything, including verify-all
```

All binaries go to a private `mktemp -d` directory (passed to the kernel
Makefile as `ZXV_TMP`), so a run never uses a fixed `/tmp` path another user
could race. Exit status is 0 only when no stage failed.

## 6. After a run

1. Write `docs/ADVERSARIAL_AUDIT_<date>.md`: what ran, every finding with
   its level, file:line, the input that shows it, and whether it was fixed,
   handed to its owner, or accepted (and why).
2. Fix what is ours, with the regression input committed beside the fix.
3. Update [AUDIT_REPORT.md](AUDIT_REPORT.md) for any item the run closes.
4. Shrink `build_system/adversarial_audit.allow` where a finding is gone.

# Security Policy

ZXV (ZEDEC-XERO-VULPINE) is pre-release software provided under the Apache
License 2.0 with no warranty (see `LICENSE`). We still treat security reports
seriously and fix them as fast as we can.

## Supported versions

| Version | Supported |
|---------|-----------|
| `main` branch (latest commit) | Yes |
| Latest published preview release (currently 0.1.x preview) | Yes, fixes land on `main` and in the next preview |
| Older previews, forks, and feature branches | No |

Until 1.0 there are no long-term support branches: please reproduce against
current `main` before reporting.

## Reporting a vulnerability

**Do not open a public issue, pull request, or discussion for a security
problem.**

Report privately through GitHub's private vulnerability reporting:

1. Open the repository's **Security** tab.
2. Choose **Report a vulnerability**.
3. Fill in the advisory form.

Direct link: <https://github.com/sykomiked/ZEDEC-XERO-VULPINE-V1.0.0/security/advisories/new>

Please include:

- the affected component (path, module, RPC, SDK, or binary) and commit SHA
  or release;
- the platform (macOS Apple Silicon or Intel, Linux, Windows, bare-metal
  UEFI) and compiler;
- steps or a proof of concept that reproduces the issue;
- the impact you expect (key or secret disclosure, signature forgery, remote
  code execution, ledger or balance corruption, denial of service, and so on);
- whether you want public credit, and under what name.

## What to expect

| Step | Target |
|------|--------|
| Acknowledge your report | within 3 business days |
| Initial assessment and severity (CVSS v4) | within 7 days |
| Fix or mitigation for Critical / High | within 30 days |
| Fix or mitigation for Medium / Low | within 90 days |
| Public advisory | when a fix is released, or at 90 days, whichever is first |

We will keep you updated in the private advisory thread, agree a disclosure
date with you, request a CVE through GitHub when one is warranted, and credit
you in the advisory unless you ask us not to. If a deadline cannot be met we
will tell you why and propose a new date.

## Scope

In scope:

- **Post-quantum cryptography** in `kernel/src/pqsec/`: ML-KEM-768/1024,
  ML-DSA-65/87, SLH-DSA-SHAKE-256s, HQC-5, and the hybrid PQ matrix
  (`docs/PQ_MATRIX.md`). This includes deviations from FIPS 203 / 204 / 205 or
  the NIST ACVP known-answer tests, timing or memory side channels in
  constant-time code paths, nonce or randomness misuse, key or shared-secret
  leakage, and signature malleability or forgery.
- The ZXV kernel (`kernel/`), userland, and `libzxv` ABI.
- The `zxvd` daemon and its JSON-RPC socket, the macOS app, and the Linux /
  Windows desktop builds.
- Mesh, transport, and two-node remote inference (authentication, key
  exchange, replay, packet parsing).
- Ledger, payments, and finance modules (balance or fee manipulation, double
  spend, settlement bypass).
- Model loading (GGUF parsing, tokenizer) and anything that handles untrusted
  model files or network input.
- SDKs and bindings under `sdk/` and `bindings/`.
- Release artifacts and the build or CI pipeline (`.github/`, `build_system/`).

Out of scope:

- Vendored third-party code, unless the problem is in how ZXV integrates or
  configures it; please report upstream issues upstream too.
- Findings that need an already-compromised host or physical access to an
  unlocked machine.
- Denial of service through resource exhaustion on your own local machine.
- Model output quality, hallucination, or prompt content (not a security bug
  unless it crosses a privilege or sandbox boundary).
- Hardware designs under `LICENSE-HARDWARE.md`; send those through the same
  private channel and we will handle them case by case.

## Safe harbor

We will not pursue legal action against good-faith research that follows this
policy: test only against your own installs, avoid privacy violations and
data destruction, do not degrade other users' nodes on a shared mesh, and give
us reasonable time to fix before disclosure.

## Security tooling

This repository runs CodeQL code scanning (advanced setup,
`.github/workflows/codeql.yml`), Dependabot alerts and grouped security updates
(`.github/dependabot.yml`), and secret scanning with push protection.

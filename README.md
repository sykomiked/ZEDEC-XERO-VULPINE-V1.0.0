<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3
-->

# ZEDEC XERO VULPINE — ZEDEC pqOS

**A freestanding, from-scratch, post-quantum operating system.**
Author: H.M. Michael-Laurence: Curzi (c) · 36N9 Genetics, LLC

ZEDEC XERO VULPINE ("VOVINA SHAKINA," the M5 Axiomatic Kernel) is a freestanding
C kernel that replaces clock-driven execution with **phase-tick** event
sequencing, treats **every system as a sovereign node** and **every user as a
sovereign individual**, and is built to run equally well on a home computer, a
workstation, a server, and a supercomputer. It carries its own network and TLS
1.3 stack, a native content-addressed economy, a from-scratch font/graphics
layer, and its own Tri-Space native file formats (`.zxvc` / `.cedez` / `.cedec`).

Everything here follows one engineering rule without exception: **no hollow
capabilities.** Nothing claims a capability the code does not deliver; ops
boundaries fail closed rather than fake a result; and every test asserts computed
values against an external anchor (an RFC/FIPS vector, a known-answer, a
conservation identity), never against the code's own output.

## Orientation

| I want to… | Read |
|---|---|
| Understand the license | [`LICENSE`](LICENSE) + [`LICENSES/`](LICENSES/) |
| Cross-compile on a server | [`SERVER_HANDOVER.md`](SERVER_HANDOVER.md) + [`build_system/handover.json`](build_system/handover.json) |
| Navigate the source | [`SOURCE_CODE_NAVIGATION_GUIDE.md`](SOURCE_CODE_NAVIGATION_GUIDE.md), [`SUBSYSTEM_INDEX.md`](SUBSYSTEM_INDEX.md) |
| Read the deep architecture | [`VOVINA_SHAKINA_WHITE_PAPER.md`](VOVINA_SHAKINA_WHITE_PAPER.md) |

## Build & verify

The reference target is **ARM64** (the only target buildable on Apple Silicon;
the rest cross-compile on Linux per the handover). From `05_KERNEL/`:

```bash
make -f build_system/Makefile.arm64 clean && make -f build_system/Makefile.arm64 all
qemu-system-aarch64 -M virt,gic-version=3 -cpu cortex-a53 -m 256M \
  -kernel kernel_arm64.elf -nographic -serial mon:stdio
```

Run the host integrity gate (from `05_KERNEL/kernel/`):

```bash
make verify-all
```

## License

ZEDEC XERO VULPINE is licensed under a **four-instrument share-alike stack** that
travels together — you may not strip any instrument off a derivative, and
attribution is required:

- **Open Piracy License 1.1 (OPL-1.1)** — the operative software copyleft
- **Creative Commons Attribution-ShareAlike 4.0 (CC BY-SA 4.0)** — attribution + share-alike
- **Royal Writ of the Sicilian Crown** — mutual sovereign recognition (reciprocity)
- **Streisand Engine License 3.3 (SEL-3.3)** — a statement of position

Precedence when they differ: `OPL-1.1 > CC BY-SA 4.0 > Royal Writ > SEL-3.3`.
See [`LICENSE`](LICENSE) for the full, authoritative text. **All software made
natively for this platform is automatically under this same stack, without
exception.**

---

## Commercial Licensing & Custom Engineering

ZEDEC XERO VULPINE is proudly open-source under the **Open Piracy License 1.1
(OPL-1.1)**, the operative copyleft of its four-instrument license stack.

The OPL-1.1 ensures that this architecture remains free and auditable forever.
However, its strict share-alike provisions mean that if you modify and embed this
kernel into a proprietary, closed-source hardware product, you must open-source
your entire product.

We understand that Tier-1 foundries, defense contractors, and enterprise
manufacturers cannot expose their proprietary silicon logic or application
stacks. For organizations requiring absolute IP protection, custom adaptation, or
guaranteed Service Level Agreements (SLAs), **36N9 Genetics LLC** offers private,
commercial dual-licensing and architectural consulting.

To negotiate a proprietary commercial license or a Joint Venture hardware
integration, contact the Principal Architect directly at: **deal@zedec.ai**

## Support the Architecture

This is a freestanding, from-scratch foundation built outside the traditional
Silicon Valley venture-capital apparatus. If this operating system solves a
critical problem for your independent project, or if you simply respect the
mathematics, gratuities are welcome and directly fund the ongoing red-team audits
and hardware cross-compilation.

| Chain | Address |
|---|---|
| **EVM** | `0xe673621b36984cb1f74a876b4ba26c4f6ca4e25f` |
| **Tron** | `TJWpaS3ByovWePBaYgeQTPcfvkEAnRwYam` |
| **Solana** | `AUcWUSpNJdjPe3M7o1MdtxMUPKp67gkGExEpHiQVF26t` |
| **Sui** | `0x6a66a72ecf7f2aec68250e5d8ff98904f57f3e45fb1df6457fe3168b2539fb11` |
| **BTC** | `bc1qc9t7nwj8ynph0nhuwygr05u6853kg0mfafxqxn` |
| **Aptos** | `0x689c200e70586733166b533268bd6c3ed68e295336a13cf6396936196d8c1e82` |
| **Cosmos** | `cosmos19kwdzlr0qfwn7klvwwlmccsecvxezt6rnkm4kh` |

**VINO Physical Standard:** settlement under the VINO triple-rail standard is
welcome; contact for terms.

**Fiat / Institutional:** contact **deal@zedec.ai** for wire routing.

---

*By design, this pitch lives here in the README — never inside the license text.
The OPL-1.1 and the rest of the stack stay mathematically pure and strictly
confined to enforceable mechanics; this page is where the human and commercial
conversation happens.*

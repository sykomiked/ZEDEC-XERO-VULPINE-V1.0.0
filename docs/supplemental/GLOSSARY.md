<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Glossary

ZXV uses many names of its own. This page gives a plain meaning for each one a
newcomer is likely to meet, and the main place in the repository where it
lives. Paths are relative to the repository root. Where a name is only a
plan, the entry says so.

Several names are word-play or working titles. The entry says what the code
does, not what the name suggests. Many module headers also state their own
"honest limits"; read those before relying on a module.

---

**Assurance fee.** The one fee in the ZXV economy: `floor(a × 8889 / 10,000,000)`,
that is 0.08889% of an amount, split 50/25/15/10 into the reserve floor, the
V-Bill dividend pool, infrastructure-node bounties and regenerative capital. It
replaced an earlier φ% tithe on 2026-10-10.
Lives in: `kernel/src/pay/pay_assure.h`, `kernel/src/settle/`; described in `docs/COMMERCE.md`.

**Carracho.** The old working name of the peer-to-peer module now called
Vinea. It was renamed because Carracho is an existing product name; a few
headers still mention it, and the superseded draft transport is kept, unbuilt,
for reference.
Lives in: `docs/VINEA.md` (historical note), `kernel/experimental/p2p_caracho/`.

**Chiglet.** ZXV's own small decision runtime, separate from the language
models. It is a bounded mixture of tiny linear experts whose weights come from
the ISF, so that duplicated evidence counts once and the runtime can answer
"uncertain" instead of reporting inflated confidence. It is not an LLM.
Lives in: `kernel/src/chiglet/` (start with the header comment of `chiglet.h`).

**Eve.** The compact shorthand of the Ada language pillar: a short form of Ada
type declarations with the same canonical layout, used for geofences,
corridors and other location data in exact integer units.
Lives in: `sdk/pillars/ada-eve/`.

**Freight packets.** The bulk-transfer format of the peer network. A
"smart packet" is one 21-octet UBH-168 frame; a freight is 168 of them
(3528 bytes), Reed-Solomon erasure-coded so that any k of the 168 rows rebuild
the payload, with a SHA-256 of the payload in a separate header. No compression
ratio is claimed.
Lives in: `kernel/src/freight/freight.h`.

**Game Master.** The test harness that boots the kernel with legacy game ROMs
attached as block devices, drives a repeatable input storm and logs every
kernel fault with its source location. It also runs each ROM a second time
with every byte inverted, to compare behaviour across the two inputs.
Lives in: `build_system/game_master.py`; background in `docs/supplemental/WHY_GAME_ROM_ENGINES.md` and `MEGAROM_PLAN.md`.

**GlyphMap.** A mark of 36N9 Genetics, LLC. In the older white paper it names
a family of planned ARM64 device profiles (phone, tablet and "M-Server"); the
current board profile code does not use the name.
Lives in: `VOVINA_SHAKINA_WHITE_PAPER.md`, `hardware/NOTICE`.

**Harmonic wire.** An integer signal-processing module: a fixed 21-octet frame
whose words alternate little- and big-endian, carried on ten audio-band
carriers (the trunks), plus adapters and routers. The electrical names in the
specs are names for data operations; nothing in it has a physical meaning
beyond that.
Lives in: `kernel/src/harmonic/`; documented in `docs/HARMONIC_WIRE.md` and `docs/supplemental/HARMONIC_WIRE_EXPLAINED.md`.

**Hexagram tag.** A 6-bit tag (64 states) put on a unit of work, such as a
ledger posting or a model request. Two bits hold the lpres truth state, two the
trend of the evidence (including a "forbidden" wall) and two the lifecycle
phase. One 64-entry table maps the tag to an action: proceed, wait, refuse,
escalate to a person, or roll back and quarantine.
Lives in: `kernel/src/lpres/lpres_hex.h`; exposed to SDKs as `Hexagram` (libzxv ABI 1.3).

**hwemu.** The hardware emulation module: an emulation matrix that serves each
hardware function from a hardware, peer or software provider, the ZHP-1
hardware profiles, standard engines with golden vectors, and signed hardware
updates with two slots and rollback.
Lives in: `kernel/src/hwemu/` (see its `README.md`).

**ISF (Interaction Surplus Framework).** The author's mathematical framework
for how much two sources of evidence add together:
f(u) = ln(1 + (N−1)u) with u = 1 − cos²(a, b), plus an open-system budget rule
Q' = (1 − δ)Q + ηS − C. The kernel computes it in integers and uses it to
weight experts and to gate redundant vectors.
Lives in: `kernel/src/surplus/surplus.h`, `kernel/src/tensor/zt_isf.c`.

**KRON.** The shorthand of the APL/K language pillar: an exact array language
(rank at most 2, 64-bit rationals, four-valued truth) that reads APL glyphs or
K ASCII into one canonical program, intended for time series.
Lives in: `sdk/pillars/apl-kron/`.

**libzxv.** The ZXV engine packaged as a library with a stable C ABI
(version 1.3 on `main`), checked in CI so the ABI can only grow. The SDKs and
the language pillars are built on it.
Lives in: `sdk/libzxv/`.

**lpres.** The kernel's four-valued (paraconsistent) logic: a claim can be
True, False, Both (evidence on each side) or Neither (no evidence), and a
contradiction does not make everything provable. It is the "K3" axiom of the
M5 substrate. Some older documents name the last two states NEUTRAL and
UNKNOWN.
Lives in: `kernel/src/lpres/`; see `docs/supplemental/MULTI_VALUED_LOGIC.md`.

**M5 / M⁵.** The name for the kernel's five base axioms: OSEQ (causal
ordering), RMAG (exact arithmetic), LPRES (four-valued logic), IPHASE
(asymmetric routing) and CHOICE (deterministic collapse). "M5 Axiomatic
Kernel" is the kernel's formal name, and the emulator code measures
relations along five "M⁵ axes".
Lives in: `ARCHITECTURE.md` ("M5 Axiomatic Substrate"); `kernel/src/oseq/`, `kernel/src/rmag/`, `kernel/src/lpres/`, `kernel/src/iphase/`, `kernel/src/choice/`, `kernel/src/emu/emu_relate.h`.

**MegaROM.** A long-range plan to build one ZXV game ROM that combines the
mechanics extracted from many classic games and runs on the kernel. Today it
consists of a mechanics-extraction tool, a deduplicated first corpus and a
schema module; the full plan is stated as decades of work.
Lives in: `MEGAROM_PLAN.md`, `kernel/src/megarom/`, `megarom_corpus/`, `tools/megarom_extract.c`.

**MYRA.** The shorthand of the MUMPS (M) language pillar: it writes M globals,
with typed subscripts and piece values, one line per node, for clinical and
genomic record trees. Clinical use is advisory only.
Lives in: `sdk/pillars/mumps-myra/`.

**Paradox engine.** The module that handles work whose lpres state is Both or
Neither. It sorts each case into one of four kinds (constructive, destructive,
neutral, unclassified) and contains, defers or logs it in a hash-chained
record. It routes contradictions; it does not make both sides true.
Lives in: `kernel/src/paradox/`; documented in `docs/PARADOX_ENGINE.md`.

**Phase-tick.** ZXV's replacement for a global clock: subsystems advance on
logical phase-tick events issued by a phase coordinator, rather than on wall
time. The phase tick is the kernel's logical time.
Lives in: `kernel/src/phase_coord/`; described in `ARCHITECTURE.md` ("Phase-Tick Sequencing").

**Porter House.** A per-port admission policy (a firewall decision function)
that sits in front of the network stack and decides whether a peer, identified
by a 168-bit id, is admitted to a sealed port before any payload is read. The
name is a pun on a doorman.
Lives in: `kernel/src/porter_house/porter_house.h`.

**Sutra.** Two related things. In the kernel, a small "nonlinear COBOL"
language whose statements run in ordinal order against the Vino triple ledger.
In the SDK, the compact shorthand of the COBOL pillar, read from the same
layout as COBOL copybooks; it rejects weights that do not sum to exactly 1 and
claims above a deposit.
Lives in: `kernel/src/sutra/`, `sdk/pillars/cobol-sutra/`.

**Trebago.** The shorthand of the Fortran language pillar: a short syntax for
exact sum, dot, scale, prefix and matrix-vector kernels over 64-bit integer
arrays, which refuses overflow instead of rounding.
Lives in: `sdk/pillars/fortran-trebago/`.

**Trunks.** The ten carriers of the harmonic wire: 111 Hz to 999 Hz in 111 Hz
steps, plus 1111 Hz, sampled at 8 kHz. They carry 21-octet frames; each trunk
has a line state (dead, on hook, off hook).
Lives in: `kernel/src/harmonic/zt_trunk_bank.h`; `docs/HARMONIC_WIRE.md`.

**UBH-168.** The project's canonical frame size: 168 bits, which is 21 octets,
24 septets or 28 sextets. The tensor engine's frames, freight packets and the
harmonic wire all use 21-octet frames. Hashes stay 256-bit.
Lives in: `kernel/src/ubh/ubh.h`; frame rules in `docs/TENSOR_ENGINE.md` (T16).

**V-Bill.** An instrument in the ZXV economy: perpetual equity
(musharakah) paid a dividend from the assurance fee and redeemable at a backing
floor. Issuance by backed-only Dutch auction is built and host-tested in
`kernel/src/vauction`, the dividend in `kernel/src/vswap` and redemption in
`kernel/src/svault`; zxvd serves them (`docs/ZXVD.md`). It is also the pivot
currency in the `vhub` clearing demo.
Lives in: `kernel/src/vauction/vauction.h`, `kernel/src/vswap/vswap.h`, `kernel/src/svault/svault.h`, `kernel/src/vhub/vhub.h`, `kernel/src/pay/pay_assure.h`; plan in `docs/ARCHITECTURE.md` and `docs/GAP_AUDIT.md` (C2, C5).

**VFV (Vino Floating Voucher).** The unit of account of the Vino ledger and of
the peer economy, for example in the two-node demo and in compute farming.
Coins are minted in Fibonacci denominations only; any other amount is minted
as its Zeckendorf split. It is not backed by anything outside the node.
Lives in: `kernel/src/vino/`, `kernel/src/pay/pay_farm.h`; see `docs/TWO_NODE_DEMO.md`.

**Vinea.** The peer-to-peer overlay: Kademlia-style routing with host spaces
in which a node publishes what it shares and on what terms, with post-quantum
signed messages and sessions. It is off by default.
Lives in: `kernel/src/vinea/` (prefix `vna_`); documented in `docs/VINEA.md`.

**Vino.** The decentralized bank node module: a triple ledger (history,
balances and a SHA-256 hash-chained audit ledger) over the nine forms of
capital, plus payment message generators. It mints VFV coins.
Lives in: `kernel/src/vino/vino.h`.

**VOVINA SHAKINA.** An alternative name for the kernel, used in older
documents and the original white paper.
Lives in: `VOVINA_SHAKINA_README.md`, `VOVINA_SHAKINA_WHITE_PAPER.md`.

**VULPINE.** The third word of the project name, and the class name of the
public reference machine R1 that the hardware profiles target by default.
Lives in: `kernel/src/hwemu/hwp.h` (`hwp_reference_r1`).

**Web 4.** ZXV's agent layer that links one person or agent across Web 2
(OIDC logins, HTTP), Web 3 (Ethereum addresses and transactions) and ZXV's own
post-quantum identity, with signed payment intents that need a human consent
token.
Lives in: `kernel/src/web4/`; documented in `docs/WEB4.md`.

**Zenny chips.** User-built baskets: a chip has a mandatory V-Bill leg plus
weighted fiat, commodity or compute voucher legs, recorded in a write-once
manifest. The claim must equal the deposited value exactly, at mint and at
every rebalance; leverage is refused. The name needs a trademark check before
public use.
Lives in: `kernel/src/zenny/zenny.h`.

**ZHP-1.** The ZXV hardware profile format, version 1 ("the hardware ROM"): a
description of a machine's components that the emulation matrix uses to decide
what runs in hardware and what is emulated.
Lives in: `kernel/src/hwemu/hwp.h`; specification in `sdk/docs/HW_PROFILE.md`.

**ZXV.** Short for ZEDEC XERO VULPINE, the whole project: a freestanding C
kernel with post-quantum security, which also runs as a desktop app and daemon,
and which hosts a local swarm of small language models on an integer-only
tensor engine.
Lives in: `README.md`, `kernel/`.

**zxvd.** The headless ZXV daemon: the one process where the swarm, the model
slot, the settlement spine and the optional peer node run together, controlled
by JSON-RPC over a local socket.
Lives in: `kernel/arch/hosted/zxvd_rpc.c`, `api/zxvd.openrpc.json`; documented in `docs/ZXVD.md`.

<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
     SPDX-License-Identifier: Apache-2.0 -->
# ZXV Complete Architecture

ZEDEC XERO VULPINE (ZXV) / VOVINA SHAKINA. Registered to 36N9 Genetics LLC, Michael Laurence Curzi.
Code licence Apache-2.0.

Version 4, **2026-10-10, 12:45 UTC**. This is the full architecture: everything specified so far,
how the parts fit together, and the state of each one. It replaces versions 1–3 of this file.

Sources: the repository (draft PR #1, branch `claude/project-thread-j44rs9`, last pushed commit
`b92bd8c`, all 21 GitHub checks green), the gap audit ([GAP_AUDIT.md](GAP_AUDIT.md)),
the per-module reference ([SYSTEM_REFERENCE.md](SYSTEM_REFERENCE.md)), the audit
([AUDIT_REPORT.md](AUDIT_REPORT.md)), project memory, the build thread "Plan adaptive local AI
architecture" and the "Real model test" thread.

---

## Contents

1. What ZXV is, and where it stands
2. The rules every part follows
3. The stack at a glance
4. The two loops that join it
5. Stratum 0: shared base
6. Stratum 1: kernel and hardware abstraction
7. Stratum 2: transport
8. Stratum 3: trust and identity
9. Stratum 4: logic and fault handling
10. Stratum 5: AI runtime and swarm
11. Stratum 6: settlement and economy
12. Stratum 7: adapters and the five language pillars
13. Above the strata: products, apps and people features
14. Cross-cutting layers: privacy, transport continuum, use cases
15. Quality: tests, self-audit, self-healing, the 13:1 target
16. Order of work
17. Decisions made
18. Decisions still open
19. Where spec text and the design differ
20. Design analogy: the seven quantum levels
21. Honest notes

### Status labels

| Label | Meaning |
|---|---|
| ✅ **Built** | In the code and tested on every CI run |
| 🧩 **Built, not connected** | Works and is tested, but no product calls it yet |
| 🟡 **Partial** | Some of it works; gaps listed |
| 🔧 **In progress** | Being built now |
| 📋 **Queued** | Specified and agreed; waiting its turn |
| 💭 **Open** | Needs a decision or a design first |

Every row also carries its **agreed limits**: what the part will deliberately not do, or not claim.

---

## 1. What ZXV is, and where it stands

**ZXV is a vertically integrated stack: one freestanding, integer-only C code base that runs as a
desktop app, as a bootable operating system and as an embeddable library.** On top of it sit a
local AI swarm, a post-quantum peer-to-peer network, and an exact, interest-free settlement
economy with adapters to the outside financial world.

### Where it stands

| Measure | Now |
|---|---|
| Code | ~300,000 lines of protocol C in 202 live modules; 12 dead modules archived |
| GitHub second opinion | 21 of 21 checks green on `b92bd8c`, including CodeQL, the mutation gate, fail-closed boots of five CPU targets, and the TLA+/Lean proofs |
| Local verify | 4,515 checks, 5 image builds, 2 boots, all passing |
| Real AI model | **Runs.** Qwen2.5-1.5B on the ZXV integer engine: perplexity 9.371 vs llama.cpp 9.362, same answers on all 8 test prompts, bit-identical on x86 and arm64. Slower: ~11 vs 15–18 tokens/s |
| Ledger fuzzing | 222 million netting operations across 4 ledgers, all clean |
| Test strength | Mutation scores: settlement spine 89%, libzxv engine 86%, quota maths 96% |
| Test-to-code ratio | **0.36:1** against a 13:1 target |

### How far along

My judgement from the code, tests and gap audit.

| Stage | Progress |
|---|---|
| Build the parts | ~80% of what was specified up to this morning. Much more has been specified since (sections 12 and 14), so as a share of the whole plan it is lower, about 50% |
| Harden them | ~80% |
| Join them (integration pass A1–A6) | 2 of 6 done (A1 ledger of record, A2 libzxv engine); A4 in progress |
| Real model | First run done and measured |
| Tests at 13:1 | 0.36:1 |
| Ship and release | Not yet |

---

## 2. The rules every part follows

These rules came from you or were agreed with you. Every module is held to them.

| # | Rule | Where it bites |
|---|---|---|
| R1 | **Integer only in the kernel.** No floating point in kernel images; banned at compile time | Bit-identical results on every chip, so answers can be signed and re-checked |
| R2 | **Lower strata never depend on higher ones** | A CI layering check is planned (gap B1) |
| R3 | **Money is exact.** A = L + E after every posting; claims equal deposits; issuance is backed only; no usury | Hard invariants that fail closed |
| R4 | **The AI proposes; the exact kernel decides.** Model output is untrusted input | Nothing a model says can write a balance |
| R5 | **Hard invariants are always FALSE when broken, never Both.** Only genuinely allowed disagreements (e.g. GAAP vs IFRS treatment) become Both | Four-valued logic can't hide a bug |
| R6 | **No silent fixes.** Forecasts report; production may only refuse, roll back or isolate, each with a signed receipt | Self-healing |
| R7 | **Shorthand parser in libzxv, execution in userland, signed integer results back** | All five language pillars |
| R8 | **Life-safety, clinical and grid actions are advisory and human-approved.** Defense work is limited to logistics, medical and mapping; no weapons targeting | Use cases |
| R9 | **Measured, never promised.** Speed, compression and netting savings are reported only after measurement | All docs and pitches |
| R10 | **Designed to follow, never "certified" or "compliant".** No claimed connection to PAPSS, SWIFT, central banks or card schemes | Adapters |
| R11 | **Local first; networking off by default** | The app opens no socket until the user turns it on |
| R12 | **No monopoly; cooperative markets; one fee for everyone** | Economy |
| R13 | **Holographic coding stays on the device; the wire carries original codes** | Measured: holographic form is lossless but larger |
| R14 | **No defensive reflection.** Tarpit and isolate; never send traffic anywhere unasked | Network defence |
| R15 | **Guardians approve with their own signatures and hold no key shares** | Recovery |

---

## 3. The stack at a glance

```text
 ┌───────────────────────────────────────────────────────────────────────────────┐
 │ PRODUCTS     Mac/Win/Linux app · zxvd daemon + SDKs · libzxv · kernel image · mobile │
 ├───────────────────────────────────────────────────────────────────────────────┤
 │ PEOPLE       chat, calls, streaming, social feed, speech, translation, games   │
 ├───────────────────────────────────────────────────────────────────────────────┤
 │ 7 ADAPTERS   ISO 20022 · accounting regimes · jurisdiction plug-ins · gateways │
 │              five language pillars: COBOL/Sutra · Fortran/Trebago · Ada/Eve ·  │
 │              APL-K/KRON · MUMPS/MYRA                                           │
 │ 6 SETTLEMENT ledger of record · 0.08889% fee · V-Bills · auctions · Zenny chips│
 │              Sicily Account · Player Cards · vault · disputes · estates        │
 │ 5 AI         integer tensor engine · SIMD · swarm · intent layer · L4 adapter  │
 │ 4 LOGIC      four-valued truth · peer audit · forecasting · designed recovery  │
 │ 3 TRUST      PQ handshakes · identity · guardian recovery · privacy proofs     │
 │ 2 TRANSPORT  Vinea DHT · UBH-168 · freight · IPFS · link selection · modems    │
 │ 1 KERNEL     boot (5 CPUs) · zero-float · capability syscalls · zxvfs · WORM   │
 ├───────────────────────────────────────────────────────────────────────────────┤
 │ 0 BASE       SHA-2/3 · AEAD · ML-KEM · exact rationals · ISF maths             │
 └───────────────────────────────────────────────────────────────────────────────┘
   cross-cutting: privacy layer · transport continuum · quality loop
```

**Three ways to run the same code:**

| Product | Contains today | Status |
|---|---|---|
| ZXV Swarm app (`zxv-engine`) | Swarm, tensor engine, **settlement spine + payment ledger + fee**, Vinea node, ML-DSA/ML-KEM, updater, IPFS node, notifications | 🟡 Runs; most of the economy and all people features not linked yet |
| Kernel image (arm64 reference, plus x86_64, riscv64, riscv32, arm32) | Core OS, AI boot self-check (`[AI_OK]`), economy self-checks at boot | 🟡 Boots under emulation; no network or full economy |
| `libzxv` packages (`libzxv-econ`, `-pqc`, `-tensor`, `-legacy`) | The economy engine as a standalone library: compress / route / decompress | ✅ Builds standalone; single-file amalgamation queued |

---

## 4. The two loops that join it

### Loop A: value (each stratum feeds the next; the top feeds the bottom)

```text
            ┌──── 7 ADAPTERS ◄────── 6 SETTLEMENT ◄──────┐
            │                                             │ A5
            ▼ A7                                     5 AI RUNTIME
      OUTSIDE WORLD                                       ▲
            │ A8  signed manifests, updates,              │ A4
            ▼     custodian receipts                      │
        1 KERNEL ──A1──► 2 TRANSPORT ──A2──► 3 TRUST ──A3──► 4 LOGIC
```

| Arc | What flows | Status |
|---|---|---|
| A1 kernel → transport | Sockets for Vinea | 🟡 App: one UDP socket, LAN or online, off by default. Kernel image: none |
| A2 transport → trust | Every datagram verified (ML-DSA-65, proof-of-work, replay window) | ✅ In the app |
| A3 trust → logic | Verified records become four-valued verdicts; peers replay them | 🧩 Peer audit not in the app |
| A4 logic → AI | The swarm budget gates every generated token | ✅ In the app |
| A5 AI → settlement | Each swarm cycle settles on the ledger of record, with A = L + E reconciled every cycle | ✅ **New (gap A1).** Mesh trade receipts feeding the next cycle's budget: 🔧 gap A4 |
| A6 settlement → adapters | Ledger entries become ISO 20022 and statements | 🧩 Both sides exist; not chained |
| A7 adapters → outside | Wallet UI, gateways, statements | 💭 |
| A8 outside → kernel | Signed updates and model hashes in the app; custodian receipts queued | 🟡 |

**The "ouroboros" closes at gap A4:** fees feed the reserve, the dividend and node bounties, and
bounties feed the next cycle's compute budget.

### Loop B: quality (self-audit feeds the tests)

```text
 code ─► tests, fuzzers, proofs ─► mutation score ─► gap audit ─► fixes ─► code
   ▲                                                                       │
   └─ peer-audit receipts ◄─ signed repair receipts ◄─ forecasts in production
```

Built: tests, tier suites, fuzzers, sanitizers, proofs, mutation gate, gap audit. Queued:
forecasting monitors, designed recovery with receipts, and peer-audit receipts feeding the test corpus.

---

## 5. Stratum 0: shared base

Small, pure building blocks that every stratum may use.

| Part | Status | Limits |
|---|---|---|
| SHA-256, SHA-3/Keccak, AES-GCM, ChaCha20-Poly1305, HKDF, X25519 (`robin_debanks`, `mlkem`, `tls`) | ✅ | Gap B4: three SHA-256 copies to merge into one; vendored Keccak copies stay (pinned to upstream test vectors) |
| Exact rationals (`rational`) | ✅ | 64-bit numerator/denominator with overflow flag; wide values built from 64-bit parts, never 128-bit types. Not arbitrary precision |
| ISF maths (`surplus`) | ✅ | Used by 45 modules |
| Divide by zero | ✅ | Returns an explicit "underdetermined" flag that stops the calculation; never a number |

## 6. Stratum 1: kernel and hardware abstraction

| Part | Status | Limits |
|---|---|---|
| Boot on arm64, x86_64, riscv64, riscv32, arm32 | ✅ | CI fails closed on all five. Emulated hardware only; raw silicon 💭 |
| Zero-float kernel images, widened scheduler pointers, allocator wrap checks | ✅ | |
| Capability-checked syscalls, scheduler, memory | ✅ | |
| zxvfs (journal, extents, Tri-Space store) | ✅ | 256-file limit to lift for model packs |
| WORM journal, retention lock, IPFS anchoring | 📋 | |
| Tri-Space filesystem recovery | 🟡 | Last open HIGH audit item |
| Hardware RNG | 📋 | Mixed into the existing generator, never used raw for keys |
| NIC adapters (POSIX, Winsock, kqueue, virtio-net) | 📋 | |
| Rename of esoteric module names | 📋 | Script ready (44 modules); brands and Enochian data stay |

## 7. Stratum 2: transport

| Part | Status | Limits |
|---|---|---|
| UBH-168 frames; 168-row erasure-coded freight | ✅ / 🧩 | Hashes and IDs stay 256-bit |
| Vinea Kademlia DHT with PQ identities, agreements, file exchange, offline outbox | 🧩 + one node in the app | No NAT traversal, IPv6 or persisted identity yet |
| IPFS node (Kubo-identical CIDs, private pins) | 🧩 + app updater | |
| Endian hopping, harmonic wire | 🧩 | Not called by anything |
| One PQ session layer (bootlegger at ML-KEM-1024 + ML-DSA-87, carried by Vinea) | 📋 gap A3 | Today there are two handshakes |
| Two-laptop demo: handshake, inference A→B, settlement with fee | 📋 phase 2 | CI runs two nodes on one host; you get a script for real laptops |
| Sybil/eclipse defence | 📋 | Per-prefix bucket caps, credibility weighting, quarantine partition |
| Link selection and the transport continuum | 📋 | Section 14 |

## 8. Stratum 3: trust and identity

| Part | Status | Limits |
|---|---|---|
| ML-KEM-768/1024, ML-DSA-65/87, SLH-DSA, HQC-5, PQ matrix | ✅ | Vendored upstream reference code with NIST vectors, not clean-room |
| Signed-transcript handshake; full-manifest update signatures | ✅ | |
| Constant-time check | 🟡 | Valgrind secret-branch test; not a full side-channel audit |
| Identity: passkeys, optional KYC | 🧩 | |
| Recovery: 3-of-5 guardians, device keys under a root anchor, view-only sandbox, time lock, staged thaw | 📋 | Guardians sign approvals and hold no shares; 4-of-5 + 14-day lock overrides a stolen-device veto; dormancy changes nothing; claims rate-limited, every guardian notified |
| Estate succession | 📋 | Only a filed death attestation starts it; living veto refunds the bond; sealed papers use a separate guardian-split key |
| Agent payment cards | 📋 | Inherit their owner's identity and limits; no zero-KYC fiat |

## 9. Stratum 4: logic and fault handling

| Part | Status | Limits |
|---|---|---|
| Four-valued truth (`lpres`: Neither/U, True, False, Both) | ✅ | Reused everywhere; no second enum |
| Peer audit (self-check before signing; peers replay and challenge) | 🧩 | Isolate, never seize funds |
| Double spends | ✅ | First valid spend wins; slashing only on signed equivocation proof, only of posted bonds |
| TLA+/Lean proofs of ledger, headroom and rational code | ✅ | Models of the C, not proof of the binary |
| Forecasting (shadow state, headroom, cycle detection) | 📋 | `bombsquad` and `predictive` exist; `bombsquad` connects to nothing yet |
| Designed recovery framework | 📋 gap B5 | Refuse / roll back / isolate, each with a signed RepairProof |
| Defensive sinks | 📋 | Stateless cookies or small proof-of-work; inbound-only tarpit |
| Four-valued deliberation with deterministic settlement | 📋 | Disputes stay in escrow |
| Exact float-free circuit simulator; quantum-inspired netting optimiser | 📋 | Labelled classical simulator; benchmarked against cycle cancellation |

## 10. Stratum 5: AI runtime and swarm

### Built

| Part | Status | Limits |
|---|---|---|
| Integer tensor engine, GGUF reader, tokenizer, RoPE, E8/Leech quantisers | ✅ | |
| SIMD (NEON, AVX2, AVX-512) + row-parallel thread pool | ✅ | Bit-identical to the plain C path |
| Swarm economy: Fibonacci tokens-per-cycle, emotional economy, cooperative market, governor, DNA | ✅ | |
| Budget gate on generated tokens | ✅ in the app | Prompt tokens not charged |

### Real model results (Qwen2.5-1.5B-Instruct, Q4_K_M, Apache-2.0)

| Measure | Result |
|---|---|
| Accuracy | Perplexity 9.371 vs llama.cpp 9.362 (0.1% worse) on WikiText-2 |
| Answers | Same tokens as llama.cpp on all 8 prompts, all correct |
| Determinism | Bit-identical across C, AVX2, AVX-512 and arm64 NEON. llama.cpp changed 5% of next-token picks when only its batch size changed |
| Speed | ~11 tokens/s vs 15–18; prompt reading ~5× slower. Main thing to improve |
| Defect | First token of a text diverges sharply (KL 4.5); on the fix list |
| Golden-ratio parts | Phi scales: small harm (9.40). Lossless holographic coding: identical output but 8.12 vs 7.31 bits per weight. Lossy holographic decode: destroys the model (perplexity 332,000), so weights always decode all 10 shells |
| Low-bit weights | On MLP output weights, E8 at 1.875 bits: 13.97; plain 4-level (2-bit): 20.5. Full-model runs in progress |

### Queued

| Part | Limits |
|---|---|
| Intent layer: routes, Sutra/copybook drafts with explicit unknowns, anomaly tags | Advisory only; every change goes through `pay_ledger_post`; market inputs must be signed feeds |
| Paraconsistent adapter (μ/λ evidence head → T/F/B/U) | Sits beside softmax, not instead of it. Step 1: cheap CPU probe on Qwen hidden states; step 2 (only if the probe shows signal): LoRA training on GPU outside the engine, exported to integer form; step 3: wire into libzxv. Benefits measured on a held-out contradiction set |
| Grammar-constrained decoding for the shorthands, plus attestation tags | Stops malformed commands, not wrong facts |
| Ternary and quaternary experiments | BitNet b1.58 (MIT) packed 5 trits per byte; 2-bit packing for T/F/B/U states; low-bit weights only work on models trained for them |
| Several models at once, LoRA per agent | 💭 |
| Model-collapse experiment (invariant filter on synthetic training data) | Offered as a fair test; no "cure" claimed |

## 11. Stratum 6: settlement and economy

### Built

| Part | Status | Limits |
|---|---|---|
| Ledger of record (`pay_ledger`) + settlement spine (`settle`) | ✅ in the app | Swarm books reconciled against it every cycle. Nine other balance-keeping modules still to post through it (gap A1 continues) |
| libzxv engine: compress / route / decompress with the fee | ✅ `libzxv-econ` | |
| 0.08889% assurance fee, 50 / 25 / 15 / 10, exact remainders | ✅ | One fee for all; no staker discounts |
| Rails 555 / 777 / 888, class 811; triple ledger; Vino; central-bank toolkit; cards; equity markets; capacity, provider and commerce markets | 🧩 | |
| Multilateral cycle netting | 🧩 in `cbank` | 222M fuzz operations clean |

### Queued (gap list C1–C9 plus later specs)

| Part | Agreed design and limits |
|---|---|
| Dual mode | Fiat netting from day one; V-Bills and Zenny chips switched on per deployment |
| V-Bills | Perpetual musharakah equity, dividend from the fee, redemption at the backing floor. Likely securities |
| **Dutch-auction issuance, backed only** | Descending uniform-price epochs; each epoch issues only what keeps the reserve at or above its floor, under a per-epoch cap; clearing price positive; auction fiat goes to the reserve or back to losing bidders, never burned |
| Custodian receipts | Custodian signs with ML-DSA; per-custodian haircut; signed median price feed with staleness limit; failed audit locks that slice; custody statements as semt.002 |
| **Zenny chips** (user-built baskets) | WORM manifest of weights + V-Bill anchor; minter picks κ and allowed assets; claim equals deposit exactly at mint and every rebalance; κ sets how much one chip holds, never exposure; spending part of a chip is a recorded sale with its cost basis. Trademark check (Capcom) |
| Solvency vault | A third of the reserve share (16.67% of the fee) until it holds 1/3 of V-Bills; never counts toward Vino's floor |
| Mint, Q-Note quarantine, credibility engine, three-level dispute funnel | As decided |
| FX and markets | Uniform-price batch epochs; multilateral non-USD netting; cooling epochs instead of cliff liquidation; leverage off by default |
| Sovereign and co-minted baskets | Each bank signs with its own steward threshold |
| Sicily Account | Assets / Liabilities / Equity, equity computed as A − L |
| Player Cards and organisation programs | Tiers earned, never buy auction priority; founder powers need a steward threshold (3 of 5 + 7-day lock); org rewards pre-funded in escrow |
| IP royalties, contribution grants, giving and investing split | Value only from realised royalties; grants capped by reserve surplus; split opt-in and shown before confirming |
| Estate escrow | Contested shares stay under the deceased's account; exact-fraction calculators per regime |
| Balances | Don't earn by default; only held or opted-in V-Bills earn |

## 12. Stratum 7: adapters and the five language pillars

### Adapters

| Part | Status | Limits |
|---|---|---|
| ISO 20022 (pacs.008, camt.053, CBPR+), schema-validated | ✅ | Format-valid, not connected |
| .NET SDK and ASP.NET gateway; C ABI for COBOL/Fortran/Pascal/Ada | ✅ | |
| Accounting adapters: IFRS, US GAAP, AAOIFI, OHADA, CAS, insurer statutory | 📋 | Designed to follow; tested against published worked examples; export files for SAP/Oracle/QuickBooks cross-checks. AAOIFI blocks interest-like terms; a Shariah board rules |
| Jurisdiction plug-in ABI, CBDC adapter | 📋 | Userland, signed manifests, fail closed |
| PAPSS / RTGS gateways | 📋 | Designed to meet published criteria; you submit to PAPSS yourself |
| Java bindings (Java 22 FFM) | 📋 | |
| Inheritance regimes (common law, forced heirship, fara'id) | 📋 | A court or notary gateway signs |

### The five language pillars

Every pillar pairs a legacy language with a compact AI shorthand. The pattern is the same for all
five (rule R7): the shorthand's parser lives in libzxv, allocates nothing and fails closed with one
reading per directive; the heavy work runs in userland; results come back signed by the operator
as fixed-denominator integers or rationals. No pillar can mint, set κ or touch a balance.

| Pillar | Legacy side | Shorthand | Role | Status | Limits |
|---|---|---|---|---|---|
| Treaty | COBOL copybooks, `CALL 'ZXV_ENGINE'` | **Sutra** | Agreements and settlement records | Copybook parser ✅ (10M fuzz runs); treaties and Sutra 📋 | Sutra rejects weights not summing to exactly 1 and claims above deposit. Tested on big-endian s390x emulation; z/OS itself can't run here |
| Physics | Fortran via `iso_c_binding` | **Trebago** | Commodity flows, large netting proposals | 📋 | Floats stay outside the kernel; replays use the signed value; may only tighten admission; proposed netting cycles re-checked exactly |
| Spatial | Ada/SPARK geodesy | **Eve** | Geofences, corridors, collateral location | 📋 | WGS-84 in integer micro-degrees and millimetres; a geofence hit can only add a hold |
| Time series | APL/K | **KRON** | SCADA and market series | 📋 | Advisory for grid control |
| Clinical | MUMPS/M | **MYRA** | Clinical and genomic trees | 📋 | Advisory; a licensed human acts |

The Hackronomicon book is all rights reserved: used as reference, its text stays out of the
Apache repo unless you relicense it.

## 13. Above the strata: products, apps and people features

| Part | Status | Limits |
|---|---|---|
| Mac (Intel + Apple Silicon), Windows, Linux builds; local API guard; update checker; notifications | ✅ | |
| Single-file `libzxv.c` + `zxv_engine.h`, no malloc in the hot path | 📋 | Same tests as the full tree |
| `zxvd` daemon + JSON-RPC + generated SDKs (Python/TS → JVM/Go → React Native/Flutter) | 📋 phase 3 | |
| Five-view UI: Sicily Ledger & Patron Deck, Local AI Studio, P2P Mesh, Storefront & Escrow, Settings & Jurisdiction Bridge; command-center map with T/F/B/U corridors | 📋 | Mockups to you first |
| UI strings from `i18n` (35k lines, CLDR) | 📋 gap B6 | |
| Calls, streaming, ISF-ranked social feed, speech, translation, game mechanics, forks | 🧩 | |
| Mobile (Android, iOS) | 🟡 | Core builds; apps not in CI |
| Live demo: P2P chat → inference → Vino settlement | 📋 phase 4 | |
| Signing and notarisation | 💭 | Needs your Apple Developer ID |
| **Public release to GitHub and Hugging Face** | 📋 | When ready, and only after your go-ahead: code on GitHub, model files and benchmarks on Hugging Face |

## 14. Cross-cutting layers

### Privacy

| Part | Status | Limits |
|---|---|---|
| Lattice zero-knowledge proofs (e.g. on genomes) | 📋 | Fit the PQ stack |
| Discrete-Gaussian differential privacy | 📋 | Exact, float-free |
| Four-valued access control with minimal emergency release in state Both | 📋 | |
| | | Doesn't protect metadata, query patterns, consent records, key handling or side channels on its own. Designed to support HIPAA/GDPR; not a replacement and not certified |

### Transport continuum (Morse to 5G, satellite and subsea)

| Part | Status | Limits |
|---|---|---|
| Self-contained PQ-signed envelopes with a 32–64 byte core | 📋 | Verify end to end across any carriers |
| Store-and-forward (DTN); dropped links marked U | 📋 | |
| Link selection by measured latency and loss; small settlement header on the fastest link, bulk on the cheapest | 📋 | Compatibility adapters only. We are a customer of Starlink/OneWeb IP service and can't steer their routing. Arriving first isn't finality: both ledgers must sign |
| Software modems (OOK, FSK, GMSK); LoRa/Meshtastic, JS8/VARA, APRS, AREDN | 📋 | **No settlement over amateur bands** (US 47 CFR 97.113): ham links carry only cleartext, signed emergency messages. Per-jurisdiction radio rules table |
| Graceful degradation in the UI | 📋 | |

### Use cases

| Area | How ZXV fits | Limits |
|---|---|---|
| Central banks and cross-border clearing | Netting, ISO 20022, interest-free design | No connection claimed; Shariah board and regulators decide |
| Disaster and emergency (wildfire, flood, evacuation, mutual aid, CME grid protection, remote trauma, pandemics) | Offline-first, contradiction-tolerant; a use-case document after integration | Recommendations an authorised person confirms; forecasts ingested from NOAA/ECMWF/agencies; mutual-aid copybooks encode agreements, seizures need the declared authority's signature |
| Defense | Logistics, medical, mapping, collateral tracking | No weapons targeting |
| AI agents trading | Agent cards under their owner | Owner's identity and limits apply |

## 15. Quality: tests, self-audit, self-healing, the 13:1 target

### Today

| Measure | Value |
|---|---|
| Ratio of test code to protocol code | **0.36:1** (target 13:1, which at today's size is roughly 3.9 million lines) |
| Mutation scores | Settlement spine 89%, engine 86%, quota maths 96%; gated in CI |
| Fuzzing | 13 parser harnesses; 222M netting operations; COBOL copybooks 10M |
| Proofs | TLA+ and Lean models; found two real arithmetic bugs |
| Shipped code in no test (gap B2) | 20.5k lines (`emu`, `net`, `modbind`, `desktop`, `sdk_bridge`, `panopticon` and smaller) |

### How the ratio is counted

Only tests that fail when the code breaks count (proved by the mutation gate). Generated tables,
copied tests and fuzz corpora don't. Archiving dead and duplicate code shrinks the protocol side.
The ratio is reported with mutation score and coverage in every update.

### The 13-tier catalogue (revised order in the build thread)

Exact-rational arithmetic · the four-valued lattice · conservation and zero drift · containment of
contradictions (no explosion) · asymmetric routing costs · cycle netting · overdraft beyond real
balance fails cleanly with no state change · backed-only Dutch auction · custodian default slashes
only posted bonds, loss stays in the basket · bit-identical replay across CPUs · zero heap in the
hot path · COBOL linkage (GnuCOBOL, PIC S9(18) parity) · Sutra grammar rejection · black-swan
partition survival on the seeded simulator · adversarial agents (local models; external model APIs
need keys and network access).

### Self-healing

Designed recovery only (rule R6): refuse, roll back to a checkpoint, or isolate a peer, each with
a signed RepairProof; hard invariants stay fail-closed; never auto-patch state.

## 16. Order of work

1. **Integration pass** (in progress): A1 ✅, A2 ✅, A4 🔧, then A3 one PQ session layer, A5
   connect or archive the 16 uncalled modules, A6 the end-to-end harness on a seeded simulated
   network, and B1–B7 (layering check, untested code, duplicates, self-healing framework, i18n).
2. **Integration phases 2–4:** sockets and the two-laptop demo; `zxvd` and the five-view UI;
   the live demo.
3. **The 13-tier suites** toward 13:1.
4. **Queued specs:** the economy items in section 11, the five language pillars, privacy,
   transport continuum, the AI adapter experiments, then the rename.
5. **Release** to GitHub and Hugging Face after your go-ahead.

This is weeks of steady, verified milestones, each pushed green on GitHub before the next starts.

## 17. Decisions made

| Date (2026-10-10 UTC) | Decision |
|---|---|
| 09 Oct | Apache-2.0 only; Vinea replaces Carracho; rails 555/777/888, class 811 |
| 00:28 | Full rename of esoteric module names, run last |
| 04:04 | Closed clearing default; open batch auction as a per-jurisdiction switch |
| 04:04 | 0.08889% fee replaces the φ% tithe; split 50 / 25 / 15 / 10 |
| 04:18 | 25% goes to the V-Bill dividend; solvency from slashed bonds and salvage |
| 04:20 | Vault funded by a third of the reserve share, capped at 1/3 of V-Bills |
| 04:30 | Founder powers behind a steward threshold |
| 04:58 | Purge binaries from history only after PR #1 merges, with a final check with you |
| 04:58 | Integration phases 1–4 before the economy specs; Qwen2.5-1.5B as base model |
| 05:27 | Guardians approve with signatures, no shares; 3 of 5 |
| 06:25 | One fee for all, no staker discounts |
| 09:07 | V-Bill issuance backed only |
| 09:24 | Test target 13:1, counted via mutation-killing tests |
| 11:40 | Transport: compatibility adapters and link selection only |
| 11:43 | Holographic coding on the device, never on the wire |
| 11:57 | Publish to GitHub and Hugging Face when ready |

## 18. Decisions still open

| Question | Default |
|---|---|
| Commons endowment's share of the fee | Half of regenerative (5%) |
| Demurrage on idle vouchers | Not built |
| Fibonacci-only denominations | Only minted vouchers |
| IPNS or fixed CID for updates | Fixed CID default |
| Dividend only to active users | Not built (changes the split) |
| Custodian default: cover remaining loss from fee income? | No; loss stays in the basket |
| Apple Developer ID | Needed to ship signed |
| Running speed tests on your M3 Max | Offered in the model thread |

Legal questions for you and a lawyer: V-Bills and Zenny chips as securities; operating the fee and
vault; bank or money-transmitter status; inheritance and Shariah rulings; HIPAA/GDPR; PAPSS
submission; "Thunderbird" and "Zenny" trademarks.

## 19. Where spec text and the design differ

| Spec text | Design |
|---|---|
| Guardians hold key shards | Guardians sign approvals; no shares |
| Sinks reflect attacks | Tarpit and isolate only |
| Forecasting silently fixes bugs | Reports; designed recovery with receipts |
| Arbitrary-precision rationals | 64-bit rationals with overflow checks |
| Clean-room PQC | Vendored upstream reference code |
| κ multiplies value or exposure; 1% anchor absorbs a 100× overdraft | κ sets chip size; claim = deposit; overdrafts fail closed |
| FX "liquidity pump" | The fee still applies; savings come from netting |
| Auction burns fiat | Fiat goes to the reserve or back to bidders |
| Fee income covers a custodian default | Not without your decision |
| Library-only escapes regulation | A lawyer decides |
| Zero-KYC agents | Owner's identity and limits apply |
| "Impossible to leak", replaces HIPAA | Protects data, not metadata; designed to support compliance |
| Four million lines of formal verification, DO-178C | 13:1 mutation-killing tests are a quality bar, not a certification |
| Holographic expansion is negentropic | Lossless decode matched bit for bit; same information in more bits |
| Lattice head replaces softmax | Sits beside it |
| Settlement over ham bands; steering satellite paths | Not allowed; not possible as a customer |
| Ada float types, Fortran doubles in settlement | Fixed point; signed results outside the kernel |

## 20. Design analogy: the seven quantum levels

You map the strata onto quantum information theory: vacuum ground state (kernel), entanglement
(transport), topological protection (trust), superposition (four-valued logic), unitary evolution
(AI), measurement (settlement), decoherence (adapters). It is a **design analogy**, like the coil and
field language in the tensor docs. The software runs on classical computers and claims no quantum
physical properties. Where it stretches: freight is erasure coding, not entanglement; lattice crypto
gives secrecy, not error correction; integer maths is deterministic but quantisation still loses
precision. The principle taken from it is rule R2.

## 21. Honest notes

- "Built" means tested in CI. Nothing here has had real users or moved real money, and there has
  been no outside audit.
- The real model runs and is measured, but it is slower than llama.cpp today.
- Standards are followed by design and checked against schemas or published examples; nothing is
  certified or connected.
- Older root documents (ARCHITECTURE_COMPLETE, VOVINA_SHAKINA_*, ARCHITECTURE_VERIFICATION) are
  superseded by this document and SYSTEM_REFERENCE.md.

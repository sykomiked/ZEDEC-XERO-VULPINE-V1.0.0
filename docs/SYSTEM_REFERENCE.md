# ZXV System Reference

ZEDEC XERO VULPINE (ZXV) / VOVINA SHAKINA M5 Axiomatic Kernel.
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC. Apache-2.0.

This document states what every part of the system does, as the code stands on 2026-10-10,
with 56 further modules added on 2026-10-15 from the architecture audit
([ARCHITECTURE_AUDIT_2026-10-15.md](ARCHITECTURE_AUDIT_2026-10-15.md)), checked against merge batch #156. It was
written from a full audit of the source: every module's header and code was read, its tests were
built and run, and what each module really does was checked against what its comments and docs
claim. Where a module is a model, an analogy or a placeholder, its entry says so. Problems found
by the same audit, and what was fixed, are in [AUDIT_REPORT.md](AUDIT_REPORT.md).

## How to read an entry

Each module (a directory under `kernel/src/`, or under `kernel/experimental/` for the unbuilt
placeholders listed in kernel/experimental/README.md) and each platform area has one entry:

- **Status**
  - WORKING (tested in verify-all): built and tested on every CI run by `make -C kernel verify-all`.
  - WORKING (has a test, not in verify-all): has a passing test that CI does not run yet.
  - PARTIAL: some of it works; the gaps are listed.
  - STUB: the interface exists but the work is not implemented.
  - UNUSED/DEAD: no build includes it, and usually it does not compile.
- **What it does**: plain language; real versus model is stated.
- **Main entry points**: the most important functions and types.
- **Tests**, **Used by**, **Gaps**.

"Library" below means a module that is tested on the host but is not yet linked into the Mac app
or a bootable kernel image. Many of the newer modules are libraries in that sense: they work and
are tested, and the wiring that puts them into the running product is listed as an integration gap
in the audit report.

## The system in one page

ZXV is three things that share one source tree.

1. **A kernel and operating system** (`kernel/`, `build_system/`). It boots on emulated hardware:
   all five images (arm64, x86_64, riscv64, riscv32, arm32) reach `[BOOT_OK]` under QEMU, and CI
   fails any of them that does not or that prints a fault marker. Drivers (ATA disk, PS/2, PCI, virtio, FAT32,
   framebuffer), a scheduler, memory management, a syscall table with capability checks, a
   journalled filesystem (`zxvfs`), signed packages and A/B updates (`loader`, `zxpkg`).
2. **A local AI swarm** (`swarm`, `tensor`, `chiglet`, `surplus`, `cotier`). An integer-only
   tensor engine loads GGUF models (F32, F16, BF16, Q8_0, Q4_0, Q5_0, Q5_1, Q4_K and Q6_K), tokenises, applies RoPE and
   runs a transformer forward pass bit-identically on every CPU. The swarm runs several models on a
   Fibonacci tokens-per-cycle budget, an emoji "emotional economy" on the imaginary axis, and a
   cooperative free market over the nine forms of capital with no-monopoly caps. The Interaction
   Surplus Framework maths (`surplus`) scores novelty and controls the budget.
3. **An economic and communications network** built from libraries:
   - money: the payment ledger and 0.08889% assurance fee (`pay`), the central-bank toolkit with ISO 4217 rails
     555/777/888 and ISO 20022 messages (`cbank`, `iso20022`), the triple ledger (`finance`),
     Vino floating vouchers (`vino`, `vino_stores`), Phoenix/Dragon/Thunderbird charge cards
     (`cardnet`), compute and capacity markets (`capmkt`, `provider`, `zcapital`), commerce
     (`market`), no-interest rules everywhere. None of it is connected to PAPSS, SWIFT or a card
     scheme; it builds and checks the messages those networks use.
   - networking: Vinea, a serverless Kademlia overlay with post-quantum identity (`vinea`), a real
     IPFS-compatible node (`ipfs_node`), UBH-168 framing (`ubh`, `freight`), endian-hopping private
     channels (`ehop`), the harmonic wire (`harmonic`), calls (`call`), swarm streaming (`stream`),
     personal device meshes (`devmesh`), Web2/Web3/Web4 bridges (`web4`, `bridge`), legacy
     telephony and mainframe bridges (`legacy`, `lightningrod`, `orbital_compat`).
   - identity and security: ML-KEM, ML-DSA, SLH-DSA and HQC post-quantum crypto with NIST test
     vectors (`mlkem`, `pqsec`), optional KYC and guardian recovery (`ident`), TLS 1.3 pieces
     (`tls`), port seals (`porter_house`).
   - people: social feed and notifications (`social`, `concord`), game mechanics (`quest`),
     speech and translation (`speech`, `xlate`, `i18n` with every CLDR locale and Enochian at the core),
     versionless forks (`evolve`).

The product you run today is the **Mac desktop app** (`kernel/arch/hosted`, `macos/`): the swarm
and tensor engine as a native program with a local web UI, built for Apple Silicon and Intel,
Linux and Windows. It now generates replies through the tensor engine, limited by the
companion's tokens-per-cycle budget. It links a Vinea node over UDP (off unless the user turns
networking on; LAN or online) with LAN multicast discovery, the IPFS node (used by AIPI and the
update checker), the update checker (on request, or daily while online) and the notification bus
with an OS bridge. Speech, IPFS file exchange for the user, and the social, call and stream stacks
are not linked into the desktop app; social, call, xlate and devmesh are linked into the headless
daemon `zxvd` (its `people.*` methods, `build_zxvd.sh`).

## Status summary

Statuses are as found by the audit. The tests the audit added or repaired (for vino, fat32, the finance core, crypto_wallet, rmag budgets, recon, predictive, the TLS handshake, xedit, tolvovina, virtio, curzi, dharana, sephirot, the ML-KEM validators, the emulators and the OS-layer suite) now also run in verify-all, so several modules marked "test not in verify-all" are now covered.

| Status | Modules |
|---|---|
| WORKING (tested in verify-all) | 200 |
| PARTIAL | 37 |
| WORKING (test not in verify-all) | 17 |
| UNUSED/DEAD | 11 |
| STUB | 3 |
| **Total** | **268** |

Nine more modules are on draft pull requests and listed in Part C, not counted here.

| Module | What it is | Status |
|---|---|---|
| [abacus](#abacus----smaugs-debt-netting-abacus-multilateral-clearing-of-ious) | Smaug's debt-netting abacus (multilateral clearing of IOUs) | WORKING (tested in verify-all) |
| [abstraction_layer](#abstraction_layer----multi-language-module-registry-dead) | multi-language module registry (dead) | UNUSED/DEAD |
| [acct](#acct--accounting-adapters-for-ifrs-us-gaap-and-aaoifi) | accounting adapters for IFRS, US GAAP and AAOIFI | WORKING (tested in verify-all) |
| [acpi](#acpi----acpi-table-finder-x86) | ACPI table finder (x86) | PARTIAL |
| [ai_layer](#ai_layer----bookkeeping-for-ai-models-tasks-and-remote-compute-peers) | bookkeeping for AI models, tasks and remote compute peers | WORKING (tested in verify-all) |
| [aipi](#aipi--content-addressed-ai-functions-over-the-ipfs-node-and-vinea) | content-addressed AI functions over the IPFS node and Vinea | WORKING (tested in verify-all) |
| [airc](#airc--agent-relay-chat-signed-boards-trust-tiers-and-the-install-decision) | agent relay chat: signed boards, trust tiers and the install decision | WORKING (tested in verify-all) |
| [alloc](#alloc----shared-resource-pool-and-personal-token-ledger) | shared resource pool and personal-token ledger | WORKING (tested in verify-all) |
| [app_constellation](#app_constellation----service-deployment-manager-header-only) | service deployment manager (header only) | STUB |
| [app_fabric](#app_fabric----application-descriptor-and-instance-registry-dead) | application descriptor and instance registry (dead) | UNUSED/DEAD |
| [appkit](#appkit----text-document-model-for-native-apps-gap-buffer-undo-save) | text document model for native apps (gap buffer, undo, save) | WORKING (tested in verify-all) |
| [apps](#apps----built-in-shell-editor-network-config-and-other-desktop-apps) | built-in shell, editor, network config and other desktop apps | PARTIAL |
| [aproto](#aproto--protocol-twins-aftp-file-transfer-the-aham-ham-frame-and-the-audit-chain) | protocol twins: AFTP file transfer, the AHAM ham frame and the audit chain | WORKING (tested in verify-all) |
| [art_studio](#art_studio----pixel-canvas-and-painting-tools) | pixel canvas and painting tools | WORKING (tested in verify-all) |
| [ascent](#ascent----ladder-of-ascent-security-practice-checklist-model) | "Ladder of Ascent" security-practice checklist model | WORKING (test not in verify-all) |
| [ata](#ata----ataide-disk-driver-pio-x86) | ATA/IDE disk driver (PIO, x86) | PARTIAL |
| [audio](#audio----audio-streams-and-software-mixer) | audio streams and software mixer | WORKING (tested in verify-all) |
| [audiogenomics_pro](#audiogenomics_pro----dna-to-sound-synthesis-and-symbolic-encodings) | DNA-to-sound synthesis and symbolic encodings | WORKING (test not in verify-all) |
| [axiom_matrix](#axiom_matrix----sparse-complex-valued-matrix-keyed-by-hash) | sparse complex-valued matrix keyed by hash | WORKING (test not in verify-all) |
| [battering_ram](#battering_ram----pooled-credit-exchange-that-pays-out-on-a-verified-outcome) | pooled-credit exchange that pays out on a verified outcome | WORKING (tested in verify-all) |
| [bearer](#bearer--link-table-framing-and-policy-gates-for-ble-lora-serial-modem-and-ham-radio) | link table, framing and policy gates for BLE, LoRa, serial, modem and ham radio | WORKING (tested in verify-all) |
| [bios](#bios----zbios-staged-signed-boot-chain-model) | ZBIOS staged, signed boot-chain model | PARTIAL |
| [blockdev](#blockdev----block-device-interface-and-ram-disk) | block device interface and RAM disk | PARTIAL |
| [bluetooth](#bluetooth----bluetooth-hcil2cap-packet-builders-and-parsers) | Bluetooth HCI/L2CAP packet builders and parsers | WORKING (tested in verify-all) |
| [bombsquad](#bombsquad----early-warning-monitor-for-invariants-that-are-drifting-toward-failure) | early-warning monitor for invariants that are drifting toward failure | WORKING (tested in verify-all) |
| [bootattest](#bootattest--measured-boot-event-log-signed-quote-and-golden-manifest) | measured-boot event log, signed quote and golden manifest | WORKING (tested in verify-all) |
| [bootfeat](#bootfeat----platform-layer-bring-up-at-boot) | platform-layer bring-up at boot | PARTIAL |
| [bootlegger](#bootlegger----peer-handshake-and-post-quantum-key-exchange) | peer handshake and post-quantum key exchange | WORKING (tested in verify-all) |
| [bridge](#bridge----one-address-resolver-across-web2-web3-and-web4) | one address resolver across Web2, Web3 and Web4 | WORKING (tested in verify-all) |
| [bringup](#bringup----shared-link-symbols-for-the-non-arm64-kernels) | shared link symbols for the non-arm64 kernels | PARTIAL |
| [broker](#broker----peer-to-peer-marketplace-for-content-addressed-information-goods) | peer-to-peer marketplace for content-addressed information goods | WORKING (tested in verify-all) |
| [browser](#browser----minimal-web-browser-engine-url-http-html-tokenizer-layout) | minimal web browser engine (URL, HTTP, HTML tokenizer, layout) | WORKING (tested in verify-all) |
| [call](#call----voice-and-video-call-stack-rtp-stun-fec-jitter-buffer-congestion-control) | voice and video call stack (RTP, STUN, FEC, jitter buffer, congestion control) | WORKING (tested in verify-all) |
| [capctl](#capctl--capital-control-profiles-as-signed-plug-ins-off-by-default) | capital-control profiles as signed plug-ins (off by default) | WORKING (tested in verify-all) |
| [capmkt](#capmkt----market-for-buying-compute-storage-or-bandwidth-from-other-machines) | market for buying compute, storage or bandwidth from other machines | WORKING (tested in verify-all) |
| [cardnet](#cardnet----charge-card-network-model-pans-iso-8583-emv-tlv-mobile-push) | charge-card network model (PANs, ISO 8583, EMV TLV, mobile push) | WORKING (tested in verify-all) |
| [cards](#cards----glyphsigil-cards-loadouts-and-the-zca-card-assembler) | glyph/sigil cards, loadouts and the ZCA card assembler | WORKING (tested in verify-all) |
| [cbank](#cbank----central-bank-toolkit-iso-4217-currencies-settlement-net-usury-iso-20022-mx) | central-bank toolkit (ISO 4217 currencies, settlement net, usury, ISO 20022 MX) | WORKING (tested in verify-all) |
| [cellular_multikernel](#cellular_multikernel----fault-isolated-kernel-cells) | fault-isolated kernel "cells" | WORKING (tested in verify-all) |
| [chiglet](#chiglet----small-on-device-ai-built-from-a-mixture-of-tiny-linear-experts) | small on-device "AI" built from a mixture of tiny linear experts | WORKING (tested in verify-all) |
| [choice](#choice----deterministic-tie-breaking-selection-among-candidates-k5) | deterministic tie-breaking selection among candidates (K5) | WORKING (tested in verify-all) |
| [chronicle](#chronicle----hash-chained-ledger-of-verdicts-that-re-judges-deferred-events) | hash-chained ledger of verdicts that re-judges deferred events | WORKING (tested in verify-all) |
| [civilizational_stack](#civilizational_stack----application-deployment-and-translation-stack-dead) | application deployment and translation "stack" (dead) | UNUSED/DEAD |
| [clock](#clock----event-driven-clock-ordinals-with-wall-clock-sync-only-on-demand) | event-driven clock (ordinals, with wall-clock sync only on demand) | WORKING (tested in verify-all) |
| [codec](#codec----tri-space-media-codec-that-keeps-the-lossy-remainder) | "Tri-Space" media codec that keeps the lossy remainder | WORKING (tested in verify-all) |
| [community_chest](#community_chest----peer-to-peer-app-store-with-paid-apps-and-vouchers) | peer-to-peer app store with paid apps and vouchers | WORKING (tested in verify-all) |
| [compute_fabric](#compute_fabric----compute-cell-manager-with-domain-migration-dead) | compute-cell manager with domain migration (dead) | UNUSED/DEAD |
| [concord](#concord----social-matching-layer-that-avoids-engagement-maximising-feeds) | social matching layer that avoids engagement-maximising feeds | WORKING (tested in verify-all) |
| [constellation](#constellation----coordinator-for-many-machines-node-registry-routing-failure-detection) | coordinator for many machines (node registry, routing, failure detection) | WORKING (tested in verify-all) |
| [cotier](#cotier----co-processor-tiers-accelerator-gate-budget-measurement-reconcile) | co-processor tiers (accelerator gate, budget, measurement, reconcile) | WORKING (tested in verify-all) |
| [count_house](#count_house----stash-bucket-reserves-and-the-floor-price-of-a-nodes-own-currency) | stash-bucket reserves and the floor price of a node's own currency | WORKING (tested in verify-all) |
| [crash](#crash--crash-records-on-disk-and-boot-modes-normal-safe-recovery) | crash records on disk and boot modes (normal, safe, recovery) | WORKING (tested in verify-all) |
| [cred](#cred--eigentrust-credibility-from-fulfilled-trades-bounded-unlocks-and-the-player-card) | EigenTrust credibility from fulfilled trades, bounded unlocks and the Player Card | WORKING (tested in verify-all) |
| [crit168](#crit168----canonical-serialisation-and-integrity-framing-ubh-168) | canonical serialisation and integrity framing (UBH-168) | WORKING (tested in verify-all) |
| [crown](#crown----credential-office-issues-and-verifies-identity-credentials-never-touches-money) | credential office: issues and verifies identity credentials, never touches money | WORKING (tested in verify-all) |
| [crypto_wallet](#crypto_wallet----five-key-wallet-file-system-derived-keys-maced-files) | five-key wallet file system (derived keys, MACed files) | WORKING (test not in verify-all) |
| [curzi](#curzi----curzi-8889-a-composite-post-quantum-key-setup-with-tiered-access) | CURZI-8889-A composite post-quantum key setup with tiered access | WORKING (test not in verify-all) |
| [decent](#decent----decentralised-building-blocks-ipfs-style-cids-swarms-encrypted-rooms) | decentralised building blocks (IPFS-style CIDs, swarms, encrypted rooms) | WORKING (tested in verify-all) |
| [denconnect](#denconnect----operator-defined-privileges-for-den-connect-servers) | operator-defined privileges for Den Connect servers | WORKING (tested in verify-all) |
| [deploy](#deploy----sizes-the-os-to-the-machine-it-runs-on) | sizes the OS to the machine it runs on | WORKING (tested in verify-all) |
| [desktop](#desktop----window-manager-taskbar-and-shell-ui) | window manager, taskbar and shell UI | WORKING (tested in verify-all) |
| [devmesh](#devmesh----private-mesh-of-one-persons-own-devices) | private mesh of one person's own devices | WORKING (tested in verify-all) |
| [dharana](#dharana----array-of-112-gates-in-four-classes-symbolic-naming) | array of 112 gates in four classes (symbolic naming) | WORKING (test not in verify-all) |
| [dharma](#dharma--dharma-scheduler-family-dharma-karma-chakra-tantra-naga_raja-upaah-) | Dharma scheduler family (dharma, karma, chakra, tantra, naga_raja, upaah, ...) | WORKING (test not in verify-all) |
| [display](#display--display-mode-negotiation-and-ui-scaling) | display mode negotiation and UI scaling | WORKING (tested in verify-all) |
| [dispute](#dispute--three-level-dispute-funnel-proofs-staked-blind-jury-with-appeals-jurisdiction-gateway) | three-level dispute funnel: proofs, staked blind jury with appeals, jurisdiction gateway | WORKING (tested in verify-all) |
| [dual_space](#dual_space--paired-positivenegative-artifact-registry) | paired positive/negative artifact registry | WORKING (tested in verify-all) |
| [dualtrack](#dualtrack--dual-track-design-vs-evidence-gate-pipeline) | dual-track (design vs. evidence) gate pipeline | PARTIAL |
| [e8](#e8--e8-lattice--icosian-ring-over-zphi) | E8 lattice / icosian ring over Z[phi] | WORKING (tested in verify-all) |
| [edgeclus](#edgeclus--edge-cluster-scheduler-on-the-swarm-token-budget) | edge cluster scheduler on the swarm token budget | WORKING (tested in verify-all) |
| [edp_risk](#edp_risk--coverageedp-risk-primitives) | coverage/"EDP" risk primitives | WORKING (tested in verify-all) |
| [ehop](#ehop--endian-hopping-encrypted-frame-transport) | endian-hopping encrypted frame transport | WORKING (tested in verify-all) |
| [el0_userspace](#el0_userspace--el0-arm64-user-mode-process-scheduler) | EL0 (ARM64 user mode) process scheduler | WORKING (test not in verify-all) |
| [emu](#emu--console-emulators-and-game-dimension-layers) | console emulators and game "dimension" layers | PARTIAL |
| [epu](#epu--emotion-processing-unit-device-model) | "Emotion Processing Unit" device model | WORKING (tested in verify-all) |
| [estate](#estate--estate-succession-account-recovery-and-faraid-shares) | estate succession, account recovery and faraid shares | WORKING (tested in verify-all) |
| [event_sched](#event_sched--event-driven-task-scheduler-with-cycle-budgets) | event-driven task scheduler with cycle budgets | WORKING (tested in verify-all) |
| [event_space](#event_space--event-envelopes-sequencer-self-audit-self-healing) | event envelopes, sequencer, self-audit, self-healing | WORKING (tested in verify-all) |
| [event_transport](#event_transport--channels-that-carry-event-envelopes-between-nodes) | channels that carry event envelopes between nodes | STUB |
| [evolve](#evolve--content-addressed-evolution-protocol-capabilities-lineage-trust-updates) | content-addressed evolution protocol (capabilities, lineage, trust, updates) | WORKING (tested in verify-all) |
| [fat32](#fat32--read-only-fat32-driver) | read-only FAT32 driver | WORKING (test not in verify-all) |
| [finance](#finance--triple-ledger-payment-rails-bridge-records-derivatives-assurance-treaty-assets) | triple ledger, payment rails, bridge records, derivatives, assurance, treaty assets | PARTIAL |
| [finance_markets](#finance_markets--instrument-tracker-and-position-valuation) | instrument tracker and position valuation | WORKING (tested in verify-all) |
| [financial_fabric](#financial_fabric--umbrella-over-finance-modules-accounts-vouchers-contracts-assets) | umbrella over finance modules (accounts, vouchers, contracts, assets) | WORKING (tested in verify-all) |
| [font](#font--bitmap-font-registry-truetype-parserrasteriser-script-itemiser) | bitmap font registry, TrueType parser/rasteriser, script itemiser | WORKING (tested in verify-all) |
| [fractal](#fractal--z-order-morton-spatial-addressing) | Z-order (Morton) spatial addressing | WORKING (tested in verify-all) |
| [freight](#freight--reed-solomon--gf256-erasure-coded-freight-containers) | Reed-Solomon / GF(256) erasure-coded freight containers | WORKING (tested in verify-all) |
| [fs](#fs--tri-space-file-system-s-s--s0-replicas-transactions-snapshots) | tri-space file system (S+, S-, S0 replicas, transactions, snapshots) | PARTIAL |
| [fusion](#fusion--capability-contract-fusion-of-tri-space-programs) | capability-contract fusion of Tri-Space programs | WORKING (tested in verify-all) |
| [fwadapt](#fwadapt--smart-firmware-adapter-which-driver-may-touch-a-device) | smart firmware adapter: which driver may touch a device | WORKING (tested in verify-all) |
| [fxepoch](#fxepoch--fx-by-uniform-price-batch-epochs-on-pay_ledger) | FX by uniform-price batch epochs on pay_ledger | WORKING (tested in verify-all) |
| [games](#games--deterministic-lockstep-game-core-with-a-chiglet-companion) | deterministic lockstep game core with a Chiglet companion | WORKING (tested in verify-all) |
| [gdt](#gdt--x86-global-descriptor-table-setup) | x86 Global Descriptor Table setup | PARTIAL |
| [gematria](#gematria--letter-value-and-grammar-to-fock-space-mapping) | letter-value and grammar-to-"Fock-space" mapping | PARTIAL |
| [governance_fabric](#governance_fabric--umbrella-over-legalconcordministrycrownzab-governance) | umbrella over legal/concord/ministry/crown/ZAB governance | UNUSED/DEAD |
| [grants](#grants--olive-oil-barrel-startup-grants-for-participants-in-good-standing) | Olive Oil Barrel startup grants for participants in good standing | WORKING (tested in verify-all) |
| [gridlock](#gridlock--exact-gridlock-resolution-for-a-queue-of-payments) | exact gridlock resolution for a queue of payments | WORKING (tested in verify-all) |
| [hardware](#hardware--dlp-projector-model-and-rtlhdl-device-abstraction) | DLP projector model and RTL/HDL device abstraction | PARTIAL |
| [harmonic](#harmonic--harmonic-wire-tags-dialgeom-routers-endian-mux-trunk-bank-adapters) | harmonic wire tags, dial/geom routers, endian mux, trunk bank, adapters | WORKING (tested in verify-all) |
| [hdcm](#hdcm--hyperdimensional-cross-language-construct-matrix) | "hyperdimensional" cross-language construct matrix | PARTIAL |
| [holodeck](#holodeck--content-addressed-viewing-swarm-inside-a-den) | content-addressed viewing swarm inside a den | WORKING (tested in verify-all) |
| [holographic](#holographic--36n99n6336m9zedeizedec-holographic-file-formats) | .36n9/.9n63/.36m9/.zedei/.zedec holographic file formats | WORKING (tested in verify-all) |
| [hwdrv](#hwdrv--standard-class-drivers-nvme-ahci-xhci-with-usb-keyboard-and-storage-e1000-igb-r8169-hda) | standard class drivers: NVMe, AHCI, xHCI with USB keyboard and storage, e1000, igb, r8169, HDA | WORKING (tested in verify-all) |
| [hwemu](#hwemu--hardware-profiles-the-emulation-matrix-standard-engines-and-signed-hardware-updates) | hardware profiles, the emulation matrix, standard engines and signed hardware updates | WORKING (tested in verify-all) |
| [hwkey](#hwkey--account-keys-sealed-by-the-tpm-or-secure-enclave) | account keys sealed by the TPM or Secure Enclave | WORKING (tested in verify-all) |
| [hypercube](#hypercube--n-dimensional-workspace-scene-projected-to-3d2d) | N-dimensional workspace scene projected to 3D/2D | WORKING (tested in verify-all) |
| [i18n](#i18n--locales-message-catalogue-formatting-enochian-script) | locales, message catalogue, formatting, Enochian script | WORKING (tested in verify-all) |
| [icon](#icon----vector-icons-for-every-module) | vector icons for every module | WORKING (tested in verify-all) |
| [ident](#ident----optional-kyc-credentials-post-quantum-passkeys-account-recovery) | optional KYC credentials, post-quantum passkeys, account recovery | WORKING (tested in verify-all) |
| [identity](#identity----national-id-registry-model) | national-ID registry model | PARTIAL |
| [identity_fabric](#identity_fabric----compound-identitygovernance-umbrella) | "compound" identity/governance umbrella | UNUSED/DEAD |
| [idt](#idt----x86-32-bit-interrupt-descriptor-table) | x86 32-bit interrupt descriptor table | PARTIAL |
| [immigration](#immigration----signed-admission-control-for-daemons) | signed admission control for daemons | WORKING (tested in verify-all) |
| [interspace](#interspace----lex-rhodia-commons-general-average-salvage-federation) | "Lex Rhodia" commons: general average, salvage, federation | WORKING (tested in verify-all) |
| [invproof](#invproof----checkable-exact-inverse-witnesses) | checkable "exact inverse" witnesses | WORKING (tested in verify-all) |
| [ipfs](#ipfs----minimal-content-addressed-get-sha-256-cid) | minimal content-addressed get (SHA-256 "CID") | WORKING (tested in verify-all) |
| [ipfs_node](#ipfs_node----real-ipfs-cids-unixfs-car-blockstore-ubh-168-wire) | real IPFS CIDs, UnixFS, CAR, blockstore, UBH-168 wire | WORKING (tested in verify-all) |
| [iphase](#iphase----k4-route-contracts-and-phase-routing) | K4 route contracts and phase routing | WORKING (tested in verify-all) |
| [iso20022](#iso20022----pacs008--camt053-message-builders) | PACS.008 / CAMT.053 message builders | WORKING (tested in verify-all) |
| [juris](#juris--jurisdiction-plug-in-abi-and-its-fail-closed-host) | jurisdiction plug-in ABI and its fail-closed host | WORKING (tested in verify-all) |
| [keyboard](#keyboard----ps2-keyboard-driver-x86) | PS/2 keyboard driver (x86) | PARTIAL |
| [kycwrap](#kycwrap--minimal-disclosure-kyc-wrapper-with-an-empty-zero-knowledge-slot) | minimal-disclosure KYC wrapper with an empty zero-knowledge slot | WORKING (tested in verify-all) |
| [lattice](#lattice----lattice-p2p-file-sharing-tables) | "LATTICE-P2P" file-sharing tables | PARTIAL |
| [launch](#launch--app-launch-screen-sequencing-and-launch-sounds) | app launch screen sequencing and launch sounds | WORKING (tested in verify-all) |
| [legacy](#legacy----sipsdp-ss7m3uasccptcapmap-isup-map-dtmf-ebcdic-cobol-fortran) | SIP/SDP, SS7/M3UA/SCCP/TCAP/MAP, ISUP map, DTMF, EBCDIC, COBOL, Fortran | WORKING (tested in verify-all) |
| [legal_engine](#legal_engine----legal-document-generator) | legal document generator | WORKING (tested in verify-all) |
| [license](#license----licence-text-and-metadata) | licence text and metadata | WORKING (tested in verify-all) |
| [lightningrod](#lightningrod----cobolfortranc-data-representation-adapters) | COBOL/Fortran/C data-representation adapters | WORKING (tested in verify-all) |
| [lingua](#lingua--fortran-basic-and-forth-front-ends-on-one-checked-integer-ir) | FORTRAN, BASIC and Forth front-ends on one checked integer IR | WORKING (tested in verify-all) |
| [loader](#loader----elf64-loader-signed-packages-zsp-v1v2-ab-updates) | ELF64 loader, signed packages (ZSP v1/v2), A/B updates | WORKING (tested in verify-all) |
| [logistics](#logistics----syndicates-escrow-and-secondary-contracts) | syndicates, escrow and secondary contracts | WORKING (tested in verify-all) |
| [lpres](#lpres----k3-paraconsistent-presence-attestations) | K3 paraconsistent presence attestations | WORKING (tested in verify-all) |
| [macgyver](#macgyver----tri-space-build-registry-mcg0) | tri-space build registry (MCG0) | WORKING (tested in verify-all) |
| [mage](#mage----role-and-authorisation-framework-hats) | role and authorisation framework ("hats") | WORKING (tested in verify-all) |
| [market](#market----p2p-commerce-state-machine) | P2P commerce state machine | WORKING (tested in verify-all) |
| [mbcomp](#mbcomp----component-circuit-element-vocabulary) | component (circuit-element) vocabulary | PARTIAL |
| [media_fabric](#media_fabric----compound-media-umbrella) | "compound" media umbrella | UNUSED/DEAD |
| [megarom](#megarom----observed-game-dynamics---sutra-schemas) | observed game dynamics -> Sutra schemas | WORKING (tested in verify-all) |
| [mesh_net](#mesh_net----mesh-network-and-trade-route-bookkeeping) | mesh network and trade-route bookkeeping | WORKING (tested in verify-all) |
| [mesh_token](#mesh_token----settlement-queue-gated-by-porter-house) | settlement queue gated by Porter House | WORKING (tested in verify-all) |
| [mfg](#mfg--manufacturing-checks-g-code-toolpaths-guard-banded-inspection-resource-budgets) | manufacturing checks: G-code toolpaths, guard-banded inspection, resource budgets | WORKING (tested in verify-all) |
| [ministry](#ministry----treasury-pillar-tribute-gratuity) | treasury pillar (tribute, gratuity) | WORKING (tested in verify-all) |
| [mint](#mint--minting-vino-or-v-bills-against-signed-capacity-claims) | minting Vino or V-Bills against signed capacity claims | WORKING (tested in verify-all) |
| [mixmat](#mixmat----exact-rational-mixing-matrices) | exact rational mixing matrices | WORKING (test not in verify-all) |
| [mlkem](#mlkem----ml-kem-768-fips-203-and-sha-3shake) | ML-KEM-768 (FIPS 203) and SHA-3/SHAKE | WORKING (tested in verify-all) |
| [mm](#mm----x86-pagingheap-model) | x86 paging/heap model | PARTIAL |
| [modbind](#modbind----module-declaration-graph-and-boot-gate) | module declaration graph and boot gate | WORKING (test not in verify-all) |
| [mouse](#mouse----ps2-mouse-driver-x86) | PS/2 mouse driver (x86) | PARTIAL |
| [net](#net----tcpip-stack-m5-router-radiosdr-models-smart-adapters) | TCP/IP stack, M5 router, radio/SDR models, smart adapters | WORKING (tested in verify-all) |
| [network_fabric](#network_fabric----compound-network-umbrella) | "compound" network umbrella | UNUSED/DEAD |
| [nlb](#nlb----nonlinear-build-session-model) | "nonlinear build" session model | PARTIAL |
| [onepolicy](#onepolicy----the-one-policy-symbiotic-term-predicate) | The One Policy (symbiotic-term predicate) | WORKING (tested in verify-all) |
| [orbital_compat](#orbital_compat----one-ir-for-cobolfortrancsutra-values) | one IR for COBOL/Fortran/C/Sutra/... values | WORKING (tested in verify-all) |
| [orbital_elevator](#orbital_elevator----event-schema-translation) | event schema translation | WORKING (tested in verify-all) |
| [orbital_fabric](#orbital_fabric----compound-orbital-layer) | "compound" orbital layer | PARTIAL |
| [oseq](#oseq----k1-ordinal-sequencer) | K1 ordinal sequencer | WORKING (tested in verify-all) |
| [outbox](#outbox--offline-spending-grants-with-once-only-settlement) | offline spending grants with once-only settlement | WORKING (tested in verify-all) |
| [p2p_caracho](#p2p_caracho----superseded-p2p-transport) | superseded P2P transport | UNUSED/DEAD |
| [panopticon](#panopticon----connection-watcher-and-vpn-mesh-model) | connection watcher and "VPN mesh" model | PARTIAL |
| [paradox](#paradox--paradox-engine-paraconsistent-ledger-and-belnap-agents) | paradox engine, paraconsistent ledger and Belnap agents | WORKING (tested in verify-all) |
| [pay](#pay----payment-ledger-assurance-fee-iso-20022-equity-treasury-farm) | payment ledger, assurance fee, ISO 20022, equity, treasury, farm | WORKING (tested in verify-all) |
| [pce](#pce--paraconsistent-decision-engines-probability-tactics-strategy-finance) | paraconsistent decision engines: probability, tactics, strategy, finance | WORKING (tested in verify-all) |
| [pci](#pci----pci-enumeration-x86) | PCI enumeration (x86) | PARTIAL |
| [peer_audit](#peer_audit----self-audit-and-peer-audit-for-swarm-transitions) | self-audit and peer audit for swarm transitions | WORKING (tested in verify-all) |
| [phase_coord](#phase_coord----k6-phase-coordinator) | K6 phase coordinator | WORKING (tested in verify-all) |
| [pic](#pic----8259-pic-x86) | 8259 PIC (x86) | PARTIAL |
| [pirate_apps](#pirate_apps----thin-uis-over-other-modules) | thin UIs over other modules | WORKING (tested in verify-all) |
| [pirate_fleet](#pirate_fleet----crew-channels-charter-dao-compute-credit) | crew channels, charter DAO, compute credit | WORKING (tested in verify-all) |
| [plnp](#plnp----phase-lattice-framing-protocol) | Phase-Lattice framing protocol | WORKING (tested in verify-all) |
| [pmm](#pmm--four-state-physical-frame-allocator) | four-state physical frame allocator | WORKING (tested in verify-all) |
| [policy](#policy--zxv_policy-engine-and-contract-dsl) | zxv_policy engine and contract DSL | WORKING (tested in verify-all) |
| [porter_house](#porter_house--port-firewall-seals-on-network-ports) | port firewall ("seals" on network ports) | WORKING (tested in verify-all) |
| [power](#power--energy-ledger-per-app-and-agent-battery-state-and-budgets) | energy ledger per app and agent, battery state and budgets | WORKING (tested in verify-all) |
| [pqsec](#pqsec--post-quantum-signatures-and-kem-ml-dsa-slh-dsa-ml-kem-hqc) | post-quantum signatures and KEM (ML-DSA, SLH-DSA, ML-KEM, HQC) | WORKING (tested in verify-all) |
| [predictive](#predictive--surplus-trajectory-forecaster) | surplus trajectory forecaster | WORKING (test not in verify-all) |
| [prism_break](#prism_break--framebuffer-prism-refraction-shader) | framebuffer "prism" refraction shader | WORKING (tested in verify-all) |
| [privacy](#privacy--exact-differential-privacy-four-valued-access-checks-and-selective-disclosure) | exact differential privacy, four-valued access checks and selective disclosure | WORKING (tested in verify-all) |
| [provenance](#provenance--canonical-signed-manifest-digest-and-a-test-only-signer) | canonical signed-manifest digest and a test-only signer | WORKING (tested in verify-all) |
| [provider](#provider--computestoragemodel-provider-marketplace) | compute/storage/model provider marketplace | WORKING (tested in verify-all) |
| [pterm](#pterm--framebufferserial-terminal-and-multiplexer) | framebuffer/serial terminal and multiplexer | WORKING (tested in verify-all) |
| [pungent](#pungent--garlic-style-layered-encryption-bundle-model) | garlic-style layered encryption bundle model | WORKING (tested in verify-all) |
| [qeq](#qeq--exact-equivalence-checker-for-small-quantum-circuits) | exact equivalence checker for small quantum circuits | WORKING (tested in verify-all) |
| [qnet](#qnet--obligation-netting-by-cycle-cancellation) | obligation netting by cycle cancellation | WORKING (tested in verify-all) |
| [qnote](#qnote--q-note-quarantine-lots-dutch-auction-and-first-loss-recovery) | Q-Note quarantine lots, Dutch auction and first-loss recovery | WORKING (tested in verify-all) |
| [qr](#qr--qr-code-encoder-byte-mode-level-m) | QR Code encoder (byte mode, level M) | WORKING (tested in verify-all) |
| [qsim](#qsim--exact-classical-quantum-circuit-simulator) | exact classical quantum circuit simulator | WORKING (tested in verify-all) |
| [quantum](#quantum--quantum-and-exotic-matter-device-numeric-model) | "quantum and exotic matter device" numeric model | WORKING (tested in verify-all) |
| [qubo](#qubo--qubo-and-ising-workbench-with-optimality-certificates) | QUBO and Ising workbench with optimality certificates | WORKING (tested in verify-all) |
| [quest](#quest--masterycooperation-game-layer-with-badges) | mastery/cooperation game layer with badges | WORKING (tested in verify-all) |
| [rational](#rational--exact-rational-arithmetic-wyverneye) | exact rational arithmetic (WyvernEye) | WORKING (tested in verify-all) |
| [rce](#rce--si-units-and-dimensional-analysis) | SI units and dimensional analysis | WORKING (tested in verify-all) |
| [reality](#reality--sigil-circuit-evaluator-over-live-variables) | sigil-circuit evaluator over live variables | WORKING (tested in verify-all) |
| [recon](#recon--reconstruction-step-labels-over-an-s-map) | reconstruction-step labels over an S-Map | WORKING (test not in verify-all) |
| [recover](#recover--recovery-framework-refuse-roll-back-isolate-with-signed-proofs) | recovery framework: refuse, roll back, isolate, with signed proofs | WORKING (tested in verify-all) |
| [refinery](#refinery--intent-text-to-sigil-card-compiler) | intent text to sigil-card compiler | WORKING (tested in verify-all) |
| [reputation](#reputation--pig-badge-abuse-signal-and-earnable-badges) | "Pig Badge" abuse signal and earnable badges | WORKING (tested in verify-all) |
| [rmag](#rmag--rational-resource-budgets-rmag-and-resource-tables) | rational resource budgets (RMAG) and resource tables | WORKING (tested in verify-all) |
| [robin_debanks](#robin_debanks--time-locked-vault-plus-the-shared-crypto-primitives) | time-locked vault plus the shared crypto primitives | WORKING (tested in verify-all) |
| [royalty](#royalty--ip-royalties-paid-only-from-realised-sales) | IP royalties paid only from realised sales | WORKING (tested in verify-all) |
| [rur](#rur--root-universal-representation-type-layer) | Root Universal Representation type layer | WORKING (tested in verify-all) |
| [sched](#sched--round-robin-task-table-legacy-x86-scheduler) | round-robin task table (legacy x86 scheduler) | PARTIAL |
| [sdk](#sdk--application-sdk-headers-and-template) | application SDK headers and template | STUB |
| [sdk_bridge](#sdk_bridge--capability-bridge-from-appslanguages-to-kernel-fabrics) | capability bridge from apps/languages to kernel fabrics | PARTIAL |
| [seats](#seats--people-on-one-device-and-the-login-after-boot) | people on one device and the login after boot | WORKING (tested in verify-all) |
| [security_fabric](#security_fabric--security-fabric-placeholder-crypto) | "security fabric" (placeholder crypto) | UNUSED/DEAD |
| [sephirot](#sephirot--13-phase-multi-valued-logic-engine) | 13-phase multi-valued logic engine | WORKING (test not in verify-all) |
| [settle](#settle--settlement-spine-swarm-money-on-the-ledger-of-record) | settlement spine: swarm money on the ledger of record | WORKING (tested in verify-all) |
| [shimmer](#shimmer--desktop-background-shimmer-effect) | desktop background shimmer effect | WORKING (tested in verify-all) |
| [sicily](#sicily--sicily-account-view-player-card-tiers-steward-threshold) | Sicily Account view, Player Card tiers, steward threshold | WORKING (tested in verify-all) |
| [situation](#situation--all-domain-situationrisk-modeller) | all-domain situation/risk modeller | PARTIAL |
| [smap](#smap--s-map-reassembly-manifest) | S-Map reassembly manifest | WORKING (tested in verify-all) |
| [social](#social--feed-groups-posts-buckets-and-desktop-notifications) | feed, groups, posts, buckets and desktop notifications | WORKING (tested in verify-all) |
| [social_spaces](#social_spaces--named-public-forum-spaces) | named public forum spaces | WORKING (tested in verify-all) |
| [sovereign_node](#sovereign_node--per-system-sovereignty-and-recognition-model) | per-system sovereignty and recognition model | WORKING (tested in verify-all) |
| [speech](#speech--speech-session-pipeline-dsp-vad-captions) | speech session pipeline (DSP, VAD, captions) | WORKING (tested in verify-all) |
| [storage_fabric](#storage_fabric--storage-fabric-unbuilt) | "storage fabric" (unbuilt) | UNUSED/DEAD |
| [stream](#stream--swarm-video-streaming-every-viewer-also-serves) | swarm video streaming (every viewer also serves) | WORKING (tested in verify-all) |
| [subterm](#subterm--root-terminal-with-many-sub-terminals) | root terminal with many sub-terminals | WORKING (tested in verify-all) |
| [superpos](#superpos--superposition-build-space-coordinator) | "superposition" build-space coordinator | PARTIAL |
| [surplus](#surplus--interaction-surplus-framework-maths) | Interaction Surplus Framework maths | WORKING (tested in verify-all) |
| [sutra](#sutra--sutra-transaction-language-lexer-parser-runtime-rails) | SUTRA transaction language (lexer, parser, runtime, rails) | WORKING (tested in verify-all) |
| [svault](#svault--protocol-solvency-vault) | protocol solvency vault | WORKING (tested in verify-all) |
| [swarm](#swarm--ai-model-swarm-economy) | AI-model swarm economy | WORKING (tested in verify-all) |
| [synthesis](#synthesis--synthesis-engine-spec-parser-and-dma-staging) | "synthesis engine" spec parser and DMA staging | PARTIAL |
| [syscall](#syscall--the-one-shared-zxv-syscall-table-and-capability-check) | the one shared ZXV syscall table and capability check | WORKING (tested in verify-all) |
| [taxhook](#taxhook--vat-gst-and-sales-tax-hooks-off-by-default) | VAT, GST and sales-tax hooks (off by default) | WORKING (tested in verify-all) |
| [telemetry](#telemetry--axiom-matrix-self-observation-counters) | "axiom matrix" self-observation counters | WORKING (tested in verify-all) |
| [tensor](#tensor--integer-tensor-engine-gguf-loader-tokenizer-rope-transformer-model) | integer tensor engine, GGUF loader, tokenizer, RoPE, transformer model | WORKING (tested in verify-all) |
| [terms](#terms--first-boot-end-user-agreement-gate) | first-boot end-user agreement gate | WORKING (tested in verify-all) |
| [theme](#theme--palette-tokens-that-every-drawable-resolves-through) | palette tokens that every drawable resolves through | WORKING (tested in verify-all) |
| [timer](#timer--legacy-x86-pit-82538254-tick-timer) | legacy x86 PIT 8253/8254 tick timer | PARTIAL |
| [timesvc](#timesvc--wall-clock-time-rtc-sntp-and-time-zones) | wall-clock time (RTC, SNTP) and time zones | WORKING (tested in verify-all) |
| [tls](#tls--tls-13-client-pieces-chacha20-poly1305-hkdf-x25519-record-layer-handshake) | TLS 1.3 client pieces: ChaCha20-Poly1305, HKDF, X25519, record layer, handshake | PARTIAL |
| [tolvovina](#tolvovina--tol-vovina-integer-3d-geometry-rasteriser-stereo-and-rom) | TOL VOVINA integer 3D geometry, rasteriser, stereo and ROM | WORKING (test not in verify-all) |
| [tripartite_fs](#tripartite_fs--index-of-positivenegativeneutral-tripartite-files) | index of positive/negative/neutral tripartite files | WORKING (tested in verify-all) |
| [trispace](#trispace--the-tri-space-artifact-triad-s-s--s0-and-its-five-hard-requirements) | the Tri-Space artifact triad (S+, S-, S0) and its five hard requirements | WORKING (tested in verify-all) |
| [ubh](#ubh--ubh-168-framed-envelope-and-multi-format-registry) | UBH-168 framed envelope and multi-format registry | WORKING (tested in verify-all) |
| [update](#update--signed-software-updates-package-index-consent-fetch-and-verify-upcheck-manifests) | signed software updates: package index, consent, fetch and verify, upcheck manifests | WORKING (tested in verify-all) |
| [vauction](#vauction--v-bill-issuance-by-descending-uniform-price-auction) | V-Bill issuance by descending uniform-price auction | WORKING (tested in verify-all) |
| [vbe](#vbe--vbevga-linear-framebuffer-driver-legacy-x86) | VBE/VGA linear framebuffer driver (legacy x86) | PARTIAL |
| [vblock](#vblock--content-addressed-block-store-over-memory-tiers-ram-pinned-vram-unified) | content-addressed block store over memory tiers (RAM, pinned, VRAM, unified) | WORKING (tested in verify-all) |
| [vbond](#vbond--v-bonds-fixed-term-locked-v-bill-lots) | V-Bonds: fixed-term locked V-Bill lots | WORKING (tested in verify-all) |
| [vderiv](#vderiv--v-calls-and-v-perps-on-auction-clearing-velocity) | V-Calls and V-Perps on auction clearing velocity | WORKING (tested in verify-all) |
| [vena](#vena--contract-and-app-runtime-on-top-of-the-vino-ledger) | contract and app runtime on top of the vino ledger | PARTIAL |
| [vfs](#vfs--mount-table-over-fat32) | mount table over FAT32 | PARTIAL |
| [video](#video--software-2d-rasteriser-into-a-caller-framebuffer-plus-ramfb-scanout) | software 2D rasteriser into a caller framebuffer, plus ramfb scanout | WORKING (tested in verify-all) |
| [vinea](#vinea--vinea-kademlia-dht-node-sessions-file-exchange-and-economy) | Vinea Kademlia DHT node, sessions, file exchange and economy | WORKING (tested in verify-all) |
| [vino](#vino--in-kernel-three-ledger-primary-audit-hash-chain-bank-node) | in-kernel three-ledger (primary, audit, hash chain) bank node | WORKING (test not in verify-all) |
| [vino_stores](#vino_stores--vino-floating-voucher-settlement-engine) | Vino floating-voucher settlement engine | WORKING (tested in verify-all) |
| [virtio](#virtio--virtio-mmio-bus-split-virtqueues-and-virtio-gpu) | virtio-MMIO bus, split virtqueues and virtio-gpu | WORKING (tested in verify-all) |
| [voice](#voice--deterministic-text-to-phoneme-bridge-with-fail-closed-model-boundaries) | deterministic text-to-phoneme bridge with fail-closed model boundaries | WORKING (tested in verify-all) |
| [vswap](#vswap--player-card-swap-vino-to-v-bills-lock-tiers-dividend) | Player Card swap: Vino to V-Bills, lock tiers, dividend | WORKING (tested in verify-all) |
| [web4](#web4--web2-web3-and-agent-bridges-http-json-jwt-oauth-secp256k1-ethbtc-cid) | Web2, Web3 and agent bridges: HTTP, JSON, JWT, OAuth, secp256k1, ETH/BTC, CID | WORKING (tested in verify-all) |
| [wifi](#wifi--ieee-80211-stationap-subsystem-frames-wpa-handshake-scan) | IEEE 802.11 station/AP subsystem (frames, WPA handshake, scan) | WORKING (tested in verify-all) |
| [wyrmgate](#wyrmgate--six-fold-tri-space-judgment-gate-for-state-changing-events) | six-fold Tri-Space judgment gate for state-changing events | WORKING (tested in verify-all) |
| [wyverneye](#wyverneye--intent-forged-into-a-sigil-card-then-judged-by-wyrmgate-before-acting) | intent forged into a sigil card, then judged by Wyrmgate before acting | WORKING (tested in verify-all) |
| [xedit](#xedit--x-edit-gap-buffer-text-and-code-editor-core) | X-EDIT gap-buffer text and code editor core | WORKING (test not in verify-all) |
| [xlate](#xlate--language-identification-and-translation-prompt-construction) | language identification and translation prompt construction | WORKING (tested in verify-all) |
| [xrisk](#xrisk--exact-loss-distributions-for-discrete-portfolios) | exact loss distributions for discrete portfolios | WORKING (tested in verify-all) |
| [yantra](#yantra--software-defined-hardware-fabric-capability-registry) | software-defined hardware fabric capability registry | WORKING (tested in verify-all) |
| [zab](#zab--zxv-artifact-bytecode-capability-verifier-vm-and-scopes) | ZXV Artifact Bytecode: capability verifier, VM and scopes | WORKING (tested in verify-all) |
| [zcapital](#zcapital--capital-form-market-offers-seeks-and-matching) | capital-form market: offers, seeks and matching | WORKING (tested in verify-all) |
| [zenny](#zenny--zenny-chips-user-built-baskets-with-an-exact-manifest) | Zenny chips: user-built baskets with an exact manifest | WORKING (tested in verify-all) |
| [zstats](#zstats--public-statistics-feed-over-the-economy-modules) | public statistics feed over the economy modules | WORKING (tested in verify-all) |
| [ztp](#ztp--zero-touch-provisioning-and-owner-claim-zhp-1-profile) | zero-touch provisioning and owner claim (ZHP-1 profile) | WORKING (tested in verify-all) |
| [zui](#zui--native-ui-toolkit-renderer-widgets-windows-and-system-screens) | native UI toolkit: renderer, widgets, windows and system screens | WORKING (tested in verify-all) |
| [zxpkg](#zxpkg--native-package-container-a-compiled-artifact-as-three-on-disk-tri-space-files) | native package container: a compiled artifact as three on-disk Tri-Space files | WORKING (tested in verify-all) |
| [zxvfs](#zxvfs--persistent-filesystem-with-a-redo-journal-extent-allocator-and-tri-space-store) | persistent filesystem with a redo journal, extent allocator and Tri-Space store | WORKING (tested in verify-all) |


## Part A: platform areas

### Mac desktop app  —  the ZXV swarm as a native program (kernel/arch/hosted, macos/)
Status: WORKING (tested in verify-all via `test_hosted.sh`; Mac-only parts untested here)
What it does: `zxv_host.c` runs the kernel's swarm modules (Fibonacci budget, market, emotions, witness, ledger, DNA, governor) as an ordinary program and serves a small web window on 127.0.0.1 only. The market cycle now stays open between reflexes and the companion's model answers through `zxv_budget_gate.c`: `max_new = min(256, swarm_budget_remaining(companion))`, the tokens generated are charged with `swarm_budget_consume`, and with nothing left `/api/ask` answers "Budget exhausted for this cycle" without running the model (the next cycle, about 1 s, refills it); `/api/state` shows `companion.{allotted,used,remaining,exhausted,last_answer}`. `zxv_net_host.c` runs one Vinea node (`vna_node`) on a non-blocking UDP socket in the same `select()` loop: OFF by default (no socket), LAN (private IPv4 only) or ONLINE via `--net`, `--peer IP:PORT` or the window's Network buttons (`POST /api/net`). The owner trades through the same token-guarded endpoint (U8): `share COMPUTE STORAGE` attaches a sharing agreement (nothing is shared by default), `want compute|storage UNITS MAX_PRICE` states a want, and `sell compute|storage UNITS PRICE NODEID` offers a peer the node has heard from a trade on credit; each sale the buyer countersigns becomes the owner's verified quest contribution (sales below quest's unit, 2^20 tokens or 2^30 bytes, carry over). There is no window control for trading yet. `zxv_update_host.c` runs `zx_upcheck` over a trustless gateway (curl via fork/execvp) only when the user presses Check (`POST /api/update`) or daily while ONLINE. `zx_notify` is the app's event bus: answers, failures and update outcomes become notes (`GET /api/notes`, `POST /api/notes/read`), and `zx_notify_host.c` pushes the important kinds to osascript or notify-send, chosen at run time (none when absent). Every API call needs a per-launch token and a same-origin Host/Origin (`zxv_http_guard.c`). `zxv_model_host.c` memory-maps one GGUF file, checks it with the tensor engine, and loads its tokenizer. New in this audit: `zxv_zt_glue.c` joins it to the forward pass (`zt_model`), so a supported model (Q8_0 etc.) now writes a greedy reply wrapped in the ChatML template. Each answer gets the 64-state tag (`zxv_answer_tag.c`, lpres_hex): the glue records the per-token margin (top-1 minus runner-up logit) and any logit on the Q16 rails without changing the engine; a clean reply is true/manifest with polarity from the margin trend (second half against first, an eighth dead band; entropic means WAIT), and a saturated logit or failed forward pass is forbidden: the reply is withheld and recorded in a SHA-256 hash-chained quarantine log, and a full log (64) stops the model. `/api/state` shows `companion.last_tag`, `quarantined` and `quarantine_ok`. The tag attests the computation, not the truth of the reply. Weight-hash checking before load is not built yet. On a Mac, `macos/ZXVApp.m` is a native AppKit shell that starts the engine as a child process and shows the zui desktop (`kernel/arch/hosted/zxv_desktop.c`) on Metal, with no web view. The swarm's agents are still stand-ins; the model answers on its own.
Main entry points: `zxv-host` CLI (`--server`, `--port`, `--model`, `--models-dir`, `--exit-with-parent`, `--net`, `--net-port`, `--net-bind`, `--peer`, `--state-dir`, `--no-lan-discovery`, `--notify`, `--update-gateway`); `/api/state`, `/api/ask`, `/api/quit`, `/api/net`, `/api/update`, `/api/notes`, `/api/notes/read`; `zxv_budget_gate`; `zxv_net_*`; `zxv_update_*`; `app_sources.sh` (the one source list); `zxv_model_open/answer/close`; `zxv_zt_generate[_n]`, `zxv_zt_can_run`, `zxv_zt_release`; `zxv_guard_*`.
Tests: `kernel/arch/hosted/test_hosted.sh` (guard unit test under ASan/UBSan, glue test `test_zxv_zt_glue.c` [16 checks, incl. the verdict], answer tag `test_zxv_answer_tag.c` [12 checks], budget gate `test_zxv_budget_gate.c` [19 checks: budget N, ask for more, exactly N generated, 0 left, refused, refilled next cycle; also on the real engine], two UDP net hosts on 127.0.0.1 `test_zxv_net_host.c` [23 checks: ping, both routing tables, FIND_NODE both ways, OFF/LAN/ONLINE policy], end-to-end socket test `test_host_api.py` twice, and `test_host_net.py` [44 checks: no UDP socket and no update request without the user, two app instances see each other, the Network setting, a sale refused before sharing and refused by a buyer that did not want it, then two compute trades on credit completing on both instances with conserved books and the seller's quest contribution rising while the buyer's does not, a requested update check against a local gateway, the note pushed to a stand-in notify-send]); in verify-all. All pass with gcc 11 and clang 18. `build_system/build_desktop.sh` built macOS x86_64+arm64, Windows and Linux engines with zig 0.13 (pip ziglang) and its end-to-end test passed; no native shell off a Mac.
Used by: nothing (top-level program). `zx_speech_mac.m` is built by nothing.
Gaps: real models have run on the engine (Qwen2.5-1.5B, see [GPU_STRATEGY.md](GPU_STRATEGY.md)) but no model ships with the app; `/api/ask` and an update check block the single-threaded server while they run; the Vinea identity and peer cache persist in the per-user folder, LAN multicast discovery (`vna_lan`), basic NAT traversal (reflexive address, hole punching through a shared contact, keepalive) AIPI over Vinea (`zxv_net_attach_aipi`) and public spaces with the synced watch hall (zxvd `pspace.*`/`hall.*`, docs/SPACES.md; control only, video delivery between viewers not built) are built (zxv_net_host.h U9 to U14), but there is no IPv6, no port mapping (NAT-PMP/PCP/UPnP) or relay, and zxvd does not attach an AIPI context yet; update checks are not wired on Windows (no curl spawn) and nothing is installed; osascript notes show as "Script Editor"; no ipfs_node file exchange, call, social or speech glue; ZXVApp.m and ad-hoc signing never run outside the macOS CI job (releases are not notarised, by decision).

### Bare-metal kernels  —  the ZXV OS booted on emulated hardware (kernel/arch/*, build_system/Makefile.*)
Status: WORKING (all five boot to `[BOOT_OK]` under QEMU with no fault marker; CI fails closed on each)
What it does: five Makefiles build freestanding kernel images from the M5 core plus the platform and economy layers. ARM64 is the reference (EL0 userspace, GICv3, virtio). Here: arm64 built and booted to `[E0174] [BOOT_OK]` with no `[FAULT]` line; x86_64 built and booted to `[E0018] [BOOT_OK]` via `make boot`/`run`; riscv64 (`-M virt`, distro OpenSBI) and riscv32 (`-M virt`, OpenSBI v1.3.1 built for rv32, via `ZXV_RV32_SBI`) boot to `[BOOT_OK] Phase E0082`; arm32 (`-M virt -cpu cortex-a15`) boots to `[BOOT_OK] ... [ARM32]` and now installs an exception vector table (VBAR) so an abort or undefined instruction prints `[FAULT]` instead of silently stopping. The swarm, tensor, Vinea, pay, cbank and the other 2026-10-09 modules are in no kernel image.
Main entry points: `make -f build_system/Makefile.<arch> all|run|clean`; `kernel_main_arm64.c`; `build.sh <arch> [run]`; `platform_layer.mk`, `economy_layer.mk` (fragments for non-arm64 arches, included by no Makefile yet).
Tests: CI `build-*` jobs all run `build_system/ci_boot_check.sh`, which fails unless the log has `[BOOT_OK]`, no `[FAULT]`/`[EL0 FAULT]`/`CPU exception vector=`/panic line, and QEMU was stopped by the timeout (not an error exit); `test_ci_boot_check` (11 cases) in verify-all tests that gate. Banner check (`verify_banners.sh`) runs in every build and passed.
Used by: CI; `ZXV-Desktop.command` / `run_desktop.sh`.
Gaps: economy/network modules of the product not linked into any image (swarm/tensor are linked into arm64 and x86_64 only, self-check only, CI requires [AI_OK]); the riscv32 CI job builds its own rv32 OpenSBI because Ubuntu ships rv64 only; Makefile.x86_64 had no `run` target (fixed).

### C ABI bindings  —  one C library for COBOL, Fortran, Pascal, Ada and others (bindings/)
Status: WORKING (C self-test in verify-all; language examples built and run here)
What it does: `zx_legacy_api.{h,c}` wraps the freestanding legacy bridge (`kernel/src/legacy`: SIP/SDP, SS7/TCAP/ISUP mapping, DTMF, EBCDIC, COBOL COMP-3, IBM HFP floats) behind an append-only C ABI. `build_lib.sh` builds `libzxlegacy` and runs whichever language examples have a compiler: here GnuCOBOL, gfortran, Free Pascal and GNAT all built and ran correctly. RPG, PL/I and BASIC examples are source only.
Main entry points: `zx_legacy_abi_version`, the COMP-3 and HFP converters, SIP/SS7 helpers in `zx_legacy_api.h`; `build_lib.sh`.
Tests: `bindings/test_abi.c` (14 checks, passes under ASan/UBSan) is `test_legacy_abi` in verify-all; the language examples are not in CI.
Used by: `bindings/dotnet/native` (separate shim), the language examples.
Gaps: language examples unchecked by CI; RPG/PL-I/BASIC never compiled.

### .NET SDK  —  C# SDK and ASP.NET operator gateway (bindings/dotnet, api/openapi)
Status: WORKING (CI job `dotnet-sdk`; built and tested here)
What it does: a native shim (`native/zxv_api.c`) exposes kernel ISO 20022, triple-ledger, ISO 4217 (cbank) and card check-digit code through a C ABI; `Zxv.Native` P/Invokes it; `Zxv.Sdk` gives accounts, payments, netting, cards and conformance in idiomatic C#; `Zxv.Gateway` serves them as `/v1/...` REST, matching `api/openapi/zxv-operator.yaml` (23 paths). It formats and checks messages; it has no SWIFT, PAPSS, CIPS or RTGS connectivity (the doc says so).
Main entry points: `ZxvPlatform`, `Money`, payments/netting/cards services, `IMxTransport` (caller-supplied transport), gateway route groups `/v1/accounts|payments|netting-cycles|cards|currencies`.
Tests: `make -C native test` 85 checks pass; `dotnet build -c Release` 0 warnings; `dotnet test` 75 + 9 pass (dotnet 8.0.131).
Used by: nothing in-tree beyond its samples.
Gaps: transports for real networks are the operator's job; uses the kernel `TEST_HOST` (double) surplus path per its own note.

### Mobile  —  lean Android and iOS apps over one C core (mobile/)
Status: PARTIAL (core library builds; apps not built here or in CI)
What it does: `mobile/core/build_mobile_core.sh` compiles devmesh (personal device mesh), capmkt (capacity market), pq_matrix, ML-KEM, ehop, pay, swarm_market and the GGUF reader/tokenizer into `libzxvcore.a` with a flat C API (`zxv_mobile.h`). Android (Kotlin/Compose + JNI) and iOS (SwiftUI package) shells call it; `FakeCore.kt` is a preview stand-in only (MainActivity uses `NativeCore`).
Main entry points: `zxv_mobile.h` (mesh create/invite/join/SAS/roster/caps/money confirm), `zxv_jni.c`, `ZXVKit`.
Tests: linux-arm64 core build here: 54 objects, archive 1,281,164 bytes, 152,370 bytes linked text. No gradle/Xcode build, no tests in CI.
Used by: the two app shells.
Gaps: apps never built in CI; no on-device inference yet (zt_model optional; Q4_K_M 0.5B now loads in the engine, untried on a device); doc archive size was wrong (fixed).

### Build and CI  —  how everything is built and checked (build_system/, .github/workflows/ci.yml, kernel/Makefile)
Status: PARTIAL
What it does: CI runs five arch builds with QEMU boots, `host-tests` (`relicense_apache.py --check` + `make verify-all`), changed-lines clang-format, `macos-app` (build_desktop.sh on macos-14, bundle checks, launch/quit) and `dotnet-sdk`. `build_system/Makefile` is a second suite for the root OS layers (`os_lattice`, `neon`, `gridchain`, `security`, `physics_sim`, `hccs`, `audiogenomics`, `governance`, `tests/test_drivers.c`).
Main entry points: `kernel/Makefile verify-all`, `build_system/Makefile test`, `build_desktop.sh`, `build_signed_app.sh`, `build.sh`.
Tests: root suite before: did not compile; now 10/10 pass and fails closed (binaries in build/os_tests). Not in CI or verify-all (lines proposed in platform_verify_lines.mk). Tiered tests (tests/01_root_axioms, 02_integration, 03_metamorphic; driver tests/harness/tiers.py, plain + ASan/LSan/UBSan + Q32.32) run in verify-all; a deterministic mutation harness (tests/harness/mutator/mutate.py) gates a small subset in the `mutation-subset` CI job; scores in tests/MUTATION_REPORT.md, open bugs the tiers expose in tests/FINDINGS.md.
Used by: CI and developers.
Gaps: CI triggers only on push/PR to main and develop; non-arm64 boots `|| true`; no verify-experimental job (fuzz, sanitizer and coverage jobs added: sanitize-asan-ubsan, sanitize-integer, sanitize-msan, fuzz-smoke, coverage; see fuzz/README.md; no TSan job because nothing is threaded); mobile and language bindings not in CI; `build_desktop.sh` runs `make test-swarm`, which writes fixed `/tmp/test_*` paths.

### Update publishing  —  signed update buckets on IPFS (build_system/zxv_publish_update.py, kernel/src/update)
Status: WORKING (library and tool tested; not wired into the app)
What it does: the Python tool makes a strict-format manifest of a bucket's files with Kubo-identical CIDv1s and SHA-256s and signs it with ML-DSA-65 (vendored pq-crystals code compiled on the fly). The kernel checker `zx_upcheck` verifies bucket manifests per user-trusted key, follows fixed CIDs or IPNS names with rollback protection, and never installs unsigned content. Here: keygen, sign and verify round-trip worked and a tampered file was caught.
Main entry points: `zxv_publish_update.py keygen|pubkey|sign|verify|cid`; `zxu_*` in `zx_upcheck.h`; `ZXU_BUILTIN_CID`.
Tests: `test_zx_upcheck` in verify-all includes an interop check of a manifest signed by the Python tool.
Used by: the desktop app (kernel/arch/hosted/zxv_update_host.c: checks on request, or daily while online). Not in the kernel images.
Gaps: the app only checks and reports (no install, no trust-a-key UI, no Windows transport); IPNS vs fixed CID undecided; no release key ceremony documented for ML-DSA (the ZSP chain is still Ed25519).

### Docs set  —  top-level and docs/*.md
Status: PARTIAL
What it does: 25 docs/*.md (one per new module family plus ARCHITECTURE_OVERVIEW with 22 gaps) and many older top-level documents. The newer docs are careful about limits (no-connectivity, no-certification statements in CARD_NETWORKS, CENTRAL_BANK, DOTNET_SDK). Older ones (ARCHITECTURE_COMPLETE, ARCHITECTURE_VERIFICATION, VOVINA_SHAKINA_*, tools/zenodo_metadata.json) carry stale counts and overclaims.
Main entry points: README.md, docs/ARCHITECTURE_OVERVIEW.md, docs/MAC_APP.md.
Tests: none (path references spot-checked mechanically).
Used by: people.
Gaps: ARCHITECTURE_OVERVIEW and MAC_APP were stale about the model runtime (status block / limit added); several cited docs (CARRACHO, NETWORK_STACK, PAY_RAILS, HOST_API, MODELS) do not exist; four overlapping ARCHITECTURE files.


## Part B: kernel modules, A to Z

### abacus  —  Smaug's debt-netting abacus (multilateral clearing of IOUs)
Status: WORKING (tested in verify-all)
What it does: Members record who owes whom. The module nets the debts into a smaller set of
transfers that settle everyone. It also checks that the total owed equals the total due. The
clearing is a real greedy algorithm; the dragon naming is only flavour.
Main entry points: smaug_init, smaug_add_member, smaug_owe, smaug_net, smaug_clear,
smaug_conservation, smaug_gross, smaug_transfer_count.
Tests: src/abacus/test_abacus.c and test_abacus_regress.c, both run by verify-all, 0 failures.
Used by: no #include from other modules. In every kernel image (Makefile.*), reached through its
ZXV_DECLARE bring-up check.
Gaps: smaug_init left clear_failed uninitialised (fixed).
Gaps: a comment calls the greedy result the "minimum possible" number of transfers. Greedy netting
is not guaranteed to be minimal; finding the minimum is NP-hard.

### abstraction_layer  —  multi-language module registry (dead)
Status: UNUSED/DEAD
Location: kernel/experimental/abstraction_layer/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: A table meant to register language runtimes and kernel modules, then initialise them
in order. Neither file compiles: types and headers are missing. No build file references it.
Main entry points: al_init, al_register_builtin_languages, al_register_language,
al_register_module, al_get_module, al_initialize_all.
Tests: none.
Used by: nothing.
Gaps: does not compile, is not built and has no test. All of it is aspirational.

### acct — accounting adapters for IFRS, US GAAP and AAOIFI
Status: WORKING (tested in verify-all)
What it does: turns one economic event (a sale, a voucher, a V-Bill issue, a murabaha, interest, tax, zakah and others) into one balanced journal entry in the captions of the chosen framework, posts it to a book, and builds a balance sheet in that framework's layout. It also reconciles tax at the statutory rate to tax expense, item by item, and under AAOIFI computes the zakah base. Real framework differences are coded (LIFO refused under IFRS, development costs, revaluation, redeemable V-Bills, riba blocked or flagged under AAOIFI). It is designed to follow the frameworks and is not certified by any standard setter.
Main entry points: acct_translate_transaction, acct_post, acct_record, acct_generate_balance_sheet, acct_bs_json, acct_reconcile_tax_schedule, acct_breakage, acct_allocate, acct_cost_flow, acct_murabaha_schedule, acct_std_from_name / acct_std_name.
Tests: test_acct.c (431 checks, ASan/UBSan), target test-acct, in verify-all.
Used by: juris (juris.h includes acct.h; juris_host_standard, rule J7). zxvd links it (app_sources.sh srcs and rpcdeps) but only uses the standard names for jurisdiction.get / jurisdiction.set and their report_standard field. It is also compiled into the desktop app's source list with no caller there.
Gaps: IPSAS, OHADA, CAS and SAP_INSURER return ACCT_E_UNSUPPORTED. No zxvd method returns a balance sheet or tax schedule, and it is not in libzxv. Nothing feeds real ledger events into it. See [ADAPTERS.md](ADAPTERS.md).

### acpi  —  ACPI table finder (x86)
Status: PARTIAL
What it does: Finds the ACPI root pointer (RSDP) in the BIOS memory area, checks its checksum,
then walks the RSDT to find a table by its 4-letter signature (for example "APIC"). This is real
x86 firmware parsing. It trusts the table lengths it reads.
Main entry points: acpi_init, acpi_find_table, acpi_checksum.
Tests: ../tests/test_drivers.c (build_system/Makefile test_drivers, not in verify-all). Passes, but
it only checks struct sizes and a lookup on empty state.
Used by: boot/kernel_main.c (x86), init/init.h, tests/test_drivers.c. In Makefile and Makefile.arm64.
Gaps: the RSDT length and each table's checksum are not validated before use.
Gaps: on arm64 the pointer/integer casts give warnings.

### ai_layer  —  bookkeeping for AI models, tasks and remote compute peers
Status: WORKING (tested in verify-all)
What it does: Registers AI models, provided the model carries a valid Ed25519 signature. It queues
tasks, admits remote compute peers through Porter House, and tracks budgets and timeouts. It does
no AI inference: ai_execute_local is a simulation, and the header says so.
Main entry points: ai_init, ai_register_model, ai_verify_model, ai_add_peer, ai_submit_local,
ai_submit_remote, ai_execute_local, ai_complete_remote.
Tests: src/ai_layer/test_ai_layer.c, run by verify-all, passes.
Used by: arch/arm64/kernel_main_arm64.c. In every kernel image.
Gaps: the signature covers the model name and provider key but not content_hash, so the model's
bytes are not bound. The header discloses this.
Gaps: no real inference.

### aipi — content-addressed AI functions over the IPFS node and Vinea
Status: WORKING (tested in verify-all)
What it does: names a function by a CIDv1 of its descriptor (the exact Z-Wasm module, export, contract and how a result is checked), fetches code and data by CID and drops bytes that do not hash to the CID. A remote result is accepted only through a checker, a certificate or a k-of-n quorum, never on trust. Bounties are held in escrow and paid only after verification. aipi_vna.c carries calls, claims and PAID notices as signed Vinea messages.
Main entry points: aipi_init, aipi_get, aipi_put_blob, aipi_publish_fn, aipi_execute, aipi_submit, aipi_serve, aipi_tick, aipi_fn_cid, aipi_call_cid; aipi_vna_init, aipi_vna_execute, aipi_vna_serve, aipi_vna_announce.
Tests: test_aipi.c (617 checks, ASan/UBSan, plus a freestanding build for aarch64 and i386) and test_aipi_vna.c (102 checks), both in verify-all. kernel/arch/hosted/test_zxvd_aipi.sh runs in the zxvd.yml CI workflow, not in verify-all.
Used by: zxvd's aipi.put / aipi.get / aipi.publish / aipi.execute / aipi.status methods (zxvd_aipi.c, opened from zxv_host.c when built with ZXV_HAVE_AIPI, not on Windows), libzxv ABI 1.5 (sdk/libzxv/src/zxv_aipi.c), and the Z-Wasm executor kit sdk/kits/aipi. zxv_net_host.c holds the Vinea glue (zxv_net_attach_aipi), but nothing outside test_zxv_net_host.c calls zxv_net_attach_aipi, so the mesh path is linked but dormant in the app and in zxvd.
Gaps: zxvd runs one node offline with an in-memory store that is lost on exit. A call or claim is one datagram of about 10 KB, and inputs and outputs are at most 1024 bytes. Modules are one block (up to 1 MiB). The codecs are unregistered private-use codes. A quorum assumes independent executors. No real intents (tensor inference, settlement, routing) are published yet. The strata.txt comment ("AI pinning index; stores model blobs") does not match what the module does. See [AIPI.md](AIPI.md).

### airc — agent relay chat: signed boards, trust tiers and the install decision
Status: WORKING (tested in verify-all)
What it does: the protocol core for IRC-shaped boards between nodes. It encodes and checks ML-DSA-87 signed posts, keeps boards with equivocation and flood control, runs a quorum gate with one-time leases for jumbo frames, gives trust tiers computed from cred, and decides whether a shared improvement may be installed (signed offer, trusted publisher and an approving review all required). It moves no bytes and never runs a post's body.
Main entry points: airc_post_encode / decode / sign / open, airc_board_init, airc_board_accept, airc_trust_init, airc_trust_settle, airc_trust_tier, airc_gate_request / vote / tally, airc_offer_decide.
Tests: test_airc.c (761 checks), plus a freestanding build for aarch64, i386, armv7a and rv32, target test-airc, in verify-all.
Used by: no caller outside its tests.
Gaps: no carrier: posts are not carried over Vinea yet, there is no zxvd airc.* group, and the IRC gateway does not use it. An approved offer still has to go through the normal signed update path, which is not connected. See [AIRC.md](AIRC.md).

### alloc  —  shared resource pool and personal-token ledger
Status: WORKING (tested in verify-all)
What it does: People contribute to a common pool, and distributions are kept within a
sustainability bound. A separate ledger mints and transfers personal tokens ("ptokens"). The
arithmetic is real. Rates and stocks are supplied inputs, not discovered.
Main entry points: alloc_pool_init, alloc_contribute, alloc_distribute, alloc_sustainable_ok,
alloc_acquire_with_financial, ptoken_ledger_init, ptoken_mint, ptoken_transfer.
Tests: test_alloc.c (45 passed) and test_alloc_q32.c, both run by verify-all.
Used by: no #include from other modules. In every kernel image and in economy_layer.mk.
Gaps: ptoken_transfer does not authenticate `from`, so the "no seizure" guarantee depends on the
caller.
Gaps: SR_ADD overflow is unchecked, and a negative draw is accepted.

### app_constellation  —  service deployment manager (header only)
Status: STUB
Location: kernel/experimental/app_constellation/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: Declares an API for deploying, rolling back, pausing and resuming services across
the constellation. There is no .c file, so nothing behind the API exists.
Main entry points: ac_init, ac_register_service, ac_get_service, ac_deploy_service,
ac_rollback_deployment, ac_pause_deployment, ac_resume_deployment.
Tests: none.
Used by: nothing (it includes app_fabric and compute_fabric, which are dead themselves).
Gaps: no implementation at all.

### app_fabric  —  application descriptor and instance registry (dead)
Status: UNUSED/DEAD
Location: kernel/experimental/app_fabric/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: Meant to register application descriptors and to create, start and stop
instances. It does not compile (missing types) and no build file references it.
Main entry points: af_init, af_register_builtins, af_register_app, af_get_descriptor,
af_create_instance, af_start_instance.
Tests: none.
Used by: app_constellation.h only (which is also dead).
Gaps: does not compile, is not built and has no test.

### appkit  —  text document model for native apps (gap buffer, undo, save)
Status: WORKING (tested in verify-all)
What it does: The shared editing core behind Writer, Sheet and the other apps. Text sits in a gap
buffer, so typing at the cursor is fast. Undo and redo history is bounded and coalesced, there is
a dirty flag, and documents load and save through ZXVFS. Real and self-contained.
Main entry points: doc_init, doc_insert, doc_delete, doc_undo, doc_text, doc_length,
doc_is_dirty, doc_char_at.
Tests: src/appkit/test_doc.c, run by verify-all, passes.
Used by: arch/arm64/kernel_main_arm64.c. In every kernel image.
Gaps: none found in the paths reviewed.

### apps  —  built-in shell, editor, network config and other desktop apps
Status: PARTIAL
What it does: apps.c holds a working text shell, an editor, a network-config panel, sysmon and a
few small tools; these are compiled into the kernel. notes_app.c with test_notes.c and vfs_stub.c
are host-tested by verify-all (test-orphans). The unbuilt app files (derivatives, treaty, assurance,
wallet, clock, dharma_monitor) moved to kernel/experimental/m5_apps/ (GAP_AUDIT B3); telecom signalling and some demo files in other
languages are still here and not built. sysmon shows invented CPU and memory figures.
Main entry points: shell_init, shell_execute, shell_handle_key, shell_render, editor_init,
editor_load_file, netcfg_refresh.
Tests: ../tests/test_apps.c exists but is not in verify-all. None of these tests run in
verify-all.
Used by: boot/kernel_main.c and tests/test_apps.c. apps.c is in Makefile and Makefile.arm64, and
demo_cross_lang.c is in Makefile.arm64.
Gaps: netcfg_refresh overflowed detail_buf, and editor_load_file overflowed filename (both fixed).
Gaps: apps.c calls derivatives_init, assurance_init and treaty_init, but nothing compiled defines
them.
Gaps: sysmon CPU and memory figures are fake: (tick*7)%100.
Gaps: shell_ps prints the wrong PID for ids of 100 or more.
Gaps: in the kernel Makefile `iso` target, apps.c fails because the zcapital.h include path is
missing.

### aproto — protocol twins: AFTP file transfer, the AHAM ham frame and the audit chain
Status: WORKING (tested in verify-all)
What it does: ap_log.c is a hash-chained audit log with signed checkpoints and an auditor that replays it. aftp.c is a peer-to-peer twin of FTP: signed folder listings with strict path rules, rollback and equivocation detection, and chunk transfers that count a chunk only after its Merkle proof and drop a source that sends a bad one. aham.c builds and checks the one signed cleartext frame allowed on an amateur band. All are pure state machines with no sockets.
Main entry points: ap_log_init, ap_log_append, ap_log_checkpoint, ap_audit_feed; aftp_init, aftp_dir_sign, aftp_dir_learn, aftp_offer_make, aftp_offer_accept, aftp_get_start; aham_build, aham_open.
Tests: test_aftp.c (510 checks, ASan/UBSan) covers aftp, aham and ap_log, target test-aproto, in verify-all.
Used by: no caller outside its tests.
Gaps: no host binds it to sessions, storage or DHT lookups, and there are no zxvd methods for it. Cross-checking two parties' logs is not automated. A listing holds 32 entries (larger folders need listings of listings). Every other twin in the catalogue is marked Planned. See [P2P_PROTOCOL_TWINS.md](P2P_PROTOCOL_TWINS.md).

### art_studio  —  pixel canvas and painting tools
Status: WORKING (tested in verify-all)
What it does: A real integer raster canvas: rectangles, lines, alpha blending and path strokes,
with colours from the theme palette. Every write is clipped to the canvas. Finished art is
exported content-addressed (SHA-256) so it can be listed as a marketplace item.
Main entry points: art_canvas_init, art_fill_rect, art_hline, art_vline, art_blend_pixel,
art_stroke_path, art_palette_from_theme, art_palette_color.
Tests: src/art_studio/test_art_studio.c, run by verify-all, passes.
Used by: desktop and media_fabric. In every kernel image and in economy_layer.mk.
Gaps: none found.

### ascent  —  "Ladder of Ascent" security-practice checklist model
Status: WORKING (has a test, not in verify-all)
What it does: Models a 10-level operational-security ladder as data. Evidence bundles hold claims,
refutations, witnesses and consensus, and gates check whether a bundle may advance. This is a
structured checklist and scoring model, not a security mechanism that enforces anything.
Main entry points: ascent_init, ascent_bundle_create, ascent_bundle_set_claim,
ascent_bundle_add_witness, ascent_bundle_add_consensus, ascent_bundle_check_gates.
Tests: test_ascent() in ../tests/test_new_modules.c. No build target or CI job runs that file,
and it was not run in this audit (it needs about 20 other modules).
Used by: tests/test_new_modules.c. In Makefile.arm32/arm64/riscv/riscv32/x86_64.
Gaps: the only test is never run.

### ata  —  ATA/IDE disk driver (PIO, x86)
Status: PARTIAL
What it does: Talks to an IDE disk through x86 I/O ports. It identifies the drive and reads or
writes 512-byte sectors (28-bit LBA, polled I/O). This is real hardware code, but no test drives
it.
Main entry points: ata_init, ata_identify, ata_read_sector, ata_write_sector.
Tests: ../tests/test_drivers.c (struct sizes only, not in verify-all). Nothing exercises I/O.
Used by: boot/kernel_main.c (x86), init/init.h, storage_fabric and tests/test_drivers.c. In
Makefile, Makefile.x86_64 and build_x86_64.sh.
Gaps: four problems in the wait and transfer paths (all fixed):
- ata_wait waited for DRDY, not DRQ.
- It ignored ERR and DF.
- It could spin forever.
- There was no LBA range check and no cache flush after a write.
Gaps: no DMA, no LBA48, no test against an emulated device.

### audio  —  audio streams and software mixer
Status: WORKING (tested in verify-all)
What it does: A real software audio path. Each stream has a ring buffer and decodes 7 PCM
formats. Gain, balance and distance panning are applied, the sample rate is converted, and N
streams are mixed down to stereo; the output saturates instead of wrapping. It drives no sound
hardware.
Main entry points: audio_init, audio_create_stream, audio_write, audio_read, audio_set_volume,
audio_set_balance, audio_set_3d_position, audio_pause.
Tests: src/audio/test_audio.c in verify-all, 353 checks, also built with ASan/UBSan
(test_audio_san). Passes.
Used by: media_fabric. In Makefile and Makefile.arm64.
Gaps: none for floating point: gains, positions and coverage are Q16.16 integers (fixed).
Gaps: no hardware output driver in this directory.

### audiogenomics_pro  —  DNA-to-sound synthesis and symbolic encodings
Status: WORKING (has a test, not in verify-all)
What it does: Maps DNA or RNA sequences to tones, with FM/AM modulation, and adds Hebrew
gematria and frequency-mapping encodings. It is a port of a Python art and analysis tool. The
"electromagnetic genomics" and "architectural directives" parts are symbolic frameworks, not
science. The audio synthesis itself is real arithmetic.
Main entry points: ddna_smap_init, ddna_smap_add_chunk, ddna_smap_reassemble,
ddna_triadic_init, ddna_triadic_measure, lpres_evaluate.
Tests: ../tests/test_audiogenomics.c (build_system/Makefile test_audiogenomics), which passes. It
is not in verify-all.
Used by: arch/arm64/kernel_main_arm64.c and media_fabric. In every arch image except x86
Makefile.
Gaps: none for floating point: frequencies are Q16.16 Hz, samples Q16.16, phases binary turns, durations ms (fixed; checked against a double oracle by tests/host/test_fixed_point.c in verify-all). Generators still keep 256 KB sample buffers on the stack.
Gaps: the scientific-sounding claims are framing, not results.

### axiom_matrix  —  sparse complex-valued matrix keyed by hash
Status: WORKING (has a test, not in verify-all)
What it does: Stores complex numbers in a fixed hash table indexed by (row, column) and projects a
"tick" across it. It is a small data structure.
Main entry points: axiom_matrix_hash, axiom_matrix_set, axiom_matrix_get,
axiom_matrix_is_symmetric, axiom_matrix_project_tick.
Tests: src/axiom_matrix/test_axiom_matrix.c runs in the kernel Makefile `test` target, not in
verify-all. It passes.
Used by: many ../tests files, every arch kernel_main and telemetry.
Gaps: the table is lossy: a colliding key silently overwrites another entry.
Gaps: the "Hermitian/symmetric" check only tests whether entries are real.
Gaps: none for floating point: entries are zxv_cq16_t (Q16.16 complex) and the index uses fx_udiv64 (fixed).

### battering_ram  —  pooled-credit exchange that pays out on a verified outcome
Status: WORKING (tested in verify-all)
What it does: Members book credit into a pool tied to a goal. When an attestation says the goal
was reached, br_distribute pays the pool out pro rata. It also has alliance swaps and rail
binding. The ledger arithmetic is real. Whether the goal was achieved comes from an external
attestor, which must now be pinned.
Main entry points: br_exchange_init, br_set_verifier, br_pin_attestor (new), br_bind_rail,
br_book_credit, br_alliance_open, br_swap_settle, br_distribute.
Tests: src/battering_ram/test_battering_ram.c, normal and q32 builds in verify-all, 49/49 each.
Used by: desktop. In every kernel image and in economy_layer.mk.
Gaps: the Ed25519 check accepted any self-signed attestation (fixed: the attestor key must now be
pinned).
Gaps: the attestation message has no domain or exchange binding.
Gaps: SR_MUL in swaps is not overflow-checked.

### bearer — link table, framing and policy gates for BLE, LoRa, serial, modem and ham radio
Status: WORKING (tested in verify-all)
What it does: describes each kind of link (LAN, BLE, Wi-Fi Direct, LoRa, serial, modem, cellular, ham SDR) with fixed capabilities, refuses messages a link must never carry (ham: signed printable emergency text only; LoRa: small signed records only), picks the best live link from measured latency, loss and cost, and fails over. The br_* files frame bytes for each link: fragmentation, COBS + CRC-32 serial frames, a Hayes AT modem state machine, AX.25 UI frames, LoRa air time and duty cycle, and a devmesh host that sends through this layer.
Main entry points: br_table_init, br_link_add, br_link_tune, br_link_measure, br_caps, br_policy_check, br_score, br_select, br_send (selection and failover); the framing files br_frag, br_ble, br_lora, br_serial, br_modem, br_ax25, br_ham, br_devmesh.
Tests: test_bearer.c (143), test_br_ble.c (57), test_br_lora.c (98), test_br_serial.c (37), test_br_modem.c (61), test_br_ax25.c (59), test_br_ham.c (45), test_br_devmesh.c (40), and kernel/arch/hosted/test_bearer_host.c (34, real pseudo-terminal), all under ASan/UBSan in mk/bearer.mk test-bearer, which runs in verify-all.
Used by: no caller outside its tests. kernel/arch/hosted/bearer_host.c (POSIX serial glue) is built only by the test.
Gaps: no real radio, cable or modem has been used. The mobile apps do not use it yet. Windows serial ports are not covered. The regulatory rules are designed to follow the cited law but have not been reviewed by a lawyer or regulator. See [retail/BEARERS.md](retail/BEARERS.md).

### bios  —  ZBIOS staged, signed boot-chain model
Status: PARTIAL
What it does: Models a 10-stage boot chain. Each stage is staged, checked against a minimum
version and sealed, and the next stage depends on the previous one. The state machine is real.
Whether a stage's signature verified is a boolean the caller passes in; the module checks no
signature itself.
Main entry points: zb_init, zb_stage, zb_seal, zb_set_min_version, zb_triad_of, zb_strerror.
Tests: src/bios/test_zbios.c, run by verify-all, passes.
Used by: no includes. In every kernel image, but no boot path calls it.
Gaps: no real signature verification, because it relies on the caller's boolean.
Gaps: not wired into any actual boot sequence.

### blockdev  —  block device interface and RAM disk
Status: PARTIAL
What it does: A generic block-device interface plus a RAM disk that can format itself as FAT32
and hand out a block device. It is used as the boot-time disk on arm64.
Main entry points: zxv_ramdisk_init, zxv_ramdisk_read_sector, zxv_ramdisk_write_sector,
zxv_ramdisk_format_fat32, zxv_ramdisk_create_blockdev, zxv_ramdisk_total_sectors.
Tests: none in this directory. FAT32 tests elsewhere touch it only indirectly.
Used by: arch/arm64/ramdisk.c. In Makefile.arm32/arm64/riscv/riscv32/x86_64.
Gaps: no direct unit test.

### bluetooth  —  Bluetooth HCI/L2CAP packet builders and parsers
Status: WORKING (tested in verify-all)
What it does: Builds and parses the Bluetooth host-controller packets: commands, events and ACL
data. It also covers L2CAP and higher-level frames, with bounds checks on every field. The
packet handling is real, but no radio driver is attached.
Main entry points: bt_hci_opcode, bt_hci_build_cmd, bt_hci_parse_cmd, bt_hci_build_event,
bt_hci_parse_event, bt_hci_build_acl.
Tests: src/bluetooth/test_bluetooth.c, run by verify-all, passes. An ASan/UBSan fuzz of HCI ingest
and every parser in this audit was clean.
Used by: network_fabric. In Makefile and Makefile.arm64.
Gaps: no transport or controller driver.

### bombsquad  —  early-warning monitor for invariants that are drifting toward failure
Status: WORKING (tested in verify-all)
What it does: Registered invariants report a safety margin instead of a plain pass or fail. The
monitor tracks how fast each margin is falling and raises a warning before the margin hits zero.
The header says this is a heuristic, not a prediction.
Main entry points: bs_init, bs_watch, bs_tick, bs_state, bs_set_reporter.
Tests: src/bombsquad/test_bombsquad.c (test_bs), run by verify-all, passes.
Used by: arch/arm64/kernel_main_arm64.c. In every kernel image.
Gaps: none found.

### bootattest — measured-boot event log, signed quote and golden manifest
Status: WORKING (tested in verify-all)
What it does: extends SHA-256 stage digests into a chain the way a TPM extends a PCR, signs a quote (event log, chain and the verifier's nonce) with ML-DSA-87, and checks it against a golden manifest signed by the release key whose id is a CIDv1. The verifier checks format, signer, signature, rollback, manifest id, nonce and every stage, each with its own error.
Main entry points: ba_log_init, ba_extend, ba_measure, ba_quote, ba_manifest_build, ba_manifest_verify, ba_verifier_init, ba_verifier_issue, ba_verify_quote, ba_soft_signer.
Tests: test_ba.c (56 checks) plus a freestanding build, in mk/rot.mk test-rot, which runs in verify-all.
Used by: ztp (zero-touch provisioning, kernel/src/ztp), which itself has no caller outside its tests.
Gaps: not wired into the boot path: no EFI stub or kernel image measures anything with it, and the existing boot_evidence hash is not a measured chain. No hardware root: ba_soft_signer keeps the key in ordinary memory. Tested on the host only. See [retail/ROOT_OF_TRUST.md](retail/ROOT_OF_TRUST.md).

### bootfeat  —  platform-layer bring-up at boot
Status: PARTIAL
What it does: One call at boot that brings up the platform layer and prints one evidence line per
subsystem. The subsystems are deploy, theme, icon, font, bridge, update, cards and economy. It is
glue code that calls other modules' init and self-check functions.
Main entry points: boot_features_init, boot_economy_init.
Tests: none directly. It is exercised only by booting.
Used by: arch/arm64/kernel_main_arm64.c and arch/x86_64/kernel_main_x86_64.c. In every arch image
and in platform_layer.mk.
Gaps: no host test.

### bootlegger  —  peer handshake and post-quantum key exchange
Status: WORKING (tested in verify-all)
What it does: Protocol v3 three-message handshake (HELLO, REPLY, FINISH). Each peer sends a fresh
nonce and an ephemeral ML-KEM-1024 key and signs, with its long-term ML-DSA-87 identity key, the
canonical length-prefixed transcript Identity_A || EK_A || Nonce_B || Identity_B || EK_B || Nonce_A
(with the version and a domain string, SHA-256). Session keys come from both ML-KEM secrets and the
transcript hash (HKDF); FINISH carries key confirmation. Rejects downgrade, zero or replayed nonces
(bounded replay cache), unsigned or wrongly signed ephemeral keys, ephemeral keys that fail the
FIPS 203 modulus check, reflection and pin mismatches. Messages are sealed with ChaCha20-Poly1305.
v3 uses the same category-5 primitives as Vinea and the pqsec hybrid; v2 peers are refused.
Main entry points: bootlegger_identity_init, bootlegger_hs_initiate, bootlegger_hs_respond,
bootlegger_hs_finish, bootlegger_hs_accept, bootlegger_pin_peer, bootlegger_seal_msg/open_msg.
Tests: src/bootlegger/test_bootlegger.c, run by verify-all, 64 checks (success, replay, MITM,
downgrade, tampered transcript, unreduced KEM keys).
Used by: no includes. In every kernel image.
Gaps: no transport (send/recv and everything built on them return ENOTIMPL); the caller supplies
the entropy; ML-DSA-87 needs tens of KiB of stack. Vinea's session (vna_session) is the live one;
whether bootlegger is folded into it or archived is A5 in docs/GAP_AUDIT.md.

### bridge  —  one address resolver across Web2, Web3 and Web4
Status: WORKING (tested in verify-all)
What it does: It accepts any of three address types:
- a host name (resolved by DNS);
- a content address (CID or DID);
- a Chiglet "intent".
It classifies the address and sends it to the matching backend. A realm with no backend returns
NOT_BOUND; nothing is invented. The "Web4" realm means Chiglet intents and nothing more.
Main entry points: bridge_init, bridge_set_ops, bridge_classify, bridge_resolve,
bridge_gateway_route, bridge_realm_name.
Tests: src/bridge/test_bridge.c, run by verify-all, passes.
Used by: bootfeat and ipfs. In every kernel image and in platform_layer.mk.
Gaps: resolution depends on which backends the host binds.

### bringup  —  shared link symbols for the non-arm64 kernels
Status: PARTIAL
What it does: A 33-line file. It defines the global vfs, net, router and scheduler objects, routes
fb_puts to the serial port, and supplies a no-op raise(). This lets the minimal x86_64, RISC-V and
arm32 kernels link.
Main entry points: none (it only defines globals, fb_puts and raise).
Tests: none. Linking the other arch kernels is the only check.
Used by: Makefile.arm32/riscv/riscv32/x86_64 (Makefile.arm64 mentions it only in a comment).
Gaps: raise() swallows divide-by-zero traps from libgcc.

### broker  —  peer-to-peer marketplace for content-addressed information goods
Status: WORKING (tested in verify-all)
What it does: Sellers list an item by its SHA-256 content ID, with a signature and a capability
contract. Delivery re-hashes the bytes, so a buyer cannot be handed something else. Payment goes
through a settlement backend the host plugs in; with none installed it fails closed. It also
covers HKDF key derivation and a five-way tribute split that never loses a unit.
Main entry points: broker_init, broker_trust_author, broker_set_verifier,
broker_set_settlement, broker_list, broker_settle, tribute_split, aipi_hs_begin.
Tests: src/broker/test_broker.c, run by verify-all, 47 checks.
Used by: pirate_apps. In every kernel image and in economy_layer.mk.
Gaps: none in signing: the seller signs broker_listing_digest (canonical CID, function, scope,
max_price; src/provenance/zx_provenance.h) and broker_settle refuses an amount above the signed
max_price.

### browser  —  minimal web browser engine (URL, HTTP, HTML tokenizer, layout)
Status: WORKING (tested in verify-all)
What it does: Parses and resolves URLs, builds HTTP/1.1 requests, and de-chunks responses. It
tokenizes HTML and lays out boxes for display. The network functions return NO_TRANSPORT unless
the host binds a transport. Real, bounded parsing code.
Main entry points: browser_parse_url, browser_resolve_url, http_bind_transport, browser_init,
browser_new_tab, browser_close_tab.
Tests: src/browser/test_browser.c, run by verify-all, 386 checks, also clean under ASan. An
HTML/URL/HTTP fuzz in this audit was clean.
Used by: no includes. In Makefile and Makefile.arm64.
Gaps: test_browser.c had CodeQL overflowing-snprintf warnings (fixed with a clamped append
helper).
Gaps: no TLS or JavaScript (out of scope for this module).

### call  —  voice and video call stack (RTP, STUN, FEC, jitter buffer, congestion control)
Status: WORKING (tested in verify-all)
What it does: The pieces of a real-time call:
- RTP packets;
- STUN;
- rendezvous and signalling;
- a mesh for group calls;
- forward error correction;
- a jitter buffer;
- congestion-control feedback.
The protocol logic is real and bounded. Media codecs and sockets come from the host.
Main entry points: call_cc_fb_write, call_cc_fb_parse, call_cc_rx_init, call_cc_rx_on_packet,
call_cc_tx_init, call_cc_tx_on_feedback.
Tests: src/call/test_call.c, run by verify-all, 67364 checks. A fuzz of every parser (RTP, STUN,
signalling, FEC, jitter) under ASan/UBSan in this audit was clean.
Used by: harmonic. Host-tested library.
Gaps: none found in the parsers.

### capctl — capital-control profiles as signed plug-ins (off by default)
Status: WORKING (tested in verify-all)
What it does: holds one signed capital-control profile per country (per-transfer and rolling limits, report thresholds, purpose codes, corridors) and answers ALLOW, ALLOW_WITH_REPORT or DENY with a reason for a proposed cross-border transfer. While disabled, every transfer is ALLOW and nothing is recorded. cp_common.h is the signed manifest format shared with taxhook and kycwrap, and cpk.h is the ML-DSA-87 signer keyring scoped by kind and country.
Main entry points: capctl_init, capctl_enable, capctl_install, capctl_require, capctl_evaluate, capctl_commit, capctl_encode / decode; cpk_init, cpk_add, cpk_verify.
Tests: test_capctl.c (123 checks), in mk/compliance.mk test-compliance (with taxhook and kycwrap), which runs in verify-all.
Used by: taxhook and kycwrap use its cp_common.h manifest and cp_test.h helpers. No runtime caller.
Gaps: the profiles in the tests are test data, not any real country's rules. No currency conversion; months and years are rolling day windows. Report records are not sent anywhere. It is not connected to zxvd's jurisdiction methods or registered with the juris plug-in host (its header still calls juris unmerged). See [retail/COMPLIANCE_PLUGINS.md](retail/COMPLIANCE_PLUGINS.md).

### capmkt  —  market for buying compute, storage or bandwidth from other machines
Status: WORKING (tested in verify-all)
What it does: Buyers bid and providers ask. A uniform-price auction clears each round, and
buyers' funds go into escrow. Providers are paid per proof of delivery, minus the 0.08889% assurance fee to the
four fee buckets, and undelivered escrow is refunded with no fee. The products are overflow-bounded, and a
conservation audit checks every account.
Main entry points: cm_params_default, cm_init, cm_deposit, cm_withdraw, cm_ask, cm_bid, cm_clear,
cm_deliver.
Tests: src/capmkt/test_capmkt.c, run by verify-all, 62 passed.
Used by: devmesh and provider. In kernel/Makefile only (host library, not in a boot image).
Gaps: proof of delivery is whatever the host's verify callback accepts.

### cardnet  —  charge-card network model (PANs, ISO 8583, EMV TLV, mobile push)
Status: WORKING (tested in verify-all)
What it does: Issues charge cards on three in-house networks (no interest). It checks card
numbers (Luhn), builds and parses ISO 8583 messages and EMV TLV data, and models mobile payment
pushes. This is a model: it does not connect to Visa, Mastercard or any real card scheme.
Main entry points: cn_network_name, cn_network_check_digit, cn_pan_valid, cn_pan_network,
cn_form_priceable, cn_fee_validate.
Tests: src/cardnet/test_cardnet.c, run by verify-all, 6365 passed. An ISO 8583/EMV/amount fuzz in
this audit was clean.
Used by: no includes. Host-tested.
Gaps: the cn_mobile push callback is unauthenticated, and this is not disclosed.
Gaps: free_card_slot reuses revoked slots, so a revoked PAN can be minted again.

### cards  —  glyph/sigil cards, loadouts and the ZCA card assembler
Status: WORKING (tested in verify-all)
What it does: Each card grants capabilities. A loadout equips cards and recomputes what the holder
is allowed to do. The ZCA assembler and the sigil encoder turn cards into bytes and back.
Main entry points: card_evidence, card_grants, loadout_init, loadout_equip, loadout_unequip,
loadout_has, loadout_apply.
Tests: test_cards.c, test_sigil.c and test_zca.c, all run by verify-all and passing. A ZCA and
sigil fuzz in this audit was clean.
Used by: art_studio, bootfeat, reality and refinery.
Gaps: none found.

### cbank  —  central-bank toolkit (ISO 4217 currencies, settlement net, usury, ISO 20022 MX)
Status: WORKING (tested in verify-all)
What it does: The pieces are:
- ISO 4217 currency and ISO 3166 country tables, generated from published lists, plus the
  African Union member list;
- multilateral netting;
- a usury (interest-cap) checker;
- verifiable secret sharing;
- an ISO 20022 MX XML writer;
- overflow-safe 64-bit multiply and divide helpers.
These are data and message-building tools only; they connect to no payment network.
Main entry points: cb_ccy_by_alpha, cb_ccy_by_num, cb_ccy_payable, cb_country_by_a2,
cb_ccy_for_country, cb_mul64.
Tests: test_cb_ccy (85), test_cb_mx (39), test_cb_net (98), test_cb_usury (29) and test_cb_vss
(58), all run by verify-all. In this audit, cb_mul64 and the muldiv helper matched __int128 on 3
million random cases.
Used by: no includes. In kernel/Makefile only (host library).
Gaps: cb_w_esc lets control characters through into the XML output.

### cellular_multikernel  —  fault-isolated kernel "cells"
Status: WORKING (tested in verify-all)
What it does: Defines a cell contract and a control plane for discovering, authenticating,
admitting, activating and revoking independent kernel cells. Cells talk through the versioned
event ABI. It is a registry and a state machine; it does not isolate memory or schedule cells
itself.
Main entry points: cell_fabric_init, cell_contract_zero, cell_set_digest,
cell_fabric_discover, cell_fabric_authenticate, cell_fabric_admit, cell_fabric_activate,
cell_fabric_revoke.
Tests: ../tests/host/test_cellular_multikernel.c, run by verify-all, 15/15.
Used by: the arm64 and x86_64 kernel_main and compute_fabric (dead). In every kernel image.
Gaps: fault containment is a declared property, not something the module enforces.

### chiglet  —  small on-device "AI" built from a mixture of tiny linear experts
Status: WORKING (tested in verify-all)
What it does: Combines tiny linear predictors. Each expert is weighted by how independent its
evidence is from the others, using ISF g(u) where a neural net would use softmax. The header says
plainly that this is not an LLM or a neural network.
Main entry points: chg_init, chg_load_model, chg_interaction, chg_effective_experts, chg_infer,
chg_state_name.
Tests: src/chiglet/test_chiglet.c in verify-all. It is also linked by about 17 other tests.
Used by: the arm64 kernel_main, cards, concord, desktop, emu, games, holodeck, pirate_fleet,
social, voice and wyrmgate.
Gaps: chiglet_triage.c (a host triage tool; implicit strtok_r, never built) moved to
kernel/experimental/chiglet_triage/ (GAP_AUDIT B3).

### choice  —  deterministic tie-breaking selection among candidates (K5)
Status: WORKING (tested in verify-all)
What it does: Registers candidates with priority, weight and capability, then picks one
deterministically using an explicit tie-break policy. Choices it cannot decide are deferred to the
S0 state, and the selection history is kept for audit.
Main entry points: choice_registry_init, choice_register_candidate, choice_decide,
choice_get_decision, choice_set_candidate_active, choice_advance_time.
Tests: ../tests/host/test_choice.c is in verify-all. src/choice/test_choice.c is in the
kernel-Makefile `test` target. Both pass.
Used by: every arch kernel_main, tests/host and vena.
Gaps: none found.

### chronicle  —  hash-chained ledger of verdicts that re-judges deferred events
Status: WORKING (tested in verify-all)
What it does: Each Wyrmgate verdict is appended to a SHA-256 hash chain, rejections included. If
an earlier entry is changed, the chain no longer verifies. A deferred verdict is kept pending and
judged again when chronicle_poke is called. A redo journal makes saves atomic.
Main entry points: chronicle_init, chronicle_submit, chronicle_poke, chronicle_head,
chronicle_length, chronicle_verify, chronicle_pending_event.
Tests: test_chronicle.c and test_chronicle_persist.c, both run by verify-all.
Used by: no includes. Kernel builds via -I paths.
Gaps: none found.

### civilizational_stack  —  application deployment and translation "stack" (dead)
Status: UNUSED/DEAD
Location: kernel/experimental/civilizational_stack/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: Meant to deploy built-in apps, translate them between language runtimes and
self-audit them. It does not compile and no build file references it. It claims to be
"military-grade".
Main entry points: cs_init, cs_deploy_builtin_apps, cs_deploy_app, cs_translate_app,
cs_self_audit_app.
Tests: none.
Used by: nothing.
Gaps: does not compile.
Gaps: the "military-grade" claim is unsupported.

### clock  —  event-driven clock (ordinals, with wall-clock sync only on demand)
Status: WORKING (tested in verify-all)
What it does: The kernel counts events with ordinals and touches wall-clock time only when an
external interface asks for it. This avoids a constant timer tick. The module keeps the two
counters separate and records when a sync is needed.
Main entry points: event_clock_default, event_clock_init, event_clock_next_ordinal,
event_clock_get_ordinal, event_clock_sync, event_clock_get_time, event_clock_needs_sync.
Tests: src/clock/test_event_clock.c, run by verify-all, passes.
Used by: desktop (clock display), with no direct includes. Kernel builds via -I paths.
Gaps: none for floating point: drift is integer ppm and the correction is Q32.32 (fixed).

### codec  —  "Tri-Space" media codec that keeps the lossy remainder
Status: WORKING (tested in verify-all)
What it does: A lossy transform codec that stores the quantised data. The part it would throw
away goes into a separate remainder stream, so the original can be restored exactly. It is real
integer code. "Lossy but not lossy" means lossy plus a residual; the claim holds only if you keep
both files.
Main entry points: zm_encode_positive, zm_decode_positive, zm_build_negative, zm_build_neutral,
zm_restore_exact.
Tests: src/codec/test_zmedia.c (test_zm), run by verify-all, passes.
Used by: arch/arm64/kernel_main_arm64.c.
Gaps: none found.

### community_chest  —  peer-to-peer app store with paid apps and vouchers
Status: WORKING (tested in verify-all)
What it does: Developers list signed apps. Users verify, download, install and buy them, and
deprecated apps can be retired. Revenue is split three ways (developer, royalty, platform), and
vouchers can be cashed in and out through the Vino ledger. Several money paths were wrong and
are fixed. Download remains a stub.
Main entry points: cc_init, cc_list_app, cc_verify_app, cc_download_app, cc_install_app,
cc_purchase_app, cc_purchase_with_vouchers, cc_calc_revenue_split.
Tests: src/community_chest/test_community_chest.c, run by verify-all. It passes, also under ASan,
and now has conservation and escrow assertions.
Used by: arch/arm64/kernel_main_arm64.c.
Gaps: the split paid out 105% at the 85% developer share (fixed).
Gaps: the voucher purchase debited the buyer and credited no one (fixed: the money now goes to
escrow).
Gaps: cash_in and cash_out did not check the capital index (fixed).
Gaps: cash_in mints vouchers with no proof of external payment.
Gaps: the signature covers only name and pubkey, not content_hash or price.
Gaps: the "post-quantum signature" label is wrong, because it is Ed25519.
Gaps: download is a stub, and cc_purchase_app takes no payment.

### compute_fabric  —  compute-cell manager with domain migration (dead)
Status: UNUSED/DEAD
Location: kernel/experimental/compute_fabric/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: Meant to create, start, stop and migrate compute cells across domains. It does not
compile (missing types) and no build file references it.
Main entry points: cf_init, cf_register_builtins, cf_create_cell, cf_start_cell, cf_stop_cell,
cf_migrate_domain.
Tests: none.
Used by: app_constellation and app_fabric (both dead).
Gaps: does not compile, is not built and has no test.

### concord  —  social matching layer that avoids engagement-maximising feeds
Status: WORKING (tested in verify-all)
What it does: Decides who is introduced to whom in the peer-to-peer social space. It uses
Interaction Surplus scores and a per-person "standing". Someone with low standing meets others
with similar standing, and standing recovers over time. Each person can set boundaries and report
when one is crossed.
Main entry points: con_init, con_join, con_standing, con_surplus, con_may_match, con_recommend,
con_report_boundary.
Tests: src/concord/test_concord.c, run by verify-all, passes.
Used by: desktop, governance_fabric, identity_fabric, market, pirate_fleet and social_spaces.
Gaps: none found.

### constellation  —  coordinator for many machines (node registry, routing, failure detection)
Status: WORKING (tested in verify-all)
What it does: Keeps a registry of nodes. Each node goes from discovered to authenticated to
active, and to failed. The coordinator records each node's capability and cost profile, routes
events to a suitable node, and marks nodes that stop responding as failed. It is bookkeeping;
the network transport comes from elsewhere.
Main entry points: cc_coordinator_init, cc_register_node, cc_node_add_schema,
cc_node_authenticate, cc_node_activate, cc_node_fail, cc_get_node.
Tests: ../tests/host/test_orbital_constellation.c, run by verify-all, passes.
Used by: the arm64 kernel_main, compute_fabric, interspace, orbital_fabric and tests/host. In every
kernel image.
Gaps: none found in the reviewed paths.

### cotier  —  co-processor tiers (accelerator gate, budget, measurement, reconcile)
Status: WORKING (tested in verify-all)
What it does: Decides how hard an accelerator may be driven and whether its result may be
stored. The decision uses an open-system "headroom" budget, a gate on the output, and a
reconcile step against a reference. Cost comes from a hardware callback. The constants have to be
calibrated per machine, as the header says.
Main entry points: ct_budget_init, ct_cost, ct_budget_headroom, ct_budget_step, ct_storage_ok,
ct_budget_commit.
Tests: test_ct_budget (29), test_ct_gate (25), test_ct_measure (30) and test_ct_reconcile (32), all
run by verify-all.
Used by: no includes. Kernel Makefile only.
Gaps: test_ct_measure.c had CodeQL index-overflow warnings at 316/317/344/356 (fixed with size_t
widening that matches upstream).

### count_house  —  stash-bucket reserves and the floor price of a node's own currency
Status: WORKING (tested in verify-all)
What it does: Peers deposit tokens into ring-fenced "stash buckets", and each deposit needs an
Ed25519 proof. Each bucket is weighted by peer trust, and crypto reserves are added. The total
gives a floor price for the node's self-minted currency, and minting is refused if the
collateral ratio would fall below a floor. A fractal mode aggregates child houses.
Main entry points: count_house_init, count_house_deposit, count_house_find_bucket,
count_house_set_crypto_reserves, count_house_mint, count_house_valuation.
Tests: test_count_house.c is in verify-all and passes; it now includes the wrap case and the deposit top-up wrap (-3) found by fuzz/econ/fuzz_econ_count_house.
test_count_house_fractal.c is not in verify-all. It passes and has a proposed verify line.
Used by: the arm64 kernel_main, finance and mesh_token. In every kernel image.
Gaps: count_house_mint let the supply counter wrap to 0 and returned the huge amount (fixed).
Gaps: valuation and the trust weights are model parameters (CH_TRUST_*, a 0.001 floor), not market data.

### crash — crash records on disk and boot modes (normal, safe, recovery)
Status: WORKING (tested in verify-all)
What it does: on a fatal fault, captures registers, a backtrace, the last 2 KiB of console output, a reason and a message, seals the record with SHA-256 and writes it to a reserved disk area through raw sectors. The next boot reads, reports and clears it. bootctl counts unconfirmed boots and steps down to SAFE or RECOVERY mode.
Main entry points: crash_begin, crash_record_fill, crash_backtrace, crash_area_attach, crash_area_save / load / clear, crash_log_puts; bootctl_begin, bootctl_mark_good, bootctl_request, bootctl_load / save.
Tests: test_crash.c (59 checks, plain and ASan/UBSan) plus a freestanding build for aarch64, i386, armv7a and rv32, target test-crash, in verify-all.
Used by: the arm64 kernel image (build_system/Makefile.arm64; kernel_main_arm64.c boot_crash_check, arm64_exceptions.c fault handler).
Gaps: arm64 only: x86_64, RISC-V and ARM32 fault handlers still only print and halt. With no persistent disk (as in CI boots) nothing is kept. Not a full memory dump. A key held at boot to pick safe mode is not wired. See [CRASH_RECOVERY.md](CRASH_RECOVERY.md).

### cred — EigenTrust credibility from fulfilled trades, bounded unlocks and the Player Card
Status: WORKING (tested in verify-all)
What it does: scores subjects from fulfilled orders only (log gain per trade, exponential loss per dispute ruling, decay per epoch) and computes EigenTrust global trust in fixed point. The score maps to a shorter escrow hold and a zero-interest credit line, and Player Card tiers come from sicily's sic_tier_eval. Nothing accrues with time or balance.
Main entry points: cred_init, cred_subject_add, cred_fulfil, cred_fail, cred_decay_epoch, cred_trust_compute, cred_credibility, cred_escrow_hold, cred_credit_line, cred_player_card, cred_card_json, cred_check.
Tests: test_cred.c (221 checks, ASan/UBSan), target test-cred, in verify-all. test_dispute.c also links it.
Used by: airc (trust tiers, airc.h includes cred.h). zxvd's player.card does not use cred; it calls sicily directly (zxv_host.c).
Gaps: no runtime caller outside airc, which itself has none. Sybil resistance holds only as far as the pre-trust rules in cred.h C6. Credit lines are regulated lending and are off by default; the module only computes a number and is not a credit score. No doc of its own (rules are in cred.h).

### crit168  —  canonical serialisation and integrity framing (UBH-168)
Status: WORKING (tested in verify-all)
What it does: Turns kernel data into canonical 168-bit-word frames with a header, a CRC-32 and a
payload hash. It handles byte order and version tags, and parses frames back with length
checks.
Main entry points: crit168_registry_init, crit168_register_format, crit168_serializer_init,
crit168_serialize_data, crit168_serialize_header, crit168_serialize_checksum.
Tests: ../tests/host/test_crit168_os.c is in verify-all. src/crit168/test_crit_168_word.c is in
the kernel-Makefile `test` target. Both pass.
Used by: tests/host, and widely via -I.
Gaps: CRC-32 detects accidents only; it is not a security check (the header says so).

### crown  —  credential office: issues and verifies identity credentials, never touches money
Status: WORKING (tested in verify-all)
What it does: Issues signed sovereign credentials (ISC). It verifies their signatures, revokes
them, and registers lineage records. By design it has no path to any wallet or settlement code.
The "Sicilian Crown" theme is narrative only.
Main entry points: crown_init, crown_register, crown_isc_build_preimage,
crown_isc_attach_signature, crown_isc_verify, crown_isc_is_active, crown_revoke,
crown_chn_register.
Tests: src/crown/test_crown.c, run by verify-all, 37 assertions.
Used by: bootfeat, desktop, governance_fabric and identity_fabric. In every kernel image and in
economy_layer.mk.
Gaps: the FINANCIAL and OVERSIGHT steps are opaque stubs, which the header discloses.

### crypto_wallet  —  five-key wallet file system (derived keys, MACed files)
Status: WORKING (has a test, not in verify-all)
What it does: Derives five keys from a root seed with HMAC-SHA256. Each stored file gets a MAC,
an integrity check catches tampering, and a file can be "phase-shifted" to a different key. The
SHA-256 and HMAC are correct: they match the FIPS 180-2 and RFC 4231 vectors.
Main entry points: cw_system_init, cw_system_set_root_seed, cw_derive_key, cw_wallet_create,
cw_file_add, cw_verify_integrity, cw_file_phase_shift, cw_hmac_sha256.
Tests: NEW src/crypto_wallet/test_crypto_wallet.c, 16 checks, 0 failures, with a proposed verify
line. It is also covered by test_crypto_wallet() in ../tests/test_new_modules.c, which nothing
runs.
Used by: tests/test_new_modules.c and sdk_bridge. In every kernel image.
Gaps: the comment says "HKDF-Expand" but the code is HMAC(seed, seed||ids).
Gaps: it carries its own duplicate SHA-256.
Gaps: the MAC comparison is not constant-time.
Gaps: integrity checks only the stored hash, not the payload bytes.

### curzi  —  CURZI-8889-A composite post-quantum key setup with tiered access
Status: WORKING (has a test, not in verify-all)
What it does: Combines standard primitives (ML-KEM, ML-DSA/SLH-DSA via pqsec) with Shamir
threshold sharing and Hamming(8,4)/Golay error-correcting framing. Access is tiered. The header
states that it is a composition of existing primitives, not a new hard problem.
Main entry points: curzi_split, curzi_combine, curzi_hamming84_encode, curzi_hamming84_decode,
curzi_golay_encode, curzi_golay_decode, curzi_frame_encode, curzi_frame_decode.
Tests: test_curzi_code.c, test_curzi_shamir.c (612 checks) and test_curzi_tier.c (116299 checks).
All pass when built by hand and none is in verify-all. Proposed verify lines are written.
Used by: no includes. In the arm64 kernel image.
Gaps: the build lines written in the test comments lack the -I paths they need.
Gaps: curzi8889a.c:244 sets `header` and never uses it.

### decent  —  decentralised building blocks (IPFS-style CIDs, swarms, encrypted rooms)
Status: WORKING (tested in verify-all)
What it does: Covers:
- content IDs with a node table and a bitswap model;
- BitTorrent-style swarms;
- DIDs;
- encrypted chat rooms with an event log.
Capabilities that are declared but not built return an explicit NOT_IMPLEMENTED, never success.
The header says it is not an implementation of IPFS itself.
Main entry points: decent_init, decent_ipfs_node_add, decent_ipfs_cid_register,
decent_ipfs_cid_find, decent_ipfs_bitswap, decent_bt_swarm_create.
Tests: src/decent/test_decent.c, run by verify-all, passes.
Used by: tests/test_new_modules.c, and bridge via -I.
Gaps: the room AEAD nonce is the event index under a shared room key, so two members posting at
the same index reuse a nonce.

### denconnect  —  operator-defined privileges for Den Connect servers
Status: WORKING (tested in verify-all)
What it does: Each server operator defines the roles and privileges, guest access, visibility and
bans. Guards stop a guest from granting themselves admin, and the operator cannot be banned. It
replaces the fixed four-role scheme.
Main entry points: den_init, den_privileges, den_can, den_may_connect,
den_set_guest_privileges, den_set_visibility, den_set_account, den_ban.
Tests: src/denconnect/test_denconnect.c, run by verify-all, passes.
Used by: holodeck. In every kernel image.
Gaps: none found.

### deploy  —  sizes the OS to the machine it runs on
Status: WORKING (tested in verify-all)
What it does: Looks at core count, memory and node count. It classifies the machine (embedded,
desktop, server, supercomputer) and picks how many cells and nodes to use, with a hard ceiling.
Main entry points: deploy_classify, deploy_resolve, deploy_class_name, deploy_exec_name,
deploy_summarise.
Tests: src/deploy/test_deploy.c, run by verify-all, passes.
Used by: bootfeat.
Gaps: none found.

### desktop  —  window manager, taskbar and shell UI
Status: WORKING (tested in verify-all)
What it does: Windows that can be created, closed, minimised, maximised, focused and moved, plus a
taskbar, a launcher, a tray and a dragon theme. render_boot.c draws the boot screen, and
zxv_shell.c is the graphical shell. The window-manager logic is real; drawing goes through the
framebuffer.
Main entry points: desktop_init, desktop_set_dragon_theme, desktop_create_window,
desktop_close_window, desktop_minimize_window, desktop_maximize_window, desktop_focus_window,
desktop_move_window.
Tests: ../tests/host/test_desktop.c, run by verify-all, passes.
Used by: arch/arm64/kernel_main_arm64.c and tests/host. In Makefile, Makefile.arm64 and the other
arch images.
Gaps: none for floating point: coverage is Q16.16 and drift_ppm an integer (fixed).
Gaps: zxv_shell.c fails the hosted build with an SR_SHIFT error but is clean in the arm64 build.

### devmesh  —  private mesh of one person's own devices
Status: WORKING (tested in verify-all)
What it does: Each device has a post-quantum identity, and a signed roster lists the members,
their roles and any revoked devices. Pairing uses a short code. Sessions are mutually
authenticated and encrypted, with separate send and receive keys and counter nonces, and replays
are rejected. It also syncs files and lends capacity through capmkt. Transport and device
discovery come from the host.
Main entry points: dm_init, dm_create, dm_wipe, dm_self_id, dm_roster, dm_roster_find,
dm_is_admin, dm_invite.
Tests: src/devmesh/test_devmesh.c, run by verify-all, 146 passed.
Used by: no includes. In kernel/Makefile only (host library).
Gaps: none found in the session or replay paths reviewed.

### dharana  —  array of 112 gates in four classes (symbolic naming)
Status: WORKING (has a test, not in verify-all)
What it does: Builds 112 small signal-processing gates in four classes of 28. The classes are
filters, settling and dual/non-dual comparison. The names come from the Vijnana Bhairava Tantra
only as labels, and the header says the maths is ordinary.
Main entry points: dharana_class_of, dharana_class_name, dharana_gate_name, sandhi_init,
sandhi_feed, visranti_init, visranti_settle, dvaitadvaita_init.
Tests: src/dharana/test_dharana.c. It passes when built with src/rmag/rmag_core.c and is not in
verify-all. A proposed verify line is written.
Used by: no includes. In the arm64 image via -I.
Gaps: the documented build line is stale; it is missing rmag_core.c.

### dharma — Dharma scheduler family (dharma, karma, chakra, tantra, naga_raja, upaah, ...)
Status: WORKING (has a test, not in verify-all)
What it does: This is a set of scheduler policies named after Buddhist and Hindu ideas. Each one
picks the next task from integer counters ("karma" credit, chakra levels, trit phases).
naga_raja moves CPU quota between tasks as exact fractions. upaah is the 3-valued "phase bridge"
logic. The scheduling arithmetic is real; the names are metaphor.
Main entry points: dharma_init/dharma_advance/dharma_emit, naga_raja_spawn/send/recv/terminate,
phase7_bridge/phase7_unbridge, upaah_bridge, tantra_init, tantra_paradox_trap, chakra_init.
Tests: test_dharma (21), test_tantra (13), test_chakra_naga (17), test_upaah (17). All pass. None is
in verify-all; verify-all builds upaah.c only through test_refinery and test_wyverneye. Before
the fixes, test_chakra_naga had 6 failures.
Used by: refinery. In all images.
Gaps: naga_raja quota send minted quota on a self-send and dropped the first transfer into an empty
slot (fixed). It used double (fixed: integer). rmag slots start at 0/0 (rmag is out of scope).

### display — display mode negotiation and UI scaling
Status: WORKING (tested in verify-all)
What it does: This picks a screen mode (up to 4K or 8K, depending on a build-time budget) from
what the firmware offers. It works out the framebuffer size and scales the UI. Everything is
integer.
Main entry points: zxv_display_negotiate, zxv_display_mode, zxv_display_mode_count,
zxv_display_bytes, zxv_display_scaled.
Tests: src/display/test_display.c. verify-all runs it in 3 budgets (QHD/4K/8K): 21/20/22 checks,
all pass. A check for a short firmware stride was added.
Used by: arch/arm64, desktop, media_fabric. In all images.
Gaps: a firmware stride smaller than the width was accepted, so the framebuffer was undersized
(fixed).

### dispute — three-level dispute funnel: proofs, staked blind jury with appeals, jurisdiction gateway
Status: WORKING (tested in verify-all)
What it does: level 1 settles a case from signed deterministic proofs through a caller predicate. Level 2 draws a staked jury with a caller-supplied VRF, takes blind commit-reveal binary votes, and allows appeals at 1x, 4x and 16x bonds. Level 3 takes a ruling signed by a jurisdiction gateway key. It moves only bonds on pay_ledger (slashed bonds go to the reserve-floor account) and produces a ruling; it never moves the order's escrow.
Main entry points: dsp_init, dsp_juror_add, dsp_open, dsp_proof, dsp_escalate, dsp_select, dsp_commit, dsp_reveal, dsp_tally, dsp_finalize, dsp_gateway_rule, dsp_ruling, dsp_ruling_digest, dsp_award_amount, dsp_check.
Tests: test_dispute.c (309 checks, ASan/UBSan), target test-dispute, in verify-all.
Used by: no caller outside its tests. (The dsp_ prefix in zui/views/display.c is an unrelated display module.)
Gaps: there is no real VRF in the tree; the test uses a SHA-256 stand-in that is not a VRF, and production needs one such as ECVRF (RFC 9381). Votes are binary below level 3. Juror bribery is made costly, not prevented. Not connected to the market escrow or to cred_fail by any caller. Needs legal review per jurisdiction. No doc of its own (rules are in dispute.h).

### dual_space — paired positive/negative artifact registry
Status: WORKING (tested in verify-all)
What it does: This registers "pairs" of artifacts, such as a program and its inverse, with
digests, schemas and peer digests. It checks that both halves agree before a pair is released. It
is a bookkeeping registry. Its "digests" are bytes that callers supply.
Main entry points: ds_init, ds_register_pair, ds_add_artifact, ds_set_artifact_digest,
ds_set_peer_digest, ds_set_release_signature.
Tests: tests/host/test_dual_space.c (verify-all test_dual_space), pass.
Used by: compute_fabric, arch/arm64, tests/host. In all images.
Gaps: the pair digest is an XOR of member digests, not a hash. The release signature is stored and
never verified.

### dualtrack — dual-track (design vs. evidence) gate pipeline
Status: PARTIAL
What it does: This tracks a project through stages. Each stage has a "gate" that passes when both
tracks agree and the attached artifacts have a hash. It reports an agreement rate.
Main entry points: dualtrack_init, dualtrack_stage_add, dualtrack_run_stage, dualtrack_eval_gate,
dualtrack_run_all, dualtrack_artifact_add.
Tests: none of its own; tests/test_new_modules.c references it but nothing builds that file.
Used by: tests only. In all images.
Gaps: "signed / hash-verified" artifacts are only checked for a nonzero hash byte (line 396).
dualtrack_agreement_permille() returns an integer (fixed; was a float rate).

### e8 — E8 lattice / icosian ring over Z[phi]
Status: WORKING (tested in verify-all)
What it does: This is exact integer arithmetic in Z[phi] (numbers a + b*golden-ratio) and the 240
icosians that make up the E8 root system. It is real mathematics with no approximation.
Main entry points: zphi_make/zphi_add/zphi_mul, e8_icosian, e8_icosian_count, icos_mul,
icos_norm4, icos_eq.
Tests: test_zt_e8 in verify-all (269 checks, 0 failures). src/e8/test_e8.c is not in verify-all,
passes, and is added to group2_verify_lines.mk.
Used by: axiom_matrix, tensor, tolvovina, arch/arm64; on CPATH for every verify-all build.
Gaps: none found.

### edgeclus — edge cluster scheduler on the swarm token budget
Status: WORKING (tested in verify-all)
What it does: each node publishes an ML-DSA-87 signed advert (models, measured speeds, free memory, queue depth, price) whose node id is its Vinea NodeID with proof of work. Every node runs the same integer functions over the adverts it heard: split the cycle's token budget with the swarm's Fibonacci allocator under a per-node cap, assign requests, and move a failed node's work. It does no networking itself.
Main entry points: ec_advert_seal, ec_advert_open, ec_cluster_init, ec_cluster_ingest, ec_cluster_expire, ec_cycle_begin, ec_schedule, ec_failover.
Tests: test_edgeclus.c (102 checks) plus a clang warning check and a freestanding build, in mk/edge.mk test-edge, which runs in verify-all. test_edge_pkg (47 checks) covers the edge package build, not this module.
Used by: no caller outside its tests.
Gaps: not wired into zxvd: a node does not send adverts or take cluster work. zxvd holds one model at a time. Advertised speeds are self-reported. Nothing has run on a real Raspberry Pi 5, Jetson or mini PC (only qemu user mode). See [retail/EDGE_NODES.md](retail/EDGE_NODES.md).

### edp_risk — coverage/"EDP" risk primitives
Status: WORKING (tested in verify-all)
What it does: This is a shared helper library. It checks the project's coverage rule (r times l
must be at least 1.8). It also computes deficits, Fibonacci numbers and paired
"annihilator" values. Many finance and device modules call it.
Main entry points: edp_coverage_satisfied, edp_coverage_product, edp_coverage_deficit,
edp_fibonacci, edp_annihilate, edp_pvd, edp_zpd.
Tests: no test of its own. It is exercised by about 20 verify-all tests (finance_markets,
count_house, mesh_net, ...), all pass.
Used by: 34 includers (finance, hardware, epu, event_sched, net, ...). In all images.
Gaps: edp_fibonacci wraps silently for n > 47. The phase "atan2" is simplified to a ratio (line
303).

### ehop — endian-hopping encrypted frame transport
Status: WORKING (tested in verify-all)
What it does: This frames and encrypts messages with ChaCha20-Poly1305 and uses a per-channel
replay window. The byte order "hops" on a keyed schedule. Peer welcome uses ML-KEM/ML-DSA from the
crypto modules. The crypto is real; the hopping is an obfuscation layer on top of it.
Main entry points: ehop_channel_init, ehop_schedule_init, ehop_apply, ehop_cfg_check,
ehop_frame_* / ehop_net_*.
Tests: src/ehop/test_ehop.c (verify-all), pass. A local fuzz of frame parsing and the replay window
found nothing.
Used by: zxvd (`kernel/arch/hosted/zxvd_rpc_modules.c`, tested by `test_zxvd_rpc_modules.c` and `test_zxvd.py`): ehop.bands, and ehop.seal / ehop.open on a channel given per call
(stateless: the caller keeps seq and the replay window). The mobile core build. Not in images.
Gaps: none found in the parser or AEAD use.

### el0_userspace — EL0 (ARM64 user mode) process scheduler
Status: WORKING (has a test, not in verify-all)
What it does: This keeps the ARM64 kernel's table of user processes: create, round-robin
switching, time slices and termination. The header lives in arch/arm64.
Main entry points: proc_sched_init, proc_create, proc_create_from_elf, proc_terminate,
proc_switch_address_space, copy_from_user/copy_to_user.
Tests: src/el0_userspace/test_el0_userspace.c (16 checks, pass). It needs -Iarch/arm64. It is not
in verify-all and is added to group2_verify_lines.mk.
Used by: arch/arm64 (Makefile.arm64 only).
Gaps: terminating an already-terminated process decremented num_procs again (fixed).

### emu — console emulators and game "dimension" layers
Status: PARTIAL
What it does: This has CPU cores (6502, 65816, Z80, LR35902, HuC6280, M68k, ARM7) and machines
(NES, SNES, GB/GBA, SMS, Genesis, PCE) that load ROM images. It also has a large set of
interpretive layers (cinder, helion, dimfold, break_potency, ...) that turn emulator state into
the project's M5 vocabulary. The CPU cores are real and pass instruction tests; the layers are
analogy.
Main entry points: cpu6502_*/z80_*/arm7_* step functions, nes/gb/gba/snes/sms/genesis/pce load
and run, game_runner_*, emu_relate_*.
Tests: test_cpu6502, test_cpu_z80, test_emu_relate, test_sms pass and are not in verify-all (added
to group2_verify_lines.mk). test_nes needs ROM arguments and tests nothing without them. A local
fuzz harness over the gb/gba/snes/genesis/pce/sms/nes loaders is clean after the arm7 fix.
Used by: game_runner, desktop, tolvovina, tripartite_fs, arch mains. In all images.
Gaps: arm7 ROR-with-carry shifted a signed int into bit 31, which is UB (fixed). Several layers
used double (break_potency, helion, emu_relate; fixed: integer permille and Q16.16). There are misleading-indentation warnings in 8
files (cpu_z80 fixed).

### epu — "Emotion Processing Unit" device model
Status: WORKING (tested in verify-all)
What it does: This is a software model of an imagined coprocessor. It covers coils, qubits,
interrupts, "emotion" processing and power. It can emit an HDL text skeleton. The header states
that it is a model, not hardware.
Main entry points: epu_handle_irq, epu_process_emotion, epu_qubit_apply_gate, epu_generate_hdl,
epu_recompute_power, epu_verify_coverage.
Tests: src/epu/test_epu_device.c (verify-all test_epu), pass.
Used by: no includers; host test only.
Gaps: it is a double-precision model, so it is no longer linked into the arm64 image (kernel images forbid floating point); a kernel port would need a fixed-point rewrite of the model and its 1e-15 test tolerances.

### estate — estate succession, account recovery and faraid shares
Status: WORKING (tested in verify-all)
What it does: estate.c runs succession phases (reported, confirmed by guardians after a wait, ruled by a gateway where required, distributing, closed) with a living veto that refunds the executor bond, an exact-fraction inheritance manifest, contested-share escrow and a time-delayed key release gate. faraid.c computes advisory Islamic inheritance shares for a small set of heirs. recovery.c counts guardian approvals and old-device vetoes for moving an account to a new device and reports staged thaw limits. It is bookkeeping and arithmetic; the ledger moves the money.
Main entry points: est_init, est_set_manifest, est_report, est_approve, est_confirm, est_rule, est_begin, est_distribute, est_contest, est_resolve, est_stream, est_veto, est_fraud_finding, est_key_release_ok, est_close, est_forced_* (estate.h); est_faraid (faraid.h); rec_init, rec_claim_open, rec_approve, rec_veto, rec_finalize, rec_cancel, rec_device_add / revoke, rec_liquid_allowance, rec_vbill_spendable (recovery.h).
Tests: test_estate.c (270 checks) and test_recovery.c (160 checks), ASan/UBSan, targets test-estate and test-recovery, in verify-all. kernel/arch/hosted/test_zxvd_estate.sh runs in the zxvd.yml CI workflow, not in verify-all.
Used by: zxvd estate.* and recovery.* methods (zxvd_estate.c, linked through app_sources.sh groups), behind the "estate" feature switch, which starts off.
Gaps: zxvd checks signatures with Ed25519 keys while the headers say ML-DSA in production. Guardian notices are counted, not delivered, and recovery moves no account. Faraid covers only spouse, parents, sons and daughters (other relatives are refused). Forced-heirship rows are simplified. Not legal or religious advice. The method list is in [ZXVD.md](ZXVD.md).

### event_sched — event-driven task scheduler with cycle budgets
Status: WORKING (tested in verify-all)
What it does: Tasks wake on posted events, are charged CPU cycles, and are replenished on a budget.
Idle detection and block/unblock are included. It is an integer scheduler.
Main entry points: ev_sched_init, ev_sched_create_task, ev_sched_post_events,
ev_sched_dispatch, ev_sched_charge_cycles, ev_sched_replenish, ev_sched_block/unblock.
Tests: src/event_sched/test_event_sched.c (verify-all), pass.
Used by: arch/arm64. In all images.
Gaps: none found.

### event_space — event envelopes, sequencer, self-audit, self-healing
Status: WORKING (tested in verify-all)
What it does: This is the event plumbing many modules share. Envelopes carry causal parents, a
payload hash and a CRC32. A sequencer orders them. Self-audit and self-healing scan registries for
drift. Its private SHA-256 matches the published test vectors.
Main entry points: ev_envelope_init, ev_envelope_set_payload, ev_envelope_compute_crc,
ev_envelope_validate, ev_seq_init, ev_audit_check_system, ev_healing_init/apply.
Tests: test_event_space plus about 30 verify-all tests that link it (sutra_*, fs, hypercube,
dual_space, ...), all pass.
Used by: about 25 modules (dual_space, fs, hypercube, phase_coord, oseq, ...), and it is on CPATH.
Gaps: the envelope CRC covers struct padding bytes.

### event_transport — channels that carry event envelopes between nodes
Status: STUB
What it does: This creates named channels (local, shared memory, IPC, network) and queues envelopes
on them. Nothing is sent: et_process_pending empties the queue and reports every envelope
"delivered".
Main entry points: et_init, et_create_channel, et_channel_activate, et_send, et_receive,
et_process_pending.
Tests: no test of its own; linked by test_orbital_constellation (verify-all), pass.
Used by: network_fabric, compute_fabric, arch/arm64, tests/host. In all images.
Gaps: no physical transport. The "authenticated network protocol" in the header has nothing behind
it. LOCAL delivers nowhere.
Gaps: the header and .c now open with a STATUS: STUB banner saying that nothing is sent or
authenticated and that the transport kinds are design targets.

### evolve — content-addressed evolution protocol (capabilities, lineage, trust, updates)
Status: WORKING (tested in verify-all)
What it does: This defines content IDs, capability descriptors, lineage records, trust scores and
signed update messages. Its encoders and decoders are length-checked. It is a protocol library with
real parsing.
Main entry points: evo_cid_of, evo_desc_init/add/encode/decode, evo_capset_encode/decode,
evo_msg_decode, evo_dag_init/add_wire/is_ancestor, evo_trust_init, evo_conform_check.
Tests: src/evolve/test_evolve.c (verify-all), pass. A local 200k-iteration decoder fuzz found
nothing.
Used by: no includers; not in images.
Gaps: none found.

### fat32 — read-only FAT32 driver
Status: WORKING (has a test, not in verify-all)
What it does: This mounts a FAT32 volume from a block device, walks cluster chains, lists a
directory and reads a file into a caller buffer. It is a real on-disk parser.
Main entry points: fat32_mount, fat32_read_cluster, fat32_next_cluster, fat32_read_dir,
fat32_read_file(fs, file, buf, cap), fat32_find_file.
Tests: new src/fat32/test_fat32.c (14 checks, pass, ASan clean), added to group2_verify_lines.mk.
Used by: vfs, storage_fabric, boot. In all images.
Gaps: before the fixes, a hostile boot sector could overflow the stack, a looping FAT spun forever,
cluster 0/1 underflowed, and read_file had no capacity (all fixed). It is read-only.

### finance — triple ledger, payment rails, bridge records, derivatives, assurance, treaty assets
Status: PARTIAL
What it does: The triple ledger keeps three books per account (money, provenance, externality) and
reports a conventional balance sheet. rails.c models card and real-time payment processing with a
settlement ring. crypto_bridge.c keeps records of cross-chain transfers. financial.c prices
instruments. derivatives, assurance and treaty_tokenization gate contracts on ledger backing. All
of it is an in-memory model. It does not connect to card networks, chains or PAPSS (headers now
say so).
Main entry points: triple_ledger_post/transfer/issue_voucher/redeem_voucher/export_conventional,
rail_process_tx/settle_batch/bridge_to_conventional, bridge_create/execute/confirm_source/
confirm_dest, financial_price_option, deriv_verify_backing, assurance_admit, treaty_asset_verify.
Tests: new test_finance_core.c (33 checks; host and Q32 builds pass; ASan/UBSan clean), added to
verify lines. Linked by test_ministry, test_vino_stores, test_finance_markets and others in
verify-all (pass).
Used by: finance_markets, financial_fabric, ministry, vino_stores, battering_ram, cardnet,
pirate_apps, sdk_bridge, apps, arch mains. In all images.
Gaps: before the fixes, it minted money (rails negative sale/refund, non-holder voucher redeem),
double counted and posted non-atomically (fixed). Still open: option pricing is not Black-Scholes;
negative balances are allowed; assurance steps 1-3 are comments; treaty_asset_verify always fails;
SR_FROM_FLOAT is used in kernel code only on compile-time constants (folded; no FP at run time).

### finance_markets — instrument tracker and position valuation
Status: WORKING (tested in verify-all)
What it does: This tracks instruments, posts quotes and fixings, and values positions. It settles
trades as triple-ledger transfers with a node fee leg. There is no order book or matching engine.
Main entry points: fm_book_init, fm_open_account, fm_track_instrument, fm_post_quote,
fm_value_position, fm_fixing_set/get, fm_settle.
Tests: src/finance_markets/test_finance_markets.c (verify-all, host and q32), pass.
Used by: financial_fabric, pirate_apps. In all images.
Gaps: the fee leg is now skipped when the fee is zero or the payer is the fee account, because
transfer now refuses those cases.

### financial_fabric — umbrella over finance modules (accounts, vouchers, contracts, assets)
Status: WORKING (tested in verify-all)
What it does: This is one registry that fronts the ledger, vouchers, derivative and assurance
contracts, and tokenised assets. It stores amounts as exact fractions.
Main entry points: ff_init, ff_register_builtins, ff_create_account, ff_transfer_capital,
ff_mint_voucher/ff_burn_voucher, ff_create_derivative, ff_create_assurance, ff_tokenize_treaty.
Tests: src/financial_fabric/test_financial_fabric.c (verify-all), pass.
Used by: about 10 *_fabric / app modules; Makefile.arm64.
Gaps: amounts were converted with `>> 32`, den 1, which dropped fractions (1.5 became 1). This
also failed to compile under TEST_HOST (fixed with ff_sr_to_rat). The header oversold Markets and
the Bridge (corrected).

### font — bitmap font registry, TrueType parser/rasteriser, script itemiser
Status: WORKING (tested in verify-all)
What it does: This holds an embedded bitmap font plus a TrueType parser and rasteriser that reads
real .ttf tables. It also has a UTF-8 decoder and a script itemiser (Latin, Arabic, Hebrew, ...)
for right-to-left runs.
Main entry points: font_registry_init, font_register, font_resolve, ttf_parse, ttf_glyph_index,
ttf_render, font_utf8_next, font_itemize.
Tests: test_font, test_truetype, test_script (verify-all), pass. A local 300k-iteration fuzz of
the TrueType parser and the UTF-8 decoder found nothing.
Used by: i18n, bootfeat, media_fabric. In all images.
Gaps: none found.

### fractal — Z-order (Morton) spatial addressing
Status: WORKING (tested in verify-all)
What it does: This interleaves x/y coordinates into Morton codes, which give a quadtree address, and
answers parent, containment and distance queries. It is plain integer bit manipulation, and the
header says honestly that it is a model.
Main entry points: zo_encode2, zo_decode2, zo_make, zo_parent, zo_contains, zo_hops,
zo_manhattan, zo_place.
Tests: src/fractal/test_zorder.c (verify-all) plus zxvfs tests, pass.
Used by: zxvfs, tolvovina. In all images.
Gaps: none found.

### freight — Reed-Solomon / GF(256) erasure-coded freight containers
Status: WORKING (tested in verify-all)
What it does: This splits data into shards with Reed-Solomon over GF(256) so it survives lost
pieces. It has a checked header format (serialize/parse).
Main entry points: freight_gf_mul/inv/div, freight_header_init/check/serialize/parse,
freight_encode, freight_decode.
Tests: test_freight, test_stream, test_web4_agent (verify-all), pass. A local fuzz of header parse
and decode found nothing.
Used by: stream, web4, and zxvd (`kernel/arch/hosted/zxvd_rpc_modules.c`, tested by `test_zxvd_rpc_modules.c` and `test_zxvd.py`): freight.pack / freight.unpack. Not in images.
Gaps: none found.

### fs — tri-space file system (S+, S-, S0 replicas, transactions, snapshots)
Status: PARTIAL
What it does: This is an in-memory file tree with nodes, data writes and deletes. It supports
transactions that snapshot before applying and roll back on failure. It also claims replicas in
"negative" and "neutral" space for recovery.
Main entry points: fs_registry_init, fs_create_node, fs_write_data, fs_delete_node,
fs_begin_transaction, fs_tx_add_write, fs_tx_commit, fs_tx_abort, fs_replicate_to_s_minus, fs_recover_from_s_minus.
Tests: tests/host/test_fs.c (verify-all test_fs), pass.
Used by: tests/host; Makefile.arm64.
Gaps: snapshots were never released, so after 32 transactions a failed one rolled back to
snapshot 0 and lost data (fixed). replicate/recover only set flags and copy no data. The
transaction table is never recycled (128 total).

### fusion — capability-contract fusion of Tri-Space programs
Status: WORKING (tested in verify-all)
What it does: This joins two programs' "provides" and "needs" bitmasks into one contract, checks
that they can compose, and content-addresses the result. It does not link code, and the header
says so.
Main entry points: fuse_program_init, fuse_set_provides/needs/neutral, fuse_can_compose,
fuse_compose, fuse_cid.
Tests: src/fusion/test_fusion.c (verify-all), pass.
Used by: no includers. In all images.
Gaps: none found.

### fwadapt — smart firmware adapter: which driver may touch a device
Status: WORKING (tested in verify-all)
What it does: collects one claim per source (ACPI, device tree, PCI and USB IDs, probes) per device, joins them with lpres, tags the result with an lpres_hex hexagram and decides: quarantine on a fault, a safe generic driver (flagged for a person) on a conflict, a specific driver on agreed IDs, a class-generic driver only for standard interfaces, defer on stale evidence, otherwise nothing. It never guesses registers. fwa_to_hwp writes the decisions into a hwemu ZHP-1 profile.
Main entry points: fwa_init, fwa_builtin_table, fwa_claim, fwa_decide, fwa_count, fwa_to_hwp.
Tests: test_fwadapt.c (274 checks, ASan/UBSan), target test-fwadapt, in verify-all.
Used by: no caller outside its tests. The x86_64 kernel picks drivers with hwdrv's own hw_drivers[] table and does not call it.
Gaps: no kernel image feeds it real ACPI, device tree or PCI evidence, and hwdrv binds without it. It cannot make unknown hardware work without a spec or driver. See kernel/src/fwadapt/README.md, [HW_DRIVERS.md](HW_DRIVERS.md) and [TURNKEY_OS_GAPS.md](TURNKEY_OS_GAPS.md).

### fxepoch — FX by uniform-price batch epochs on pay_ledger
Status: WORKING (tested in verify-all)
What it does: a batch-auction FX market with one book per currency pair (no vehicle currency). Signed orders collect during an epoch, each book clears at one uniform price inside a band (cooling epochs instead of liquidation), fills settle as one atomic posting per order with the assurance fee, and a multilateral netting report is computed. It is off by default and refuses any leverage.
Main entry points: fx_config_default, fx_init, fx_add_book, fx_open, fx_order, fx_clear, fx_settle, fx_cancel, fx_net, fx_record, fx_check.
Tests: test_fxepoch.c (426 checks, including 600 random epochs, ASan/UBSan), target test-fxepoch, in verify-all.
Used by: no caller outside its tests.
Gaps: no runtime caller, no zxvd method and no libzxv binding. FX dealing and running an auction are regulated; it needs legal review before any use. A thin book clears badly or not at all. The netting report is arithmetic, not a settlement-finality claim. The verify hook is the caller's. No doc of its own; the design line is in [ARCHITECTURE.md](ARCHITECTURE.md).

### games — deterministic lockstep game core with a Chiglet companion
Status: WORKING (tested in verify-all)
What it does: This is a grid-forager game whose whole state is a pure function of seed, inputs and
tick, so peers stay in lockstep by exchanging inputs only. The companion decides moves through the
Chiglet runtime, or holds still if no model is bound.
Main entry points: game_init, game_step, game_state_hash, game_bind_companion,
game_companion_decide, game_def_publish.
Tests: src/games/test_games.c (verify-all), pass.
Used by: media_fabric. In all images.
Gaps: no rendering or networking (the header states this).

### gdt — x86 Global Descriptor Table setup
Status: PARTIAL
What it does: This writes the six classic x86 segment descriptors (null, kernel/user code and
data, TSS placeholder), loads them with lgdt, and reloads the segment registers.
Main entry points: gdt_init, gdt_set_entry.
Tests: none.
Used by: boot (x86 boot path), tests.
Gaps: it is 32-bit-only code. `(uint32_t)&gdt_entries` truncates on a 64-bit host, and the TSS
entry is a placeholder.

### gematria — letter-value and grammar-to-"Fock-space" mapping
Status: PARTIAL (compiled into the kernel images, no caller and no test; was listed as UNUSED/DEAD, but gematria.c is in Makefile.arm64/x86_64 and the other image Makefiles, so it was not moved to kernel/experimental/)
What it does: This turns letters into numbers (Hebrew standard, ordinal, digital root, primes,
custom table) and splits sentences into words with punctuation roles. It then maps those words into
hdcm "Fock-space" structures. The word parser is bounded; the physics mapping is analogy.
Main entry points: gematria_init, gematria_char_value, gematria_word_value,
gematria_analyze_sentence, gematria_to_fock, gematria_sentence_to_fock.
Tests: none (only tests/test_new_modules.c, which nothing builds).
Used by: no module; listed in the image Makefiles.
Gaps: "Greek isopsophy" is the Latin ordinal and "Torah" is standard x 10. Custom tables ignore
non-letters.

### governance_fabric — umbrella over legal/concord/ministry/crown/ZAB governance
Status: UNUSED/DEAD
Location: kernel/experimental/governance_fabric/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: This is meant to front proposals, votes, policies, contracts and jurisdictions over
the governance modules. It does not compile: zab_state_t, concord_state_t and crown_state_t are
unknown types, and no Makefile builds it.
Main entry points: gf_init, gf_create_proposal, gf_cast_vote, gf_tally_votes,
gf_execute_proposal, gf_create_policy, gf_create_contract.
Tests: none.
Used by: headers only (app_constellation, app_fabric, also unbuilt).
Gaps: does not compile. The header claims "ZK-ABFT consensus" and "cryptographically verifiable
delegation", and none of it is built.

### grants — Olive Oil Barrel startup grants for participants in good standing
Status: WORKING (tested in verify-all)
What it does: judges good standing from deterministic metrics only (uptime, clean peer-audit record, volume, stake), with a signed appeal path. Grants are paid only from a funded pool, never minted, as virtual barrel units on pay_ledger. A lot counts as backed only while an outside underwriter's signed attestation is valid; unbacked units cannot be redeemed or locked.
Main entry points: grant_init, grant_set_thresholds, grant_pool_credit, grant_epoch_begin, grant_eval, grant_appeal, grant_lot_open, grant_epoch_distribute, grant_attest, grant_dispute, grant_backed, grant_snapshot, grant_rate.
Tests: test_grants.c (304 checks, ASan/UBSan), target test-grants, in verify-all. test_zstats.c also links it. test_zxvd_estate.sh (zxvd.yml CI) covers the zxvd methods.
Used by: zxvd grants.* methods (zxvd_estate.c), behind the regulated "grants" feature switch, which starts off, on a sandbox ledger; zstats reads its snapshot.
Gaps: barrels are software units until an underwriter attests, and an attestation proves who signed, not that any oil exists. Standing metrics are only as true as the reports fed in. zxvd uses Ed25519 keys for signatures. The redemption price comes from the caller. The method list is in [ZXVD.md](ZXVD.md).

### gridlock — exact gridlock resolution for a queue of payments
Status: WORKING (tested in verify-all)
What it does: given participants' balances and credit caps and a queue of payments that cannot settle one at a time, it picks the subset that can settle together with the most value (or by priority class first). FIFO mode is solved exactly in polynomial time; free mode uses exact branch and bound seeded by the classical heuristic, with a brute-force reference for tests. Integer only.
Main entry points: gl_init, gl_set_balance, gl_add_payment, gl_solve, gl_heuristic, gl_brute_force, gl_verify, gl_check.
Tests: test_gridlock.c (126,375 checks, ASan/UBSan), bench_gridlock.c builds, and a freestanding build, target test-gridlock, in verify-all.
Used by: no caller outside its tests and the benchmark.
Gaps: not wired into cbank, settle or any ledger path, and no ISO 20022 import. Not combined with qnet netting. One currency per instance, no partial payments, no incremental re-solve. Free mode is exponential in the worst case and can end at "best + bound". Benchmarks use synthetic queues. See [GRIDLOCK_RESOLUTION.md](GRIDLOCK_RESOLUTION.md).

### hardware — DLP projector model and RTL/HDL device abstraction
Status: PARTIAL
What it does: dlp_projector models a projector's actuator, wobulation, photometrics and laser
using integers and is tested. rtl_device describes a hardware block (registers, DMA, IRQs, gate
count) and prints a SystemVerilog, VHDL or Chisel skeleton.
Main entry points: dlp_projector_init/tick/set_photometrics/compute_screen_lux,
rtl_device_create, rtl_device_add_register/dma/irq, rtl_device_generate_hdl.
Tests: src/hardware/test_dlp_projector.c (verify-all), pass. rtl_device has no test.
Used by: net, boot, arch mains. In all images.
Gaps: HDL text generation overflowed the output buffer after truncation (fixed). DMA depth 0 caused
a divide by zero (fixed). DMA push and pop discard data. The header's "FPGA bitstream" and "GDSII"
outputs are not implemented.

### harmonic — harmonic wire tags, dial/geom routers, endian mux, trunk bank, adapters
Status: WORKING (tested in verify-all)
What it does: This packs and routes tagged words on "harmonic trunks": wire tags, E.164 dial
routing, geometric routing, an endian multiplexer, a metronome, and residual carriers/taps. Its
large tables are generated by Python scripts in the directory, and they match the generators.
Main entry points: zt_wire_make_tag, zt_wire_pack_tagged, zt_dial_parse_number, zt_dial_format,
zt_geom_router_init, zt_mux_channel_mask, zt_trunk_bank_init/process_pcm, zt_adapter_audio_init.
Tests: 10 tests in verify-all (harmonic_wire 61, adapter_house 60, dial_router 47, trunk_bank 25,
geom_router 19, endian_mux 14, residual_carrier 14, signal_data 11, residual_tap 8,
metronome 6), all pass.
Used by: zxvd (`kernel/arch/hosted/zxvd_rpc_modules.c`, tested by `test_zxvd_rpc_modules.c` and `test_zxvd.py`): harmonic.encode / harmonic.decode (zt_harmonic_wire: the frame,
its tag and the stateless resolver). The routers, mux, trunk bank and adapters still have no caller
outside the directory; not in images.
Gaps: none found.

### hdcm — "hyperdimensional" cross-language construct matrix
Status: PARTIAL
What it does: This registers programming languages and their constructs (loop, function, ...) and
builds a compatibility matrix between them. "Translation" replaces each construct with the nearest
construct in the target language. It is a lookup table, not a translator.
Main entry points: hdcm_init, hdcm_language_register, hdcm_language_add_construct,
hdcm_matrix_build, hdcm_matrix_compatibility, hdcm_translate.
Tests: none (only tests/test_new_modules.c, unbuilt).
Used by: gematria. In all images.
Gaps: an "[unmapped]" placeholder was reported as a successful translation (fixed: returns -1). The
"universal language interoperability" claim is far beyond what it does.

### holodeck — content-addressed viewing swarm inside a den
Status: WORKING (tested in verify-all)
What it does: The people watching the same content share its chunks. Total capacity grows with
viewers, and each chunk is checked against its digest, so a peer cannot substitute content.
viewing.c applies a den's access rules (who may watch, seed or speak).
Main entry points: swarm_init, swarm_join/leave, swarm_capacity_kbps, swarm_select_source,
swarm_verify_chunk, viewing_open, viewing_join, viewing_may_watch/seed/speak.
Tests: test_swarm and test_viewing (verify-all), pass.
Used by: no includers. In all images.
Gaps: no network transport (scheduling and verification only).

### holographic — .36n9/.9n63/.36m9/.zedei/.zedec holographic file formats
Status: WORKING (tested in verify-all)
What it does: This defines five file types: "positive" and "negative" data, a dataset manifest, a
renderer and a container. It has headers, magic numbers, checksums, serialize/deserialize, and an
"interference" combine (add, XOR, ...) of positive and negative bytes. The formats and
serialisation are real; the holography is metaphor.
Main entry points: holo_init, holo_make_header, holo_positive_create/set_payload,
holo_dataset_add_pair, holo_container_add_entry, holo_interference, holo_render,
holo_serialize_* / holo_deserialize_*.
Tests: linked by test_tripartite_fs (verify-all), pass. No direct test of its own.
Used by: tripartite_fs, emu, tolvovina, boot and arch mains. In all images.
Gaps: deserialised counts were trusted, length sums could wrap on 32-bit, and name copies were
unbounded (fixed). Deserialise still takes payload_out without a capacity.

### hwdrv — standard class drivers: NVMe, AHCI, xHCI with USB keyboard and storage, e1000, igb, r8169, HDA
Status: WORKING (tested in verify-all)
What it does: polled, integer-only bare-metal drivers written against standards, not chips. At boot the x86_64 kernel scans PCI, binds a driver per device with a T/F/B/N verdict, and runs a short self-test per device (disks read the GPT and a file from the EFI partition, NICs get a DHCP offer, keyboards echo keys, audio checks the DMA position). hw_storage.c reads GPT and FAT32 on those disks.
Main entry points: hwdrv_core.c and hwdrv_probe.c (PCI config, BAR sizing, hw_drivers[] match and bind), the per-driver bind functions, hw_storage_selftest; platform hooks hw_plat_t (kernel/arch/x86_64/hwplat_x86.c).
Tests: kernel/src/hwdrv/test/ against a register-level fake device: test_ahci (785), test_e1000 (2741), test_hda (745), test_hw_storage (58), test_hwdrv_core (233), test_igb (1219), test_nvme (334), test_r8169 (299), test_xhci (651), ASan/UBSan, target test-hwdrv, in verify-all. build_system/hw_drivers_check.sh boots the UEFI disc in QEMU in the CI uefi-disc job.
Used by: the x86_64 kernel image (build_system/Makefile.x86_64, kernel_main_x86_64.c).
Gaps: no interrupts (MSI/MSI-X) and no IOMMU. No USB CDC, USB audio, UVC, Bluetooth HCI or Intel igc. No arm64 platform glue. The Linux driver VM for Wi-Fi and GPUs is not started. r8169 and some e1000/igb device ids have no QEMU model. Nothing has run on a physical machine. See [HW_DRIVERS.md](HW_DRIVERS.md).

### hwemu — hardware profiles, the emulation matrix, standard engines and signed hardware updates
Status: WORKING (tested in verify-all)
What it does: hwp.c is the ZHP-1 hardware profile format. hwm.c is a matrix of functions served by HARD, FABRIC, MICRO, PEER, SOFT or REMOTE providers with proof, failover and placement. hwe.c gives standard engines (SHA-256, Keccak, HMAC, GCD, LOGIC64, tensor dot) with SOFT implementations and golden vectors. hwr.c handles reconfiguration images signed with ML-DSA-87 plus SLH-DSA (two slots, probation, rollback, quarantine), and hwa.c is their hash-chained audit log. Only the SOFT engines run real work; hardware is reached through caller callbacks.
Main entry points: hwp_init, hwp_node_add, hwp_encode / decode, hwp_present, hwp_match; hwm_init, hwm_func_add, hwm_provider_add, hwm_select, hwm_call, hwm_attest; hwe_install; hwr_parse, hwr_region_init, hwr_trust_key, hwr_link, hwr_stage, hwr_trial; hwr_pq_verify, hwr_dual_sign; hwa_init, hwa_append, hwa_verify.
Tests: test_hwm.c (110), test_hwe.c (43), test_hwp.c (55) and test_hwr.c (122), ASan/UBSan, plus a freestanding no-FP build, target test-hwemu, in verify-all. bench_hwm.c (bench-hwemu) is not in verify-all.
Used by: libzxv ABI 1.4 (sdk/libzxv/src/zxv_hw.c) and the Python, TypeScript and .NET SDK bindings over it; fwadapt (fwa_to_hwp writes hwp profiles). No kernel image links it.
Gaps: no real FPGA, microcode or soft-core device backend exists in the tree; regions are driven only through test callbacks. The matrix never beats native code (about 2% overhead on a compute-heavy call in the documented benchmark). See [../sdk/docs/HW_EMULATION.md](../sdk/docs/HW_EMULATION.md) and [../sdk/docs/HW_PROFILE.md](../sdk/docs/HW_PROFILE.md).

### hwkey — account keys sealed by the TPM or Secure Enclave
Status: WORKING (tested in verify-all)
What it does: the ML-DSA-87 account seed is sealed to a P-256 key that stays inside the TPM or Secure Enclave (ECIES-shaped: ECDH with a fresh point, SHAKE256 key derivation, keyed MAC over every stored byte). It is opened only for one signature and wiped. hwkey_soft.c is a file-based fallback that reports itself as software so a caller can refuse it. Host providers are in kernel/arch/hosted/hwkey (Linux TPM, Windows, macOS Secure Enclave).
Main entry points: hwk_keygen, hwk_sign, hwk_check, hwk_require_hardware, hwk_encode / decode, hwk_key_id, hwk_soft_provider.
Tests: test_hwkey.c (110 checks) and test_outbox.c, targets test-hwkey and test-outbox; test-hwkey-host runs the host providers with the software fallback (20 checks here) and on a swtpm TPM 2.0 simulator when one is installed (skipped here). All are in test-retail, which runs in verify-all.
Used by: no runtime caller. The outbox module names hwk_sign as its normal signer, but only its test links hwkey. The host providers are not in app_sources.sh or build_zxvd.sh, so neither zxvd nor the desktop app uses them.
Gaps: the ML-DSA secret is in host memory for each signature. TPM keys use an empty authorisation value and enclave keys have no user-presence check. Not linked into any bare-metal image. The macOS Secure Enclave and Windows providers are not run here (not checked). No doc of its own (rules are in hwkey.h).

### hypercube — N-dimensional workspace scene projected to 3D/2D
Status: WORKING (tested in verify-all)
What it does: Apps occupy cells of a logical hypercube. Each cell has coordinates, a parent, colours
and subscriptions. The scene projects them to the current display mode and animates transitions
by event sequence rather than wall-clock time.
Main entry points: hc_init, hc_set_display_mode, hc_create_cell, hc_cell_set_coord,
hc_set_focus, hc_project, hc_start_transition, hc_update_transitions, hc_render.
Tests: tests/host/test_hypercube.c (verify-all), pass.
Used by: arch/arm64, tests/host. In all images.
Gaps: hc_render produces no pixels by itself.

### i18n — locales, message catalogue, formatting, Enochian script
Status: WORKING (tested in verify-all)
What it does: This holds generated locale tables and a message catalogue with parent-locale
fallback. It formats numbers and plurals, and it includes Enochian transliteration. It is about 37k
lines, mostly generated tables (gen_i18n_tables.py).
Main entry points: i18n_msg, i18n_locale_find/resolve/default/parent, i18n_format_msg,
i18n_fmt_int/decimal/currency/date, i18n_grapheme_next, i18n_enochian_render.
Tests: test_i18n, test_i18n_cov, test_i18n_enochian (verify-all), pass; test_i18n checks the
desktop window's UI_ ids. check_ui_i18n.py (test_hosted.sh, verify-all) checks the window's table.
Used by: the desktop window (kernel/arch/hosted/zxv_ui.html). At build time gen_ui.py compiles
kernel/arch/hosted/gen_ui_strings.c with the i18n sources and embeds the string table, CLDR
number formats and a locale map (checked against i18n_locale_resolve) in the page; the page
chooses the locale from navigator.language. Not in kernel images.
Gaps: the window's UI_ ids are English only, so other languages fall back to English (the window
says so); the SwiftUI studio (macos/studio) does not use the table.

### icon  —  vector icons for every module
Status: WORKING (tested in verify-all)
What it does: Each icon is a short list of drawing operations (rectangle, disc, line, triangle) on a 64x64 grid, coloured by theme tokens, so icons recolour with the theme. Headline systems have hand-made icons; every other module name gets a deterministic generated icon. Renders into a caller's pixel buffer.
Main entry points: icon_for, icon_render, icon_is_builtin, icon_procedural.
Tests: icon/test_icon.c ("theme + icons" block) — passes.
Used by: bootfeat. Kernel: yes.
Gaps: icon.c / test_icon.c have no SPDX line.

### ident  —  optional KYC credentials, post-quantum passkeys, account recovery
Status: WORKING (tested in verify-all)
What it does: Real cryptography: Argon2id (RFC 9106), a BCH fuzzy extractor, Shamir secret sharing, WebAuthn-shaped passkeys signed with ML-DSA-65, a dual-signature root key, guardian-based recovery, and optional KYC attestations with selective disclosure. KYC verifiers and devices plug in as callbacks; no document is ever stored.
Main entry points: id_root_from_seed, id_passkey_create/register/assert, id_kyc_request, id_kyc_check_payment, id_recovery_enrol, id_argon2id (ident.h).
Tests: ident/test_ident.c — passes.
Used by: nothing includes ident.h outside ident (consumers call it through tests only). Hosted only: not in any kernel image.
Gaps: not linked into a kernel; signature scratch is a single static (not re-entrant, documented).

### identity  —  national-ID registry model
Status: PARTIAL
What it does: A table of 181 countries (ISO codes, regional label) and a "virtual device" record per identity. identity_verify only checks the M5 coverage number and the record's own active/sanctioned flags; nothing checks a document. The biometric call used to grant level 3 for anyone; it now refuses (no matcher exists).
Main entry points: identity_registry_init, identity_register, identity_verify, identity_attest_biometric, identity_check_compliance, identity_resolve_cross_system.
Tests: none of its own (tests/test_new_modules.c includes it but no Makefile builds that file).
Used by: arch/* kernel mains, boot, finance, identity_fabric. Kernel: yes.
Gaps: header claimed "all 193 UN member states" (fixed to the truth); no real verification of any kind; uses __int128 via surplus.h.

### identity_fabric  —  "compound" identity/governance umbrella
Status: UNUSED/DEAD
Location: kernel/experimental/identity_fabric/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: A header and source meant to bundle identity, ledger, consensus, reputation and governance modules. It does not compile (unknown types zab_state_t, concord_state_t, crown_state_t, ...) and no build or test compiles it.
Main entry points: identity_fabric.h types only.
Tests: none. Used by: other *_fabric headers include identity_fabric.h (also unbuilt).
Gaps: does not compile; claims ("ZK-ABFT consensus", "cryptographically verifiable delegation") are not implemented.

### idt  —  x86 32-bit interrupt descriptor table
Status: PARTIAL
What it does: Sets up the 256-entry x86 IDT, ISR stubs (isr_stubs.s) and a handler table. Only meaningful for the legacy 32-bit x86 build in kernel/Makefile; Makefile.arm64 deliberately excludes it.
Main entry points: idt_init, idt_set_gate, idt_register_handler, irq_handler.
Tests: none. Used by: boot, keyboard, mouse, sched, timer, ../init.
Gaps: no SPDX; pointer-to-int casts warn on 64-bit hosts.

### immigration  —  signed admission control for daemons
Status: WORKING (tested in verify-all)
What it does: Before a daemon may run it must present an Ed25519 signature (over content hash + public key + 168-bit id) that verifies against the kernel's embedded immigration public key. Visas come in three tiers; unsigned daemons are refused and logged; violators can be deported. Network-facing daemons also go through Porter House.
Main entry points: immig_init, immig_apply_visa, immig_verify_daemon, immig_record_violation, immig_deport, immig_check_expired.
Tests: immigration/test_immigration.c — passes.
Used by: arch/arm64. Kernel: yes.
Gaps: comments said HMAC-SHA256 (stale; fixed). Header says "All Rights Reserved" next to Apache-2.0.

### interspace  —  "Lex Rhodia" commons: general average, salvage, federation
Status: WORKING (tested in verify-all)
What it does: Numeric sea-law rules: split a loss in proportion to stake with exact conservation, salvage awards, content-hash "flag" immunity, time-limited safe passage, flag-state co-jurisdiction (host port policy AND vessel integrity must both pass), microstate charters and council votes, and a "Minister" that computes but can only bind consenting nodes. The writ check is a content binding, not a signature (stated in the header).
Main entry points: interstitial_open/claim, lex_rhodia_general_average, cojurisdiction_eval, microstate_constitute, minister_adjudicate_average.
Tests: interspace/test_interspace.c — passes.
Used by: desktop, finance, governance_fabric, identity_fabric, sovereign_node. Kernel: yes.
Gaps: none found beyond surplus.h __int128.

### invproof  —  checkable "exact inverse" witnesses
Status: WORKING (tested in verify-all)
What it does: Turns a claimed exact undo into a check: it stores the XOR delta between before and after plus SHA-256 of both, and verification re-applies the undo and compares hashes. Proves the inverse for one recorded transition, not in general (the header says so).
Main entry points: zxi_build, zxi_verify, zxi_is_witness, zxi_result_name.
Tests: invproof/test_invproof.c (15 checks) and used by zxvfs/zmedia tests — pass.
Used by: arch/arm64, codec, security_fabric, zxvfs. Kernel: yes.
Gaps: none found.

### ipfs  —  minimal content-addressed get (SHA-256 "CID")
Status: WORKING (tested in verify-all)
What it does: Treats a 32-byte SHA-256 as the content id, fetches through a caller-supplied transport and rejects any bytes that do not hash to the id asked for. With no transport bound every fetch fails. Not real IPFS CIDs (ipfs_node does those).
Main entry points: ipfs_cid_from_bytes, ipfs_cid_parse, ipfs_get_verify, ipfs_add_pin, ipfs_install_as_bridge_resolver.
Tests: ipfs/test_ipfs.c — passes.
Used by: art_studio, bootfeat, broker, finance, games, interspace, pirate_apps, sovereign_node, storage_fabric. Kernel: yes.
Gaps: none found.

### ipfs_node  —  real IPFS CIDs, UnixFS, CAR, blockstore, UBH-168 wire
Status: WORKING (tested in verify-all)
What it does: Builds byte-exact Kubo-compatible CIDs and UnixFS DAGs, reads dag-pb and plain/HAMT directories, reads/writes CAR files, keeps a blockstore with sealed private blocks, an OS file index, gateway URL building and a UBH-168 framed wire format. Every block is re-hashed before use. There is no libp2p/Bitswap/DHT (stated).
Main entry points: ipfsn_cid_*, ipfsn_ufs_*, ipfsn_dagpb_parse, ipfsn_dir_list/lookup/resolve, ipfsn_car_*, ipfsn_bs_*, ipfsn_wire_*.
Tests: test_ipfs_node.c (536 checks) and test_ipfsn_dir.c (Kubo 0.32.1 vectors) — pass. group3 also fuzzed all parsers 5M iterations under ASan/UBSan: clean.
Used by: ident, stream, update, vblock, web4. Hosted only (not in a kernel image).
Gaps: ipfsn_dir.c and test files lack SPDX.

### iphase  —  K4 route contracts and phase routing
Status: WORKING (tested in verify-all)
What it does: iphase.c keeps an endpoint registry, prioritised/weighted route contracts and failover chains with deterministic selection. iphase_core.c is a separate topology table whose "route" returns a double-complex "phase vector" (a model; net_connect stores it but routes nothing with it).
Main entry points: iphase_registry_init, iphase_register_endpoint, iphase_add_route, iphase_add_failover, iphase_get_route, iphase_route (core).
Tests: iphase/test_iphase.c (make test loop) and K4 block — pass.
Used by: all arch mains, boot, net. Kernel: yes.
Gaps: no SPDX on iphase_core.* (the double complex route vector is now zxv_cq16_t, fixed).

### iso20022  —  PACS.008 / CAMT.053 message builders
Status: WORKING (tested in verify-all)
What it does: Writes ISO 20022 pacs.008 and camt.053 XML from supplied data through a bounded writer, maps the internal 555/777/888 rail codes and refuses to put them in a currency field. It only serialises; it does not send anything (stated).
Main entry points: iso20022_pacs008_build, iso20022_camt053_build, iso20022_format_amount, iso20022_ccy_alpha.
Tests: iso20022/test_iso20022.c — passes.
Used by: no #include outside the dir (kernel-linked, unreferenced). Kernel: yes.
Gaps: no parser here (pay/pay_iso_parse.c is the strict inbound parser).

### juris — jurisdiction plug-in ABI and its fail-closed host
Status: WORKING (tested in verify-all)
What it does: rules that differ by country (capital controls, tax rates, the reporting framework, whether the open market may run) live in signed plug-ins. The host parses zxvd's jurisdiction settings, registers only plug-ins with a valid signed manifest for the chosen country and no version rollback, asks every loaded plug-in, and combines the 64-state answers so that a missing answer waits and a conflict escalates to a person. juris_vhub.c adapts it to the V-Bill hub's rules.
Main entry points: juris_settings_parse, juris_host_init, juris_host_apply_settings, juris_host_register, juris_check_transfer, juris_commit_transfer, juris_check_market, juris_host_tax_rate, juris_host_standard; juris_vhub_rule, juris_limit_plugin.
Tests: test_juris.c (192 checks, ASan/UBSan), target test-juris, in verify-all. test_intent.c also uses juris_vhub.h.
Used by: zxvd jurisdiction.get / jurisdiction.set (zxvd_rpc.c), and intent (guard G6), which has no runtime caller. Also compiled into the desktop app's source list with no caller there.
Gaps: zxvd starts the host with no verify hook, so no plug-in can register there (plugins is always 0) and nothing on the payment path asks it. Loading plug-in code is left to the embedding process. capctl, taxhook and kycwrap are not registered with it. The header still says zxvd reports in_effect=false, but zxvd now reports true. Not legal or tax advice. See [ADAPTERS.md](ADAPTERS.md).

### keyboard  —  PS/2 keyboard driver (x86)
Status: PARTIAL
What it does: Port-I/O PS/2 driver with a ring buffer and modifier tracking, for the 32-bit x86 build only (excluded from arm64).
Main entry points: keyboard_init, keyboard_handler, keyboard_getchar.
Tests: none. Used by: boot, ../gui, ../init.
Gaps: no SPDX; no test.

### kycwrap — minimal-disclosure KYC wrapper with an empty zero-knowledge slot
Status: WORKING (tested in verify-all)
What it does: one verifier API for proving a fact about a person ("over 18", "KYC'd by issuer X", "resident of country C") without showing who they are. The verifier issues a fresh challenge and a pluggable backend checks the answer. The minimal-disclosure backend is real: issuer-signed single-use tokens (ML-DSA-87 through hooks, SHA-256), weekly expiry, a spent-token set, signed revocation lists and one-time challenges. It is honestly not zero-knowledge. The zero-knowledge backend is an empty slot that returns KYC_E_NOT_AVAILABLE. The verifier is off by default.
Main entry points: kyc_verifier_init/enable, kyc_request_new, kyc_verify, kyc_md_backend, kyc_md_issue/issue_batch, kyc_md_present, kyc_md_wallet_present, kyc_md_install_revlist, kyc_zk_backend.
Tests: test_kycwrap.c (83 checks, ASan/UBSan) in mk/compliance.mk test-compliance, which verify-all runs through test-retail (kernel/Makefile:414). Freestanding builds for aarch64 and i386 are checked there too.
Used by: no caller outside its tests. Not in any kernel image, zxvd or libzxv. It is a separate thing from ident's id_kyc adapter ([IDENTITY_AND_RECOVERY.md](IDENTITY_AND_RECOVERY.md)).
Gaps: no zero-knowledge library is included, so kyc_zk_backend refuses every call. A colluding issuer can link every presentation to the person. The issuer's identity check and its legal duties sit outside this code. Host-tested only. See [retail/COMPLIANCE_PLUGINS.md](retail/COMPLIANCE_PLUGINS.md).

### lattice  —  "LATTICE-P2P" file-sharing tables
Status: PARTIAL
What it does: In-memory tables of peers, swarms, chunks and a small key/value "DHT" with phase-aware seeding labels. It opens no connection: lattice_peer_connect only sets a flag.
Main entry points: lattice_init, lattice_peer_add/connect, lattice_swarm_create/add_chunk, lattice_dht_put/get.
Tests: only tests/test_new_modules.c, which no Makefile builds.
Used by: nothing. Kernel: yes (linked, unreferenced).
Gaps: no networking, no hashing of chunks inside the module; untested.

### launch — app launch screen sequencing and launch sounds
Status: WORKING (tested in verify-all)
What it does: zxl_launch decides what each app's launch screen shows and when, driven by the app's real loading: a begin cue, one tile and one pitched tick per finished load part, a lit icon, and an end cue only when the app says it is ready. It holds a theme (key, timbre, ambient motion) for every app in the 407-app catalogue (zxl_apps.inc) and sound and motion preferences. zxl_synth synthesises every cue live in integers (48 kHz stereo, 16-bit), with no recorded samples. Drawing is not here; it lives in zui.
Main entry points: zxl_theme_for/pick, zxl_group_theme, zxl_launch_init, zxl_launch_part, zxl_launch_rest, zxl_launch_next, zxl_launch_finish, zxl_icon_file, zxl_icon_ink, zxl_player_init/play/render.
Tests: test_zxl_launch.c (3749 checks) and test_zxl_synth.c (199 checks), both in verify-all. zui's test_zui_launch_theme and test_zui_launch_shell, also in verify-all, drive it too.
Used by: zui_launch_theme and zui_launch_shell, which have no caller outside their own tests. Not in any kernel image, the hosted app or zxvd. tools/launch/launch_catalogue.py generates zxl_apps.inc and zxl_icons.inc.
Gaps: no desktop session creates a zui_launch_shell yet, so no app opens on these screens today. This is item 1 of "Next" in [APP_LAUNCH_SCREENS.md](APP_LAUNCH_SCREENS.md).

### legacy  —  SIP/SDP, SS7/M3UA/SCCP/TCAP/MAP, ISUP map, DTMF, EBCDIC, COBOL, Fortran
Status: WORKING (tested in verify-all)
What it does: Bounded parsers/builders for telecom and mainframe formats: SIP messages and SDP audio offers, M3UA/SCCP/TCAP/MAP (USSD and SMS carriage), ISUP<->SIP cause mapping, DTMF tones/RTP events, EBCDIC code pages, COBOL copybooks and packed/zoned decimals, Fortran records and IBM float. Data model only; no carrier connection (stated).
Main entry points: sip_parse, sdp_parse, m3ua_parse_data, sccp_parse_udt, tcap_parse, map_parse_*, cobol_parse_copybook, ebcdic_to_utf8.
Tests: test_sip/ss7/isup_map/dtmf/ebcdic/cobol/fortran (50/46/33/61/795/59/36) — pass. bench/legacy checks COMP-3, zoned, COMP and IBM HFP conversions bit-exact against 44 published or hand-worked golden rows (verify-all: legacy_bench). group3 fuzzed all parsers 3M iterations under ASan/UBSan after fixing three memory bugs (findings). fuzz/parsers/fuzz_cobol_copybook (libFuzzer, see fuzz/FUZZ_REPORT.md) found that unsigned PIC 9 fields read a negative sign as -n; now refused.
Used by: arch/arm, boot, net, orbital_compat (includes). Hosted only. Packaged in packages/libzxv-legacy (cobol, fortran, ebcdic only) (static library built from these sources; smoke program in verify-all).
Gaps: none open after fixes.

### legal_engine  —  legal document generator
Status: WORKING (tested in verify-all)
What it does: Fills treaty, user-agreement and policy templates in six languages into text buffers via the bounded fs_snprintf, and an auto-response notice that makes no unearned claims. The "golden ratio seal" is an 8-bit label, not an integrity check (now said in the code, and integer-only).
Main entry points: legal_engine_init, legal_gen_user_agreement, legal_gen_diplomatic, legal_render_document, legal_auto_respond, legal_seal_agreement, legal_assess_risk.
Tests: test_fs_snprintf.c, test_legal_response.c — pass.
Used by: governance_fabric, identity_fabric. Kernel: yes.
Gaps: test files lack SPDX.

### license  —  licence text and metadata
Status: WORKING (tested in verify-all)
What it does: Holds the Apache-2.0 text and the project's licence-instrument names and precedence, and prints them.
Main entry points: license_print, license_get_name, license_get_full_text, license_precedence_rank.
Tests: license/test_license.c — passes.
Used by: kernel (Makefile COMMON_SRCS, arm64). Gaps: test_license.c lacks SPDX.

### lightningrod  —  COBOL/Fortran/C data-representation adapters
Status: WORKING (tested in verify-all)
What it does: Converts values and layouts between COBOL packed/zoned decimals, Fortran column-major arrays and C conventions exactly, using exact rationals, and says when a conversion cannot be lossless.
Main entry points: lr_packed_to_rat, lr_rat_to_packed, lr_zoned_to_rat, lr_scaled_to_rat, lr_ebcdic_to_ascii, lr_fixed_to_cstr.
Tests: test_lightningrod.c, test_lightningrod_regress.c — pass. bench/legacy golden rows cover lr_packed_to_rat and lr_zoned_to_rat.
Used by: orbital_compat. Kernel: yes. Packaged in packages/libzxv-legacy (static library built from these sources; smoke program in verify-all).
Gaps: lightningrod.c and tests lack SPDX.

### lingua — FORTRAN, BASIC and Forth front-ends on one checked integer IR
Status: WORKING (tested in verify-all)
What it does: compiles subsets of FORTRAN 77, Minimal BASIC (ECMA-55) and Forth-2012 core to one small stack-machine IR and runs it on a checked integer runtime with a fuel budget. It traps on overflow, division by zero, inexact division, bounds and depth instead of continuing. Anything outside the supported subset is refused with a line and column. Each language has an "AI twin" canonical spelling that compiles to the same IR. zl_pack and zl_unpack store verified programs as digest-checked "zlir" text. zxlang.c is a command-line front end.
Main entry points: zl_compile, zl_verify, zl_run, zl_twin, zl_pack, zl_unpack, zl_prog_digest.
Tests: test-lingua in verify-all: test_zl.c (3972 checks, ASan/UBSan), fuzz_zl (20000 iterations, plus 5000 under ASan/UBSan), a freestanding build check and check_catalogue.sh (104 languages). test-lingua-ref, also in verify-all, compares golden and generated programs with gfortran and gforth; it skips when they are missing unless ZXV_REQUIRE_LINGUA_REF=1, which CI sets. mut_check.sh (test-lingua-mut) is not in verify-all.
Used by: kernel/src/apps/zxapp_lingua.c (apps with `logic zlir` lines), which is built only by apps/test_zxapp.sh. That script runs from kernel/arch/hosted/zxvd_checks.sh (scripts/verify-local.sh), not from verify-all. Not in any kernel image, zxvd or libzxv.
Gaps: only three languages run; the other 101 in catalogue.tsv are planned tiers. Integer only. The module README says it is in libzxv, but sdk/libzxv/Makefile does not build it. See [LANGUAGE_CATALOGUE.md](LANGUAGE_CATALOGUE.md) and [kernel/src/lingua/README.md](../kernel/src/lingua/README.md).

### loader  —  ELF64 loader, signed packages (ZSP v1/v2), A/B updates
Status: WORKING (tested in verify-all)
What it does: Validates and loads static AArch64 ELF64 images (W^X enforced via callback), verifies ZSP packages (SHA-256 + Ed25519 against a root key; v2 also binds version, arch, key id) and runs A/B updates with probation and auto-rollback on ZXVFS. Versions are strictly monotonic: ab_stage_update refuses (AB_ERR_ROLLBACK) any package whose signed version is <= the active one or below the floor, and every v1 package (no signed version); ab_confirm re-checks the probation slot's header version before promoting.
Main entry points: elf_load, zsp_verify, zsp_verify2, ab_init, ab_stage_update, ab_confirm, ab_boot_tick, ab_rollback.
Tests: test_elf.c, test_zsp.c, test_zsp2.c, test_abupdate.c (+ make fuzz fuzz_elf) — pass.
Used by: arch/arm64, zxpkg. Kernel: yes.
Gaps: kernel_main_arm64.c still loads apps with zsp_verify (no floor, accepts v1). zxpkg now uses zsp_verify2.

### logistics  —  syndicates, escrow and secondary contracts
Status: WORKING (tested in verify-all)
What it does: Splits one supply contract among shareholders with exact integer conservation, holds escrow until delivery, caps a negotiator's margin at 11%, runs every term through The One Policy, and rates follow-through.
Main entry points: log_init, log_contract_open, log_syndicate_form, log_subcontract_split, log_escrow_deposit, log_escrow_release, log_credibility.
Tests: logistics/test_logistics.c (host and Q32 builds) — pass.
Used by: desktop. Kernel: yes.
Gaps: none found.

### lpres  —  K3 paraconsistent presence attestations
Status: WORKING (tested in verify-all)
What it does: Four-valued logic (true, false, both, neither) for attesting presence of sources, data and evidence without contradictions "exploding", with conjoin/disjoin/negate. lpres_core.c is an older BIOS-stage table of rational magnitudes. `lpres_hex.c` adds the 64-state hexagram tag on units of work: 6 bits = base (the four states) x polarity (negentropic, entropic, neutral, forbidden) x lifecycle phase (potential, emerging, manifest, dissolving, a Gray cycle). One 64-entry table gives the action (proceed, wait, refuse, escalate, roll back); forbidden overrides everything and is absorbing; the phase moves one step at a time; forbidden work goes into a hash-chained quarantine log for audit. A tag reports what a real check found (bounds checks, sanitizers, the ledger's A = L + E); it does not replace the check. Known defect in the base tables: lpres_conjoin/lpres_disjoin give Both AND Neither = Neither and Both OR Neither = Both, which breaks De Morgan for that pair (Belnap's tables give False and True); the owner decides whether to switch.
Main entry points: lpres_negate, lpres_conjoin, lpres_disjoin, lpres_is_contradictory, lpres_registry_init, lpres_state_name; lpx_make, lpx_action, lpx_advance, lpx_polarity, lpx_and/or/not, lpx_qlog_append/verify.
Tests: test_lpres.c (make test) and the K3 block — pass; test_lpres_hex.c (`make test-lpres-hex`, 76 checks, mutation 87.2%, in the CI mutation gate). Used by ~40 modules.
Gaps: lpres_core.* and test lack SPDX.

### macgyver  —  tri-space build registry (MCG0)
Status: WORKING (tested in verify-all)
What it does: Shared node registry, obligation tracking and "triads" binding S+/S-/S0 artifacts. It compiles nothing; the triad digest is an XOR of caller digests (header previously said "signed ... cryptographically"; corrected).
Main entry points: mcg_registry_init, mcg_add_node, mcg_add_child, mcg_set_node_digest, mcg_add_obligation, mcg_satisfy_obligation.
Tests: tests/host/test_macgyver.c — passes. Used by: nothing includes it. Kernel: yes.
Gaps: no real compilation or signing.

### mage  —  role and authorisation framework ("hats")
Status: WORKING (tested in verify-all)
What it does: Classifies security tools by role and refuses offensive actions unless a scoped, signed (SHA-256-bound), expiring engagement authorises them.
Main entry points: mage_init, mage_set_verifier, mage_asset_add, mage_engage_self, mage_engage_signed, mage_authorize, mage_hat_grants.
Tests: mage/test_mage.c — passes. Used by: bootfeat. Kernel: yes.
Gaps: test lacks SPDX.

### market  —  P2P commerce state machine
Status: WORKING (tested in verify-all, under ASan+UBSan)
What it does: Signed storefronts and listings, carts, orders through one table-driven state machine, per-order escrow with exact conservation, the 0.08889% assurance fee from pay (charged once per sale), refunds exact to the minor unit, operator tax rates, disputes, reviews, ISF-ranked discovery and agent shopping with user confirmation tokens. Money only moves through a settle hook. `mk_pay.c` is that hook on the ledger of record: each settlement becomes one atomic `pay_ledger_post` (one line per account with a nonzero net, escrow balancing captures, releases, refunds and rulings; invoices must net to zero), so a ledger refusal (funds, freeze, balance) leaves the market unchanged. Identifiers are SHA-256(seed || tag || n).
Main entry points: mk_init, mk_ccy_register, mk_listing_publish, mk_cart_add, mk_checkout, mk_dispute_open/rule, mk_discover, mk_mandate_grant, mk_invoice_issue; mk_pay_init, mk_pay_bind, mk_pay_settle.
Tests: market/test_market.c (`make test-market`) — passes; every flow also posts to a real pay_ledger through mk_pay and the two books are compared after each settlement (866 postings, 0 mismatches); mk_pay mutation 91.5%, in the CI gate. Used by: nothing outside tests yet (the app does not open a market). Hosted only.
Gaps: not linked into a kernel.

### mbcomp  —  component (circuit-element) vocabulary
Status: PARTIAL
What it does: Names module archetypes in electronics terms (resistor, capacitor, ...) and lets the system query modbind for a module's declared kind.
Main entry points: the MC_* archetype enum, mc_name, mc_holds_state, mc_needs_alternation (query modbind_count/modbind_get).
Tests: none. Used by: nothing includes it. Kernel: yes.
Gaps: untested.

### media_fabric  —  "compound" media umbrella
Status: UNUSED/DEAD
Location: kernel/experimental/media_fabric/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: Intended to bundle audio/video/codec/art modules. Does not compile (codec.h not found) and nothing builds it.
Tests: none. Used by: app_constellation, app_fabric headers (also unbuilt).
Gaps: does not compile; "real-time M5 coverage guarantees" claim unbacked.

### megarom  —  observed game dynamics -> Sutra schemas
Status: WORKING (tested in verify-all)
What it does: Turns measured execution traces of a title (tempo, render pressure, agency under different input) into abstract Sutra schemas and comparisons. Explicitly does not mine story content.
Main entry points: mrs_observe, mrs_emit_sutra, mrs_relate, mrs_matrix, mrs_widest_axis.
Tests: megarom/test_mrschema.c (21 checks) — passes. Used by: nothing. Kernel: yes.
Gaps: none found.

### mesh_net  —  mesh network and trade-route bookkeeping
Status: WORKING (tested in verify-all)
What it does: Users create mesh networks, peers join through Porter House, trade routes record hops, data volume and revenue, routes expire, networks federate. It moves no bytes and does no crypto (header claim "post-quantum signed and encrypted" corrected).
Main entry points: mn_init, mn_create_network, mn_join_network, mn_create_route, mn_send_data, mn_federate.
Tests: mesh_net/test_mesh_net.c — passes (new overflow check added).
Used by: arch/arm64, emu, *_fabric, sdk_bridge. Kernel: yes.
Gaps: route capacity is stored but not enforced.

### mesh_token  —  settlement queue gated by Porter House
Status: WORKING (tested in verify-all)
What it does: Queues token settlements to peers that Porter House admits, records a Count House valuation, moves them to in-transit, confirms on ack, times them out. Finished slots are now recycled (was permanent exhaustion after 64).
Main entry points: mesh_token_init, mesh_token_settle, mesh_token_advance, mesh_token_ack, mesh_token_check_timeouts.
Tests: mesh_token/test_mesh_token.c — passes (new reuse test).
Used by: arch/arm64, financial_fabric. Kernel: yes.
Gaps: no transport is attached; "settlement" is a state record.

### mfg — manufacturing checks: G-code toolpaths, guard-banded inspection, resource budgets
Status: WORKING (tested in verify-all)
What it does: three read-only, advisory checkers. mfg_gcode parses a G-code subset into exact integer micrometres and checks a program against a machine profile (travel envelope, feeds, spindle, fixtures) before a person releases it; unknown words reject the job. mfg_qc makes four-valued, guard-banded pass/reject/hold decisions from gauge readings and their uncertainty. mfg_budget holds per-site hard limits for energy, water, material, waste and emissions fed by meter readings, plus a small machine-time admission queue. Nothing here talks to a machine.
Main entry points: mfg_gc_check, mfg_gc_profile_check, mfg_gc_parse_um, mfg_qc_gauge/feature/part, mfg_site_init/meter/check/reserve/release, mfg_queue_add/run.
Tests: test_mfg.c (763 checks, ASan/UBSan) and a freestanding build check, in test-mfg under test-research, which verify-all runs.
Used by: no caller outside its tests. Not in any kernel image, zxvd or libzxv.
Gaps: the tool is a point (no cutter compensation, work offsets, canned cycles, R-form arcs or other planes); robot programs are not parsed; RELEASE_OK means "these checks found nothing", not "safe". mfg_qc trusts the caller's uncertainty figures. Meter readings are trusted as given. Only F.2, F.4, F.10 and a minimal F.6 queue are built; the rest of track F is later work. See [MANUFACTURING.md](MANUFACTURING.md).

### ministry  —  treasury pillar (tribute, gratuity)
Status: WORKING (tested in verify-all)
What it does: Measures Ministry-form capital, runs treasury postings through finance/triple_ledger, computes tribute/gratuity, refuses to price Crown forms, and never issues credentials.
Main entry points: ministry_init, ministry_measure, ministry_tribute, ministry_gratuity, ministry_settle.
Tests: ministry/test_ministry.c (host and Q32) — passes. Used by: bootfeat, governance_fabric, identity_fabric. Kernel: yes.
Gaps: none found.

### mint — minting Vino or V-Bills against signed capacity claims
Status: WORKING (tested in verify-all)
What it does: issues Vino or V-Bills on pay_ledger only against a capacity claim (electronic warehouse receipt, compute or kWh) signed by a registered attestor. It applies an attestor haircut, then a jurisdiction haircut, and a per-epoch jurisdiction cap. It records every remainder it does not mint and refuses replays. Claims move from LIVE to QUARANTINED (a hand-off to qnote; holders keep their units) or RETIRED (one redemption posting). Every jurisdiction starts disabled. No interest or fees.
Main entry points: mint_init, mint_juris_set, mint_attestor_set, mint_unit_set, mint_issue, mint_epoch_advance, mint_quarantine, mint_retire, mint_backing, mint_check, mint_zeckendorf.
Tests: test_mint.c (226 checks, ASan/UBSan) in test-mint under test-econ, which verify-all runs. test_qnote.c also uses it.
Used by: no caller outside its tests. Not in any kernel image, zxvd or libzxv.
Gaps: a signature proves who attested, not that the capacity exists or was not attested twice elsewhere. Unit values come from the operator's own price source. The header notes that minted V-Bills are likely securities and that real use needs legal review. No dedicated doc; [ARCHITECTURE.md](ARCHITECTURE.md) lists it as built and host-tested.

### mixmat  —  exact rational mixing matrices
Status: WORKING (has a test, not in verify-all)
What it does: Row-stochastic matrices over exact rationals that generalise the "Sonic Chemistry" compound rule into a weighted average; composition, fixed points and checks.
Main entry points: mixmat_identity, mixmat_is_stochastic, mixmat_mul, mixmat_pow, mixmat_compound, mixmat_stationary, mixmat_selfcheck.
Tests: mixmat_selfcheck() runs at arm64 boot; no host test target.
Used by: arch/arm64. Kernel: yes.
Gaps: no host test in verify-all.

### mlkem  —  ML-KEM-768 (FIPS 203) and SHA-3/SHAKE
Status: WORKING (tested in verify-all)
What it does: Keccak/SHA-3/SHAKE, NTT, sampling, encoding, K-PKE and ML-KEM-768 keygen/encaps/decaps with implicit rejection (now a branch-free select). genomic_codon adds a domain-separation label only, not security.
Main entry points: mlkem768_keygen, mlkem768_encaps, mlkem768_decaps, kpe_*, sha3_256, sha3_512, shake128_*, shake256.
Tests: keccak_validate, mlkem768_validate, test_mlkem_kat (30 NIST ACVP comparisons), test_mlkem_reject (tampered ciphertext gives K_bar = SHAKE256(z||c) for ML-KEM-768 and -1024, plus a valgrind ctgrind run with the secret key marked undefined: no secret-dependent branch in decapsulation on this host's gcc/clang at -O0..-O3), pq tests — pass. `make -C kernel kat-cross` also runs test_mlkem_kat on aarch64, riscv64, armhf, i686 and s390x (big-endian) under qemu-user; `make -C kernel ct-valgrind` runs decaps and encaps under valgrind with the secrets marked undefined and finds no secret-dependent branch or index (not a constant-time proof). SHA-3/SHAKE was wrong on big-endian CPUs (byte view of the lanes); fixed in keccak.c. The encode/ntt/sample/kpe/genomic validators also run in verify-all.
Used by: ~17 modules (pay, ident, plnp, cardnet, web4, ...). Kernel: yes. Packaged in packages/libzxv-pqc (static library built from these sources; smoke program in verify-all).
Gaps: validator files lack SPDX; headers say "All Rights Reserved" beside Apache-2.0.

### mm  —  x86 paging/heap model
Status: PARTIAL
What it does: Frame bitmap, a first-fit heap with split/coalesce, kmalloc/kcalloc/krealloc. mm_get_page cannot create tables ("would allocate"). Size overflows in kmalloc/kcalloc fixed.
Main entry points: mm_init, kmalloc, kfree, kcalloc, krealloc, mm_get_page.
Tests: src/mm/test_mm_alloc.c (verify-all test_mm_alloc): kmalloc/kcalloc/krealloc and fs_malloc/fs_calloc refuse wrapping sizes. Used by: arch/arm, boot. Kernel: yes (arm64 list).
Gaps: mm_get_page(make=true) returns NULL. Fixed: blocks are 8-byte aligned (a 4-byte round put split headers on a misaligned address on 64-bit), mm_free_frame debited frame 0's quota instead of the freed frame's, double kfree corrupted the accounting, and bit-31 bitmap shifts were signed.

### modbind  —  module declaration graph and boot gate
Status: WORKING (has a test, not in verify-all)
What it does: Every module declares what it provides and requires in a linker section; the gate checks the graph at boot, holds modules whose requirements are missing and reports names. Exists because many modules compiled but were never linked.
Main entry points: ZXV_DECLARE, modbind_resolve, modbind_verify_graph, zxv_decl_register_all, zxv_decl_bringup_ready.
Tests: exercised by build_system/verify_layers.sh / verify_banners.sh on the ELF, not by a host test.
Used by: arch/arm64, mbcomp; zxv_decl.h used by most modules. Kernel: yes.
Gaps: no host unit test.

### mouse  —  PS/2 mouse driver (x86)
Status: PARTIAL
What it does: PS/2 mouse packet decoding into a ring buffer, x86 build only.
Main entry points: mouse_init, mouse_handler, mouse_has_packet, mouse_get_packet.
Tests: none. Used by: boot, ../gui, ../init. Gaps: no SPDX, no test.

### net  —  TCP/IP stack, M5 router, radio/SDR models, smart adapters
Status: WORKING (tested in verify-all) for the stack; PARTIAL for radio/smart_adapter
What it does: A real small stack: Ethernet, ARP, IPv4 with checksums, ICMP echo, UDP, a TCP endpoint (handshake, retransmit, sequence wrap, hostile options), DHCP client and DNS resolver with pointer-loop protection; the RTL8139 driver is x86 only. m5route maps phone numbers/IPs/callsigns to "M5 addresses". radio.c, jdr_piratenet.c and smart_adapter*.c are models: cellular/satellite/radio/"quantum"/"neutrino" adapters have no driver (their send now reports failure instead of success).
Main entry points: net_init, net_rx_packet, net_socket/connect/send/recv, tcp_input, dhcp_input, dns_parse_response, m5_router_send.
Tests: test_dhcp, test_dns, test_tcp, test_netstack — pass. test_smart_adapter.c does not compile (missing smart_adapter_jdr.h) and is not built.
Used by: apps, arch mains, boot, bringup, legacy, *_fabric, pterm. Kernel: yes (rtl8139 x86 only).
Gaps: ARP accepts unsolicited replies; no SPDX on most files.

### network_fabric  —  "compound" network umbrella
Status: UNUSED/DEAD
Location: kernel/experimental/network_fabric/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: Intended to bundle mesh, SDR, Bluetooth, Wi-Fi, LoRa. Does not compile (jdr_adapter_t, radio_modem_t, ... unknown) and is not built.
Tests: none. Used by: app_constellation, app_fabric headers (unbuilt).
Gaps: does not compile; "post-quantum security" claim unbacked.

### nlb  —  "nonlinear build" session model
Status: PARTIAL
What it does: A session with sources, branches and CREATE/ENTANGLE/MEASURE phases, registers, DMA and IRQ fields. "Compilation" is simulated: a branch passes if its sources have non-zero size (the code says so).
Main entry points: nlb_session_init, nlb_source_add, nlb_branch_create, nlb_build_run, nlb_check_convergence.
Tests: only tests/test_new_modules.c, which nothing builds. Used by: nothing includes it. Kernel: yes.
Gaps: no compiler behind it; header reads as if it builds software.

### onepolicy  —  The One Policy (symbiotic-term predicate)
Status: WORKING (tested in verify-all)
What it does: One decidable check that every exchange or contract term passes: a term with asymmetric harm, unilateral extraction or non-reciprocal burden is void. Used by economy modules for no-usury and fairness checks.
Main entry points: op_evaluate, op_symbiotic_ok, op_grace_of_somalia_ok, op_verdict_name, op_term_t.
Tests: onepolicy/test_onepolicy.c (host and Q32) — passes. Used by: alloc, battering_ram, crown, finance, interspace, logistics, ministry, vino_stores and more. Kernel: yes.
Gaps: no SPDX on .c/test.

### orbital_compat  —  one IR for COBOL/Fortran/C/Sutra/... values
Status: WORKING (tested in verify-all)
What it does: Typed records with exact rational values that language adapters lower into and lift out of, so translation is N adapters, not N*N. Adapters for asm, DTMF, Python, Rust, Sutra, WASM and Zig are value-shape adapters, not compilers.
Main entry points: oc_lift, oc_lower, oc_register_lang, oc_register_builtins, oc_ir_get_rat, oc_field_eq.
Tests: orbital_compat/test_orbital_compat.c — passes.
Used by: abstraction_layer, apps/telecom, civilizational_stack, orbital_fabric, sdk_bridge. Kernel: yes.
Gaps: adapter files warn (unused, enum mismatch lpres_state_t vs anonymous enum in orbital_compat_sutra.c).

### orbital_elevator  —  event schema translation
Status: WORKING (tested in verify-all)
What it does: Schema registry, compatibility graph and adapters that rewrite versioned event envelopes (recomputing the CRC); no instruction translation. "Signed adapters" are a flag, not a signature check.
Main entry points: oe_init, oe_register_schema, oe_schema_add_version, oe_register_adapter, oe_register_compat, oe_translate.
Tests: tests/host/test_orbital_constellation.c — passes. Used by: abstraction_layer, arch/arm64, net, orbital_fabric. Kernel: yes.
Gaps: adapter signing not implemented.

### orbital_fabric  —  "compound" orbital layer
Status: PARTIAL
What it does: Compiles and links in the arm64 kernel; wires orbital_elevator, orbital_compat, constellation, yantra fabric, smart adapters, event space and mesh_net together at init.
Main entry points: of_init, of_register_builtins, of_create_domain, of_add_schema_mapping, of_translate_event.
Tests: none. Used by: abstraction_layer/boot_modules.c and other fabric headers. Kernel: yes.
Gaps: no test; unused parameters (the float at orbital_fabric.c:248 is fixed).

### oseq  —  K1 ordinal sequencer
Status: WORKING (tested in verify-all)
What it does: Monotonic ordinals, causal parent DAG, replay and cycle rejection, happens-before queries (oseq.c); oseq_core.c is the older device-ordinal table.
Main entry points: oseq_registry_init, oseq_register_node, oseq_register_event, oseq_commit_event, oseq_reject_event, oseq_happens_before; core: oseq_init, oseq_register_device.
Tests: test_oseq.c (make test) and tests/host/test_oseq.c — pass. Used by: arch mains, boot, dharma, vfs. Kernel: yes.
Gaps: no SPDX on core files.

### outbox — offline spending grants with once-only settlement
Status: WORKING (tested in verify-all)
What it does: lets a device pay while offline. While online, an offline grant moves a limit from the holder's pay_ledger account into escrow. Offline, the device signs hash-chained entries against the grant (write-ahead persisted before an entry leaves the device), and a payee can verify them with no network. On reconnect, either side submits entries and the ledger pays each one once out of escrow. Conflicting entries flag the device. An optional rollback guard binds the device state to a monotonic hardware counter so a restored backup cannot sign.
Main entry points: obx_ledger_init/grant/submit/close/expire/check, obx_device_init/spend/close, obx_device_guard, obx_device_spend_guarded, obx_verify_offline.
Tests: test_outbox.c (302 checks) in test-outbox, a prerequisite of test-retail, which verify-all runs. kernel/arch/hosted/hwkey/test_hwkey_host.c (test-hwkey-host, also in test-retail) runs a grant with a host key: 20 checks on the software fallback; the TPM 2.0 simulator part skips when swtpm is missing.
Used by: no caller outside tests. Not in any kernel image, zxvd or libzxv.
Gaps: without a hardware counter (the Secure Enclave and the Windows Platform Crypto Provider expose none to apps), a device restored from an old backup can sign two entries with the same number; only one is paid and the other payee is left with the conflict evidence. The guard cannot stop someone who controls the chip. No doc in docs/ describes it; the design is in outbox.h.

### p2p_caracho  —  superseded P2P transport
Status: UNUSED/DEAD
Location: kernel/experimental/p2p_caracho/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: Old transport, replaced by bootlegger; its own header says do not build. Makefile.arm64 excludes it by design.
Tests: none. Used by: nothing.
Gaps: dead file kept on disk.

### panopticon  —  connection watcher and "VPN mesh" model
Status: PARTIAL
What it does: panopticon.c rates watchers/connections and logs; panopticon_vpn.c selects a 5-hop path from a 5x5 table of placeholder nodes and prepends per-hop labels. It opens no tunnel and encrypts nothing (now stated; encryption_active is 0; buffer overflow fixed).
Main entry points: panopticon_init, panopticon_register_watcher, panopticon_rate_friendliness, panopticon_classify, vpn_mesh_init, vpn_connect, vpn_route_packet.
Tests: none. Used by: nothing includes it. Kernel: yes.
Gaps: no test; not a VPN.

### paradox — paradox engine, paraconsistent ledger and Belnap agents
Status: WORKING (tested in verify-all)
What it does: paradox.c sorts work whose four-valued state is Both or Neither into four kinds (constructive, destructive, neutral, special) and runs a fixed operator for each, with every step appended to a hash-chained log. paradox_graph.c resolves graphs of dependent paradoxes in a fixed order. pdx_ledger.c is a paraconsistent claim ledger with witness votes, merges and horizon finality. pdx_agent.c is a Belnap-Dunn reasoning agent with support sets and no explosion. It never decides which side of a contradiction is true.
Main entry points: pdx_classify, pdx_classify_answer, pdx_constructive, pdx_destructive, pdx_neutral, pdx_special, pdx_review, pdx_graph_load/pass, pdx_log_append/verify, plg_claim/vote/merge/advance, pag_tell/run/decide.
Tests: test_paradox.c (256 checks) in test-paradox and test_pdx_ledger.c (284 checks, covers pdx_ledger and pdx_agent) in test-pdx-ledger, both ASan/UBSan with freestanding checks, both in verify-all. fuzz/parsers/fuzz_paradox.c is a fuzz harness.
Used by: pce (its log), recover (kernel/src/recover), libzxv's paradox group (sdk/libzxv/src/zxv_paradox.c builds paradox.c and paradox_graph.c), and tensor's test_zt_evh. pdx_ledger and pdx_agent have no caller outside test_pdx_ledger. Not in any kernel image or zxvd.
Gaps: it detects only what its checks look at; signing logs and RepairProofs is the caller's job; a full log halts. Ledger finality depends on the caller's horizon protocol and identical configuration on every node. zxv_api.h still marks the paradox ABI group "reserved until it reaches main". See [PARADOX_ENGINE.md](PARADOX_ENGINE.md) and [PARACONSISTENT_LEDGER.md](PARACONSISTENT_LEDGER.md).

### pay  —  payment ledger, assurance fee, ISO 20022, equity, treasury, farm
Status: WORKING (tested in verify-all, under ASan+UBSan)
What it does: Exact-integer three-rail ledger with all-or-nothing postings, hash-chained provenance, idempotency and replay protection, no interest; the 0.08889% assurance fee (pay_assure.h: floor(g·8889/10^7), per-account sub-unit carry, exact 50/25/15/10 split into reserve floor / V-Bill dividend pool / infrastructure-node bounties / regenerative capital; it replaced the φ% tithe 2026-10-10) checked against a Python reference; strict ISO 20022 builders and parsers checked against the official XSDs; share equity, group treasuries, role/IBAN checks and gateways as callbacks only (no SWIFT connection, stated).
Main entry points: pay_ledger_init, pay_ledger_post, pay_ledger_transfer, pay_ledger_issue, pay_ledger_check, pay_ledger_verify_chain, pay_ledger_pay_with_fee, pay_assure_fee, pay_assure_charge, pay_assure_split, pay_iso_pacs008, pay_iso_parse_pacs008/camt053.
Tests: pay/test_pay.c — passes. group3 fuzzed both ISO parsers 200k iterations: clean. fuzz/parsers/fuzz_pay_xml and the ledger property harness fuzz/econ/fuzz_econ_pay run in verify-all (corpus replay) and CI (fuzz-smoke, -fsanitize=integer).
Used by: capmkt, devmesh, evolve, market, provider, quest. Hosted only.
Gaps: not linked in a kernel.

### pce — paraconsistent decision engines: probability, tactics, strategy, finance
Status: WORKING (tested in verify-all)
What it does: four exact, integer and rational engines that keep disagreement instead of picking a winner. Probability uses four-valued belief masses (conflict and ignorance kept apart). Tactics is a negamax alpha-beta search scored by several evaluators. Strategy ranks options under interval scenario likelihoods with several decision rules, plus negotiation ranges. Finance reconciles statements and gives NPV and risk findings. Every verdict is a four-valued tag and a hexagram tag. Work touching money, devices or life-safety is advisory only, and runs must name an allowed purpose (weapons targeting is not one). Nothing here moves money or places orders.
Main entry points: pce_mass_* and pce_bayes (pce_prob.h), pce_tac_search, pce_strat_decide/bounds/zopa, pce_fin_analyse/npv/risk, pce_verdict, pce_log, pce_entries/pce_entry_call (pce_entry.h).
Tests: test-pce in verify-all: test_pce.c, test_pce_tac.c, test_pce_strat.c, test_pce_fin.c and test_pce_golden.c in one binary (9558 checks, ASan/UBSan), apps/test_zxapp_pce.c (40 checks) and freestanding checks.
Used by: kernel/src/apps/zxapp_pce.c (the app model's `c` runtime and three blocks), which is built only by that test. Not in any kernel image, zxvd or libzxv. kernel/src/emu/pce.c is the unrelated PC Engine console emulator.
Gaps: the "witnessed" second implementation of the probability entries in KRON is the next step, not built. Purpose limits are a policy refusal the API makes explicit, not something the logic detects. See [PARACONSISTENT_ENGINES.md](PARACONSISTENT_ENGINES.md).

### pci  —  PCI enumeration (x86)
Status: PARTIAL
What it does: Config-space port I/O enumeration of up to 32 devices, x86 only.
Main entry points: pci_init, pci_config_read, pci_config_write, pci_find_device, pci_find_class.
Tests: none. Used by: boot, ../init. Gaps: no SPDX, no test.

### peer_audit  —  self-audit and peer audit for swarm transitions
Status: WORKING (tested in verify-all, also under ASan+UBSan; freestanding aarch64/i386 build checked)
What it does: Two layers of checks. (1) Before a node signs a ledger, budget or task transition, pa_precommit_check tests conservation, overflow, token budget, ISF headroom and sizes. On failure nothing is signed and the node enters a contradiction state (value-equal to LPRES_STATE_BOTH). (2) Peers replay each signed record (ML-DSA-65) through the real pay_ledger and swarm_budget code and compare bit for bit. A committee is drawn deterministically from the record hash, with a 2f+1 of 3f+1 signed-vote quorum. Canary challenges use the normal request format. Signed evidence can be re-checked by any third node and isolates the misbehaving peer through a host revoke callback; no funds are ever moved. Evidence that does not verify counts against the accuser. Inference uses commit-and-sample spot checks. See docs/PEER_AUDIT.md.
Main entry points: pa_precommit_check, pa_emit_record, pa_replay_check, pa_committee, pa_vote, pa_quorum, pa_evidence_make, pa_evidence_check, pa_canary_issue, pa_canary_check, pa_infer_commit, pa_spot_sampled, pa_spot_check, pa_runtime_replay.
Tests: peer_audit/test_peer_audit.c — 101 checks pass.
Signatures are ML-DSA-87, the same scheme as a Vinea node identity.
Used by: settle (every spine transfer is encoded in pa_runtime's replayable form) and the desktop app, which self-audits and signs every ledger posting through settle's audit hook and reports the state, record count and events in /api/status.
Gaps: records are not yet carried to peers (vinea); the revoke callback is not yet connected to porter_house or vinea; there is no agreed per-epoch peer list; full GGUF model replay is not tested; state lives in memory only.

### phase_coord  —  K6 phase coordinator
Status: WORKING (tested in verify-all)
What it does: Admits, vetoes or defers steps by coverage and health policy, issues admission tokens and coordinates the 13-phase pipeline; phase_coordinator.c is the tick driver used by every kernel.
Main entry points: pc_registry_init, pc_register_gate, pc_check_all_gates, pc_admit, pc_revoke_token, phase_coordinator_init, phase_coordinator_tick.
Tests: test_phase_coordinator.c (make test) and K6 block — pass. Used by: arch mains, boot, dharma, finance, holographic, sched. Kernel: yes.
Gaps: none for floating point: m5_types.h is integer-only (fixed).

### pic  —  8259 PIC (x86)
Status: PARTIAL
What it does: Remaps and masks the legacy PIC, sends EOI; x86 only.
Main entry points: pic_init, pic_send_eoi, pic_mask, pic_unmask.
Tests: none. Used by: boot, idt, keyboard, mouse, timer, ../init. Gaps: no SPDX, no test.

### pirate_apps  —  thin UIs over other modules
Status: WORKING (tested in verify-all)
What it does: Five app surfaces (Hold, Counter, Cellar, Spyglass, Club) that call the real module (ipfs, broker, vino, social, ...) and return its result unchanged, or say the capability is unavailable.
Main entry points: app_hold_open, app_counter_open, app_cellar_open, app_spyglass_open, app_club_open, app_status, app_render.
Tests: pirate_apps/test_pirate_apps.c — passes. Used by: nothing includes it. Kernel: yes.
Gaps: none found.

### pirate_fleet  —  crew channels, charter DAO, compute credit
Status: WORKING (tested in verify-all)
What it does: Local IRC-style channels, a crew charter with Ed25519 founders and a crew-state machine, a 2/3 quorum that certifies nodes before they accrue Vino for compute. Explicitly no legal personality.
Main entry points: pf_init, jdr_crew_charter, jdr_crew_transition, jdr_node_register, jdr_node_certify, jdr_accrue_vino, jdr_channel_post.
Tests: pirate_fleet/test_pirate_fleet.c (host and Q32) — passes. Used by: desktop. Kernel: yes.
Gaps: none found.

### plnp  —  Phase-Lattice framing protocol
Status: WORKING (tested in verify-all)
What it does: A frame format with content-id fields, CRC32 and optional HMAC-SHA256 tag; per-connection send/receive that checks CRC, ids, sequence and (if keyed) the tag. No transport, routing or encryption (stated). plnp_frame_parser.sv is a SystemVerilog sketch nobody builds.
Main entry points: plnp_conn_create, plnp_conn_set_key, plnp_conn_send, plnp_conn_receive, plnp_frame_(de)serialize.
Tests: plnp/test_plnp.c — passes. Used by: nothing outside tests includes plnp.h. Kernel: yes.
Gaps: plnp_frame_parser.sv has no SPDX and is unbuilt.

### pmm — four-state physical frame allocator
Status: WORKING (tested in verify-all)
What it does: tracks each 4 KB frame in two bits as unusable, free, fenced or owned. A double free or a double claim fences the frame for the life of the allocator and reports BOTH instead of corrupting a free list. Allocation with nothing free returns NEITHER (wait), not a failure. No allocation, no libc; not thread-safe.
Main entry points: pmm_init, pmm_reserve, pmm_alloc, pmm_free, pmm_claim, pmm_state, pmm_owns_range.
Tests: test_pmm.c (90 checks, ASan/UBSan) in test-pmm, in verify-all.
Used by: kernel/arch/arm64/el0_userspace.c, for the 2 MB static user page pool; pmm.c is in build_system/Makefile.arm64. Not used by the x86_64 kernel.
Gaps: it manages only the arm64 user pool, not all physical RAM, and nothing on x86_64. The kernel_core audit table ([baremetal-gaps/kernel_core.md](baremetal-gaps/kernel_core.md)) still lists the PMM as missing; it predates this module. [BAREMETAL_OS_GAPS.md](BAREMETAL_OS_GAPS.md) records the wave 1 work.

### policy — zxv_policy engine and contract DSL
Status: WORKING (tested in verify-all)
What it does: a deterministic policy evaluation engine. A contract's rule layer is a short DSL (Sutra text or a fixed COBOL record) holding only bounded predicates: per-transfer caps, cumulative limits, signature thresholds, validity windows, lockouts, jurisdiction allow and deny tags, and dated payment terms. policy_check answers ALLOW, DENY or FAULT for a transfer before a ledger commit; a contradiction is denied with escalation to a person. policy_lower turns the terms into a zxt treaty with escrow and mediation bundles. zxp_draft fills templates. It gives no legal advice and makes no legal determinations.
Main entry points: zxp_parse_sutra/emit_sutra, zxp_parse_record/emit_record, zxp_validate, policy_check, policy_commit, policy_terms, policy_lower, policy_bundle_text, policy_pack_init, policy_host_register, CALL 'ZXV_POLICY' (policy_cobol.c).
Tests: test-policy in verify-all: test_zxp.c (156 checks), test_policy.c (81 checks), fuzz_zxp (300000 cases, plus 30000 under ASan/UBSan) and a freestanding check. test-policy-cobol, also in verify-all, checks COBOL parity and skips when cobc is missing unless ZXV_REQUIRE_COBOL=1. mut_check.sh (test-policy-mut) is not in verify-all.
Used by: no caller outside its tests. Not in any kernel image, zxvd or libzxv. The zxp_* names in sdk/embedded are an unrelated port layer.
Gaps: jurisdiction packs are operator configuration and do not state what the law is. The COBOL call is stateless (no packs, limits judged from zero). It shares the zxp_ prefix with provenance's zx_provenance.h (no name clash found). See [kernel/src/policy/README.md](../kernel/src/policy/README.md).

### porter_house — port firewall ("seals" on network ports)
Status: WORKING (tested in verify-all)
What it does: Keeps a table of up to 32 port seals. Each seal is OPEN, TRUSTED (a trust threshold), ALLOWLIST (listed peer ids) or CLOSED. `porter_house_admit` decides whether a peer may reach a port. A port with no seal is admitted (the firewall is opt-in). It also computes a coverage score from admit/reject counts.
Main entry points: porter_house_init, porter_house_seal_port, porter_house_admit, porter_house_close_port (now returns int32_t), porter_house_open_port, porter_house_allowlist_add, porter_house_update_coverage.
Tests: src/porter_house/test_porter_house.c (verify-all). A full-seal-table close_port case was added.
Used by: mesh_net, identity_fabric, immigration, governance_fabric, finance, ai_layer, emu, abstraction_layer, robin_debanks (pointer stored, never used). arm64: yes.
Gaps: Default-admit for unsealed ports is a policy choice that callers must know about. Fixed in this audit: close_port used to fail OPEN when the table was full.

### power — energy ledger per app and agent, battery state and budgets
Status: WORKING (tested in verify-all)
What it does: power_ledger is a pure integer ledger fed by a platform layer. It takes cumulative energy meters (whole device, parts, sub-parts), rejects impossible samples, settles each interval (marking disagreeing meters as a conflict and using the larger figure), and splits the total among apps, agents and the system by metered energy or CPU and GPU time so the figures add up exactly. It also derives battery health, time left and a power posture, and admits work against energy budgets. power_rapl.c converts x86 RAPL MSR readings for bare metal.
Main entry points: pw_init, pw_meter_add/sample, pw_consumer_add, pw_note_activity/energy, pw_settle, pw_battery_set, pw_posture, pw_budget_set, pw_admit, pw_top, pw_render_json, pw_rapl_to_uj.
Tests: test-power in verify-all: test_power.c (304 checks), arch/hosted test_zxv_power_host.c (55 checks), mobile/core test_zxv_mobile_power.c (29 checks) and a freestanding check.
Used by: zxvd's power group (power.status, power.top, power.budget in kernel/arch/hosted/zxvd_power.c, listed in app_sources.sh), the zxpower tool (kernel/arch/hosted/zxpower.c), and the mobile core (mobile/core/zxv_mobile_power.c, build_mobile_core.sh). Not in any kernel image.
Gaps: power_rapl.c has no caller; RAPL is not wired into a boot image (no fault-safe rdmsr), and bare metal has no ACPI battery, thermal zones, sleep states or frequency scaling. The Brazier power app is not built. Windows reads no battery design capacity. [POWER.md](POWER.md) still says zxvd is wired "on PR #109, not in this PR", but zxvd_power.c is now in the tree.

### pqsec — post-quantum signatures and KEM (ML-DSA, SLH-DSA, ML-KEM, HQC)
Status: WORKING (tested in verify-all)
What it does: Vendored/ported ML-DSA-65/87, SLH-DSA, ML-KEM-768/1024 and HQC-5, with a "matrix" layer that wraps every algorithm behind one encode/decode/sign/verify table. pq_security.c adds a hybrid ML-DSA-65 + SLH-DSA signature for ledger entries and boot verification, with results reported as LPRES states.
Main entry points: pq_hybrid_sign / pq_hybrid_verify, pq_boot_verify, pq_identity_authenticate, pq_self_test, pq_matrix_* (pq_matrix.h).
Tests: test_pq_security.c (24 checks), test_pq_kat.c (29, known-answer vectors from gen_pq_kat.py), test_pq_matrix.c (165 + 45,600 fuzzed decoder inputs). All are in verify-all. `make -C kernel kat-cross` (CI job kat-cross) runs test_pq_kat and test_pq_matrix on aarch64, riscv64, armhf, i686 and s390x under qemu-user. `make -C kernel ct-valgrind` runs ML-DSA-65 signing under valgrind with the secret key marked undefined and prints every report (not gated; the reports are in the vendored reference's rejection loop, hint and challenge code).
Used by: quest, ident, cardnet, curzi, devmesh, ehop, evolve, market. Not in arm64 list. Packaged in packages/libzxv-pqc (ML-DSA-65/87, SLH-DSA-128s/256s, ML-KEM-1024; not HQC or pq_matrix) (static library built from these sources; smoke program in verify-all).
Gaps: The header claimed "voltage severing" layers, ML-KEM-1024 for layer 5 and "valid if EITHER half verifies". All three are corrected. Hybrid verify must accept only LPRES_STATE_TRUE. The PQ_KEM1024_* macros are unused. The vendored HQC-5 reference serialises uint64_t vectors with memcpy, so it fails its KAT on big-endian CPUs (s390x); do not use HQC-5 there. SLH-DSA-SHAKE was wrong on big-endian and is fixed (sha3_f1600.c, slh_shake.c).

### predictive — surplus trajectory forecaster
Status: WORKING (has a test, not in verify-all)
What it does: Runs the surplus dynamics model (surplus.c) forward over a horizon with interaction and cost trajectories. It reports the projected surplus, cumulative cost and a sustainability index, and edp_risk gives a risk overlay.
Main entry points: predictive_single, predictive_multi, predictive_model_init (predictive_model.h).
Tests: src/predictive/test_predictive.c (NEW, 6 checks; passes under both TEST_HOST double and Q32.32). Not in verify-all, so a recipe is in group4_verify_lines.mk. tests/test_new_modules.c (repo root) also covers it but is not built.
Used by: finance, situation. arm64: yes.
Gaps: Fixed in this audit: predictive_multi never stepped the dynamics, so cost was ignored and the sustainability index was always 0. On x86 -m32 it uses libgcc 64-bit division helpers.

### prism_break — framebuffer "prism" refraction shader
Status: WORKING (tested in verify-all)
What it does: Draws a prism and depth visual effect into a 32-bit ARGB framebuffer for the touchscreen desktop. Width and height are clamped to 1920x1080, and it keeps a back buffer that is copied out each frame.
Main entry points: prism_break_init, prism_break_render / frame functions (prism_break.h).
Tests: test_prism_break.c (verify-all).
Used by: desktop. arm64: yes.
Gaps: CodeQL cpp/integer-multiplication-cast-to-long at prism_break.c:389 is fixed. The header says "All Rights Reserved" next to an Apache-2.0 SPDX line.

### privacy — exact differential privacy, four-valued access checks and selective disclosure
Status: WORKING (tested in verify-all)
What it does: draws discrete Laplace noise with integer arithmetic only (Canonne, Kamath and Steinke 2020) and spends an exact privacy budget before each noisy count. It checks access requests against allow and deny rules with a four-valued answer (no rule never allows; allow and deny together escalate), and refuses special-category data without current consent or records past retention. It checks selectively disclosed claims against an issuer-signed list of salted SHA-256 digests. It is designed to support GDPR- and HIPAA-style controls, not to claim compliance.
Main entry points: priv_dlap, priv_bern/bern_exp, priv_budget_init/spend, priv_release_count, priv_access, priv_sd_digest, priv_sd_list_msg, priv_sd_check.
Tests: test_privacy.c (120 checks, ASan/UBSan) in test-privacy under test-adapters, which verify-all runs.
Used by: no caller outside its tests. Not in any kernel image, zxvd or libzxv. zui_privacy (the sensor in-use indicator) is a separate module.
Gaps: no zero-knowledge proofs. The noise needs a CSPRNG from the caller, and the sampler's running time depends on the noise drawn. Not in libzxv yet and not wired to zxvd. See [ADAPTERS.md](ADAPTERS.md).

### provenance — canonical signed-manifest digest and a test-only signer
Status: WORKING (tested in verify-all)
What it does: zx_provenance.h is a header-only, length-prefixed canonical encoding of (domain, package id, version, architecture, dependencies, content CID), hashed with SHA-256, so a signature covers the whole manifest and not only a CID. Domain tags keep a signature for one module from being accepted by another. test_signer.c/.h derive Ed25519 test keys from fixed public seeds for host tests only; no build trusts them.
Main entry points: zxp_manifest_digest, zxp_canon_init/u32/u64/bytes, zxp_str, zxp_mem; test_signer_init, test_signer_sign.
Tests: test_provenance.c (18 checks) in verify-all. test_signer is linked into test_broker, test_abupdate, test_update and test_zxrelease (all in verify-all) and into the zxvd estate and instruments test scripts.
Used by: update.c (all five kernel images and the hosted app's update checker), broker.c (all five kernel images), zxpkg.c and bootlegger.c (arm64 kernel).
Gaps: the header names zx_upcheck manifests as a user and defines ZXP_DOMAIN_UPMAN, but zx_upcheck does not include it and nothing uses that domain. No dedicated doc.

### provider — compute/storage/model provider marketplace
Status: WORKING (tested in verify-all)
What it does: Lets providers list services, matches them to demand, meters delivered usage from signed receipts, and settles payment. It has a capital-markets view, a chain adapter with exact base-unit conversion, a PQ-signature binding and a swarm bridge. Adapter/env/receipt decoders parse external bytes.
Main entry points: prov.h (listing, match, fill state machine), prov_adapter_parse, prov_env_decode, prov_receipt_decode / settle (prov_receipt.c), prov_chain.h, prov_pay.h, prov_swarm.h.
Tests: test_prov.c (150), test_prov_chain.c (62, vectors from gen_prov_chain_vectors.py), test_prov_bridge.c (33). All are in verify-all. The three decoders were fuzzed here for 20k iterations each under ASan/UBSan with no crash.
Used by: no #include from outside the directory; it is linked by tests of pay/swarm. Not in arm64 list.
Gaps: A receipt with a bad signature moves a RESERVED fill to DISPUTED (prov_receipt.c:264), so a third party could possibly grief a fill (LOW).

### pterm — framebuffer/serial terminal and multiplexer
Status: WORKING (tested in verify-all)
What it does: A freestanding virtual-console engine over the framebuffer and UART, with line editing, history and built-in commands (pterm_commands.c). pterm_mux runs master/sub terminals that advance on the phase tick rather than a timeslice.
Main entry points: pterm.h (pterm_init, input/render, command dispatch), pterm_mux.h (pmux_spawn, pmux_tick).
Tests: src/pterm/test_pterm_mux.c and tests/host test_pterm (23/23). Both are in verify-all.
Used by: kernel console paths; uses sched. arm64: yes (3 files).
Gaps: None found beyond general freestanding-portability notes.

### pungent — garlic-style layered encryption bundle model
Status: WORKING (tested in verify-all)
What it does: Seals several messages ("cloves") for their own destinations, packs them into a "bulb" and wraps the bulb in one authenticated encryption layer. Services are addressed by SHA3-256 of their public key.
Main entry points: pungent.h (clove seal, bulb pack/unpack, service lookup).
Tests: test_pungent.c (verify-all).
Used by: decent/bootlegger tests (linked). arm64: yes.
Gaps: It is a model; there is no transport. The header says so honestly.

### qeq — exact equivalence checker for small quantum circuits
Status: WORKING (tested in verify-all)
What it does: decides whether two circuits on the same n qubits (n <= 10) implement the same unitary, exactly, by simulating every basis column with qsim. Modes are strict or up to a global phase; methods are direct comparison or a miter with the adjoint. On a mismatch it returns an exact counterexample. A strict OpenQASM 2.0 subset parser accepts only exact multiples of pi/8. This is classical software on an ordinary CPU, not a quantum computer.
Main entry points: qeq_check, qeq_opts_default, qeq_circuit_N, qeq_qasm_parse, qeq_qasm_write.
Tests: test_qeq.c (11829 checks, ASan/UBSan), a bench_qeq build check and a freestanding check, in test-qeq under test-research, which verify-all runs.
Used by: no caller outside its tests and bench. Not in any kernel image, zxvd or libzxv.
Gaps: exponential cost; n > 10 is refused. Only the Clifford+T plus pi/8 gate set; arbitrary angles give NEITHER. No ancillas, qubit permutation search, measurement or classical control. See [CIRCUIT_EQUIVALENCE.md](CIRCUIT_EQUIVALENCE.md).

### qnet — obligation netting by cycle cancellation
Status: WORKING (tested in verify-all)
What it does: takes a directed graph of integer obligations and finds a circulation that cancels debt around cycles, so every member's net position is unchanged and no new counterparty pairs appear. Three solvers share one output type: a greedy cycle canceller, an exact optimum (min-cost circulation with an independent optimality check), and a "quantum-inspired" annealer that is really classical replica-exchange annealing on a CPU. Every result is verified before it is returned.
Main entry points: qn_graph_init/add/check, qn_greedy_cycles, qn_exact_optimum, qn_check_optimal, qn_anneal, qn_verify, qn_net_positions.
Tests: test_qnet.c (9170 checks, ASan/UBSan), a bench_qnet build check and a freestanding check, in test-qnet under test-research, which verify-all runs.
Used by: no caller outside its tests and bench. Not in any kernel image, zxvd or libzxv.
Gaps: nothing here is quantum, and the annealer can only tie or lose against the exact solver. Fixed capacity (64 members, 512 obligations), one currency per graph, no persistence, and no combination with the gridlock module. See [QUANTUM_NETTING.md](QUANTUM_NETTING.md).

### qnote — Q-Note quarantine lots, Dutch auction and first-loss recovery
Status: WORKING (tested in verify-all)
What it does: for a claim that mint quarantined, opens a lot of Q-Notes (a claim on whatever is later salvaged) and sells them by a descending-price Dutch auction with escrowed bids on pay_ledger. All proceeds go to the reserve. Later salvage pays holders up to the face of their notes, the solvency vault (svault) burns V-Bills as first loss, and closing writes off what remains without seizing anything. Q-Notes never carry interest.
Main entry points: qnote_init, qnote_lot_open, qnote_bid, qnote_demand, qnote_clear, qnote_settle, qnote_loss, qnote_first_loss, qnote_recover, qnote_close, qnote_check.
Tests: test_qnote.c (372 checks, ASan/UBSan, also links mint) in test-qnote under test-econ, which verify-all runs.
Used by: no caller outside its tests. Not in any kernel image, zxvd or libzxv.
Gaps: a Q-Note is worth only what is actually recovered. The uncovered loss must come from slashed bonds or salvage outside this module. The header notes Q-Notes are likely securities and need legal review before real use. No dedicated doc; [ARCHITECTURE.md](ARCHITECTURE.md) lists it as built and host-tested.

### qr — QR Code encoder (byte mode, level M)
Status: WORKING (tested in verify-all)
What it does: encodes bytes into a QR Code module matrix (ISO/IEC 18004), byte mode and error-correction level M only, versions 1 to 10, with Reed-Solomon and automatic or chosen masks, and renders it as text or SVG. It was written for device-claim payloads (ztp). No allocation, no libc, no float.
Main entry points: qr_encode_bytes, qr_capacity, qr_module, qr_render_text, qr_render_svg.
Tests: test_qr.c (60 checks) compares whole symbols with fixtures on which python-qrcode and qrcodegen agree (gen_qr_fixtures.py), in mk/rot.mk test-rot, which verify-all runs through test-retail. A freestanding check runs there too.
Used by: only ztp's test (test_ztp.c); ztp.c itself does not call it. Not in any kernel image, zxvd or libzxv.
Gaps: no numeric, alphanumeric, Kanji, ECI or structured-append modes, no other error-correction levels, no Micro QR, no raster output. See [retail/ROOT_OF_TRUST.md](retail/ROOT_OF_TRUST.md).

### qsim — exact classical quantum circuit simulator
Status: WORKING (tested in verify-all)
What it does: simulates up to 10 qubits exactly on a classical CPU. Every amplitude is an element of a cyclotomic integer ring over one shared power of sqrt(2), so there is no floating point and equal states have identical representations. It supports the Clifford+T gate set, pi/8 phases, CNOT, CZ, SWAP and Toffoli, exact probabilities, caller-chosen measurement and seeded sampling. It is not a quantum computer and gives no speedup.
Main entry points: qs_init, qs_load, qs_apply, qs_gate, qs_circuit_init/push, qs_run_circuit, qs_circuit_adjoint, qs_state_equal, qs_prob_basis/qubit, qs_prob_cmp, qs_check_unit, qs_measure, qs_sample.
Tests: test_qsim.c (23422 checks, ASan/UBSan) and a freestanding check, in test-qsim under test-research, which verify-all runs.
Used by: qeq only. Not in any kernel image, zxvd or libzxv. It is separate from the older quantum module.
Gaps: exponential state size (10 qubits, a 64 KiB state). int64 coefficients overflow after enough T-type gates, and probability queries then return QS_ERR_OVERFLOW. No arbitrary rotation angles. See [QUANTUM_SIMULATOR.md](QUANTUM_SIMULATOR.md).

### quantum — "quantum and exotic matter device" numeric model
Status: WORKING (tested in verify-all)
What it does: Holds structs and formulas for Casimir cavities, "zero-point energy extraction", a "wormhole throat" and exotic matter, and returns numbers from them. It controls no hardware and extracts no energy.
Main entry points: quantum_device.h (device init/step/readout functions).
Tests: test_quantum_device.c (verify-all, TEST_HOST only). Line 236 gives an incompatible-pointer warning when built in Q32 mode.
Used by: no includes outside the directory. arm64: yes.
Gaps: The physics claims were misleading; a "WHAT THIS IS: a numeric model" note was added to the header. On x86 -m32 it uses libgcc 64-bit division helpers.

### qubo — QUBO and Ising workbench with optimality certificates
Status: WORKING (tested in verify-all)
What it does: solves QUBO and Ising problems (the format sent to quantum annealers) on an ordinary CPU and states how good each answer is: every result carries an exact energy, a proven lower bound and a gap, so it is marked optimal, a bounded gap, or heuristic only. Solvers are brute force, branch and bound, replica-exchange annealing and local search. The text parser refuses contradictory or incomplete input instead of guessing. Nothing here is quantum.
Main entry points: qubo_init/set/add, qubo_parse, qubo_energy, qubo_to_ising/from_ising, qubo_brute_force, qubo_bnb, qubo_anneal, qubo_local_search, qubo_root_bound, qubo_verify.
Tests: test_qubo.c (295 checks, ASan/UBSan), a bench_qubo build check and a freestanding check, in test-qubo under test-research, which verify-all runs.
Used by: no caller outside its tests and bench. Not in any kernel image, zxvd or libzxv.
Gaps: at most 64 variables. The branch-and-bound bound is simple, so dense instances beyond about 30 to 40 variables usually end with a gap, not a proof. No speedup is claimed. See [QUBO_WORKBENCH.md](QUBO_WORKBENCH.md).

### quest — mastery/cooperation game layer with badges
Status: WORKING (tested in verify-all)
What it does: Quests, co-op groups, anti-abuse guards and earnable badges, plus quest payouts through the pay layer. Badges are signed with ML-DSA and encoded and decoded as bytes, and minors' badges are kept non-public.
Main entry points: quest.h, quest_badge.h (qst_badge_encode/decode), quest_coop.h, quest_guard.h, quest_pay.h, quest_sign_mldsa.h.
Tests: test_quest.c (133 checks, verify-all). qst_badge_decode was fuzzed here for 20k iterations with no crash.
Used by: social (and it uses reputation and pqsec), and the desktop app: every compute or storage sale the node completes (vna_link L5, countersigned by the buyer, who is the verifier) is recorded for the owner, and /api/status shows the eight skills. Not in arm64 list.
Gaps: the app's node trades nothing until owner controls for agreements and wants exist, so nothing is credited yet in practice.

### rational — exact rational arithmetic (WyvernEye)
Status: WORKING (tested in verify-all)
What it does: Exact fraction arithmetic for spreadsheet and finance values, so that 0.1+0.2 == 0.3 and splits conserve value. Operations reduce by gcd and report overflow instead of wrapping.
Main entry points: rational.h (rat_add/sub/mul/div, rat_cmp, rat_split, rat_is_int, rat_from_int).
Tests: test_rational.c and test_rational_regress.c (verify-all).
Used by: finance, abacus, lightningrod, mixmat, orbital_compat, sdk_bridge. arm64: yes. Packaged in packages/libzxv-legacy (static library built from these sources; smoke program in verify-all).
Gaps: rational.c has no SPDX line.

### rce — SI units and dimensional analysis
Status: WORKING (tested in verify-all)
What it does: Represents physical quantities with the 7 SI base dimensions and checks that arithmetic is dimensionally consistent (you cannot add metres to seconds). It also formats quantities with units.
Main entry points: rce_units.h (quantity constructors, mul/div/add with dimension checks, quantity_format).
Tests: tests/host test_rce_units (21/21, verify-all). There is no test inside src/rce.
Used by: no includes outside the directory. arm64: yes.
Gaps: The "Jacob's Ladder physics" framing in the header is decorative.

### reality — sigil-circuit evaluator over live variables
Status: WORKING (tested in verify-all)
What it does: Evaluates a small directed circuit of nodes (SUM, DIFF, etc.) whose inputs are bound to live kernel variables. An all-zero circuit evaluates cleanly to zero.
Main entry points: reality.h (circuit build, bind, evaluate).
Tests: test_reality.c (verify-all).
Used by: bootfeat. arm64: yes.
Gaps: reality.c has no SPDX line.

### recon — reconstruction-step labels over an S-Map
Status: WORKING (has a test, not in verify-all)
What it does: Six "reconstruction" operations on an smap_t: each checks that the S-Map is internally consistent with smap_verify and then records a label or phase. It does not rebuild payload bytes; smap does that.
Main entry points: recon.h (six recon_* operations).
Tests: src/recon/test_recon.c (NEW, 8 checks). A recipe is in group4_verify_lines.mk. tests/test_new_modules.c (root) is not built.
Used by: no includes outside the directory. arm64: yes.
Gaps: Fixed in this audit: all six ops were stubs that succeeded on a corrupt map, and the header claimed "O(1) reconstruction". The module is thin and may be a candidate for merging into smap.

### recover — recovery framework: refuse, roll back, isolate, with signed proofs
Status: WORKING (tested in verify-all)
What it does: The designed recovery of audit gap B5 (roadmap P3.2). A fault is answered with exactly one of three actions: REFUSE (nothing changes), ROLL BACK (the state is restored byte for byte from a checkpoint the caller made) or ISOLATE (the component is quarantined for good). Checkpoints go into two A/B shadow slots, the update work's discipline: written to the slot that is not known-good, read back, then promoted; at rollback a slot whose bytes no longer match their SHA-256 is dropped for good and the older slot is used. A rollback is run by the paradox engine's `pdx_destructive`, so the restore, the quarantine record and the RepairProof are paradox's own. Every action produces a RecoveryProof (paradox's `pdx_repair_t` plus action, reason, checkpoint generation and log position), appended to paradox's hash-chained operator log and signed by the caller's signer; `recover_pq` binds ML-DSA-87 with the domain context `zxv-recover-v1`. Hard invariants fail closed: a forbidden tag or a failed conservation check (the framework's invariant and an optional per-case one) ends in a rollback, or in isolation when no intact checkpoint passes the invariant; a broken state is never checkpointed and a broken checkpoint is never restored. A full log or isolation table changes nothing and halts the framework; a failing signer halts it after the safe action. Crypto-free policy file, freestanding (no libc, no allocation, no float).
Main entry points: recover.h (rcv_init, rcv_shadow_init, rcv_checkpoint, rcv_refuse, rcv_rollback, rcv_isolate, rcv_recover, rcv_proof_verify, rcv_proof_in_log); recover_pq.h (rcv_mldsa87_sign, rcv_mldsa87_verify).
Tests: test_recover.c (`make -C kernel test-recover`, in verify-all, ASan/UBSan, 156 checks): every action and refusal path with digests recomputed from the documented layouts, the A/B shadow, exact-byte rollback, sticky isolation, full logs and a full isolation table, a failing signer, every proof field tampered, ML-DSA-87 end to end, and 2,000 random faults after which no hard break stays live; plus a freestanding aarch64/i386 build of recover.c. tripartite_fs's test covers the migrated caller.
Used by: tripartite_fs (tf_self_heal_file's refusal, tf_checkpoint_file, tf_recover_file). arm64: no.
Gaps: It records and decides recoveries; it detects only what the caller's tag and invariant checks report. A checkpoint is one caller-owned byte region of fixed length; devices, the network and other modules' state are not rolled back. The A/B functions of abupdate (ZXVFS files) and hwr (signed device images) are bound to their formats, so the two-slot discipline is reused, not their code. sdk_bridge and event_space still recover ad hoc and link into kernel images; sdk_bridge's self-heal still sets its attestation to TRUE (an auto-patch).

### refinery — intent text to sigil-card compiler
Status: WORKING (tested in verify-all)
What it does: Takes typed intent in any language, normalises it, and "forges" a deterministic sigil card using the Enochian table (enochian.c). Empty and oversized intents are refused. Presets pack and unpack as bytes.
Main entry points: refinery.h (ref_forge, ref_preset_pack/unpack), enochian.h.
Tests: test_refinery.c (verify-all). ref_preset_unpack and ref_forge were fuzzed here for 20k iterations with no crash.
Used by: desktop, wyverneye. arm64: yes (2 files).
Gaps: The refinery/* files have no SPDX line.

### reputation — "Pig Badge" abuse signal and earnable badges
Status: WORKING (tested in verify-all)
What it does: Tracks a counter-abuse signal for accounts with repeated platform violations, and monotonic earnable badges. A badge re-awarded at a lower level does not demote.
Main entry points: reputation.h (record violation, badge award/query).
Tests: test_reputation.c, plus a q32 variant (verify-all, also clean under ASan/UBSan).
Used by: social, quest, desktop, governance_fabric, identity_fabric, logistics. arm64: yes.
Gaps: None found.

### rmag — rational resource budgets (RMAG) and resource tables
Status: WORKING (tested in verify-all)
What it does: rmag.c keeps rational-valued resource budgets (allocate, consume, release, compare) for CPU/memory/energy. rmag_core.c is a slot table of rational quotas used by the scheduler and the sephirot engine.
Main entry points: rmag.h (rmag_consume, rmag_release, rmag_allocate_from_source, rational add/mul/div/equal/less_than), rmag_core.h.
Tests: tests/host test_rmag (21/21, verify-all), src/rmag/test_rmag.c (make test), and src/rmag/test_rmag_budget.c (NEW, 15 checks; fails 7 of them on the old code). The new test's recipe is in group4_verify_lines.mk.
Used by: sched, sephirot, mm, dharma, dharana, vino, wyrmgate. arm64: yes (2 files).
Gaps: Fixed: the arithmetic wrapped and comparisons were inexact, so a consume of 2^64-400 was accepted, and negative amounts refunded budget. Fixed: rmag_div_quotas(x, 0) returned sign(x)/1 (now 0/1, plus rmag_div_quotas_checked), and rmag_rational_add(x, -x) saturated with an order-dependent sign when the common denominator overflowed (now 0); test src/rmag/test_rmag_div.c (verify-all), Lean model proofs/rational_bounds. Not fixed: rmag_core add/sub/mul quota math has unchecked signed int64 overflow, and it uses libgcc 64-bit division on x86.

### robin_debanks — time-locked vault plus the shared crypto primitives
Status: WORKING (tested in verify-all)
What it does: Contains AES-256-GCM, SHA-256, HMAC-SHA256 and Ed25519 verify (orlp/ed25519 plus a canonical-S check), which much of the tree links against. robin_debanks.c is a vault of AES-GCM-encrypted entries that unlock after a cycle count.
Main entry points: aes256_gcm.h, sha256.h, ed25519_verify.h, crypto_verify.h, robin_debanks.h (robin_vault_init, robin_store, robin_unlock_ready, robin_retrieve).
Tests: test_hmac_fullcover.c (verify-all). test_robin_debanks.c and crypto_validate.c are not in verify-all; recipes are in group4_verify_lines.mk. Here AES-GCM, SHA-256 and HMAC matched Python `cryptography` on 145 vectors, and Ed25519 on 43 cases.
Used by: dozens of modules (ipfs_node, pay, cb_net, freight, ident, chronicle, zsp, ...). arm64: yes (5 files).
Gaps: Vault claims of PIN/KDF, Porter House gating (the pointer is unused), an audit trail and a ROBIN_MAX_ATTEMPTS lockout have no code behind them, and total_denied is never incremented. crypto_verify.c hard-codes patterned "authority keys". The Makefile KAT loop points at files that do not exist. "All Rights Reserved" sits next to Apache-2.0. ed25519_verify.c.bak is dead.

### royalty — IP royalties paid only from realised sales
Status: WORKING (tested in verify-all)
What it does: registers works by content hash with a manifest signed by the first owner (owners and weights, an optional parent work and upstream share, licence terms). On a sale already realised on pay_ledger, it computes the royalty, walks the derivative-work chain upstream, splits each amount exactly among owners by largest remainder, and applies each owner's opt-in giving or investing split. Every payment is shown as a quote first. There are no advances, interest or minimum guarantees, and roy_check proves every split sums exactly.
Main entry points: roy_init, roy_register, roy_update, roy_split_set, roy_quote, roy_pay, roy_royalty, roy_lrm, roy_grant_cap, roy_check, roy_terms_zxv_hw.
Tests: test_royalty.c (400 checks, ASan/UBSan) in test-royalty under test-econ, which verify-all runs.
Used by: no caller outside its tests. Not in any kernel image, zxvd or libzxv.
Gaps: registering a hash proves who signed a manifest, not who owns the work. Nothing is withheld for tax. A receipt is checked only within the ledger's journal window, and the declared gross is checked only against what that posting received. Collection outside a realised sale is not modelled. No dedicated doc; [ARCHITECTURE.md](ARCHITECTURE.md) lists it as built and host-tested.

### rur — Root Universal Representation type layer
Status: WORKING (tested in verify-all)
What it does: A typed metadata layer that connects source-language front-ends to the ZXV Event IR. It records module and type compatibility and is explicitly not a runtime authority.
Main entry points: rur.h (module register, type lookup, compatibility checks).
Tests: tests/host test_rur (11/11, verify-all).
Used by: no includes outside the directory. arm64: yes.
Gaps: None found.

### sched — round-robin task table (legacy x86 scheduler)
Status: PARTIAL
What it does: A fixed table of tasks with name, type, state, a stack and a saved eip/esp. Tasks can be created, terminated and picked round-robin, and rmag coverage feeds a "phase" value. It never saves or restores registers, so there is no actual context switch.
Main entry points: sched_init, sched_create_task, sched_terminate, sched_next (sched.h).
Tests: src/sched/test_sched_ptr.c (verify-all test_sched_ptr): an entry address above 4 GB survives sched_create_task in eip and the prepared stack frame.
Used by: pterm, apps, bringup, compute_fabric, dharma. arm64: yes (2 entries).
Gaps: Fixed: create_task silently overwrote slot 0 when full, the name copy was unbounded, terminate compared the wrong thing, and the header claimed a context switch. Also fixed: esp/ebp/eip/cr3 and the stack are uintptr_t (no truncation above 4 GB), sleep_until is 64-bit like ticks (it wrapped after 2^32 ticks), and the phase is integer permille.

### sdk — application SDK headers and template
Status: STUB
What it does: m5_api.h declares an app API (m5_* drawing, input and capital calls) and selfaudit.h declares self-audit hooks. app_template.c and Makefile.app show how an app would be built.
Main entry points: m5_api.h, selfaudit.h, app_template.c, Makefile.app.
Tests: verify-all compiles app_template.c with Makefile.app's host `test` flags (compile only,
not linked). Makefile.app's `test` target is an honest compile-only check.
Used by: headers are included by apps, app_fabric, compute_fabric, financial_fabric, governance_fabric, identity_fabric and others. Not in arm64 list.
Gaps: No m5_* function is implemented anywhere, so apps will not link; a status note was added to SDK_README.md. Fixed: the KERNEL_ROOT path in Makefile.app, and the -Werror failure in app_template.c.
Gaps: m5_api.h and selfaudit.h now open with a STATUS: STUB banner (declarations only, no stable ABI).

### sdk_bridge — capability bridge from apps/languages to kernel fabrics
Status: PARTIAL
What it does: sdk_bridge.c gives processes capability-checked contexts into the financial, crypto-wallet and mesh fabrics. sdk_bridge_lang.c exposes the same operations through the orbital_compat IR so language front-ends can call them. polyglot_matrix.c maps languages to back-ends.
Main entry points: sb_create_context, sb_has_capability, sb_self_audit_context (sdk_bridge.h); sb_lang_execute, sb_lang_create_context, sb_lang_set_bridge (NEW) (sdk_bridge_lang.h).
Tests: none. It is compile-checked for arm64 and TEST_HOST here.
Used by: apps. arm64: yes (sdk_bridge.c, sdk_bridge_lang.c). Nothing calls sb_lang_*.
Gaps: Fixed: the context type confusion that wrote past the struct; a NULL bridge dereference in every op; reads past oc_ir_t.fields[8] (indices up to 94); rat_to_surplus sign and overflow. Not fixed: the ops that need more than 8 IR fields can never run, the callbacks pass placeholder ids of 0xFFFFFFFF, and there are unused-parameter warnings.

### seats — people on one device and the login after boot
Status: WORKING (tested in verify-all)
What it does: keeps the roster of people who use one device (roles owner, person, guest) and checks a login. Each seat chooses no lock, a passphrase (stored only as an Argon2id hash from kernel/src/ident with a per-seat salt) or the device's own passkey unlock. Wrong passphrases back off: after 5 free tries each wait doubles from 30 s to at most one hour. There is always at least one owner, and only an owner may add or remove seats. Integer only, freestanding, no allocation; the caller owns the roster array and can grow it.
Main entry points: seat_roster_init, seat_roster_grow, seat_add, seat_remove, seat_set_lock, seat_unlock, seat_auto_enter, seat_wait_ms; types seat_roster_t, seat_t, seat_env_t.
Tests: src/seats/test_seats.c (41/41 checks) runs twice in verify-all (the main block and test-orphans). kernel/arch/hosted/test_zxvd_seats.sh tests the zxvd group; it runs in CI through zxvd_checks.sh, not in verify-all.
Used by: zxvd's seats.* group (kernel/arch/hosted/zxvd_seats.c: seats.list, add, remove, set_lock, set_auto, auto, unlock, lock), which saves the roster in a "seats" file beside the books (written to seats.tmp and renamed) and keeps login sessions in memory only. zui_login.h draws the screen but does not call this module. Not in either kernel image.
Gaps: a passkey seat opens on the caller's `passkey_ok` flag; zxvd does not check the passkey itself (the header says the host checks it). Login and per-seat terms on screen on bare metal are missing ([BAREMETAL_OS_GAPS.md](BAREMETAL_OS_GAPS.md), row 13). The zxvd group is documented in [ZXVD.md](ZXVD.md#seats-the-people-on-this-device-and-the-login-after-boot).

### security_fabric — "security fabric" (placeholder crypto)
Status: UNUSED/DEAD
Location: kernel/experimental/security_fabric/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: Meant to provide signing, verification and encryption services to the app fabric. Every crypto routine is a placeholder: the signature length is fixed, the ciphertext is length+32, and verification returned true.
Main entry points: security_fabric.h.
Tests: none.
Used by: app_fabric and app_constellation include the header, but no build compiles the .c (it does not compile: missing headers and `return -1.` syntax errors). Not in arm64 list.
Gaps: Verification now fails closed and there is an AUDIT STATUS banner. It should be removed or rebuilt on robin_debanks/pqsec primitives before anything depends on it.

### sephirot — 13-phase multi-valued logic engine
Status: WORKING (has a test, not in verify-all)
What it does: Uses the Tree of Life as names for a 13-state logic that sits over the trit/M5 types, with rmag_core quotas per phase. The header is honest that it gives no cryptographic or consensus guarantee.
Main entry points: sephirot.h (init, evaluate, phase transitions).
Tests: src/sephirot/test_sephirot.c passes but is not in verify-all; a recipe is in group4_verify_lines.mk.
Used by: dharma. arm64: yes.
Gaps: The sephirot files have no SPDX line.

### settle — settlement spine: swarm money on the ledger of record
Status: WORKING (tested in verify-all)
What it does: posts every change in the swarm market's Financial capital to a pay_ledger (asset "SWC", swarm credit, never an ISO currency), then reconciles the two books after every cycle. If the market's money is not conserved, or the ledger and the market disagree, the spine halts and refuses every later call; it never edits either book to make them agree. settle_levy charges the 0.08889% assurance fee on the cycle's pot, with a sub-unit carry, into the four fee buckets, and returns last cycle's node-bounty bucket to the pot. An optional peer_audit hook sees each posting before it is applied and the spine checks the resulting state hash.
Main entry points: settle_init, settle_join, settle_sync, settle_levy, settle_set_audit, settle_reconciled; type settle_spine_t.
Tests: src/settle/test_settle.c under ASan/UBSan, 89 passed, via test-settle in verify-all.
Used by: kernel/arch/hosted/zxv_host.c, which is built into both the desktop app (zxv-host) and zxvd (app_sources.sh srcs). It calls settle_init when the swarm is built, settle_join per model, and settle_levy then settle_sync every market cycle; a halted spine stops the market. Not in either kernel image.
Gaps: the spine's ledger lives in memory and is rebuilt with the swarm; the owner's saved books only get a mirror "swarm earnings" posting ([ZXVD.md](ZXVD.md#swarm-earnings-in-your-books)). What spends the reserve, dividend and regenerative buckets is outside this module.

### shimmer — desktop background shimmer effect
Status: WORKING (tested in verify-all)
What it does: Computes a slow, integer-only caustic interference pattern with occasional gold glints and blends it into the desktop ground colour each frame. With amplitude 0 and glint 255 the effect is fully disabled.
Main entry points: shimmer.h (init, shade, step).
Tests: test_shimmer.c (verify-all).
Used by: tolvovina (desktop). arm64: yes.
Gaps: shimmer.c has no SPDX line.

### sicily — Sicily Account view, Player Card tiers, steward threshold
Status: WORKING (tested in verify-all)
What it does: three pieces on pay_ledger. sic_view sums one owner's accounts into assets ("Neo-Roman"), liabilities ("New California") and derived equity ("Sicilian Crown"), and gives Spendable, Pending and Net Worth. sic_tier_eval turns credibility, bond, uptime and giving into one of five earned Player Card tiers (Copper to Platinum); tiers carry perk bits and give no auction priority. The founder tier (Curzi/Admas) acts only through a 3-of-5 steward threshold with a 7-day time lock, and sic_org_* runs organisation card programs (deposit, fee sweep, points award and redeem).
Main entry points: sic_view, sic_view_compute, sic_tier_table_default, sic_tier_eval, sic_steward_init/propose/approve/cancel/execute/state, sic_org_open/deposit/sweep/join/leave/award/redeem.
Tests: src/sicily/test_sicily.c, 315 passed, via test-econ in verify-all.
Used by: zxvd's player.card method (zxv_host.c rpc_player_card, tier evaluation only) and kernel/src/cred (cred.c calls sic_tier_eval). The steward threshold, the organisation programs and sic_view have no caller outside the tests.
Gaps: player.card feeds only credibility (from quest); bond and giving are reported as 0, so tiers above what credibility alone earns are not reachable today. Signatures go through the caller's verify hook. Described briefly in [ZXVD.md](ZXVD.md#peers-offers-and-the-player-card).

### situation — all-domain situation/risk modeller
Status: PARTIAL
What it does: Holds "field" records (amplitude and phase as double) for military, economic, logistics and geopolitical domains and combines them into a risk assessment using predictive and edp_risk.
Main entry points: situation_model.h.
Tests: none in src. tests/test_new_modules.c (root) covers it but is not built anywhere.
Used by: no includes outside the directory. arm64: yes.
Gaps: libgcc 64-bit division on x86 -m32 (values are surplus_real_t: Q32.32 in kernel images, double only in TEST_HOST builds). Its outputs are unvalidated model numbers.

### smap — S-Map reassembly manifest
Status: WORKING (tested in verify-all)
What it does: For each ingested payload, makes a root ID (plain SHA-256, honestly stated as not an IPFS CID) and an S-Map with per-chunk digests, offsets and shard indices. smap_verify checks that the map is self-consistent.
Main entry points: smap.h (smap_build, smap_verify, phase shift, lookup by root).
Tests: test_smap.c (verify-all).
Used by: recon. arm64: yes.
Gaps: None found.

### social — feed, groups, posts, buckets and desktop notifications
Status: WORKING (tested in verify-all)
What it does: Ranks the feed by what a post adds to what the viewer has already seen, explicitly without engagement metrics. It also handles groups/syndicates, posts signed with ML-DSA, storage buckets, and zx_notify, which builds safe notify-send argv (escaped, with no shell).
Main entry points: social_feed.h, social_group.h, social_post.h, social_store.h, social_bucket.h, social_sign_mldsa.h, zx_notify.h.
Tests: test_social_feed.c (112) and test_zx_notify.c (43), both in verify-all. The feed test includes its own decoder fuzzing.
Used by: quest; zx_notify is the desktop app's event bus (zxv_host.c, with arch/hosted/zx_notify_host.c as its OS bridge). Not in arm64 list.
Gaps: None found.

### social_spaces — named public forum spaces
Status: WORKING (tested in verify-all)
What it does: Lets people open, join and post in named spaces (social club, developer den, casual). Sorting must keep every post byte-for-byte intact.
Main entry points: social.h (space open/join/post/sort).
Tests: test_social.c, plus a q32 variant (verify-all, also clean under ASan/UBSan).
Used by: pirate_apps. arm64: yes.
Gaps: None found.

### sovereign_node — per-system sovereignty and recognition model
Status: WORKING (tested in verify-all)
What it does: Models every system as a sovereign node that declares its own capabilities and recognises others peer-to-peer, never ruling over them.
Main entry points: sovereign_node.h.
Tests: test_sovereign_node.c (97 assertions, verify-all).
Used by: interspace and others through tests. arm64: yes.
Gaps: None found.

### speech — speech session pipeline (DSP, VAD, captions)
Status: WORKING (tested in verify-all)
What it does: Resamples microphone PCM to 16 kHz, runs VAD with a pre-roll, and computes log-mel features in integer DSP. It then drives bound speech engines for dictation, captions and read-aloud. The tables are generated by the Python scripts in the directory.
Main entry points: speech_session.h, speech_dsp.h.
Tests: test_speech.c (101 checks against a fixture, verify-all).
Used by: xlate. Not in arm64 list.
Gaps: The speech engines themselves are external bindings.

### storage_fabric — "storage fabric" (unbuilt)
Status: UNUSED/DEAD
Location: kernel/experimental/storage_fabric/ (moved from kernel/src/). An unbuilt placeholder kept for the design record, not shipped; see kernel/experimental/README.md.
What it does: Meant to expose NVMe/network storage to apps with revenue accounting. The revenue numbers are placeholders.
Main entry points: storage_fabric.h.
Tests: none.
Used by: app_fabric and app_constellation include the header; no build compiles the .c (missing nvme.h and other headers). Not in arm64 list.
Gaps: An AUDIT STATUS banner was added. It should be removed or rebuilt on fs/ipfs_node.

### stream — swarm video streaming (every viewer also serves)
Status: WORKING (tested in verify-all)
What it does: Splits a stream into segments of erasure-coded freights (any k of 256 rows decode). It moves rows in pieces between viewers through advert/request/notice/piece messages, and includes a churn simulator.
Main entry points: stream_seg.h (stream_manifest_parse), stream_swarm.h (ssw_* message parse/build, scheduler), stream_sim.h.
Tests: test_stream.c (79 checks, verify-all). Its 5 wire decoders were fuzzed here for 20k iterations each with no crash.
Used by: linked by viewing/tests, and zxvd (`kernel/arch/hosted/zxvd_rpc_modules.c`, tested by `test_zxvd_rpc_modules.c` and `test_zxvd.py`): stream.plan (stream_vod_plan) and
stream.segment (a segment's CID and freight hashes). Not in arm64 list.
Gaps: The stream/* files carry a copyright line but no SPDX line.

### subterm — root terminal with many sub-terminals
Status: WORKING (tested in verify-all)
What it does: A root terminal under which commands open sub-terminals. Commands that need several sub-terminals all advance on the same tick. Indices are bounds-checked.
Main entry points: subterm.h.
Tests: test_subterm.c (88/88, verify-all).
Used by: no includes outside the directory. arm64: yes.
Gaps: None found.

### superpos — "superposition" build-space coordinator
Status: PARTIAL
What it does: Describes the space of build configurations (compiler flags, passes, targets) in "quantum-inspired" terms and tracks agreement between build tracks.
Main entry points: superpos.h (superpos_agreement_permille returns an integer, among others).
Tests: none.
Used by: no includes outside the directory. arm64: yes.
Gaps: The "Hilbert space" and quantum language describes ordinary bookkeeping. Untested.

### surplus — Interaction Surplus Framework maths
Status: WORKING (tested in verify-all)
What it does: Implements f(u) = ln(1 + (N-1)u) and the surplus dynamics in Q32.32 fixed point for the kernel, or double under TEST_HOST. It provides SR_* arithmetic macros used across the tree.
Main entry points: surplus.h (SR_MUL/DIV/FROM_INT, sr_ln, surplus_dynamics_step).
Tests: test_surplus_axioms.c (verify-all).
Used by: most of the tree (porter_house, predictive, sdk_bridge, battering_ram, alloc, apps, ...). arm64: yes.
Gaps: The freestanding path uses __int128 when available, against the brief's rule. SR_FROM_FLOAT uses a double at compile time (constants only; kernel images reject run-time FP). On x86 -m32 it uses libgcc 64-bit division. These are shared-header changes and were not made here.

### sutra — SUTRA transaction language (lexer, parser, runtime, rails)
Status: WORKING (tested in verify-all)
What it does: A small language for capital transactions: a lexer and parser, a runtime with paraconsistent states, capital coverage checks against the vino ledger, chiglet sentence matching, a self-audit pass and "rails" that format ISO 20022 / MT103 style text locally. It does not connect to any network.
Main entry points: sutra.h (sutra_parse, sutra_run, sutra_check_coverage, rails emit).
Tests: 7 tests (lexer, parser, e2e, capital, chiglet, rails, selfaudit), all in verify-all. 3 capital checks were added. sutra_parse was fuzzed here for 20k iterations with no crash.
Used by: orbital_compat_sutra, linked by rmag/sutra tests. arm64: yes (8 files; debug_vino.c correctly excluded).
Gaps: Fixed: coverage used doubles (wrongly approved 2^53 vs 2^53+1/2) and read out of bounds for a bad capital type. Not fixed: EMIT-SWIFT with an unknown subtype emits MT103 text. The output is a local text format only; it implies no SWIFT connectivity.

### svault — protocol solvency vault
Status: WORKING (tested in verify-all)
What it does: implements the owner's fee rule on pay_ledger. One third of the reserve share of the 0.08889% assurance fee is swept, with an exact carry, into a vault that buys V-Bills with collected fees only, up to one third of V-Bills outstanding; past the cap the money stays in the reserve. A sweep is refused if it would leave the reserve below the configured solvency floor. Vault V-Bills do not count toward the redemption floor and take a haircut first on a toxic-asset loss (svault_first_loss). Nothing accrues with time.
Main entry points: svault_init, svault_set_floor, svault_sweep, svault_sweep_split, svault_sweep_fee, svault_buy, svault_first_loss, svault_balances, svault_redemption_floor, svault_verify_solvency, svault_check.
Tests: src/svault/test_svault.c, 188 passed, via test-econ in verify-all. fuzz/econ/fuzz_econ_svault.c corpus replay and property run also run in verify-all (make -C fuzz replay prop).
Used by: zxvd's vault.* group (zxvd_econ.c: vault.deposit, buy, first_loss, status, check), stats.* through zstats, and kernel/src/qnote (qnote_first_loss calls svault_first_loss).
Gaps: the zxvd economy is one in-memory ledger that is not saved and is not the owner's books; nothing is connected to a bank or custodian ([ZXVD.md](ZXVD.md#economy-modules)). The header notes vault V-Bills are likely securities exposure in many jurisdictions.

### swarm — AI-model swarm economy
Status: WORKING (tested in verify-all)
What it does: Budgets, a reserve, a witnessed triple ledger, a market, governor and quality scoring for a swarm of models. A different model witnesses each allotment. It also has hk/enterprise packet formats that pack and unpack bytes.
Main entry points: swarm_budget.h, swarm_ledger.h, swarm_market.h, swarm_hk.h (swarm_hk_parse), swarm_enterprise.h (swarm_en_pack/unpack), swarm_governor.h, and others.
Tests: 6 tests plus a q32 economy variant (verify-all). swarm_hk_parse and en_pack/unpack were fuzzed here for 20k iterations each with no crash.
Used by: pay, provider, social, harmonic, i18n, tensor, vinea. Linked into the arm64 and x86_64 kernel images (all non-test swarm_*.c); `swarm_boot.c` runs one Fibonacci budget cycle at boot and `ai_boot_selfcheck` prints `[AI_OK] swarm+tensor selfcheck 0x90553e54` before `[BOOT_OK]` (halts with `[FAULT]` on mismatch); test_ai_selfcheck asserts the same constants on the host.
Gaps: in the kernel only the boot self-check calls it; no kernel task drives the swarm yet. riscv/arm32 images do not link it.

### synthesis — "synthesis engine" spec parser and DMA staging
Status: PARTIAL
What it does: Accepts a text spec of modules over a DMA-style write call, parses "module:name" entries, and scores them with surplus/edp_risk. The header's claim that it is "the engine that built the kernel" is not supported by anything in the tree.
Main entry points: synth_dma_write_spec, synth_parse_spec (synthesis_engine.h).
Tests: tests/host/test_fixed_point.c (verify-all) checks coverage, surplus and risk. tests/test_new_modules.c (root) is not built.
Used by: no includes outside the directory. arm64: yes.
Gaps: Fixed: a heap read past the end when a spec ends in "module:ab", and a missing NUL terminator. Also fixed: the double fields are Q16.16 integers. Not fixed: overclaiming documentation.

### syscall — the one shared ZXV syscall table and capability check
Status: WORKING (tested in verify-all)
What it does: one table maps each syscall number to its name and the capability it needs, for every architecture. It rejects unknown or "reserved" numbers with ZXV_ENOSYS, and a self-check catches table holes.
Main entry points: zxv_syscall_info, zxv_syscall_permit, zxv_syscall_count, zxv_abi_compatible, zxv_syscall_selfcheck.
Tests: src/syscall/test_syscall.c (22 checks) in verify-all.
Used by: arch/arm64/arm64_exceptions.c and el0_userspace.c, arch/arm, sdk/m5_api.h. Compiled into both kernels.
Gaps: the "reserved" hole mechanism only allows nr 0 (syscall.c:64), unlike its documentation. x86_64 still implements only part of the ABI (the header says so).

### taxhook — VAT, GST and sales-tax hooks (off by default)
Status: WORKING (tested in verify-all)
What it does: computes VAT, GST or US sales tax for an invoice from a signed rate table per country and region (rates in parts per million, date ranges, categories, half-up or half-even rounding, per line or per invoice, reverse charge). A table installs only with a valid signature from a key scoped to that country, inside its validity window, and with a version above the installed one. It starts disabled: taxhook_compute returns TAXHOOK_E_DISABLED until taxhook_enable. taxhook_sample.c ships three SAMPLE tables (GB VAT, DE VAT, US-NY sales tax) that are flagged so every receipt says SAMPLE; they are not tax advice.
Main entry points: taxhook_init, taxhook_enable, taxhook_encode, taxhook_decode, taxhook_install, taxhook_compute, taxhook_compute_table, taxhook_round_div, taxhook_receipt_text, taxhook_sample_gb_vat/de_vat/us_ny_sales.
Tests: src/taxhook/test_taxhook.c (98 checks) in kernel/mk/compliance.mk test-compliance, reached from verify-all through test-retail.
Used by: no caller outside its tests.
Gaps: not connected to zxvd's jurisdiction.get/set or any settings screen; no adviser-approved table exists; no credit notes, fractional quantities, excise or per-unit taxes, product-to-category mapping or filing; reverse charge is simplified. See [retail/COMPLIANCE_PLUGINS.md](retail/COMPLIANCE_PLUGINS.md).

### telemetry — "axiom matrix" self-observation counters
Status: WORKING (tested in verify-all)
What it does: projects a tick through the axiom matrix into a complex telemetry value, and resolves a choice from its magnitude bounded by a Fibonacci table.
Main entry points: emit_and_observe, choice_resolve_from_telemetry, fib_bound, run_telemetry_recursion_demo.
Tests: src/telemetry/test_telemetry.c, in TEST_SRCS (kernel/Makefile:157); `make test` is a prerequisite of verify-all (Makefile:237).
Used by: pirate_fleet, arch/arm64/kernel_main_arm64.c, arch/arm, arch/riscv.
Gaps: none for floating point: telemetry values are zxv_cq16_t and the magnitude is an integer square root (fixed).

### tensor — integer tensor engine, GGUF loader, tokenizer, RoPE, transformer model
Status: WORKING (tested in verify-all)
What it does: zt.c is the Q-format integer tensor core (matmul, softmax and so on, using zt_udiv64). zt_gguf parses GGUF model files (external input) with bounds checks. zt_tok is a BPE tokenizer with a Unicode table. zt_rope implements RoPE from integer tables. zt_model is a llama-style forward pass. zt_lattice, zt_coil, zt_isf and zt_holo are extra operators. zt_kern.h holds the forward pass's matrix-vector kernels as one portable C reference; hosted builds (app, bench, tests) can switch a model to zt_simd.h's AVX2, AVX-512 or NEON kernels plus a row-parallel pthread pool (`m.kern = zt_simd_default()`), which give the same bits. zt_simd*.c is hosted only: no kernel image or package links it.
Main entry points: zt.h (zt_matvec, zt_rmsnorm, zt_softmax, zt_udiv64...), zt_gguf_open/zt_gguf_find_tensor/zt_gguf_dequant, zt_tok_load/encode/decode, zt_rope_init/apply, zt_model_load/eval/generate.
Tests: test_zt, test_zt_gguf, test_zt_q5 (Q5_0/Q5_1, 87 checks, bit-exact against golden values from gen_q5_fixture.py, a pure-Python transcription of ggml's dequantize_row_q5_0/q5_1), test_zt_tok, test_zt_lattice, test_zt_rope, test_zt_model, test_zt_audit (41), test_zt_e8 (269) and test_zt_simd (every SIMD kernel set and the thread pool against the C reference on random and edge-case rows and on whole forward passes of the fixture models, bit for bit; also under ASan/UBSan, and NEON cross-compiled and run under qemu-aarch64), all in verify-all. Other fixtures come from gen_*.py using the reference gguf-py.
Used by: host tests, swarm (an include), and the arm64 and x86_64 kernel images (all non-test zt*.c). `zt_boot.c` is a known-answer self-test at boot (exact Q8 matvec, RMSNorm, RoPE, softmax, SiLU, exp; hash 0xfbc3385a, same on host and bare metal); it is not a trained model running in the kernel. Also packaged as the standalone static library packages/libzxv-tensor (built from these sources; smoke program in verify-all).
Gaps: CodeQL multiply-overflow items in test_zt_model.c:333-339 and test_zt_lattice.c:409 are fixed (widened). There is no fuzz test on the GGUF parser; it was reviewed by hand and looked bounds-checked. Speed: measured only with synthetic random weights (bench/tensor/make_synth_model.py) on a shared x86 sandbox, never on a real model or on Apple hardware; NEON is verified for correctness under qemu, not timed. No SDOT/I8MM or RISC-V vector path (the activations are 16-bit; see zt_simd_neon.c).

### terms — first-boot end-user agreement gate
Status: WORKING (tested in verify-all)
What it does: holds the logic every screen shares for showing the ZXV End-User Agreement at a person's first sign-in. It lays the embedded text out for reading (character cells or pixels with a font measure callback), tracks scroll, page, section jumps and screen-reader progress, and only allows "I agree" once the end has been shown. An acceptance is a per-seat, hash-chained record, and the log status says whether a seat has accepted the current version. The text is agreement/en.txt embedded by tools/terms/embed_terms.py with its SHA-256; a mismatch refuses to show or record the text.
Main entry points: terms_reader_init/_ex, terms_reader_resize, terms_scroll, terms_page, terms_goto_section, terms_reader_spoke, terms_view, terms_can_accept, terms_accept, terms_decline, terms_record_format/parse, terms_log_verify, terms_log_status.
Tests: src/terms/test_terms.c under ASan/UBSan, 446 passed, plus a freestanding x86_64 build check, via test-terms in verify-all. src/zui/test_zui_terms.c (132 checks, in verify-all through test_zui_toolkit.sh) tests the screen.
Used by: src/zui/zui_terms.c (the native screen). Nothing else calls it: no shell opens the screen and nothing writes the acceptance log on a device.
Gaps: English text only; the README recommends counsel review before the first public release. Login, lock and per-seat terms on screen on bare metal are missing ([BAREMETAL_OS_GAPS.md](BAREMETAL_OS_GAPS.md), row 13). Flow and clauses backed in code: [kernel/src/terms/README.md](../kernel/src/terms/README.md).

### theme — palette tokens that every drawable resolves through
Status: WORKING (tested in verify-all)
What it does: maps THEME_* tokens to RGB values, with overrides, so the UI can be recoloured from one place.
Main entry points: theme_init, theme_set, theme_get, theme_reset, theme_slot_name.
Tests: no test in src/theme. It is exercised by test_icon and test_browser.
Used by: desktop, browser, bootfeat, tolvovina (in the test link). Compiled into both kernels.
Gaps: no direct unit test.

### timer — legacy x86 PIT 8253/8254 tick timer
Status: PARTIAL
What it does: programs the PIT divisor for a tick rate and counts ticks from IRQ0. It also reports uptime in ms.
Main entry points: timer_init(hz), timer_interrupt_handler, timer_register_callback, timer_get_ticks, timer_get_uptime_ms.
Tests: none.
Used by: abstraction_layer/boot_modules.c. x86_64 kernel and legacy kernel/Makefile only (arm uses arch/arm*/timer).
Gaps: timer_init(0) divided by zero (FIXED: it now defaults to TIMER_DEFAULT_HZ). timer_get_uptime_ms does a 64-bit division (timer.c:60), which needs libgcc on i386. No test.

### timesvc — wall-clock time (RTC, SNTP) and time zones
Status: WORKING (tested in verify-all)
What it does: gives a device a trustworthy wall-clock time so TLS can check certificate dates. It takes the battery-backed RTC reading and SNTP samples from several servers, needs at least 2 agreeing servers that are a strict majority, never goes below the build-time floor and refuses a time more than 100 years past it. tz converts UTC to local time from a built-in table of 497 IANA zones (POSIX rules, tzdata 2026c), from TZif files, or from POSIX TZ strings. Freestanding, integer only, no 64-bit division.
Main entry points: timesvc_init, timesvc_set_min_agree, timesvc_set_rtc, timesvc_add_sample, timesvc_commit, timesvc_now_us, timesvc_now_s; sntp_build_request, sntp_parse_reply; tz_posix_parse/lookup, tz_tzif_parse/lookup, tz_civil.
Tests: test_timesvc (55 checks) and test_tz (27,462 checks, every built-in zone against Python zoneinfo for 2027 to 2046), both again under ASan/UBSan, plus freestanding builds for aarch64, i386, armv7a and rv32, via test-timesvc in verify-all. fuzz/parsers/fuzz_time.c is in the fuzz replay set.
Used by: compiled into the arm64, x86_64, arm32, riscv and riscv32 kernel images (build_system/Makefile.*), but no code calls it there or anywhere else outside its tests and fuzzer.
Gaps: SNTP is unauthenticated (Network Time Security, RFC 8915, not done). Built-in zones are exact only from 2027 on; Asia/Gaza and Asia/Hebron can be an hour off without a TZif file. On bare metal the RTC and SNTP path that lets TLS verify is missing ([BAREMETAL_OS_GAPS.md](BAREMETAL_OS_GAPS.md), row 23). See [TIME.md](TIME.md).

### tls — TLS 1.3 client pieces: ChaCha20-Poly1305, HKDF, X25519, record layer, handshake
Status: PARTIAL
What it does: aead.c implements RFC 8439 AEAD and the record nonce. hkdf.c implements the HKDF and TLS 1.3 key schedule over SHA-256. x25519.c implements RFC 7748. record.c frames and protects records. handshake.c is a client state machine for TLS_CHACHA20_POLY1305_SHA256 with X25519.
Main entry points: aead_seal/aead_open, hkdf_extract/expand + tls13_* key schedule, x25519_public/x25519_shared, tls_record_read/write, tls_client_init/hello/feed/send/read/is_authenticated.
Tests: test_aead, test_hkdf, test_record and test_x25519 are in verify-all (RFC vectors). test_handshake.c is NEW (12 checks) and runs against an in-test server built from the same primitives.
Used by: nothing calls the handshake; security_fabric.c:531 has the call commented out. All files are compiled into both kernels.
Gaps: by design it does no certificate verification, so it is unauthenticated (the header warns about this). The handshake test only shows self-consistency, not interop with a real server. Three handshake gaps were fixed: no session-id echo check, no compression check, and handshake messages could span the key change. ChangeCipherSpec and warning alerts are tolerated after the handshake.

### tolvovina — TOL VOVINA integer 3D geometry, rasteriser, stereo and ROM
Status: WORKING (has a test, not in verify-all)
What it does: provides Q32.32 integer geometry (tvl_geom), a triangle rasteriser with perspective-correct gradients (tvl_raster), stereo pair generation (tvl_stereo) and a ROM image index (tvl_rom). tvl_bringup runs its four self-checks at boot.
Main entry points: tvl_bringup, tvl_geom (tvl_frame_*, tvl_rot*, tvl_orient), tvl_raster/tvl_stereo/tvl_rom APIs.
Tests: test_tvl_host.c is NEW; it requires 4/4 bring-up checks and passes clean under UBSan.
Used by: bootfeat/boot_features.c, abstraction_layer/boot_modules.c, modbind gate. Compiled into both kernels.
Gaps: left shifts of negative int64 values in tvl_raster were UB (FIXED). The test link needs -Wno-misleading-indentation because of warnings in out-of-scope emu/dimfold.c.

### tripartite_fs — index of positive/negative/neutral tripartite files
Status: WORKING (tested in verify-all)
What it does: an in-memory index of files with the .36n9 and .9n63 family of extensions, plus their pairs, datasets, containers and renderers, each keyed by a content id.
Main entry points: tf_init, tf_create_file, tf_create_pair, tf_create_dataset, tf_compute_cid, tf_get_file_by_cid, tf_enforce_policy.
Tests: test_tripartite_fs in verify-all.
Used by: host tests only; not linked into either kernel.
Gaps: no on-disk persistence (it is an index only).

### trispace — the Tri-Space artifact triad (S+, S-, S0) and its five hard requirements
Status: WORKING (tested in verify-all)
What it does: binds each module's positive, negative and neutral artifacts by digest, and enforces the five requirements (binding, declared caps, and so on) over metadata.
Main entry points: tri_init, tri_set_member, tri_bind, tri_verify, tri_may_release.
Tests: test_trispace in verify-all.
Used by: zxpkg, zxvfs_tri, loader/zsp.c, boot_modules. Compiled into both kernels.
Gaps: requirement 2 (capabilities) relies on zab, which treats a non-ZAB artifact as data with empty caps (see the zab finding).

### ubh — UBH-168 framed envelope and multi-format registry
Status: WORKING (tested in verify-all)
What it does: a canonical 168-bit envelope with 8-, 7- and 6-bit views, plus a registry of payload formats. UBH-7 is an analysis-only profile.
Main entry points: ubh_168_header_init/validate/pack/unpack, ubh_octets_to_septets, ubh_registry_init/register/find.
Tests: none in src/ubh. ../tests/host/test_ubh.c is in verify-all (kernel/Makefile:588).
Used by: call (RTP), ipfs_node. arm64 kernel only.
Gaps: no test in its own directory.

### update — signed software updates: package index, consent, fetch and verify, upcheck manifests
Status: WORKING (tested in verify-all)
What it does: update.c is the Ed25519-signed package index. It handles publish, user opt-in (select), fetch, and verify-by-CID. zx_upcheck, zx_upmanifest and zx_upcheck_mldsa handle ML-DSA-65 signed release manifests with version monotonicity. zx_ipns resolves names.
Main entry points: upd_trust_author, upd_publish, upd_select, upd_resolve, upd_fetch_verify; zx_upcheck/zx_upmanifest manifest check.
Tests: test_update (extended in this audit) and test_zx_upcheck, both in verify-all.
Used by: evolve (evo_upd, evo_zxu), bootfeat, boot_modules. update.c is in both kernels.
Gaps: none in signing: update.c verifies the signature over upd_manifest_digest (canonical id, version, arch, deps, cid; src/provenance/zx_provenance.h) and refuses a re-publish with a version <= the known one; zx_to_catalog attests the same digest. Fixed earlier: consent inheritance on re-publish, and trusting the transport's length. Per the coordinator, the snprintf CodeQL items in test_zx_upcheck.c are handled upstream.

### vauction — V-Bill issuance by descending uniform-price auction
Status: WORKING (tested in verify-all)
What it does: issues V-Bills in Dutch auction epochs with one clearing price, "backed only": each epoch issues only what keeps the reserve at or above its floor, under a per-epoch cap. The reserve counts the reserve account plus custodian-signed warehouse receipts valued at the lower median of fresh signed oracle quotes, less a per-custodian haircut; a disputed receipt is quarantined for good. Bids lock their money by ledger transfer; auction money goes to the reserve and refunds go back to bidders. The bid has no tier field, so Player Card tiers give no priority.
Main entry points: vauc_init, vauc_set_config, vauc_custodian_set, vauc_oracle_add, vauc_feed_add, vauc_quote, vauc_receipt_add, vauc_receipt_dispute, vauc_reserve, vauc_floor_ok, vauc_open, vauc_bid, vauc_clear, vauc_settle, vauc_cancel.
Tests: src/vauction/test_vauction.c, 353 passed, via test-econ in verify-all; fuzz/econ/fuzz_econ_vauction.c replay and property run in verify-all.
Used by: zxvd's auction.* group (zxvd_econ.c: auction.configure, open, bid, run, cancel, quote, status, check) and zstats.
Gaps: in zxvd the verify hook refuses every oracle quote and warehouse receipt, so the reserve is the reserve account alone; the economy is in memory only ([ZXVD.md](ZXVD.md#economy-modules)). The header notes V-Bills are likely securities in many jurisdictions.

### vbe — VBE/VGA linear framebuffer driver (legacy x86)
Status: PARTIAL
What it does: sets a VBE mode from a multiboot-provided framebuffer and does pixel, rect and blit drawing into the LFB.
Main entry points: vbe_init, vbe_init_fb, vbe_set_pixel, vbe_fill_rect, vbe_draw_line, vbe_draw_string.
Tests: none.
Used by: desktop/zxv_shell.c, desktop/render_boot.c, pterm. Compiled into both kernels.
Gaps: no test. It depends on firmware-provided mode info.

### vblock — content-addressed block store over memory tiers (RAM, pinned, VRAM, unified)
Status: WORKING (tested in verify-all)
What it does: an IPFS-compatible block store keyed by real CIDv1. It places copies across memory tiers and moves bytes through a CPU backend or a simulated device backend.
Main entry points: vb_init, vb_put, vb_acquire/vb_release, vb_pin/unpin, vb_route; backends vblock_cpu.c / vblock_sim.c.
Tests: test_vblock in verify-all.
Used by: host tests only.
Gaps: there is no real GPU backend; vblock_sim stands in for VRAM.

### vbond — V-Bonds: fixed-term locked V-Bill lots
Status: WORKING (tested in verify-all)
What it does: a V-Bond locks a lot of V-Bills for a Fibonacci term (default 233, 1,597 or 10,946 ticks) and pays only a share of realised revenue actually in the bond pool; there is no rate field and nothing grows with time. A bond can be handed to another holder by a signed bilateral transfer without releasing the V-Bills early. Issuing, transferring and crediting the pool are off until a jurisdiction manifest signed by the gateway key turns them on; redemption at maturity and distribution of what is already in the pool always work.
Main entry points: vbond_cfg_default, vbond_init, vbond_enable, vbond_issue, vbond_transfer, vbond_redeem, vbond_premium, vbond_premium_credit, vbond_pool_credit, vbond_distribute, vbond_check.
Tests: src/vbond/test_vbond.c, 183 passed, via test-econ in verify-all; fuzz/econ/fuzz_econ_vbond.c replay and property run in verify-all. kernel/arch/hosted/test_zxvd_instruments.sh runs in CI through zxvd_checks.sh.
Used by: zxvd's bonds.* group (zxvd_instruments.c: bonds.setup, switch, issue, transfer, redeem, distribute, fund, credit, status and others) on its own in-memory sandbox ledger, and zstats.
Gaps: zxvd's state is in memory only and its ticks are unix seconds, so a 233-tick bond matures 233 seconds after issue. The header notes this is likely a regulated instrument. See [ZXVD.md](ZXVD.md#bonds-derivatives-grants-and-estates).

### vderiv — V-Calls and V-Perps on auction clearing velocity
Status: WORKING (tested in verify-all)
What it does: exact-rational derivatives whose index is the number of V-Bills issued at each auction epoch, pushed in order with no gaps. A V-Call is a capped call, fully funded at open; a V-Perp is a bilateral perpetual position, fully collateralised with its maximum loss up front. There is no margin, liquidation, borrowing or funding rate, and no order book. The whole module is off until a signed jurisdiction manifest enables it; closing and settling always work.
Main entry points: vderiv_cfg_default, vderiv_init, vderiv_enable, vderiv_index_push, vderiv_index_at, vderiv_call_payoff, vderiv_perp_variation, vderiv_call_open, vderiv_call_settle, vderiv_perp_open, vderiv_perp_close, vderiv_check.
Tests: src/vderiv/test_vderiv.c, 208 passed, via test-econ in verify-all; fuzz/econ/fuzz_econ_vderiv.c replay and property run in verify-all; test_zxvd_instruments.sh in CI.
Used by: zxvd's derivatives.* group (zxvd_instruments.c: derivatives.setup, switch, push, call_open, call_settle, perp_open, perp_close, fund, status and others) on its own sandbox ledger. The index is pushed by the caller (derivatives.push), not read from the auction module.
Gaps: in-memory state only in zxvd; the header says derivatives are the most heavily regulated class and the module is never enabled by default. See [ZXVD.md](ZXVD.md#bonds-derivatives-grants-and-estates).

### vena — contract and app runtime on top of the vino ledger
Status: PARTIAL
What it does: registers contracts, apps and oracles. Contract execution runs ZAB bytecode under derived and granted capabilities, scoped to the contract itself. Non-ZAB code returns -3 ("not executed").
Main entry points: vena_init, vena_register_contract, vena_execute_contract, vena_load_app, vena_start_app, vena_register_oracle.
Tests: none.
Used by: vena_init is called from arch/arm64 and arch/arm kernel_main, identity_fabric, financial_fabric and boot_modules. vena_register_contract and vena_execute_contract have no callers. Compiled into both kernels.
Gaps: contract code is stored as a C string (char code[1024]). Real ZAB contains 0x00 bytes, so it is truncated and the ZAB path is effectively unreachable. Unbounded string copies were FIXED. No test.

### vfs — mount table over FAT32
Status: PARTIAL
What it does: a mount table and open-file table that look files up in a mounted FAT32 instance. It also records an LPRES "presence" attestation per path.
Main entry points: vfs_init, vfs_mount, vfs_open, vfs_list_dir, vfs_chdir, vfs_stat.
Tests: none.
Used by: storage_fabric, apps (vfs_stub, notes_app), arch/arm64 and arch/arm kernel_main.
Gaps: vfs_read and vfs_write are stubs that return 0 bytes. vfs_mkdir returns -1. vfs_close does not free the slot (num_open never decreases). vfs_stat returns size 0. The unbounded path copy into name[32] was FIXED.

### vhub — V-Bill hub demo: fiat A to V-Bill to fiat B with signed exact pegs and rule plug-ins
Status: WORKING (tested in verify-all, under ASan+UBSan). A demo, not a deployment.
What it does: each registered currency has an issuer, a hub pool account and a V-Bill float account on one pay_ledger. An issuer sets its peg as an exact fraction (num/den V-Bills per unit) by signing "zxv/vhub-peg/v1" || ccy || num || den || epoch; the hub checks the signature through a caller-supplied verifier and only accepts a newer epoch. A swap converts amount -> V-Bills -> output with integer arithmetic and refuses any amount that does not convert exactly (vhub_quote gives the smallest exact step), so no rounding is ever posted. Both currencies' rule plug-ins judge the swap as lpres_hex tags; the worst wins (forbidden > refused > escalate > wait), a forbidden swap is written to the quarantine log and the hub halts when that log is full. An allowed swap is one atomic six-line pay_ledger_post (sender, pool A, float A, float B, pool B, receiver), so a ledger refusal (funds, freeze, bounds) moves nothing. vhub_rule_limit is a sample capital-control plug-in: a per-sender outflow cap per epoch. An exact peg removes rounding only; it does not remove FX risk and holds only while the issuer backs it.
Main entry points: vhub_init, vhub_add_ccy, vhub_peg_msg, vhub_set_peg, vhub_add_rule, vhub_quote, vhub_swap; vhub_rule_limit, vhub_limit_commit.
Tests: vhub/test_vhub.c (`make test-vhub`) — 101 checks pass: three test currencies, a round trip, a triangular cross rate, a brute-force check of vhub_quote (19649 cases), a stale or forged peg, the limit plug-in, ledger backing, every rule outcome, quarantine and halt; mutation 94.9%, in the CI gate.
Used by: nothing outside tests yet. Hosted only.
Gaps: no real issuer keys (the test verifier is a hash mock; ML-DSA plugs into the same verify hook); no UI; V-Bills are likely securities, so any real use needs legal review.

### video — software 2D rasteriser into a caller framebuffer, plus ramfb scanout
Status: WORKING (tested in verify-all)
What it does: integer 2D drawing (lines, rects, glyphs, blit) with optional double buffering, a vsync and scanout ops struct, and the QEMU ramfb fw_cfg driver.
Main entry points: video_init, video_set_mode, video_set_framebuffer, video_fill_rect/draw_line/put_pixel, video_flip, video_vsync_wait, ramfb_init.
Tests: test_video in verify-all.
Used by: desktop. video.c is in the arm64 kernel only.
Gaps: none for floating point (coverage is Q16.16, fixed).

### vinea — Vinea Kademlia DHT node, sessions, file exchange and economy
Status: WORKING (tested in verify-all)
What it does: a pure-state-machine DHT node with signed and verified messages (ML-DSA-87 identities, protocol v3), a ping-before-evict Kademlia table, PQ sessions (X25519 + ML-KEM-1024 hybrid), CIDs, chunked file transfer, LAN discovery, schema validation, and a resource "economy" ledger (vna_econ).
Main entry points: vna_node_init/handle/lookup/publish/announce/poll_event, vna_agree_*, vna_cid_*, vna_session_*, vna_file_*, vna_econ_*.
Tests: test_vinea in verify-all.
Used by: host tests and the desktop app (arch/hosted/zxv_net_host.c: one node on UDP, off by default). Not in either kernel.
Gaps: the vna_econ.c:333/342/363 counters add without saturation (LOW). The wire parsing was reviewed and is length-checked.

### vino — in-kernel three-ledger (primary, audit, hash chain) bank node
Status: WORKING (has a test, not in verify-all)
What it does: accounts with per-capital-form balances, transfers recorded in a primary ledger plus an audit copy plus a SHA-256 hash chain (versioned, VINO_CHAIN_V2_SHA256), asset issuance, asset registry, peers, and labelled trade and bridge calls.
Main entry points: vino_init, vino_create_account, vino_transfer, vino_issue, vino_trade, vino_bridge, vino_register_asset, vino_add_peer, vino_msg_from_iso20022/mt103/camt053/btc/eth (format adapters).
Tests: test_vino.c is NEW (21 checks; passes under ASan). fuzz/econ/fuzz_econ_vino checks conservation after every op (found an out-of-range capital read, RMAG quota overflow and a wrapping volume counter; fixed). It is also exercised indirectly by the sutra and community_chest tests.
Used by: vena, financial_fabric and others. Compiled into both kernels.
Gaps: vino_trade ignores asset and price, and vino_bridge ignores its rails. Fixed: unbounded copies, unchecked capital index and overflow, unrecorded minting, duplicate accounts, the FNV chain (now SHA-256 with a tamper test), and the capital order (now the canonical zcap_form_t).

### vino_stores — Vino floating-voucher settlement engine
Status: WORKING (tested in verify-all)
What it does: extends finance/triple_ledger floating vouchers with the invariant that a unit is active on exactly one rail (bearer note or ledger ghost). Every value event posts through triple_ledger_post (Debit:Credit:Equity).
Main entry points: vino_stores_init, vino_mint, vino_ledger_act, vino_swap_to_physical/digital, vino_single_active_state_ok, vino_can_spend.
Tests: test_vino_stores in verify-all.
Used by: battering_ram, pirate_fleet. Compiled into both kernels.
Gaps: none found. It was reviewed and the conservation checks hold.

### virtio — virtio-MMIO bus, split virtqueues and virtio-gpu
Status: WORKING (tested in verify-all)
What it does: vring.c implements split-virtqueue descriptor, avail and used ring handling with bounds checks. virtio_bus.c probes MMIO slots, classifies device IDs and matches registered drivers. virtio_gpu.c is the 2D scanout driver.
Main entry points: vring.h ring ops, virtio_register_driver, virtio_bus_probe/poll, virtio_classify_slot, virtio_gpu_probe/state.
Tests: test_vring is in verify-all. test_virtio_bus (not in verify-all) is now fixed so it links under -DHOST_TEST; its recipe was added.
Used by: vring and virtio_bus are in both kernels; virtio_gpu is arm64 only.
Gaps: virtio_gpu has no host test.

### voice — deterministic text-to-phoneme bridge with fail-closed model boundaries
Status: WORKING (tested in verify-all)
What it does: grapheme-to-phoneme decomposition that is deterministic across targets. TTS and STT model boundaries return "unavailable" instead of faking output.
Main entry points: voice_init, voice_text_to_phonemes, voice_speak/voice_listen (fail-closed without a backend), voice_set_tts/stt.
Tests: test_voice in verify-all.
Used by: speech_session, desktop/zxv_shell, wyverneye. Compiled into both kernels.
Gaps: no real speech engine (stated honestly in the header).

### vswap — Player Card swap: Vino to V-Bills, lock tiers, dividend
Status: WORKING (tested in verify-all)
What it does: lets a holder swap idle Vino into V-Bills at the exact reserve ratio R/B (or par when either is zero), and back out at R/B. Positions can sit in deeper lock tiers for a larger dividend weight; the dividend is a share of realised fee surplus actually in the dividend pool, never a fixed rate. Swapping in, locking and the dividend are off until a signed jurisdiction manifest enables them; swap-out, exit notice and unlock always work. Vault-held V-Bills are excluded from the redemption base.
Main entry points: vswap_cfg_default, vswap_init, vswap_enable, vswap_rate, vswap_quote_in, vswap_quote_out, vswap_swap_in, vswap_swap_out, vswap_lock, vswap_notice, vswap_unlock, vswap_dividend, vswap_epoch_check.
Tests: src/vswap/test_vswap.c, 196 passed, via test-econ in verify-all; fuzz/econ/fuzz_econ_vswap.c replay and property run in verify-all.
Used by: zxvd's swap.* group (zxvd_econ.c: swap.deposit, withdraw, enable, in, out, lock, unlock, notice, dividend, quote, status, check) and zstats.
Gaps: in zxvd, swap.enable is signed by zxvd itself with a stand-in hook (SHA-256 over a fixed tag), which proves only that zxvd made the call; the economy is in memory only and not connected to any bank ([ZXVD.md](ZXVD.md#economy-modules)). The header notes a redeemable revenue-share instrument is likely regulated.

### web4 — Web2, Web3 and agent bridges: HTTP, JSON, JWT, OAuth, secp256k1, ETH/BTC, CID
Status: WORKING (tested in verify-all)
What it does: parsers and encoders for HTTP/1.1, JSON, RLP, JWT (alg pinned; "none" rejected), OAuth flows, secp256k1 ECDSA (RFC 6979, complete formulas), Ethereum and Bitcoin address and transaction encoding, CIDs, and ML-DSA-65 signed agent messaging (web4_agent).
Main entry points: w4_http_parse_request/response, w4_json_get*, JWT/OAuth (web4_jwt.c, web4_oauth.c), secp256k1 sign/verify, w4_btc_p2wpkh, w4_abi_erc20_*, w4_cid_of, w4_agent_create/w4_sign/w4_verify.
Tests: test_web4_web2, test_web4_web3 and test_web4_agent, all in verify-all, with vectors from gen_web4_vectors.py.
Used by: zxvd (web4_json parses every request) and zxvd (`kernel/arch/hosted/zxvd_rpc_modules.c`, tested by `test_zxvd_rpc_modules.c` and `test_zxvd.py`): web4.cid and
web4.agent_card / web4.card_verify (self-bound ML-DSA-65 cards).
Gaps: no network transport. It only builds and parses bytes, and makes no claim of live chain connectivity.

### wifi — IEEE 802.11 station/AP subsystem (frames, WPA handshake, scan)
Status: WORKING (tested in verify-all)
What it does: 802.11 frame building and parsing, a scan and BSS table, association, and a 4-way handshake/key-derivation state machine over a driver ops struct.
Main entry points: wifi_init, wifi_bind_ops, wifi_scan, wifi_connect, wifi_start_ap, wifi_set_channel.
Tests: test_wifi in verify-all.
Used by: arm64 kernel only.
Gaps: there is no real radio driver behind the ops (the double coverage is Q16.16, fixed).

### wyrmgate — six-fold Tri-Space judgment gate for state-changing events
Status: WORKING (tested in verify-all)
What it does: composes OSEQ (causal order), LPRES, RMAG, surplus and others to give each event an S+ commit, S- reject or S0 defer verdict, with a reason code.
Main entry points: wyrm_judge, wyrm_verdict_name, wyrm_reason_name.
Tests: test_wyrmgate in verify-all.
Used by: wyverneye, desktop/zxv_shell, chronicle, arch/arm64 kernel_main. Compiled into both kernels.
Gaps: none found.

### wyverneye — intent forged into a sigil card, then judged by Wyrmgate before acting
Status: WORKING (tested in verify-all)
What it does: turns an intent string into a refinery sigil card, then passes the proposed action through Wyrmgate. Forging and acting are kept separate.
Main entry points: wyvern_read.
Tests: test_wyverneye in verify-all.
Used by: test only (no kernel caller found). Compiled into both kernels.
Gaps: no runtime caller.

### xedit — X-EDIT gap-buffer text and code editor core
Status: WORKING (has a test, not in verify-all)
What it does: per-buffer gap buffers with insert and delete, cursor and line motion, a line table, filetype detection, modes, and a content hash used for integrity.
Main entry points: xedit_init, xedit_buffer_create, xedit_insert, xedit_delete_back/forward, xedit_get_text/get_line, xedit_cursor_*, xedit_update_hash, xedit_verify_integrity.
Tests: test_xedit.c is NEW (13 checks, ASan/UBSan clean). ../tests/test_new_modules.c also uses it but is not built by any Makefile.
Used by: compiled into both kernels.
Gaps: xedit_undo and xedit_redo are stubs that report success without undoing anything (xedit.c:443). The "CID/Merkle root" is FNV-1a, not a CID. Fixed: the unbounded filename copy, gap-relative line starts, get_line reading the raw buffer, the get_text NUL written past max_len, and the 64 KB stack buffer in update_hash.

### xlate — language identification and translation prompt construction
Status: WORKING (tested in verify-all)
What it does: xlate_langid is an integer n-gram language identifier using an embedded model generated by gen_langid_model.py. xlate.c and xlate_prompts.c build translation requests for an external model boundary.
Main entry points: xlate_langid detection API (xlate_langid.h), xlate_init, xlate_set_backend, xlate_translate, xlate_build_prompt, xlate_prefs_*.
Tests: test_xlate in verify-all.
Used by: speech_session (an include). Host tests only.
Gaps: the out-of-bounds read of cps[n] at xlate_langid.c:263 was FIXED (UBSan found it).

### xrisk — exact loss distributions for discrete portfolios
Status: WORKING (tested in verify-all)
What it does: computes the whole loss distribution of up to 256 independent exposures exactly, by dynamic programming over loss units with fixed-width big integers (4096 bits by default), so every probability is an exact fraction with one shared denominator. From it come P(L = k), the CDF, tails, expected loss, VaR and expected shortfall, all exact. Correlation is a finite mixture of up to 16 weighted scenarios. Several probability sources per exposure can be fused with the four lpres values, and a conflict is shown rather than averaged. Inputs that would overflow are refused, never rounded.
Main entry points: xrisk_model_init, xrisk_model_set_loss/weight/p/sources, xrisk_model_conflicts, xrisk_fuse, xrisk_compute, xrisk_pmf, xrisk_cdf, xrisk_tail, xrisk_expected_loss, xrisk_var, xrisk_es, xrisk_rat_*.
Tests: src/xrisk/test_xrisk.c, 28,670 checks, built twice (normal and XRISK_SOFT_DIV), plus a bench_xrisk build and a freestanding aarch64/i386 check, via test-research in verify-all.
Used by: no caller outside its tests and bench_xrisk.c.
Gaps: discrete models only (continuous severities must be discretised by the caller); capped at 256 exposures, 4096 loss units and 16 scenarios; an xrisk_dist_t is about 4.3 MB. Not built yet: a report generator, per-exposure VaR/ES contributions, more scenarios. See [EXACT_RISK.md](EXACT_RISK.md).

### yantra — software-defined hardware fabric capability registry
Status: WORKING (tested in verify-all)
What it does: tracks hardware capabilities through maturity states, from MODEL_ONLY up to CERTIFIED. It refuses to report a state beyond what has been recorded.
Main entry points: yf_init, yf_register_device, yf_set_capability, yf_device_set_state, yf_twin_assert, yf_twin_enter_safe_state.
Tests: none in src/yantra. ../tests/host/test_yantra.c is in verify-all (kernel/Makefile:554).
Used by: net/smart_adapter_integration, boot_modules, dharma/tantra. Compiled into both kernels.
Gaps: the "QUALIFIED/CERTIFIED" states are labels only; nothing in the tree certifies hardware.

### zab — ZXV Artifact Bytecode: capability verifier, VM and scopes
Status: WORKING (tested in verify-all)
What it does: zab.c decodes bytecode and derives the capabilities a program actually uses. zab_exec.c runs it with granted ∩ derived capabilities and per-object scopes, and calls only the host handlers that are present.
Main entry points: zab_derive_capabilities, zab_artifact_capabilities, zab_compute_seal, zab_execute, zab_scope_set/permits.
Tests: test_zab (17), test_zab_exec (19) and test_zab_scope (43), all in verify-all.
Used by: vena, trispace and zxpkg (via the caps requirement), sdk. Compiled into both kernels.
Gaps: a non-ZAB artifact is "data" with empty caps, but native S+ images are not ZAB. So "declared caps = derived caps" is not actually checked for native code (MED, NOT FIXED).

### zcapital — capital-form market: offers, seeks and matching
Status: WORKING (tested in verify-all)
What it does: per-capital-form order books of offers and seeks, and a FIFO matcher that commits fills.
Main entry points: zmarket_init, zmarket_offer, zmarket_seek, zmarket_match.
Tests: test_zcapital (plain and q32 variants) in verify-all. Anchors 8-10 were added.
Used by: ministry, bootfeat, boot_modules, pay, cardnet and others (header users). Compiled into both kernels.
Gaps: the over-fill, wash-pair blocking and book exhaustion bugs are FIXED. There is no partial-order cancellation API.

### zenny — Zenny chips: user-built baskets with an exact manifest
Status: WORKING (tested in verify-all)
What it does: a Zenny chip is a basket with a mandatory V-Bill leg plus weighted fiat, commodity and compute voucher legs, recorded in a hash-chained (WORM) manifest. The chip's claim must equal the deposited value exactly at mint and at every rebalance; leverage and credit claims are refused, commodity legs need a custodian attestation, and bilateral chips need both minters' signatures. Partial decompression (tap-to-pay) pays exact amounts per leg with the 0.08889% fee and writes a disposal receipt. Posting the leg movements on a ledger is the caller's job. A credit hook adapts an allotment's basket-value payout to a top-up.
Main entry points: zenny_init, zenny_mint, zenny_rebalance, zenny_decompress, zenny_topup, zenny_attest, zenny_verify_chain, zenny_check, zenny_find, zenny_receipt_get/ack, zenny_mint_digest.
Tests: src/zenny/test_zenny.c, 236 passed, via test-econ in verify-all; fuzz/econ/fuzz_econ_zenny.c replay and property run in verify-all.
Used by: zxvd's zenny.* group (zxvd_econ.c: zenny.mint, decompress, quote, receipts, status, check) and zstats.
Gaps: in zxvd, zenny.mint is signed with zxvd's stand-in hook, and co-minter signatures and custodian attestations never verify, so bilateral chips and commodity legs are refused there. "Zenny" has had no trademark check. The header says a chip's legal class needs review. See [kernel/src/zenny/README.md](../kernel/src/zenny/README.md) and [ZXVD.md](ZXVD.md#economy-modules).

### zstats — public statistics feed over the economy modules
Status: WORKING (tested in verify-all)
What it does: builds per-epoch snapshots of aggregate economy figures only (no per-account data): reserve against Vino claims and the floor, vault holdings against V-Bills outstanding, lock tiers, the auction, an allotment section, and the grants barrel figures with G_rate. Snapshots have a fixed binary encoding, a digest, a signature message and a hash chain, and can be printed as JSON. It reads the other modules; it changes nothing.
Main entry points: zstats_sources_default, zstats_collect, zstats_check, zstats_encode, zstats_decode, zstats_digest, zstats_sign_msg, zstats_chain_init, zstats_verify, zstats_json.
Tests: src/zstats/test_zstats.c, 411 passed (linked with grants, svault, vswap, vauction, vbond and zenny), via test-econ in verify-all.
Used by: zxvd's stats.* group (zxvd_econ.c: stats.snapshot, stats.status); kernel/arch/hosted/test_zxvd_econ.c covers it in CI.
Gaps: the allotment section is a plain input the caller fills (that module is not on this branch); in zxvd the snapshots are hash-chained but not signed, and the figures are only as good as the in-memory ledger behind them.

### ztp — zero-touch provisioning and owner claim (ZHP-1 profile)
Status: WORKING (tested in verify-all)
What it does: lets a device get an owner without a cloud account. On first boot the device makes an ML-DSA-87 key pair and a one-time claim secret; the maker signs a ZHP-1 provisioning profile with a commitment to that secret; the device or box shows a QR code ("ZTP1:" plus base32 of device id and secret). The first valid claim (MAC'd with the secret and signed by the new owner) wins and wipes the secret. Transfer and factory reset need the current owner's signature; every message carries an ownership epoch so old messages cannot be replayed. A physical-presence reset exists but is compiled out by default.
Main entry points: ztp_device_init, ztp_first_boot, ztp_device_id, ztp_qr_text, ztp_profile_build, ztp_profile_verify, ztp_qr_parse, ztp_qr_check, ztp_msg_build, ztp_device_accept, ztp_reset_physical, ztp_commit.
Tests: src/ztp/test_ztp.c (51 checks) and the ZTP_PHYSICAL_RESET=1 build (52 checks) under ASan/UBSan, plus freestanding aarch64/i386 checks, in kernel/mk/rot.mk test-rot, reached from verify-all through test-retail.
Used by: no caller outside its tests.
Gaps: not wired into the boot path or any owner app; no hardware root of trust yet (the software signer keeps keys in ordinary memory); not tested on real hardware; whoever scans the QR code first can claim the device. See [retail/ROOT_OF_TRUST.md](retail/ROOT_OF_TRUST.md).

### zui — native UI toolkit: renderer, widgets, windows and system screens
Status: WORKING (tested in verify-all)
What it does: the native user interface in integer C with no allocation, meant to run the same hosted and on bare metal. About 44 source files and 8 app screens. Parts:
- Drawing: zui_canvas (surface, shapes, text through src/font/truetype), zui_layout (responsive pane plans), zui_vec (vector renderer and the ZXVG format), zui_image (PNG), zui_tokens and zui_palette (design tokens, three switchable colour schemes), zui_brand (system marks as ZXVG), zui_depth and zui_pxl (a depth effect and a paraconsistent per-pixel depth filter), zui_logic (paraconsistent rules for how things look), zui_space (places).
- Widgets and input: zui_widget (retained widget tree, roles and names), zui_input (keyboard, pointer, touch as one stream), zui_keymap, zui_ime, zui_osk (on-screen keyboard), zui_gesture (touchpad), zui_kinetic (animated vector icons), zui_motion (reduced motion and comfort settings), zui_progress (loading bar and launch screen).
- Windows and shell: zui_wm (windows and switching), zui_dock, zui_workspace (saved window sets and preferences), zui_settings (typed settings store), zui_launch_theme and zui_launch_shell (app launch screens), zui_notice (notification center), zui_share (share sheet), zui_cliphist (clipboard history), zui_capture (screenshots and recording), zui_privacy (camera, mic, screen and location indicators).
- Sign-in screens: zui_login (the people row and lock entry; checks live in seats), zui_lock (lock screen and idle lock), zui_terms (the agreement card over src/terms).
- Accessibility and language: zui_reader (screen reader through a speech callback), zui_magnifier, zui_switch (switch access), zui_voicectl (voice control), zui_caption (live captions), zui_bidi (UAX #9), zui_shape (Arabic joining, Brahmic reordering), zui_locale (language and region formats).
- Companion: zui_chiglet (the companion drawn from its DNA), zui_emote (morphing status faces).
- Present: zui_present_fb (framebuffer); host back ends live in kernel/arch/hosted (zui_present_x11, _vulkan, _d3d11, _metal).
- views/: zv (shared app shell) and the screens for Burrow, Glint, Quillon, Spindle, Murmuration, Circuit, Display settings and autoview (apps built without C).
Main entry points: zui_canvas.h, zui_widget.h, zui_input.h, zui_wm.h, zui_login.h, zui_terms.h, zui_present.h, zui_vec.h, views/zv.h.
Tests: in verify-all: test_zui, test_zui_widget, test_zui_wm, test_zui_launch_theme, test_zui_image, test_zui_launch_shell, and src/zui/test_zui_toolkit.sh (28 tests including views/test_display_view, for example test_zui_keymap 4,304 checks, test_zui_shape 2,622, test_zui_locale 1,387). Only in CI, not verify-all: views/test_views.sh (the app screens) through zxvd_checks.sh and zxvd.yml, and kernel/arch/hosted/test_zui_present.sh (X11 and Vulkan under Xvfb, Direct3D 11 cross-built) in zxvd.yml.
Used by: no shipped program. tools/zui/zui_window.c (opens a real window through a present back end) and tools/zui/zui_mockup.c (review mockups) link it; the hosted present back ends wrap it. It is not linked into the desktop app, zxvd or either kernel image.
Gaps: the session spine (zui in both images, one event loop, damage-only present) is missing ([BAREMETAL_OS_GAPS.md](BAREMETAL_OS_GAPS.md), row 12), as are login and lock on screen on bare metal (row 13). zui_present_metal.m is not built by any script or CI job. Every word table except English is a machine translation awaiting native review (tools/zui/locales/REVIEW.md). How apps use it: [APP_MODEL.md](APP_MODEL.md), [APP_LAUNCH_SCREENS.md](APP_LAUNCH_SCREENS.md), [SPOKEN_LANGUAGES.md](SPOKEN_LANGUAGES.md).

### zxpkg — native package container: a compiled artifact as three on-disk Tri-Space files
Status: WORKING (tested in verify-all)
What it does: wraps real build output (kernel, app, driver) into self-describing S+, S- and S0 files with digests bound by trispace, and verifies releases: a ZSP v2 envelope (caller floor and arch) whose signed identity is the canonical manifest digest of (triad id, version, arch, seal).
Main entry points: zxpkg_seal, zxpkg_write, zxpkg_read, zxpkg_verify_triad, zxpkg_verify_release.
Tests: test_zxpkg and test_zxrelease in verify-all.
Used by: zxvfs_tri, evolve, loader/zsp. arm64 kernel only.
Gaps: inherits the zab native-image capability gap. build_system/sign_release.sh still emits v1 envelopes, which zxpkg_verify_release now refuses.

### zxvfs — persistent filesystem with a redo journal, extent allocator and Tri-Space store
Status: WORKING (tested in verify-all)
What it does: a block filesystem with redo-journal crash consistency and an extent allocator. zxvfs_tri stores triads atomically.
Main entry points: zxvfs_format, zxvfs_mount, zxvfs_read/write/pread/pwrite, zxvfs_unlink, zxvfs_list, zxvfs_tri_write/zxvfs_tri_open.
Tests: test_zxvfs, test_zxvfs_tri (54) and test_zxvfs_fuzz (5 checks, 3000 iterations), all in verify-all.
Used by: fs/fs.c, chronicle_persist, tolvovina ROM. Compiled into both kernels.
Gaps: none found. It was reviewed, and the fuzz test covers journal replay.


## Part C: modules on draft branches

These modules are built and tested on a draft pull request that has not merged. They are listed so the reference is complete; their status is the branch's, not main's.

### evolve/evo_comm — community-maintained updates: contract, audits, consent gate, rollback
Status: WORKING (tested in verify-all) on draft PR #155, not merged
What it does: adds four pieces on top of evolve so anyone can publish an OS change and installs stay compatible. A compatibility contract pins the wire-format descriptors and conformance bits every build must keep, and a new contract may only extend the old one. Auditors sign PASS/FAIL attestations for a lineage record. The install gate allows a record only if it is in the person's lineage DAG, keeps the contract, has k distinct passing auditors from the person's own list (default 2, never its author), no listed auditor failed it, nothing is blocked, and the person consented to that exact record before the consent expired. A journal runs each install as a trial and rolls back to the last kept build on a failed or missing health check.
Main entry points: evo_contract_init/add/encode/decode/cid/check/extends, evo_audit_encode/decode/open, evo_audit_policy_init/add, evo_community_gate, evo_journal_init/install/confirm/rollback/tick/in_trial.
Tests: src/evolve/test_evo_comm.c (461 passed when run from the branch, ML-DSA-65-signed audits and a 20,000-step random journal check) under ASan/UBSan in verify-all on the branch, plus a freestanding aarch64/riscv32 check.
Used by: no caller outside its tests on the branch.
Gaps: no zxvd RPC drives the gate; the launch contract itself (the list of wire formats) is not pinned yet; two auditor keys held by one person count as two. See docs/USER_EVOLVED_OS.md on the branch.

### chiglet/chg_bond — private on-device habit model for the companion
Status: WORKING (tested in verify-all) on draft PR #155, not merged
What it does: a small frequency model of one person's habits, not a neural network. Three experts (hour of week, app in front, previous action) each keep a weight row over the person's own actions, updated by the ISF open-system rule with decay, and are combined by how independent their evidence is (the ISF u parameter in integers). It offers a suggestion only when the top action has a clear share and was seen more than once, and every suggestion waits for the person to accept it. Bound to the owner's key; the owner can pause, inspect and forget; export needs the owner's opt-in and is integrity-checked, not encrypted.
Main entry points: chgb_init, chgb_observe, chgb_suggest, chgb_feedback, chgb_decay_all, chgb_forget_action/app/all, chgb_set_learning, chgb_export, chgb_import, chgb_interaction.
Tests: src/chiglet/test_chg_bond.c (658 passed when run from the branch, including a 60,000-step random run) under ASan/UBSan in verify-all on the branch, plus the freestanding check.
Used by: no caller outside its tests on the branch.
Gaps: tracks 32 actions and 48 apps (least used evicted); bond data does not yet feed the companion's mood or suggestions in the shell; no zxvd RPC.

### chiglet/chg_kin — companion digital DNA, hobbies and visits
Status: WORKING (tested in verify-all) on draft PR #155, not merged
What it does: gives each companion a fixed DNA record (owner, species, lineage, parent hash, seed and the 8 genes swarm_dna grows) with a SHA3-256 id that is unique within a registry. It picks 3 to 5 hobbies from a 144-item catalogue; hobby time comes only from idle growth tokens, hobbies only break ties between equal proposals, and drift past a cap resets them. Two companions may "visit" inside a grant both owners sign (topics, depth, time window, revocable, accepted once); only claims and verdicts with an evidence hash pass, logged in both hash chains, and experience is scaled by genome difference and divided by how often the pair has met.
Main entry points: chgk_registry_init, chgk_dna_birth/encode/decode/distance, chgk_hobbies_for, chgk_hobby_step/drift/budget/tiebreak, chgk_grant_encode/decode/verify, chgk_grantbook_init, chgk_revoke, chgk_visit_open/receive/close, chgk_xp_init, chgk_log_step.
Tests: src/chiglet/test_chg_kin.c (4,080 passed when run from the branch) under ASan/UBSan in verify-all on the branch, plus the freestanding check.
Used by: no caller outside its tests on the branch.
Gaps: uniqueness is per registry, not global; the ISF surplus scores genome difference, not the claims; grants and claims are not yet carried over Web 4 envelopes on Vinea; no zxvd RPC; the on-screen visit is not built.

### pay/pay_bounty — community bounties paid in Vino Floating Vouchers
Status: WORKING (tested in verify-all) on draft PR #154, not merged
What it does: anyone posts a bug, security, improvement or docs bounty for free; the price comes from the operator's schedule (base per kind times a Fibonacci tier), never from the poster. A fixer signs a delivery; the operator's audit callback runs; on PASS a review panel is drawn by a SHA3 stream from a beacon, never including the poster or fixer. The four-valued verdict holds the bounty on any dissent until an arbiter rules, and the paid tier is the lowest any approver named. Approved bounties mint VFV at epoch close within an epoch cap and a per-fixer share, in one pay_ledger posting with the 0.08889% fee split into the four buckets; what does not fit carries to the next epoch. A fraud finding reopens an unpaid bounty, never claws back a paid one, and strikes the fixer and approvers.
Main entry points: pay_bounty_cfg_default, pay_bounty_init, pay_bounty_add_reviewer, pay_bounty_post, pay_bounty_withdraw, pay_bounty_deliver, pay_bounty_audit, pay_bounty_vote, pay_bounty_verdict, pay_bounty_rule, pay_bounty_fraud, pay_bounty_close_epoch, pay_bounty_check, pay_bounty_value.
Tests: src/pay/test_pay_bounty.c (1,587 passed when run from the branch) under ASan/UBSan in verify-all on the branch; also added to tests/harness/tiers.py and the mutation targets.
Used by: no caller outside its tests on the branch.
Gaps: q colluding reviewers can mint a fake fix; no quorum timeout; reviewers are not paid; the schedule and epoch cap default to zero, so nothing can be posted until the operator sets them; the legal and tax status of VFV and bounty payments is pending counsel. See docs/BOUNTIES.md on the branch.

### speech/speech_zt_tts — on-device voice (OuteTTS on the integer engine)
Status: WORKING (tested in verify-all) on draft PR #153, not merged
What it does: joins tensor/zt_tts (prompt, audio-code tokens, WavTokenizer decoder) into one call that turns a sentence into 16 kHz PCM on the device, behind speech_session's speech-out boundary. It respells known words first (for example "Chiglet" as "chig lett"), uses a fixed seed so the same sentence gives the same audio, and refuses any language other than English rather than read it badly. Buffers are the caller's and bounded.
Main entry points: speech_zt_tts_defaults, speech_zt_tts_ready, speech_zt_tts_need, speech_zt_tts_has_lang, speech_zt_respell, speech_zt_tts_say, speech_zt_tts_synthesize, speech_zt_tts_ops.
Tests: src/speech/test_speech_zt_tts.c (77/77 when run from the branch), plain and ASan/UBSan, in verify-all on the branch. The tests replace the code-generation step (codes_fn) because there is no 500M model in CI.
Used by: no code caller on the branch; zui_chgvoice expects the host to bind it as the speaker.
Gaps: English only; the OuteTTS-0.3-500M and WavTokenizer weights are fetched outside git and are not exercised end to end in the tests.

### speech/speech_zt_stt — on-device speech recognition (Whisper on the integer engine)
Status: WORKING (tested in verify-all) on draft PR #153, not merged
What it does: turns one captured 16 kHz utterance into UTF-8 text with tensor/zt_whisper (log-mel, encoder, greedy decoder) for speech_session. Audio longer than 30 s is cut into windows and joined. A language hint picks Whisper's language, otherwise it is detected. Quiet audio, or a window where the no-speech token wins, gives empty text instead of Whisper's invented words.
Main entry points: speech_zt_stt_defaults, speech_zt_stt_ready, speech_zt_stt_transcribe, speech_zt_stt_ops, speech_zt_stt_level.
Tests: src/speech/test_speech_zt_stt.c (54/54 when run from the branch) on a tiny Whisper fixture, plain and ASan/UBSan, in verify-all on the branch; with ZXV_STT_MODEL and ZXV_STT_WAV set it also transcribes real speech (not run in CI, not checked).
Used by: no code caller on the branch; zui_hear expects the host's speech session to use it.
Gaps: no overlap between 30 s windows, so a word on the edge can be split; whisper-base weights are fetched and converted outside git.

### speech/speech_tidy — text rewritten for reading aloud
Status: WORKING (tested in verify-all) on draft PR #153, not merged
What it does: rewrites on-screen text before it goes to a voice, so read-aloud sounds like a person. Hashes, UUIDs, random ids, codes and very long numbers are named ("a hash", "3 hashes") rather than spelled; links become "a link to" the site; paths become "the file" plus its last part; code blocks become one sentence; markdown marks are silent. speech_spell gives the exact form on request, grouped in fours. All spoken words come from a per-language table.
Main entry points: speech_tidy, speech_spell, speech_tidy_kind, speech_tidy_fn.
Tests: src/speech/test_speech_tidy.c (121/121 when run from the branch), plain and ASan/UBSan, in verify-all on the branch.
Used by: zui_chgvoice on the branch (it calls speech_tidy), and zui_reader gains an optional tidy hook that a host can set to speech_tidy_fn.
Gaps: only the English word table is built in (not checked for others).

### zui/zui_chgvoice — the companion speaks when asked
Status: WORKING (tested in verify-all) on draft PR #153, not merged
What it does: decides what the companion says, when, and how it looks while saying it. Speech starts only from the person's own act (tap, hold, status, read, push-to-talk words), never from a timer or notification. Status is built from facts the caller gives; text is spoken one sentence at a time and shown in a speech bubble at the same time; it waits while another voice speaks; a refused chunk is shown as words, never spoken in the wrong language's voice. It rises from its corner with an ease (no travel under reduced motion), moves its mouth with the speech level, and never flashes.
Main entry points: zui_cv_init, zui_cv_tap, zui_cv_hold, zui_cv_status, zui_cv_read, zui_cv_stop, zui_cv_resume, zui_cv_repeat, zui_cv_spell, zui_cv_hear, zui_cv_spoken, zui_cv_level, zui_cv_tick, zui_cv_draw, zui_cv_hit, zui_cv_status_text, zui_cv_call, zui_cv_visit.
Tests: src/zui/test_zui_chgvoice.c (475/475 when run from the branch with the ASCW Golden Latin font present) through test_zui_toolkit.sh in verify-all on the branch.
Used by: zui_hear on the branch. Like the rest of zui, no shipped program links it.
Gaps: the speaker is whatever the host binds; no host binds speech_zt_tts yet (not checked beyond a search of the branch).

### zui/zui_hear — routing what the person says
Status: WORKING (tested in verify-all) on draft PR #153, not merged
What it does: decides who gets a transcript from the speech session. With dictation on, voice control types it; otherwise a phrase starting with the companion's name goes to the companion, then the companion's own phrases unless a text field has focus, then voice control. Speaking over the companion stops it, the microphone state is exposed for a status mark, and an empty or lossy transcript does nothing.
Main entry points: zui_hear_init, zui_hear_set_facts, zui_hear_tick, zui_hear_text, zui_hear_start, zui_hear_end, zui_hear_on_event.
Tests: src/zui/test_zui_hear.c (36/36 when run from the branch) through test_zui_toolkit.sh in verify-all on the branch.
Used by: no caller outside its tests on the branch.
Gaps: no host wires the speech session and speech_zt_stt to it yet.

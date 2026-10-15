<!-- Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Layer map: what goes in each of the 145 layers

This page writes out the mapping `tools/alexandria/pack_matrix.py` applies. The source of truth is `kernel/src/alexandria/MATRIX.tsv` (one line per layer) and the rules below; `tools/alexandria/layer_map.py` generated this page from that file and one pack of the current tree, so the counts change as the tree does. MATRIX.md has the formats, and its section 8 how a layer is addressed.

## Rules, in order

1. **The corpus is every tracked file of the repository** (`git ls-files`): the kernel, the host app and daemon, boot, the SDK, tools, tests, assets and documents.
2. **Booklet 000** is the first booklet of layer 0 and the first thing read after boot. It holds, in this order: `kernel/src/lingua/alphabet.tsv`, `kernel/src/lingua/zl.h`, `kernel/src/lingua/zxp_rt.h`, `kernel/src/lingua/zxp_rt.c`, `kernel/src/lingua/zl_proj.c`, `kernel/src/lingua/zl_sutra.h`, `kernel/src/lingua/zl_sutra.c`, `kernel/src/lingua/zl_kpy.h`, `kernel/src/lingua/zl_kpy.c`, `kernel/src/lingua/zl_nada.h`, `kernel/src/lingua/zl_nada.c`. The packer refuses a tree where any of them is outside layer 0.
3. **Layer N, for N from 1 to 144, is L1 system app N**, in the order of `zxv-ui/APP_CATALOG_407.csv`, with that app's vector icon.
4. **A file goes to the layer whose home matches it.** A home is a path under `kernel/src`, or from the top of the repository when it starts with `/`; the longest match wins.
5. **`docs/`, and files at the top of the repository that no home names, go to layer 130**, the developer documentation.
6. **A module no home names** (its directory under `kernel/src`, or its top-level directory) goes to the layer its glue links pull hardest, app layers preferred over the core; if nothing pulls it, to the core. The table lists where these landed.

## The layers

Files: how many files the current tree puts there. Pulled in: modules placed by rule 6.

| Layer | App | Group | Homes | Files | Pulled in |
|---|---|---|---|---|---|
| 0 | core | Core | `alloc` `mm` `sched` `syscall` `gdt` `idt` `pic` `timer` `pci` `acpi` `ata` `blockdev` `bios` `bringup` `el0_userspace` `loader` `modbind` `lpres` `oseq` `event_space` `event_sched` `event_transport` `e8` `edp_risk` `surplus` `trispace` `pqsec` `mlkem` `tls` `robin_debanks` `clock` `keyboard` `mouse` `display` `vbe` `virtio` `fs` `vfs` `zxvfs` `fat32` `tripartite_fs` `ubh` `alexandria` `lingua` `tensor` `zui` `appkit` `apps` `cellular_multikernel` `hwdrv` `net` `/kernel/arch` `/kernel/boot` `/kernel/include` `/kernel/compat` `/kernel/third_party` `/kernel/experimental` `/kernel/userapp` `/kernel/freestanding.c` `/kernel/linker.ld` `/kernel/linker_arm64.ld` `/kernel/grub.cfg` `/kernel/lpres_core.c` `/kernel/rmag_core.c` `/kernel/telemetry_core.c` `/kernel/Makefile` `pmm` `bootattest` | 1236 | `cotier` `curzi` `epu` `fusion` `kernel` `mbcomp` |
| 1 | Burrow | System core | `apps/burrow_model.c` `apps/burrow_model.h` | 2 |  |
| 2 | Hollow | System core | `desktop` `theme` `dual_space` `launch` `icon` `/macos` `/gui` `/subsystems/gui` `/ZXV-Desktop.command` | 72 | `init` `prism_break` |
| 3 | Threshold | System core | `ident` `identity` `cred` `kycwrap` | 18 | `seats` |
| 4 | Keystone | System core | `license` `terms` `bootfeat` `/LICENSES` `/LICENSE` `/LICENSE-HARDWARE.md` `/NOTICE` `/COMMERCIAL_USE.md` `/CLA.md` `/REUSE.toml` | 30 | `bridge` `mage` `reality` |
| 5 | Scent | System core | none yet | 0 |  |
| 6 | Tideturn | System core | `update` `evolve` | 22 |  |
| 7 | Satchel | System core | `zxpkg` `deploy` `/packages` `/install` `/build_system` `/scripts` `/Makefile` `/build.sh` | 133 | `lightningrod` |
| 8 | Gatehouse | System core | `privacy` `policy` `onepolicy` `ascent` `panopticon` `treaty` `legal_engine` `/governance` `/subsystems/governance` | 77 | `choice` `neon` `os_lattice` `vena` `zab` |
| 9 | Lockbox | System core | `hwkey` `svault` `pungent` `/security` `/subsystems/security` `/SECURITY.md` | 15 |  |
| 10 | Keepsake | System core | none yet | 0 |  |
| 11 | Lifeboat | System core | `macgyver` `recon` `crash` `recover` | 8 | `smap` |
| 12 | Ferry | System core | `immigration` `ztp` | 3 |  |
| 13 | Flint | Terminal, console and monitors | `pterm` `subterm` | 9 |  |
| 14 | Wake | Terminal, console and monitors | `situation` `chronicle` | 9 |  |
| 15 | Tidewatch | Terminal, console and monitors | `zstats` `telemetry` `apps/dharma_monitor.c` | 10 | `vswap` |
| 16 | Sounding | Terminal, console and monitors | `wyverneye` | 3 |  |
| 17 | Almanac | Terminal, console and monitors | `hardware` `/hardware` | 16 |  |
| 18 | Witness | Terminal, console and monitors | `peer_audit` `invproof` `provenance` `dispute` | 19 | `broker` |
| 19 | Cellar | Terminal, console and monitors | `megarom` `/megarom_corpus` | 5 |  |
| 20 | Warren | Terminal, console and monitors | none yet | 0 |  |
| 21 | Brazier | Terminal, console and monitors | `power` | 5 |  |
| 22 | Spindle | Terminal, console and monitors | `apps/spindle_model.c` `apps/spindle_model.h` | 2 |  |
| 23 | Lodestone | Terminal, console and monitors | `aipi` | 6 |  |
| 24 | Treadle | Terminal, console and monitors | none yet | 0 |  |
| 25 | Ramble | Network, mesh and Web4 | `web4` `browser` `intent` | 29 |  |
| 26 | Rookery | Network, mesh and Web4 | `outbox` | 3 |  |
| 27 | Commons | Network, mesh and Web4 | `concord` `airc` | 3 |  |
| 28 | Halloo | Network, mesh and Web4 | `call` | 16 |  |
| 29 | Trellis | Network, mesh and Web4 | `mesh_net` `devmesh` `denconnect` `constellation` `vinea` `/mobile` | 190 | `orbital_fabric` `yantra` |
| 30 | Hedgerow | Network, mesh and Web4 | `wyrmgate` `battering_ram` | 6 |  |
| 31 | Pollen | Network, mesh and Web4 | `lattice` `bootlegger` | 5 |  |
| 32 | Cove | Network, mesh and Web4 | `qnet` `plnp` `ehop` | 14 |  |
| 33 | Seedbank | Network, mesh and Web4 | `ipfs` `ipfs_node` | 22 | `vblock` |
| 34 | Signpost | Network, mesh and Web4 | `decent` `aproto` | 3 |  |
| 35 | Aerie | Network, mesh and Web4 | `wifi` `bluetooth` `harmonic` `bearer` | 52 |  |
| 36 | Gazette | Network, mesh and Web4 | `stream` | 7 |  |
| 37 | Quillon | Writing and office | `apps/quillon_model.c` `apps/quillon_model.h` `xedit` | 5 |  |
| 38 | Margins | Writing and office | `apps/notes_app.c` `qnote` | 4 |  |
| 39 | Folio | Writing and office | none yet | 0 |  |
| 40 | Furrow | Writing and office | none yet | 0 |  |
| 41 | Footlight | Writing and office | none yet | 0 |  |
| 42 | Broadsheet | Writing and office | none yet | 0 |  |
| 43 | Solstice | Writing and office | none yet | 0 |  |
| 44 | Kindred | Writing and office | none yet | 0 |  |
| 45 | Bowline | Writing and office | none yet | 0 |  |
| 46 | Wordhoard | Writing and office | none yet | 0 |  |
| 47 | Sealwax | Writing and office | none yet | 0 |  |
| 48 | Bramble | Writing and office | none yet | 0 |  |
| 49 | Glint | Media and creative | `apps/glint_model.c` `apps/glint_model.h` | 2 |  |
| 50 | Warble | Media and creative | `audio` `/assets/music` | 22 |  |
| 51 | Kinetoscope | Media and creative | `video` `codec` | 9 |  |
| 52 | Fireglow | Media and creative | none yet | 0 |  |
| 53 | Gesso | Media and creative | `art_studio` | 3 |  |
| 54 | Burin | Media and creative | none yet | 0 |  |
| 55 | Birdsong | Media and creative | none yet | 0 |  |
| 56 | Cutwater | Media and creative | none yet | 0 |  |
| 57 | Fiddlehead | Media and creative | `audiogenomics_pro` `/audiogenomics` `/subsystems/audiogenomics` | 15 | `crit168` |
| 58 | Pinhole | Media and creative | none yet | 0 |  |
| 59 | Bellows | Media and creative | `synthesis` | 2 |  |
| 60 | Relief | Media and creative | `holographic` `holodeck` `spatial` `fractal` | 18 |  |
| 61 | Murmuration | AI and the swarm | `swarm` `apps/murmuration_model.c` `apps/murmuration_model.h` `apps/murmuration.zxapp` `edgeclus` | 49 |  |
| 62 | Kitling | AI and the swarm | `chiglet` | 4 |  |
| 63 | Sieve | AI and the swarm | `predictive` | 3 |  |
| 64 | Granary | AI and the swarm | `ai_layer` | 3 |  |
| 65 | Hatchery | AI and the swarm | none yet | 0 |  |
| 66 | Temper | AI and the swarm | none yet | 0 |  |
| 67 | Lore | AI and the swarm | none yet | 0 |  |
| 68 | Parley | AI and the swarm | `i18n` `xlate` `babel` | 175 |  |
| 69 | Quarry | AI and the swarm | none yet | 0 |  |
| 70 | Retort | AI and the swarm | `engine` `qsim` `/physics_sim` `/subsystems/physics_sim` `qeq` `qubo` | 10 |  |
| 71 | Gleaner | AI and the swarm | `refinery` | 42 | `dharana` `dharma` `rmag` `sephirot` |
| 72 | Assay | AI and the swarm | `reputation` | 3 |  |
| 73 | Coffer | Money and the economy | `pay` `acct` `crypto_wallet` `finance` `apps/assurance_app.c` `apps/fee_check.zxapp` `cbank` `mint` `navport` | 105 | `fxepoch` `phase_coord` `vhub` |
| 74 | Gavel | Money and the economy | `vauction` `vbond` `vderiv` `apps/derivatives_app.c` `xrisk` | 10 |  |
| 75 | Crown | Money and the economy | `crown` `community_chest` `allotment` | 6 |  |
| 76 | Talisman | Money and the economy | `cards` `cardnet` `sicily` `/cards` | 27 |  |
| 77 | Trickle | Money and the economy | `royalty` | 3 |  |
| 78 | Tallystick | Money and the economy | `settle` `iso20022` `gridlock` | 6 |  |
| 79 | Fairground | Money and the economy | `market` `capmkt` `finance_markets` `financial_fabric` `zenny` | 28 | `mesh_token` |
| 80 | Ninefold | Money and the economy | `zcapital` `abacus` | 9 |  |
| 81 | Heirloom | Money and the economy | `estate` | 8 |  |
| 82 | Larder | Money and the economy | none yet | 0 |  |
| 83 | Reckoner | Money and the economy | `count_house` `juris` `capctl` `taxhook` | 9 |  |
| 84 | Forage | Money and the economy | `grants` `provider` `quest` | 37 |  |
| 85 | Waymark | Places, spaces and life | none yet | 0 |  |
| 86 | Meadow | Places, spaces and life | `social_spaces` `interspace` | 14 |  |
| 87 | Belvedere | Places, spaces and life | `sovereign_node` | 3 |  |
| 88 | Weatherglass | Places, spaces and life | none yet | 0 |  |
| 89 | Wellspring | Places, spaces and life | none yet | 0 |  |
| 90 | Caravan | Places, spaces and life | `freight` `logistics` `porter_house` | 10 |  |
| 91 | Vintner | Places, spaces and life | `vino` `vino_stores` `jroute` `jringress` | 6 |  |
| 92 | Hearth | Places, spaces and life | none yet | 0 |  |
| 93 | Understory | Places, spaces and life | none yet | 0 |  |
| 94 | Homestead | Places, spaces and life | none yet | 0 |  |
| 95 | Bonfire | Places, spaces and life | none yet | 0 |  |
| 96 | Hamlet | Places, spaces and life | `social` | 16 |  |
| 97 | Den | Learning, play and family | `games` `pirate_apps` `pirate_fleet` `/gamedrive_run` | 11 |  |
| 98 | Sapling | Learning, play and family | none yet | 0 |  |
| 99 | Typewright | Learning, play and family | none yet | 0 |  |
| 100 | Primer | Learning, play and family | none yet | 0 |  |
| 101 | Orrery | Learning, play and family | none yet | 0 |  |
| 102 | Fieldguide | Learning, play and family | none yet | 0 |  |
| 103 | Cubby | Learning, play and family | none yet | 0 |  |
| 104 | Riddle | Learning, play and family | none yet | 0 |  |
| 105 | Patience | Learning, play and family | none yet | 0 |  |
| 106 | Nutshell | Learning, play and family | none yet | 0 |  |
| 107 | Gnomon | Learning, play and family | none yet | 0 |  |
| 108 | Talewind | Learning, play and family | none yet | 0 |  |
| 109 | Tally | Desk utilities | `rational` | 8 | `mixmat` |
| 110 | Sundial | Desk utilities | `timesvc` `apps/clock_app.c` | 13 |  |
| 111 | Glimpse | Desk utilities | none yet | 0 |  |
| 112 | Hamper | Desk utilities | none yet | 0 |  |
| 113 | Typecase | Desk utilities | `font` `/assets/fonts` | 102 |  |
| 114 | Runebook | Desk utilities | none yet | 0 |  |
| 115 | Platen | Desk utilities | `print` | 0 |  |
| 116 | Dyer | Desk utilities | none yet | 0 |  |
| 117 | Spyglass | Desk utilities | none yet | 0 |  |
| 118 | Burr | Desk utilities | none yet | 0 |  |
| 119 | Magpie | Desk utilities | none yet | 0 |  |
| 120 | Gridmark | Desk utilities | `qr` | 0 |  |
| 121 | Joinery | Developer tools | `sdk_bridge` | 7 |  |
| 122 | Pawprint | Developer tools | none yet | 0 |  |
| 123 | Gait | Developer tools | none yet | 0 |  |
| 124 | Graft | Developer tools | none yet | 0 |  |
| 125 | Whetstone | Developer tools | `nlb` `dualtrack` | 4 |  |
| 126 | Toolchest | Developer tools | `sdk` `mfg` `/sdk` `/tools` `/kernel/tools` `/api` `/bindings` | 2092 | `orbital_compat` |
| 127 | Colonnade | Developer tools | `sutra` `legacy` `apps/cobol` `apps/fortran` `apps/python` `apps/rust` `apps/sutra` `apps/assembly` `apps/telecom` `apps/blocks` | 69 |  |
| 128 | Mockingbird | Developer tools | `hwemu` `emu` | 117 | `fwadapt` `shimmer` `tolvovina` |
| 129 | Gauntlet | Developer tools | `bombsquad` `/tests` `/fuzz` `/kernel/tests` `/bench` `/proofs` `/.github` | 5154 | `gematria` `hdcm` `hypercube` `iphase` `orbital_elevator` `quantum` `rce` `rur` |
| 130 | Scriptorium | Developer tools | none yet and `docs/` | 131 |  |
| 131 | Trestle | Developer tools | none yet | 0 |  |
| 132 | Twelvefold | Developer tools | `paradox` `axiom_matrix` `superpos` `pce` | 32 | `gridchain` `hccs` `subsystems` |
| 133 | Nightjar | Access, language and safety | `speech` | 9 |  |
| 134 | Hark | Access, language and safety | none yet | 0 |  |
| 135 | Ticker | Access, language and safety | none yet | 0 |  |
| 136 | Pivot | Access, language and safety | none yet | 0 |  |
| 137 | Glyphwright | Access, language and safety | none yet | 0 |  |
| 138 | Tint | Access, language and safety | none yet | 0 |  |
| 139 | Lamplight | Access, language and safety | `voice` | 3 |  |
| 140 | Steady | Access, language and safety | none yet | 0 |  |
| 141 | Conch | Access, language and safety | none yet | 0 |  |
| 142 | Shepherd | Access, language and safety | `ministry` | 3 |  |
| 143 | Watchfire | Access, language and safety | none yet | 0 |  |
| 144 | Stillwater | Access, language and safety | none yet | 0 |  |

10471 files in 81 layers that hold content; 64 layers are empty until their apps' code lands.

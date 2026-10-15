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
7. **The twelve Fibonacci layers are the spine** (Anchor column): 0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89 and 144, the prime dimensional layers and the corners of the frame. Every other layer N splits one way only as F(k) + M: it hangs from anchor F(k), repeats layer M's pattern and draws chunks only from that lineage. A layer with no files is an empty scaffold position (state N) until its content lands (MATRIX.md sections 9 and 10).

## The layers

Files: how many files the current tree puts there. Pulled in: modules placed by rule 6.

| Layer | Anchor | App | Group | Homes | Files | Pulled in |
|---|---|---|---|---|---|---|
| 0 | F0 | core | Core | `alloc` `mm` `sched` `syscall` `gdt` `idt` `pic` `timer` `pci` `acpi` `ata` `blockdev` `bios` `bringup` `el0_userspace` `loader` `modbind` `lpres` `oseq` `event_space` `event_sched` `event_transport` `e8` `edp_risk` `surplus` `trispace` `pqsec` `mlkem` `tls` `robin_debanks` `clock` `keyboard` `mouse` `display` `vbe` `virtio` `fs` `vfs` `zxvfs` `fat32` `tripartite_fs` `ubh` `alexandria` `lingua` `tensor` `zui` `appkit` `apps` `cellular_multikernel` `hwdrv` `net` `/kernel/arch` `/kernel/boot` `/kernel/include` `/kernel/compat` `/kernel/third_party` `/kernel/experimental` `/kernel/userapp` `/kernel/freestanding.c` `/kernel/linker.ld` `/kernel/linker_arm64.ld` `/kernel/grub.cfg` `/kernel/lpres_core.c` `/kernel/rmag_core.c` `/kernel/telemetry_core.c` `/kernel/Makefile` `pmm` `bootattest` | 1237 | `cotier` `curzi` `epu` `fusion` `kernel` `mbcomp` |
| 1 | F1 | The Banker | System core | `apps/burrow_model.c` `apps/burrow_model.h` | 2 |  |
| 2 | F3 | Hollow | System core | `desktop` `theme` `dual_space` `launch` `icon` `/macos` `/gui` `/subsystems/gui` `/ZXV-Desktop.command` | 72 | `init` `prism_break` |
| 3 | F4 | Threshold | System core | `ident` `identity` `cred` `kycwrap` | 18 | `seats` |
| 4 | 3 + 1 | Keystone | System core | `license` `terms` `bootfeat` `/LICENSES` `/LICENSE` `/LICENSE-HARDWARE.md` `/NOTICE` `/COMMERCIAL_USE.md` `/CLA.md` `/REUSE.toml` | 30 | `bridge` `mage` `reality` |
| 5 | F5 | Scent | System core | `apps/scent.zxapp` `apps/scent_model.c` `apps/scent_model.h` `apps/blocks/scent` | 0 |  |
| 6 | 5 + 1 | Tideturn | System core | `update` `evolve` | 22 |  |
| 7 | 5 + 2 | Satchel | System core | `zxpkg` `deploy` `/packages` `/install` `/build_system` `/scripts` `/Makefile` `/build.sh` | 133 | `lightningrod` |
| 8 | F6 | Gatehouse | System core | `privacy` `policy` `onepolicy` `ascent` `panopticon` `treaty` `legal_engine` `/governance` `/subsystems/governance` | 77 | `choice` `neon` `os_lattice` `vena` `zab` |
| 9 | 8 + 1 | Lockbox | System core | `hwkey` `svault` `pungent` `/security` `/subsystems/security` `/SECURITY.md` | 15 |  |
| 10 | 8 + 2 | Keepsake | System core | none yet | 0 |  |
| 11 | 8 + 3 | Lifeboat | System core | `macgyver` `recon` `crash` `recover` | 8 | `smap` |
| 12 | 8 + 4 | Ferry | System core | `immigration` `ztp` | 3 |  |
| 13 | F7 | Flint | Terminal, console and monitors | `pterm` `subterm` | 9 |  |
| 14 | 13 + 1 | Wake | Terminal, console and monitors | `situation` `chronicle` | 9 |  |
| 15 | 13 + 2 | Tidewatch | Terminal, console and monitors | `zstats` `telemetry` `apps/dharma_monitor.c` | 10 | `vswap` |
| 16 | 13 + 3 | Sounding | Terminal, console and monitors | `wyverneye` | 3 |  |
| 17 | 13 + 4 | Almanac | Terminal, console and monitors | `hardware` `/hardware` | 16 |  |
| 18 | 13 + 5 | Witness | Terminal, console and monitors | `peer_audit` `invproof` `provenance` `dispute` | 19 | `broker` |
| 19 | 13 + 6 | Cellar | Terminal, console and monitors | `megarom` `/megarom_corpus` | 5 |  |
| 20 | 13 + 7 | Warren | Terminal, console and monitors | none yet | 0 |  |
| 21 | F8 | Brazier | Terminal, console and monitors | `power` | 5 |  |
| 22 | 21 + 1 | Spindle | Terminal, console and monitors | `apps/spindle_model.c` `apps/spindle_model.h` | 2 |  |
| 23 | 21 + 2 | Lodestone | Terminal, console and monitors | `aipi` | 6 |  |
| 24 | 21 + 3 | Treadle | Terminal, console and monitors | none yet | 0 |  |
| 25 | 21 + 4 | Ramble | Network, mesh and Web4 | `web4` `browser` `intent` | 29 |  |
| 26 | 21 + 5 | Rookery | Network, mesh and Web4 | `outbox` | 3 |  |
| 27 | 21 + 6 | Commons | Network, mesh and Web4 | `concord` `airc` | 3 |  |
| 28 | 21 + 7 | Halloo | Network, mesh and Web4 | `call` | 16 |  |
| 29 | 21 + 8 | Trellis | Network, mesh and Web4 | `mesh_net` `devmesh` `denconnect` `constellation` `vinea` `/mobile` | 190 | `orbital_fabric` `yantra` |
| 30 | 21 + 9 | Hedgerow | Network, mesh and Web4 | `wyrmgate` `battering_ram` | 6 |  |
| 31 | 21 + 10 | Pollen | Network, mesh and Web4 | `lattice` `bootlegger` | 5 |  |
| 32 | 21 + 11 | Cove | Network, mesh and Web4 | `qnet` `plnp` `ehop` | 14 |  |
| 33 | 21 + 12 | Seedbank | Network, mesh and Web4 | `ipfs` `ipfs_node` | 22 | `vblock` |
| 34 | F9 | Signpost | Network, mesh and Web4 | `decent` `aproto` | 3 |  |
| 35 | 34 + 1 | Aerie | Network, mesh and Web4 | `wifi` `bluetooth` `harmonic` `bearer` | 52 |  |
| 36 | 34 + 2 | Gazette | Network, mesh and Web4 | `stream` | 7 |  |
| 37 | 34 + 3 | Quillon | Writing and office | `apps/quillon_model.c` `apps/quillon_model.h` `xedit` | 5 |  |
| 38 | 34 + 4 | Margins | Writing and office | `apps/notes_app.c` `qnote` | 4 |  |
| 39 | 34 + 5 | Folio | Writing and office | none yet | 0 |  |
| 40 | 34 + 6 | Furrow | Writing and office | none yet | 0 |  |
| 41 | 34 + 7 | Footlight | Writing and office | none yet | 0 |  |
| 42 | 34 + 8 | Broadsheet | Writing and office | none yet | 0 |  |
| 43 | 34 + 9 | Solstice | Writing and office | none yet | 0 |  |
| 44 | 34 + 10 | Kindred | Writing and office | none yet | 0 |  |
| 45 | 34 + 11 | Bowline | Writing and office | none yet | 0 |  |
| 46 | 34 + 12 | Wordhoard | Writing and office | none yet | 0 |  |
| 47 | 34 + 13 | Sealwax | Writing and office | none yet | 0 |  |
| 48 | 34 + 14 | Bramble | Writing and office | none yet | 0 |  |
| 49 | 34 + 15 | Glint | Media and creative | `apps/glint_model.c` `apps/glint_model.h` | 2 |  |
| 50 | 34 + 16 | Warble | Media and creative | `audio` `/assets/music` | 22 |  |
| 51 | 34 + 17 | Kinetoscope | Media and creative | `video` `codec` | 9 |  |
| 52 | 34 + 18 | Fireglow | Media and creative | none yet | 0 |  |
| 53 | 34 + 19 | Gesso | Media and creative | `art_studio` | 3 |  |
| 54 | 34 + 20 | Burin | Media and creative | none yet | 0 |  |
| 55 | F10 | Birdsong | Media and creative | `apps/birdsong.zxapp` `apps/birdsong_model.c` `apps/birdsong_model.h` `apps/blocks/birdsong` | 0 |  |
| 56 | 55 + 1 | Cutwater | Media and creative | none yet | 0 |  |
| 57 | 55 + 2 | Fiddlehead | Media and creative | `audiogenomics_pro` `/audiogenomics` `/subsystems/audiogenomics` | 15 | `crit168` |
| 58 | 55 + 3 | Pinhole | Media and creative | none yet | 0 |  |
| 59 | 55 + 4 | Bellows | Media and creative | `synthesis` | 2 |  |
| 60 | 55 + 5 | Relief | Media and creative | `holographic` `holodeck` `spatial` `fractal` | 18 |  |
| 61 | 55 + 6 | Murmuration | AI and the swarm | `swarm` `apps/murmuration_model.c` `apps/murmuration_model.h` `apps/murmuration.zxapp` `edgeclus` | 49 |  |
| 62 | 55 + 7 | Kitling | AI and the swarm | `chiglet` | 4 |  |
| 63 | 55 + 8 | Sieve | AI and the swarm | `predictive` | 3 |  |
| 64 | 55 + 9 | Granary | AI and the swarm | `ai_layer` | 3 |  |
| 65 | 55 + 10 | Hatchery | AI and the swarm | none yet | 0 |  |
| 66 | 55 + 11 | Temper | AI and the swarm | none yet | 0 |  |
| 67 | 55 + 12 | Lore | AI and the swarm | none yet | 0 |  |
| 68 | 55 + 13 | Parley | AI and the swarm | `i18n` `xlate` `babel` | 175 |  |
| 69 | 55 + 14 | Quarry | AI and the swarm | none yet | 0 |  |
| 70 | 55 + 15 | Retort | AI and the swarm | `engine` `qsim` `/physics_sim` `/subsystems/physics_sim` `qeq` `qubo` | 10 |  |
| 71 | 55 + 16 | Gleaner | AI and the swarm | `refinery` | 42 | `dharana` `dharma` `rmag` `sephirot` |
| 72 | 55 + 17 | Assay | AI and the swarm | `reputation` | 3 |  |
| 73 | 55 + 18 | Coffer | Money and the economy | `pay` `acct` `crypto_wallet` `finance` `apps/assurance_app.c` `apps/fee_check.zxapp` `cbank` `mint` `navport` | 105 | `fxepoch` `phase_coord` `vhub` |
| 74 | 55 + 19 | Gavel | Money and the economy | `vauction` `vbond` `vderiv` `apps/derivatives_app.c` `xrisk` | 10 |  |
| 75 | 55 + 20 | Crown | Money and the economy | `crown` `community_chest` `allotment` | 6 |  |
| 76 | 55 + 21 | Talisman | Money and the economy | `cards` `cardnet` `sicily` `/cards` | 27 |  |
| 77 | 55 + 22 | Trickle | Money and the economy | `royalty` | 3 |  |
| 78 | 55 + 23 | Tallystick | Money and the economy | `settle` `iso20022` `gridlock` | 6 |  |
| 79 | 55 + 24 | Fairground | Money and the economy | `market` `capmkt` `finance_markets` `financial_fabric` `zenny` | 28 | `mesh_token` |
| 80 | 55 + 25 | Ninefold | Money and the economy | `zcapital` `abacus` | 9 |  |
| 81 | 55 + 26 | Heirloom | Money and the economy | `estate` | 8 |  |
| 82 | 55 + 27 | Larder | Money and the economy | none yet | 0 |  |
| 83 | 55 + 28 | Reckoner | Money and the economy | `count_house` `juris` `capctl` `taxhook` | 9 |  |
| 84 | 55 + 29 | Forage | Money and the economy | `grants` `provider` `quest` | 37 |  |
| 85 | 55 + 30 | Waymark | Places, spaces and life | none yet | 0 |  |
| 86 | 55 + 31 | Meadow | Places, spaces and life | `social_spaces` `interspace` | 14 |  |
| 87 | 55 + 32 | Belvedere | Places, spaces and life | `sovereign_node` | 3 |  |
| 88 | 55 + 33 | Weatherglass | Places, spaces and life | none yet | 0 |  |
| 89 | F11 | Wellspring | Places, spaces and life | `apps/wellspring.zxapp` `apps/wellspring_model.c` `apps/wellspring_model.h` `apps/blocks/wellspring` | 0 |  |
| 90 | 89 + 1 | Caravan | Places, spaces and life | `freight` `logistics` `porter_house` | 10 |  |
| 91 | 89 + 2 | Vintner | Places, spaces and life | `vino` `vino_stores` `jroute` `jringress` | 6 |  |
| 92 | 89 + 3 | Kettle | Places, spaces and life | none yet | 0 |  |
| 93 | 89 + 4 | Understory | Places, spaces and life | none yet | 0 |  |
| 94 | 89 + 5 | Homestead | Places, spaces and life | none yet | 0 |  |
| 95 | 89 + 6 | Bonfire | Places, spaces and life | none yet | 0 |  |
| 96 | 89 + 7 | Hamlet | Places, spaces and life | `social` | 16 |  |
| 97 | 89 + 8 | Den | Learning, play and family | `games` `pirate_apps` `pirate_fleet` `/gamedrive_run` | 11 |  |
| 98 | 89 + 9 | Sapling | Learning, play and family | none yet | 0 |  |
| 99 | 89 + 10 | Typewright | Learning, play and family | none yet | 0 |  |
| 100 | 89 + 11 | Primer | Learning, play and family | none yet | 0 |  |
| 101 | 89 + 12 | Orrery | Learning, play and family | none yet | 0 |  |
| 102 | 89 + 13 | Fieldguide | Learning, play and family | none yet | 0 |  |
| 103 | 89 + 14 | Cubby | Learning, play and family | none yet | 0 |  |
| 104 | 89 + 15 | Riddle | Learning, play and family | none yet | 0 |  |
| 105 | 89 + 16 | Patience | Learning, play and family | none yet | 0 |  |
| 106 | 89 + 17 | Nutshell | Learning, play and family | none yet | 0 |  |
| 107 | 89 + 18 | Gnomon | Learning, play and family | none yet | 0 |  |
| 108 | 89 + 19 | Talewind | Learning, play and family | none yet | 0 |  |
| 109 | 89 + 20 | Tally | Desk utilities | `rational` | 8 | `mixmat` |
| 110 | 89 + 21 | Sundial | Desk utilities | `timesvc` `apps/clock_app.c` | 13 |  |
| 111 | 89 + 22 | Glimpse | Desk utilities | none yet | 0 |  |
| 112 | 89 + 23 | Hamper | Desk utilities | none yet | 0 |  |
| 113 | 89 + 24 | Typecase | Desk utilities | `font` `/assets/fonts` | 102 |  |
| 114 | 89 + 25 | Runebook | Desk utilities | none yet | 0 |  |
| 115 | 89 + 26 | Platen | Desk utilities | `print` | 0 |  |
| 116 | 89 + 27 | Dyer | Desk utilities | none yet | 0 |  |
| 117 | 89 + 28 | Spyglass | Desk utilities | none yet | 0 |  |
| 118 | 89 + 29 | Burr | Desk utilities | none yet | 0 |  |
| 119 | 89 + 30 | Magpie | Desk utilities | none yet | 0 |  |
| 120 | 89 + 31 | Gridmark | Desk utilities | `qr` | 0 |  |
| 121 | 89 + 32 | Joinery | Developer tools | `sdk_bridge` | 7 |  |
| 122 | 89 + 33 | Pawprint | Developer tools | none yet | 0 |  |
| 123 | 89 + 34 | Gait | Developer tools | none yet | 0 |  |
| 124 | 89 + 35 | Graft | Developer tools | none yet | 0 |  |
| 125 | 89 + 36 | Whetstone | Developer tools | `nlb` `dualtrack` | 4 |  |
| 126 | 89 + 37 | Toolchest | Developer tools | `sdk` `mfg` `fab` `/sdk` `/tools` `/kernel/tools` `/api` `/bindings` | 2092 | `orbital_compat` |
| 127 | 89 + 38 | Colonnade | Developer tools | `sutra` `legacy` `apps/cobol` `apps/fortran` `apps/python` `apps/rust` `apps/sutra` `apps/assembly` `apps/telecom` `apps/blocks` | 69 |  |
| 128 | 89 + 39 | Mockingbird | Developer tools | `hwemu` `emu` | 117 | `fwadapt` `shimmer` `tolvovina` |
| 129 | 89 + 40 | Gauntlet | Developer tools | `bombsquad` `/tests` `/fuzz` `/kernel/tests` `/bench` `/proofs` `/.github` | 5154 | `gematria` `hdcm` `hypercube` `iphase` `orbital_elevator` `quantum` `rce` `rur` |
| 130 | 89 + 41 | Scriptorium | Developer tools | none yet and `docs/` | 132 |  |
| 131 | 89 + 42 | Trestle | Developer tools | none yet | 0 |  |
| 132 | 89 + 43 | Twelvefold | Developer tools | `paradox` `axiom_matrix` `superpos` `pce` | 32 | `gridchain` `hccs` `subsystems` |
| 133 | 89 + 44 | Nightjar | Access, language and safety | `speech` | 9 |  |
| 134 | 89 + 45 | Hark | Access, language and safety | none yet | 0 |  |
| 135 | 89 + 46 | Ticker | Access, language and safety | none yet | 0 |  |
| 136 | 89 + 47 | Pivot | Access, language and safety | none yet | 0 |  |
| 137 | 89 + 48 | Glyphwright | Access, language and safety | none yet | 0 |  |
| 138 | 89 + 49 | Tint | Access, language and safety | none yet | 0 |  |
| 139 | 89 + 50 | Lamplight | Access, language and safety | `voice` | 3 |  |
| 140 | 89 + 51 | Steady | Access, language and safety | none yet | 0 |  |
| 141 | 89 + 52 | Conch | Access, language and safety | none yet | 0 |  |
| 142 | 89 + 53 | Shepherd | Access, language and safety | `ministry` | 3 |  |
| 143 | 89 + 54 | Watchfire | Access, language and safety | none yet | 0 |  |
| 144 | F12 | Stillwater | Access, language and safety | `apps/stillwater.zxapp` `apps/stillwater_model.c` `apps/stillwater_model.h` `apps/stillwater_rules.h` `apps/blocks/stillwater` | 0 |  |

10473 files in 81 layers that hold content; 64 layers are empty until their apps' code lands.

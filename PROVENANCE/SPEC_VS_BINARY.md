# Spec versus binary — an LVS pass over the VIT proposal specifications

_Method: for each capability a specification claims, three independent checks —
(1) does source exist, (2) is it in `build_system/Makefile.arm64`, (3) do its
symbols survive `--gc-sections` into `kernel_arm64.elf`. A claim is only LIVE if
all three pass **and** a non-test caller on the boot path exercises it._

_Companion to `LAYERED_BRINGUP.md` (the layer table this feeds) and
`SILICON_METHODOLOGY.md` (which names this check LVS: extract the netlist from
the artefact, compare it to the schematic)._

Measured 2026-08-10 against `kernel_arm64.elf` with
`aarch64-linux-gnu-nm --defined-only`, intersecting per-object symbol sets
against the ELF symbol set (`comm -12`), CSWTCH compiler artefacts excluded.

---

## 0. Coverage of this pass — stated first so the headline is not read wider than it is

`02_TECHNICAL_SPECIFICATIONS/` holds **15 distinct documents** (8 prose specs
with PDF twins, 7 YAML schemas). **This pass audited 3 of them**:

- `ACOUSTIC_ENGINE_PROPOSAL.md`
- `CHIGLET_ARCHITECTURE.md`
- `HOLOGRAPHIC_DISPLAY_SPECIFICATION.md`

**12 documents are unaudited**, including `SDH_TECHNICAL_SPECIFICATION.md`,
`ZXV_TELECOM_EVENT_FABRIC.yaml`, `ZXV_REALITY_CORE_ENGINE_SCHEMA.yaml` and
`ZXV_TURNKEY_CONSUMER_OS_MASTER_ROADMAP.yaml`. Nothing below should be read as a
statement about them. The three audited are the three the VIT proposal leads
with, so the sample is the front page, not a random draw.

---

## 1. The headline number

**43 capability claims audited. 2 LIVE, 13 PARTIAL, 9 DARK, 19 ABSENT.**

| verdict | meaning | count | share |
|---|---|---|---|
| **LIVE** | in build, symbols in ELF, exercised by a non-test caller | **2** | 4.7% |
| **PARTIAL** | container lives, the capability-defining function does not | **13** | 30.2% |
| **DARK** | compiled and linked, 100% discarded by `--gc-sections` | **9** | 20.9% |
| **ABSENT** | no implementation anywhere in the tree | **19** | 44.2% |

Two verdicts in the input set were corrected on re-verification and the
corrections are carried through above:

- *Encrypted acoustic data transfer*: **DARK → ABSENT.** DARK implies complete
  code that the linker dropped. There is no acoustic transmit/receive code to
  drop — only three enum labels (`JDR_BAND_ACOUSTIC=16`, `INFRASONIC=17`,
  `ULTRASONIC=18` at `jdr_piratenet.h:61-63`; `M5_PROTO_ACOUSTIC=41` at
  `m5route.h:66`), referenced by zero `.c` files.
- *Microphone array / ABHA speaker*: **DARK → PARTIAL.** The speaker half is
  genuinely live in a module the first pass never opened
  (`kernel/arch/arm64/virtio_snd.c`, 19/20 symbols live, called at
  `kernel_main_arm64.c:1970`). The microphone half is ABSENT — virtio-snd is
  TX-only (control queue 0 + tx queue 2, `virtio_snd.c:5-8`), no capture queue.

### Whole-tree context (ERC, not LVS)

| measure | value |
|---|---|
| non-test `.c` under `kernel/src` | 289 |
| of those, **no `.o` produced for arm64** | **76 (26%)** |
| symbols defined across `kernel/src/**/*.o` | 2,937 |
| of those, reachable in `kernel_arm64.elf` | **1,013 (34.5%)** |

`LAYERED_BRINGUP.md §0` recorded 628/2,301; the tree has grown since and the
ratio is materially unchanged. **This is not 1,924 defects** — see §8.

---

## 2. Architecture-level LVS result

Stated the way a silicon review would state it:

> **LVS FAIL — schematic and layout do not match, and the mismatch is
> structural rather than incidental.**
>
> The schematic (the three specifications) describes a signal chain:
> transducer → HAL → synthesis → application. The extracted netlist (the ELF)
> contains **one instance of the transducer** (`virtio_snd`, 19/20 nets
> connected) driving a **hardwired 4-note test pattern**, and **no HAL cell at
> all** — `kernel/src/audio/audio.c` is not in the netlist because it was never
> placed (`grep -c 'src/audio/' Makefile.arm64` → 0, no `.o` on disk). The
> synthesis block (`audiogenomics_pro`, 48 nets) is placed but every net is
> floating: 0/48 connected.
>
> The dominant failure mode is not missing cells. It is **open nets between
> cells that both exist**. `audio.h:214-232` declares the driver boundary
> `audio_ops_t`; `audio.h:281` comments `const audio_ops_t *ops; /* NULL => no
> silicon */`; the one real driver in the tree never calls `audio_bind_ops`
> (`grep` outside `kernel/src/audio` returns nothing). Two halves of one net,
> both fabricated, never joined.
>
> The same shape repeats: `voice.c` (13 nets, 0 connected) documents a handoff
> to `audio_write()` in an unplaced cell. `cards.c` (11 nets, 0 connected) is
> the only production path that would grant Chiglet its capability bitmask and
> call `chg_infer` — and `chg_infer` is itself floating.
>
> **DRC was never run.** `build_system/verify_layers.sh` does not exist;
> `grep -rn ZXV_INITCALL kernel/src --include="*.h"` returns nothing. The
> floorplan in `LAYERED_BRINGUP.md §2` is a drawing with no checker behind it,
> which is why the five violations in §7 reached the artefact.
>
> **What passes cleanly:** the trust gate. `porter_house_init`
> (`kernel_main_arm64.c:916`) precedes both dependents, and both `ai_init`
> (`:958`) and `mn_init` (`:969`) take `&porter_house` as an explicit
> constructor argument. That is a correctly-routed power net and it should be
> the template for the rest.

---

## 3. DARK — compiled, linked, 100% discarded

Ordered by how prominently the spec leans on the claim.

| # | capability | spec | module | live/total | evidence |
|---|---|---|---|---|---|
| D1 | Chiglet **inference** — deciding anything | CHIGLET §2 | `kernel/src/chiglet/chiglet.c` | 0/1 (`chg_infer`) | `chg_infer` defined in `chiglet.o`, absent from ELF. Non-test callers `cards.c:132` and `voice.c:176` are themselves 0% live. The only live use is `kernel_main_arm64.c:2279-2291`, which calls `chg_init(&chg, 0u)` — caps=0, so `CHG_CAP_INFER` is not even granted — then two `chg_interaction()` calls. A metric sanity check, not inference. |
| D2 | Chiglet **model loading** / rollback-protected epochs | CHIGLET §2 | `kernel/src/chiglet/chiglet.c` | 0/1 (`chg_load_model`) | Referenced only from `test_cards.c:109,124` and `test_games.c:48-161`. No production caller in `kernel/src`. Consequence: even if D1 were retained it would return `CHG_ERR_NO_MODEL`. |
| D3 | SD-HAL audio HAL / mixer / stream layer | ACOUSTIC §2 | `kernel/src/audio/audio.c` | 0/33 | **Worse than DARK: not compiled.** `audio.c` 45,521 B, `audio.h` 20,342 B, no `.o`, no `.d`. `grep -c 'src/audio/' Makefile.arm64` → 0. Only build reference is a host unit test (`kernel/Makefile:859,865`, `gcc -DTEST_HOST -o /tmp/test_audio`). `nm -a kernel_arm64.elf \| grep -i audio` → empty. |
| D4 | Genomic frequency database (DNA/RNA resonance) | ACOUSTIC §1 | `kernel/src/audiogenomics_pro/{audiogenomics_pro,digital_dna}.c` | 0/48 and 1/73 | In build (`Makefile.arm64:281-283`). The frequency table (`AGP_FREQ_A=146.83`, `T=174.61`, `C=261.63`, `G=391.99`, `audiogenomics_pro.h:28-32`) and every accessor are discarded. Sole survivor tree-wide: `ddna_phi_checksum_compute` (`400d1f60 T`). |
| D5 | Chiglet speaks and listens (TTS/STT) | CHIGLET §3 | `kernel/src/voice/voice.c` | 0/13 | In build (`Makefile.arm64:330`). Textbook gc case. Independently, `voice.h:20-22` states it never synthesises audio: "it does not synthesize audio and it does not recognize speech. Those are NEURAL MODELS" bound behind `voice_set_tts`/`voice_set_stt`, fail-closed with `VOICE_ERR_NO_ENGINE`. |
| D6 | Chiglet capability / loadout cards | CHIGLET §4 | `kernel/src/cards/cards.c` | 0/11 | In build. `loadout_apply` (`cards.c:111`) and `loadout_infer` (`:123-132`) are the only production path granting a Chiglet its capability bitmask and driving inference. Neither is in the binary. **This is the open net that makes D1 unreachable.** |
| D7 | 15+ Indian languages (Hindi, Tamil, Telugu, Bengali, Marathi, Urdu) | CHIGLET §3 | `kernel/src/font/script.c` | 0/7 | In build (`Makefile.arm64`). Only Indic-aware code in the tree, and it is **script segmentation for rendering**, not language or speech. Telugu and Marathi return no hits at all. `font_itemize`, `font_script_of`, `font_script_name`, `RANGES` all discarded. |
| D8 | Natural-language conversation / intent extraction | CHIGLET §3 | `kernel/src/sutra/sutra_chiglet.c` | 0/0 (not built) | `grep -c 'sutra/sutra_chiglet.c' Makefile.arm64` → 0, no `.o` on disk. Self-described "Simple keyword/pattern-based NL extractor" (`sutra_chiglet.c:1`) that parses *"Send 500 rupees from Alice to Bob"* (`test_sutra_chiglet.c:14`) — a **payment-intent parser**, not a conversational agent. |
| D9 | Text-to-speech as an acoustic-engine service | ACOUSTIC (adjacent) | `kernel/src/voice/voice.c` | 0/13 | Same module as D5, counted once against each spec because both documents rely on it. Listed for completeness of the acoustic chain: transducer LIVE → HAL absent → synthesis dark → voice dark. |

**Pattern:** 6 of 9 are in the build and discarded; 3 are not compiled at all.
The distinction matters — a discarded module compiles clean against current
headers and can be revived by adding one caller; an uncompiled module
(`audio.c`, `sutra_chiglet.c`) has not been type-checked against the tree in
however long, and may not build.

---

## 4. ABSENT — specified, no implementation anywhere

Split by whether the spec presents the item as future work (legitimate) or as
existing capability (not legitimate).

### 4a. Described as done or as present capability — **must be corrected before the proposal ships**

| # | claim | spec | evidence |
|---|---|---|---|
| A1 | Volumetric holographic display (the document's core claim) | HOLOGRAPHIC §1 | `nm \| grep -c voxel_` = 0, `volumetric_` = 0, `lbs_` = 0. Only source hits are two enum labels: `holo.h:81 HOLO_DIM_3D = 3, /* Volumetric */` and `holo.c:133 return "3D (Volumetric)"`. Neither `holo.c` is a display: `holographic/holo.c` is a file-format serializer, `emu/holo.c` is a 2D colour-tint shader (`holo_shade`, `emu/holo.c:10-33`). The live display path is an ordinary 32bpp linear framebuffer (`ramfb_init`, `vbe_init_fb`, `zxv_shell_frame`). |
| A2 | TriLite LBS laser-scanning projector | HOLOGRAPHIC §2 | Zero at all three levels. `grep -rli trilite kernel/src/` → nothing; no `kernel/src/trilite/` or `kernel/src/lbs/`; `nm \| grep -c trilite_` = 0, `lbs_` = 0. The spec nonetheless assigns VIT "Projector mounting, alignment" for a module with no software counterpart. |
| A3 | On-device NPU (Hailo-8 / Akida) accessed via SD-HAL | CHIGLET:107 | `grep -rni 'sd.hal' kernel/src` → 0. `Hailo` → 0, `Akida` → 0. The only `NPU` hits are two comments in an unrelated header (`event_transport.h:10,44`). *Caution for re-runs: case-insensitive `npu` false-positives on "inputs".* The sentence "it runs on ZXV OS using the on-device NPU via SD-HAL" describes nothing that exists. |
| A4 | Encrypted acoustic data transfer | ACOUSTIC §4 | Bands are enum labels only; zero `.c` references. `jdr_transceiver_create` (`jdr_piratenet.c:85-100`) fills struct fields in a 32-entry array. `jdr_transmit/receive` are gc'd and are anyway a plaintext byte-packer into a 256-word ring. **"Encrypted" is unearned**: the only crypto reference is `jdr_piratenet.c:146-147 tc->encrypted = false;` — never set true anywhere in the repo. Boot prints "[REGISTERED] 22 frequency bands" (`kernel_main_arm64.c:841-847`) after creating exactly one VHF transceiver. |
| A5 | Named by operator at first boot; name becomes identity seed | CHIGLET §2 | `chiglet_t` has no name field (`chiglet.h:130-135`: model, caps, inferences, uncertain_count). `CHG_NAME_LEN` is used solely for `chg_model_t.label_name` — output class labels. `kernel/src/identity/` is country/document identity, unrelated, and 0/14 live. |
| A6 | Self-evolving; learns operator communication style | CHIGLET §2 | No training or online-update code. `chiglet.h:64-67` states the opposite design intent: "everything is statically sized. No allocation, no libc, no floating point, no clock. Inference is a fixed number of operations." Models are *replaced* via `chg_load_model` with a monotonic epoch counter, never learned. |
| A7 | Seamless session handoff between devices | CHIGLET §5.3 | No serializer for `chiglet_t` exists. The transport (`mn_send_data`) and the state producer (`chg_infer`) are both discarded. |
| A8 | Agent Mesh / TPC multiplier (13× … 15,000×) | CHIGLET:176-183 | `grep -rnw 'TPC\|tpc' kernel/src` → 0; `agent_mesh` → 0. **Contradicted by a built constant**: `chiglet.h:82-84` sets `CHG_MAX_EXPERTS = 8` and `CHG_DIM = 8` — the runtime cannot represent N=54, let alone the 15,000× row. |
| A9 | On-screen avatar when projector inactive | CHIGLET §3 | `grep -rni avatar kernel/src` → **0 hits tree-wide.** The only Chiglet presence on the desktop is a static text panel rendering `chg_state_name(CHG_UNAVAILABLE)` from a literal (`zxv_shell.c:418-420`, `:496-498`). This is exactly why `chg_state_name`/`chg_reason_name` are among the 5 live Chiglet symbols — the shell prints label strings and never invokes the runtime. |
| A10 | OLED panel + optical bonding driver | HOLOGRAPHIC §3 | `nm \| grep -c oled_` = 0. Source `grep -rli oled` hits are false positives — `grep -o "[a-z]*oled[a-z]*" alloc.c` returns only `pooled`. What the binary drives is device-agnostic: QEMU ramfb over fw_cfg, or a UEFI GOP handoff framebuffer (`kernel_main_arm64.c:1913-1930`). |
| A11 | India-specific variants (agri advisor, medical assistant, government services, defense tactical) | CHIGLET:141-144 | No variant, profile, persona or domain-model code. No model corpus for `chg_load_model` to consume, and `chg_load_model` is itself dark. Product propositions with no code. |
| A12 | Layered bring-up conformance / initcall registration | ACOUSTIC (implied) | `build_system/verify_layers.sh` → No such file. `grep -rn ZXV_INITCALL kernel/src --include="*.h"` → nothing. `LAYERED_BRINGUP.md` is still marked *"Design."* **The enforcement mechanism does not exist, which is why nothing caught §7.** |

### 4b. Legitimately aspirational — hardware, research, or explicitly future

These are fine **provided the document says so**. Several currently do not.

| # | claim | spec | note |
|---|---|---|---|
| A13 | Therapeutic frequency protocols (wound healing, pain, cellular regeneration) | ACOUSTIC §5 | `grep -ril therapeut kernel/src/` → 0 files; `wound heal` → 0. Nearest is a nine-entry Solfeggio enum in `epu/epu_device.h:314-326`, neither built nor linked (`grep -n 'epu/' Makefile.arm64` → nothing, no `.o`). **The codebase refuses this claim in its own words**: `epu_device.h:135-138` — "THE SOLFEGGIO AND VORTEX FREQUENCY SETS ARE NUMEROLOGY … this module makes no claim that they do." A spec asserting biological effect that the source explicitly disclaims is the most serious document defect in this audit. |
| A14 | Frequency-response analysis for pathogen detection | ACOUSTIC §5 | `grep -ril pathogen kernel/src/` → 0. No FFT, no spectrum analyser, no response measurement. Same disclaimer problem as A13. |
| A15 | Real-time biological feedback processing | ACOUSTIC §5 | No sensor, ADC, capture or biosignal code. The only capture API lives in the uncompiled `audio.h`. virtio-snd has no RX queue. |
| A16 | Acoustic counter-measures / sonic barriers / high-power variant | ACOUSTIC §5 | No SPL control, no directional array, no beamforming. Reachable output is a 24,000-frame stereo chime at fixed amplitude 7000 (`virtio_snd.c:204`). |
| A17 | Agricultural pest control (rugged outdoor variant) | ACOUSTIC:128 | Named only in the application table. No code. |
| A18 | Acoustic chamber BOM — Niobium magnet array, N52 neodymium, titanium+Mylar dual-membrane log drum, carbon-nanotube preamp | ACOUSTIC §2 | Physical BOM, below L0, correctly outside the kernel. Flagged only because it sits in the same architecture diagram as the SD-HAL software claims. `grep -ril 'niobium\|neodymium\|N52' kernel/src/` → one file, both hits periodic-table name strings (`audiogenomics_pro.c:619,622`). **Also internally inconsistent as written** — niobium and Mylar are not ferromagnetic and cannot form the described magnet array/log drum, which confirms this section is a marketing description rather than a build. |
| A19 | Noise-harvesting layer (ambient noise → energy) | ACOUSTIC §2 | `grep -ril 'noise harvest\|energy harvest' kernel/src/` → 0. No implementation, no hardware interface. |

**The distinction that matters to VIT:** A13–A19 are acceptable as a research
annex. A1–A12 are written in the present indicative about a system that does
not do them. Seven of the twelve (A1, A2, A3, A4, A8, A9, A10) would be
falsified by a reviewer running one `nm` command.

---

## 5. PARTIAL — the most misleading category

A subsystem 2% live boots, prints `[INITIALIZED]`, and reads as present.
`live/total` is measured per object against the ELF.

| # | capability | module | live/total | % | what is missing |
|---|---|---|---|---|---|
| P1 | Digital DNA strand sync across devices | `audiogenomics_pro/digital_dna.c` | **1/73** | 1.4% | Only `ddna_phi_checksum_compute`. `ddna_init`, `ddna_translate`, `ddna_compute_hash`, `ddna_verify_integrity`, `ddna_os_genome_fingerprint` all dark. The "89-byte tau" the spec names (lines 163, 193) does not exist as a struct anywhere. **No synchronization code of any kind.** |
| P2 | Holographic data system (`.36n9`/`.9n63`/`.zedei`) | `holographic/holo.c` | **1/51** | 2.0% | Only `holo_init`. Boot banner at `kernel_main_arm64.c:1400` claims 12 registered file types; `holo_render`, `holo_reconstruct`, `holo_interference`, `holo_serialize_container`, `holo_deserialize_container` and 45 more are dark. **It announces itself at boot and can do nothing.** |
| P3 | Frequency synthesis, 0.1 Hz resolution | `audiogenomics_pro.c` (dark) vs `virtio_snd.c` (live) | **1/53** | 1.9% | The only reachable generator is `gen_chime()` (`virtio_snd.c:202-215`): `notes[4] = {440,554,659,880}`, `half = SR / (2u*notes[n])` at SR=48000. Integer half-period division **cannot express 0.1 Hz steps at all**. The real engine (`agp_generate_tone`, `agp_fm_modulate`, `agp_apply_adsr`, `agp_nested_modulation`) is 0/48. |
| P4 | ISF governance / F(1)=ln(N) capability scaling | `surplus/surplus.c` | **1/25** | 4.0% | Only `sr_sqrt`. `surplus_F` (`surplus.h:162`), `sr_ln`, `surplus_effective_count` (`:223`), `surplus_sustainability_ceiling`, `surplus_jensen_upper`, `surplus_lipschitz_bound` all dark. **The binary cannot evaluate the spec's own scaling table** (CHIGLET:176-183). Live ISF arithmetic reaching Chiglet is only `chg_interaction`/`chg_effective_experts`, which compute u and R internally. |
| P5 | AI dispatch / remote compute offload | `ai_layer/ai_layer.c` | **3/14** | 21% | `ai_init`, `ai_update_coverage` only. Dark: `ai_register_model`, `ai_verify_model`, `ai_submit_local`, `ai_execute_local`, `ai_submit_remote`, `ai_complete_remote`, `ai_add_peer`, `ai_check_timeouts`. Banner at `:961` claims "On-device inference + Porter House-gated remote compute (port 8900)". **No model can be registered, verified, executed or offloaded.** |
| P6 | Chiglet marketplace / royalty split | `community_chest/community_chest.c` | **4/16** | 25% | `cc_init`, `cc_link_vino`, `cc_update_coverage`. Every commerce op dark: `cc_purchase_app`, `cc_install_app`, `cc_verify_app`, `cc_voucher_cash_in/out`, and critically **`cc_calc_revenue_split`** — so the 20/5/75 structure in CHIGLET §6 has no enforcement mechanism in the running system. |
| P7 | Acoustic/radio transport (JDR PirateNet) | `net/jdr_piratenet.c` | **3/26** | 12% | Live: `jdr_network_init`, `jdr_transceiver_create`, `jdr_register_node`. Dark: `jdr_transmit`, `jdr_receive`, `jdr_band_name`, `jdr_band_freq_start/end`, `jdr_fhss_*`, `jdr_hum_*`, `jdr_switch_*`. *Note: live `jdr_cap_wall_ok`/`jdr_channel_depth`/`jdr_crew_state_name` belong to a different file (`pirate_fleet.c:76,265,269`) — fleet governance, not radio. Naive prefix grepping conflates them.* |
| P8 | M5 routing | `net/m5route.c` | **3/23** | 13% | Only `m5_proto_name`, `m5_router_init` (+1). `m5_router_send`, `m5_router_poll`, `m5_router_register_adapter`, `m5_router_add_route`, `m5_router_broadcast` all dark. |
| P9 | Cross-device migration over encrypted mesh radio | `mesh_net/mesh_net.c` | **5/15** | 33% | Live: `mn_init`, `mn_create_network`, `mn_join_network`, `mn_update_coverage`. **Every symbol that would move data is dark**: `mn_send_data`, `mn_create_route`, `mn_federate`, `mn_add_hop`. A network can be created and joined; nothing can be sent. There is also **no radio**: `wifi.c` and `bluetooth.c` have no `.o` and zero Makefile references. |
| P10 | Porter House port-seal firewall | `porter_house/porter_house.c` | **5/10** | 50% | **The most genuinely-live capability in the audit.** Wired for real at `kernel_main_arm64.c:916-923`: init, seal 8800 `PH_SEAL_TRUSTED` min-trust 500, seal 9090 `PH_SEAL_ALLOWLIST`, close debug port 23. Caveat: `porter_house_allowlist_add` is dark, so the allowlist seal on 9090 can never have a peer added — the operator-access path in CHIGLET §5.1 is inert. |
| P11 | TI DLP projector / DMD | `hardware/dlp_projector.c` | **6/12** | 50% | Live: `_init`, `_tick`, `_set_photometrics`, `_set_wobulation`, `_compute_screen_lux`, `_update_coverage`. Dark: `_apply_dither`, `_laser_measure`, `_set_tilt`, `_verify_actuator_sync` — **precisely the four matching the spec's §3 test list.** The kernel declares it is not hardware: `kernel_main_arm64.c:1258 /* Register DLP projector as MODEL_ONLY (no physical hardware yet) */`, banner `:1283 projector=MODEL_ONLY`, `:880 [SIMULATED] Laser AF …`. Its photometrics reach no renderer. |
| P12 | Chiglet core runtime (MoE gate) | `chiglet/chiglet.c` | **5/7** | 71% | Live: `chg_effective_experts`, `chg_init`, `chg_interaction`, `chg_reason_name`, `chg_state_name`. Dark: the two that matter (D1, D2). *Warning: `grep -ic chiglet` on ELF symbols returns 0 — the prefix is `chg_`. A naive check reports a false ABSENT here.* |
| P13 | ABHA speaker output + microphone array | `arch/arm64/virtio_snd.c` | **19/20** | 95% | Speaker LIVE (see §6 note). **Microphone ABSENT** — TX-only, no capture queue; `mic_`, `microphone`, `i2s_`, `hda_`, `ac97` all → 0. The capability is exactly half-built and the spec (CHIGLET:114-115) presents both halves alike. |

**Median PARTIAL is 25% live. Four are under 5%** (P1–P4) and those four carry
the spec's headline numbers: DNA sync, holographic containers, 0.1 Hz
synthesis, and the ISF scaling table.

---

## 6. LIVE — credit where it is due

| capability | module | live/total | proof of exercise |
|---|---|---|---|
| **Audio output to real hardware** | `kernel/arch/arm64/virtio_snd.c` | **19/20** | In build (`Makefile.arm64:116`). ELF: `virtio_snd_init` 0x4008d6a0, `virtio_snd_chime` 0x4008d700, `snd_init_slot` 0x4008d460, `SND_DRV` 0x400e8988, plus the full `s_pcm/s_tx/s_ctl/s_stream/s_xfer/s_ready/s_base/s_qi/s_ph/s_sp/s_resp/s_xstat` ring state (`s_pcm` at 0x477e0000). Called at `kernel_main_arm64.c:1970-1972`; banner "[DRIVER ONLINE] virtio-snd — audio output live"; string "[virtio-snd] boot chime played" present in the binary. Only `virtio_snd_ready` is gc'd. **This is the single most complete driver the three specs touch.** |
| **Media codec (8×8 integer DCT-II)** | `kernel/src/codec/zmedia.c` | **7/8** | `Makefile.arm64:255`. Live: `zm_encode_positive` 0x400c8d60, `zm_decode_positive` 0x400c91c0, `zm_build_negative` 0x400c9600, `zm_restore_exact` 0x400c9640, plus COS/QBASE/ZIGZAG tables. Exercised on a 64×64 image at `kernel_main_arm64.c:303-306`. **Listed explicitly so nobody counts it as evidence for the ABHA audio module — it is an image codec** (`zmedia.h:32-39`). |

Also genuinely correct, though scored PARTIAL: the **Porter House ordering**
(P10) and the **DLP device model's honesty** (P11) — both the source comment and
the boot banner tell the truth about being a model. Modules whose headers refuse
to overclaim (`audio.h:23-30`, `voice.h:20-32`, `epu_device.h:135-138`) are the
best-engineered artefacts in this audit; the defect is that the *specifications*
claim what those headers explicitly deny.

---

## 7. Layer assignment table

Every module named across the three specs, mapped to `LAYERED_BRINGUP.md §2`.
**This is the actionable output** — it is the floorplan input for the initcall
table, and each row is the layer the module would register into.

| module | L | reason for the placement | status | live/total |
|---|---|---|---|---|
| `surplus` | **L0** | Named in the L0 row. Pure arithmetic, depends on nothing, not even memory. | PARTIAL | 1/25 |
| `edp_risk` | **L0** | Includes only `surplus` (`edp_risk.h:22-24`). | (dep only) | — |
| `zphi`, `rat`, `e8`, `mixmat`, `crit168` | **L0** | Named in the L0 row. | out of scope | — |
| `identity` | **L1** | Named in the L1 row. An operator-chosen first-boot seed (A5) belongs here, not in the L5 service that consumes it. | DARK | 0/14 |
| `invproof` | **L1** | Named in the L1 row; consumed by `zmedia` at L5. | LIVE (dep) | — |
| `digital_dna` (identity half) | **L1** | Defines `ddna_os_genome_fingerprint`, `ddna_os_identity_string`, `ddna_verify_integrity` — identity is L1 per §2. **Wrongly compiled into an L5-including TU.** | PARTIAL | 1/73 |
| `zxvfs`, `alloc`, `mm` | **L2** | Named in the L2 row. Cross-device strand merge (P1) would need L1 causal ordering here. | out of scope | — |
| `porter_house` | **L3** | Capability gating. Both `ai_layer` and `mesh_net` take `&porter_house` as a constructor argument — that makes it their precondition, definitionally L3. | PARTIAL | 5/10 |
| `mlkem`, `zab`, `tls` | **L3** | Named in the L3 row. The unearned "encrypted" in A4 would bind here. | out of scope | — |
| `virtio_snd` | **L4** | virtio-mmio class driver; needs L2 identity-mapped memory for its static rings and the shared `virtio_bus` scan (`virtio_snd.c:3-8` mirrors `virtio_input.c`/`virtio_blk.c`). | **LIVE** | 19/20 |
| `audio` (HAL/mixer) | **L4** | Owns streams, DMA staging, mixing and the `audio_ops_t` hardware boundary — it *is* the device abstraction `virtio_snd` should bind into. | **NOT BUILT** | 0/33 |
| `dlp_projector` | **L4** | Register-mapped, event-cycle-driven peripheral model. Include graph is clean: `surplus` (L0) + `edp_risk` + `m5_types`. | PARTIAL | 6/12 |
| `ramfb` / `vbe` | **L4** | Scanout devices; the actual live display path. | LIVE | — |
| `jdr_piratenet` | **L4/L5** | Straddles: a virtual-SDR device model (L4) exposing a routing/registration API (L5). Should be split when it is registered. | PARTIAL | 3/26 |
| TriLite LBS, OLED panel, NPU (Hailo/Akida) | **L4** | Would-be device drivers. Nothing to place. | ABSENT | 0 |
| `chiglet` | **L5** | Named explicitly in the L5 row. Depends only on `surplus` (L0) via `chiglet.h:78` — strictly downward. | PARTIAL | 5/7 |
| `codec` (`zmedia`) | **L5** | Named in the L5 row. | **LIVE** | 7/8 |
| `audiogenomics_pro` | **L5** | A frequency-mapping service: consumes arithmetic, produces PCM for an L4 device. | DARK | 0/48 |
| `voice` | **L5** | Depends on `chiglet` (L5, lateral) and feeds `audio_write()` at L4. | DARK | 0/13 |
| `cards` | **L5** | Configures the L5 chiglet runtime; calls `chg_effective_experts` (`cards.c:84`). | DARK | 0/11 |
| `ai_layer` | **L5** | Dispatch service above devices, gated by L3. Boot comment labels it "Phase 15f". | PARTIAL | 3/14 |
| `mesh_net` | **L5** | Overlay networking over L4 radio + L3 gate ("Phase 15g", `kernel_main_arm64.c:962-967`). | PARTIAL | 5/15 |
| `m5route` | **L5** | Routing over devices. | PARTIAL | 3/23 |
| `holographic/holo` | **L5** | Data/codec service; peer of `codec` in the L5 row. | PARTIAL | 1/51 |
| `font/script` | **L5** | Text shaping over an L4 display. **Not a language capability at any layer.** | DARK | 0/7 |
| volumetric render pipeline | **L5** | Would consume an L4 scanout. Nothing to place. | ABSENT | 0 |
| `community_chest` | **L6** | Marketplace with revenue splits; `cc_link_vino` confirms the vino coupling named in the L6 row. | PARTIAL | 4/16 |
| Agent Mesh / TPC entitlement | **L6** | Cross-device aggregation changing what a device may compute is a governance claim. | ABSENT | 0 |
| `zxv_shell` / desktop | **L7** | Named in the L7 row. Renders Chiglet label strings only. | LIVE (cosmetic) | — |
| `sutra_chiglet` | **L7** | `sutra` named explicitly in the L7 row. | NOT BUILT | 0 |
| acoustic chamber BOM | **below L0** | Mechanical/electrical, outside the kernel floorplan entirely. Recorded so proximity in the architecture diagram does not imply software. | n/a | — |

---

## 8. Floorplan violations

A module may depend only on strictly lower layers (`LAYERED_BRINGUP.md §2`).
Five violations, none currently detectable by any check.

**V1 — L4 device gated behind an L5 service (ordering).**
`virtio_snd_init()` at `kernel_main_arm64.c:1970` is nested inside the ramfb
display success branch (`:1930 int rc = … ramfb_init(...)`, then `if (rc == 0)`
… `else if (rc == -2) [SKIP] no ramfb device` at `:1977`). **Boot without
`-device ramfb` and virtio-snd is never probed even when the sound device is
present.** Audio availability is silently conditional on display availability.
Fix: hoist the call out of the branch. Three lines.

**V2 — L1 identity compiled into an L5 translation unit (include graph).**
`digital_dna.c` defines `ddna_os_genome_fingerprint`, `ddna_os_identity_string`,
`ddna_verify_integrity`, `ddna_compute_hash` — L1 concerns — in a TU whose
header does `#include "audiogenomics_pro.h"` (`digital_dna.h:28`), an L5 header.
The live symbol is exactly this case: `kernel_main_arm64.c:2197-2198` calls
`ddna_phi_checksum_compute()` as an **early integrity check**, i.e. a low-layer
consumer reaching into the audio-genomics module. This is the one violation a
`verify_layers.sh` include-graph check would catch mechanically today.

**V3 — missing edge, both cells present (ERC open).**
`audio.h:214-232` defines `audio_ops_t` as the driver boundary; `audio.h:281`
comments `const audio_ops_t *ops; /* NULL => no silicon */`. `virtio_snd.c`
never calls `audio_bind_ops` — grep outside `kernel/src/audio` returns nothing.
The HAL and the driver are two disconnected halves, and the live chime bypasses
the HAL entirely by writing `s_pcm` directly.

**V4 — dangling dependency on an unbuilt module.**
`voice.h:23-26` documents that its PCM "is the same S16LE the audio mixer
speaks … so a bound TTS can hand its output straight to `audio_write()`" — a
symbol in a module that is not in the build at all. The declared consumer edge
points at something no link ever resolves.

**V5 — L4 device before L3 trust (ordering).**
`dlp_projector_init` at `kernel_main_arm64.c:874` runs 42 lines before
`porter_house_init` at `:916`. §2 states L4 devices need "memory (L2) and
capability gating (L3)". Fix: move the call below `:916`.

**V6 — no enforcement, which is why V1–V5 shipped.**
`build_system/verify_layers.sh` does not exist. `ZXV_INITCALL` does not exist in
any header. `SILICON_METHODOLOGY.md §1` already lists DRC as **"to build"** and
the floorplan as **"designed, unenforced"**. That is accurate and this audit is
the cost of it.

**Correctly ordered, for contrast:** `porter_house_init` (`:916`) →
`ai_init` (`:958`) → `mn_init` (`:969`), with the gate passed explicitly to both.
`chiglet` is initialised at `:2279`, after L3/L4 — order-legal, though inert
(caps=0, no model). `jdr_piratenet` at `:840` is labelled "Phase 12" and runs
after device bring-up: correct.

Also worth recording as a non-violation: **`dlp_projector` is layer-clean but
isolated.** `grep -rln dlp_projector_t kernel/src/ kernel/arch/` (excluding
tests) returns only the module and `kernel_main_arm64.c`, and every reference
there (`:334`, `:874-878`, `:2915`) is init/config/tick. No framebuffer or
compositor code reads it. A live island, not an integrated device — an ERC
finding rather than a DRC one.

---

## 9. What to wire, what to correct, what to leave staged

**Wiring everything is not the goal, and must not become one.** 1,924
unreachable symbols is not a defect count. `LAYERED_BRINGUP.md §0` already
settled this: hand-writing ~1,600 callers "would raise the metric and lower the
truth — boot-time calls that do nothing, and a number that no longer
distinguishes integration from ceremony." Per the organism architecture, an
unlinked module staged on purpose is a **stem cell**, not dead code. The
question for every row above is not "can it be reached" but "does a real
consumer need it".

### 9a. Wire — six items, ordered by value per line changed

| # | change | closes | size |
|---|---|---|---|
| W1 | Hoist `virtio_snd_init()` out of the ramfb success branch | **V1** | ~3 lines |
| W2 | Move `dlp_projector_init` below `porter_house_init` | **V5** | 1 line moved |
| W3 | Add `kernel/src/audio/audio.c` to `Makefile.arm64`, fix whatever has bit-rotted since it was last target-compiled | **D3**, unblocks V3/V4 | 1 line + fallout |
| W4 | Have `virtio_snd` call `audio_bind_ops()` with a real `audio_ops_t` | **V3** — joins the two halves; makes the HAL, then `voice`, then `agp` reachable *targets* rather than orphans | ~40 lines |
| W5 | Write `build_system/verify_layers.sh` (include-graph DRC) and gate the link rule on it, as `verify_banners.sh` already is | **V2, V6** — and prevents regression of W1/W2 | one script |
| W6 | Land `ZXV_INITCALL` + `KEEP()` in the linker script for L0–L7 | **A12** — makes reachability a consequence of layer declaration rather than a chase | per `LAYERED_BRINGUP.md §1` |

W1, W2 and W5 are the highest-value work in this document: two are trivial and
one makes the other four permanent. **W3+W4 together are the difference between
"has an audio driver" and "has an audio stack"** — everything acoustic in the
proposal hangs off that single unbound `audio_ops_t`.

Deliberately *not* recommended: retaining `surplus_F`/`sr_ln` (P4),
`cc_calc_revenue_split` (P6) or `mn_send_data` (P9) by adding synthetic callers.
Retain them when the consumer exists; a self-check made of literals is exactly
the class of thing this project spent a week removing.

### 9b. Correct — documents, not code

1. **A13/A14 must be rewritten or removed.** `epu_device.h:135-138` states the
   frequency sets are numerology and "this module makes no claim" of biological
   effect. A specification asserting therapeutic and diagnostic efficacy that
   the source explicitly disclaims is the single largest liability here, and it
   is a medical claim.
2. **A8's table contradicts a built constant** (`CHG_MAX_EXPERTS = 8`). Either
   the constant or the 15,000× row is wrong; both cannot ship.
3. **A18's transducer stack is not physically constructible as written**
   (niobium and Mylar are not ferromagnetic). Move to a clearly-labelled BOM
   concept section.
4. **Fix three boot banners or fix the code behind them** — the banners are the
   LVS reference netlist, and `verify_banners.sh` exists precisely to compare
   against them: `:961` "On-device inference + Porter House-gated remote
   compute" (P5, nothing can be registered or executed), `:1400` "12 registered
   file types" (P2, 1/51 live), `:844` "22 frequency bands" (A4, one VHF
   transceiver created).
5. **Restate A1–A4, A9, A10 in the future tense** or attach the measured status.
   Each is falsifiable by one `nm` invocation.
6. **Delete "encrypted" from the acoustic/mesh transport claims** until a cipher
   is actually called. `tc->encrypted = false` is never set true anywhere.

### 9c. Leave staged — and say so in the spec rather than quietly

`audiogenomics_pro`, `digital_dna`, `cards`, `holographic/holo`, `font/script`,
`sutra_chiglet`, `epu_device`, `net/dtmf.c`, `net/radio.c`, `wifi`, `bluetooth`,
`identity`.

Two of these deserve a specific note:

- **`net/dtmf.c` (18,484 B) is a real acoustic modem** — tone synthesis by phase
  increment (`:50-58`), Goertzel demodulation (`:83-116`), RTTY FSK (`:241-288`)
  — and it is *not* in the arm64 build (`grep -c 'net/dtmf.c' Makefile.arm64` →
  0; it survives only in the legacy `build_arm.sh:52` / `build_x86_64.sh:66`).
  So the modulator A4 needs already exists in the tree, unencrypted and
  unconnected. That is a staging decision, not a gap — but the acoustic spec
  should point at it rather than at enum labels.
- **`font/script.c`** is correct, useful code that is simply mis-cited. It
  should be described as Indic *script segmentation for rendering*, which it
  genuinely is, and removed from the "speaks 15+ languages" claim, which it is
  not.

---

## 10. Reproducing this

```sh
cd /Users/36n9/CascadeProjects/VIT_INDIA_PROPOSAL/05_KERNEL
E=kernel_arm64.elf
aarch64-linux-gnu-nm --defined-only $E | awk '{print $3}' | sort -u > /tmp/elf.txt
aarch64-linux-gnu-nm --defined-only kernel/src/<mod>/<mod>.o \
  | awk '{print $3}' | grep -v CSWTCH | sort -u > /tmp/mod.txt
comm -12 /tmp/mod.txt /tmp/elf.txt | wc -l    # live
wc -l < /tmp/mod.txt                          # total
```

Three traps this audit hit, recorded so the next pass does not:

- **Prefix guessing produces false ABSENTs.** Chiglet exports `chg_`, not
  `chiglet_`; `grep -ic chiglet` on ELF symbols returns 0 for a module that is
  71% live. Always enumerate the `.o`'s real exported names first.
- **Prefix greps produce false LIVEs.** ` cc_` matches 13 lines in the ELF but
  `community_chest.o` contributes only 4 — substring collisions from other
  modules. `jdr_cap_wall_ok`/`jdr_channel_depth`/`jdr_crew_state_name` are
  `pirate_fleet.c`, not `jdr_piratenet.c`.
- **Case-insensitive substring greps lie.** `grep -i npu` matches "inputs";
  `grep -i oled` matches "pooled".

Absence of a `.o` beside a `.c` is the fastest DARK-versus-not-built
discriminator in this tree: 76 of 289 non-test sources have none.

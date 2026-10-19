FULL-STACK FABRICATION CRITERIA — the one-stack build, the updater, the languages
(bound into the A6000 pipeline 2026-10-19; the master fabrication contract)

THE PREMISE. The kernel and the OS are ONE stack, fabricated together — not
a kernel with apps bolted on. The ELF is the recursive fractal seed: it
carries the committed geometry (SCP-144 lattice ROM, Fibonacci phase table,
curriculum CAR, the 561-seed bank) and the stage-0 grow driver unfolds it
ring by ring. Every layer of the OS — the shell, the ledger, the mesh, the
languages, the fonts, the installer — derives from that seed and is
committed by digest into it. New code must EXPAND the seed, never sit beside
it.

CRITERION 1 — ONE STACK, FABRICATED TOGETHER.
Every module fabricated must land inside kernel/src with a stratum
(build_system/strata.txt), a real test, and a digest committed into the
seed's generated layers. There is no "userland outside the kernel": the
stack is monolithic by design (the megalithic kernel). The fabrication
pipeline's syntax gate is the FIRST check; the real gates are:
- build_system/verify_layers.sh (strata, no upward includes)
- make -C kernel test-<module> (a real test, proven to fail first)
- freestanding compile on x86_64, riscv32 and aarch64 (no libc, no float,
  no division by a variable 64-bit divisor)
- exact integer arithmetic for money, tokens, settlement, protocol state
- complete code only: no TODO, no stubs, no placeholder branches

CRITERION 2 — THE UPDATER, WITHOUT REBUILD.
The OS must update itself without rebuilding from scratch. The mechanism
already exists and is authoritative (kernel/src/update/update.c,
zx_upcheck.c):
- updates are content-addressed bundles (sha256 CIDs), signed by the
  publisher, verifiable by anyone; nothing installs without opt-in;
- update buckets are checked on IPFS via zx_upcheck with ML-DSA signature
  verification (pqsec);
- grouped BUNDLES let a user opt into a set as one decision;
- fabricated update modules must feed THIS mechanism (a signed, CID-
  addressed, ML-DSA-verified bundle), never a side channel.
The updater itself is the rock-solid foundation: A/B-able, rollback-able,
verified before install, atomic after.

CRITERION 3 — THE INSTALLER FOUNDATION, EVERY PLATFORM.
Installer images for every chipset family, all reproducible from the
committed seed:
- El Torito (grub-mkrescue) for the x86 family: x86_64 AND i386;
- UEFI ISO (GPT + ESP + BOOT*.EFI) for arm64 and riscv64;
- raw disk images for arm32, riscv32 and i386;
- every image carries the MegaROM array (MEGAROM.TVL / -SERVER / -USER /
  -FOUNDRY) plus BOOTSEL.TVL sealing the chosen world, and the whole
  561-seed bank;
- reproducible: same commit -> same image bytes (SOURCE_DATE_EPOCH, fixed
  GUIDs, deterministic FAT).
The installer chooses the world ONCE (server / user / foundry) and seals it;
switching worlds is the installer's act, never a boot toggle.

CRITERION 4 — ALL 104 CODING LANGUAGES, NATIVE.
The Lingua layer (kernel/src/lingua) is the universal execution core: 112
languages in the catalogue, 85 with zl_*.c front-ends, 20 at run tier.
Rules:
- every language gets a real front-end (zl_<lang>.c) with a twin, a golden
  collatz.<ext> + .out, a features.<ext> + .out, a row in
  tests/harness/mutator/targets.json, and its catalogue.tsv line moved to
  run; the gates are make -C kernel test-lingua and test-lingua-ref;
- languages compile to ONE IR (zl.h) — the Fielddrake twin canonical form;
- when the build finishes, NO ONE should need freestanding C: every part of
  the stack must be expressible and compilable through the Lingua front-ends
  while the host SDK and Wasm compatibility paths remain;
- the remaining ~84 languages (catalogue/next tiers) are fabricated in
  batches by the pipeline, each verified against the same gates.

CRITERION 5 — ALL ~224 HUMAN LANGUAGES, NATIVE.
The human-language layer is already seeded: 152 ASCW-Golden fonts in the
bank (assets/fonts/ascw-golden/, every script from Adlam to Zanabazar
Square). The work is the MAPPING, not the glyphs:
- a language registry mapping each human language (ISO 639 codes) to its
  ASCW-Golden face, plural rules, direction (LTR/RTL), and digit system;
- the UI (zui / zxv_shell) renders any registered language natively, with
  the correct face and direction, through the existing font machinery;
- fabrication extends the registry to the full target set (~224 languages),
  each entry verified by a round-trip test (a sample string renders back to
  the same codepoints);
- the registry itself is a committed seed layer, digest-bound into the ELF.

CRITERION 6 — EXACTNESS AND FAIL-CLOSED, ALWAYS.
Everything fabricated inherits the kernel's invariants: exact integer
arithmetic (Q16.16, Z[phi], Zeckendorf — never float for exact values), the
four-valued lpres logic (never re-derived), bounded preallocated rings with
explicit full checks, check-invariants-then-mutate-then-commit, and the
paraconsistent rule: contradictory modules are preserved until their purpose
is understood — never delete to simplify.

CRITERION 7 — THE CRITERIA THEMSELVES ARE FABRICATED TOO.
The armada lessons (peer list baked in, ladder-bound handshake, Fibonacci
tick, policy-as-.zxapp, curriculum-fed index, inbound-only), the megalithic
mirror + vortex + Solfeggio/555 rules, and the Fibonacci lattice criteria
are all binding on fabricated output. A module that violates them is not
fabricated; it is a draft.

END — the goal state: one seed, one stack, every platform installable,
self-updating, and every one of the 104 coding + ~224 human languages
native. No freestanding C required to build any part of it.
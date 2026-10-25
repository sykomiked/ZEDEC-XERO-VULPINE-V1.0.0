# QA progress on the Claude branch

Target: `claude/project-thread-j44rs9`, starting commit
`b567ac1ea0b89ff78645f3dc37d519a8024119fd`.
This is a development checkpoint, not a completion or certification claim.

## Repairs and evidence

- Corrected Belnap conjunction/disjunction for BOTH with NEITHER. The independent
  evidence-bit oracle checks the truth tables and algebra, including De Morgan's
  laws. LPRES accepts only canonical trits, normalizes the legacy GLUT alias,
  resets cycle state on initialization and saturates evidence counts.
- Replaced the fixed 512-byte HMAC staging buffer with streaming SHA-256.
  Independent Python HMAC/HKDF tests cover block and former buffer boundaries;
  known-answer tests also cover X25519, ChaCha20 and Poly1305.
- Checked rational intermediates on narrow integer targets and compare using
  continued fractions rather than overflowing cross products. Unsigned magnitude
  normalization supports reducible INT64_MIN inputs without signed overflow.
  Both wide and forced-narrow backends pass 50,011 Fraction-oracle checks and
  ten sanitizer boundary checks each.
- Adopted the owner's exact repeating assurance rate 8/9000 in the payment
  engine, independent fee reference, fork-conformance checker and market oracle.
  The payment suite passes 52,479 checks with sanitizers and its 120,000-amount
  reference. Existing carries from the former rate require explicit migration;
  this change does not migrate a deployed ledger.
- Added a rational-fee COBOL corridor copybook. Its boundary tests reproduced
  unsigned negative values and excess PIC digits accepted by the record codec.
  The codec now validates declared sign and digit width before committing an
  output. The new 174 checks and existing 49 COBOL checks pass.
- Fixed an ARM32 boot alignment abort: compiler-generated unaligned stores in
  the serialized TVUL probe are disallowed using `-mno-unaligned-access`.
- Allowed the freestanding i386 host tests to use qemu-i386 when installed.
  Direct execution is unsupported in this cloud container. The first failing
  binary passes all 61 checks under the emulator; assertions remain enabled.
- Added the bounded CORE_AUDIT agent-manifest parser, exact coverage gate and
  integer topic quotas. Ten test cases exercise 20,301 coverage boundary pairs,
  3,000 quota allocations and unsupported/malformed inputs. They validate SDK
  control data, not direct transformer behavior or evidence correctness.
- Added the atomic dividend ledger primitive for the owner's two separately
  tracked source categories and holdings-based allocation. Tests cover source
  conservation, opt-in, replay, reversal and failure atomicity. It compiles
  freestanding for all five architecture families; distributions currently
  support at most six recipient mappings per atomic posting.

## Gates

`bash build_system/qa.sh` runs the independent oracles, sanitizer regressions,
copybook tests, installer security self-check and existing ELF/filesystem fuzzers.
It is included in the host CI job alongside the existing `make verify-all` gate.

All five advertised architecture builds succeeded. The existing
`build_system/ci_boot_check.sh` was run for ten seconds on ARM64, ARM32, x86_64,
RV64 and RV32: each reached BOOT_OK with no fault marker. ARM64 and x86_64 also
required AI_OK. These checks establish simulated bring-up, not every subsystem's
functionality, hardware validation or real payment-network connectivity.

The full `make -C kernel verify-all` run completed successfully on the Claude
branch. Its log ended with ALL STAGE-1 CHECKS PASSED. Subsequent COBOL codec
validation against an actual GnuCOBOL-generated packed record passed 183 checks;
it exposed and corrected the unsigned COMP-3 sign nibble (F rather than C).
The focused gate now compiles that independent fixture with GnuCOBOL.

`python3 tools/qa_inventory.py --strict --output build/qa/inventory.json`
records literal missing test inputs, specification source-reference candidates,
subsystems and the test-code budget. Source presence is not integration proof.
The inventory resolves component Makefile paths in their own directories.

## Work still required

The six-to-one test-code target is not met. The measured ratio is approximately
0.32, using nonblank repository C/H production lines and C/H/Python test lines,
including generated tables and excluding third-party code. The inventory also
reports C/H-only test counts. Reaching the requested ratio requires roughly
1.86 million additional substantive test lines at the current production size;
duplicated or padded tests would not improve assurance.

The supplied Zenny engine facade, funded collateral integration, global dividend
orchestration, new chip-mint Sutra syntax and a semantics-preserving COBOL bridge
are not complete. See `ZENNY_SETTLEMENT_REQUIREMENTS.md` and
`COBOL_AGREEMENTS.md` for exact decisions and acceptance criteria. Any opted-in
account is dividend-eligible; funded fee-pool/external sources and pro-rata
V-Bill holdings allocation are selected and implemented as the bounded primitive
described in `DIVIDENDS.md`. Existing codecs and that primitive must not be
presented as a complete agreement interpreter or global dividend service.

Repository-wide functional traceability, adversarial integration tests,
coverage measurement, formal-model/C correspondence, hardware testing and
external rail conformance remain separate audit work. Boot banners and file
counts do not establish these properties.

# Wave 20 task: Fusee (slot 247), Boot chain keeper
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (017 almanac, merged); wave 1 (006 tideturn, 009 lockbox, 018 witness); wave 2 (011 lifeboat).
Manifest: `slot 247`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/247-fusee.svg`, `budget 233`, model `apps/fusee_model.{h,c}`, blocks under kernel/src/apps/blocks/fusee/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts the boot chain: the UEFI stubs (kernel/arch/x86_64/efi_stub.c, kernel/arch/arm64/efi_stub.c, kernel/arch/efi_models.h), measured boot (kernel/src/bootattest), boot features (kernel/src/bootfeat), A/B slots (kernel/src/loader/abupdate.c), update checks (kernel/src/update, zx_upcheck.c) and hardware keys (kernel/src/hwkey). Signed update check (`update.check`), the journal's chain (`journal.verify`, `journal.query`), switches (`features.get`), the machine (`sys.info`). Secure Boot keys, boot entries, A/B slot selection, firmware capsules, microcode and reading the measured-boot log have no method: use only existing methods; list what's missing in your report.
Uses: `use tideturn 1` (version ordering and "both signatures valid"); `use lifeboat 1` ("safe to start normally" and the next repair step); `use lockbox 1` (key expiry and limits); `use almanac 1` (uptime `days`); `use witness 1` ("chain intact"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may boot this image" = both signature checks pass AND the stub is signed AND the version is at or above the rollback floor, in Ada as Eve + APL as KRON witnessed
- (critical) slot header check at boot (magic, version, tries left) in Forth (twin Cairndrake) + Ada as Eve witnessed
- firmware version arithmetic (major.minor.patch packed into one integer, distance to the floor) exact in FORTRAN as Trebago
- measurement table (expected and logged digest indexes, the first mismatch) in APL as KRON
- boot-entry and slot ledger (tries counters per slot, refusing on overflow) in COBOL as Sutra
- firmware inventory tree in MUMPS as MYRA
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): one signature check passes and the other fails on the same image, giving BOTH; the test asserts state 3 and that the image is never booted until the owner rules. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never writes firmware, keys or boot entries in tests; tests use image files only. Never touch build_system/keys/.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: sys.read, journal, net, notify.
Planned (no runtime yet): Nim, Racket.

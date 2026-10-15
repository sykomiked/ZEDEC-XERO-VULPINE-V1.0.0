# Wave 21 task: Grotto (slot 263), Hidden compartment
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (001 banker, merged); wave 1 (009 lockbox, 008 gatehouse); wave 9 (112 hamper).
Manifest: `slot 263`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/263-grotto.svg`, `budget 233`, model `apps/grotto_model.{h,c}`, blocks under kernel/src/apps/blocks/grotto/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Keeps a hidden, encrypted compartment for the most sensitive files: a private space (`spaces.create`, `spaces.get`), files moved in only through `files.cross` and kept with `files.*` (`files.list`, `files.read`, `files.write`), unlock by passkey (`passkeys.auth_begin`, `passkeys.auth_finish`), the seat locked on leaving (`seats.lock`). Encrypting a compartment at rest has no method: use only existing methods; list what's missing in your report.
Uses: `use lockbox 1` (strength score, limits and key expiry); `use banker 1` (`share`, `total`, `entry_ok`); `use gatehouse 1` ("is this use allowed"); `use hamper 1` (every entry in bounds and the path check). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may unlock now" (attempts within the limit, back-off over, this seat's passkey) in Ada as Eve + APL as KRON witnessed
- key derivation cost (memory KiB x passes within the time budget; parameters only, never key bytes) exact in FORTRAN as Trebago
- block map (compartment blocks x used) in APL as KRON
- access records (attempts and opens, refusing on overflow) in COBOL as Sutra
- folder tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): an unlock from this seat and another from a second device at the same moment give BOTH; the test asserts state 3, the attempt reported, and nothing opened. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never log or store key bytes; tests use lengths and counts only. Never touch build_system/keys/.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, sys.read, notify.

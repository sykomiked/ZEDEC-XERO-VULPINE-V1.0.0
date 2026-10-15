# Wave 20 task: Overture (slot 252), First start
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (003 threshold), wave 2 (133 nightjar), wave 3 (004 keystone), wave 4 (012 ferry), wave 11 (137 glyphwright).
Manifest: `slot 252`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/252-overture.svg`, `budget 233`, model `apps/overture_model.{h,c}`, blocks under kernel/src/apps/blocks/overture/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts first start (kernel/src/seats, kernel/src/ident, kernel/src/terms (the agreement), kernel/src/license, kernel/src/i18n, kernel/src/timesvc for zones). Seats and passkeys (`seats.add`, `seats.list`, `seats.set_lock`, `seats.auto`, `seats.set_auto`, `passkeys.register_begin`, `passkeys.register_finish`), language and simple mode (`xlate.languages`, `ui.get`, `ui.set`), network (`net.command`), moving in from an old machine (`devmesh.join`, `devmesh.confirm_sas`, `files.cross`, `spaces.list`), the clock (`sys.info`). Recording a licence acceptance, setting the time zone and setting the keyboard layout have no method: use only existing methods; list what's missing in your report.
Uses: `use threshold 1` (role rights, "skip the login", passphrase limits); `use keystone 1` (setting limits); `use ferry 1` (transfer plan totals and "fits on the target"); `use glyphwright 1` (composed character and valid code point); `use nightjar 1` ("interrupt now" and speech limits). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "this seat may be used" = the licence accepted by this user (never inherited from another seat) in Ada as Eve + APL as KRON witnessed
- (critical) local time from UTC minutes and the zone's offset and daylight rule in FORTRAN as Trebago + APL as KRON witnessed
- language and layout tables (language x layout x script) in APL as KRON
- setup log as fixed records (step, result), the step counter refusing on overflow, in COBOL as Sutra
- account records in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the old machine's text size 18 and the new machine's 14 give BOTH; the test asserts state 3 and that both are shown to the user, neither overwritten. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: The licence is accepted by each user personally; nothing moves from the old machine, and no key, without the owner's approval.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: sys.read, fs.read, fs.write, peers, net, backup, notify.
Planned (no runtime yet): Gleam, Raku, Kotlin.

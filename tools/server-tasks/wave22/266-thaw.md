# Wave 22 task: Thaw (slot 266), Disaster rebuild
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (010 keepsake), wave 4 (009 lockbox, 011 lifeboat, 012 ferry).
Manifest: `slot 266`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/266-thaw.svg`, `budget 233`, model `apps/thaw_model.{h,c}`, blocks under kernel/src/apps/blocks/thaw/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Rebuilds a whole machine after disaster from backups, keys and settings (kernel/src/recover): backups (`backup.list`, `backup.verify`, `backup.versions`, `backup.restore`), checks (`journal.verify`, `ledger.check`, `sys.disk`), guardians (`recovery.status`, `recovery.claim`), spaces and files (`spaces.list`, `files.cross`), switches (`features.get`).
Uses: `use lifeboat 1` ("safe to start normally", next repair step, "restore fits"); `use keepsake 1` (`removed` and `keep_ok`); `use ferry 1` (transfer totals and "fits on the target"); `use lockbox 1` (limits and key expiry). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may restore this item" (`backup.verify` ok AND chain intact AND it fits) in Ada as Eve + APL as KRON witnessed
- (critical) restore byte totals (refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- restore time estimate (bytes / measured rate) in FORTRAN as Trebago
- block map (items x restored) in APL as KRON
- system tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two backups of one folder disagree, giving BOTH; the test (scratch Hoard) asserts state 3 and that both are restored side by side as new items. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: A restore always makes a new item; keys return only with the owner and guardians; tests use a scratch Hoard and books.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: backup, fs.read, fs.write, journal, ledger.read, sys.read, peers, notify.
Planned (no runtime yet): Erlang, JCL.

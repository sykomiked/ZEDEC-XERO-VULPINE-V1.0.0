# Wave 32 task: Planisphere (slot 391), Your own machine
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 19 (229 muster), wave 20 (247 fusee, 252 overture), wave 21 (254 plinth, 262 thicket).
Manifest: `slot 391`, `layer L3`, `version 1.0.0`, `icon icons/l3/391-planisphere.svg`, `budget 233`, model `apps/planisphere_model.{h,c}`, blocks under kernel/src/apps/blocks/planisphere/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Your own computer from disc to desktop in one place, every step signed: install (Plinth), firmware and boot (Fusee), devices (Muster), first start (Overture) and community updates (Thicket). Uses only their daemon methods (`sys.disk`, `sys.info`, `update.check`, `journal.verify`, `journal.query`, `features.get`, `seats.list`, `spaces.propose`, `spaces.vote`); the steps they report as missing stay missing here: list them in your report.
Uses: `use plinth 1` ("may write this disc" and partition sizes); `use fusee 1` ("may boot this image" and the slot header check); `use muster 1` ("trust this USB device" and power per port); `use overture 1` ("this seat may be used" and local time); `use thicket 1` ("may accept" and test score). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `step_ok`: (critical) "do this step" (its part's check TRUE AND every earlier step done AND the owner confirmed; any BOTH keeps the last good state) in Ada as Eve + APL as KRON witnessed, joined with `lpres_conjoin` in C
- `capacity`: (critical) capacity planning (disc, memory and model space needed against what the machine has, bytes) in FORTRAN as Trebago + APL as KRON witnessed
- `inventory`: machine inventory (parts x status) in APL as KRON
- `history`: machine history (signed steps, refusing on overflow) in COBOL as Sutra
- `config`: configuration tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): Fusee passes a boot image and Thicket's auditors disagree on the update inside it: the step is BOTH; the test asserts state 3 and that the machine stays on its last good state. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never touch a disc without explicit confirmation; tests use image files only; never write firmware or keys in tests; never touch build_system/keys/.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, fs.read, fs.write, journal, backup, net, peers, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Crystal inventory`; `pending ALGOL_60 step_ok`; `pending Haskell capacity`.
Now runs (Lingua front-end built): ALGOL 60 (twin Rootwyrm) for proofs of correctness: `logic algol60 <entry> apps/blocks/planisphere/<entry>.alg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Crystal, Haskell.
